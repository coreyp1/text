/**
 * @file json_stream.h
 * @brief Streaming (incremental) JSON parser API
 *
 * This header provides an event-based streaming parser that accepts input
 * in chunks and emits events for each JSON value encountered. This is useful
 * for parsing large JSON documents without building a full DOM tree in memory.
 */

#ifndef GHOTI_IO_TEXT_JSON_STREAM_H
#define GHOTI_IO_TEXT_JSON_STREAM_H

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/json/json_core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event types emitted by the streaming parser
 */
typedef enum {
  TEXT_JSON_EVT_NULL,          ///< null value
  TEXT_JSON_EVT_BOOL,          ///< boolean value (true/false)
  TEXT_JSON_EVT_NUMBER,        ///< number value (lexeme always available)
  TEXT_JSON_EVT_STRING,        ///< string value (decoded UTF-8)
  TEXT_JSON_EVT_ARRAY_BEGIN,   ///< Array start marker
  TEXT_JSON_EVT_ARRAY_END,     ///< Array end marker
  TEXT_JSON_EVT_OBJECT_BEGIN,   ///< Object start marker
  TEXT_JSON_EVT_OBJECT_END,     ///< Object end marker
  TEXT_JSON_EVT_KEY            ///< Object key (before value)
} text_json_event_type;

/**
 * @brief Event structure emitted by the streaming parser
 *
 * Contains the event type and associated data. String and number data
 * are valid only for the duration of the callback invocation.
 */
typedef struct {
  text_json_event_type type;   ///< Event type
  union {
    bool boolean;               ///< For TEXT_JSON_EVT_BOOL
    struct {
      const char* s;            ///< String data (decoded UTF-8, null-terminated)
      size_t len;               ///< String length in bytes
    } str;                      ///< For TEXT_JSON_EVT_STRING and TEXT_JSON_EVT_KEY
    struct {
      const char* s;            ///< Number lexeme (exact token text)
      size_t len;               ///< Lexeme length in bytes
    } number;                   ///< For TEXT_JSON_EVT_NUMBER
  } as;
} text_json_event;

/**
 * @brief Event callback function type
 *
 * Called by the streaming parser for each event encountered. The callback
 * should return TEXT_JSON_OK to continue parsing, or a non-zero error code
 * to stop parsing.
 *
 * @param user User-provided context pointer
 * @param evt Event structure (valid only during callback)
 * @param err Error structure (can be populated by callback to report errors)
 * @return TEXT_JSON_OK to continue, non-zero to stop parsing
 */
typedef text_json_status (*text_json_event_cb)(
  void* user,
  const text_json_event* evt,
  text_json_error* err
);

/**
 * @brief Forward declaration of streaming parser structure
 *
 * The actual structure is defined internally. Streams are created via
 * text_json_stream_new() and freed via text_json_stream_free().
 */
typedef struct text_json_stream text_json_stream;

/**
 * @brief Create a new streaming parser
 *
 * Creates a new streaming parser instance with the specified parse options
 * and event callback. The parser accepts input via text_json_stream_feed()
 * and emits events through the callback.
 *
 * **Parameter Validation:**
 * - If `cb` is NULL, returns NULL (callback is required)
 * - If `opt` is NULL, uses default parse options
 *
 * **Error Handling:**
 * - Returns NULL on allocation failure
 * - All resources are cleaned up automatically on failure
 *
 * **Resource Cleanup:**
 * - Caller must free returned stream using text_json_stream_free()
 * - Stream must be freed even if feed/finish operations fail
 *
 * @param opt Parse options (can be NULL for defaults)
 * @param cb Event callback function (must not be NULL)
 * @param user User context pointer passed to callback
 * @return New stream instance, or NULL on allocation failure
 */
TEXT_API text_json_stream* text_json_stream_new(
  const text_json_parse_options* opt,
  text_json_event_cb cb,
  void* user
);

