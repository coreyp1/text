/**
 * @file csv_internal.h
 * @brief Internal definitions for CSV module implementation
 *
 * This header contains internal-only definitions used by the CSV module
 * implementation. It should not be included by external code.
 */

#ifndef GHOTI_IO_TEXT_CSV_INTERNAL_H
#define GHOTI_IO_TEXT_CSV_INTERNAL_H

#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_writer.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct text_csv_stream text_csv_stream;

/**
 * @brief Default limits for CSV parsing (used when opts->max_* is 0)
 */
#define CSV_DEFAULT_MAX_ROWS (10 * 1000 * 1000)        // 10M rows
#define CSV_DEFAULT_MAX_COLS (100 * 1000)              // 100k columns
#define CSV_DEFAULT_MAX_FIELD_BYTES (16 * 1024 * 1024)  // 16MB
#define CSV_DEFAULT_MAX_RECORD_BYTES (64 * 1024 * 1024) // 64MB
#define CSV_DEFAULT_MAX_TOTAL_BYTES (1024 * 1024 * 1024) // 1GB

/**
 * @brief Default context radius for error snippets
 */
#define CSV_DEFAULT_CONTEXT_RADIUS_BYTES 40

/**
 * @brief Position tracking structure for CSV processing
 */
typedef struct {
    size_t offset;       ///< Byte offset from start
    int line;            ///< Line number (1-based)
    int column;          ///< Column number (1-based, byte-based)
} csv_position;

/**
 * @brief Newline type detected
 */
typedef enum {
    CSV_NEWLINE_NONE,     ///< No newline detected
    CSV_NEWLINE_LF,       ///< LF (\n)
    CSV_NEWLINE_CRLF,     ///< CRLF (\r\n)
    CSV_NEWLINE_CR        ///< CR (\r)
} csv_newline_type;

/**
 * @brief UTF-8 validation result
 */
typedef enum {
    CSV_UTF8_VALID,       ///< Valid UTF-8 sequence
    CSV_UTF8_INVALID,    ///< Invalid UTF-8 sequence
    CSV_UTF8_INCOMPLETE  ///< Incomplete UTF-8 sequence (needs more bytes)
} csv_utf8_result;

/**
 * @brief Detect and consume newline from input
 *
 * Detects newline type according to dialect settings and advances position.
 * Supports LF, CRLF, and optionally CR.
 *
 * @param input Input buffer
 * @param input_len Length of input buffer
 * @param pos Current position (updated on success)
 * @param dialect Dialect configuration
 * @return CSV_NEWLINE_* type if newline detected, CSV_NEWLINE_NONE otherwise
 */
TEXT_INTERNAL_API csv_newline_type csv_detect_newline(
    const char* input,
    size_t input_len,
    csv_position* pos,
    const text_csv_dialect* dialect
);

/**
 * @brief Validate UTF-8 sequence
 *
 * Validates UTF-8 encoding in a byte sequence. Returns validation result
 * and optionally advances position.
 *
 * @param input Input buffer
 * @param input_len Length of input buffer
 * @param pos Current position (updated on success)
 * @param validate Whether to validate (if false, always returns VALID)
 * @return CSV_UTF8_VALID, CSV_UTF8_INVALID, or CSV_UTF8_INCOMPLETE
 */
TEXT_INTERNAL_API csv_utf8_result csv_validate_utf8(
    const char* input,
    size_t input_len,
    csv_position* pos,
    bool validate
);

/**
 * @brief Strip UTF-8 BOM from input
 *
 * Checks if input starts with UTF-8 BOM (0xEF 0xBB 0xBF) and strips it if present.
 * Updates position accordingly.
 *
 * @param input Input buffer
 * @param input_len Length of input buffer (updated if BOM found)
 * @param pos Current position (updated if BOM found)
 * @param strip Whether to strip BOM (if false, does nothing)
 * @return true if BOM was stripped, false otherwise
 */
TEXT_INTERNAL_API bool csv_strip_bom(
    const char** input,
    size_t* input_len,
    csv_position* pos,
    bool strip
);

/**
 * @brief Forward declaration of csv_arena (defined in csv_table.c)
 */
typedef struct csv_arena csv_arena;

/**
 * @brief CSV context structure
 *
 * Holds the arena allocator and other context information
 * for a CSV table.
 */
typedef struct csv_context {
    csv_arena* arena;                ///< Arena allocator for this table
    const char* input_buffer;        ///< Original input buffer (for in-situ mode, caller-owned)
    size_t input_buffer_len;         ///< Length of input buffer (for in-situ mode)
} csv_context;

/**
 * @brief Create a new CSV context with arena
 *
 * Internal function for creating a new context with an arena allocator.
 *
 * @return New context, or NULL on failure
 */
TEXT_INTERNAL_API csv_context* csv_context_new(void);

