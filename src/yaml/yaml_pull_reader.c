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
 * Pull-model YAML reader built on the streaming parser.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/yaml/yaml_stream.h>

#include "yaml_internal.h"

typedef struct {
	GTEXT_YAML_Event *items;
	size_t head;
	size_t count;
	size_t capacity;
} yaml_event_queue;

struct GTEXT_YAML_Reader {
	/* The caller's allocator: the queue and every copied string come from it,
	   matching the JSON and CSV readers. */
	const GTEXT_Allocator *alloc;
	GTEXT_YAML_Stream *stream;
	yaml_event_queue queue;
	GTEXT_YAML_Event last_event;
	bool has_last_event;
	bool finished;
	bool stream_end_queued;
};

/**
 * @brief Report a stream failure, preferring the scanner's own account of it.
 *
 * The stream hands back a status and keeps the detail, so asking it turns
 * "Failed to parse YAML input" into the fault the scanner actually found and
 * the line it found it on. @p fallback covers the failures that came from
 * somewhere the scanner never saw.
 */
static void reader_report(
	GTEXT_YAML_Reader *reader,
	GTEXT_YAML_Error *out_err,
	GTEXT_YAML_Status status,
	const char *fallback
) {
	if (!out_err) return;
	GTEXT_YAML_Error scan_err;
	memset(&scan_err, 0, sizeof(scan_err));
	if (reader && gtext_yaml_stream_last_error(reader->stream, &scan_err)
			&& scan_err.message) {
		out_err->code = scan_err.code;
		out_err->message = scan_err.message;
		out_err->offset = scan_err.offset;
		out_err->line = scan_err.line;
		out_err->col = scan_err.col;
		return;
	}
	out_err->code = status;
	out_err->message = fallback;
}

static void event_zero(GTEXT_YAML_Event *event) {
	if (!event) return;
	memset(event, 0, sizeof(*event));
}

static char *dup_len(const char *src, size_t len, const GTEXT_Allocator *alloc) {
	if (!src) return NULL;
	char *out = (char *)gtext_allocator_malloc(alloc, len + 1);
	if (!out) return NULL;
	memcpy(out, src, len);
	out[len] = '\0';
	return out;
}

static char *dup_str(const char *src, const GTEXT_Allocator *alloc) {
	if (!src) return NULL;
	size_t len = strlen(src);
	return dup_len(src, len, alloc);
}

static void event_free(GTEXT_YAML_Event *event, const GTEXT_Allocator *alloc) {
	if (!event) return;

	switch (event->type) {
		case GTEXT_YAML_EVENT_SCALAR:
			gtext_allocator_free(alloc, (void *)event->data.scalar.ptr);
			break;
		case GTEXT_YAML_EVENT_DIRECTIVE:
			gtext_allocator_free(alloc, (void *)event->data.directive.name);
			gtext_allocator_free(alloc, (void *)event->data.directive.value);
			gtext_allocator_free(alloc, (void *)event->data.directive.value2);
			break;
		case GTEXT_YAML_EVENT_COMMENT:
			gtext_allocator_free(alloc, (void *)event->data.comment.ptr);
			break;
		case GTEXT_YAML_EVENT_ALIAS:
			gtext_allocator_free(alloc, (void *)event->data.alias_name);
			break;
		default:
			break;
	}

	gtext_allocator_free(alloc, (void *)event->anchor);
	gtext_allocator_free(alloc, (void *)event->tag);
	event_zero(event);
}

static bool queue_reserve(yaml_event_queue *queue, size_t needed, const GTEXT_Allocator *alloc) {
	if (!queue) return false;
	if (queue->capacity >= needed) return true;

	size_t new_cap = queue->capacity == 0 ? 8 : queue->capacity * 2;
	while (new_cap < needed) {
		if (new_cap > SIZE_MAX / 2) return false;
		new_cap *= 2;
	}

	GTEXT_YAML_Event *items = (GTEXT_YAML_Event *)gtext_allocator_calloc(alloc, new_cap, sizeof(*items));
	if (!items) return false;

	for (size_t i = 0; i < queue->count; i++) {
		size_t idx = (queue->head + i) % queue->capacity;
		items[i] = queue->items[idx];
	}

	gtext_allocator_free(alloc, queue->items);
	queue->items = items;
	queue->capacity = new_cap;
	queue->head = 0;
	return true;
}