/**
 * @brief Feed input data to the streaming parser
 *
 * Processes the provided input chunk and emits events through the callback.
 * The parser maintains state between calls, allowing incremental parsing
 * of large inputs.
 *
 * **Multi-Chunk Value Handling:**
 * The parser correctly handles values (strings, numbers) that span multiple
 * chunks. When a value is incomplete at the end of a chunk, the parser
 * preserves state and waits for more input. Values can span an unlimited
 * number of chunks, limited only by the `max_total_bytes` option (default: 64MB).
 *
 * **Examples:**
 * - String spanning chunks: `"hello` (chunk 1) + `world"` (chunk 2) -> `"helloworld"`
 * - Number spanning chunks: `12345` (chunk 1) + `.678` (chunk 2) -> `12345.678`
 * - Values can span 2, 3, 100, or more chunks as long as total size is within limits
 *
 * **Important:** If the last value in the JSON is incomplete at the end of
 * the final chunk, it will not be emitted until `text_json_stream_finish()`
 * is called. Always call `finish()` after feeding all input to ensure all
 * values are processed and emitted.
 *
 * **Parameter Validation:**
 * - If `st` is NULL, returns TEXT_JSON_E_INVALID
 * - If `bytes` is NULL, returns TEXT_JSON_E_INVALID
 * - If `len` exceeds SIZE_MAX/2, returns TEXT_JSON_E_INVALID
 *   (prevents obvious overflow in internal calculations)
 * - If `err` is NULL, error details are not populated
 * - State validation ensures stream is in a valid state for feeding
 *
 * **Overflow Protection:**
 * - All arithmetic operations are protected against integer overflow
 * - Input size validation prevents overflow in buffer calculations
 * - String length, container size, and total bytes are validated against limits
 * - Buffer growth operations use overflow-safe calculations
 *
 * **Error Handling:**
 * - Returns error code on failure (parse error, limit exceeded, state error)
 * - Error details are populated in `err` structure if provided
 * - Error structure includes position information (offset, line, column)
 * - Context snippets are generated for better error diagnostics
 * - Stream enters error state on failure (subsequent operations return error)
 *
 * **Resource Cleanup:**
 * - On error: stream state is preserved for error reporting
 * - Buffered data is maintained until stream is freed
 * - Error context snippets (if generated) must be freed via text_json_error_free()
 *
 * @param st Stream instance (must not be NULL)
 * @param bytes Input data (must not be NULL)
 * @param len Length of input data in bytes
 * @param err Error structure for error reporting (can be NULL)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_stream_feed(
  text_json_stream* st,
  const char* bytes,
  size_t len,
  text_json_error* err
);

/**
 * @brief Finish parsing and validate structure
 *
 * Signals that no more input will be provided. This function:
 * - Processes any remaining buffered input (including incomplete values)
 * - Emits any final events that were waiting for completion
 * - Validates that the JSON structure is complete (no unmatched brackets, etc.)
 *
 * **Important:** Always call this function after feeding all input chunks.
 * The last value may not be emitted until `finish()` is called, especially
 * if it was incomplete at the end of the final chunk (e.g., a number ending
 * with a digit, or a string without a closing quote).
 *
 * **Parameter Validation:**
 * - If `st` is NULL, returns TEXT_JSON_E_INVALID
 * - If `err` is NULL, error details are not populated
 * - State validation ensures stream is in a valid state for finishing
 *
 * **Error Handling:**
 * - Returns error code on failure (incomplete structure, parse error)
 * - Error details are populated in `err` structure if provided
 * - Error structure includes position information (offset, line, column)
 * - Stream enters error state on failure
 *
 * **Resource Cleanup:**
 * - On success: stream is ready for freeing (no more operations needed)
 * - On error: stream state is preserved for error reporting
 * - Error context snippets (if generated) must be freed via text_json_error_free()
 *
 * @param st Stream instance (must not be NULL)
 * @param err Error structure for error reporting (can be NULL)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_stream_finish(
  text_json_stream* st,
  text_json_error* err
);

/**
 * @brief Free a streaming parser instance
 *
 * Frees all resources associated with the stream, including any buffered
 * data. After calling this function, the stream pointer is invalid.
 *
 * **Parameter Validation:**
 * - If `st` is NULL, this function is a no-op (safe to call with NULL)
 *
 * **Resource Cleanup:**
 * - Frees all internal buffers (input buffer, token buffer, stack)
 * - Frees any error context snippets that were allocated
 * - All resources are properly cleaned up (no memory leaks)
 * - Safe to call even if stream is in an error state
 *
 * @param st Stream instance to free (can be NULL, in which case this is a no-op)
 */
TEXT_API void text_json_stream_free(text_json_stream* st);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_STREAM_H */
