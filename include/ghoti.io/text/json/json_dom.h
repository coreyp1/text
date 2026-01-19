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

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/json/json_core.h>
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

/**
 * @brief Get the type of a JSON value
 *
 * Returns the type of the JSON value. If the value is NULL, returns
 * TEXT_JSON_NULL.
 *
 * @param v JSON value (can be NULL)
 * @return Type of the value, or TEXT_JSON_NULL if v is NULL
 */
TEXT_API text_json_type text_json_typeof(const text_json_value* v);

/**
 * @brief Get the boolean value from a JSON value
 *
 * Retrieves the boolean value from a JSON value. Returns an error if
 * the value is not a boolean.
 *
 * @param v JSON value (must not be NULL)
 * @param out Pointer to store the boolean value (0 = false, non-zero = true)
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if v is NULL or not a boolean
 */
TEXT_API text_json_status text_json_get_bool(const text_json_value* v, int* out);

/**
 * @brief Get the string value from a JSON value
 *
 * Retrieves the string data and length from a JSON value. Returns an error
 * if the value is not a string. The returned string pointer is valid for
 * the lifetime of the JSON value.
 *
 * @param v JSON value (must not be NULL)
 * @param out Pointer to store the string data pointer
 * @param out_len Pointer to store the string length in bytes
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if v is NULL or not a string
 */
TEXT_API text_json_status text_json_get_string(const text_json_value* v, const char** out, size_t* out_len);

/**
 * @brief Get the number lexeme from a JSON value
 *
 * Retrieves the original number lexeme (string representation) from a
 * JSON value. Returns an error if the value is not a number or if the
 * lexeme is not available. The returned string pointer is valid for the
 * lifetime of the JSON value.
 *
 * @param v JSON value (must not be NULL)
 * @param out Pointer to store the lexeme string pointer
 * @param out_len Pointer to store the lexeme length in bytes
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if v is NULL or not a number
 */
TEXT_API text_json_status text_json_get_number_lexeme(const text_json_value* v, const char** out, size_t* out_len);

/**
 * @brief Get the int64 value from a JSON value
 *
 * Retrieves the int64 representation from a JSON value. Returns an error
 * if the value is not a number or if the int64 representation is not
 * available.
 *
 * @param v JSON value (must not be NULL)
 * @param out Pointer to store the int64 value
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if v is NULL, not a number, or int64 not available
 */
TEXT_API text_json_status text_json_get_i64(const text_json_value* v, int64_t* out);

/**
 * @brief Get the uint64 value from a JSON value
 *
 * Retrieves the uint64 representation from a JSON value. Returns an error
 * if the value is not a number or if the uint64 representation is not
 * available.
 *
 * @param v JSON value (must not be NULL)
 * @param out Pointer to store the uint64 value
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if v is NULL, not a number, or uint64 not available
 */
TEXT_API text_json_status text_json_get_u64(const text_json_value* v, uint64_t* out);

/**
 * @brief Get the double value from a JSON value
 *
 * Retrieves the double representation from a JSON value. Returns an error
 * if the value is not a number or if the double representation is not
 * available.
 *
 * @param v JSON value (must not be NULL)
 * @param out Pointer to store the double value
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if v is NULL, not a number, or double not available
 */
TEXT_API text_json_status text_json_get_double(const text_json_value* v, double* out);

/**
 * @brief Get the size of a JSON array
 *
 * Returns the number of elements in a JSON array. Returns 0 if the value
 * is NULL or not an array.
 *
 * @param v JSON value (can be NULL)
 * @return Number of elements in the array, or 0 if v is NULL or not an array
 */
TEXT_API size_t text_json_array_size(const text_json_value* v);

/**
 * @brief Get an element from a JSON array by index
 *
 * Retrieves a pointer to an array element by index. The returned pointer
 * is valid for the lifetime of the array value. Returns NULL if the value
 * is not an array, if the index is out of bounds, or if v is NULL.
 *
 * @param v JSON value (must not be NULL)
 * @param idx Index of the element (0-based)
 * @return Pointer to the element, or NULL on error
 */
TEXT_API const text_json_value* text_json_array_get(const text_json_value* v, size_t idx);

/**
 * @brief Get the size of a JSON object
 *
 * Returns the number of key-value pairs in a JSON object. Returns 0 if
 * the value is NULL or not an object.
 *
 * @param v JSON value (can be NULL)
 * @return Number of key-value pairs in the object, or 0 if v is NULL or not an object
 */
TEXT_API size_t text_json_object_size(const text_json_value* v);

