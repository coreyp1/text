/**
 * @file yaml_to_json.c
 * @brief YAML to JSON conversion utility.
 *
 * Provides conversion from YAML DOM to JSON DOM for compatible documents.
 * Strictly rejects YAML-specific features incompatible with JSON by default:
 * - Anchors and aliases
 * - Non-standard tags (except basic scalars)
 * - Merge keys (<<)
 * - Complex keys in mappings
 * - Special YAML types (set, omap, pairs)
 * Options allow relaxing some of these constraints.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/yaml/yaml_stream.h>

#define GTEXT_YAML_JSON_MAX_SAFE_INT 9007199254740991LL
#define GTEXT_YAML_JSON_MIN_SAFE_INT (-9007199254740991LL)

typedef struct {
	GTEXT_YAML_To_JSON_Options options;
	const GTEXT_YAML_Node **stack;
	size_t stack_len;
	size_t stack_cap;
} yaml_to_json_context;

static bool yaml_tag_is_json_compatible(const char *tag)
{
	static const char * allowed_tags[] = {
		"!!str",
		"!!int",
		"!!float",
		"!!bool",
		"!!null",
		"!!seq",
		"!!map",
		"!!timestamp",
		"!!binary",
	};

	if (!tag) {
		return true;
	}

	for (size_t i = 0; i < sizeof(allowed_tags) / sizeof(allowed_tags[0]); i++) {
		if (strcmp(tag, allowed_tags[i]) == 0) {
			return true;
		}
	}

	return false;
}

static const GTEXT_YAML_Custom_Tag *yaml_to_json_find_custom_tag(
	const GTEXT_YAML_To_JSON_Options *options,
	const char *tag
)
{
	if (!options || !tag || !options->enable_custom_tags) {
		return NULL;
	}
	if (!options->custom_tags || options->custom_tag_count == 0) {
		return NULL;
	}

	for (size_t i = 0; i < options->custom_tag_count; i++) {
		const GTEXT_YAML_Custom_Tag *handler = &options->custom_tags[i];
		if (!handler->tag) {
			continue;
		}
		if (strcmp(handler->tag, tag) == 0) {
			return handler;
		}
	}

	return NULL;
}

static GTEXT_YAML_Status yaml_to_json_validate_tags(
	const char * input,
	size_t length,
	const GTEXT_YAML_Parse_Options * parse_options,
	const GTEXT_YAML_To_JSON_Options * json_options,
	GTEXT_YAML_Error * out_err
)
{
	GTEXT_YAML_Reader * reader = NULL;
	GTEXT_YAML_Status status = GTEXT_YAML_OK;

	reader = gtext_yaml_reader_new(parse_options);
	if (!reader) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_OOM;
			out_err->message = "out of memory creating YAML reader";
		}
		return GTEXT_YAML_E_OOM;
	}

	status = gtext_yaml_reader_feed(reader, input, length, out_err);
	if (status != GTEXT_YAML_OK) {
		gtext_yaml_reader_free(reader);
		return status;
	}

	status = gtext_yaml_reader_feed(reader, NULL, 0, out_err);
	if (status != GTEXT_YAML_OK) {
		gtext_yaml_reader_free(reader);
		return status;
	}

	for (;;) {
		GTEXT_YAML_Event event;
		status = gtext_yaml_reader_next(reader, &event, out_err);
		if (status == GTEXT_YAML_E_STATE) {
			break;
		}
		if (status != GTEXT_YAML_OK) {
			gtext_yaml_reader_free(reader);
			return status;
		}
		if (event.tag && !yaml_tag_is_json_compatible(event.tag)) {
			const GTEXT_YAML_Custom_Tag *handler = yaml_to_json_find_custom_tag(
				json_options,
				event.tag
			);
			if (handler) {
				continue;
			}
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "cannot convert: explicit tag not JSON-compatible";
				out_err->offset = event.offset;
				out_err->line = event.line;
				out_err->col = event.col;
			}
			gtext_yaml_reader_free(reader);
			return GTEXT_YAML_E_INVALID;
		}
	}

	gtext_yaml_reader_free(reader);
	return GTEXT_YAML_OK;
}

static bool yaml_to_json_stack_contains(
	const yaml_to_json_context *ctx,
	const GTEXT_YAML_Node *node
)
{
	for (size_t i = 0; i < ctx->stack_len; i++) {
		if (ctx->stack[i] == node) {
			return true;
		}
	}

	return false;
}

static bool yaml_to_json_stack_push(
	yaml_to_json_context *ctx,
	const GTEXT_YAML_Node *node,
	GTEXT_YAML_Error *out_err
)
{
	if (ctx->stack_len == ctx->stack_cap) {
		size_t new_cap = ctx->stack_cap == 0 ? 16 : ctx->stack_cap * 2;
		const GTEXT_YAML_Node **new_stack = (const GTEXT_YAML_Node **)realloc(
			ctx->stack,
			new_cap * sizeof(*new_stack)
		);
		if (!new_stack) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory tracking YAML conversion stack";
			}
			return false;
		}
		ctx->stack = new_stack;
		ctx->stack_cap = new_cap;
	}

	ctx->stack[ctx->stack_len++] = node;
	return true;
}

static void yaml_to_json_stack_pop(yaml_to_json_context *ctx)
{
	if (ctx->stack_len > 0) {
		ctx->stack_len--;
	}
}

/**
 * @brief Check if a node has a reference (anchor/alias).
 *
 * @param n Node to check
 * @return true if node is an alias or has an anchor, false otherwise
 */
