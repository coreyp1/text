/**
 * @file yaml_dom.h
 * @brief DOM inspection helpers for YAML documents.
 *
 * The DOM API provides read-only accessors for nodes produced by the
 * in-memory parser. The DOM is owned by a @ref GTEXT_YAML_Document and
 * callers must not attempt to free individual nodes; instead call
 * @ref gtext_yaml_free(document) to release the whole graph.
 */

#ifndef GHOTI_IO_TEXT_YAML_DOM_H
#define GHOTI_IO_TEXT_YAML_DOM_H

#include <ghoti.io/text/yaml/yaml_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a YAML string into a DOM document.
 *
 * Parses the input string and builds an in-memory tree representation.
 * On success, returns a document that must be freed with gtext_yaml_free().
 * On failure, returns NULL and populates the error structure (if provided).
 *
 * @param input Input YAML string (must remain valid for document lifetime if in-situ mode is enabled)
 * @param length Length of input string in bytes
 * @param options Parse options (NULL for defaults)
 * @param error Error output (may be NULL)
 * @return Document on success, NULL on error
 */
GTEXT_API GTEXT_YAML_Document * gtext_yaml_parse(
	const char * input,
	size_t length,
	const GTEXT_YAML_Parse_Options * options,
	GTEXT_YAML_Error * error
);

/**
 * @brief Get the root node of a YAML document.
 *
 * Returns NULL if the document is NULL or empty.
 *
 * @param doc Document to query
 * @return Root node, or NULL
 */
GTEXT_API const GTEXT_YAML_Node * gtext_yaml_document_root(const GTEXT_YAML_Document * doc);

/**
 * @brief Return the node type for @p n.
 *
 * The returned value is one of @ref GTEXT_YAML_Node_Type.
 */
GTEXT_API GTEXT_YAML_Node_Type gtext_yaml_node_type(const GTEXT_YAML_Node * n);

/**
 * @brief Return a nul-terminated string view for scalar nodes.
 *
 * For non-string nodes this function returns NULL. The returned pointer
 * is valid for the lifetime of the owning document; callers must not free it.
 */
GTEXT_API const char * gtext_yaml_node_as_string(const GTEXT_YAML_Node * n);

/* ============================================================================
 * Sequence Accessors (Phase 4.3)
 * ============================================================================ */

/**
 * @brief Get the number of items in a sequence.
 *
 * Returns 0 if the node is not a sequence or is empty.
 *
 * @param node Sequence node to query
 * @return Number of items in the sequence
 */
GTEXT_API size_t gtext_yaml_sequence_length(const GTEXT_YAML_Node * node);

/**
 * @brief Get a child node from a sequence by index.
 *
 * Returns NULL if the node is not a sequence or the index is out of bounds.
 * Indexing is zero-based.
 *
 * @param node Sequence node to query
 * @param index Zero-based index of child to retrieve
 * @return Child node at the given index, or NULL
 */
GTEXT_API const GTEXT_YAML_Node * gtext_yaml_sequence_get(const GTEXT_YAML_Node * node, size_t index);

/**
 * @brief Iterator callback for sequence traversal.
 *
 * @param node Child node
 * @param index Zero-based index of the child
 * @param user User data passed to gtext_yaml_sequence_iterate()
 * @return true to continue iteration, false to stop
 */
typedef bool (*GTEXT_YAML_Sequence_Iterator)(
	const GTEXT_YAML_Node * node,
	size_t index,
	void * user
);

/**
 * @brief Iterate over all children in a sequence.
 *
 * Calls the provided callback for each child in order. Iteration stops
 * if the callback returns false or all children have been visited.
 *
 * @param node Sequence node to iterate
 * @param callback Function to call for each child
 * @param user User data passed to callback
 * @return Number of children visited
 */
GTEXT_API size_t gtext_yaml_sequence_iterate(
	const GTEXT_YAML_Node * node,
	GTEXT_YAML_Sequence_Iterator callback,
	void * user
);