/**
 * @brief Get a key from a JSON object by index
 *
 * Retrieves the key string and length from an object by index. The returned
 * string pointer is valid for the lifetime of the object value. Returns
 * NULL if the value is not an object, if the index is out of bounds, or
 * if v is NULL.
 *
 * @param v JSON value (must not be NULL)
 * @param idx Index of the key-value pair (0-based)
 * @param key_len Pointer to store the key length in bytes (can be NULL)
 * @return Pointer to the key string, or NULL on error
 */
TEXT_API const char* text_json_object_key(const text_json_value* v, size_t idx, size_t* key_len);

/**
 * @brief Get a value from a JSON object by index
 *
 * Retrieves a pointer to an object value by index. The returned pointer
 * is valid for the lifetime of the object value. Returns NULL if the
 * value is not an object, if the index is out of bounds, or if v is NULL.
 *
 * @param v JSON value (must not be NULL)
 * @param idx Index of the key-value pair (0-based)
 * @return Pointer to the value, or NULL on error
 */
TEXT_API const text_json_value* text_json_object_value(const text_json_value* v, size_t idx);

/**
 * @brief Get a value from a JSON object by key
 *
 * Searches for a key in the object and returns a pointer to its value.
 * The key comparison is exact (case-sensitive, byte-for-byte). The returned
 * pointer is valid for the lifetime of the object value. Returns NULL if
 * the value is not an object, if the key is not found, or if v is NULL.
 *
 * @param v JSON value (must not be NULL)
 * @param key Key string to search for (must not be NULL)
 * @param key_len Length of the key string in bytes
 * @return Pointer to the value, or NULL if key not found or on error
 */
TEXT_API const text_json_value* text_json_object_get(const text_json_value* v, const char* key, size_t key_len);

/**
 * @brief Add an element to the end of a JSON array
 *
 * Appends a value to the end of an array. The array will grow automatically
 * if needed. The value becomes part of the array's DOM tree.
 *
 * @param arr Array value (must not be NULL, must be TEXT_JSON_ARRAY type)
 * @param child Value to add to the array (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_array_push(text_json_value* arr, text_json_value* child);

/**
 * @brief Set an element in a JSON array by index
 *
 * Replaces the value at the specified index in the array. The index must
 * be within the current bounds of the array.
 *
 * @param arr Array value (must not be NULL, must be TEXT_JSON_ARRAY type)
 * @param idx Index of the element to set (0-based, must be < array size)
 * @param child Value to set at the index (must not be NULL)
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if index is out of bounds
 */
TEXT_API text_json_status text_json_array_set(text_json_value* arr, size_t idx, text_json_value* child);

/**
 * @brief Insert an element into a JSON array at a specific index
 *
 * Inserts a value at the specified index, shifting existing elements to
 * the right. The index can be equal to the array size (equivalent to push).
 * The array will grow automatically if needed.
 *
 * @param arr Array value (must not be NULL, must be TEXT_JSON_ARRAY type)
 * @param idx Index where to insert (0-based, must be <= array size)
 * @param child Value to insert (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_array_insert(text_json_value* arr, size_t idx, text_json_value* child);

/**
 * @brief Remove an element from a JSON array by index
 *
 * Removes the value at the specified index, shifting remaining elements
 * to the left to fill the gap. The index must be within the current
 * bounds of the array.
 *
 * @param arr Array value (must not be NULL, must be TEXT_JSON_ARRAY type)
 * @param idx Index of the element to remove (0-based, must be < array size)
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if index is out of bounds
 */
TEXT_API text_json_status text_json_array_remove(text_json_value* arr, size_t idx);

/**
 * @brief Add or replace a key-value pair in a JSON object
 *
 * If the key already exists in the object, its value is replaced.
 * If the key does not exist, a new key-value pair is added.
 * The key string is copied into the object's arena.
 *
 * @param obj Object value (must not be NULL, must be TEXT_JSON_OBJECT type)
 * @param key Key string (must not be NULL)
 * @param key_len Length of key string in bytes
 * @param val Value to associate with the key (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_object_put(text_json_value* obj, const char* key, size_t key_len, text_json_value* val);

/**
 * @brief Remove a key-value pair from a JSON object
 *
 * Removes the key-value pair with the specified key from the object.
 * If the key is not found, returns an error. The key comparison is
 * exact (case-sensitive, byte-for-byte).
 *
 * @param obj Object value (must not be NULL, must be TEXT_JSON_OBJECT type)
 * @param key Key string to remove (must not be NULL)
 * @param key_len Length of key string in bytes
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if key is not found
 */
TEXT_API text_json_status text_json_object_remove(text_json_value* obj, const char* key, size_t key_len);

