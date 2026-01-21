/**
 * @file csv_stream.c
 * @brief Streaming CSV parser implementation
 *
 * Implements an event-based streaming parser that accepts input in chunks
 * and emits events for each CSV record/field encountered.
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

// Forward declaration of parser state (simplified version for streaming)
typedef enum {
    CSV_STREAM_STATE_START_OF_RECORD,
    CSV_STREAM_STATE_START_OF_FIELD,
    CSV_STREAM_STATE_UNQUOTED_FIELD,
    CSV_STREAM_STATE_QUOTED_FIELD,
    CSV_STREAM_STATE_QUOTE_IN_QUOTED,
    CSV_STREAM_STATE_ESCAPE_IN_QUOTED,
    CSV_STREAM_STATE_COMMENT,
    CSV_STREAM_STATE_END
} csv_stream_state;

// Streaming parser structure (internal)
struct text_csv_stream {
    // Configuration
    text_csv_parse_options opts;
    text_csv_event_cb callback;
    void* user_data;

    // State machine
    csv_stream_state state;
    bool in_record;
    size_t field_count;
    size_t row_count;

    // Input buffering (for fields spanning chunks)
    char* input_buffer;
    size_t input_buffer_size;
    size_t input_buffer_used;
    size_t input_buffer_processed;
    size_t buffer_start_offset;

    // Position tracking
    csv_position pos;
    size_t total_bytes_consumed;

    // Field accumulation
    const char* field_start;         ///< Start of current field (pointer into current chunk, or field_buffer if buffered)
    size_t field_length;             ///< Length of current field
    size_t field_start_offset;       ///< Offset in current chunk where field started (SIZE_MAX if field already buffered)
    bool field_is_quoted;            ///< Whether field is quoted
    bool field_needs_copy;           ///< Whether field needs copying (for escaping)
    char* field_buffer;              ///< Buffer for field data (when escaping needed or spanning chunks)
    size_t field_buffer_size;
    size_t field_buffer_used;
    bool field_is_buffered;          ///< Whether field data is in field_buffer (not in current chunk)
    bool just_processed_doubled_quote; ///< Whether we just processed a doubled quote (allows delimiter to end field)
    bool quote_in_quoted_at_chunk_boundary; ///< Whether we transitioned to QUOTE_IN_QUOTED at end of previous chunk

    // Limits
    size_t max_rows;
    size_t max_cols;
    size_t max_field_bytes;
    size_t max_record_bytes;
    size_t max_total_bytes;
    size_t current_record_bytes;

    // Comment handling
    bool in_comment;
    size_t comment_prefix_len;

    // In-situ mode tracking (for table parsing)
    const char* original_input_buffer;  ///< Original input buffer (caller-owned, for in-situ mode)
    size_t original_input_buffer_len;   ///< Length of original input buffer

    // Error state
    text_csv_error error;
};

// Get effective limit value
static size_t csv_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Forward declarations
static text_csv_status csv_stream_grow_field_buffer(
    text_csv_stream* stream,
    size_t needed
);
static text_csv_status csv_stream_append_to_field_buffer(
    text_csv_stream* stream,
    const char* data,
    size_t data_len
);

// Buffer field data when reaching end of chunk in middle of field
// When a field spans chunks, we need to copy the partial field data from the current
// chunk into field_buffer so it remains valid when the chunk is cleared.
static text_csv_status csv_stream_buffer_field_at_chunk_boundary(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t field_start_offset,
    size_t current_offset
) {
    // If field is already buffered, just append new data
    if (stream->field_is_buffered) {
        // Append data from field_start_offset to current_offset
        if (field_start_offset < current_offset && field_start_offset < process_len && field_start_offset != SIZE_MAX) {
            size_t append_len = current_offset - field_start_offset;
            if (append_len > process_len - field_start_offset) {
                append_len = process_len - field_start_offset;
            }
            if (append_len > 0) {
                text_csv_status status = csv_stream_append_to_field_buffer(stream,
                    process_input + field_start_offset, append_len);
                if (status != TEXT_CSV_OK) {
                    return status;
                }
            }
        }
        stream->field_start = stream->field_buffer;
        stream->field_length = stream->field_buffer_used;
        return TEXT_CSV_OK;
    }

    // Field not yet buffered - copy from process_input to field_buffer
    if (field_start_offset < current_offset && field_start_offset < process_len && field_start_offset != SIZE_MAX) {
        size_t copy_len = current_offset - field_start_offset;
        if (copy_len > process_len - field_start_offset) {
            copy_len = process_len - field_start_offset;
        }
        if (copy_len > 0) {
            text_csv_status status = csv_stream_grow_field_buffer(stream, copy_len);
            if (status != TEXT_CSV_OK) {
                return status;
            }
            memcpy(stream->field_buffer, process_input + field_start_offset, copy_len);
            stream->field_buffer_used = copy_len;
            stream->field_is_buffered = true;
            stream->field_start = stream->field_buffer;
            stream->field_length = stream->field_buffer_used;
        } else {
            // Empty field - just allocate buffer
            text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
            if (status != TEXT_CSV_OK) {
                return status;
            }
            stream->field_buffer_used = 0;
            stream->field_is_buffered = true;
            stream->field_start = stream->field_buffer;
            stream->field_length = 0;
        }
    } else {
        // Empty field or invalid offset
        text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
        if (status != TEXT_CSV_OK) {
            return status;
        }
        stream->field_buffer_used = 0;
        stream->field_is_buffered = true;
        stream->field_start = stream->field_buffer;
        stream->field_length = 0;
    }
    return TEXT_CSV_OK;
}

// Grow input buffer (kept for compatibility, but not used in new algorithm)
static text_csv_status csv_stream_grow_input_buffer(
    text_csv_stream* stream,
    size_t needed
) {
    if (stream->input_buffer_size >= needed) {
        return TEXT_CSV_OK;
    }

    size_t new_size = stream->input_buffer_size * 2;
    if (new_size < needed) {
        new_size = needed;
    }

    if (new_size < stream->input_buffer_size) {
        return TEXT_CSV_E_OOM;
    }

    char* new_buffer = realloc(stream->input_buffer, new_size);
    if (!new_buffer) {
        return TEXT_CSV_E_OOM;
    }

    // Update field_start if it was pointing into the old input buffer
    if (!stream->field_is_buffered && stream->input_buffer && stream->field_start &&
        stream->field_start >= stream->input_buffer &&
        stream->field_start < stream->input_buffer + stream->input_buffer_size) {
        // field_start points into the old input buffer - update it
        size_t offset = stream->field_start - stream->input_buffer;
        stream->field_start = new_buffer + offset;
    }
    stream->input_buffer = new_buffer;
    stream->input_buffer_size = new_size;
    return TEXT_CSV_OK;
}

// Grow field buffer
static text_csv_status csv_stream_grow_field_buffer(
    text_csv_stream* stream,
    size_t needed
) {
    if (stream->field_buffer_size >= needed) {
        return TEXT_CSV_OK;
    }

    size_t new_size;
    if (stream->field_buffer_size == 0) {
        // Initial allocation - start with at least the needed size
        new_size = needed;
    } else {
        // Grow by at least 2x, or to needed size
        new_size = stream->field_buffer_size * 2;
        if (new_size < needed) {
            new_size = needed;
        }
    }

    if (new_size < stream->field_buffer_size) {
        return TEXT_CSV_E_OOM;
    }

    char* new_buffer = realloc(stream->field_buffer, new_size);
    if (!new_buffer) {
        return TEXT_CSV_E_OOM;
    }

    // Update field_start if it was pointing to the old buffer
    if (stream->field_is_buffered && stream->field_start == stream->field_buffer) {
        stream->field_start = new_buffer;
    }
    stream->field_buffer = new_buffer;
    stream->field_buffer_size = new_size;
    return TEXT_CSV_OK;
}

// Set stream error
static text_csv_status csv_stream_set_error(
    text_csv_stream* stream,
    text_csv_status code,
    const char* message
) {
    // Free any existing context snippet
    if (stream->error.context_snippet) {
        free(stream->error.context_snippet);
        stream->error.context_snippet = NULL;
    }

    // Set error fields first
    stream->error.code = code;
    stream->error.message = message;
    stream->error.byte_offset = stream->pos.offset;
    stream->error.line = stream->pos.line;
    stream->error.column = stream->pos.column;
    stream->error.row_index = stream->row_count;
    stream->error.col_index = stream->field_count;
    stream->error.context_snippet = NULL;
    stream->error.context_snippet_len = 0;
    stream->error.caret_offset = 0;

    // Generate context snippet if we have input buffer access
    const char* input_for_snippet = NULL;
    size_t input_len_for_snippet = 0;
    size_t error_offset = stream->error.byte_offset;

    // Prefer original_input_buffer (full input for table parsing)
    if (stream->original_input_buffer && stream->original_input_buffer_len > 0) {
        input_for_snippet = stream->original_input_buffer;
        input_len_for_snippet = stream->original_input_buffer_len;
        // Error offset is already relative to the original input buffer
    }
    // Fall back to buffered input if available
    else if (stream->input_buffer && stream->input_buffer_used > 0) {
        input_for_snippet = stream->input_buffer;
        input_len_for_snippet = stream->input_buffer_used;
        // For buffered input, we need to adjust the error offset
        // The error offset is relative to the total bytes consumed, but the buffer
        // only contains recent data. For now, clamp to buffer bounds.
        if (error_offset > input_len_for_snippet) {
            error_offset = input_len_for_snippet;
        }
    }

    if (input_for_snippet && input_len_for_snippet > 0 && error_offset <= input_len_for_snippet) {
        char* snippet = NULL;
        size_t snippet_len = 0;
        size_t caret_offset = 0;

        text_csv_status snippet_status = csv_error_generate_context_snippet(
            input_for_snippet,
            input_len_for_snippet,
            error_offset,
            CSV_DEFAULT_CONTEXT_RADIUS_BYTES,
            CSV_DEFAULT_CONTEXT_RADIUS_BYTES,
            &snippet,
            &snippet_len,
            &caret_offset
        );

        if (snippet_status == TEXT_CSV_OK && snippet) {
            stream->error.context_snippet = snippet;
            stream->error.context_snippet_len = snippet_len;
            stream->error.caret_offset = caret_offset;
        }
    }

    stream->state = CSV_STREAM_STATE_END;
    return code;
}

// Unescape doubled quotes in field data
// Converts doubled quotes ("") to single quotes (") in the field data.
// The result is written to the field_buffer, which must be large enough.
static text_csv_status csv_stream_unescape_field(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
) {
    // For quoted fields with doubled-quote escape, we always need to check for doubled quotes
    // even if field_needs_copy is false (doubled quotes may have been detected during parsing
    // or may exist in the data from previous chunks)
    bool needs_unescape = stream->field_needs_copy;
    if (!needs_unescape && stream->field_is_quoted &&
        stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE) {
        // Check if there are any doubled quotes in the data
        size_t check_len = input_len;
        if (input_data == stream->field_buffer) {
            check_len = stream->field_buffer_used < input_len ? stream->field_buffer_used : input_len;
        }
        for (size_t i = 0; i + 1 < check_len; i++) {
            if (input_data[i] == stream->opts.dialect.quote &&
                input_data[i + 1] == stream->opts.dialect.quote) {
                needs_unescape = true;
                break;
            }
        }
    }

    if (!needs_unescape) {
        // No unescaping needed, but we still need to ensure the data is in a stable buffer
        // If input_data points to field_buffer, we can return it directly
        // But we must use field_buffer_used as the actual length, not input_len
        if (input_data == stream->field_buffer) {
            // When input_data is field_buffer, use field_buffer_used as the actual length
            // This ensures we never read past the actual data in the buffer
            // Use the minimum of input_len and field_buffer_used to prevent reading past buffer
            size_t actual_len = stream->field_buffer_used;
            if (input_len < actual_len) {
                actual_len = input_len;
            }
            // Also ensure it doesn't exceed buffer size
            if (actual_len > stream->field_buffer_size) {
                actual_len = stream->field_buffer_size;
            }
            *output_data = input_data;
            *output_len = actual_len;
            return TEXT_CSV_OK;
        }

        // Check if input_data points to the original input buffer (for in-situ mode)
        if (stream->opts.in_situ_mode && stream->original_input_buffer && input_data) {
            const char* input_start = stream->original_input_buffer;
            size_t input_buffer_len = stream->original_input_buffer_len;
            const char* field_start = input_data;
            size_t field_len = input_len;

            // Check for pointer arithmetic overflow safety
            // Use subtraction to check bounds instead of addition to avoid overflow
            if (field_start >= input_start) {
                size_t offset_from_start = (size_t)(field_start - input_start);
                // Check that field fits within buffer and doesn't overflow
                if (offset_from_start <= input_buffer_len &&
                    field_len <= input_buffer_len - offset_from_start) {
                    // Can use in-situ mode: return original pointer
                    *output_data = input_data;
                    *output_len = input_len;
                    return TEXT_CSV_OK;
                }
            }
        }

        // Input is not in field_buffer and not in original input - copy it to ensure stability
        // Safety check: ensure input_data is valid (not NULL)
        if (!input_data) {
            return TEXT_CSV_E_INVALID;
        }
        // Safety check: check for integer overflow in allocation size
        if (input_len > SIZE_MAX - 1) {
            return TEXT_CSV_E_OOM;
        }
        // Safety check: ensure input_len is reasonable
        if (input_len > stream->field_buffer_size && stream->field_buffer_size > 0) {
            // If input_len is larger than current buffer, we need to grow it
            text_csv_status status = csv_stream_grow_field_buffer(stream, input_len + 1);
            if (status != TEXT_CSV_OK) {
                return status;
            }
        } else if (stream->field_buffer_size == 0 || stream->field_buffer_size < input_len + 1) {
            // Initial allocation or need to grow
            text_csv_status status = csv_stream_grow_field_buffer(stream, input_len + 1);
            if (status != TEXT_CSV_OK) {
                return status;
            }
        }
        // Only copy the actual data length, not more
        size_t copy_len = input_len;
        if (copy_len > stream->field_buffer_size) {
            copy_len = stream->field_buffer_size;
        }
        // Safety check: ensure we have a valid buffer before copying
        if (!stream->field_buffer || copy_len == 0) {
            *output_data = stream->field_buffer;
            *output_len = 0;
            return TEXT_CSV_OK;
        }
        memcpy(stream->field_buffer, input_data, copy_len);
        // Reset field_buffer_used to the actual copied length
        stream->field_buffer_used = copy_len;
        *output_data = stream->field_buffer;
        *output_len = copy_len;
        return TEXT_CSV_OK;
    }

    // Check if input_data points to field_buffer - if so, we need to handle in-place operation
    bool input_is_field_buffer = (input_data == stream->field_buffer);
    const char* actual_input = input_data;  // Local variable to track actual input pointer

    // Ensure field_buffer is large enough for output (worst case: same size)
    size_t needed_size = input_len;

    // If input is in field_buffer and we need to grow, we must copy the data first
    if (input_is_field_buffer && stream->field_buffer_size < needed_size) {
        // Safety: clamp input_len to actual valid data in buffer
        size_t copy_len = input_len;
        if (copy_len > stream->field_buffer_used) {
            copy_len = stream->field_buffer_used;
        }
        // Save the current data before reallocation
        char* temp_buffer = malloc(copy_len);
        if (!temp_buffer) {
            return TEXT_CSV_E_OOM;
        }
        memcpy(temp_buffer, stream->field_buffer, copy_len);

        // Now grow the buffer
        text_csv_status status = csv_stream_grow_field_buffer(stream, needed_size);
        if (status != TEXT_CSV_OK) {
            free(temp_buffer);
            return status;
        }

        // Restore the data
        memcpy(stream->field_buffer, temp_buffer, copy_len);
        free(temp_buffer);
        actual_input = stream->field_buffer;  // Update pointer after reallocation
        // Update field_buffer_used to reflect the copied data
        stream->field_buffer_used = copy_len;
    } else if (!input_is_field_buffer) {
        // Input is not in field_buffer - safe to grow
        text_csv_status status = csv_stream_grow_field_buffer(stream, needed_size);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    // CRITICAL: When unescaping in-place, we must not read past the actual valid data
    // Clamp input_len to field_buffer_used if input is in field_buffer
    size_t actual_input_len = input_len;
    if (input_is_field_buffer && input_len > stream->field_buffer_used) {
        actual_input_len = stream->field_buffer_used;
    }

    // Unescape doubled quotes (in-place if input is field_buffer, otherwise copy)
    size_t out_idx = 0;
    for (size_t in_idx = 0; in_idx < actual_input_len; in_idx++) {
        if (stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE &&
            in_idx + 1 < actual_input_len &&
            actual_input[in_idx] == stream->opts.dialect.quote &&
            actual_input[in_idx + 1] == stream->opts.dialect.quote) {
            // Doubled quote - output single quote
            if (out_idx >= stream->field_buffer_size) {
                return TEXT_CSV_E_OOM;
            }
            stream->field_buffer[out_idx++] = stream->opts.dialect.quote;
            in_idx++;  // Skip second quote
        } else {
            // Regular character
            if (out_idx >= stream->field_buffer_size) {
                return TEXT_CSV_E_OOM;
            }
            stream->field_buffer[out_idx++] = actual_input[in_idx];
        }
    }

    // Safety check: ensure output length doesn't exceed buffer size
    // This should never happen due to bounds checking in the loop above, but be defensive
    if (out_idx > stream->field_buffer_size) {
        out_idx = stream->field_buffer_size;
    }
    // Additional check: if input was in field_buffer, out_idx should not exceed what we read
    if (input_is_field_buffer && out_idx > actual_input_len) {
        // This should never happen (unescaping can only shrink, not grow)
        // But be defensive
        out_idx = actual_input_len;
    }

    *output_data = stream->field_buffer;
    *output_len = out_idx;

    // Update field_buffer_used to reflect the actual unescaped data length
    // This is critical when unescaping in-place in field_buffer
    // Update AFTER setting output pointers but the callback is synchronous so this is safe
    if (input_is_field_buffer) {
        stream->field_buffer_used = out_idx;
    } else if (*output_data == stream->field_buffer) {
        // Output is in field_buffer (we copied input to field_buffer)
        stream->field_buffer_used = out_idx;
    }

    return TEXT_CSV_OK;
}

// Emit an event
static text_csv_status csv_stream_emit_event(
    text_csv_stream* stream,
    text_csv_event_type type,
    const char* data,
    size_t data_len
) {
    if (!stream->callback) {
        return TEXT_CSV_OK;
    }

    text_csv_event event;
    event.type = type;
    event.data = data;
    event.data_len = data_len;
    event.row_index = stream->row_count;
    event.col_index = stream->field_count;

    return stream->callback(&event, stream->user_data);
}

/**
 * @brief Check if at start of comment line
 */
