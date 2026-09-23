/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Core CSV types and definitions.
 *
 * This header provides the core types, enums, and option structures for the CSV
 * module. It does not include the full API headers. Use this for internal
 * implementations that only need type definitions.
 *
 * For the full CSV API, include <ghoti.io/text/csv.h> instead.
 */

#ifndef GHOTI_IO_GTEXT_CSV_CSV_CORE_H
#define GHOTI_IO_GTEXT_CSV_CSV_CORE_H

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
  GTEXT_CSV_OK = 0,

  // General errors
  GTEXT_CSV_E_INVALID, ///< Invalid input or operation
  GTEXT_CSV_E_OOM,     ///< Out of memory
  GTEXT_CSV_E_LIMIT,   ///< Resource limit exceeded

  // Parsing errors
  GTEXT_CSV_E_UNTERMINATED_QUOTE, ///< Unterminated quoted field (EOF inside
                                  ///< quotes)
  GTEXT_CSV_E_INVALID_ESCAPE, ///< Invalid escape sequence (for backslash-escape
                              ///< dialect)
  GTEXT_CSV_E_UNEXPECTED_QUOTE, ///< Unexpected quote in unquoted field (when
                                ///< disallowed)
  GTEXT_CSV_E_INVALID_UTF8,     ///< UTF-8 validation failure (when enabled)
  GTEXT_CSV_E_INCOMPLETE_CRLF,  ///< Incomplete CRLF sequence (strict CRLF-only
                                ///< dialect)
  GTEXT_CSV_E_TOO_MANY_COLS,    ///< Too many columns in record
  GTEXT_CSV_E_TOO_MANY_ROWS,    ///< Too many rows

  // Writing errors
  GTEXT_CSV_E_WRITE, ///< Write operation failed
  GTEXT_CSV_E_STATE, ///< Invalid state for operation

  /// A field cannot be written under the quoting the options ask for.
  ///
  /// Appended rather than grouped with the other writing errors so that no
  /// existing enumerator's value moves.
  ///
  /// An unquoted field cannot contain the delimiter, a CR or an LF: no escape
  /// mode covers them, so the bytes would re-read as more fields or more
  /// records than were written. Nor can it contain the quote character while
  /// `always_escape_quotes` is set, because the escape it would emit means a
  /// quote to no reader. The write stops with this status instead of emitting
  /// a document that says something other than the table did.
  GTEXT_CSV_E_UNQUOTABLE_FIELD
} GTEXT_CSV_Status;

/**
 * @brief CSV error information
 *
 * Contains detailed error information including code, message, position,
 * and optional enhanced diagnostics (context snippet, caret positioning).
 */
typedef struct {
  GTEXT_CSV_Status code; ///< Error code
  const char * message;  ///< Human-readable error message.  Usually a static
                         ///< string; when @ref message_is_owned is true it is
                         ///< heap-allocated and released by
                         ///< gtext_csv_error_free().
  bool message_is_owned; ///< True when @ref message must be freed by
                         ///< gtext_csv_error_free().  A caller that copies the
                         ///< error struct and frees both copies must clear
                         ///< this on one of them.
  size_t byte_offset;    ///< Byte offset from start of input (0-based)
  int line;              ///< Line number (1-based)
  int column;            ///< Column number (1-based, byte-based)
  size_t row_index;      ///< Row index (0-based, first data row is 0)
  size_t col_index;      ///< Column index (0-based)

  // Enhanced error reporting (optional, may be NULL)
  char * context_snippet;     ///< Context snippet around error (dynamically
                              ///< allocated, caller must free)
  size_t context_snippet_len; ///< Length of context snippet
  size_t
      caret_offset; ///< Byte offset of caret within context snippet (0-based)
} GTEXT_CSV_Error;

/**
 * @brief Escape mode for CSV dialect
 */