static bool queue_push(yaml_event_queue *queue, const GTEXT_YAML_Event *event, const GTEXT_Allocator *alloc) {
	if (!queue || !event) return false;
	if (!queue_reserve(queue, queue->count + 1, alloc)) return false;

	size_t idx = (queue->head + queue->count) % queue->capacity;
	queue->items[idx] = *event;
	queue->count++;
	return true;
}

static bool queue_pop(yaml_event_queue *queue, GTEXT_YAML_Event *event) {
	if (!queue || !event || queue->count == 0) return false;
	*event = queue->items[queue->head];
	queue->head = (queue->head + 1) % queue->capacity;
	queue->count--;
	return true;
}

static void queue_clear(yaml_event_queue *queue, const GTEXT_Allocator *alloc) {
	if (!queue || !queue->items) return;
	for (size_t i = 0; i < queue->count; i++) {
		size_t idx = (queue->head + i) % queue->capacity;
		event_free(&queue->items[idx], alloc);
	}
	gtext_allocator_free(alloc, queue->items);
	queue->items = NULL;
	queue->capacity = 0;
	queue->count = 0;
	queue->head = 0;
}

static GTEXT_YAML_Status queue_event_copy(
	GTEXT_YAML_Reader *reader,
	const GTEXT_YAML_Event *event
) {
	const GTEXT_Allocator *alloc = reader ? reader->alloc : NULL;
	GTEXT_YAML_Event copy;
	event_zero(&copy);
	copy.type = event->type;
	copy.offset = event->offset;
	copy.line = event->line;
	copy.col = event->col;
	copy.scalar_style = event->scalar_style;

	if (event->anchor) {
		copy.anchor = dup_str(event->anchor, alloc);
		if (!copy.anchor) return GTEXT_YAML_E_OOM;
	}

	if (event->tag) {
		copy.tag = dup_str(event->tag, alloc);
		if (!copy.tag) {
			event_free(&copy, alloc);
			return GTEXT_YAML_E_OOM;
		}
	}

	switch (event->type) {
		case GTEXT_YAML_EVENT_SCALAR:
			copy.data.scalar.ptr = dup_len(event->data.scalar.ptr, event->data.scalar.len, alloc);
			if (!copy.data.scalar.ptr && event->data.scalar.len > 0) {
				event_free(&copy, alloc);
				return GTEXT_YAML_E_OOM;
			}
			copy.data.scalar.len = event->data.scalar.len;
			break;
		case GTEXT_YAML_EVENT_DIRECTIVE:
			copy.data.directive.name = dup_str(event->data.directive.name, alloc);
			copy.data.directive.value = dup_str(event->data.directive.value, alloc);
			copy.data.directive.value2 = dup_str(event->data.directive.value2, alloc);
			if ((event->data.directive.name && !copy.data.directive.name) ||
				(event->data.directive.value && !copy.data.directive.value) ||
				(event->data.directive.value2 && !copy.data.directive.value2)) {
				event_free(&copy, alloc);
				return GTEXT_YAML_E_OOM;
			}
			break;
		case GTEXT_YAML_EVENT_COMMENT:
			copy.data.comment.ptr = dup_len(event->data.comment.ptr, event->data.comment.len, alloc);
			copy.data.comment.len = event->data.comment.len;
			copy.data.comment.inline_comment = event->data.comment.inline_comment;
			if (!copy.data.comment.ptr && event->data.comment.len > 0) {
				event_free(&copy, alloc);
				return GTEXT_YAML_E_OOM;
			}
			break;
		case GTEXT_YAML_EVENT_ALIAS:
			copy.data.alias_name = dup_str(event->data.alias_name, alloc);
			if (!copy.data.alias_name && event->data.alias_name) {
				event_free(&copy, alloc);
				return GTEXT_YAML_E_OOM;
			}
			break;
		case GTEXT_YAML_EVENT_INDICATOR:
			copy.data.indicator = event->data.indicator;
			break;
		default:
			break;
	}

	if (!queue_push(&reader->queue, &copy, alloc)) {
		event_free(&copy, alloc);
		return GTEXT_YAML_E_OOM;
	}

	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status reader_callback(
	GTEXT_YAML_Stream *stream,
	const void *payload,
	void *user
) {
	(void)stream;
	GTEXT_YAML_Reader *reader = (GTEXT_YAML_Reader *)user;
	const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)payload;
	if (!reader || !event) return GTEXT_YAML_E_INVALID;
	return queue_event_copy(reader, event);
}

