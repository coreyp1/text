/**
 * @file json_writer.h
 * @brief JSON writer infrastructure and sink abstraction
 *
 * This header provides the sink abstraction for writing JSON output to
 * various destinations (buffers, files, callbacks, etc.) and helper
 * functions for common sink types.
 */

#ifndef GHOTI_IO_TEXT_JSON_WRITER_H
#define GHOTI_IO_TEXT_JSON_WRITER_H

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write callback function type
 *
 * This callback is invoked by the writer to output JSON data chunks.
 * The callback should write the provided bytes to the destination and
 * return 0 on success, or a non-zero value on error.
 *
 * @param user User-provided context pointer
 * @param bytes Pointer to the data to write
 * @param len Number of bytes to write
 * @return 0 on success, non-zero on error
 */
typedef int (*text_json_write_fn)(void* user, const char* bytes, size_t len);

/**
 * @brief JSON output sink structure
 *
 * A sink encapsulates a write callback and user context for outputting
 * JSON data. The writer uses this abstraction to write to various
 * destinations (buffers, files, streams, etc.).
 */
typedef struct {
  text_json_write_fn write;  ///< Write callback function
  void* user;                ///< User context pointer passed to callback
} text_json_sink;

/**
 * @brief Growable buffer sink structure
 *
 * Internal structure for managing a growable buffer sink. Users should
 * use text_json_sink_buffer() to create a sink, not this structure directly.
 */
typedef struct {
  char* data;      ///< Buffer data (owned by sink)
  size_t size;     ///< Current buffer size (bytes allocated)
  size_t used;     ///< Bytes used in buffer
} text_json_buffer_sink;

/**
 * @brief Fixed buffer sink structure
 *
 * Internal structure for managing a fixed-size buffer sink. Users should
 * use text_json_sink_fixed_buffer() to create a sink, not this structure directly.
 */
typedef struct {
  char* data;      ///< Buffer data (provided by user, not owned)
  size_t size;     ///< Maximum buffer size
  size_t used;     ///< Bytes written to buffer
  int truncated;   ///< Non-zero if truncation occurred
} text_json_fixed_buffer_sink;

/**
 * @brief Create a growable buffer sink
 *
 * Creates a sink that writes to a dynamically-growing buffer. The buffer
 * is allocated and managed by the sink. Use text_json_sink_buffer_data()
 * and text_json_sink_buffer_size() to access the buffer, and
 * text_json_sink_buffer_free() to free it.
 *
 * @param sink Output parameter for the created sink
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_OOM on allocation failure
 */
TEXT_API text_json_status text_json_sink_buffer(text_json_sink* sink);

/**
 * @brief Get the buffer data from a growable buffer sink
 *
 * Returns a pointer to the buffer data. The buffer is null-terminated
 * for convenience, but may contain null bytes in the middle. Use
 * text_json_sink_buffer_size() to get the actual size.
 *
 * @param sink Sink created by text_json_sink_buffer()
 * @return Pointer to buffer data, or NULL if sink is invalid
 */
TEXT_API const char* text_json_sink_buffer_data(const text_json_sink* sink);

/**
 * @brief Get the buffer size from a growable buffer sink
 *
 * Returns the number of bytes written to the buffer (not including
 * the null terminator).
 *
 * @param sink Sink created by text_json_sink_buffer()
 * @return Number of bytes written, or 0 if sink is invalid
 */
TEXT_API size_t text_json_sink_buffer_size(const text_json_sink* sink);

/**
 * @brief Free a growable buffer sink
 *
 * Frees the buffer allocated by text_json_sink_buffer(). After calling
 * this function, the sink is invalid and should not be used.
 *
 * @param sink Sink created by text_json_sink_buffer()
 */
TEXT_API void text_json_sink_buffer_free(text_json_sink* sink);

