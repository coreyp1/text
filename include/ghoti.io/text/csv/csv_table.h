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
 * @param data Input CSV data
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

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_TABLE_H */