typedef enum {
  GTEXT_CSV_ESCAPE_DOUBLED_QUOTE, ///< Escape quotes by doubling ("") (default)
  GTEXT_CSV_ESCAPE_BACKSLASH,     ///< Escape quotes with backslash (\")
  GTEXT_CSV_ESCAPE_NONE           ///< No escaping (not recommended)
} GTEXT_CSV_Escape_Mode;

/**
 * @brief Duplicate column name handling mode
 */
typedef enum {
  GTEXT_CSV_DUPCOL_ERROR,      ///< Fail parse on duplicate column name
  GTEXT_CSV_DUPCOL_FIRST_WINS, ///< Use first occurrence of duplicate column
                               ///< (default)
  GTEXT_CSV_DUPCOL_LAST_WINS,  ///< Use last occurrence of duplicate column
  GTEXT_CSV_DUPCOL_COLLECT     ///< Store all indices for duplicate columns
} GTEXT_CSV_Dupcol_Mode;

/**
 * @brief CSV dialect structure
 *
 * Defines the exact format rules for parsing and writing CSV.
 */
typedef struct {
  char delimiter;               ///< Field delimiter (default ',')
  char quote;                   ///< Quote character (default '"')
  GTEXT_CSV_Escape_Mode escape; ///< Escape mode (default DOUBLED_QUOTE)
  bool
      newline_in_quotes; ///< Allow newlines inside quoted fields (default true)
  bool accept_lf;        ///< Accept LF as newline (default true)
  bool accept_crlf;      ///< Accept CRLF as newline (default true)
  bool accept_cr;        ///< Accept CR as newline (default false)
  bool trim_unquoted_fields; ///< Trim whitespace from unquoted fields (default
                             ///< false)
  bool allow_space_after_delimiter; ///< Allow spaces after delimiter (default
                                    ///< false)
  bool allow_unquoted_quotes;   ///< Allow quotes in unquoted fields (default
                                ///< false)
  bool allow_unquoted_newlines; ///< Allow newlines in unquoted fields (default
                                ///< false)
  bool allow_comments;          ///< Allow comment lines (default false)
  const char * comment_prefix;  ///< Comment prefix string (default "#")
  bool treat_first_row_as_header; ///< Treat first row as header (default false)
  GTEXT_CSV_Dupcol_Mode
      header_dup_mode; ///< Duplicate column name handling (default FIRST_WINS)
} GTEXT_CSV_Dialect;

/**
 * @brief CSV parse options structure
 *
 * Controls parsing behavior including dialect, limits, and error reporting.
 */
typedef struct {
  GTEXT_CSV_Dialect dialect; ///< CSV dialect configuration
  bool validate_utf8;        ///< Validate UTF-8 sequences (default true)
  bool in_situ_mode; ///< Zero-copy mode: reference input buffer directly
                     ///< (default false)
  bool keep_bom;     ///< Keep UTF-8 BOM (default false, strips BOM if false)

  // Limits (0 => library default)
  size_t max_rows; ///< Maximum number of rows (0 = default, e.g. 10M)
  size_t
      max_cols; ///< Maximum number of columns per row (0 = default, e.g. 100k)
  size_t
      max_field_bytes; ///< Maximum field size in bytes (0 = default, e.g. 16MB)
  size_t max_record_bytes; ///< Maximum record size in bytes (0 = default, e.g.
                           ///< 64MB)
  size_t max_total_bytes;  ///< Maximum total input size (0 = default, e.g. 1GB)

  // Error context
  bool enable_context_snippet; ///< Generate context snippet for errors (default
                               ///< true).  When false no snippet is allocated
                               ///< and GTEXT_CSV_Error::context_snippet stays
                               ///< NULL.
  size_t context_radius_bytes; ///< Bytes before and after the error position to
                               ///< include in the snippet (0 = library
                               ///< default, 40)
} GTEXT_CSV_Parse_Options;