/**
 * @brief Create a fixed-size buffer sink
 *
 * Creates a sink that writes to a fixed-size buffer provided by the caller.
 * If the output exceeds the buffer size, it will be truncated and the
 * truncated flag will be set. Use text_json_sink_fixed_buffer_used() to
 * get the number of bytes written, and text_json_sink_fixed_buffer_truncated()
 * to check if truncation occurred.
 *
 * @param sink Output parameter for the created sink
 * @param buffer Buffer to write to (must remain valid for sink lifetime)
 * @param size Maximum size of the buffer
 * @return TEXT_JSON_OK on success, TEXT_JSON_E_INVALID if buffer is NULL or size is 0
 */
TEXT_API text_json_status text_json_sink_fixed_buffer(
  text_json_sink* sink,
  char* buffer,
  size_t size
);

/**
 * @brief Get the number of bytes written to a fixed buffer sink
 *
 * Returns the number of bytes written to the buffer (may be less than
 * the buffer size if truncation occurred).
 *
 * @param sink Sink created by text_json_sink_fixed_buffer()
 * @return Number of bytes written, or 0 if sink is invalid
 */
TEXT_API size_t text_json_sink_fixed_buffer_used(const text_json_sink* sink);

/**
 * @brief Check if truncation occurred in a fixed buffer sink
 *
 * Returns non-zero if the output was truncated due to insufficient buffer
 * space, zero otherwise.
 *
 * @param sink Sink created by text_json_sink_fixed_buffer()
 * @return Non-zero if truncated, zero otherwise
 */
TEXT_API int text_json_sink_fixed_buffer_truncated(const text_json_sink* sink);

/**
 * @brief Free a fixed buffer sink
 *
 * Frees the internal structure allocated by text_json_sink_fixed_buffer().
 * After calling this function, the sink is invalid and should not be used.
 * Note: This does NOT free the buffer itself (it's owned by the caller).
 *
 * @param sink Sink created by text_json_sink_fixed_buffer()
 */
TEXT_API void text_json_sink_fixed_buffer_free(text_json_sink* sink);

/**
 * @brief Write a JSON value to a sink
 *
 * Serializes a JSON DOM value to JSON text using the provided sink and
 * write options. Supports both compact and pretty-print output modes,
 * configurable escaping, and canonical output options.
 *
 * @param sink Output sink (must not be NULL)
 * @param opt Write options (can be NULL for defaults)
 * @param v JSON value to write (must not be NULL)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_write_value(
  text_json_sink* sink,
  const text_json_write_options* opt,
  const text_json_value* v,
  text_json_error* err
);

/**
 * @brief Forward declaration of streaming writer structure
 *
 * The streaming writer maintains internal state to enforce structural
 * correctness (e.g., preventing values without keys inside objects).
 */
typedef struct text_json_writer text_json_writer;

/**
 * @brief Create a new streaming JSON writer
 *
 * Creates a new streaming writer that writes JSON output to the provided
 * sink using the specified write options. The writer enforces structural
 * correctness (e.g., prevents writing values without keys inside objects).
 *
 * @param sink Output sink (must not be NULL)
 * @param opt Write options (can be NULL for defaults)
 * @return New writer instance, or NULL on allocation failure
 */
TEXT_API text_json_writer* text_json_writer_new(
  text_json_sink sink,
  const text_json_write_options* opt
);

/**
 * @brief Free a streaming JSON writer
 *
 * Frees all resources associated with the writer. After calling this
 * function, the writer pointer is invalid and must not be used.
 *
 * @param w Writer to free (can be NULL, in which case this is a no-op)
 */
TEXT_API void text_json_writer_free(text_json_writer* w);

/**
 * @brief Begin writing an object
 *
 * Writes the opening brace `{` for a JSON object. Must be followed by
 * key-value pairs or be closed immediately with text_json_writer_object_end().
 *
 * @param w Writer instance (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure (e.g., invalid state)
 */
TEXT_API text_json_status text_json_writer_object_begin(text_json_writer* w);

/**
 * @brief End writing an object
 *
 * Writes the closing brace `}` for a JSON object. The object must have
 * been started with text_json_writer_object_begin().
 *
 * @param w Writer instance (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure (e.g., incomplete object)
 */
