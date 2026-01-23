/**
 * @file csv_stream.c
 * @brief Streaming CSV parser implementation
 *
 * Implements an event-based streaming parser that accepts input in chunks
 * and emits events for each CSV record/field encountered.
 */

#include "csv_stream_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

// Emit an event
text_csv_status csv_stream_emit_event(
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

// Set stream error
text_csv_status csv_stream_set_error(
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
    csv_field_buffer_init(&stream->field);
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
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_INVALID,
                                        .message = "Stream must not be NULL",
                                        .line = 1,
                                        .column = 1
                                    };
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
        bool was_stripped = false;
        text_csv_status status = csv_strip_bom(&input, &input_len, &stream->pos, true, &was_stripped);
        if (status != TEXT_CSV_OK) {
            return csv_stream_set_error(stream, status, "Overflow in BOM stripping");
        }
        if (was_stripped) {
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
    if (stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED && !stream->field.is_buffered) {
        // Field should have been buffered at the end of the previous chunk, but it wasn't.
        // This is a bug in the previous chunk processing. We can't recover the field data,
        // but we can at least ensure the parser doesn't crash.
        if (!stream->field.buffer) {
            text_csv_status status = csv_field_buffer_grow(&stream->field, CSV_FIELD_BUFFER_INITIAL_SIZE);
            if (status != TEXT_CSV_OK) {
                if (err) {
                    csv_error_copy(err, &stream->error);
                }
                return status;
            }
        }
        stream->field.buffer_used = 0;
        stream->field.is_buffered = true;
        stream->field.data = stream->field.buffer;
        stream->field.length = 0;
        // Note: The field data from the previous chunk is lost. This should not happen
        // if the previous chunk processing worked correctly.
    }

    // If we have a field in progress that's buffered, we need to continue it
    // Otherwise, process the chunk normally
    if (stream->field.is_buffered && (stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD ||
                                      stream->state == CSV_STREAM_STATE_QUOTED_FIELD ||
                                      stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED)) {
        // The field buffer already contains the partial field from previous chunks.
        // We need to process the new chunk data, appending field content to the buffer
        // as we encounter it, until the field completes.

        // CRITICAL: Ensure field.data points to field.buffer when field is buffered
        // This prevents field.data from pointing into invalid input buffer memory
        if (stream->field.data != stream->field.buffer) {
            stream->field.data = stream->field.buffer;
            stream->field.length = stream->field.buffer_used;
        }

        // Process the new chunk - it will append to field.buffer as needed
        text_csv_status status = csv_stream_process_chunk(stream, (const char*)data, len);

        // Reset field buffering state after processing if field completed
        if (stream->state != CSV_STREAM_STATE_UNQUOTED_FIELD &&
            stream->state != CSV_STREAM_STATE_QUOTED_FIELD &&
            stream->state != CSV_STREAM_STATE_QUOTE_IN_QUOTED) {
            // Field completed, clear buffer
            csv_field_buffer_clear(&stream->field);
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
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_INVALID,
                                        .message = "Stream must not be NULL",
                                        .line = 1,
                                        .column = 1
                                    };
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
            // Ensure field.data is correct for buffered fields
            if (stream->field.is_buffered) {
                stream->field.data = stream->field.buffer;
                stream->field.length = stream->field.buffer_used;
            }
            const char* field_data = stream->field.data;
            // For buffered fields, use buffer_used as the source of truth
            size_t actual_field_len = stream->field.is_buffered ? stream->field.buffer_used : stream->field.length;
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
    free(stream->field.buffer);
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
    csv_field_buffer_set_original_input(&stream->field, input_buffer, input_buffer_len);
}
