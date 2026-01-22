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
// Returns CSV_NEWLINE_* type if newline detected, CSV_NEWLINE_NONE otherwise
// Sets *error_out to TEXT_CSV_E_LIMIT if overflow occurs, TEXT_CSV_OK otherwise
TEXT_INTERNAL_API csv_newline_type csv_detect_newline(
    const char* input,
    size_t input_len,
    csv_position* pos,
    const text_csv_dialect* dialect,
    text_csv_status* error_out
) {
    if (error_out) {
        *error_out = TEXT_CSV_OK;
    }

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
            // Check all overflow conditions upfront before performing any operations
            if (pos->offset > SIZE_MAX - 2) {
                if (error_out) {
                    *error_out = TEXT_CSV_E_LIMIT;
                }
                return CSV_NEWLINE_NONE;  // Offset would overflow
            }
            if (pos->line >= INT_MAX) {
                if (error_out) {
                    *error_out = TEXT_CSV_E_LIMIT;
                }
                return CSV_NEWLINE_NONE;  // Line would overflow
            }
            // All checks passed, perform all updates
            pos->offset += 2;
            pos->line++;
            pos->column = 1;
            return CSV_NEWLINE_CRLF;
        }
    }

    // Check for LF
    if (dialect->accept_lf && input[offset] == '\n') {
        // Check all overflow conditions upfront before performing any operations
        if (pos->offset >= SIZE_MAX) {
            if (error_out) {
                *error_out = TEXT_CSV_E_LIMIT;
            }
            return CSV_NEWLINE_NONE;  // Offset would overflow
        }
        if (pos->line >= INT_MAX) {
            if (error_out) {
                *error_out = TEXT_CSV_E_LIMIT;
            }
            return CSV_NEWLINE_NONE;  // Line would overflow
        }
        // All checks passed, perform all updates
        pos->offset += 1;
        pos->line++;
        pos->column = 1;
        return CSV_NEWLINE_LF;
    }

    // Check for CR (only if CRLF not accepted or not present)
    if (dialect->accept_cr && input[offset] == '\r') {
        // Check all overflow conditions upfront before performing any operations
        if (pos->offset >= SIZE_MAX) {
            if (error_out) {
                *error_out = TEXT_CSV_E_LIMIT;
            }
            return CSV_NEWLINE_NONE;  // Offset would overflow
        }
        if (pos->line >= INT_MAX) {
            if (error_out) {
                *error_out = TEXT_CSV_E_LIMIT;
            }
            return CSV_NEWLINE_NONE;  // Line would overflow
        }
        // All checks passed, perform all updates
        pos->offset += 1;
        pos->line++;
        pos->column = 1;
        return CSV_NEWLINE_CR;
    }

    return CSV_NEWLINE_NONE;
}

// Validate UTF-8 sequence
// Sets *error_out to TEXT_CSV_E_LIMIT if overflow occurs, TEXT_CSV_OK otherwise
csv_utf8_result csv_validate_utf8(
    const char* input,
    size_t input_len,
    csv_position* pos,
    bool validate,
    text_csv_status* error_out
) {
    if (error_out) {
        *error_out = TEXT_CSV_OK;
    }

    if (!input || input_len == 0) {
        return CSV_UTF8_VALID;
    }

    if (!validate) {
        // Skip validation, just advance position
        if (pos) {
            // Check all overflow conditions upfront before performing any operations
            if (pos->offset > SIZE_MAX - input_len) {
                if (error_out) {
                    *error_out = TEXT_CSV_E_LIMIT;
                }
                return CSV_UTF8_INVALID;  // Offset would overflow
            }
            if (input_len > (size_t)INT_MAX || pos->column > INT_MAX - (int)input_len) {
                // Column overflow - return error
                if (error_out) {
                    *error_out = TEXT_CSV_E_LIMIT;
                }
                return CSV_UTF8_INVALID;  // Column would overflow
            }
            // All checks passed, perform all updates
            pos->offset += input_len;
            pos->column += (int)input_len;
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

        // Check for underflow in remaining (defensive check)
        if (remaining < seq_len) {
            return CSV_UTF8_INVALID;  // Logic error - remaining < seq_len
        }
        // Check all overflow conditions upfront before performing any operations
        if (offset > SIZE_MAX - seq_len) {
            return CSV_UTF8_INVALID;  // Offset would overflow
        }
        if (pos) {
            if (seq_len > (size_t)INT_MAX || pos->column > INT_MAX - (int)seq_len) {
                // Column overflow - return error
                if (error_out) {
                    *error_out = TEXT_CSV_E_LIMIT;
                }
                return CSV_UTF8_INVALID;  // Column would overflow
            }
        }
        // All checks passed, perform all updates
        offset += seq_len;
        remaining -= seq_len;
        if (pos) {
            pos->offset = offset;
            pos->column += (int)seq_len;
        }
    }

    return CSV_UTF8_VALID;
}

// Strip UTF-8 BOM from input (BOM is 0xEF 0xBB 0xBF)
// Returns TEXT_CSV_OK on success (BOM stripped or no BOM found), TEXT_CSV_E_LIMIT on overflow
// Sets *was_stripped to true if BOM was found and stripped, false otherwise
TEXT_INTERNAL_API text_csv_status csv_strip_bom(
    const char** input,
    size_t* input_len,
    csv_position* pos,
    bool strip,
    bool* was_stripped
) {
    if (was_stripped) {
        *was_stripped = false;
    }

    if (!input || !input_len || !*input || *input_len < 3) {
        return TEXT_CSV_OK;  // No BOM found (not an error)
    }

    if (!strip) {
        return TEXT_CSV_OK;  // Stripping disabled (not an error)
    }

    // Check for UTF-8 BOM: 0xEF 0xBB 0xBF
    if ((unsigned char)(*input)[0] == 0xEF &&
        (unsigned char)(*input)[1] == 0xBB &&
        (unsigned char)(*input)[2] == 0xBF) {
        // Check for underflow before subtracting (defensive check)
        if (*input_len < 3) {
            return TEXT_CSV_OK;  // Should not happen due to earlier check, but be safe
        }
        if (pos) {
            // Check all overflow conditions upfront before performing any operations
            if (pos->offset > SIZE_MAX - 3) {
                return TEXT_CSV_E_LIMIT;  // Offset would overflow
            }
            if (pos->column > INT_MAX - 3) {
                return TEXT_CSV_E_LIMIT;  // Column would overflow
            }
            // All checks passed, perform all updates
            pos->offset += 3;
            pos->column += 3;
        }
        // Strip BOM (update input pointers after position checks)
        *input += 3;
        *input_len -= 3;
        if (was_stripped) {
            *was_stripped = true;
        }
        return TEXT_CSV_OK;
    }

    return TEXT_CSV_OK;  // No BOM found (not an error)
}
