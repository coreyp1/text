/**
 * @file csv_stream_internal.h
 * @brief Internal definitions for CSV streaming parser
 *
 * This header contains internal-only definitions used by the CSV streaming parser
 * implementation. It should not be included by external code.
 */

#ifndef GHOTI_IO_TEXT_CSV_STREAM_INTERNAL_H
#define GHOTI_IO_TEXT_CSV_STREAM_INTERNAL_H

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct text_csv_stream text_csv_stream;

/**
 * @brief Parser state enumeration
 *
 * Represents the current state of the CSV streaming parser state machine.
 * The parser transitions between these states as it processes input.
 */
typedef enum {
    CSV_STREAM_STATE_START_OF_RECORD,    ///< At the beginning of a new record
    CSV_STREAM_STATE_START_OF_FIELD,     ///< At the beginning of a new field
    CSV_STREAM_STATE_UNQUOTED_FIELD,     ///< Processing an unquoted field
    CSV_STREAM_STATE_QUOTED_FIELD,       ///< Processing a quoted field
    CSV_STREAM_STATE_QUOTE_IN_QUOTED,    ///< Encountered a quote character inside a quoted field
    CSV_STREAM_STATE_ESCAPE_IN_QUOTED,   ///< Processing an escape sequence inside a quoted field
    CSV_STREAM_STATE_COMMENT,            ///< Processing a comment line
    CSV_STREAM_STATE_END                 ///< Parsing has ended (error or completion)
} csv_stream_state;

/**
 * @brief Initial size for field buffer allocation
 */
#define CSV_FIELD_BUFFER_INITIAL_SIZE 64

/**
 * @brief Minimum size for field buffer
 */
#define CSV_FIELD_BUFFER_MIN_SIZE 1

/**
 * @brief Multiplier for buffer growth
 */
#define CSV_BUFFER_GROWTH_MULTIPLIER 2

/**
 * @brief Threshold for hybrid growth strategy (1KB)
 *
 * Buffers smaller than this use exponential growth, larger buffers
 * use linear growth to avoid excessive memory usage.
 */
#define CSV_BUFFER_SMALL_THRESHOLD 1024

/**
 * @brief Field buffer abstraction structure
 *
 * Unified structure for managing field data during parsing. Supports both
 * in-situ mode (direct references to input buffer) and buffered mode
 * (for fields spanning chunk boundaries or requiring unescaping).
 */
typedef struct csv_field_buffer {
    // Data source
    const char* data;              ///< Points to input buffer or allocated buffer
    size_t length;                 ///< Current field length in bytes
    bool is_quoted;                ///< Whether field is quoted
    bool needs_unescape;           ///< Whether field needs unescaping

    // Buffer management
    char* buffer;                  ///< Allocated buffer (NULL if using in-situ mode)
    size_t buffer_size;            ///< Allocated buffer size in bytes
    size_t buffer_used;            ///< Used buffer size in bytes
    bool is_buffered;              ///< Whether data is in allocated buffer

    // In-situ mode tracking
    const char* original_input;    ///< Original input buffer (for in-situ mode validation)
    size_t original_input_len;     ///< Original input buffer length

    // Field start tracking (for chunk boundary handling)
    size_t start_offset;           ///< Offset in current chunk where field started (SIZE_MAX if field already buffered)
} csv_field_buffer;

/**
 * @brief Streaming parser structure (internal)
 *
 * Internal structure for the CSV streaming parser. Contains all state
 * needed for incremental parsing including state machine state, buffers,
 * position tracking, and limits.
 */
struct text_csv_stream {
    // Configuration
    text_csv_parse_options opts;    ///< Parse options and dialect configuration
    text_csv_event_cb callback;     ///< Event callback function
    void* user_data;                ///< User context passed to callback

    // State machine
    csv_stream_state state;         ///< Current parser state
    bool in_record;                 ///< Whether currently processing a record
    size_t field_count;             ///< Number of fields in current record
    size_t row_count;               ///< Number of records processed

    // Input buffering (for fields spanning chunks)
    char* input_buffer;             ///< Buffer for input data (when field spans chunks)
    size_t input_buffer_size;       ///< Allocated size of input buffer
    size_t input_buffer_used;       ///< Used size of input buffer
    size_t input_buffer_processed;  ///< Number of bytes processed from buffer
    size_t buffer_start_offset;     ///< Start offset of buffered data

    // Position tracking
    csv_position pos;               ///< Current parsing position (line, column, offset)
    size_t total_bytes_consumed;    ///< Total bytes consumed across all chunks

