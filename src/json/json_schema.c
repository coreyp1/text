/**
 * @file
 *
 * JSON Schema validation (core subset) implementation.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/macros.h>
#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_pointer.h>
#include <ghoti.io/text/json/json_schema.h>

static void json_schema_node_free(json_schema_node * node) {
  if (!node) {
    return;
  }

  // Free properties
  if (node->properties) {
    for (size_t i = 0; i < node->properties_count; i++) {
      free(node->properties[i].key);
      json_schema_node_free(node->properties[i].schema);
    }
    free(node->properties);
  }

  // Free required keys
  if (node->required_keys) {
    for (size_t i = 0; i < node->required_count; i++) {
      free(node->required_keys[i]);
    }
    free(node->required_keys);
  }

  // Free items schema
  json_schema_node_free(node->items_schema);

  // Free the applicator subschemas
  if (node->all_of) {
    for (size_t i = 0; i < node->all_of_count; i++) {
      json_schema_node_free(node->all_of[i]);
    }
    free(node->all_of);
  }
  if (node->any_of) {
    for (size_t i = 0; i < node->any_of_count; i++) {
      json_schema_node_free(node->any_of[i]);
    }
    free(node->any_of);
  }
  if (node->one_of) {
    for (size_t i = 0; i < node->one_of_count; i++) {
      json_schema_node_free(node->one_of[i]);
    }
    free(node->one_of);
  }
  json_schema_node_free(node->not_schema);
  json_schema_node_free(node->if_schema);
  json_schema_node_free(node->then_schema);
  json_schema_node_free(node->else_schema);

  /* ref_target is deliberately not freed here: it is owned by the schema's
   * registry, because a recursive or shared reference would otherwise be
   * freed once per referring node. */

  if (node->prefix_items) {
    for (size_t i = 0; i < node->prefix_items_count; i++) {
      json_schema_node_free(node->prefix_items[i]);
    }
    free(node->prefix_items);
  }
  json_schema_node_free(node->additional_items);
  json_schema_node_free(node->contains_schema);
  json_schema_node_free(node->additional_properties);
  json_schema_node_free(node->property_names);

  if (node->dep_schemas) {
    for (size_t i = 0; i < node->dep_schemas_count; i++) {
      free(node->dep_schemas[i].key);
      json_schema_node_free(node->dep_schemas[i].schema);
    }
    free(node->dep_schemas);
  }

  // Free dependentRequired
  if (node->dep_required) {
    for (size_t i = 0; i < node->dep_required_count; i++) {
      free(node->dep_required[i].key);
      if (node->dep_required[i].required) {
        for (size_t j = 0; j < node->dep_required[i].required_count; j++) {
          free(node->dep_required[i].required[j]);
        }
        free(node->dep_required[i].required);
      }
    }
    free(node->dep_required);
  }

  /* The provider's compiled patterns. `regex_provider` is set on a node only
   * when one of them was compiled, so a schema compiled without a provider
   * frees nothing here and needs no branch anywhere else. */
  if (node->regex_provider) {
    if (node->pattern_regex) {
      node->regex_provider->free_fn(
          node->regex_provider->ctx, node->pattern_regex);
    }
    for (size_t i = 0; i < node->pattern_properties_count; i++) {
      if (node->pattern_properties[i].regex) {
        node->regex_provider->free_fn(
            node->regex_provider->ctx, node->pattern_properties[i].regex);
      }
    }
  }
  if (node->pattern_properties) {
    for (size_t i = 0; i < node->pattern_properties_count; i++) {
      json_schema_node_free(node->pattern_properties[i].schema);
    }
    free(node->pattern_properties);
  }

  // Free enum values (values are in context, just free array)
  free(node->enum_values);

  // Free const value (value is in context, just clear pointer)
  // Note: const_value is freed when context is freed

  free(node);
}

static GTEXT_JSON_Status json_schema_parse_type(json_schema_node * node,
    const GTEXT_JSON_Value * type_value, GTEXT_JSON_Error * err) {
  if (type_value->type == GTEXT_JSON_STRING) {
    // Single type string
    const char * type_str;
    size_t type_len;
    if (gtext_json_get_string(type_value, &type_str, &type_len) !=
        GTEXT_JSON_OK) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
            .message = "Invalid type value in schema"};
      }
      return GTEXT_JSON_E_INVALID;
    }

    if (json_matches(type_str, type_len, "null")) {
      node->type_flags |= JSON_SCHEMA_TYPE_NULL;
    }
    else if (json_matches(type_str, type_len, "boolean")) {
      node->type_flags |= JSON_SCHEMA_TYPE_BOOL;
    }
    else if (json_matches(type_str, type_len, "number")) {
      node->type_flags |= JSON_SCHEMA_TYPE_NUMBER;
    }
    else if (json_matches(type_str, type_len, "integer")) {
      node->type_flags |= JSON_SCHEMA_TYPE_INTEGER;
    }
    else if (json_matches(type_str, type_len, "string")) {
      node->type_flags |= JSON_SCHEMA_TYPE_STRING;
    }
    else if (json_matches(type_str, type_len, "array")) {
      node->type_flags |= JSON_SCHEMA_TYPE_ARRAY;
    }
    else if (json_matches(type_str, type_len, "object")) {
      node->type_flags |= JSON_SCHEMA_TYPE_OBJECT;
    }
    else {
      if (err) {
        *err = (GTEXT_JSON_Error){
            .code = GTEXT_JSON_E_INVALID, .message = "Unknown type in schema"};
      }
      return GTEXT_JSON_E_INVALID;
    }
  }
  else if (type_value->type == GTEXT_JSON_ARRAY) {
    // Array of types
    size_t count = gtext_json_array_size(type_value);
    for (size_t i = 0; i < count; i++) {
      const GTEXT_JSON_Value * type_elem = gtext_json_array_get(type_value, i);
      if (!type_elem || type_elem->type != GTEXT_JSON_STRING) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Invalid type array element in schema"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      GTEXT_JSON_Status status = json_schema_parse_type(node, type_elem, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
  }
  else {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Type must be string or array of strings"};
    }
    return GTEXT_JSON_E_INVALID;
  }

  return GTEXT_JSON_OK;
}

/*
 * Standard JSON Schema keywords this engine does not enforce.
 *
 * Every one of these changes which instances are valid.  Ignoring such a
 * keyword means a schema that looks like it constrains data does not, and the
 * caller has no way to find out - the failure mode this list exists to
 * prevent.  A schema using one is refused at compile time instead.
 */
static const char * const json_schema_unsupported_keywords[] = {
    /* Applicators. */
    "$recursiveRef", "$dynamicRef", "unevaluatedItems",
    "unevaluatedProperties",
    /* Assertions. */
    "format", "contentEncoding", "contentMediaType", "contentSchema", NULL};

/*
 * `pattern` and `patternProperties` are deliberately not in that list.
 * Whether they can be enforced is not a property of this library: it depends
 * on whether the caller supplied a regular-expression engine.  They are
 * handled by name in json_schema_compile_node(), which refuses them through
 * json_schema_reject_keyword() - the same error, so a caller cannot tell the
 * two kinds of refusal apart and does not need to - when no provider is
 * present, and compiles them when one is.
 */

static int json_schema_keyword_in(
    const char * const * list, const char * key, size_t key_len) {
  for (size_t i = 0; list[i]; i++) {
    if (json_matches(key, key_len, list[i])) {
      return 1;
    }
  }
  return 0;
}

/*
 * Report a keyword the engine cannot honor.  The message is a static string,
 * as the error contract requires, so the keyword itself travels in
 * context_snippet, which gtext_json_error_free() already owns and frees.
 */
static GTEXT_JSON_Status json_schema_reject_keyword(
    const char * key, size_t key_len, GTEXT_JSON_Error * err) {
  if (err) {
    *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA_UNSUPPORTED,
        .message = "Schema uses a standard keyword this implementation does "
                   "not enforce"};
    char * name = (char *)malloc(key_len + 1);
    if (name) {
      memcpy(name, key, key_len);
      name[key_len] = '\0';
      err->context_snippet = name;
      err->context_snippet_len = key_len;
      err->caret_offset = 0;
    }
  }
  return GTEXT_JSON_E_SCHEMA_UNSUPPORTED;
}

/*
 * State threaded through compilation.
 *
 * `schema` is here so that a `$ref` can reach the registry and the cloned
 * document; `depth` bounds a schema that nests subschemas without bound, which
 * a `$ref` cycle cannot cause but a deeply literal document can.
 */
typedef struct {
  json_context * ctx;
  const GTEXT_JSON_Schema_Options * opts;
  GTEXT_JSON_Schema * schema;
  int depth;
} json_schema_compile_ctx;

#define JSON_SCHEMA_MAX_COMPILE_DEPTH 256
#define JSON_SCHEMA_MAX_VALIDATE_DEPTH 256