static bool csv_stream_is_comment_start(
    text_csv_stream* stream,
    const char* input,
    size_t input_len,
    size_t offset
) {
    if (!stream->opts.dialect.allow_comments || stream->comment_prefix_len == 0) {
        return false;
    }

    if (stream->field_count > 0 || stream->in_record) {
        return false;
    }

    if (offset + stream->comment_prefix_len > input_len) {
        return false;
    }

    return memcmp(input + offset, stream->opts.dialect.comment_prefix, stream->comment_prefix_len) == 0;
}

// Append data to field buffer
static text_csv_status csv_stream_append_to_field_buffer(
    text_csv_stream* stream,
    const char* data,
    size_t len
) {
    if (len == 0) {
        return TEXT_CSV_OK;
    }

    // Grow buffer if needed
    // Check for overflow before addition
    if (len > SIZE_MAX - stream->field_buffer_used ||
        stream->field_buffer_used + len > stream->field_buffer_size) {
        size_t new_size;
        if (stream->field_buffer_size == 0) {
            // Initial allocation - start with at least the needed size
            if (len > SIZE_MAX - stream->field_buffer_used) {
                return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Field buffer size overflow");
            }
            new_size = stream->field_buffer_used + len;
        } else {
            // Grow by at least 2x, or to needed size
            new_size = stream->field_buffer_size * 2;
            // Check for overflow in new_size calculation
            if (new_size < stream->field_buffer_size) {
                return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Field buffer size overflow");
            }
            // Check for overflow in needed size
            if (len > SIZE_MAX - stream->field_buffer_used) {
                return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Field buffer size overflow");
            }
            size_t needed = stream->field_buffer_used + len;
            if (new_size < needed) {
                new_size = needed;
            }
        }

        if (new_size < stream->field_buffer_size) {
            return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Field buffer size overflow");
        }

        char* new_buffer = realloc(stream->field_buffer, new_size);
        if (!new_buffer) {
            return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Failed to allocate field buffer");
        }

        // Update field_start if it was pointing to the old buffer
        if (stream->field_is_buffered && stream->field_start == stream->field_buffer) {
            stream->field_start = new_buffer;
        }
        stream->field_buffer = new_buffer;
        stream->field_buffer_size = new_size;
    }

    // Safety check: ensure we don't write past buffer
    if (stream->field_buffer_used + len > stream->field_buffer_size) {
        return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Field buffer overflow");
    }

    memcpy(stream->field_buffer + stream->field_buffer_used, data, len);
    // Overflow already checked above
    stream->field_buffer_used += len;

    // Safety check: ensure field_buffer_used never exceeds field_buffer_size
    if (stream->field_buffer_used > stream->field_buffer_size) {
        stream->field_buffer_used = stream->field_buffer_size;
        return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Field buffer used exceeds size");
    }

    return TEXT_CSV_OK;
}

