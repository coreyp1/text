/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Streaming CSV parser API.
 *
 * Provides an event-based streaming parser for processing CSV data
 * incrementally.
 */

#ifndef GHOTI_IO_GTEXT_CSV_CSV_STREAM_H
#define GHOTI_IO_GTEXT_CSV_CSV_STREAM_H

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

/**
 * @brief Opaque pull-model reader structure
 */
typedef struct GTEXT_CSV_Reader GTEXT_CSV_Reader;

/**
 * @brief Create a new pull-model CSV reader
 *
 * The push parser calls the caller; this lets the caller call the parser. Which
 * way round that is decides the shape of the program on top of it: a callback
 * that has to remember where it is becomes a state machine, and a loop that
 * reads a record at a time does not.
 *
 * It wraps the streaming parser and queues events for
 * gtext_csv_reader_next(). Every event's bytes are copied into that queue,
 * which is what makes the lifetime documented on gtext_csv_reader_next() true -
 * the push parser's `data` pointer lives only for the duration of its
 * callback.
 *
 * @param opts Parse options, or NULL for defaults.
 *             GTEXT_CSV_Parse_Options::allocator covers the reader, its queue
 *             and the copied bytes.
 * @return New reader, or NULL on allocation failure
 */
GTEXT_API GTEXT_CSV_Reader * gtext_csv_reader_new(
    const GTEXT_CSV_Parse_Options * opts);

/**
 * @brief Feed input to the pull reader
 *
 * To signal end of input, call with `data` NULL and `len` 0, which finishes the
 * parse and enqueues any remaining events. Doing that twice is accepted and
 * does nothing, so a loop that finishes on a short read may finish again
 * without it being an error. Feeding bytes after end of input is
 * GTEXT_CSV_E_STATE.
 *
 * @param reader Reader (must not be NULL)
 * @param data Input chunk, or NULL with len 0 for end of input
 * @param len Length of the chunk
 * @param err Error output, or NULL
 * @return GTEXT_CSV_OK, or the parse error
 */
GTEXT_API GTEXT_CSV_Status gtext_csv_reader_feed(GTEXT_CSV_Reader * reader,
    const void * data, size_t len, GTEXT_CSV_Error * err);

/**
 * @brief Take the next available event from the reader
 *
 * @param reader Reader (must not be NULL)
 * @param out_event Receives the event (must not be NULL)
 * @return GTEXT_CSV_OK when an event was available;
 *         GTEXT_CSV_E_INCOMPLETE when more input is needed;
 *         GTEXT_CSV_E_STATE when input has ended and the queue is empty;
 *         or the parse error, repeated on every later call once one has
 *         happened, so that a failure is never mistaken for the end.
 *
 * The event's `data` pointer stays valid until the next call to
 * gtext_csv_reader_next() or gtext_csv_reader_free(), whichever comes first.
 * Copy it if it must outlive that.
 */
GTEXT_API GTEXT_CSV_Status gtext_csv_reader_next(
    GTEXT_CSV_Reader * reader, GTEXT_CSV_Event * out_event);

/**
 * @brief Free the reader, its queue and any event still held
 *
 * Passing NULL is a no-op.
 */
GTEXT_API void gtext_csv_reader_free(GTEXT_CSV_Reader * reader);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_CSV_CSV_STREAM_H
