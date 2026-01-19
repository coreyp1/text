/**
 * @file json_patch.c
 * @brief JSON Patch (RFC 6902) implementation
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_pointer.h>
#include <ghoti.io/text/json/json_patch.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

// Helper function to deep clone a JSON value into the same context
// This is needed for the copy operation and schema validation
// Made non-static so it can be shared with json_schema.c
text_json_value* json_value_clone(const text_json_value* src, json_context* ctx) {
    if (!src || !ctx) {
        return NULL;
    }

    text_json_value* dst = json_value_new_with_existing_context(src->type, ctx);
    if (!dst) {
        return NULL;
    }

    switch (src->type) {
        case TEXT_JSON_NULL:
            // Nothing to copy
            break;

        case TEXT_JSON_BOOL:
            dst->as.boolean = src->as.boolean;
            break;

        case TEXT_JSON_STRING: {
            // Allocate and copy string data
            // Check for integer overflow in len + 1
            if (src->as.string.len > SIZE_MAX - 1) {
                return NULL;  // Overflow
            }
            char* str_data = (char*)json_arena_alloc_for_context(ctx, src->as.string.len + 1, 1);
            if (!str_data) {
                return NULL;
            }
            memcpy(str_data, src->as.string.data, src->as.string.len);
            str_data[src->as.string.len] = '\0';
            dst->as.string.data = str_data;
            dst->as.string.len = src->as.string.len;
            break;
        }

        case TEXT_JSON_NUMBER: {
            // Allocate and copy lexeme
            // Check for integer overflow in lexeme_len + 1
            if (src->as.number.lexeme_len > SIZE_MAX - 1) {
                return NULL;  // Overflow
            }
            char* lexeme = (char*)json_arena_alloc_for_context(ctx, src->as.number.lexeme_len + 1, 1);
            if (!lexeme) {
                return NULL;
            }
            memcpy(lexeme, src->as.number.lexeme, src->as.number.lexeme_len);
            lexeme[src->as.number.lexeme_len] = '\0';
            dst->as.number.lexeme = lexeme;
            dst->as.number.lexeme_len = src->as.number.lexeme_len;
            dst->as.number.i64 = src->as.number.i64;
            dst->as.number.u64 = src->as.number.u64;
            dst->as.number.dbl = src->as.number.dbl;
            dst->as.number.has_i64 = src->as.number.has_i64;
            dst->as.number.has_u64 = src->as.number.has_u64;
            dst->as.number.has_dbl = src->as.number.has_dbl;
            break;
        }

        case TEXT_JSON_ARRAY: {
            // Initialize array
            dst->as.array.count = 0;
            dst->as.array.capacity = src->as.array.count;
            if (dst->as.array.capacity > 0) {
                // Check for integer overflow in multiplication
                size_t elem_size = sizeof(text_json_value*);
                if (dst->as.array.capacity > SIZE_MAX / elem_size) {
                    return NULL;  // Overflow
                }
                dst->as.array.elems = (text_json_value**)json_arena_alloc_for_context(
                    ctx,
                    dst->as.array.capacity * elem_size,
                    sizeof(text_json_value*)
                );
                if (!dst->as.array.elems) {
                    return NULL;
                }
            } else {
                dst->as.array.elems = NULL;
            }

            // Clone each element
            for (size_t i = 0; i < src->as.array.count; i++) {
                text_json_value* cloned_elem = json_value_clone(src->as.array.elems[i], ctx);
                if (!cloned_elem) {
                    return NULL;
                }
                dst->as.array.elems[i] = cloned_elem;
                dst->as.array.count++;
            }
            break;
        }

        case TEXT_JSON_OBJECT: {
            // Initialize object
            dst->as.object.count = 0;
            dst->as.object.capacity = src->as.object.count;
            if (dst->as.object.capacity > 0) {
                // Allocate pairs array - cast through void* to avoid anonymous struct type mismatch
                size_t pair_size = sizeof(*(src->as.object.pairs));
                // Check for integer overflow in multiplication
                if (dst->as.object.capacity > SIZE_MAX / pair_size) {
                    return NULL;  // Overflow
                }
                void* new_pairs_ptr = json_arena_alloc_for_context(
                    ctx,
                    dst->as.object.capacity * pair_size,
                    sizeof(void*)
                );
                if (!new_pairs_ptr) {
                    return NULL;
                }
                // Assign through void* cast (types match structurally)
                dst->as.object.pairs = (void*)new_pairs_ptr;
            } else {
                dst->as.object.pairs = NULL;
            }

            // Clone each key-value pair
            for (size_t i = 0; i < src->as.object.count; i++) {
                // Allocate and copy key
                // Check for integer overflow in key_len + 1
                if (src->as.object.pairs[i].key_len > SIZE_MAX - 1) {
                    return NULL;  // Overflow
                }
                char* key = (char*)json_arena_alloc_for_context(ctx, src->as.object.pairs[i].key_len + 1, 1);
                if (!key) {
                    return NULL;
                }
                memcpy(key, src->as.object.pairs[i].key, src->as.object.pairs[i].key_len);
                key[src->as.object.pairs[i].key_len] = '\0';

                // Clone value
                text_json_value* cloned_val = json_value_clone(src->as.object.pairs[i].value, ctx);
                if (!cloned_val) {
                    return NULL;
                }

                dst->as.object.pairs[i].key = key;
                dst->as.object.pairs[i].key_len = src->as.object.pairs[i].key_len;
                dst->as.object.pairs[i].value = cloned_val;
                dst->as.object.count++;
            }
            break;
        }
    }

    return dst;
}

// Helper function to check deep equality of two JSON values
// This is needed for the test operation and schema validation
// Made non-static so it can be shared with json_schema.c
int json_value_equal(const text_json_value* a, const text_json_value* b) {
    if (a == b) {
        return 1;  // Same pointer
    }

    if (!a || !b) {
        return 0;  // One is NULL
    }

    if (a->type != b->type) {
        return 0;  // Different types
    }

    switch (a->type) {
        case TEXT_JSON_NULL:
            return 1;  // Both are null

        case TEXT_JSON_BOOL:
            return a->as.boolean == b->as.boolean;

        case TEXT_JSON_STRING:
            if (a->as.string.len != b->as.string.len) {
                return 0;
            }
            return memcmp(a->as.string.data, b->as.string.data, a->as.string.len) == 0;

        case TEXT_JSON_NUMBER: {
            // For numbers, check if they are numerically equal
            // First check if both have the same representation available
            if (a->as.number.has_i64 && b->as.number.has_i64) {
                return a->as.number.i64 == b->as.number.i64;
            }
            if (a->as.number.has_u64 && b->as.number.has_u64) {
                return a->as.number.u64 == b->as.number.u64;
            }
            if (a->as.number.has_dbl && b->as.number.has_dbl) {
                // Use approximate equality for doubles (with epsilon)
                double diff = fabs(a->as.number.dbl - b->as.number.dbl);
                return diff < 1e-15 || (a->as.number.dbl == b->as.number.dbl);
            }
            // Fall back to lexeme comparison
            if (a->as.number.lexeme_len != b->as.number.lexeme_len) {
                return 0;
            }
            return memcmp(a->as.number.lexeme, b->as.number.lexeme, a->as.number.lexeme_len) == 0;
        }

        case TEXT_JSON_ARRAY: {
            if (a->as.array.count != b->as.array.count) {
                return 0;
            }
            for (size_t i = 0; i < a->as.array.count; i++) {
                if (!json_value_equal(a->as.array.elems[i], b->as.array.elems[i])) {
                    return 0;
                }
            }
            return 1;
        }

        case TEXT_JSON_OBJECT: {
            if (a->as.object.count != b->as.object.count) {
                return 0;
            }
            // For objects, we need to check that all keys in a exist in b with equal values
            // and vice versa. Since objects may not have stable key order, we need to
            // search for each key.
            for (size_t i = 0; i < a->as.object.count; i++) {
                const char* key = a->as.object.pairs[i].key;
                size_t key_len = a->as.object.pairs[i].key_len;
                const text_json_value* b_val = text_json_object_get(b, key, key_len);
                if (!b_val) {
                    return 0;  // Key not found in b
                }
                if (!json_value_equal(a->as.object.pairs[i].value, b_val)) {
                    return 0;  // Values not equal
                }
            }
            return 1;
        }
    }

    return 0;  // Should not reach here
}

// Parse a string field from an operation object
static text_json_status json_patch_get_string_field(
    const text_json_value* op,
    const char* field_name,
    const char** out_str,
    size_t* out_len
) {
    if (!op || op->type != TEXT_JSON_OBJECT) {
        return TEXT_JSON_E_INVALID;
    }

    const text_json_value* field_val = text_json_object_get(op, field_name, strlen(field_name));
    if (!field_val) {
        return TEXT_JSON_E_INVALID;
    }

    if (field_val->type != TEXT_JSON_STRING) {
        return TEXT_JSON_E_INVALID;
    }

    *out_str = field_val->as.string.data;
    *out_len = field_val->as.string.len;
    return TEXT_JSON_OK;
}

// Parse the "op" field from an operation object
static text_json_status json_patch_get_op(
    const text_json_value* op,
    const char** out_op_str,
    size_t* out_op_len
) {
    return json_patch_get_string_field(op, "op", out_op_str, out_op_len);
}

// Parse the "path" field from an operation object
static text_json_status json_patch_get_path(
    const text_json_value* op,
    const char** out_path_str,
    size_t* out_path_len
) {
    return json_patch_get_string_field(op, "path", out_path_str, out_path_len);
}

// Parse the "from" field from an operation object
static text_json_status json_patch_get_from(
    const text_json_value* op,
    const char** out_from_str,
    size_t* out_from_len
) {
    return json_patch_get_string_field(op, "from", out_from_str, out_from_len);
}

// Parse the "value" field from an operation object
static const text_json_value* json_patch_get_value(const text_json_value* op) {
    if (!op || op->type != TEXT_JSON_OBJECT) {
        return NULL;
    }

    const text_json_value* value_val = text_json_object_get(op, "value", 5);
    return value_val;
}

// Check if "from" is a proper prefix of "path" (for move operation validation)
static int json_pointer_is_prefix(const char* from, size_t from_len, const char* path, size_t path_len) {
    if (from_len >= path_len) {
        return 0;  // from cannot be a prefix if it's longer or equal
    }

    // Check if path starts with from
    if (memcmp(from, path, from_len) != 0) {
        return 0;
    }

    // Check that the next character in path is '/' (proper prefix)
    if (from_len < path_len && path[from_len] == '/') {
        return 1;
    }

    return 0;
}

// Helper to find parent and last token of a JSON pointer
// Returns the parent value and the last token (key or index)
static text_json_status json_patch_find_parent_and_token(
    text_json_value* root,
    const char* path,
    size_t path_len,
    text_json_value** out_parent,
    const char** out_token,
    size_t* out_token_len,
    int* out_is_array_index
) {
    if (!root || !path || path_len == 0) {
        return TEXT_JSON_E_INVALID;
    }

    // Empty path means root itself
    if (path_len == 0 || (path_len == 1 && path[0] == '/')) {
        *out_parent = NULL;  // No parent (root is the target)
        *out_token = NULL;
        *out_token_len = 0;
        *out_is_array_index = 0;
        return TEXT_JSON_OK;
    }

    // Find the last '/' in the path
    const char* last_slash = NULL;
    size_t last_slash_pos = 0;
    for (size_t i = path_len; i > 0; i--) {
        // Check for unescaped '/'
        if (path[i - 1] == '/') {
            // Check if it's escaped (preceded by '~1')
            if (i >= 2 && path[i - 2] == '~' && path[i - 1] == '1') {
                // This is an escaped '/', continue
                continue;
            }
            last_slash = &path[i - 1];
            last_slash_pos = i - 1;
            break;
        }
    }

    if (!last_slash) {
        // No '/' found, path is just a single token
        // This means we're operating on root with a single key/index
        if (path[0] != '/') {
            return TEXT_JSON_E_INVALID;
        }

        *out_parent = root;
        *out_token = path + 1;  // Skip leading '/'
        *out_token_len = path_len - 1;

        // Check if it's an array index
        int is_idx = 0;
        if (*out_token_len > 0) {
            // Try parsing as index
            char* token_buf = (char*)malloc(*out_token_len + 1);
            if (!token_buf) {
                return TEXT_JSON_E_OOM;
            }
            memcpy(token_buf, *out_token, *out_token_len);
            token_buf[*out_token_len] = '\0';

            // Decode escape sequences
            size_t decoded_len;
            char* decoded = (char*)malloc(*out_token_len + 1);
            if (!decoded) {
                free(token_buf);
                return TEXT_JSON_E_OOM;
            }
            // Simple decode: ~0 -> ~, ~1 -> /
            // Note: out_pos can never exceed *out_token_len because each input char
            // produces at most one output char (escape sequences consume 2 input, produce 1 output)
            size_t out_pos = 0;
            for (size_t i = 0; i < *out_token_len; i++) {
                // Defensive bounds check (should never trigger, but prevents overflow)
                if (out_pos >= *out_token_len) {
                    free(decoded);
                    free(token_buf);
                    return TEXT_JSON_E_INVALID;
                }
                if (token_buf[i] == '~' && i + 1 < *out_token_len) {
                    if (token_buf[i + 1] == '0') {
                        decoded[out_pos++] = '~';
                        i++;
                    } else if (token_buf[i + 1] == '1') {
                        decoded[out_pos++] = '/';
                        i++;
                    } else {
                        decoded[out_pos++] = token_buf[i];
                    }
                } else {
                    decoded[out_pos++] = token_buf[i];
                }
            }
            decoded_len = out_pos;
            decoded[decoded_len] = '\0';  // Null-terminate for strtoull

            // Check if it's a valid array index
            if (decoded_len > 0 && isdigit((unsigned char)decoded[0])) {
                // Check for leading zeros
                if (decoded_len == 1 || (decoded[0] != '0')) {
                    // Try to parse as index
                    char* endptr;
                    unsigned long long parsed = strtoull(decoded, &endptr, 10);
                    if (*endptr == '\0' && parsed <= SIZE_MAX) {
                        is_idx = 1;
                    }
                }
            }

            free(decoded);
            free(token_buf);
        }

        *out_is_array_index = is_idx;
        return TEXT_JSON_OK;
    }

    // Extract parent path (everything before last '/')
    size_t parent_path_len = last_slash_pos;
    char* parent_path = (char*)malloc(parent_path_len + 1);
    if (!parent_path) {
        return TEXT_JSON_E_OOM;
    }
    memcpy(parent_path, path, parent_path_len);
    parent_path[parent_path_len] = '\0';

    // Get parent value
    text_json_value* parent = text_json_pointer_get_mut(root, parent_path, parent_path_len);
    free(parent_path);

    if (!parent) {
        return TEXT_JSON_E_INVALID;
    }

    *out_parent = parent;
    *out_token = last_slash + 1;  // After the '/'
    *out_token_len = path_len - (last_slash_pos + 1);

    // Check if it's an array index
    int is_idx = 0;
    if (*out_token_len > 0) {
        // Try parsing as index
        char* token_buf = (char*)malloc(*out_token_len + 1);
        if (!token_buf) {
            return TEXT_JSON_E_OOM;
        }
        memcpy(token_buf, *out_token, *out_token_len);
        token_buf[*out_token_len] = '\0';

        // Decode escape sequences
        size_t decoded_len;
        char* decoded = (char*)malloc(*out_token_len + 1);
        if (!decoded) {
            free(token_buf);
            return TEXT_JSON_E_OOM;
        }
        // Simple decode: ~0 -> ~, ~1 -> /
        // Note: out_pos can never exceed *out_token_len because each input char
        // produces at most one output char (escape sequences consume 2 input, produce 1 output)
        size_t out_pos = 0;
        for (size_t i = 0; i < *out_token_len; i++) {
            // Defensive bounds check (should never trigger, but prevents overflow)
            if (out_pos >= *out_token_len) {
                free(decoded);
                free(token_buf);
                return TEXT_JSON_E_INVALID;
            }
            if (token_buf[i] == '~' && i + 1 < *out_token_len) {
                if (token_buf[i + 1] == '0') {
                    decoded[out_pos++] = '~';
                    i++;
                } else if (token_buf[i + 1] == '1') {
                    decoded[out_pos++] = '/';
                    i++;
                } else {
                    decoded[out_pos++] = token_buf[i];
                }
            } else {
                decoded[out_pos++] = token_buf[i];
            }
        }
        decoded_len = out_pos;
        decoded[decoded_len] = '\0';  // Null-terminate for strtoull

        // Check if it's a valid array index
        if (decoded_len > 0 && isdigit((unsigned char)decoded[0])) {
            // Check for leading zeros
            if (decoded_len == 1 || (decoded[0] != '0')) {
                // Try to parse as index
                char* endptr;
                unsigned long long parsed = strtoull(decoded, &endptr, 10);
                if (*endptr == '\0' && parsed <= SIZE_MAX) {
                    is_idx = 1;
                }
            }
        }

        free(decoded);
        free(token_buf);
    }

    *out_is_array_index = is_idx;
    return TEXT_JSON_OK;
}

// Implement add operation
static text_json_status json_patch_add(
    text_json_value* root,
    const char* path,
    size_t path_len,
    const text_json_value* value,
    text_json_error* err
) {
    if (!root || !path || !value) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments for add operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Empty path means replace root
    if (path_len == 0 || (path_len == 1 && path[0] == '/')) {
        // Cannot add at root (would need to replace entire tree)
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Cannot add at root path";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Find parent and token
    text_json_value* parent = NULL;
    const char* token = NULL;
    size_t token_len = 0;
    int is_array_index = 0;
    text_json_status status = json_patch_find_parent_and_token(
        root, path, path_len, &parent, &token, &token_len, &is_array_index
    );
    if (status != TEXT_JSON_OK) {
        if (err) {
            err->code = status;
            err->message = "Invalid path for add operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return status;
    }

    if (!parent) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Parent not found for add operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Clone the value into the parent's context
    json_context* ctx = parent->ctx;
    text_json_value* cloned_value = json_value_clone(value, ctx);
    if (!cloned_value) {
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory cloning value for add operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_OOM;
    }

    // Check if parent is an array - if so, treat token as array index (including "-")
    if (parent->type == TEXT_JSON_ARRAY) {
        // Add to array

        // Parse index
        char* token_buf = (char*)malloc(token_len + 1);
        if (!token_buf) {
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Out of memory parsing array index";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_OOM;
        }
        memcpy(token_buf, token, token_len);
        token_buf[token_len] = '\0';

        // Decode escape sequences
        size_t decoded_len;
        char* decoded = (char*)malloc(token_len + 1);
        if (!decoded) {
            free(token_buf);
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Out of memory decoding array index";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_OOM;
        }
        size_t out_pos = 0;
        for (size_t i = 0; i < token_len; i++) {
            if (token_buf[i] == '~' && i + 1 < token_len) {
                if (token_buf[i + 1] == '0') {
                    decoded[out_pos++] = '~';
                    i++;
                } else if (token_buf[i + 1] == '1') {
                    decoded[out_pos++] = '/';
                    i++;
                } else {
                    decoded[out_pos++] = token_buf[i];
                }
            } else {
                decoded[out_pos++] = token_buf[i];
            }
        }
        decoded_len = out_pos;
        decoded[decoded_len] = '\0';

        size_t idx;
        if (strcmp(decoded, "-") == 0) {
            // Append to end
            idx = parent->as.array.count;
        } else {
            char* endptr;
            unsigned long long parsed = strtoull(decoded, &endptr, 10);
            if (*endptr != '\0' || parsed > SIZE_MAX) {
                free(decoded);
                free(token_buf);
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid array index";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            idx = (size_t)parsed;
            if (idx > parent->as.array.count) {
                free(decoded);
                free(token_buf);
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Array index out of bounds";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
        }

        free(decoded);
        free(token_buf);

        // Insert at index
        status = text_json_array_insert(parent, idx, cloned_value);
        if (status != TEXT_JSON_OK) {
            if (err) {
                err->code = status;
                err->message = "Failed to insert into array";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return status;
        }
    } else {
        // Add to object (or replace existing key)
        if (parent->type != TEXT_JSON_OBJECT) {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Path points to object key but parent is not an object";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }

        // Decode token (handle escape sequences)
        char* decoded_key = (char*)malloc(token_len + 1);
        if (!decoded_key) {
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Out of memory decoding object key";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_OOM;
        }

        size_t out_pos = 0;
        for (size_t i = 0; i < token_len; i++) {
            // Defensive bounds check (should never trigger, but prevents overflow)
            if (out_pos >= token_len) {
                free(decoded_key);
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Buffer overflow in key decoding";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            if (token[i] == '~' && i + 1 < token_len) {
                if (token[i + 1] == '0') {
                    decoded_key[out_pos++] = '~';
                    i++;
                } else if (token[i + 1] == '1') {
                    decoded_key[out_pos++] = '/';
                    i++;
                } else {
                    decoded_key[out_pos++] = token[i];
                }
            } else {
                decoded_key[out_pos++] = token[i];
            }
        }
        size_t decoded_key_len = out_pos;

        // Put key-value pair (replaces if exists)
        status = text_json_object_put(parent, decoded_key, decoded_key_len, cloned_value);
        free(decoded_key);

        if (status != TEXT_JSON_OK) {
            if (err) {
                err->code = status;
                err->message = "Failed to add to object";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return status;
        }
    }

    return TEXT_JSON_OK;
}

// Implement remove operation
static text_json_status json_patch_remove(
    text_json_value* root,
    const char* path,
    size_t path_len,
    text_json_error* err
) {
    if (!root || !path) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments for remove operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Empty path means remove root (not allowed)
    if (path_len == 0 || (path_len == 1 && path[0] == '/')) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Cannot remove root";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Find parent and token
    text_json_value* parent = NULL;
    const char* token = NULL;
    size_t token_len = 0;
    int is_array_index = 0;
    text_json_status status = json_patch_find_parent_and_token(
        root, path, path_len, &parent, &token, &token_len, &is_array_index
    );
    if (status != TEXT_JSON_OK) {
        if (err) {
            err->code = status;
            err->message = "Invalid path for remove operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return status;
    }

    if (!parent) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Parent not found for remove operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Check if parent is an array - if so, treat token as array index
    if (parent->type == TEXT_JSON_ARRAY) {
        // Remove from array

        // Parse index
        char* token_buf = (char*)malloc(token_len + 1);
        if (!token_buf) {
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Out of memory parsing array index";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_OOM;
        }
        memcpy(token_buf, token, token_len);
        token_buf[token_len] = '\0';

        // Decode escape sequences
        size_t decoded_len;
        char* decoded = (char*)malloc(token_len + 1);
        if (!decoded) {
            free(token_buf);
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Out of memory decoding array index";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_OOM;
        }
        size_t out_pos = 0;
        for (size_t i = 0; i < token_len; i++) {
            // Defensive bounds check (should never trigger, but prevents overflow)
            if (out_pos >= token_len) {
                free(decoded);
                free(token_buf);
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Buffer overflow in token decoding";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            if (token_buf[i] == '~' && i + 1 < token_len) {
                if (token_buf[i + 1] == '0') {
                    decoded[out_pos++] = '~';
                    i++;
                } else if (token_buf[i + 1] == '1') {
                    decoded[out_pos++] = '/';
                    i++;
                } else {
                    decoded[out_pos++] = token_buf[i];
                }
            } else {
                decoded[out_pos++] = token_buf[i];
            }
        }
        decoded_len = out_pos;
        decoded[decoded_len] = '\0';

        char* endptr;
        unsigned long long parsed = strtoull(decoded, &endptr, 10);
        if (*endptr != '\0' || parsed > SIZE_MAX) {
            free(decoded);
            free(token_buf);
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Invalid array index";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }
        size_t idx = (size_t)parsed;

        free(decoded);
        free(token_buf);

        if (idx >= parent->as.array.count) {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Array index out of bounds";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }

        status = text_json_array_remove(parent, idx);
        if (status != TEXT_JSON_OK) {
            if (err) {
                err->code = status;
                err->message = "Failed to remove from array";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return status;
        }
    } else {
        // Remove from object
        if (parent->type != TEXT_JSON_OBJECT) {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Path points to object key but parent is not an object";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }

        // Decode token (handle escape sequences)
        char* decoded_key = (char*)malloc(token_len + 1);
        if (!decoded_key) {
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Out of memory decoding object key";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_OOM;
        }

        size_t out_pos = 0;
        for (size_t i = 0; i < token_len; i++) {
            // Defensive bounds check (should never trigger, but prevents overflow)
            if (out_pos >= token_len) {
                free(decoded_key);
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Buffer overflow in key decoding";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
            if (token[i] == '~' && i + 1 < token_len) {
                if (token[i + 1] == '0') {
                    decoded_key[out_pos++] = '~';
                    i++;
                } else if (token[i + 1] == '1') {
                    decoded_key[out_pos++] = '/';
                    i++;
                } else {
                    decoded_key[out_pos++] = token[i];
                }
            } else {
                decoded_key[out_pos++] = token[i];
            }
        }
        size_t decoded_key_len = out_pos;

        status = text_json_object_remove(parent, decoded_key, decoded_key_len);
        free(decoded_key);

        if (status != TEXT_JSON_OK) {
            if (err) {
                err->code = status;
                err->message = "Failed to remove from object";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return status;
        }
    }

    return TEXT_JSON_OK;
}

// Implement replace operation
static text_json_status json_patch_replace(
    text_json_value* root,
    const char* path,
    size_t path_len,
    const text_json_value* value,
    text_json_error* err
) {
    // Replace is semantically equivalent to remove + add
    // But we need to ensure the target exists first
    const text_json_value* target = text_json_pointer_get(root, path, path_len);
    if (!target) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Target path does not exist for replace operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Remove then add
    text_json_status status = json_patch_remove(root, path, path_len, err);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    return json_patch_add(root, path, path_len, value, err);
}

// Implement move operation
static text_json_status json_patch_move(
    text_json_value* root,
    const char* from,
    size_t from_len,
    const char* path,
    size_t path_len,
    text_json_error* err
) {
    if (!root || !from || !path) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments for move operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Check that "from" is not a proper prefix of "path"
    if (json_pointer_is_prefix(from, from_len, path, path_len)) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Cannot move a location into one of its own descendants";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Get value from "from" location
    const text_json_value* from_value = text_json_pointer_get(root, from, from_len);
    if (!from_value) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Source path does not exist for move operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Clone the value (it will be added to the target's context)
    // First, find the target parent to get its context
    text_json_value* target_parent = NULL;
    const char* token = NULL;
    size_t token_len = 0;
    int is_array_index = 0;
    text_json_status status = json_patch_find_parent_and_token(
        root, path, path_len, &target_parent, &token, &token_len, &is_array_index
    );
    if (status != TEXT_JSON_OK || !target_parent) {
        if (err) {
            err->code = status != TEXT_JSON_OK ? status : TEXT_JSON_E_INVALID;
            err->message = "Invalid target path for move operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return status != TEXT_JSON_OK ? status : TEXT_JSON_E_INVALID;
    }

    json_context* target_ctx = target_parent->ctx;
    text_json_value* cloned_value = json_value_clone(from_value, target_ctx);
    if (!cloned_value) {
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory cloning value for move operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_OOM;
    }

    // Add to target location
    status = json_patch_add(root, path, path_len, cloned_value, err);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    // Remove from source location
    return json_patch_remove(root, from, from_len, err);
}

// Implement copy operation
static text_json_status json_patch_copy(
    text_json_value* root,
    const char* from,
    size_t from_len,
    const char* path,
    size_t path_len,
    text_json_error* err
) {
    if (!root || !from || !path) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments for copy operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Get value from "from" location
    const text_json_value* from_value = text_json_pointer_get(root, from, from_len);
    if (!from_value) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Source path does not exist for copy operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Add to target location (add will clone the value)
    return json_patch_add(root, path, path_len, from_value, err);
}

// Implement test operation
static text_json_status json_patch_test(
    text_json_value* root,
    const char* path,
    size_t path_len,
    const text_json_value* expected_value,
    text_json_error* err
) {
    if (!root || !path || !expected_value) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments for test operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Get value at path
    const text_json_value* actual_value = text_json_pointer_get(root, path, path_len);
    if (!actual_value) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Path does not exist for test operation";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Compare values
    if (!json_value_equal(actual_value, expected_value)) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Test operation failed: values are not equal";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    return TEXT_JSON_OK;
}

// Helper function to deep copy content from source to destination
// This preserves the destination's context but replaces its content
// Used for atomic patch application: apply to clone, then copy back to original
// Note: This function allows type changes because merge patch can change the
// target's type (e.g., object -> null, string -> object)
static text_json_status json_value_copy_content(
    text_json_value* dst,
    const text_json_value* src
) {
    if (!dst || !src) {
        return TEXT_JSON_E_INVALID;
    }

    // If types differ, we need to change the type first
    // The old content will remain in memory but won't be accessible
    // since we're changing the type. It will be freed when the context is freed.
    if (dst->type != src->type) {
        dst->type = src->type;
        // Initialize the union to a safe state based on new type
        // This prevents accessing old type's data
        switch (src->type) {
            case TEXT_JSON_NULL:
                // Nothing to initialize
                break;
            case TEXT_JSON_BOOL:
                dst->as.boolean = 0;
                break;
            case TEXT_JSON_STRING:
                dst->as.string.data = NULL;
                dst->as.string.len = 0;
                break;
            case TEXT_JSON_NUMBER:
                dst->as.number.lexeme = NULL;
                dst->as.number.lexeme_len = 0;
                dst->as.number.i64 = 0;
                dst->as.number.u64 = 0;
                dst->as.number.dbl = 0.0;
                dst->as.number.has_i64 = 0;
                dst->as.number.has_u64 = 0;
                dst->as.number.has_dbl = 0;
                break;
            case TEXT_JSON_ARRAY:
                dst->as.array.elems = NULL;
                dst->as.array.count = 0;
                dst->as.array.capacity = 0;
                break;
            case TEXT_JSON_OBJECT:
                dst->as.object.pairs = NULL;
                dst->as.object.count = 0;
                dst->as.object.capacity = 0;
                break;
        }
    }

    json_context* dst_ctx = dst->ctx;
    if (!dst_ctx) {
        return TEXT_JSON_E_INVALID;
    }

    switch (src->type) {
        case TEXT_JSON_NULL:
            // Nothing to copy
            break;

        case TEXT_JSON_BOOL:
            dst->as.boolean = src->as.boolean;
            break;

        case TEXT_JSON_STRING: {
            // Free old string data (if any) - it's in the arena, will be freed with context
            // Allocate and copy new string data
            // Check for integer overflow in len + 1
            if (src->as.string.len > SIZE_MAX - 1) {
                return TEXT_JSON_E_OOM;  // Overflow
            }
            char* str_data = (char*)json_arena_alloc_for_context(dst_ctx, src->as.string.len + 1, 1);
            if (!str_data) {
                return TEXT_JSON_E_OOM;
            }
            memcpy(str_data, src->as.string.data, src->as.string.len);
            str_data[src->as.string.len] = '\0';
            dst->as.string.data = str_data;
            dst->as.string.len = src->as.string.len;
            break;
        }

        case TEXT_JSON_NUMBER: {
            // Allocate and copy lexeme
            // Check for integer overflow in lexeme_len + 1
            if (src->as.number.lexeme_len > SIZE_MAX - 1) {
                return TEXT_JSON_E_OOM;  // Overflow
            }
            char* lexeme = (char*)json_arena_alloc_for_context(dst_ctx, src->as.number.lexeme_len + 1, 1);
            if (!lexeme) {
                return TEXT_JSON_E_OOM;
            }
            memcpy(lexeme, src->as.number.lexeme, src->as.number.lexeme_len);
            lexeme[src->as.number.lexeme_len] = '\0';
            dst->as.number.lexeme = lexeme;
            dst->as.number.lexeme_len = src->as.number.lexeme_len;
            dst->as.number.i64 = src->as.number.i64;
            dst->as.number.u64 = src->as.number.u64;
            dst->as.number.dbl = src->as.number.dbl;
            dst->as.number.has_i64 = src->as.number.has_i64;
            dst->as.number.has_u64 = src->as.number.has_u64;
            dst->as.number.has_dbl = src->as.number.has_dbl;
            break;
        }

        case TEXT_JSON_ARRAY: {
            // Free old array elements (they're in the arena, will be freed with context)
            // Allocate new array
            dst->as.array.count = 0;
            dst->as.array.capacity = src->as.array.count;
            if (dst->as.array.capacity > 0) {
                // Check for integer overflow in multiplication
                size_t elem_size = sizeof(text_json_value*);
                if (dst->as.array.capacity > SIZE_MAX / elem_size) {
                    return TEXT_JSON_E_OOM;  // Overflow
                }
                dst->as.array.elems = (text_json_value**)json_arena_alloc_for_context(
                    dst_ctx,
                    dst->as.array.capacity * elem_size,
                    sizeof(text_json_value*)
                );
                if (!dst->as.array.elems) {
                    return TEXT_JSON_E_OOM;
                }
            } else {
                dst->as.array.elems = NULL;
            }

            // Clone each element
            for (size_t i = 0; i < src->as.array.count; i++) {
                text_json_value* cloned_elem = json_value_clone(src->as.array.elems[i], dst_ctx);
                if (!cloned_elem) {
                    return TEXT_JSON_E_OOM;
                }
                dst->as.array.elems[i] = cloned_elem;
                dst->as.array.count++;
            }
            break;
        }

        case TEXT_JSON_OBJECT: {
            // Free old object pairs (they're in the arena, will be freed with context)
            // Allocate new pairs array
            dst->as.object.count = 0;
            dst->as.object.capacity = src->as.object.count;
            if (dst->as.object.capacity > 0) {
                size_t pair_size = sizeof(*(src->as.object.pairs));
                // Check for integer overflow in multiplication
                if (dst->as.object.capacity > SIZE_MAX / pair_size) {
                    return TEXT_JSON_E_OOM;  // Overflow
                }
                void* new_pairs_ptr = json_arena_alloc_for_context(
                    dst_ctx,
                    dst->as.object.capacity * pair_size,
                    sizeof(void*)
                );
                if (!new_pairs_ptr) {
                    return TEXT_JSON_E_OOM;
                }
                dst->as.object.pairs = (void*)new_pairs_ptr;
            } else {
                dst->as.object.pairs = NULL;
            }

            // Clone each key-value pair
            for (size_t i = 0; i < src->as.object.count; i++) {
                // Allocate and copy key
                // Check for integer overflow in key_len + 1
                if (src->as.object.pairs[i].key_len > SIZE_MAX - 1) {
                    return TEXT_JSON_E_OOM;  // Overflow
                }
                char* key = (char*)json_arena_alloc_for_context(dst_ctx, src->as.object.pairs[i].key_len + 1, 1);
                if (!key) {
                    return TEXT_JSON_E_OOM;
                }
                memcpy(key, src->as.object.pairs[i].key, src->as.object.pairs[i].key_len);
                key[src->as.object.pairs[i].key_len] = '\0';

                // Clone value
                text_json_value* cloned_val = json_value_clone(src->as.object.pairs[i].value, dst_ctx);
                if (!cloned_val) {
                    return TEXT_JSON_E_OOM;
                }

                dst->as.object.pairs[i].key = key;
                dst->as.object.pairs[i].key_len = src->as.object.pairs[i].key_len;
                dst->as.object.pairs[i].value = cloned_val;
                dst->as.object.count++;
            }
            break;
        }
    }

    return TEXT_JSON_OK;
}

// Main patch apply function
// Implements true atomicity: applies all operations to a clone,
// then replaces the original only if all operations succeed
TEXT_API text_json_status text_json_patch_apply(
    text_json_value* root,
    const text_json_value* patch_array,
    text_json_error* err
) {
    if (!root || !patch_array) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments: root and patch_array must not be NULL";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    if (patch_array->type != TEXT_JSON_ARRAY) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Patch must be a JSON array";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // For atomicity: clone the root, apply operations to the clone,
    // then copy the clone's content back to the original only if all succeed
    json_context* clone_ctx = json_context_new();
    if (!clone_ctx) {
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory creating clone for atomic patch";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_OOM;
    }

    text_json_value* clone = json_value_clone(root, clone_ctx);
    if (!clone) {
        json_context_free(clone_ctx);
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory cloning root for atomic patch";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_OOM;
    }

    // Process each operation in order on the clone
    for (size_t i = 0; i < patch_array->as.array.count; i++) {
        const text_json_value* op = patch_array->as.array.elems[i];
        if (!op || op->type != TEXT_JSON_OBJECT) {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Patch operation must be an object";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            text_json_free(clone);
            return TEXT_JSON_E_INVALID;
        }

        // Get operation type
        const char* op_str = NULL;
        size_t op_len = 0;
        text_json_status status = json_patch_get_op(op, &op_str, &op_len);
        if (status != TEXT_JSON_OK) {
            if (err) {
                err->code = status;
                err->message = "Patch operation missing 'op' field";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            text_json_free(clone);
            return status;
        }

        // Get path
        const char* path_str = NULL;
        size_t path_len = 0;
        status = json_patch_get_path(op, &path_str, &path_len);
        if (status != TEXT_JSON_OK) {
            if (err) {
                err->code = status;
                err->message = "Patch operation missing 'path' field";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            text_json_free(clone);
            return status;
        }

        // Execute operation based on type
        if (op_len == 3 && memcmp(op_str, "add", 3) == 0) {
            const text_json_value* value = json_patch_get_value(op);
            if (!value) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Add operation missing 'value' field";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                text_json_free(clone);
                return TEXT_JSON_E_INVALID;
            }
            status = json_patch_add(clone, path_str, path_len, value, err);
            if (status != TEXT_JSON_OK) {
                // Operation failed - free clone and return error (atomicity)
                text_json_free(clone);
                return status;
            }
        } else if (op_len == 6 && memcmp(op_str, "remove", 6) == 0) {
            status = json_patch_remove(clone, path_str, path_len, err);
            if (status != TEXT_JSON_OK) {
                // Operation failed - free clone and return error (atomicity)
                text_json_free(clone);
                return status;
            }
        } else if (op_len == 7 && memcmp(op_str, "replace", 7) == 0) {
            const text_json_value* value = json_patch_get_value(op);
            if (!value) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Replace operation missing 'value' field";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                text_json_free(clone);
                return TEXT_JSON_E_INVALID;
            }
            status = json_patch_replace(clone, path_str, path_len, value, err);
            if (status != TEXT_JSON_OK) {
                // Operation failed - free clone and return error (atomicity)
                text_json_free(clone);
                return status;
            }
        } else if (op_len == 4 && memcmp(op_str, "move", 4) == 0) {
            const char* from_str = NULL;
            size_t from_len = 0;
            status = json_patch_get_from(op, &from_str, &from_len);
            if (status != TEXT_JSON_OK) {
                if (err) {
                    err->code = status;
                    err->message = "Move operation missing 'from' field";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                text_json_free(clone);
                return status;
            }
            status = json_patch_move(clone, from_str, from_len, path_str, path_len, err);
            if (status != TEXT_JSON_OK) {
                // Operation failed - free clone and return error (atomicity)
                text_json_free(clone);
                return status;
            }
        } else if (op_len == 4 && memcmp(op_str, "copy", 4) == 0) {
            const char* from_str = NULL;
            size_t from_len = 0;
            status = json_patch_get_from(op, &from_str, &from_len);
            if (status != TEXT_JSON_OK) {
                if (err) {
                    err->code = status;
                    err->message = "Copy operation missing 'from' field";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                text_json_free(clone);
                return status;
            }
            status = json_patch_copy(clone, from_str, from_len, path_str, path_len, err);
            if (status != TEXT_JSON_OK) {
                // Operation failed - free clone and return error (atomicity)
                text_json_free(clone);
                return status;
            }
        } else if (op_len == 4 && memcmp(op_str, "test", 4) == 0) {
            const text_json_value* value = json_patch_get_value(op);
            if (!value) {
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Test operation missing 'value' field";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                text_json_free(clone);
                return TEXT_JSON_E_INVALID;
            }
            status = json_patch_test(clone, path_str, path_len, value, err);
            if (status != TEXT_JSON_OK) {
                // Operation failed - free clone and return error (atomicity)
                text_json_free(clone);
                return status;
            }
        } else {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Unknown patch operation type";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            text_json_free(clone);
            return TEXT_JSON_E_INVALID;
        }
    }

    // All operations succeeded - copy clone's content back to original
    // This preserves the original's context but replaces its content
    text_json_status status = json_value_copy_content(root, clone);
    if (status != TEXT_JSON_OK) {
        // Copy failed - free clone and return error
        text_json_free(clone);
        if (err) {
            err->code = status;
            err->message = "Failed to copy patch results back to original";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return status;
    }

    // Free the clone (all its content has been copied to original)
    text_json_free(clone);

    return TEXT_JSON_OK;
}

// Recursive helper function for JSON Merge Patch
// Implements the MergePatch(Target, Patch) algorithm from RFC 7386
static text_json_status json_merge_patch_recursive(
    text_json_value* target,
    const text_json_value* patch,
    text_json_error* err
) {
    if (!target || !patch) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments: target and patch must not be NULL";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // If patch is not an object, replace target entirely
    if (patch->type != TEXT_JSON_OBJECT) {
        // Clone patch content directly into target
        json_context* target_ctx = target->ctx;
        if (!target_ctx) {
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Target value has no context";
                err->offset = 0;
                err->line = 0;
                err->col = 0;
            }
            return TEXT_JSON_E_INVALID;
        }

        // Change target's type first
        target->type = patch->type;

        // Clone content based on type (similar to json_value_clone but into existing target)
        switch (patch->type) {
            case TEXT_JSON_NULL:
                // Nothing to copy
                break;

            case TEXT_JSON_BOOL:
                target->as.boolean = patch->as.boolean;
                break;

            case TEXT_JSON_STRING: {
                // Allocate and copy string data
                // Check for integer overflow in len + 1
                if (patch->as.string.len > SIZE_MAX - 1) {
                    if (err) {
                        err->code = TEXT_JSON_E_OOM;
                        err->message = "String length overflow";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_OOM;
                }
                char* str_data = (char*)json_arena_alloc_for_context(target_ctx, patch->as.string.len + 1, 1);
                if (!str_data) {
                    if (err) {
                        err->code = TEXT_JSON_E_OOM;
                        err->message = "Out of memory cloning string";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_OOM;
                }
                memcpy(str_data, patch->as.string.data, patch->as.string.len);
                str_data[patch->as.string.len] = '\0';
                target->as.string.data = str_data;
                target->as.string.len = patch->as.string.len;
                break;
            }

            case TEXT_JSON_NUMBER: {
                // Allocate and copy lexeme
                // Check for integer overflow in lexeme_len + 1
                if (patch->as.number.lexeme_len > SIZE_MAX - 1) {
                    if (err) {
                        err->code = TEXT_JSON_E_OOM;
                        err->message = "Number lexeme length overflow";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_OOM;
                }
                char* lexeme = (char*)json_arena_alloc_for_context(target_ctx, patch->as.number.lexeme_len + 1, 1);
                if (!lexeme) {
                    if (err) {
                        err->code = TEXT_JSON_E_OOM;
                        err->message = "Out of memory cloning number lexeme";
                        err->offset = 0;
                        err->line = 0;
                        err->col = 0;
                    }
                    return TEXT_JSON_E_OOM;
                }
                memcpy(lexeme, patch->as.number.lexeme, patch->as.number.lexeme_len);
                lexeme[patch->as.number.lexeme_len] = '\0';
                target->as.number.lexeme = lexeme;
                target->as.number.lexeme_len = patch->as.number.lexeme_len;
                target->as.number.i64 = patch->as.number.i64;
                target->as.number.u64 = patch->as.number.u64;
                target->as.number.dbl = patch->as.number.dbl;
                target->as.number.has_i64 = patch->as.number.has_i64;
                target->as.number.has_u64 = patch->as.number.has_u64;
                target->as.number.has_dbl = patch->as.number.has_dbl;
                break;
            }

            case TEXT_JSON_ARRAY: {
                // Initialize array
                target->as.array.count = 0;
                target->as.array.capacity = patch->as.array.count;
                if (target->as.array.capacity > 0) {
                    size_t elem_size = sizeof(text_json_value*);
                    if (target->as.array.capacity > SIZE_MAX / elem_size) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Array capacity overflow";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    target->as.array.elems = (text_json_value**)json_arena_alloc_for_context(
                        target_ctx,
                        target->as.array.capacity * elem_size,
                        sizeof(text_json_value*)
                    );
                    if (!target->as.array.elems) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory cloning array";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                } else {
                    target->as.array.elems = NULL;
                }

                // Clone each element
                for (size_t i = 0; i < patch->as.array.count; i++) {
                    text_json_value* cloned_elem = json_value_clone(patch->as.array.elems[i], target_ctx);
                    if (!cloned_elem) {
                        if (err) {
                            err->code = TEXT_JSON_E_OOM;
                            err->message = "Out of memory cloning array element";
                            err->offset = 0;
                            err->line = 0;
                            err->col = 0;
                        }
                        return TEXT_JSON_E_OOM;
                    }
                    target->as.array.elems[i] = cloned_elem;
                    target->as.array.count++;
                }
                break;
            }

            case TEXT_JSON_OBJECT: {
                // This case shouldn't happen (we check patch->type != TEXT_JSON_OBJECT above)
                // But handle it defensively
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Internal error: object patch in non-object branch";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_INVALID;
            }
        }

        return TEXT_JSON_OK;
    }

    // Patch is an object - merge recursively
    // If target is not an object, convert it to an empty object first
    // According to RFC 7386: "if Target is not an Object, Target = {}"
    if (target->type != TEXT_JSON_OBJECT) {
        // Convert target to empty object
        // The old content will remain in memory but won't be accessible
        // since we're changing the type. It will be freed when the context is freed.
        target->type = TEXT_JSON_OBJECT;
        target->as.object.count = 0;
        target->as.object.capacity = 0;
        target->as.object.pairs = NULL;
    }

    // Now target is guaranteed to be an object
    // Verify target is actually an object (defensive check)
    if (target->type != TEXT_JSON_OBJECT) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Internal error: target type mismatch in merge";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // Process each member in the patch
    // Defensive check: pairs should not be NULL when count > 0
    if (patch->as.object.count > 0 && !patch->as.object.pairs) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid patch object: count > 0 but pairs is NULL";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    for (size_t i = 0; i < patch->as.object.count; i++) {
        const char* key = patch->as.object.pairs[i].key;
        size_t key_len = patch->as.object.pairs[i].key_len;
        const text_json_value* patch_value = patch->as.object.pairs[i].value;

        if (!patch_value) {
            // Skip null values (shouldn't happen, but be defensive)
            continue;
        }

        // If patch value is null, remove the key from target
        if (patch_value->type == TEXT_JSON_NULL) {
            // Remove key from target (if it exists)
            // Note: text_json_object_remove returns error if key doesn't exist,
            // but according to RFC 7386, removal is idempotent, so we ignore the error
            // We don't check the return value because removal is idempotent per RFC 7386
            // Only attempt removal if target is actually an object (defensive check)
            if (target->type == TEXT_JSON_OBJECT) {
                (void)text_json_object_remove(target, key, key_len);
            }
            // Continue to next key (removal is idempotent)
            continue;
        }

        // Patch value is non-null - recursively merge
        // Find the key in target's pairs array to get mutable access
        // We need to re-find the key after each operation because the pairs array
        // might be reallocated during recursive operations
        text_json_value* target_value_mut = NULL;

        // Search for the key - we need to do this each time because the pairs array
        // might have been reallocated in a previous iteration
        // Defensive check: pairs should not be NULL when count > 0
        if (target->as.object.count > 0 && target->as.object.pairs) {
            for (size_t j = 0; j < target->as.object.count; j++) {
                // Defensive check: key should not be NULL when key_len > 0
                if (target->as.object.pairs[j].key_len == key_len) {
                    if (key_len == 0 ||
                        (target->as.object.pairs[j].key && key &&
                         memcmp(target->as.object.pairs[j].key, key, key_len) == 0)) {
                        target_value_mut = target->as.object.pairs[j].value;
                        break;
                    }
                }
            }
        }

        if (target_value_mut) {
            // Key exists - recursively merge patch_value into target_value
            // Important: target_value_mut points to pairs[j].value
            // Even if the pairs array shifts during recursive operations, this pointer
            // should remain valid as long as we don't free the value itself
            text_json_status status = json_merge_patch_recursive(
                target_value_mut,
                patch_value,
                err
            );
            if (status != TEXT_JSON_OK) {
                return status;
            }
        } else {
            // Key doesn't exist - add it (clone patch_value into target's context)
            json_context* target_ctx = target->ctx;
            text_json_value* cloned_value = json_value_clone(patch_value, target_ctx);
            if (!cloned_value) {
                if (err) {
                    err->code = TEXT_JSON_E_OOM;
                    err->message = "Out of memory cloning patch value";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return TEXT_JSON_E_OOM;
            }

            // Add to target
            text_json_status status = text_json_object_put(target, key, key_len, cloned_value);
            if (status != TEXT_JSON_OK) {
                text_json_free(cloned_value);
                if (err) {
                    err->code = status;
                    err->message = "Failed to add key to target object";
                    err->offset = 0;
                    err->line = 0;
                    err->col = 0;
                }
                return status;
            }
        }
    }

    return TEXT_JSON_OK;
}

// Main JSON Merge Patch function
// Implements true atomicity: applies merge to a clone,
// then replaces the original only if all operations succeed
TEXT_API text_json_status text_json_merge_patch(
    text_json_value* target,
    const text_json_value* patch,
    text_json_error* err
) {
    if (!target || !patch) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Invalid arguments: target and patch must not be NULL";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_INVALID;
    }

    // For atomicity: clone the target, apply merge to the clone,
    // then copy the clone's content back to the original only if all succeed
    json_context* clone_ctx = json_context_new();
    if (!clone_ctx) {
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory creating clone for atomic merge patch";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_OOM;
    }

    text_json_value* clone = json_value_clone(target, clone_ctx);
    if (!clone) {
        json_context_free(clone_ctx);
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Out of memory cloning target for atomic merge patch";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return TEXT_JSON_E_OOM;
    }

    // Apply merge to clone
    text_json_status status = json_merge_patch_recursive(clone, patch, err);
    if (status != TEXT_JSON_OK) {
        // Merge failed - free clone and return error (atomicity)
        text_json_free(clone);
        return status;
    }

    // All operations succeeded - copy clone's content back to original
    // This preserves the original's context but replaces its content
    status = json_value_copy_content(target, clone);
    if (status != TEXT_JSON_OK) {
        // Copy failed - free clone and return error
        text_json_free(clone);
        if (err) {
            err->code = status;
            err->message = "Failed to copy merge patch results back to original";
            err->offset = 0;
            err->line = 0;
            err->col = 0;
        }
        return status;
    }

    // Free the clone (all its content has been copied to original)
    text_json_free(clone);

    return TEXT_JSON_OK;
}
