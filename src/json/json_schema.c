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

static GTEXT_JSON_Status json_schema_compile_node(json_schema_node * node,
    const GTEXT_JSON_Value * schema_doc, json_context * ctx,
    GTEXT_JSON_Error * err) {
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
              json_schema_compile_node(prop->schema, prop_schema, ctx, err);
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
          json_schema_compile_node(node->items_schema, value, ctx, err);
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
    // Ignore unknown keywords (for forward compatibility)
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
    case GTEXT_JSON_NUMBER:
      instance_flag = JSON_SCHEMA_TYPE_NUMBER;
      break;
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

GTEXT_API GTEXT_JSON_Schema * gtext_json_schema_compile(
    const GTEXT_JSON_Value * schema_doc, GTEXT_JSON_Error * err) {
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
  schema->ctx = json_context_new();
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
      json_schema_compile_node(schema->root, schema_doc, schema->ctx, err);
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
