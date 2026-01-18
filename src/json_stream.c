/**
 * @file json_stream.c
 * @brief Streaming (incremental) JSON parser implementation
 *
 * Implements an event-based streaming parser that accepts input in chunks
 * and emits events for each JSON value encountered.
 */

#include "json_internal.h"
#include <text/json.h>
#include <text/json_stream.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Streaming parser state machine states
typedef enum {
    JSON_STREAM_STATE_INIT,        ///< Initial state, waiting for first value
    JSON_STREAM_STATE_VALUE,       ///< Parsing a value
    JSON_STREAM_STATE_ARRAY,       ///< Inside an array
    JSON_STREAM_STATE_OBJECT_KEY,  ///< Expecting object key
    JSON_STREAM_STATE_OBJECT_VALUE,///< Expecting object value (after key)
    JSON_STREAM_STATE_DONE,        ///< Parsing complete
    JSON_STREAM_STATE_ERROR        ///< Error state
} json_stream_state;

// Stack entry for tracking nesting
typedef struct {
    json_stream_state state;       ///< State when entering this level
    int is_array;                  ///< 1 if array, 0 if object
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

    // TODO: Process buffered input and emit events through callback
    // - Initialize lexer when we have enough input
    // - Tokenize and parse incrementally
    // - Emit events for each value encountered
    // - Handle string/number buffering (complete tokens before emitting)
    return TEXT_JSON_OK;
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

    // TODO: Complete parsing of any remaining buffered input
    // - Process any remaining tokens
    // - Validate structure is complete
    // - Emit final events if needed
    st->state = JSON_STREAM_STATE_DONE;
    return TEXT_JSON_OK;
}
