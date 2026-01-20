/**
 * @file csv_writer.h
 * @brief CSV writer infrastructure and sink abstraction
 *
 * This header provides the sink abstraction for writing CSV output to
 * various destinations (buffers, files, callbacks, etc.) and helper
 * functions for common sink types. It also provides the streaming writer
 * API for incremental CSV construction with structural enforcement.
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

// ============================================================================
// Streaming Writer API
// ============================================================================

/**
 * @brief Opaque CSV writer structure
 *
 * The writer maintains state for incremental CSV construction and enforces
 * valid call ordering (fields only within records, etc.).
 */
typedef struct text_csv_writer text_csv_writer;

/**
 * @brief Create a new CSV writer
 *
 * Creates a new streaming writer that will write CSV data to the provided
 * sink according to the specified write options. The writer enforces
 * structural correctness (fields only within records, proper record
 * boundaries, etc.).
 *
 * @param sink Output sink (must remain valid for writer lifetime)
 * @param opts Write options (copied internally, can be freed after call)
 * @return Pointer to new writer, or NULL on failure (invalid sink/opts or OOM)
 */
TEXT_API text_csv_writer* text_csv_writer_new(
  const text_csv_sink* sink,
  const text_csv_write_options* opts
);

/**
 * @brief Begin a new CSV record
 *
 * Starts a new record. Fields can only be written between record_begin()
 * and record_end() calls. Multiple records can be written sequentially.
 *
 * @param writer Writer instance
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if writer is NULL or
 *         already in a record, or write error if sink write fails
 */
TEXT_API text_csv_status text_csv_writer_record_begin(text_csv_writer* writer);

/**
 * @brief Write a field to the current record
 *
 * Writes a field with proper quoting and escaping according to the write
 * options. Automatically inserts delimiters between fields. This function
 * can only be called between record_begin() and record_end() calls.
 *
 * @param writer Writer instance
 * @param bytes Field data (may be NULL if len is 0)
 * @param len Field length in bytes
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if writer is NULL or
 *         not in a record, or write error if sink write fails
 */
TEXT_API text_csv_status text_csv_writer_field(
  text_csv_writer* writer,
  const void* bytes,
  size_t len
);

/**
 * @brief End the current CSV record
 *
 * Ends the current record and writes the appropriate newline sequence.
 * This function can only be called after record_begin() and before the
 * next record_begin() or finish().
 *
 * @param writer Writer instance
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if writer is NULL or
 *         not in a record, or write error if sink write fails
 */
TEXT_API text_csv_status text_csv_writer_record_end(text_csv_writer* writer);

/**
 * @brief Finish writing CSV output
 *
 * Finalizes the CSV output. If a record is currently open, it will be
 * closed. If trailing_newline is enabled in options, a final newline
 * will be written. After calling finish(), the writer should not be used
 * for further writing (though free() can still be called).
 *
 * @param writer Writer instance
 * @return TEXT_CSV_OK on success, TEXT_CSV_E_INVALID if writer is NULL,
 *         or write error if sink write fails
 */
TEXT_API text_csv_status text_csv_writer_finish(text_csv_writer* writer);

/**
 * @brief Free a CSV writer
 *
 * Frees all resources associated with the writer. The sink is not freed
 * (it's owned by the caller). It is safe to call this function even if
 * finish() was not called, though finish() should be called first for
 * proper output finalization.
 *
 * @param writer Writer instance (can be NULL)
 */
TEXT_API void text_csv_writer_free(text_csv_writer* writer);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_WRITER_H */
