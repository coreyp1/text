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
 * The pull-model CSV reader.
 *
 * The push parser calls the caller; this lets the caller call the parser. Which
 * way round that is decides the shape of the program built on top: a push
 * parser forces a state machine in the callback for anything that has to
 * remember where it is, and a `for` loop that reads a record at a time does
 * not. YAML has had one; JSON and CSV had not, which comparison.md lists as
 * finding 6.
 *
 * It wraps the push parser rather than reimplementing anything, which is how
 * the YAML reader is built too: a private callback enqueues each event, and
 * gtext_csv_reader_next() takes them off the front.
 *
 * **Every event's bytes are copied into the queue.** The push callback's
 * `data` pointer is only valid for the duration of the call - it may point into
 * the caller's chunk, or into a field buffer the parser is about to reuse or
 * grow - so holding it until the caller asks for the event would be a
 * use-after-free that in-situ mode makes near-certain. The copy is what makes
 * the documented lifetime ("valid until the next call to
 * gtext_csv_reader_next()") true, and it is why this reader allocates where the
 * push parser does not.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <ghoti.io/text/macros.h>

#include "csv_internal.h"
#include "csv_stream_internal.h"

typedef struct {
  GTEXT_CSV_Event * items;
  size_t head;
  size_t count;
  size_t capacity;
} csv_event_queue;

struct GTEXT_CSV_Reader {
  GTEXT_CSV_Stream * stream;
  csv_event_queue queue;
  /// The event gtext_csv_reader_next() handed out last, still owned here so
  /// that its bytes stay valid until the call after it.
  GTEXT_CSV_Event last_event;
  bool has_last_event;
  /// Set once the caller has signalled end of input, so a second finish is not
  /// pushed through the parser.
  bool finished;
  /// The first error the parser reported, kept so that every later call says
  /// the same thing rather than "no more events".
  GTEXT_CSV_Status failed;
  const GTEXT_Allocator * alloc;
};

// ===========================================================================
// Events
// ===========================================================================

static void event_zero(GTEXT_CSV_Event * event) {
  memset(event, 0, sizeof(*event));
}

static void event_free(const GTEXT_Allocator * alloc, GTEXT_CSV_Event * event) {
  if (!event) {
    return;
  }
  // Cast away const: the queue owns this copy, whatever the public type says
  // about the parser's own events.
  gtext_allocator_free(alloc, (void *)(uintptr_t)event->data);
  event_zero(event);
}

/* A field of zero bytes is a real field, and distinct from no field at all, so
   an empty one still gets a one-byte buffer rather than NULL. Otherwise
   `data == NULL` would mean both "empty field" and "not a field event". */
static bool event_copy(const GTEXT_Allocator * alloc,
    const GTEXT_CSV_Event * src, GTEXT_CSV_Event * dst) {
  event_zero(dst);
  dst->type = src->type;
  dst->data_len = src->data_len;
  dst->row_index = src->row_index;
  dst->col_index = src->col_index;

  if (src->type != GTEXT_CSV_EVENT_FIELD) {
    return true;
  }

  char * bytes = (char *)gtext_allocator_malloc(alloc, src->data_len + 1);
  if (!bytes) {
    return false;
  }
  if (src->data_len > 0 && src->data) {
    memcpy(bytes, src->data, src->data_len);
  }
  bytes[src->data_len] = '\0';
  dst->data = bytes;
  return true;
}

// ===========================================================================
// The queue
// ===========================================================================

static bool queue_reserve(
    const GTEXT_Allocator * alloc, csv_event_queue * queue, size_t needed) {
  if (queue->capacity >= needed) {
    return true;
  }

  size_t new_cap = queue->capacity == 0 ? 8 : queue->capacity * 2;
  while (new_cap < needed) {
    if (new_cap > SIZE_MAX / 2) {
      return false;
    }
    new_cap *= 2;
  }

  GTEXT_CSV_Event * items = (GTEXT_CSV_Event *)gtext_allocator_calloc(
      alloc, new_cap, sizeof(*items));
  if (!items) {
    return false;
  }

  // Unwrap the ring into the front of the new array, so `head` starts over.
  for (size_t i = 0; i < queue->count; i++) {
    items[i] = queue->items[(queue->head + i) % queue->capacity];
  }

  gtext_allocator_free(alloc, queue->items);
  queue->items = items;
  queue->capacity = new_cap;
  queue->head = 0;
  return true;
}

static bool queue_push(const GTEXT_Allocator * alloc, csv_event_queue * queue,
    const GTEXT_CSV_Event * event) {
  if (!queue_reserve(alloc, queue, queue->count + 1)) {
    return false;
  }
  queue->items[(queue->head + queue->count) % queue->capacity] = *event;
  queue->count++;
  return true;
}

static bool queue_pop(csv_event_queue * queue, GTEXT_CSV_Event * event) {
  if (queue->count == 0) {
    return false;
  }
  *event = queue->items[queue->head];
  queue->head = (queue->head + 1) % queue->capacity;
  queue->count--;
  return true;
}

static void queue_clear(const GTEXT_Allocator * alloc, csv_event_queue * queue) {
  if (!queue->items) {
    return;
  }
  for (size_t i = 0; i < queue->count; i++) {
    event_free(alloc, &queue->items[(queue->head + i) % queue->capacity]);
  }
  gtext_allocator_free(alloc, queue->items);
  queue->items = NULL;
  queue->capacity = 0;
  queue->count = 0;
  queue->head = 0;
}

// ===========================================================================
// The bridge from push to pull
// ===========================================================================

static GTEXT_CSV_Status reader_on_event(
    const GTEXT_CSV_Event * event, void * user_data) {
  GTEXT_CSV_Reader * reader = (GTEXT_CSV_Reader *)user_data;

  GTEXT_CSV_Event copy;
  if (!event_copy(reader->alloc, event, &copy)) {
    return GTEXT_CSV_E_OOM;
  }
  if (!queue_push(reader->alloc, &reader->queue, &copy)) {
    event_free(reader->alloc, &copy);
    return GTEXT_CSV_E_OOM;
  }
  return GTEXT_CSV_OK;
}

// ===========================================================================
// Public interface
// ===========================================================================

GTEXT_API GTEXT_CSV_Reader * gtext_csv_reader_new(
    const GTEXT_CSV_Parse_Options * opts) {
  const GTEXT_Allocator * alloc = opts ? opts->allocator : NULL;

  GTEXT_CSV_Reader * reader = (GTEXT_CSV_Reader *)gtext_allocator_calloc(
      alloc, 1, sizeof(GTEXT_CSV_Reader));
  if (!reader) {
    return NULL;
  }
  reader->alloc = alloc;
  reader->failed = GTEXT_CSV_OK;

  // The callback needs the reader, and the reader needs the stream, so the
  // reader has to exist first.
  reader->stream = gtext_csv_stream_new(opts, reader_on_event, reader);
  if (!reader->stream) {
    gtext_allocator_free(alloc, reader);
    return NULL;
  }
  return reader;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_reader_feed(GTEXT_CSV_Reader * reader,
    const void * data, size_t len, GTEXT_CSV_Error * err) {
  if (!reader) {
    return GTEXT_CSV_E_INVALID;
  }
  if (reader->failed != GTEXT_CSV_OK) {
    return reader->failed;
  }

  // data == NULL with len == 0 is end of input, the same spelling the YAML
  // reader uses. A second one is accepted and does nothing, rather than being
  // an error: a loop that finishes on a short read can otherwise finish twice.
  if (!data && len == 0) {
    if (reader->finished) {
      return GTEXT_CSV_OK;
    }
    reader->finished = true;
    GTEXT_CSV_Status status = gtext_csv_stream_finish(reader->stream, err);
    if (status != GTEXT_CSV_OK) {
      reader->failed = status;
    }
    return status;
  }

  if (reader->finished) {
    // Bytes after end of input are the caller's mistake, not a parse error.
    return GTEXT_CSV_E_STATE;
  }
  if (!data) {
    return GTEXT_CSV_E_INVALID;
  }

  GTEXT_CSV_Status status =
      gtext_csv_stream_feed(reader->stream, (const char *)data, len, err);
  if (status != GTEXT_CSV_OK) {
    reader->failed = status;
  }
  return status;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_reader_next(
    GTEXT_CSV_Reader * reader, GTEXT_CSV_Event * out_event) {
  if (!reader || !out_event) {
    return GTEXT_CSV_E_INVALID;
  }

  // The event handed out last is released here rather than when it was handed
  // out, which is what makes its bytes valid until this call.
  if (reader->has_last_event) {
    event_free(reader->alloc, &reader->last_event);
    reader->has_last_event = false;
  }

  if (!queue_pop(&reader->queue, &reader->last_event)) {
    // Nothing queued. Which of the three answers that is depends on where the
    // stream got to, and a caller looping on OK needs to tell them apart.
    if (reader->failed != GTEXT_CSV_OK) {
      return reader->failed;
    }
    return reader->finished ? GTEXT_CSV_E_STATE : GTEXT_CSV_E_INCOMPLETE;
  }

  reader->has_last_event = true;
  *out_event = reader->last_event;
  return GTEXT_CSV_OK;
}

GTEXT_API void gtext_csv_reader_free(GTEXT_CSV_Reader * reader) {
  if (!reader) {
    return;
  }
  const GTEXT_Allocator * alloc = reader->alloc;
  if (reader->has_last_event) {
    event_free(alloc, &reader->last_event);
  }
  queue_clear(alloc, &reader->queue);
  gtext_csv_stream_free(reader->stream);
  gtext_allocator_free(alloc, reader);
}
