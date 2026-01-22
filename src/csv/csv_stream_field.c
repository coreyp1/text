/**
 * @file csv_stream_field.c
 * @brief Field processing and unescaping for CSV streaming parser
 *
 * Handles field completion, unescaping, validation, and scanning.
 */

#include "csv_stream_internal.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Clear field state
void csv_stream_clear_field_state(text_csv_stream* stream) {
    csv_field_buffer_clear(&stream->field);
    stream->just_processed_doubled_quote = false;
    stream->quote_in_quoted_at_chunk_boundary = false;
}

// Complete field and transition to next field or record
// This function encapsulates the common pattern of emitting a field,
// clearing field state, and transitioning to the appropriate next state.
text_csv_status csv_stream_complete_field(
    text_csv_stream* stream,
    size_t* offset,
    bool emit_record_end
) {
    text_csv_status status = csv_stream_emit_field(stream, emit_record_end);
    if (status != TEXT_CSV_OK) {
        return status;
    }

    csv_stream_clear_field_state(stream);

    if (emit_record_end) {
        stream->field_count = 0;
        stream->state = CSV_STREAM_STATE_START_OF_RECORD;
        stream->in_record = false;
        if (stream->row_count >= SIZE_MAX) {
            return csv_stream_set_error(stream, TEXT_CSV_E_LIMIT, "Row count overflow");
        }
        stream->row_count++;
    } else {
        stream->state = CSV_STREAM_STATE_START_OF_FIELD;
    }

    return csv_stream_advance_position(stream, offset, 1);
}

// Complete field at delimiter (field separator, not end of record)
text_csv_status csv_stream_complete_field_at_delimiter(
    text_csv_stream* stream,
    size_t* offset
) {
    return csv_stream_complete_field(stream, offset, false);
}

// Complete field at newline (end of record)
text_csv_status csv_stream_complete_field_at_newline(
    text_csv_stream* stream,
    size_t* offset
) {
    return csv_stream_complete_field(stream, offset, true);
}