static bool node_is_alias(const GTEXT_YAML_Node * n)
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
	yaml_to_json_context *ctx,
	GTEXT_YAML_Error * out_err
)
{
	GTEXT_YAML_Node_Type type = GTEXT_YAML_NULL;
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	const GTEXT_YAML_Node * target = NULL;
	const GTEXT_YAML_Custom_Tag * handler = NULL;
	const char * tag = NULL;

	if (!yaml_node || !out_json) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "convert_node: invalid arguments";
		}
		return GTEXT_YAML_E_INVALID;
	}

	if (yaml_to_json_stack_contains(ctx, yaml_node)) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: cyclic alias reference detected";
		}
		return GTEXT_YAML_E_INVALID;
	}

	/* Check for incompatible references (anchors/aliases) */
	if (node_is_alias(yaml_node)) {
		if (!ctx->options.allow_resolved_aliases) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "cannot convert: node is an alias reference";
			}
			return GTEXT_YAML_E_INVALID;
		}

		target = gtext_yaml_alias_target(yaml_node);
		if (!target || target == yaml_node) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "cannot convert: unresolved alias reference";
			}
			return GTEXT_YAML_E_INVALID;
		}

		return convert_node(target, out_json, ctx, out_err);
	}

	tag = gtext_yaml_node_tag(yaml_node);
	if (tag && ctx->options.enable_custom_tags) {
		handler = yaml_to_json_find_custom_tag(&ctx->options, tag);
		if (handler && handler->to_json) {
			status = handler->to_json(yaml_node, tag, handler->user, out_json, out_err);
			if (status == GTEXT_YAML_OK && !*out_json) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "custom tag JSON converter did not return a value";
				}
				return GTEXT_YAML_E_INVALID;
			}
			return status;
		}
	}

	/* Check for incompatible tags */
	if (node_has_incompatible_tag(yaml_node)) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: YAML-specific type (set/omap/pairs) not compatible with JSON";
		}
		return GTEXT_YAML_E_INVALID;
	}

	type = gtext_yaml_node_type(yaml_node);
	if (!yaml_to_json_stack_push(ctx, yaml_node, out_err)) {
		return GTEXT_YAML_E_OOM;
	}

	switch (type) {
	case GTEXT_YAML_NULL: {
		*out_json = gtext_json_new_null();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON null";
			}
			status = GTEXT_YAML_E_OOM;
			break;
		}
		status = GTEXT_YAML_OK;
		break;
	}

	case GTEXT_YAML_BOOL: {
		bool value;
		if (!gtext_yaml_node_as_bool(yaml_node, &value)) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract boolean value from YAML node";
			}
			status = GTEXT_YAML_E_INVALID;
			break;
		}
		*out_json = gtext_json_new_bool(value);
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON boolean";
			}
			status = GTEXT_YAML_E_OOM;
			break;
		}
		status = GTEXT_YAML_OK;
		break;
	}

	case GTEXT_YAML_INT: {
		int64_t value;
		if (!gtext_yaml_node_as_int(yaml_node, &value)) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract integer value from YAML node";
			}
			status = GTEXT_YAML_E_INVALID;
			break;
		}
		if (value < GTEXT_YAML_JSON_MIN_SAFE_INT || value > GTEXT_YAML_JSON_MAX_SAFE_INT) {
			switch (ctx->options.large_int_policy) {
			case GTEXT_YAML_JSON_LARGE_INT_ERROR:
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "cannot convert: integer exceeds JSON safe range";
				}
				status = GTEXT_YAML_E_INVALID;
				break;
			case GTEXT_YAML_JSON_LARGE_INT_STRING: {
				const char * value_str = gtext_yaml_node_as_string(yaml_node);
				char buffer[32];
				if (!value_str) {
					snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
					value_str = buffer;
				}
				*out_json = gtext_json_new_string(value_str, strlen(value_str));
				if (!*out_json) {
					if (out_err) {
						out_err->code = GTEXT_YAML_E_OOM;
						out_err->message = "out of memory creating JSON string";
					}
					status = GTEXT_YAML_E_OOM;
					break;
				}
				status = GTEXT_YAML_OK;
				break;
			}
			case GTEXT_YAML_JSON_LARGE_INT_DOUBLE:
				*out_json = gtext_json_new_number_double((double)value);
				if (!*out_json) {
					if (out_err) {
						out_err->code = GTEXT_YAML_E_OOM;
						out_err->message = "out of memory creating JSON number";
					}
					status = GTEXT_YAML_E_OOM;
					break;
				}
				status = GTEXT_YAML_OK;
				break;
			default:
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "invalid large integer policy";
				}
				status = GTEXT_YAML_E_INVALID;
				break;
			}
			break;
		}

		*out_json = gtext_json_new_number_i64(value);
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON number";
			}
			status = GTEXT_YAML_E_OOM;
			break;
		}
		status = GTEXT_YAML_OK;
		break;
	}

	case GTEXT_YAML_FLOAT: {
		double value;
		if (!gtext_yaml_node_as_float(yaml_node, &value)) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract float value from YAML node";
			}
			status = GTEXT_YAML_E_INVALID;
			break;
		}
		*out_json = gtext_json_new_number_double(value);
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON number";
			}
			status = GTEXT_YAML_E_OOM;
			break;
		}
		status = GTEXT_YAML_OK;
		break;
	}

	case GTEXT_YAML_STRING: {
		const char * value = gtext_yaml_node_as_string(yaml_node);
		if (!value) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_INVALID;
				out_err->message = "failed to extract string value from YAML node";
			}
			status = GTEXT_YAML_E_INVALID;
			break;
		}
		*out_json = gtext_json_new_string(value, strlen(value));
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON string";
			}
			status = GTEXT_YAML_E_OOM;
			break;
		}
		status = GTEXT_YAML_OK;
		break;
	}

	case GTEXT_YAML_SEQUENCE: {
		*out_json = gtext_json_new_array();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON array";
			}
			status = GTEXT_YAML_E_OOM;
			break;
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
				status = GTEXT_YAML_E_INVALID;
				break;
			}

			GTEXT_JSON_Value * elem = NULL;
			GTEXT_YAML_Status child_status = convert_node(child, &elem, ctx, out_err);
			if (child_status != GTEXT_YAML_OK) {
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = child_status;
				break;
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
				status = GTEXT_YAML_E_OOM;
				break;
			}
		}

		break;
	}

	case GTEXT_YAML_MAPPING: {
		*out_json = gtext_json_new_object();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON object";
			}
			status = GTEXT_YAML_E_OOM;
			break;
		}

		size_t length = gtext_yaml_mapping_size(yaml_node);
		for (size_t i = 0; i < length; i++) {
			const GTEXT_YAML_Node * key_node = NULL;
			const GTEXT_YAML_Node * value_node = NULL;
			const GTEXT_YAML_Node * resolved_key = NULL;
			GTEXT_YAML_Node_Type key_type = GTEXT_YAML_NULL;
			const char * key = NULL;
			GTEXT_JSON_Value * value = NULL;
			GTEXT_YAML_Status value_status = GTEXT_YAML_OK;
			GTEXT_JSON_Status json_status = GTEXT_JSON_OK;

			if (!gtext_yaml_mapping_get_at(yaml_node, i, &key_node, &value_node)) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to access mapping pair";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = GTEXT_YAML_E_INVALID;
				break;
			}

			if (!key_node || !value_node) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to access mapping key or value";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = GTEXT_YAML_E_INVALID;
				break;
			}

			resolved_key = key_node;
			if (node_is_alias(key_node)) {
				if (!ctx->options.allow_resolved_aliases) {
					if (out_err) {
						out_err->code = GTEXT_YAML_E_INVALID;
						out_err->message = "cannot convert: alias used as mapping key";
					}
					gtext_json_free(*out_json);
					*out_json = NULL;
					status = GTEXT_YAML_E_INVALID;
					break;
				}
				resolved_key = gtext_yaml_alias_target(key_node);
				if (!resolved_key || resolved_key == key_node) {
					if (out_err) {
						out_err->code = GTEXT_YAML_E_INVALID;
						out_err->message = "cannot convert: unresolved alias key";
					}
					gtext_json_free(*out_json);
					*out_json = NULL;
					status = GTEXT_YAML_E_INVALID;
					break;
				}
			}

			key_type = gtext_yaml_node_type(resolved_key);
			if (key_type != GTEXT_YAML_STRING && !ctx->options.coerce_keys_to_strings) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "cannot convert: JSON requires string keys in objects";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = GTEXT_YAML_E_INVALID;
				break;
			}

			if (key_type != GTEXT_YAML_STRING && ctx->options.coerce_keys_to_strings) {
				if (key_type != GTEXT_YAML_NULL &&
					key_type != GTEXT_YAML_BOOL &&
					key_type != GTEXT_YAML_INT &&
					key_type != GTEXT_YAML_FLOAT) {
					if (out_err) {
						out_err->code = GTEXT_YAML_E_INVALID;
						out_err->message = "cannot convert: complex mapping key cannot be coerced";
					}
					gtext_json_free(*out_json);
					*out_json = NULL;
					status = GTEXT_YAML_E_INVALID;
					break;
				}
			}

			key = gtext_yaml_node_as_string(resolved_key);
			if (!key) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "failed to extract key string from YAML mapping";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = GTEXT_YAML_E_INVALID;
				break;
			}

			/* Reject merge keys (YAML 1.1 extension not compatible with JSON) */
			if (!ctx->options.allow_merge_keys && strcmp(key, "<<") == 0) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_INVALID;
					out_err->message = "cannot convert: YAML merge keys (<<) are not compatible with JSON";
				}
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = GTEXT_YAML_E_INVALID;
				break;
			}

			value_status = convert_node(value_node, &value, ctx, out_err);
			if (value_status != GTEXT_YAML_OK) {
				gtext_json_free(*out_json);
				*out_json = NULL;
				status = value_status;
				break;
			}

			json_status = gtext_json_object_put(*out_json, key, strlen(key), value);
			if (json_status != GTEXT_JSON_OK) {
				if (out_err) {
					out_err->code = GTEXT_YAML_E_OOM;
					out_err->message = "failed to add key-value pair to JSON object";
				}
				gtext_json_free(*out_json);
				gtext_json_free(value);
				*out_json = NULL;
				status = GTEXT_YAML_E_OOM;
				break;
			}
		}

		break;
	}

	case GTEXT_YAML_ALIAS:
		/* Already checked above, but handle explicitly for completeness */
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: node is an alias (anchors/aliases not supported in JSON)";
		}
		status = GTEXT_YAML_E_INVALID;
		break;

	case GTEXT_YAML_SET:
	case GTEXT_YAML_OMAP:
	case GTEXT_YAML_PAIRS:
		/* Already checked above, but handle explicitly for completeness */
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: YAML-specific type not compatible with JSON";
		}
		status = GTEXT_YAML_E_INVALID;
		break;

	default:
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "unknown YAML node type";
		}
		status = GTEXT_YAML_E_INVALID;
		break;
	}

	yaml_to_json_stack_pop(ctx);
	return status;
}

