/**
 * @file json_string_internal.h
 * @brief Internal helper functions for JSON string operations
 *
 * This header contains documentation for static helper functions used
 * internally within json_string.c. These functions are not part of the
 * public API and are only visible within json_string.c.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_JSON_STRING_INTERNAL_H
#define GHOTI_IO_GTEXT_JSON_STRING_INTERNAL_H

#include "json_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Escape Sequence Decoding
// ============================================================================

/**
 * @brief Decode a standard escape sequence
 *
 * @fn static int json_decode_escape(char c)
 *
 * @param c Escape character (e.g., 'n', 't', 'r')
 * @return Decoded character, or 0 if invalid
 *
 * @note This is a static function defined in json_string.c
 */

/**
 * @brief Decode a hex digit to its value
 *
 * @fn static int json_hex_digit(char c)
 *
 * @param c Hex digit character
 * @return Value (0-15), or -1 if invalid
 *
 * @note This is a static function defined in json_string.c
 */

/**
 * @brief Decode a \uXXXX Unicode escape sequence
 *
 * @fn static size_t json_decode_unicode_escape(const char * input, size_t len,
 * uint32_t * out_codepoint)
 *
 * @param input Pointer to the 'u' character
 * @param len Remaining length in input
 * @param out_codepoint Output codepoint value
 * @return Number of bytes consumed (6 if valid, 0 if invalid)
 *
 * @note This is a static function defined in json_string.c
 */

// ============================================================================
// Unicode Surrogate Handling
// ============================================================================

/**
 * @brief Check if a codepoint is a high surrogate
 *
 * @fn static int json_is_high_surrogate(uint32_t codepoint)
 *
 * @param codepoint Unicode codepoint
 * @return 1 if high surrogate, 0 otherwise
 *
 * @note This is a static function defined in json_string.c
 */

/**
 * @brief Check if a codepoint is a low surrogate
 *
 * @fn static int json_is_low_surrogate(uint32_t codepoint)
 *
 * @param codepoint Unicode codepoint
 * @return 1 if low surrogate, 0 otherwise
 *
 * @note This is a static function defined in json_string.c
 */

/**
 * @brief Decode a surrogate pair to a UTF-32 codepoint
 *
 * @fn static int json_decode_surrogate_pair(uint32_t high, uint32_t low,
 * uint32_t * out_codepoint)
 *
 * @param high High surrogate (0xD800-0xDBFF)
 * @param low Low surrogate (0xDC00-0xDFFF)
 * @param out_codepoint Output UTF-32 codepoint
 * @return 1 if valid, 0 if invalid
 *
 * @note This is a static function defined in json_string.c
 */

// ============================================================================
// UTF-8 Encoding/Validation
// ============================================================================

/**
 * @brief Encode a UTF-32 codepoint to UTF-8
 *
 * @fn static size_t json_encode_utf8(uint32_t codepoint, unsigned char * out)
 *
 * @param codepoint UTF-32 codepoint
 * @param out Output buffer (must have at least 4 bytes)
 * @return Number of bytes written (1-4)
 *
 * @note This is a static function defined in json_string.c
 */

/**
 * @brief Validate UTF-8 sequence
 *
 * @fn static int json_validate_utf8(const unsigned char * bytes, size_t len)
 *
 * @param bytes Pointer to UTF-8 bytes
 * @param len Length in bytes
 * @return 1 if valid, 0 if invalid
 *
 * @note This is a static function defined in json_string.c
 */

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_JSON_STRING_INTERNAL_H
