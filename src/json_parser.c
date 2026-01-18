/**
 * @file json_parser.c
 * @brief Recursive descent parser for JSON
 *
 * Implements a recursive descent parser that builds a DOM tree from JSON input.
 * Enforces strict JSON grammar, depth limits, and size limits.
 */

#include "json_internal.h"
#include <text/json.h>
#include <text/json_dom.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

// Forward declaration (json_context is defined in json_dom.c)
typedef struct json_context json_context;

// Default limits (used when opts->max_* is 0)
#define JSON_DEFAULT_MAX_DEPTH 256
#define JSON_DEFAULT_MAX_STRING_BYTES (16 * 1024 * 1024)  // 16MB
#define JSON_DEFAULT_MAX_CONTAINER_ELEMS (1024 * 1024)     // 1M
#define JSON_DEFAULT_MAX_TOTAL_BYTES (64 * 1024 * 1024)    // 64MB

// Parser state structure
typedef struct {
    json_lexer lexer;                    ///< Lexer for tokenization
    const text_json_parse_options* opts; ///< Parse options
    size_t depth;                        ///< Current nesting depth
    size_t total_bytes_consumed;         ///< Total bytes processed
    text_json_error* error_out;          ///< Error output structure
} json_parser;

// Helper to set error and return status
static text_json_status json_parser_set_error(
    json_parser* parser,
    text_json_status code,
    const char* message,
    json_position pos
) {
    if (parser->error_out) {
        parser->error_out->code = code;
        parser->error_out->message = message;
        parser->error_out->offset = pos.offset;
        parser->error_out->line = pos.line;
        parser->error_out->col = pos.col;
    }
    return code;
}

// Get effective limit value (use default if 0)
static size_t json_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Check depth limit
static text_json_status json_parser_check_depth(json_parser* parser) {
    size_t max_depth = json_get_limit(
        parser->opts ? parser->opts->max_depth : 0,
        JSON_DEFAULT_MAX_DEPTH
    );
    if (parser->depth >= max_depth) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_DEPTH,
            "Maximum nesting depth exceeded",
            parser->lexer.pos
        );
    }
    return TEXT_JSON_OK;
}

// Check total bytes limit
static text_json_status json_parser_check_total_bytes(json_parser* parser, size_t additional) {
    size_t max_total = json_get_limit(
        parser->opts ? parser->opts->max_total_bytes : 0,
        JSON_DEFAULT_MAX_TOTAL_BYTES
    );
    // Check for underflow: if additional > max_total, subtraction would underflow
    if (additional > max_total) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_LIMIT,
            "Maximum total input size exceeded",
            parser->lexer.pos
        );
    }
    if (parser->total_bytes_consumed > max_total - additional) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_LIMIT,
            "Maximum total input size exceeded",
            parser->lexer.pos
        );
    }
    return TEXT_JSON_OK;
}

// Check string size limit
static text_json_status json_parser_check_string_size(json_parser* parser, size_t string_len) {
    size_t max_string = json_get_limit(
        parser->opts ? parser->opts->max_string_bytes : 0,
        JSON_DEFAULT_MAX_STRING_BYTES
    );
    if (string_len > max_string) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_LIMIT,
            "Maximum string size exceeded",
            parser->lexer.pos
        );
    }
    return TEXT_JSON_OK;
}

// Check container element limit
static text_json_status json_parser_check_container_elems(json_parser* parser, size_t current_count) {
    size_t max_elems = json_get_limit(
        parser->opts ? parser->opts->max_container_elems : 0,
        JSON_DEFAULT_MAX_CONTAINER_ELEMS
    );
    if (current_count >= max_elems) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_LIMIT,
            "Maximum container element count exceeded",
            parser->lexer.pos
        );
    }
    return TEXT_JSON_OK;
}

// Forward declaration
static text_json_status json_parse_value(json_parser* parser, text_json_value** out, json_context* ctx);

