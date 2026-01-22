/**
 * @file json_stream.c
 * @brief Streaming (incremental) JSON parser implementation
 *
 * Implements an event-based streaming parser that accepts input in chunks
 * and emits events for each JSON value encountered.
 */

#include "json_internal.h"
#include "json_stream_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_stream.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

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

    // Token buffer for incomplete tokens (strings, numbers) spanning chunks
    json_token_buffer token_buffer;  ///< Buffer for incomplete tokens

    // Limits tracking
    size_t total_bytes_consumed;    ///< Total bytes processed
    size_t container_elem_count;    ///< Current container element count
};

// Grow a buffer if needed (uses unified buffer growth function)
static text_json_status json_stream_grow_buffer(
    char** buffer,
    size_t* capacity,
    size_t needed
) {
    return json_buffer_grow_unified(
        buffer,
        capacity,
        needed,
        JSON_BUFFER_GROWTH_SIMPLE,  // Simple doubling strategy
        0,                          // Use default initial size
        0,                          // Not used for simple strategy
        0,                          // Use default multiplier (2)
        0,                          // Not used for simple strategy
        1024                        // Add 1024 bytes headroom
    );
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
    // Check for overflow (for tracking purposes - if overflow, clamp to SIZE_MAX)
    if (st->buffer_start_offset > SIZE_MAX - st->input_buffer_processed) {
        st->buffer_start_offset = SIZE_MAX;
    } else {
        st->buffer_start_offset += st->input_buffer_processed;
    }
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
    // If we have an incomplete token but no input, we need to wait for more input
    // (finish() will handle completing incomplete tokens)
    if (st->input_buffer_used == 0) {
        return TEXT_JSON_OK;
    }

    // Initialize or reinitialize lexer with current buffer
    // We reinitialize after compacting to ensure the input pointer is valid
    text_json_status status = json_lexer_init(
        &st->lexer,
        st->input_buffer,
        st->input_buffer_used,
        &st->opts,
        1  // streaming mode
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
    // Set token buffer pointer for resumption support
    st->lexer.token_buffer = &st->token_buffer;
    st->lexer_initialized = 1;

    // If resuming from incomplete token, adjust lexer position and fix state if needed
    if (st->token_buffer.type != JSON_TOKEN_BUFFER_NONE) {
        // Token buffer is active - we need to resume parsing
        // The lexer should start from current_offset, which is already set correctly
        // by json_lexer_init. The parse functions will check token_buffer.type
        // and resume accordingly.
        // When resuming a value token (string or number), we should be in a state that expects a value,
        // not in VALUE state (which means we just processed a value and expect comma/closing bracket).
        // If we're in VALUE state, it means the state changed between when we detected the incomplete
        // token and now. We need to fix the state to expect a value.
        if (st->state == JSON_STREAM_STATE_VALUE &&
            (st->token_buffer.type == JSON_TOKEN_BUFFER_STRING || st->token_buffer.type == JSON_TOKEN_BUFFER_NUMBER)) {
            // We're resuming a value token, so we should be expecting a value
            // Determine the correct state based on context
            json_stream_stack_entry* top = json_stream_top(st);
            if (top && top->is_array) {
                // In an array, expecting a value
                st->state = JSON_STREAM_STATE_EXPECT_VALUE;
            } else if (top && !top->is_array) {
                // In an object, expecting a value after colon
                st->state = JSON_STREAM_STATE_EXPECT_VALUE;
            } else {
                // At root level, expecting first value
                st->state = JSON_STREAM_STATE_INIT;
            }
        }
    }

    // Process tokens until we can't continue (EOF or error)
    json_token token;
    while (1) {
        memset(&token, 0, sizeof(token));

        text_json_status status = json_lexer_next(&st->lexer, &token);
        if (status != TEXT_JSON_OK) {
            // Check for incomplete input (string or number spanning chunks, or partial keyword)
            if (status == TEXT_JSON_E_INCOMPLETE) {
                // Incomplete token - need more input
                // For strings/numbers: token buffer has the partial token data, input can be marked as processed
                // For keywords: no token buffer, partial keyword stays in input buffer, don't mark as processed
                if (st->token_buffer.type != JSON_TOKEN_BUFFER_NONE) {
                    // String or number - token buffer has the data
                    // The lexer has advanced current_offset to the end of the incomplete token
                    // Mark input as processed - the incomplete token data is in token_buffer, not input_buffer
                    // If current_offset == input_buffer_used, all input is processed and buffer will be reset
                    // This is correct - the next chunk will be appended to the (now empty) buffer
                    st->input_buffer_processed = st->lexer.current_offset;

                    // Ensure we don't exceed input_buffer_used
                    if (st->input_buffer_processed > st->input_buffer_used) {
                        st->input_buffer_processed = st->input_buffer_used;
                    }
                } else {
                    // Keyword - partial keyword stays in input buffer, don't mark as processed
                    // current_offset should not have advanced (lexer keeps partial keyword in buffer)
                    // Don't update input_buffer_processed - keep partial keyword in buffer
                }

                json_token_cleanup(&token);
                return TEXT_JSON_OK;
            }

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
                err->offset = st->buffer_start_offset + token.pos.offset;
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
            // Error occurred - state may have been set to ERROR
            return status;
        }

        // If we're in error or done state, stop processing
        if (st->state == JSON_STREAM_STATE_ERROR || st->state == JSON_STREAM_STATE_DONE) {
            return TEXT_JSON_OK;
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
                err->offset = st->buffer_start_offset + token->pos.offset;
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
                err->offset = st->buffer_start_offset + token->pos.offset;
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

    // Initialize token buffer
    json_token_buffer_init(&st->token_buffer);
    // Note: buffer is allocated on-demand, not here

    // Initialize stack
    st->stack_capacity = 16;
    st->stack = (json_stream_stack_entry*)malloc(
        st->stack_capacity * sizeof(json_stream_stack_entry)
    );
    if (!st->stack) {
        // Free token buffer if it was allocated (shouldn't be at this point, but be safe)
        if (st->token_buffer.buffer) {
            free(st->token_buffer.buffer);
        }
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
    // Free token buffer if it was allocated
    if (st->token_buffer.buffer) {
        free(st->token_buffer.buffer);
        st->token_buffer.buffer = NULL;
    }
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
    text_json_status process_status = json_stream_process_tokens(st, err);
    if (process_status == TEXT_JSON_E_INCOMPLETE) {
        // This should never happen - json_stream_process_tokens should return TEXT_JSON_OK for incomplete tokens
        // Convert to TEXT_JSON_OK since incomplete tokens are expected in streaming mode
        return TEXT_JSON_OK;
    }
    return process_status;
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

    // Complete parsing of any remaining buffered input
    // First, compact buffer to ensure unprocessed data is at the start
    json_stream_compact_buffer(st);

    text_json_status status = json_stream_process_tokens(st, err);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    // Check if we have an incomplete token in the buffer that needs to be force-completed
    // This can happen even if there's no unprocessed input (the input was already marked as processed)
    if (st->token_buffer.type != JSON_TOKEN_BUFFER_NONE) {
        // We have an incomplete token in the buffer - force complete it
        // by temporarily disabling streaming mode and trying again
        int old_streaming_mode = st->lexer.streaming_mode;
        st->lexer.streaming_mode = 0;  // Force complete mode

        // Reinitialize lexer with any remaining unprocessed input
        // If there's unprocessed input, it's at the start after compaction
        size_t unprocessed_len = st->input_buffer_used - st->input_buffer_processed;

        // Defensive check: ensure unprocessed_len doesn't exceed buffer size
        if (unprocessed_len > st->input_buffer_size) {
            st->lexer.streaming_mode = old_streaming_mode;  // Restore
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = TEXT_JSON_E_INVALID;
                err->message = "Invalid buffer state";
                err->offset = st->total_bytes_consumed;
                err->line = 1;
                err->col = 1;
            }
            return TEXT_JSON_E_INVALID;
        }

        // Reinitialize lexer - if there's unprocessed input, use it; otherwise use empty buffer
        status = json_lexer_init(
            &st->lexer,
            st->input_buffer,
            unprocessed_len,
            &st->opts,
            0  // force complete mode (not streaming)
        );
        if (status != TEXT_JSON_OK) {
            st->lexer.streaming_mode = old_streaming_mode;  // Restore
            st->state = JSON_STREAM_STATE_ERROR;
            if (err) {
                err->code = status;
                err->message = "Failed to reinitialize lexer for completion";
                err->offset = st->total_bytes_consumed;
                err->line = 1;
                err->col = 1;
            }
            return status;
        }
        // Ensure streaming_mode is set correctly
        st->lexer.streaming_mode = 0;
        // Set token buffer pointer - we'll try to complete the incomplete token
        st->lexer.token_buffer = &st->token_buffer;
        st->lexer_initialized = 1;

        // Process all remaining tokens (including the completed incomplete token)
        // After compaction, input_buffer_processed is 0, so we start from the beginning
        json_token token;
        while (1) {
            memset(&token, 0, sizeof(token));
            status = json_lexer_next(&st->lexer, &token);
            if (status != TEXT_JSON_OK) {
                // If still incomplete or error, it's a real problem
                st->lexer.streaming_mode = old_streaming_mode;  // Restore
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = status;
                    err->message = "Incomplete or invalid JSON input";
                    // Check for overflow in offset calculation (for error reporting)
                    if (st->buffer_start_offset > SIZE_MAX - token.pos.offset) {
                        err->offset = SIZE_MAX;
                    } else {
                        err->offset = st->buffer_start_offset + token.pos.offset;
                    }
                    err->line = token.pos.line;
                    err->col = token.pos.col;
                }
                json_token_cleanup(&token);
                return status;
            }

            // Handle EOF
            if (token.type == JSON_TOKEN_EOF) {
                // Update processed offset before breaking
                // Safe: lexer.current_offset is always <= lexer.input_len (which is unprocessed_len)
                if (st->lexer.current_offset > unprocessed_len) {
                    // Defensive check
                    st->lexer.streaming_mode = old_streaming_mode;  // Restore
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_INVALID;
                        err->message = "Lexer offset out of bounds";
                        err->offset = st->buffer_start_offset;
                        err->line = 1;
                        err->col = 1;
                    }
                    json_token_cleanup(&token);
                    return TEXT_JSON_E_INVALID;
                }
                st->input_buffer_processed = st->lexer.current_offset;
                json_token_cleanup(&token);
                break;
            }

            // Adjust token position to be relative to original buffer start
            // token.pos.offset is relative to the reinitialized buffer start (which is st->input_buffer)
            // After compaction, buffer_start_offset points to where the unprocessed data originally started
            // Check for overflow in offset calculation (for error reporting - not critical but avoids UB)
            json_token adjusted_token = token;
            if (st->buffer_start_offset > SIZE_MAX - token.pos.offset) {
                // Overflow in offset calculation - clamp to SIZE_MAX for error reporting
                adjusted_token.pos.offset = SIZE_MAX;
            } else {
                adjusted_token.pos.offset = st->buffer_start_offset + token.pos.offset;
            }

            // Process token
            status = json_stream_handle_token(st, &adjusted_token, err);
            json_token_cleanup(&token);

            if (status != TEXT_JSON_OK) {
                st->lexer.streaming_mode = old_streaming_mode;  // Restore
                return status;
            }

            // Update processed offset after successful token processing
            // After compaction, we start from 0, so current_offset is the amount processed
            // Safe: lexer.current_offset is always <= lexer.input_len (which is unprocessed_len)
            // and unprocessed_len <= st->input_buffer_used, so this assignment is safe
            if (st->lexer.current_offset > unprocessed_len) {
                // Defensive check: current_offset should not exceed what we gave the lexer
                st->lexer.streaming_mode = old_streaming_mode;  // Restore
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Lexer offset out of bounds";
                    err->offset = st->buffer_start_offset;
                    err->line = 1;
                    err->col = 1;
                }
                return TEXT_JSON_E_INVALID;
            }
            st->input_buffer_processed = st->lexer.current_offset;

            // If we're in error or done state, stop processing
            if (st->state == JSON_STREAM_STATE_ERROR || st->state == JSON_STREAM_STATE_DONE) {
                break;
            }
        }

        st->lexer.streaming_mode = old_streaming_mode;  // Restore
    }

    // Check if we have unprocessed input that needs to be force-completed
    // This can happen even if state is INIT (when an incomplete token was encountered)
    if (st->input_buffer_used > st->input_buffer_processed) {
            // Have unprocessed input - might be incomplete token
            // Since finish() was called, we have all input, so force completion
            // by temporarily disabling streaming mode and trying again
            int old_streaming_mode = st->lexer.streaming_mode;
            st->lexer.streaming_mode = 0;  // Force complete mode

            // Reinitialize lexer to start from unprocessed position
            // First, ensure we have unprocessed data to work with
            // Safe subtraction: we already checked st->input_buffer_used > st->input_buffer_processed
            size_t unprocessed_len = st->input_buffer_used - st->input_buffer_processed;

            // Defensive check: ensure unprocessed_len doesn't exceed buffer size
            if (unprocessed_len > st->input_buffer_size) {
                st->lexer.streaming_mode = old_streaming_mode;  // Restore
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = TEXT_JSON_E_INVALID;
                    err->message = "Invalid buffer state";
                    err->offset = st->total_bytes_consumed;
                    err->line = 1;
                    err->col = 1;
                }
                return TEXT_JSON_E_INVALID;
            }

            if (unprocessed_len == 0) {
                // No unprocessed data, nothing to do
                st->lexer.streaming_mode = old_streaming_mode;  // Restore
                // Continue to next check
            } else {

            // After compaction, unprocessed data is at the start of the buffer
            // and input_buffer_processed should be 0
            // So we use st->input_buffer directly (not st->input_buffer + st->input_buffer_processed)
            // Safe: unprocessed_len <= st->input_buffer_size, so st->input_buffer + unprocessed_len is valid
            status = json_lexer_init(
                &st->lexer,
                st->input_buffer,
                unprocessed_len,
                &st->opts,
                0  // force complete mode (not streaming)
            );
            if (status != TEXT_JSON_OK) {
                st->lexer.streaming_mode = old_streaming_mode;  // Restore
                st->state = JSON_STREAM_STATE_ERROR;
                if (err) {
                    err->code = status;
                    err->message = "Failed to reinitialize lexer for completion";
                    err->offset = st->total_bytes_consumed;
                    err->line = 1;
                    err->col = 1;
                }
                return status;
            }
            // Ensure streaming_mode is set correctly (json_lexer_init should have set it, but be explicit)
            st->lexer.streaming_mode = 0;
            // Set token buffer pointer - if there's an incomplete token, we'll try to complete it
            st->lexer.token_buffer = &st->token_buffer;
            st->lexer_initialized = 1;

            // Process the remaining input with force-complete mode
            // After compaction, input_buffer_processed is 0, so we start from the beginning
            json_token token;
            while (1) {
                memset(&token, 0, sizeof(token));
                status = json_lexer_next(&st->lexer, &token);
                if (status != TEXT_JSON_OK) {
                    // If still incomplete or error, it's a real problem
                    st->lexer.streaming_mode = old_streaming_mode;  // Restore
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = status;
                        err->message = "Incomplete or invalid JSON input";
                        // Check for overflow in offset calculation (for error reporting)
                        if (st->buffer_start_offset > SIZE_MAX - token.pos.offset) {
                            err->offset = SIZE_MAX;
                        } else {
                            err->offset = st->buffer_start_offset + token.pos.offset;
                        }
                        err->line = token.pos.line;
                        err->col = token.pos.col;
                    }
                    json_token_cleanup(&token);
                    return status;
                }

                // Handle EOF
                if (token.type == JSON_TOKEN_EOF) {
                    // Update processed offset before breaking
                    // Safe: lexer.current_offset is always <= lexer.input_len (which is unprocessed_len)
                    if (st->lexer.current_offset > unprocessed_len) {
                        // Defensive check
                        st->lexer.streaming_mode = old_streaming_mode;  // Restore
                        st->state = JSON_STREAM_STATE_ERROR;
                        if (err) {
                            err->code = TEXT_JSON_E_INVALID;
                            err->message = "Lexer offset out of bounds";
                            err->offset = st->buffer_start_offset;
                            err->line = 1;
                            err->col = 1;
                        }
                        json_token_cleanup(&token);
                        return TEXT_JSON_E_INVALID;
                    }
                    st->input_buffer_processed = st->lexer.current_offset;
                    json_token_cleanup(&token);
                    break;
                }

                // Adjust token position to be relative to original buffer start
                // token.pos.offset is relative to the reinitialized buffer start (which is st->input_buffer)
                // After compaction, buffer_start_offset points to where the unprocessed data originally started
                // Check for overflow in offset calculation (for error reporting - not critical but avoids UB)
                json_token adjusted_token = token;
                if (st->buffer_start_offset > SIZE_MAX - token.pos.offset) {
                    // Overflow in offset calculation - clamp to SIZE_MAX for error reporting
                    adjusted_token.pos.offset = SIZE_MAX;
                } else {
                    adjusted_token.pos.offset = st->buffer_start_offset + token.pos.offset;
                }

                // Process token
                status = json_stream_handle_token(st, &adjusted_token, err);
                json_token_cleanup(&token);

                if (status != TEXT_JSON_OK) {
                    st->lexer.streaming_mode = old_streaming_mode;  // Restore
                    return status;
                }

                // Update processed offset after successful token processing
                // After compaction, we start from 0, so current_offset is the amount processed
                // Safe: lexer.current_offset is always <= lexer.input_len (which is unprocessed_len)
                // and unprocessed_len <= st->input_buffer_used, so this assignment is safe
                if (st->lexer.current_offset > unprocessed_len) {
                    // Defensive check: current_offset should not exceed what we gave the lexer
                    st->lexer.streaming_mode = old_streaming_mode;  // Restore
                    st->state = JSON_STREAM_STATE_ERROR;
                    if (err) {
                        err->code = TEXT_JSON_E_INVALID;
                        err->message = "Lexer offset out of bounds";
                        err->offset = st->buffer_start_offset;
                        err->line = 1;
                        err->col = 1;
                    }
                    return TEXT_JSON_E_INVALID;
                }
                st->input_buffer_processed = st->lexer.current_offset;

                // If we're in error or done state, stop processing
                if (st->state == JSON_STREAM_STATE_ERROR || st->state == JSON_STREAM_STATE_DONE) {
                    break;
                }
            }
            st->lexer.streaming_mode = old_streaming_mode;  // Restore
            }
    }

    // Validate that structure is complete (no unmatched brackets)
    // Check this AFTER processing all remaining tokens
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

    // Validate final state
    // After force-completion, state should be DONE if we successfully processed a value
    // Only report INIT as error if we truly have no input (buffer is empty)
    if (st->state == JSON_STREAM_STATE_INIT && st->input_buffer_used == 0) {
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
