/**
 * @file csv_stream_buffer.c
 * @brief Buffer management for CSV streaming parser
 *
 * Handles field buffer management, growth, and chunk boundary handling.
 */

#include "csv_stream_internal.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Field buffer helper functions
void csv_field_buffer_init(csv_field_buffer* fb) {
    memset(fb, 0, sizeof(*fb));
    fb->start_offset = SIZE_MAX;
}

void csv_field_buffer_clear(csv_field_buffer* fb) {
    fb->data = NULL;
    fb->length = 0;
    fb->is_buffered = false;
    fb->needs_unescape = false;
    fb->start_offset = SIZE_MAX;
    fb->buffer_used = 0;  // Reset buffer usage for reuse
    // Note: Don't free buffer here - reuse it
}

text_csv_status csv_field_buffer_set_from_input(
    csv_field_buffer* fb,
    const char* input_data,
    size_t input_len,
    bool is_quoted,
    size_t start_offset
) {
    fb->data = input_data;
    fb->length = input_len;
    fb->is_quoted = is_quoted;
    fb->is_buffered = false;
    fb->needs_unescape = false;
    fb->start_offset = start_offset;
    return TEXT_CSV_OK;
}

text_csv_status csv_field_buffer_grow(
    csv_field_buffer* fb,
    size_t needed
) {
    if (fb->buffer_size >= needed) {
        return TEXT_CSV_OK;
    }

    size_t new_size;
    if (fb->buffer_size == 0) {
        // Initial allocation - use minimum size or needed size, whichever is larger
        new_size = (needed < CSV_FIELD_BUFFER_INITIAL_SIZE) ? CSV_FIELD_BUFFER_INITIAL_SIZE : needed;
    } else {
        // Hybrid growth strategy:
        // - For small buffers (< 1KB): grow by fixed increments (64 bytes)
        // - For large buffers (>= 1KB): use doubling strategy
        if (fb->buffer_size < CSV_BUFFER_SMALL_THRESHOLD) {
            // Small buffer: grow by fixed increment
            new_size = fb->buffer_size + 64;
            if (new_size < needed) {
                new_size = needed;
            }
        } else {
            // Large buffer: double the size
            new_size = fb->buffer_size * CSV_BUFFER_GROWTH_MULTIPLIER;
            if (new_size < needed) {
                new_size = needed;
            }
        }
    }

    if (new_size < fb->buffer_size) {
        return TEXT_CSV_E_OOM;
    }

    char* new_buffer = realloc(fb->buffer, new_size);
    if (!new_buffer) {
        return TEXT_CSV_E_OOM;
    }

    // Update data pointer if it was pointing to the old buffer
    if (fb->is_buffered && fb->data == fb->buffer) {
        fb->data = new_buffer;
    }
    fb->buffer = new_buffer;
    fb->buffer_size = new_size;
    return TEXT_CSV_OK;
}

