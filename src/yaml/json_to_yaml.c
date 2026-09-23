/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * JSON to YAML: the direction gtext_yaml_to_json() does not go.
 *
 * Every JSON document is a YAML document - YAML 1.2 section 10.2 says JSON is a
 * subset - so unlike the other direction this conversion cannot fail on the
 * grammar. What it can fail on is depth, and what it has to get right is types.
 *
 * **The whole difficulty is that YAML resolves a plain scalar by its contents.**
 * A JSON string is a string whatever it says; a YAML plain scalar saying `true`,
 * `null`, `42` or `1.5` is a boolean, a null, an integer or a float. So a JSON
 * string whose text happens to look like one of those must not become a plain
 * scalar, or the value changes type on the way through. Every string is
 * therefore built with an explicit type of GTEXT_YAML_STRING rather than left to
 * be resolved, which is what gtext_yaml_node_new_scalar_typed() is for, and the
 * writer then quotes whatever needs quoting because it knows the node is a
 * string.
 *
 * Numbers keep the lexeme the JSON parser preserved rather than being formatted
 * from a double. `1.0`, `1e3` and `10000000000000000000000` all survive as
 * written; going through a double would turn the first into `1`, and the third
 * into something that is not the same number.
 *
 * The walk is iterative. A recursive one is bounded by the C stack rather than
 * by the depth limit, which means a deep document is a crash instead of a
 * refusal - the same reasoning yaml_dom.c's clone walk records.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/json.h>
#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/yaml/yaml_dom.h>

#include "yaml_internal.h"

/*
 * The walk is bottom-up, and that is forced rather than chosen.
 *
 * gtext_yaml_sequence_append() and gtext_yaml_mapping_set() are *functional*:
 * each returns a NEW container node and the old one must not be used again, as
 * their documentation says in as many words. So a container cannot be attached
 * to its parent and then filled - the act of filling it replaces it, and the
 * parent would still be holding the empty one. Written that way the conversion
 * produced `{}` for every object, which is what it did before this comment
 * existed.
 *
 * So each container is finished before anyone points at it: children are
 * converted first, their nodes accumulate on a results stack, and the container
 * is folded out of them and pushed as a single result. Recursion would say the
 * same thing more briefly and bound the depth by the C stack rather than by
 * max_depth, which turns a deep document into a crash instead of a refusal.
 */

typedef enum {
  J2Y_VISIT,   /* convert this JSON value */
  J2Y_ASSEMBLE /* its children are done; fold them into a container */
} j2y_kind;

typedef struct {
  j2y_kind kind;
  const GTEXT_JSON_Value * json;
  size_t count; /* ASSEMBLE: how many results belong to this container */
  size_t depth;
} j2y_task;

typedef struct {
  j2y_task * items;
  size_t count;
  size_t capacity;
} j2y_stack;

typedef struct {
  GTEXT_YAML_Node ** items;
  size_t count;
  size_t capacity;
} j2y_results;

static bool j2y_stack_push(
    const GTEXT_Allocator * alloc, j2y_stack * st, j2y_task task) {
  if (st->count == st->capacity) {
    size_t cap = st->capacity == 0 ? 32 : st->capacity * 2;
    if (cap > SIZE_MAX / sizeof(j2y_task)) {
      return false;
    }
    j2y_task * items = (j2y_task *)gtext_allocator_realloc(
        alloc, st->items, cap * sizeof(j2y_task));
    if (!items) {
      return false;
    }
    st->items = items;
    st->capacity = cap;
  }
  st->items[st->count++] = task;
  return true;
}

static bool j2y_result_push(
    const GTEXT_Allocator * alloc, j2y_results * r, GTEXT_YAML_Node * node) {
  if (r->count == r->capacity) {
    size_t cap = r->capacity == 0 ? 32 : r->capacity * 2;
    if (cap > SIZE_MAX / sizeof(GTEXT_YAML_Node *)) {
      return false;
    }
    GTEXT_YAML_Node ** items = (GTEXT_YAML_Node **)gtext_allocator_realloc(
        alloc, r->items, cap * sizeof(GTEXT_YAML_Node *));
    if (!items) {
      return false;
    }
    r->items = items;
    r->capacity = cap;
  }
  r->items[r->count++] = node;
  return true;
}

static void j2y_fail(
    GTEXT_YAML_Error * err, GTEXT_YAML_Status code, const char * message) {
  if (err) {
    err->code = code;
    err->message = message;
    err->line = 0;
    err->col = 0;
  }
}

