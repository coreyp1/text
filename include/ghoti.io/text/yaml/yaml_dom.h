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

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_TEXT_YAML_DOM_H
