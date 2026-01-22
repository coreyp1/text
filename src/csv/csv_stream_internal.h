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

// Parser state enum
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

// Constants
#define CSV_FIELD_BUFFER_INITIAL_SIZE 64
#define CSV_FIELD_BUFFER_MIN_SIZE 1
#define CSV_BUFFER_GROWTH_MULTIPLIER 2
#define CSV_BUFFER_SMALL_THRESHOLD 1024  // 1KB threshold for hybrid growth strategy

// Field buffer abstraction - unified structure for field buffer management
typedef struct csv_field_buffer {
    // Data source
    const char* data;              // Points to input buffer or buffer
    size_t length;                 // Current field length
    bool is_quoted;                // Whether field is quoted
    bool needs_unescape;           // Whether field needs unescaping

    // Buffer management
    char* buffer;                  // Allocated buffer (if needed)
    size_t buffer_size;            // Allocated buffer size
    size_t buffer_used;            // Used buffer size
    bool is_buffered;              // Whether data is in buffer

    // In-situ mode tracking
    const char* original_input;    // Original input buffer (for in-situ)
    size_t original_input_len;     // Original input length

    // Field start tracking (for chunk boundary handling)
    size_t start_offset;           // Offset in current chunk where field started (SIZE_MAX if field already buffered)
} csv_field_buffer;

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

    // Field accumulation (unified buffer management)
    csv_field_buffer field;          ///< Unified field buffer structure
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

// Utility functions
static inline size_t csv_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

// Field buffer functions (in csv_stream_buffer.c)
void csv_field_buffer_init(csv_field_buffer* fb);
void csv_field_buffer_clear(csv_field_buffer* fb);
text_csv_status csv_field_buffer_set_from_input(
    csv_field_buffer* fb,
    const char* input_data,
    size_t input_len,
    bool is_quoted,
    size_t start_offset
);
text_csv_status csv_field_buffer_grow(csv_field_buffer* fb, size_t needed);
text_csv_status csv_field_buffer_append(
    csv_field_buffer* fb,
    const char* data,
    size_t len
);
bool csv_field_buffer_can_use_in_situ(csv_field_buffer* fb);
text_csv_status csv_field_buffer_ensure_buffered(csv_field_buffer* fb);
void csv_field_buffer_set_original_input(
    csv_field_buffer* fb,
    const char* original_input,
    size_t original_input_len
);

// Buffer management functions (in csv_stream_buffer.c)
text_csv_status csv_stream_buffer_field_at_chunk_boundary(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t field_start_offset,
    size_t current_offset
);
text_csv_status csv_stream_buffer_unquoted_field_if_needed(text_csv_stream* stream);
text_csv_status csv_stream_ensure_field_buffered(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t current_offset
);
text_csv_status csv_stream_grow_field_buffer(text_csv_stream* stream, size_t needed);
text_csv_status csv_stream_append_to_field_buffer(
    text_csv_stream* stream,
    const char* data,
    size_t data_len
);

// Field processing functions (in csv_stream_field.c)
void csv_stream_clear_field_state(text_csv_stream* stream);
text_csv_status csv_stream_complete_field(
    text_csv_stream* stream,
    size_t* offset,
    bool emit_record_end
);
text_csv_status csv_stream_complete_field_at_delimiter(
    text_csv_stream* stream,
    size_t* offset
);
text_csv_status csv_stream_complete_field_at_newline(
    text_csv_stream* stream,
    size_t* offset
);
text_csv_status csv_stream_handle_chunk_boundary(text_csv_stream* stream);
text_csv_status csv_stream_emit_field(text_csv_stream* stream, bool emit_record_end);
text_csv_status csv_stream_unescape_field(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
);
bool csv_stream_field_needs_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len
);
text_csv_status csv_stream_unescape_field_no_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
);
text_csv_status csv_stream_unescape_field_with_unescape(
    text_csv_stream* stream,
    const char* input_data,
    size_t input_len,
    const char** output_data,
    size_t* output_len
);
size_t csv_stream_scan_unquoted_field_ahead(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t start_offset,
    bool* found_special,
    char* special_char,
    size_t* special_pos
);
text_csv_status csv_stream_validate_field_input(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t byte_pos
);
bool csv_stream_can_use_in_situ(
    text_csv_stream* stream,
    const char* field_start,
    size_t field_len
);

// State machine functions (in csv_stream_state.c)
text_csv_status csv_stream_process_start_of_record(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_process_start_of_field(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_process_unquoted_field(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_unquoted_handle_delimiter(
    text_csv_stream* stream,
    size_t* offset
);
text_csv_status csv_stream_unquoted_handle_newline(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos
);
text_csv_status csv_stream_unquoted_validate_char(
    text_csv_stream* stream,
    char c
);
text_csv_status csv_stream_unquoted_handle_special_char(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t special_pos,
    char special_char
);
text_csv_status csv_stream_unquoted_process_bulk(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos
);
text_csv_status csv_stream_process_quoted_field(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_process_quote_in_quoted(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_process_escape_in_quoted(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_process_comment(
    text_csv_stream* stream,
    const char* process_input,
    size_t process_len,
    size_t* offset,
    size_t byte_pos,
    char c
);
text_csv_status csv_stream_process_chunk(
    text_csv_stream* stream,
    const char* input,
    size_t input_len
);
csv_newline_type csv_stream_handle_newline(
    text_csv_stream* stream,
    const char* input,
    size_t input_len,
    size_t* offset,
    size_t byte_pos
);
void csv_stream_advance_position(text_csv_stream* stream, size_t* offset, size_t bytes);
bool csv_stream_is_comment_start(
    text_csv_stream* stream,
    const char* input,
    size_t input_len,
    size_t offset
);

// Event and error functions (in csv_stream.c)
text_csv_status csv_stream_emit_event(
    text_csv_stream* stream,
    text_csv_event_type type,
    const char* data,
    size_t data_len
);
text_csv_status csv_stream_set_error(
    text_csv_stream* stream,
    text_csv_status code,
    const char* message
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_STREAM_INTERNAL_H */