// Parse a JSON array
static text_json_status json_parse_array(json_parser* parser, text_json_value** out, json_context* ctx) {
    // Check depth limit
    text_json_status status = json_parser_check_depth(parser);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    parser->depth++;
    text_json_status result = TEXT_JSON_OK;

    // Create array value
    text_json_value* array;
    if (ctx) {
        // Use existing context
        array = json_value_new_with_existing_context(TEXT_JSON_ARRAY, ctx);
        if (!array) {
            parser->depth--;
            return json_parser_set_error(
                parser,
                TEXT_JSON_E_OOM,
                "Failed to allocate array",
                parser->lexer.pos
            );
        }
        // Initialize empty array
        array->as.array.elems = NULL;
        array->as.array.count = 0;
        array->as.array.capacity = 0;
    } else {
        // Root array - create with new context
        array = text_json_new_array();
        if (!array) {
            parser->depth--;
            return json_parser_set_error(
                parser,
                TEXT_JSON_E_OOM,
                "Failed to allocate array",
                parser->lexer.pos
            );
        }
        ctx = array->ctx;  // Use the context from the created array
    }

    // Get opening bracket token (already consumed by caller)
    json_token token;
    int first = 1;

    while (1) {
        // Get next token
        status = json_lexer_next(&parser->lexer, &token);
        if (status != TEXT_JSON_OK) {
            result = status;
            json_token_cleanup(&token);
            break;
        }

        // Check for closing bracket
        if (token.type == JSON_TOKEN_RBRACKET) {
            json_token_cleanup(&token);
            break;
        }

        // Check for trailing comma (if not first element and not allowed)
        if (!first && token.type == JSON_TOKEN_COMMA) {
            if (!parser->opts || !parser->opts->allow_trailing_commas) {
                result = json_parser_set_error(
                    parser,
                    TEXT_JSON_E_BAD_TOKEN,
                    "Trailing comma not allowed",
                    token.pos
                );
                json_token_cleanup(&token);
                break;
            }
            // Trailing comma - consume it and check for closing bracket
            json_token_cleanup(&token);
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }
            if (token.type == JSON_TOKEN_RBRACKET) {
                json_token_cleanup(&token);
                break;
            }
            // Not a closing bracket after trailing comma - error
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Expected value or closing bracket after comma",
                token.pos
            );
            json_token_cleanup(&token);
            break;
        }

        // Check for comma between elements
        if (!first) {
            if (token.type != JSON_TOKEN_COMMA) {
                result = json_parser_set_error(
                    parser,
                    TEXT_JSON_E_BAD_TOKEN,
                    "Expected comma between array elements",
                    token.pos
                );
                json_token_cleanup(&token);
                break;
            }
            json_token_cleanup(&token);
            // Get value token
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }
        }

        // Check container element limit
        status = json_parser_check_container_elems(parser, array->as.array.count);
        if (status != TEXT_JSON_OK) {
            result = status;
            json_token_cleanup(&token);
            break;
        }

        // Parse value (use array's context)
        text_json_value* element = NULL;
        status = json_parse_value(parser, &element, array->ctx);
        if (status != TEXT_JSON_OK) {
            result = status;
            json_token_cleanup(&token);
            break;
        }

        // Add element to array
        status = json_array_add_element(array, element);
        if (status != TEXT_JSON_OK) {
            text_json_free(element);
            result = status;
            json_token_cleanup(&token);
            break;
        }

        first = 0;
        json_token_cleanup(&token);
    }

    parser->depth--;

    if (result != TEXT_JSON_OK) {
        text_json_free(array);
        return result;
    }

    *out = array;
    return TEXT_JSON_OK;
}

