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
 * The row index refers to data rows only. If the table has headers, the header
 * row is not accessible via this function. Row indices are 0-based for data rows.
 *
 * @param table Table (must not be NULL)
 * @param row Row index (0-based, data rows only)
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
 * The row index refers to data rows only. If the table has headers, the header
 * row is not accessible via this function. Row indices are 0-based for data rows.
 *
 * @param table Table (must not be NULL)
 * @param row Row index (0-based, data rows only)
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
 * @brief Get the next column index for a header name after a given index
 *
 * Finds the next column with the same header name after the specified index.
 * This function is useful for iterating through all columns with duplicate header names.
 *
 * The function searches for the next entry in the header map that:
 * - Has the same name as the specified header name
 * - Has an index greater than `current_idx`
 *
 * To iterate through all columns with a given header name:
 * 1. Call `text_csv_header_index()` to get the first match
 * 2. Repeatedly call `text_csv_header_index_next()` with the previous index
 *    until it returns `TEXT_CSV_E_INVALID` (no more matches)
 *
 * Only works if the table was parsed with header processing enabled.
 *
 * @param table Table (must not be NULL)
 * @param name Column name to look up
 * @param current_idx Current column index (must be a valid index for this header name)
 * @param out_idx Output parameter for the next column index (must not be NULL)
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if no more matches found or parameters are invalid
 */
