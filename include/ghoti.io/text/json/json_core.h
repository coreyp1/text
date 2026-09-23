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
 * Core JSON types and definitions.
 *
 * This header provides the core types, enums, and option structures for the
 * JSON module. It does not include the full API headers. Use this for internal
 * implementations that only need type definitions.
 *
 * For the full JSON API, include <ghoti.io/text/json.h> instead.
 */

#ifndef GHOTI_IO_GTEXT_JSON_JSON_CORE_H
#define GHOTI_IO_GTEXT_JSON_JSON_CORE_H

#include <ghoti.io/text/allocator.h>
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
  GTEXT_JSON_OK = 0,

  // General errors
  GTEXT_JSON_E_INVALID,    ///< Invalid input or operation
  GTEXT_JSON_E_OOM,        ///< Out of memory
  GTEXT_JSON_E_LIMIT,      ///< Resource limit exceeded
  GTEXT_JSON_E_DEPTH,      ///< Maximum nesting depth exceeded
  GTEXT_JSON_E_INCOMPLETE, ///< Incomplete input

  // Lexing / parsing specific errors
  GTEXT_JSON_E_BAD_TOKEN,        ///< Invalid token encountered
  GTEXT_JSON_E_BAD_NUMBER,       ///< Invalid number format
  GTEXT_JSON_E_BAD_ESCAPE,       ///< Invalid escape sequence
  GTEXT_JSON_E_BAD_UNICODE,      ///< Invalid Unicode sequence
  GTEXT_JSON_E_TRAILING_GARBAGE, ///< Trailing garbage after valid JSON

  // Semantics / policy errors
  GTEXT_JSON_E_DUPKEY,    ///< Duplicate key in object (when policy is ERROR)
  GTEXT_JSON_E_NONFINITE, ///< Non-finite number when not allowed
  GTEXT_JSON_E_SCHEMA,    ///< Schema validation error

  // Writer errors
  GTEXT_JSON_E_WRITE, ///< Write operation failed
  GTEXT_JSON_E_STATE, ///< Invalid state for operation

  /// Schema uses a standard keyword this implementation does not enforce.
  /// Appended rather than grouped with GTEXT_JSON_E_SCHEMA so that the
  /// numeric value of every pre-existing constant is unchanged.
  GTEXT_JSON_E_SCHEMA_UNSUPPORTED
} GTEXT_JSON_Status;

/**
 * @brief JSON error information
 *
 * Contains detailed error information including code, message, position,
 * and optional enhanced diagnostics (context snippet, caret positioning,
 * expected/actual token descriptions).
 */
typedef struct {
  GTEXT_JSON_Status code; ///< Error code
  const char * message;   ///< Human-readable error message (static string)
  size_t offset;          ///< Byte offset from start of input (0-based)
  int line;               ///< Line number (1-based)
  int col;                ///< Column number (1-based, byte-based for v1)

  // Enhanced error reporting (optional, may be NULL)
  char * context_snippet;     ///< Context snippet around error (dynamically
                              ///< allocated, caller must free)
  size_t context_snippet_len; ///< Length of context snippet
  size_t
      caret_offset; ///< Byte offset of caret within context snippet (0-based)
  const char * expected_token; ///< Description of expected token (static
                               ///< string, may be NULL)
  const char * actual_token;   ///< Description of actual token encountered
                               ///< (static string, may be NULL)
} GTEXT_JSON_Error;

/**
 * @brief JSON value type enumeration
 */
typedef enum {
  GTEXT_JSON_NULL,   ///< null value
  GTEXT_JSON_BOOL,   ///< boolean value (true/false)
  GTEXT_JSON_NUMBER, ///< number value
  GTEXT_JSON_STRING, ///< string value
  GTEXT_JSON_ARRAY,  ///< array value
  GTEXT_JSON_OBJECT  ///< object value
} GTEXT_JSON_Type;

