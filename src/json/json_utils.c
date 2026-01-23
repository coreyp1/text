/**
 * @file json_utils.c
 * @brief Shared utility functions for JSON module implementation
 *
 * This file contains shared utility functions that are used across multiple
 * JSON implementation files. These functions help reduce code duplication
 * and ensure consistent behavior, especially for security-critical operations
 * like overflow checking, bounds checking, and position tracking.
 *
 * This follows the pattern established in csv_utils.c for the CSV module.
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>

/**
 * @brief Get effective limit value (use default if configured is 0)
 *
 * This utility function returns the effective limit value. If the configured
 * value is greater than 0, it returns that value. Otherwise, it returns the
 * default value. This is used throughout the JSON parser to handle optional
 * limit configurations.
 *
 * @param configured The configured limit value (0 means use default)
 * @param default_val The default limit value to use if configured is 0
 * @return The effective limit value (either configured or default)
 */
size_t json_get_limit(size_t configured, size_t default_val) {
    return configured > 0 ? configured : default_val;
}

/**
 * @brief Safely update position offset, checking for overflow
 *
 * Updates the offset field of a position structure, checking for overflow
 * and saturating at SIZE_MAX if overflow would occur.
 *
 * @param pos Position structure to update (must not be NULL)
 * @param increment Number of bytes to add to offset
 */
void json_position_update_offset(json_position* pos, size_t increment) {
    if (!pos) {
        return;
    }
    // Check for overflow in offset (size_t, so wraps around, but we check anyway)
    if (json_check_add_overflow(pos->offset, increment)) {
        pos->offset = SIZE_MAX;  // Saturate at max
    } else {
        pos->offset += increment;
    }
}

/**
 * @brief Safely update position column, checking for overflow
 *
 * Updates the column field of a position structure, checking for integer
 * overflow and saturating at INT_MAX if overflow would occur.
 *
 * @param pos Position structure to update (must not be NULL)
 * @param increment Number of columns to add
 */
void json_position_update_column(json_position* pos, size_t increment) {
    if (!pos) {
        return;
    }
    // Check for integer overflow in column
    if (json_check_int_overflow(pos->col, increment)) {
        pos->col = INT_MAX;  // Saturate at max
    } else {
        pos->col += (int)increment;
    }
}

/**
 * @brief Safely increment line number, checking for overflow
 *
 * Increments the line number in a position structure, checking for integer
 * overflow and saturating at INT_MAX if already at maximum.
 *
 * @param pos Position structure to update (must not be NULL)
 */
void json_position_increment_line(json_position* pos) {
    if (!pos) {
        return;
    }
    if (pos->line < INT_MAX) {
        pos->line++;
    }
    // If already at INT_MAX, don't increment (avoid overflow)
}

/**
 * @brief Advance position by bytes, handling newlines
 *
 * Advances the position by a specified number of bytes, updating offset
 * and column appropriately. If the input contains newlines, line numbers
 * are incremented and columns are reset appropriately.
 *
 * This function scans the input buffer to detect newlines and updates
 * position tracking accordingly. It handles both single-byte newlines (\n)
 * and multi-byte newlines (\r\n).
 *
 * @param pos Position structure to update (must not be NULL)
 * @param input Input buffer to scan for newlines (can be NULL if input_len is 0)
 * @param input_len Number of bytes to advance
 * @param start_offset Starting offset in input buffer (for newline detection)
 */
void json_position_advance(json_position* pos, const char* input, size_t input_len, size_t start_offset) {
    if (!pos || input_len == 0) {
        return;
    }

    // Update offset
    json_position_update_offset(pos, input_len);

    // Scan for newlines to update line/column
    if (input) {
        size_t end_offset = start_offset + input_len;
        for (size_t i = start_offset; i < end_offset; i++) {
            if (input[i] == '\n') {
                // Single-byte newline
                json_position_increment_line(pos);
                pos->col = 1;
            } else if (input[i] == '\r') {
                // Check for CRLF
                if (i + 1 < end_offset && input[i + 1] == '\n') {
                    // CRLF - increment line, skip the LF in next iteration
                    json_position_increment_line(pos);
                    pos->col = 1;
                    i++;  // Skip the LF
                } else {
                    // Standalone CR
                    json_position_increment_line(pos);
                    pos->col = 1;
                }
            } else {
                // Regular character - increment column
                json_position_update_column(pos, 1);
            }
        }
    } else {
        // No input buffer - just update column by input_len
        // (assumes no newlines in the bytes being advanced)
        json_position_update_column(pos, input_len);
    }
}

