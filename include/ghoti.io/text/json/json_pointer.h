/**
 * @file
 *
 * JSON Pointer (RFC 6901) evaluation on JSON DOM.
 *
 * This header provides functions for evaluating JSON Pointer strings against
 * JSON DOM trees. JSON Pointers allow accessing nested values using a
 * path-like syntax (e.g., "/a/0/b").
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_TEXT_JSON_POINTER_H
#define GHOTI_IO_TEXT_POINTER_H

#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get a value from a JSON DOM using a JSON Pointer
 *
 * Evaluates a JSON Pointer string against a JSON DOM tree and returns
 * a pointer to the referenced value. The returned pointer is valid for
 * the lifetime of the DOM tree.
 *
 * JSON Pointer format (RFC 6901):
 * - Empty string ("") refers to the root value
 * - "/" followed by reference tokens separated by "/"
 * - Reference tokens can contain escape sequences:
 *   - `~0` represents `~`
 *   - `~1` represents `/`
 * - Array indices are numeric strings (e.g., "0", "1")
 * - Object keys are reference tokens
 *
 * Examples:
 * - "" -> root value
 * - "/a" -> value at key "a" in root object
 * - "/0" -> first element of root array
 * - "/a/0/b" -> value at key "b" in first element of array at key "a"
 *
 * @param root Root JSON value to evaluate pointer against (must not be NULL)
 * @param ptr JSON Pointer string (must not be NULL)
 * @param len Length of pointer string in bytes
 * @return Pointer to the referenced value, or NULL if:
 *   - root is NULL
 *   - ptr is NULL
 *   - Pointer string is invalid
 *   - Referenced path does not exist
 *   - Array index is out of bounds
 *   - Object key is not found
 */
GTEXT_API const GTEXT_JSON_Value * gtext_json_pointer_get(
    const GTEXT_JSON_Value * root, const char * ptr, size_t len);

/**
 * @brief Get a mutable value from a JSON DOM using a JSON Pointer
 *
 * Same as gtext_json_pointer_get(), but returns a mutable pointer that
 * allows modification of the referenced value. The returned pointer is
 * valid for the lifetime of the DOM tree.
 *
 * @param root Root JSON value to evaluate pointer against (must not be NULL)
 * @param ptr JSON Pointer string (must not be NULL)
 * @param len Length of pointer string in bytes
 * @return Mutable pointer to the referenced value, or NULL on error
 *   (see gtext_json_pointer_get() for error conditions)
 */
GTEXT_API GTEXT_JSON_Value * gtext_json_pointer_get_mut(
    GTEXT_JSON_Value * root, const char * ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_POINTER_H */
