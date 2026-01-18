// Number parsing and multi-representation support for JSON module

#include "json_internal.h"
#include <text/json.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

// Check if a character is a digit
static int json_is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Validate number syntax according to RFC 8259
// Validates that the number follows JSON number grammar:
// - No leading zeros (except 0 itself)
// - No trailing decimal point (1. is invalid)
// - No leading decimal point (.1 is invalid, but 0.1 is valid)
// - Proper exponent format
// - No invalid characters
static int json_validate_number_syntax(const char* input, size_t len, size_t start_pos) {
    if (len == 0 || start_pos >= len) {
        return 0;
    }

    size_t i = start_pos;
    int has_digit = 0;

    // Optional minus sign
    if (i < len && input[i] == '-') {
        i++;
    }

    // Integer part
    if (i < len && input[i] == '0') {
        // Leading zero - must be followed by . or end or e/E
        i++;
        has_digit = 1;
        if (i < len && json_is_digit(input[i])) {
            // Leading zero followed by digit (e.g., 01) is invalid
            return 0;
        }
    } else if (i < len && json_is_digit(input[i])) {
        // One or more digits
        while (i < len && json_is_digit(input[i])) {
            i++;
            has_digit = 1;
        }
    } else {
        // Must start with digit or minus followed by digit
        return 0;
    }

    // Fractional part (optional)
    if (i < len && input[i] == '.') {
        i++;
        // Must have at least one digit after decimal point
        if (i >= len || !json_is_digit(input[i])) {
            return 0;  // Invalid: 1. or .1 (without leading zero)
        }
        while (i < len && json_is_digit(input[i])) {
            i++;
        }
    }

    // Exponent part (optional)
    if (i < len && (input[i] == 'e' || input[i] == 'E')) {
        i++;
        // Optional sign
        if (i < len && (input[i] == '+' || input[i] == '-')) {
            i++;
        }
        // Must have at least one digit after e/E
        if (i >= len || !json_is_digit(input[i])) {
            return 0;  // Invalid: 1e or 1e+
        }
        while (i < len && json_is_digit(input[i])) {
            i++;
        }
    }

    // Must have consumed entire input
    if (i != len) {
        return 0;
    }

    // Must have at least one digit
    return has_digit;
}

// Parse nonfinite number (NaN, Infinity, -Infinity)
static int json_parse_nonfinite(const char* input, size_t len, json_number* num) {
    if (json_matches(input, len, "NaN")) {
        num->dbl = NAN;
        num->flags = JSON_NUMBER_HAS_DOUBLE | JSON_NUMBER_IS_NONFINITE;
        return 1;
    }
    if (json_matches(input, len, "Infinity")) {
        num->dbl = INFINITY;
        num->flags = JSON_NUMBER_HAS_DOUBLE | JSON_NUMBER_IS_NONFINITE;
        return 1;
    }
    if (json_matches(input, len, "-Infinity")) {
        num->dbl = -INFINITY;
        num->flags = JSON_NUMBER_HAS_DOUBLE | JSON_NUMBER_IS_NONFINITE;
        return 1;
    }
    return 0;
}

// Parse uint64 from string with overflow detection
static int json_parse_uint64(const char* input, size_t len, uint64_t* out_u64) {
    if (len == 0) {
        return 0;
    }

    // uint64 cannot be negative
    if (input[0] == '-') {
        return 0;
    }

    size_t i = 0;

    // Skip leading zeros
    while (i < len && input[i] == '0') {
        i++;
    }

    // If all zeros, result is 0
    if (i >= len) {
        *out_u64 = 0;
        return 1;
    }

    // Check if we have a fractional part or exponent
    // For now, only parse pure integers
    for (size_t j = i; j < len; ++j) {
        if (input[j] == '.' || input[j] == 'e' || input[j] == 'E') {
            // Has fractional part or exponent - not a pure integer
            return 0;
        }
    }

    // Parse digits
    uint64_t result = 0;
    uint64_t max_before_mult = UINT64_MAX / 10;
    uint64_t max_last_digit = UINT64_MAX % 10;

    for (; i < len; ++i) {
        if (!json_is_digit(input[i])) {
            return 0;
        }

        int digit = input[i] - '0';

        // Check for overflow before multiplying
        if (result > max_before_mult || (result == max_before_mult && (uint64_t)digit > max_last_digit)) {
            return 0;  // Overflow
        }

        result = result * 10 + digit;
    }

    *out_u64 = result;
    return 1;
}

// Parse int64 from string with overflow detection
// This function delegates to json_parse_uint64() after handling the sign
static int json_parse_int64(const char* input, size_t len, int64_t* out_i64) {
    if (len == 0) {
        return 0;
    }

    // Check for negative sign
    int negative = 0;
    size_t start = 0;
    if (input[0] == '-') {
        negative = 1;
        start = 1;
        // After '-', we need at least one digit
        if (start >= len) {
            return 0;
        }
    }

    // Parse as uint64 (this handles leading zeros, fractional/exponent checks, etc.)
    uint64_t u64_val;
    if (!json_parse_uint64(input + start, len - start, &u64_val)) {
        return 0;
    }

    // Check if the value fits in int64 range
    if (negative) {
        // For negative numbers: the absolute value must be <= INT64_MAX + 1
        // Since -INT64_MIN = INT64_MAX + 1, we check: u64_val <= (uint64_t)INT64_MAX + 1
        if (u64_val > (uint64_t)INT64_MAX + 1) {
            return 0;  // Underflow: absolute value too large
        }
        // Special case: if u64_val == INT64_MAX + 1, result is INT64_MIN
        if (u64_val == (uint64_t)INT64_MAX + 1) {
            *out_i64 = INT64_MIN;
        } else {
            *out_i64 = -(int64_t)u64_val;
        }
    } else {
        // For positive numbers: must be <= INT64_MAX
        if (u64_val > (uint64_t)INT64_MAX) {
            return 0;  // Overflow: value too large for int64
        }
        *out_i64 = (int64_t)u64_val;
    }

    return 1;
}