/**
 * @brief Unified buffer growth function
 *
 * Grows a buffer to accommodate at least the needed size, using a configurable
 * growth strategy. Supports both simple doubling and hybrid growth strategies.
 *
 * The hybrid strategy uses:
 * - Fixed increment (64 bytes) for small buffers (< threshold)
 * - Exponential growth (doubling) for large buffers (>= threshold)
 *
 * The simple strategy always doubles the size (with optional headroom).
 *
 * All operations include overflow protection to prevent integer overflow.
 *
 * @param buffer Pointer to buffer pointer (will be updated on reallocation)
 * @param capacity Pointer to current capacity (will be updated)
 * @param needed Minimum size needed
 * @param strategy Growth strategy to use
 * @param initial_size Initial size to use if capacity is 0 (0 = use default)
 * @param small_threshold Threshold for hybrid strategy (0 = use default 1024)
 * @param growth_multiplier Multiplier for exponential growth (0 = use default 2)
 * @param fixed_increment Fixed increment for hybrid small buffers (0 = use default 64)
 * @param headroom Additional headroom to add after growth (0 = no headroom)
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_OOM on failure
 */
text_json_status json_buffer_grow_unified(
    char** buffer,
    size_t* capacity,
    size_t needed,
    json_buffer_growth_strategy strategy,
    size_t initial_size,
    size_t small_threshold,
    size_t growth_multiplier,
    size_t fixed_increment,
    size_t headroom
) {
    if (!buffer || !capacity) {
        return TEXT_JSON_E_INVALID;
    }

    // Use defaults if not specified
    if (initial_size == 0) {
        initial_size = 64;  // Default initial size
    }
    if (small_threshold == 0) {
        small_threshold = 1024;  // Default threshold (1KB)
    }
    if (growth_multiplier == 0) {
        growth_multiplier = 2;  // Default multiplier (doubling)
    }
    if (fixed_increment == 0) {
        fixed_increment = 64;  // Default fixed increment
    }

    // If already large enough, no need to grow
    if (needed <= *capacity) {
        return TEXT_JSON_OK;
    }

    size_t new_capacity;

    if (*capacity == 0) {
        // Initial allocation - use minimum size or needed size, whichever is larger
        new_capacity = (needed < initial_size) ? initial_size : needed;
    } else if (strategy == JSON_BUFFER_GROWTH_HYBRID) {
        // Hybrid growth strategy
        if (*capacity < small_threshold) {
            // Small buffer: grow by fixed increment
            if (json_check_add_overflow(*capacity, fixed_increment)) {
                // Cannot add increment without overflow - use needed size if possible
                if (needed > SIZE_MAX) {
                    return TEXT_JSON_E_OOM;
                }
                new_capacity = needed;
            } else {
                new_capacity = *capacity + fixed_increment;
                if (new_capacity < needed) {
                    new_capacity = needed;
                }
            }
        } else {
            // Large buffer: use exponential growth
            // Check for overflow before multiplication
            if (json_check_mul_overflow(*capacity, growth_multiplier)) {
                // Cannot multiply without overflow - use needed size if possible
                if (needed > SIZE_MAX) {
                    return TEXT_JSON_E_OOM;
                }
                new_capacity = needed;
            } else {
                new_capacity = *capacity * growth_multiplier;
                if (new_capacity < needed) {
                    new_capacity = needed;
                }
            }
        }
    } else {
        // Simple strategy: double the size
        // Check for overflow before multiplication
        if (json_check_mul_overflow(*capacity, growth_multiplier)) {
            // Cannot double without overflow, use needed size directly
            new_capacity = needed;
        } else {
            new_capacity = *capacity * growth_multiplier;
            if (new_capacity < needed) {
                new_capacity = needed;
            }
        }
    }

    // Add headroom if specified (check for overflow before addition)
    if (headroom > 0) {
        if (json_check_add_overflow(new_capacity, headroom)) {
            // Cannot add headroom without overflow
            if (new_capacity < needed) {
                return TEXT_JSON_E_OOM;  // Cannot satisfy request
            }
            // Use new_capacity as-is (already >= needed)
        } else {
            new_capacity += headroom;
        }
    }

    // Final overflow check
    if (new_capacity < needed || new_capacity < *capacity) {
        return TEXT_JSON_E_OOM;
    }

    // Reallocate buffer
    char* new_buffer = (char*)realloc(*buffer, new_capacity);
    if (!new_buffer) {
        return TEXT_JSON_E_OOM;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return TEXT_JSON_OK;
}

/**
 * @brief Check if addition would overflow (size_t)
 *
 * Returns true if adding b to a would cause an overflow.
 *
 * @param a First operand
 * @param b Second operand
 * @return 1 if overflow would occur, 0 otherwise
 */
int json_check_add_overflow(size_t a, size_t b) {
    return a > SIZE_MAX - b;
}

/**
 * @brief Check if multiplication would overflow (size_t)
 *
 * Returns true if multiplying a by b would cause an overflow.
 *
 * @param a First operand
 * @param b Second operand
 * @return 1 if overflow would occur, 0 otherwise
 */
int json_check_mul_overflow(size_t a, size_t b) {
    if (b == 0) {
        return 0;  // No overflow if multiplying by 0
    }
    return a > SIZE_MAX / b;
}

/**
 * @brief Check if subtraction would underflow (size_t)
 *
 * Returns true if subtracting b from a would cause an underflow.
 *
 * @param a First operand (minuend)
 * @param b Second operand (subtrahend)
 * @return 1 if underflow would occur, 0 otherwise
 */
int json_check_sub_underflow(size_t a, size_t b) {
    return a < b;
}

/**
 * @brief Check if integer addition would overflow (int)
 *
 * Returns true if adding increment to current would cause an integer overflow.
 * Used for column/line tracking where values are int.
 *
 * @param current Current integer value
 * @param increment Value to add
 * @return 1 if overflow would occur, 0 otherwise
 */
int json_check_int_overflow(int current, size_t increment) {
    if (increment > (size_t)INT_MAX) {
        return 1;  // Increment itself is too large
    }
    return current > INT_MAX - (int)increment;
}

/**
 * @brief Validate a pointer parameter and optionally set error
 *
 * This helper function checks if a pointer is NULL and, if so, optionally
 * sets an error code and message if an error output structure is provided.
 * This is useful for parameter validation at function entry points.
 *
 * Note: Many NULL checks are context-specific and don't need this helper.
 * Use this only when the pattern matches: check NULL, set error if provided,
 * return error code.
 *
 * @param ptr Pointer to validate (can be NULL)
 * @param err Error output structure (can be NULL, in which case no error is set)
 * @param error_code Error code to set if ptr is NULL
 * @param error_message Error message to set if ptr is NULL
 * @return 1 if ptr is NULL (error case), 0 if ptr is valid
 */
int json_check_null_param(const void* ptr, text_json_error* err, text_json_status error_code, const char* error_message) {
    if (ptr == NULL) {
        if (err) {
            *err = (text_json_error){
                                        .code = error_code,
                                        .message = error_message,
                                        .line = 1,
                                        .col = 1
                                    };
        }
        return 1;  // NULL detected
    }
    return 0;  // Valid pointer
}

/**
 * @brief Check if an array index is within bounds
 *
 * Returns true if the index is valid (within bounds) for an array of the given size.
 * An index is valid if it is less than the size.
 *
 * @param index Index to check
 * @param size Size of the array
 * @return 1 if index is in bounds (valid), 0 if out of bounds
 */
int json_check_bounds_index(size_t index, size_t size) {
    return index < size;
}

/**
 * @brief Check if a buffer offset is within bounds
 *
 * Returns true if the offset is valid (within bounds) for a buffer of the given size.
 * An offset is valid if it is less than the size.
 *
 * @param offset Offset to check
 * @param size Size of the buffer
 * @return 1 if offset is in bounds (valid), 0 if out of bounds
 */
int json_check_bounds_offset(size_t offset, size_t size) {
    return offset < size;
}

/**
 * @brief Check if a pointer is within a range
 *
 * Returns true if the pointer is within the range [start, end) (start inclusive, end exclusive).
 * This is useful for validating pointer arithmetic results.
 *
 * @param ptr Pointer to check
 * @param start Start of valid range (inclusive)
 * @param end End of valid range (exclusive)
 * @return 1 if ptr is in range, 0 if out of range
 */
int json_check_bounds_ptr(const void* ptr, const void* start, const void* end) {
    if (!ptr || !start || !end) {
        return 0;  // Invalid pointers
    }
    // Use pointer comparison (requires same array/object)
    return ptr >= start && ptr < end;
}

/**
 * @brief Initialize error structure fields to defaults
 *
 * Initializes an error structure to default values. This is useful for
 * setting up error structures before populating them with specific error
 * information. Note that this does NOT free any existing context snippet;
 * use text_json_error_free() first if needed.
 *
 * @param err Error structure to initialize (must not be NULL)
 * @param code Error code to set
 * @param message Error message to set
 * @param offset Byte offset of error
 * @param line Line number of error (1-based)
 * @param col Column number of error (1-based)
 */
void json_error_init_fields(
    text_json_error* err,
    text_json_status code,
    const char* message,
    size_t offset,
    int line,
    int col
) {
    if (!err) {
        return;
    }
    *err = (text_json_error){
                    .code = code,
                    .message = message,
                    .offset = offset,
                    .line = line,
                    .col = col
                };
}

/**
 * @brief Check if a string length would overflow when adding null terminator
 *
 * This is a common validation pattern used when allocating buffers for strings.
 * Returns true if adding 1 (for null terminator) to the length would cause
 * an overflow. This is equivalent to checking `len > SIZE_MAX - 1`.
 *
 * @param len String length to check
 * @return 1 if overflow would occur, 0 otherwise
 */
int json_check_string_length_overflow(size_t len) {
    return len > SIZE_MAX - 1;
}
