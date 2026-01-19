/**
 * @file json_string.c
 * @brief String and Unicode handling utilities for JSON module
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

/**
 * @brief Decode a standard escape sequence
 *
 * @param c Escape character (e.g., 'n', 't', 'r')
 * @return Decoded character, or 0 if invalid
 */
static int json_decode_escape(char c) {
    switch (c) {
        case '"':  return '"';
        case '\\': return '\\';
        case '/':  return '/';
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
        default:   return 0;  // Invalid escape
    }
}

/**
 * @brief Decode a hex digit to its value
 *
 * @param c Hex digit character
 * @return Value (0-15), or -1 if invalid
 */
static int json_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Decode a \uXXXX Unicode escape sequence
 *
 * @param input Pointer to the 'u' character
 * @param len Remaining length in input
 * @param out_codepoint Output codepoint value
 * @return Number of bytes consumed (6 if valid, 0 if invalid)
 */
static size_t json_decode_unicode_escape(const char* input, size_t len, uint32_t* out_codepoint) {
    if (len < 5 || input[0] != 'u') {
        return 0;
    }

    // Decode 4 hex digits
    uint32_t codepoint = 0;
    for (int i = 1; i < 5; ++i) {
        int digit = json_hex_digit(input[i]);
        if (digit < 0) {
            return 0;  // Invalid hex digit
        }
        codepoint = (codepoint << 4) | digit;
    }

    *out_codepoint = codepoint;
    return 5;  // 'u' + 4 hex digits
}

/**
 * @brief Check if a codepoint is a high surrogate
 *
 * @param codepoint Unicode codepoint
 * @return 1 if high surrogate, 0 otherwise
 */
static int json_is_high_surrogate(uint32_t codepoint) {
    return codepoint >= 0xD800 && codepoint <= 0xDBFF;
}

/**
 * @brief Check if a codepoint is a low surrogate
 *
 * @param codepoint Unicode codepoint
 * @return 1 if low surrogate, 0 otherwise
 */
static int json_is_low_surrogate(uint32_t codepoint) {
    return codepoint >= 0xDC00 && codepoint <= 0xDFFF;
}

/**
 * @brief Decode a surrogate pair to a UTF-32 codepoint
 *
 * @param high High surrogate (0xD800-0xDBFF)
 * @param low Low surrogate (0xDC00-0xDFFF)
 * @param out_codepoint Output UTF-32 codepoint
 * @return 1 if valid, 0 if invalid
 */
static int json_decode_surrogate_pair(uint32_t high, uint32_t low, uint32_t* out_codepoint) {
    if (!json_is_high_surrogate(high) || !json_is_low_surrogate(low)) {
        return 0;
    }

    // Decode: ((high - 0xD800) << 10) + (low - 0xDC00) + 0x10000
    *out_codepoint = ((high - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
    return 1;
}

/**
 * @brief Encode a UTF-32 codepoint to UTF-8
 *
 * @param codepoint UTF-32 codepoint
 * @param out Output buffer (must have at least 4 bytes)
 * @return Number of bytes written (1-4)
 */
static size_t json_encode_utf8(uint32_t codepoint, unsigned char* out) {
    if (codepoint < 0x80) {
        out[0] = (unsigned char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        out[0] = 0xC0 | (unsigned char)(codepoint >> 6);
        out[1] = 0x80 | (unsigned char)(codepoint & 0x3F);
        return 2;
    } else if (codepoint < 0x10000) {
        out[0] = 0xE0 | (unsigned char)(codepoint >> 12);
        out[1] = 0x80 | (unsigned char)((codepoint >> 6) & 0x3F);
        out[2] = 0x80 | (unsigned char)(codepoint & 0x3F);
        return 3;
    } else if (codepoint < 0x110000) {
        out[0] = 0xF0 | (unsigned char)(codepoint >> 18);
        out[1] = 0x80 | (unsigned char)((codepoint >> 12) & 0x3F);
        out[2] = 0x80 | (unsigned char)((codepoint >> 6) & 0x3F);
        out[3] = 0x80 | (unsigned char)(codepoint & 0x3F);
        return 4;
    }
    return 0;  // Invalid codepoint
}

/**
 * @brief Validate UTF-8 sequence
 *
 * @param bytes Pointer to UTF-8 bytes
 * @param len Length in bytes
 * @return 1 if valid, 0 if invalid
 */
static int json_validate_utf8(const unsigned char* bytes, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char byte = bytes[i];

        if (byte < 0x80) {
            // ASCII
            i++;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) {
                return 0;
            }
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= len ||
                (bytes[i + 1] & 0xC0) != 0x80 ||
                (bytes[i + 2] & 0xC0) != 0x80) {
                return 0;
            }
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 >= len ||
                (bytes[i + 1] & 0xC0) != 0x80 ||
                (bytes[i + 2] & 0xC0) != 0x80 ||
                (bytes[i + 3] & 0xC0) != 0x80) {
                return 0;
            }
            i += 4;
        } else {
            // Invalid start byte
            return 0;
        }
    }
    return 1;
}

