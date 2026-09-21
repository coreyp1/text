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
 * @brief Walking a parsed document as composed node events.
 */

#include <string.h>

#include <ghoti.io/text/yaml/yaml_core.h>
#include <ghoti.io/text/yaml/yaml_events.h>

#include "yaml_internal.h"

/* The empty node (7.2) has no text.  Scalar events point `value` here rather
   than at NULL so that a caller never has to check before reading it. */
static const char yaml_event_empty[] = "";

static GTEXT_YAML_Status walk_node(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
);

static GTEXT_YAML_Status emit(
	GTEXT_YAML_Node_Event *event,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	return cb(event, user);
}

/**
 * @brief The empty node, which has no node of its own to point at.
 *
 * A mapping pair with no value written after the ":" and a sequence entry
 * with nothing after the "-" both hold a NULL child.  That is a node all the
 * same - null, with no properties - and leaving it out would make the walk
 * disagree with the tree about how many entries the collection has.
 */
static GTEXT_YAML_Status walk_empty_node(
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	GTEXT_YAML_Node_Event event;
	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_SCALAR;
	event.value = yaml_event_empty;
	event.scalar_style = GTEXT_YAML_SCALAR_STYLE_PLAIN;
	return emit(&event, cb, user);
}

static GTEXT_YAML_Status walk_sequence(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	const yaml_node_sequence *seq = &node->as.sequence;
	GTEXT_YAML_Node_Event event;
	GTEXT_YAML_Status status;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_SEQUENCE_START;
	event.node = node;
	event.anchor = seq->anchor;
	event.tag = seq->tag;
	event.value = yaml_event_empty;
	event.flow_style = seq->flow_style;
	status = emit(&event, cb, user);
	if (status != GTEXT_YAML_OK) return status;

	for (size_t i = 0; i < seq->count; i++) {
		status = walk_node(seq->children[i], cb, user);
		if (status != GTEXT_YAML_OK) return status;
	}

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_SEQUENCE_END;
	event.node = node;
	event.value = yaml_event_empty;
	return emit(&event, cb, user);
}

static GTEXT_YAML_Status walk_mapping(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	const yaml_node_mapping *map = &node->as.mapping;
	GTEXT_YAML_Node_Event event;
	GTEXT_YAML_Status status;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_MAPPING_START;
	event.node = node;
	event.anchor = map->anchor;
	event.tag = map->tag;
	event.value = yaml_event_empty;
	event.flow_style = map->flow_style;
	status = emit(&event, cb, user);
	if (status != GTEXT_YAML_OK) return status;

	for (size_t i = 0; i < map->count; i++) {
		status = walk_node(map->pairs[i].key, cb, user);
		if (status != GTEXT_YAML_OK) return status;
		status = walk_node(map->pairs[i].value, cb, user);
		if (status != GTEXT_YAML_OK) return status;
	}

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_MAPPING_END;
	event.node = node;
	event.value = yaml_event_empty;
	return emit(&event, cb, user);
}

static GTEXT_YAML_Status walk_scalar(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	const yaml_node_scalar *scalar = &node->as.scalar;
	GTEXT_YAML_Node_Event event;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_SCALAR;
	event.node = node;
	event.anchor = scalar->anchor;
	event.tag = scalar->tag;
	event.value = scalar->value ? scalar->value : yaml_event_empty;
	event.value_len = scalar->value ? scalar->length : 0;
	event.scalar_style = scalar->scalar_style;
	return emit(&event, cb, user);
}

static GTEXT_YAML_Status walk_alias(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	const yaml_node_alias *alias = &node->as.alias;
	GTEXT_YAML_Node_Event event;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_ALIAS;
	event.node = node;
	/* The target is deliberately not followed.  An alias may refer to a node
	   that contains it, so a walk that expanded them would not terminate. */
	event.value = alias->anchor_name ? alias->anchor_name : yaml_event_empty;
	event.value_len = event.value ? strlen(event.value) : 0;
	return emit(&event, cb, user);
}

static GTEXT_YAML_Status walk_node(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Node_Event_Callback cb,
	void *user
) {
	if (!node) return walk_empty_node(cb, user);

	switch (node->type) {
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			/* !!omap and !!pairs are sequences of single-pair mappings that
			   carry a tag; the tag is on the event and the shape is the
			   sequence's own. */
			return walk_sequence(node, cb, user);
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			return walk_mapping(node, cb, user);
		case GTEXT_YAML_ALIAS:
			return walk_alias(node, cb, user);
		default:
			/* NULL, BOOL, INT, FLOAT and STRING are all one scalar that the
			   resolver typed; the text it was typed from is still there and
			   is what the walk reports. */
			return walk_scalar(node, cb, user);
	}
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_document_walk(
	const GTEXT_YAML_Document * doc,
	GTEXT_YAML_Node_Event_Callback cb,
	void * user
) {
	GTEXT_YAML_Node_Event event;
	GTEXT_YAML_Status status;

	if (!doc || !cb) return GTEXT_YAML_E_INVALID;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_DOCUMENT_START;
	event.value = yaml_event_empty;
	event.explicit_marker = doc->explicit_start;
	status = emit(&event, cb, user);
	if (status != GTEXT_YAML_OK) return status;

	/* A document with no root is a document holding the empty node, not a
	   document holding nothing: walk_node() answers both the same way. */
	status = walk_node(doc->root, cb, user);
	if (status != GTEXT_YAML_OK) return status;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_DOCUMENT_END;
	event.value = yaml_event_empty;
	event.explicit_marker = doc->explicit_end;
	return emit(&event, cb, user);
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_stream_walk(
	GTEXT_YAML_Document * const * docs,
	size_t count,
	GTEXT_YAML_Node_Event_Callback cb,
	void * user
) {
	GTEXT_YAML_Node_Event event;
	GTEXT_YAML_Status status;

	/* A count of zero is a stream with no documents in it, which is a stream;
	   docs may be NULL only in that case. */
	if (!cb || (!docs && count > 0)) return GTEXT_YAML_E_INVALID;

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_STREAM_START;
	event.value = yaml_event_empty;
	status = emit(&event, cb, user);
	if (status != GTEXT_YAML_OK) return status;

	for (size_t i = 0; i < count; i++) {
		status = gtext_yaml_document_walk(docs[i], cb, user);
		if (status != GTEXT_YAML_OK) return status;
	}

	memset(&event, 0, sizeof(event));
	event.type = GTEXT_YAML_NODE_EVENT_STREAM_END;
	event.value = yaml_event_empty;
	return emit(&event, cb, user);
}