/**
 * @brief When the writer puts quotes around a field
 *
 * The four policies Python's `csv` module names, which is where callers will
 * have met them. `GTEXT_CSV_QUOTE_MINIMAL` is zero, so a zero-initialized
 * GTEXT_CSV_Write_Options keeps the behavior this writer has always had.
 *
 * The older booleans still work and are not going away. `quote_all_fields`
 * wins over this field when set, so code written before this enum existed
 * behaves as it did; the rest are read where the policy leaves room for them,
 * which each value below states.
 */
typedef enum {
  /// Quote only where the field requires it: it holds the delimiter, the quote
  /// character, a CR or an LF. `quote_empty_fields` and `quote_if_needed` both
  /// apply. This is the default and is RFC 4180's own rule.
  GTEXT_CSV_QUOTE_MINIMAL = 0,

  /// Quote every field, for a consumer fussier than the format.
  /// `quote_empty_fields` and `quote_if_needed` are not consulted.
  GTEXT_CSV_QUOTE_ALL,

  /// Quote every field whose text does not spell a number.
  ///
  /// **This is a question about the bytes, not about a type.** Nothing in this
  /// module infers types - fields are bytes, deliberately - so where Python
  /// asks whether the value it holds is an int or a float, this asks whether
  /// the text would be read as a number. The grammar is exactly:
  ///
  ///     [+-]? ( digits ( '.' digits? )? | '.' digits ) ( [eE] [+-]? digits )?
  ///
  /// and it must match the whole field. No hex, no `inf`, no `nan`, no
  /// surrounding space, and an empty field is not a number. `+1`, `007`, `1.`
  /// and `.5` are, because spreadsheets write them.
  ///
  /// A field this policy would leave bare is still quoted when it needs to be,
  /// so the policy can only ever add quotes. That matters when the dialect's
  /// delimiter is one of the characters a number can contain - a `.` delimiter
  /// makes `1.5` both numeric and unwritable bare.
  GTEXT_CSV_QUOTE_NONNUMERIC,

  /// Never quote. `quote_empty_fields` and `quote_if_needed` are not
  /// consulted.
  ///
  /// A field that unquoted bytes cannot carry is refused with
  /// GTEXT_CSV_E_UNQUOTABLE_FIELD rather than written wrongly - see that
  /// status. Python raises in the same situation; there is no escape character
  /// here that would be the alternative, because both escape modes concern the
  /// quote character alone.
  GTEXT_CSV_QUOTE_NONE
} GTEXT_CSV_Quoting;

/**
 * @brief CSV write options structure
 *
 * Controls serialization behavior including dialect, quoting rules, and
 * formatting.
 */
typedef struct {
  GTEXT_CSV_Dialect dialect; ///< CSV dialect configuration
  const char * newline;  ///< Newline string for output (default "\n" or "\r\n"
                         ///< per dialect)
  bool quote_all_fields; ///< Quote all fields (default false)
  bool quote_empty_fields; ///< Quote empty fields (default true)
  bool quote_if_needed;    ///< Quote fields containing delimiter/quote/newline
                           ///< (default true)
  /**
   * What to do with a quote character in a field that is *not* being quoted.
   * Default true.
   *
   * Set, such a field is refused with GTEXT_CSV_E_UNQUOTABLE_FIELD. It used to
   * be escaped, which had no correct reading: RFC 4180 gives a quote inside an
   * unquoted field no special meaning, so unquoted `a""b` is the four
   * characters `a""b` rather than `a"b` - the escape changed the value - and a
   * reader with the default `allow_unquoted_quotes` refuses those bytes
   * anyway.
   *
   * Cleared, the quote is emitted verbatim. That is readable by a parser with
   * `allow_unquoted_quotes` on, and is a deliberate interoperability choice
   * this library leaves to the caller.
   *
   * Quotes inside quoted fields are always escaped regardless of this option,
   * since leaving one unescaped would end the field early. No effect when the
   * dialect's escape mode is GTEXT_CSV_ESCAPE_NONE.
   */
  bool always_escape_quotes;
  bool trailing_newline;     ///< Terminate the final record with a newline
                             ///< (default true).  Records are always
                             ///< separated by one; this decides whether the
                             ///< last one is followed by one too.  An empty
                             ///< table writes nothing either way, since it has
                             ///< no record to terminate.
  bool trim_trailing_empty_fields; ///< Trim trailing empty fields from rows
                                   ///< (default false)

  /// When to put quotes around a field. Default GTEXT_CSV_QUOTE_MINIMAL, which
  /// is zero, so this is what a zero-initialized structure already asked for.
  ///
  /// Appended rather than grouped with the quoting booleans above so that the
  /// offset of every field that was already here stays where it was.
  GTEXT_CSV_Quoting quoting;
} GTEXT_CSV_Write_Options;

