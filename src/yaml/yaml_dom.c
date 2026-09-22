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
 * @file yaml_dom.c
 * @brief DOM node factory and public DOM accessor functions
 *
 * Implements node creation (allocated from context arena) and public
 * inspection APIs for the YAML DOM.
 */

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Node Factory Functions (Internal)
 * ============================================================================ */

/**
 * @brief Duplicate a string into the arena.
 *
 * Returns NULL if str is NULL. The returned string is null-terminated.
 */
static const char *arena_strdup(yaml_context *ctx, const char *str, size_t len) {
	if (!str) return NULL;
	
	/* Allocate space for string + null terminator */
	char *copy = (char *)yaml_context_alloc(ctx, len + 1, 1);
	if (!copy) return NULL;
	
	memcpy(copy, str, len);
	copy[len] = '\0';
	return copy;
}

/**
 * @brief Create a scalar node.
 *
 * All scalars are stored as strings initially. Type resolution (int/float/bool)
 * is deferred to Phase 5.
 */
GTEXT_YAML_Node *yaml_node_new_scalar(
	yaml_context *ctx,
	const char *value,
	size_t length,
	const char *tag,
	const char *anchor
) {
	if (!ctx) return NULL;
	
	/* Allocate node */
	GTEXT_YAML_Node *node = (GTEXT_YAML_Node *)yaml_context_alloc(
		ctx, sizeof(GTEXT_YAML_Node), 8
	);
	if (!node) return NULL;
	
	/* Initialize scalar fields */
	node->type = GTEXT_YAML_STRING;
	node->as.scalar.type = GTEXT_YAML_STRING;
	node->as.scalar.leading_comment = NULL;
	node->as.scalar.inline_comment = NULL;
	node->as.scalar.source_offset = 0;
	node->as.scalar.source_line = 0;
	node->as.scalar.source_col = 0;
	node->as.scalar.scalar_style = GTEXT_YAML_SCALAR_STYLE_PLAIN;
	node->as.scalar.bool_value = false;
	node->as.scalar.int_value = 0;
	node->as.scalar.float_value = 0.0;
	node->as.scalar.has_timestamp = false;
	/* Zeroed wholesale rather than field by field: GCHRON_YAML_NONE is zero,
	   so a cleared value is recognisably unset, and a field chron adds later
	   is cleared here without this file having to learn about it. */
	memset(&node->as.scalar.timestamp, 0, sizeof(node->as.scalar.timestamp));
	node->as.scalar.timestamp_leap_second = false;
	node->as.scalar.has_binary = false;
	node->as.scalar.binary_data = NULL;
	node->as.scalar.binary_len = 0;
	
	/* Copy value into arena */
	node->as.scalar.value = arena_strdup(ctx, value, length);
	if (!node->as.scalar.value && value) {
		return NULL;  /* OOM during string copy */
	}
	node->as.scalar.length = length;
	
	/* Copy tag and anchor if present */
	node->as.scalar.tag = tag ? arena_strdup(ctx, tag, strlen(tag)) : NULL;
	node->as.scalar.anchor = anchor ? arena_strdup(ctx, anchor, strlen(anchor)) : NULL;
	
	ctx->node_count++;
	return node;
}

/**
 * @brief Create a sequence node with pre-allocated capacity.
 *
 * The returned node has space for 'capacity' children. Caller must populate
 * the children array and set count appropriately.
 */
GTEXT_YAML_Node *yaml_node_new_sequence(
	yaml_context *ctx,
	size_t capacity,
	const char *tag,
	const char *anchor
) {
	if (!ctx) return NULL;
	
	/* Calculate size: base struct + (capacity - 1) extra pointers */
	size_t size = sizeof(GTEXT_YAML_Node) + 
	              (capacity > 0 ? (capacity - 1) : 0) * sizeof(GTEXT_YAML_Node *);
	
	GTEXT_YAML_Node *node = (GTEXT_YAML_Node *)yaml_context_alloc(ctx, size, 8);
	if (!node) return NULL;
	
	/* Initialize sequence fields */
	node->type = GTEXT_YAML_SEQUENCE;
	node->as.sequence.type = GTEXT_YAML_SEQUENCE;
	node->as.sequence.flow_style = GTEXT_YAML_FLOW_STYLE_AUTO;
	node->as.sequence.leading_comment = NULL;
	node->as.sequence.inline_comment = NULL;
	node->as.sequence.source_offset = 0;
	node->as.sequence.source_line = 0;
	node->as.sequence.source_col = 0;
	node->as.sequence.count = 0;  /* Caller will populate */
	node->as.sequence.tag = tag ? arena_strdup(ctx, tag, strlen(tag)) : NULL;
	node->as.sequence.anchor = anchor ? arena_strdup(ctx, anchor, strlen(anchor)) : NULL;
	
	/* Zero the children array */
	if (capacity > 0) {
		memset(node->as.sequence.children, 0, capacity * sizeof(GTEXT_YAML_Node *));
	}
	
	ctx->node_count++;
	return node;
}

/**
 * @brief Create a mapping node with pre-allocated capacity.
 *
 * The returned node has space for 'capacity' key-value pairs. Caller must
 * populate the pairs array and set count appropriately.
 */
GTEXT_YAML_Node *yaml_node_new_mapping(
	yaml_context *ctx,
	size_t capacity,
	const char *tag,
	const char *anchor
) {
	if (!ctx) return NULL;
	
	/* Calculate size: base struct + (capacity - 1) extra pairs */
	size_t size = sizeof(GTEXT_YAML_Node) + 
	              (capacity > 0 ? (capacity - 1) : 0) * sizeof(yaml_mapping_pair);
	
	GTEXT_YAML_Node *node = (GTEXT_YAML_Node *)yaml_context_alloc(ctx, size, 8);
	if (!node) return NULL;
	
	/* Initialize mapping fields */
	node->type = GTEXT_YAML_MAPPING;
	node->as.mapping.type = GTEXT_YAML_MAPPING;
	node->as.mapping.flow_style = GTEXT_YAML_FLOW_STYLE_AUTO;
	node->as.mapping.leading_comment = NULL;
	node->as.mapping.inline_comment = NULL;
	node->as.mapping.source_offset = 0;
	node->as.mapping.source_line = 0;
	node->as.mapping.source_col = 0;
	node->as.mapping.count = 0;  /* Caller will populate */
	node->as.mapping.tag = tag ? arena_strdup(ctx, tag, strlen(tag)) : NULL;
	node->as.mapping.anchor = anchor ? arena_strdup(ctx, anchor, strlen(anchor)) : NULL;
	
	/* Zero the pairs array */
	if (capacity > 0) {
		memset(node->as.mapping.pairs, 0, capacity * sizeof(yaml_mapping_pair));
	}
	
	ctx->node_count++;
	return node;
}

/**
 * @brief Create alias node
 *
 * Creates an alias node that references an anchor. The target is not
 * resolved at creation time - it must be resolved in a separate pass.
 *
 * @param ctx Context (must not be NULL)
 * @param anchor_name Name of the referenced anchor
 * @return New alias node, or NULL on failure
 */
