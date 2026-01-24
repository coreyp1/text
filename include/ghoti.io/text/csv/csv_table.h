/**
 * @file csv_table.h
 * @brief Table (DOM) CSV parser API
 *
 * Provides a DOM-style parser that builds an in-memory representation
 * of CSV data.
 */

#ifndef GHOTI_IO_TEXT_CSV_TABLE_H
#define GHOTI_IO_TEXT_CSV_TABLE_H

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/csv/csv_core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque table structure
 */
typedef struct text_csv_table text_csv_table;

/**
 * @brief Parse CSV input into a table structure
 *
 * Parses the provided CSV data and builds an in-memory table representation.
 * The table uses arena allocation for efficient memory management.
 *
 * When `in_situ_mode` is enabled in parse options:
 * - Fields that don't require transformation (no unescaping, no UTF-8 validation)
 *   may reference the input buffer directly for zero-copy efficiency.
 * - The input buffer (`data`) must remain valid for the lifetime of the table.
 * - Fields that require transformation (e.g., unescaping doubled quotes) are
 *   copied to the arena and do not reference the input buffer.
 * - When `validate_utf8` is enabled, all fields are copied (in-situ mode is disabled).
 *
 * @param data Input CSV data (must remain valid for table lifetime if in_situ_mode is enabled)
 * @param len Length of input data
 * @param opts Parse options (can be NULL for defaults)
 * @param err Error output structure (can be NULL)
 * @return New table, or NULL on error
 */
TEXT_API text_csv_table* text_csv_parse_table(
    const void* data,
    size_t len,
    const text_csv_parse_options* opts,
    text_csv_error* err
);

/**
 * @brief Free a CSV table
 *
 * Frees all memory associated with the table, including all fields and rows.
 * This is an O(1) operation due to arena allocation.
 *
 * @param table Table to free (can be NULL)
 */
TEXT_API void text_csv_free_table(text_csv_table* table);

/**
 * @brief Get the number of rows in the table
 *
 * @param table Table (must not be NULL)
 * @return Number of rows (0-based count of data rows, excluding header if present)
 */
TEXT_API size_t text_csv_row_count(const text_csv_table* table);

/**
 * @brief Get the number of columns in a specific row
 *
 * @param table Table (must not be NULL)
 * @param row Row index (0-based)
 * @return Number of columns in the row, or 0 if row index is invalid
 */
TEXT_API size_t text_csv_col_count(const text_csv_table* table, size_t row);

/**
 * @brief Get a field value from the table
 *
 * Returns a pointer to the field data. The pointer is valid for the lifetime
 * of the table. If in-situ mode was used, the pointer may reference the original
 * input buffer (which must remain valid). Otherwise, the pointer references
 * arena-allocated memory.
 *
 * @param table Table (must not be NULL)
 * @param row Row index (0-based)
 * @param col Column index (0-based)
 * @param len Output parameter for field length (can be NULL)
 * @return Pointer to field data, or NULL if indices are invalid
 */
TEXT_API const char* text_csv_field(
    const text_csv_table* table,
    size_t row,
    size_t col,
    size_t* len
);

/**
 * @brief Get column index by header name
 *
 * Only works if the table was parsed with header processing enabled.
 *
 * @param table Table (must not be NULL)
 * @param name Column name to look up
 * @param out_idx Output parameter for column index (can be NULL)
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if column not found
 */
TEXT_API text_csv_status text_csv_header_index(
    const text_csv_table* table,
    const char* name,
    size_t* out_idx
);

/**
 * @brief Create an empty CSV table
 *
 * Creates a new empty table with initialized context and arena.
 * The table starts with a default row capacity of 16 rows.
 * No columns are defined until the first row is added.
 *
 * The table must be freed with text_csv_free_table() when no longer needed.
 *
 * @return New empty table, or NULL on allocation failure
 */
TEXT_API text_csv_table* text_csv_new_table(void);

/**
 * @brief Append a row to the end of the table
 *
 * Adds a new row with the specified field values to the end of the table.
 * The first row added sets the column count for the table. Subsequent rows
 * must have the same number of fields (strict validation).
 *
 * All field data is copied to the arena and does not reference external buffers.
 * If field_lengths is NULL, all fields are assumed to be null-terminated strings.
 *
 * @param table Table (must not be NULL)
 * @param fields Array of field data pointers (must not be NULL)
 * @param field_lengths Array of field lengths, or NULL if all fields are null-terminated
 * @param field_count Number of fields (must be > 0)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_row_append(
    text_csv_table* table,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count
);

/**
 * @brief Insert a row at the specified index
 *
 * Inserts a new row with the specified field values at the given index.
 * Existing rows at and after the insertion point are shifted right.
 * The index can equal row_count, which is equivalent to appending.
 *
 * The first row added sets the column count for the table. Subsequent rows
 * must have the same number of fields (strict validation).
 *
 * All field data is copied to the arena and does not reference external buffers.
 * If field_lengths is NULL, all fields are assumed to be null-terminated strings.
 *
 * @param table Table (must not be NULL)
 * @param row_idx Row index where to insert (0-based, must be <= row_count, adjusted for header if present)
 * @param fields Array of field data pointers (must not be NULL)
 * @param field_lengths Array of field lengths, or NULL if all fields are null-terminated
 * @param field_count Number of fields (must be > 0)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_row_insert(
    text_csv_table* table,
    size_t row_idx,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count
);

/**
 * @brief Remove a row at the specified index
 *
 * Removes the row at the specified index, shifting remaining rows left.
 * If the table has headers, the header row (index 0) cannot be removed
 * and this function will return an error.
 *
 * The row index is 0-based for data rows only. If the table has headers,
 * the header row is at index 0 and data rows start at index 1.
 *
 * Field data remains in the arena (no individual cleanup needed).
 *
 * @param table Table (must not be NULL)
 * @param row_idx Row index to remove (0-based, adjusted for header if present)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_row_remove(
    text_csv_table* table,
    size_t row_idx
);

/**
 * @brief Set the value of a field at specified row and column indices
 *
 * Sets the value of a field at the specified row and column indices.
 * The field data is copied to the arena and does not reference external buffers.
 * If the field was previously in-situ (referencing the input buffer), it will
 * be copied to the arena.
 *
 * If field_length is 0:
 * - If field_data is not NULL, it is assumed to be a null-terminated string
 *   and strlen() will be used to determine the length.
 * - If field_data is NULL, the field is set to empty (length 0).
 *
 * This is consistent with the writer API which allows NULL for empty fields.
 *
 * @param table Table (must not be NULL)
 * @param row Row index (0-based, adjusted for header if present)
 * @param col Column index (0-based)
 * @param field_data Field data (may be NULL if field_length is 0)
 * @param field_length Field length in bytes (0 if null-terminated or empty)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_field_set(
    text_csv_table* table,
    size_t row,
    size_t col,
    const char* field_data,
    size_t field_length
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_TABLE_H */
