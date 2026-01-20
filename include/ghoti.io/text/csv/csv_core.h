/**
 * @file csv_core.h
 * @brief Core CSV types and definitions
 *
 * This header provides the core types, enums, and option structures for the CSV module.
 * It does not include the full API headers. Use this for internal implementations that
 * only need type definitions.
 *
 * For the full CSV API, include <ghoti.io/text/csv.h> instead.
 */

#ifndef GHOTI_IO_TEXT_CSV_CORE_H
#define GHOTI_IO_TEXT_CSV_CORE_H

#include <ghoti.io/text/macros.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CSV operation status codes
 */
typedef enum {
  TEXT_CSV_OK = 0,

  // General errors
  TEXT_CSV_E_INVALID,        ///< Invalid input or operation
  TEXT_CSV_E_OOM,            ///< Out of memory
  TEXT_CSV_E_LIMIT,          ///< Resource limit exceeded

  // Parsing errors
  TEXT_CSV_E_UNTERMINATED_QUOTE,  ///< Unterminated quoted field (EOF inside quotes)
  TEXT_CSV_E_INVALID_ESCAPE,      ///< Invalid escape sequence (for backslash-escape dialect)
  TEXT_CSV_E_UNEXPECTED_QUOTE,    ///< Unexpected quote in unquoted field (when disallowed)
  TEXT_CSV_E_INVALID_UTF8,        ///< UTF-8 validation failure (when enabled)
  TEXT_CSV_E_INCOMPLETE_CRLF,     ///< Incomplete CRLF sequence (strict CRLF-only dialect)
  TEXT_CSV_E_TOO_MANY_COLS,       ///< Too many columns in record
  TEXT_CSV_E_TOO_MANY_ROWS,       ///< Too many rows

  // Writing errors
  TEXT_CSV_E_WRITE,           ///< Write operation failed
  TEXT_CSV_E_STATE            ///< Invalid state for operation
} text_csv_status;

/**
 * @brief CSV error information
 *
 * Contains detailed error information including code, message, position,
 * and optional enhanced diagnostics (context snippet, caret positioning).
 */
typedef struct {
  text_csv_status code;      ///< Error code
  const char* message;       ///< Human-readable error message (static string)
  size_t byte_offset;        ///< Byte offset from start of input (0-based)
  int line;                  ///< Line number (1-based)
  int column;                ///< Column number (1-based, byte-based)
  size_t row_index;          ///< Row index (0-based, first data row is 0)
  size_t col_index;          ///< Column index (0-based)

  // Enhanced error reporting (optional, may be NULL)
  char* context_snippet;      ///< Context snippet around error (dynamically allocated, caller must free)
  size_t context_snippet_len; ///< Length of context snippet
  size_t caret_offset;        ///< Byte offset of caret within context snippet (0-based)
} text_csv_error;

/**
 * @brief Escape mode for CSV dialect
 */
typedef enum {
  TEXT_CSV_ESCAPE_DOUBLED_QUOTE,  ///< Escape quotes by doubling ("") (default)
  TEXT_CSV_ESCAPE_BACKSLASH,     ///< Escape quotes with backslash (\")
  TEXT_CSV_ESCAPE_NONE            ///< No escaping (not recommended)
} text_csv_escape_mode;

/**
 * @brief Duplicate column name handling mode
 */
typedef enum {
  TEXT_CSV_DUPCOL_ERROR,      ///< Fail parse on duplicate column name (default)
  TEXT_CSV_DUPCOL_FIRST_WINS, ///< Use first occurrence of duplicate column
  TEXT_CSV_DUPCOL_LAST_WINS,  ///< Use last occurrence of duplicate column
  TEXT_CSV_DUPCOL_COLLECT     ///< Store all indices for duplicate columns
} text_csv_dupcol_mode;

/**
 * @brief CSV dialect structure
 *
 * Defines the exact format rules for parsing and writing CSV.
 */
