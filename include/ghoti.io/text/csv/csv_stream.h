/**
 * @file
 *
 * Streaming CSV parser API.
 *
 * Provides an event-based streaming parser for processing CSV data
 * incrementally.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_CSV_STREAM_H
#define GHOTI_IO_GTEXT_CSV_STREAM_H

#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CSV event types
 */
typedef enum {
  GTEXT_CSV_EVENT_RECORD_BEGIN, ///< Start of a new record
  GTEXT_CSV_EVENT_FIELD,        ///< A field value (data provided)
  GTEXT_CSV_EVENT_RECORD_END,   ///< End of current record
  GTEXT_CSV_EVENT_END           ///< End of input (parsing complete)
} GTEXT_CSV_Event_Type;

/**
 * @brief CSV event structure
 *
 * Contains event type and associated data for streaming parser events.
 */
typedef struct {
  GTEXT_CSV_Event_Type type; ///< Event type
  const char * data;         ///< Field data (for FIELD events, NULL otherwise)
  size_t data_len;  ///< Field data length (for FIELD events, 0 otherwise)
  size_t row_index; ///< Row index (0-based, for FIELD/RECORD events)
  size_t col_index; ///< Column index (0-based, for FIELD events)
} GTEXT_CSV_Event;

/**
 * @brief Event callback function type
 *
 * Called by the streaming parser for each event.
 *
 * @param event Event data
 * @param user_data User-provided context
 * @return GTEXT_CSV_OK to continue, or error code to stop parsing
 */
typedef GTEXT_CSV_Status (*GTEXT_CSV_Event_cb)(
    const GTEXT_CSV_Event * event, void * user_data);

/**
 * @brief Opaque streaming parser structure
 */
typedef struct GTEXT_CSV_Stream GTEXT_CSV_Stream;

/**
 * @brief Create a new streaming CSV parser
 *
 * @param opts Parse options (can be NULL for defaults)
 * @param callback Event callback function (must not be NULL)
 * @param user_data User context passed to callback
 * @return New stream parser, or NULL on failure
 */
GTEXT_API GTEXT_CSV_Stream * gtext_csv_stream_new(
    const GTEXT_CSV_Parse_Options * opts, GTEXT_CSV_Event_cb callback,
    void * user_data);

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
 * @return GTEXT_CSV_OK on success, or error code
 */
GTEXT_API GTEXT_CSV_Status gtext_csv_stream_feed(GTEXT_CSV_Stream * stream,
    const void * data, size_t len, GTEXT_CSV_Error * err);

/**
 * @brief Finish parsing and emit final events
 *
 * Should be called after all data has been fed. Emits RECORD_END if a record
 * is in progress, then emits END event.
 *
 * @param stream Stream parser (must not be NULL)
 * @param err Error output structure (can be NULL)
 * @return GTEXT_CSV_OK on success, or error code
 */
GTEXT_API GTEXT_CSV_Status gtext_csv_stream_finish(
    GTEXT_CSV_Stream * stream, GTEXT_CSV_Error * err);

/**
 * @brief Free a streaming parser
 *
 * @param stream Stream parser to free (can be NULL)
 */
GTEXT_API void gtext_csv_stream_free(GTEXT_CSV_Stream * stream);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GTEXT_CSV_STREAM_H */