    // Field accumulation (unified buffer management)
    csv_field_buffer field;         ///< Unified field buffer structure
    bool just_processed_doubled_quote; ///< Whether we just processed a doubled quote (allows delimiter to end field)
    bool quote_in_quoted_at_chunk_boundary; ///< Whether we transitioned to QUOTE_IN_QUOTED at end of previous chunk

    // Limits
    size_t max_rows;                ///< Maximum number of rows allowed
    size_t max_cols;                ///< Maximum number of columns per row
    size_t max_field_bytes;         ///< Maximum field size in bytes
    size_t max_record_bytes;        ///< Maximum record size in bytes
    size_t max_total_bytes;         ///< Maximum total input size
    size_t current_record_bytes;    ///< Current record size in bytes

    // Comment handling
    bool in_comment;                ///< Whether currently processing a comment
    size_t comment_prefix_len;      ///< Length of comment prefix string

    // In-situ mode tracking (for table parsing)
    const char* original_input_buffer;  ///< Original input buffer (caller-owned, for in-situ mode)
    size_t original_input_buffer_len;   ///< Length of original input buffer

    // Error state
    text_csv_error error;           ///< Current error state (if any)
};

/**
 * @brief Get limit value with default fallback
 *
 * Returns the configured limit if non-zero, otherwise returns the default value.
 *
 * @param configured Configured limit value (0 means use default)
 * @param default_val Default limit value to use if configured is 0
 * @return Effective limit value
 */
static inline size_t csv_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Field buffer functions (in csv_stream_buffer.c)

/**
 * @brief Initialize a field buffer structure
 *
 * Initializes all fields to safe defaults. The buffer must be initialized
 * before use.
 *
 * @param fb Field buffer to initialize (must not be NULL)
 */
void csv_field_buffer_init(csv_field_buffer* fb);

/**
 * @brief Clear a field buffer structure
 *
 * Frees any allocated buffer and resets all fields to initial state.
 * The buffer can be reused after clearing.
 *
 * @param fb Field buffer to clear (must not be NULL)
 */
void csv_field_buffer_clear(csv_field_buffer* fb);

/**
 * @brief Set field buffer from input data
 *
 * Sets the field buffer to reference input data directly (in-situ mode).
 * The input data must remain valid for the lifetime of the field buffer.
 *
 * @param fb Field buffer to set (must not be NULL)
 * @param input_data Input data to reference (must not be NULL)
 * @param input_len Length of input data
 * @param is_quoted Whether the field is quoted
 * @param start_offset Offset in chunk where field started
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_field_buffer_set_from_input(
    csv_field_buffer* fb,
    const char* input_data,
    size_t input_len,
    bool is_quoted,
    size_t start_offset
);

/**
 * @brief Grow field buffer to accommodate needed size
 *
 * Allocates or reallocates the field buffer to at least the needed size.
 * Uses hybrid growth strategy (exponential for small buffers, linear for large).
 *
 * @param fb Field buffer to grow (must not be NULL)
 * @param needed Minimum size needed in bytes
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_field_buffer_grow(csv_field_buffer* fb, size_t needed);

/**
 * @brief Append data to field buffer
 *
 * Appends data to the field buffer, growing it if necessary.
 * The buffer must be in buffered mode (is_buffered == true).
 *
 * @param fb Field buffer to append to (must not be NULL)
 * @param data Data to append (must not be NULL)
 * @param len Length of data to append
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_field_buffer_append(
    csv_field_buffer* fb,
    const char* data,
    size_t len
);

/**
 * @brief Check if field buffer can use in-situ mode
 *
 * Determines if the field buffer can reference input data directly
 * without copying. This is possible when the field doesn't need
 * unescaping and is within the original input buffer.
 *
 * @param fb Field buffer to check (must not be NULL)
 * @return true if in-situ mode is possible, false otherwise
 */
bool csv_field_buffer_can_use_in_situ(csv_field_buffer* fb);

/**
 * @brief Ensure field buffer is in buffered mode
 *
 * If the field buffer is currently in in-situ mode, allocates a buffer
 * and copies the data. If already buffered, does nothing.
 *
 * @param fb Field buffer to ensure buffered (must not be NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_field_buffer_ensure_buffered(csv_field_buffer* fb);

/**
 * @brief Set original input buffer for in-situ mode validation
 *
 * Sets the original input buffer reference for validating in-situ mode.
 * This is used to ensure field data references are within the original buffer.
 *
 * @param fb Field buffer to set input on (must not be NULL)
 * @param original_input Original input buffer (caller-owned, must remain valid)
 * @param original_input_len Length of original input buffer
 */