GTEXT_INTERNAL_API GTEXT_YAML_Node *yaml_node_new_alias(
	yaml_context *ctx,
	const char *anchor_name
) {
	if (!ctx || !anchor_name) return NULL;
	
	GTEXT_YAML_Node *node = (GTEXT_YAML_Node *)yaml_context_alloc(ctx, sizeof(GTEXT_YAML_Node), 8);
	if (!node) return NULL;
	
	/* Initialize alias fields */
	node->type = GTEXT_YAML_ALIAS;
	node->as.alias.type = GTEXT_YAML_ALIAS;
	node->as.alias.leading_comment = NULL;
	node->as.alias.inline_comment = NULL;
	node->as.alias.source_offset = 0;
	node->as.alias.source_line = 0;
	node->as.alias.source_col = 0;
	node->as.alias.anchor_name = arena_strdup(ctx, anchor_name, strlen(anchor_name));
	node->as.alias.target = NULL;  /* Will be resolved later */
	
	if (!node->as.alias.anchor_name) return NULL;
	
	ctx->node_count++;
	return node;
}

/* ============================================================================
 * Public DOM Accessor Functions
 * ============================================================================ */

/**
 * @brief Parse a YAML string into a DOM document (public API wrapper).
 */
GTEXT_YAML_Document *gtext_yaml_parse(
	const char *input,
	size_t length,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error
) {
	return yaml_parse_document(input, length, options, error);
}

GTEXT_YAML_Document *gtext_yaml_parse_safe(
	const char *input,
	size_t length,
	GTEXT_YAML_Error *error
) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_safe();
	return yaml_parse_document(input, length, &opts, error);
}

/**
 * @brief Get the root node of a document.
 */
GTEXT_API const GTEXT_YAML_Node *gtext_yaml_document_root(const GTEXT_YAML_Document *doc) {
	return doc ? doc->root : NULL;
}

/**
 * @brief Get document index (0-based) from multi-document stream.
 */
GTEXT_API size_t gtext_yaml_document_index(const GTEXT_YAML_Document *doc) {
	if (!doc) return 0;
	return doc->document_index;
}

GTEXT_API bool gtext_yaml_document_has_merge_keys(const GTEXT_YAML_Document *doc) {
	if (!doc) return false;
	return doc->has_merge_keys;
}

GTEXT_API bool gtext_yaml_document_has_explicit_start(
	const GTEXT_YAML_Document *doc
) {
	if (!doc) return false;
	return doc->explicit_start;
}

GTEXT_API bool gtext_yaml_document_has_explicit_end(
	const GTEXT_YAML_Document *doc
) {
	if (!doc) return false;
	return doc->explicit_end;
}

/**
 * @brief Get the type of a YAML node.
 */
GTEXT_API GTEXT_YAML_Node_Type gtext_yaml_node_type(const GTEXT_YAML_Node *n) {
	return n ? n->type : GTEXT_YAML_NULL;
}

/**
 * @brief Get scalar value as a string.
 *
 * Returns NULL for non-scalar nodes.
 */
const char *gtext_yaml_node_as_string(const GTEXT_YAML_Node *n) {
	if (!n) return NULL;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return n->as.scalar.value;
		default:
			return NULL;
	}
}

/**
 * @brief The length of a scalar's value in bytes.
 *
 * "\0" is an escape 5.7 defines, so a scalar may hold a NUL and a parsed
 * document may hand one back.  gtext_yaml_node_as_string() was the only way
 * to read a scalar and returns a C string, so that value could be written and
 * parsed but never read: everything from the NUL on was unreachable.  The DOM
 * has always kept the length; this is what says so.
 */
size_t gtext_yaml_node_scalar_length(const GTEXT_YAML_Node *n) {
	if (!n) return 0;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return n->as.scalar.value ? n->as.scalar.length : 0;
		default:
			return 0;
	}
}

GTEXT_API bool gtext_yaml_node_as_bool(const GTEXT_YAML_Node *n, bool *out) {
	if (!n || !out) return false;
	if (n->type != GTEXT_YAML_BOOL) return false;
	*out = n->as.scalar.bool_value;
	return true;
}

GTEXT_API bool gtext_yaml_node_as_int(const GTEXT_YAML_Node *n, int64_t *out) {
	if (!n || !out) return false;
	if (n->type != GTEXT_YAML_INT) return false;
	*out = n->as.scalar.int_value;
	return true;
}

GTEXT_API bool gtext_yaml_node_as_float(const GTEXT_YAML_Node *n, double *out) {
	if (!n || !out) return false;
	if (n->type != GTEXT_YAML_FLOAT) return false;
	*out = n->as.scalar.float_value;
	return true;
}

GTEXT_API bool gtext_yaml_node_is_null(const GTEXT_YAML_Node *n) {
	if (!n) return false;
	return n->type == GTEXT_YAML_NULL;
}

GTEXT_API bool gtext_yaml_node_as_timestamp(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Timestamp *out
) {
	if (!n || !out) return false;
	if (n->type != GTEXT_YAML_STRING) return false;
	if (!n->as.scalar.has_timestamp) return false;

	const GCHRON_YamlValue *ts = &n->as.scalar.timestamp;
	out->has_time = (ts->kind != GCHRON_YAML_DATE);
	out->tz_specified = (ts->kind == GCHRON_YAML_OFFSET_DATE_TIME);
	out->tz_utc = ts->offset_is_z;
	out->year = ts->civil.date.year;
	out->month = ts->civil.date.month;
	out->day = ts->civil.date.day;
	out->hour = ts->civil.time.hour;
	out->minute = ts->civil.time.minute;
	out->second = ts->civil.time.second;
	out->nsec = ts->civil.time.nsec;
	/* Minutes, as this struct has always reported them.  chron holds seconds,
	   because Europe/Amsterdam ran on +00:19:32 until 1937 - a sub-minute
	   offset this field cannot express.  No YAML timestamp can write one,
	   since the grammar's offset is whole minutes, so the conversion is exact
	   for every value that can reach it; gtext_yaml_node_timestamp_value()
	   is the accessor that does not have to make the claim at all. */
	out->tz_offset = ts->offset_sec / 60;
	out->leap_second = n->as.scalar.timestamp_leap_second;
	return true;
}

GTEXT_API bool gtext_yaml_node_timestamp_value(
	const GTEXT_YAML_Node *n,
	GCHRON_YamlValue *out
) {
	if (!n || !out) return false;
	if (n->type != GTEXT_YAML_STRING) return false;
	if (!n->as.scalar.has_timestamp) return false;
	*out = n->as.scalar.timestamp;
	return true;
}

GTEXT_API bool gtext_yaml_node_timestamp_is_leap_second(
	const GTEXT_YAML_Node *n
) {
	if (!n) return false;
	if (n->type != GTEXT_YAML_STRING) return false;
	if (!n->as.scalar.has_timestamp) return false;
	return n->as.scalar.timestamp_leap_second;
}

GTEXT_API bool gtext_yaml_node_as_binary(
	const GTEXT_YAML_Node *n,
	const unsigned char **out_data,
	size_t *out_len
) {
	if (!n || !out_data || !out_len) return false;
	if (!n->as.scalar.has_binary) return false;
	*out_data = n->as.scalar.binary_data;
	*out_len = n->as.scalar.binary_len;
	return true;
}

static const char *node_leading_comment(const GTEXT_YAML_Node *n) {
	if (!n) return NULL;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return n->as.scalar.leading_comment;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			return n->as.sequence.leading_comment;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			return n->as.mapping.leading_comment;
		case GTEXT_YAML_ALIAS:
			return n->as.alias.leading_comment;
		default:
			return NULL;
	}
}

