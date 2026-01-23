/**
 * @file csv_parser.c
 * @brief Core CSV parser state machine (tokenizer)
 *
 * Implements the CSV parser state machine for handling:
 * - Start-of-record, start-of-field
 * - Unquoted field accumulation
 * - Quoted field accumulation (with delimiter/newline allowed)
 * - Quote escaping (doubled-quote) and optional backslash-escape mode
 * - Record termination via newline rules
 * - Comment line handling (dialect opt-in)
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

// CSV parser state machine states
typedef enum {
    CSV_STATE_START_OF_RECORD,    ///< Start of a new record
    CSV_STATE_START_OF_FIELD,     ///< Start of a new field
    CSV_STATE_UNQUOTED_FIELD,     ///< Accumulating unquoted field
    CSV_STATE_QUOTED_FIELD,        ///< Accumulating quoted field
    CSV_STATE_QUOTE_IN_QUOTED,     ///< Quote character encountered in quoted field (may be escape)
    CSV_STATE_ESCAPE_IN_QUOTED,    ///< Backslash encountered in quoted field (backslash-escape mode)
    CSV_STATE_COMMENT,             ///< Processing comment line
    CSV_STATE_END                  ///< Parsing complete
} csv_parser_state;

// Field data structure for accumulating field content
typedef struct {
    const char* start;             ///< Start of field data (pointer into input or buffer)
    size_t length;                 ///< Length of field data
    bool is_quoted;                ///< Whether field was quoted
    bool needs_copy;               ///< Whether field needs to be copied (for escaping/unescaping)
} csv_field_data;

// CSV parser structure (internal)
typedef struct {
    // Configuration
    const text_csv_dialect* dialect;
    const text_csv_parse_options* opts;

    // Input tracking
    const char* input;             ///< Current input buffer
    size_t input_len;              ///< Length of current input buffer
    size_t input_offset;           ///< Offset into current input buffer
    size_t total_bytes_consumed;   ///< Total bytes consumed across all feeds

    // Position tracking
    csv_position pos;               ///< Current position (byte offset, line, column)

    // State machine
    csv_parser_state state;         ///< Current parser state
    bool in_record;                 ///< Whether we're currently in a record
    size_t field_count;             ///< Number of fields in current record

    // Field accumulation
    csv_field_data current_field;   ///< Current field being accumulated
    char* field_buffer;             ///< Buffer for field data (when escaping/unescaping needed)
    size_t field_buffer_size;       ///< Allocated size of field buffer
    size_t field_buffer_used;       ///< Used size of field buffer

    // Limits tracking
    size_t row_count;               ///< Number of rows processed
    size_t max_rows;                ///< Effective max rows limit
    size_t max_cols;                ///< Effective max cols limit
    size_t max_field_bytes;          ///< Effective max field bytes limit
    size_t max_record_bytes;         ///< Effective max record bytes limit
    size_t max_total_bytes;          ///< Effective max total bytes limit
    size_t current_record_bytes;     ///< Bytes in current record

    // Error reporting
    text_csv_error* error_out;      ///< Error output structure (if provided)

    // Comment handling
    bool in_comment;                 ///< Whether we're currently in a comment line
    size_t comment_prefix_len;       ///< Length of comment prefix
} csv_parser;

// Get effective limit value (use default if 0)
static size_t csv_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Initialize parser with options
static text_csv_status csv_parser_init(
    csv_parser* parser,
    const text_csv_parse_options* opts,
    text_csv_error* err
) {
    if (!parser || !opts) {
        if (err) {
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_INVALID,
                                        .message = "Parser or options must not be NULL",
                                        .line = 1,
                                        .column = 1
                                    };
        }
        return TEXT_CSV_E_INVALID;
    }

    memset(parser, 0, sizeof(csv_parser));

    parser->dialect = &opts->dialect;
    parser->opts = opts;
    parser->state = CSV_STATE_START_OF_RECORD;
    parser->in_record = false;
    parser->field_count = 0;
    parser->row_count = 0;
    parser->pos.offset = 0;
    parser->pos.line = 1;
    parser->pos.column = 1;
    parser->error_out = err;

    // Set effective limits
    parser->max_rows = csv_get_limit(opts->max_rows, CSV_DEFAULT_MAX_ROWS);
    parser->max_cols = csv_get_limit(opts->max_cols, CSV_DEFAULT_MAX_COLS);
    parser->max_field_bytes = csv_get_limit(opts->max_field_bytes, CSV_DEFAULT_MAX_FIELD_BYTES);
    parser->max_record_bytes = csv_get_limit(opts->max_record_bytes, CSV_DEFAULT_MAX_RECORD_BYTES);
    parser->max_total_bytes = csv_get_limit(opts->max_total_bytes, CSV_DEFAULT_MAX_TOTAL_BYTES);

    // Comment prefix length
    if (opts->dialect.allow_comments && opts->dialect.comment_prefix) {
        parser->comment_prefix_len = strlen(opts->dialect.comment_prefix);
    } else {
        parser->comment_prefix_len = 0;
    }

    return TEXT_CSV_OK;
}

// Set parser error
static text_csv_status csv_parser_set_error(
    csv_parser* parser,
    text_csv_status code,
    const char* message
) {
    if (parser->error_out) {
        // Free any existing context snippet
        if (parser->error_out->context_snippet) {
            free(parser->error_out->context_snippet);
            parser->error_out->context_snippet = NULL;
        }

        parser->error_out->code = code;
        parser->error_out->message = message;
        parser->error_out->byte_offset = parser->pos.offset;
        parser->error_out->line = parser->pos.line;
        parser->error_out->column = parser->pos.column;
        parser->error_out->row_index = parser->row_count;
        parser->error_out->col_index = parser->field_count;
        parser->error_out->context_snippet = NULL;
        parser->error_out->context_snippet_len = 0;
        parser->error_out->caret_offset = 0;

        // Generate context snippet if we have input buffer access
        if (parser->input && parser->input_len > 0) {
            char* snippet = NULL;
            size_t snippet_len = 0;
            size_t caret_offset = 0;

            // Use error offset relative to the current input buffer
            // For table parsing, this should be the full input, so offset should be accurate
            size_t error_offset = parser->error_out->byte_offset;

            text_csv_status snippet_status = csv_error_generate_context_snippet(
                parser->input,
                parser->input_len,
                error_offset,
                CSV_DEFAULT_CONTEXT_RADIUS_BYTES,
                CSV_DEFAULT_CONTEXT_RADIUS_BYTES,
                &snippet,
                &snippet_len,
                &caret_offset
            );

            if (snippet_status == TEXT_CSV_OK && snippet) {
                parser->error_out->context_snippet = snippet;
                parser->error_out->context_snippet_len = snippet_len;
                parser->error_out->caret_offset = caret_offset;
            }
        }
    }
    parser->state = CSV_STATE_END;
    return code;
}

// Grow field buffer if needed
static text_csv_status csv_parser_grow_field_buffer(
    csv_parser* parser,
    size_t needed
) {
    if (parser->field_buffer_size >= needed) {
        return TEXT_CSV_OK;
    }

    // Grow by at least 2x, or to needed size
    size_t new_size = parser->field_buffer_size * 2;
    if (new_size < needed) {
        new_size = needed;
    }

    // Check for overflow
    if (new_size < parser->field_buffer_size) {
        return csv_parser_set_error(parser, TEXT_CSV_E_OOM, "Field buffer size overflow");
    }

    char* new_buffer = realloc(parser->field_buffer, new_size);
    if (!new_buffer) {
        return csv_parser_set_error(parser, TEXT_CSV_E_OOM, "Failed to allocate field buffer");
    }

    parser->field_buffer = new_buffer;
    parser->field_buffer_size = new_size;
    return TEXT_CSV_OK;
}

// Append character to field buffer
static text_csv_status csv_parser_append_to_field(
    csv_parser* parser,
    char c
) {
    if (parser->field_buffer_used >= parser->field_buffer_size) {
        text_csv_status status = csv_parser_grow_field_buffer(parser, parser->field_buffer_used + 1);
        if (status != TEXT_CSV_OK) {
            return status;
        }
    }

    parser->field_buffer[parser->field_buffer_used++] = c;
    return TEXT_CSV_OK;
}

// Check if we're at start of comment line
static bool csv_parser_is_comment_start(
    csv_parser* parser,
    const char* input,
    size_t input_len,
    size_t offset
) {
    if (!parser->dialect->allow_comments || parser->comment_prefix_len == 0) {
        return false;
    }

    // Must be at start of record (no fields yet)
    if (parser->field_count > 0 || parser->in_record) {
        return false;
    }

    // Check if we have enough bytes for comment prefix
    if (offset + parser->comment_prefix_len > input_len) {
        return false;
    }

    // Check if prefix matches
    return memcmp(input + offset, parser->dialect->comment_prefix, parser->comment_prefix_len) == 0;
}

// Process a single byte in the parser (core state machine logic)
static text_csv_status csv_parser_process_byte(
    csv_parser* parser,
    const char* input,
    size_t input_len,
    size_t* offset
) {
    if (*offset >= input_len) {
        return TEXT_CSV_OK;  // End of input chunk
    }

    char c = input[*offset];
    size_t byte_pos = *offset;

    // Check total bytes limit (before processing)
    if (parser->total_bytes_consumed >= parser->max_total_bytes) {
        return csv_parser_set_error(parser, TEXT_CSV_E_LIMIT, "Maximum total bytes exceeded");
    }

    // Check record bytes limit
    if (parser->in_record) {
        parser->current_record_bytes++;
        if (parser->current_record_bytes > parser->max_record_bytes) {
            return csv_parser_set_error(parser, TEXT_CSV_E_LIMIT, "Maximum record bytes exceeded");
        }
    }

    switch (parser->state) {
        case CSV_STATE_START_OF_RECORD: {
            // Check for comment line
            if (csv_parser_is_comment_start(parser, input, input_len, byte_pos)) {
                parser->state = CSV_STATE_COMMENT;
                parser->in_comment = true;
                (*offset)++;
                return TEXT_CSV_OK;
            }

            // Start of field
            parser->state = CSV_STATE_START_OF_FIELD;
            parser->in_record = true;
            parser->current_record_bytes = 0;
            // Fall through to START_OF_FIELD
        }
        // Fall through
        case CSV_STATE_START_OF_FIELD: {
            // Check for field count limit
            if (parser->field_count >= parser->max_cols) {
                return csv_parser_set_error(parser, TEXT_CSV_E_TOO_MANY_COLS, "Too many columns in record");
            }

            // Initialize field
            parser->current_field.start = input + byte_pos;
            parser->current_field.length = 0;
            parser->current_field.is_quoted = false;
            parser->current_field.needs_copy = false;
            parser->field_buffer_used = 0;

            // Check for quote character
            if (c == parser->dialect->quote) {
                parser->state = CSV_STATE_QUOTED_FIELD;
                parser->current_field.is_quoted = true;
                parser->current_field.start = input + byte_pos + 1;  // Skip opening quote
                (*offset)++;
                return TEXT_CSV_OK;
            }

            // Check for delimiter (empty field)
            if (c == parser->dialect->delimiter) {
                // Empty unquoted field
                parser->current_field.length = 0;
                parser->field_count++;
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;
                (*offset)++;
                return TEXT_CSV_OK;  // Field complete, will be handled by caller
            }

            // Check for newline (empty field, end of record)
            csv_position pos_before = parser->pos;
            text_csv_status detect_error = TEXT_CSV_OK;
            csv_newline_type nl = csv_detect_newline(input, input_len, &pos_before, parser->dialect, &detect_error);
            if (detect_error != TEXT_CSV_OK) {
                return csv_parser_set_error(parser, detect_error, "Overflow in newline detection");
            }
            if (nl != CSV_NEWLINE_NONE) {
                // Empty field, end of record
                parser->current_field.length = 0;
                parser->field_count++;
                parser->state = CSV_STATE_START_OF_RECORD;
                parser->in_record = false;
                parser->row_count++;
                parser->field_count = 0;
                parser->pos = pos_before;  // Position updated by csv_detect_newline
                parser->total_bytes_consumed += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                (*offset) += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                return TEXT_CSV_OK;  // Record complete, will be handled by caller
            }

            // Update position for single character
            parser->pos.offset++;
            parser->pos.column++;
            parser->total_bytes_consumed++;

            // Start unquoted field
            parser->state = CSV_STATE_UNQUOTED_FIELD;
            parser->current_field.start = input + byte_pos;
            parser->current_field.length = 1;
            parser->pos.offset++;
            parser->pos.column++;
            parser->total_bytes_consumed++;
            (*offset)++;
            return TEXT_CSV_OK;
        }

        case CSV_STATE_UNQUOTED_FIELD: {
            // Check field size limit
            if (parser->current_field.length >= parser->max_field_bytes) {
                return csv_parser_set_error(parser, TEXT_CSV_E_LIMIT, "Maximum field bytes exceeded");
            }

            // Check for quote in unquoted field
            if (c == parser->dialect->quote) {
                if (!parser->dialect->allow_unquoted_quotes) {
                    return csv_parser_set_error(parser, TEXT_CSV_E_UNEXPECTED_QUOTE, "Unexpected quote in unquoted field");
                }
                // Allow quote, continue accumulating
            }

            // Check for delimiter
            if (c == parser->dialect->delimiter) {
                // Field complete
                parser->field_count++;
                parser->state = CSV_STATE_START_OF_FIELD;
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;
                (*offset)++;
                return TEXT_CSV_OK;  // Field complete
            }

            // Check for newline
            text_csv_status detect_error = TEXT_CSV_OK;
            csv_newline_type nl = csv_detect_newline(input, input_len, &parser->pos, parser->dialect, &detect_error);
            if (detect_error != TEXT_CSV_OK) {
                return csv_parser_set_error(parser, detect_error, "Overflow in newline detection");
            }
            if (nl != CSV_NEWLINE_NONE) {
                // Field complete, end of record
                parser->field_count++;
                parser->state = CSV_STATE_START_OF_RECORD;
                parser->in_record = false;
                parser->row_count++;
                parser->field_count = 0;
                (*offset) += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                return TEXT_CSV_OK;  // Record complete
            }

            // Check for newline in unquoted field
            if (c == '\n' || c == '\r') {
                if (!parser->dialect->allow_unquoted_newlines) {
                    return csv_parser_set_error(parser, TEXT_CSV_E_INVALID, "Newline in unquoted field");
                }
            }

            // Accumulate character (position already updated above)
            parser->current_field.length++;
            (*offset)++;
            return TEXT_CSV_OK;
        }

        case CSV_STATE_QUOTED_FIELD: {
            // Check field size limit
            if (parser->current_field.length >= parser->max_field_bytes) {
                return csv_parser_set_error(parser, TEXT_CSV_E_LIMIT, "Maximum field bytes exceeded");
            }

            // Check for escape character (backslash mode)
            if (parser->dialect->escape == TEXT_CSV_ESCAPE_BACKSLASH && c == '\\') {
                parser->state = CSV_STATE_ESCAPE_IN_QUOTED;
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;
                (*offset)++;
                return TEXT_CSV_OK;
            }

            // Check for quote character
            if (c == parser->dialect->quote) {
                parser->state = CSV_STATE_QUOTE_IN_QUOTED;
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;
                (*offset)++;
                return TEXT_CSV_OK;
            }

            // Accumulate character (including newlines if allowed)
            if (c == '\n' || c == '\r') {
                if (!parser->dialect->newline_in_quotes) {
                    return csv_parser_set_error(parser, TEXT_CSV_E_INVALID, "Newline in quoted field not allowed");
                }
                // Check for newline sequence
                csv_position pos_before = parser->pos;
                text_csv_status detect_error = TEXT_CSV_OK;
                csv_newline_type nl = csv_detect_newline(input, input_len, &pos_before, parser->dialect, &detect_error);
                if (detect_error != TEXT_CSV_OK) {
                    return csv_parser_set_error(parser, detect_error, "Overflow in newline detection");
                }
                if (nl != CSV_NEWLINE_NONE) {
                    parser->pos = pos_before;
                    parser->total_bytes_consumed += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    parser->current_field.length += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    (*offset) += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    return TEXT_CSV_OK;
                }
            }

            // Update position for single character
            parser->pos.offset++;
            parser->pos.column++;
            parser->total_bytes_consumed++;
            parser->current_field.length++;
            (*offset)++;
            return TEXT_CSV_OK;
        }

        case CSV_STATE_QUOTE_IN_QUOTED: {
            // This is either an escaped quote or the end of the quoted field
            if (parser->dialect->escape == TEXT_CSV_ESCAPE_DOUBLED_QUOTE && c == parser->dialect->quote) {
                // Doubled quote - escape sequence
                parser->current_field.needs_copy = true;
                // We'll need to unescape this, so mark for copy
                parser->state = CSV_STATE_QUOTED_FIELD;
                parser->current_field.length++;  // Count both quotes as one character
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;
                (*offset)++;
                return TEXT_CSV_OK;
            } else if (c == parser->dialect->delimiter) {
                // End of quoted field
                parser->field_count++;
                parser->state = CSV_STATE_START_OF_FIELD;
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;
                (*offset)++;
                return TEXT_CSV_OK;  // Field complete
            } else {
                // Check for newline (end of record)
                csv_position pos_before = parser->pos;
                text_csv_status detect_error = TEXT_CSV_OK;
                csv_newline_type nl = csv_detect_newline(input, input_len, &pos_before, parser->dialect, &detect_error);
                if (detect_error != TEXT_CSV_OK) {
                    return csv_parser_set_error(parser, detect_error, "Overflow in newline detection");
                }
                if (nl != CSV_NEWLINE_NONE) {
                    // End of quoted field, end of record
                    parser->field_count++;
                    parser->state = CSV_STATE_START_OF_RECORD;
                    parser->in_record = false;
                    parser->row_count++;
                    parser->field_count = 0;
                    parser->pos = pos_before;  // Position updated by csv_detect_newline
                    parser->total_bytes_consumed += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    (*offset) += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                    return TEXT_CSV_OK;  // Record complete
                }

                // Update position for single character
                parser->pos.offset++;
                parser->pos.column++;
                parser->total_bytes_consumed++;

                // Invalid: quote must be followed by delimiter, newline, or another quote
                return csv_parser_set_error(parser, TEXT_CSV_E_INVALID, "Invalid quote usage in quoted field");
            }
        }

        case CSV_STATE_ESCAPE_IN_QUOTED: {
            // Process escape sequence
            switch (c) {
                case 'n':
                case 'r':
                case 't':
                case '\\':
                case '"':
                    // Valid escape sequence
                    break;
                default:
                    return csv_parser_set_error(parser, TEXT_CSV_E_INVALID_ESCAPE, "Invalid escape sequence");
            }

            parser->current_field.needs_copy = true;
            parser->state = CSV_STATE_QUOTED_FIELD;
            parser->current_field.length++;
            parser->pos.offset++;
            parser->pos.column++;
            parser->total_bytes_consumed++;
            (*offset)++;
            return TEXT_CSV_OK;
        }

        case CSV_STATE_COMMENT: {
            // Consume until newline
            csv_position pos_before = parser->pos;
            text_csv_status detect_error = TEXT_CSV_OK;
            csv_newline_type nl = csv_detect_newline(input, input_len, &pos_before, parser->dialect, &detect_error);
            if (detect_error != TEXT_CSV_OK) {
                return csv_parser_set_error(parser, detect_error, "Overflow in newline detection");
            }
            if (nl != CSV_NEWLINE_NONE) {
                // End of comment line
                parser->state = CSV_STATE_START_OF_RECORD;
                parser->in_comment = false;
                parser->row_count++;  // Comment line counts as a row
                parser->pos = pos_before;  // Position updated by csv_detect_newline
                parser->total_bytes_consumed += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                (*offset) += (nl == CSV_NEWLINE_CRLF ? 2 : 1);
                return TEXT_CSV_OK;
            }
            // Update position for single character
            parser->pos.offset++;
            parser->pos.column++;
            parser->total_bytes_consumed++;
            (*offset)++;
            return TEXT_CSV_OK;
        }

        case CSV_STATE_END:
            return TEXT_CSV_OK;  // Already ended

        default:
            return csv_parser_set_error(parser, TEXT_CSV_E_INVALID, "Invalid parser state");
    }
}

// Free parser resources
static void csv_parser_free(csv_parser* parser) {
    if (parser) {
        free(parser->field_buffer);
        parser->field_buffer = NULL;
        parser->field_buffer_size = 0;
        parser->field_buffer_used = 0;
    }
}

// The parser functions will be used by streaming and table parsers
// For now, we export the structure and key functions via internal header
// The actual public API will be in csv_stream.c and csv_table.c
