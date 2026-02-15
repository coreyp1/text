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

#include <stdint.h>

#include <ghoti.io/text/yaml/yaml_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of JSON value type */
typedef struct GTEXT_JSON_Value GTEXT_JSON_Value;

/**
 * @brief Parse a YAML string into a DOM document.
 *
 * Parses the input string and builds an in-memory tree representation.
 * On success, returns a document that must be freed with gtext_yaml_free().
 * On failure, returns NULL and populates the error structure (if provided).
 *
 * For multi-document streams, this function parses only the first document.
 * Use gtext_yaml_parse_all() to parse all documents in a stream.
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
 * @brief Parse JSON input into a YAML DOM document.
 *
 * This function accepts JSON input and returns an equivalent YAML DOM.
 * JSON parsing is strict and does not allow YAML-specific extensions.
 *
 * @param input Input JSON string (must not be NULL)
 * @param length Length of input string in bytes
 * @param options Parse options (NULL for defaults)
 * @param error Error output (may be NULL)
 * @return Document on success, NULL on error
 */
GTEXT_API GTEXT_YAML_Document * gtext_yaml_parse_json(
	const char * input,
	size_t length,
	const GTEXT_YAML_Parse_Options * options,
	GTEXT_YAML_Error * error
);

/**
 * @brief Parse a YAML string into a DOM document using safe-mode defaults.
 */
GTEXT_API GTEXT_YAML_Document * gtext_yaml_parse_safe(
	const char * input,
	size_t length,
	GTEXT_YAML_Error * error
);

/**
 * @brief Parse all documents in a YAML stream.
 *
 * Parses the input string and builds in-memory tree representations for
 * ALL documents in the stream. On success, returns an array of documents
 * and sets *document_count to the number of documents.
 *
 * Each document must be freed individually with gtext_yaml_free(), and
 * the array itself must be freed with free().
 *
 * Example:
 * @code
 *   size_t count;
 *   GTEXT_YAML_Document **docs = gtext_yaml_parse_all(input, len, &count, NULL, NULL);
 *   if (docs) {
 *     for (size_t i = 0; i < count; i++) {
 *       // Use docs[i]...
 *       gtext_yaml_free(docs[i]);
 *     }
 *     free(docs);
 *   }
 * @endcode
 *
 * @param input Input YAML string
 * @param length Length of input string in bytes
 * @param document_count Output: number of documents parsed (must not be NULL)
 * @param options Parse options (NULL for defaults)
 * @param error Error output (may be NULL)
 * @return Array of documents on success, NULL on error
 */
GTEXT_API GTEXT_YAML_Document ** gtext_yaml_parse_all(
	const char * input,
	size_t length,
	size_t * document_count,
	const GTEXT_YAML_Parse_Options * options,
	GTEXT_YAML_Error * error
);

/**
 * @brief Parse all documents in a YAML stream using safe-mode defaults.
 */