static const char *node_inline_comment(const GTEXT_YAML_Node *n) {
	if (!n) return NULL;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return n->as.scalar.inline_comment;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			return n->as.sequence.inline_comment;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			return n->as.mapping.inline_comment;
		case GTEXT_YAML_ALIAS:
			return n->as.alias.inline_comment;
		default:
			return NULL;
	}
}

static bool node_source_location(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Source_Location *out
) {
	if (!n || !out) return false;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			out->offset = n->as.scalar.source_offset;
			out->line = n->as.scalar.source_line;
			out->col = n->as.scalar.source_col;
			return true;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			out->offset = n->as.sequence.source_offset;
			out->line = n->as.sequence.source_line;
			out->col = n->as.sequence.source_col;
			return true;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			out->offset = n->as.mapping.source_offset;
			out->line = n->as.mapping.source_line;
			out->col = n->as.mapping.source_col;
			return true;
		case GTEXT_YAML_ALIAS:
			out->offset = n->as.alias.source_offset;
			out->line = n->as.alias.source_line;
			out->col = n->as.alias.source_col;
			return true;
		default:
			break;
	}

	return false;
}

GTEXT_API const char *gtext_yaml_node_leading_comment(const GTEXT_YAML_Node *n) {
	return node_leading_comment(n);
}

GTEXT_API const char *gtext_yaml_node_inline_comment(const GTEXT_YAML_Node *n) {
	return node_inline_comment(n);
}

GTEXT_API bool gtext_yaml_node_source_location(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Source_Location *out
) {
	return node_source_location(n, out);
}

static bool node_scalar_style(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Scalar_Style *out
) {
	if (!n || !out) return false;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			*out = n->as.scalar.scalar_style;
			return true;
		default:
			return false;
	}
}

GTEXT_API bool gtext_yaml_node_scalar_style(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Scalar_Style *out
) {
	return node_scalar_style(n, out);
}

GTEXT_API bool gtext_yaml_node_flow_style(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Flow_Style *out
) {
	if (!n || !out) return false;
	switch (n->type) {
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			*out = n->as.sequence.flow_style;
			return true;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			*out = n->as.mapping.flow_style;
			return true;
		default:
			return false;
	}
}

GTEXT_API bool gtext_yaml_node_set_scalar_style(
	GTEXT_YAML_Node *n,
	GTEXT_YAML_Scalar_Style style
) {
	if (!n) return false;
	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			n->as.scalar.scalar_style = style;
			return true;
		default:
			return false;
	}
}

