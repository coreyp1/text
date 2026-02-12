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
#include <string.h>

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
const GTEXT_YAML_Node *gtext_yaml_document_root(const GTEXT_YAML_Document *doc) {
	return doc ? doc->root : NULL;
}

/**
 * @brief Get the type of a YAML node.
 */
GTEXT_YAML_Node_Type gtext_yaml_node_type(const GTEXT_YAML_Node *n) {
	return n ? n->type : GTEXT_YAML_NULL;
}

/**
 * @brief Get scalar value as a string.
 *
 * Returns NULL for non-scalar nodes.
 */
const char *gtext_yaml_node_as_string(const GTEXT_YAML_Node *n) {
	if (!n) return NULL;
	if (n->type != GTEXT_YAML_STRING) return NULL;
	return n->as.scalar.value;
}
