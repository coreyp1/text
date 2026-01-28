/**
 * @file csv_writer_internal.h
 * @brief Internal helper functions for CSV writer operations
 *
 * This header contains documentation for static helper functions used
 * internally within csv_writer.c. These functions are not part of the
 * public API and are only visible within csv_writer.c.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_CSV_WRITER_INTERNAL_H
#define GHOTI_IO_GTEXT_CSV_WRITER_INTERNAL_H

#include "csv_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Field Operations
// ============================================================================

/**
 * @brief Determine if a field needs to be quoted
 *
 * Checks the write options to determine if a field should be quoted:
 * - quote_all_fields: always quote
 * - quote_empty_fields: quote if field is empty
 * - quote_if_needed: quote if field contains delimiter, quote char, or newline
 *
 * @fn static bool csv_field_needs_quoting(const char * field_data, size_t
 * field_len, const GTEXT_CSV_Write_Options * opts)
 *
 * @param field_data Field data (may be NULL if field_len is 0)
 * @param field_len Field length in bytes (0 for empty field)
 * @param opts Write options
 * @return true if field should be quoted, false otherwise
 *
 * @note This is a static function defined in csv_writer.c
 */

/**
 * @brief Calculate the escaped length of a field
 *
 * Calculates how many bytes the field will take after escaping quotes
 * according to the escape mode.
 *
 * @fn static size_t csv_field_escaped_length(const char * field_data, size_t
 * field_len, GTEXT_CSV_Escape_Mode escape_mode, char quote_char)
 *
 * @param field_data Field data
 * @param field_len Field length in bytes
 * @param escape_mode Escape mode
 * @param quote_char Quote character
 * @return Escaped length in bytes
 *
 * @note This is a static function defined in csv_writer.c
 */

/**
 * @brief Escape a field into a buffer
 *
 * Escapes quotes in a field according to the escape mode and writes
 * the result to the output buffer. The output buffer must be large
 * enough to hold the escaped field (use csv_field_escaped_length to
 * calculate the required size).
 *
 * @fn static GTEXT_CSV_Status csv_field_escape(const char * field_data, size_t
 * field_len, char * output_buffer, size_t output_buffer_size,
 * GTEXT_CSV_Escape_Mode escape_mode, char quote_char, size_t * output_len)
 *
 * @param field_data Field data to escape
 * @param field_len Field length in bytes
 * @param output_buffer Output buffer (must be large enough)
 * @param output_buffer_size Size of output buffer
 * @param escape_mode Escape mode
 * @param quote_char Quote character
 * @param output_len Output parameter: number of bytes written
 * @return GTEXT_CSV_OK on success, GTEXT_CSV_E_INVALID if buffer too small
 *
 * @note This is a static function defined in csv_writer.c
 */

// ============================================================================
// Table Serialization
// ============================================================================

/**
 * @brief Find the last non-empty field in a row
 *
 * Iterates backwards through fields to find the index of the last non-empty
 * field. A field is considered empty if its length is 0.
 *
 * @fn static size_t csv_find_last_non_empty_field(const csv_table_row * row)
 *
 * @param row Row structure (must not be NULL)
 * @return Index of last non-empty field, or SIZE_MAX if all fields are empty
 *
 * @note This is a static function defined in csv_writer.c
 */

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_CSV_WRITER_INTERNAL_H