static GTEXT_YAML_Status node_set_comment(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *n,
	const char *comment,
	bool inline_comment
) {
	if (!doc || !doc->ctx || !n) return GTEXT_YAML_E_INVALID;
	const char *stored = NULL;
	if (comment && comment[0] != '\0') {
		stored = arena_strdup(doc->ctx, comment, strlen(comment));
		if (!stored) return GTEXT_YAML_E_OOM;
	}

	switch (n->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			if (inline_comment) {
				n->as.scalar.inline_comment = stored;
			} else {
				n->as.scalar.leading_comment = stored;
			}
			return GTEXT_YAML_OK;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			if (inline_comment) {
				n->as.sequence.inline_comment = stored;
			} else {
				n->as.sequence.leading_comment = stored;
			}
			return GTEXT_YAML_OK;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			if (inline_comment) {
				n->as.mapping.inline_comment = stored;
			} else {
				n->as.mapping.leading_comment = stored;
			}
			return GTEXT_YAML_OK;
		case GTEXT_YAML_ALIAS:
			if (inline_comment) {
				n->as.alias.inline_comment = stored;
			} else {
				n->as.alias.leading_comment = stored;
			}
			return GTEXT_YAML_OK;
		default:
			return GTEXT_YAML_E_INVALID;
	}
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_node_set_leading_comment(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *n,
	const char *comment
) {
	return node_set_comment(doc, n, comment, false);
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_node_set_inline_comment(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *n,
	const char *comment
) {
	return node_set_comment(doc, n, comment, true);
}

GTEXT_API bool gtext_yaml_node_set_bool(GTEXT_YAML_Node *n, bool value) {
	if (!n) return false;
	if (n->type != GTEXT_YAML_STRING && n->type != GTEXT_YAML_BOOL &&
		n->type != GTEXT_YAML_INT && n->type != GTEXT_YAML_FLOAT &&
		n->type != GTEXT_YAML_NULL) {
		return false;
	}
	n->type = GTEXT_YAML_BOOL;
	n->as.scalar.type = GTEXT_YAML_BOOL;
	n->as.scalar.bool_value = value;
	return true;
}

GTEXT_API bool gtext_yaml_node_set_int(GTEXT_YAML_Node *n, int64_t value) {
	if (!n) return false;
	if (n->type != GTEXT_YAML_STRING && n->type != GTEXT_YAML_BOOL &&
		n->type != GTEXT_YAML_INT && n->type != GTEXT_YAML_FLOAT &&
		n->type != GTEXT_YAML_NULL) {
		return false;
	}
	n->type = GTEXT_YAML_INT;
	n->as.scalar.type = GTEXT_YAML_INT;
	n->as.scalar.int_value = value;
	return true;
}

GTEXT_API bool gtext_yaml_node_set_float(GTEXT_YAML_Node *n, double value) {
	if (!n) return false;
	if (n->type != GTEXT_YAML_STRING && n->type != GTEXT_YAML_BOOL &&
		n->type != GTEXT_YAML_INT && n->type != GTEXT_YAML_FLOAT &&
		n->type != GTEXT_YAML_NULL) {
		return false;
	}
	n->type = GTEXT_YAML_FLOAT;
	n->as.scalar.type = GTEXT_YAML_FLOAT;
	n->as.scalar.float_value = value;
	return true;
}

/* ============================================================================
 * Sequence and Mapping Helpers
 * ============================================================================ */

static bool node_is_sequence_type(const GTEXT_YAML_Node *node) {
	if (!node) return false;
	return node->type == GTEXT_YAML_SEQUENCE ||
		node->type == GTEXT_YAML_OMAP ||
		node->type == GTEXT_YAML_PAIRS;
}

static bool node_is_mapping_type(const GTEXT_YAML_Node *node) {
	if (!node) return false;
	return node->type == GTEXT_YAML_MAPPING ||
		node->type == GTEXT_YAML_SET;
}

/* ============================================================================
 * Sequence Accessors (Phase 4.3)
 * ============================================================================ */

size_t gtext_yaml_sequence_length(const GTEXT_YAML_Node *node) {
	if (!node_is_sequence_type(node)) return 0;
	return node->as.sequence.count;
}

const GTEXT_YAML_Node *gtext_yaml_sequence_get(const GTEXT_YAML_Node *node, size_t index) {
	if (!node_is_sequence_type(node)) return NULL;
	if (index >= node->as.sequence.count) return NULL;
	return node->as.sequence.children[index];
}

size_t gtext_yaml_sequence_iterate(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Sequence_Iterator callback,
	void *user
) {
	if (!node_is_sequence_type(node) || !callback) return 0;
	
	for (size_t i = 0; i < node->as.sequence.count; i++) {
		if (!callback(node->as.sequence.children[i], i, user)) {
			return i + 1;  /* Stopped early - return count of items visited */
		}
	}
	return node->as.sequence.count;
}

/* ============================================================================
 * Mapping Accessors (Phase 4.3)
 * ============================================================================ */

size_t gtext_yaml_mapping_size(const GTEXT_YAML_Node *node) {
	if (!node_is_mapping_type(node)) return 0;
	return node->as.mapping.count;
}

const GTEXT_YAML_Node *gtext_yaml_mapping_get(const GTEXT_YAML_Node *node, const char *key) {
	if (!node_is_mapping_type(node) || !key) return NULL;
	
	/* Linear search through key-value pairs */
	for (size_t i = 0; i < node->as.mapping.count; i++) {
		const GTEXT_YAML_Node *k = node->as.mapping.pairs[i].key;
		if (k && k->type == GTEXT_YAML_STRING) {
			if (strcmp(k->as.scalar.value, key) == 0) {
				return node->as.mapping.pairs[i].value;
			}
		}
	}
	return NULL;
}

bool gtext_yaml_mapping_get_at(
	const GTEXT_YAML_Node *node,
	size_t index,
	const GTEXT_YAML_Node **key,
	const GTEXT_YAML_Node **value
) {
	if (!node_is_mapping_type(node)) return false;
	if (index >= node->as.mapping.count) return false;
	
	if (key) *key = node->as.mapping.pairs[index].key;
	if (value) *value = node->as.mapping.pairs[index].value;
	return true;
}

size_t gtext_yaml_mapping_iterate(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Mapping_Iterator callback,
	void *user
) {
	if (!node_is_mapping_type(node) || !callback) return 0;
	
	for (size_t i = 0; i < node->as.mapping.count; i++) {
		const GTEXT_YAML_Node *k = node->as.mapping.pairs[i].key;
		const GTEXT_YAML_Node *v = node->as.mapping.pairs[i].value;
		if (!callback(k, v, i, user)) {
			return i + 1;  /* Stopped early - return count of items visited */
		}
	}
	return node->as.mapping.count;
}

/* ============================================================================
 * Set Accessors (Phase 7.3a)
 * ============================================================================ */

size_t gtext_yaml_set_size(const GTEXT_YAML_Node *node) {
	if (!node || node->type != GTEXT_YAML_SET) return 0;
	return node->as.mapping.count;
}

const GTEXT_YAML_Node *gtext_yaml_set_get_at(
	const GTEXT_YAML_Node *node,
	size_t index
) {
	if (!node || node->type != GTEXT_YAML_SET) return NULL;
	if (index >= node->as.mapping.count) return NULL;
	return node->as.mapping.pairs[index].key;
}

size_t gtext_yaml_set_iterate(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Set_Iterator callback,
	void *user
) {
	if (!node || node->type != GTEXT_YAML_SET || !callback) return 0;

	for (size_t i = 0; i < node->as.mapping.count; i++) {
		const GTEXT_YAML_Node *key = node->as.mapping.pairs[i].key;
		if (!callback(key, i, user)) {
			return i + 1;
		}
	}
	return node->as.mapping.count;
}

/* ============================================================================
 * Omap/Pairs Accessors (Phase 7.3a)
 * ============================================================================ */

static bool omap_pairs_get_at_internal(
	const GTEXT_YAML_Node *node,
	size_t index,
	const GTEXT_YAML_Node **key,
	const GTEXT_YAML_Node **value
) {
	if (!node) return false;
	if (index >= node->as.sequence.count) return false;

	const GTEXT_YAML_Node *entry = node->as.sequence.children[index];
	if (!entry || entry->type != GTEXT_YAML_MAPPING || entry->as.mapping.count != 1) {
		return false;
	}

	if (key) *key = entry->as.mapping.pairs[0].key;
	if (value) *value = entry->as.mapping.pairs[0].value;
	return true;
}

size_t gtext_yaml_omap_size(const GTEXT_YAML_Node *node) {
	if (!node || node->type != GTEXT_YAML_OMAP) return 0;
	return node->as.sequence.count;
}

bool gtext_yaml_omap_get_at(
	const GTEXT_YAML_Node *node,
	size_t index,
	const GTEXT_YAML_Node **key,
	const GTEXT_YAML_Node **value
) {
	if (!node || node->type != GTEXT_YAML_OMAP) return false;
	return omap_pairs_get_at_internal(node, index, key, value);
}

size_t gtext_yaml_omap_iterate(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Omap_Iterator callback,
	void *user
) {
	if (!node || node->type != GTEXT_YAML_OMAP || !callback) return 0;

	for (size_t i = 0; i < node->as.sequence.count; i++) {
		const GTEXT_YAML_Node *key = NULL;
		const GTEXT_YAML_Node *value = NULL;
		if (!omap_pairs_get_at_internal(node, i, &key, &value)) return i;
		if (!callback(key, value, i, user)) {
			return i + 1;
		}
	}
	return node->as.sequence.count;
}

size_t gtext_yaml_pairs_size(const GTEXT_YAML_Node *node) {
	if (!node || node->type != GTEXT_YAML_PAIRS) return 0;
	return node->as.sequence.count;
}

bool gtext_yaml_pairs_get_at(
	const GTEXT_YAML_Node *node,
	size_t index,
	const GTEXT_YAML_Node **key,
	const GTEXT_YAML_Node **value
) {
	if (!node || node->type != GTEXT_YAML_PAIRS) return false;
	return omap_pairs_get_at_internal(node, index, key, value);
}

size_t gtext_yaml_pairs_iterate(
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Pairs_Iterator callback,
	void *user
) {
	if (!node || node->type != GTEXT_YAML_PAIRS || !callback) return 0;

	for (size_t i = 0; i < node->as.sequence.count; i++) {
		const GTEXT_YAML_Node *key = NULL;
		const GTEXT_YAML_Node *value = NULL;
		if (!omap_pairs_get_at_internal(node, i, &key, &value)) return i;
		if (!callback(key, value, i, user)) {
			return i + 1;
		}
	}
	return node->as.sequence.count;
}

/* ============================================================================
 * Node Metadata Accessors (Phase 4.3)
 * ============================================================================ */

const char *gtext_yaml_node_tag(const GTEXT_YAML_Node *node) {
	if (!node) return NULL;
	
	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return node->as.scalar.tag;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			return node->as.sequence.tag;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			return node->as.mapping.tag;
		default:
			return NULL;
	}
}

GTEXT_API const char *gtext_yaml_node_anchor(const GTEXT_YAML_Node *node) {
	if (!node) return NULL;
	
	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return node->as.scalar.anchor;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			return node->as.sequence.anchor;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			return node->as.mapping.anchor;
		default:
			return NULL;
	}
}

/* ============================================================================
 * Alias Accessors (Phase 4.4)
 * ============================================================================ */

GTEXT_API const GTEXT_YAML_Node *gtext_yaml_alias_target(const GTEXT_YAML_Node *node) {
	if (!node) return NULL;
	
	if (node->type == GTEXT_YAML_ALIAS) {
		return node->as.alias.target;
	}
	
	return node;
}

/* ============================================================================
 * DOM Manipulation API (Phase 4.7)
 * ============================================================================ */

/**
 * @brief Create a new empty YAML document.
 */