// Parse a JSON object
static text_json_status json_parse_object(json_parser* parser, text_json_value** out, json_context* ctx) {
    // Check depth limit
    text_json_status status = json_parser_check_depth(parser);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    parser->depth++;
    text_json_status result = TEXT_JSON_OK;

    // Create object value
    text_json_value* object;
    if (ctx) {
        // Use existing context
        object = json_value_new_with_existing_context(TEXT_JSON_OBJECT, ctx);
        if (!object) {
            parser->depth--;
            return json_parser_set_error(
                parser,
                TEXT_JSON_E_OOM,
                "Failed to allocate object",
                parser->lexer.pos
            );
        }
        // Initialize empty object
        object->as.object.pairs = NULL;
        object->as.object.count = 0;
        object->as.object.capacity = 0;
    } else {
        // Root object - create with new context
        object = text_json_new_object();
        if (!object) {
            parser->depth--;
            return json_parser_set_error(
                parser,
                TEXT_JSON_E_OOM,
                "Failed to allocate object",
                parser->lexer.pos
            );
        }
        ctx = object->ctx;  // Use the context from the created object
    }

    // Get opening brace token (already consumed by caller)
    json_token token;
    int first = 1;

    while (1) {
        // Get next token
        status = json_lexer_next(&parser->lexer, &token);
        if (status != TEXT_JSON_OK) {
            result = status;
            json_token_cleanup(&token);
            break;
        }

        // Check for closing brace
        if (token.type == JSON_TOKEN_RBRACE) {
            json_token_cleanup(&token);
            break;
        }

        // Check for trailing comma (if not first element and not allowed)
        if (!first && token.type == JSON_TOKEN_COMMA) {
            if (!parser->opts || !parser->opts->allow_trailing_commas) {
                result = json_parser_set_error(
                    parser,
                    TEXT_JSON_E_BAD_TOKEN,
                    "Trailing comma not allowed",
                    token.pos
                );
                json_token_cleanup(&token);
                break;
            }
            // Trailing comma - consume it and check for closing brace
            json_token_cleanup(&token);
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }
            if (token.type == JSON_TOKEN_RBRACE) {
                json_token_cleanup(&token);
                break;
            }
            // Not a closing brace after trailing comma - error
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Expected key or closing brace after comma",
                token.pos
            );
            json_token_cleanup(&token);
            break;
        }

        // Check for comma between pairs
        if (!first) {
            if (token.type != JSON_TOKEN_COMMA) {
                result = json_parser_set_error(
                    parser,
                    TEXT_JSON_E_BAD_TOKEN,
                    "Expected comma between object pairs",
                    token.pos
                );
                json_token_cleanup(&token);
                break;
            }
            json_token_cleanup(&token);
            // Get key token
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }
        }

        // Key must be a string
        if (token.type != JSON_TOKEN_STRING) {
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Object key must be a string",
                token.pos
            );
            json_token_cleanup(&token);
            break;
        }

        // Check string size limit
        status = json_parser_check_string_size(parser, token.data.string.value_len);
        if (status != TEXT_JSON_OK) {
            result = status;
            json_token_cleanup(&token);
            break;
        }

        // Store key and length (must save before cleanup since token.data.string.value is freed)
        const char* key = token.data.string.value;
        size_t key_len = token.data.string.value_len;

        // Allocate temporary copy of key to avoid use-after-free
        // (we'll use it after json_token_cleanup frees the original)
        char* key_copy = NULL;
        if (key_len > 0) {
            // Check for overflow
            if (key_len > SIZE_MAX - 1) {
                result = TEXT_JSON_E_LIMIT;
                json_token_cleanup(&token);
                break;
            }
            key_copy = (char*)malloc(key_len + 1);
            if (!key_copy) {
                result = TEXT_JSON_E_OOM;
                json_token_cleanup(&token);
                break;
            }
            memcpy(key_copy, key, key_len);
            key_copy[key_len] = '\0';
        }

        // Get colon (now safe to cleanup token)
        json_token_cleanup(&token);
        status = json_lexer_next(&parser->lexer, &token);
        if (status != TEXT_JSON_OK) {
            result = status;
            if (key_copy) {
                free(key_copy);
            }
            break;
        }

        if (token.type != JSON_TOKEN_COLON) {
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Expected colon after object key",
                token.pos
            );
            json_token_cleanup(&token);
            if (key_copy) {
                free(key_copy);
            }
            break;
        }

        json_token_cleanup(&token);

        // Check container element limit
        status = json_parser_check_container_elems(parser, object->as.object.count);
        if (status != TEXT_JSON_OK) {
            result = status;
            if (key_copy) {
                free(key_copy);
            }
            break;
        }

        // Parse value (use object's context)
        text_json_value* value = NULL;
        status = json_parse_value(parser, &value, object->ctx);
        if (status != TEXT_JSON_OK) {
            result = status;
            if (key_copy) {
                free(key_copy);
            }
            break;
        }

        // TODO: Implement duplicate key handling policies (ERROR, FIRST_WINS, LAST_WINS, COLLECT)
        // For now, just add the pair (first wins by default since we're adding sequentially)
        // json_object_add_pair copies the key into the arena
        status = json_object_add_pair(object, key_copy ? key_copy : "", key_len, value);
        if (status != TEXT_JSON_OK) {
            text_json_free(value);
            result = status;
            if (key_copy) {
                free(key_copy);
            }
            break;
        }

        // Free temporary key copy (json_object_add_pair copied it to arena)
        if (key_copy) {
            free(key_copy);
        }

        first = 0;
    }

    parser->depth--;

    if (result != TEXT_JSON_OK) {
        text_json_free(object);
        return result;
    }

    *out = object;
    return TEXT_JSON_OK;
}

