/**
 * @file csv_stream.h
 * @brief Streaming CSV parser API
 *
 * Provides an event-based streaming parser for processing CSV data incrementally.
 */

#ifndef GHOTI_IO_TEXT_CSV_STREAM_H
#define GHOTI_IO_TEXT_CSV_STREAM_H

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/csv/csv_core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CSV event types
 */
typedef enum {
    TEXT_CSV_EVENT_RECORD_BEGIN,  ///< Start of a new record
    TEXT_CSV_EVENT_FIELD,          ///< A field value (data provided)
    TEXT_CSV_EVENT_RECORD_END,     ///< End of current record
    TEXT_CSV_EVENT_END             ///< End of input (parsing complete)
} text_csv_event_type;

/**
 * @brief CSV event structure
 *
 * Contains event type and associated data for streaming parser events.
 */
typedef struct {
    text_csv_event_type type;      ///< Event type
    const char* data;              ///< Field data (for FIELD events, NULL otherwise)
    size_t data_len;               ///< Field data length (for FIELD events, 0 otherwise)
    size_t row_index;               ///< Row index (0-based, for FIELD/RECORD events)
    size_t col_index;               ///< Column index (0-based, for FIELD events)
} text_csv_event;

/**
 * @brief Event callback function type
 *
 * Called by the streaming parser for each event.
 *
 * @param event Event data
 * @param user_data User-provided context
 * @return TEXT_CSV_OK to continue, or error code to stop parsing
 */
typedef text_csv_status (*text_csv_event_cb)(
    const text_csv_event* event,
    void* user_data
);

/**
 * @brief Opaque streaming parser structure
 */
typedef struct text_csv_stream text_csv_stream;

/**
 * @brief Create a new streaming CSV parser
 *
 * @param opts Parse options (can be NULL for defaults)
 * @param callback Event callback function (must not be NULL)
 * @param user_data User context passed to callback
 * @return New stream parser, or NULL on failure
 */
TEXT_API text_csv_stream* text_csv_stream_new(
    const text_csv_parse_options* opts,
    text_csv_event_cb callback,
    void* user_data
);

/**
 * @brief Feed data to the streaming parser
 *
 * Processes the provided data incrementally and emits events via the callback.
 * Can be called multiple times with different chunks of data.
 *
 * @param stream Stream parser (must not be NULL)
 * @param data Input data chunk
 * @param len Length of input data
 * @param err Error output structure (can be NULL)
 * @return TEXT_CSV_OK on success, or error code
 */
TEXT_API text_csv_status text_csv_stream_feed(
    text_csv_stream* stream,
    const void* data,
    size_t len,
    text_csv_error* err
);

/**
 * @brief Finish parsing and emit final events
 *
 * Should be called after all data has been fed. Emits RECORD_END if a record
 * is in progress, then emits END event.
 *
 * @param stream Stream parser (must not be NULL)
 * @param err Error output structure (can be NULL)
 * @return TEXT_CSV_OK on success, or error code
 */
TEXT_API text_csv_status text_csv_stream_finish(
    text_csv_stream* stream,
    text_csv_error* err
);

/**
 * @brief Free a streaming parser
 *
 * @param stream Stream parser to free (can be NULL)
 */
TEXT_API void text_csv_stream_free(text_csv_stream* stream);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_CSV_STREAM_H */