/**
 * @brief Forward declaration of JSON value structure
 *
 * The actual structure is defined internally. Values are allocated from
 * an arena and freed via gtext_json_free().
 */
typedef struct GTEXT_JSON_Value GTEXT_JSON_Value;

/**
 * @brief Duplicate key handling mode
 */
typedef enum {
  GTEXT_JSON_DUPKEY_ERROR,      ///< Fail parse on duplicate key
  GTEXT_JSON_DUPKEY_FIRST_WINS, ///< Use first occurrence of duplicate key
  GTEXT_JSON_DUPKEY_LAST_WINS,  ///< Use last occurrence of duplicate key
  GTEXT_JSON_DUPKEY_COLLECT     ///< Store duplicates as array (key -> array of
                                ///< values)
} GTEXT_JSON_Dupkey_Mode;

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
  bool allow_unescaped_controls; ///< Allow unescaped control characters
                                 ///< (relaxed mode)

  // Unicode / input handling
  bool allow_leading_bom; ///< Allow leading UTF-8 BOM (default: on)
  bool validate_utf8;     ///< Validate UTF-8 sequences (default: on)
  /**
   * Normalize every string to Unicode NFC. Default: off.
   *
   * Applies to object names as well as values, which is what makes
   * duplicate-name detection meaningful: `{"\u00e9":1,"e\u0301":2}` is one
   * name written twice, and with this off it is two. Normalization happens
   * before the parser compares names, so dupkeys sees the normalized form.
   *
   * Two interactions, both deliberate:
   *
   * - **Requires validate_utf8**, which is on by default. Normalizing bytes
   *   that have not been established as text is not defined; asking for one
   *   without the other fails the parse with GTEXT_JSON_E_INVALID rather than
   *   normalizing some strings and not others.
   * - **Disables in_situ_mode for strings.** Normalization has to copy, and
   *   the length equality in-situ tests for is not evidence the bytes are
   *   unchanged - canonical ordering reorders combining marks without changing
   *   how many bytes they occupy. Numbers are still referenced in place.
   *
   * A string that is not well-formed UTF-8 fails with
   * GTEXT_JSON_E_BAD_UNICODE, the same status the validator uses.
   */
  bool normalize_unicode;
  bool in_situ_mode;      ///< Zero-copy mode: reference input buffer directly

  /**
   * Allocator for everything the parse produces, or NULL for
   * gtext_allocator_default().
   *
   * The DOM records it, so gtext_json_free() releases through the same
   * allocator without the caller passing it again. It must stay valid for
   * the lifetime of the value the parse returns.
   *
   * Covered: gtext_json_parse(), gtext_json_parse_multiple() and
   * gtext_json_parse_file() - the arena and every DOM node, key and string in
   * it, the preserved number lexemes, and the parser's transient buffers.
   *
   * Not covered, and still using the C library:
   * - `GTEXT_JSON_Error::context_snippet`, because gtext_json_error_free()
   *   receives only the error and has no way to learn which allocator made
   *   it. Freeing it through a mismatched allocator would be worse than the
   *   one diagnostic allocation it avoids.
   * - The writer, the streaming parser, JSON Pointer, Patch and Schema, none
   *   of which takes an allocator yet.
   *
   * Nothing silently falls back: `make check-allocators` fails the build if a
   * file on the covered list calls malloc, calloc, realloc or free directly.
   */
  const GTEXT_Allocator * allocator;
                          ///< (default: off)

  // Duplicate keys
  GTEXT_JSON_Dupkey_Mode dupkeys; ///< Duplicate key handling policy

  // Limits (0 => library default)
  size_t max_depth;        ///< Maximum nesting depth (0 = default, e.g. 256)
  size_t max_string_bytes; ///< Maximum string size in bytes (0 = default, e.g.
                           ///< 16MB)
  size_t max_container_elems; ///< Maximum array/object elements (0 = default,
                              ///< e.g. 1M)
  size_t max_total_bytes; ///< Maximum total input size (0 = default, e.g. 64MB)

  // Number fidelity / representations
  bool
      preserve_number_lexeme; ///< Preserve original number token for round-trip
  bool parse_int64;           ///< Detect and parse exact int64 representation
  bool parse_uint64;          ///< Detect and parse exact uint64 representation
  bool parse_double; ///< Derive double representation when representable
} GTEXT_JSON_Parse_Options;

