/**
 * @file json_stream.c
 * @brief Streaming (incremental) JSON parser implementation
 *
 * Implements an event-based streaming parser that accepts input in chunks
 * and emits events for each JSON value encountered.
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_stream.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Streaming parser state machine states
typedef enum {
    JSON_STREAM_STATE_INIT,        ///< Initial state, waiting for first value
    JSON_STREAM_STATE_VALUE,       ///< Just processed a value, waiting for comma or closing bracket/brace
    JSON_STREAM_STATE_ARRAY,       ///< Inside an array, expecting value or ]
    JSON_STREAM_STATE_OBJECT_KEY,  ///< Inside object, expecting key
    JSON_STREAM_STATE_OBJECT_VALUE,///< Just processed key, expecting colon
    JSON_STREAM_STATE_EXPECT_VALUE,///< Expecting a value (after colon in object, or in array)
    JSON_STREAM_STATE_DONE,        ///< Parsing complete
    JSON_STREAM_STATE_ERROR        ///< Error state
} json_stream_state;

// Stack entry for tracking nesting
typedef struct {
    json_stream_state state;       ///< State when entering this level
    int is_array;                  ///< 1 if array, 0 if object
    int has_elements;              ///< 1 if container has at least one element
} json_stream_stack_entry;

// Internal streaming parser structure
struct text_json_stream {
    // Configuration
    text_json_parse_options opts;  ///< Parse options (copied)
    text_json_event_cb callback;   ///< Event callback
    void* user_data;               ///< User context for callback

    // State machine
    json_stream_state state;       ///< Current parser state
    size_t depth;                  ///< Current nesting depth

    // Input buffering (for incremental parsing)
    char* input_buffer;            ///< Buffered input data
    size_t input_buffer_size;      ///< Allocated size of buffer
    size_t input_buffer_used;      ///< Used portion of buffer
    size_t input_buffer_processed; ///< Processed portion of buffer
    size_t buffer_start_offset;    ///< Absolute offset where buffer starts in total input

    // Lexer state (will be initialized when we have enough input)
    json_lexer lexer;              ///< Lexer instance
    int lexer_initialized;         ///< Whether lexer is initialized

    // Stack for tracking nesting
    json_stream_stack_entry* stack; ///< Stack of nested structures
    size_t stack_capacity;          ///< Allocated stack capacity
    size_t stack_size;             ///< Current stack depth

    // Buffers for string/number tokens (complete before emitting)
    char* string_buffer;           ///< Buffer for string tokens
    size_t string_buffer_size;      ///< Allocated size
    size_t string_buffer_used;      ///< Used size

    char* number_buffer;            ///< Buffer for number tokens
    size_t number_buffer_size;      ///< Allocated size
    size_t number_buffer_used;      ///< Used size

    // Limits tracking
    size_t total_bytes_consumed;    ///< Total bytes processed
    size_t container_elem_count;    ///< Current container element count
};

// Get effective limit value (use default if 0)
static size_t json_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Grow a buffer if needed
static text_json_status json_stream_grow_buffer(
    char** buffer,
    size_t* capacity,
    size_t needed
) {
    if (needed <= *capacity) {
        return TEXT_JSON_OK;
    }

    // Calculate new capacity (at least double, or needed size)
    // Check for overflow before multiplication
    size_t new_capacity;
    if (*capacity > SIZE_MAX / 2) {
        // Cannot double without overflow, use needed size directly
        new_capacity = needed;
    } else {
        new_capacity = *capacity * 2;
        if (new_capacity < needed) {
            new_capacity = needed;
        }
    }

    // Add some headroom (check for overflow before addition)
    if (new_capacity > SIZE_MAX - 1024) {
        // Cannot add headroom without overflow
        if (new_capacity < needed) {
            return TEXT_JSON_E_OOM;  // Cannot satisfy request
        }
        // Use new_capacity as-is (already >= needed)
    } else {
        new_capacity += 1024;
    }

    // Final overflow check
    if (new_capacity < needed || new_capacity < *capacity) {
        return TEXT_JSON_E_OOM;
    }

    char* new_buffer = (char*)realloc(*buffer, new_capacity);
    if (!new_buffer) {
        return TEXT_JSON_E_OOM;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return TEXT_JSON_OK;
}

// Grow stack if needed
static text_json_status json_stream_grow_stack(text_json_stream* st) {
    if (st->stack_size < st->stack_capacity) {
        return TEXT_JSON_OK;
    }

    // Check for overflow before multiplication
    size_t new_capacity;
    if (st->stack_capacity == 0) {
        new_capacity = 16;  // Initial capacity
    } else if (st->stack_capacity > SIZE_MAX / 2) {
        return TEXT_JSON_E_OOM;  // Cannot double without overflow
    } else {
        new_capacity = st->stack_capacity * 2;
        // Verify no overflow occurred
        if (new_capacity < st->stack_capacity) {
            return TEXT_JSON_E_OOM;  // Overflow
        }
    }

    // Check for overflow in size calculation
    size_t alloc_size = new_capacity * sizeof(json_stream_stack_entry);
    if (alloc_size / sizeof(json_stream_stack_entry) != new_capacity) {
        return TEXT_JSON_E_OOM;  // Multiplication overflowed
    }

    json_stream_stack_entry* new_stack = (json_stream_stack_entry*)realloc(
        st->stack,
        alloc_size
    );
    if (!new_stack) {
        return TEXT_JSON_E_OOM;
    }

    st->stack = new_stack;
    st->stack_capacity = new_capacity;
    return TEXT_JSON_OK;
}

// Push state onto stack
static text_json_status json_stream_push(
    text_json_stream* st,
    json_stream_state state,
    int is_array
) {
    // Check depth limit before growing stack (more efficient)
    size_t max_depth = json_get_limit(
        st->opts.max_depth,
        JSON_DEFAULT_MAX_DEPTH
    );
    if (st->depth >= max_depth) {
        return TEXT_JSON_E_DEPTH;
    }

    // Grow stack if needed
    text_json_status status = json_stream_grow_stack(st);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    // Verify bounds before array access (defensive check)
    if (st->stack_size >= st->stack_capacity) {
        return TEXT_JSON_E_OOM;  // Should not happen after grow_stack, but be safe
    }

    st->stack[st->stack_size].state = state;
    st->stack[st->stack_size].is_array = is_array;
    st->stack[st->stack_size].has_elements = 0;
    st->stack_size++;
    st->depth++;

    return TEXT_JSON_OK;
}

// Pop state from stack
static void json_stream_pop(text_json_stream* st) {
    if (st->stack_size > 0) {
        st->stack_size--;
        st->depth--;
    }
}

// Get current stack entry (or NULL if empty)
static json_stream_stack_entry* json_stream_top(text_json_stream* st) {
    if (st->stack_size == 0) {
        return NULL;
    }
    return &st->stack[st->stack_size - 1];
}

// Forward declarations
static text_json_status json_stream_handle_token(
    text_json_stream* st,
    const json_token* token,
    text_json_error* err
);
static text_json_status json_stream_handle_value_token(
    text_json_stream* st,
    const json_token* token,
    text_json_error* err
);

// Emit an event through the callback
static text_json_status json_stream_emit_event(
    text_json_stream* st,
    text_json_event_type type,
    const text_json_event* evt_data
) {
    text_json_event evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = type;

    if (evt_data) {
        evt = *evt_data;
    }

    text_json_error err;
    memset(&err, 0, sizeof(err));
    text_json_status status = st->callback(st->user_data, &evt, &err);

    if (status != TEXT_JSON_OK) {
        st->state = JSON_STREAM_STATE_ERROR;
        return status;
    }

    return TEXT_JSON_OK;
}

// Compact the input buffer by shifting unprocessed data to the start
static void json_stream_compact_buffer(text_json_stream* st) {
    if (st->input_buffer_processed == 0) {
        return;  // Nothing to compact
    }

    if (st->input_buffer_processed >= st->input_buffer_used) {
        // All data processed, reset buffer
        st->input_buffer_used = 0;
        st->input_buffer_processed = 0;
        st->buffer_start_offset = st->total_bytes_consumed;
        return;
    }

    // Shift remaining data to start of buffer
    size_t remaining = st->input_buffer_used - st->input_buffer_processed;
    memmove(st->input_buffer, st->input_buffer + st->input_buffer_processed, remaining);

    // Update buffer start offset to reflect the new buffer position
    st->buffer_start_offset += st->input_buffer_processed;
    st->input_buffer_used = remaining;
    st->input_buffer_processed = 0;
}

// Process tokens from the buffered input
static text_json_status json_stream_process_tokens(
    text_json_stream* st,
    text_json_error* err
) {
    // Compact buffer if needed (shift unprocessed data to start)
    json_stream_compact_buffer(st);

    // If no unprocessed data, nothing to do
    if (st->input_buffer_used == 0) {
        return TEXT_JSON_OK;
    }

    // Initialize or reinitialize lexer with current buffer
    // We reinitialize after compacting to ensure the input pointer is valid
    text_json_status status = json_lexer_init(
        &st->lexer,
        st->input_buffer,
        st->input_buffer_used,
        &st->opts
    );
    if (status != TEXT_JSON_OK) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = status;
            err->message = "Failed to initialize lexer";
                err->offset = st->buffer_start_offset;
            err->line = 1;
            err->col = 1;
        }
        return status;
    }
    st->lexer_initialized = 1;

    // Process tokens until we can't continue (EOF or error)
    json_token token;
    while (1) {
        memset(&token, 0, sizeof(token));

        text_json_status status = json_lexer_next(&st->lexer, &token);
        if (status != TEXT_JSON_OK) {
            // Error tokenizing - might be incomplete input or actual error
            // If it's a string/number parsing error, might need more input
            if (token.type == JSON_TOKEN_ERROR) {
                // Check if we're at EOF - if so, we might need more input
                if (st->lexer.current_offset >= st->lexer.input_len) {
                    // At end of current buffer, wait for more input
                    json_token_cleanup(&token);
                    return TEXT_JSON_OK;
                }
            }

            // Real error
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = status;
                err->message = "Tokenization error";
                err->offset = st->total_bytes_consumed - st->input_buffer_used + token.pos.offset;
                err->line = token.pos.line;
                err->col = token.pos.col;
            }
            json_token_cleanup(&token);
            return status;
        }

        // Update processed offset
        st->input_buffer_processed = st->lexer.current_offset;

        // Handle EOF
        if (token.type == JSON_TOKEN_EOF) {
            json_token_cleanup(&token);
            // At end of buffer, wait for more input (unless finish() was called)
            return TEXT_JSON_OK;
        }

        // Process token based on current state
        status = json_stream_handle_token(st, &token, err);
        json_token_cleanup(&token);

        if (status != TEXT_JSON_OK) {
            return status;
        }

        // If we're in error or done state, stop processing
        if (st->state == JSON_STREAM_STATE_ERROR || st->state == JSON_STREAM_STATE_DONE) {
            return status;
        }
    }
}

// Handle a token based on current parser state
static text_json_status json_stream_handle_token(
    text_json_stream* st,
    const json_token* token,
    text_json_error* err
) {
    text_json_status status;

    // Handle based on current state
    switch (st->state) {
        case JSON_STREAM_STATE_INIT:
            // Expecting first value
            return json_stream_handle_value_token(st, token, err);

        case JSON_STREAM_STATE_VALUE:
            // Just processed a value, expect comma or closing bracket/brace
            if (token->type == JSON_TOKEN_COMMA) {
                // Continue to next value
                json_stream_stack_entry* top = json_stream_top(st);
                if (!top) {
                    // Comma at root level - invalid
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                    err->code = TEXT_JSON_E_BAD_TOKEN;
                    err->message = "Unexpected comma at root level";
                    err->offset = st->buffer_start_offset + token->pos.offset;
                    err->line = token->pos.line;
                    err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Update state based on container type
                if (top->is_array) {
                    st->state = JSON_STREAM_STATE_EXPECT_VALUE;
                } else {
                    st->state = JSON_STREAM_STATE_OBJECT_KEY;
                }
                return TEXT_JSON_OK;
            } else if (token->type == JSON_TOKEN_RBRACKET) {
                // End of array
                json_stream_stack_entry* top = json_stream_top(st);
                if (!top || !top->is_array) {
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_BAD_TOKEN;
                        err->message = "Unexpected ]";
                        err->offset = st->buffer_start_offset + token->pos.offset;
                        err->line = token->pos.line;
                        err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Check for trailing comma (if container has elements and we're expecting a value)
                if (top->has_elements && st->state == JSON_STREAM_STATE_EXPECT_VALUE && !st->opts.allow_trailing_commas) {
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_BAD_TOKEN;
                        err->message = "Trailing comma not allowed";
                        err->offset = st->buffer_start_offset + token->pos.offset;
                        err->line = token->pos.line;
                        err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Emit array end event
                text_json_event evt;
                evt.type = TEXT_JSON_EVT_ARRAY_END;
                status = json_stream_emit_event(st, TEXT_JSON_EVT_ARRAY_END, &evt);
                if (status != TEXT_JSON_OK) {
                    return status;
                }

                json_stream_pop(st);
                if (st->stack_size == 0) {
                    st->state = JSON_STREAM_STATE_DONE;
                } else {
                    // Mark that parent container has elements (nested array counts as element)
                    json_stream_stack_entry* top = json_stream_top(st);
                    if (top) {
                        top->has_elements = 1;
                    }
                    st->state = JSON_STREAM_STATE_VALUE;
                }
                return TEXT_JSON_OK;
            } else if (token->type == JSON_TOKEN_RBRACE) {
                // End of object
                json_stream_stack_entry* top = json_stream_top(st);
                if (!top || top->is_array) {
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_BAD_TOKEN;
                        err->message = "Unexpected }";
                        err->offset = st->buffer_start_offset + token->pos.offset;
                        err->line = token->pos.line;
                        err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Check for trailing comma (if container has elements and we're expecting a key)
                if (top->has_elements && st->state == JSON_STREAM_STATE_OBJECT_KEY && !st->opts.allow_trailing_commas) {
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_BAD_TOKEN;
                        err->message = "Trailing comma not allowed";
                        err->offset = st->buffer_start_offset + token->pos.offset;
                        err->line = token->pos.line;
                        err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Emit object end event
                text_json_event evt;
                evt.type = TEXT_JSON_EVT_OBJECT_END;
                status = json_stream_emit_event(st, TEXT_JSON_EVT_OBJECT_END, &evt);
                if (status != TEXT_JSON_OK) {
                    return status;
                }

                json_stream_pop(st);
                if (st->stack_size == 0) {
                    st->state = JSON_STREAM_STATE_DONE;
                } else {
                    // Mark that parent container has elements (nested object counts as element)
                    json_stream_stack_entry* top = json_stream_top(st);
                    if (top) {
                        top->has_elements = 1;
                    }
                    st->state = JSON_STREAM_STATE_VALUE;
                }
                return TEXT_JSON_OK;
            } else {
                // Unexpected token
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = TEXT_JSON_E_BAD_TOKEN;
                    err->message = "Unexpected token after value";
                    err->offset = st->buffer_start_offset + token->pos.offset;
                    err->line = token->pos.line;
                    err->col = token->pos.col;
                }
                return TEXT_JSON_E_BAD_TOKEN;
            }

        case JSON_STREAM_STATE_ARRAY:
            // Expecting array element value
            return json_stream_handle_value_token(st, token, err);

        case JSON_STREAM_STATE_EXPECT_VALUE:
            // Expecting a value (after colon in object, or first element in array)
            // But also check for empty containers (closing bracket/brace)
            if (token->type == JSON_TOKEN_RBRACKET) {
                // End of array (empty array)
                json_stream_stack_entry* top = json_stream_top(st);
                if (!top || !top->is_array) {
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_BAD_TOKEN;
                        err->message = "Unexpected ]";
                        err->offset = st->buffer_start_offset + token->pos.offset;
                        err->line = token->pos.line;
                        err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Emit array end event
                text_json_event evt;
                evt.type = TEXT_JSON_EVT_ARRAY_END;
                status = json_stream_emit_event(st, TEXT_JSON_EVT_ARRAY_END, &evt);
                if (status != TEXT_JSON_OK) {
                    return status;
                }

                json_stream_pop(st);
                if (st->stack_size == 0) {
                    st->state = JSON_STREAM_STATE_DONE;
                } else {
                    json_stream_stack_entry* parent = json_stream_top(st);
                    if (parent) {
                        parent->has_elements = 1;
                    }
                    st->state = JSON_STREAM_STATE_VALUE;
                }
                return TEXT_JSON_OK;
            } else if (token->type == JSON_TOKEN_RBRACE) {
                // End of object (empty object)
                json_stream_stack_entry* top = json_stream_top(st);
                if (!top || top->is_array) {
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_BAD_TOKEN;
                        err->message = "Unexpected }";
                        err->offset = st->buffer_start_offset + token->pos.offset;
                        err->line = token->pos.line;
                        err->col = token->pos.col;
                    }
                    return TEXT_JSON_E_BAD_TOKEN;
                }

                // Emit object end event
                text_json_event evt;
                evt.type = TEXT_JSON_EVT_OBJECT_END;
                status = json_stream_emit_event(st, TEXT_JSON_EVT_OBJECT_END, &evt);
                if (status != TEXT_JSON_OK) {
                    return status;
                }

                json_stream_pop(st);
                if (st->stack_size == 0) {
                    st->state = JSON_STREAM_STATE_DONE;
                } else {
                    json_stream_stack_entry* parent = json_stream_top(st);
                    if (parent) {
                        parent->has_elements = 1;
                    }
                    st->state = JSON_STREAM_STATE_VALUE;
                }
                return TEXT_JSON_OK;
            }
            // Otherwise, handle as value token
            return json_stream_handle_value_token(st, token, err);

        case JSON_STREAM_STATE_OBJECT_KEY:
            // Expecting object key
            if (token->type != JSON_TOKEN_STRING) {
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = TEXT_JSON_E_BAD_TOKEN;
                    err->message = "Expected object key (string)";
                    err->offset = st->buffer_start_offset + token->pos.offset;
                    err->line = token->pos.line;
                    err->col = token->pos.col;
                }
                return TEXT_JSON_E_BAD_TOKEN;
            }

            // Emit key event
            {
                text_json_event evt;
                evt.type = TEXT_JSON_EVT_KEY;
                evt.as.str.s = token->data.string.value;
                evt.as.str.len = token->data.string.value_len;
                status = json_stream_emit_event(st, TEXT_JSON_EVT_KEY, &evt);
                if (status != TEXT_JSON_OK) {
                    return status;
                }
            }

            st->state = JSON_STREAM_STATE_OBJECT_VALUE;
            return TEXT_JSON_OK;

        case JSON_STREAM_STATE_OBJECT_VALUE:
            // Expecting colon after key
            if (token->type != JSON_TOKEN_COLON) {
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = TEXT_JSON_E_BAD_TOKEN;
                    err->message = "Expected colon after object key";
                    err->offset = st->buffer_start_offset + token->pos.offset;
                    err->line = token->pos.line;
                    err->col = token->pos.col;
                }
                return TEXT_JSON_E_BAD_TOKEN;
            }

            // After colon, expect value
            st->state = JSON_STREAM_STATE_EXPECT_VALUE;
            return TEXT_JSON_OK;

        default:
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = TEXT_JSON_E_STATE;
                err->message = "Invalid parser state";
                err->offset = st->total_bytes_consumed - st->input_buffer_used + token->pos.offset;
                err->line = token->pos.line;
                err->col = token->pos.col;
            }
            return TEXT_JSON_E_STATE;
    }
}

// Handle a value token (null, bool, number, string, array begin, object begin)
static text_json_status json_stream_handle_value_token(
    text_json_stream* st,
    const json_token* token,
    text_json_error* err
) {
    text_json_status status;
    text_json_event evt;

    switch (token->type) {
        case JSON_TOKEN_NULL:
            evt.type = TEXT_JSON_EVT_NULL;
            status = json_stream_emit_event(st, TEXT_JSON_EVT_NULL, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }
            break;

        case JSON_TOKEN_TRUE:
        case JSON_TOKEN_FALSE:
            evt.type = TEXT_JSON_EVT_BOOL;
            evt.as.boolean = (token->type == JSON_TOKEN_TRUE) ? 1 : 0;
            status = json_stream_emit_event(st, TEXT_JSON_EVT_BOOL, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }
            break;

        case JSON_TOKEN_NUMBER:
            evt.type = TEXT_JSON_EVT_NUMBER;
            evt.as.number.s = token->data.number.lexeme;
            evt.as.number.len = token->data.number.lexeme_len;
            status = json_stream_emit_event(st, TEXT_JSON_EVT_NUMBER, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }
            break;

        case JSON_TOKEN_STRING:
            evt.type = TEXT_JSON_EVT_STRING;
            evt.as.str.s = token->data.string.value;
            evt.as.str.len = token->data.string.value_len;
            status = json_stream_emit_event(st, TEXT_JSON_EVT_STRING, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }
            break;

        case JSON_TOKEN_LBRACKET:
            // Array begin
            status = json_stream_push(st, st->state, 1);
            if (status != TEXT_JSON_OK) {
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = status;
                    err->message = "Failed to push array onto stack";
                    err->offset = st->buffer_start_offset + token->pos.offset;
                    err->line = token->pos.line;
                    err->col = token->pos.col;
                }
                return status;
            }

            // Reset element count for new container
            st->container_elem_count = 0;

            evt.type = TEXT_JSON_EVT_ARRAY_BEGIN;
            status = json_stream_emit_event(st, TEXT_JSON_EVT_ARRAY_BEGIN, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }

            st->state = JSON_STREAM_STATE_EXPECT_VALUE;
            return TEXT_JSON_OK;

        case JSON_TOKEN_LBRACE:
            // Object begin
            status = json_stream_push(st, st->state, 0);
            if (status != TEXT_JSON_OK) {
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = status;
                    err->message = "Failed to push object onto stack";
                    err->offset = st->buffer_start_offset + token->pos.offset;
                    err->line = token->pos.line;
                    err->col = token->pos.col;
                }
                return status;
            }

            // Reset element count for new container
            st->container_elem_count = 0;

            evt.type = TEXT_JSON_EVT_OBJECT_BEGIN;
            status = json_stream_emit_event(st, TEXT_JSON_EVT_OBJECT_BEGIN, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }

            st->state = JSON_STREAM_STATE_OBJECT_KEY;
            return TEXT_JSON_OK;

        case JSON_TOKEN_NAN:
        case JSON_TOKEN_INFINITY:
        case JSON_TOKEN_NEG_INFINITY:
            // Nonfinite numbers - emit as number event with lexeme
            evt.type = TEXT_JSON_EVT_NUMBER;
            // For nonfinite numbers, we need to get the lexeme from the token
            // The lexer should have stored it in the number structure
            if (token->type == JSON_TOKEN_NAN) {
                evt.as.number.s = "NaN";
                evt.as.number.len = 3;
            } else if (token->type == JSON_TOKEN_INFINITY) {
                evt.as.number.s = "Infinity";
                evt.as.number.len = 8;
            } else {
                evt.as.number.s = "-Infinity";
                evt.as.number.len = 9;
            }
            status = json_stream_emit_event(st, TEXT_JSON_EVT_NUMBER, &evt);
            if (status != TEXT_JSON_OK) {
                return status;
            }
            break;

        default:
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = TEXT_JSON_E_BAD_TOKEN;
                err->message = "Unexpected token, expected value";
                err->offset = st->total_bytes_consumed - st->input_buffer_used + token->pos.offset;
                err->line = token->pos.line;
                err->col = token->pos.col;
            }
            return TEXT_JSON_E_BAD_TOKEN;
    }

    // After emitting a scalar value, update state
    if (st->stack_size == 0) {
        // Root level value - we're done
        st->state = JSON_STREAM_STATE_DONE;
    } else {
        // Mark that container has elements
        json_stream_stack_entry* top = json_stream_top(st);
        if (top) {
            top->has_elements = 1;
        }

        // Check container element limit
        st->container_elem_count++;
        size_t max_elems = json_get_limit(
            st->opts.max_container_elems,
            JSON_DEFAULT_MAX_CONTAINER_ELEMS
        );
        if (st->container_elem_count > max_elems) {
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = TEXT_JSON_E_LIMIT;
                err->message = "Maximum container element count exceeded";
                err->offset = st->buffer_start_offset + token->pos.offset;
                err->line = token->pos.line;
                err->col = token->pos.col;
            }
            return TEXT_JSON_E_LIMIT;
        }

        // Inside container - expect comma or closing bracket/brace
        st->state = JSON_STREAM_STATE_VALUE;
    }

    return TEXT_JSON_OK;
}

TEXT_API text_json_stream* text_json_stream_new(
    const text_json_parse_options* opt,
    text_json_event_cb cb,
    void* user
) {
    if (!cb) {
        return NULL;
    }

    text_json_stream* st = (text_json_stream*)calloc(1, sizeof(text_json_stream));
    if (!st) {
        return NULL;
    }

    // Copy parse options or use defaults
    if (opt) {
        st->opts = *opt;
    } else {
        st->opts = text_json_parse_options_default();
    }

    st->callback = cb;
    st->user_data = user;
    st->state = JSON_STREAM_STATE_INIT;
    st->depth = 0;
    st->lexer_initialized = 0;
    st->buffer_start_offset = 0;

    // Initialize buffers with reasonable starting sizes
    st->input_buffer_size = 4096;
    st->input_buffer = (char*)malloc(st->input_buffer_size);
    if (!st->input_buffer) {
        free(st);
        return NULL;
    }

    st->string_buffer_size = 4096;
    st->string_buffer = (char*)malloc(st->string_buffer_size);
    if (!st->string_buffer) {
        free(st->input_buffer);
        free(st);
        return NULL;
    }

    st->number_buffer_size = 256;
    st->number_buffer = (char*)malloc(st->number_buffer_size);
    if (!st->number_buffer) {
        free(st->string_buffer);
        free(st->input_buffer);
        free(st);
        return NULL;
    }

    // Initialize stack
    st->stack_capacity = 16;
    st->stack = (json_stream_stack_entry*)malloc(
        st->stack_capacity * sizeof(json_stream_stack_entry)
    );
    if (!st->stack) {
        free(st->number_buffer);
        free(st->string_buffer);
        free(st->input_buffer);
        free(st);
        return NULL;
    }

    return st;
}

TEXT_API void text_json_stream_free(text_json_stream* st) {
    if (!st) {
        return;
    }

    // Free all buffers
    free(st->input_buffer);
    free(st->string_buffer);
    free(st->number_buffer);
    free(st->stack);

    free(st);
}

TEXT_API text_json_status text_json_stream_feed(
    text_json_stream* st,
    const char* bytes,
    size_t len,
    text_json_error* err
) {
    if (!st) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Stream must not be NULL";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_INVALID;
    }

    if (!bytes && len > 0) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Input bytes must not be NULL";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_INVALID;
    }

    if (st->state == JSON_STREAM_STATE_ERROR || st->state == JSON_STREAM_STATE_DONE) {
        if (err) {
            err->code = TEXT_JSON_E_STATE;
            err->message = "Stream is in invalid state for feeding";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_STATE;
    }

    // TODO: Implement incremental parsing logic
    // Currently just buffers input; parsing will process buffered data and emit events
    if (len == 0) {
        return TEXT_JSON_OK;
    }

    // Append to input buffer
    // Check for integer overflow before addition
    if (st->input_buffer_used > SIZE_MAX - len) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Input buffer size would overflow";
            err->offset = st->total_bytes_consumed;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_OOM;
    }

    size_t needed = st->input_buffer_used + len;
    text_json_status status = json_stream_grow_buffer(
        &st->input_buffer,
        &st->input_buffer_size,
        needed
    );
    if (status != TEXT_JSON_OK) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = status;
            err->message = "Failed to grow input buffer";
            err->offset = st->total_bytes_consumed;
            err->line = 1;
            err->col = 1;
        }
        return status;
    }

    // Verify bounds before memcpy (defensive check)
    if (st->input_buffer_used + len > st->input_buffer_size) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Buffer size mismatch";
            err->offset = st->total_bytes_consumed;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_OOM;
    }

    memcpy(st->input_buffer + st->input_buffer_used, bytes, len);
    st->input_buffer_used += len;

    // Update buffer start offset if this is the first chunk
    if (st->buffer_start_offset == 0 && st->input_buffer_used == len) {
        st->buffer_start_offset = 0;  // First chunk starts at offset 0
    }

    // Check for integer overflow before updating total_bytes_consumed
    if (st->total_bytes_consumed > SIZE_MAX - len) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = TEXT_JSON_E_OOM;
            err->message = "Total bytes consumed would overflow";
            err->offset = st->total_bytes_consumed;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_OOM;
    }
    st->total_bytes_consumed += len;

    // Check total bytes limit
    size_t max_total = json_get_limit(
        st->opts.max_total_bytes,
        JSON_DEFAULT_MAX_TOTAL_BYTES
    );
    if (st->total_bytes_consumed > max_total) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = TEXT_JSON_E_LIMIT;
            err->message = "Maximum total input size exceeded";
            err->offset = st->total_bytes_consumed;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_LIMIT;
    }

    // Process buffered input and emit events through callback
    return json_stream_process_tokens(st, err);
}

TEXT_API text_json_status text_json_stream_finish(
    text_json_stream* st,
    text_json_error* err
) {
    if (!st) {
        if (err) {
            err->code = TEXT_JSON_E_INVALID;
            err->message = "Stream must not be NULL";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_INVALID;
    }

    if (st->state == JSON_STREAM_STATE_ERROR) {
        if (err) {
            err->code = TEXT_JSON_E_STATE;
            err->message = "Stream is in error state";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_STATE;
    }

    // Validate that structure is complete (no unmatched brackets)
    if (st->stack_size > 0) {
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = TEXT_JSON_E_INCOMPLETE;
            err->message = "Incomplete JSON structure";
            err->offset = st->total_bytes_consumed;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_INCOMPLETE;
    }

    // Complete parsing of any remaining buffered input
    text_json_status status = json_stream_process_tokens(st, err);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    // Check if we're in a valid final state
    if (st->state != JSON_STREAM_STATE_DONE && st->state != JSON_STREAM_STATE_INIT) {
        // Still have unprocessed input or incomplete structure
        if (st->input_buffer_used > st->input_buffer_processed) {
            // Have unprocessed input - might be incomplete token
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = TEXT_JSON_E_INCOMPLETE;
                err->message = "Incomplete JSON input";
                err->offset = st->total_bytes_consumed;
                err->line = 1;
                err->col = 1;
            }
            return TEXT_JSON_E_INCOMPLETE;
        }
    }

    // Validate final state
    if (st->state == JSON_STREAM_STATE_INIT) {
        // No input was provided
        st->state = JSON_STREAM_STATE_ERROR;
        if (err) {
            err->code = TEXT_JSON_E_INCOMPLETE;
            err->message = "No JSON value provided";
            err->offset = 0;
            err->line = 1;
            err->col = 1;
        }
        return TEXT_JSON_E_INCOMPLETE;
    }

    st->state = JSON_STREAM_STATE_DONE;
    return TEXT_JSON_OK;
}