/**
 * @brief Parse JSON input into a DOM tree
 *
 * Parses a JSON input string and returns a DOM tree representing the JSON
 * structure. The returned value and all its descendants are allocated from
 * an arena and must be freed using text_json_free().
 *
 * The parser enforces strict JSON grammar by default, but can be configured
 * via parse options to allow extensions like comments, trailing commas, etc.
 *
 * This function parses a single JSON value. Any trailing content after the
 * value will result in an error. For parsing multiple top-level values from
 * the same buffer, use text_json_parse_multiple() instead.
 *
 * @param bytes Input JSON string (must not be NULL)
 * @param len Length of input string in bytes
 * @param opt Parse options (can be NULL for defaults)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return Root JSON value on success, NULL on error (check err for details)
 */
TEXT_API text_json_value* text_json_parse(
    const char* bytes,
    size_t len,
    const text_json_parse_options* opt,
    text_json_error* err
);

/**
 * @brief Parse a single JSON value from input, returning bytes consumed
 *
 * Parses a single JSON value from the input buffer and returns the number
 * of bytes consumed. This allows the caller to continue parsing subsequent
 * values from the same buffer by advancing the pointer by the bytes consumed.
 *
 * Unlike text_json_parse(), this function allows trailing content after
 * the parsed value. The bytes_consumed parameter indicates where the next
 * value begins (or end of input if no more values).
 *
 * @param bytes Input JSON string (must not be NULL)
 * @param len Length of input string in bytes
 * @param opt Parse options (can be NULL for defaults)
 * @param err Error output structure (can be NULL if error details not needed)
 * @param bytes_consumed Output parameter for number of bytes consumed (must not be NULL)
 * @return Root JSON value on success, NULL on error (check err for details)
 */
TEXT_API text_json_value* text_json_parse_multiple(
    const char* bytes,
    size_t len,
    const text_json_parse_options* opt,
    text_json_error* err,
    size_t* bytes_consumed
);

/**
 * @brief Equality comparison mode for deep equality
 */
typedef enum {
    TEXT_JSON_EQUAL_LEXEME,      ///< Compare numbers by lexeme (exact string match)
    TEXT_JSON_EQUAL_NUMERIC      ///< Compare numbers by numeric equivalence (int64/uint64/double)
} text_json_equal_mode;

/**
 * @brief Deep equality comparison for JSON values
 *
 * Performs a deep structural comparison of two JSON values. Returns 1 if
 * the values are equal (same structure and content), 0 otherwise.
 *
 * For numbers, the comparison mode determines how equality is checked:
 * - TEXT_JSON_EQUAL_LEXEME: Numbers must have identical lexemes (exact string match)
 * - TEXT_JSON_EQUAL_NUMERIC: Numbers are compared using numeric equivalence
 *   (int64, uint64, or double representations with epsilon for doubles)
 *
 * For objects, keys are compared regardless of insertion order.
 *
 * @param a First value to compare (can be NULL)
 * @param b Second value to compare (can be NULL)
 * @param mode Equality comparison mode for numbers
 * @return 1 if values are equal, 0 otherwise
 */
TEXT_API int text_json_equal(const text_json_value* a, const text_json_value* b, text_json_equal_mode mode);

/**
 * @brief Deep clone a JSON value into a new arena
 *
 * Creates a deep copy of a JSON value, allocating all memory from a new
 * arena. The cloned value is independent of the original and must be freed
 * separately using text_json_free().
 *
 * @param src Source value to clone (must not be NULL)
 * @return Cloned value, or NULL on allocation failure
 */
TEXT_API text_json_value* text_json_clone(const text_json_value* src);

/**
 * @brief Object merge conflict policy
 */
typedef enum {
    TEXT_JSON_MERGE_FIRST_WINS,  ///< Keep value from first object on conflict
    TEXT_JSON_MERGE_LAST_WINS,   ///< Keep value from second object on conflict
    TEXT_JSON_MERGE_ERROR        ///< Return error on conflict
} text_json_merge_policy;

/**
 * @brief Merge two JSON objects with configurable conflict policy
 *
 * Merges two JSON objects into the first object. If a key exists in both
 * objects, the conflict policy determines which value is kept:
 * - TEXT_JSON_MERGE_FIRST_WINS: Keep the value from the first object
 * - TEXT_JSON_MERGE_LAST_WINS: Keep the value from the second object (replace)
 * - TEXT_JSON_MERGE_ERROR: Return an error if a conflict is detected
 *
 * Nested objects are merged recursively. Arrays and other value types are
 * replaced entirely (not merged).
 *
 * This function is distinct from JSON Merge Patch (RFC 7386) semantics.
 * Use text_json_merge_patch() for RFC 7386-compliant merging.
 *
 * @param target Target object to merge into (must not be NULL, must be TEXT_JSON_OBJECT)
 * @param source Source object to merge from (must not be NULL, must be TEXT_JSON_OBJECT)
 * @param policy Conflict resolution policy
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_object_merge(
    text_json_value* target,
    const text_json_value* source,
    text_json_merge_policy policy
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_DOM_H */