GTEXT_API GTEXT_YAML_Document *gtext_yaml_document_new(
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error
) {
	(void)error;  /* Not used yet - future error reporting */
	
	/* Create context */
	yaml_context *ctx = yaml_context_new();
	if (!ctx) {
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Failed to allocate document context";
		}
		return NULL;
	}
	
	/* Allocate document structure from arena (not malloc!) */
	GTEXT_YAML_Document *doc = (GTEXT_YAML_Document *)yaml_context_alloc(
		ctx, sizeof(GTEXT_YAML_Document), 8
	);
	if (!doc) {
		yaml_context_free(ctx);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Failed to allocate document structure";
		}
		return NULL;
	}
	
	/* Initialize document */
	memset(doc, 0, sizeof(*doc));
	doc->ctx = ctx;
	doc->root = NULL;
	/* Effective, not raw: a document built through this constructor is handed
	   to gtext_yaml_to_json_with_options(), which spends max_alias_expansion
	   as its node budget. A caller passing a zeroed options struct meant "the
	   defaults" - the header says so - and used to get no budget at all. */
	doc->options = gtext_yaml_parse_options_effective(options);
	doc->node_count = 0;
	doc->document_index = 0;
	doc->has_directives = false;
	doc->yaml_version_major = 0;
	doc->yaml_version_minor = 0;
	doc->input_newline = NULL;
	doc->tag_handles = NULL;
	doc->tag_handle_count = 0;
	
	return doc;
}

/**
 * @brief Set or replace the root node of a document.
 */
GTEXT_API bool gtext_yaml_document_set_root(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *root
) {
	if (!doc) return false;
	
	/* Allow NULL root to clear the document */
	doc->root = root;
	return true;
}

static GTEXT_YAML_Node_Type dom_scalar_type(
	const char *value,
	size_t length,
	const char *tag
);

/* Whether @p tag is the "!!binary" of the type repository, in either of the
   two spellings the resolver accepts. */
static bool tag_is_binary(const char *tag) {
	static const char yaml_prefix[] = "tag:yaml.org,2002:";
	if (!tag || !*tag) return false;
	if (strcmp(tag, "!!binary") == 0) return true;
	return strncmp(tag, yaml_prefix, sizeof(yaml_prefix) - 1) == 0
		&& strcmp(tag + sizeof(yaml_prefix) - 1, "binary") == 0;
}

/**
 * @brief Create a new scalar node.
 */
/* A scalar built from text is the text written plain, so its type is what the
   text resolves to - exactly what a parsed document would report for the same
   characters, and what the writer needs to know to leave it unquoted.
 *
 * The factory made a string of everything, which is a default rather than an
 * assertion and made both halves wrong: gtext_yaml_node_type() said "string"
 * of a node holding "1", and the writer, told it was a string, had to quote it
 * or the integer 1 came back.  Neither is what the caller asked for, because
 * the caller was never asked.  gtext_yaml_node_new_scalar_typed() is where
 * they say. */
static GTEXT_YAML_Node *dom_new_scalar(
	GTEXT_YAML_Document *doc,
	const char *value,
	size_t length,
	GTEXT_YAML_Node_Type type,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	GTEXT_YAML_Node *node =
		yaml_node_new_scalar(doc->ctx, value, value ? length : 0, tag, anchor);
	if (!node) return NULL;
	node->type = type;
	node->as.scalar.type = type;
	/* A node that says it is an integer has to hold one.  Setting the type
	   and leaving the union at zero made gtext_yaml_node_as_int() answer 0
	   for a scalar of "1", and every conversion built on it - to_json
	   included - answered the same. */
	switch (type) {
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT: {
			bool b = false;
			int64_t i = 0;
			double f = 0.0;
			const GTEXT_YAML_Node_Type from_text =
				gtext_yaml_plain_text_classify(
					value, value ? length : 0, &b, &i, &f);
			if (type == GTEXT_YAML_BOOL) node->as.scalar.bool_value = b;
			else if (type == GTEXT_YAML_INT) node->as.scalar.int_value = i;
			else node->as.scalar.float_value = f;
			/* An integer written as a float, or the other way about, still
			   has a value; only text that is neither leaves the union at its
			   zero, and the caller asserted the type knowing that. */
			if (type == GTEXT_YAML_FLOAT && from_text == GTEXT_YAML_INT) {
				node->as.scalar.float_value = (double)i;
			}
			/* ".INF" claimed as an int has no integer to be converted to,
			   and converting it anyway is undefined - UBSan said so, from a
			   node the writer fuzzer built.  The union stays at its zero,
			   which is what text that is neither already gets. */
			else if (type == GTEXT_YAML_INT && from_text == GTEXT_YAML_FLOAT
					&& gtext_yaml_double_fits_int64(f)) {
				node->as.scalar.int_value = (int64_t)f;
			}
			break;
		}
		default:
			break;
	}
	/* "!!binary" says the text is base64 and the node's value is what it
	   decodes to.  The resolver does this for a parsed node; the constructor
	   did not, so a node built with perfectly good base64 answered false to
	   gtext_yaml_node_as_binary() where the same document parsed answers with
	   the bytes.  And text that is not base64 was taken all the same, and the
	   writer put it after the tag - so "(((" went out as '!!binary (((' and
	   the parser refused the writer's own output.  A tag is an assertion
	   about the value, and this one can be false, so it is checked here the
	   way the parser checks it on the way in. */
	/* A tag names the type whose syntax 10.3.2 defines, and the constructor
	   is where a caller's claim about it is checked - the parser checks the
	   same claim on the way in, and refuses "!!int \"\"".  Without this the
	   writer put such a node out as '!!int ""' and this parser refused the
	   writer's own output.

	   "!!str" takes any text, and the type the tag names is the type the node
	   already has, so only a mismatch has to be looked for. */
	if (tag && *tag) {
		/* Two separate claims have to agree with the tag, and only the
		   second was being checked.

		   The first is the declared type.  A tag names a type, so a node
		   tagged "!!int" and declared a string is as false as one tagged
		   "!!int" holding "abc" - and gtext_yaml_node_new_scalar() refuses
		   that text outright.  The test below exempted every string-typed
		   node, on the reasoning that "!!str" takes any text and needs no
		   checking; true of "!!str", and the exemption was written for the
		   tag and applied to the type.  So the typed constructor built nodes
		   tagged "!!int" whose type was string, the writer put them out as
		   '!!int "abc"', and this library refused to read its own output.

		   dom_scalar_type() is what a tag names, and it answers string for
		   "!!str", for the non-specific "!", and for any tag this library
		   does not resolve - so a custom tag still leaves the type to the
		   caller, which is the point of one. */
		if (type != dom_scalar_type(value, value ? length : 0, tag)) {
			return NULL;
		}
	}
	/* The second claim is the text, and it is checked whether or not there is
	   a tag on the node.

	   It was gated on the tag, which reads as "a tag is an assertion that can
	   be false" and is true as far as it goes.  But the tag is not what makes
	   the claim checkable - the *type* is.  A caller who says NULL of the
	   text "NO" has said something false with a tag and exactly as false
	   without one, and only the tagged spelling was refused: the same claim
	   went through one door and not the other.

	   The node that got through could not be written by anything.  Canonical
	   form emitted '!!null "NO"', which this library then refuses to read,
	   and plain form emitted NO, which reads back as the string.  The
	   contradiction was in the node, so this is where it stops.

	   gtext_yaml_node_new_scalar() cannot produce one, because it takes the
	   type from the text rather than from a caller; this is the only door a
	   false claim can enter by.

	   A string needs no check: 10.3.2 resolves by contents only for a plain
	   scalar, and a string whose text would resolve to something else is
	   given a quoted style a few lines below, which is what keeps it one. */
	if (type != GTEXT_YAML_STRING) {
		const GTEXT_YAML_Node_Type from_text =
			gtext_yaml_plain_text_classify(value, value ? length : 0,
				NULL, NULL, NULL);
		bool ok;
		switch (type) {
			case GTEXT_YAML_NULL:
				ok = (from_text == GTEXT_YAML_NULL);
				break;
			case GTEXT_YAML_BOOL:
				ok = (from_text == GTEXT_YAML_BOOL);
				break;
			/* An integer spelling is a float spelling too: 10.3.2's float row
			   makes its fraction optional, so "!!float 12" is a float of 12
			   even though the text on its own resolves to an int. */
			case GTEXT_YAML_FLOAT:
				ok = (from_text == GTEXT_YAML_FLOAT
					|| from_text == GTEXT_YAML_INT);
				break;
			case GTEXT_YAML_INT:
				ok = (from_text == GTEXT_YAML_INT);
				break;
			default:
				ok = true;
				break;
		}
		if (!ok) return NULL;
	}

	if (tag_is_binary(tag)) {
		const unsigned char *data = NULL;
		size_t data_len = 0;
		if (!gtext_yaml_base64_decode(
				doc, value, value ? length : 0, &data, &data_len)) {
			return NULL;
		}
		node->as.scalar.has_binary = true;
		node->as.scalar.binary_data = data;
		node->as.scalar.binary_len = data_len;
	}

	/* A string whose text would resolve to something else has to go out
	   quoted, and the writer reads the style to know it.  Everything else
	   stays plain. */
	if (type == GTEXT_YAML_STRING
			&& !(tag && *tag)
			&& gtext_yaml_plain_text_resolves_to_non_string(
				value, value ? length : 0)) {
		node->as.scalar.scalar_style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
	}
	return node;
}