void csv_field_buffer_set_original_input(
    csv_field_buffer* fb,
    const char* original_input,
    size_t original_input_len
);

// Buffer management functions (in csv_stream_buffer.c)

/**
 * @brief Buffer a field that spans a chunk boundary
 *
 * When a field starts in one chunk and continues into the next, this function
 * buffers the partial field data from the current chunk so it can be continued
 * in the next chunk.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed
 * @param process_len Length of input data
 * @param field_start_offset Offset where field started in current chunk
 * @param current_offset Current processing offset
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_buffer_field_at_chunk_boundary(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t field_start_offset,
    size_t current_offset
);

/**
 * @brief Buffer unquoted field if it needs buffering
 *
 * Checks if an unquoted field needs to be buffered (e.g., for unescaping)
 * and buffers it if necessary.
 *
 * @param stream Stream parser (must not be NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_buffer_unquoted_field_if_needed(text_csv_stream* stream);

/**
 * @brief Ensure field is buffered for chunk boundary handling
 *
 * Ensures the current field is in buffered mode, allocating a buffer
 * and copying data if necessary. Used when a field may span chunk boundaries.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed
 * @param process_len Length of input data
 * @param current_offset Current processing offset
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_ensure_field_buffered(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t current_offset
);

/**
 * @brief Grow the stream's field buffer
 *
 * Grows the field buffer to accommodate at least the needed size.
 * Uses hybrid growth strategy.
 *
 * @param stream Stream parser (must not be NULL)
 * @param needed Minimum size needed in bytes
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_grow_field_buffer(text_csv_stream* stream, size_t needed);

/**
 * @brief Append data to the stream's field buffer
 *
 * Appends data to the current field buffer, growing it if necessary.
 *
 * @param stream Stream parser (must not be NULL)
 * @param data Data to append (must not be NULL)
 * @param data_len Length of data to append
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_append_to_field_buffer(
    text_csv_stream* stream,
    const char* data,
    size_t data_len
);

// Field processing functions (in csv_stream_field.c)

/**
 * @brief Clear field state in stream
 *
 * Resets field-related state in the stream parser, preparing for
 * the next field.
 *
 * @param stream Stream parser (must not be NULL)
 */
void csv_stream_clear_field_state(text_csv_stream* stream);

/**
 * @brief Complete processing of current field
 *
 * Finalizes the current field, unescaping if needed, emitting the field
 * event, and optionally emitting a record end event.
 *
 * @param stream Stream parser (must not be NULL)
 * @param offset Current offset (updated on success)
 * @param emit_record_end Whether to emit RECORD_END event after field
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_complete_field(
    text_csv_stream* stream,
    size_t* offset,
    bool emit_record_end
);

/**
 * @brief Complete field at delimiter
 *
 * Completes the current field when a delimiter is encountered.
 * Advances offset past the delimiter.
 *
 * @param stream Stream parser (must not be NULL)
 * @param offset Current offset (updated to after delimiter)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_complete_field_at_delimiter(
    text_csv_stream* stream,
    size_t* offset
);

/**
 * @brief Complete field at newline
 *
 * Completes the current field when a newline is encountered.
 * Advances offset past the newline and emits RECORD_END event.
 *
 * @param stream Stream parser (must not be NULL)
 * @param offset Current offset (updated to after newline)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_complete_field_at_newline(
    text_csv_stream* stream,
    size_t* offset
);

/**
 * @brief Handle field spanning chunk boundary
 *
 * Called at the end of a chunk when a field is in progress.
 * Buffers the partial field data so it can continue in the next chunk.
 *
 * @param stream Stream parser (must not be NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_handle_chunk_boundary(text_csv_stream* stream);

/**
 * @brief Emit field event
 *
 * Emits a FIELD event for the current field, unescaping if needed.
 * Optionally emits RECORD_END event after the field.
 *
 * @param stream Stream parser (must not be NULL)
 * @param emit_record_end Whether to emit RECORD_END event after field
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_emit_field(text_csv_stream* stream, bool emit_record_end);

/**
 * @brief Unescape field data
 *
 * Unescapes field data according to the dialect's escape mode.
 * Returns pointers to the unescaped data (may be same as input if no unescaping needed).
 *
 * @param stream Stream parser (must not be NULL)
 * @param input_data Input field data (must not be NULL)
 * @param input_len Length of input data
 * @param output_data Output parameter for unescaped data pointer
 * @param output_len Output parameter for unescaped data length
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unescape_field(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
);

/**
 * @brief Check if field needs unescaping
 *
 * Determines if a field requires unescaping based on the dialect
 * configuration and field content.
 *
 * @param stream Stream parser (must not be NULL)
 * @param input_data Input field data (must not be NULL)
 * @param input_len Length of input data
 * @return true if field needs unescaping, false otherwise
 */
