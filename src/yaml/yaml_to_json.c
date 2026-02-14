/**
 * @file yaml_to_json.c
 * @brief YAML to JSON conversion utility.
 *
 * Provides conversion from YAML DOM to JSON DOM for compatible documents.
 * Rejects YAML-specific features incompatible with JSON:
 * - Anchors and aliases
 * - Non-standard tags (except basic scalars)
 * - Merge keys (<<)
 * - Complex keys in mappings
 * - Special YAML types (set, omap, pairs)
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/macros.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Check if a node has a reference (anchor/alias).
 *
 * @param n Node to check
 * @return true if node is an alias or has an anchor, false otherwise
 */
static bool node_has_reference(const GTEXT_YAML_Node * n)
{
	GTEXT_YAML_Node_Type type = gtext_yaml_node_type(n);
	return type == GTEXT_YAML_ALIAS;
}

/**
 * @brief Check if a node has a non-convertible tag.
 *
 * JSON doesn't support custom tags or YAML-specific types like set, omap, pairs.
 * Only basic scalar types are allowed.
 *
 * @param n Node to check
 * @return true if node has incompatible tag, false otherwise
 */
static bool node_has_incompatible_tag(const GTEXT_YAML_Node * n)
{
	GTEXT_YAML_Node_Type type = gtext_yaml_node_type(n);

	/* These types are YAML-specific and cannot be represented in JSON */
	if (type == GTEXT_YAML_SET || type == GTEXT_YAML_OMAP || type == GTEXT_YAML_PAIRS) {
		return true;
	}

	return false;
}

/**
 * @brief Recursively convert a YAML node to a JSON value.
 *
 * @param yaml_node YAML node to convert
 * @param out_json Pointer to store converted JSON value
 * @param out_err Error output (may be NULL)
 * @return GTEXT_YAML_OK on success, error code otherwise
 */
