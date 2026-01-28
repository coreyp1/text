/**
 * @file csv_table_internal.h
 * @brief Internal helper functions for CSV table operations
 *
 * This header contains documentation for static helper functions used
 * internally within csv_table.c. These functions are not part of the
 * public API and are only visible within csv_table.c.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_CSV_TABLE_INTERNAL_H
#define GHOTI_IO_GTEXT_CSV_TABLE_INTERNAL_H

#include "csv_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Field Operations
// ============================================================================

/**
 * @brief Calculate field length from field_data and optional field_lengths
 * array
 *
 * @fn static size_t csv_calculate_field_length(const char * field_data, const
 * size_t * field_lengths, size_t field_index)
 *
 * @param field_data Field data (may be NULL for empty fields)
 * @param field_lengths Array of field lengths, or NULL if all fields are
 * null-terminated
 * @param field_index Index of the field
 * @return Calculated field length (0 if field_data is NULL and field_lengths is
 * NULL)
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Set up an empty field structure
 *
 * Sets the field to point to the global empty string constant.
 *
 * @fn static void csv_setup_empty_field(csv_table_field * field)
 *
 * @param field Field structure to set up
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Set error information for field count validation failures
 *
 * Populates an error structure with detailed information about field count
 * mismatches, including a formatted message with expected vs actual counts.
 * The formatted message is stored in context_snippet and message points to it,
 * so it can be freed via gtext_csv_error_free().
 *
 * @fn static void csv_set_field_count_error(GTEXT_CSV_Error * err, size_t
 * expected_count, size_t actual_count, size_t row_index)
 *
 * @param err Error structure to populate (can be NULL)
 * @param expected_count Expected field count
 * @param actual_count Actual field count provided
 * @param row_index Row index where error occurred (0-based for data rows,
 * SIZE_MAX for append)
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Allocate and copy a single field to the arena
 *
 * Allocates memory from the arena and copies the field data.
 * Handles overflow checks and allocation failures.
 *
 * @fn static GTEXT_CSV_Status csv_allocate_and_copy_field(csv_context * ctx,
 * const char * field_data, size_t field_len, csv_table_field * field_out)
 *
 * @param ctx Context with arena
 * @param field_data Field data to copy (must not be NULL)
 * @param field_len Field length in bytes
 * @param field_out Output field structure to populate
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

// ============================================================================
// Header Map Operations
// ============================================================================

/**
 * @brief Check if a header name already exists in the header map
 *
 * Checks for duplicate header names when uniqueness is required.
 * Optionally excludes a specific column index from the check (useful for rename
 * operations).
 *
 * @fn static GTEXT_CSV_Status csv_check_header_uniqueness(const GTEXT_CSV_Table
 * * table, const char * name, size_t name_len, size_t exclude_index)
 *
 * @param table Table with header map
 * @param name Header name to check
 * @param name_len Length of header name
 * @param exclude_index Column index to exclude from check (SIZE_MAX to exclude
 * none)
 * @return GTEXT_CSV_E_INVALID if duplicate found and uniqueness required,
 * GTEXT_CSV_OK otherwise
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Ensure index_to_entry array has sufficient capacity
 *
 * Grows the index_to_entry array if needed to accommodate the given index.
 * Allocates in the arena for permanent storage.
 *
 * @fn static GTEXT_CSV_Status
 * csv_ensure_index_to_entry_capacity(GTEXT_CSV_Table * table, size_t
 * required_index)
 *
 * @param table Table with header map
 * @param required_index Minimum index that must be supported
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Set entry in reverse mapping (index -> entry pointer)
 *
 * Updates the reverse mapping to point from column index to header entry.
 * Automatically grows the array if needed.
 *
 * @fn static GTEXT_CSV_Status csv_set_index_to_entry(GTEXT_CSV_Table * table,
 * size_t col_idx, csv_header_entry * entry)
 *
 * @param table Table with header map
 * @param col_idx Column index
 * @param entry Header entry pointer (can be NULL to clear)
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Find header entry by column index using reverse mapping
 *
 * O(1) lookup of header entry by column index using the reverse mapping array.
 * Falls back to O(n) search if reverse mapping is not available or entry not
 * found.
 *
 * @fn static bool csv_find_header_entry_by_index(const GTEXT_CSV_Table * table,
 * size_t col_idx, csv_header_entry ** entry_out, csv_header_entry ***
 * prev_ptr_out)
 *
 * @param table Table with header map
 * @param col_idx Column index to find
 * @param entry_out Output parameter for found entry (NULL if not found)
 * @param prev_ptr_out Output parameter for pointer to previous entry in chain
 * (for removal)
 * @return true if entry found, false otherwise
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Rebuild reverse mapping from header map
 *
 * Rebuilds the index_to_entry array by iterating through all header map
 * entries. This is needed after operations that rebuild the header map
 * (compact, clone).
 *
 * @fn static GTEXT_CSV_Status csv_rebuild_index_to_entry(GTEXT_CSV_Table *
 * table)
 *
 * @param table Table with header map (must not be NULL)
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Reindex header map entries by incrementing indices
 *
 * Increments the index of all header map entries with index >= start_index.
 * Used when inserting columns to shift existing column indices.
 *
 * @fn static void csv_header_map_reindex_increment(GTEXT_CSV_Table * table,
 * size_t start_index)
 *
 * @param table Table (must not be NULL)
 * @param start_index Starting index - entries with index >= this are
 * incremented
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Reindex header map entries by decrementing indices
 *
 * Decrements the index of all header map entries with index > start_index.
 * Used when removing columns to shift remaining column indices.
 *
 * @fn static void csv_header_map_reindex_decrement(GTEXT_CSV_Table * table,
 * size_t start_index)
 *
 * @param table Table (must not be NULL)
 * @param start_index Starting index - entries with index > this are decremented
 *
 * @note This is a static function defined in csv_table.c
 */

