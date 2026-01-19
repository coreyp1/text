/**
 * @file json_core.h
 * @brief Core JSON types and definitions
 *
 * This header provides the core types, enums, and option structures for the JSON module.
 * It does not include the full API headers. Use this for internal implementations that
 * only need type definitions.
 *
 * For the full JSON API, include <ghoti.io/text/json.h> instead.
 */

#ifndef GHOTI_IO_TEXT_JSON_CORE_H
#define GHOTI_IO_TEXT_JSON_CORE_H

#include <ghoti.io/text/macros.h>
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
  TEXT_JSON_E_LIMIT,           ///< Resource limit exceeded
  TEXT_JSON_E_DEPTH,            ///< Maximum nesting depth exceeded
  TEXT_JSON_E_INCOMPLETE,       ///< Incomplete input

  // Lexing / parsing specific errors
  TEXT_JSON_E_BAD_TOKEN,        ///< Invalid token encountered
  TEXT_JSON_E_BAD_NUMBER,       ///< Invalid number format
  TEXT_JSON_E_BAD_ESCAPE,       ///< Invalid escape sequence
  TEXT_JSON_E_BAD_UNICODE,      ///< Invalid Unicode sequence
  TEXT_JSON_E_TRAILING_GARBAGE, ///< Trailing garbage after valid JSON

  // Semantics / policy errors
  TEXT_JSON_E_DUPKEY,           ///< Duplicate key in object (when policy is ERROR)
  TEXT_JSON_E_NONFINITE,        ///< Non-finite number when not allowed
  TEXT_JSON_E_SCHEMA,           ///< Schema validation error

  // Writer errors
  TEXT_JSON_E_WRITE,           ///< Write operation failed
  TEXT_JSON_E_STATE            ///< Invalid state for operation
} text_json_status;

/**
 * @brief JSON error information
 *
 * Contains detailed error information including code, message, position,
 * and optional enhanced diagnostics (context snippet, caret positioning,
 * expected/actual token descriptions).
 */
typedef struct {
  text_json_status code;      ///< Error code
  const char* message;        ///< Human-readable error message (static string)
  size_t offset;              ///< Byte offset from start of input (0-based)
  int line;                   ///< Line number (1-based)
  int col;                    ///< Column number (1-based, byte-based for v1)
  
  // Enhanced error reporting (optional, may be NULL)
  char* context_snippet;       ///< Context snippet around error (dynamically allocated, caller must free)
  size_t context_snippet_len; ///< Length of context snippet
  size_t caret_offset;        ///< Byte offset of caret within context snippet (0-based)
  const char* expected_token; ///< Description of expected token (static string, may be NULL)
  const char* actual_token;   ///< Description of actual token encountered (static string, may be NULL)
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
  int allow_comments;           ///< Allow JSONC comments (// and /* */)
  int allow_trailing_commas;    ///< Allow trailing commas in arrays/objects
  int allow_nonfinite_numbers;  ///< Allow NaN, Infinity, -Infinity
  int allow_single_quotes;      ///< Allow single-quoted strings (relaxed mode)
  int allow_unescaped_controls; ///< Allow unescaped control characters (relaxed mode)

  // Unicode / input handling
  int allow_leading_bom;        ///< Allow leading UTF-8 BOM (default: on)
  int validate_utf8;            ///< Validate UTF-8 sequences (default: on)
  int normalize_unicode;        ///< NFC normalization (v2 feature, default: off)
  int in_situ_mode;             ///< Zero-copy mode: reference input buffer directly (default: off)

  // Duplicate keys
  text_json_dupkey_mode dupkeys; ///< Duplicate key handling policy

  // Limits (0 => library default)
  size_t max_depth;             ///< Maximum nesting depth (0 = default, e.g. 256)
  size_t max_string_bytes;      ///< Maximum string size in bytes (0 = default, e.g. 16MB)
  size_t max_container_elems;   ///< Maximum array/object elements (0 = default, e.g. 1M)
  size_t max_total_bytes;       ///< Maximum total input size (0 = default, e.g. 64MB)

  // Number fidelity / representations
  int preserve_number_lexeme;   ///< Preserve original number token for round-trip
  int parse_int64;              ///< Detect and parse exact int64 representation
  int parse_uint64;             ///< Detect and parse exact uint64 representation
  int parse_double;             ///< Derive double representation when representable
  int allow_big_decimal;        ///< Store decimal as string-backed big-decimal
} text_json_parse_options;

/**
 * @brief Write options structure
 *
 * Controls serialization behavior including formatting, escaping, and
 * canonical output options.
 */
typedef struct {
  // Formatting
  int pretty;                   ///< Pretty-print output (0 = compact, 1 = pretty)
  int indent_spaces;            ///< Number of spaces per indent level (e.g. 2, 4)
  const char* newline;          ///< Newline string ("\n" default, allow "\r\n")

  // Escaping
  int escape_solidus;           ///< Escape forward slash (optional)
  int escape_unicode;           ///< Output \\uXXXX for non-ASCII (canonical mode)
  int escape_all_non_ascii;     ///< Escape all non-ASCII characters (stricter)

  // Canonical / deterministic
  int sort_object_keys;         ///< Sort object keys for stable output
  int canonical_numbers;        ///< Normalize numeric lexemes (use with care)
  int canonical_strings;       ///< Normalize string escapes

  // Extensions
  int allow_nonfinite_numbers;  ///< Emit NaN/Infinity if node contains it
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
 * - Compact output (pretty = 0)
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

/**
 * @brief Free the context snippet in an error structure
 *
 * Frees the dynamically allocated context snippet in a text_json_error
 * structure. This should be called when the error structure is no longer
 * needed to prevent memory leaks. Other fields (message, expected_token,
 * actual_token) are static strings and should not be freed.
 *
 * @param err Error structure to clean up (can be NULL, in which case this is a no-op)
 */
TEXT_API void text_json_error_free(text_json_error* err);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_CORE_H */
