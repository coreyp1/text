/**
 * @file csv_utils.c
 * @brief Utility functions for CSV parsing: newline detection, BOM stripping, UTF-8 validation
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Detect and consume newline from input
TEXT_INTERNAL_API csv_newline_type csv_detect_newline(
    const char* input,
    size_t input_len,
    csv_position* pos,
    const text_csv_dialect* dialect
) {
    if (!input || input_len == 0 || !pos || !dialect) {
        return CSV_NEWLINE_NONE;
    }

    size_t offset = pos->offset;
    if (offset >= input_len) {
        return CSV_NEWLINE_NONE;
    }

    // Check for CRLF first (must check before LF/CR individually)
    if (dialect->accept_crlf && offset + 1 < input_len) {
        if (input[offset] == '\r' && input[offset + 1] == '\n') {
            // Check for overflow before updating offset
            if (pos->offset > SIZE_MAX - 2) {
                return CSV_NEWLINE_NONE;  // Offset would overflow
            }
            pos->offset += 2;
            // Check for integer overflow in line (int can overflow)
            if (pos->line < INT_MAX) {
                pos->line++;
            }
            pos->column = 1;
            return CSV_NEWLINE_CRLF;
        }
    }

    // Check for LF
    if (dialect->accept_lf && input[offset] == '\n') {
        // Check for overflow before updating offset
        if (pos->offset == SIZE_MAX) {
            return CSV_NEWLINE_NONE;  // Offset would overflow
        }
        pos->offset += 1;
        // Check for integer overflow in line (int can overflow)
        if (pos->line < INT_MAX) {
            pos->line++;
        }
        pos->column = 1;
        return CSV_NEWLINE_LF;
    }

    // Check for CR (only if CRLF not accepted or not present)
    if (dialect->accept_cr && input[offset] == '\r') {
        // Check for overflow before updating offset
        if (pos->offset == SIZE_MAX) {
            return CSV_NEWLINE_NONE;  // Offset would overflow
        }
        pos->offset += 1;
        // Check for integer overflow in line (int can overflow)
        if (pos->line < INT_MAX) {
            pos->line++;
        }
        pos->column = 1;
        return CSV_NEWLINE_CR;
    }

    return CSV_NEWLINE_NONE;
}

// Validate UTF-8 sequence
csv_utf8_result csv_validate_utf8(
    const char* input,
    size_t input_len,
    csv_position* pos,
    bool validate
) {
    if (!input || input_len == 0) {
        return CSV_UTF8_VALID;
    }

    if (!validate) {
        // Skip validation, just advance position
        if (pos) {
            // Check for overflow before updating offset
            if (pos->offset > SIZE_MAX - input_len) {
                return CSV_UTF8_INVALID;  // Offset would overflow
            }
            pos->offset += input_len;
            // Check for integer overflow in column (int can overflow)
            if (input_len > (size_t)INT_MAX || pos->column > INT_MAX - (int)input_len) {
                // Column overflow - clamp to INT_MAX
                pos->column = INT_MAX;
            } else {
                pos->column += (int)input_len;
            }
        }
        return CSV_UTF8_VALID;
    }

    size_t offset = pos ? pos->offset : 0;
    // Validate that offset is within input_len bounds
    if (offset > input_len) {
        return CSV_UTF8_INVALID;  // Invalid offset
    }
    // Calculate remaining bytes to process (from offset to end)
    size_t remaining = input_len - offset;

    while (remaining > 0) {
        if (offset >= input_len) {
            break;
        }

        // Bounds check: ensure offset is valid before accessing input
        if (offset >= input_len) {
            return CSV_UTF8_INVALID;  // Invalid offset
        }

        unsigned char byte = (unsigned char)input[offset];
        size_t seq_len = 0;

        // Determine sequence length from first byte
        if ((byte & 0x80) == 0) {
            // ASCII (0xxxxxxx)
            seq_len = 1;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2-byte sequence (110xxxxx)
            seq_len = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            // 3-byte sequence (1110xxxx)
            seq_len = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            // 4-byte sequence (11110xxx)
            seq_len = 4;
        } else {
            // Invalid first byte
            return CSV_UTF8_INVALID;
        }

        // Check if we have enough bytes for the sequence
        // Check for overflow in offset + seq_len
        if (offset > SIZE_MAX - seq_len || offset + seq_len > input_len) {
            return CSV_UTF8_INCOMPLETE;
        }

        // Validate continuation bytes (must be 10xxxxxx)
        for (size_t i = 1; i < seq_len; i++) {
            // Bounds check: ensure offset + i is valid
            if (offset + i >= input_len) {
                return CSV_UTF8_INCOMPLETE;
            }
            unsigned char cont_byte = (unsigned char)input[offset + i];
            if ((cont_byte & 0xC0) != 0x80) {
                return CSV_UTF8_INVALID;
            }
        }

        // Check for overlong encodings and invalid code points
        if (seq_len == 2) {
            if ((byte & 0x1E) == 0) {
                return CSV_UTF8_INVALID;  // Overlong encoding
            }
        } else if (seq_len == 3) {
            // Bounds check: ensure offset + 1 is valid
            if (offset + 1 >= input_len) {
                return CSV_UTF8_INCOMPLETE;
            }
            if ((byte & 0x0F) == 0 && (input[offset + 1] & 0x20) == 0) {
                return CSV_UTF8_INVALID;  // Overlong encoding
            }
        } else if (seq_len == 4) {
            // Bounds check: ensure offset + 1 is valid
            if (offset + 1 >= input_len) {
                return CSV_UTF8_INCOMPLETE;
            }
            if ((byte & 0x07) == 0 && (input[offset + 1] & 0x30) == 0) {
                return CSV_UTF8_INVALID;  // Overlong encoding
            }
            // Check for code points > U+10FFFF
            unsigned char b1 = (unsigned char)input[offset];
            unsigned char b2 = (unsigned char)input[offset + 1];
            if (b1 == 0xF4 && (b2 & 0xF0) != 0) {
                return CSV_UTF8_INVALID;  // > U+10FFFF
            }
            if (b1 > 0xF4) {
                return CSV_UTF8_INVALID;  // > U+10FFFF
            }
        }

        // Advance position
        // Check for overflow before updating offset
        if (offset > SIZE_MAX - seq_len) {
            return CSV_UTF8_INVALID;  // Offset would overflow
        }
        offset += seq_len;
        // Check for underflow in remaining
        if (remaining < seq_len) {
            return CSV_UTF8_INVALID;  // Logic error - remaining < seq_len
        }
        remaining -= seq_len;
        if (pos) {
            pos->offset = offset;
            // Check for integer overflow in column (int can overflow)
            if (seq_len > (size_t)INT_MAX || pos->column > INT_MAX - (int)seq_len) {
                // Column overflow - clamp to INT_MAX
                pos->column = INT_MAX;
            } else {
                pos->column += (int)seq_len;
            }
        }
    }

    return CSV_UTF8_VALID;
}

// Strip UTF-8 BOM from input (BOM is 0xEF 0xBB 0xBF)
TEXT_INTERNAL_API bool csv_strip_bom(
    const char** input,
    size_t* input_len,
    csv_position* pos,
    bool strip
) {
    if (!input || !input_len || !*input || *input_len < 3) {
        return false;
    }

    if (!strip) {
        return false;
    }

    // Check for UTF-8 BOM: 0xEF 0xBB 0xBF
    if ((unsigned char)(*input)[0] == 0xEF &&
        (unsigned char)(*input)[1] == 0xBB &&
        (unsigned char)(*input)[2] == 0xBF) {
        // Strip BOM
        *input += 3;
        // Check for underflow before subtracting
        if (*input_len < 3) {
            return false;  // Should not happen due to earlier check, but be safe
        }
        *input_len -= 3;
        if (pos) {
            // Check for overflow before updating offset
            if (pos->offset > SIZE_MAX - 3) {
                return false;  // Offset would overflow
            }
            pos->offset += 3;
            // Check for integer overflow in column (int can overflow)
            if (pos->column > INT_MAX - 3) {
                // Column overflow - clamp to INT_MAX
                pos->column = INT_MAX;
            } else {
                pos->column += 3;
            }
        }
        return true;
    }

    return false;
}