TEXT_API text_csv_status text_csv_header_index_next(
    const text_csv_table* table,
    const char* name,
    size_t current_idx,
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
 * @brief Create a CSV table with specified column headers
 *
 * Creates a new table with the specified column headers. Headers are treated
 * as the first row and are excluded from the row count. A header map is built
 * for fast column name lookup.
 *
 * If header_lengths is NULL, all header names are assumed to be null-terminated
 * strings. Otherwise, header_lengths[i] specifies the length of headers[i].
 *
 * Duplicate header names are not allowed and will result in an error.
 *
 * The table must be freed with text_csv_free_table() when no longer needed.
 *
 * @param headers Array of header name pointers (must not be NULL)
 * @param header_lengths Array of header name lengths, or NULL if all are null-terminated
 * @param header_count Number of headers (must be > 0)
 * @return New table with headers, or NULL on allocation failure or duplicate header names
 */
TEXT_API text_csv_table* text_csv_new_table_with_headers(
    const char* const* headers,
    const size_t* header_lengths,
    size_t header_count
);

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
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All memory allocations are performed before any
 * state changes, ensuring no partial state modifications.
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
 * The row index refers to data rows only. If the table has headers, the header
 * row is not accessible via this function. Row indices are 0-based for data rows.
 * The function internally adjusts the index to account for the header row.
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All memory allocations are performed before any
 * state changes (including row shifting), ensuring no partial state modifications.
 *
 * @param table Table (must not be NULL)
 * @param row_idx Row index where to insert (0-based, data rows only, must be <= row_count)
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
 *
 * The row index refers to data rows only. If the table has headers, the header
 * row is not accessible via this function and cannot be removed. Row indices
 * are 0-based for data rows. The function internally adjusts the index to account
 * for the header row.
 *
 * Field data remains in the arena (no individual cleanup needed).
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. This is a simple operation with no memory allocations
 * that can fail, ensuring atomicity.
 *
 * @param table Table (must not be NULL)
 * @param row_idx Row index to remove (0-based, data rows only)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_row_remove(
    text_csv_table* table,
    size_t row_idx
);

/**
 * @brief Replace a row at the specified index with new field values
 *
 * Replaces the row at the specified index with new field values.
 * The field count must match the table's column count.
 * All field data is copied to the arena and does not reference external buffers.
 * If field_lengths is NULL, all fields are assumed to be null-terminated strings.
 *
 * The row index refers to data rows only. If the table has headers, the header
 * row is not accessible via this function. Row indices are 0-based for data rows.
 * The function internally adjusts the index to account for the header row.
 *
 * Existing field data remains in the arena (no individual cleanup needed).
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. Uses a two-phase approach: all field data is
 * bulk-allocated in one contiguous block before any field structures are updated,
 * ensuring no partial state modifications.
 *
 * @param table Table (must not be NULL)
 * @param row_idx Row index to replace (0-based, data rows only)
 * @param fields Array of field data pointers (must not be NULL)
 * @param field_lengths Array of field lengths, or NULL if all fields are null-terminated
 * @param field_count Number of fields (must match table column count)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_row_set(
    text_csv_table* table,
    size_t row_idx,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count
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
 * The row index refers to data rows only. If the table has headers, the header
 * row is not accessible via this function. Row indices are 0-based for data rows.
 * The function internally adjusts the index to account for the header row.
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. Since only a single field is modified, the operation
 * is inherently atomic - if allocation fails, the field structure is not updated.
 *
 * @param table Table (must not be NULL)
 * @param row Row index (0-based, data rows only)
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

/**
 * @brief Clear all data rows from the table
 *
 * Removes all data rows from the table while preserving the table structure.
 * If the table has headers, the header row is preserved. The column count,
 * row capacity, and header map (if present) are all preserved.
 *
 * This function automatically compacts the table by moving preserved data
 * (headers if present) to a new arena and freeing the old arena, which
 * releases memory from cleared data rows.
 *
 * This function is useful for reusing a table structure with new data.
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. If compaction fails, the original row count is
 * restored, ensuring the table state is never partially modified.
 *
 * @param table Table (must not be NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_table_clear(text_csv_table* table);

/**
 * @brief Compact the table by moving data to a new arena
 *
 * Allocates a new arena and moves all current table data to the new arena,
 * then frees the old arena. This releases memory from old allocations that
 * may have been left behind due to repeated modifications (e.g., row/column
 * insertions, deletions, or field updates).
 *
 * This function preserves all current rows and data - it does not remove any
 * data, only reorganizes it into a fresh arena to reclaim wasted memory.
 *
 * This function is automatically called by `text_csv_table_clear()`, but can
 * also be called independently to reclaim memory without clearing any data.
 *
 * All rows are copied to the new arena:
 * - All row structures are copied
 * - All field arrays are copied
 * - All field data is copied (including in-situ fields, which are now in arena)
 * - Header map entries are copied (if present)
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All structures are pre-allocated in the new arena
 * before any data is copied, and the context switch only occurs after all
 * allocations and copies succeed. If any step fails, the new context is freed
 * and the old context remains unchanged.
 *
 * @param table Table (must not be NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_table_compact(text_csv_table* table);

/**
 * @brief Append a new column to all rows in the table
 *
 * Adds a new column to the end of all rows in the table. All existing rows
 * get an empty field added at the end. If the table is empty, only the
 * column count is updated (no rows to modify).
 *
 * If the table has no headers, the header_name parameter is ignored.
 * Header map updates are deferred to Phase 4 (header support).
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All new field arrays are pre-allocated before any
 * row structures are updated, and the column count is only updated in the final
 * atomic state update phase. If any allocation fails, no state has been modified.
 *
 * @param table Table (must not be NULL)
 * @param header_name Header name for the new column (ignored if table has no headers, can be NULL)
 * @param header_name_len Length of header name, or 0 if null-terminated (ignored if table has no headers)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_column_append(
    text_csv_table* table,
    const char* header_name,
    size_t header_name_len
);

/**
 * @brief Append a new column to all rows in the table with initial values
 *
 * Adds a new column to the end of all rows in the table, initializing each
 * field with the provided values. The number of values must exactly match
 * the number of rows in the table.
 *
 * **Value Count Requirements**:
 * - If the table has headers: value count must match `row_count + 1` (data rows + header row)
 * - If the table has no headers: value count must match `row_count`
 * - If the table is empty (`row_count == 0`), returns `TEXT_CSV_E_INVALID`
 *
 * **Header Handling**:
 * - If the table has headers and `values` is provided:
 *   - `values[0]` is used for both the header field value and the header map entry
 *   - The `header_name` parameter is ignored (can be NULL)
 * - If the table has headers and `values` is NULL (empty column):
 *   - `header_name` is used for both the header field value and the header map entry
 *   - `header_name` must not be NULL
 * - If the table has no headers: `header_name` is ignored
 *
 * **Uniqueness Validation**:
 * - If `require_unique_headers` is `true`, validates that the header value doesn't already exist
 * - The check occurs before any allocations or state changes
 * - If a duplicate is found, returns `TEXT_CSV_E_INVALID` and leaves the table unchanged
 *
 * **Value Length Handling**:
 * - If `value_lengths` is NULL, all values are treated as null-terminated strings (uses strlen())
 * - If `value_lengths` is provided, individual entries with length 0 use strlen() fallback
 *
 * All value data is copied to the arena and does not reference external buffers.
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All memory allocations are performed before any
 * state changes, ensuring no partial state modifications.
 *
 * @param table Table (must not be NULL)
 * @param header_name Header name for the new column (used when values is NULL and table has headers, ignored otherwise)
 * @param header_name_len Length of header name, or 0 if null-terminated
 * @param values Array of field values (NULL for empty column, must match row count if provided)
 * @param value_lengths Array of value lengths, or NULL if all are null-terminated
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_column_append_with_values(
    text_csv_table* table,
    const char* header_name,
    size_t header_name_len,
    const char* const* values,
    const size_t* value_lengths
);

/**
 * @brief Insert a new column at the specified index
 *
 * Inserts a new column at the specified index, shifting existing columns right.
 * The index can equal the column count, which is equivalent to appending.
 * All existing rows get an empty field inserted at the specified position.
 *
 * If the table has headers, the header_name parameter is required (not NULL).
 * If the table has no headers, the header_name parameter is ignored.
 *
 * When headers are present:
 * - The header field is inserted in the header row at the specified index
 * - All header map entries with index >= col_idx are reindexed (incremented by 1)
 * - A new header map entry is added with index = col_idx
 * - Duplicate header names are not allowed (returns error)
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All new field arrays and header map entries are
 * pre-allocated before any row structures or header map entries are updated.
 * Header map reindexing occurs after all allocations succeed but before the
 * final atomic state update. If any allocation fails, no state has been modified.
 *
 * @param table Table (must not be NULL)
 * @param col_idx Column index where to insert (0-based, must be <= column count)
 * @param header_name Header name for the new column (required if table has headers, ignored otherwise)
 * @param header_name_len Length of header name, or 0 if null-terminated (ignored if table has no headers)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_column_insert(
    text_csv_table* table,
    size_t col_idx,
    const char* header_name,
    size_t header_name_len
);

/**
 * @brief Insert a new column at the specified index with initial values
 *
 * Inserts a new column at the specified index with initial values for all rows,
 * shifting existing columns right. The index can equal the column count, which
 * is equivalent to appending.
 *
 * **Value Count Requirements**:
 * - If the table has headers: value count must match `row_count + 1` (data rows + header row)
 * - If the table has no headers: value count must match `row_count`
 * - If the table is empty (`row_count == 0`), returns `TEXT_CSV_E_INVALID`
 *
 * **Header Handling**:
 * - If the table has headers and `values` is provided:
 *   - `values[0]` is used for both the header field value and the header map entry
 *   - The `header_name` parameter is ignored (can be NULL)
 * - If the table has headers and `values` is NULL (empty column):
 *   - `header_name` is used for both the header field value and the header map entry
 *   - `header_name` must not be NULL
 * - If the table has no headers: `header_name` is ignored
 *
 * **Uniqueness Validation**:
 * - If `require_unique_headers` is `true`, validates that the header value doesn't already exist
 * - The check occurs before any allocations or state changes
 * - If a duplicate is found, returns `TEXT_CSV_E_INVALID` and leaves the table unchanged
 *
 * **Value Length Handling**:
 * - If `value_lengths` is NULL, all values are treated as null-terminated strings (uses strlen())
 * - If `value_lengths` is provided, individual entries with length 0 use strlen() fallback
 *
 * All value data is copied to the arena and does not reference external buffers.
 *
 * When headers are present:
 * - The header field is inserted in the header row at the specified index
 * - All header map entries with index >= col_idx are reindexed (incremented by 1)
 * - A new header map entry is added with index = col_idx
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All memory allocations are performed before any
 * state changes, ensuring no partial state modifications.
 *
 * @param table Table (must not be NULL)
 * @param col_idx Column index where to insert (0-based, must be <= column count)
 * @param header_name Header name for the new column (used when values is NULL and table has headers, ignored otherwise)
 * @param header_name_len Length of header name, or 0 if null-terminated
 * @param values Array of field values (NULL for empty column, must match row count if provided)
 * @param value_lengths Array of value lengths, or NULL if all are null-terminated
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_column_insert_with_values(
    text_csv_table* table,
    size_t col_idx,
    const char* header_name,
    size_t header_name_len,
    const char* const* values,
    const size_t* value_lengths
);

/**
 * @brief Remove a column at the specified index from all rows
 *
 * Removes the column at the specified index from all rows in the table, shifting
 * remaining columns left. The column index must be valid (must be < column count).
 *
 * When the table has headers:
 * - The header field is removed from the header row
 * - The header map entry for the removed column is removed from the hash table
 * - All header map entries with index > col_idx are reindexed (decremented by 1)
 *
 * When the table has no headers, only the column removal is performed.
 *
 * Field data remains in the arena (no individual cleanup needed).
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. This operation involves shifting fields in all rows
 * and updating the header map, but no memory allocations that can fail, ensuring
 * atomicity. Header map entry removal and reindexing occur before the final
 * atomic state update.
 *
 * @param table Table (must not be NULL)
 * @param col_idx Column index to remove (0-based, must be < column count)
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_column_remove(
    text_csv_table* table,
    size_t col_idx
);

/**
 * @brief Rename a column header
 *
 * Renames the column header at the specified index. This function only works
 * if the table has headers (returns error otherwise).
 *
 * The new header name must not duplicate an existing header name. If the new
 * name already exists in the header map, the function returns an error.
 *
 * The header field in the header row is updated with the new name, and the
 * header map is updated accordingly (old entry removed, new entry added with
 * the same index).
 *
 * If new_name_length is 0, the new_name is assumed to be a null-terminated
 * string and strlen() will be used to determine the length.
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All memory allocations (new name string, new header
 * entry) are performed before any state changes. If any allocation fails, the
 * table state is unchanged.
 *
 * @param table Table (must not be NULL)
 * @param col_idx Column index to rename (0-based, must be < column count)
 * @param new_name New header name (must not be NULL)
 * @param new_name_length Length of new header name, or 0 if null-terminated
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_column_rename(
    text_csv_table* table,
    size_t col_idx,
    const char* new_name,
    size_t new_name_length
);

/**
 * @brief Set whether unique headers are required for mutation operations
 *
 * Controls whether mutation operations (column append, insert, rename) enforce
 * uniqueness of header names. When set to `true`, these operations will fail
 * if they would create duplicate header names. When set to `false` (the default),
 * duplicate header names are allowed.
 *
 * This flag only affects mutation operations. Parsing behavior is controlled
 * by the `header_dup_mode` in the parse options dialect.
 *
 * @param table Table (must not be NULL)
 * @param require Whether to require unique headers (true) or allow duplicates (false)
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if table is NULL
 */
TEXT_API text_csv_status text_csv_set_require_unique_headers(
    text_csv_table* table,
    bool require
);

/**
 * @brief Check if table can have unique headers
 *
 * Returns `true` if the table has headers and all header names are currently unique.
 * Returns `false` if:
 * - The table does not have headers
 * - The table has headers but contains duplicate header names
 * - The table is NULL
 *
 * This function is useful for checking if a table is in a state where unique headers
 * can be enforced (i.e., before enabling `require_unique_headers`).
 *
 * @param table Table (must not be NULL)
 * @return `true` if headers exist and are unique, `false` otherwise
 */
TEXT_API bool text_csv_can_have_unique_headers(const text_csv_table* table);

/**
 * @brief Enable or disable header row processing
 *
 * Toggles whether the first row of the table is treated as a header row.
 *
 * When enabling headers (`enable = true`):
 * - The first data row becomes the header row
 * - A header map is built for column name lookup
 * - The row count decreases by 1 (header row is excluded from data row count)
 * - If the table is empty, returns `TEXT_CSV_E_INVALID`
 * - If headers already exist, returns `TEXT_CSV_E_INVALID`
 * - If `require_unique_headers` is `true`, validates that all header names are unique
 * - Column count is adjusted if the first row has a different number of columns
 *
 * When disabling headers (`enable = false`):
 * - The header row becomes the first data row
 * - The header map is cleared
 * - The row count increases by 1 (header row becomes a data row)
 * - If headers don't exist, returns `TEXT_CSV_E_INVALID`
 *
 * **Atomic Operation**: This function is atomic - either the entire operation
 * succeeds and the table remains in a consistent state, or it fails and the
 * table remains unchanged. All memory allocations are performed before any
 * state changes.
 *
 * @param table Table (must not be NULL)
 * @param enable `true` to enable headers, `false` to disable headers
 * @return TEXT_CSV_OK on success, error code on failure
 */
TEXT_API text_csv_status text_csv_set_header_row(
    text_csv_table* table,
    bool enable
);

/**
 * @brief Create a deep copy of a CSV table
 *
 * Creates a complete independent copy of the source table, allocating all
 * memory from a new arena. The cloned table is completely independent of
 * the original - modifications to one table do not affect the other.
 *
 * All field data is copied to the new arena, including fields that were
 * originally in-situ (referencing the input buffer). This ensures the
 * cloned table is fully independent and does not reference any external
 * buffers.
 *
 * The cloned table must be freed separately with text_csv_free_table()
 * when no longer needed.
 *
 * @param source Source table to clone (must not be NULL)
 * @return New cloned table, or NULL on allocation failure
 */
TEXT_API text_csv_table* text_csv_clone(const text_csv_table* source);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_TABLE_H */
