/**
 * @file yaml_core.h
 * @brief Core types, status codes, and options for the YAML module.
 *
 * This header defines the public error/status codes, the error reporting
 * structure, the primary node/document forward declarations and the
 * parse/write option types and their default constructors.
 *
 * All public APIs in the YAML module return @ref GTEXT_YAML_Status where
 * 0 indicates success (GTEXT_YAML_OK) and non-zero values indicate
 * different failure modes.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_TEXT_YAML_CORE_H
#define GHOTI_IO_TEXT_YAML_CORE_H

#include <ghoti.io/text/macros.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum GTEXT_YAML_Status
 * @brief Status codes returned by YAML APIs.
 *
 * - GTEXT_YAML_OK: Success.
 * - GTEXT_YAML_E_INVALID: Generic parse/validation error.
 * - GTEXT_YAML_E_OOM: Out of memory.
 * - GTEXT_YAML_E_LIMIT: A configured limit was exceeded.
 * - GTEXT_YAML_E_DEPTH: Maximum nesting depth exceeded.
 * - GTEXT_YAML_E_INCOMPLETE: More input required to complete parsing.
 * - GTEXT_YAML_E_BAD_TOKEN: Unexpected token encountered by the scanner.
 * - GTEXT_YAML_E_BAD_ESCAPE: Invalid escape sequence in a quoted scalar.
 * - GTEXT_YAML_E_DUPKEY: Duplicate mapping key (policy may vary by options).
 * - GTEXT_YAML_E_WRITE: Sink/write error during serialization.
 * - GTEXT_YAML_E_STATE: Operation not valid in current parser/writer state.
 */
typedef enum {
  GTEXT_YAML_OK = 0,
  GTEXT_YAML_E_INVALID,
  GTEXT_YAML_E_OOM,
  GTEXT_YAML_E_LIMIT,
  GTEXT_YAML_E_DEPTH,
  GTEXT_YAML_E_INCOMPLETE,
  GTEXT_YAML_E_BAD_TOKEN,
  GTEXT_YAML_E_BAD_ESCAPE,
  GTEXT_YAML_E_DUPKEY,
  GTEXT_YAML_E_WRITE,
  GTEXT_YAML_E_STATE
} GTEXT_YAML_Status;

/**
 * @struct GTEXT_YAML_Error
 * @brief Rich error payload returned/filled by YAML operations.
 *
 * Fields:
 * - code: status code (see @ref GTEXT_YAML_Status).
 * - message: human-readable message (owned by caller or static; do not free).
 * - offset/line/col: location in the input where the error occurred.
 * - context_snippet: optional heap-allocated snippet to show nearby input; freed by gtext_yaml_error_free().
 * - caret_offset: position within context_snippet of the error location.
 * - expected_token/actual_token: optional textual tokens to aid diagnostics.
 */
typedef struct {
  GTEXT_YAML_Status code;
  const char * message;
  size_t offset;
  int line;
  int col;
  char * context_snippet;
  size_t context_snippet_len;
  size_t caret_offset;
  const char * expected_token;
  const char * actual_token;
} GTEXT_YAML_Error;

/**
 * @enum GTEXT_YAML_Node_Type
 * @brief Node types present in the YAML DOM.
 */
typedef enum {
  GTEXT_YAML_NULL,
  GTEXT_YAML_BOOL,
  GTEXT_YAML_INT,
  GTEXT_YAML_FLOAT,
  GTEXT_YAML_STRING,
  GTEXT_YAML_SEQUENCE,
  GTEXT_YAML_MAPPING,
  GTEXT_YAML_ALIAS
} GTEXT_YAML_Node_Type;

typedef struct GTEXT_YAML_Node GTEXT_YAML_Node;
typedef struct GTEXT_YAML_Document GTEXT_YAML_Document;

typedef enum { GTEXT_YAML_DUPKEY_ERROR, GTEXT_YAML_DUPKEY_FIRST_WINS, GTEXT_YAML_DUPKEY_LAST_WINS } GTEXT_YAML_Dupkey_Mode;

/**
 * @enum GTEXT_YAML_Scalar_Style
 * @brief Preferred scalar style for YAML emission.
 */
typedef enum {
  GTEXT_YAML_SCALAR_STYLE_PLAIN,
  GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED,
  GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED,
  GTEXT_YAML_SCALAR_STYLE_LITERAL,
  GTEXT_YAML_SCALAR_STYLE_FOLDED
} GTEXT_YAML_Scalar_Style;

/**
 * @enum GTEXT_YAML_Flow_Style
 * @brief Preferred collection style for YAML emission.
 */
typedef enum {
  GTEXT_YAML_FLOW_STYLE_AUTO,
  GTEXT_YAML_FLOW_STYLE_BLOCK,
  GTEXT_YAML_FLOW_STYLE_FLOW
} GTEXT_YAML_Flow_Style;

/**
 * @struct GTEXT_YAML_Parse_Options
 * @brief Options that control parsing behavior and limits.
 *
 * All size limits use 0 to denote "use the library default".
 */
typedef struct {
  /* Limits and behavior */
  GTEXT_YAML_Dupkey_Mode dupkeys;
  size_t max_depth;
  size_t max_total_bytes;
  size_t max_alias_expansion;

  /* Toggles */
  bool validate_utf8;
  bool resolve_tags;
  bool retain_comments;
} GTEXT_YAML_Parse_Options;

/**
 * @struct GTEXT_YAML_Write_Options
 * @brief Options controlling document emission/serialization.
 */
typedef struct {
  bool pretty;
  int indent_spaces;
  int line_width;
  const char * newline;
  bool trailing_newline;
  bool canonical;
  GTEXT_YAML_Scalar_Style scalar_style;
  GTEXT_YAML_Flow_Style flow_style;
} GTEXT_YAML_Write_Options;

/**
 * @brief Return a parse options object initialized to sensible defaults.
 *
 * The returned struct is by-value; callers may modify fields before
 * passing to APIs that accept @ref GTEXT_YAML_Parse_Options *.
 */
GTEXT_API GTEXT_YAML_Parse_Options gtext_yaml_parse_options_default(void);

/**
 * @brief Return write options initialized to sensible defaults.
 */
GTEXT_API GTEXT_YAML_Write_Options gtext_yaml_write_options_default(void);

/**
 * @brief Free any heap owned members inside @p err and zero the structure.
 *
 * Safe to call with a NULL pointer.
 */
GTEXT_API void gtext_yaml_error_free(GTEXT_YAML_Error * err);

/**
 * @brief Free a parsed YAML document and all associated memory.
 *
 * After this call the @p doc pointer must not be used. Passing NULL is a no-op.
 */
GTEXT_API void gtext_yaml_free(GTEXT_YAML_Document * doc);

/**
 * @brief Parse a YAML file into a document.
 */
GTEXT_API GTEXT_YAML_Document * gtext_yaml_parse_file(
  const char * path,
  const GTEXT_YAML_Parse_Options * options,
  GTEXT_YAML_Error * out_err
);

/**
 * @brief Parse a YAML file into an array of documents.
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_parse_file_all(
  const char * path,
  const GTEXT_YAML_Parse_Options * options,
  GTEXT_YAML_Document *** out_docs,
  size_t * out_count,
  GTEXT_YAML_Error * out_err
);

/**
 * @brief Write a YAML document to a file.
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_write_file(
  const char * path,
  const GTEXT_YAML_Document * doc,
  const GTEXT_YAML_Write_Options * options,
  GTEXT_YAML_Error * out_err
);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_TEXT_YAML_CORE_H
