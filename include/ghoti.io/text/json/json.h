/**
 * @file json.h
 * @brief JSON parsing and serialization
 *
 * This header provides the core types and error handling for the JSON module.
 * It serves as the umbrella header for JSON functionality.
 */

#ifndef GHOTI_IO_TEXT_JSON_H
#define GHOTI_IO_TEXT_JSON_H

#include <ghoti.io/text/macros.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JSON operation status codes
 */
typedef enum {
  TEXT_JSON_OK = 0,

  // General errors
  TEXT_JSON_E_INVALID,        ///< Invalid input or operation
  TEXT_JSON_E_OOM,            ///< Out of memory
  TEXT_JSON_E_LIMIT,          ///< Resource limit exceeded
  TEXT_JSON_E_DEPTH,          ///< Maximum nesting depth exceeded
  TEXT_JSON_E_INCOMPLETE,     ///< Incomplete input

  // Lexing / parsing specific errors
  TEXT_JSON_E_BAD_TOKEN,      ///< Invalid token encountered
  TEXT_JSON_E_BAD_NUMBER,     ///< Invalid number format
  TEXT_JSON_E_BAD_ESCAPE,     ///< Invalid escape sequence
  TEXT_JSON_E_BAD_UNICODE,    ///< Invalid Unicode sequence
  TEXT_JSON_E_TRAILING_GARBAGE, ///< Trailing garbage after valid JSON

  // Semantics / policy errors
  TEXT_JSON_E_DUPKEY,         ///< Duplicate key in object (when policy is ERROR)
  TEXT_JSON_E_NONFINITE,      ///< Non-finite number when not allowed
  TEXT_JSON_E_SCHEMA,         ///< Schema validation error

  // Writer errors
  TEXT_JSON_E_WRITE,          ///< Write operation failed
  TEXT_JSON_E_STATE           ///< Invalid state for operation
} text_json_status;

/**
 * @brief JSON error information
 *
 * Contains detailed error information including code, message, and position.
 */
typedef struct {
  text_json_status code;      ///< Error code
  const char* message;        ///< Human-readable error message (static string)
  size_t offset;              ///< Byte offset from start of input (0-based)
  int line;                   ///< Line number (1-based)
  int col;                    ///< Column number (1-based, byte-based for v1)
} text_json_error;

/**
 * @brief JSON value type enumeration
 */
typedef enum {
  TEXT_JSON_NULL,             ///< null value
  TEXT_JSON_BOOL,             ///< boolean value (true/false)
  TEXT_JSON_NUMBER,           ///< number value
  TEXT_JSON_STRING,           ///< string value
  TEXT_JSON_ARRAY,            ///< array value
  TEXT_JSON_OBJECT            ///< object value
} text_json_type;

/**
 * @brief Forward declaration of JSON value structure
 *
 * The actual structure is defined internally. Values are allocated from
 * an arena and freed via text_json_free().
 */
typedef struct text_json_value text_json_value;

/**
 * @brief Duplicate key handling mode
 */
typedef enum {
  TEXT_JSON_DUPKEY_ERROR,      ///< Fail parse on duplicate key
  TEXT_JSON_DUPKEY_FIRST_WINS, ///< Use first occurrence of duplicate key
  TEXT_JSON_DUPKEY_LAST_WINS,  ///< Use last occurrence of duplicate key
  TEXT_JSON_DUPKEY_COLLECT     ///< Store duplicates as array (key -> array of values)
} text_json_dupkey_mode;

/**
 * @brief Parse options structure
 *
 * Controls parsing behavior including strictness, extensions, limits, and
 * number representation options.
 */