/**
 * @brief Floating-point formatting strategy
 */
typedef enum {
  GTEXT_JSON_FLOAT_SHORTEST, ///< Shortest representation (%.17g, default)
  GTEXT_JSON_FLOAT_FIXED, ///< Fixed-point notation (%.Nf, use float_precision)
  GTEXT_JSON_FLOAT_SCIENTIFIC ///< Scientific notation (%.Ne, use
                              ///< float_precision)
} GTEXT_JSON_Float_Format;

/**
 * @brief Write options structure
 *
 * Controls serialization behavior including formatting, escaping, and
 * canonical output options.
 */
typedef struct {
  // Formatting
  bool pretty;       ///< Pretty-print output (false = compact, true = pretty)
  int indent_spaces; ///< Number of spaces per indent level (e.g. 2, 4)
  const char * newline;   ///< Newline string ("\n" default, allow "\r\n")
  bool trailing_newline;  ///< Add trailing newline at end of output (default:
                          ///< false)
  bool space_after_colon; ///< Add space after ':' in objects (default: false)
  bool space_after_comma; ///< Add space after ',' in arrays/objects (default:
                          ///< false)
  int inline_array_threshold;  ///< Max elements for inline array (0=always
                               ///< pretty, -1=always inline, default: -1)
  int inline_object_threshold; ///< Max pairs for inline object (0=always
                               ///< pretty, -1=always inline, default: -1)

  // Escaping
  bool escape_solidus;       ///< Escape forward slash (optional)
  bool escape_unicode;       ///< Output \\uXXXX for non-ASCII (canonical mode)
  bool escape_all_non_ascii; ///< Escape all non-ASCII characters (stricter)

  // Canonical / deterministic
  bool sort_object_keys;  ///< Sort object keys for stable output
  bool canonical_numbers; ///< Normalize numeric lexemes (use with care)

  // Extensions
  bool allow_nonfinite_numbers; ///< Emit NaN/Infinity if node contains it

  // Floating-point formatting
  GTEXT_JSON_Float_Format
      float_format; ///< Floating-point formatting strategy (default: SHORTEST)
  int float_precision; ///< Precision for fixed/scientific format (default: 6,
                       ///< ignored for SHORTEST)
} GTEXT_JSON_Write_Options;

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
GTEXT_API GTEXT_JSON_Parse_Options gtext_json_parse_options_default(void);

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
GTEXT_API GTEXT_JSON_Write_Options gtext_json_write_options_default(void);

/**
 * @brief Free a JSON value and its entire DOM tree
 *
 * Frees the arena associated with the value, which deallocates
 * all nodes and strings in the DOM tree. After calling this function,
 * the value pointer and all pointers to values in the tree are invalid.
 *
 * @param v Value to free (can be NULL, in which case this is a no-op)
 */
GTEXT_API void gtext_json_free(GTEXT_JSON_Value * v);

/**
 * @brief Free the context snippet in an error structure
 *
 * Frees the dynamically allocated context snippet in a GTEXT_JSON_Error
 * structure. This should be called when the error structure is no longer
 * needed to prevent memory leaks. Other fields (message, expected_token,
 * actual_token) are static strings and should not be freed.
 *
 * @param err Error structure to clean up (can be NULL, in which case this is a
 * no-op)
 */
GTEXT_API void gtext_json_error_free(GTEXT_JSON_Error * err);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_JSON_JSON_CORE_H