// Parse a JSON value (recursive entry point)
static text_json_status json_parse_value(json_parser* parser, text_json_value** out, json_context* ctx) {
    json_token token;
    text_json_status status = json_lexer_next(&parser->lexer, &token);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    text_json_status result = TEXT_JSON_OK;
    text_json_value* value = NULL;

    // If ctx is NULL, this is the root value - create it using text_json_new_* to establish context
    // Otherwise, use existing context
    int is_root = (ctx == NULL);

    switch (token.type) {
        case JSON_TOKEN_NULL:
            if (is_root) {
                value = text_json_new_null();
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_NULL, ctx);
            }
            if (!value) {
                result = TEXT_JSON_E_OOM;
            }
            break;

        case JSON_TOKEN_TRUE:
            if (is_root) {
                value = text_json_new_bool(1);
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_BOOL, ctx);
                if (value) {
                    value->as.boolean = 1;
                }
            }
            if (!value) {
                result = TEXT_JSON_E_OOM;
            }
            break;

        case JSON_TOKEN_FALSE:
            if (is_root) {
                value = text_json_new_bool(0);
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_BOOL, ctx);
                if (value) {
                    value->as.boolean = 0;
                }
            }
            if (!value) {
                result = TEXT_JSON_E_OOM;
            }
            break;

        case JSON_TOKEN_STRING: {
            // Check string size limit
            status = json_parser_check_string_size(parser, token.data.string.value_len);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }

            // Create string value
            if (is_root) {
                value = text_json_new_string(token.data.string.value, token.data.string.value_len);
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                }
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_STRING, ctx);
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }

                // Allocate string data in arena
                // Check for overflow in value_len + 1
                if (token.data.string.value_len > SIZE_MAX - 1) {
                    result = TEXT_JSON_E_LIMIT;
                    json_token_cleanup(&token);
                    break;
                }
                char* str_data = (char*)json_arena_alloc_for_context(ctx, token.data.string.value_len + 1, 1);
                if (!str_data) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }

                memcpy(str_data, token.data.string.value, token.data.string.value_len);
                str_data[token.data.string.value_len] = '\0';

                value->as.string.data = str_data;
                value->as.string.len = token.data.string.value_len;
            }

            // Free the lexer-allocated string
            if (value) {
                free((void*)token.data.string.value);
            }
            json_token_cleanup(&token);
            break;
        }

        case JSON_TOKEN_NUMBER: {
            json_number* num = &token.data.number;

            // For root, create with text_json_new_number_from_lexeme then update representations
            // For children, create with existing context
            if (is_root) {
                value = text_json_new_number_from_lexeme(
                    num->lexeme ? num->lexeme : "",
                    num->lexeme_len
                );
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_NUMBER, ctx);
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }

                // Copy lexeme to arena
                if (num->lexeme && num->lexeme_len > 0) {
                    // Check for overflow in lexeme_len + 1
                    if (num->lexeme_len > SIZE_MAX - 1) {
                        result = TEXT_JSON_E_LIMIT;
                        json_token_cleanup(&token);
                        break;
                    }
                    char* lexeme = (char*)json_arena_alloc_for_context(ctx, num->lexeme_len + 1, 1);
                    if (!lexeme) {
                        result = TEXT_JSON_E_OOM;
                        json_token_cleanup(&token);
                        break;
                    }
                    memcpy(lexeme, num->lexeme, num->lexeme_len);
                    lexeme[num->lexeme_len] = '\0';
                    value->as.number.lexeme = lexeme;
                    value->as.number.lexeme_len = num->lexeme_len;
                } else {
                    value->as.number.lexeme = NULL;
                    value->as.number.lexeme_len = 0;
                }
            }

            // Copy number representations (for both root and children)
            if (num->flags & JSON_NUMBER_HAS_I64) {
                value->as.number.i64 = num->i64;
                value->as.number.has_i64 = 1;
            } else {
                value->as.number.has_i64 = 0;
            }
            if (num->flags & JSON_NUMBER_HAS_U64) {
                value->as.number.u64 = num->u64;
                value->as.number.has_u64 = 1;
            } else {
                value->as.number.has_u64 = 0;
            }
            if (num->flags & JSON_NUMBER_HAS_DOUBLE) {
                value->as.number.dbl = num->dbl;
                value->as.number.has_dbl = 1;
            } else {
                value->as.number.has_dbl = 0;
            }

            // Clean up temporary number structure
            json_number_destroy(num);
            json_token_cleanup(&token);
            break;
        }

        case JSON_TOKEN_LBRACKET:
            json_token_cleanup(&token);
            result = json_parse_array(parser, &value, is_root ? NULL : ctx);
            break;

        case JSON_TOKEN_LBRACE:
            json_token_cleanup(&token);
            result = json_parse_object(parser, &value, is_root ? NULL : ctx);
            break;

        case JSON_TOKEN_NAN:
        case JSON_TOKEN_INFINITY:
        case JSON_TOKEN_NEG_INFINITY:
            // Nonfinite numbers - create as number with special lexeme
            // For now, treat as error if not allowed (Task 11 will handle this properly)
            if (!parser->opts || !parser->opts->allow_nonfinite_numbers) {
                result = json_parser_set_error(
                    parser,
                    TEXT_JSON_E_NONFINITE,
                    "Nonfinite numbers not allowed",
                    token.pos
                );
                json_token_cleanup(&token);
                break;
            }
            // TODO: Create number value with NaN/Infinity representation
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_INVALID,
                "Nonfinite number support not yet implemented",
                token.pos
            );
            json_token_cleanup(&token);
            break;

        default:
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Unexpected token",
                token.pos
            );
            json_token_cleanup(&token);
            break;
    }

    if (result != TEXT_JSON_OK) {
        if (value) {
            text_json_free(value);
        }
        return result;
    }

    *out = value;
    return TEXT_JSON_OK;
}