/* Forward declaration: the applicator helpers below compile subschemas. */
static GTEXT_JSON_Status json_schema_compile_node(json_schema_node * node,
    const GTEXT_JSON_Value * schema_doc, json_schema_compile_ctx * cc,
    GTEXT_JSON_Error * err);


/*
 * How much of a provider's compile diagnostic is kept.
 *
 * A fixed buffer rather than a returned pointer, so that ownership is not a
 * question: the provider writes into memory it does not own and does not free,
 * and this function copies what it needs before returning.  The one thing a
 * provider must not do is assume more room than it was given.
 */
#define JSON_SCHEMA_REGEX_MESSAGE_MAX 256

/*
 * Report a `pattern` or `patternProperties` regular expression the provider
 * refused.
 *
 * The provider's own message is what a schema author needs - "nothing to
 * repeat at offset 1" says more than "invalid pattern" ever can - but
 * GTEXT_JSON_Error::message is a static string by contract, so the message
 * travels in context_snippet, the way json_schema_reject_keyword() sends the
 * keyword name.  The offset the provider reported goes in `offset`: the
 * pattern is the input being compiled here, so a byte offset into it is
 * exactly what that field means.
 *
 * `(size_t)-1` is passed through rather than folded to 0.  Some refusals have
 * no position - "this pattern needs an engine whose worst case is
 * exponential" is about the whole of it - and reporting those as "at byte 0"
 * would point at a character that is not the problem.
 */
static GTEXT_JSON_Status json_schema_reject_regex(
    const char * message, size_t offset, GTEXT_JSON_Error * err) {
  if (err) {
    *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
        .message = "Schema pattern is not a valid regular expression",
        .offset = offset};
    size_t len = strlen(message);
    char * copy = (char *)malloc(len + 1);
    if (copy) {
      memcpy(copy, message, len + 1);
      err->context_snippet = copy;
      err->context_snippet_len = len;
      err->caret_offset = 0;
    }
  }
  return GTEXT_JSON_E_INVALID;
}

/*
 * Compile one regular expression through the caller's provider.
 *
 * The caller has already established that a provider exists.  `doc` is the
 * schema value the pattern came from, and its bytes outlive this call: the
 * schema owns the cloned document, and the provider is given a pointer and a
 * length rather than a C string, as the vtable's documentation says.
 */
static GTEXT_JSON_Status json_schema_compile_regex(const GTEXT_JSON_Value * doc,
    json_schema_compile_ctx * cc, void ** out_regex, GTEXT_JSON_Error * err) {
  const char * text = NULL;
  size_t text_len = 0;
  if (gtext_json_typeof(doc) != GTEXT_JSON_STRING
      || gtext_json_get_string(doc, &text, &text_len) != GTEXT_JSON_OK) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "A schema pattern must be a string"};
    }
    return GTEXT_JSON_E_INVALID;
  }

  char message[JSON_SCHEMA_REGEX_MESSAGE_MAX];
  message[0] = '\0';
  size_t offset = (size_t)-1;
  void * regex = NULL;
  const GTEXT_JSON_Regex_Provider * provider = &cc->schema->regex_provider;
  int rc = provider->compile_fn(provider->ctx, text ? text : "", text_len,
      &regex, message, sizeof(message), &offset);
  /* A provider is not trusted to have written a terminator. */
  message[sizeof(message) - 1] = '\0';

  if (rc != 0 || !regex) {
    return json_schema_reject_regex(
        message[0] ? message
                   : "the regular-expression engine refused this pattern",
        offset, err);
  }

  *out_regex = regex;
  return GTEXT_JSON_OK;
}


/*
 * Resolve a `$ref` to a compiled node, compiling the target on first use.
 *
 * Only same-document JSON Pointer fragments are supported: "#" for the root
 * and "#/..." for a pointer into it. An external URI, or a "#name" anchor,
 * is refused rather than quietly ignored - a schema whose reference does not
 * resolve constrains nothing, which is the failure this whole engine is meant
 * not to have.
 *
 * The entry is registered before the target's children compile, so a schema
 * that refers to itself terminates. Targets are owned by the registry, so one
 * reached from several places is compiled once and freed once.
 */
static GTEXT_JSON_Status json_schema_resolve_ref(json_schema_node ** out,
    const char * ref, size_t ref_len, json_schema_compile_ctx * cc,
    GTEXT_JSON_Error * err) {
  if (ref_len == 0 || ref[0] != '#') {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA_UNSUPPORTED,
          .message = "Only same-document $ref (\"#\" or \"#/...\") is "
                     "supported"};
    }
    return GTEXT_JSON_E_SCHEMA_UNSUPPORTED;
  }
  /* "#" alone is the root; otherwise the fragment must be a JSON Pointer,
   * which always starts with '/'. "#name" is an $anchor, which needs anchor
   * collection this engine does not do. */
  const char * ptr = ref + 1;
  size_t ptr_len = ref_len - 1;
  if (ptr_len != 0 && ptr[0] != '/') {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA_UNSUPPORTED,
          .message = "$ref to a named anchor is not supported"};
    }
    return GTEXT_JSON_E_SCHEMA_UNSUPPORTED;
  }

  GTEXT_JSON_Schema * schema = cc->schema;
  for (size_t i = 0; i < schema->refs_count; i++) {
    if (strlen(schema->refs[i].pointer) == ptr_len
        && memcmp(schema->refs[i].pointer, ptr, ptr_len) == 0) {
      *out = schema->refs[i].node;
      return GTEXT_JSON_OK;
    }
  }

  const GTEXT_JSON_Value * target =
      ptr_len == 0 ? schema->doc : gtext_json_pointer_get(schema->doc, ptr, ptr_len);
  if (!target) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
          .message = "$ref does not resolve to anything in this document"};
    }
    return GTEXT_JSON_E_SCHEMA;
  }

  if (schema->refs_count == schema->refs_capacity) {
    size_t cap = schema->refs_capacity ? schema->refs_capacity * 2 : 8;
    if (cap > SIZE_MAX / sizeof(json_schema_ref_entry)) {
      if (err) {
        *err = (GTEXT_JSON_Error){
            .code = GTEXT_JSON_E_OOM, .message = "Too many $ref targets"};
      }
      return GTEXT_JSON_E_OOM;
    }
    json_schema_ref_entry * grown = (json_schema_ref_entry *)realloc(
        schema->refs, cap * sizeof(json_schema_ref_entry));
    if (!grown) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
            .message = "Out of memory growing the $ref registry"};
      }
      return GTEXT_JSON_E_OOM;
    }
    schema->refs = grown;
    schema->refs_capacity = cap;
  }

  json_schema_node * node =
      (json_schema_node *)calloc(1, sizeof(json_schema_node));
  char * key = (char *)malloc(ptr_len + 1);
  if (!node || !key) {
    free(node);
    free(key);
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory allocating a $ref target"};
    }
    return GTEXT_JSON_E_OOM;
  }
  memcpy(key, ptr, ptr_len);
  key[ptr_len] = '\0';

  /* Registered before compiling, so a self-reference finds this entry rather
   * than recursing forever. */
  schema->refs[schema->refs_count].pointer = key;
  schema->refs[schema->refs_count].node = node;
  schema->refs_count++;

  GTEXT_JSON_Status status = json_schema_compile_node(node, target, cc, err);
  if (status != GTEXT_JSON_OK) {
    /* The entry stays in the registry so it is freed with the schema; the
     * compile as a whole is about to fail. */
    return status;
  }
  *out = node;
  return GTEXT_JSON_OK;
}

/* Compile one subschema into a freshly allocated node. */
static GTEXT_JSON_Status json_schema_compile_sub(json_schema_node ** out,
    const GTEXT_JSON_Value * doc, json_schema_compile_ctx * cc,
    GTEXT_JSON_Error * err) {
  json_schema_node * sub =
      (json_schema_node *)calloc(1, sizeof(json_schema_node));
  if (!sub) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory allocating subschema"};
    }
    return GTEXT_JSON_E_OOM;
  }
  GTEXT_JSON_Status status = json_schema_compile_node(sub, doc, cc, err);
  if (status != GTEXT_JSON_OK) {
    json_schema_node_free(sub);
    return status;
  }
  *out = sub;
  return GTEXT_JSON_OK;
}

