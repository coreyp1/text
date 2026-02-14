/**
 * @file yaml_dom.c
 * @brief DOM node factory and public DOM accessor functions
 *
 * Implements node creation (allocated from context arena) and public
 * inspection APIs for the YAML DOM.
 *
 * Copyright 2026 by Corey Pennycuff
 */

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
	node->as.scalar.bool_value = false;
	node->as.scalar.int_value = 0;
	node->as.scalar.float_value = 0.0;
	node->as.scalar.has_timestamp = false;
	node->as.scalar.timestamp_has_time = false;
	node->as.scalar.timestamp_tz_specified = false;
	node->as.scalar.timestamp_tz_utc = false;
	node->as.scalar.timestamp_year = 0;
	node->as.scalar.timestamp_month = 0;
	node->as.scalar.timestamp_day = 0;
	node->as.scalar.timestamp_hour = 0;
	node->as.scalar.timestamp_minute = 0;
	node->as.scalar.timestamp_second = 0;
	node->as.scalar.timestamp_nsec = 0;
	node->as.scalar.timestamp_tz_offset = 0;
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

	out->has_time = n->as.scalar.timestamp_has_time;
	out->tz_specified = n->as.scalar.timestamp_tz_specified;
	out->tz_utc = n->as.scalar.timestamp_tz_utc;
	out->year = n->as.scalar.timestamp_year;
	out->month = n->as.scalar.timestamp_month;
	out->day = n->as.scalar.timestamp_day;
	out->hour = n->as.scalar.timestamp_hour;
	out->minute = n->as.scalar.timestamp_minute;
	out->second = n->as.scalar.timestamp_second;
	out->nsec = n->as.scalar.timestamp_nsec;
	out->tz_offset = n->as.scalar.timestamp_tz_offset;
	return true;
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
	doc->options = options ? *options : gtext_yaml_parse_options_default();
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

/**
 * @brief Create a new scalar node.
 */
GTEXT_API GTEXT_YAML_Node *gtext_yaml_node_new_scalar(
	GTEXT_YAML_Document *doc,
	const char *value,
	const char *tag,
	const char *anchor
) {
	if (!doc || !doc->ctx) return NULL;
	
	/* Use internal node factory */
	size_t value_len = value ? strlen(value) : 0;
	return yaml_node_new_scalar(doc->ctx, value, value_len, tag, anchor);
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
			clone->as.scalar.timestamp_has_time = node->as.scalar.timestamp_has_time;
			clone->as.scalar.timestamp_tz_specified = node->as.scalar.timestamp_tz_specified;
			clone->as.scalar.timestamp_tz_utc = node->as.scalar.timestamp_tz_utc;
			clone->as.scalar.timestamp_year = node->as.scalar.timestamp_year;
			clone->as.scalar.timestamp_month = node->as.scalar.timestamp_month;
			clone->as.scalar.timestamp_day = node->as.scalar.timestamp_day;
			clone->as.scalar.timestamp_hour = node->as.scalar.timestamp_hour;
			clone->as.scalar.timestamp_minute = node->as.scalar.timestamp_minute;
			clone->as.scalar.timestamp_second = node->as.scalar.timestamp_second;
			clone->as.scalar.timestamp_nsec = node->as.scalar.timestamp_nsec;
			clone->as.scalar.timestamp_tz_offset = node->as.scalar.timestamp_tz_offset;
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
