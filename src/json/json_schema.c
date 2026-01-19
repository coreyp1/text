/**
 * @file json_schema.c
 * @brief JSON Schema validation (core subset) implementation
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_schema.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

/**
 * @brief Schema type flags
 */
typedef enum {
    JSON_SCHEMA_TYPE_NULL = 1,
    JSON_SCHEMA_TYPE_BOOL = 2,
    JSON_SCHEMA_TYPE_NUMBER = 4,
    JSON_SCHEMA_TYPE_STRING = 8,
    JSON_SCHEMA_TYPE_ARRAY = 16,
    JSON_SCHEMA_TYPE_OBJECT = 32
} json_schema_type_flags;

/**
 * @brief Property schema entry
 */
typedef struct {
    char* key;                      ///< Property name (allocated)
    size_t key_len;                 ///< Property name length
    struct json_schema_node* schema; ///< Schema for this property
} json_schema_property;

/**
 * @brief Compiled schema node
 */
typedef struct json_schema_node {
    // Type validation
    unsigned int type_flags;        ///< Bitmask of allowed types (0 = any type)

    // Object validation
    json_schema_property* properties; ///< Property schemas (NULL if none)
    size_t properties_count;       ///< Number of properties
    size_t properties_capacity;     ///< Allocated capacity

    char** required_keys;           ///< Array of required property names (NULL if none)
    size_t required_count;          ///< Number of required keys
    size_t required_capacity;       ///< Allocated capacity for required keys

    // Array validation
    struct json_schema_node* items_schema; ///< Schema for array items (NULL if none)

    // Enum/const validation
    text_json_value** enum_values;  ///< Array of allowed enum values (NULL if none)
    size_t enum_count;              ///< Number of enum values
    size_t enum_capacity;           ///< Allocated capacity for enum values
    text_json_value* const_value;   ///< Single const value (NULL if none)

    // Numeric constraints
    int has_minimum;                ///< 1 if minimum is set
    double minimum;                 ///< Minimum value (inclusive)
    int has_maximum;                ///< 1 if maximum is set
    double maximum;                 ///< Maximum value (inclusive)

    // String constraints
    int has_min_length;             ///< 1 if minLength is set
    size_t min_length;              ///< Minimum string length
    int has_max_length;             ///< 1 if maxLength is set
    size_t max_length;              ///< Maximum string length

    // Array constraints
    int has_min_items;              ///< 1 if minItems is set
    size_t min_items;               ///< Minimum array size
    int has_max_items;              ///< 1 if maxItems is set
    size_t max_items;               ///< Maximum array size
} json_schema_node;

/**
 * @brief Compiled schema structure
 */
struct text_json_schema {
    json_schema_node* root;          ///< Root schema node
    json_context* ctx;               ///< Context for cloned enum/const values
};

/**
 * @brief Free a schema node recursively
 */