/* Compile an array of subschemas, as allOf, anyOf and oneOf all take. */
static GTEXT_JSON_Status json_schema_compile_sub_list(
    json_schema_node *** out_list, size_t * out_count, const char * keyword,
    const GTEXT_JSON_Value * value, json_schema_compile_ctx * cc,
    GTEXT_JSON_Error * err) {
  if (value->type != GTEXT_JSON_ARRAY) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Applicator keyword must be an array of schemas"};
    }
    (void)keyword;
    return GTEXT_JSON_E_INVALID;
  }
  size_t n = gtext_json_array_size(value);
  if (n == 0) {
    /* An empty array asserts nothing; JSON Schema requires at least one
     * element, so treat it as a malformed schema rather than a no-op. */
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Applicator keyword must have at least one schema"};
    }
    return GTEXT_JSON_E_INVALID;
  }
  if (n > SIZE_MAX / sizeof(json_schema_node *)) {
    if (err) {
      *err = (GTEXT_JSON_Error){
          .code = GTEXT_JSON_E_OOM, .message = "Applicator list too large"};
    }
    return GTEXT_JSON_E_OOM;
  }
  json_schema_node ** list =
      (json_schema_node **)calloc(n, sizeof(json_schema_node *));
  if (!list) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory allocating applicator list"};
    }
    return GTEXT_JSON_E_OOM;
  }
  for (size_t i = 0; i < n; i++) {
    const GTEXT_JSON_Value * elem = gtext_json_array_get(value, i);
    GTEXT_JSON_Status status =
        json_schema_compile_sub(&list[i], elem, cc, err);
    if (status != GTEXT_JSON_OK) {
      for (size_t j = 0; j < i; j++) {
        json_schema_node_free(list[j]);
      }
      free(list);
      return status;
    }
  }
  *out_list = list;
  *out_count = n;
  return GTEXT_JSON_OK;
}

