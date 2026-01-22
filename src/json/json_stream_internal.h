/**
 * @file json_stream_internal.h
 * @brief Internal definitions for JSON streaming parser
 *
 * This header contains internal-only definitions used by the JSON streaming parser
 * implementation. It should not be included by external code.
 */

#ifndef GHOTI_IO_TEXT_JSON_STREAM_INTERNAL_H
#define GHOTI_IO_TEXT_JSON_STREAM_INTERNAL_H

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_stream.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Token buffer type enumeration
 */
typedef enum {
    JSON_TOKEN_BUFFER_NONE,      ///< No active token buffer
    JSON_TOKEN_BUFFER_STRING,    ///< Buffering a string token
    JSON_TOKEN_BUFFER_NUMBER     ///< Buffering a number token
} json_token_buffer_type;

/**
 * @brief Token buffer structure
 *
 * Unified structure for managing incomplete tokens (strings, numbers) that
 * span chunk boundaries. Maintains parsing state to allow resumption when
 * more input arrives.
 */
typedef struct json_token_buffer {
    json_token_buffer_type type;  ///< Type of token being buffered

    // Buffer management (similar to CSV field_buffer)
    char* buffer;                  ///< Allocated buffer (NULL if not buffered)
    size_t buffer_size;            ///< Allocated buffer size in bytes
    size_t buffer_used;            ///< Used buffer size in bytes
    bool is_buffered;              ///< Whether data is in allocated buffer

    // JSON-specific parsing state
    union {
        struct {
            int in_escape;                  ///< 1 if last char was '\'
            int unicode_escape_remaining;   ///< Hex digits remaining for \uXXXX (0-4)
            int high_surrogate_seen;        ///< 1 if high surrogate seen, waiting for low
        } string_state;
        struct {
            int has_dot;                    ///< 1 if number contains '.'
            int has_exp;                    ///< 1 if number contains 'e' or 'E'
            int exp_sign_seen;              ///< 1 if exponent sign (+/-) seen
            int starts_with_minus;          ///< 1 if number starts with '-'
        } number_state;
    } parse_state;

    // Position tracking
    size_t start_offset;                   ///< Offset where token started in input_buffer
    size_t consumed_length;                ///< Length of data consumed from input_buffer (for incomplete tokens)
} json_token_buffer;

/**
 * @brief Initial size for token buffer allocation
 */
#define JSON_TOKEN_BUFFER_INITIAL_SIZE 64

/**
 * @brief Minimum size for token buffer
 */
#define JSON_TOKEN_BUFFER_MIN_SIZE 1

/**
 * @brief Multiplier for buffer growth
 */
#define JSON_BUFFER_GROWTH_MULTIPLIER 2

/**
 * @brief Threshold for hybrid growth strategy (1KB)
 *
 * Buffers smaller than this use exponential growth, larger buffers
 * use linear growth to avoid excessive memory usage.
 */
#define JSON_BUFFER_SMALL_THRESHOLD 1024

// Token buffer functions (in json_stream_buffer.c)

/**
 * @brief Initialize a token buffer structure
 *
 * Initializes all fields to safe defaults. The buffer must be initialized
 * before use.
 *
 * @param tb Token buffer to initialize (must not be NULL)
 */
void json_token_buffer_init(json_token_buffer* tb);

/**
 * @brief Clear a token buffer structure
 *
 * Frees any allocated buffer and resets all fields to initial state.
 * The buffer can be reused after clearing.
 *
 * @param tb Token buffer to clear (must not be NULL)
 */
void json_token_buffer_clear(json_token_buffer* tb);

/**
 * @brief Append data to token buffer
 *
 * Appends data to the token buffer, growing it if necessary.
 * The buffer must be in buffered mode (is_buffered == true).
 *
 * @param tb Token buffer to append to (must not be NULL)
 * @param data Data to append (must not be NULL)
 * @param len Length of data to append
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_token_buffer_append(
    json_token_buffer* tb,
    const char* data,
    size_t len
);

/**
 * @brief Grow token buffer to accommodate needed size
 *
 * Allocates or reallocates the token buffer to at least the needed size.
 * Uses hybrid growth strategy (exponential for small buffers, linear for large).
 *
 * @param tb Token buffer to grow (must not be NULL)
 * @param needed Minimum size needed in bytes
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_token_buffer_grow(
    json_token_buffer* tb,
    size_t needed
);

/**
 * @brief Set parsing state for string token
 *
 * Sets the string parsing state in the token buffer.
 *
 * @param tb Token buffer to set state on (must not be NULL)
 * @param in_escape 1 if last char was '\', 0 otherwise
 * @param unicode_escape_remaining Hex digits remaining for \uXXXX (0-4)
 * @param high_surrogate_seen 1 if high surrogate seen, waiting for low, 0 otherwise
 */
void json_token_buffer_set_string_state(
    json_token_buffer* tb,
    int in_escape,
    int unicode_escape_remaining,
    int high_surrogate_seen
);

/**
 * @brief Set parsing state for number token
 *
 * Sets the number parsing state in the token buffer.
 *
 * @param tb Token buffer to set state on (must not be NULL)
 * @param has_dot 1 if number contains '.', 0 otherwise
 * @param has_exp 1 if number contains 'e' or 'E', 0 otherwise
 * @param exp_sign_seen 1 if exponent sign (+/-) seen, 0 otherwise
 * @param starts_with_minus 1 if number starts with '-', 0 otherwise
 */
void json_token_buffer_set_number_state(
    json_token_buffer* tb,
    int has_dot,
    int has_exp,
    int exp_sign_seen,
    int starts_with_minus
);

/**
 * @brief Get current buffer data pointer
 *
 * Returns a pointer to the current buffer data. If the buffer is not
 * yet allocated, returns NULL.
 *
 * @param tb Token buffer (must not be NULL)
 * @return Pointer to buffer data, or NULL if not allocated
 */
const char* json_token_buffer_data(const json_token_buffer* tb);

/**
 * @brief Get current buffer length
 *
 * Returns the current length of data in the buffer.
 *
 * @param tb Token buffer (must not be NULL)
 * @return Current buffer length in bytes
 */
size_t json_token_buffer_length(const json_token_buffer* tb);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_STREAM_INTERNAL_H */