static void json_schema_node_free(json_schema_node* node) {
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

/**
 * @brief Parse type keyword
 */
static text_json_status json_schema_parse_type(
    json_schema_node* node,
    const text_json_value* type_value,
    text_json_error* err
) {
    if (type_value->type == TEXT_JSON_STRING) {
        // Single type string
        const char* type_str;
        size_t type_len;
        if (text_json_get_string(type_value, &type_str, &type_len) != TEXT_JSON_OK) {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Invalid type value in schema";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }

        if (json_matches(type_str, type_len, "null")) {
            node->type_flags |= JSON_SCHEMA_TYPE_NULL;
        } else if (json_matches(type_str, type_len, "boolean")) {
            node->type_flags |= JSON_SCHEMA_TYPE_BOOL;
        } else if (json_matches(type_str, type_len, "number")) {
            node->type_flags |= JSON_SCHEMA_TYPE_NUMBER;
        } else if (json_matches(type_str, type_len, "string")) {
            node->type_flags |= JSON_SCHEMA_TYPE_STRING;
        } else if (json_matches(type_str, type_len, "array")) {
            node->type_flags |= JSON_SCHEMA_TYPE_ARRAY;
        } else if (json_matches(type_str, type_len, "object")) {
            node->type_flags |= JSON_SCHEMA_TYPE_OBJECT;
        } else {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Unknown type in schema";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }
    } else if (type_value->type == TEXT_JSON_ARRAY) {
        // Array of types
        size_t count = text_json_array_size(type_value);
        for (size_t i = 0; i < count; i++) {
            const text_json_value* type_elem = text_json_array_get(type_value, i);
            if (!type_elem || type_elem->type != TEXT_JSON_STRING) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid type array element in schema";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            text_json_status status = json_schema_parse_type(node, type_elem, err);
            if (status != TEXT_JSON_OK) {
                return status;
            }
        }
    } else {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Type must be string or array of strings";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    return TEXT_JSON_OK;
}

/**
 * @brief Compile a schema node from a JSON value
 */
static text_json_status json_schema_compile_node(
    json_schema_node* node,
    const text_json_value* schema_doc,
    json_context* ctx,
    text_json_error* err
) {
    if (schema_doc->type != TEXT_JSON_OBJECT) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Schema must be an object";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    size_t obj_size = text_json_object_size(schema_doc);
    for (size_t i = 0; i < obj_size; i++) {
        const char* key;
        size_t key_len;
        text_json_object_key(schema_doc, i, &key_len);
        key = text_json_object_key(schema_doc, i, NULL);
        const text_json_value* value = text_json_object_value(schema_doc, i);

        if (json_matches(key, key_len, "type")) {
            text_json_status status = json_schema_parse_type(node, value, err);
            if (status != TEXT_JSON_OK) {
                return status;
            }
        } else if (json_matches(key, key_len, "properties")) {
            if (value->type != TEXT_JSON_OBJECT) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Properties must be an object";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            size_t prop_count = text_json_object_size(value);
            if (prop_count > 0) {
                // Allocate properties array
                if (node->properties_capacity < prop_count) {
                    size_t new_capacity = prop_count;
                    // Check for integer overflow in multiplication
                    if (new_capacity > SIZE_MAX / sizeof(json_schema_property)) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Properties array size overflow";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    json_schema_property* new_props = (json_schema_property*)realloc(
                        node->properties,
                        new_capacity * sizeof(json_schema_property)
                    );
                    if (!new_props) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory allocating properties";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    node->properties = new_props;
                    node->properties_capacity = new_capacity;
                }

                // Compile each property schema
                for (size_t j = 0; j < prop_count; j++) {
                    const char* prop_key;
                    size_t prop_key_len;
                    text_json_object_key(value, j, &prop_key_len);
                    prop_key = text_json_object_key(value, j, NULL);
                    const text_json_value* prop_schema = text_json_object_value(value, j);

                    // Allocate property entry
                    json_schema_property* prop = &node->properties[node->properties_count];
                    prop->key_len = prop_key_len;
                    // Check for integer overflow in key_len + 1
                    if (prop_key_len > SIZE_MAX - 1) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Property key length overflow";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    prop->key = (char*)malloc(prop_key_len + 1);
                    if (!prop->key) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory allocating property key";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    memcpy(prop->key, prop_key, prop_key_len);
                    prop->key[prop_key_len] = '\0';

                    // Compile property schema recursively
                    prop->schema = (json_schema_node*)calloc(1, sizeof(json_schema_node));
                    if (!prop->schema) {
                        free(prop->key);
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory allocating property schema";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }

                    text_json_status status = json_schema_compile_node(prop->schema, prop_schema, ctx, err);
                    if (status != TEXT_JSON_OK) {
                        free(prop->key);
                        free(prop->schema);
                        return status;
                    }

                    node->properties_count++;
                }
            }
        } else if (json_matches(key, key_len, "required")) {
            if (value->type != TEXT_JSON_ARRAY) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Required must be an array";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            size_t req_count = text_json_array_size(value);
            if (req_count > 0) {
                if (node->required_capacity < req_count) {
                    size_t new_capacity = req_count;
                    // Check for integer overflow in multiplication
                    if (new_capacity > SIZE_MAX / sizeof(char*)) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Required keys array size overflow";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    char** new_keys = (char**)realloc(
                        node->required_keys,
                        new_capacity * sizeof(char*)
                    );
                    if (!new_keys) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory allocating required keys";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    node->required_keys = new_keys;
                    node->required_capacity = new_capacity;
                }

                for (size_t j = 0; j < req_count; j++) {
                    const text_json_value* req_key = text_json_array_get(value, j);
                    if (!req_key || req_key->type != TEXT_JSON_STRING) {
                        if (err) {
                            err->code = TEXT_JSON_E_INVALID;
                            err->message = "Required array must contain strings";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_INVALID;
                    }

                    const char* req_key_str;
                    size_t req_key_len;
                    if (text_json_get_string(req_key, &req_key_str, &req_key_len) != TEXT_JSON_OK) {
                        if (err) {
                            err->code = TEXT_JSON_E_INVALID;
                            err->message = "Invalid required key";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_INVALID;
                    }

                    // Check for integer overflow in req_key_len + 1
                    if (req_key_len > SIZE_MAX - 1) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Required key length overflow";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    node->required_keys[node->required_count] = (char*)malloc(req_key_len + 1);
                    if (!node->required_keys[node->required_count]) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory allocating required key";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    memcpy(node->required_keys[node->required_count], req_key_str, req_key_len);
                    node->required_keys[node->required_count][req_key_len] = '\0';
                    node->required_count++;
                }
            }
        } else if (json_matches(key, key_len, "items")) {
            // Compile items schema recursively
            node->items_schema = (json_schema_node*)calloc(1, sizeof(json_schema_node));
            if (!node->items_schema) {
                if (err) {
                    err->code = TEXT_JSON_E_OOM;
                    err->message = "Out of memory allocating items schema";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_OOM;
            }

            text_json_status status = json_schema_compile_node(node->items_schema, value, ctx, err);
            if (status != TEXT_JSON_OK) {
                free(node->items_schema);
                node->items_schema = NULL;
                return status;
            }
        } else if (json_matches(key, key_len, "enum")) {
            if (value->type != TEXT_JSON_ARRAY) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Enum must be an array";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            size_t enum_count = text_json_array_size(value);
            if (enum_count > 0) {
                if (node->enum_capacity < enum_count) {
                    size_t new_capacity = enum_count;
                    // Check for integer overflow in multiplication
                    if (new_capacity > SIZE_MAX / sizeof(text_json_value*)) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Enum values array size overflow";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    text_json_value** new_enum = (text_json_value**)realloc(
                        node->enum_values,
                        new_capacity * sizeof(text_json_value*)
                    );
                    if (!new_enum) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory allocating enum values";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    node->enum_values = new_enum;
                    node->enum_capacity = new_capacity;
                }

                // Clone enum values into context
                // Initialize enum_count to 0 to ensure clean state
                node->enum_count = 0;
                for (size_t j = 0; j < enum_count; j++) {
                    const text_json_value* enum_val = text_json_array_get(value, j);
                    if (!enum_val) {
                        if (err) {
                            err->code = TEXT_JSON_E_INVALID;
                            err->message = "Invalid enum value";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        // Cleanup: enum_values already allocated, but enum_count is correct
                        // Context will free any cloned values on error
                        return TEXT_JSON_E_INVALID;
                    }

                    // Clone value into context
                    text_json_value* cloned = json_value_clone(enum_val, ctx);
                    if (!cloned) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory cloning enum value";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        // Cleanup: enum_count is correct, context will free cloned values
                        return TEXT_JSON_E_OOM;
                    }

                    node->enum_values[node->enum_count++] = cloned;
                }
            }
        } else if (json_matches(key, key_len, "const")) {
            // Clone const value into context
            node->const_value = json_value_clone(value, ctx);
            if (!node->const_value) {
                if (err) {
                    err->code = TEXT_JSON_E_OOM;
                    err->message = "Out of memory cloning const value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_OOM;
            }
        } else if (json_matches(key, key_len, "minimum")) {
            if (value->type != TEXT_JSON_NUMBER) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Minimum must be a number";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            double min_val;
            if (text_json_get_double(value, &min_val) != TEXT_JSON_OK) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid minimum value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            node->has_minimum = 1;
            node->minimum = min_val;
        } else if (json_matches(key, key_len, "maximum")) {
            if (value->type != TEXT_JSON_NUMBER) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Maximum must be a number";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            double max_val;
            if (text_json_get_double(value, &max_val) != TEXT_JSON_OK) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid maximum value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            node->has_maximum = 1;
            node->maximum = max_val;
        } else if (json_matches(key, key_len, "minLength")) {
            if (value->type != TEXT_JSON_NUMBER) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "MinLength must be a number";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            double min_len_val;
            if (text_json_get_double(value, &min_len_val) != TEXT_JSON_OK || min_len_val < 0) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid minLength value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            // Check for overflow when casting to size_t
            if (min_len_val > (double)SIZE_MAX) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "minLength value exceeds maximum size_t";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            node->has_min_length = 1;
            node->min_length = (size_t)min_len_val;
        } else if (json_matches(key, key_len, "maxLength")) {
            if (value->type != TEXT_JSON_NUMBER) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "MaxLength must be a number";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            double max_len_val;
            if (text_json_get_double(value, &max_len_val) != TEXT_JSON_OK || max_len_val < 0) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid maxLength value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            // Check for overflow when casting to size_t
            if (max_len_val > (double)SIZE_MAX) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "maxLength value exceeds maximum size_t";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            node->has_max_length = 1;
            node->max_length = (size_t)max_len_val;
        } else if (json_matches(key, key_len, "minItems")) {
            if (value->type != TEXT_JSON_NUMBER) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "MinItems must be a number";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            double min_items_val;
            if (text_json_get_double(value, &min_items_val) != TEXT_JSON_OK || min_items_val < 0) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid minItems value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            // Check for overflow when casting to size_t
            if (min_items_val > (double)SIZE_MAX) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "minItems value exceeds maximum size_t";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            node->has_min_items = 1;
            node->min_items = (size_t)min_items_val;
        } else if (json_matches(key, key_len, "maxItems")) {
            if (value->type != TEXT_JSON_NUMBER) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "MaxItems must be a number";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            double max_items_val;
            if (text_json_get_double(value, &max_items_val) != TEXT_JSON_OK || max_items_val < 0) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid maxItems value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            // Check for overflow when casting to size_t
            if (max_items_val > (double)SIZE_MAX) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "maxItems value exceeds maximum size_t";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }

            node->has_max_items = 1;
            node->max_items = (size_t)max_items_val;
        }
        // Ignore unknown keywords (for forward compatibility)
    }

    return TEXT_JSON_OK;
}


/**
 * @brief Validate a value against a schema node
 */
static text_json_status json_schema_validate_node(
    const json_schema_node* node,
    const text_json_value* instance,
    text_json_error* err
) {
    if (!node || !instance) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments to schema validation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Check const first (most restrictive)
    if (node->const_value) {
        if (!json_value_equal(instance, node->const_value)) {
            if (err) {
                err->code = TEXT_JSON_E_SCHEMA;
                err->message = "Value does not match const";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_SCHEMA;
        }
        // If const matches, all other checks pass
        return TEXT_JSON_OK;
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
                err->code = TEXT_JSON_E_SCHEMA;
                err->message = "Value is not in enum";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_SCHEMA;
        }
    }

    // Check type
    if (node->type_flags != 0) {
        unsigned int instance_flag = 0;
        switch (instance->type) {
            case TEXT_JSON_NULL:
                instance_flag = JSON_SCHEMA_TYPE_NULL;
                break;
            case TEXT_JSON_BOOL:
                instance_flag = JSON_SCHEMA_TYPE_BOOL;
                break;
            case TEXT_JSON_NUMBER:
                instance_flag = JSON_SCHEMA_TYPE_NUMBER;
                break;
            case TEXT_JSON_STRING:
                instance_flag = JSON_SCHEMA_TYPE_STRING;
                break;
            case TEXT_JSON_ARRAY:
                instance_flag = JSON_SCHEMA_TYPE_ARRAY;
                break;
            case TEXT_JSON_OBJECT:
                instance_flag = JSON_SCHEMA_TYPE_OBJECT;
                break;
        }

        if ((node->type_flags & instance_flag) == 0) {
            if (err) {
                err->code = TEXT_JSON_E_SCHEMA;
                err->message = "Value type does not match schema type";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_SCHEMA;
        }
    }

    // Type-specific validations
    switch (instance->type) {
        case TEXT_JSON_NUMBER: {
            // Check numeric constraints
            double num_val;
            if (text_json_get_double(instance, &num_val) == TEXT_JSON_OK) {
                if (node->has_minimum && num_val < node->minimum) {
                    if (err) {
                        err->code = TEXT_JSON_E_SCHEMA;
                        err->message = "Number is less than minimum";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_SCHEMA;
                }
                if (node->has_maximum && num_val > node->maximum) {
                    if (err) {
                        err->code = TEXT_JSON_E_SCHEMA;
                        err->message = "Number is greater than maximum";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_SCHEMA;
                }
            }
            break;
        }

        case TEXT_JSON_STRING: {
            // Check string length constraints
            size_t str_len = instance->as.string.len;
            if (node->has_min_length && str_len < node->min_length) {
                if (err) {
                    err->code = TEXT_JSON_E_SCHEMA;
                    err->message = "String is shorter than minLength";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_SCHEMA;
            }
            if (node->has_max_length && str_len > node->max_length) {
                if (err) {
                    err->code = TEXT_JSON_E_SCHEMA;
                    err->message = "String is longer than maxLength";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_SCHEMA;
            }
            break;
        }

        case TEXT_JSON_ARRAY: {
            // Check array size constraints
            size_t arr_size = text_json_array_size(instance);
            if (node->has_min_items && arr_size < node->min_items) {
                if (err) {
                    err->code = TEXT_JSON_E_SCHEMA;
                    err->message = "Array has fewer items than minItems";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_SCHEMA;
            }
            if (node->has_max_items && arr_size > node->max_items) {
                if (err) {
                    err->code = TEXT_JSON_E_SCHEMA;
                    err->message = "Array has more items than maxItems";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_SCHEMA;
            }

            // Validate items if schema is provided
            if (node->items_schema) {
                for (size_t i = 0; i < arr_size; i++) {
                    const text_json_value* item = text_json_array_get(instance, i);
                    if (!item) {
                        continue;
                    }

                    text_json_status status = json_schema_validate_node(node->items_schema, item, err);
                    if (status != TEXT_JSON_OK) {
                        return status;
                    }
                }
            }
            break;
        }

        case TEXT_JSON_OBJECT: {
            // Check required properties
            for (size_t i = 0; i < node->required_count; i++) {
                const char* req_key = node->required_keys[i];
                size_t req_key_len = strlen(req_key);
                const text_json_value* prop_val = text_json_object_get(instance, req_key, req_key_len);
                if (!prop_val) {
                    if (err) {
                        err->code = TEXT_JSON_E_SCHEMA;
                        err->message = "Required property is missing";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_SCHEMA;
                }
            }

            // Validate properties
            if (node->properties_count > 0) {
                for (size_t i = 0; i < node->properties_count; i++) {
                    const json_schema_property* prop = &node->properties[i];
                    const text_json_value* prop_val = text_json_object_get(
                        instance,
                        prop->key,
                        prop->key_len
                    );

                    if (prop_val) {
                        // Property exists, validate it
                        text_json_status status = json_schema_validate_node(prop->schema, prop_val, err);
                        if (status != TEXT_JSON_OK) {
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

    return TEXT_JSON_OK;
}

// Public API functions

TEXT_API text_json_schema* text_json_schema_compile(
    const text_json_value* schema_doc,
    text_json_error* err
) {
    if (!schema_doc) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Schema document must not be NULL";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return NULL;
    }

    if (schema_doc->type != TEXT_JSON_OBJECT) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Schema document must be an object";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return NULL;
    }

    // Allocate schema structure
    text_json_schema* schema = (text_json_schema*)calloc(1, sizeof(text_json_schema));
    if (!schema) {
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory allocating schema";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return NULL;
    }

    // Create context for cloned enum/const values
    schema->ctx = json_context_new();
    if (!schema->ctx) {
        free(schema);
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory creating schema context";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return NULL;
    }

    // Allocate root node
    schema->root = (json_schema_node*)calloc(1, sizeof(json_schema_node));
    if (!schema->root) {
        json_context_free(schema->ctx);
        free(schema);
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory allocating schema node";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return NULL;
    }

    // Compile schema
    text_json_status status = json_schema_compile_node(schema->root, schema_doc, schema->ctx, err);
    if (status != TEXT_JSON_OK) {
        json_schema_node_free(schema->root);
        json_context_free(schema->ctx);
        free(schema);
        return NULL;
    }

    return schema;
}

TEXT_API void text_json_schema_free(text_json_schema* schema) {
    if (!schema) {
        return;
    }

    json_schema_node_free(schema->root);
    json_context_free(schema->ctx);
    free(schema);
}

TEXT_API text_json_status text_json_schema_validate(
    const text_json_schema* schema,
    const text_json_value* instance,
    text_json_error* err
) {
    if (!schema || !instance) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Schema and instance must not be NULL";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    return json_schema_validate_node(schema->root, instance, err);
}