static GTEXT_JSON_Status json_schema_compile_node(json_schema_node * node,
    const GTEXT_JSON_Value * schema_doc, json_schema_compile_ctx * cc,
    GTEXT_JSON_Error * err) {
  if (cc->depth >= JSON_SCHEMA_MAX_COMPILE_DEPTH) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_DEPTH,
          .message = "Schema nests deeper than the compiler allows"};
    }
    return GTEXT_JSON_E_DEPTH;
  }

  /* A boolean is a schema: true accepts everything, false rejects
   * everything.  "additionalProperties": false is the common case. */
  if (schema_doc->type == GTEXT_JSON_BOOL) {
    bool b = false;
    if (gtext_json_get_bool(schema_doc, &b) != GTEXT_JSON_OK) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
            .message = "Invalid boolean schema"};
      }
      return GTEXT_JSON_E_INVALID;
    }
    node->is_bool_schema = 1;
    node->bool_schema_value = b ? 1 : 0;
    return GTEXT_JSON_OK;
  }

  if (schema_doc->type != GTEXT_JSON_OBJECT) {
    if (err) {
      *err = (GTEXT_JSON_Error){
          .code = GTEXT_JSON_E_INVALID, .message = "Schema must be an object"};
    }
    return GTEXT_JSON_E_INVALID;
  }

  size_t obj_size = gtext_json_object_size(schema_doc);
  for (size_t i = 0; i < obj_size; i++) {
    const char * key;
    size_t key_len;
    gtext_json_object_key(schema_doc, i, &key_len);
    key = gtext_json_object_key(schema_doc, i, NULL);
    const GTEXT_JSON_Value * value = gtext_json_object_value(schema_doc, i);

    if (json_matches(key, key_len, "type")) {
      GTEXT_JSON_Status status = json_schema_parse_type(node, value, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "properties")) {
      if (value->type != GTEXT_JSON_OBJECT) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Properties must be an object"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      size_t prop_count = gtext_json_object_size(value);
      if (prop_count > 0) {
        // Allocate properties array
        if (node->properties_capacity < prop_count) {
          size_t new_capacity = prop_count;
          // Check for integer overflow in multiplication
          if (new_capacity > SIZE_MAX / sizeof(json_schema_property)) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Properties array size overflow"};
            }
            return GTEXT_JSON_E_OOM;
          }
          json_schema_property * new_props = (json_schema_property *)realloc(
              node->properties, new_capacity * sizeof(json_schema_property));
          if (!new_props) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating properties"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->properties = new_props;
          node->properties_capacity = new_capacity;
        }

        // Compile each property schema
        for (size_t j = 0; j < prop_count; j++) {
          const char * prop_key;
          size_t prop_key_len;
          gtext_json_object_key(value, j, &prop_key_len);
          prop_key = gtext_json_object_key(value, j, NULL);
          const GTEXT_JSON_Value * prop_schema =
              gtext_json_object_value(value, j);

          // Allocate property entry
          json_schema_property * prop =
              &node->properties[node->properties_count];
          prop->key_len = prop_key_len;
          // Check for integer overflow in key_len + 1
          if (prop_key_len > SIZE_MAX - 1) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Property key length overflow"};
            }
            return GTEXT_JSON_E_OOM;
          }
          prop->key = (char *)malloc(prop_key_len + 1);
          if (!prop->key) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating property key"};
            }
            return GTEXT_JSON_E_OOM;
          }
          memcpy(prop->key, prop_key, prop_key_len);
          prop->key[prop_key_len] = '\0';

          // Compile property schema recursively
          prop->schema =
              (json_schema_node *)calloc(1, sizeof(json_schema_node));
          if (!prop->schema) {
            free(prop->key);
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating property schema"};
            }
            return GTEXT_JSON_E_OOM;
          }

          GTEXT_JSON_Status status =
              json_schema_compile_node(prop->schema, prop_schema, cc, err);
          if (status != GTEXT_JSON_OK) {
            free(prop->key);
            free(prop->schema);
            return status;
          }

          node->properties_count++;
        }
      }
    }
    else if (json_matches(key, key_len, "required")) {
      if (value->type != GTEXT_JSON_ARRAY) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Required must be an array"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      size_t req_count = gtext_json_array_size(value);
      if (req_count > 0) {
        if (node->required_capacity < req_count) {
          size_t new_capacity = req_count;
          // Check for integer overflow in multiplication
          if (new_capacity > SIZE_MAX / sizeof(char *)) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Required keys array size overflow"};
            }
            return GTEXT_JSON_E_OOM;
          }
          char ** new_keys = (char **)realloc(
              node->required_keys, new_capacity * sizeof(char *));
          if (!new_keys) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating required keys"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->required_keys = new_keys;
          node->required_capacity = new_capacity;
        }

        for (size_t j = 0; j < req_count; j++) {
          const GTEXT_JSON_Value * req_key = gtext_json_array_get(value, j);
          if (!req_key || req_key->type != GTEXT_JSON_STRING) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                  .message = "Required array must contain strings"};
            }
            return GTEXT_JSON_E_INVALID;
          }

          const char * req_key_str;
          size_t req_key_len;
          if (gtext_json_get_string(req_key, &req_key_str, &req_key_len) !=
              GTEXT_JSON_OK) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                  .message = "Invalid required key"};
            }
            return GTEXT_JSON_E_INVALID;
          }

          // Check for integer overflow in req_key_len + 1
          if (req_key_len > SIZE_MAX - 1) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Required key length overflow"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->required_keys[node->required_count] =
              (char *)malloc(req_key_len + 1);
          if (!node->required_keys[node->required_count]) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating required key"};
            }
            return GTEXT_JSON_E_OOM;
          }
          memcpy(node->required_keys[node->required_count], req_key_str,
              req_key_len);
          node->required_keys[node->required_count][req_key_len] = '\0';
          node->required_count++;
        }
      }
    }
    else if (json_matches(key, key_len, "items")) {
      /* draft-07 spells the positional form `items: [ ... ]`, which 2020-12
       * renamed to prefixItems.  Both compile to prefix_items, so a schema
       * written either way validates the same. */
      if (value->type == GTEXT_JSON_ARRAY) {
        GTEXT_JSON_Status status =
            json_schema_compile_sub_list(&node->prefix_items,
                &node->prefix_items_count, "items", value, cc, err);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
        continue;
      }
      // Compile items schema recursively
      node->items_schema =
          (json_schema_node *)calloc(1, sizeof(json_schema_node));
      if (!node->items_schema) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
              .message = "Out of memory allocating items schema"};
        }
        return GTEXT_JSON_E_OOM;
      }

      GTEXT_JSON_Status status =
          json_schema_compile_node(node->items_schema, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        free(node->items_schema);
        node->items_schema = NULL;
        return status;
      }
    }
    else if (json_matches(key, key_len, "enum")) {
      if (value->type != GTEXT_JSON_ARRAY) {
        if (err) {
          *err = (GTEXT_JSON_Error){
              .code = GTEXT_JSON_E_INVALID, .message = "Enum must be an array"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      size_t enum_count = gtext_json_array_size(value);
      if (enum_count > 0) {
        if (node->enum_capacity < enum_count) {
          size_t new_capacity = enum_count;
          // Check for integer overflow in multiplication
          if (new_capacity > SIZE_MAX / sizeof(GTEXT_JSON_Value *)) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Enum values array size overflow"};
            }
            return GTEXT_JSON_E_OOM;
          }
          GTEXT_JSON_Value ** new_enum = (GTEXT_JSON_Value **)realloc(
              node->enum_values, new_capacity * sizeof(GTEXT_JSON_Value *));
          if (!new_enum) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating enum values"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->enum_values = new_enum;
          node->enum_capacity = new_capacity;
        }

        // Clone enum values into context
        // Initialize enum_count to 0 to ensure clean state
        node->enum_count = 0;
        for (size_t j = 0; j < enum_count; j++) {
          const GTEXT_JSON_Value * enum_val = gtext_json_array_get(value, j);
          if (!enum_val) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                  .message = "Invalid enum value"};
            }
            // Cleanup: enum_values already allocated, but enum_count is correct
            // Context will free any cloned values on error
            return GTEXT_JSON_E_INVALID;
          }

          // Clone value into context
          GTEXT_JSON_Value * cloned = json_value_clone(enum_val, cc->ctx);
          if (!cloned) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory cloning enum value"};
            }
            // Cleanup: enum_count is correct, context will free cloned values
            return GTEXT_JSON_E_OOM;
          }

          node->enum_values[node->enum_count++] = cloned;
        }
      }
    }
    else if (json_matches(key, key_len, "const")) {
      // Clone const value into context
      node->const_value = json_value_clone(value, cc->ctx);
      if (!node->const_value) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
              .message = "Out of memory cloning const value"};
        }
        return GTEXT_JSON_E_OOM;
      }
    }
    else if (json_matches(key, key_len, "minimum")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Minimum must be a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      double min_val;
      if (gtext_json_get_double(value, &min_val) != GTEXT_JSON_OK) {
        if (err) {
          *err = (GTEXT_JSON_Error){
              .code = GTEXT_JSON_E_INVALID, .message = "Invalid minimum value"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      node->has_minimum = 1;
      node->minimum = min_val;
    }
    else if (json_matches(key, key_len, "maximum")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Maximum must be a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      double max_val;
      if (gtext_json_get_double(value, &max_val) != GTEXT_JSON_OK) {
        if (err) {
          *err = (GTEXT_JSON_Error){
              .code = GTEXT_JSON_E_INVALID, .message = "Invalid maximum value"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      node->has_maximum = 1;
      node->maximum = max_val;
    }
    else if (json_matches(key, key_len, "minLength")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "MinLength must be a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      double min_len_val;
      if (gtext_json_get_double(value, &min_len_val) != GTEXT_JSON_OK ||
          min_len_val < 0) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Invalid minLength value"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      // Check for overflow when casting to size_t
      if (min_len_val > (double)SIZE_MAX) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "minLength value exceeds maximum size_t"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      node->has_min_length = 1;
      node->min_length = (size_t)min_len_val;
    }
    else if (json_matches(key, key_len, "maxLength")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "MaxLength must be a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      double max_len_val;
      if (gtext_json_get_double(value, &max_len_val) != GTEXT_JSON_OK ||
          max_len_val < 0) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Invalid maxLength value"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      // Check for overflow when casting to size_t
      if (max_len_val > (double)SIZE_MAX) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "maxLength value exceeds maximum size_t"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      node->has_max_length = 1;
      node->max_length = (size_t)max_len_val;
    }
    else if (json_matches(key, key_len, "minItems")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "MinItems must be a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      double min_items_val;
      if (gtext_json_get_double(value, &min_items_val) != GTEXT_JSON_OK ||
          min_items_val < 0) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Invalid minItems value"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      // Check for overflow when casting to size_t
      if (min_items_val > (double)SIZE_MAX) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "minItems value exceeds maximum size_t"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      node->has_min_items = 1;
      node->min_items = (size_t)min_items_val;
    }
    else if (json_matches(key, key_len, "maxItems")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "MaxItems must be a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      double max_items_val;
      if (gtext_json_get_double(value, &max_items_val) != GTEXT_JSON_OK ||
          max_items_val < 0) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Invalid maxItems value"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      // Check for overflow when casting to size_t
      if (max_items_val > (double)SIZE_MAX) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "maxItems value exceeds maximum size_t"};
        }
        return GTEXT_JSON_E_INVALID;
      }

      node->has_max_items = 1;
      node->max_items = (size_t)max_items_val;
    }
    // --- $ref ---------------------------------------------------------
    else if (json_matches(key, key_len, "$ref")) {
      if (value->type != GTEXT_JSON_STRING) {
        if (err) {
          *err = (GTEXT_JSON_Error){
              .code = GTEXT_JSON_E_INVALID, .message = "$ref must be a string"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      const char * rs = NULL;
      size_t rl = 0;
      if (gtext_json_get_string(value, &rs, &rl) != GTEXT_JSON_OK) {
        if (err) {
          *err = (GTEXT_JSON_Error){
              .code = GTEXT_JSON_E_INVALID, .message = "Invalid $ref value"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      cc->depth++;
      GTEXT_JSON_Status status =
          json_schema_resolve_ref(&node->ref_target, rs, rl, cc, err);
      cc->depth--;
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    // --- positional array schemas -------------------------------------
    else if (json_matches(key, key_len, "prefixItems")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub_list(&node->prefix_items,
              &node->prefix_items_count, "prefixItems", value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "additionalItems")) {
      GTEXT_JSON_Status status = json_schema_compile_sub(
          &node->additional_items, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    // --- contains -----------------------------------------------------
    else if (json_matches(key, key_len, "contains")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->contains_schema, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "minContains")
        || json_matches(key, key_len, "maxContains")) {
      double d = 0.0;
      if (value->type != GTEXT_JSON_NUMBER
          || gtext_json_get_double(value, &d) != GTEXT_JSON_OK || d < 0
          || d > (double)SIZE_MAX) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "minContains/maxContains must be a non-negative "
                         "integer"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      if (json_matches(key, key_len, "minContains")) {
        node->has_min_contains = 1;
        node->min_contains = (size_t)d;
      }
      else {
        node->has_max_contains = 1;
        node->max_contains = (size_t)d;
      }
    }
    // --- the two regular-expression keywords ---------------------------
    // Both are enforced when the caller supplied an engine and refused when
    // they did not.  The refusal is the same one any unenforceable keyword
    // gets, and for the same reason: a `pattern` that is read and ignored
    // makes a schema that looks like it constrains its data not do so, with
    // nothing to tell the caller.
    else if (json_matches(key, key_len, "pattern")) {
      if (!cc->schema->has_regex_provider) {
        if (!cc->opts->allow_unsupported_keywords) {
          return json_schema_reject_keyword(key, key_len, err);
        }
      }
      else {
        GTEXT_JSON_Status status =
            json_schema_compile_regex(value, cc, &node->pattern_regex, err);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
        node->regex_provider = &cc->schema->regex_provider;
      }
    }
    else if (json_matches(key, key_len, "patternProperties")) {
      if (!cc->schema->has_regex_provider) {
        if (!cc->opts->allow_unsupported_keywords) {
          return json_schema_reject_keyword(key, key_len, err);
        }
      }
      else {
        if (value->type != GTEXT_JSON_OBJECT) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                .message = "patternProperties must be an object"};
          }
          return GTEXT_JSON_E_INVALID;
        }
        size_t n = gtext_json_object_size(value);
        if (n > 0) {
          if (n > SIZE_MAX / sizeof(json_schema_pattern_property)) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "patternProperties list too large"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->pattern_properties = (json_schema_pattern_property *)calloc(
              n, sizeof(json_schema_pattern_property));
          if (!node->pattern_properties) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating patternProperties"};
            }
            return GTEXT_JSON_E_OOM;
          }
          /* The provider is recorded before the first pattern is compiled,
           * so that a failure part way through still frees the handles the
           * earlier iterations produced. */
          node->regex_provider = &cc->schema->regex_provider;
          for (size_t j = 0; j < n; j++) {
            size_t pat_len = 0;
            const char * pat = gtext_json_object_key(value, j, &pat_len);
            json_schema_pattern_property * entry =
                &node->pattern_properties[j];
            /* Counted before anything in the entry can fail, so that
             * json_schema_node_free() sees an entry it must clean up. */
            node->pattern_properties_count = j + 1;

            char message[JSON_SCHEMA_REGEX_MESSAGE_MAX];
            message[0] = '\0';
            size_t offset = (size_t)-1;
            const GTEXT_JSON_Regex_Provider * provider =
                &cc->schema->regex_provider;
            int rc = provider->compile_fn(provider->ctx, pat ? pat : "",
                pat_len, &entry->regex, message, sizeof(message), &offset);
            message[sizeof(message) - 1] = '\0';
            if (rc != 0 || !entry->regex) {
              entry->regex = NULL;
              return json_schema_reject_regex(
                  message[0] ? message
                             : "the regular-expression engine refused this "
                               "pattern",
                  offset, err);
            }

            const GTEXT_JSON_Value * sub = gtext_json_object_value(value, j);
            GTEXT_JSON_Status status =
                json_schema_compile_sub(&entry->schema, sub, cc, err);
            if (status != GTEXT_JSON_OK) {
              return status;
            }
          }
        }
      }
    }
    // --- object applicators -------------------------------------------
    else if (json_matches(key, key_len, "additionalProperties")) {
      GTEXT_JSON_Status status = json_schema_compile_sub(
          &node->additional_properties, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "propertyNames")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->property_names, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "dependencies")) {
      /* draft-07's `dependencies` is the union of what 2020-12 split into
       * dependentRequired and dependentSchemas: each value is either an array
       * of property names or a schema.  Rather than a third structure, each
       * entry is compiled into whichever of the two it means. */
      if (value->type != GTEXT_JSON_OBJECT) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "dependencies must be an object"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      size_t n = gtext_json_object_size(value);
      for (size_t i = 0; i < n; i++) {
        size_t dk_len = 0;
        const char * dk = gtext_json_object_key(value, i, &dk_len);
        const GTEXT_JSON_Value * dv = gtext_json_object_value(value, i);
        if (!dv) {
          continue;
        }
        if (dv->type == GTEXT_JSON_ARRAY) {
          size_t rn = gtext_json_array_size(dv);
          json_schema_dep_required * grown =
              (json_schema_dep_required *)realloc(node->dep_required,
                  (node->dep_required_count + 1)
                      * sizeof(json_schema_dep_required));
          if (!grown) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating dependencies"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->dep_required = grown;
          json_schema_dep_required * entry =
              &node->dep_required[node->dep_required_count];
          memset(entry, 0, sizeof(*entry));
          entry->key = (char *)malloc(dk_len + 1);
          if (!entry->key) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating dependencies key"};
            }
            return GTEXT_JSON_E_OOM;
          }
          memcpy(entry->key, dk, dk_len);
          entry->key[dk_len] = '\0';
          node->dep_required_count++;
          if (rn > 0) {
            entry->required = (char **)calloc(rn, sizeof(char *));
            if (!entry->required) {
              if (err) {
                *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                    .message = "Out of memory allocating dependencies list"};
              }
              return GTEXT_JSON_E_OOM;
            }
          }
          for (size_t j = 0; j < rn; j++) {
            const GTEXT_JSON_Value * name = gtext_json_array_get(dv, j);
            const char * ns = NULL;
            size_t nl = 0;
            if (!name || name->type != GTEXT_JSON_STRING
                || gtext_json_get_string(name, &ns, &nl) != GTEXT_JSON_OK) {
              if (err) {
                *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                    .message = "dependencies names must be strings"};
              }
              return GTEXT_JSON_E_INVALID;
            }
            entry->required[j] = (char *)malloc(nl + 1);
            if (!entry->required[j]) {
              if (err) {
                *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                    .message = "Out of memory allocating dependencies name"};
              }
              return GTEXT_JSON_E_OOM;
            }
            memcpy(entry->required[j], ns ? ns : "", nl);
            entry->required[j][nl] = '\0';
            entry->required_count = j + 1;
          }
        }
        else {
          json_schema_dep_schema * grown =
              (json_schema_dep_schema *)realloc(node->dep_schemas,
                  (node->dep_schemas_count + 1)
                      * sizeof(json_schema_dep_schema));
          if (!grown) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating dependencies"};
            }
            return GTEXT_JSON_E_OOM;
          }
          node->dep_schemas = grown;
          json_schema_dep_schema * entry =
              &node->dep_schemas[node->dep_schemas_count];
          memset(entry, 0, sizeof(*entry));
          entry->key = (char *)malloc(dk_len + 1);
          if (!entry->key) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating dependencies key"};
            }
            return GTEXT_JSON_E_OOM;
          }
          memcpy(entry->key, dk, dk_len);
          entry->key[dk_len] = '\0';
          node->dep_schemas_count++;
          GTEXT_JSON_Status status =
              json_schema_compile_sub(&entry->schema, dv, cc, err);
          if (status != GTEXT_JSON_OK) {
            return status;
          }
        }
      }
    }
    else if (json_matches(key, key_len, "dependentSchemas")) {
      if (value->type != GTEXT_JSON_OBJECT) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "dependentSchemas must be an object"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      size_t n = gtext_json_object_size(value);
      if (n > 0) {
        node->dep_schemas = (json_schema_dep_schema *)calloc(
            n, sizeof(json_schema_dep_schema));
        if (!node->dep_schemas) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                .message = "Out of memory allocating dependentSchemas"};
          }
          return GTEXT_JSON_E_OOM;
        }
      }
      for (size_t i = 0; i < n; i++) {
        size_t dk_len = 0;
        const char * dk = gtext_json_object_key(value, i, &dk_len);
        const GTEXT_JSON_Value * sub = gtext_json_object_value(value, i);
        json_schema_dep_schema * entry = &node->dep_schemas[i];
        entry->key = (char *)malloc(dk_len + 1);
        if (!entry->key) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                .message = "Out of memory allocating dependentSchemas key"};
          }
          return GTEXT_JSON_E_OOM;
        }
        memcpy(entry->key, dk, dk_len);
        entry->key[dk_len] = '\0';
        node->dep_schemas_count = i + 1;
        GTEXT_JSON_Status status =
            json_schema_compile_sub(&entry->schema, sub, cc, err);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
    }
    // --- numeric assertions -------------------------------------------
    else if (json_matches(key, key_len, "exclusiveMinimum")
        || json_matches(key, key_len, "exclusiveMaximum")
        || json_matches(key, key_len, "multipleOf")) {
      if (value->type != GTEXT_JSON_NUMBER) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Numeric keyword requires a number"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      double d = 0.0;
      if (gtext_json_get_double(value, &d) != GTEXT_JSON_OK) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "Numeric keyword value is not representable"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      if (json_matches(key, key_len, "exclusiveMinimum")) {
        node->has_exclusive_minimum = 1;
        node->exclusive_minimum = d;
      }
      else if (json_matches(key, key_len, "exclusiveMaximum")) {
        node->has_exclusive_maximum = 1;
        node->exclusive_maximum = d;
      }
      else {
        /* multipleOf must be strictly greater than zero; zero would make
         * every instance fail a division that has no meaning. */
        if (!(d > 0.0)) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                .message = "multipleOf must be greater than zero"};
          }
          return GTEXT_JSON_E_INVALID;
        }
        node->has_multiple_of = 1;
        node->multiple_of = d;
      }
    }
    // --- object and array size assertions -----------------------------
    else if (json_matches(key, key_len, "minProperties")
        || json_matches(key, key_len, "maxProperties")) {
      double d = 0.0;
      if (value->type != GTEXT_JSON_NUMBER
          || gtext_json_get_double(value, &d) != GTEXT_JSON_OK || d < 0
          || d > (double)SIZE_MAX) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "minProperties/maxProperties must be a "
                         "non-negative integer"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      if (json_matches(key, key_len, "minProperties")) {
        node->has_min_properties = 1;
        node->min_properties = (size_t)d;
      }
      else {
        node->has_max_properties = 1;
        node->max_properties = (size_t)d;
      }
    }
    else if (json_matches(key, key_len, "uniqueItems")) {
      if (value->type != GTEXT_JSON_BOOL) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "uniqueItems must be a boolean"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      bool b = false;
      if (gtext_json_get_bool(value, &b) != GTEXT_JSON_OK) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "uniqueItems must be a boolean"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      node->has_unique_items = 1;
      node->unique_items = b ? 1 : 0;
    }
    // --- boolean applicators ------------------------------------------
    else if (json_matches(key, key_len, "allOf")) {
      GTEXT_JSON_Status status = json_schema_compile_sub_list(
          &node->all_of, &node->all_of_count, "allOf", value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "anyOf")) {
      GTEXT_JSON_Status status = json_schema_compile_sub_list(
          &node->any_of, &node->any_of_count, "anyOf", value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "oneOf")) {
      GTEXT_JSON_Status status = json_schema_compile_sub_list(
          &node->one_of, &node->one_of_count, "oneOf", value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "not")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->not_schema, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "if")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->if_schema, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "then")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->then_schema, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "else")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->else_schema, value, cc, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    // --- dependentRequired --------------------------------------------
    else if (json_matches(key, key_len, "dependentRequired")) {
      if (value->type != GTEXT_JSON_OBJECT) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
              .message = "dependentRequired must be an object"};
        }
        return GTEXT_JSON_E_INVALID;
      }
      size_t n = gtext_json_object_size(value);
      if (n > 0) {
        node->dep_required = (json_schema_dep_required *)calloc(
            n, sizeof(json_schema_dep_required));
        if (!node->dep_required) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                .message = "Out of memory allocating dependentRequired"};
          }
          return GTEXT_JSON_E_OOM;
        }
      }
      for (size_t i = 0; i < n; i++) {
        size_t dk_len = 0;
        const char * dk = gtext_json_object_key(value, i, &dk_len);
        const GTEXT_JSON_Value * list = gtext_json_object_value(value, i);
        if (!list || list->type != GTEXT_JSON_ARRAY) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                .message = "dependentRequired values must be arrays of names"};
          }
          return GTEXT_JSON_E_INVALID;
        }
        json_schema_dep_required * entry = &node->dep_required[i];
        entry->key = (char *)malloc(dk_len + 1);
        if (!entry->key) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                .message = "Out of memory allocating dependentRequired key"};
          }
          return GTEXT_JSON_E_OOM;
        }
        memcpy(entry->key, dk, dk_len);
        entry->key[dk_len] = '\0';
        node->dep_required_count = i + 1;

        size_t rn = gtext_json_array_size(list);
        if (rn > 0) {
          entry->required = (char **)calloc(rn, sizeof(char *));
          if (!entry->required) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating dependentRequired list"};
            }
            return GTEXT_JSON_E_OOM;
          }
        }
        for (size_t j = 0; j < rn; j++) {
          const GTEXT_JSON_Value * name = gtext_json_array_get(list, j);
          if (!name || name->type != GTEXT_JSON_STRING) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                  .message = "dependentRequired names must be strings"};
            }
            return GTEXT_JSON_E_INVALID;
          }
          size_t nl = 0;
          const char * ns = NULL;
          if (gtext_json_get_string(name, &ns, &nl) != GTEXT_JSON_OK) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
                  .message = "dependentRequired names must be strings"};
            }
            return GTEXT_JSON_E_INVALID;
          }
          entry->required[j] = (char *)malloc(nl + 1);
          if (!entry->required[j]) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                  .message = "Out of memory allocating dependentRequired name"};
            }
            return GTEXT_JSON_E_OOM;
          }
          memcpy(entry->required[j], ns ? ns : "", nl);
          entry->required[j][nl] = '\0';
          entry->required_count = j + 1;
        }
      }
    }
    else if (!cc->opts->allow_unsupported_keywords &&
             json_schema_keyword_in(
                 json_schema_unsupported_keywords, key, key_len)) {
      return json_schema_reject_keyword(key, key_len, err);
    }
    // Everything else is ignored, because none of it can change which
    // instances are valid.  That covers the annotation keywords (title,
    // description, default, examples, $comment, readOnly, writeOnly,
    // deprecated); the core plumbing that is inert while $ref is
    // unsupported ($schema selects a dialect when only one is implemented,
    // $defs and definitions are containers nothing can reach, $id and
    // $anchor name a base URI nothing resolves against, $vocabulary); and
    // vendor extensions and keywords from newer drafts, which JSON Schema
    // requires an implementation to ignore.
  }

  return GTEXT_JSON_OK;
}