text_json_status json_decode_string(
    const char* input,
    size_t input_len,
    char* output,
    size_t output_capacity,
    size_t* output_len,
    json_position* pos,
    int validate_utf8,
    json_utf8_mode utf8_mode,
    int allow_unescaped_controls
) {
    size_t out_idx = 0;
    size_t in_idx = 0;

    if (!output || !output_len || output_capacity == 0) {
        return TEXT_JSON_E_INVALID;
    }

    while (in_idx < input_len) {
        if (input[in_idx] == '\\') {
            // Escape sequence
            if (in_idx + 1 >= input_len) {
                return TEXT_JSON_E_BAD_ESCAPE;
            }

            char esc_char = input[in_idx + 1];

            if (esc_char == 'u') {
                // Unicode escape
                if (in_idx + 5 >= input_len) {
                    return TEXT_JSON_E_BAD_UNICODE;
                }

                uint32_t codepoint;
                size_t consumed = json_decode_unicode_escape(input + in_idx + 1,
                                                             input_len - in_idx - 1,
                                                             &codepoint);
                if (consumed == 0) {
                    return TEXT_JSON_E_BAD_UNICODE;
                }

                // Check for surrogate pair
                if (json_is_high_surrogate(codepoint)) {
                    // Look for low surrogate
                    if (in_idx + 12 <= input_len &&
                        input[in_idx + 6] == '\\' &&
                        input[in_idx + 7] == 'u') {
                        uint32_t low_codepoint;
                        size_t low_consumed = json_decode_unicode_escape(input + in_idx + 7,
                                                                        input_len - in_idx - 7,
                                                                        &low_codepoint);
                        if (low_consumed > 0 && json_is_low_surrogate(low_codepoint)) {
                            // Decode surrogate pair
                            uint32_t full_codepoint;
                            if (json_decode_surrogate_pair(codepoint, low_codepoint, &full_codepoint)) {
                                unsigned char utf8_bytes[4];
                                size_t utf8_len = json_encode_utf8(full_codepoint, utf8_bytes);
                                if (utf8_len == 0) {
                                    return TEXT_JSON_E_BAD_UNICODE;
                                }
                                if (out_idx + utf8_len > output_capacity) {
                                    return TEXT_JSON_E_LIMIT;
                                }
                                for (size_t i = 0; i < utf8_len; ++i) {
                                    output[out_idx++] = utf8_bytes[i];
                                }
                                in_idx += 12;  // \uXXXX\uXXXX
                                continue;
                            }
                        }
                    }
                    // High surrogate without valid low surrogate
                    return TEXT_JSON_E_BAD_UNICODE;
                } else if (json_is_low_surrogate(codepoint)) {
                    // Low surrogate without high surrogate
                    return TEXT_JSON_E_BAD_UNICODE;
                }

                // Regular Unicode escape
                unsigned char utf8_bytes[4];
                size_t utf8_len = json_encode_utf8(codepoint, utf8_bytes);
                if (utf8_len == 0) {
                    return TEXT_JSON_E_BAD_UNICODE;
                }
                if (out_idx + utf8_len > output_capacity) {
                    return TEXT_JSON_E_LIMIT;
                }
                for (size_t i = 0; i < utf8_len; ++i) {
                    output[out_idx++] = utf8_bytes[i];
                }
                in_idx += 6;  // \uXXXX
            } else {
                // Standard escape
                int decoded = json_decode_escape(esc_char);
                if (decoded == 0) {
                    return TEXT_JSON_E_BAD_ESCAPE;
                }
                if (out_idx >= output_capacity) {
                    return TEXT_JSON_E_LIMIT;
                }
                output[out_idx++] = decoded;
                in_idx += 2;  // \X
            }
        } else {
            // Regular character
            unsigned char c = (unsigned char)input[in_idx];

            // Check for control characters (0x00-0x1F) unless allowed
            // Note: JSON spec requires control characters to be escaped
            if (!allow_unescaped_controls && c < 0x20) {
                return TEXT_JSON_E_BAD_TOKEN;  // Unescaped control character
            }

            if (out_idx >= output_capacity) {
                return TEXT_JSON_E_LIMIT;
            }
            output[out_idx++] = input[in_idx++];
        }

        // Update position
        if (pos) {
            pos->offset = in_idx;
            if (in_idx > 0 && input[in_idx - 1] == '\n') {
                // Check for integer overflow in line
                if (pos->line < INT_MAX) {
                    pos->line++;
                }
                pos->col = 1;
            } else {
                // Check for integer overflow in column
                if (pos->col < INT_MAX) {
                    pos->col++;
                }
            }
        }
    }

    // Validate UTF-8 if requested
    if (validate_utf8 && out_idx > 0) {
        if (!json_validate_utf8((const unsigned char*)output, out_idx)) {
            if (utf8_mode == JSON_UTF8_REJECT) {
                return TEXT_JSON_E_BAD_UNICODE;
            } else if (utf8_mode == JSON_UTF8_REPLACE) {
                // Replace invalid sequences with replacement character (U+FFFD)
                // For now, we'll just reject - full replacement implementation
                // would require more complex logic
                return TEXT_JSON_E_BAD_UNICODE;
            }
            // VERBATIM mode: allow invalid UTF-8
        }
    }

    *output_len = out_idx;
    return TEXT_JSON_OK;
}
