/**
 * @file json_dom.h
 * @brief DOM (Document Object Model) API for JSON values
 *
 * This header provides functions for creating, accessing, and manipulating
 * JSON values in a DOM tree structure. All values are allocated from an
 * arena and freed via text_json_free().
 */

#ifndef GHOTI_IO_TEXT_JSON_DOM_H
#define GHOTI_IO_TEXT_JSON_DOM_H

#include <text/macros.h>
#include <text/json.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a null JSON value
 *
 * Allocates a new null value from an arena. The value and all its
 * descendants are freed via text_json_free().
 *
 * @return New null value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_null(void);

/**
 * @brief Create a boolean JSON value
 *
 * Allocates a new boolean value from an arena. The value and all its
 * descendants are freed via text_json_free().
 *
 * @param b Boolean value (0 = false, non-zero = true)
 * @return New boolean value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_bool(int b);

/**
 * @brief Create a string JSON value
 *
 * Allocates a new string value from an arena. The string data is copied
 * into the arena. The value and all its descendants are freed via
 * text_json_free().
 *
 * @param s String data (may contain null bytes)
 * @param len Length of string in bytes
 * @return New string value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_string(const char* s, size_t len);

/**
 * @brief Create a number JSON value from a lexeme string
 *
 * Allocates a new number value from an arena. The lexeme is copied into
 * the arena. The number representations (int64, uint64, double) are not
 * automatically parsed; use the parser or number parsing utilities to
 * populate them if needed.
 *
 * @param s Number lexeme string (e.g., "123", "45.67", "-1e10")
 * @param len Length of lexeme string
 * @return New number value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_number_from_lexeme(const char* s, size_t len);

/**
 * @brief Create a number JSON value from an int64
 *
 * Allocates a new number value from an arena. The int64 representation
 * is stored, and a lexeme is generated from the value. The value and all
 * its descendants are freed via text_json_free().
 *
 * @param x int64 value
 * @return New number value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_number_i64(int64_t x);

/**
 * @brief Create a number JSON value from a uint64
 *
 * Allocates a new number value from an arena. The uint64 representation
 * is stored, and a lexeme is generated from the value. The value and all
 * its descendants are freed via text_json_free().
 *
 * @param x uint64 value
 * @return New number value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_number_u64(uint64_t x);

/**
 * @brief Create a number JSON value from a double
 *
 * Allocates a new number value from an arena. The double representation
 * is stored, and a lexeme is generated from the value. The value and all
 * its descendants are freed via text_json_free().
 *
 * @param x double value
 * @return New number value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_number_double(double x);

/**
 * @brief Create an empty array JSON value
 *
 * Allocates a new empty array value from an arena. Elements can be added
 * using text_json_array_push() and related functions. The value and all
 * its descendants are freed via text_json_free().
 *
 * @return New array value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_array(void);

/**
 * @brief Create an empty object JSON value
 *
 * Allocates a new empty object value from an arena. Key-value pairs can
 * be added using text_json_object_put(). The value and all its
 * descendants are freed via text_json_free().
 *
 * @return New object value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_new_object(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_DOM_H */