/*
 * Run one compiled pattern over one string.
 *
 * Three outcomes, not two.  A provider that could not finish - a step budget
 * spent on a pattern whose worst case is exponential, an allocation refused -
 * has not said the instance is invalid, and recording that as "no match" would
 * turn a denial-of-service defence into a wrong answer.  It becomes
 * GTEXT_JSON_E_LIMIT, which is neither OK nor E_SCHEMA, and the caller can
 * tell the two apart.
 */
static GTEXT_JSON_Status json_schema_regex_search(
    const json_schema_node * node, void * regex, const char * subject,
    size_t subject_len, int * out_matched, GTEXT_JSON_Error * err) {
  const GTEXT_JSON_Regex_Provider * provider = node->regex_provider;
  int rc = provider->search_fn(
      provider->ctx, regex, subject ? subject : "", subject_len);
  if (rc < 0) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_LIMIT,
          .message = "The regular-expression engine could not finish this "
                     "search; the instance is neither valid nor invalid"};
    }
    return GTEXT_JSON_E_LIMIT;
  }
  *out_matched = rc > 0;
  return GTEXT_JSON_OK;
}

static GTEXT_JSON_Status json_schema_validate_depth(
    const json_schema_node * node, const GTEXT_JSON_Value * instance,
    int depth, GTEXT_JSON_Error * err);