TEXT_API text_json_value* text_json_parse(
    const char* bytes,
    size_t len,
    const text_json_parse_options* opt,
    text_json_error* err
) {
    if (!bytes) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input bytes must not be NULL";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return NULL;
    }

    // Initialize parser state
    json_parser parser = {0};
    parser.opts = opt;
    parser.depth = 0;
    parser.total_bytes_consumed = 0;
    parser.error_out = err;

    // Initialize lexer
    text_json_status status = json_lexer_init(&parser.lexer, bytes, len, opt);
    if (status != TEXT_JSON_OK) {
        if (err) {
            err->code = status;
            err->message = "Failed to initialize lexer";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return NULL;
    }

    // Parse root value (ctx=NULL means it's the root and will create its own context)
    text_json_value* root = NULL;
    status = json_parse_value(&parser, &root, NULL);

    if (status != TEXT_JSON_OK) {
        if (root) {
            text_json_free(root);
        }
        return NULL;
    }

    // Check for trailing garbage
    json_token token;
    status = json_lexer_next(&parser.lexer, &token);
    if (status == TEXT_JSON_OK && token.type != JSON_TOKEN_EOF) {
        text_json_free(root);
        json_token_cleanup(&token);
        if (err) {
            err->code = TEXT_JSON_E_TRAILING_GARBAGE;
            err->message = "Trailing garbage after valid JSON";
            err->offset = token.pos.offset;
            err->line = token.pos.line;
            err->col = token.pos.col;
        }
        return NULL;
    }
    json_token_cleanup(&token);

    return root;
}