/* ============================================================================
 * Mapping Accessors (Phase 4.3)
 * ============================================================================ */

/**
 * @brief Get the number of key-value pairs in a mapping.
 *
 * Returns 0 if the node is not a mapping or is empty.
 *
 * @param node Mapping node to query
 * @return Number of key-value pairs
 */
GTEXT_API size_t gtext_yaml_mapping_size(const GTEXT_YAML_Node * node);

/**
 * @brief Look up a value in a mapping by string key.
 *
 * Performs a linear search through the mapping's keys, comparing each
 * key as a string. Returns the first matching value, or NULL if not found
 * or if the node is not a mapping.
 *
 * @param node Mapping node to search
 * @param key String key to look up
 * @return Value node associated with the key, or NULL if not found
 */
GTEXT_API const GTEXT_YAML_Node * gtext_yaml_mapping_get(const GTEXT_YAML_Node * node, const char * key);

/**
 * @brief Get a key-value pair from a mapping by index.
 *
 * Returns false if the node is not a mapping or the index is out of bounds.
 * Indexing is zero-based and reflects insertion order.
 *
 * @param node Mapping node to query
 * @param index Zero-based index of the pair
 * @param key Output pointer for key node (may be NULL)
 * @param value Output pointer for value node (may be NULL)
 * @return true if the pair was retrieved, false otherwise
 */
GTEXT_API bool gtext_yaml_mapping_get_at(
	const GTEXT_YAML_Node * node,
	size_t index,
	const GTEXT_YAML_Node ** key,
	const GTEXT_YAML_Node ** value
);

/**
 * @brief Iterator callback for mapping traversal.
 *
 * @param key Key node
 * @param value Value node
 * @param index Zero-based index of the pair
 * @param user User data passed to gtext_yaml_mapping_iterate()
 * @return true to continue iteration, false to stop
 */
typedef bool (*GTEXT_YAML_Mapping_Iterator)(
	const GTEXT_YAML_Node * key,
	const GTEXT_YAML_Node * value,
	size_t index,
	void * user
);

/**
 * @brief Iterate over all key-value pairs in a mapping.
 *
 * Calls the provided callback for each pair in insertion order. Iteration
 * stops if the callback returns false or all pairs have been visited.
 *
 * @param node Mapping node to iterate
 * @param callback Function to call for each pair
 * @param user User data passed to callback
 * @return Number of pairs visited
 */
GTEXT_API size_t gtext_yaml_mapping_iterate(
	const GTEXT_YAML_Node * node,
	GTEXT_YAML_Mapping_Iterator callback,
	void * user
);

/* ============================================================================
 * Node Metadata Accessors (Phase 4.3)
 * ============================================================================ */

/**
 * @brief Get the YAML tag associated with a node.
 *
 * Returns NULL if the node has no tag.
 *
 * @param node Node to query
 * @return Tag string, or NULL
 */
GTEXT_API const char * gtext_yaml_node_tag(const GTEXT_YAML_Node * node);

/**
 * @brief Get the YAML anchor associated with a node.
 *
 * Returns NULL if the node has no anchor.
 *
 * @param node Node to query
 * @return Anchor string, or NULL
 */
GTEXT_API const char * gtext_yaml_node_anchor(const GTEXT_YAML_Node * node);

/* ============================================================================
 * Alias Accessors (Phase 4.4)
 * ============================================================================ */

/**
 * @brief Resolve an alias node to its target.
 *
 * If the node is an alias, returns the target node it references.
 * If the node is not an alias, returns the node itself.
 * 
 * Note: YAML allows cycles. Callers must detect cycles if needed.
 *
 * @param node Node to resolve
 * @return Target node if alias, or the node itself otherwise; NULL if node is NULL
 */
GTEXT_API const GTEXT_YAML_Node * gtext_yaml_alias_target(const GTEXT_YAML_Node * node);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_TEXT_YAML_DOM_H