/**
 * @brief Initialize dialect with strict CSV defaults
 *
 * Returns a dialect structure with:
 * - Comma delimiter
 * - Double quote character
 * - Doubled quote escaping
 * - Standard newline handling
 * - Strict mode (no extensions)
 * - Duplicate header names allowed by default (header_dup_mode = FIRST_WINS)
 *
 * @return Initialized dialect structure
 */
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_default(void);

/**
 * @brief Get a tab-separated-values dialect
 *
 * The default dialect with a tab delimiter. TSV in the wild is usually
 * written without quoting at all, on the assumption that fields contain no
 * tabs; this dialect still honors `"` quoting when it is present, so it reads
 * both the quoted and unquoted conventions.
 *
 * @return TSV dialect
 */
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_tsv(void);

/**
 * @brief Get a semicolon-delimited dialect
 *
 * The default dialect with a `;` delimiter. This is what spreadsheet programs
 * export in locales where `,` is the decimal separator, so it is the common
 * shape of a European CSV file.
 *
 * @return Semicolon-delimited dialect
 */
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_semicolon(void);

/**
 * @brief Get a dialect that escapes with a backslash
 *
 * The default dialect with GTEXT_CSV_ESCAPE_BACKSLASH, so a quote inside a
 * quoted field is written `\"` rather than `""`. This is the MySQL and
 * PostgreSQL convention rather than the RFC 4180 one.
 *
 * @return Backslash-escape dialect
 */
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_backslash_escape(void);

/**
 * @brief Get a dialect matching Microsoft Excel's export
 *
 * Comma delimiter, doubled-quote escaping, and CRLF line endings on write.
 * Equivalent to Python's `csv` "excel" dialect, and to RFC 4180 proper.
 * Reading accepts LF as well, because files move between platforms.
 *
 * @return Excel dialect
 */
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_excel(void);

/**
 * @brief Get a permissive dialect for hand-written input
 *
 * Trims whitespace around unquoted fields, allows a space after a delimiter,
 * tolerates a bare quote inside an unquoted field, accepts a lone CR as a line
 * ending, and treats `#` lines as comments.
 *
 * Every one of those relaxations can change what a document means, so this is
 * for input a person typed rather than input a machine produced. Use
 * gtext_csv_dialect_default() for anything that has to round-trip.
 *
 * @return Permissive dialect
 */
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_permissive(void);

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
GTEXT_API GTEXT_CSV_Parse_Options gtext_csv_parse_options_default(void);

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
GTEXT_API GTEXT_CSV_Write_Options gtext_csv_write_options_default(void);

/**
 * @brief Free the context snippet in an error structure
 *
 * Frees the dynamically allocated context snippet in a GTEXT_CSV_Error
 * structure. This should be called when the error structure is no longer
 * needed to prevent memory leaks. Other fields (message) are static strings
 * and should not be freed.
 *
 * @param err Error structure to clean up (can be NULL, in which case this is a
 * no-op)
 */
GTEXT_API void gtext_csv_error_free(GTEXT_CSV_Error * err);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_CSV_CSV_CORE_H