GTEXT_API GTEXT_YAML_To_JSON_Options gtext_yaml_to_json_options_default(void)
{
	GTEXT_YAML_To_JSON_Options options = {
		.allow_resolved_aliases = false,
		.allow_merge_keys = false,
		.coerce_keys_to_strings = false,
		.large_int_policy = GTEXT_YAML_JSON_LARGE_INT_ERROR,
		.enable_custom_tags = false,
		.custom_tags = NULL,
		.custom_tag_count = 0,
	};

	return options;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json_with_options(
	const GTEXT_YAML_Document * yaml_doc,
	GTEXT_JSON_Value ** out_json,
	const GTEXT_YAML_To_JSON_Options * options,
	GTEXT_YAML_Error * out_err
)
{
	yaml_to_json_context ctx = {0};
	const GTEXT_YAML_Node * root = NULL;
	GTEXT_YAML_Status status = GTEXT_YAML_OK;

	if (!yaml_doc || !out_json) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "gtext_yaml_to_json: invalid arguments (doc and out_json must not be NULL)";
		}
		return GTEXT_YAML_E_INVALID;
	}

	ctx.options = options ? *options : gtext_yaml_to_json_options_default();

	if (gtext_yaml_document_has_merge_keys(yaml_doc) && !ctx.options.allow_merge_keys) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "cannot convert: YAML merge keys (<<) are not compatible with JSON";
		}
		free(ctx.stack);
		return GTEXT_YAML_E_INVALID;
	}

	root = gtext_yaml_document_root(yaml_doc);
	if (!root) {
		/* Empty document → JSON null */
		*out_json = gtext_json_new_null();
		if (!*out_json) {
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "out of memory creating JSON null for empty document";
			}
			free(ctx.stack);
			return GTEXT_YAML_E_OOM;
		}
		free(ctx.stack);
		return GTEXT_YAML_OK;
	}

	status = convert_node(root, out_json, &ctx, out_err);
	free(ctx.stack);
	return status;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json(
	const GTEXT_YAML_Document * yaml_doc,
	GTEXT_JSON_Value ** out_json,
	GTEXT_YAML_Error * out_err
)
{
	return gtext_yaml_to_json_with_options(yaml_doc, out_json, NULL, out_err);
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_to_json_with_tags(
	const char * input,
	size_t length,
	const GTEXT_YAML_Parse_Options * parse_options,
	const GTEXT_YAML_To_JSON_Options * json_options,
	GTEXT_JSON_Value ** out_json,
	GTEXT_YAML_Error * out_err
)
{
	GTEXT_YAML_Document * yaml_doc = NULL;
	GTEXT_YAML_Status status = GTEXT_YAML_OK;

	if (!input || !out_json) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "gtext_yaml_to_json_with_tags: invalid arguments";
		}
		return GTEXT_YAML_E_INVALID;
	}

	*out_json = NULL;

	status = yaml_to_json_validate_tags(
		input,
		length,
		parse_options,
		json_options,
		out_err
	);
	if (status != GTEXT_YAML_OK) {
		return status;
	}

	yaml_doc = gtext_yaml_parse(input, length, parse_options, out_err);
	if (!yaml_doc) {
		status = out_err ? out_err->code : GTEXT_YAML_E_INVALID;
		return status == GTEXT_YAML_OK ? GTEXT_YAML_E_INVALID : status;
	}

	status = gtext_yaml_to_json_with_options(
		yaml_doc,
		out_json,
		json_options,
		out_err
	);
	gtext_yaml_free(yaml_doc);
	return status;
}
