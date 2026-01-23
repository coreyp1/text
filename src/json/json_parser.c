/**
 * @file json_parser.c
 * @brief Recursive descent parser for JSON
 *
 * Implements a recursive descent parser that builds a DOM tree from JSON input.
 * Enforces strict JSON grammar, depth limits, and size limits.
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

// Context window sizes for error snippets
#define JSON_ERROR_CONTEXT_BEFORE 20
#define JSON_ERROR_CONTEXT_AFTER 20


// Parser state structure
typedef struct {
    json_lexer lexer;                    ///< Lexer for tokenization
    const text_json_parse_options* opts; ///< Parse options
    size_t depth;                        ///< Current nesting depth
    size_t total_bytes_consumed;         ///< Total bytes processed
    text_json_error* error_out;          ///< Error output structure
} json_parser;

// Forward declaration
static text_json_status json_parser_set_error_with_tokens(
    json_parser* parser,
    text_json_status code,
    const char* message,
    json_position pos,
    const char* expected_token,
    const char* actual_token
);

// Helper to set error and return status
static text_json_status json_parser_set_error(
    json_parser* parser,
    text_json_status code,
    const char* message,
    json_position pos
) {
    return json_parser_set_error_with_tokens(
        parser, code, message, pos, NULL, NULL
    );
}

// Helper to set error with expected/actual token information
static text_json_status json_parser_set_error_with_tokens(
    json_parser* parser,
    text_json_status code,
    const char* message,
    json_position pos,
    const char* expected_token,
    const char* actual_token
) {
    if (parser->error_out) {
        // Free any existing context snippet
        if (parser->error_out->context_snippet) {
            free(parser->error_out->context_snippet);
            parser->error_out->context_snippet = NULL;
        }

        parser->error_out->code = code;
        parser->error_out->message = message;
        parser->error_out->offset = pos.offset;
        parser->error_out->line = pos.line;
        parser->error_out->col = pos.col;
        parser->error_out->expected_token = expected_token;
        parser->error_out->actual_token = actual_token;
        parser->error_out->context_snippet = NULL;
        parser->error_out->context_snippet_len = 0;
        parser->error_out->caret_offset = 0;

        // Generate context snippet if we have input buffer access
        if (parser->lexer.input && parser->lexer.input_len > 0) {
            char* snippet = NULL;
            size_t snippet_len = 0;
            size_t caret_offset = 0;
            text_json_status snippet_status = json_error_generate_context_snippet(
                parser->lexer.input,
                parser->lexer.input_len,
                pos.offset,
                JSON_ERROR_CONTEXT_BEFORE,
                JSON_ERROR_CONTEXT_AFTER,
                &snippet,
                &snippet_len,
                &caret_offset
            );
            if (snippet_status == TEXT_JSON_OK && snippet) {
                parser->error_out->context_snippet = snippet;
                parser->error_out->context_snippet_len = snippet_len;
                parser->error_out->caret_offset = caret_offset;
            }
        }
    }
    return code;
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
    // Check for overflow in total_bytes_consumed + additional using shared helper
    if (json_check_add_overflow(parser->total_bytes_consumed, additional)) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_LIMIT,
            "Maximum total input size exceeded (overflow)",
            parser->lexer.pos
        );
    }
    // Check for underflow: if additional > max_total, subtraction would underflow
    if (additional > max_total || json_check_sub_underflow(max_total, additional)) {
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

// Forward declarations
static text_json_status json_parse_value(json_parser* parser, text_json_value** out, json_context* ctx);
static text_json_status json_parse_object(json_parser* parser, text_json_value** out, json_context* ctx);
static text_json_status json_parse_array(json_parser* parser, text_json_value** out, json_context* ctx);

// Helper to parse an array element from a token (inline parsing to avoid double token consumption)
// The token is consumed/cleaned up by this function
static text_json_status json_parse_array_element(
    json_parser* parser,
    json_token* token,
    json_context* ctx,
    text_json_value** out
) {
    text_json_value* element = NULL;
    text_json_status result = TEXT_JSON_OK;

    switch (token->type) {
        case JSON_TOKEN_NULL:
            element = json_value_new_with_existing_context(TEXT_JSON_NULL, ctx);
            if (!element) {
                result = TEXT_JSON_E_OOM;
            }
            json_token_cleanup(token);
            break;

        case JSON_TOKEN_TRUE:
            element = json_value_new_with_existing_context(TEXT_JSON_BOOL, ctx);
            if (!element) {
                result = TEXT_JSON_E_OOM;
            } else {
                element->as.boolean = 1;
            }
            json_token_cleanup(token);
            break;

        case JSON_TOKEN_FALSE:
            element = json_value_new_with_existing_context(TEXT_JSON_BOOL, ctx);
            if (!element) {
                result = TEXT_JSON_E_OOM;
            } else {
                element->as.boolean = 0;
            }
            json_token_cleanup(token);
            break;

        case JSON_TOKEN_STRING: {
            element = json_value_new_with_existing_context(TEXT_JSON_STRING, ctx);
            if (!element) {
                result = TEXT_JSON_E_OOM;
                json_token_cleanup(token);
                break;
            }

            // Check if we can use in-situ mode
            // Conditions: in-situ mode enabled, context has input buffer, no escape sequences
            int use_in_situ = 0;
            size_t original_start = 0;
            size_t original_len = 0;
            if (parser->opts && parser->opts->in_situ_mode &&
                ctx->input_buffer && ctx->input_buffer_len > 0 &&
                token->data.string.value_len == token->data.string.original_len) {
                // Verify the original string position is within bounds
                // Check for integer overflow and bounds
                original_start = token->data.string.original_start;
                original_len = token->data.string.original_len;
                if (original_start < ctx->input_buffer_len &&
                    original_len <= ctx->input_buffer_len - original_start) {
                    use_in_situ = 1;
                }
            }

            if (use_in_situ) {
                // Use in-situ mode: reference input buffer directly
                // Safe: original_start and original_len were validated above
                element->as.string.data = (char*)(ctx->input_buffer + original_start);
                element->as.string.len = original_len;
                element->as.string.is_in_situ = 1;
            } else {
                // Copy string data
                size_t str_len = token->data.string.value_len;
                if (str_len > SIZE_MAX - 1) {
                    result = TEXT_JSON_E_LIMIT;
                    text_json_free(element);
                    json_token_cleanup(token);
                    break;
                }
                char* str_data = (char*)json_arena_alloc_for_context(ctx, str_len + 1, 1);
                if (!str_data) {
                    result = TEXT_JSON_E_OOM;
                    text_json_free(element);
                    json_token_cleanup(token);
                    break;
                }
                memcpy(str_data, token->data.string.value, str_len);
                str_data[str_len] = '\0';

                element->as.string.data = str_data;
                element->as.string.len = str_len;
                element->as.string.is_in_situ = 0;
            }

            json_token_cleanup(token);
            break;
        }

        case JSON_TOKEN_NUMBER: {
            json_number* num = &token->data.number;
            element = json_value_new_with_existing_context(TEXT_JSON_NUMBER, ctx);
            if (!element) {
                result = TEXT_JSON_E_OOM;
                json_token_cleanup(token);
                break;
            }

            // Check if we can use in-situ mode for lexeme
            // Conditions: in-situ mode enabled, context has input buffer, lexeme exists
            int use_in_situ = 0;
            size_t number_offset = 0;
            size_t number_len = 0;
            if (parser->opts && parser->opts->in_situ_mode &&
                ctx->input_buffer && ctx->input_buffer_len > 0 &&
                num->lexeme && num->lexeme_len > 0) {
                // Verify the number position is within bounds
                // Check for integer overflow and bounds using shared helpers
                number_offset = token->pos.offset;
                number_len = token->length;
                if (json_check_bounds_offset(number_offset, ctx->input_buffer_len) &&
                    !json_check_add_overflow(number_offset, number_len) &&
                    number_offset + number_len <= ctx->input_buffer_len) {
                    use_in_situ = 1;
                }
            }

            if (use_in_situ && num->lexeme_len > 0) {
                // Use in-situ mode: reference input buffer directly
                // Safe: number_offset and number_len were validated above
                element->as.number.lexeme = (char*)(ctx->input_buffer + number_offset);
                element->as.number.lexeme_len = num->lexeme_len;
                element->as.number.is_in_situ = 1;
            } else if (num->lexeme && num->lexeme_len > 0) {
                // Copy lexeme if available
                size_t lexeme_len = num->lexeme_len;
                if (lexeme_len > SIZE_MAX - 1) {
                    result = TEXT_JSON_E_LIMIT;
                    text_json_free(element);
                    json_token_cleanup(token);
                    break;
                }
                char* lexeme = (char*)json_arena_alloc_for_context(ctx, lexeme_len + 1, 1);
                if (!lexeme) {
                    result = TEXT_JSON_E_OOM;
                    text_json_free(element);
                    json_token_cleanup(token);
                    break;
                }
                memcpy(lexeme, num->lexeme, lexeme_len);
                lexeme[lexeme_len] = '\0';
                element->as.number.lexeme = lexeme;
                element->as.number.lexeme_len = lexeme_len;
                element->as.number.is_in_situ = 0;
            } else {
                element->as.number.lexeme = NULL;
                element->as.number.lexeme_len = 0;
                element->as.number.is_in_situ = 0;
            }

            // Copy numeric representations
            if (num->flags & JSON_NUMBER_HAS_I64) {
                element->as.number.i64 = num->i64;
                element->as.number.has_i64 = 1;
            }
            if (num->flags & JSON_NUMBER_HAS_U64) {
                element->as.number.u64 = num->u64;
                element->as.number.has_u64 = 1;
            }
            if (num->flags & JSON_NUMBER_HAS_DOUBLE) {
                element->as.number.dbl = num->dbl;
                element->as.number.has_dbl = 1;
            }

            json_token_cleanup(token);
            break;
        }

        case JSON_TOKEN_LBRACKET:
            // Nested array - call json_parse_array recursively
            json_token_cleanup(token);
            result = json_parse_array(parser, &element, ctx);
            break;

        case JSON_TOKEN_LBRACE:
            // Nested object - call json_parse_object recursively
            json_token_cleanup(token);
            result = json_parse_object(parser, &element, ctx);
            break;

        default:
            // Invalid token for array element
            result = json_parser_set_error(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Unexpected token in array",
                token->pos
            );
            json_token_cleanup(token);
            break;
    }

    if (result != TEXT_JSON_OK) {
        *out = NULL;
        return result;
    }

    if (!element) {
        *out = NULL;
        return TEXT_JSON_E_OOM;
    }

    *out = element;
    return TEXT_JSON_OK;
}

// Helper to find an existing key in an object
// Returns the index of the key if found, or SIZE_MAX if not found
static size_t json_object_find_key(const text_json_value* object, const char* key, size_t key_len) {
    if (!object || object->type != TEXT_JSON_OBJECT || !key) {
        return SIZE_MAX;
    }

    // Defensive check: pairs might be NULL if object is empty
    if (!object->as.object.pairs || object->as.object.count == 0) {
        return SIZE_MAX;
    }

    for (size_t i = 0; i < object->as.object.count; ++i) {
        // Defensive bounds check before array access
        if (!json_check_bounds_index(i, object->as.object.count)) {
            break;
        }
        if (object->as.object.pairs[i].key_len == key_len) {
            if (key_len == 0 || memcmp(object->as.object.pairs[i].key, key, key_len) == 0) {
                return i;
            }
        }
    }

    return SIZE_MAX;
}

// Parse a JSON array
static text_json_status json_parse_array(json_parser* parser, text_json_value** out, json_context* ctx) {
    // Check depth limit
    text_json_status status = json_parser_check_depth(parser);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    // Check for overflow before incrementing depth
    if (json_check_add_overflow(parser->depth, 1)) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_DEPTH,
            "Maximum nesting depth exceeded (overflow)",
            parser->lexer.pos
        );
    }
    parser->depth++;
    text_json_status result = TEXT_JSON_OK;

    // Track if this is a root array (has its own context)
    int is_root_array = (ctx == NULL);

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
        if (first) {
            // Get next token to check if array is empty
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }

            // Check for closing bracket (empty array)
            if (token.type == JSON_TOKEN_RBRACKET) {
                json_token_cleanup(&token);
                break;
            }

            // Parse first element inline to avoid double token consumption
            // (json_parse_value would call json_lexer_next again, skipping this token)

            // Check container element limit
            status = json_parser_check_container_elems(parser, array->as.array.count);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }

            // Parse value based on token type (inline to avoid double token consumption)
            text_json_value* element = NULL;
            result = json_parse_array_element(parser, &token, array->ctx, &element);
            if (result != TEXT_JSON_OK) {
                break;
            }

            // Add element to array
            status = json_array_add_element(array, element);
            if (status != TEXT_JSON_OK) {
                result = status;
                break;
            }

            first = 0;
            continue;
        } else {
            // Subsequent elements: We need to get comma, then value
            // Get next token (should be comma or closing bracket)
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

            // Should be comma between elements
            if (token.type != JSON_TOKEN_COMMA) {
                result = json_parser_set_error_with_tokens(
                    parser,
                    TEXT_JSON_E_BAD_TOKEN,
                    "Expected comma between array elements",
                    token.pos,
                    json_token_type_description(JSON_TOKEN_COMMA),
                    json_token_type_description(token.type)
                );
                json_token_cleanup(&token);
                break;
            }
            json_token_cleanup(&token);

            // Get the value token after the comma
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }

            // Check if this is a trailing comma (comma followed by closing bracket)
            if (token.type == JSON_TOKEN_RBRACKET) {
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
                // Trailing comma allowed - consume closing bracket and exit
                json_token_cleanup(&token);
                break;
            }

            // Check container element limit
            status = json_parser_check_container_elems(parser, array->as.array.count);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }

            // Parse value using the token we already have (inline to avoid double consumption)
            text_json_value* element = NULL;
            result = json_parse_array_element(parser, &token, array->ctx, &element);
            if (result != TEXT_JSON_OK) {
                break;
            }

            // Add element to array
            status = json_array_add_element(array, element);
            if (status != TEXT_JSON_OK) {
                result = status;
                break;
            }
        }
    }

    parser->depth--;

    if (result != TEXT_JSON_OK) {
        // Only free array if it has its own context (root case).
        // If ctx was provided, array is in that context and will be freed
        // when the parent object/value is freed.
        if (is_root_array) {
            // Root array - has its own context, free it
            text_json_free(array);
        }
        // Otherwise, array is in parent's context, don't free it here
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

    // Check for overflow before incrementing depth
    if (json_check_add_overflow(parser->depth, 1)) {
        return json_parser_set_error(
            parser,
            TEXT_JSON_E_DEPTH,
            "Maximum nesting depth exceeded (overflow)",
            parser->lexer.pos
        );
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

        // Check for comma between pairs (if not first element)
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
            // Consume comma and check next token
            json_token_cleanup(&token);
            status = json_lexer_next(&parser->lexer, &token);
            if (status != TEXT_JSON_OK) {
                result = status;
                json_token_cleanup(&token);
                break;
            }
            // Check if this is a trailing comma (comma followed by closing brace)
            if (token.type == JSON_TOKEN_RBRACE) {
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
                // Trailing comma allowed - consume closing brace and exit
                json_token_cleanup(&token);
                break;
            }
            // Not a trailing comma - continue with key parsing (token is already the key)
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
        json_position key_pos = token.pos;  // Save position for error reporting

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
            result = json_parser_set_error_with_tokens(
                parser,
                TEXT_JSON_E_BAD_TOKEN,
                "Expected colon after object key",
                token.pos,
                json_token_type_description(JSON_TOKEN_COLON),
                json_token_type_description(token.type)
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

        // Handle duplicate key policies
        text_json_dupkey_mode dupkey_mode = parser->opts ? parser->opts->dupkeys : TEXT_JSON_DUPKEY_ERROR;
        size_t existing_idx = json_object_find_key(object, key_copy ? key_copy : "", key_len);
        int handled_duplicate = 0;  // Flag to track if we handled a duplicate

        if (existing_idx != SIZE_MAX) {
            // Duplicate key found - handle according to policy
            int should_break = 0;  // Flag to break from while loop

            switch (dupkey_mode) {
                case TEXT_JSON_DUPKEY_ERROR: {
                    // Fail parse on duplicate key
                    // Note: Don't free value here - it's part of object's arena.
                    // Freeing object at the end will free everything including value.
                    result = json_parser_set_error(
                        parser,
                        TEXT_JSON_E_DUPKEY,
                        "Duplicate key in object",
                        key_pos
                    );
                    if (key_copy) {
                        free(key_copy);
                    }
                    should_break = 1;
                    break;
                }

                case TEXT_JSON_DUPKEY_FIRST_WINS: {
                    // Keep first occurrence, discard new value
                    // Note: Don't free value here - it's part of object's arena.
                    // It will be freed when the object is freed. Just don't add it to the object.
                    // Free temporary key copy (not needed)
                    if (key_copy) {
                        free(key_copy);
                    }
                    // Continue to next pair (don't break)
                    status = TEXT_JSON_OK;
                    handled_duplicate = 1;
                    break;
                }

                case TEXT_JSON_DUPKEY_LAST_WINS: {
                    // Replace existing value with new one
                    // Note: Don't free the old value - it's part of object's arena.
                    // It will be freed when the object is freed. Just overwrite the pointer.
                    // Set the new value
                    // Defensive bounds check before array access
                    if (json_check_bounds_index(existing_idx, object->as.object.count)) {
                        object->as.object.pairs[existing_idx].value = value;
                    } else {
                        // Bounds check failed - should not happen, but be defensive
                        if (key_copy) {
                            free(key_copy);
                        }
                        return json_parser_set_error(
                            parser,
                            TEXT_JSON_E_INVALID,
                            "Internal error: array index out of bounds",
                            parser->lexer.pos
                        );
                    }
                    // Free temporary key copy (key already in arena)
                    if (key_copy) {
                        free(key_copy);
                    }
                    // Continue to next pair (don't break)
                    status = TEXT_JSON_OK;
                    handled_duplicate = 1;
                    break;
                }

                case TEXT_JSON_DUPKEY_COLLECT: {
                    // Convert to array on collision
                    // Defensive bounds check before array access
                    if (!json_check_bounds_index(existing_idx, object->as.object.count)) {
                        if (key_copy) {
                            free(key_copy);
                        }
                        return json_parser_set_error(
                            parser,
                            TEXT_JSON_E_INVALID,
                            "Internal error: array index out of bounds",
                            parser->lexer.pos
                        );
                    }
                    text_json_value* existing_value = object->as.object.pairs[existing_idx].value;

                    // Check if existing value is already an array
                    if (existing_value->type == TEXT_JSON_ARRAY) {
                        // Append new value to existing array
                        status = json_array_add_element(existing_value, value);
                        if (status != TEXT_JSON_OK) {
                            // Note: Don't free value here - it's part of object's arena.
                            // Freeing object at the end will free everything including value.
                            result = status;
                            if (key_copy) {
                                free(key_copy);
                            }
                            should_break = 1;
                            break;
                        }
                        // Free temporary key copy (not needed)
                        if (key_copy) {
                            free(key_copy);
                        }
                        // Continue to next pair (don't break)
                        status = TEXT_JSON_OK;
                    } else {
                        // Convert single value to array [old_value, new_value]
                        text_json_value* array = json_value_new_with_existing_context(TEXT_JSON_ARRAY, object->ctx);
                        if (!array) {
                            // Note: Don't free value here - it's part of object's arena.
                            result = TEXT_JSON_E_OOM;
                            if (key_copy) {
                                free(key_copy);
                            }
                            should_break = 1;
                            break;
                        }

                        // Add old value to array
                        status = json_array_add_element(array, existing_value);
                        if (status != TEXT_JSON_OK) {
                            // Note: Don't free array or value here - they're part of object's arena.
                            // Freeing object at the end will free everything.
                            result = status;
                            if (key_copy) {
                                free(key_copy);
                            }
                            should_break = 1;
                            break;
                        }

                        // Add new value to array
                        status = json_array_add_element(array, value);
                        if (status != TEXT_JSON_OK) {
                            // Note: Don't free array or value here - they're part of object's arena.
                            // Freeing object at the end will free everything.
                            result = status;
                            if (key_copy) {
                                free(key_copy);
                            }
                            should_break = 1;
                            break;
                        }

                        // Replace old value with array
                        // Defensive bounds check before array access (already checked above, but be safe)
                        if (json_check_bounds_index(existing_idx, object->as.object.count)) {
                            object->as.object.pairs[existing_idx].value = array;
                        } else {
                            // Should not happen, but be defensive
                            if (key_copy) {
                                free(key_copy);
                            }
                            return json_parser_set_error(
                                parser,
                                TEXT_JSON_E_INVALID,
                                "Internal error: array index out of bounds",
                                parser->lexer.pos
                            );
                        }
                        // Free temporary key copy (key already in arena)
                        if (key_copy) {
                            free(key_copy);
                        }
                        // Continue to next pair (don't break)
                        status = TEXT_JSON_OK;
                    }
                    handled_duplicate = 1;
                    break;
                }

                default: {
                    // Unknown mode - treat as error
                    // Note: Don't free value here - it's part of object's arena.
                    result = TEXT_JSON_E_INVALID;
                    if (key_copy) {
                        free(key_copy);
                    }
                    should_break = 1;
                    break;
                }
            }

            if (should_break) {
                break;
            }
            // If we handled a duplicate and didn't break, continue to next iteration
            // Skip the else block which would try to add the pair again
            if (handled_duplicate) {
                first = 0;
                continue;  // Continue to next iteration of while loop
            }
        } else {
            // No duplicate - add pair normally
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
        }

        // After handling a pair (duplicate or not), continue to next iteration
        // This will consume the next token (comma or closing brace) in the next loop iteration

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
                value = text_json_new_bool(true);
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
                value = text_json_new_bool(false);
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
                // For root, create context first, then check in-situ mode
                value = text_json_new_string(token.data.string.value, token.data.string.value_len);
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }
                // After creating root value, input buffer will be set in text_json_parse()
                // So we can't use in-situ mode for root strings at creation time
                // They will be copied, which is fine for root values
                value->as.string.is_in_situ = 0;
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_STRING, ctx);
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }

                // Check if we can use in-situ mode
                // Conditions: in-situ mode enabled, context has input buffer, no escape sequences
                int use_in_situ = 0;
                size_t original_start = 0;
                size_t original_len = 0;
                if (parser->opts && parser->opts->in_situ_mode &&
                    ctx->input_buffer && ctx->input_buffer_len > 0 &&
                    token.data.string.value_len == token.data.string.original_len) {
                    // Verify the original string position is within bounds
                    // Check for integer overflow and bounds
                    original_start = token.data.string.original_start;
                    original_len = token.data.string.original_len;
                    if (original_start < ctx->input_buffer_len &&
                        original_len <= ctx->input_buffer_len - original_start) {
                        use_in_situ = 1;
                    }
                }

                if (use_in_situ) {
                    // Use in-situ mode: reference input buffer directly
                    // Safe: original_start and original_len were validated above
                    value->as.string.data = (char*)(ctx->input_buffer + original_start);
                    value->as.string.len = original_len;
                    value->as.string.is_in_situ = 1;
                } else {
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
                    value->as.string.is_in_situ = 0;
                }
            }

            // Note: We've copied the string to the arena. The lexer-allocated string
            // will be freed by json_token_cleanup, so we don't need to free it manually.
            // Just let json_token_cleanup handle the cleanup.
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
                // After creating root value, input buffer will be set in text_json_parse()
                // So we can't use in-situ mode for root numbers at creation time
                // They will be copied, which is fine for root values
                value->as.number.is_in_situ = 0;
            } else {
                value = json_value_new_with_existing_context(TEXT_JSON_NUMBER, ctx);
                if (!value) {
                    result = TEXT_JSON_E_OOM;
                    json_token_cleanup(&token);
                    break;
                }

                // Check if we can use in-situ mode for lexeme
                // Conditions: in-situ mode enabled, context has input buffer, lexeme exists
                int use_in_situ = 0;
                size_t number_offset = 0;
                size_t number_len = 0;
                if (parser->opts && parser->opts->in_situ_mode &&
                    ctx->input_buffer && ctx->input_buffer_len > 0 &&
                    num->lexeme && num->lexeme_len > 0) {
                    // Verify the number position is within bounds
                    // Check for integer overflow and bounds using shared helpers
                    number_offset = token.pos.offset;
                    number_len = token.length;
                    if (json_check_bounds_offset(number_offset, ctx->input_buffer_len) &&
                        !json_check_add_overflow(number_offset, number_len) &&
                        number_offset + number_len <= ctx->input_buffer_len) {
                        use_in_situ = 1;
                    }
                }

                if (use_in_situ && num->lexeme_len > 0) {
                    // Use in-situ mode: reference input buffer directly
                    // Safe: number_offset and number_len were validated above
                    value->as.number.lexeme = (char*)(ctx->input_buffer + number_offset);
                    value->as.number.lexeme_len = num->lexeme_len;
                    value->as.number.is_in_situ = 1;
                } else if (num->lexeme && num->lexeme_len > 0) {
                    // Copy lexeme to arena
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
                    value->as.number.is_in_situ = 0;
                } else {
                    value->as.number.lexeme = NULL;
                    value->as.number.lexeme_len = 0;
                    value->as.number.is_in_situ = 0;
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
            // TODO: Handle extension options (comments, trailing commas, etc.) if needed
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
            // This should not happen for valid JSON input
            // Common causes: lexer error, incomplete input, or internal bug
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
        // Only free value if it has its own context (root case).
        // If ctx was provided, value is in that context and will be freed
        // when the parent object/array is freed.
        if (is_root && value) {
            // Root value - has its own context, free it
            text_json_free(value);
        }
        // Otherwise, value is in parent's context, don't free it here
        return result;
    }

    *out = value;
    return TEXT_JSON_OK;
}

// Internal helper function for parsing a single JSON value
static text_json_value* json_parse_internal(
    const char* bytes,
    size_t len,
    const text_json_parse_options* opt,
    text_json_error* err,
    int allow_multiple,
    size_t* bytes_consumed
) {
    // Defensive NULL pointer checks
    if (!bytes && len > 0) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input bytes must not be NULL";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
            err->expected_token = NULL;
            err->actual_token = NULL;
        }
        if (bytes_consumed) {
            *bytes_consumed = 0;
        }
        return NULL;
    }

    // Input size validation: check for reasonable input size before processing
    // This is a defensive check - actual limits are enforced during parsing
    if (len > SIZE_MAX / 2) {
        if (err) {
            memset(err, 0, sizeof(*err));
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input size is too large";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        if (bytes_consumed) {
            *bytes_consumed = 0;
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
    text_json_status status = json_lexer_init(&parser.lexer, bytes, len, opt, 0);  // not streaming mode
    if (status != TEXT_JSON_OK) {
        if (err) {
            err->code = status;
            err->message = "Failed to initialize lexer";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
            err->expected_token = NULL;
            err->actual_token = NULL;
        }
        if (bytes_consumed) {
            *bytes_consumed = 0;
        }
        return NULL;
    }

    // Parse root value (ctx=NULL means it's the root and will create its own context)
    // For in-situ mode, we need to create the context first and set the input buffer
    json_context* root_ctx = NULL;
    if (opt && opt->in_situ_mode) {
        root_ctx = json_context_new();
        if (!root_ctx) {
            if (err) {
                err->code = TEXT_JSON_E_OOM;
                err->message = "Failed to allocate context";
                err->offset = 0;
                err->line = 1;
                err->col = 1;
                err->context_snippet = NULL;
                err->context_snippet_len = 0;
                err->caret_offset = 0;
                err->expected_token = NULL;
                err->actual_token = NULL;
            }
            if (bytes_consumed) {
                *bytes_consumed = 0;
            }
            return NULL;
        }
        json_context_set_input_buffer(root_ctx, bytes, len);
    }

    text_json_value* root = NULL;
    status = json_parse_value(&parser, &root, root_ctx);

    if (status != TEXT_JSON_OK) {
        if (root) {
            text_json_free(root);
        } else if (root_ctx) {
            json_context_free(root_ctx);
        }
        if (bytes_consumed) {
            *bytes_consumed = 0;
        }
        return NULL;
    }

    // If root_ctx was created, it's now owned by root->ctx
    // If root was created with text_json_new_*, we need to set input buffer on its context
    if (root && root->ctx && opt && opt->in_situ_mode && !root_ctx) {
        json_context_set_input_buffer(root->ctx, bytes, len);
    }

    // Check for trailing content
    json_token token;
    status = json_lexer_next(&parser.lexer, &token);

    if (status != TEXT_JSON_OK) {
        // Error from lexer when checking for trailing content
        // This means there's invalid input after the successfully parsed value
        // Note: When json_lexer_next returns an error, token.pos may not be set
        // (it's initialized to 0), so we use parser.lexer.pos which is always valid
        if (allow_multiple) {
            // Multiple values allowed - return the value and bytes_consumed pointing to error
            if (bytes_consumed) {
                // Use lexer position (always valid) as the error occurred at current lexer position
                // Check for bounds to prevent overflow
                *bytes_consumed = (parser.lexer.pos.offset <= len) ? parser.lexer.pos.offset : len;
            }
            json_token_cleanup(&token);
            return root;
        } else {
            // Single value mode - treat trailing invalid content as error
            text_json_free(root);
            json_token_cleanup(&token);
            if (err) {
                json_position pos = parser.lexer.pos;
                // Clamp offset to input length
                if (pos.offset > len) {
                    pos.offset = len;
                }
                json_parser_set_error(
                    &parser,
                    TEXT_JSON_E_TRAILING_GARBAGE,
                    "Trailing garbage after valid JSON",
                    pos
                );
            }
            if (bytes_consumed) {
                *bytes_consumed = 0;
            }
            return NULL;
        }
    }

    if (token.type != JSON_TOKEN_EOF) {
        // There's trailing content (valid token)
        if (allow_multiple) {
            // Multiple top-level values allowed - return bytes consumed (position of next token)
            if (bytes_consumed) {
                // token.pos.offset is the start of the next token, which is correct
                // Check bounds to prevent overflow
                *bytes_consumed = (token.pos.offset <= len) ? token.pos.offset : len;
            }
            json_token_cleanup(&token);
            return root;
        } else {
            // Trailing garbage not allowed - error
            text_json_free(root);
            json_token_cleanup(&token);
            if (err) {
                json_position pos = token.pos;
                // Clamp offset to input length
                if (pos.offset > len) {
                    pos.offset = len;
                }
                json_parser_set_error(
                    &parser,
                    TEXT_JSON_E_TRAILING_GARBAGE,
                    "Trailing garbage after valid JSON",
                    pos
                );
            }
            if (bytes_consumed) {
                *bytes_consumed = 0;
            }
            return NULL;
        }
    }

    // EOF reached - bytes consumed is the lexer position (end of input)
    if (bytes_consumed) {
        // Check bounds to prevent overflow
        *bytes_consumed = (parser.lexer.pos.offset <= len) ? parser.lexer.pos.offset : len;
    }
    json_token_cleanup(&token);

    return root;
}

TEXT_API text_json_value* text_json_parse(
    const char* bytes,
    size_t len,
    const text_json_parse_options* opt,
    text_json_error* err
) {
    // Input validation: check for NULL bytes when len > 0
    if (!bytes && len > 0) {
        if (err) {
            memset(err, 0, sizeof(*err));
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input bytes must not be NULL when length is non-zero";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return NULL;
    }

    // Input validation: check for reasonable input size (prevent obvious overflow issues)
    // Note: We don't enforce a hard limit here, but check for obviously invalid values
    // The actual limit checking happens in json_parse_internal via max_total_bytes
    if (len > SIZE_MAX / 2) {
        // Input size is suspiciously large (more than half of SIZE_MAX)
        // This could indicate an overflow or invalid input
        if (err) {
            memset(err, 0, sizeof(*err));
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input size is too large";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return NULL;
    }

    // Always treat trailing content as error (single value only)
    return json_parse_internal(bytes, len, opt, err, 0, NULL);
}

TEXT_API text_json_value* text_json_parse_multiple(
    const char* bytes,
    size_t len,
    const text_json_parse_options* opt,
    text_json_error* err,
    size_t* bytes_consumed
) {
    // Input validation: bytes_consumed is required
    if (!bytes_consumed) {
        if (err) {
            memset(err, 0, sizeof(*err));
            err->code = TEXT_JSON_E_INVALID;
            err->message = "bytes_consumed must not be NULL";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return NULL;
    }

    // Input validation: check for NULL bytes when len > 0
    if (!bytes && len > 0) {
        if (err) {
            memset(err, 0, sizeof(*err));
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input bytes must not be NULL when length is non-zero";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        *bytes_consumed = 0;
        return NULL;
    }

    // Input validation: check for reasonable input size
    if (len > SIZE_MAX / 2) {
        if (err) {
            memset(err, 0, sizeof(*err));
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input size is too large";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        *bytes_consumed = 0;
        return NULL;
    }
    // Allow multiple values and return bytes consumed
    return json_parse_internal(bytes, len, opt, err, 1, bytes_consumed);
}