typedef struct {
  // Strictness / extensions
  bool allow_comments;           ///< Allow JSONC comments (// and /* */)
  bool allow_trailing_commas;    ///< Allow trailing commas in arrays/objects
  bool allow_nonfinite_numbers;  ///< Allow NaN, Infinity, -Infinity
  bool allow_single_quotes;      ///< Allow single-quoted strings (relaxed mode)
  bool allow_unescaped_controls; ///< Allow unescaped control characters (relaxed mode)

  // Unicode / input handling
  bool allow_leading_bom;        ///< Allow leading UTF-8 BOM (default: on)
  bool validate_utf8;            ///< Validate UTF-8 sequences (default: on)
  bool normalize_unicode;        ///< NFC normalization (v2 feature, default: off)

  // Duplicate keys
  text_json_dupkey_mode dupkeys; ///< Duplicate key handling policy

  // Limits (0 => library default)
  size_t max_depth;             ///< Maximum nesting depth (0 = default, e.g. 256)
  size_t max_string_bytes;      ///< Maximum string size in bytes (0 = default, e.g. 16MB)
  size_t max_container_elems;   ///< Maximum array/object elements (0 = default, e.g. 1M)
  size_t max_total_bytes;       ///< Maximum total input size (0 = default, e.g. 64MB)

  // Number fidelity / representations
  bool preserve_number_lexeme;  ///< Preserve original number token for round-trip
  bool parse_int64;             ///< Detect and parse exact int64 representation
  bool parse_uint64;            ///< Detect and parse exact uint64 representation
  bool parse_double;            ///< Derive double representation when representable
  bool allow_big_decimal;       ///< Store decimal as string-backed big-decimal
} text_json_parse_options;

/**
 * @brief Floating-point formatting strategy
 */
typedef enum {
  TEXT_JSON_FLOAT_SHORTEST,     ///< Shortest representation (%.17g, default)
  TEXT_JSON_FLOAT_FIXED,        ///< Fixed-point notation (%.Nf, use float_precision)
  TEXT_JSON_FLOAT_SCIENTIFIC    ///< Scientific notation (%.Ne, use float_precision)
} text_json_float_format;

/**
 * @brief Write options structure
 *
 * Controls serialization behavior including formatting, escaping, and
 * canonical output options.
 */
typedef struct {
  // Formatting
  bool pretty;                  ///< Pretty-print output (false = compact, true = pretty)
  int indent_spaces;            ///< Number of spaces per indent level (e.g. 2, 4)
  const char* newline;          ///< Newline string ("\n" default, allow "\r\n")
  bool trailing_newline;        ///< Add trailing newline at end of output (default: false)
  bool space_after_colon;       ///< Add space after ':' in objects (default: false)
  bool space_after_comma;       ///< Add space after ',' in arrays/objects (default: false)
  int inline_array_threshold;   ///< Max elements for inline array (0=always pretty, -1=always inline, default: -1)
  int inline_object_threshold;  ///< Max pairs for inline object (0=always pretty, -1=always inline, default: -1)

  // Escaping
  bool escape_solidus;          ///< Escape forward slash (optional)
  bool escape_unicode;          ///< Output \\uXXXX for non-ASCII (canonical mode)
  bool escape_all_non_ascii;    ///< Escape all non-ASCII characters (stricter)

  // Canonical / deterministic
  bool sort_object_keys;        ///< Sort object keys for stable output
  bool canonical_numbers;       ///< Normalize numeric lexemes (use with care)
  bool canonical_strings;       ///< Normalize string escapes

  // Extensions
  bool allow_nonfinite_numbers; ///< Emit NaN/Infinity if node contains it

  // Floating-point formatting
  text_json_float_format float_format;  ///< Floating-point formatting strategy (default: SHORTEST)
  int float_precision;          ///< Precision for fixed/scientific format (default: 6, ignored for SHORTEST)
} text_json_write_options;

/**
 * @brief Initialize parse options with strict JSON defaults
 *
 * Returns a parse options structure with:
 * - Strict JSON mode (all extensions off)
 * - UTF-8 validation enabled
 * - Number lexeme preservation enabled
 * - Duplicate key policy: ERROR
 * - All limits set to 0 (library defaults)
 *
 * @return Initialized parse options structure
 */
TEXT_API text_json_parse_options text_json_parse_options_default(void);

/**
 * @brief Initialize write options with compact output defaults
 *
 * Returns a write options structure with:
 * - Compact output (pretty = false)
 * - Standard escaping
 * - No canonical formatting
 *
 * @return Initialized write options structure
 */
TEXT_API text_json_write_options text_json_write_options_default(void);

/**
 * @brief Free a JSON value and its entire DOM tree
 *
 * Frees the arena associated with the value, which deallocates
 * all nodes and strings in the DOM tree. After calling this function,
 * the value pointer and all pointers to values in the tree are invalid.
 *
 * @param v Value to free (can be NULL, in which case this is a no-op)
 */
TEXT_API void text_json_free(text_json_value* v);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_H */