// Process a chunk of input data
// Algorithm:
// 1. Always process the chunk directly (no input buffering for combining chunks)
// 2. Process character-by-character according to the state machine
// 3. When a field is complete, emit it (copying to field_buffer if needed for unescaping)
// 4. If end of chunk is reached while in the middle of a field:
//    - Remember the current state
//    - Copy field data from field_start to end of chunk into field_buffer
//    - Set field_is_buffered = true
//    - Return to wait for next chunk
// 5. When next chunk arrives:
//    - Start processing from the saved state
//    - Continue accumulating into field_buffer (if field spans chunks)
//    - When field ends, emit the complete field from field_buffer
//    - Clear field_buffer and continue processing the rest of the chunk
//
// Key insight: We only buffer FIELD DATA when it spans chunks, not the input chunks themselves.
// This avoids the complexity of reprocessing and state conflicts.
static text_csv_status csv_stream_process_chunk(
    text_csv_stream* stream,
    const char* input,
    size_t input_len
) {
    // Always process the chunk directly - no input buffering
    const char* process_input = input;
    size_t process_len = input_len;
    size_t offset = 0;

    while (offset < process_len && stream->state != CSV_STREAM_STATE_END) {
        char c = process_input[offset];
        size_t byte_pos = offset;

        // Check limits
        if (stream->total_bytes_consumed >= stream->max_total_bytes) {
            return csv_stream_set_error(stream, TEXT_CSV_E_LIMIT, "Maximum total bytes exceeded");
        }

        if (stream->in_record) {
            stream->current_record_bytes++;
            if (stream->current_record_bytes > stream->max_record_bytes) {
                return csv_stream_set_error(stream, TEXT_CSV_E_LIMIT, "Maximum record bytes exceeded");
            }
        }

        switch (stream->state) {
            case CSV_STREAM_STATE_START_OF_RECORD: {
                // Check for comment
                if (csv_stream_is_comment_start(stream, process_input, process_len, byte_pos)) {
                    stream->state = CSV_STREAM_STATE_COMMENT;
                    stream->in_comment = true;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                // Check for newline at start of record - skip trailing empty records
                csv_position pos_before = stream->pos;
                pos_before.offset = byte_pos;
                csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                if (nl != CSV_NEWLINE_NONE) {
                    // Skip the newline without creating a record
                    size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    stream->pos.offset += (pos_before.offset - byte_pos);
                    stream->pos.line = pos_before.line;
                    stream->pos.column = pos_before.column;
                        stream->total_bytes_consumed += newline_bytes;
                        offset = pos_before.offset;
                        // Mark data as processed up to and including the newline
                        // This is unambiguously recognized - the record is complete
                        continue;
                }

                // Emit RECORD_BEGIN
                text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_BEGIN, NULL, 0);
                if (status != TEXT_CSV_OK) {
                    return status;
                }

                stream->state = CSV_STREAM_STATE_START_OF_FIELD;
                stream->in_record = true;
                stream->current_record_bytes = 0;
                stream->field_count = 0;
                // Fall through
            }
            // Fall through
            case CSV_STREAM_STATE_START_OF_FIELD: {
                if (stream->field_count >= stream->max_cols) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_TOO_MANY_COLS, "Too many columns in record");
                }

                // Clear any previous field buffering
                stream->field_buffer_used = 0;
                stream->field_is_buffered = false;
                // Validate byte_pos before setting field_start
                if (byte_pos > process_len) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Invalid byte position");
                }
                // Ensure process_input is valid
                if (!process_input) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "process_input is NULL");
                }
                stream->field_start = process_input + byte_pos;
                // Safety check: ensure field_start is within bounds
                if (stream->field_start < process_input ||
                    stream->field_start > process_input + process_len) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "field_start out of bounds");
                }
                stream->field_length = 0;
                stream->field_is_quoted = false;
                stream->field_needs_copy = false;
                stream->just_processed_doubled_quote = false;
                stream->quote_in_quoted_at_chunk_boundary = false;

                if (c == stream->opts.dialect.quote) {
                    stream->state = CSV_STREAM_STATE_QUOTED_FIELD;
                    stream->field_is_quoted = true;
                    stream->field_start = process_input + byte_pos + 1;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;

                    // If we're at the end of the chunk, initialize buffer for the next chunk
                    if (offset >= process_len) {
                        // field_start is past the chunk (e.g., quoted field started but no content yet)
                        // Initialize empty buffer so we can append to it in the next chunk
                        if (!stream->field_is_buffered) {
                            text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            stream->field_buffer_used = 0;
                            stream->field_is_buffered = true;
                            stream->field_start = stream->field_buffer;
                            stream->field_length = 0;
                        }
                        return TEXT_CSV_OK;  // Wait for next chunk
                    }
                    continue;
                }

                if (c == stream->opts.dialect.delimiter) {
                    // Empty field
                    text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, "", 0);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count++;
                    stream->state = CSV_STREAM_STATE_START_OF_FIELD;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                // Check for newline
                csv_position pos_before = stream->pos;
                // csv_detect_newline uses pos->offset as an index into input, so we need to set it to the current offset
                pos_before.offset = byte_pos;
                csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                if (nl != CSV_NEWLINE_NONE) {
                    // Empty field, end of record
                    text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, "", 0);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count = 0;
                    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
                    stream->in_record = false;
                    stream->row_count++;
                    // pos_before.offset is now the position in process_input after the newline
                    // stream->pos.offset is the absolute position before processing the character at byte_pos
                    // csv_detect_newline has already advanced pos_before.offset by newline_bytes
                    // So the absolute position after the newline is stream->pos.offset + (pos_before.offset - byte_pos)
                    size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    stream->pos.offset += (pos_before.offset - byte_pos);
                    stream->pos.line = pos_before.line;
                    stream->pos.column = pos_before.column;
                    stream->total_bytes_consumed += newline_bytes;
                    offset = pos_before.offset;
                    // Record complete
                    continue;
                }

                // Start unquoted field
                stream->state = CSV_STREAM_STATE_UNQUOTED_FIELD;
                // Safety check: ensure process_input is valid
                if (!process_input || byte_pos >= process_len) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Invalid process_input or byte_pos");
                }
                // For unquoted fields, immediately buffer the first character to ensure stability
                // This prevents issues if process_input is cleared or reallocated
                stream->field_buffer_used = 0;
                text_csv_status status = csv_stream_grow_field_buffer(stream, 2);
                if (status != TEXT_CSV_OK) {
                    return status;
                }
                stream->field_buffer[0] = c;
                stream->field_buffer_used = 1;
                stream->field_is_buffered = true;
                stream->field_start = stream->field_buffer;
                stream->field_length = 1;
                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;
                continue;
            }

            case CSV_STREAM_STATE_UNQUOTED_FIELD: {
                if (stream->field_length >= stream->max_field_bytes) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_LIMIT, "Maximum field bytes exceeded");
                }

                if (c == stream->opts.dialect.quote && !stream->opts.dialect.allow_unquoted_quotes) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_UNEXPECTED_QUOTE, "Unexpected quote in unquoted field");
                }

                if (c == stream->opts.dialect.delimiter) {
                    // Field complete - ensure field_start is correct
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }
                    const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
                    // For buffered fields, use field_buffer_used as the source of truth
                    size_t actual_field_len = stream->field_is_buffered ? stream->field_buffer_used : stream->field_length;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count++;
                    // Field complete
                    // Clear buffering state before transitioning
                    stream->field_buffer_used = 0;
                    stream->field_is_buffered = false;
                    stream->field_needs_copy = false;
                    stream->just_processed_doubled_quote = false;
                    stream->quote_in_quoted_at_chunk_boundary = false;
                    stream->state = CSV_STREAM_STATE_START_OF_FIELD;
                    // Field tracking will be reset in START_OF_FIELD state
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                // Check for newline
                csv_position pos_before = stream->pos;
                // csv_detect_newline uses pos->offset as an index into input, so we need to set it to the current offset
                pos_before.offset = offset;
                csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                if (nl != CSV_NEWLINE_NONE) {
                    // Field complete, end of record - ensure field_start is correct
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    } else {
                        // For non-buffered fields, check if we can use in-situ mode
                        bool can_use_in_situ = false;
                        if (stream->opts.in_situ_mode && stream->original_input_buffer && stream->field_start) {
                            const char* input_start = stream->original_input_buffer;
                            size_t input_buffer_len = stream->original_input_buffer_len;
                            const char* field_start = stream->field_start;
                            size_t field_len = stream->field_length;

                            // Check for pointer arithmetic overflow safety
                            // Use subtraction to check bounds instead of addition to avoid overflow
                            if (field_start >= input_start) {
                                size_t offset_from_start = (size_t)(field_start - input_start);
                                // Check that field fits within buffer and doesn't overflow
                                if (offset_from_start <= input_buffer_len &&
                                    field_len <= input_buffer_len - offset_from_start) {
                                    can_use_in_situ = true;
                                }
                            }
                        }

                        if (!can_use_in_situ) {
                            // Copy to buffer to ensure stability
                            // This prevents issues if process_input is cleared or modified
                            size_t copy_len = stream->field_length;
                            // Validate field_start is within bounds before copying
                            if (stream->field_start >= process_input &&
                                stream->field_start < process_input + process_len) {
                                // field_start is valid - copy what we can
                                size_t available = (process_input + process_len) - stream->field_start;
                                if (copy_len > available) {
                                    copy_len = available;
                                }
                                if (copy_len > 0) {
                                    stream->field_buffer_used = 0;
                                    text_csv_status status = csv_stream_grow_field_buffer(stream, copy_len + 1);
                                    if (status != TEXT_CSV_OK) {
                                        return status;
                                    }
                                    memcpy(stream->field_buffer, stream->field_start, copy_len);
                                    stream->field_buffer_used = copy_len;
                                    stream->field_is_buffered = true;
                                    stream->field_start = stream->field_buffer;
                                    stream->field_length = copy_len;
                                }
                            } else {
                                // field_start is out of bounds - this shouldn't happen for valid input
                                // but handle gracefully by using empty field
                                stream->field_length = 0;
                            }
                        }
                        // If can_use_in_situ is true, field_start already points to original input, so we're done
                    }
                    const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
                    // For buffered fields, use field_buffer_used as the source of truth
                    size_t actual_field_len = stream->field_is_buffered ? stream->field_buffer_used : stream->field_length;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count = 0;
                    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
                    stream->in_record = false;
                    stream->row_count++;
                    // Clear buffering state
                    stream->field_buffer_used = 0;
                    stream->field_is_buffered = false;
                    stream->field_needs_copy = false;
                    stream->just_processed_doubled_quote = false;
                    stream->quote_in_quoted_at_chunk_boundary = false;
                    stream->field_start = NULL;
                    stream->field_length = 0;
                    // pos_before.offset is now the position in process_input after the newline
                    // stream->pos.offset is the absolute position before processing the character at offset
                    // csv_detect_newline has already advanced pos_before.offset by newline_bytes
                    // So the absolute position after the newline is stream->pos.offset + (pos_before.offset - offset)
                    size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    stream->pos.offset += (pos_before.offset - offset);
                    stream->pos.line = pos_before.line;
                    stream->pos.column = pos_before.column;
                        stream->total_bytes_consumed += newline_bytes;
                        offset = pos_before.offset;
                        // Mark data as processed up to and including the newline
                        // This is unambiguously recognized - the record is complete
                        continue;
                }

                if ((c == '\n' || c == '\r') && !stream->opts.dialect.allow_unquoted_newlines) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Newline in unquoted field");
                }

                // If field is buffered, append new character to buffer
                if (stream->field_is_buffered) {
                    text_csv_status status = csv_stream_append_to_field_buffer(stream, &c, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_length = stream->field_buffer_used;
                } else {
                    stream->field_length++;
                }
                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;

                // Check if we're at end of chunk and field is incomplete
                if (offset >= process_len && stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD) {
                    // Ensure field_start points to buffer if buffered
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    } else {
                        // Start buffering - copy the partial field data
                        stream->field_buffer_used = 0;
                        const char* partial_data = stream->field_start;
                        size_t partial_len = stream->field_length;
                        // Safety check: ensure field_start is valid before using it
                        if (partial_data && partial_len > 0) {
                            text_csv_status status = csv_stream_append_to_field_buffer(stream, partial_data, partial_len);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        }
                        stream->field_is_buffered = true;
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }

                    // Don't clear input buffer - we need to accumulate data across chunks
                    // The buffer will be cleared when we've processed everything
                    return TEXT_CSV_OK;  // Wait for next chunk
                }
                continue;
            }

            case CSV_STREAM_STATE_QUOTED_FIELD: {
                // Ensure field_start and field_length are correct if buffered
                if (stream->field_is_buffered) {
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                }

                if (stream->field_length >= stream->max_field_bytes) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_LIMIT, "Maximum field bytes exceeded");
                }

                // If we just processed a doubled quote and see a delimiter, end the field
                // This handles the case: "text"",field2 where the doubled quote is followed by delimiter
                if (stream->just_processed_doubled_quote && c == stream->opts.dialect.delimiter) {
                    // End of quoted field - emit field
                    // Ensure field is buffered if needed
                    if (!stream->field_is_buffered) {
                        size_t field_start_off = stream->field_start ? (stream->field_start - process_input) : 0;
                        text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                            stream, process_input, process_len, field_start_off, offset);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                    }

                    // Emit the field
                    const char* field_data = stream->field_buffer;
                    size_t actual_field_len = stream->field_buffer_used;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count++;

                    // Clear field state
                    stream->field_buffer_used = 0;
                    stream->field_is_buffered = false;
                    stream->field_needs_copy = false;
                    stream->just_processed_doubled_quote = false;
                    stream->quote_in_quoted_at_chunk_boundary = false;
                    stream->field_start = NULL;
                    stream->field_start_offset = SIZE_MAX;
                    stream->field_length = 0;
                    stream->state = CSV_STREAM_STATE_START_OF_FIELD;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                // If we just processed a doubled quote and see a newline, end the field and record
                if (stream->just_processed_doubled_quote && (c == '\n' || c == '\r')) {
                    csv_position pos_before = stream->pos;
                    pos_before.offset = offset;
                    csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                    if (nl != CSV_NEWLINE_NONE) {
                        // End of quoted field, end of record
                        // Ensure field is buffered if needed
                        if (!stream->field_is_buffered) {
                            size_t field_start_off = stream->field_start ? (stream->field_start - process_input) : 0;
                            text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                                stream, process_input, process_len, field_start_off, offset);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        }

                        // Emit field and record
                        const char* field_data = stream->field_buffer;
                        size_t actual_field_len = stream->field_buffer_used;
                        const char* unescaped_data;
                        size_t unescaped_len;
                        text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        stream->field_count = 0;
                        stream->state = CSV_STREAM_STATE_START_OF_RECORD;
                        stream->in_record = false;
                        stream->row_count++;

                        // Clear field state
                        stream->field_buffer_used = 0;
                        stream->field_is_buffered = false;
                        stream->field_needs_copy = false;
                        stream->just_processed_doubled_quote = false;
                        stream->field_start = NULL;
                        stream->field_start_offset = SIZE_MAX;
                        stream->field_length = 0;

                        size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                        stream->pos.offset += (pos_before.offset - offset);
                        stream->pos.line = pos_before.line;
                        stream->pos.column = pos_before.column;
                        stream->total_bytes_consumed += newline_bytes;
                        offset = pos_before.offset;
                        continue;
                    }
                }

                if (stream->opts.dialect.escape == TEXT_CSV_ESCAPE_BACKSLASH && c == '\\') {
                    stream->state = CSV_STREAM_STATE_ESCAPE_IN_QUOTED;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                if (c == stream->opts.dialect.quote) {
                    // Don't append the quote yet - we need to check if it's doubled or closing
                    // Transition to QUOTE_IN_QUOTED to check next character
                    stream->state = CSV_STREAM_STATE_QUOTE_IN_QUOTED;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;

                    // If we're at the end of the chunk, buffer the field data (up to but not including the quote)
                    // The quote will be handled in the next chunk when we see what follows it
                    if (offset >= process_len) {
                        // Buffer field data from field_start to offset-1 (before the quote)
                        // The quote is at position byte_pos (which is offset-1 after we advanced)
                        // We need to buffer everything up to (but not including) the quote
                        if (!stream->field_is_buffered) {
                            size_t field_start_off = SIZE_MAX;
                            if (stream->field_start &&
                                stream->field_start >= process_input &&
                                stream->field_start < process_input + process_len) {
                                field_start_off = stream->field_start - process_input;
                            }
                            // Buffer from field_start_off to byte_pos (the quote position, which we don't include)
                            // byte_pos is offset - 1 after we advanced offset
                            size_t quote_pos = offset - 1;
                            text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                                stream, process_input, process_len, field_start_off, quote_pos);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        } else {
                            // Field is already buffered - ensure field_start points to buffer
                            stream->field_start = stream->field_buffer;
                            stream->field_length = stream->field_buffer_used;
                        }
                        // Mark that we transitioned to QUOTE_IN_QUOTED at chunk boundary
                        stream->quote_in_quoted_at_chunk_boundary = true;
                        return TEXT_CSV_OK;  // Wait for next chunk
                    }
                    // Not at chunk boundary - clear the flag
                    stream->quote_in_quoted_at_chunk_boundary = false;
                    continue;
                }

                // CRITICAL: In a quoted field, a delimiter or newline can only appear after a closing quote.
                // If we see a delimiter or newline directly in QUOTED_FIELD state, it means we're missing
                // a closing quote. However, if we just processed a doubled quote and are at a chunk boundary,
                // we might be in QUOTED_FIELD state when we should actually be looking for a closing quote.
                // This case is handled by checking if we're at the start of a new chunk with a buffered field.
                // For now, treat delimiter/newline as field content (they're valid inside quoted fields).
                // Regular character in quoted field - accumulate it
                if (stream->field_is_buffered) {
                    // Append to field buffer
                    text_csv_status status = csv_stream_append_to_field_buffer(stream, &c, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                } else {
                    // Track in current chunk - set field_start if not set
                    if (!stream->field_start) {
                        stream->field_start = process_input + offset;
                        stream->field_start_offset = offset;
                        stream->field_length = 0;
                    }
                    stream->field_length++;
                }
                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;

                // If we're at the end of the chunk, buffer the field data
                if (offset >= process_len) {
                    if (!stream->field_is_buffered) {
                        size_t field_start_off = SIZE_MAX;
                        if (stream->field_start &&
                            stream->field_start >= process_input &&
                            stream->field_start < process_input + process_len) {
                            field_start_off = stream->field_start - process_input;
                            text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                                stream, process_input, process_len, field_start_off, offset);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        } else {
                            // field_start is past the chunk (e.g., quoted field started but no content yet)
                            // Initialize empty buffer so we can append to it in the next chunk
                            text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            stream->field_buffer_used = 0;
                            stream->field_is_buffered = true;
                            stream->field_start = stream->field_buffer;
                            stream->field_length = 0;
                        }
                    } else {
                        // Field is already buffered - ensure field_start points to buffer
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }
                    return TEXT_CSV_OK;  // Wait for next chunk
                }
                continue;
            }

            case CSV_STREAM_STATE_QUOTE_IN_QUOTED: {
                // We saw a quote - check if it's doubled (next char is quote) or closing (next char is delimiter/newline)

                if (stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE && c == stream->opts.dialect.quote) {
                    // Doubled quote escape - append both quotes to field data
                    // Ensure field is buffered
                    if (!stream->field_is_buffered) {
                        // Buffer existing field data first (if any)
                        size_t field_start_off = SIZE_MAX;
                        if (stream->field_start && !stream->field_is_buffered &&
                            stream->field_start >= process_input &&
                            stream->field_start < process_input + process_len) {
                            field_start_off = stream->field_start - process_input;
                            text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                                stream, process_input, process_len, field_start_off, offset - 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        } else {
                            // Field is empty or field_start is not in current chunk - initialize empty buffer
                            text_csv_status status = csv_stream_grow_field_buffer(stream, 2);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            stream->field_buffer_used = 0;
                            stream->field_is_buffered = true;
                            stream->field_start = stream->field_buffer;
                            stream->field_length = 0;
                        }
                    } else {
                        // Field is already buffered - ensure field_start points to buffer
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }

                    // Append both quotes (the one that put us in QUOTE_IN_QUOTED + this one)
                    char quote_char = stream->opts.dialect.quote;
                    text_csv_status status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                    // Mark that field needs unescaping (doubled quotes need to be converted to single quotes)
                    stream->field_needs_copy = true;

                    // Doubled quote processed - return to QUOTED_FIELD state to continue field
                    stream->state = CSV_STREAM_STATE_QUOTED_FIELD;
                    stream->just_processed_doubled_quote = true;  // Mark that we just processed a doubled quote
                    stream->quote_in_quoted_at_chunk_boundary = false;  // Clear flag
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;

                    // If at end of chunk, buffer field data
                    if (offset >= process_len) {
                        // Field is already buffered, so we're good
                        return TEXT_CSV_OK;
                    }
                    continue;
                }

                if (c == stream->opts.dialect.delimiter) {
                    // End of quoted field - emit field
                    // Special case: if we transitioned to QUOTE_IN_QUOTED at chunk boundary with empty field,
                    // and we're using doubled quote escape, treat "" as doubled quote (literal quote)
                    if (stream->quote_in_quoted_at_chunk_boundary &&
                        stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE) {
                        bool is_empty = stream->field_is_buffered ?
                            (stream->field_buffer_used == 0) : (stream->field_length == 0);
                        if (is_empty) {
                            // Treat as doubled quote
                            if (!stream->field_is_buffered) {
                                text_csv_status status = csv_stream_grow_field_buffer(stream, 2);
                                if (status != TEXT_CSV_OK) {
                                    return status;
                                }
                                stream->field_buffer_used = 0;
                                stream->field_is_buffered = true;
                                stream->field_start = stream->field_buffer;
                                stream->field_length = 0;
                            }
                            char quote_char = stream->opts.dialect.quote;
                            text_csv_status status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            stream->field_needs_copy = true;
                        }
                    }
                    // Clear the flag
                    stream->quote_in_quoted_at_chunk_boundary = false;

                    // Ensure field is buffered if needed
                    if (!stream->field_is_buffered) {
                        if (stream->field_start && stream->field_start >= process_input &&
                            stream->field_start < process_input + process_len) {
                            size_t field_start_off = stream->field_start - process_input;
                            text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                                stream, process_input, process_len, field_start_off, offset - 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        } else {
                            // Field is empty or field_start is not in current chunk - initialize empty buffer
                            text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            stream->field_buffer_used = 0;
                            stream->field_is_buffered = true;
                            stream->field_start = stream->field_buffer;
                            stream->field_length = 0;
                        }
                    } else {
                        // Field is already buffered - ensure field_start points to buffer
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }

                    // Emit the field
                    const char* field_data = stream->field_buffer;
                    size_t actual_field_len = stream->field_buffer_used;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count++;

                    // Clear field state
                    stream->field_buffer_used = 0;
                    stream->field_is_buffered = false;
                    stream->field_needs_copy = false;
                    stream->field_start = NULL;
                    stream->field_start_offset = SIZE_MAX;
                    stream->field_length = 0;
                    stream->state = CSV_STREAM_STATE_START_OF_FIELD;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                // Check for newline
                if (c == '\n' || c == '\r') {
                    csv_position pos_before = stream->pos;
                    pos_before.offset = byte_pos;
                    csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                    if (nl != CSV_NEWLINE_NONE) {
                        // End of quoted field, end of record
                        // Ensure field is buffered if needed
                        if (!stream->field_is_buffered) {
                            size_t field_start_off = SIZE_MAX;
                            if (stream->field_start &&
                                stream->field_start >= process_input &&
                                stream->field_start < process_input + process_len) {
                                field_start_off = stream->field_start - process_input;
                            } else {
                                field_start_off = 0;
                            }
                            text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                                stream, process_input, process_len, field_start_off, offset - 1);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                        }

                        // Emit field and record
                        const char* field_data = stream->field_buffer;
                        size_t actual_field_len = stream->field_buffer_used;
                        const char* unescaped_data;
                        size_t unescaped_len;
                        text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        stream->field_count = 0;
                        stream->state = CSV_STREAM_STATE_START_OF_RECORD;
                        stream->in_record = false;
                        stream->row_count++;

                        // Clear field state
                        stream->field_buffer_used = 0;
                        stream->field_is_buffered = false;
                        stream->field_needs_copy = false;
                        stream->just_processed_doubled_quote = false;
                        stream->field_start = NULL;
                        stream->field_start_offset = SIZE_MAX;
                        stream->field_length = 0;

                        size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                        stream->pos.offset += (pos_before.offset - byte_pos);
                        stream->pos.line = pos_before.line;
                        stream->pos.column = pos_before.column;
                        stream->total_bytes_consumed += newline_bytes;
                        offset = pos_before.offset;
                        continue;
                    }
                }

                // Regular character after quote - invalid quote usage
                // In a quoted field, a quote must be followed by:
                // 1. Another quote (doubled quote escape)
                // 2. A delimiter (end of field)
                // 3. A newline (end of field and record)
                // Anything else is an error
                return csv_stream_set_error(stream, TEXT_CSV_E_INVALID,
                    "Quote in quoted field must be followed by quote, delimiter, or newline");
            }

            // Orphaned code block removed - this was duplicate logic that's already handled above

            // Duplicate QUOTE_IN_QUOTED case removed - handled above at line 1128
            // (This was the old complex logic with input_buffer reprocessing - completely removed)

            case CSV_STREAM_STATE_ESCAPE_IN_QUOTED: {
                switch (c) {
                    case 'n':
                    case 'r':
                    case 't':
                    case '\\':
                    case '"':
                        // Valid escape sequence
                        break;
                    default:
                        return csv_stream_set_error(stream, TEXT_CSV_E_INVALID_ESCAPE, "Invalid escape sequence");
                }

                stream->field_needs_copy = true;
                stream->state = CSV_STREAM_STATE_QUOTED_FIELD;

                // Append escaped character to field buffer
                if (stream->field_is_buffered) {
                    char escaped_char = c;
                    if (c == 'n') escaped_char = '\n';
                    else if (c == 'r') escaped_char = '\r';
                    else if (c == 't') escaped_char = '\t';
                    // '\\' and '"' stay as-is
                    text_csv_status status = csv_stream_append_to_field_buffer(stream, &escaped_char, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                } else {
                    // If not buffered, we need to buffer when we see escape sequences
                    // since we need to transform them
                    if (!stream->field_start) {
                        stream->field_start = process_input + offset - 1; // Point to backslash
                        stream->field_start_offset = offset - 1;
                        stream->field_length = 0;
                    }
                    stream->field_length += 2; // Backslash + escaped char
                }

                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;

                // If at end of chunk, buffer field data
                if (offset >= process_len) {
                    size_t field_start_off = SIZE_MAX;
                    if (!stream->field_is_buffered && stream->field_start &&
                        stream->field_start >= process_input &&
                        stream->field_start < process_input + process_len) {
                        field_start_off = stream->field_start - process_input;
                    } else if (!stream->field_is_buffered && stream->field_start_offset != SIZE_MAX) {
                        field_start_off = stream->field_start_offset;
                    }
                    if (field_start_off != SIZE_MAX) {
                        text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
                            stream, process_input, process_len, field_start_off, offset);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                    }
                    return TEXT_CSV_OK;  // Wait for next chunk
                }
                continue;
            }

            case CSV_STREAM_STATE_COMMENT: {
                csv_position pos_before = stream->pos;
                // csv_detect_newline uses pos->offset as an index into input, so we need to set it to the current offset
                pos_before.offset = byte_pos;
                csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                if (nl != CSV_NEWLINE_NONE) {
                    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
                    stream->in_comment = false;
                    stream->row_count++;
                    // pos_before.offset is now the position in process_input after the newline
                    // stream->pos.offset is the absolute position before processing the character at byte_pos
                    // csv_detect_newline has already advanced pos_before.offset by newline_bytes
                    // So the absolute position after the newline is stream->pos.offset + (pos_before.offset - byte_pos)
                    size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    stream->pos.offset += (pos_before.offset - byte_pos);
                    stream->pos.line = pos_before.line;
                    stream->pos.column = pos_before.column;
                        stream->total_bytes_consumed += newline_bytes;
                        offset = pos_before.offset;
                        // Mark data as processed up to and including the newline
                        // This is unambiguously recognized - the record is complete
                        continue;
                }
                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;
                continue;
            }

            case CSV_STREAM_STATE_END:
                return TEXT_CSV_OK;
        }
    }

    // Check if we exited the loop while in a state that requires buffering at chunk boundaries
    // These states require seeing the next character to determine completion:
    // - QUOTE_IN_QUOTED: need to see if next char is quote (doubled), delimiter, or newline
    // - ESCAPE_IN_QUOTED: need to see the escaped character
    if ((stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED ||
         stream->state == CSV_STREAM_STATE_ESCAPE_IN_QUOTED) && offset >= process_len) {
        // At end of chunk, wait for more data to determine completion:
        // - QUOTE_IN_QUOTED: need to see if next char is quote (doubled), delimiter, or newline
        // - ESCAPE_IN_QUOTED: need to see the escaped character
        // CRITICAL: We must buffer the field data because we don't know if the sequence is complete
        // until we see the next chunk. The field data might be pointing into the input buffer which
        // will be cleared or reused.
        if (stream->field_is_buffered) {
            stream->field_start = stream->field_buffer;
            stream->field_length = stream->field_buffer_used;
            // Note: We do NOT append the quote here even if in QUOTE_IN_QUOTED state.
            // We need to wait for the next chunk to see if it's a doubled quote (next char is quote)
            // or a closing quote (next char is delimiter/newline).
        } else {
            // Field is not buffered yet - we need to buffer it now because we're at a chunk boundary
            // and need to preserve the field data until we see the next character
            stream->field_buffer_used = 0;
            const char* partial_data = stream->field_start;
            size_t partial_len = stream->field_length;
            // Safety check: ensure partial_data is valid and within bounds
            if (partial_data && partial_data >= process_input &&
                partial_data < process_input + process_len) {
                // Ensure partial_len doesn't exceed available data
                if (partial_data + partial_len > process_input + process_len) {
                    size_t available = (process_input + process_len) - partial_data;
                    if (available > 0) {
                        partial_len = available;
                    } else {
                        partial_len = 0;
                    }
                }
                if (partial_len > 0) {
                    text_csv_status status = csv_stream_append_to_field_buffer(stream, partial_data, partial_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_is_buffered = true;
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                } else {
                    // No data to buffer - create empty field buffer
                    stream->field_is_buffered = true;
                    stream->field_start = stream->field_buffer;
                    stream->field_length = 0;
                }
            } else {
                // field_start is not within bounds - this should not happen for valid input
                // but if it does, we need to handle it. The field should have been buffered earlier.
                // This can happen if field_start points to data from a previous chunk that's no longer valid.
                // In this case, we need to ensure the field buffer is initialized, even if it's empty.
                // The field data should have been buffered earlier, but if it wasn't, we'll create an empty buffer.
                if (!stream->field_buffer) {
                    // Initialize field buffer if it doesn't exist
                    text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                }
                stream->field_buffer_used = 0;
                stream->field_is_buffered = true;
                stream->field_start = stream->field_buffer;
                stream->field_length = 0;
            }
            // Note: We do NOT append the quote here even if in QUOTE_IN_QUOTED state.
            // We need to wait for the next chunk to see if it's a doubled quote (next char is quote)
            // or a closing quote (next char is delimiter/newline).
        }
        // Field data is now buffered - return to wait for next chunk
        return TEXT_CSV_OK;
    }

    return TEXT_CSV_OK;
}