/* Convert one JSON scalar. Containers are handled by the walk. */
static GTEXT_YAML_Node * j2y_scalar(GTEXT_YAML_Document * doc,
    const GTEXT_JSON_Value * json, GTEXT_YAML_Status * status,
    GTEXT_YAML_Error * err) {
  switch (gtext_json_typeof(json)) {
    case GTEXT_JSON_NULL:
      return gtext_yaml_node_new_scalar_typed(
          doc, "null", 4, GTEXT_YAML_NULL, NULL, NULL);

    case GTEXT_JSON_BOOL: {
      bool b = false;
      if (gtext_json_get_bool(json, &b) != GTEXT_JSON_OK) {
        *status = GTEXT_YAML_E_INVALID;
        j2y_fail(err, *status, "could not read a JSON boolean");
        return NULL;
      }
      const char * text = b ? "true" : "false";
      return gtext_yaml_node_new_scalar_typed(
          doc, text, strlen(text), GTEXT_YAML_BOOL, NULL, NULL);
    }

    case GTEXT_JSON_NUMBER: {
      /* The lexeme the parser kept, not a reformatted double: 1.0, 1e3 and an
         integer too large for any C type all survive as written. */
      const char * lex = NULL;
      size_t lex_len = 0;
      if (gtext_json_get_number_lexeme(json, &lex, &lex_len) != GTEXT_JSON_OK
          || !lex) {
        *status = GTEXT_YAML_E_INVALID;
        j2y_fail(err, *status, "a JSON number had no lexeme");
        return NULL;
      }
      /* Which of YAML's two number types it is, decided from the text: a
         lexeme holding '.', 'e' or 'E' is a float and anything else an
         integer, which is the question 10.3.2's rows ask of the same text. */
      GTEXT_YAML_Node_Type type = GTEXT_YAML_INT;
      for (size_t i = 0; i < lex_len; i++) {
        if (lex[i] == '.' || lex[i] == 'e' || lex[i] == 'E') {
          type = GTEXT_YAML_FLOAT;
          break;
        }
      }
      GTEXT_YAML_Node * num =
          gtext_yaml_node_new_scalar_typed(doc, lex, lex_len, type, NULL, NULL);
      if (num) {
        return num;
      }
      /*
       * A number JSON can write and YAML cannot hold as a number: this
       * library's YAML integers are int64, so 123456789012345678901234567890
       * has no integer node and the constructor refuses it.
       *
       * It becomes a string, which is not a third answer invented here - it is
       * the answer this library's own YAML parser already gives. Handed
       * `v: 123456789012345678901234567890` it produces a STRING node, because
       * the digits do not fit either. So the conversion agrees with the parser,
       * and the document it produces reads back as the document it describes.
       *
       * The type does change, which is a real loss and is recorded as a
       * deviation rather than left to be discovered.
       */
      return gtext_yaml_node_new_scalar_typed(
          doc, lex, lex_len, GTEXT_YAML_STRING, NULL, NULL);
    }

    case GTEXT_JSON_STRING: {
      const char * str = NULL;
      size_t len = 0;
      if (gtext_json_get_string(json, &str, &len) != GTEXT_JSON_OK) {
        *status = GTEXT_YAML_E_INVALID;
        j2y_fail(err, *status, "could not read a JSON string");
        return NULL;
      }
      /* GTEXT_YAML_STRING explicitly, never resolved from the text. A JSON
         string saying "true" or "42" is a string, and a plain YAML scalar
         saying either is not - so leaving this to the resolver would change
         the value's type on the way through. */
      return gtext_yaml_node_new_scalar_typed(
          doc, str ? str : "", len, GTEXT_YAML_STRING, NULL, NULL);
    }

    default:
      *status = GTEXT_YAML_E_INVALID;
      j2y_fail(err, *status, "unknown JSON value type");
      return NULL;
  }
}