text_json_status json_parse_number(
    const char* input,
    size_t input_len,
    json_number* num,
    json_position* pos,
    const text_json_parse_options* opts
) {
    if (!input || !num || input_len == 0) {
        return TEXT_JSON_E_INVALID;
    }

    // Initialize output structure
    memset(num, 0, sizeof(json_number));

    // Check for nonfinite numbers first (if enabled)
    if (opts && opts->allow_nonfinite_numbers) {
        if (json_parse_nonfinite(input, input_len, num)) {
            // Preserve lexeme if requested
            if (opts->preserve_number_lexeme) {
                // Check for integer overflow
                if (input_len > SIZE_MAX - 1) {
                    return TEXT_JSON_E_LIMIT;
                }
                num->lexeme = malloc(input_len + 1);
                if (!num->lexeme) {
                    return TEXT_JSON_E_OOM;
                }
                memcpy(num->lexeme, input, input_len);
                num->lexeme[input_len] = '\0';
                num->lexeme_len = input_len;
                num->flags |= JSON_NUMBER_HAS_LEXEME;
            }
            return TEXT_JSON_OK;
        }
    }

    // Validate number syntax
    if (!json_validate_number_syntax(input, input_len, 0)) {
        return TEXT_JSON_E_BAD_NUMBER;
    }

    // Preserve lexeme if requested
    if (opts && opts->preserve_number_lexeme) {
        // Check for integer overflow
        if (input_len > SIZE_MAX - 1) {
            return TEXT_JSON_E_LIMIT;
        }
        num->lexeme = malloc(input_len + 1);
        if (!num->lexeme) {
            return TEXT_JSON_E_OOM;
        }
        memcpy(num->lexeme, input, input_len);
        num->lexeme[input_len] = '\0';
        num->lexeme_len = input_len;
        num->flags |= JSON_NUMBER_HAS_LEXEME;
    }

    // Parse int64 if requested
    if (opts && opts->parse_int64) {
        int64_t i64_val;
        if (json_parse_int64(input, input_len, &i64_val)) {
            num->i64 = i64_val;
            num->flags |= JSON_NUMBER_HAS_I64;
        }
    }

    // Parse uint64 if requested (only for non-negative numbers)
    if (opts && opts->parse_uint64 && input[0] != '-') {
        uint64_t u64_val;
        if (json_parse_uint64(input, input_len, &u64_val)) {
            num->u64 = u64_val;
            num->flags |= JSON_NUMBER_HAS_U64;
        }
    }

    // Parse double using strtod
    if (opts && opts->parse_double) {
        // Create null-terminated string for strtod
        // Check for integer overflow
        if (input_len > SIZE_MAX - 1) {
            // Clean up lexeme if allocated
            if (num->lexeme) {
                free(num->lexeme);
                num->lexeme = NULL;
            }
            return TEXT_JSON_E_LIMIT;
        }
        char* strtod_input = malloc(input_len + 1);
        if (!strtod_input) {
            // Clean up lexeme if allocated
            if (num->lexeme) {
                free(num->lexeme);
                num->lexeme = NULL;
            }
            return TEXT_JSON_E_OOM;
        }
        memcpy(strtod_input, input, input_len);
        strtod_input[input_len] = '\0';

        char* endptr;
        errno = 0;
        double dbl_val = strtod(strtod_input, &endptr);

        // Check if entire string was consumed
        if (endptr == strtod_input + input_len && errno == 0) {
            // Check for nonfinite numbers (if not already handled)
            if (!isnan(dbl_val) && !isinf(dbl_val)) {
                num->dbl = dbl_val;
                num->flags |= JSON_NUMBER_HAS_DOUBLE;
            } else if (opts && opts->allow_nonfinite_numbers) {
                num->dbl = dbl_val;
                num->flags |= JSON_NUMBER_HAS_DOUBLE | JSON_NUMBER_IS_NONFINITE;
            }
        }

        free(strtod_input);
    }

    // Update position if provided
    if (pos) {
        // Check for overflow in offset (size_t, so wraps around, but we check anyway)
        if (pos->offset > SIZE_MAX - input_len) {
            pos->offset = SIZE_MAX;  // Saturate at max
        } else {
            pos->offset += input_len;
        }
        // Update column (line doesn't change for numbers)
        // Check for integer overflow in column
        if (input_len > (size_t)INT_MAX || pos->col > INT_MAX - (int)input_len) {
            pos->col = INT_MAX;  // Saturate at max
        } else {
            pos->col += (int)input_len;
        }
    }

    return TEXT_JSON_OK;
}

void json_number_destroy(json_number* num) {
    if (!num) {
        return;
    }

    // Free the lexeme if it was allocated
    if (num->lexeme) {
        free(num->lexeme);
        num->lexeme = NULL;
        num->lexeme_len = 0;
        num->flags &= ~JSON_NUMBER_HAS_LEXEME;
    }
}