TEXT_API text_csv_stream* text_csv_stream_new(
    const text_csv_parse_options* opts,
    text_csv_event_cb callback,
    void* user_data
) {
    if (!callback) {
        return NULL;
    }

    text_csv_stream* stream = calloc(1, sizeof(text_csv_stream));
    if (!stream) {
        return NULL;
    }

    if (opts) {
        stream->opts = *opts;
    } else {
        stream->opts = text_csv_parse_options_default();
    }

    stream->callback = callback;
    stream->user_data = user_data;
    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
    stream->in_record = false;
    stream->field_count = 0;
    stream->row_count = 0;
    stream->pos.offset = 0;
    stream->pos.line = 1;
    stream->pos.column = 1;
    stream->total_bytes_consumed = 0;
    stream->original_input_buffer = NULL;
    stream->original_input_buffer_len = 0;

    // Set limits
    stream->max_rows = csv_get_limit(stream->opts.max_rows, CSV_DEFAULT_MAX_ROWS);
    stream->max_cols = csv_get_limit(stream->opts.max_cols, CSV_DEFAULT_MAX_COLS);
    stream->max_field_bytes = csv_get_limit(stream->opts.max_field_bytes, CSV_DEFAULT_MAX_FIELD_BYTES);
    stream->max_record_bytes = csv_get_limit(stream->opts.max_record_bytes, CSV_DEFAULT_MAX_RECORD_BYTES);
    stream->max_total_bytes = csv_get_limit(stream->opts.max_total_bytes, CSV_DEFAULT_MAX_TOTAL_BYTES);

    // Comment prefix
    if (stream->opts.dialect.allow_comments && stream->opts.dialect.comment_prefix) {
        stream->comment_prefix_len = strlen(stream->opts.dialect.comment_prefix);
    } else {
        stream->comment_prefix_len = 0;
    }

    // Initialize error
    memset(&stream->error, 0, sizeof(stream->error));

    return stream;
}