// Handle chunk boundary - buffer field if needed
// This function encapsulates the common pattern of checking if we're at a chunk boundary
// and ensuring the field is buffered so it remains valid when the chunk is cleared.
text_csv_status csv_stream_handle_chunk_boundary(
    text_csv_stream* stream
) {
    // Only buffer if we're in a field state
    if (stream->state != CSV_STREAM_STATE_UNQUOTED_FIELD &&
        stream->state != CSV_STREAM_STATE_QUOTED_FIELD) {
        return TEXT_CSV_OK;
    }

    // Ensure field is buffered
    if (stream->field.is_buffered) {
        stream->field.data = stream->field.buffer;
        stream->field.length = stream->field.buffer_used;
    } else {
        // Start buffering - ensure field is buffered (this will copy existing data if needed)
        text_csv_status status = csv_field_buffer_ensure_buffered(&stream->field);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    return TEXT_CSV_OK;
}

// Emit a field (unescape and emit, optionally emit record end)
text_csv_status csv_stream_emit_field(
    text_csv_stream* stream,
    bool emit_record_end
) {
    // Get field data
    const char* field_data = stream->field.data;
    size_t actual_field_len = stream->field.is_buffered ? stream->field.buffer_used : stream->field.length;

    // Unescape if needed
    const char* unescaped_data;
    size_t unescaped_len;
    text_csv_status status = csv_stream_unescape_field(stream, field_data, actual_field_len, &unescaped_data, &unescaped_len);
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Emit field
    status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
    if (status != TEXT_CSV_OK) {
        return status;
    }

    if (stream->field_count >= SIZE_MAX) {
        return csv_stream_set_error(stream, TEXT_CSV_E_LIMIT, "Field count overflow");
    }
    stream->field_count++;

    // Optionally emit record end
    if (emit_record_end) {
        status = csv_stream_emit_event(stream, TEXT_CSV_EVENT_RECORD_END, NULL, 0);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    return TEXT_CSV_OK;
}

// Validate input parameters for field processing
text_csv_status csv_stream_validate_field_input(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t byte_pos
) {
    if (!process_input) {
        return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "process_input is NULL");
    }
    if (byte_pos > process_len) {
        return csv_stream_set_error(stream, TEXT_CSV_E_INVALID, "Invalid byte position");
    }
    return TEXT_CSV_OK;
}

// Check if field can use in-situ mode
bool csv_stream_can_use_in_situ(
    text_csv_stream* stream,
    const char* field_start,
    size_t field_len
) {
    if (!stream->opts.in_situ_mode || !stream->original_input_buffer || !field_start) {
        return false;
    }

    const char* input_start = stream->original_input_buffer;
    size_t input_buffer_len = stream->original_input_buffer_len;

    // Check for pointer arithmetic overflow safety
    // Use subtraction to check bounds instead of addition to avoid overflow
    if (field_start >= input_start) {
        size_t offset_from_start = (size_t)(field_start - input_start);
        // Check that field fits within buffer and doesn't overflow
        if (offset_from_start <= input_buffer_len &&
            field_len <= input_buffer_len - offset_from_start) {
            return true;
        }
    }

    return false;
}

// Scan ahead in unquoted field for special characters (delimiter, newline, quote)
// Returns the number of safe characters that can be processed in bulk,
// or 0 if a special character is found immediately.
// Sets *found_special to true if a special character was found, false otherwise.
// Sets *special_char to the character found (if found_special is true).
// Sets *special_pos to the position of the special character (if found_special is true).
size_t csv_stream_scan_unquoted_field_ahead(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t start_offset,
    bool* found_special,
    char* special_char,
    size_t* special_pos
) {
    const char delimiter = stream->opts.dialect.delimiter;
    const char quote = stream->opts.dialect.quote;
    bool allow_unquoted_quotes = stream->opts.dialect.allow_unquoted_quotes;
    bool allow_unquoted_newlines = stream->opts.dialect.allow_unquoted_newlines;

    size_t pos = start_offset;
    *found_special = false;

    while (pos < process_len) {
        char c = process_input[pos];

        // Check for delimiter (always ends field)
        if (c == delimiter) {
            *found_special = true;
            *special_char = c;
            *special_pos = pos;
            return pos - start_offset;
        }

        // Check for newline
        if (c == '\n' || c == '\r') {
            // Check if it's a complete newline sequence
            csv_position pos_before = stream->pos;
            pos_before.offset = pos;
            text_csv_status detect_error = TEXT_CSV_OK;
            csv_newline_type nl = csv_detect_newline(process_input, process_len, &pos_before, &stream->opts.dialect, &detect_error);
            // Note: We ignore overflow errors here since this is just scanning ahead
            // The actual newline handling will check for overflow
            if (nl != CSV_NEWLINE_NONE) {
                // Found a complete newline sequence
                if (!allow_unquoted_newlines) {
                    // Newlines not allowed - this ends the field
                    *found_special = true;
                    *special_char = c;
                    *special_pos = pos;
                    return pos - start_offset;
                }
                // Newlines allowed - this is just part of the field content
                // Advance past the newline sequence and continue scanning
                pos += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                continue;
            }
            // Not a complete newline sequence (e.g., standalone CR when dialect expects CRLF)
            // If newlines not allowed, this is an error
            if (!allow_unquoted_newlines) {
                *found_special = true;
                *special_char = c;
                *special_pos = pos;
                return pos - start_offset;
            }
            // Newlines allowed - single newline character is just field content
            // Continue scanning
        }

        // Check for quote
        if (c == quote) {
            if (!allow_unquoted_quotes) {
                // Quote not allowed - this is an error
                *found_special = true;
                *special_char = c;
                *special_pos = pos;
                return pos - start_offset;
            }
            // Quote allowed, continue scanning (it's just part of the field)
        }

        pos++;
    }

    // Reached end of chunk without finding special character
    *found_special = false;
    return pos - start_offset;
}

// Check if unescaping is needed
bool csv_stream_field_needs_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len
) {
    // For quoted fields with doubled-quote escape, we always need to check for doubled quotes
    // even if needs_unescape is false (doubled quotes may have been detected during parsing
    // or may exist in the data from previous chunks)
    if (stream->field.needs_unescape) {
        return true;
    }

    if (stream->field.is_quoted &&
        stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE) {
        // Check if there are any doubled quotes in the data
        size_t check_len = input_len;
        if (input_data == stream->field.buffer) {
            check_len = stream->field.buffer_used < input_len ? stream->field.buffer_used : input_len;
        }
        for (size_t i = 0; i + 1 < check_len; i++) {
            if (input_data[i] == stream->opts.dialect.quote &&
                input_data[i + 1] == stream->opts.dialect.quote) {
                return true;
            }
        }
    }

    return false;
}

