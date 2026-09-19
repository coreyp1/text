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
    "$ref", "$recursiveRef", "$dynamicRef", "additionalItems", "prefixItems",
    "contains", "minContains", "maxContains", "additionalProperties",
    "patternProperties", "propertyNames", "dependentSchemas", "dependencies",
    "unevaluatedItems", "unevaluatedProperties",
    /* Assertions. */
    "pattern", "format", "contentEncoding", "contentMediaType",
    "contentSchema",
    NULL};

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

/* Forward declaration: the applicator helpers below compile subschemas. */
static GTEXT_JSON_Status json_schema_compile_node(json_schema_node * node,
    const GTEXT_JSON_Value * schema_doc, json_context * ctx,
    const GTEXT_JSON_Schema_Options * opts, GTEXT_JSON_Error * err);

/* Compile one subschema into a freshly allocated node. */
static GTEXT_JSON_Status json_schema_compile_sub(json_schema_node ** out,
    const GTEXT_JSON_Value * doc, json_context * ctx,
    const GTEXT_JSON_Schema_Options * opts, GTEXT_JSON_Error * err) {
  json_schema_node * sub =
      (json_schema_node *)calloc(1, sizeof(json_schema_node));
  if (!sub) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_OOM,
          .message = "Out of memory allocating subschema"};
    }
    return GTEXT_JSON_E_OOM;
  }
  GTEXT_JSON_Status status = json_schema_compile_node(sub, doc, ctx, opts, err);
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
    const GTEXT_JSON_Value * value, json_context * ctx,
    const GTEXT_JSON_Schema_Options * opts, GTEXT_JSON_Error * err) {
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
        json_schema_compile_sub(&list[i], elem, ctx, opts, err);
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
    const GTEXT_JSON_Value * schema_doc, json_context * ctx,
    const GTEXT_JSON_Schema_Options * opts, GTEXT_JSON_Error * err) {
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
              json_schema_compile_node(prop->schema, prop_schema, ctx, opts, err);
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
          json_schema_compile_node(node->items_schema, value, ctx, opts, err);
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
          GTEXT_JSON_Value * cloned = json_value_clone(enum_val, ctx);
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
      node->const_value = json_value_clone(value, ctx);
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
          &node->all_of, &node->all_of_count, "allOf", value, ctx, opts, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "anyOf")) {
      GTEXT_JSON_Status status = json_schema_compile_sub_list(
          &node->any_of, &node->any_of_count, "anyOf", value, ctx, opts, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "oneOf")) {
      GTEXT_JSON_Status status = json_schema_compile_sub_list(
          &node->one_of, &node->one_of_count, "oneOf", value, ctx, opts, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "not")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->not_schema, value, ctx, opts, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "if")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->if_schema, value, ctx, opts, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "then")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->then_schema, value, ctx, opts, err);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    else if (json_matches(key, key_len, "else")) {
      GTEXT_JSON_Status status =
          json_schema_compile_sub(&node->else_schema, value, ctx, opts, err);
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
    else if (!opts->allow_unsupported_keywords &&
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


static GTEXT_JSON_Status json_schema_validate_node(
    const json_schema_node * node, const GTEXT_JSON_Value * instance,
    GTEXT_JSON_Error * err) {
  if (!node || !instance) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Invalid arguments to schema validation"};
    }
    return GTEXT_JSON_E_INVALID;
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
      if (json_schema_validate_node(node->all_of[i], instance, &sub)
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
      if (json_schema_validate_node(node->any_of[i], instance, &sub)
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
      if (json_schema_validate_node(node->one_of[i], instance, &sub)
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
        json_schema_validate_node(node->not_schema, instance, &sub);
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
    int cond = json_schema_validate_node(node->if_schema, instance, &sub)
        == GTEXT_JSON_OK;
    gtext_json_error_free(&sub);
    const json_schema_node * branch =
        cond ? node->then_schema : node->else_schema;
    if (branch) {
      GTEXT_JSON_Error berr;
      memset(&berr, 0, sizeof(berr));
      if (json_schema_validate_node(branch, instance, &berr)
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

    // Validate items if schema is provided
    if (node->items_schema) {
      for (size_t i = 0; i < arr_size; i++) {
        const GTEXT_JSON_Value * item = gtext_json_array_get(instance, i);
        if (!item) {
          continue;
        }

        GTEXT_JSON_Status status =
            json_schema_validate_node(node->items_schema, item, err);
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

    // Validate properties
    if (node->properties_count > 0) {
      for (size_t i = 0; i < node->properties_count; i++) {
        const json_schema_property * prop = &node->properties[i];
        const GTEXT_JSON_Value * prop_val =
            gtext_json_object_get(instance, prop->key, prop->key_len);

        if (prop_val) {
          // Property exists, validate it
          GTEXT_JSON_Status status =
              json_schema_validate_node(prop->schema, prop_val, err);
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

  if (schema_doc->type != GTEXT_JSON_OBJECT) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Schema document must be an object"};
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

  // Compile schema
  GTEXT_JSON_Status status =
      json_schema_compile_node(schema->root, schema_doc, schema->ctx, opts, err);
  if (status != GTEXT_JSON_OK) {
    json_schema_node_free(schema->root);
    json_context_free(schema->ctx);
    free(schema);
    return NULL;
  }

  return schema;
}

GTEXT_API void gtext_json_schema_free(GTEXT_JSON_Schema * schema) {
  if (!schema) {
    return;
  }

  json_schema_node_free(schema->root);
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
