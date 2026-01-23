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