// Handle case where no unescaping is needed
text_csv_status csv_stream_unescape_field_no_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
) {
    // No unescaping needed, but we still need to ensure the data is in a stable buffer
    // If input_data points to field buffer, we can return it directly
    // But we must use buffer_used as the actual length, not input_len
    if (input_data == stream->field.buffer) {
        // When input_data is field buffer, use buffer_used as the actual length
        // This ensures we never read past the actual data in the buffer
        // Use the minimum of input_len and buffer_used to prevent reading past buffer
        size_t actual_len = stream->field.buffer_used;
        if (input_len < actual_len) {
            actual_len = input_len;
        }
        // Also ensure it doesn't exceed buffer size
        if (actual_len > stream->field.buffer_size) {
            actual_len = stream->field.buffer_size;
        }
        *output_data = input_data;
        *output_len = actual_len;
        return TEXT_CSV_OK;
    }

    // Check if input_data points to the original input buffer (for in-situ mode)
    if (stream->opts.in_situ_mode && stream->original_input_buffer && input_data) {
        if (csv_stream_can_use_in_situ(stream, input_data, input_len)) {
            *output_data = input_data;
            *output_len = input_len;
            return TEXT_CSV_OK;
        }
    }

    // Input is not in field buffer and not in original input - copy it to ensure stability
    // Safety check: ensure input_data is valid (not NULL)
    if (!input_data) {
        return TEXT_CSV_E_INVALID;
    }
    // Safety check: check for integer overflow in allocation size
    if (input_len > SIZE_MAX - 1) {
        return TEXT_CSV_E_OOM;
    }

    // Determine if we need to grow the buffer
    bool need_grow = false;
    if (stream->field.buffer_size == 0) {
        need_grow = true;
    } else if (stream->field.buffer_size < input_len + 1) {
        need_grow = true;
    }

    if (need_grow) {
        text_csv_status status = csv_field_buffer_grow(&stream->field, input_len + 1);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    // Only copy the actual data length, not more
    size_t copy_len = input_len;
    if (copy_len > stream->field.buffer_size) {
        copy_len = stream->field.buffer_size;
    }

    // Safety check: ensure we have a valid buffer before copying
    if (!stream->field.buffer || copy_len == 0) {
        *output_data = stream->field.buffer;
        *output_len = 0;
        return TEXT_CSV_OK;
    }

    // Ensure buffer_used is reset before copying (defensive)
    stream->field.buffer_used = 0;
    memcpy(stream->field.buffer, input_data, copy_len);
    // Set buffer_used to the actual copied length
    stream->field.buffer_used = copy_len;
    stream->field.is_buffered = true;
    stream->field.data = stream->field.buffer;
    *output_data = stream->field.buffer;
    *output_len = copy_len;
    return TEXT_CSV_OK;
}

// Handle unescaping
text_csv_status csv_stream_unescape_field_with_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
) {
    // Check if input_data points to field buffer - if so, we need to handle in-place operation
    bool input_is_field_buffer = (input_data == stream->field.buffer);
    const char* actual_input = input_data;  // Local variable to track actual input pointer

    // Ensure field buffer is large enough for output (worst case: same size)
    size_t needed_size = input_len;

    // If input is in field buffer and we need to grow, we must copy the data first
    if (input_is_field_buffer && stream->field.buffer_size < needed_size) {
        // Safety: clamp input_len to actual valid data in buffer
        size_t copy_len = input_len;
        if (copy_len > stream->field.buffer_used) {
            copy_len = stream->field.buffer_used;
        }
        // Save the current data before reallocation
        char* temp_buffer = malloc(copy_len);
        if (!temp_buffer) {
            return TEXT_CSV_E_OOM;
        }
        memcpy(temp_buffer, stream->field.buffer, copy_len);

        // Now grow the buffer
        text_csv_status status = csv_field_buffer_grow(&stream->field, needed_size);
        if (status != TEXT_CSV_OK) {
            free(temp_buffer);
            return status;
        }

        // Restore the data
        memcpy(stream->field.buffer, temp_buffer, copy_len);
        free(temp_buffer);
        actual_input = stream->field.buffer;  // Update pointer after reallocation
        // Update buffer_used to reflect the copied data
        stream->field.buffer_used = copy_len;
    } else if (!input_is_field_buffer) {
        // Input is not in field buffer - safe to grow
        text_csv_status status = csv_field_buffer_grow(&stream->field, needed_size);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    // CRITICAL: When unescaping in-place, we must not read past the actual valid data
    // Clamp input_len to buffer_used if input is in field buffer
    size_t actual_input_len = input_len;
    if (input_is_field_buffer && input_len > stream->field.buffer_used) {
        actual_input_len = stream->field.buffer_used;
    }

    // Unescape doubled quotes (in-place if input is field buffer, otherwise copy)
    size_t out_idx = 0;
    for (size_t in_idx = 0; in_idx < actual_input_len; in_idx++) {
        if (stream->opts.dialect.escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE &&
            in_idx + 1 < actual_input_len &&
            actual_input[in_idx] == stream->opts.dialect.quote &&
            actual_input[in_idx + 1] == stream->opts.dialect.quote) {
            // Doubled quote - output single quote
            if (out_idx >= stream->field.buffer_size) {
                return TEXT_CSV_E_OOM;
            }
            stream->field.buffer[out_idx++] = stream->opts.dialect.quote;
            in_idx++;  // Skip second quote
        } else {
            // Regular character
            if (out_idx >= stream->field.buffer_size) {
                return TEXT_CSV_E_OOM;
            }
            stream->field.buffer[out_idx++] = actual_input[in_idx];
        }
    }

    // Safety check: ensure output length doesn't exceed buffer size
    // This should never happen due to bounds checking in the loop above, but be defensive
    if (out_idx > stream->field.buffer_size) {
        out_idx = stream->field.buffer_size;
    }
    // Additional check: if input was in field buffer, out_idx should not exceed what we read
    if (input_is_field_buffer && out_idx > actual_input_len) {
        // This should never happen (unescaping can only shrink, not grow)
        // But be defensive
        out_idx = actual_input_len;
    }

    *output_data = stream->field.buffer;
    *output_len = out_idx;

    // Update buffer_used to reflect the actual unescaped data length
    // This is critical when unescaping in-place in field buffer
    // Update AFTER setting output pointers but the callback is synchronous so this is safe
    if (input_is_field_buffer) {
        stream->field.buffer_used = out_idx;
    } else if (*output_data == stream->field.buffer) {
        // Output is in field buffer (we copied input to field buffer)
        stream->field.buffer_used = out_idx;
    }

    return TEXT_CSV_OK;
}

// Unescape doubled quotes in field data
// Converts doubled quotes ("") to single quotes (") in the field data.
// The result is written to the field_buffer, which must be large enough.
text_csv_status csv_stream_unescape_field(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
) {
    if (!csv_stream_field_needs_unescape(stream, input_data, input_len)) {
        return csv_stream_unescape_field_no_unescape(
            stream, input_data, input_len, output_data, output_len);
    }

    return csv_stream_unescape_field_with_unescape(
        stream, input_data, input_len, output_data, output_len);
}