bool csv_stream_field_needs_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len
);

/**
 * @brief Unescape field when no unescaping is needed
 *
 * Specialized unescape function for fields that don't require unescaping.
 * Simply returns pointers to the input data.
 *
 * @param stream Stream parser (must not be NULL)
 * @param input_data Input field data (must not be NULL)
 * @param input_len Length of input data
 * @param output_data Output parameter for data pointer (set to input_data)
 * @param output_len Output parameter for data length (set to input_len)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unescape_field_no_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
);

/**
 * @brief Unescape field when unescaping is needed
 *
 * Performs actual unescaping of field data according to escape mode
 * (doubled quotes or backslash escaping).
 *
 * @param stream Stream parser (must not be NULL)
 * @param input_data Input field data (must not be NULL)
 * @param input_len Length of input data
 * @param output_data Output parameter for unescaped data pointer
 * @param output_len Output parameter for unescaped data length
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unescape_field_with_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
);

/**
 * @brief Scan ahead in unquoted field for special characters
 *
 * Scans forward in an unquoted field to find special characters
 * (delimiter, newline, quote) that would end the field.
 * Used for optimized bulk processing of unquoted fields.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data to scan (must not be NULL)
 * @param process_len Length of input data
 * @param start_offset Offset to start scanning from
 * @param found_special Output parameter: true if special character found
 * @param special_char Output parameter: the special character found
 * @param special_pos Output parameter: position of special character
 * @return Number of bytes scanned before special character or end
 */
size_t csv_stream_scan_unquoted_field_ahead(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t start_offset,
    bool* found_special,
    char* special_char,
    size_t* special_pos
);

/**
 * @brief Validate field input data
 *
 * Validates field input data according to dialect rules (UTF-8 validation,
 * character restrictions, etc.).
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data to validate (must not be NULL)
 * @param process_len Length of input data
 * @param byte_pos Byte position in input for error reporting
 * @return TEXT_CSV_OK on success, error code on validation failure
 */
text_csv_status csv_stream_validate_field_input(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t byte_pos
);

/**
 * @brief Check if field can use in-situ mode
 *
 * Determines if a field can reference input data directly without copying.
 * This is possible when the field doesn't need unescaping and is within
 * the original input buffer.
 *
 * @param stream Stream parser (must not be NULL)
 * @param field_start Pointer to start of field in input
 * @param field_len Length of field in bytes
 * @return true if in-situ mode is possible, false otherwise
 */
bool csv_stream_can_use_in_situ(
    text_csv_stream* stream,
    const char* field_start,
    size_t field_len
);

// State machine functions (in csv_stream_state.c)

/**
 * @brief Process character in START_OF_RECORD state
 *
 * Handles character processing when at the start of a new record.
 * May transition to START_OF_FIELD, COMMENT, or END states.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_start_of_record(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Process character in START_OF_FIELD state
 *
 * Handles character processing when at the start of a new field.
 * May transition to UNQUOTED_FIELD, QUOTED_FIELD, or COMMENT states.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_start_of_field(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Process character in UNQUOTED_FIELD state
 *
 * Handles character processing when in an unquoted field.
 * May transition to START_OF_FIELD (at delimiter) or START_OF_RECORD (at newline).
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_unquoted_field(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Handle delimiter in unquoted field
 *
 * Processes a delimiter character encountered in an unquoted field,
 * completing the current field and transitioning to the next field.
 *
 * @param stream Stream parser (must not be NULL)
 * @param offset Current offset (updated to after delimiter)
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unquoted_handle_delimiter(
    text_csv_stream* stream,
    size_t* offset
);

/**
 * @brief Handle newline in unquoted field
 *
 * Processes a newline character encountered in an unquoted field,
 * completing the current field and record, then transitioning to next record.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated to after newline)
 * @param byte_pos Byte position for error reporting
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unquoted_handle_newline(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos
);

/**
 * @brief Validate character in unquoted field
 *
 * Validates that a character is allowed in an unquoted field
 * according to dialect rules.
 *
 * @param stream Stream parser (must not be NULL)
 * @param c Character to validate
 * @return TEXT_CSV_OK if valid, error code if invalid
 */
text_csv_status csv_stream_unquoted_validate_char(
    text_csv_stream* stream,
    char c
);