/* A tag decides what kind of scalar a node is; the text only decides it when
   there is no tag (10.3.2).  The non-specific "!" resolves to
   tag:yaml.org,2002:str for a scalar - that is what writing "!" in front of
   one means - so "! " and an empty scalar is the empty *string*, not null,
   which is what a re-read says and what the constructor did not.

   A tag this library does not resolve leaves the question to the text: it
   names a type the caller has defined, and a custom constructor will settle
   it if one is registered. */
static GTEXT_YAML_Node_Type dom_scalar_type(
	const char *value,
	size_t length,
	const char *tag
) {
	if (!tag || !*tag) return gtext_yaml_plain_text_type(value, length);
	if (strcmp(tag, "!") == 0) return GTEXT_YAML_STRING;

	const char *suffix = NULL;
	static const char yaml_prefix[] = "tag:yaml.org,2002:";
	if (tag[0] == '!' && tag[1] == '!') suffix = tag + 2;
	else if (strncmp(tag, yaml_prefix, sizeof(yaml_prefix) - 1) == 0) {
		suffix = tag + sizeof(yaml_prefix) - 1;
	}
	/* A tag this library does not resolve still stops the text from deciding.
	   10.3.2 resolves by contents only where the tag is non-specific and the
	   node was written plain; with any other tag the failsafe answer for a
	   scalar is a string, and that is what a re-read gives - so "!&!" over an
	   empty scalar is the empty string, not null. */
	if (!suffix) return GTEXT_YAML_STRING;

	if (strcmp(suffix, "str") == 0) return GTEXT_YAML_STRING;
	if (strcmp(suffix, "bool") == 0) return GTEXT_YAML_BOOL;
	if (strcmp(suffix, "int") == 0) return GTEXT_YAML_INT;
	if (strcmp(suffix, "float") == 0) return GTEXT_YAML_FLOAT;
	if (strcmp(suffix, "null") == 0) return GTEXT_YAML_NULL;
	if (strcmp(suffix, "binary") == 0) return GTEXT_YAML_STRING;
	if (strcmp(suffix, "timestamp") == 0) return GTEXT_YAML_STRING;
	return GTEXT_YAML_STRING;
}

GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_scalar(
	GTEXT_YAML_Document *doc,
	const char *value,
	const char *tag,
	const char *anchor
) {
	size_t value_len = value ? strlen(value) : 0;
	return dom_new_scalar(doc, value, value_len,
		dom_scalar_type(value, value_len, tag), tag, anchor);
}

GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_scalar_n(
	GTEXT_YAML_Document *doc,
	const char *value,
	size_t length,
	const char *tag,
	const char *anchor
) {
	if (!value) length = 0;
	return dom_new_scalar(doc, value, length,
		dom_scalar_type(value, length, tag), tag, anchor);
}

GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_scalar_typed(
	GTEXT_YAML_Document *doc,
	const char *value,
	size_t length,
	GTEXT_YAML_Node_Type type,
	const char *tag,
	const char *anchor
) {
	switch (type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			break;
		default:
			return NULL;   /* not a scalar type */
	}
	if (!value) length = 0;
	return dom_new_scalar(doc, value, length, type, tag, anchor);
}

/**
 * @brief Create a new empty sequence node.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_sequence(
	GTEXT_YAML_Document *doc,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	
	/* Create with initial capacity of 0 */
	return yaml_node_new_sequence(doc->ctx, 0, tag, anchor);
}

/**
 * @brief Create a new empty mapping node.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_mapping(
	GTEXT_YAML_Document *doc,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	
	/* Create with initial capacity of 0 */
	return yaml_node_new_mapping(doc->ctx, 0, tag, anchor);
}