TEXT_API text_csv_status text_csv_stream_feed(
    text_csv_stream* stream,
    const void* data,
    size_t len,
    text_csv_error* err
) {
    if (!stream) {
        if (err) {
            err->code = TEXT_CSV_E_INVALID;
            err->message = "Stream must not be NULL";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
        }
        return TEXT_CSV_E_INVALID;
    }

    if (stream->state == CSV_STREAM_STATE_END) {
        if (err) {
            csv_error_copy(err, &stream->error);
        }
        return stream->error.code != TEXT_CSV_OK ? stream->error.code : TEXT_CSV_E_INVALID;
    }

    if (!data || len == 0) {
        return TEXT_CSV_OK;
    }

    // Handle BOM on first feed
    if (stream->total_bytes_consumed == 0 && !stream->opts.keep_bom) {
        const char* input = (const char*)data;
        size_t input_len = len;
        csv_strip_bom(&input, &input_len, &stream->pos, true);
        if (input != (const char*)data) {
            // BOM was stripped, adjust data pointer
            data = input;
            len = input_len;
        }
    }

    // If we have a field in progress, we need to continue it
    // CRITICAL: If we're in QUOTE_IN_QUOTED state and the field is not buffered,
    // this should not happen - the field should have been buffered at the end of the previous chunk.
    // However, if it wasn't (due to a bug), we need to handle it gracefully.
    // Since we can't access the previous chunk's data, we'll create an empty buffer.
    // This will cause the field to be empty, which is incorrect but better than crashing.
    if (stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED && !stream->field_is_buffered) {
        // Field should have been buffered at the end of the previous chunk, but it wasn't.
        // This is a bug in the previous chunk processing. We can't recover the field data,
        // but we can at least ensure the parser doesn't crash.
        if (!stream->field_buffer) {
            text_csv_status status = csv_stream_grow_field_buffer(stream, 1);
            if (status != TEXT_CSV_OK) {
                if (err) {
                    csv_error_copy(err, &stream->error);
                }
                return status;
            }
        }
        stream->field_buffer_used = 0;
        stream->field_is_buffered = true;
        stream->field_start = stream->field_buffer;
        stream->field_length = 0;
        // Note: The field data from the previous chunk is lost. This should not happen
        // if the previous chunk processing worked correctly.
    }

    // If we have a field in progress that's buffered, we need to continue it
    // Otherwise, process the chunk normally
    if (stream->field_is_buffered && (stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD ||
                                      stream->state == CSV_STREAM_STATE_QUOTED_FIELD ||
                                      stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED)) {
        // The field buffer already contains the partial field from previous chunks.
        // We need to process the new chunk data, appending field content to the buffer
        // as we encounter it, until the field completes.

        // CRITICAL: Ensure field_start points to field_buffer when field is buffered
        // This prevents field_start from pointing into invalid input buffer memory
        if (stream->field_start != stream->field_buffer) {
            stream->field_start = stream->field_buffer;
            stream->field_length = stream->field_buffer_used;
        }

        // Process the new chunk - it will append to field_buffer as needed
        text_csv_status status = csv_stream_process_chunk(stream, (const char*)data, len);

        // Reset field buffering state after processing if field completed
        if (stream->state != CSV_STREAM_STATE_UNQUOTED_FIELD &&
            stream->state != CSV_STREAM_STATE_QUOTED_FIELD &&
            stream->state != CSV_STREAM_STATE_QUOTE_IN_QUOTED) {
            // Field completed, clear buffer
            stream->field_buffer_used = 0;
            stream->field_is_buffered = false;
        }

        if (status != TEXT_CSV_OK && err) {
            csv_error_copy(err, &stream->error);
        }
        return status;
    }

    text_csv_status status = csv_stream_process_chunk(stream, (const char*)data, len);

    if (status != TEXT_CSV_OK && err) {
        csv_error_copy(err, &stream->error);
    }

    return status;
}