/**
 * @brief Handle special character in unquoted field
 *
 * Processes a special character (delimiter, newline, quote) found
 * during bulk scanning of an unquoted field.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param special_pos Position of special character
 * @param special_char The special character found
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unquoted_handle_special_char(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t special_pos,
    char special_char
);

/**
 * @brief Process bulk data in unquoted field
 *
 * Optimized bulk processing of unquoted field data, scanning ahead
 * for special characters and processing in larger chunks.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_unquoted_process_bulk(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos
);

/**
 * @brief Process character in QUOTED_FIELD state
 *
 * Handles character processing when in a quoted field.
 * May transition to QUOTE_IN_QUOTED or ESCAPE_IN_QUOTED states.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_quoted_field(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Process character in QUOTE_IN_QUOTED state
 *
 * Handles character processing when a quote character is encountered
 * inside a quoted field. May represent an escaped quote or end of field.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_quote_in_quoted(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Process character in ESCAPE_IN_QUOTED state
 *
 * Handles character processing when an escape character is encountered
 * inside a quoted field (for backslash-escape mode).
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_escape_in_quoted(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Process character in COMMENT state
 *
 * Handles character processing when in a comment line.
 * Consumes characters until newline is encountered.
 *
 * @param stream Stream parser (must not be NULL)
 * @param process_input Input data being processed (must not be NULL)
 * @param process_len Length of input data
 * @param offset Current offset (updated as processing advances)
 * @param byte_pos Byte position for error reporting
 * @param c Current character being processed
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_comment(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);

/**
 * @brief Process a chunk of input data
 *
 * Main entry point for processing a chunk of CSV input data.
 * Advances the state machine through the input, emitting events as fields
 * and records are completed.
 *
 * @param stream Stream parser (must not be NULL)
 * @param input Input data chunk (must not be NULL)
 * @param input_len Length of input data
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_process_chunk(
    text_csv_stream* stream,
    const char* input,
    size_t input_len
);

/**
 * @brief Handle newline character
 *
 * Detects and processes newline characters according to dialect settings.
 * Supports LF, CRLF, and optionally CR.
 *
 * @param stream Stream parser (must not be NULL)
 * @param input Input data (must not be NULL)
 * @param input_len Length of input data
 * @param offset Current offset (updated to after newline)
 * @param byte_pos Byte position for error reporting
 * @param nl_out Output parameter for detected newline type
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_handle_newline(
    text_csv_stream* stream,
    const char* input,
    size_t input_len,
    size_t* offset,
    size_t byte_pos,
    csv_newline_type* nl_out
);

/**
 * @brief Advance position tracking
 *
 * Updates the stream's position tracking (line, column, offset) after
 * processing a number of bytes.
 *
 * @param stream Stream parser (must not be NULL)
 * @param offset Current offset (updated by bytes)
 * @param bytes Number of bytes to advance
 * @return TEXT_CSV_OK on success, error code on failure
 */
text_csv_status csv_stream_advance_position(text_csv_stream* stream, size_t* offset, size_t bytes);

/**
 * @brief Check if position is start of comment
 *
 * Determines if the current position in the input is the start of a
 * comment line (matches comment prefix).
 *
 * @param stream Stream parser (must not be NULL)
 * @param input Input data (must not be NULL)
 * @param input_len Length of input data
 * @param offset Offset to check
 * @return true if comment start detected, false otherwise
 */
bool csv_stream_is_comment_start(
    text_csv_stream* stream,
    const char* input,
    size_t input_len,
    size_t offset
);

// Event and error functions (in csv_stream.c)

/**
 * @brief Emit an event to the callback
 *
 * Creates an event structure and calls the stream's callback function.
 * Used internally to emit FIELD, RECORD_BEGIN, RECORD_END, and END events.
 *
 * @param stream Stream parser (must not be NULL)
 * @param type Event type to emit
 * @param data Event data (for FIELD events, NULL otherwise)
 * @param data_len Event data length (for FIELD events, 0 otherwise)
 * @return TEXT_CSV_OK on success, error code if callback returns error
 */
text_csv_status csv_stream_emit_event(
    text_csv_stream* stream,
    text_csv_event_type type,
    const char* data,
    size_t data_len
);

/**
 * @brief Set error state in stream
 *
 * Sets the error state in the stream parser, including error code, message,
 * position information, and optionally generates a context snippet for
 * enhanced error reporting.
 *
 * @param stream Stream parser (must not be NULL)
 * @param code Error code
 * @param message Error message (static string, must remain valid)
 * @return The error code (same as code parameter)
 */
text_csv_status csv_stream_set_error(
    text_csv_stream* stream,
    text_csv_status code,
    const char* message
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_STREAM_INTERNAL_H */