/**
 * @brief Create a new empty set node.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_set(
	GTEXT_YAML_Document *doc,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	const char *resolved_tag = tag ? tag : "!!set";
	GTEXT_YAML_Node *node = yaml_node_new_mapping(doc->ctx, 0, resolved_tag, anchor);
	if (!node) return NULL;
	node->type = GTEXT_YAML_SET;
	node->as.mapping.type = GTEXT_YAML_SET;
	return node;
}

/**
 * @brief Create a new empty ordered map node.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_omap(
	GTEXT_YAML_Document *doc,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	const char *resolved_tag = tag ? tag : "!!omap";
	GTEXT_YAML_Node *node = yaml_node_new_sequence(doc->ctx, 0, resolved_tag, anchor);
	if (!node) return NULL;
	node->type = GTEXT_YAML_OMAP;
	node->as.sequence.type = GTEXT_YAML_OMAP;
	return node;
}

/**
 * @brief Create a new empty pairs node.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_pairs(
	GTEXT_YAML_Document *doc,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	const char *resolved_tag = tag ? tag : "!!pairs";
	GTEXT_YAML_Node *node = yaml_node_new_sequence(doc->ctx, 0, resolved_tag, anchor);
	if (!node) return NULL;
	node->type = GTEXT_YAML_PAIRS;
	node->as.sequence.type = GTEXT_YAML_PAIRS;
	return node;
}

typedef struct {
	const GTEXT_YAML_Node *source;
	GTEXT_YAML_Node *clone;
} yaml_clone_entry;

typedef struct {
	yaml_clone_entry *entries;
	size_t count;
	size_t capacity;
} yaml_clone_map;

static const yaml_clone_entry *clone_map_find(
	const yaml_clone_map *map,
	const GTEXT_YAML_Node *source
) {
	if (!map || !source) return NULL;
	for (size_t i = 0; i < map->count; i++) {
		if (map->entries[i].source == source) {
			return &map->entries[i];
		}
	}
	return NULL;
}

static bool clone_map_add(
	yaml_clone_map *map,
	const GTEXT_YAML_Node *source,
	GTEXT_YAML_Node *clone
) {
	if (!map || !source || !clone) return false;
	if (map->count >= map->capacity) {
		size_t new_capacity = map->capacity == 0 ? 16 : map->capacity * 2;
		yaml_clone_entry *new_entries = (yaml_clone_entry *)realloc(
			map->entries, new_capacity * sizeof(yaml_clone_entry)
		);
		if (!new_entries) return false;
		map->entries = new_entries;
		map->capacity = new_capacity;
	}
	map->entries[map->count].source = source;
	map->entries[map->count].clone = clone;
	map->count++;
	return true;
}

static GTEXT_YAML_Node *clone_node(
	yaml_context *ctx,
	const GTEXT_YAML_Node *node,
	yaml_clone_map *map
) {
	const yaml_clone_entry *entry = NULL;
	GTEXT_YAML_Node *clone = NULL;

	if (!ctx || !node || !map) return NULL;
	entry = clone_map_find(map, node);
	if (entry) return entry->clone;

	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			clone = yaml_node_new_scalar(
				ctx,
				node->as.scalar.value,
				node->as.scalar.length,
				node->as.scalar.tag,
				node->as.scalar.anchor
			);
			if (!clone) return NULL;
			clone->type = node->type;
			clone->as.scalar.type = node->type;
			clone->as.scalar.bool_value = node->as.scalar.bool_value;
			clone->as.scalar.int_value = node->as.scalar.int_value;
			clone->as.scalar.float_value = node->as.scalar.float_value;
			clone->as.scalar.has_timestamp = node->as.scalar.has_timestamp;
			/* One assignment, where this was twelve. A clone that copied the
			   fields one at a time is a clone that silently drops whichever
			   one is added next. */
			clone->as.scalar.timestamp = node->as.scalar.timestamp;
			clone->as.scalar.timestamp_leap_second =
				node->as.scalar.timestamp_leap_second;
			clone->as.scalar.has_binary = node->as.scalar.has_binary;
			clone->as.scalar.binary_len = node->as.scalar.binary_len;
			clone->as.scalar.binary_data = NULL;
			if (node->as.scalar.has_binary && node->as.scalar.binary_data && node->as.scalar.binary_len > 0) {
				unsigned char *data = (unsigned char *)yaml_context_alloc(
					ctx,
					node->as.scalar.binary_len,
					1
				);
				if (!data) return NULL;
				memcpy(data, node->as.scalar.binary_data, node->as.scalar.binary_len);
				clone->as.scalar.binary_data = data;
			}
			if (!clone_map_add(map, node, clone)) return NULL;
			return clone;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS: {
			size_t count = node->as.sequence.count;
			clone = yaml_node_new_sequence(
				ctx,
				count,
				node->as.sequence.tag,
				node->as.sequence.anchor
			);
			if (!clone) return NULL;
			if (!clone_map_add(map, node, clone)) return NULL;
			clone->type = node->type;
			clone->as.sequence.type = node->type;
			clone->as.sequence.count = count;
			for (size_t i = 0; i < count; i++) {
				clone->as.sequence.children[i] = clone_node(
					ctx,
					node->as.sequence.children[i],
					map
				);
				if (!clone->as.sequence.children[i]) return NULL;
			}
			return clone;
		}
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET: {
			size_t count = node->as.mapping.count;
			clone = yaml_node_new_mapping(
				ctx,
				count,
				node->as.mapping.tag,
				node->as.mapping.anchor
			);
			if (!clone) return NULL;
			if (!clone_map_add(map, node, clone)) return NULL;
			clone->type = node->type;
			clone->as.mapping.type = node->type;
			clone->as.mapping.count = count;
			for (size_t i = 0; i < count; i++) {
				clone->as.mapping.pairs[i].key_tag =
					node->as.mapping.pairs[i].key_tag
						? arena_strdup(ctx,
							node->as.mapping.pairs[i].key_tag,
							strlen(node->as.mapping.pairs[i].key_tag))
						: NULL;
				clone->as.mapping.pairs[i].value_tag =
					node->as.mapping.pairs[i].value_tag
						? arena_strdup(ctx,
							node->as.mapping.pairs[i].value_tag,
							strlen(node->as.mapping.pairs[i].value_tag))
						: NULL;
				clone->as.mapping.pairs[i].key = clone_node(
					ctx,
					node->as.mapping.pairs[i].key,
					map
				);
				clone->as.mapping.pairs[i].value = clone_node(
					ctx,
					node->as.mapping.pairs[i].value,
					map
				);
				if (!clone->as.mapping.pairs[i].key || !clone->as.mapping.pairs[i].value) {
					return NULL;
				}
			}
			return clone;
		}
		case GTEXT_YAML_ALIAS:
			clone = yaml_node_new_alias(ctx, node->as.alias.anchor_name);
			if (!clone) return NULL;
			if (!clone_map_add(map, node, clone)) return NULL;
			if (node->as.alias.target) {
				clone->as.alias.target = clone_node(ctx, node->as.alias.target, map);
				if (!clone->as.alias.target) return NULL;
			}
			return clone;
		default:
			return NULL;
	}
}

/**
 * @brief Helper to grow a sequence node by creating a new larger node.
 */
static GTEXT_YAML_Node *sequence_grow(
	yaml_context *ctx,
	GTEXT_YAML_Node *old_seq,
	size_t new_capacity
) {
	if (!ctx || !old_seq || !node_is_sequence_type(old_seq)) return NULL;
	
	/* Create new sequence with larger capacity */
	GTEXT_YAML_Node *new_seq = yaml_node_new_sequence(
		ctx, new_capacity, old_seq->as.sequence.tag, old_seq->as.sequence.anchor
	);
	if (!new_seq) return NULL;
	new_seq->type = old_seq->type;
	new_seq->as.sequence.type = old_seq->type;
	
	/* Copy existing children */
	new_seq->as.sequence.count = old_seq->as.sequence.count;
	for (size_t i = 0; i < old_seq->as.sequence.count; i++) {
		new_seq->as.sequence.children[i] = old_seq->as.sequence.children[i];
	}
	
	return new_seq;
}

/**
 * @brief Append a child node to a sequence.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_sequence_append(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *sequence,
	GTEXT_YAML_Node *child
) {
	if (!doc || !doc->ctx || !sequence || !node_is_sequence_type(sequence) || !child) return NULL;
	/* An omap is an ordered mapping, so what may go into one is not what may
	   go into any other sequence: a single-pair mapping whose key is not
	   already there.  The resolver holds a parsed omap to both rules and
	   these appenders held it to neither, so an omap with the same key twice
	   was built happily, written as '!!omap [{a: 1}, {a: 2}]', and refused by
	   this library's own parser.  !!pairs is the type that takes duplicates -
	   that is what distinguishes the two - and is deliberately not checked. */
	if (sequence->type == GTEXT_YAML_OMAP
			&& !gtext_yaml_omap_can_take(sequence, child)) {
		return NULL;
	}
	
	/* Create new sequence with room for one more child */
	size_t new_count = sequence->as.sequence.count + 1;
	GTEXT_YAML_Node *new_seq = sequence_grow(doc->ctx, sequence, new_count);
	if (!new_seq) return NULL;
	
	/* Add the new child */
	new_seq->as.sequence.children[sequence->as.sequence.count] = child;
	new_seq->as.sequence.count = new_count;
	
	return new_seq;
}