// ============================================================================
// Row Operations
// ============================================================================

/**
 * @brief Prepare fields for row operations
 *
 * Calculates field lengths, validates inputs, and allocates bulk field data.
 * This is the common logic shared by gtext_csv_row_append and
 * gtext_csv_row_insert.
 *
 * @fn static GTEXT_CSV_Status csv_row_prepare_fields(GTEXT_CSV_Table * table,
 * const char * const * fields, const size_t * field_lengths, size_t
 * field_count, const char ** allocated_data, size_t * allocated_lengths, char
 * ** bulk_arena_data_out)
 *
 * @param table Table (must not be NULL)
 * @param fields Array of field data pointers (must not be NULL)
 * @param field_lengths Array of field lengths, or NULL if all are
 * null-terminated
 * @param field_count Number of fields (must be > 0)
 * @param allocated_data Output array to store allocated field data pointers
 * (must have size field_count)
 * @param allocated_lengths Output array to store field lengths (must have size
 * field_count)
 * @param bulk_arena_data_out Output parameter for bulk allocated data block (or
 * NULL if all fields empty)
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Allocate structures for row operations
 *
 * Allocates field array and handles row capacity growth if needed.
 * This is the common logic shared by gtext_csv_row_append and
 * gtext_csv_row_insert.
 *
 * @fn static GTEXT_CSV_Status csv_row_allocate_structures(GTEXT_CSV_Table *
 * table, size_t field_count, csv_table_field ** new_fields_out, csv_table_row
 * ** new_rows_out, size_t * new_capacity_out)
 *
 * @param table Table (must not be NULL)
 * @param field_count Number of fields (must be > 0)
 * @param new_fields_out Output parameter for allocated field array
 * @param new_rows_out Output parameter for row array (may be same as
 * table->rows if no growth)
 * @param new_capacity_out Output parameter for new row capacity (may be same as
 * table->row_capacity)
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

// ============================================================================
// Column Operations
// ============================================================================

/**
 * @brief Allocate all temporary arrays for column operations
 *
 * Allocates all temporary arrays needed for column operations.
 * If any allocation fails, all previously allocated arrays are freed.
 * Note: field_data_array and field_data_lengths are allocated separately
 * by csv_preallocate_column_field_data and should be set in temp_arrays
 * after allocation.
 *
 * @fn static GTEXT_CSV_Status csv_column_op_alloc_temp_arrays(size_t
 * rows_to_modify, csv_column_op_temp_arrays * temp_arrays_out)
 *
 * @param rows_to_modify Number of rows to modify
 * @param temp_arrays_out Output parameter for allocated structure (must not be
 * NULL)
 * @return GTEXT_CSV_OK on success, GTEXT_CSV_E_OOM on allocation failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Clean up all temporary arrays for column operations
 *
 * Safely frees all temporary arrays. Safe to call with partially allocated
 * structure.
 *
 * @fn static void csv_column_op_cleanup_temp_arrays(csv_column_op_temp_arrays *
 * temp_arrays)
 *
 * @param temp_arrays Temporary arrays structure to clean up (can be NULL)
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Clean up temporary arrays from individual pointers
 *
 * Helper function for callers that have individual pointers instead of a
 * structure. Safely frees all provided pointers.
 *
 * @fn static void csv_column_op_cleanup_individual(csv_table_field **
 * new_field_arrays, size_t * old_field_counts, char ** field_data_array, size_t
 * * field_data_lengths)
 *
 * @param new_field_arrays Array of field array pointers to free (can be NULL)
 * @param old_field_counts Array of field counts to free (can be NULL)
 * @param field_data_array Array of field data pointers to free (can be NULL)
 * @param field_data_lengths Array of field data lengths to free (can be NULL)
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Validate column values array
 *
 * Validates that the values array has the correct number of entries
 * matching the table's row count.
 *
 * @fn static GTEXT_CSV_Status csv_validate_column_values(const GTEXT_CSV_Table
 * * table, const char * const * values)
 *
 * @param table Table (must not be NULL)
 * @param values Values array (must not be NULL)
 * @return GTEXT_CSV_OK if valid, GTEXT_CSV_E_INVALID if invalid
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Determine header value and name for header map
 *
 * Determines the header value (for the header field) and header map name
 * based on whether the column is empty or has values, and whether the
 * table has headers.
 *
 * @fn static GTEXT_CSV_Status csv_determine_header_value(const GTEXT_CSV_Table
 * * table, bool is_empty_column, const char * header_name, size_t
 * header_name_len, const char * const * values, const size_t * value_lengths,
 * const char ** header_value_out, size_t * header_value_len_out, const char **
 * header_map_name_out, size_t * header_map_name_len_out, size_t * name_len_out)
 *
 * @param table Table (must not be NULL)
 * @param is_empty_column Whether this is an empty column (values is NULL)
 * @param header_name Header name (required if empty column with headers)
 * @param header_name_len Length of header name, or 0 if null-terminated
 * @param values Values array (must not be NULL if not empty column)
 * @param value_lengths Optional array of value lengths
 * @param header_value_out Output parameter for header value pointer
 * @param header_value_len_out Output parameter for header value length
 * @param header_map_name_out Output parameter for header map name pointer
 * @param header_map_name_len_out Output parameter for header map name length
 * @param name_len_out Output parameter for name length
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Pre-allocate field data for column operation
 *
 * Pre-allocates all field data for the new column. Handles empty fields
 * by setting them to NULL (will use csv_empty_field_string).
 *
 * @fn static GTEXT_CSV_Status csv_preallocate_column_field_data(GTEXT_CSV_Table
 * * table, bool is_empty_column, size_t rows_to_modify, const char * const *
 * values, const size_t * value_lengths, char *** field_data_array_out, size_t
 * ** field_data_lengths_out)
 *
 * @param table Table (must not be NULL)
 * @param is_empty_column Whether this is an empty column (values is NULL)
 * @param rows_to_modify Number of rows to modify
 * @param values Values array (must not be NULL if not empty column)
 * @param value_lengths Optional array of value lengths
 * @param field_data_array_out Output parameter for allocated field data array
 * (caller must free with free())
 * @param field_data_lengths_out Output parameter for field data lengths (caller
 * must free with free())
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Internal helper for column append/insert operations
 *
 * Handles common logic for column operations including validation,
 * field array pre-allocation, header field data allocation, header map
 * entry allocation, and data copying. Parameterized by insertion index
 * (use SIZE_MAX for append operations). Can optionally handle provided
 * values for all fields (when values is not NULL).
 *
 * @fn static GTEXT_CSV_Status csv_column_operation_internal(GTEXT_CSV_Table *
 * table, size_t col_idx, const char * header_name, size_t header_name_len,
 * const char * const * values, const size_t * value_lengths, csv_table_field
 * *** new_field_arrays_out, size_t ** old_field_counts_out, size_t *
 * rows_to_modify_out, char ** header_field_data_out, size_t *
 * header_field_data_len_out, csv_header_entry ** new_entry_out, size_t *
 * header_hash_out, csv_table_field ** new_header_fields_out, size_t *
 * old_header_field_count_out, char *** field_data_array_out, size_t **
 * field_data_lengths_out)
 *
 * @param table Table (must not be NULL)
 * @param col_idx Column index where to insert (SIZE_MAX for append, must be <=
 * column_count for insert)
 * @param header_name Header name for the new column (required if table has
 * headers when values is NULL, ignored otherwise)
 * @param header_name_len Length of header name, or 0 if null-terminated
 * @param values Optional array of field values (NULL for empty fields, must
 * match row count if provided)
 * @param value_lengths Optional array of value lengths, or NULL if all are
 * null-terminated
 * @param new_field_arrays_out Output parameter for allocated field arrays
 * (caller must free with free())
 * @param old_field_counts_out Output parameter for old field counts (caller
 * must free with free())
 * @param rows_to_modify_out Output parameter for number of rows to modify
 * @param header_field_data_out Output parameter for header field data (or NULL
 * if empty)
 * @param header_field_data_len_out Output parameter for header field data
 * length
 * @param new_entry_out Output parameter for header map entry (or NULL if no
 * headers)
 * @param header_hash_out Output parameter for header hash (if headers present)
 * @param new_header_fields_out Output parameter for new header fields array (or
 * NULL if no headers)
 * @param old_header_field_count_out Output parameter for old header field count
 * @param field_data_array_out Output parameter for allocated field data array
 * (caller must free with free(), NULL if values is NULL)
 * @param field_data_lengths_out Output parameter for field data lengths (caller
 * must free with free(), NULL if values is NULL)
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

// ============================================================================
// Table Utilities
// ============================================================================

/**
 * @brief Recalculate maximum column count across all rows
 *
 * Iterates through all rows and finds the maximum field_count.
 * This is useful when irregular rows are allowed and we need to
 * update column_count to reflect the current maximum.
 *
 * @fn static size_t csv_recalculate_max_column_count(const GTEXT_CSV_Table *
 * table)
 *
 * @param table Table (must not be NULL)
 * @return Maximum field_count across all rows, or 0 if table is empty
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Get the number of data rows (excluding header row if present)
 *
 * Returns the number of data rows in the table, excluding the header row
 * if one exists. This is equivalent to:
 * `row_count - (has_header && row_count > 0 ? 1 : 0)`
 *
 * @fn static size_t csv_get_data_row_count(const GTEXT_CSV_Table * table)
 *
 * @param table Table (must not be NULL)
 * @return Number of data rows (0 if table is empty or only has header)
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Get the starting row index for data rows
 *
 * Returns the index of the first data row. If the table has a header row,
 * data rows start at index 1; otherwise they start at index 0.
 * This is equivalent to: `has_header && row_count > 0 ? 1 : 0`
 *
 * @fn static size_t csv_get_start_row_idx(const GTEXT_CSV_Table * table)
 *
 * @param table Table (must not be NULL)
 * @return Starting row index for data rows (0 or 1)
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Get the number of rows to modify for column operations
 *
 * Calculates the number of rows that need to be modified when performing
 * column operations. This includes all data rows plus the header row if
 * present. This is equivalent to: `row_count - start_row_idx + (has_header &&
 * row_count > 0 ? 1 : 0)`
 *
 * @fn static size_t csv_get_rows_to_modify(const GTEXT_CSV_Table * table)
 *
 * @param table Table (must not be NULL)
 * @return Number of rows to modify (includes header row if present)
 *
 * @note This is a static function defined in csv_table.c
 */