TEXT_API text_json_status text_json_writer_object_end(text_json_writer* w);

/**
 * @brief Begin writing an array
 *
 * Writes the opening bracket `[` for a JSON array. Must be followed by
 * values or be closed immediately with text_json_writer_array_end().
 *
 * @param w Writer instance (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure (e.g., invalid state)
 */
TEXT_API text_json_status text_json_writer_array_begin(text_json_writer* w);

/**
 * @brief End writing an array
 *
 * Writes the closing bracket `]` for a JSON array. The array must have
 * been started with text_json_writer_array_begin().
 *
 * @param w Writer instance (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure (e.g., incomplete array)
 */
TEXT_API text_json_status text_json_writer_array_end(text_json_writer* w);

/**
 * @brief Write an object key
 *
 * Writes a key string for an object key-value pair. Must be called inside
 * an object context (after object_begin, before the corresponding value).
 * The key will be properly escaped according to write options.
 *
 * @param w Writer instance (must not be NULL)
 * @param key Key string (must not be NULL)
 * @param len Length of key string in bytes
 * @return TEXT_JSON_OK on success, error code on failure (e.g., not in object context)
 */
TEXT_API text_json_status text_json_writer_key(
  text_json_writer* w,
  const char* key,
  size_t len
);

/**
 * @brief Write a null value
 *
 * Writes the JSON null value. Can be used in arrays or as object values.
 *
 * @param w Writer instance (must not be NULL)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_null(text_json_writer* w);

/**
 * @brief Write a boolean value
 *
 * Writes a JSON boolean value (true or false).
 *
 * @param w Writer instance (must not be NULL)
 * @param b Boolean value (0 = false, non-zero = true)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_bool(text_json_writer* w, int b);

/**
 * @brief Write a number value from lexeme
 *
 * Writes a JSON number value using the exact lexeme string. The lexeme
 * should be a valid JSON number format.
 *
 * @param w Writer instance (must not be NULL)
 * @param s Number lexeme string (must not be NULL)
 * @param len Length of lexeme string in bytes
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_number_lexeme(
  text_json_writer* w,
  const char* s,
  size_t len
);

/**
 * @brief Write a number value from int64
 *
 * Writes a JSON number value formatted from a signed 64-bit integer.
 *
 * @param w Writer instance (must not be NULL)
 * @param x Integer value to write
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_number_i64(
  text_json_writer* w,
  long long x
);

/**
 * @brief Write a number value from uint64
 *
 * Writes a JSON number value formatted from an unsigned 64-bit integer.
 *
 * @param w Writer instance (must not be NULL)
 * @param x Unsigned integer value to write
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_number_u64(
  text_json_writer* w,
  unsigned long long x
);

/**
 * @brief Write a number value from double
 *
 * Writes a JSON number value formatted from a double-precision floating-point
 * number. Non-finite numbers (NaN, Infinity) are only written if the
 * allow_nonfinite_numbers option is enabled.
 *
 * @param w Writer instance (must not be NULL)
 * @param x Floating-point value to write
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_number_double(
  text_json_writer* w,
  double x
);

/**
 * @brief Write a string value
 *
 * Writes a JSON string value. The string will be properly escaped according
 * to write options (escape sequences, Unicode escaping, etc.).
 *
 * @param w Writer instance (must not be NULL)
 * @param s String data (must not be NULL)
 * @param len Length of string in bytes
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_writer_string(
  text_json_writer* w,
  const char* s,
  size_t len
);

/**
 * @brief Finish writing and validate structure
 *
 * Completes the JSON output and validates that the structure is complete
 * (all objects and arrays are properly closed). Returns an error if the
 * structure is incomplete or invalid.
 *
 * @param w Writer instance (must not be NULL)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return TEXT_JSON_OK on success, error code on failure (e.g., incomplete structure)
 */
TEXT_API text_json_status text_json_writer_finish(
  text_json_writer* w,
  text_json_error* err
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_WRITER_H */