GTEXT_API GTEXT_YAML_Reader *gtext_yaml_reader_new(
	const GTEXT_YAML_Parse_Options *opts
) {
	/* Read before the structure is allocated: it is the first thing the
	   caller's allocator has to own. */
	const GTEXT_Allocator *alloc = opts ? opts->allocator : NULL;

	GTEXT_YAML_Reader *reader =
		(GTEXT_YAML_Reader *)gtext_allocator_calloc(alloc, 1, sizeof(*reader));
	if (!reader) return NULL;
	reader->alloc = alloc;

	reader->stream = gtext_yaml_stream_new(opts, reader_callback, reader);
	if (!reader->stream) {
		gtext_allocator_free(alloc, reader);
		return NULL;
	}

	GTEXT_YAML_Event start_event;
	event_zero(&start_event);
	start_event.type = GTEXT_YAML_EVENT_STREAM_START;
	if (!queue_push(&reader->queue, &start_event, alloc)) {
		gtext_yaml_stream_free(reader->stream);
		gtext_allocator_free(alloc, reader);
		return NULL;
	}

	return reader;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_reader_feed(
	GTEXT_YAML_Reader *reader,
	const void *data,
	size_t len,
	GTEXT_YAML_Error *out_err
) {
	const GTEXT_Allocator *alloc = reader ? reader->alloc : NULL;
	if (!reader) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "Reader is NULL";
		}
		return GTEXT_YAML_E_INVALID;
	}

	if (!data && len > 0) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "Data is NULL";
		}
		return GTEXT_YAML_E_INVALID;
	}

	if (!data && len == 0) {
		if (reader->finished) {
			return GTEXT_YAML_E_STATE;
		}
		GTEXT_YAML_Status status = gtext_yaml_stream_finish(reader->stream);
		if (status != GTEXT_YAML_OK) {
			reader_report(reader, out_err, status,
				"Failed to finalize YAML stream");
			return status;
		}
		reader->finished = true;
		if (!reader->stream_end_queued) {
			GTEXT_YAML_Event end_event;
			event_zero(&end_event);
			end_event.type = GTEXT_YAML_EVENT_STREAM_END;
			if (!queue_push(&reader->queue, &end_event, alloc)) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_OOM;
					out_err->message = "Out of memory queueing stream end";
				}
				return GTEXT_YAML_E_OOM;
			}
			reader->stream_end_queued = true;
		}
		return GTEXT_YAML_OK;
	}

	GTEXT_YAML_Status status = gtext_yaml_stream_feed(reader->stream, (const char *)data, len);
	if (status != GTEXT_YAML_OK) {
		reader_report(reader, out_err, status, "Failed to parse YAML input");
		return status;
	}

	return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_reader_next(
	GTEXT_YAML_Reader *reader,
	GTEXT_YAML_Event *out_event,
	GTEXT_YAML_Error *out_err
) {
	const GTEXT_Allocator *alloc = reader ? reader->alloc : NULL;
	if (!reader || !out_event) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "Invalid reader arguments";
		}
		return GTEXT_YAML_E_INVALID;
	}

	if (reader->has_last_event) {
		event_free(&reader->last_event, alloc);
		reader->has_last_event = false;
	}

	GTEXT_YAML_Event next_event;
	if (!queue_pop(&reader->queue, &next_event)) {
		if (reader->finished) {
			return GTEXT_YAML_E_STATE;
		}
		return GTEXT_YAML_E_INCOMPLETE;
	}

	reader->last_event = next_event;
	reader->has_last_event = true;
	*out_event = next_event;
	(void)out_err;
	return GTEXT_YAML_OK;
}

GTEXT_API void gtext_yaml_reader_free(GTEXT_YAML_Reader *reader) {
	if (!reader) return;
	/* Read before the structure it lives in is released. */
	const GTEXT_Allocator *alloc = reader->alloc;
	if (reader->has_last_event) {
		event_free(&reader->last_event, alloc);
		reader->has_last_event = false;
	}
	queue_clear(&reader->queue, alloc);
	gtext_yaml_stream_free(reader->stream);
	gtext_allocator_free(alloc, reader);
}