typedef struct {
  char delimiter;                    ///< Field delimiter (default ',')
  char quote;                       ///< Quote character (default '"')
  text_csv_escape_mode escape;      ///< Escape mode (default DOUBLED_QUOTE)
  bool newline_in_quotes;            ///< Allow newlines inside quoted fields (default true)
  bool accept_lf;                    ///< Accept LF as newline (default true)
  bool accept_crlf;                  ///< Accept CRLF as newline (default true)
  bool accept_cr;                    ///< Accept CR as newline (default false)
  bool trim_unquoted_fields;         ///< Trim whitespace from unquoted fields (default false)
  bool allow_space_after_delimiter;  ///< Allow spaces after delimiter (default false)
  bool allow_unquoted_quotes;        ///< Allow quotes in unquoted fields (default false)
  bool allow_unquoted_newlines;      ///< Allow newlines in unquoted fields (default false)
  bool allow_comments;               ///< Allow comment lines (default false)
  const char* comment_prefix;        ///< Comment prefix string (default "#")
  bool treat_first_row_as_header;    ///< Treat first row as header (default false)
  text_csv_dupcol_mode header_dup_mode; ///< Duplicate column name handling (default ERROR)
} text_csv_dialect;

/**
 * @brief CSV parse options structure
 *
 * Controls parsing behavior including dialect, limits, and error reporting.
 */
typedef struct {
  text_csv_dialect dialect;          ///< CSV dialect configuration
  bool validate_utf8;                ///< Validate UTF-8 sequences (default true)
  bool in_situ_mode;                 ///< Zero-copy mode: reference input buffer directly (default false)
  bool keep_bom;                     ///< Keep UTF-8 BOM (default false, strips BOM if false)

  // Limits (0 => library default)
  size_t max_rows;                   ///< Maximum number of rows (0 = default, e.g. 10M)
  size_t max_cols;                   ///< Maximum number of columns per row (0 = default, e.g. 100k)
  size_t max_field_bytes;             ///< Maximum field size in bytes (0 = default, e.g. 16MB)
  size_t max_record_bytes;            ///< Maximum record size in bytes (0 = default, e.g. 64MB)
  size_t max_total_bytes;             ///< Maximum total input size (0 = default, e.g. 1GB)

  // Error context
  bool enable_context_snippet;        ///< Generate context snippet for errors (default true)
  size_t context_radius_bytes;       ///< Bytes before/after error in snippet (default 40)
} text_csv_parse_options;

/**
 * @brief CSV write options structure
 *
 * Controls serialization behavior including dialect, quoting rules, and formatting.
 */
typedef struct {
  text_csv_dialect dialect;          ///< CSV dialect configuration
  const char* newline;                ///< Newline string for output (default "\n" or "\r\n" per dialect)
  bool quote_all_fields;              ///< Quote all fields (default false)
  bool quote_empty_fields;            ///< Quote empty fields (default true)
  bool quote_if_needed;               ///< Quote fields containing delimiter/quote/newline (default true)
  bool always_escape_quotes;          ///< Always escape quotes (default: depends on escape mode)
  bool trailing_newline;              ///< Add trailing newline at end (default false)
} text_csv_write_options;

/**
 * @brief Initialize dialect with strict CSV defaults
 *
 * Returns a dialect structure with:
 * - Comma delimiter
 * - Double quote character
 * - Doubled quote escaping
 * - Standard newline handling
 * - Strict mode (no extensions)
 *
 * @return Initialized dialect structure
 */
TEXT_API text_csv_dialect text_csv_dialect_default(void);

/**
 * @brief Initialize parse options with strict CSV defaults
 *
 * Returns a parse options structure with:
 * - Strict CSV dialect
 * - UTF-8 validation enabled
 * - In-situ mode disabled
 * - BOM stripping enabled
 * - All limits set to 0 (library defaults)
 * - Context snippets enabled
 *
 * @return Initialized parse options structure
 */
TEXT_API text_csv_parse_options text_csv_parse_options_default(void);

/**
 * @brief Initialize write options with standard defaults
 *
 * Returns a write options structure with:
 * - Standard dialect
 * - Quote-if-needed policy
 * - No trailing newline
 *
 * @return Initialized write options structure
 */
TEXT_API text_csv_write_options text_csv_write_options_default(void);

/**
 * @brief Free the context snippet in an error structure
 *
 * Frees the dynamically allocated context snippet in a text_csv_error
 * structure. This should be called when the error structure is no longer
 * needed to prevent memory leaks. Other fields (message) are static strings
 * and should not be freed.
 *
 * @param err Error structure to clean up (can be NULL, in which case this is a no-op)
 */
TEXT_API void text_csv_error_free(text_csv_error* err);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_CORE_H */