/**
 * @brief Set input buffer for in-situ mode
 *
 * Internal function for storing a reference to the input buffer in the context.
 * The buffer is caller-owned and must remain valid for the lifetime of the table.
 *
 * @param ctx Context to set input buffer on (must not be NULL)
 * @param input_buffer Original input buffer (caller-owned, must remain valid)
 * @param input_buffer_len Length of input buffer
 */
TEXT_INTERNAL_API void csv_context_set_input_buffer(csv_context* ctx, const char* input_buffer, size_t input_buffer_len);

/**
 * @brief Free a CSV context and its arena
 *
 * Internal function for freeing a context and its associated arena.
 * Note: The input buffer (if set) is caller-owned and is NOT freed here.
 *
 * @param ctx Context to free (can be NULL)
 */
TEXT_INTERNAL_API void csv_context_free(csv_context* ctx);

/**
 * @brief Allocate memory from a context's arena
 *
 * Internal function for allocating memory from the
 * arena associated with a context.
 *
 * @param ctx Context containing the arena
 * @param size Size in bytes to allocate
 * @param align Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory, or NULL on failure
 */
TEXT_INTERNAL_API void* csv_arena_alloc_for_context(csv_context* ctx, size_t size, size_t align);

/**
 * @brief Write a field with proper quoting and escaping
 *
 * Internal function for writing a field to a sink with appropriate
 * quoting and escaping according to write options and dialect.
 * This function handles:
 * - Quote-if-needed logic (delimiter, quote char, newline present)
 * - Quote-all and quote-empty options
 * - Escape mode: doubled quote vs backslash vs none
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
);

/**
 * @brief Set original input buffer for in-situ mode
 *
 * Internal function for setting the original input buffer in a stream.
 * This is used by table parsing to enable in-situ mode field references.
 *
 * @param stream Stream parser (must not be NULL)
 * @param input_buffer Original input buffer (caller-owned, must remain valid)
 * @param input_buffer_len Length of input buffer
 */
TEXT_INTERNAL_API void csv_stream_set_original_input_buffer(
    text_csv_stream* stream,
    const char* input_buffer,
    size_t input_buffer_len
);

/**
 * @brief Generate a context snippet around an error position
 *
 * Extracts a snippet of text around the error position for better error reporting.
 * The snippet includes context before and after the error position, with a caret
 * offset indicating the exact error location.
 *
 * @param input Input buffer containing the CSV data
 * @param input_len Length of input buffer
 * @param error_offset Byte offset of the error (0-based)
 * @param context_before Number of bytes of context before the error
 * @param context_after Number of bytes of context after the error
 * @param snippet_out Output parameter for the allocated snippet (caller must free)
 * @param snippet_len_out Output parameter for snippet length
 * @param caret_offset_out Output parameter for caret offset within snippet
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_INTERNAL_API text_csv_status csv_error_generate_context_snippet(
    const char* input,
    size_t input_len,
    size_t error_offset,
    size_t context_before,
    size_t context_after,
    char** snippet_out,
    size_t* snippet_len_out,
    size_t* caret_offset_out
);

/**
 * @brief Copy an error structure, deep-copying the context snippet
 *
 * Copies an error structure from source to destination, including a deep copy
 * of the context snippet if present. The destination's existing context snippet
 * (if any) is freed before copying.
 *
 * @param dst Destination error structure (must not be NULL)
 * @param src Source error structure (must not be NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_INTERNAL_API text_csv_status csv_error_copy(text_csv_error* dst, const text_csv_error* src);

// ============================================================================
// Table Structure Definitions (for internal use)
// ============================================================================

/**
 * @brief Field structure (stored in arena)
 */
typedef struct {
    const char* data;       ///< Field data (pointer into input or arena)
    size_t length;          ///< Field length
    bool is_in_situ;        ///< Whether field references input buffer directly
} csv_table_field;

/**
 * @brief Row structure (stored in arena)
 */
typedef struct {
    csv_table_field* fields; ///< Array of fields
    size_t field_count;      ///< Number of fields
} csv_table_row;

/**
 * @brief Header map entry (for column name lookup)
 */
typedef struct csv_header_entry {
    const char* name;        ///< Column name (in arena or input buffer)
    size_t name_len;        ///< Column name length
    size_t index;           ///< Column index
    struct csv_header_entry* next;  ///< Next entry (for hash table chaining)
} csv_header_entry;

/**
 * @brief Table structure (internal)
 *
 * This is the internal representation of text_csv_table.
 * External code should use the opaque text_csv_table type.
 * This structure definition must match the one in csv_table.c.
 */
struct text_csv_table {
    csv_context* ctx;           ///< Context with arena
    csv_table_row* rows;         ///< Array of rows
    size_t row_count;            ///< Number of rows
    size_t row_capacity;         ///< Allocated row capacity

    // Header map (optional, only if header processing enabled)
    csv_header_entry** header_map;  ///< Hash table for header lookup
    size_t header_map_size;         ///< Size of hash table
    bool has_header;                ///< Whether header was processed
};

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_INTERNAL_H */