static GTEXT_YAML_Status convert_node(
	const GTEXT_YAML_Node * yaml_node,
	GTEXT_JSON_Value ** out_json,
	GTEXT_YAML_Error * out_err
)
{
	if (!yaml_node || !out_json) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "convert_node: invalid arguments";
		}
		return GTEXT_YAML_E_INVALID;
	}

	/* Check for incompatible references (anchors/aliases) */
	if (node_has_reference(yaml_node)) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: node has anchor or alias reference";
		}
		return GTEXT_YAML_E_INVALID;
	}

	/* Check for incompatible tags */
	if (node_has_incompatible_tag(yaml_node)) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: YAML-specific type (set/omap/pairs) not compatible with JSON";
		}
		return GTEXT_YAML_E_INVALID;
	}

	GTEXT_YAML_Node_Type type = gtext_yaml_node_type(yaml_node);

	switch (type) {
	case GTEXT_YAML_NULL: {
		*out_json = gtext_json_new_null();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON null";
			}
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_BOOL: {
		bool value;
		if (!gtext_yaml_node_as_bool(yaml_node, &value)) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract boolean value from YAML node";
			}
			return GTEXT_YAML_E_INVALID;
		}
		*out_json = gtext_json_new_bool(value);
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON boolean";
			}
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_INT: {
		int64_t value;
		if (!gtext_yaml_node_as_int(yaml_node, &value)) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract integer value from YAML node";
			}
			return GTEXT_YAML_E_INVALID;
		}
		*out_json = gtext_json_new_number_i64(value);
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON number";
			}
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_FLOAT: {
		double value;
		if (!gtext_yaml_node_as_float(yaml_node, &value)) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract float value from YAML node";
			}
			return GTEXT_YAML_E_INVALID;
		}
		*out_json = gtext_json_new_number_double(value);
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON number";
			}
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_STRING: {
		const char * value = gtext_yaml_node_as_string(yaml_node);
		if (!value) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract string value from YAML node";
			}
			return GTEXT_YAML_E_INVALID;
		}
		*out_json = gtext_json_new_string(value, strlen(value));
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON string";
			}
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_SEQUENCE: {
		*out_json = gtext_json_new_array();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON array";
			}
			return GTEXT_YAML_E_OOM;
		}

		size_t length = gtext_yaml_sequence_length(yaml_node);
		for (size_t i = 0; i < length; i++) {
			const GTEXT_YAML_Node * child = gtext_yaml_sequence_get(yaml_node, i);
			if (!child) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to access sequence element";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				return GTEXT_YAML_E_INVALID;
			}

			GTEXT_JSON_Value * elem = NULL;
			GTEXT_YAML_Status status = convert_node(child, &elem, out_err);
			if (status != GTEXT_YAML_OK) {
				gtext_json_free(*out_json);
				*out_json = NULL;
				return status;
			}

			GTEXT_JSON_Status json_status = gtext_json_array_push(*out_json, elem);
			if (json_status != GTEXT_JSON_OK) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_OOM;
					out_err->message = "failed to add element to JSON array";
				}
				gtext_json_free(*out_json);
				gtext_json_free(elem);
				*out_json = NULL;
				return GTEXT_YAML_E_OOM;
			}
		}

		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_MAPPING: {
		*out_json = gtext_json_new_object();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON object";
			}
			return GTEXT_YAML_E_OOM;
		}

		size_t length = gtext_yaml_mapping_size(yaml_node);
		for (size_t i = 0; i < length; i++) {
			const GTEXT_YAML_Node * key_node = NULL;
			const GTEXT_YAML_Node * value_node = NULL;

			if (!gtext_yaml_mapping_get_at(yaml_node, i, &key_node, &value_node)) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to access mapping pair";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				return GTEXT_YAML_E_INVALID;
			}

			if (!key_node || !value_node) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to access mapping key or value";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				return GTEXT_YAML_E_INVALID;
			}

			/* JSON requires string keys */
			GTEXT_YAML_Node_Type key_type = gtext_yaml_node_type(key_node);
			if (key_type != GTEXT_YAML_STRING) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "cannot convert: JSON requires string keys in objects";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				return GTEXT_YAML_E_INVALID;
			}

			const char * key = gtext_yaml_node_as_string(key_node);
			if (!key) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to extract key string from YAML mapping";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				return GTEXT_YAML_E_INVALID;
			}

			GTEXT_JSON_Value * value = NULL;
			GTEXT_YAML_Status status = convert_node(value_node, &value, out_err);
			if (status != GTEXT_YAML_OK) {
				gtext_json_free(*out_json);
				*out_json = NULL;
				return status;
			}

			GTEXT_JSON_Status json_status = gtext_json_object_put(*out_json, key, strlen(key), value);
			if (json_status != GTEXT_JSON_OK) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_OOM;
					out_err->message = "failed to add key-value pair to JSON object";
				}
				gtext_json_free(*out_json);
				gtext_json_free(value);
				*out_json = NULL;
				return GTEXT_YAML_E_OOM;
			}
		}

		return GTEXT_YAML_OK;
	}

	case GTEXT_YAML_ALIAS:
		/* Already checked above, but handle explicitly for completeness */
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: node is an alias (anchors/aliases not supported in JSON)";
		}
		return GTEXT_YAML_E_INVALID;

	case GTEXT_YAML_SET:
	case GTEXT_YAML_OMAP:
	case GTEXT_YAML_PAIRS:
		/* Already checked above, but handle explicitly for completeness */
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: YAML-specific type not compatible with JSON";
		}
		return GTEXT_YAML_E_INVALID;

	default:
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "unknown YAML node type";
		}
		return GTEXT_YAML_E_INVALID;
	}
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json(
	const GTEXT_YAML_Document * yaml_doc,
	GTEXT_JSON_Value ** out_json,
	GTEXT_YAML_Error * out_err
)
{
	if (!yaml_doc || !out_json) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "gtext_yaml_to_json: invalid arguments (doc and out_json must not be NULL)";
		}
		return GTEXT_YAML_E_INVALID;
	}

	const GTEXT_YAML_Node * root = gtext_yaml_document_root(yaml_doc);
	if (!root) {
		/* Empty document → JSON null */
		*out_json = gtext_json_new_null();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON null for empty document";
			}
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}

	return convert_node(root, out_json, out_err);
}
