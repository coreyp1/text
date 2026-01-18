/**
 * @file json_internal.h
 * @brief Internal definitions for JSON module implementation
 *
 * This header contains internal-only definitions used by the JSON module
 * implementation. It should not be included by external code.
 */

#ifndef GHOTI_IO_TEXT_JSON_INTERNAL_H
#define GHOTI_IO_TEXT_JSON_INTERNAL_H

#include <text/json.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Position tracking structure for string processing
 */
typedef struct {
    size_t offset;       ///< Byte offset from start
    int line;            ///< Line number (1-based)
    int col;             ///< Column number (1-based, byte-based)
} json_position;

/**
 * @brief UTF-8 handling mode
 */
typedef enum {
    JSON_UTF8_REJECT,    ///< Reject invalid UTF-8 sequences
    JSON_UTF8_REPLACE,   ///< Replace invalid sequences with replacement character
    JSON_UTF8_VERBATIM   ///< Allow invalid sequences verbatim
} json_utf8_mode;

/**
 * @brief Decode a JSON string with escape sequences
 *
 * This function decodes a JSON string, handling:
 * - Standard escape sequences (\", \\, \/, \b, \f, \n, \r, \t)
 * - Unicode escapes (\uXXXX)
 * - Surrogate pairs (\uD83D\uDE00)
 *
 * The function performs bounds checking to prevent buffer overflow.
 * If the decoded string would exceed the output buffer capacity,
 * TEXT_JSON_E_LIMIT is returned.
 *
 * @param input Input string (without surrounding quotes)
 * @param input_len Length of input
 * @param output Output buffer
 * @param output_capacity Capacity of output buffer in bytes
 * @param output_len Output: length of decoded string
 * @param pos Input/output: position tracking (can be NULL)
 * @param validate_utf8 Whether to validate UTF-8
 * @param utf8_mode UTF-8 handling mode if validation fails
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_decode_string(
    const char* input,
    size_t input_len,
    char* output,
    size_t output_capacity,
    size_t* output_len,
    json_position* pos,
    int validate_utf8,
    json_utf8_mode utf8_mode
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_INTERNAL_H */