/*
 * Kept so the existing call sites read the same.  Every recursive call goes
 * through the depth-carrying form: a schema like {"$ref":"#"} consumes no
 * instance as it recurses, so instance depth is not a bound.
 */
static GTEXT_JSON_Status json_schema_validate_node(
    const json_schema_node * node, const GTEXT_JSON_Value * instance,
    GTEXT_JSON_Error * err) {
  return json_schema_validate_depth(node, instance, 0, err);
}

static GTEXT_JSON_Status json_schema_validate_depth(
    const json_schema_node * node, const GTEXT_JSON_Value * instance,
    int depth, GTEXT_JSON_Error * err) {
  if (depth >= JSON_SCHEMA_MAX_VALIDATE_DEPTH) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_DEPTH,
          .message = "Schema validation nested too deeply"};
    }
    return GTEXT_JSON_E_DEPTH;
  }

  if (!node || !instance) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Invalid arguments to schema validation"};
    }
    return GTEXT_JSON_E_INVALID;
  }

  // A boolean schema settles it outright.
  if (node->is_bool_schema) {
    if (node->bool_schema_value) {
      return GTEXT_JSON_OK;
    }
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
          .message = "Schema is false, so nothing is valid against it"};
    }
    return GTEXT_JSON_E_SCHEMA;
  }

  // $ref applies the referenced schema.  In 2020-12 a $ref sits alongside
  // other keywords and all of them apply, which is what happens here: the
  // reference is checked and then the rest of this node continues.
  if (node->ref_target) {
    GTEXT_JSON_Status status = json_schema_validate_depth(
        node->ref_target, instance, depth + 1, err);
    if (status != GTEXT_JSON_OK) {
      return status;
    }
  }

  // Applicators run before the local assertions.  They can reject an instance
  // the local keywords would accept, and putting them first means a schema
  // that is only applicators still says something.
  //
  // Errors from inside a subschema are deliberately not propagated verbatim:
  // "anyOf failed" is the truth, while the last branch's complaint would name
  // a constraint the instance was never required to satisfy.
  if (node->all_of_count > 0) {
    for (size_t i = 0; i < node->all_of_count; i++) {
      GTEXT_JSON_Error sub;
      memset(&sub, 0, sizeof(sub));
      if (json_schema_validate_depth(node->all_of[i], instance, depth + 1, &sub)
          != GTEXT_JSON_OK) {
        gtext_json_error_free(&sub);
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Value does not match every schema in allOf"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
      gtext_json_error_free(&sub);
    }
  }

  if (node->any_of_count > 0) {
    int matched = 0;
    for (size_t i = 0; i < node->any_of_count && !matched; i++) {
      GTEXT_JSON_Error sub;
      memset(&sub, 0, sizeof(sub));
      if (json_schema_validate_depth(node->any_of[i], instance, depth + 1, &sub)
          == GTEXT_JSON_OK) {
        matched = 1;
      }
      gtext_json_error_free(&sub);
    }
    if (!matched) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "Value does not match any schema in anyOf"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
  }

  if (node->one_of_count > 0) {
    size_t matches = 0;
    for (size_t i = 0; i < node->one_of_count; i++) {
      GTEXT_JSON_Error sub;
      memset(&sub, 0, sizeof(sub));
      if (json_schema_validate_depth(node->one_of[i], instance, depth + 1, &sub)
          == GTEXT_JSON_OK) {
        matches++;
      }
      gtext_json_error_free(&sub);
      /* No early exit on the second match: oneOf is "exactly one", and
       * stopping at two would still be the same verdict but a different
       * count if this is ever reported. */
    }
    if (matches != 1) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = matches == 0
                ? "Value does not match any schema in oneOf"
                : "Value matches more than one schema in oneOf"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
  }

  if (node->not_schema) {
    GTEXT_JSON_Error sub;
    memset(&sub, 0, sizeof(sub));
    GTEXT_JSON_Status inner =
        json_schema_validate_depth(node->not_schema, instance, depth + 1, &sub);
    gtext_json_error_free(&sub);
    if (inner == GTEXT_JSON_OK) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "Value matches a schema it must not match"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
  }

  // if / then / else.  `if` asserts nothing on its own - it only selects.  An
  // absent branch means "no constraint", not "fail".
  if (node->if_schema) {
    GTEXT_JSON_Error sub;
    memset(&sub, 0, sizeof(sub));
    int cond = json_schema_validate_depth(node->if_schema, instance, depth + 1, &sub)
        == GTEXT_JSON_OK;
    gtext_json_error_free(&sub);
    const json_schema_node * branch =
        cond ? node->then_schema : node->else_schema;
    if (branch) {
      GTEXT_JSON_Error berr;
      memset(&berr, 0, sizeof(berr));
      if (json_schema_validate_depth(branch, instance, depth + 1, &berr)
          != GTEXT_JSON_OK) {
        gtext_json_error_free(&berr);
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = cond ? "Value does not match the then schema"
                              : "Value does not match the else schema"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
      gtext_json_error_free(&berr);
    }
  }

  // Check const first (most restrictive)
  if (node->const_value) {
    if (!json_value_equal(instance, node->const_value)) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "Value does not match const"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
    // If const matches, all other checks pass
    return GTEXT_JSON_OK;
  }

  // Check enum
  if (node->enum_count > 0) {
    int found = 0;
    for (size_t i = 0; i < node->enum_count; i++) {
      if (json_value_equal(instance, node->enum_values[i])) {
        found = 1;
        break;
      }
    }
    if (!found) {
      if (err) {
        *err = (GTEXT_JSON_Error){
            .code = GTEXT_JSON_E_SCHEMA, .message = "Value is not in enum"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
  }

  // Check type
  if (node->type_flags != 0) {
    unsigned int instance_flag = 0;
    switch (instance->type) {
    case GTEXT_JSON_NULL:
      instance_flag = JSON_SCHEMA_TYPE_NULL;
      break;
    case GTEXT_JSON_BOOL:
      instance_flag = JSON_SCHEMA_TYPE_BOOL;
      break;
    case GTEXT_JSON_NUMBER: {
      instance_flag = JSON_SCHEMA_TYPE_NUMBER;
      /* "integer" is a constraint on the value, not a separate JSON type, so
       * a whole number satisfies both "number" and "integer".  A schema
       * saying {"type":"integer"} therefore matches 5 and 5.0 but not 5.5,
       * which is what JSON Schema requires. */
      double dv = 0.0;
      if (gtext_json_get_double(instance, &dv) == GTEXT_JSON_OK
          && dv == (double)(long long)dv) {
        instance_flag |= JSON_SCHEMA_TYPE_INTEGER;
      }
      break;
    }
    case GTEXT_JSON_STRING:
      instance_flag = JSON_SCHEMA_TYPE_STRING;
      break;
    case GTEXT_JSON_ARRAY:
      instance_flag = JSON_SCHEMA_TYPE_ARRAY;
      break;
    case GTEXT_JSON_OBJECT:
      instance_flag = JSON_SCHEMA_TYPE_OBJECT;
      break;
    }

    if ((node->type_flags & instance_flag) == 0) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "Value type does not match schema type"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
  }

  // Type-specific validations
  switch (instance->type) {
  case GTEXT_JSON_NUMBER: {
    // Check numeric constraints
    double num_val;
    if (gtext_json_get_double(instance, &num_val) == GTEXT_JSON_OK) {
      if (node->has_minimum && num_val < node->minimum) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Number is less than minimum"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
      if (node->has_maximum && num_val > node->maximum) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Number is greater than maximum"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
    }
    if (node->has_exclusive_minimum || node->has_exclusive_maximum
        || node->has_multiple_of) {
      double v = 0.0;
      if (gtext_json_get_double(instance, &v) == GTEXT_JSON_OK) {
        if (node->has_exclusive_minimum && !(v > node->exclusive_minimum)) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
                .message = "Number is not greater than exclusiveMinimum"};
          }
          return GTEXT_JSON_E_SCHEMA;
        }
        if (node->has_exclusive_maximum && !(v < node->exclusive_maximum)) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
                .message = "Number is not less than exclusiveMaximum"};
          }
          return GTEXT_JSON_E_SCHEMA;
        }
        if (node->has_multiple_of) {
          /* fmod is exact for values that divide evenly in binary, and the
           * spec's own examples (0.0001 and the like) are not representable,
           * so a tolerance would trade one class of wrong answer for
           * another.  An exact remainder is the behavior other validators
           * have, and is what a caller can reason about. */
          double r = fmod(v, node->multiple_of);
          if (r != 0.0) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
                  .message = "Number is not a multiple of multipleOf"};
            }
            return GTEXT_JSON_E_SCHEMA;
          }
        }
      }
    }
    break;
  }

  case GTEXT_JSON_STRING: {
    // Check string length constraints
    size_t str_len = instance->as.string.len;
    if (node->has_min_length && str_len < node->min_length) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "String is shorter than minLength"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
    if (node->has_max_length && str_len > node->max_length) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "String is longer than maxLength"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }

    /* `pattern` searches: the expression need only match somewhere in the
     * string (JSON Schema core section 6.4), so a validator that anchored
     * would reject instances the specification accepts.  The anchoring is the
     * provider's obligation; nothing here can check it, which is why the
     * vtable's documentation says it twice. */
    if (node->pattern_regex) {
      int matched = 0;
      GTEXT_JSON_Status status = json_schema_regex_search(node,
          node->pattern_regex, instance->as.string.data, str_len, &matched,
          err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
      if (!matched) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "String does not match pattern"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
    }
    break;
  }

  case GTEXT_JSON_ARRAY: {
    // Check array size constraints
    size_t arr_size = gtext_json_array_size(instance);
    if (node->has_min_items && arr_size < node->min_items) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "Array has fewer items than minItems"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }
    if (node->has_max_items && arr_size > node->max_items) {
      if (err) {
        *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
            .message = "Array has more items than maxItems"};
      }
      return GTEXT_JSON_E_SCHEMA;
    }

    if (node->has_unique_items && node->unique_items && arr_size > 1) {
      /* Pairwise, using the same equality the enum and const keywords use.
       * Quadratic, which is fine for the array sizes a schema plausibly
       * constrains and avoids inventing a hash over arbitrary JSON values
       * whose equality is structural. */
      for (size_t i = 0; i < arr_size; i++) {
        const GTEXT_JSON_Value * a = gtext_json_array_get(instance, i);
        for (size_t j = i + 1; j < arr_size; j++) {
          const GTEXT_JSON_Value * b = gtext_json_array_get(instance, j);
          if (json_value_equal(a, b)) {
            if (err) {
              *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
                  .message = "Array has duplicate items but uniqueItems is set"};
            }
            return GTEXT_JSON_E_SCHEMA;
          }
        }
      }
    }

    // prefixItems constrains by position; anything past the last of them is
    // additional_items' business.  draft-07's `items: [...]` compiles to the
    // same place, so both spellings behave alike.
    for (size_t i = 0; i < node->prefix_items_count && i < arr_size; i++) {
      const GTEXT_JSON_Value * item = gtext_json_array_get(instance, i);
      if (!item) {
        continue;
      }
      GTEXT_JSON_Status status = json_schema_validate_depth(
          node->prefix_items[i], item, depth + 1, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    if (node->additional_items && arr_size > node->prefix_items_count) {
      for (size_t i = node->prefix_items_count; i < arr_size; i++) {
        const GTEXT_JSON_Value * item = gtext_json_array_get(instance, i);
        if (!item) {
          continue;
        }
        GTEXT_JSON_Status status = json_schema_validate_depth(
            node->additional_items, item, depth + 1, err);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
    }

    // contains: at least one item must match, unless minContains says
    // otherwise.  minContains of 0 makes an empty array valid, which is the
    // one case where "contains" asserts nothing.
    if (node->contains_schema) {
      size_t matches = 0;
      for (size_t i = 0; i < arr_size; i++) {
        const GTEXT_JSON_Value * item = gtext_json_array_get(instance, i);
        if (!item) {
          continue;
        }
        GTEXT_JSON_Error sub;
        memset(&sub, 0, sizeof(sub));
        if (json_schema_validate_depth(
                node->contains_schema, item, depth + 1, &sub)
            == GTEXT_JSON_OK) {
          matches++;
        }
        gtext_json_error_free(&sub);
      }
      size_t need = node->has_min_contains ? node->min_contains : 1;
      if (matches < need) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Array does not contain enough matching items"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
      if (node->has_max_contains && matches > node->max_contains) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Array contains more matching items than maxContains"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
    }

    // Validate items if schema is provided
    if (node->items_schema) {
      for (size_t i = 0; i < arr_size; i++) {
        const GTEXT_JSON_Value * item = gtext_json_array_get(instance, i);
        if (!item) {
          continue;
        }

        GTEXT_JSON_Status status =
            json_schema_validate_depth(node->items_schema, item, depth + 1, err);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
    }
    break;
  }

  case GTEXT_JSON_OBJECT: {
    // Object size constraints
    if (node->has_min_properties || node->has_max_properties) {
      size_t count = gtext_json_object_size(instance);
      if (node->has_min_properties && count < node->min_properties) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Object has fewer properties than minProperties"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
      if (node->has_max_properties && count > node->max_properties) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Object has more properties than maxProperties"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
    }

    // dependentRequired: a property's presence can require others.
    for (size_t i = 0; i < node->dep_required_count; i++) {
      const json_schema_dep_required * dep = &node->dep_required[i];
      if (!gtext_json_object_get(instance, dep->key, strlen(dep->key))) {
        continue; // trigger absent, nothing required
      }
      for (size_t j = 0; j < dep->required_count; j++) {
        const char * name = dep->required[j];
        if (!gtext_json_object_get(instance, name, strlen(name))) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
                .message = "A property required by dependentRequired is "
                           "missing"};
          }
          return GTEXT_JSON_E_SCHEMA;
        }
      }
    }

    // Check required properties
    for (size_t i = 0; i < node->required_count; i++) {
      const char * req_key = node->required_keys[i];
      size_t req_key_len = strlen(req_key);
      const GTEXT_JSON_Value * prop_val =
          gtext_json_object_get(instance, req_key, req_key_len);
      if (!prop_val) {
        if (err) {
          *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_SCHEMA,
              .message = "Required property is missing"};
        }
        return GTEXT_JSON_E_SCHEMA;
      }
    }

    // propertyNames applies to each key, as a string instance.  The key is
    // wrapped in a temporary value so the same node machinery can check it.
    if (node->property_names) {
      size_t count = gtext_json_object_size(instance);
      for (size_t i = 0; i < count; i++) {
        size_t klen = 0;
        const char * kname = gtext_json_object_key(instance, i, &klen);
        if (!kname) {
          continue;
        }
        GTEXT_JSON_Value * key_val = gtext_json_new_string(kname, klen);
        if (!key_val) {
          if (err) {
            *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
                .message = "Out of memory checking propertyNames"};
          }
          return GTEXT_JSON_E_OOM;
        }
        GTEXT_JSON_Status status = json_schema_validate_depth(
            node->property_names, key_val, depth + 1, err);
        gtext_json_free(key_val);
        if (status != GTEXT_JSON_OK) {
          if (err && err->code == GTEXT_JSON_E_SCHEMA) {
            err->message = "A property name does not match propertyNames";
          }
          return status;
        }
      }
    }

    /* patternProperties and additionalProperties, in one pass over the
     * instance's properties.
     *
     * They are here together because the second is defined in terms of the
     * first: additionalProperties applies to every property that neither
     * `properties` named nor any `patternProperties` expression matched.
     * Two passes would mean running each regular expression over each
     * property name twice, and - the reason that matters - would mean two
     * places that have to agree on what "matched" means.
     *
     * A property can match several patterns, and then every one of their
     * schemas applies. */
    if (node->pattern_properties_count > 0 || node->additional_properties) {
      size_t count = gtext_json_object_size(instance);
      for (size_t i = 0; i < count; i++) {
        size_t klen = 0;
        const char * kname = gtext_json_object_key(instance, i, &klen);
        if (!kname) {
          continue;
        }
        const GTEXT_JSON_Value * pv = gtext_json_object_value(instance, i);
        if (!pv) {
          continue;
        }

        int covered = 0;
        for (size_t p = 0; p < node->properties_count && !covered; p++) {
          if (node->properties[p].key_len == klen
              && memcmp(node->properties[p].key, kname, klen) == 0) {
            covered = 1;
          }
        }

        for (size_t p = 0; p < node->pattern_properties_count; p++) {
          const json_schema_pattern_property * entry =
              &node->pattern_properties[p];
          int matched = 0;
          GTEXT_JSON_Status status = json_schema_regex_search(
              node, entry->regex, kname, klen, &matched, err);
          if (status != GTEXT_JSON_OK) {
            return status;
          }
          if (!matched) {
            continue;
          }
          covered = 1;
          status = json_schema_validate_depth(entry->schema, pv, depth + 1,
              err);
          if (status != GTEXT_JSON_OK) {
            if (err && err->code == GTEXT_JSON_E_SCHEMA) {
              err->message =
                  "A property does not satisfy its patternProperties schema";
            }
            return status;
          }
        }

        if (covered || !node->additional_properties) {
          continue;
        }
        GTEXT_JSON_Status status = json_schema_validate_depth(
            node->additional_properties, pv, depth + 1, err);
        if (status != GTEXT_JSON_OK) {
          if (err && err->code == GTEXT_JSON_E_SCHEMA) {
            err->message = "A property is not allowed by additionalProperties";
          }
          return status;
        }
      }
    }

    // dependentSchemas: a property's presence applies a whole schema.
    for (size_t i = 0; i < node->dep_schemas_count; i++) {
      const json_schema_dep_schema * dep = &node->dep_schemas[i];
      if (!gtext_json_object_get(instance, dep->key, strlen(dep->key))) {
        continue;
      }
      GTEXT_JSON_Status status =
          json_schema_validate_depth(dep->schema, instance, depth + 1, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }

    // Validate properties
    if (node->properties_count > 0) {
      for (size_t i = 0; i < node->properties_count; i++) {
        const json_schema_property * prop = &node->properties[i];
        const GTEXT_JSON_Value * prop_val =
            gtext_json_object_get(instance, prop->key, prop->key_len);

        if (prop_val) {
          // Property exists, validate it
          GTEXT_JSON_Status status =
              json_schema_validate_depth(prop->schema, prop_val, depth + 1, err);
          if (status != GTEXT_JSON_OK) {
            return status;
          }
        }
      }
    }
    break;
  }

  default:
    break;
  }

  return GTEXT_JSON_OK;
}

