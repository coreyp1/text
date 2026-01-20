/**
 * @file csv_writer.h
 * @brief CSV writer infrastructure and sink abstraction
 *
 * This header provides the sink abstraction for writing CSV output to
 * various destinations (buffers, files, callbacks, etc.) and helper
 * functions for common sink types.
 */

#ifndef GHOTI_IO_TEXT_CSV_WRITER_H
#define GHOTI_IO_TEXT_CSV_WRITER_H

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/csv/csv_core.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write callback function type
 *
 * This callback is invoked by the writer to output CSV data chunks.
 * The callback should write the provided bytes to the destination and
 * return TEXT_CSV_OK on success, or an error code on failure.
 *
 * @param user User-provided context pointer
 * @param bytes Pointer to the data to write
 * @param len Number of bytes to write
 * @return TEXT_CSV_OK on success, error code on failure
 */
typedef text_csv_status (*text_csv_write_fn)(void* user, const char* bytes, size_t len);

/**
 * @brief CSV output sink structure
 *
 * A sink encapsulates a write callback and user context for outputting
 * CSV data. The writer uses this abstraction to write to various
 * destinations (buffers, files, streams, etc.).
 */
typedef struct {
  text_csv_write_fn write;  ///< Write callback function
  void* user;               ///< User context pointer passed to callback
} text_csv_sink;

/**
 * @brief Growable buffer sink structure
 *
 * Internal structure for managing a growable buffer sink. Users should
 * use text_csv_sink_buffer() to create a sink, not this structure directly.
 */
typedef struct {
  char* data;      ///< Buffer data (owned by sink)
  size_t size;     ///< Current buffer size (bytes allocated)
  size_t used;     ///< Bytes used in buffer
} text_csv_buffer_sink;

/**
 * @brief Fixed buffer sink structure
 *
 * Internal structure for managing a fixed-size buffer sink. Users should
 * use text_csv_sink_fixed_buffer() to create a sink, not this structure directly.
 */
typedef struct {
  char* data;      ///< Buffer data (provided by user, not owned)
  size_t size;     ///< Maximum buffer size
  size_t used;     ///< Bytes written to buffer
  bool truncated;  ///< true if truncation occurred
} text_csv_fixed_buffer_sink;

/**
 * @brief Create a growable buffer sink
 *
 * Creates a sink that writes to a dynamically-growing buffer. The buffer
 * is allocated and managed by the sink. Use text_csv_sink_buffer_data()
 * and text_csv_sink_buffer_size() to access the buffer, and
 * text_csv_sink_buffer_free() to free it.
 *
 * @param sink Output parameter for the created sink
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_OOM on allocation failure
 */
TEXT_API text_csv_status text_csv_sink_buffer(text_csv_sink* sink);

/**
 * @brief Get the buffer data from a growable buffer sink
 *
 * Returns a pointer to the buffer data. The buffer is null-terminated
 * for convenience, but may contain null bytes in the middle. Use
 * text_csv_sink_buffer_size() to get the actual size.
 *
 * @param sink Sink created by text_csv_sink_buffer()
 * @return Pointer to buffer data, or NULL if sink is invalid
 */
TEXT_API const char* text_csv_sink_buffer_data(const text_csv_sink* sink);

/**
 * @brief Get the buffer size from a growable buffer sink
 *
 * Returns the number of bytes written to the buffer (not including
 * the null terminator).
 *
 * @param sink Sink created by text_csv_sink_buffer()
 * @return Number of bytes written, or 0 if sink is invalid
 */
TEXT_API size_t text_csv_sink_buffer_size(const text_csv_sink* sink);

/**
 * @brief Free a growable buffer sink
 *
 * Frees the buffer allocated by text_csv_sink_buffer(). After calling
 * this function, the sink is invalid and should not be used.
 *
 * @param sink Sink created by text_csv_sink_buffer()
 */
TEXT_API void text_csv_sink_buffer_free(text_csv_sink* sink);

/**
 * @brief Create a fixed-size buffer sink
 *
 * Creates a sink that writes to a fixed-size buffer provided by the caller.
 * If the output exceeds the buffer size, it will be truncated and the
 * truncated flag will be set. Use text_csv_sink_fixed_buffer_used() to
 * get the number of bytes written, and text_csv_sink_fixed_buffer_truncated()
 * to check if truncation occurred.
 *
 * @param sink Output parameter for the created sink
 * @param buffer Buffer to write to (must remain valid for sink lifetime)
 * @param size Maximum size of the buffer
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if buffer is NULL or size is 0
 */
TEXT_API text_csv_status text_csv_sink_fixed_buffer(
  text_csv_sink* sink,
  char* buffer,
  size_t size
);

/**
 * @brief Get the number of bytes written to a fixed buffer sink
 *
 * Returns the number of bytes written to the buffer (may be less than
 * the buffer size if truncation occurred).
 *
 * @param sink Sink created by text_csv_sink_fixed_buffer()
 * @return Number of bytes written, or 0 if sink is invalid
 */
TEXT_API size_t text_csv_sink_fixed_buffer_used(const text_csv_sink* sink);

/**
 * @brief Check if truncation occurred in a fixed buffer sink
 *
 * Returns true if the output was truncated due to insufficient buffer
 * space, false otherwise.
 *
 * @param sink Sink created by text_csv_sink_fixed_buffer()
 * @return true if truncated, false otherwise
 */
TEXT_API bool text_csv_sink_fixed_buffer_truncated(const text_csv_sink* sink);

/**
 * @brief Free a fixed buffer sink
 *
 * Frees the internal structure allocated by text_csv_sink_fixed_buffer().
 * After calling this function, the sink is invalid and should not be used.
 * Note: This does NOT free the buffer itself (it's owned by the caller).
 *
 * @param sink Sink created by text_csv_sink_fixed_buffer()
 */
TEXT_API void text_csv_sink_fixed_buffer_free(text_csv_sink* sink);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_WRITER_H */