/**
 * @brief Insert a child node at a specific index in a sequence.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_sequence_insert(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *sequence,
	size_t index,
	GTEXT_YAML_Node *child
) {
	if (!doc || !doc->ctx || !sequence || !node_is_sequence_type(sequence) || !child) return NULL;
	if (index > sequence->as.sequence.count) return NULL;
	/* An omap is an ordered mapping, so what may go into one is not what may
	   go into any other sequence: a single-pair mapping whose key is not
	   already there.  The resolver holds a parsed omap to both rules and
	   these appenders held it to neither, so an omap with the same key twice
	   was built happily, written as '!!omap [{a: 1}, {a: 2}]', and refused by
	   this library's own parser.  !!pairs is the type that takes duplicates -
	   that is what distinguishes the two - and is deliberately not checked. */
	if (sequence->type == GTEXT_YAML_OMAP
			&& !gtext_yaml_omap_can_take(sequence, child)) {
		return NULL;
	}
	
	/* Create new sequence with room for one more child */
	size_t new_count = sequence->as.sequence.count + 1;
	GTEXT_YAML_Node *new_seq = yaml_node_new_sequence(
		doc->ctx, new_count, sequence->as.sequence.tag, sequence->as.sequence.anchor
	);
	if (!new_seq) return NULL;
	new_seq->type = sequence->type;
	new_seq->as.sequence.type = sequence->type;
	
	/* Copy children before insertion point */
	for (size_t i = 0; i < index; i++) {
		new_seq->as.sequence.children[i] = sequence->as.sequence.children[i];
	}
	
	/* Insert new child */
	new_seq->as.sequence.children[index] = child;
	
	/* Copy children after insertion point */
	for (size_t i = index; i < sequence->as.sequence.count; i++) {
		new_seq->as.sequence.children[i + 1] = sequence->as.sequence.children[i];
	}
	
	new_seq->as.sequence.count = new_count;
	return new_seq;
}

/**
 * @brief Remove a child node at a specific index from a sequence.
 */
GTEXT_API bool gtext_yaml_sequence_remove(
	GTEXT_YAML_Node *sequence,
	size_t index
) {
	if (!sequence || !node_is_sequence_type(sequence)) return false;
	if (index >= sequence->as.sequence.count) return false;
	
	/* Shift remaining elements left */
	for (size_t i = index; i < sequence->as.sequence.count - 1; i++) {
		sequence->as.sequence.children[i] = sequence->as.sequence.children[i + 1];
	}
	sequence->as.sequence.count--;
	
	return true;
}

/**
 * @brief Helper to grow a mapping node by creating a new larger node.
 */
static GTEXT_YAML_Node *mapping_grow(
	yaml_context *ctx,
	GTEXT_YAML_Node *old_map,
	size_t new_capacity
) {
	if (!ctx || !old_map || !node_is_mapping_type(old_map)) return NULL;
	
	/* Create new mapping with larger capacity */
	GTEXT_YAML_Node *new_map = yaml_node_new_mapping(
		ctx, new_capacity, old_map->as.mapping.tag, old_map->as.mapping.anchor
	);
	if (!new_map) return NULL;
	new_map->type = old_map->type;
	new_map->as.mapping.type = old_map->type;
	
	/* Copy existing pairs */
	new_map->as.mapping.count = old_map->as.mapping.count;
	for (size_t i = 0; i < old_map->as.mapping.count; i++) {
		new_map->as.mapping.pairs[i] = old_map->as.mapping.pairs[i];
	}
	
	return new_map;
}

/**
 * @brief Set or add a key-value pair in a mapping.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_mapping_set(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *mapping,
	GTEXT_YAML_Node *key,
	GTEXT_YAML_Node *value
) {
	if (!doc || !doc->ctx || !mapping || !node_is_mapping_type(mapping) || !key || !value) return NULL;
	
	/* Check if key already exists (for string keys) */
	size_t existing_idx = (size_t)-1;
	if (key->type == GTEXT_YAML_STRING) {
		for (size_t i = 0; i < mapping->as.mapping.count; i++) {
			const GTEXT_YAML_Node *k = mapping->as.mapping.pairs[i].key;
			if (k && k->type == GTEXT_YAML_STRING) {
				if (strcmp(k->as.scalar.value, key->as.scalar.value) == 0) {
					existing_idx = i;
					break;
				}
			}
		}
	}
	
	if (existing_idx != (size_t)-1) {
		/* Key exists - replace value in place */
		mapping->as.mapping.pairs[existing_idx].value = value;
		return mapping;
	}
	
	/* Key doesn't exist - add new pair */
	size_t new_count = mapping->as.mapping.count + 1;
	GTEXT_YAML_Node *new_map = mapping_grow(doc->ctx, mapping, new_count);
	if (!new_map) return NULL;
	
	/* Add the new pair */
	new_map->as.mapping.pairs[mapping->as.mapping.count].key = key;
	new_map->as.mapping.pairs[mapping->as.mapping.count].value = value;
	new_map->as.mapping.pairs[mapping->as.mapping.count].key_tag = NULL;
	new_map->as.mapping.pairs[mapping->as.mapping.count].value_tag = NULL;
	new_map->as.mapping.count = new_count;
	
	return new_map;
}

/**
 * @brief Remove a key-value pair from a mapping by string key.
 */
GTEXT_API bool gtext_yaml_mapping_delete(
	GTEXT_YAML_Node *mapping,
	const char *key
) {
	if (!mapping || !node_is_mapping_type(mapping) || !key) return false;
	
	/* Find the key */
	size_t found_idx = (size_t)-1;
	for (size_t i = 0; i < mapping->as.mapping.count; i++) {
		const GTEXT_YAML_Node *k = mapping->as.mapping.pairs[i].key;
		if (k && k->type == GTEXT_YAML_STRING) {
			if (strcmp(k->as.scalar.value, key) == 0) {
				found_idx = i;
				break;
			}
		}
	}
	
	if (found_idx == (size_t)-1) return false;  /* Not found */
	
	/* Shift remaining pairs left */
	for (size_t i = found_idx; i < mapping->as.mapping.count - 1; i++) {
		mapping->as.mapping.pairs[i] = mapping->as.mapping.pairs[i + 1];
	}
	mapping->as.mapping.count--;
	
	return true;
}

/**
 * @brief Check if a mapping contains a string key.
 */
GTEXT_API bool gtext_yaml_mapping_has_key(
	const GTEXT_YAML_Node *mapping,
	const char *key
) {
	if (!mapping || !node_is_mapping_type(mapping) || !key) return false;
	
	/* Linear search through key-value pairs */
	for (size_t i = 0; i < mapping->as.mapping.count; i++) {
		const GTEXT_YAML_Node *k = mapping->as.mapping.pairs[i].key;
		if (k && k->type == GTEXT_YAML_STRING) {
			if (strcmp(k->as.scalar.value, key) == 0) {
				return true;
			}
		}
	}
	return false;
}

GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_clone(
	GTEXT_YAML_Document *doc,
	const GTEXT_YAML_Node *node
) {
	yaml_clone_map map = {0};
	GTEXT_YAML_Node *clone = NULL;

	if (!doc || !doc->ctx || !node) return NULL;

	clone = clone_node(doc->ctx, node, &map);
	free(map.entries);
	return clone;
}
