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
    const char* field_start;       ///< Start of current field (pointer into input or buffer)
    size_t field_length;            ///< Length of current field
    bool field_is_quoted;            ///< Whether field is quoted
    bool field_needs_copy;           ///< Whether field needs copying (for escaping)
    char* field_buffer;             ///< Buffer for field data (when escaping needed or spanning chunks)
    size_t field_buffer_size;
    size_t field_buffer_used;
    bool field_is_buffered;          ///< Whether field data is in field_buffer (not in current chunk)

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

    // Error state
    text_csv_error error;
};

/**
 * @brief Get effective limit value
 */
static size_t csv_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Grow input buffer
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
    if (!stream->field_is_buffered && stream->input_buffer &&
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

/**
 * @brief Grow field buffer
 */
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
    stream->state = CSV_STREAM_STATE_END;
    return code;
}

/**
 * @brief Unescape doubled quotes in field data
 *
 * Converts doubled quotes ("") to single quotes (") in the field data.
 * The result is written to the field_buffer, which must be large enough.
 */
static text_csv_status csv_stream_unescape_field(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
) {
    if (!stream->field_needs_copy) {
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

        // Input is not in field_buffer - copy it to ensure stability
        // Safety check: ensure input_data is valid (not NULL)
        if (!input_data) {
            return TEXT_CSV_E_INVALID;
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
        // Save the current data before reallocation
        char* temp_buffer = malloc(input_len);
        if (!temp_buffer) {
            return TEXT_CSV_E_OOM;
        }
        memcpy(temp_buffer, stream->field_buffer, input_len);

        // Now grow the buffer
        text_csv_status status = csv_stream_grow_field_buffer(stream, needed_size);
        if (status != TEXT_CSV_OK) {
            free(temp_buffer);
            return status;
        }

        // Restore the data
        memcpy(stream->field_buffer, temp_buffer, input_len);
        free(temp_buffer);
        actual_input = stream->field_buffer;  // Update pointer after reallocation
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

    // Ensure the buffer is large enough for the output
    if (out_idx > stream->field_buffer_size) {
        return TEXT_CSV_E_OOM;  // This should never happen, but safety check
    }

    // Safety check: ensure output length doesn't exceed buffer size
    if (out_idx > stream->field_buffer_size) {
        out_idx = stream->field_buffer_size;
    }

    // Final safety check: ensure out_idx is valid
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

/**
 * @brief Emit an event
 */
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

// Check if at start of comment line
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

// Process a chunk of input (handles fields spanning chunks via buffering)
static text_csv_status csv_stream_process_chunk(
    text_csv_stream* stream,
    const char* input,
    size_t input_len
) {
    // If we have buffered data, prepend it to the current input
    const char* process_input = input;
    size_t process_len = input_len;
    bool using_buffer = false;

    if (stream->input_buffer_used > 0) {
        // Need to process buffered data first
        // For simplicity, we'll append new data to buffer and process the whole buffer
        // Check for overflow before addition
        if (input_len > SIZE_MAX - stream->input_buffer_used) {
            return csv_stream_set_error(stream, TEXT_CSV_E_OOM, "Input buffer size overflow");
        }
        text_csv_status status = csv_stream_grow_input_buffer(stream, stream->input_buffer_used + input_len);
        if (status != TEXT_CSV_OK) {
            return status;
        }

        memcpy(stream->input_buffer + stream->input_buffer_used, input, input_len);
        // Overflow already checked above
        stream->input_buffer_used += input_len;
        process_input = stream->input_buffer;
        process_len = stream->input_buffer_used;
        using_buffer = true;
    }

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

                if (c == stream->opts.dialect.quote) {
                    stream->state = CSV_STREAM_STATE_QUOTED_FIELD;
                    stream->field_is_quoted = true;
                    stream->field_start = process_input + byte_pos + 1;
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
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
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, stream->field_length, &unescaped_data, &unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count++;
                    // Clear buffering state before transitioning
                    stream->field_buffer_used = 0;
                    stream->field_is_buffered = false;
                    stream->field_needs_copy = false;
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
                        // For non-buffered fields, always copy to buffer to ensure stability
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
                    const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, stream->field_length, &unescaped_data, &unescaped_len);
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
                        text_csv_status status = csv_stream_append_to_field_buffer(stream, partial_data, partial_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        stream->field_is_buffered = true;
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }

                    // Clear input buffer for next chunk
                    if (using_buffer) {
                        stream->input_buffer_used = 0;
                    }
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
                    continue;
                }

                if ((c == '\n' || c == '\r') && !stream->opts.dialect.newline_in_quotes) {
                    return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Newline in quoted field not allowed");
                }

                // Handle newline in quoted field
                if (c == '\n' || c == '\r') {
                    csv_position pos_before = stream->pos;
                    // csv_detect_newline uses pos->offset as an index into input, so we need to set it to the current offset
                    pos_before.offset = byte_pos;
                    csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                    if (nl != CSV_NEWLINE_NONE) {
                        // pos_before.offset is now the position in process_input after the newline
                        // stream->pos.offset is the absolute position before processing the character at byte_pos
                        // csv_detect_newline has already advanced pos_before.offset by newline_bytes
                        // So the absolute position after the newline is stream->pos.offset + (pos_before.offset - byte_pos)
                        size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                        stream->pos.offset += (pos_before.offset - byte_pos);
                        stream->pos.line = pos_before.line;
                        stream->pos.column = pos_before.column;
                        stream->total_bytes_consumed += newline_bytes;
                        // If field is buffered, append newline to buffer
                        if (stream->field_is_buffered) {
                            const char* nl_str = (nl == CSV_NEWLINE_CRLF) ? "\r\n" : "\n";
                            size_t nl_len = (nl == CSV_NEWLINE_CRLF) ? 2 : 1;
                            text_csv_status status = csv_stream_append_to_field_buffer(stream, nl_str, nl_len);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            // Ensure field_start points to buffer (may have been reallocated)
                            stream->field_start = stream->field_buffer;
                            stream->field_length = stream->field_buffer_used;
                        } else {
                            // Field is not buffered - need to buffer it before appending newline
                            // to ensure field_start remains valid
                            stream->field_buffer_used = 0;
                            const char* partial_data = stream->field_start;
                            size_t partial_len = stream->field_length;
                            // Validate field_start is within bounds before copying
                            if (partial_data >= process_input &&
                                partial_data < process_input + process_len) {
                                size_t available = (process_input + process_len) - partial_data;
                                if (partial_len > available) {
                                    partial_len = available;
                                }
                                if (partial_len > 0) {
                                    text_csv_status status = csv_stream_grow_field_buffer(stream, partial_len + newline_bytes + 1);
                                    if (status != TEXT_CSV_OK) {
                                        return status;
                                    }
                                    memcpy(stream->field_buffer, partial_data, partial_len);
                                    stream->field_buffer_used = partial_len;
                                    stream->field_is_buffered = true;
                                    stream->field_start = stream->field_buffer;
                                }
                            }
                            // Append newline to buffer
                            const char* nl_str = (nl == CSV_NEWLINE_CRLF) ? "\r\n" : "\n";
                            size_t nl_len = (nl == CSV_NEWLINE_CRLF) ? 2 : 1;
                            text_csv_status status = csv_stream_append_to_field_buffer(stream, nl_str, nl_len);
                            if (status != TEXT_CSV_OK) {
                                return status;
                            }
                            stream->field_start = stream->field_buffer;
                            stream->field_length = stream->field_buffer_used;
                        }
                        offset = pos_before.offset;
                        continue;
                    }
                }

                // If field is buffered, append new character to buffer
                if (stream->field_is_buffered) {
                    text_csv_status status = csv_stream_append_to_field_buffer(stream, &c, 1);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    // Ensure field_start points to buffer (may have been reallocated)
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                } else {
                    stream->field_length++;
                }
                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;

                // Check if we're at end of chunk and field is incomplete
                if (offset >= process_len) {
                    // Ensure field_start points to buffer if buffered
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    } else {
                        // Start buffering - copy the partial field data
                        stream->field_buffer_used = 0;
                        const char* partial_data = stream->field_start;
                        size_t partial_len = stream->field_length;
                        // Safety check: ensure partial_data is valid and within bounds
                        // field_start points into process_input, so we can check bounds
                        if (!partial_data || partial_data < process_input ||
                            partial_data >= process_input + process_len) {
                            return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Invalid field_start pointer when buffering");
                        }
                        // Ensure partial_len doesn't exceed available data
                        if (partial_data + partial_len > process_input + process_len) {
                            size_t available = (process_input + process_len) - partial_data;
                            if (available > 0) {
                                partial_len = available;
                            } else {
                                partial_len = 0;
                            }
                        }
                        text_csv_status status = csv_stream_append_to_field_buffer(stream, partial_data, partial_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        stream->field_is_buffered = true;
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }

                    // Clear input buffer for next chunk
                    if (using_buffer) {
                        stream->input_buffer_used = 0;
                    }
                    return TEXT_CSV_OK;  // Wait for next chunk
                }
                continue;
            }

            case CSV_STREAM_STATE_QUOTE_IN_QUOTED: {
                // Ensure field_start is correct if buffered (may have been reallocated)
                if (stream->field_is_buffered) {
                    stream->field_start = stream->field_buffer;
                    stream->field_length = stream->field_buffer_used;
                }


                if (stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE && c == stream->opts.dialect.quote) {
                    // Doubled quote escape - append both quotes to field data
                    stream->field_needs_copy = true;
                    stream->state = CSV_STREAM_STATE_QUOTED_FIELD;
                    // Append both quotes to field data (doubled quote represents a single quote in the data)
                    char quote_char = stream->opts.dialect.quote;
                    if (stream->field_is_buffered) {
                        // Append first quote
                        text_csv_status status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        // Append second quote
                        status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    } else {
                        // For non-buffered fields, we need to handle this differently
                        // Since we can't modify the input, we need to start buffering
                        stream->field_buffer_used = 0;
                        const char* partial_data = stream->field_start;
                        size_t partial_len = stream->field_length;
                        text_csv_status status = csv_stream_append_to_field_buffer(stream, partial_data, partial_len);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        // Append both quotes
                        status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
                        if (status != TEXT_CSV_OK) {
                            return status;
                        }
                        stream->field_is_buffered = true;
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }
                    offset++;
                    stream->pos.offset++;
                    stream->pos.column++;
                    stream->total_bytes_consumed++;
                    continue;
                }

                if (c == stream->opts.dialect.delimiter) {
                    // End of quoted field - ensure field_start is correct
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        // Use field_buffer_used directly - it's the actual data length
                        stream->field_length = stream->field_buffer_used;
                        // Safety check: ensure field_length doesn't exceed buffer size
                        if (stream->field_length > stream->field_buffer_size) {
                            return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Field buffer used exceeds buffer size");
                        }
                    } else {
                        // Validate field_start is within bounds for non-buffered fields
                        if (stream->field_start < process_input ||
                            stream->field_start >= process_input + process_len) {
                            return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Invalid field_start pointer");
                        }
                    }
                    const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
                    // For buffered fields, use field_buffer_used as the actual data length
                    // This is critical - field_buffer_used is the source of truth for actual data length
                    size_t actual_field_len = stream->field_is_buffered ? stream->field_buffer_used : stream->field_length;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    // Safety check: ensure unescaped_len doesn't exceed buffer size
                    // Note: csv_stream_unescape_field already updates field_buffer_used to unescaped_len
                    // when output is in field_buffer, so we can trust unescaped_len
                    if (unescaped_data == stream->field_buffer) {
                        // Ensure it doesn't exceed field_buffer_size
                        if (unescaped_len > stream->field_buffer_size) {
                            unescaped_len = stream->field_buffer_size;
                        }
                    }
                    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
                    if (status != TEXT_CSV_OK) {
                        return status;
                    }
                    stream->field_count++;
                    // Clear buffering state before transitioning
                    stream->field_buffer_used = 0;
                    stream->field_is_buffered = false;
                    stream->field_needs_copy = false;
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
                pos_before.offset = byte_pos;
                csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect);
                if (nl != CSV_NEWLINE_NONE) {
                    // End of quoted field, end of record - ensure field_start is correct
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }
                    const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
                    const char* unescaped_data;
                    size_t unescaped_len;
                    text_csv_status status = csv_stream_unescape_field(stream, field_data, stream->field_length, &unescaped_data, &unescaped_len);
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
                    // pos_before.offset is now the position in process_input after the newline
                    // Convert it back to absolute position
                    size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    stream->pos.offset += (pos_before.offset - byte_pos) + newline_bytes;
                    stream->pos.line = pos_before.line;
                    stream->pos.column = pos_before.column;
                    stream->total_bytes_consumed += newline_bytes;
                    offset = pos_before.offset;
                    continue;
                }

                // Invalid: quote must be followed by delimiter, newline, or another quote
                // But check if we're at end of chunk - might need to wait for more data
                if (offset >= process_len) {
                    // At end of chunk, wait for more data
                    // Ensure field buffer state is preserved
                    if (stream->field_is_buffered) {
                        stream->field_start = stream->field_buffer;
                        stream->field_length = stream->field_buffer_used;
                    }
                    // Clear input buffer for next chunk
                    if (using_buffer) {
                        stream->input_buffer_used = 0;
                    }
                    return TEXT_CSV_OK;
                }
                return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Invalid quote usage in quoted field");
            }

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
                stream->field_length++;
                offset++;
                stream->pos.offset++;
                stream->pos.column++;
                stream->total_bytes_consumed++;
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

    // Check if we exited the loop while in QUOTE_IN_QUOTED state (quote was last character)
    // This can happen when we transition from QUOTED_FIELD to QUOTE_IN_QUOTED and increment offset
    if (stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED && offset >= process_len) {
        // At end of chunk, wait for more data to see if quote is followed by delimiter/newline/doubled quote
        // Ensure field buffer state is preserved
        if (stream->field_is_buffered) {
            stream->field_start = stream->field_buffer;
            stream->field_length = stream->field_buffer_used;
        }
        // Clear input buffer for next chunk
        if (using_buffer) {
            stream->input_buffer_used = 0;
        }
        return TEXT_CSV_OK;
    }

    // Clear processed data from input buffer if we were using it
    if (using_buffer && offset > 0) {
        // Move remaining unprocessed data to start of buffer
        size_t remaining = process_len - offset;
        if (remaining > 0) {
            // If field_start points into the input buffer, we need to handle it
            if (!stream->field_is_buffered && stream->field_start >= process_input &&
                stream->field_start < process_input + process_len) {
                if (stream->field_start >= process_input + offset) {
                    // field_start points to unprocessed data - update it
                    stream->field_start = stream->input_buffer + (stream->field_start - (process_input + offset));
                } else {
                    // field_start points to processed data - buffer it to ensure stability
                    size_t field_offset = stream->field_start - process_input;
                    size_t copy_len = stream->field_length;
                    if (field_offset + copy_len > offset) {
                        // Field extends into processed area - only copy the part that's still valid
                        copy_len = offset - field_offset;
                    }
                    if (copy_len > 0) {
                        stream->field_buffer_used = 0;
                        text_csv_status status = csv_stream_grow_field_buffer(stream, copy_len + 1);
                        if (status == TEXT_CSV_OK) {
                            memcpy(stream->field_buffer, stream->field_start, copy_len);
                            stream->field_buffer_used = copy_len;
                            stream->field_is_buffered = true;
                            stream->field_start = stream->field_buffer;
                            stream->field_length = copy_len;
                        }
                    }
                }
            }
            memmove(stream->input_buffer, process_input + offset, remaining);
            stream->input_buffer_used = remaining;
        } else {
            stream->input_buffer_used = 0;
        }
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
            *err = stream->error;
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
            *err = stream->error;
        }
        return status;
    }

    text_csv_status status = csv_stream_process_chunk(stream, (const char*)data, len);

    if (status != TEXT_CSV_OK && err) {
        *err = stream->error;
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
            *err = stream->error;
        }
        return status;
    }

    // Emit final record end if in record
    if (stream->in_record) {
        // Emit current field if any (only if we're actually in a field, not just at start)
        if (stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD || stream->state == CSV_STREAM_STATE_QUOTED_FIELD) {
            const char* field_data = stream->field_is_buffered ? stream->field_buffer : stream->field_start;
            const char* unescaped_data;
            size_t unescaped_len;
            text_csv_status status = csv_stream_unescape_field(stream, field_data, stream->field_length, &unescaped_data, &unescaped_len);
            if (status != TEXT_CSV_OK) {
                if (err) {
                    *err = stream->error;
                }
                return status;
            }
            status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
            if (status != TEXT_CSV_OK) {
                if (err) {
                    *err = stream->error;
                }
                return status;
            }
        }
        // If we're at START_OF_FIELD, we haven't started a field yet, so don't emit one
        // This prevents creating empty records from trailing newlines
        text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
        if (status != TEXT_CSV_OK) {
            if (err) {
                *err = stream->error;
            }
            return status;
        }
    }

    // Emit END event
    text_csv_status status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_END, NULL, 0);
    if (status != TEXT_CSV_OK && err) {
        *err = stream->error;
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