TEXT_API text_csv_status text_csv_stream_finish(
    text_csv_stream* stream,
    text_csv_error* err
) {
    if (!stream) {
        if (err) {
            err->code = TEXT_CSV_E_INVALID;
            err->message = "Stream must not be NULL";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
        }
        return TEXT_CSV_E_INVALID;
    }

    // Check for unterminated quote
    if (stream->state == CSV_STREAM_STATE_QUOTED_FIELD ||
        stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED ||
        stream->state == CSV_STREAM_STATE_ESCAPE_IN_QUOTED) {
        text_csv_status status = csv_stream_set_error(stream, TEXT_CSV_E_UNTERMINATED_QUOTE, "Unterminated quoted field");
        if (err) {
            csv_error_copy(err, &stream->error);
        }
        return status;
    }

    // Emit final record end if in record
    if (stream->in_record) {
        // Emit current field if any (only if we're actually in a field, not just at start)
        if (stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD || stream->state == CSV_STREAM_STATE_QUOTED_FIELD) {
            // Ensure field_start is correct for buffered fields
            if (stream->field_is_buffered) {
                stream->field_start = stream->field_buffer;
                stream->field_length = stream->field_buffer_used;
            }
            const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
            // For buffered fields, use field_buffer_used as the source of truth
            size_t actual_field_len = stream->field_is_buffered ? stream->field_buffer_used : stream->field_length;
            const char* unescaped_data;
            size_t unescaped_len;
            text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
            if (status != TEXT_CSV_OK) {
                if (err) {
                    csv_error_copy(err, &stream->error);
                }
                return status;
            }
            status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
            if (status != TEXT_CSV_OK) {
                if (err) {
                    csv_error_copy(err, &stream->error);
                }
                return status;
            }
        }
        // If we're at START_OF_FIELD, we haven't started a field yet, so don't emit one
        // This prevents creating empty records from trailing newlines
        text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
        if (status != TEXT_CSV_OK) {
            if (err) {
                csv_error_copy(err, &stream->error);
            }
            return status;
        }
    }

    // Emit END event
    text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_END, NULL, 0);
    if (status != TEXT_CSV_OK && err) {
        csv_error_copy(err, &stream->error);
    }

    stream->state = CSV_STREAM_STATE_END;
    return status;
}

TEXT_API void text_csv_stream_free(text_csv_stream* stream) {
    if (!stream) {
        return;
    }

    free(stream->input_buffer);
    free(stream->field_buffer);
    text_csv_error_free(&stream->error);
    free(stream);
}

TEXT_INTERNAL_API void csv_stream_set_original_input_buffer(
    text_csv_stream* stream,
    const char* input_buffer,
    size_t input_buffer_len
) {
    if (!stream) {
        return;
    }
    stream->original_input_buffer = input_buffer;
    stream->original_input_buffer_len = input_buffer_len;
}