// Public API functions

GTEXT_API GTEXT_JSON_Schema_Options gtext_json_schema_options_default(void) {
  GTEXT_JSON_Schema_Options opts;
  opts.allow_unsupported_keywords = false;
  opts.regex = NULL;
  return opts;
}

GTEXT_API GTEXT_JSON_Schema * gtext_json_schema_compile(
    const GTEXT_JSON_Value * schema_doc, GTEXT_JSON_Error * err) {
  return gtext_json_schema_compile_with_options(schema_doc, NULL, err);
}

GTEXT_API GTEXT_JSON_Schema * gtext_json_schema_compile_with_options(
    const GTEXT_JSON_Value * schema_doc,
    const GTEXT_JSON_Schema_Options * opts_in, GTEXT_JSON_Error * err) {
  GTEXT_JSON_Schema_Options defaults = gtext_json_schema_options_default();
  const GTEXT_JSON_Schema_Options * opts = opts_in ? opts_in : &defaults;

  if (!schema_doc) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Schema document must not be NULL"};
    }
    return NULL;
  }

  /* A boolean is a schema too - "true" accepts everything and "false"
   * accepts nothing - and that is as true at the root as it is for a
   * subschema. */
  if (schema_doc->type != GTEXT_JSON_OBJECT
      && schema_doc->type != GTEXT_JSON_BOOL) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Schema document must be an object or a boolean"};
    }
    return NULL;
  }

  /* A provider missing one of its three functions is a caller error, not a
   * schema that has no regular-expression support: the difference matters,
   * because the second silently refuses every `pattern` and the first is a
   * bug in the adapter that would otherwise surface as a null call much
   * later. */
  if (opts->regex
      && (!opts->regex->compile_fn || !opts->regex->search_fn
          || !opts->regex->free_fn)) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "A regular-expression provider must supply compile_fn, "
                     "search_fn and free_fn"};
    }
    return NULL;
  }

  // Allocate schema structure
  GTEXT_JSON_Schema * schema =
      (GTEXT_JSON_Schema *)calloc(1, sizeof(GTEXT_JSON_Schema));
  if (!schema) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory allocating schema"};
    }
    return NULL;
  }

  // Create context for cloned enum/const values
  schema->ctx = json_context_new(NULL);
  if (!schema->ctx) {
    free(schema);
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory creating schema context"};
    }
    return NULL;
  }

  // Allocate root node
  schema->root = (json_schema_node *)calloc(1, sizeof(json_schema_node));
  if (!schema->root) {
    json_context_free(schema->ctx);
    free(schema);
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory allocating schema node"};
    }
    return NULL;
  }

  /* The document is cloned so that $ref can be resolved against it after the
   * caller frees theirs.  A schema with no $ref pays for this too; keeping
   * two code paths, one of which only some schemas exercise, is the more
   * expensive choice in the end. */
  schema->doc = gtext_json_clone(schema_doc);
  if (!schema->doc) {
    json_schema_node_free(schema->root);
    json_context_free(schema->ctx);
    free(schema);
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory cloning the schema document"};
    }
    return NULL;
  }

  /* The provider is copied rather than pointed at: the options structure is
   * the caller's automatic variable as often as not, and the compiled
   * patterns outlive the call that made them.  What `ctx` points at is still
   * the caller's to keep alive, and the header says so. */
  if (opts->regex) {
    schema->regex_provider = *opts->regex;
    schema->has_regex_provider = 1;
  }

  // Compile schema
  json_schema_compile_ctx cc = {
      .ctx = schema->ctx, .opts = opts, .schema = schema, .depth = 0};
  GTEXT_JSON_Status status =
      json_schema_compile_node(schema->root, schema->doc, &cc, err);
  if (status != GTEXT_JSON_OK) {
    gtext_json_schema_free(schema);
    return NULL;
  }

  return schema;
}

GTEXT_API void gtext_json_schema_free(GTEXT_JSON_Schema * schema) {
  if (!schema) {
    return;
  }

  json_schema_node_free(schema->root);

  /* The registry owns every $ref target, so they are freed here rather than
   * by the nodes that refer to them. */
  if (schema->refs) {
    for (size_t i = 0; i < schema->refs_count; i++) {
      free(schema->refs[i].pointer);
      json_schema_node_free(schema->refs[i].node);
    }
    free(schema->refs);
  }

  gtext_json_free(schema->doc);
  json_context_free(schema->ctx);
  free(schema);
}

GTEXT_API GTEXT_JSON_Status gtext_json_schema_validate(
    const GTEXT_JSON_Schema * schema, const GTEXT_JSON_Value * instance,
    GTEXT_JSON_Error * err) {
  if (!schema || !instance) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Schema and instance must not be NULL"};
    }
    return GTEXT_JSON_E_INVALID;
  }

  return json_schema_validate_node(schema->root, instance, err);
}