text_csv_status csv_field_buffer_append(
    csv_field_buffer* fb,
    const char* data,
    size_t len
) {
    // Grow buffer if needed
    if (fb->buffer_used + len > fb->buffer_size) {
        text_csv_status status = csv_field_buffer_grow(fb, fb->buffer_used + len);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    // Append data
    memcpy(fb->buffer + fb->buffer_used, data, len);
    fb->buffer_used += len;
    fb->data = fb->buffer;
    fb->length = fb->buffer_used;
    fb->is_buffered = true;
    fb->start_offset = SIZE_MAX;  // No longer tracking offset when buffered
    return TEXT_CSV_OK;
}

bool csv_field_buffer_can_use_in_situ(
    csv_field_buffer* fb
) {
    if (!fb->original_input || !fb->data) {
        return false;
    }

    // Check if data points to original input buffer
    if (fb->data >= fb->original_input) {
        size_t offset_from_start = (size_t)(fb->data - fb->original_input);
        // Check that field fits within buffer and doesn't overflow
        if (offset_from_start <= fb->original_input_len &&
            fb->length <= fb->original_input_len - offset_from_start) {
            return true;
        }
    }

    return false;
}

text_csv_status csv_field_buffer_ensure_buffered(
    csv_field_buffer* fb
) {
    if (fb->is_buffered) {
        return TEXT_CSV_OK;
    }

    // Check if we can use in-situ mode
    if (csv_field_buffer_can_use_in_situ(fb)) {
        // Can use in-situ - no need to buffer
        return TEXT_CSV_OK;
    }

    // Need to buffer - copy data
    if (fb->data && fb->length > 0) {
        return csv_field_buffer_append(fb, fb->data, fb->length);
    }

    // Empty field - just allocate buffer
    text_csv_status status = csv_field_buffer_grow(fb, CSV_FIELD_BUFFER_INITIAL_SIZE);
    if (status != TEXT_CSV_OK) {
        return status;
    }
    fb->buffer_used = 0;
    fb->is_buffered = true;
    fb->data = fb->buffer;
    fb->length = 0;
    fb->start_offset = SIZE_MAX;
    return TEXT_CSV_OK;
}

void csv_field_buffer_set_original_input(
    csv_field_buffer* fb,
    const char* original_input,
    size_t original_input_len
) {
    fb->original_input = original_input;
    fb->original_input_len = original_input_len;
}

// Buffer field data when reaching end of chunk in middle of field
// When a field spans chunks, we need to copy the partial field data from the current
// chunk into field_buffer so it remains valid when the chunk is cleared.
text_csv_status csv_stream_buffer_field_at_chunk_boundary(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t field_start_offset,
    size_t current_offset
) {
    // If field is already buffered, just append new data
    if (stream->field.is_buffered) {
        // Early return if no valid data to append
        if (field_start_offset >= current_offset || field_start_offset >= process_len || field_start_offset == SIZE_MAX) {
            return TEXT_CSV_OK;
        }

        size_t append_len = current_offset - field_start_offset;
        if (append_len > process_len - field_start_offset) {
            append_len = process_len - field_start_offset;
        }

        // Early return if nothing to append
        if (append_len == 0) {
            return TEXT_CSV_OK;
        }

        text_csv_status status = csv_stream_append_to_field_buffer(stream,
            process_input + field_start_offset, append_len);
        if (status != TEXT_CSV_OK) {
            return status;
        }
        return TEXT_CSV_OK;
    }

    // Field not yet buffered - copy from process_input to field_buffer
    // Early return if invalid offset
    if (field_start_offset >= current_offset || field_start_offset >= process_len || field_start_offset == SIZE_MAX) {
        // Empty field or invalid offset - just allocate buffer
        text_csv_status status = csv_field_buffer_grow(&stream->field, CSV_FIELD_BUFFER_INITIAL_SIZE);
        if (status != TEXT_CSV_OK) {
            return status;
        }
        stream->field.buffer_used = 0;
        stream->field.is_buffered = true;
        stream->field.data = stream->field.buffer;
        stream->field.length = 0;
        return TEXT_CSV_OK;
    }

    size_t copy_len = current_offset - field_start_offset;
    if (copy_len > process_len - field_start_offset) {
        copy_len = process_len - field_start_offset;
    }

    if (copy_len > 0) {
        text_csv_status status = csv_field_buffer_append(&stream->field,
            process_input + field_start_offset, copy_len);
        if (status != TEXT_CSV_OK) {
            return status;
        }
        return TEXT_CSV_OK;
    }

    // Empty field - just allocate buffer
    text_csv_status status = csv_field_buffer_grow(&stream->field, CSV_FIELD_BUFFER_INITIAL_SIZE);
    if (status != TEXT_CSV_OK) {
        return status;
    }
    stream->field.buffer_used = 0;
    stream->field.is_buffered = true;
    stream->field.data = stream->field.buffer;
    stream->field.length = 0;
    return TEXT_CSV_OK;
}

// Buffer unquoted field if in-situ mode cannot be used
// This reduces nesting in the newline handling code
text_csv_status csv_stream_buffer_unquoted_field_if_needed(
    text_csv_stream* stream
) {
    return csv_field_buffer_ensure_buffered(&stream->field);
}

// Ensure field is buffered at chunk boundary (helper to reduce duplication)
// This function handles the common pattern of checking if field is buffered,
// calculating field_start_off, and calling csv_stream_buffer_field_at_chunk_boundary
text_csv_status csv_stream_ensure_field_buffered(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t current_offset
) {
    if (stream->field.is_buffered) {
        // Field is already buffered - ensure data points to buffer
        stream->field.data = stream->field.buffer;
        stream->field.length = stream->field.buffer_used;
        return TEXT_CSV_OK;
    }

    // Calculate field_start_off
    size_t field_start_off = stream->field.start_offset;
    if (field_start_off == SIZE_MAX && stream->field.data) {
        if (stream->field.data >= process_input &&
            stream->field.data < process_input + process_len) {
            field_start_off = stream->field.data - process_input;
        } else {
            // field_start exists but is not in current chunk - use 0 as fallback
            field_start_off = 0;
        }
    }

    // Buffer the field
    text_csv_status status = csv_stream_buffer_field_at_chunk_boundary(
        stream, process_input, process_len, field_start_off, current_offset);
    if (status != TEXT_CSV_OK) {
        return status;
    }

    return TEXT_CSV_OK;
}

// Grow field buffer (wrapper for field buffer structure)
text_csv_status csv_stream_grow_field_buffer(
    text_csv_stream* stream,
    size_t needed
) {
    return csv_field_buffer_grow(&stream->field, needed);
}

// Append to field buffer (wrapper for field buffer structure)
text_csv_status csv_stream_append_to_field_buffer(
    text_csv_stream* stream,
    const char* data,
    size_t data_len
) {
    // If field is not yet buffered, we need to buffer existing data first
    if (!stream->field.is_buffered && stream->field.data && stream->field.length > 0) {
        text_csv_status status = csv_field_buffer_append(&stream->field, stream->field.data, stream->field.length);
        if (status != TEXT_CSV_OK) {
            return status;
        }
        // length is already set correctly by csv_field_buffer_append
    }

    return csv_field_buffer_append(&stream->field, data, data_len);
}
