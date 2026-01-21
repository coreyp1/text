/**
 * @file csv_writer.c
 * @brief CSV writer infrastructure implementation
 *
 * This file implements the sink abstraction for writing CSV output
 * to various destinations.
 */

#include <ghoti.io/text/csv/csv_writer.h>
#include <ghoti.io/text/csv/csv_core.h>
#include "csv_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

// Internal write callback for growable buffer sink
static text_csv_status buffer_write_fn(void* user, const char* bytes, size_t len) {
  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)user;
  if (!buf || !bytes) {
    return TEXT_CSV_E_INVALID;
  }

  // Check for integer overflow in needed calculation
  if (len > SIZE_MAX - buf->used || buf->used > SIZE_MAX - len - 1) {
    return TEXT_CSV_E_LIMIT; // Overflow
  }

  // Check if we need to grow the buffer
  size_t needed = buf->used + len + 1; // +1 for null terminator
  if (needed > buf->size) {
    // Grow buffer (double size strategy, with minimum growth)
    size_t new_size = buf->size;
    if (new_size == 0) {
      new_size = 256; // Initial size
    }
    while (new_size < needed) {
      // Check for overflow before doubling
      if (new_size > SIZE_MAX / 2) {
        return TEXT_CSV_E_OOM; // Overflow - cannot grow further
      }
      new_size *= 2;
    }

    char* new_data = (char*)realloc(buf->data, new_size);
    if (!new_data) {
      return TEXT_CSV_E_OOM; // Out of memory
    }
    buf->data = new_data;
    buf->size = new_size;
  }

  // Verify bounds before copying (defensive check)
  if (buf->used + len > buf->size - 1) {
    return TEXT_CSV_E_LIMIT; // Should not happen, but be safe
  }

  // Copy data
  memcpy(buf->data + buf->used, bytes, len);
  buf->used += len;
  // Verify bounds before writing null terminator
  if (buf->used < buf->size) {
    buf->data[buf->used] = '\0'; // Null terminate for convenience
  }

  return TEXT_CSV_OK; // Success
}

// Internal write callback for fixed buffer sink
static text_csv_status fixed_buffer_write_fn(void* user, const char* bytes, size_t len) {
  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)user;
  if (!buf || !bytes) {
    return TEXT_CSV_E_INVALID;
  }

  // Calculate how much we can write (leave room for null terminator)
  // We can write up to (size - used - 1) bytes to leave room for null terminator
  // If used >= size, available = 0 (no underflow possible since size_t is unsigned)
  size_t available = 0;
  if (buf->size > buf->used) {
    // Check for underflow: if size - used would underflow, but since size > used,
    // we know size - used is valid. Then check if we can subtract 1.
    if (buf->size - buf->used > 1) {
      available = buf->size - buf->used - 1;
    }
    // else: available remains 0 (only room for null terminator, no data)
  }

  size_t to_write = len;
  bool truncated = false;

  if (to_write > available) {
    to_write = available;
    truncated = true;
    buf->truncated = true;
  }

  // Copy data - verify bounds before copying
  if (to_write > 0 && available > 0) {
    // Verify bounds: used + to_write must not exceed size - 1
    // This check ensures we don't write beyond buffer bounds
    if (buf->used <= buf->size - 1 && to_write <= buf->size - 1 - buf->used) {
      memcpy(buf->data + buf->used, bytes, to_write);
      buf->used += to_write;
      // Verify bounds before writing null terminator
      if (buf->used < buf->size) {
        buf->data[buf->used] = '\0';
      }
    } else {
      // Should not happen if logic is correct, but be safe
      truncated = true;
      buf->truncated = true;
    }
  }

  // Return error if truncation occurred
  return truncated ? TEXT_CSV_E_WRITE : TEXT_CSV_OK;
}

text_csv_status text_csv_sink_buffer(text_csv_sink* sink) {
  if (!sink) {
    return TEXT_CSV_E_INVALID;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)malloc(sizeof(text_csv_buffer_sink));
  if (!buf) {
    return TEXT_CSV_E_OOM;
  }

  buf->data = NULL;
  buf->size = 0;
  buf->used = 0;

  sink->write = buffer_write_fn;
  sink->user = buf;

  return TEXT_CSV_OK;
}

