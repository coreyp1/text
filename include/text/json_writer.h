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

#include <text/macros.h>
#include <text/json.h>
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

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_WRITER_H */