GTEXT_API GTEXT_YAML_Status gtext_json_to_yaml(const GTEXT_JSON_Value * json,
    const GTEXT_YAML_Parse_Options * options, GTEXT_YAML_Document ** out_doc,
    GTEXT_YAML_Error * out_err) {
  if (!json || !out_doc) {
    j2y_fail(out_err, GTEXT_YAML_E_INVALID, "json and out_doc must not be NULL");
    return GTEXT_YAML_E_INVALID;
  }
  *out_doc = NULL;

  GTEXT_YAML_Document * doc = gtext_yaml_document_new(options, out_err);
  if (!doc) {
    return GTEXT_YAML_E_OOM;
  }

  const GTEXT_Allocator * alloc = options ? options->allocator : NULL;
  /* 0 means unlimited, as it does in the parse options. */
  const size_t max_depth = options ? options->max_depth : 0;

  j2y_stack stack = {NULL, 0, 0};
  j2y_results results = {NULL, 0, 0};
  GTEXT_YAML_Status status = GTEXT_YAML_OK;

  j2y_task first = {J2Y_VISIT, json, 0, 0};
  if (!j2y_stack_push(alloc, &stack, first)) {
    status = GTEXT_YAML_E_OOM;
    j2y_fail(out_err, status, "out of memory converting JSON to YAML");
  }

  while (status == GTEXT_YAML_OK && stack.count > 0) {
    j2y_task task = stack.items[--stack.count];

    if (task.kind == J2Y_ASSEMBLE) {
      /* The container's children are the last `task.count` results, in
         document order. Fold them in, taking the new node each time, and leave
         the finished container in their place. */
      if (results.count < task.count) {
        status = GTEXT_YAML_E_INVALID;
        j2y_fail(out_err, status, "internal: missing converted children");
        break;
      }
      GTEXT_YAML_Node ** children = results.items + (results.count - task.count);

      if (gtext_json_typeof(task.json) == GTEXT_JSON_ARRAY) {
        GTEXT_YAML_Node * seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
        for (size_t i = 0; seq && i < task.count; i++) {
          seq = gtext_yaml_sequence_append(doc, seq, children[i]);
        }
        if (!seq) {
          status = GTEXT_YAML_E_OOM;
          j2y_fail(out_err, status, "out of memory building a YAML sequence");
          break;
        }
        results.count -= task.count;
        if (!j2y_result_push(alloc, &results, seq)) {
          status = GTEXT_YAML_E_OOM;
          j2y_fail(out_err, status, "out of memory building a YAML sequence");
          break;
        }
      }
      else {
        GTEXT_YAML_Node * map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
        for (size_t i = 0; map && i < task.count; i++) {
          size_t key_len = 0;
          const char * key = gtext_json_object_key(task.json, i, &key_len);
          if (!key) {
            map = NULL;
            break;
          }
          /* The key is a string for the same reason a value is: an object name
             of "true" or "1" must come back as that text and not as a boolean
             or an integer key. */
          GTEXT_YAML_Node * key_node = gtext_yaml_node_new_scalar_typed(
              doc, key, key_len, GTEXT_YAML_STRING, NULL, NULL);
          if (!key_node) {
            map = NULL;
            break;
          }
          map = gtext_yaml_mapping_set(doc, map, key_node, children[i]);
        }
        if (!map) {
          status = GTEXT_YAML_E_OOM;
          j2y_fail(out_err, status, "out of memory building a YAML mapping");
          break;
        }
        results.count -= task.count;
        if (!j2y_result_push(alloc, &results, map)) {
          status = GTEXT_YAML_E_OOM;
          j2y_fail(out_err, status, "out of memory building a YAML mapping");
          break;
        }
      }
      continue;
    }

    if (max_depth != 0 && task.depth > max_depth) {
      status = GTEXT_YAML_E_DEPTH;
      j2y_fail(out_err, status, "JSON nesting exceeds max_depth");
      break;
    }

    GTEXT_JSON_Type type = gtext_json_typeof(task.json);
    if (type == GTEXT_JSON_ARRAY || type == GTEXT_JSON_OBJECT) {
      size_t n = type == GTEXT_JSON_ARRAY ? gtext_json_array_size(task.json)
                                          : gtext_json_object_size(task.json);
      j2y_task assemble = {J2Y_ASSEMBLE, task.json, n, task.depth};
      if (!j2y_stack_push(alloc, &stack, assemble)) {
        status = GTEXT_YAML_E_OOM;
        j2y_fail(out_err, status, "out of memory converting a JSON container");
        break;
      }
      /* Pushed in reverse so they are visited in document order, which is what
         puts their results in order for the fold above. */
      for (size_t i = n; i > 0; i--) {
        const GTEXT_JSON_Value * child =
            type == GTEXT_JSON_ARRAY
                ? gtext_json_array_get(task.json, i - 1)
                : gtext_json_object_value(task.json, i - 1);
        if (!child) {
          status = GTEXT_YAML_E_INVALID;
          j2y_fail(out_err, status, "could not read a JSON container member");
          break;
        }
        j2y_task visit = {J2Y_VISIT, child, 0, task.depth + 1};
        if (!j2y_stack_push(alloc, &stack, visit)) {
          status = GTEXT_YAML_E_OOM;
          j2y_fail(out_err, status, "out of memory converting a JSON container");
          break;
        }
      }
      continue;
    }

    GTEXT_YAML_Node * node = j2y_scalar(doc, task.json, &status, out_err);
    if (!node) {
      if (status == GTEXT_YAML_OK) {
        status = GTEXT_YAML_E_OOM;
        j2y_fail(out_err, status, "out of memory converting a JSON scalar");
      }
      break;
    }
    if (!j2y_result_push(alloc, &results, node)) {
      status = GTEXT_YAML_E_OOM;
      j2y_fail(out_err, status, "out of memory converting a JSON scalar");
      break;
    }
  }

  GTEXT_YAML_Node * root = (status == GTEXT_YAML_OK && results.count == 1)
      ? results.items[0]
      : NULL;

  gtext_allocator_free(alloc, stack.items);
  gtext_allocator_free(alloc, results.items);

  if (status != GTEXT_YAML_OK) {
    gtext_yaml_free(doc);
    return status;
  }
  if (!root) {
    /* Every JSON value converts to exactly one node, so anything else here is
       a failure that did not say so. */
    gtext_yaml_free(doc);
    j2y_fail(out_err, GTEXT_YAML_E_OOM, "out of memory converting JSON to YAML");
    return GTEXT_YAML_E_OOM;
  }

  if (!gtext_yaml_document_set_root(doc, root)) {
    gtext_yaml_free(doc);
    j2y_fail(out_err, GTEXT_YAML_E_INVALID, "could not set the document root");
    return GTEXT_YAML_E_INVALID;
  }

  *out_doc = doc;
  return GTEXT_YAML_OK;
}