const char* text_csv_sink_buffer_data(const text_csv_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return NULL;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)sink->user;
  if (!buf) {
    return NULL;
  }

  return buf->data ? buf->data : "";
}

size_t text_csv_sink_buffer_size(const text_csv_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return 0;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

void text_csv_sink_buffer_free(text_csv_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)sink->user;
  if (buf) {
    free(buf->data);
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

text_csv_status text_csv_sink_fixed_buffer(
  text_csv_sink* sink,
  char* buffer,
  size_t size
) {
  if (!sink || !buffer || size == 0) {
    return TEXT_CSV_E_INVALID;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)malloc(sizeof(text_csv_fixed_buffer_sink));
  if (!buf) {
    return TEXT_CSV_E_OOM;
  }

  buf->data = buffer;
  buf->size = size;
  buf->used = 0;
  buf->truncated = false;

  // Initialize buffer with null terminator
  if (size > 0) {
    buffer[0] = '\0';
  }

  sink->write = fixed_buffer_write_fn;
  sink->user = buf;

  return TEXT_CSV_OK;
}

size_t text_csv_sink_fixed_buffer_used(const text_csv_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return 0;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

bool text_csv_sink_fixed_buffer_truncated(const text_csv_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return false;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)sink->user;
  if (!buf) {
    return false;
  }

  return buf->truncated;
}

void text_csv_sink_fixed_buffer_free(text_csv_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)sink->user;
  if (buf) {
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

// ============================================================================
// Field Escaping and Quoting Logic
// ============================================================================

/**
 * @brief Determine if a field needs to be quoted
 *
 * Checks the write options to determine if a field should be quoted:
 * - quote_all_fields: always quote
 * - quote_empty_fields: quote if field is empty
 * - quote_if_needed: quote if field contains delimiter, quote char, or newline
 *
 * @param field_data Field data (may be NULL if field_len is 0)
 * @param field_len Field length in bytes (0 for empty field)
 * @param opts Write options
 * @return true if field should be quoted, false otherwise
 */
static bool csv_field_needs_quoting(
    const char* field_data,
    size_t field_len,
    const text_csv_write_options* opts
) {
    if (!opts) {
        return false;
    }

    // quote_all_fields: always quote
    if (opts->quote_all_fields) {
        return true;
    }

    // quote_empty_fields: quote if empty
    if (opts->quote_empty_fields && field_len == 0) {
        return true;
    }

    // quote_if_needed: check for special characters
    if (opts->quote_if_needed && field_data && field_len > 0) {
        const char delimiter = opts->dialect.delimiter;
        const char quote = opts->dialect.quote;

        for (size_t i = 0; i < field_len; i++) {
            char c = field_data[i];

            // Check for delimiter
            if (c == delimiter) {
                return true;
            }

            // Check for quote character
            if (c == quote) {
                return true;
            }

            // Check for newline (LF, CR, or CRLF)
            if (c == '\n' || c == '\r') {
                return true;
            }

            // Check for leading/trailing spaces if policy requires
            // (This is typically handled by the parser, but we check here
            // for completeness - RFC 4180 says spaces are significant)
        }
    }

    return false;
}

/**
 * @brief Calculate the escaped length of a field
 *
 * Calculates how many bytes the field will take after escaping quotes
 * according to the escape mode.
 *
 * @param field_data Field data
 * @param field_len Field length in bytes
 * @param escape_mode Escape mode
 * @param quote_char Quote character
 * @return Escaped length in bytes
 */
static size_t csv_field_escaped_length(
    const char* field_data,
    size_t field_len,
    text_csv_escape_mode escape_mode,
    char quote_char
) {
    if (!field_data || field_len == 0) {
        return field_len;
    }

    size_t escaped_len = field_len;

    switch (escape_mode) {
        case TEXT_CSV_ESCAPE_DOUBLED_QUOTE: {
            // Each quote character becomes two quotes
            for (size_t i = 0; i < field_len; i++) {
                if (field_data[i] == quote_char) {
                    // Check for overflow before incrementing
                    if (escaped_len == SIZE_MAX) {
                        return SIZE_MAX; // Overflow - return max value
                    }
                    escaped_len++; // One extra character per quote
                }
            }
            break;
        }

        case TEXT_CSV_ESCAPE_BACKSLASH: {
            // Each quote becomes \" and each backslash becomes double backslash
            for (size_t i = 0; i < field_len; i++) {
                if (field_data[i] == quote_char || field_data[i] == '\\') {
                    // Check for overflow before incrementing
                    if (escaped_len == SIZE_MAX) {
                        return SIZE_MAX; // Overflow - return max value
                    }
                    escaped_len++; // One extra character per quote or backslash
                }
            }
            break;
        }

        case TEXT_CSV_ESCAPE_NONE: {
            // No escaping - length unchanged
            break;
        }

        default:
            // Unknown escape mode - treat as no escaping
            break;
    }

    return escaped_len;
}

/**
 * @brief Escape a field into a buffer
 *
 * Escapes quotes in a field according to the escape mode and writes
 * the result to the output buffer. The output buffer must be large
 * enough to hold the escaped field (use csv_field_escaped_length to
 * calculate the required size).
 *
 * @param field_data Field data to escape
 * @param field_len Field length in bytes
 * @param output_buffer Output buffer (must be large enough)
 * @param output_buffer_size Size of output buffer
 * @param escape_mode Escape mode
 * @param quote_char Quote character
 * @param output_len Output parameter: number of bytes written
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if buffer too small
 */
static text_csv_status csv_field_escape(
    const char* field_data,
    size_t field_len,
    char* output_buffer,
    size_t output_buffer_size,
    text_csv_escape_mode escape_mode,
    char quote_char,
    size_t* output_len
) {
    if (!field_data || !output_buffer || !output_len) {
        return TEXT_CSV_E_INVALID;
    }

    if (field_len == 0) {
        *output_len = 0;
        return TEXT_CSV_OK;
    }

    size_t out_idx = 0;

    switch (escape_mode) {
        case TEXT_CSV_ESCAPE_DOUBLED_QUOTE: {
            // Double each quote character
            for (size_t i = 0; i < field_len; i++) {
                if (out_idx + 1 > output_buffer_size) {
                    return TEXT_CSV_E_INVALID;
                }
                if (field_data[i] == quote_char) {
                    // Write two quotes
                    output_buffer[out_idx++] = quote_char;
                    if (out_idx + 1 > output_buffer_size) {
                        return TEXT_CSV_E_INVALID;
                    }
                    output_buffer[out_idx++] = quote_char;
                } else {
                    output_buffer[out_idx++] = field_data[i];
                }
            }
            break;
        }

        case TEXT_CSV_ESCAPE_BACKSLASH: {
            // Escape quotes with backslash and backslashes with double backslash
            for (size_t i = 0; i < field_len; i++) {
                if (field_data[i] == quote_char) {
                    // Write backslash followed by quote
                    if (out_idx + 2 > output_buffer_size) {
                        return TEXT_CSV_E_INVALID;
                    }
                    output_buffer[out_idx++] = '\\';
                    output_buffer[out_idx++] = quote_char;
                } else if (field_data[i] == '\\') {
                    // Write double backslash
                    if (out_idx + 2 > output_buffer_size) {
                        return TEXT_CSV_E_INVALID;
                    }
                    output_buffer[out_idx++] = '\\';
                    output_buffer[out_idx++] = '\\';
                } else {
                    if (out_idx + 1 > output_buffer_size) {
                        return TEXT_CSV_E_INVALID;
                    }
                    output_buffer[out_idx++] = field_data[i];
                }
            }
            break;
        }

        case TEXT_CSV_ESCAPE_NONE: {
            // No escaping - copy as-is
            if (field_len > output_buffer_size) {
                return TEXT_CSV_E_INVALID;
            }
            memcpy(output_buffer, field_data, field_len);
            out_idx = field_len;
            break;
        }

        default:
            // Unknown escape mode - copy as-is
            if (field_len > output_buffer_size) {
                return TEXT_CSV_E_INVALID;
            }
            memcpy(output_buffer, field_data, field_len);
            out_idx = field_len;
            break;
    }

    *output_len = out_idx;
    return TEXT_CSV_OK;
}

/**
 * @brief Write a field with proper quoting and escaping
 *
 * Writes a field to the sink with appropriate quoting and escaping
 * according to the write options and dialect.
 *
 * @param sink Output sink
 * @param field_data Field data (may be NULL if field_len is 0)
 * @param field_len Field length in bytes
 * @param opts Write options
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_INTERNAL_API text_csv_status csv_write_field(
    const text_csv_sink* sink,
    const char* field_data,
    size_t field_len,
    const text_csv_write_options* opts
) {
    if (!sink || !sink->write || !opts) {
        return TEXT_CSV_E_INVALID;
    }

    const char quote_char = opts->dialect.quote;
    const text_csv_escape_mode escape_mode = opts->dialect.escape;
    bool needs_quoting = csv_field_needs_quoting(field_data, field_len, opts);

    // If field doesn't need quoting and has no quotes to escape, write directly
    if (!needs_quoting && field_len > 0 && field_data) {
        // Check if field contains quotes that need escaping
        bool has_quotes = false;
        for (size_t i = 0; i < field_len; i++) {
            if (field_data[i] == quote_char) {
                has_quotes = true;
                break;
            }
        }

        // If no quotes and no special chars, write directly
        if (!has_quotes) {
            return sink->write(sink->user, field_data, field_len);
        }
    }

    // Field needs quoting or escaping - handle it
    if (needs_quoting) {
        // Write opening quote
        text_csv_status status = sink->write(sink->user, &quote_char, 1);
        if (status != TEXT_CSV_OK) {
            return status;
        }

        // Escape and write field content
        if (field_len > 0 && field_data) {
            // Calculate escaped length
            size_t escaped_len = csv_field_escaped_length(
                field_data, field_len, escape_mode, quote_char
            );

            // Check for overflow in escaped length calculation
            if (escaped_len == SIZE_MAX || escaped_len < field_len) {
                // Overflow detected or invalid calculation - return error
                return TEXT_CSV_E_LIMIT;
            }

            // Allocate temporary buffer for escaped field
            // For efficiency, use stack buffer for small fields
            // and heap buffer for large fields
            const size_t STACK_BUFFER_SIZE = 256;
            char* escape_buffer = NULL;
            bool use_stack = (escaped_len < STACK_BUFFER_SIZE);
            char stack_buffer[STACK_BUFFER_SIZE];

            if (use_stack) {
                escape_buffer = stack_buffer;
            } else {
                // Check for reasonable allocation size (prevent excessive memory allocation)
                // In practice, escaped_len should be reasonable, but be defensive
                if (escaped_len > SIZE_MAX / 2) {
                    // Avoid allocating more than half of addressable space
                    return TEXT_CSV_E_LIMIT;
                }
                escape_buffer = (char*)malloc(escaped_len);
                if (!escape_buffer) {
                    return TEXT_CSV_E_OOM;
                }
            }

            size_t actual_escaped_len = 0;
            status = csv_field_escape(
                field_data, field_len,
                escape_buffer, escaped_len,
                escape_mode, quote_char,
                &actual_escaped_len
            );

            if (status == TEXT_CSV_OK) {
                status = sink->write(sink->user, escape_buffer, actual_escaped_len);
            }

            if (!use_stack) {
                free(escape_buffer);
            }

            if (status != TEXT_CSV_OK) {
                return status;
            }
        }

        // Write closing quote
        return sink->write(sink->user, &quote_char, 1);
    } else {
        // Field doesn't need quoting but may need escaping (for unquoted quotes)
        // This is an edge case - if allow_unquoted_quotes is true, we might
        // have quotes in unquoted fields. For now, we'll escape them anyway
        // if escape mode is not NONE.
        if (escape_mode != TEXT_CSV_ESCAPE_NONE && field_len > 0 && field_data) {
            size_t escaped_len = csv_field_escaped_length(
                field_data, field_len, escape_mode, quote_char
            );

            // Check for overflow in escaped length calculation
            if (escaped_len == SIZE_MAX || escaped_len < field_len) {
                // Overflow detected or invalid calculation - return error
                return TEXT_CSV_E_LIMIT;
            }

            if (escaped_len > field_len) {
                // Need to escape - use temporary buffer
                const size_t STACK_BUFFER_SIZE = 256;
                char* escape_buffer = NULL;
                bool use_stack = (escaped_len < STACK_BUFFER_SIZE);
                char stack_buffer[STACK_BUFFER_SIZE];

                if (use_stack) {
                    escape_buffer = stack_buffer;
                } else {
                    // Check for reasonable allocation size (prevent excessive memory allocation)
                    if (escaped_len > SIZE_MAX / 2) {
                        // Avoid allocating more than half of addressable space
                        return TEXT_CSV_E_LIMIT;
                    }
                    escape_buffer = (char*)malloc(escaped_len);
                    if (!escape_buffer) {
                        return TEXT_CSV_E_OOM;
                    }
                }

                size_t actual_escaped_len = 0;
                text_csv_status status = csv_field_escape(
                    field_data, field_len,
                    escape_buffer, escaped_len,
                    escape_mode, quote_char,
                    &actual_escaped_len
                );

                if (status == TEXT_CSV_OK) {
                    status = sink->write(sink->user, escape_buffer, actual_escaped_len);
                }

                if (!use_stack) {
                    free(escape_buffer);
                }

                return status;
            } else {
                // No escaping needed - write directly
                return sink->write(sink->user, field_data, field_len);
            }
        } else {
            // No escaping needed - write directly
            return sink->write(sink->user, field_data, field_len);
        }
    }
}

// ============================================================================
// Streaming Writer Implementation
// ============================================================================

/**
 * @brief Writer state enumeration
 */
typedef enum {
    CSV_WRITER_STATE_INITIAL,      ///< Initial state (no record open)
    CSV_WRITER_STATE_IN_RECORD,    ///< Record is open (fields can be written)
    CSV_WRITER_STATE_FINISHED      ///< Writer has been finished (no more writes)
} csv_writer_state;

/**
 * @brief CSV writer structure
 */
struct text_csv_writer {
    text_csv_sink sink;                    ///< Output sink (not owned)
    text_csv_write_options opts;           ///< Write options (copy)
    csv_writer_state state;                ///< Current writer state
    bool has_fields_in_record;             ///< Whether current record has any fields
    text_csv_status last_error;            ///< Last error status (if any)
};

text_csv_writer* text_csv_writer_new(
    const text_csv_sink* sink,
    const text_csv_write_options* opts
) {
    if (!sink || !sink->write || !opts) {
        return NULL;
    }

    text_csv_writer* writer = (text_csv_writer*)malloc(sizeof(text_csv_writer));
    if (!writer) {
        return NULL;
    }

    writer->sink = *sink;  // Copy sink structure
    writer->opts = *opts;  // Copy options
    writer->state = CSV_WRITER_STATE_INITIAL;
    writer->has_fields_in_record = false;
    writer->last_error = TEXT_CSV_OK;

    return writer;
}

text_csv_status text_csv_writer_record_begin(text_csv_writer* writer) {
    if (!writer) {
        return TEXT_CSV_E_INVALID;
    }

    // Check if already in a record
    if (writer->state == CSV_WRITER_STATE_IN_RECORD) {
        writer->last_error = TEXT_CSV_E_INVALID;
        return TEXT_CSV_E_INVALID;
    }

    // Check if already finished
    if (writer->state == CSV_WRITER_STATE_FINISHED) {
        writer->last_error = TEXT_CSV_E_INVALID;
        return TEXT_CSV_E_INVALID;
    }

    writer->state = CSV_WRITER_STATE_IN_RECORD;
    writer->has_fields_in_record = false;
    writer->last_error = TEXT_CSV_OK;

    return TEXT_CSV_OK;
}

text_csv_status text_csv_writer_field(
    text_csv_writer* writer,
    const void* bytes,
    size_t len
) {
    if (!writer) {
        return TEXT_CSV_E_INVALID;
    }

    // Check if not in a record
    if (writer->state != CSV_WRITER_STATE_IN_RECORD) {
        writer->last_error = TEXT_CSV_E_INVALID;
        return TEXT_CSV_E_INVALID;
    }

    // Insert delimiter before field if this is not the first field in the record
    if (writer->has_fields_in_record) {
        char delimiter = writer->opts.dialect.delimiter;
        text_csv_status status = writer->sink.write(writer->sink.user, &delimiter, 1);
        if (status != TEXT_CSV_OK) {
            writer->last_error = status;
            return status;
        }
    }

    // Write the field with proper quoting and escaping
    text_csv_status status = csv_write_field(
        &writer->sink,
        (const char*)bytes,
        len,
        &writer->opts
    );

    if (status != TEXT_CSV_OK) {
        writer->last_error = status;
        return status;
    }

    writer->has_fields_in_record = true;
    return TEXT_CSV_OK;
}

text_csv_status text_csv_writer_record_end(text_csv_writer* writer) {
    if (!writer) {
        return TEXT_CSV_E_INVALID;
    }

    // Check if not in a record
    if (writer->state != CSV_WRITER_STATE_IN_RECORD) {
        writer->last_error = TEXT_CSV_E_INVALID;
        return TEXT_CSV_E_INVALID;
    }

    // Write newline sequence
    const char* newline = writer->opts.newline;
    if (!newline) {
        newline = "\n";  // Default newline
    }
    // newline is expected to be a null-terminated string per API contract
    // strlen() is safe here as newline is either a string literal or null-terminated
    size_t newline_len = strlen(newline);

    text_csv_status status = writer->sink.write(writer->sink.user, newline, newline_len);
    if (status != TEXT_CSV_OK) {
        writer->last_error = status;
        return status;
    }

    writer->state = CSV_WRITER_STATE_INITIAL;
    writer->has_fields_in_record = false;
    return TEXT_CSV_OK;
}

text_csv_status text_csv_writer_finish(text_csv_writer* writer) {
    if (!writer) {
        return TEXT_CSV_OK;
    }

    // If a record is open, close it first
    if (writer->state == CSV_WRITER_STATE_IN_RECORD) {
        text_csv_status status = text_csv_writer_record_end(writer);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    // If trailing_newline is enabled and we've written at least one record,
    // write a final newline (but only if we haven't already written one)
    // Note: trailing_newline typically means add newline at end of file,
    // but since we already write newlines after each record, we only need
    // to add one if no records were written or if explicitly requested.
    // For simplicity, we'll skip this for now as it's typically handled
    // by the caller if needed.

    writer->state = CSV_WRITER_STATE_FINISHED;
    return TEXT_CSV_OK;
}

void text_csv_writer_free(text_csv_writer* writer) {
    if (writer) {
        // Sink is not owned by writer, so we don't free it
        free(writer);
    }
}

// ============================================================================
// Table Serialization Implementation
// ============================================================================

TEXT_API text_csv_status text_csv_write_table(
    const text_csv_sink* sink,
    const text_csv_write_options* opts,
    const text_csv_table* table
) {
    if (!sink || !sink->write || !table) {
        return TEXT_CSV_E_INVALID;
    }

    // Get default options if not provided
    text_csv_write_options default_opts = text_csv_write_options_default();
    if (!opts) {
        opts = &default_opts;
    }

    // Cast to internal structure to access fields
    // The structure definition is in csv_internal.h and matches csv_table.c
    const struct text_csv_table* table_internal = (const struct text_csv_table*)table;

    // Defensive check: verify rows array is allocated
    if (!table_internal->rows) {
        return TEXT_CSV_E_INVALID;
    }

    // Handle empty table
    if (table_internal->row_count == 0) {
        // Empty table - write nothing (or trailing newline if requested)
        if (opts->trailing_newline) {
            const char* newline = opts->newline ? opts->newline : "\n";
            size_t newline_len = strlen(newline);
            return sink->write(sink->user, newline, newline_len);
        }
        return TEXT_CSV_OK;
    }

    // Defensive check: verify row_count doesn't exceed capacity (sanity check)
    if (table_internal->row_count > table_internal->row_capacity) {
        return TEXT_CSV_E_INVALID;
    }

    // Determine start row (skip header if present)
    size_t start_row = 0;

    // Write header row if present
    if (table_internal->has_header && table_internal->row_count > 0) {
        // Bounds check: ensure row 0 is within allocated capacity
        if (table_internal->row_capacity == 0) {
            return TEXT_CSV_E_INVALID;
        }

        const csv_table_row* header_row = &table_internal->rows[0];

        // Defensive check: verify fields array is allocated
        if (!header_row->fields && header_row->field_count > 0) {
            return TEXT_CSV_E_INVALID;
        }

        // Write all fields in header row
        for (size_t col = 0; col < header_row->field_count; col++) {
            const csv_table_field* field = &header_row->fields[col];

            // Defensive check: if field has length > 0, data must not be NULL
            if (field->length > 0 && !field->data) {
                return TEXT_CSV_E_INVALID;
            }

            // Insert delimiter before field if not first field
            if (col > 0) {
                char delimiter = opts->dialect.delimiter;
                text_csv_status status = sink->write(sink->user, &delimiter, 1);
                if (status != TEXT_CSV_OK) {
                    return status;
                }
            }

            // Write field with proper quoting and escaping
            text_csv_status status = csv_write_field(
                sink,
                field->data,
                field->length,
                opts
            );
            if (status != TEXT_CSV_OK) {
                return status;
            }
        }

        // Write newline after header row
        const char* newline = opts->newline ? opts->newline : "\n";
        size_t newline_len = strlen(newline);
        text_csv_status status = sink->write(sink->user, newline, newline_len);
        if (status != TEXT_CSV_OK) {
            return status;
        }

        // Data rows start at index 1
        start_row = 1;
    }

    // Write all data rows
    // Bounds check: ensure start_row is valid
    if (start_row > table_internal->row_count) {
        return TEXT_CSV_E_INVALID;
    }

    for (size_t row = start_row; row < table_internal->row_count; row++) {
        // Bounds check: ensure row index is within allocated array
        if (row >= table_internal->row_capacity) {
            return TEXT_CSV_E_INVALID;
        }

        const csv_table_row* table_row = &table_internal->rows[row];

        // Defensive check: verify fields array is allocated
        if (!table_row->fields && table_row->field_count > 0) {
            return TEXT_CSV_E_INVALID;
        }

        // Write all fields in row
        for (size_t col = 0; col < table_row->field_count; col++) {
            const csv_table_field* field = &table_row->fields[col];

            // Defensive check: if field has length > 0, data must not be NULL
            if (field->length > 0 && !field->data) {
                return TEXT_CSV_E_INVALID;
            }

            // Insert delimiter before field if not first field
            if (col > 0) {
                char delimiter = opts->dialect.delimiter;
                text_csv_status status = sink->write(sink->user, &delimiter, 1);
                if (status != TEXT_CSV_OK) {
                    return status;
                }
            }

            // Write field with proper quoting and escaping
            text_csv_status status = csv_write_field(
                sink,
                field->data,
                field->length,
                opts
            );
            if (status != TEXT_CSV_OK) {
                return status;
            }
        }

        // Write newline after row (except possibly last row if trailing_newline is false)
        // For CSV, we typically write newline after each row
        const char* newline = opts->newline ? opts->newline : "\n";
        size_t newline_len = strlen(newline);
        text_csv_status status = sink->write(sink->user, newline, newline_len);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    // trailing_newline option is typically handled per-row above,
    // but if the table is empty and trailing_newline is true, we already handled it.
    // For non-empty tables, we've written newlines after each row, which is standard CSV.

    return TEXT_CSV_OK;
}