// ============================================================================
// Table Compaction
// ============================================================================

/**
 * @brief Calculate total size needed for compaction
 *
 * Calculates the total size needed to compact a table, including:
 * - Rows array
 * - Field arrays for all rows
 * - Field data for non-empty, non-in-situ fields
 * - Header map entries and names
 * - Overhead for alignment and safety margin
 *
 * @fn static GTEXT_CSV_Status csv_calculate_compact_size(const GTEXT_CSV_Table
 * * table, size_t * total_size_out)
 *
 * @param table Table to compact (must not be NULL)
 * @param total_size_out Output parameter for calculated size
 * @return GTEXT_CSV_OK on success, GTEXT_CSV_E_OOM on overflow
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Pre-allocate all structures in new arena
 *
 * Pre-allocates rows array, field arrays, and field data blocks in the new
 * arena. All allocations happen before any state changes to preserve atomicity.
 *
 * @fn static GTEXT_CSV_Status csv_preallocate_compact_structures(const
 * GTEXT_CSV_Table * table, csv_context * new_ctx, csv_compact_structures *
 * structures_out)
 *
 * @param table Table to compact (must not be NULL)
 * @param new_ctx New context with arena (must not be NULL)
 * @param structures_out Output structure with pre-allocated pointers
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Copy all data to pre-allocated structures
 *
 * Copies all row and field data to the pre-allocated structures in the new
 * arena. Handles empty fields, in-situ fields, and arena-allocated fields.
 *
 * @fn static GTEXT_CSV_Status csv_copy_data_to_new_arena(const GTEXT_CSV_Table
 * * table, const csv_compact_structures * structures)
 *
 * @param table Table to compact (must not be NULL)
 * @param structures Pre-allocated structures (must not be NULL)
 * @return GTEXT_CSV_OK on success
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Rebuild header map in new arena
 *
 * Pre-allocates and copies all header map entries and names to the new arena.
 * Rebuilds the hash table structure.
 *
 * @fn static GTEXT_CSV_Status csv_rebuild_header_map(const GTEXT_CSV_Table *
 * table, const csv_context * old_ctx, csv_context * new_ctx,
 * csv_compact_header_map * header_map_out)
 *
 * @param table Table to compact (must not be NULL)
 * @param old_ctx Old context (for input_buffer reference, must not be NULL)
 * @param new_ctx New context with arena (must not be NULL)
 * @param header_map_out Output structure with pre-allocated header map
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

// ============================================================================
// Table Cloning
// ============================================================================

/**
 * @brief Calculate total size needed for cloning a table
 *
 * Calculates the total size needed to clone a table, including:
 * - Table structure
 * - Rows array
 * - Field arrays for all rows
 * - Field data for ALL non-empty fields (including in-situ ones)
 * - Header map entries and names (including in-situ ones)
 * - Overhead for alignment and safety margin
 *
 * @fn static GTEXT_CSV_Status csv_clone_calculate_size(const GTEXT_CSV_Table *
 * source, size_t * total_size_out)
 *
 * @param source Source table to clone (must not be NULL)
 * @param total_size_out Output parameter for calculated size
 * @return GTEXT_CSV_OK on success, GTEXT_CSV_E_OOM on overflow
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Pre-allocate all structures in new arena for cloning
 *
 * Pre-allocates table structure, rows array, field arrays, field data blocks,
 * and header map structures in the new arena. All allocations happen before
 * any state changes to preserve atomicity.
 *
 * @fn static GTEXT_CSV_Status csv_clone_preallocate_structures(const
 * GTEXT_CSV_Table * source, csv_context * new_ctx, csv_clone_structures *
 * structures_out, csv_clone_header_map * header_map_out)
 *
 * @param source Source table to clone (must not be NULL)
 * @param new_ctx New context with arena (must not be NULL)
 * @param structures_out Output structure with pre-allocated pointers
 * @param header_map_out Output structure with pre-allocated header map pointers
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

/**
 * @brief Copy all data to pre-allocated structures for cloning
 *
 * Copies all row data, field data, and header map data to the pre-allocated
 * structures. No allocations are performed - all memory is already allocated.
 *
 * @fn static GTEXT_CSV_Status csv_clone_copy_data(const GTEXT_CSV_Table *
 * source, csv_clone_structures * structures, csv_clone_header_map * header_map)
 *
 * @param source Source table to clone (must not be NULL)
 * @param structures Pre-allocated structures (must not be NULL)
 * @param header_map Pre-allocated header map structures (must not be NULL)
 * @return GTEXT_CSV_OK on success, error code on failure
 *
 * @note This is a static function defined in csv_table.c
 */

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_CSV_TABLE_INTERNAL_H