GTEXT_API GTEXT_YAML_Document ** gtext_yaml_parse_all_safe(
	const char * input,
	size_t length,
	size_t * document_count,
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
 * @brief Get the document index in a multi-document stream.
 *
 * For multi-document streams, this returns the 0-based index of the document.
 * Documents returned by gtext_yaml_parse() always have index 0 (first document).
 * Documents returned by gtext_yaml_parse_all() have indices 0, 1, 2, etc.
 *
 * @param doc Document to query
 * @return Document index (0-based)
 */
GTEXT_API size_t gtext_yaml_document_index(const GTEXT_YAML_Document * doc);

/**
 * @brief Return true if the document used merge keys (<<).
 *
 * This flag is set when merge keys are detected during resolution.
 *
 * @param doc Document to query
 * @return true if merge keys were used, false otherwise
 */
GTEXT_API bool gtext_yaml_document_has_merge_keys(const GTEXT_YAML_Document * doc);
/**
 * @brief Return the node type for @p n.
 *
 * The returned value is one of @ref GTEXT_YAML_Node_Type.
 */
GTEXT_API GTEXT_YAML_Node_Type gtext_yaml_node_type(const GTEXT_YAML_Node * n);

/**
 * @brief Return a nul-terminated string view for scalar nodes.
 *
 * For non-scalar nodes this function returns NULL. The returned pointer
 * is valid for the lifetime of the owning document; callers must not free it.
 */
GTEXT_API const char * gtext_yaml_node_as_string(const GTEXT_YAML_Node * n);

/**
 * @brief Return scalar value as a boolean.
 *
 * Returns false if the node is NULL, not a boolean, or @p out is NULL.
 *
 * @param n Scalar node to query
 * @param out Output boolean value
 * @return true on success, false otherwise
 */
GTEXT_API bool gtext_yaml_node_as_bool(const GTEXT_YAML_Node * n, bool * out);

/**
 * @brief Return scalar value as an integer.
 *
 * Returns false if the node is NULL, not an integer, or @p out is NULL.
 *
 * @param n Scalar node to query
 * @param out Output integer value
 * @return true on success, false otherwise
 */
GTEXT_API bool gtext_yaml_node_as_int(const GTEXT_YAML_Node * n, int64_t * out);

/**
 * @brief Return scalar value as a floating-point number.
 *
 * Returns false if the node is NULL, not a float, or @p out is NULL.
 *
 * @param n Scalar node to query
 * @param out Output float value
 * @return true on success, false otherwise
 */
GTEXT_API bool gtext_yaml_node_as_float(const GTEXT_YAML_Node * n, double * out);

/**
 * @brief Return true if the node is a null scalar.
 *
 * @param n Node to query
 * @return true if node is a null scalar, false otherwise
 */
GTEXT_API bool gtext_yaml_node_is_null(const GTEXT_YAML_Node * n);

/**
 * @struct GTEXT_YAML_Timestamp
 * @brief Parsed timestamp fields for !!timestamp scalars.
 */
typedef struct {
	bool has_time;
	bool tz_specified;
	bool tz_utc;
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;
	int nsec;
	int tz_offset;
} GTEXT_YAML_Timestamp;

/**
 * @brief Return scalar value as a timestamp.
 *
 * Returns false if the node is NULL, not a timestamp scalar, or @p out is NULL.
 *
 * @param n Scalar node to query
 * @param out Output timestamp fields
 * @return true on success, false otherwise
 */
GTEXT_API bool gtext_yaml_node_as_timestamp(
	const GTEXT_YAML_Node * n,
	GTEXT_YAML_Timestamp * out
);

/**
 * @brief Return scalar value as binary payload.
 *
 * Returns false if the node is NULL, not a binary scalar, or outputs are NULL.
 *
 * @param n Scalar node to query
 * @param out_data Output pointer to binary data
 * @param out_len Output length of binary data
 * @return true on success, false otherwise
 */
GTEXT_API bool gtext_yaml_node_as_binary(
	const GTEXT_YAML_Node * n,
	const unsigned char ** out_data,
	size_t * out_len
);

/**
 * @brief Get the leading comment attached to a node.
 */
GTEXT_API const char * gtext_yaml_node_leading_comment(
	const GTEXT_YAML_Node *n
);

/**
 * @brief Get the inline comment attached to a node.
 */
GTEXT_API const char * gtext_yaml_node_inline_comment(
	const GTEXT_YAML_Node *n
);

/**
 * @brief Get the source location for a node.
 */
GTEXT_API bool gtext_yaml_node_source_location(
	const GTEXT_YAML_Node *n,
	GTEXT_YAML_Source_Location *out
);

/**
 * @brief Set the leading comment for a node.
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_node_set_leading_comment(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *n,
	const char *comment
);

/**
 * @brief Set the inline comment for a node.
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_node_set_inline_comment(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *n,
	const char *comment
);

/**
 * @brief Set a scalar node to a boolean value.
 *
 * Returns false if the node is NULL or not a scalar.
 */
GTEXT_API bool gtext_yaml_node_set_bool(GTEXT_YAML_Node * n, bool value);

/**
 * @brief Set a scalar node to an integer value.
 *
 * Returns false if the node is NULL or not a scalar.
 */
GTEXT_API bool gtext_yaml_node_set_int(GTEXT_YAML_Node * n, int64_t value);

/**
 * @brief Set a scalar node to a floating-point value.
 *
 * Returns false if the node is NULL or not a scalar.
 */
GTEXT_API bool gtext_yaml_node_set_float(GTEXT_YAML_Node * n, double value);

/* ============================================================================
 * Sequence Accessors (Phase 4.3)
 * ============================================================================ */

/**
 * @brief Get the number of items in a sequence.
 *
 * Returns 0 if the node is not a sequence-like node or is empty.
 * Sequence-like nodes include GTEXT_YAML_SEQUENCE, GTEXT_YAML_OMAP, and
 * GTEXT_YAML_PAIRS.
 *
 * @param node Sequence node to query
 * @return Number of items in the sequence
 */
GTEXT_API size_t gtext_yaml_sequence_length(const GTEXT_YAML_Node * node);

/**
 * @brief Get a child node from a sequence by index.
 *
 * Returns NULL if the node is not a sequence-like node or the index is out of
 * bounds. Sequence-like nodes include GTEXT_YAML_SEQUENCE, GTEXT_YAML_OMAP,
 * and GTEXT_YAML_PAIRS.
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
 * Returns 0 if the node is not a mapping-like node or is empty. Mapping-like
 * nodes include GTEXT_YAML_MAPPING and GTEXT_YAML_SET.
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
 * or if the node is not a mapping-like node.
 *
 * @param node Mapping node to search
 * @param key String key to look up
 * @return Value node associated with the key, or NULL if not found
 */
GTEXT_API const GTEXT_YAML_Node * gtext_yaml_mapping_get(const GTEXT_YAML_Node * node, const char * key);

/**
 * @brief Get a key-value pair from a mapping by index.
 *
 * Returns false if the node is not a mapping-like node or the index is out of
 * bounds. Mapping-like nodes include GTEXT_YAML_MAPPING and GTEXT_YAML_SET.
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

/* ==========================================================================
 * Set Accessors (Phase 7.3a)
 * ==========================================================================
 */

/**
 * @brief Get the number of keys in a set.
 *
 * Returns 0 if the node is not a set.
 *
 * @param node Set node to query
 * @return Number of keys in the set
 */
GTEXT_API size_t gtext_yaml_set_size(const GTEXT_YAML_Node * node);

/**
 * @brief Get a key node from a set by index.
 *
 * Returns NULL if the node is not a set or the index is out of bounds.
 * Indexing is zero-based and reflects insertion order.
 *
 * @param node Set node to query
 * @param index Zero-based index of key to retrieve
 * @return Key node at the given index, or NULL
 */
GTEXT_API const GTEXT_YAML_Node * gtext_yaml_set_get_at(
	const GTEXT_YAML_Node * node,
	size_t index
);

/**
 * @brief Iterator callback for set traversal.
 *
 * @param key Key node
 * @param index Zero-based index of the key
 * @param user User data passed to gtext_yaml_set_iterate()
 * @return true to continue iteration, false to stop
 */
typedef bool (*GTEXT_YAML_Set_Iterator)(
	const GTEXT_YAML_Node * key,
	size_t index,
	void * user
);

/**
 * @brief Iterate over all keys in a set.
 *
 * Calls the provided callback for each key in order. Iteration stops
 * if the callback returns false or all keys have been visited.
 *
 * @param node Set node to iterate
 * @param callback Function to call for each key
 * @param user User data passed to callback
 * @return Number of keys visited
 */
GTEXT_API size_t gtext_yaml_set_iterate(
	const GTEXT_YAML_Node * node,
	GTEXT_YAML_Set_Iterator callback,
	void * user
);

/* ==========================================================================
 * Omap/Pairs Accessors (Phase 7.3a)
 * ==========================================================================
 */

/**
 * @brief Get the number of entries in an ordered map (omap).
 *
 * Returns 0 if the node is not an omap.
 *
 * @param node Omap node to query
 * @return Number of entries in the omap
 */
GTEXT_API size_t gtext_yaml_omap_size(const GTEXT_YAML_Node * node);

/**
 * @brief Get a key-value pair from an omap by index.
 *
 * Returns false if the node is not an omap or the index is out of bounds.
 * Indexing is zero-based and reflects insertion order.
 *
 * @param node Omap node to query
 * @param index Zero-based index of the pair
 * @param key Output pointer for key node (may be NULL)
 * @param value Output pointer for value node (may be NULL)
 * @return true if the pair was retrieved, false otherwise
 */
GTEXT_API bool gtext_yaml_omap_get_at(
	const GTEXT_YAML_Node * node,
	size_t index,
	const GTEXT_YAML_Node ** key,
	const GTEXT_YAML_Node ** value
);

/**
 * @brief Iterator callback for omap traversal.
 *
 * @param key Key node
 * @param value Value node
 * @param index Zero-based index of the pair
 * @param user User data passed to gtext_yaml_omap_iterate()
 * @return true to continue iteration, false to stop
 */
typedef bool (*GTEXT_YAML_Omap_Iterator)(
	const GTEXT_YAML_Node * key,
	const GTEXT_YAML_Node * value,
	size_t index,
	void * user
);

/**
 * @brief Iterate over all key-value pairs in an omap.
 *
 * Calls the provided callback for each pair in insertion order. Iteration
 * stops if the callback returns false or all pairs have been visited.
 *
 * @param node Omap node to iterate
 * @param callback Function to call for each pair
 * @param user User data passed to callback
 * @return Number of pairs visited
 */
GTEXT_API size_t gtext_yaml_omap_iterate(
	const GTEXT_YAML_Node * node,
	GTEXT_YAML_Omap_Iterator callback,
	void * user
);

/**
 * @brief Get the number of entries in a pairs node.
 *
 * Returns 0 if the node is not a pairs node.
 *
 * @param node Pairs node to query
 * @return Number of entries in the pairs list
 */
GTEXT_API size_t gtext_yaml_pairs_size(const GTEXT_YAML_Node * node);

/**
 * @brief Get a key-value pair from a pairs node by index.
 *
 * Returns false if the node is not a pairs node or the index is out of bounds.
 * Indexing is zero-based and reflects insertion order.
 *
 * @param node Pairs node to query
 * @param index Zero-based index of the pair
 * @param key Output pointer for key node (may be NULL)
 * @param value Output pointer for value node (may be NULL)
 * @return true if the pair was retrieved, false otherwise
 */
GTEXT_API bool gtext_yaml_pairs_get_at(
	const GTEXT_YAML_Node * node,
	size_t index,
	const GTEXT_YAML_Node ** key,
	const GTEXT_YAML_Node ** value
);

/**
 * @brief Iterator callback for pairs traversal.
 *
 * @param key Key node
 * @param value Value node
 * @param index Zero-based index of the pair
 * @param user User data passed to gtext_yaml_pairs_iterate()
 * @return true to continue iteration, false to stop
 */
typedef bool (*GTEXT_YAML_Pairs_Iterator)(
	const GTEXT_YAML_Node * key,
	const GTEXT_YAML_Node * value,
	size_t index,
	void * user
);

/**
 * @brief Iterate over all key-value pairs in a pairs node.
 *
 * Calls the provided callback for each pair in insertion order. Iteration
 * stops if the callback returns false or all pairs have been visited.
 *
 * @param node Pairs node to iterate
 * @param callback Function to call for each pair
 * @param user User data passed to callback
 * @return Number of pairs visited
 */
GTEXT_API size_t gtext_yaml_pairs_iterate(
	const GTEXT_YAML_Node * node,
	GTEXT_YAML_Pairs_Iterator callback,
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

/* ============================================================================
 * DOM Manipulation API (Phase 4.7)
 * ============================================================================ */

/**
 * @brief Create a new empty YAML document.
 *
 * The returned document has no root node yet. Use gtext_yaml_document_set_root()
 * to assign a root, or use node creation functions to build the tree.
 *
 * The document must be freed with gtext_yaml_free() when no longer needed.
 *
 * @param options Parse options to configure document limits (may be NULL for defaults)
 * @param error Error output (may be NULL)
 * @return New document on success, NULL on error
 */
GTEXT_API GTEXT_YAML_Document * gtext_yaml_document_new(
	const GTEXT_YAML_Parse_Options * options,
	GTEXT_YAML_Error * error
);

/**
 * @brief Set or replace the root node of a document.
 *
 * The node must have been created in the same document's context using
 * gtext_yaml_node_new_* functions. Returns false if node was created in
 * a different document or if doc is NULL.
 *
 * @param doc Document to modify
 * @param root New root node (may be NULL to clear root)
 * @return true on success, false on error
 */
GTEXT_API bool gtext_yaml_document_set_root(
	GTEXT_YAML_Document * doc,
	GTEXT_YAML_Node * root
);

/**
 * @brief Create a new scalar node.
 *
 * The node is allocated from the document's arena and must not be freed
 * directly. It will be freed when the document is freed.
 *
 * @param doc Document that will own the node
 * @param value String value (will be copied)
 * @param tag Optional tag string (may be NULL)
 * @param anchor Optional anchor name (may be NULL)
 * @return New scalar node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_new_scalar(
	GTEXT_YAML_Document * doc,
	const char * value,
	const char * tag,
	const char * anchor
);

/**
 * @brief Create a new empty sequence node.
 *
 * The node is allocated from the document's arena. Use gtext_yaml_sequence_append()
 * to add child nodes.
 *
 * @param doc Document that will own the node
 * @param tag Optional tag string (may be NULL)
 * @param anchor Optional anchor name (may be NULL)
 * @return New sequence node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_new_sequence(
	GTEXT_YAML_Document * doc,
	const char * tag,
	const char * anchor
);

/**
 * @brief Create a new empty mapping node.
 *
 * The node is allocated from the document's arena. Use gtext_yaml_mapping_set()
 * to add key-value pairs.
 *
 * @param doc Document that will own the node
 * @param tag Optional tag string (may be NULL)
 * @param anchor Optional anchor name (may be NULL)
 * @return New mapping node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_new_mapping(
	GTEXT_YAML_Document * doc,
	const char * tag,
	const char * anchor
);

/**
 * @brief Create a new empty set node.
 *
 * If @p tag is NULL, this defaults to "!!set".
 *
 * @param doc Document that will own the node
 * @param tag Optional tag string (may be NULL)
 * @param anchor Optional anchor name (may be NULL)
 * @return New set node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_new_set(
	GTEXT_YAML_Document * doc,
	const char * tag,
	const char * anchor
);

/**
 * @brief Create a new empty ordered map (omap) node.
 *
 * If @p tag is NULL, this defaults to "!!omap".
 *
 * @param doc Document that will own the node
 * @param tag Optional tag string (may be NULL)
 * @param anchor Optional anchor name (may be NULL)
 * @return New omap node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_new_omap(
	GTEXT_YAML_Document * doc,
	const char * tag,
	const char * anchor
);

/**
 * @brief Create a new empty pairs node.
 *
 * If @p tag is NULL, this defaults to "!!pairs".
 *
 * @param doc Document that will own the node
 * @param tag Optional tag string (may be NULL)
 * @param anchor Optional anchor name (may be NULL)
 * @return New pairs node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_new_pairs(
	GTEXT_YAML_Document * doc,
	const char * tag,
	const char * anchor
);

/**
 * @brief Append a child node to a sequence.
 *
 * The child node must have been created in the same document context.
 * This operation creates a new sequence node with the appended child and
 * returns it. The old sequence node remains valid but should not be used.
 *
 * Important: The returned node is the new sequence. Update any references
 * to the old sequence (like document root) to point to the returned node.
 *
 * @param doc Document owning the sequence (needed for arena allocation)
 * @param sequence Sequence node to modify
 * @param child Node to append
 * @return New sequence node with child appended, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_sequence_append(
	GTEXT_YAML_Document * doc,
	GTEXT_YAML_Node * sequence,
	GTEXT_YAML_Node * child
);

/**
 * @brief Insert a child node at a specific index in a sequence.
 *
 * Existing nodes at index and beyond are shifted right. This operation creates
 * a new sequence node with the inserted child and returns it.
 *
 * Important: The returned node is the new sequence. Update any references
 * to the old sequence to point to the returned node.
 *
 * @param doc Document owning the sequence (needed for arena allocation)
 * @param sequence Sequence node to modify
 * @param index Zero-based index where to insert (0 to length inclusive)
 * @param child Node to insert
 * @return New sequence node with child inserted, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_sequence_insert(
	GTEXT_YAML_Document * doc,
	GTEXT_YAML_Node * sequence,
	size_t index,
	GTEXT_YAML_Node * child
);

/**
 * @brief Remove a child node at a specific index from a sequence.
 *
 * Nodes after index are shifted left. Returns false if sequence is not a
 * sequence node or index is out of bounds.
 *
 * Note: The removed node is not freed (arena-allocated). It remains valid
 * until the document is freed.
 *
 * @param sequence Sequence node to modify
 * @param index Zero-based index of node to remove
 * @return true on success, false on error
 */
GTEXT_API bool gtext_yaml_sequence_remove(
	GTEXT_YAML_Node * sequence,
	size_t index
);

/**
 * @brief Set or add a key-value pair in a mapping.
 *
 * If the key already exists, its value is replaced (last-wins). If the key
 * doesn't exist, a new pair is added. This operation creates a new mapping
 * node and returns it.
 *
 * Important: The returned node is the new mapping. Update any references
 * to the old mapping to point to the returned node.
 *
 * For string keys, use gtext_yaml_node_new_scalar() to create the key node.
 *
 * @param doc Document owning the mapping (needed for arena allocation)
 * @param mapping Mapping node to modify
 * @param key Key node
 * @param value Value node
 * @return New mapping node with key-value set, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_mapping_set(
	GTEXT_YAML_Document * doc,
	GTEXT_YAML_Node * mapping,
	GTEXT_YAML_Node * key,
	GTEXT_YAML_Node * value
);

/**
 * @brief Remove a key-value pair from a mapping by string key.
 *
 * Performs a linear search for a scalar key matching the given string.
 * Returns true if the pair was found and removed, false otherwise.
 *
 * Note: The removed nodes are not freed (arena-allocated). They remain valid
 * until the document is freed.
 *
 * @param mapping Mapping node to modify
 * @param key String key to remove
 * @return true if pair was removed, false if not found or mapping is invalid
 */
GTEXT_API bool gtext_yaml_mapping_delete(
	GTEXT_YAML_Node * mapping,
	const char * key
);

/**
 * @brief Check if a mapping contains a string key.
 *
 * Performs a linear search for a scalar key matching the given string.
 *
 * @param mapping Mapping node to search
 * @param key String key to look for
 * @return true if key exists, false otherwise
 */
GTEXT_API bool gtext_yaml_mapping_has_key(
	const GTEXT_YAML_Node * mapping,
	const char * key
);

/**
 * @brief Deep-clone a node into a target document.
 *
 * Creates a deep copy of @p node and all of its descendants, allocating
 * all new nodes from @p doc's arena. The clone preserves tags, anchors,
 * and alias relationships, and handles cyclic graphs by reusing already
 * cloned nodes.
 *
 * @param doc Target document to own the cloned nodes
 * @param node Node to clone
 * @return Cloned node, or NULL on error
 */
GTEXT_API GTEXT_YAML_Node * gtext_yaml_node_clone(
	GTEXT_YAML_Document * doc,
	const GTEXT_YAML_Node * node
);

/**
 * @brief Convert a YAML DOM to a JSON DOM.
 *
 * Converts a YAML document to a JSON value for documents that only use
 * JSON-compatible features. Rejects YAML-specific features:
 * - Anchors and aliases (anchor/alias references)
 * - Non-standard tags (set, omap, pairs, custom tags)
 * - Complex keys (only string keys allowed in JSON objects)
 * - Merge keys (<<)
 *
 * For opt-in conversions that relax these constraints, use
 * gtext_yaml_to_json_with_options().
 *
 * Custom tags can be converted by registering a GTEXT_YAML_Custom_Tag with
 * a JSON converter callback and enabling custom tags in the conversion
 * options.
 *
 * Limitations:
 * - Custom tags are not preserved in the DOM; use the streaming API for tag-aware validation.
 * - Merge keys are resolved before the DOM is exposed; use the merge key flag or streaming API
 *   if you need to detect their presence.
 * - Only the first document in a multi-document stream is converted.
 * - Large integers outside JSON safe range require an explicit large-int policy.
 *
 * The JSON value and all its descendants are owned by the caller and
 * must be freed with gtext_json_free().
 *
 * Supported conversions:
 * - YAML null → JSON null
 * - YAML bool → JSON boolean
 * - YAML int/float → JSON number
 * - YAML string → JSON string
 * - YAML sequence → JSON array
 * - YAML mapping with string keys → JSON object
 *
 * @param yaml_doc YAML document to convert
 * @param out_json Pointer to store converted JSON value
 * @param out_err Error output (may be NULL)
 * @return GTEXT_YAML_OK on success, error code if document contains incompatible features
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json(
	const GTEXT_YAML_Document * yaml_doc,
	GTEXT_JSON_Value ** out_json,
	GTEXT_YAML_Error * out_err
);

/**
 * @enum GTEXT_YAML_JSON_Large_Int_Policy
 * @brief Controls handling of integers outside JSON safe range.
 */
typedef enum {
	GTEXT_YAML_JSON_LARGE_INT_ERROR,
	GTEXT_YAML_JSON_LARGE_INT_STRING,
	GTEXT_YAML_JSON_LARGE_INT_DOUBLE
} GTEXT_YAML_JSON_Large_Int_Policy;

/**
 * @struct GTEXT_YAML_To_JSON_Options
 * @brief Options controlling YAML to JSON conversion behavior.
 */
typedef struct {
	bool allow_resolved_aliases;    /* Resolve alias nodes to targets. */
	bool allow_merge_keys;          /* Allow merge-expanded mappings. */
	bool coerce_keys_to_strings;    /* Convert scalar keys to strings. */
	GTEXT_YAML_JSON_Large_Int_Policy large_int_policy;
	bool enable_custom_tags;        /* Enable custom tag JSON conversions. */
	const GTEXT_YAML_Custom_Tag * custom_tags;
	size_t custom_tag_count;
} GTEXT_YAML_To_JSON_Options;

/**
 * @brief Return YAML to JSON options initialized to strict defaults.
 */
GTEXT_API GTEXT_YAML_To_JSON_Options gtext_yaml_to_json_options_default(void);

/**
 * @brief Convert a YAML DOM to a JSON DOM with conversion options.
 *
 * @param yaml_doc YAML document to convert
 * @param out_json Pointer to store converted JSON value
 * @param options Conversion options (NULL for defaults)
 * @param out_err Error output (may be NULL)
 * @return GTEXT_YAML_OK on success, error code if document contains incompatible features
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json_with_options(
	const GTEXT_YAML_Document * yaml_doc,
	GTEXT_JSON_Value ** out_json,
	const GTEXT_YAML_To_JSON_Options * options,
	GTEXT_YAML_Error * out_err
);

/**
 * @brief Convert YAML input to JSON while validating explicit tags.
 *
 * Uses the streaming parser to detect explicit tags and rejects tags
 * that are not JSON-compatible before converting the first document
 * into a JSON DOM.
 *
 * @param input Input YAML string
 * @param length Length of input string in bytes
 * @param parse_options Parse options (NULL for defaults)
 * @param json_options Conversion options (NULL for defaults)
 * @param out_json Pointer to store converted JSON value
 * @param out_err Error output (may be NULL)
 * @return GTEXT_YAML_OK on success, error code if tag validation or conversion fails
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json_with_tags(
	const char * input,
	size_t length,
	const GTEXT_YAML_Parse_Options * parse_options,
	const GTEXT_YAML_To_JSON_Options * json_options,
	GTEXT_JSON_Value ** out_json,
	GTEXT_YAML_Error * out_err
);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_TEXT_YAML_DOM_H

