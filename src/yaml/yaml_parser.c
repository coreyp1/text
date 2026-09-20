/**
 * @file yaml_parser.c
 * @brief DOM parser - converts streaming events to DOM tree
 *
 * Implements gtext_yaml_parse() by using the streaming parser internally
 * and building a DOM tree from events. Uses a stack to track nesting.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#define _POSIX_C_SOURCE 200809L  /* for strdup */

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/json/json_dom.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Saved temp state for a nesting level */
typedef struct {
	GTEXT_YAML_Node **items;
	size_t count;
	size_t capacity;
	char *anchor;  /* Anchor for this collection level (malloc'd, NULL if none) */
	char *tag;     /* Tag for this collection level (malloc'd, NULL if none) */
	size_t source_offset; /* Byte offset for collection start */
	int source_line;      /* 1-based line for collection start */
	int source_col;       /* 1-based column for collection start */
} saved_temp;

/* Anchor map entry */
typedef struct {
	char *name;              /* Anchor name (malloc'd) */
	GTEXT_YAML_Node *node;   /* Associated node */
} anchor_entry;

typedef struct {
	char *handle;
	char *prefix;
} tag_handle_entry;

/* Per-level flow flags.  PAIR marks a "[a: 1]" single-pair mapping, which has
 * no "}" and is closed by the "," or "]" that ends the entry.  ITEM_DONE says
 * a flow collection already holds a complete entry, so the next one needs a
 * "," in front of it. */
#define GTEXT_YAML_FLOW_PAIR      0x1u
#define GTEXT_YAML_FLOW_ITEM_DONE 0x2u

/* Parser state for building DOM from events */
typedef struct {
	yaml_context *ctx;                  /* Context owns arena */
	GTEXT_YAML_Document *doc;           /* Document being built */
	GTEXT_YAML_Error *error;            /* Error output */
	
	/* Stack for tracking nesting (sequences/mappings in progress) */
	struct {
		GTEXT_YAML_Node **nodes;        /* Stack of nodes being built */
		int *states;                    /* State per level: 0=seq, 1=map_key, 2=map_value */
		int *indents;                   /* Indent level for block collections */
		bool *is_block;                 /* True if this level is block-style */
		unsigned char *flow_flags;      /* GTEXT_YAML_FLOW_* bits, per level */
		saved_temp *temps;              /* Saved temp state per level */
		size_t capacity;
		size_t depth;
	} stack;
	
	/* Temporary storage for building collections */
	struct {
		GTEXT_YAML_Node **items;        /* Child nodes */
		size_t count;
		size_t capacity;
	} temp;
	
	/* Anchor map for resolving aliases */
	struct {
		anchor_entry *entries;          /* Array of anchor entries */
		size_t count;
		size_t capacity;
	} anchors;
	
	/* List of alias nodes to resolve after parsing */
	struct {
		GTEXT_YAML_Node **nodes;        /* Array of alias nodes */
		size_t count;
		size_t capacity;
	} aliases;

	/* Tag handle map for %TAG directives */
	struct {
		tag_handle_entry *entries;
		size_t count;
		size_t capacity;
	} tag_handles;
	
	GTEXT_YAML_Node *root;              /* Root node once complete */
	bool failed;                        /* True if error occurred */
	size_t document_count;              /* Number of documents seen (0-based) */
	bool document_started;              /* True if inside a document */
	bool first_document_complete;       /* True when first document is done */
	
	/* Block collection detection state */
	bool in_block_sequence;             /* True if we're building a block sequence */
	bool in_block_mapping;              /* True if we're building a block mapping */
	bool expect_mapping_value;          /* True if next item is a mapping value after : */
	int last_event_line;                /* Last event line processed */
	int last_scalar_line;               /* Last scalar event line */
	int last_scalar_col;                /* Last scalar event column */
	int last_scalar_key_col;            /* Last scalar key start column */
	GTEXT_YAML_Node *last_scalar_node;  /* Last scalar node seen */
	/* True when the last scalar's tag was written on an earlier line than the
	 * scalar itself.  A tag applies to the node that follows it, and in block
	 * context that node may turn out to be the collection this scalar begins
	 * rather than the scalar: "!custom" then "a: 1" tags the mapping, while
	 * "!custom a: 1" tags the key.  The events are otherwise identical. */
	bool last_scalar_tag_own_line;
	bool last_scalar_in_root;           /* True if last scalar stored in root */
	bool last_scalar_in_temp;           /* True if last scalar stored in temp */
	size_t last_scalar_temp_depth;      /* Stack depth when scalar added to temp */
	bool explicit_key_pending;          /* True if '?' indicator seen and key is pending */
	bool explicit_key_active;           /* True if explicit key stored and awaiting ':' */
	int explicit_key_indent;            /* Indent column for explicit key */
	size_t explicit_key_depth;          /* Stack depth for explicit key mapping */
	char *pending_leading_comment;      /* Pending leading comment (malloc'd) */
	GTEXT_YAML_Node *last_emitted_node; /* Last node created for inline comments */
	int last_emitted_line;              /* Line for last_emitted_node */
} parser_state;

/* Stack states */
#define STATE_SEQUENCE 0
#define STATE_MAPPING_KEY 1
#define STATE_MAPPING_VALUE 2

/**
 * @brief Initialize parser state.
 */
static bool parser_init(parser_state *p, yaml_context *ctx, GTEXT_YAML_Error *error) {
	memset(p, 0, sizeof(*p));
	p->ctx = ctx;
	p->error = error;
	
	/* Allocate initial stack capacity */
	p->stack.capacity = 32;
	p->stack.nodes = (GTEXT_YAML_Node **)malloc(p->stack.capacity * sizeof(GTEXT_YAML_Node *));
	p->stack.states = (int *)malloc(p->stack.capacity * sizeof(int));
	p->stack.indents = (int *)malloc(p->stack.capacity * sizeof(int));
	p->stack.is_block = (bool *)malloc(p->stack.capacity * sizeof(bool));
	p->stack.flow_flags = (unsigned char *)calloc(p->stack.capacity, 1);
	p->stack.temps = (saved_temp *)calloc(p->stack.capacity, sizeof(saved_temp));
	
	if (!p->stack.nodes || !p->stack.states || !p->stack.indents ||
		!p->stack.is_block || !p->stack.flow_flags || !p->stack.temps) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.indents);
		free(p->stack.is_block);
		free(p->stack.flow_flags);
		free(p->stack.temps);
		return false;
	}
	
	/* Allocate temporary storage for collection children */
	p->temp.capacity = 16;
	p->temp.items = (GTEXT_YAML_Node **)malloc(p->temp.capacity * sizeof(GTEXT_YAML_Node *));
	if (!p->temp.items) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.indents);
		free(p->stack.is_block);
		free(p->stack.flow_flags);
		free(p->stack.temps);
		return false;
	}
	
	/* Allocate anchor map */
	p->anchors.capacity = 16;
	p->anchors.entries = (anchor_entry *)calloc(p->anchors.capacity, sizeof(anchor_entry));
	if (!p->anchors.entries) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.indents);
		free(p->stack.is_block);
		free(p->stack.flow_flags);
		free(p->stack.temps);
		free(p->temp.items);
		return false;
	}
	
	/* Allocate alias list */
	p->aliases.capacity = 16;
	p->aliases.nodes = (GTEXT_YAML_Node **)malloc(p->aliases.capacity * sizeof(GTEXT_YAML_Node *));
	if (!p->aliases.nodes) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.indents);
		free(p->stack.is_block);
		free(p->stack.flow_flags);
		free(p->stack.temps);
		free(p->temp.items);
		free(p->anchors.entries);
		return false;
	}

	/* Tag handles start empty */
	p->tag_handles.entries = NULL;
	p->tag_handles.count = 0;
	p->tag_handles.capacity = 0;

	p->last_event_line = -1;
	p->last_scalar_line = -1;
	p->last_scalar_col = -1;
	p->last_scalar_key_col = -1;
	p->last_scalar_node = NULL;
	p->last_scalar_tag_own_line = false;
	p->pending_leading_comment = NULL;
	p->last_emitted_node = NULL;
	p->last_emitted_line = -1;
	p->last_scalar_in_root = false;
	p->last_scalar_in_temp = false;
	p->last_scalar_temp_depth = 0;
	p->explicit_key_pending = false;
	p->explicit_key_active = false;
	p->explicit_key_indent = -1;
	p->explicit_key_depth = 0;
	
	return true;
}

/**
 * @brief Free parser state.
 */
static void parser_free(parser_state *p) {
	/* Free saved temp arrays and metadata */
	for (size_t i = 0; i < p->stack.depth; i++) {
		free(p->stack.temps[i].items);
		free(p->stack.temps[i].anchor);
		free(p->stack.temps[i].tag);
	}
	/* Also check capacity range for any lingering allocated metadata */
	for (size_t i = p->stack.depth; i < p->stack.capacity; i++) {
		free(p->stack.temps[i].anchor);
		free(p->stack.temps[i].tag);
	}
	free(p->stack.nodes);
	free(p->stack.states);
	free(p->stack.indents);
	free(p->stack.is_block);
	free(p->stack.flow_flags);
	free(p->stack.temps);
	free(p->temp.items);
	
	/* Free anchor map */
	for (size_t i = 0; i < p->anchors.count; i++) {
		free(p->anchors.entries[i].name);
	}
	free(p->anchors.entries);
	
	/* Free alias list */
	free(p->aliases.nodes);

	free(p->pending_leading_comment);

	/* Free tag handles */
	for (size_t i = 0; i < p->tag_handles.count; i++) {
		free(p->tag_handles.entries[i].handle);
		free(p->tag_handles.entries[i].prefix);
	}
	free(p->tag_handles.entries);
}

/* Forward declarations */
static GTEXT_YAML_Node *lookup_anchor(parser_state *p, const char *name);

static const char *parser_arena_strdup(
	yaml_context *ctx,
	const char *str,
	size_t len
) {
	if (!ctx || !str) return NULL;
	char *copy = (char *)yaml_context_alloc(ctx, len + 1, 1);
	if (!copy) return NULL;
	memcpy(copy, str, len);
	copy[len] = '\0';
	return copy;
}

static void parser_attach_leading_comment(parser_state *p, GTEXT_YAML_Node *node) {
	if (!p || !node || !p->pending_leading_comment || !p->ctx) return;
	const char *stored = parser_arena_strdup(
		p->ctx,
		p->pending_leading_comment,
		strlen(p->pending_leading_comment)
	);
	if (!stored) return;
	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			node->as.scalar.leading_comment = stored;
			break;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			node->as.sequence.leading_comment = stored;
			break;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			node->as.mapping.leading_comment = stored;
			break;
		case GTEXT_YAML_ALIAS:
			node->as.alias.leading_comment = stored;
			break;
		default:
			break;
	}
	free(p->pending_leading_comment);
	p->pending_leading_comment = NULL;
}

static void parser_attach_inline_comment(
	parser_state *p,
	GTEXT_YAML_Node *node,
	const char *comment
) {
	if (!p || !node || !comment || !p->ctx) return;
	const char *stored = parser_arena_strdup(p->ctx, comment, strlen(comment));
	if (!stored) return;
	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			node->as.scalar.inline_comment = stored;
			break;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			node->as.sequence.inline_comment = stored;
			break;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			node->as.mapping.inline_comment = stored;
			break;
		case GTEXT_YAML_ALIAS:
			node->as.alias.inline_comment = stored;
			break;
		default:
			break;
	}
}

static bool json_fastpath_candidate(const char *input, size_t length) {
	size_t i = 0;
	size_t depth = 0;
	bool in_string = false;
	bool escape = false;
	struct {
		int type;
		bool expect_key;
	} stack[64];
	const int ctx_object = 1;
	const int ctx_array = 2;

	if (!input || length == 0) return false;

	if (length >= 4) {
		unsigned char b0 = (unsigned char)input[0];
		unsigned char b1 = (unsigned char)input[1];
		unsigned char b2 = (unsigned char)input[2];
		unsigned char b3 = (unsigned char)input[3];
		if ((b0 == 0x00 && b1 == 0x00 && b2 == 0xFE && b3 == 0xFF) ||
			(b0 == 0xFF && b1 == 0xFE && b2 == 0x00 && b3 == 0x00)) {
			return false;
		}
	}

	if (length >= 2) {
		unsigned char b0 = (unsigned char)input[0];
		unsigned char b1 = (unsigned char)input[1];
		if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
			return false;
		}
	}

	if (length >= 3 &&
		(unsigned char)input[0] == 0xEF &&
		(unsigned char)input[1] == 0xBB &&
		(unsigned char)input[2] == 0xBF) {
		i = 3;
	}

	while (i < length) {
		char ch = input[i];
		if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
			break;
		}
		i++;
	}

	if (i >= length) return false;
	if (input[i] != '{' && input[i] != '[') return false;

	for (size_t j = i; j < length; j++) {
		char ch = input[j];
		if (in_string) {
			if (escape) {
				escape = false;
				continue;
			}
			if (ch == '\\') {
				escape = true;
				continue;
			}
			if (ch == '"') {
				in_string = false;
			}
			continue;
		}

		if (ch == '"') {
			in_string = true;
			if (depth > 0 && stack[depth - 1].type == ctx_object &&
				stack[depth - 1].expect_key) {
				stack[depth - 1].expect_key = false;
			}
			continue;
		}

		if (ch == '#') return false;
		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;

		if (ch == '{') {
			if (depth >= sizeof(stack) / sizeof(stack[0])) return false;
			stack[depth].type = ctx_object;
			stack[depth].expect_key = true;
			depth++;
			continue;
		}

		if (ch == '[') {
			if (depth >= sizeof(stack) / sizeof(stack[0])) return false;
			stack[depth].type = ctx_array;
			stack[depth].expect_key = false;
			depth++;
			continue;
		}

		if (ch == '}') {
			if (depth == 0 || stack[depth - 1].type != ctx_object) return false;
			depth--;
			continue;
		}

		if (ch == ']') {
			if (depth == 0 || stack[depth - 1].type != ctx_array) return false;
			depth--;
			continue;
		}

		if (ch == ',') {
			if (depth > 0 && stack[depth - 1].type == ctx_object) {
				stack[depth - 1].expect_key = true;
			}
			continue;
		}

		if (ch == ':') {
			if (depth > 0 && stack[depth - 1].type == ctx_object) {
				stack[depth - 1].expect_key = false;
			}
			continue;
		}

		if (depth > 0 && stack[depth - 1].type == ctx_object &&
			stack[depth - 1].expect_key) {
			return false;
		}
	}

	return true;
}

static GTEXT_JSON_Dupkey_Mode json_dupkey_mode(GTEXT_YAML_Dupkey_Mode mode) {
	switch (mode) {
		case GTEXT_YAML_DUPKEY_FIRST_WINS:
			return GTEXT_JSON_DUPKEY_FIRST_WINS;
		case GTEXT_YAML_DUPKEY_LAST_WINS:
			return GTEXT_JSON_DUPKEY_LAST_WINS;
		case GTEXT_YAML_DUPKEY_ERROR:
		default:
			return GTEXT_JSON_DUPKEY_ERROR;
	}
}

static GTEXT_JSON_Parse_Options json_parse_options_from_yaml(
	const GTEXT_YAML_Parse_Options *opts
) {
	GTEXT_JSON_Parse_Options json_opts = gtext_json_parse_options_default();

	if (!opts) return json_opts;

	json_opts.dupkeys = json_dupkey_mode(opts->dupkeys);
	json_opts.validate_utf8 = opts->validate_utf8;
	if (opts->max_depth > 0) json_opts.max_depth = opts->max_depth;
	if (opts->max_total_bytes > 0) json_opts.max_total_bytes = opts->max_total_bytes;

	return json_opts;
}

static void map_json_error(const GTEXT_JSON_Error *json_err, GTEXT_YAML_Error *yaml_err) {
	if (!json_err || !yaml_err) return;

	switch (json_err->code) {
		case GTEXT_JSON_E_OOM:
			yaml_err->code = GTEXT_YAML_E_OOM;
			break;
		case GTEXT_JSON_E_LIMIT:
			yaml_err->code = GTEXT_YAML_E_LIMIT;
			break;
		case GTEXT_JSON_E_DEPTH:
			yaml_err->code = GTEXT_YAML_E_DEPTH;
			break;
		case GTEXT_JSON_E_INCOMPLETE:
			yaml_err->code = GTEXT_YAML_E_INCOMPLETE;
			break;
		case GTEXT_JSON_E_BAD_TOKEN:
			yaml_err->code = GTEXT_YAML_E_BAD_TOKEN;
			break;
		case GTEXT_JSON_E_BAD_ESCAPE:
			yaml_err->code = GTEXT_YAML_E_BAD_ESCAPE;
			break;
		default:
			yaml_err->code = GTEXT_YAML_E_INVALID;
			break;
	}

	yaml_err->message = json_err->message;
	yaml_err->offset = json_err->offset;
	yaml_err->line = json_err->line;
	yaml_err->col = json_err->col;
	yaml_err->context_snippet = NULL;
	yaml_err->context_snippet_len = 0;
	yaml_err->caret_offset = 0;
	yaml_err->expected_token = NULL;
	yaml_err->actual_token = NULL;
}

static GTEXT_YAML_Node *json_to_yaml_node(
	yaml_context *ctx,
	const GTEXT_JSON_Value *json,
	GTEXT_YAML_Error *error
) {
	GTEXT_JSON_Type type = GTEXT_JSON_NULL;
	GTEXT_YAML_Node *node = NULL;

	if (!json) {
		if (error) {
			error->code = GTEXT_YAML_E_INVALID;
			error->message = "Missing JSON value";
		}
		return NULL;
	}

	type = gtext_json_typeof(json);

	switch (type) {
		case GTEXT_JSON_NULL:
			return yaml_node_new_scalar(ctx, "null", 4, NULL, NULL);
		case GTEXT_JSON_BOOL: {
			bool value = false;
			if (gtext_json_get_bool(json, &value) != GTEXT_JSON_OK) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid JSON boolean value";
				}
				return NULL;
			}
			return yaml_node_new_scalar(ctx, value ? "true" : "false", value ? 4 : 5, NULL, NULL);
		}
		case GTEXT_JSON_NUMBER: {
			const char *lexeme = NULL;
			size_t lexeme_len = 0;
			if (gtext_json_get_number_lexeme(json, &lexeme, &lexeme_len) != GTEXT_JSON_OK || !lexeme) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid JSON number value";
				}
				return NULL;
			}
			return yaml_node_new_scalar(ctx, lexeme, lexeme_len, NULL, NULL);
		}
		case GTEXT_JSON_STRING: {
			const char *value = NULL;
			size_t value_len = 0;
			if (gtext_json_get_string(json, &value, &value_len) != GTEXT_JSON_OK || !value) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid JSON string value";
				}
				return NULL;
			}
			return yaml_node_new_scalar(ctx, value, value_len, NULL, NULL);
		}
		case GTEXT_JSON_ARRAY: {
			size_t count = gtext_json_array_size(json);
			node = yaml_node_new_sequence(ctx, count, NULL, NULL);
			if (!node) return NULL;
			node->as.sequence.count = count;
			for (size_t i = 0; i < count; i++) {
				const GTEXT_JSON_Value *child = gtext_json_array_get(json, i);
				node->as.sequence.children[i] = json_to_yaml_node(ctx, child, error);
				if (!node->as.sequence.children[i]) return NULL;
			}
			return node;
		}
		case GTEXT_JSON_OBJECT: {
			size_t count = gtext_json_object_size(json);
			node = yaml_node_new_mapping(ctx, count, NULL, NULL);
			if (!node) return NULL;
			node->as.mapping.count = count;
			for (size_t i = 0; i < count; i++) {
				size_t key_len = 0;
				const char *key = gtext_json_object_key(json, i, &key_len);
				const GTEXT_JSON_Value *value = gtext_json_object_value(json, i);
				if (!key || !value) {
					if (error) {
						error->code = GTEXT_YAML_E_INVALID;
						error->message = "Invalid JSON object member";
					}
					return NULL;
				}
				node->as.mapping.pairs[i].key = yaml_node_new_scalar(ctx, key, key_len, NULL, NULL);
				if (!node->as.mapping.pairs[i].key) return NULL;
				node->as.mapping.pairs[i].value = json_to_yaml_node(ctx, value, error);
				if (!node->as.mapping.pairs[i].value) return NULL;
				node->as.mapping.pairs[i].key_tag = NULL;
				node->as.mapping.pairs[i].value_tag = NULL;
			}
			return node;
		}
		default:
			break;
	}

	if (error) {
		error->code = GTEXT_YAML_E_INVALID;
		error->message = "Unsupported JSON value type";
	}
	return NULL;
}

static GTEXT_YAML_Document *yaml_parse_json_document_internal(
	const char *input,
	size_t length,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error,
	bool report_errors
) {
	GTEXT_YAML_Parse_Options effective_opts =
		gtext_yaml_parse_options_effective(options);
	const GTEXT_YAML_Parse_Options *opts = &effective_opts;
	GTEXT_JSON_Parse_Options json_opts = json_parse_options_from_yaml(opts);
	GTEXT_JSON_Error json_err = {0};
	GTEXT_JSON_Value *json_root = gtext_json_parse(input, length, &json_opts, &json_err);
	GTEXT_YAML_Document *doc = NULL;
	yaml_context *ctx = NULL;
	GTEXT_YAML_Node *root = NULL;
	GTEXT_YAML_Status status = GTEXT_YAML_OK;

	if (!json_root) {
		if (report_errors && error) {
			map_json_error(&json_err, error);
		}
		gtext_json_error_free(&json_err);
		return NULL;
	}
	gtext_json_error_free(&json_err);

	ctx = yaml_context_new();
	if (!ctx) {
		gtext_json_free(json_root);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating context";
		}
		return NULL;
	}

	yaml_context_set_input_buffer(ctx, input, length);

	doc = (GTEXT_YAML_Document *)yaml_context_alloc(ctx, sizeof(GTEXT_YAML_Document), 8);
	if (!doc) {
		yaml_context_free(ctx);
		gtext_json_free(json_root);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating document";
		}
		return NULL;
	}

	memset(doc, 0, sizeof(*doc));
	doc->ctx = ctx;
	doc->options = *opts;
	doc->document_index = 0;

	root = json_to_yaml_node(ctx, json_root, error);
	gtext_json_free(json_root);
	if (!root) {
		yaml_context_free(ctx);
		return NULL;
	}

	doc->root = root;
	doc->node_count = 1;

	status = yaml_resolve_document(doc, error);
	if (status != GTEXT_YAML_OK) {
		yaml_context_free(ctx);
		return NULL;
	}

	return doc;
}

/**
 * @brief Register an anchor name with its node.
 */
static GTEXT_YAML_Status register_anchor(parser_state *p, const char *name, GTEXT_YAML_Node *node) {
	if (!name || !node) return GTEXT_YAML_OK;  /* No anchor to register */

	if (lookup_anchor(p, name)) {
		p->failed = true;
		if (p->error) {
			p->error->code = GTEXT_YAML_E_INVALID;
			p->error->message = "Duplicate anchor name";
		}
		return GTEXT_YAML_E_INVALID;
	}

	/* Check if we need to grow the anchor map */
	if (p->anchors.count >= p->anchors.capacity) {
		size_t new_cap = p->anchors.capacity * 2;
		anchor_entry *new_entries = (anchor_entry *)realloc(
			p->anchors.entries, new_cap * sizeof(anchor_entry)
		);
		if (!new_entries) {
			p->failed = true;
			if (p->error) {
				p->error->code = GTEXT_YAML_E_OOM;
				p->error->message = "Out of memory registering anchor";
			}
			return GTEXT_YAML_E_OOM;
		}
		memset(new_entries + p->anchors.capacity, 0, (new_cap - p->anchors.capacity) * sizeof(anchor_entry));
		p->anchors.entries = new_entries;
		p->anchors.capacity = new_cap;
	}

	/* Add the anchor */
	p->anchors.entries[p->anchors.count].name = strdup(name);
	p->anchors.entries[p->anchors.count].node = node;
	if (!p->anchors.entries[p->anchors.count].name) {
		p->failed = true;
		if (p->error) {
			p->error->code = GTEXT_YAML_E_OOM;
			p->error->message = "Out of memory registering anchor";
		}
		return GTEXT_YAML_E_OOM;
	}
	p->anchors.count++;

	return GTEXT_YAML_OK;
}

/**
 * @brief Track an alias node for later resolution.
 */
static bool track_alias(parser_state *p, GTEXT_YAML_Node *alias_node) {
	if (!alias_node) return true;
	
	/* Check if we need to grow the alias list */
	if (p->aliases.count >= p->aliases.capacity) {
		size_t new_cap = p->aliases.capacity * 2;
		GTEXT_YAML_Node **new_nodes = (GTEXT_YAML_Node **)realloc(
			p->aliases.nodes, new_cap * sizeof(GTEXT_YAML_Node *)
		);
		if (!new_nodes) return false;
		p->aliases.nodes = new_nodes;
		p->aliases.capacity = new_cap;
	}
	
	p->aliases.nodes[p->aliases.count++] = alias_node;
	return true;
}

/**
 * @brief Look up an anchor by name.
 */
static GTEXT_YAML_Node *lookup_anchor(parser_state *p, const char *name) {
	if (!name) return NULL;
	
	for (size_t i = 0; i < p->anchors.count; i++) {
		if (strcmp(p->anchors.entries[i].name, name) == 0) {
			return p->anchors.entries[i].node;
		}
	}
	
	return NULL;
}

static const char *context_strdup(yaml_context *ctx, const char *value) {
	if (!ctx || !value) return NULL;
	size_t len = strlen(value);
	char *copy = (char *)yaml_context_alloc(ctx, len + 1, 1);
	if (!copy) return NULL;
	memcpy(copy, value, len);
	copy[len] = '\0';
	return copy;
}

static bool tag_handle_add(parser_state *p, const char *handle, const char *prefix) {
	if (!p || !handle || !prefix) return false;

	for (size_t i = 0; i < p->tag_handles.count; i++) {
		if (strcmp(p->tag_handles.entries[i].handle, handle) == 0) {
			char *new_prefix = strdup(prefix);
			if (!new_prefix) return false;
			free(p->tag_handles.entries[i].prefix);
			p->tag_handles.entries[i].prefix = new_prefix;
			return true;
		}
	}

	if (p->tag_handles.count >= p->tag_handles.capacity) {
		size_t new_cap = p->tag_handles.capacity == 0 ? 8 : p->tag_handles.capacity * 2;
		tag_handle_entry *new_entries = (tag_handle_entry *)realloc(
			p->tag_handles.entries, new_cap * sizeof(tag_handle_entry)
		);
		if (!new_entries) return false;
		p->tag_handles.entries = new_entries;
		p->tag_handles.capacity = new_cap;
	}

	p->tag_handles.entries[p->tag_handles.count].handle = strdup(handle);
	p->tag_handles.entries[p->tag_handles.count].prefix = strdup(prefix);
	if (!p->tag_handles.entries[p->tag_handles.count].handle ||
		!p->tag_handles.entries[p->tag_handles.count].prefix) {
		free(p->tag_handles.entries[p->tag_handles.count].handle);
		free(p->tag_handles.entries[p->tag_handles.count].prefix);
		return false;
	}
	p->tag_handles.count++;
	return true;
}

/**
 * @brief Resolve all alias nodes.
 */
static GTEXT_YAML_Status resolve_aliases(parser_state *p) {
	if (!p || !p->doc) return GTEXT_YAML_E_INVALID;
	size_t max_aliases = p->doc->options.max_alias_expansion;
	size_t alias_count = 0;

	for (size_t i = 0; i < p->aliases.count; i++) {
		GTEXT_YAML_Node *alias = p->aliases.nodes[i];
		if (alias->type != GTEXT_YAML_ALIAS) continue;  /* Shouldn't happen */

		if (max_aliases > 0 && alias_count + 1 > max_aliases) {
			p->failed = true;
			if (p->error) {
				p->error->code = GTEXT_YAML_E_LIMIT;
				p->error->message = "Alias expansion limit exceeded";
			}
			return GTEXT_YAML_E_LIMIT;
		}
		alias_count++;
		
		const char *anchor_name = alias->as.alias.anchor_name;
		GTEXT_YAML_Node *target = lookup_anchor(p, anchor_name);
		
		if (!target) {
			/* Unknown anchor */
			p->failed = true;
			if (p->error) {
				p->error->code = GTEXT_YAML_E_INVALID;
				p->error->message = "Unknown anchor referenced by alias";
			}
			return GTEXT_YAML_E_INVALID;
		}
		
		alias->as.alias.target = target;
	}
	
	return GTEXT_YAML_OK;
}

static bool finalize_tag_handles(parser_state *p, GTEXT_YAML_Document *doc) {
	if (!p || !doc) return false;
	if (p->tag_handles.count == 0) return true;

	yaml_tag_handle *handles = (yaml_tag_handle *)yaml_context_alloc(
		doc->ctx, sizeof(yaml_tag_handle) * p->tag_handles.count, 8
	);
	if (!handles) return false;

	for (size_t i = 0; i < p->tag_handles.count; i++) {
		handles[i].handle = context_strdup(doc->ctx, p->tag_handles.entries[i].handle);
		handles[i].prefix = context_strdup(doc->ctx, p->tag_handles.entries[i].prefix);
		if (!handles[i].handle || !handles[i].prefix) return false;
	}

	doc->tag_handles = handles;
	doc->tag_handle_count = p->tag_handles.count;
	return true;
}

/**
 * @brief Push a node onto the stack (for tracking nesting).
 * Saves the current temp state and clears temp for the new level.
 */
static bool stack_push(
	parser_state *p,
	GTEXT_YAML_Node *node,
	int state,
	const char *anchor,
	const char *tag,
	int indent,
	bool is_block,
	size_t source_offset,
	int source_line,
	int source_col
) {
	if (p->stack.depth >= p->stack.capacity) {
		/* Grow stack */
		size_t new_cap = p->stack.capacity * 2;
		GTEXT_YAML_Node **new_nodes = (GTEXT_YAML_Node **)realloc(
			p->stack.nodes, new_cap * sizeof(GTEXT_YAML_Node *)
		);
		int *new_states = (int *)realloc(p->stack.states, new_cap * sizeof(int));
		int *new_indents = (int *)realloc(p->stack.indents, new_cap * sizeof(int));
		bool *new_is_block = (bool *)realloc(p->stack.is_block, new_cap * sizeof(bool));
		unsigned char *new_flow_flags = (unsigned char *)realloc(p->stack.flow_flags, new_cap);
		saved_temp *new_temps = (saved_temp *)realloc(p->stack.temps, new_cap * sizeof(saved_temp));
		
		if (!new_nodes || !new_states || !new_indents || !new_is_block ||
			!new_flow_flags || !new_temps) {
			free(new_nodes);
			free(new_states);
			free(new_indents);
			free(new_is_block);
			free(new_flow_flags);
			free(new_temps);
			return false;
		}
		
		/* Zero-initialize new slots */
		memset(new_temps + p->stack.capacity, 0, (new_cap - p->stack.capacity) * sizeof(saved_temp));
		memset(new_flow_flags + p->stack.capacity, 0, new_cap - p->stack.capacity);
		
		p->stack.nodes = new_nodes;
		p->stack.states = new_states;
		p->stack.indents = new_indents;
		p->stack.is_block = new_is_block;
		p->stack.flow_flags = new_flow_flags;
		p->stack.temps = new_temps;
		p->stack.capacity = new_cap;
	}
	
	/* Save current temp state to the stack */
	p->stack.temps[p->stack.depth].items = p->temp.items;
	p->stack.temps[p->stack.depth].count = p->temp.count;
	p->stack.temps[p->stack.depth].capacity = p->temp.capacity;
	p->stack.temps[p->stack.depth].anchor = anchor ? strdup(anchor) : NULL;
	p->stack.temps[p->stack.depth].tag = tag ? strdup(tag) : NULL;
	p->stack.temps[p->stack.depth].source_offset = source_offset;
	p->stack.temps[p->stack.depth].source_line = source_line;
	p->stack.temps[p->stack.depth].source_col = source_col;
	
	/* Allocate new temp storage for this level */
	p->temp.capacity = 16;
	p->temp.items = (GTEXT_YAML_Node **)malloc(p->temp.capacity * sizeof(GTEXT_YAML_Node *));
	if (!p->temp.items) {
		/* Restore old temp on failure */
		p->temp.items = p->stack.temps[p->stack.depth].items;
		p->temp.count = p->stack.temps[p->stack.depth].count;
		p->temp.capacity = p->stack.temps[p->stack.depth].capacity;
		free(p->stack.temps[p->stack.depth].anchor);
		free(p->stack.temps[p->stack.depth].tag);
		p->stack.temps[p->stack.depth].anchor = NULL;
		p->stack.temps[p->stack.depth].tag = NULL;
		return false;
	}
	p->temp.count = 0;
	
	p->stack.nodes[p->stack.depth] = node;
	p->stack.states[p->stack.depth] = state;
	p->stack.indents[p->stack.depth] = indent;
	p->stack.is_block[p->stack.depth] = is_block;
	p->stack.flow_flags[p->stack.depth] = 0;
	p->stack.depth++;
	return true;
}

/**
 * @brief Pop a node from the stack.
 * Restores the saved temp state from the previous level.
 */
static void stack_pop(parser_state *p) {
	if (p->stack.depth > 0) {
		p->stack.depth--;
		
		/* Free current temp and restore saved temp state */
		free(p->temp.items);
		p->temp.items = p->stack.temps[p->stack.depth].items;
		p->temp.count = p->stack.temps[p->stack.depth].count;
		p->temp.capacity = p->stack.temps[p->stack.depth].capacity;
		
		/* Note: Don't free anchor/tag here - they're still needed for node creation */
		/* They'll be freed after creating the node in the event handler */
	}
}

/**
 * @brief Pop and get the saved anchor/tag, then clear them.
 */
static void stack_get_and_clear_metadata(
	parser_state *p,
	char **anchor,
	char **tag,
	size_t *source_offset,
	int *source_line,
	int *source_col
) {
	if (p->stack.depth > 0 && p->stack.depth <= p->stack.capacity) {
		size_t idx = p->stack.depth - 1;
		*anchor = p->stack.temps[idx].anchor;
		*tag = p->stack.temps[idx].tag;
		if (source_offset) *source_offset = p->stack.temps[idx].source_offset;
		if (source_line) *source_line = p->stack.temps[idx].source_line;
		if (source_col) *source_col = p->stack.temps[idx].source_col;
		p->stack.temps[idx].anchor = NULL;
		p->stack.temps[idx].tag = NULL;
		p->stack.temps[idx].source_offset = 0;
		p->stack.temps[idx].source_line = 0;
		p->stack.temps[idx].source_col = 0;
	} else {
		*anchor = NULL;
		*tag = NULL;
		if (source_offset) *source_offset = 0;
		if (source_line) *source_line = 0;
		if (source_col) *source_col = 0;
	}
}

static void node_set_source_location(
	GTEXT_YAML_Node *node,
	size_t offset,
	int line,
	int col
) {
	if (!node) return;
	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			node->as.scalar.source_offset = offset;
			node->as.scalar.source_line = line;
			node->as.scalar.source_col = col;
			return;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			node->as.sequence.source_offset = offset;
			node->as.sequence.source_line = line;
			node->as.sequence.source_col = col;
			return;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			node->as.mapping.source_offset = offset;
			node->as.mapping.source_line = line;
			node->as.mapping.source_col = col;
			return;
		case GTEXT_YAML_ALIAS:
			node->as.alias.source_offset = offset;
			node->as.alias.source_line = line;
			node->as.alias.source_col = col;
			return;
		default:
			return;
	}
}

static void node_get_source_location(
	const GTEXT_YAML_Node *node,
	size_t *offset,
	int *line,
	int *col
) {
	if (offset) *offset = 0;
	if (line) *line = 0;
	if (col) *col = 0;
	if (!node) return;
	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			if (offset) *offset = node->as.scalar.source_offset;
			if (line) *line = node->as.scalar.source_line;
			if (col) *col = node->as.scalar.source_col;
			return;
		case GTEXT_YAML_SEQUENCE:
		case GTEXT_YAML_OMAP:
		case GTEXT_YAML_PAIRS:
			if (offset) *offset = node->as.sequence.source_offset;
			if (line) *line = node->as.sequence.source_line;
			if (col) *col = node->as.sequence.source_col;
			return;
		case GTEXT_YAML_MAPPING:
		case GTEXT_YAML_SET:
			if (offset) *offset = node->as.mapping.source_offset;
			if (line) *line = node->as.mapping.source_line;
			if (col) *col = node->as.mapping.source_col;
			return;
		case GTEXT_YAML_ALIAS:
			if (offset) *offset = node->as.alias.source_offset;
			if (line) *line = node->as.alias.source_line;
			if (col) *col = node->as.alias.source_col;
			return;
		default:
			return;
	}
}

/**
 * @brief Add a node to the temporary collection being built.
 */
static bool temp_add(parser_state *p, GTEXT_YAML_Node *node) {
	if (p->temp.count >= p->temp.capacity) {
		/* Grow temp storage */
		size_t new_cap = p->temp.capacity * 2;
		GTEXT_YAML_Node **new_items = (GTEXT_YAML_Node **)realloc(
			p->temp.items, new_cap * sizeof(GTEXT_YAML_Node *)
		);
		if (!new_items) return false;
		
		p->temp.items = new_items;
		p->temp.capacity = new_cap;
	}
	
	p->temp.items[p->temp.count++] = node;
	return true;
}

/**
 * @brief Give a mapping key that has no value an explicit null one.
 *
 * A mapping's children are collected as a flat alternating key, value, key,
 * value list, and the pairs are formed by halving that list.  A key whose
 * value is absent therefore did not produce an empty value - it left the list
 * one short, so the halving dropped the last key and, worse, paired every
 * later key with the wrong value:
 *
 *     a:          gave {}              rather than {a: null}
 *     a:          gave {a: b}, losing  rather than {a: null, b: 1}
 *     b: 1          the 1
 *     ? a         gave {}              rather than {a: null}
 *     ? a         was a parse error    rather than {a: null, b: null}
 *     ? b
 *
 * YAML says an absent value is null, so the fix is to supply one.
 *
 * The supplied node is "~", the plain null scalar, rather than an empty one.
 * The resolver reads a scalar's text and does not look at whether it was
 * quoted, so an empty scalar resolves to the empty *string* - which would
 * make "a:" indistinguishable from "a: ''", and those are different values.
 * "~" resolves to GTEXT_YAML_NULL through the ordinary path and needs no tag,
 * so the result is exactly what writing "a: ~" by hand produces.
 */
static bool mapping_supply_null_value(parser_state *p) {
	GTEXT_YAML_Node *empty = yaml_node_new_scalar(p->ctx, "~", 1, NULL, NULL);
	if (!empty) {
		return false;
	}
	return temp_add(p, empty);
}

/**
 * @brief True when a node starting at @p col is a sibling key rather than the
 *        value of the key already waiting for one.
 *
 * Indentation decides it.  Content indented past the key belongs to the key:
 *
 *     x:            {x: {y: 1}}
 *       y: 1
 *
 * Content at the key's own column is the next key, so the previous one had no
 * value:
 *
 *     a:            {a: null, b: 1}
 *     b: 1
 *
 * Only scalars are asked about.  A block sequence may sit at its key's own
 * column and still be that key's value - "a:" then "- 1" is {a: [1]} - so the
 * '-' indicator must not be treated this way.
 */
static bool block_value_is_missing(parser_state *p, int col) {
	if (p->stack.depth == 0) return false;
	size_t top = p->stack.depth - 1;
	if (p->stack.states[top] != STATE_MAPPING_VALUE) return false;
	if (!p->stack.is_block[top]) return false;
	if (p->stack.indents[top] < 0) return false;
	return col <= p->stack.indents[top];
}


static int line_key_col_from_offset(const parser_state *p, size_t offset) {
	const char *buffer = NULL;
	size_t length = 0;
	size_t line_start = 0;
	size_t end = 0;
	size_t i = 0;
	int col = 1;

	if (!p || !p->ctx || !p->ctx->input_buffer) return -1;

	buffer = p->ctx->input_buffer;
	length = p->ctx->input_buffer_len;
	if (offset > length) offset = length;

	line_start = offset;
	while (line_start > 0) {
		char ch = buffer[line_start - 1];
		if (ch == '\n' || ch == '\r') break;
		line_start--;
	}

	end = offset;
	for (i = line_start; i < end; i++) {
		char ch = buffer[i];
		if (ch == ' ' || ch == '\t') {
			col++;
			continue;
		}
		break;
	}

	if (i < end && buffer[i] == '-') {
		size_t j = i + 1;
		int col_after_dash = col + 1;
		if (j < end && (buffer[j] == ' ' || buffer[j] == '\t')) {
			while (j < end && (buffer[j] == ' ' || buffer[j] == '\t')) {
				j++;
				col_after_dash++;
			}
			return col_after_dash;
		}
	}

	return col;
}

static bool stack_top_is_block(const parser_state *p) {
	if (!p || p->stack.depth == 0) return false;
	return p->stack.is_block[p->stack.depth - 1];
}

static int stack_top_indent(const parser_state *p) {
	if (!p || p->stack.depth == 0) return -1;
	return p->stack.indents[p->stack.depth - 1];
}


/**
 * @brief Move an own-line tag from a scalar onto the collection it begins.
 *
 * A tag applies to the node that follows it.  The scanner can only attach a
 * pending tag to the next scalar, because in block context nothing yet says
 * whether the node starting there is that scalar or a collection whose first
 * key or item it is.  Once the parser has decided, this puts the tag where it
 * belongs.
 *
 * Only own-line tags move.  "!custom a: 1" tags the key and "!custom" on its
 * own line then "a: 1" tags the mapping; the two differ in nothing but where
 * the tag was written, which is why GTEXT_YAML_Event carries tag_line.
 *
 * The collection's tag lives in the stack entry until the node is created at
 * its end, which is the same slot a flow collection's tag arrives in.
 */
static void adopt_own_line_tag(parser_state *p, GTEXT_YAML_Node *node) {
	if (!p || !node || !p->last_scalar_tag_own_line) return;
	if (p->stack.depth == 0) return;
	/* The DOM has no single scalar type: NULL through STRING are the scalar
	 * kinds, and SEQUENCE onwards are collections and aliases. */
	if (node->type >= GTEXT_YAML_SEQUENCE) return;

	const char *tag = node->as.scalar.tag;
	if (!tag) return;

	size_t top = p->stack.depth - 1;
	if (p->stack.temps[top].tag) return; /* the collection already has one */

	char *moved = strdup(tag);
	if (!moved) return; /* leaving the tag on the scalar beats losing it */

	p->stack.temps[top].tag = moved;
	node->as.scalar.tag = NULL;
	p->last_scalar_tag_own_line = false;
}

static void maybe_finish_block_mapping_value(parser_state *p) {
	if (!p || p->stack.depth == 0) return;
	if (!p->stack.is_block[p->stack.depth - 1]) return;
	if (p->stack.states[p->stack.depth - 1] != STATE_MAPPING_VALUE) return;

	p->stack.states[p->stack.depth - 1] = STATE_MAPPING_KEY;
}

/**
 * @brief Make @p node the document root, or refuse a second one.
 *
 * A document has exactly one root node (3.2.1). Every site that finished a
 * node at stack depth zero simply assigned it, so a second top-level node
 * overwrote the first and the first was gone: "- a\n- b\ninvalid: x"
 * returned {"invalid": "x"} with the sequence dropped, and "word1 # comment"
 * over "word2" returned just "word2". Silent data loss rather than a
 * refusal, which is the shape most of the defects in this parser have had.
 *
 * detach_last_scalar() clears the root when a scalar taken provisionally as
 * the root turns out to be a mapping key, so the ordinary "a: 1" path still
 * reaches here with no root set.
 */
static GTEXT_YAML_Status set_document_root(parser_state *p, GTEXT_YAML_Node *node) {
	if (p->root) {
		if (p->error) {
			p->error->code = GTEXT_YAML_E_INVALID;
			p->error->message = "Second top-level node in one document";
		}
		p->failed = true;
		return GTEXT_YAML_E_INVALID;
	}
	p->root = node;
	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Node *detach_last_scalar(parser_state *p) {
	GTEXT_YAML_Node *node = p->last_scalar_node;

	if (!node) return NULL;

	if (p->last_scalar_in_root) {
		p->root = NULL;
		p->last_scalar_node = NULL;
		return node;
	}

	if (!p->last_scalar_in_temp) return NULL;
	if (p->last_scalar_temp_depth != p->stack.depth) return NULL;
	if (p->temp.count == 0) return NULL;
	if (p->temp.items[p->temp.count - 1] != node) return NULL;

	p->temp.count--;
	p->last_scalar_node = NULL;
	return node;
}

static GTEXT_YAML_Status capture_explicit_key(
	parser_state *p,
	GTEXT_YAML_Node *node,
	bool *handled
) {
	if (!handled) return GTEXT_YAML_E_INVALID;
	*handled = false;

	if (!p || !node) return GTEXT_YAML_E_INVALID;
	if (!p->explicit_key_pending) return GTEXT_YAML_OK;
	if (p->stack.depth != p->explicit_key_depth) return GTEXT_YAML_OK;
	if (p->stack.depth == 0) {
		if (p->error) {
			p->error->code = GTEXT_YAML_E_INVALID;
			p->error->message = "Explicit key missing mapping context";
		}
		return GTEXT_YAML_E_INVALID;
	}

	size_t top = p->stack.depth - 1;
	if (p->stack.states[top] != STATE_MAPPING_KEY &&
		p->stack.states[top] != STATE_MAPPING_VALUE) {
		if (p->error) {
			p->error->code = GTEXT_YAML_E_INVALID;
			p->error->message = "Explicit key used outside mapping";
		}
		return GTEXT_YAML_E_INVALID;
	}

	/* An explicit key creates its mapping in the '?' handler rather than at a
	 * ':', and arrives here rather than through the ordinary scalar path, so
	 * an own-line tag before it needs adopting here too.  Only the first key:
	 * a later one cannot retag a mapping that already exists. */
	const bool first_key = (p->temp.count == 0);

	if (!temp_add(p, node)) {
		if (p->error) {
			p->error->code = GTEXT_YAML_E_OOM;
			p->error->message = "Out of memory adding explicit key";
		}
		return GTEXT_YAML_E_OOM;
	}

	if (first_key) {
		adopt_own_line_tag(p, node);
	}

	p->explicit_key_pending = false;
	p->explicit_key_active = true;
	p->stack.states[top] = STATE_MAPPING_KEY;
	*handled = true;
	return GTEXT_YAML_OK;
}

static void flow_entry_completed(parser_state *p);
static GTEXT_YAML_Status flow_entry_needs_separator(parser_state *p);

static GTEXT_YAML_Status finalize_top_collection(parser_state *p) {
	GTEXT_YAML_Node *node = NULL;
	char *anchor = NULL;
	char *tag = NULL;
	size_t source_offset = 0;
	int source_line = 0;
	int source_col = 0;
	int state = 0;

	if (!p || p->stack.depth == 0) return GTEXT_YAML_OK;

	state = p->stack.states[p->stack.depth - 1];
	stack_get_and_clear_metadata(
		p,
		&anchor,
		&tag,
		&source_offset,
		&source_line,
		&source_col
	);

	if (state == STATE_SEQUENCE) {
		node = yaml_node_new_sequence(p->ctx, p->temp.count, tag, anchor);
		if (!node) {
			free(anchor);
			free(tag);
			if (p->error) {
				p->error->code = GTEXT_YAML_E_OOM;
				p->error->message = "Out of memory creating sequence";
			}
			return GTEXT_YAML_E_OOM;
		}
		
		node->as.sequence.count = p->temp.count;
		for (size_t i = 0; i < p->temp.count; i++) {
			node->as.sequence.children[i] = p->temp.items[i];
		}
	} else {
		/* A trailing key with no value, by the same rule: "a:" at the end of a
		 * document, or "? a" with no ':' after it.  On OOM the key is dropped
		 * as it was before, which is the old behavior rather than a new one. */
		if ((p->temp.count % 2) != 0) {
			(void)mapping_supply_null_value(p);
		}
		size_t pair_count = p->temp.count / 2;
		node = yaml_node_new_mapping(p->ctx, pair_count, tag, anchor);
		if (!node) {
			free(anchor);
			free(tag);
			if (p->error) {
				p->error->code = GTEXT_YAML_E_OOM;
				p->error->message = "Out of memory creating mapping";
			}
			return GTEXT_YAML_E_OOM;
		}
		
		node->as.mapping.count = pair_count;
		for (size_t i = 0; i < pair_count; i++) {
			node->as.mapping.pairs[i].key = p->temp.items[i * 2];
			node->as.mapping.pairs[i].value = p->temp.items[i * 2 + 1];
			node->as.mapping.pairs[i].key_tag = NULL;
			node->as.mapping.pairs[i].value_tag = NULL;
		}
	}

	free(anchor);
	free(tag);

	node_set_source_location(node, source_offset, source_line, source_col);

	if (node->type == GTEXT_YAML_SEQUENCE && node->as.sequence.anchor) {
		GTEXT_YAML_Status anchor_status = register_anchor(p, node->as.sequence.anchor, node);
		if (anchor_status != GTEXT_YAML_OK) {
			return anchor_status;
		}
	}
	if (node->type == GTEXT_YAML_MAPPING && node->as.mapping.anchor) {
		GTEXT_YAML_Status anchor_status = register_anchor(p, node->as.mapping.anchor, node);
		if (anchor_status != GTEXT_YAML_OK) {
			return anchor_status;
		}
	}

	stack_pop(p);

	bool explicit_handled = false;
	GTEXT_YAML_Status explicit_status = capture_explicit_key(p, node, &explicit_handled);
	if (explicit_status != GTEXT_YAML_OK) {
		return explicit_status;
	}
	if (explicit_handled) {
		return GTEXT_YAML_OK;
	}

	if (p->stack.depth == 0) {
		GTEXT_YAML_Status root_status = set_document_root(p, node);
		if (root_status != GTEXT_YAML_OK) return root_status;
	} else {
		if (!temp_add(p, node)) {
			if (p->error) {
				p->error->code = GTEXT_YAML_E_OOM;
				p->error->message = "Out of memory nesting collection";
			}
			return GTEXT_YAML_E_OOM;
		}
		flow_entry_completed(p);
		maybe_finish_block_mapping_value(p);
	}

	return GTEXT_YAML_OK;
}

/**
 * @brief Note that a flow collection now holds a complete entry.
 *
 * The next one needs a "," in front of it.  Within a line two words are one
 * plain scalar, so this only ever fires across a line break: "[a" over "b]"
 * is two entries with nothing between them, which is not a flow sequence.
 */
static void flow_entry_completed(parser_state *p) {
	if (!p || p->stack.depth == 0) return;
	const size_t top = p->stack.depth - 1;
	if (p->stack.is_block[top]) return;
	if (p->stack.states[top] == STATE_SEQUENCE) {
		p->stack.flow_flags[top] |= GTEXT_YAML_FLOW_ITEM_DONE;
		return;
	}
	if (p->stack.states[top] == STATE_MAPPING_KEY ||
		p->stack.states[top] == STATE_MAPPING_VALUE) {
		/* A flow mapping's entry is a whole pair, so it is complete only when
		 * the value has landed and the alternating list is even again. */
		if (p->temp.count > 0 && (p->temp.count % 2) == 0) {
			p->stack.flow_flags[top] |= GTEXT_YAML_FLOW_ITEM_DONE;
		}
	}
}

/**
 * @brief Refuse a second entry where the first was never separated from it.
 */
static GTEXT_YAML_Status flow_entry_needs_separator(parser_state *p) {
	if (!p || p->stack.depth == 0) return GTEXT_YAML_OK;
	const size_t top = p->stack.depth - 1;
	if (p->stack.is_block[top]) return GTEXT_YAML_OK;
	if (!(p->stack.flow_flags[top] & GTEXT_YAML_FLOW_ITEM_DONE)) return GTEXT_YAML_OK;

	p->failed = true;
	if (p->error) {
		p->error->code = GTEXT_YAML_E_INVALID;
		p->error->message = "Flow collection entries must be separated by ','";
	}
	return GTEXT_YAML_E_INVALID;
}

/**
 * @brief Close a "[a: 1]" single-pair mapping if one is open.
 *
 * Such a pair is opened by a ":" inside a flow sequence and has no "}" to
 * close it, so the "," or "]" that ends the entry closes it instead.  A
 * pair whose value never arrived gets the null it stands for, the same as
 * anywhere else.
 */
static GTEXT_YAML_Status close_flow_pair(parser_state *p) {
	if (!p || p->stack.depth == 0) return GTEXT_YAML_OK;
	if (!(p->stack.flow_flags[p->stack.depth - 1] & GTEXT_YAML_FLOW_PAIR)) return GTEXT_YAML_OK;

	if ((p->temp.count % 2) == 1 && !mapping_supply_null_value(p)) {
		p->failed = true;
		if (p->error) {
			p->error->code = GTEXT_YAML_E_OOM;
			p->error->message = "Out of memory completing flow pair";
		}
		return GTEXT_YAML_E_OOM;
	}
	return finalize_top_collection(p);
}

/**
 * @brief Refuse a flow collection that the input ended inside.
 *
 * close_block_contexts() deliberately stops at a flow collection, because
 * indentation says nothing about where "[" and "{" end.  Nothing else closed
 * them either, so input ending inside one left the stack standing and the
 * root never set - and a document with a NULL root was handed back as a
 * success.  A caller that checked only for a NULL document got an empty one
 * instead of an error.
 */
static GTEXT_YAML_Status check_flow_contexts_closed(parser_state *p) {
	if (!p || p->stack.depth == 0) return GTEXT_YAML_OK;
	if (p->stack.is_block[p->stack.depth - 1]) return GTEXT_YAML_OK;

	p->failed = true;
	if (p->error && p->error->code == GTEXT_YAML_OK) {
		p->error->code = GTEXT_YAML_E_INVALID;
		p->error->message = "Unterminated flow collection";
	}
	return GTEXT_YAML_E_INVALID;
}

/**
 * @brief Close the block collections a line at @p new_indent has left.
 *
 * A collection indented further than the new line has ended.  A block
 * sequence at exactly the new line's indentation has ended too, because YAML
 * lets a sequence sit at the same column as the key that owns it:
 *
 *     a:
 *     - 1
 *     b: 2
 *
 * Here "b" is a key of the mapping that owns "a", not a third entry of the
 * sequence.  Only another entry keeps that sequence open, so @p starts_entry
 * says whether this line begins with a "-" at that column.  A block *mapping*
 * at the same indentation is never closed: that is the mapping the new key
 * belongs to.
 */
static GTEXT_YAML_Status close_block_contexts_for(
	parser_state *p,
	int new_indent,
	bool starts_entry
) {
	if (!p) return GTEXT_YAML_E_INVALID;

	while (p->stack.depth > 0) {
		if (!stack_top_is_block(p)) break;
		const int top_indent = stack_top_indent(p);
		if (new_indent < top_indent) {
			/* left the collection entirely */
		} else if (new_indent == top_indent && !starts_entry &&
			p->stack.states[p->stack.depth - 1] == STATE_SEQUENCE) {
			/* a sequence at the owning key's own column, and this is not
			   another entry of it */
		} else {
			break;
		}

		GTEXT_YAML_Status status = finalize_top_collection(p);
		if (status != GTEXT_YAML_OK) return status;
	}

	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status close_block_contexts(parser_state *p, int new_indent) {
	return close_block_contexts_for(p, new_indent, false);
}

/**
 * @brief Streaming parser callback - builds DOM from events.
 */
static GTEXT_YAML_Status parse_callback(
	GTEXT_YAML_Stream *s,
	const void *event_payload,
	void *user_data
) {
	(void)s;  /* Unused */
	parser_state *p = (parser_state *)user_data;
	if (p->failed) return GTEXT_YAML_E_STATE;
	
	const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)event_payload;
	GTEXT_YAML_Event_Type type = event->type;

	if (event->line >= 0 && event->line != p->last_event_line) {
		const bool starts_entry = (type == GTEXT_YAML_EVENT_INDICATOR
			&& event->data.indicator == '-');
		GTEXT_YAML_Status close_status =
			close_block_contexts_for(p, event->col, starts_entry);
		if (close_status != GTEXT_YAML_OK) return close_status;
		p->last_event_line = event->line;
	}
	
	/* Skip events if first document already complete (multi-doc streams) */
	if (p->first_document_complete) {
		return GTEXT_YAML_OK;
	}
	
	switch (type) {
		case GTEXT_YAML_EVENT_STREAM_START:
			/* Start of stream - nothing to do */
			break;
			
		case GTEXT_YAML_EVENT_DOCUMENT_START:
			/* Start of a document */
			if (!p->document_started) {
				p->document_started = true;
				/* This is the first (or only) document, parse it */
			} else {
				/* We've already started a document, this is a second one */
				/* Stop parsing - we only want the first document */
				p->first_document_complete = true;
			}
			break;

		case GTEXT_YAML_EVENT_DIRECTIVE: {
			const char *name = event->data.directive.name;
			const char *value = event->data.directive.value;
			const char *value2 = event->data.directive.value2;

			if (!name) {
				break;
			}

			p->doc->has_directives = true;
			if (strcmp(name, "YAML") == 0) {
				if (!value) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_INVALID;
						p->error->message = "YAML directive missing version";
					}
					return GTEXT_YAML_E_INVALID;
				}
				char *end = NULL;
				long major = strtol(value, &end, 10);
				if (!end || *end != '.') {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_INVALID;
						p->error->message = "Invalid YAML directive version";
					}
					return GTEXT_YAML_E_INVALID;
				}
				long minor = strtol(end + 1, &end, 10);
				if (!end || *end != '\0') {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_INVALID;
						p->error->message = "Invalid YAML directive version";
					}
					return GTEXT_YAML_E_INVALID;
				}
				p->doc->yaml_version_major = (int)major;
				p->doc->yaml_version_minor = (int)minor;
			} else if (strcmp(name, "TAG") == 0) {
				if (!value || !value2) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_INVALID;
						p->error->message = "TAG directive missing handle or prefix";
					}
					return GTEXT_YAML_E_INVALID;
				}
				if (!tag_handle_add(p, value, value2)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory storing tag handle";
					}
					return GTEXT_YAML_E_OOM;
				}
			}
			break;
		}
		case GTEXT_YAML_EVENT_COMMENT: {
			const char *comment = event->data.comment.ptr;
			bool inline_comment = event->data.comment.inline_comment;
			if (!comment) {
				break;
			}
			if (inline_comment && p->last_emitted_node &&
				p->last_emitted_line == event->line) {
				parser_attach_inline_comment(p, p->last_emitted_node, comment);
			} else {
				size_t existing = p->pending_leading_comment
					? strlen(p->pending_leading_comment)
					: 0;
				size_t add_len = strlen(comment);
				size_t extra = existing > 0 ? 1 : 0;
				char *buf = (char *)malloc(existing + add_len + extra + 1);
				if (!buf) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory storing comment";
					}
					return GTEXT_YAML_E_OOM;
				}
				if (existing > 0) {
					memcpy(buf, p->pending_leading_comment, existing);
					buf[existing] = '\n';
					memcpy(buf + existing + 1, comment, add_len);
					buf[existing + 1 + add_len] = '\0';
					free(p->pending_leading_comment);
				} else {
					memcpy(buf, comment, add_len);
					buf[add_len] = '\0';
				}
				p->pending_leading_comment = buf;
			}
			break;
		}
			
		case GTEXT_YAML_EVENT_SCALAR: {
			/* Create scalar node */
			GTEXT_YAML_Node *node = yaml_node_new_scalar(
				p->ctx,
				event->data.scalar.ptr,
				event->data.scalar.len,
				event->tag,     /* Tag from event (may be NULL) */
				event->anchor   /* Anchor from event (may be NULL) */
			);
			
			if (!node) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory creating scalar node";
				}
				return GTEXT_YAML_E_OOM;
			}

			node_set_source_location(node, event->offset, event->line, event->col);
			node->as.scalar.scalar_style = event->scalar_style;

			/* Set for every scalar, so a scalar without one clears whatever the
			 * previous scalar left behind. */
			p->last_scalar_tag_own_line = (event->tag != NULL
				&& event->tag_line > 0 && event->tag_line < event->line);

			parser_attach_leading_comment(p, node);
			
			/* Register anchor if present */
			if (event->anchor) {
				GTEXT_YAML_Status anchor_status = register_anchor(p, event->anchor, node);
				if (anchor_status != GTEXT_YAML_OK) {
					return anchor_status;
				}
			}

			bool explicit_handled = false;
			GTEXT_YAML_Status explicit_status = capture_explicit_key(p, node, &explicit_handled);
			if (explicit_status != GTEXT_YAML_OK) {
				return explicit_status;
			}
			if (explicit_handled) {
				p->last_scalar_node = node;
				p->last_scalar_line = event->line;
				p->last_scalar_col = event->col;
				p->last_scalar_key_col = line_key_col_from_offset(p, event->offset);
				p->last_emitted_node = node;
				p->last_emitted_line = event->line;
				break;
			}
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				GTEXT_YAML_Status root_status = set_document_root(p, node);
				if (root_status != GTEXT_YAML_OK) return root_status;
				p->last_scalar_in_root = true;
				p->last_scalar_in_temp = false;
			} else {
				/* A scalar at the key's own column, while a value is still
				 * expected, is the next key rather than that value.  Supply
				 * the null the absent value stands for, before this scalar
				 * takes its place in the alternating list. */
				if (block_value_is_missing(p, event->col)) {
					if (!mapping_supply_null_value(p)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory completing mapping value";
						}
						return GTEXT_YAML_E_OOM;
					}
					p->stack.states[p->stack.depth - 1] = STATE_MAPPING_KEY;
				}

				/* Before the add, so that "was this the first item?" is still
				 * answerable. */
				const bool first_in_level = (p->temp.count == 0);

				{
					GTEXT_YAML_Status sep_status = flow_entry_needs_separator(p);
					if (sep_status != GTEXT_YAML_OK) return sep_status;
				}

				/* A scalar indented past its block mapping is that mapping's
				 * value, and only when a key above is still waiting for one.
				 * With every key already paired there is nothing for it to be,
				 * and it used to become a trailing key with a null value - so
				 * "a: |" over a deeper "deep" over a shallower "shallow" gave
				 * {"a": "deep\n", "shallow": null} for input no other parser
				 * accepts.  A "-" never reaches here, so a block sequence at
				 * its key's own column is unaffected.
				 *
				 * The column is the line's first non-space, not the scalar's
				 * own: a tag or anchor sits before the scalar and belongs to
				 * the same node, so "!!str true" as a key starts where the
				 * tag does. */
				const int node_col = line_key_col_from_offset(p, event->offset);
				if (node_col >= 0 &&
					p->stack.is_block[p->stack.depth - 1] &&
					(p->stack.states[p->stack.depth - 1] == STATE_MAPPING_KEY ||
					 p->stack.states[p->stack.depth - 1] == STATE_MAPPING_VALUE) &&
					p->stack.indents[p->stack.depth - 1] >= 0 &&
					node_col > p->stack.indents[p->stack.depth - 1] &&
					(p->temp.count % 2) == 0) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_INVALID;
						p->error->message =
							"Scalar indented deeper than its mapping with no key to hold it";
					}
					return GTEXT_YAML_E_INVALID;
				}

				if (!temp_add(p, node)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory adding child node";
					}
					return GTEXT_YAML_E_OOM;
				}

				/* An own-line tag before a block sequence reaches the parser on
				 * the sequence's first item, because the '-' that opens the
				 * sequence carries no node of its own.  It belongs to the
				 * sequence. */
				if (first_in_level
					&& p->stack.states[p->stack.depth - 1] == STATE_SEQUENCE
					&& p->stack.is_block[p->stack.depth - 1]) {
					adopt_own_line_tag(p, node);
				}

				p->last_scalar_in_root = false;
				p->last_scalar_in_temp = true;
				p->last_scalar_temp_depth = p->stack.depth;

				flow_entry_completed(p);
				maybe_finish_block_mapping_value(p);
			}

			p->last_scalar_node = node;
			p->last_scalar_line = event->line;
			p->last_scalar_col = event->col;
			p->last_scalar_key_col = line_key_col_from_offset(p, event->offset);
			p->last_emitted_node = node;
			p->last_emitted_line = event->line;
			break;
		}
		
		case GTEXT_YAML_EVENT_SEQUENCE_START: {
			/* Start building a sequence - we don't know the size yet */
			const GTEXT_YAML_Event *evt = (const GTEXT_YAML_Event *)event;

			/* Checked as this one opens, not as it closes: by the time it
			 * closes its own level is on the stack and the entry it has to be
			 * separated from is a level below. */
			{
				GTEXT_YAML_Status sep_status = flow_entry_needs_separator(p);
				if (sep_status != GTEXT_YAML_OK) return sep_status;
			}
			
			/* Push placeholder (we'll create the actual node on SEQUENCE_END) */
			/* Store anchor and tag from event for later use */
			if (!stack_push(
				p,
				NULL,
				STATE_SEQUENCE,
				evt->anchor,
				evt->tag,
				-1,
				false,
				evt->offset,
				evt->line,
				evt->col
			)) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory tracking sequence";
				}
				return GTEXT_YAML_E_OOM;
			}
			break;
		}
		
		case GTEXT_YAML_EVENT_SEQUENCE_END: {
			/* A "[a: 1]" pair has no "}" of its own; the "]" that ends the
			 * entry closes it. */
			{
				GTEXT_YAML_Status pair_status = close_flow_pair(p);
				if (pair_status != GTEXT_YAML_OK) return pair_status;
			}
			/* Get anchor and tag from saved stack state */
			char *anchor = NULL;
			char *tag = NULL;
			size_t source_offset = 0;
			int source_line = 0;
			int source_col = 0;
			stack_get_and_clear_metadata(
				p,
				&anchor,
				&tag,
				&source_offset,
				&source_line,
				&source_col
			);
			
			/* Create sequence node with collected children */
			GTEXT_YAML_Node *node = yaml_node_new_sequence(
				p->ctx,
				p->temp.count,
				tag,
				anchor
			);
			
			/* Free the malloc'd anchor and tag strings */
			free(anchor);
			free(tag);
			
			if (!node) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory creating sequence node";
				}
				return GTEXT_YAML_E_OOM;
			}

			parser_attach_leading_comment(p, node);
			
			/* Copy children into node */
			for (size_t i = 0; i < p->temp.count; i++) {
				node->as.sequence.children[i] = p->temp.items[i];
			}
			node->as.sequence.count = p->temp.count;
			node_set_source_location(node, source_offset, source_line, source_col);
			
			/* Register anchor if present */
			if (node->as.sequence.anchor) {
				GTEXT_YAML_Status anchor_status = register_anchor(p, node->as.sequence.anchor, node);
				if (anchor_status != GTEXT_YAML_OK) {
					return anchor_status;
				}
			}
			
			/* Pop sequence from stack (restores parent temp) */
			stack_pop(p);
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				GTEXT_YAML_Status root_status = set_document_root(p, node);
				if (root_status != GTEXT_YAML_OK) return root_status;
			} else {
				bool explicit_handled = false;
				GTEXT_YAML_Status explicit_status = capture_explicit_key(p, node, &explicit_handled);
				if (explicit_status != GTEXT_YAML_OK) {
					return explicit_status;
				}
				if (!explicit_handled) {
					/* Add to parent's temp */
					if (!temp_add(p, node)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory nesting sequence";
						}
						return GTEXT_YAML_E_OOM;
					}
					flow_entry_completed(p);
					maybe_finish_block_mapping_value(p);
				}
			}

			p->last_emitted_node = node;
			p->last_emitted_line = event->line;
			
			break;
		}
		
		case GTEXT_YAML_EVENT_MAPPING_START: {
			/* Start building a mapping */
			const GTEXT_YAML_Event *evt = (const GTEXT_YAML_Event *)event;

			{
				GTEXT_YAML_Status sep_status = flow_entry_needs_separator(p);
				if (sep_status != GTEXT_YAML_OK) return sep_status;
			}
			
			if (!stack_push(
				p,
				NULL,
				STATE_MAPPING_KEY,
				evt->anchor,
				evt->tag,
				-1,
				false,
				evt->offset,
				evt->line,
				evt->col
			)) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory tracking mapping";
				}
				return GTEXT_YAML_E_OOM;
			}
			break;
		}
		
		case GTEXT_YAML_EVENT_MAPPING_END: {
			/* Get anchor and tag from saved stack state */
			char *anchor = NULL;
			char *tag = NULL;
			size_t source_offset = 0;
			int source_line = 0;
			int source_col = 0;
			stack_get_and_clear_metadata(
				p,
				&anchor,
				&tag,
				&source_offset,
				&source_line,
				&source_col
			);
			
			/* Create mapping node with collected key-value pairs */
			/* temp.items should have [key0, val0, key1, val1, ...] */
			/* A trailing key with no value, by the same rule: "a:" at the end of a
		 * document, or "? a" with no ':' after it.  On OOM the key is dropped
		 * as it was before, which is the old behavior rather than a new one. */
		if ((p->temp.count % 2) != 0) {
			(void)mapping_supply_null_value(p);
		}
		size_t pair_count = p->temp.count / 2;
			
			GTEXT_YAML_Node *node = yaml_node_new_mapping(
				p->ctx,
				pair_count,
				tag,
				anchor
			);
			
			/* Free the malloc'd anchor and tag strings */
			free(anchor);
			free(tag);
			
			if (!node) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory creating mapping node";
				}
				return GTEXT_YAML_E_OOM;
			}

			parser_attach_leading_comment(p, node);
			
			/* Copy pairs into node */
			for (size_t i = 0; i < pair_count; i++) {
				node->as.mapping.pairs[i].key = p->temp.items[i * 2];
				node->as.mapping.pairs[i].value = p->temp.items[i * 2 + 1];
				node->as.mapping.pairs[i].key_tag = NULL;
				node->as.mapping.pairs[i].value_tag = NULL;
			}
			node->as.mapping.count = pair_count;
			node_set_source_location(node, source_offset, source_line, source_col);
			
			/* Register anchor if present */
			if (node->as.mapping.anchor) {
				GTEXT_YAML_Status anchor_status = register_anchor(p, node->as.mapping.anchor, node);
				if (anchor_status != GTEXT_YAML_OK) {
					return anchor_status;
				}
			}
			
			/* Pop mapping from stack (restores parent temp) */
			stack_pop(p);
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				GTEXT_YAML_Status root_status = set_document_root(p, node);
				if (root_status != GTEXT_YAML_OK) return root_status;
			} else {
				bool explicit_handled = false;
				GTEXT_YAML_Status explicit_status = capture_explicit_key(p, node, &explicit_handled);
				if (explicit_status != GTEXT_YAML_OK) {
					return explicit_status;
				}
				if (!explicit_handled) {
					if (!temp_add(p, node)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory nesting mapping";
						}
						return GTEXT_YAML_E_OOM;
					}
					flow_entry_completed(p);
					maybe_finish_block_mapping_value(p);
				}
			}

			p->last_emitted_node = node;
			p->last_emitted_line = event->line;
			
			break;
		}
		
		case GTEXT_YAML_EVENT_STREAM_END:
			/* End of stream */
			break;
			
		case GTEXT_YAML_EVENT_DOCUMENT_END:
			/* End of document */
			if (p->document_started && !p->first_document_complete) {
				GTEXT_YAML_Status close_status = close_block_contexts(p, -1);
				if (close_status != GTEXT_YAML_OK) return close_status;
				
				/* First document is complete */
				p->first_document_complete = true;
				p->document_count = 1;
			}
			break;
			
		case GTEXT_YAML_EVENT_ALIAS: {
			/* Create alias node */
			const GTEXT_YAML_Parse_Options *opts = p->doc ? &p->doc->options : NULL;
			const char *anchor_name = event->data.alias_name;
			GTEXT_YAML_Node *node = NULL;
			bool explicit_handled = false;
			GTEXT_YAML_Status explicit_status = GTEXT_YAML_OK;

			if (opts && !opts->allow_aliases) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_INVALID;
					p->error->message = "Aliases are disabled by parse options";
				}
				return GTEXT_YAML_E_INVALID;
			}

			if (!anchor_name) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_INVALID;
					p->error->message = "Alias event missing anchor name";
				}
				return GTEXT_YAML_E_INVALID;
			}
			
			node = yaml_node_new_alias(p->ctx, anchor_name);
			if (!node) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory creating alias node";
				}
				return GTEXT_YAML_E_OOM;
			}

			node_set_source_location(node, event->offset, event->line, event->col);

			parser_attach_leading_comment(p, node);
			
			/* Track alias for later resolution */
			if (!track_alias(p, node)) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_OOM;
					p->error->message = "Out of memory tracking alias";
				}
				return GTEXT_YAML_E_OOM;
			}
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				GTEXT_YAML_Status root_status = set_document_root(p, node);
				if (root_status != GTEXT_YAML_OK) return root_status;
			} else {
				explicit_status = capture_explicit_key(p, node, &explicit_handled);
				if (explicit_status != GTEXT_YAML_OK) {
					return explicit_status;
				}
				if (!explicit_handled) {
					GTEXT_YAML_Status sep_status = flow_entry_needs_separator(p);
					if (sep_status != GTEXT_YAML_OK) return sep_status;
					if (!temp_add(p, node)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory adding alias node";
						}
						return GTEXT_YAML_E_OOM;
					}
					flow_entry_completed(p);
					maybe_finish_block_mapping_value(p);
				}
			}

			p->last_emitted_node = node;
			p->last_emitted_line = event->line;
			break;
		}
			
		case GTEXT_YAML_EVENT_INDICATOR: {
			/* Handle structural indicators: [ ] { } , : - */
			char ch = event->data.indicator;
			
			switch (ch) {
				case '[':
					/* Start flow sequence (fallback if START event not emitted) */
					{
						GTEXT_YAML_Status sep_status = flow_entry_needs_separator(p);
						if (sep_status != GTEXT_YAML_OK) return sep_status;
					}
					if (!stack_push(
						p,
						NULL,
						STATE_SEQUENCE,
						NULL,
						NULL,
						-1,
						false,
						event->offset,
						event->line,
						event->col
					)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory tracking sequence";
						}
						return GTEXT_YAML_E_OOM;
					}
					break;
					
				case ']': {
					/* End flow sequence - create node with collected items */
					{
						GTEXT_YAML_Status pair_status = close_flow_pair(p);
						if (pair_status != GTEXT_YAML_OK) return pair_status;
					}
					if (p->stack.depth == 0 || p->stack.states[p->stack.depth - 1] != STATE_SEQUENCE) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Unexpected ] without matching [";
						}
						return GTEXT_YAML_E_INVALID;
					}
					char *anchor = NULL;
					char *tag = NULL;
					size_t source_offset = 0;
					int source_line = 0;
					int source_col = 0;
					stack_get_and_clear_metadata(
						p,
						&anchor,
						&tag,
						&source_offset,
						&source_line,
						&source_col
					);
					
					GTEXT_YAML_Node *node = yaml_node_new_sequence(
						p->ctx,
						p->temp.count,
						tag,
						anchor
					);
					free(anchor);
					free(tag);
					if (!node) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory creating sequence";
						}
						return GTEXT_YAML_E_OOM;
					}

					parser_attach_leading_comment(p, node);
					
					/* Set the count */
					node->as.sequence.count = p->temp.count;
					node_set_source_location(node, source_offset, source_line, source_col);
					
					/* Copy collected items */
					for (size_t i = 0; i < p->temp.count; i++) {
						node->as.sequence.children[i] = p->temp.items[i];
					}
					
					stack_pop(p);
					
					/* Add to parent or set as root */
					if (p->stack.depth == 0) {
						GTEXT_YAML_Status root_status = set_document_root(p, node);
						if (root_status != GTEXT_YAML_OK) return root_status;
					} else {
						if (!temp_add(p, node)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory nesting sequence";
							}
							return GTEXT_YAML_E_OOM;
						}
						flow_entry_completed(p);
						maybe_finish_block_mapping_value(p);
					}

					p->last_emitted_node = node;
					p->last_emitted_line = event->line;
					break;
				}
				
				case '{':
					/* Start flow mapping (fallback if START event not emitted) */
					if (!stack_push(
						p,
						NULL,
						STATE_MAPPING_KEY,
						NULL,
						NULL,
						-1,
						false,
						event->offset,
						event->line,
						event->col
					)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory tracking mapping";
						}
						return GTEXT_YAML_E_OOM;
					}
					break;
					
				case '}': {
					/* End flow mapping - create node with collected pairs */
					if (p->stack.depth == 0 || 
					    (p->stack.states[p->stack.depth - 1] != STATE_MAPPING_KEY &&
					     p->stack.states[p->stack.depth - 1] != STATE_MAPPING_VALUE)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Unexpected } without matching {";
						}
						return GTEXT_YAML_E_INVALID;
					}
					char *anchor = NULL;
					char *tag = NULL;
					size_t source_offset = 0;
					int source_line = 0;
					int source_col = 0;
					stack_get_and_clear_metadata(
						p,
						&anchor,
						&tag,
						&source_offset,
						&source_line,
						&source_col
					);
					
					/* temp.count should be even (key-value pairs) */
					/* A trailing key with no value, by the same rule: "a:" at the end of a
		 * document, or "? a" with no ':' after it.  On OOM the key is dropped
		 * as it was before, which is the old behavior rather than a new one. */
		if ((p->temp.count % 2) != 0) {
			(void)mapping_supply_null_value(p);
		}
		size_t pair_count = p->temp.count / 2;
					GTEXT_YAML_Node *node = yaml_node_new_mapping(
						p->ctx, pair_count, tag, anchor
					);
					free(anchor);
					free(tag);
					if (!node) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory creating mapping";
						}
						return GTEXT_YAML_E_OOM;
					}

					parser_attach_leading_comment(p, node);
					
					/* Set the count */
					node->as.mapping.count = pair_count;
					node_set_source_location(node, source_offset, source_line, source_col);
					
					/* Copy key-value pairs */
					for (size_t i = 0; i < pair_count; i++) {
						node->as.mapping.pairs[i].key = p->temp.items[i * 2];
						node->as.mapping.pairs[i].value = p->temp.items[i * 2 + 1];
					}
					
					stack_pop(p);
					
					/* Add to parent or set as root */
					if (p->stack.depth == 0) {
						GTEXT_YAML_Status root_status = set_document_root(p, node);
						if (root_status != GTEXT_YAML_OK) return root_status;
					} else {
						if (!temp_add(p, node)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory nesting mapping";
							}
							return GTEXT_YAML_E_OOM;
						}
						flow_entry_completed(p);
						maybe_finish_block_mapping_value(p);
					}

					p->last_emitted_node = node;
					p->last_emitted_line = event->line;
					break;
				}
				
				case ':':
				{
					/* Mapping key-value separator */
					bool in_flow_mapping = false;
					bool in_block_mapping = false;
					int key_indent = p->last_scalar_key_col >= 0
						? p->last_scalar_key_col
						: p->last_scalar_col;
					GTEXT_YAML_Node *key_node = NULL;
					size_t source_offset = 0;
					int source_line = 0;
					int source_col = 0;
					size_t top = 0;

					if (p->explicit_key_pending && p->stack.depth <= p->explicit_key_depth) {
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Explicit key missing before ':'";
						}
						return GTEXT_YAML_E_INVALID;
					}

					if (p->stack.depth > 0) {
						top = p->stack.depth - 1;
						in_flow_mapping = !p->stack.is_block[top] &&
							(p->stack.states[top] == STATE_MAPPING_KEY ||
							 p->stack.states[top] == STATE_MAPPING_VALUE);
						in_block_mapping = p->stack.is_block[top] &&
							(p->stack.states[top] == STATE_MAPPING_KEY ||
							 p->stack.states[top] == STATE_MAPPING_VALUE);
					}

					if (p->explicit_key_active && p->stack.depth == p->explicit_key_depth) {
						if (p->stack.depth == 0) {
							if (p->error) {
								p->error->code = GTEXT_YAML_E_INVALID;
								p->error->message = "Explicit key missing mapping context";
							}
							return GTEXT_YAML_E_INVALID;
						}
						if (in_block_mapping && p->stack.indents[top] != event->col) {
							if (p->error) {
								p->error->code = GTEXT_YAML_E_INVALID;
								p->error->message = "Explicit key ':' indentation mismatch";
							}
							return GTEXT_YAML_E_INVALID;
						}
						p->stack.states[top] = STATE_MAPPING_VALUE;
						p->expect_mapping_value = true;
						p->explicit_key_active = false;
						break;
					}

					if (in_flow_mapping) {
						p->stack.states[top] = STATE_MAPPING_VALUE;
						break;
					}

					/* A ":" directly inside a flow sequence makes that entry a
					 * single-pair mapping: "[a: 1]" is "[{a: 1}]".  The pair
					 * closes at the next "," or "]" rather than at a "}", so
					 * the level is marked to say so.  Without this the ":" fell
					 * through to the block-mapping path below and pushed a
					 * block level inside the sequence, which then swallowed the
					 * "]" that should have closed it. */
					if (p->stack.depth > 0 && !p->stack.is_block[top] &&
						p->stack.states[top] == STATE_SEQUENCE) {
						GTEXT_YAML_Node *pair_key = detach_last_scalar(p);
						if (!pair_key && p->temp.count > 0) {
							/* The key may be a collection rather than a scalar,
							 * as in "[[1]: 2]", and detach_last_scalar() only
							 * knows about scalars. */
							pair_key = p->temp.items[--p->temp.count];
						}
						if (!pair_key) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_INVALID;
								p->error->message = "Mapping key not found before ':'";
							}
							return GTEXT_YAML_E_INVALID;
						}
						size_t pair_offset = 0;
						int pair_line = 0, pair_col = 0;
						node_get_source_location(pair_key, &pair_offset, &pair_line, &pair_col);
						if (!stack_push(p, NULL, STATE_MAPPING_VALUE, NULL, NULL,
								-1, false, pair_offset, pair_line, pair_col)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory starting flow pair";
							}
							return GTEXT_YAML_E_OOM;
						}
						p->stack.flow_flags[p->stack.depth - 1] |= GTEXT_YAML_FLOW_PAIR;
						if (!temp_add(p, pair_key)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory starting flow pair";
							}
							return GTEXT_YAML_E_OOM;
						}
						p->expect_mapping_value = true;
						break;
					}

					if (key_indent < 0) {
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Mapping key missing before ':'";
						}
						return GTEXT_YAML_E_INVALID;
					}

					if (p->last_scalar_line != event->line) {
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Mapping key not on same line as ':'";
						}
						return GTEXT_YAML_E_INVALID;
					}

					if (in_block_mapping && p->stack.indents[top] == key_indent) {
						/* The scalar before a ":" is its key, and it is held
						 * provisionally as the previous key's value until this
						 * ":" arrives to claim it.  An even count means no such
						 * scalar is outstanding, so this ":" has no key at all:
						 * "key: a : b" used to yield {"key": "a", "b": null},
						 * silently turning the tail of a value into a pair. */
						if ((p->temp.count % 2) == 0) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_INVALID;
								p->error->message = "Mapping key missing before ':'";
							}
							return GTEXT_YAML_E_INVALID;
						}
						p->stack.states[top] = STATE_MAPPING_VALUE;
						p->expect_mapping_value = true;
						break;
					}

					key_node = detach_last_scalar(p);
					if (!key_node) {
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Mapping key not found before ':'";
						}
						return GTEXT_YAML_E_INVALID;
					}
					node_get_source_location(key_node, &source_offset, &source_line, &source_col);

					/* A key indented further than its mapping starts a nested
					 * mapping only when there is a key above still waiting for
					 * a value to put it under.  A mapping's children are held
					 * as alternating key, value pairs, so with the key just
					 * detached an even count means every key already has one
					 * and this key belongs to nothing.  That used to nest
					 * regardless, which put a mapping where a key should be:
					 * "a: 1" followed by an indented "b: 2" parsed as
					 * {"a": 1, {"b": 2}: null} rather than being refused. */
					if (in_block_mapping && key_indent > p->stack.indents[top] &&
						(p->temp.count % 2) == 0) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message =
								"Mapping key indented deeper than its mapping";
						}
						return GTEXT_YAML_E_INVALID;
					}

					if (!stack_push(
						p,
						NULL,
						STATE_MAPPING_KEY,
						NULL,
						NULL,
						key_indent,
						true,
						source_offset,
						source_line,
						source_col
					)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory tracking block mapping";
						}
						return GTEXT_YAML_E_OOM;
					}

					/* An own-line tag before a block mapping reaches the parser
					 * on the mapping's first key, because the key is the first
					 * node the scanner has to attach it to.  It belongs to the
					 * mapping.  Done after the push, so the mapping's stack
					 * entry is the one on top. */
					adopt_own_line_tag(p, key_node);

					if (!temp_add(p, key_node)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory adding key to mapping";
						}
						return GTEXT_YAML_E_OOM;
					}

					p->stack.states[p->stack.depth - 1] = STATE_MAPPING_VALUE;
					p->expect_mapping_value = true;
					break;
				}

				case '?':
				{
					bool in_mapping = false;
					bool in_flow_mapping = false;
					bool at_block_mapping = false;
					int indent = event->col;
					size_t top = 0;

					if (p->explicit_key_pending) {
						/* A '?' with no key between it and the last one. */
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Explicit key already pending";
						}
						return GTEXT_YAML_E_INVALID;
					}

					/* A second '?' means the key before it never got a ':', so
					 * its value is null.  This used to be refused outright,
					 * which made "? a" followed by "? b" - and so the block
					 * spelling of !!set - unparseable. */
					if (p->explicit_key_active) {
						if (!mapping_supply_null_value(p)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory completing explicit key";
							}
							return GTEXT_YAML_E_OOM;
						}
						p->explicit_key_active = false;
						if (p->stack.depth > 0) {
							p->stack.states[p->stack.depth - 1] = STATE_MAPPING_KEY;
						}
					}

					if (p->stack.depth > 0) {
						top = p->stack.depth - 1;
						in_mapping = (p->stack.states[top] == STATE_MAPPING_KEY ||
							p->stack.states[top] == STATE_MAPPING_VALUE);
						in_flow_mapping = in_mapping && !p->stack.is_block[top];
						at_block_mapping = in_mapping && p->stack.is_block[top] &&
							p->stack.indents[top] == indent;
					}

					if (in_flow_mapping) {
						p->explicit_key_pending = true;
						p->explicit_key_active = false;
						p->explicit_key_indent = indent;
						p->explicit_key_depth = p->stack.depth;
						break;
					}

					if (!at_block_mapping) {
						if (!stack_push(
							p,
							NULL,
							STATE_MAPPING_KEY,
							NULL,
							NULL,
							indent,
							true,
							event->offset,
							event->line,
							event->col
						)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory tracking explicit key";
							}
							return GTEXT_YAML_E_OOM;
						}
					}

					p->explicit_key_pending = true;
					p->explicit_key_active = false;
					p->explicit_key_indent = indent;
					p->explicit_key_depth = p->stack.depth;
					break;
				}
					
				case ',':
					/* Item separator - handle mapping state flip */
					{
						GTEXT_YAML_Status pair_status = close_flow_pair(p);
						if (pair_status != GTEXT_YAML_OK) return pair_status;
					}
					if (p->stack.depth > 0) {
						p->stack.flow_flags[p->stack.depth - 1] &=
							(unsigned char)~GTEXT_YAML_FLOW_ITEM_DONE;
						const size_t sep_top = p->stack.depth - 1;
						const bool in_flow_map = !p->stack.is_block[sep_top] &&
							(p->stack.states[sep_top] == STATE_MAPPING_KEY ||
							 p->stack.states[sep_top] == STATE_MAPPING_VALUE);
						/* An entry that reached the comma without a ':' is a key
						 * whose value is null, the same rule a block mapping
						 * follows.  Children are held as alternating key, value
						 * pairs, so an odd count means the entry just closed has
						 * no value yet.  Without this, "{a, b}" paired the two
						 * keys with each other. */
						if (in_flow_map && (p->temp.count % 2) == 1) {
							if (!mapping_supply_null_value(p)) {
								p->failed = true;
								if (p->error) {
									p->error->code = GTEXT_YAML_E_OOM;
									p->error->message =
										"Out of memory completing flow mapping entry";
								}
								return GTEXT_YAML_E_OOM;
							}
						}
						if (p->stack.states[sep_top] == STATE_MAPPING_VALUE) {
							p->stack.states[sep_top] = STATE_MAPPING_KEY;
						}
					}
					break;
					
				case '-':
				{
					/* Block sequence indicator */
					bool start_new = true;
					int indent = event->col;

					if (p->stack.depth > 0) {
						size_t top = p->stack.depth - 1;
						if (p->stack.is_block[top] &&
							p->stack.states[top] == STATE_SEQUENCE &&
							p->stack.indents[top] == indent) {
							start_new = false;
						}
					}

					/* A block mapping's entries are pairs, so a "-" at the
					 * mapping's own column with every key already paired has
					 * nothing to be an entry of. It was becoming a sequence
					 * standing where a key belongs: "a: 1" over "- b" gave
					 * {"a": 1, ["b"]: null}, a sequence as a mapping key,
					 * which neither PyYAML nor js-yaml will parse. A key
					 * still waiting for its value is the ordinary case -
					 * "a:" over "- b" is that sequence as a's value - and an
					 * odd temp count is what tells them apart. */
					if (start_new && p->stack.depth > 0) {
						const size_t top = p->stack.depth - 1;
						if (p->stack.is_block[top] &&
							(p->stack.states[top] == STATE_MAPPING_KEY ||
							 p->stack.states[top] == STATE_MAPPING_VALUE) &&
							p->stack.indents[top] >= 0 &&
							indent <= p->stack.indents[top] &&
							(p->temp.count % 2) == 0) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_INVALID;
								p->error->message =
									"Block sequence entry where a mapping key belongs";
							}
							return GTEXT_YAML_E_INVALID;
						}
					}

					if (start_new) {
						if (!stack_push(
							p,
							NULL,
							STATE_SEQUENCE,
							NULL,
							NULL,
							indent,
							true,
							event->offset,
							event->line,
							event->col
						)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory tracking block sequence";
							}
							return GTEXT_YAML_E_OOM;
						}
					}
					break;
				}
					
				default:
					/* Unknown indicator - ignore */
					break;
			}
			break;
		}
	}
	
	return GTEXT_YAML_OK;
}

/**
 * @brief Parse YAML string into DOM document (internal implementation).
 */
GTEXT_YAML_Document *yaml_parse_document(
	const char *input,
	size_t length,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error
) {
	if (!input) {
		if (error) {
			error->code = GTEXT_YAML_E_INVALID;
			error->message = "Input string is NULL";
		}
		return NULL;
	}
	
	GTEXT_YAML_Parse_Options effective_opts =
		gtext_yaml_parse_options_effective(options);
	const GTEXT_YAML_Parse_Options *opts = &effective_opts;

	if (opts->enable_json_fast_path && json_fastpath_candidate(input, length)) {
		GTEXT_YAML_Document *json_doc = yaml_parse_json_document_internal(
			input,
			length,
			opts,
			error,
			false
		);
		if (json_doc) {
			return json_doc;
		}
	}
	
	/* Create context */
	yaml_context *ctx = yaml_context_new();
	if (!ctx) {
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating context";
		}
		return NULL;
	}
	
	/* Store input buffer reference (for future in-situ optimization) */
	yaml_context_set_input_buffer(ctx, input, length);
	
	/* Create document */
	GTEXT_YAML_Document *doc = (GTEXT_YAML_Document *)yaml_context_alloc(
		ctx, sizeof(GTEXT_YAML_Document), 8
	);
	if (!doc) {
		yaml_context_free(ctx);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating document";
		}
		return NULL;
	}
	
	memset(doc, 0, sizeof(*doc));
	doc->ctx = ctx;
	doc->options = *opts;
	doc->document_index = 0;  /* Always parsing first document */
	
	/* Initialize parser state */
	parser_state parser;
	if (!parser_init(&parser, ctx, error)) {
		yaml_context_free(ctx);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory initializing parser";
		}
		return NULL;
	}
	parser.doc = doc;
	
	/* Create streaming parser */
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(opts, parse_callback, &parser);
	if (!stream) {
		parser_free(&parser);
		yaml_context_free(ctx);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating stream parser";
		}
		return NULL;
	}
	
	/* Enable synchronous mode so aliases can be processed immediately */
	gtext_yaml_stream_set_sync_mode(stream, true);
	
	/* Feed input to streaming parser */
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, input, length);
	if (status == GTEXT_YAML_OK) {
		status = gtext_yaml_stream_finish(stream);
	}
	
	gtext_yaml_stream_free(stream);
	
	/* Finalize any open block collections */
	if (status == GTEXT_YAML_OK && !parser.failed) {
		status = close_block_contexts(&parser, -1);
	}
	if (status == GTEXT_YAML_OK && !parser.failed) {
		status = check_flow_contexts_closed(&parser);
	}
	
	/* Check if parsing succeeded */
	if (status != GTEXT_YAML_OK || parser.failed) {
		parser_free(&parser);
		yaml_context_free(ctx);
		if (error && error->code == GTEXT_YAML_OK) {
			error->code = status;
			error->message = "Parse error";
		}
		return NULL;
	}
	
	/* Resolve all alias nodes */
	status = resolve_aliases(&parser);
	if (status != GTEXT_YAML_OK) {
		parser_free(&parser);
		yaml_context_free(ctx);
		/* Error already set by resolve_aliases */
		return NULL;
	}
	
	/* Set document root */
	doc->root = parser.root;
	doc->node_count = 1;  /* TODO: track actual count */

	if (!finalize_tag_handles(&parser, doc)) {
		parser_free(&parser);
		yaml_context_free(ctx);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory finalizing tag handles";
		}
		return NULL;
	}

	/* Resolve tags and implicit scalar types */
	status = yaml_resolve_document(doc, error);
	if (status != GTEXT_YAML_OK) {
		parser_free(&parser);
		yaml_context_free(ctx);
		return NULL;
	}
	
	parser_free(&parser);
	return doc;
}

/* ==========================================================================
 * Partial Parser (gtext_yaml_parse_partial)
 * ==========================================================================
 */

typedef struct {
	yaml_context *ctx;
	GTEXT_YAML_Document *doc;
	parser_state parser;
	bool parser_ready;
	bool recovering;
	bool in_document;
	GTEXT_YAML_Error last_error;
	GTEXT_YAML_Error *errors;
	size_t error_count;
	size_t error_capacity;
	GTEXT_YAML_Node **top_nodes;
	size_t top_count;
	size_t top_capacity;
} partial_state;

static bool partial_errors_reserve(partial_state *state, size_t needed) {
	if (!state) return false;
	if (state->error_capacity >= needed) return true;

	size_t new_capacity = state->error_capacity == 0 ? 4 : state->error_capacity * 2;
	while (new_capacity < needed) {
		if (new_capacity > SIZE_MAX / 2) return false;
		new_capacity *= 2;
	}

	GTEXT_YAML_Error *errors = (GTEXT_YAML_Error *)realloc(
		state->errors, new_capacity * sizeof(*errors)
	);
	if (!errors) return false;
	state->errors = errors;
	state->error_capacity = new_capacity;
	return true;
}

static bool partial_errors_push(partial_state *state, const GTEXT_YAML_Error *err) {
	if (!state || !err) return false;
	if (!partial_errors_reserve(state, state->error_count + 1)) return false;

	GTEXT_YAML_Error *dst = &state->errors[state->error_count];
	memset(dst, 0, sizeof(*dst));
	*dst = *err;

	if (err->context_snippet && err->context_snippet_len > 0) {
		size_t len = err->context_snippet_len;
		char *copy = (char *)malloc(len + 1);
		if (!copy) return false;
		memcpy(copy, err->context_snippet, len);
		copy[len] = '\0';
		dst->context_snippet = copy;
		dst->context_snippet_len = len;
	} else {
		dst->context_snippet = NULL;
		dst->context_snippet_len = 0;
		dst->caret_offset = 0;
	}

	state->error_count++;
	return true;
}

static bool partial_top_reserve(partial_state *state, size_t needed) {
	if (!state) return false;
	if (state->top_capacity >= needed) return true;

	size_t new_capacity = state->top_capacity == 0 ? 4 : state->top_capacity * 2;
	while (new_capacity < needed) {
		if (new_capacity > SIZE_MAX / 2) return false;
		new_capacity *= 2;
	}

	GTEXT_YAML_Node **nodes = (GTEXT_YAML_Node **)realloc(
		state->top_nodes, new_capacity * sizeof(*nodes)
	);
	if (!nodes) return false;
	state->top_nodes = nodes;
	state->top_capacity = new_capacity;
	return true;
}

static bool partial_top_push(partial_state *state, GTEXT_YAML_Node *node) {
	if (!state || !node) return false;
	if (!partial_top_reserve(state, state->top_count + 1)) return false;
	state->top_nodes[state->top_count++] = node;
	return true;
}

static void partial_capture_root(partial_state *state) {
	parser_state *p = NULL;
	if (!state || !state->parser_ready) return;
	p = &state->parser;
	if (!p->root) return;
	if (!partial_top_push(state, p->root)) return;
	p->root = NULL;
	p->last_scalar_node = NULL;
	p->last_scalar_in_root = false;
	p->last_scalar_in_temp = false;
	p->last_scalar_temp_depth = 0;
	if (p->pending_leading_comment) {
		free(p->pending_leading_comment);
		p->pending_leading_comment = NULL;
	}
}

static void partial_error_fallback(
	GTEXT_YAML_Error *err,
	const GTEXT_YAML_Event *event,
	GTEXT_YAML_Status status
) {
	if (!err) return;
	err->code = status != GTEXT_YAML_OK ? status : GTEXT_YAML_E_INVALID;
	err->message = "Parse error";
	err->offset = event ? event->offset : 0;
	err->line = event ? event->line : 0;
	err->col = event ? event->col : 0;
	err->context_snippet = NULL;
	err->context_snippet_len = 0;
	err->caret_offset = 0;
	err->expected_token = NULL;
	err->actual_token = NULL;
}

static void partial_add_error_marker(partial_state *state, const GTEXT_YAML_Error *err) {
	if (!state || !state->ctx || !err) return;
	const char *message = err->message ? err->message : "parse error";
	char buf[256];
	snprintf(buf, sizeof(buf), "error: %s", message);
	GTEXT_YAML_Node *node = yaml_node_new_scalar(state->ctx, buf, strlen(buf), NULL, NULL);
	if (!node) return;
	node_set_source_location(node, err->offset, err->line, err->col);

	char comment[128];
	snprintf(comment, sizeof(comment), "parse_error line %d col %d", err->line, err->col);
	const char *stored = parser_arena_strdup(state->ctx, comment, strlen(comment));
	if (stored) {
		node->as.scalar.leading_comment = stored;
	}
	partial_top_push(state, node);
}

static bool partial_event_starts_node(const GTEXT_YAML_Event *event) {
	if (!event) return false;
	switch (event->type) {
		case GTEXT_YAML_EVENT_SCALAR:
		case GTEXT_YAML_EVENT_SEQUENCE_START:
		case GTEXT_YAML_EVENT_MAPPING_START:
		case GTEXT_YAML_EVENT_ALIAS:
			return true;
		case GTEXT_YAML_EVENT_INDICATOR:
			switch (event->data.indicator) {
				case '-':
				case '?':
				case ':':
				case '[':
				case '{':
					return true;
				default:
					return false;
			}
		default:
			return false;
	}
}

static bool partial_reset_parser(partial_state *state) {
	if (!state || !state->parser_ready) return false;
	parser_free(&state->parser);
	if (!parser_init(&state->parser, state->ctx, &state->last_error)) return false;
	state->parser.doc = state->doc;
	state->parser.document_started = state->in_document;
	state->parser.first_document_complete = false;
	return true;
}

static GTEXT_YAML_Status partial_callback(
	GTEXT_YAML_Stream *s,
	const void *event_payload,
	void *user_data
) {
	(void)s;
	partial_state *state = (partial_state *)user_data;
	const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)event_payload;

	if (!state || !event) return GTEXT_YAML_E_INVALID;

	switch (event->type) {
		case GTEXT_YAML_EVENT_STREAM_START:
			return GTEXT_YAML_OK;
		case GTEXT_YAML_EVENT_DOCUMENT_START:
			state->in_document = true;
			if (state->recovering) {
				partial_reset_parser(state);
				state->recovering = false;
			}
			return GTEXT_YAML_OK;
		case GTEXT_YAML_EVENT_DOCUMENT_END:
			if (state->parser_ready) {
				GTEXT_YAML_Status close_status = close_block_contexts(&state->parser, -1);
				if (close_status != GTEXT_YAML_OK) {
					if (close_status == GTEXT_YAML_E_OOM || close_status == GTEXT_YAML_E_LIMIT) {
						return close_status;
					}
					GTEXT_YAML_Error captured = state->last_error;
					if (captured.code == GTEXT_YAML_OK) {
						partial_error_fallback(&captured, event, close_status);
					}
					if (!partial_errors_push(state, &captured)) {
						return GTEXT_YAML_E_OOM;
					}
					partial_add_error_marker(state, &captured);
				}
			}
			partial_capture_root(state);
			state->in_document = false;
			return GTEXT_YAML_OK;
		case GTEXT_YAML_EVENT_STREAM_END:
			if (state->parser_ready) {
				GTEXT_YAML_Status close_status = close_block_contexts(&state->parser, -1);
				if (close_status != GTEXT_YAML_OK) {
					if (close_status == GTEXT_YAML_E_OOM || close_status == GTEXT_YAML_E_LIMIT) {
						return close_status;
					}
					GTEXT_YAML_Error captured = state->last_error;
					if (captured.code == GTEXT_YAML_OK) {
						partial_error_fallback(&captured, event, close_status);
					}
					if (!partial_errors_push(state, &captured)) {
						return GTEXT_YAML_E_OOM;
					}
					partial_add_error_marker(state, &captured);
				}
			}
			partial_capture_root(state);
			return GTEXT_YAML_OK;
		default:
			break;
	}

	if (!state->in_document) {
		state->in_document = true;
		state->parser.document_started = true;
	}

	if (state->recovering) {
		if (event->col == 1 && partial_event_starts_node(event)) {
			partial_reset_parser(state);
			state->recovering = false;
		} else {
			return GTEXT_YAML_OK;
		}
	}

	GTEXT_YAML_Status status = parse_callback(s, event_payload, &state->parser);
	if (status != GTEXT_YAML_OK) {
		if (status == GTEXT_YAML_E_OOM || status == GTEXT_YAML_E_LIMIT) {
			return status;
		}
		GTEXT_YAML_Error captured = state->last_error;
		if (captured.code == GTEXT_YAML_OK) {
			partial_error_fallback(&captured, event, status);
		}
		if (!partial_errors_push(state, &captured)) {
			return GTEXT_YAML_E_OOM;
		}
		partial_capture_root(state);
		partial_add_error_marker(state, &captured);
		state->recovering = true;
		return GTEXT_YAML_OK;
	}

	return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_parse_partial(
	const void *data,
	size_t len,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Document **out_doc,
	GTEXT_YAML_Error **out_errors,
	size_t *out_error_count,
	GTEXT_YAML_Error *out_err
) {
	const char *input = (const char *)data;
	if (!input || !out_doc || !out_errors || !out_error_count) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "Invalid arguments";
		}
		return GTEXT_YAML_E_INVALID;
	}

	*out_doc = NULL;
	*out_errors = NULL;
	*out_error_count = 0;

	GTEXT_YAML_Parse_Options effective_opts =
		gtext_yaml_parse_options_effective(options);
	const GTEXT_YAML_Parse_Options *opts = &effective_opts;

	partial_state state;
	memset(&state, 0, sizeof(state));

	state.ctx = yaml_context_new();
	if (!state.ctx) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_OOM;
			out_err->message = "Out of memory creating context";
		}
		return GTEXT_YAML_E_OOM;
	}
	yaml_context_set_input_buffer(state.ctx, input, len);

	state.doc = (GTEXT_YAML_Document *)yaml_context_alloc(
		state.ctx, sizeof(GTEXT_YAML_Document), 8
	);
	if (!state.doc) {
		yaml_context_free(state.ctx);
		if (out_err) {
			out_err->code = GTEXT_YAML_E_OOM;
			out_err->message = "Out of memory creating document";
		}
		return GTEXT_YAML_E_OOM;
	}
	memset(state.doc, 0, sizeof(*state.doc));
	state.doc->ctx = state.ctx;
	state.doc->options = *opts;
	state.doc->document_index = 0;

	if (!parser_init(&state.parser, state.ctx, &state.last_error)) {
		yaml_context_free(state.ctx);
		if (out_err) {
			out_err->code = GTEXT_YAML_E_OOM;
			out_err->message = "Out of memory initializing parser";
		}
		return GTEXT_YAML_E_OOM;
	}
	state.parser_ready = true;
	state.parser.doc = state.doc;

	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(opts, partial_callback, &state);
	if (!stream) {
		parser_free(&state.parser);
		yaml_context_free(state.ctx);
		if (out_err) {
			out_err->code = GTEXT_YAML_E_OOM;
			out_err->message = "Out of memory creating stream parser";
		}
		return GTEXT_YAML_E_OOM;
	}
	gtext_yaml_stream_set_sync_mode(stream, true);

	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, input, len);
	if (status == GTEXT_YAML_OK) {
		status = gtext_yaml_stream_finish(stream);
	}
	gtext_yaml_stream_free(stream);

	partial_capture_root(&state);

	if (status != GTEXT_YAML_OK) {
		parser_free(&state.parser);
		if (out_err) {
			*out_err = state.last_error;
			if (out_err->code == GTEXT_YAML_OK) {
				out_err->code = status;
				out_err->message = "Parse error";
			}
		}
		for (size_t i = 0; i < state.error_count; i++) {
			gtext_yaml_error_free(&state.errors[i]);
		}
		free(state.errors);
		free(state.top_nodes);
		yaml_context_free(state.ctx);
		return status;
	}

	GTEXT_YAML_Node *root = NULL;
	if (state.top_count == 1) {
		root = state.top_nodes[0];
	} else if (state.top_count > 1) {
		root = yaml_node_new_sequence(state.ctx, state.top_count, NULL, NULL);
		if (!root) {
			parser_free(&state.parser);
			for (size_t i = 0; i < state.error_count; i++) {
				gtext_yaml_error_free(&state.errors[i]);
			}
			free(state.errors);
			free(state.top_nodes);
			yaml_context_free(state.ctx);
			if (out_err) {
				out_err->code = GTEXT_YAML_E_OOM;
				out_err->message = "Out of memory creating recovery root";
			}
			return GTEXT_YAML_E_OOM;
		}
		for (size_t i = 0; i < state.top_count; i++) {
			root->as.sequence.children[i] = state.top_nodes[i];
		}
		root->as.sequence.count = state.top_count;
	}

	state.doc->root = root;
	state.doc->node_count = state.ctx->node_count;

	if (root) {
		GTEXT_YAML_Error resolve_error = {0};
		GTEXT_YAML_Status resolve_status = yaml_resolve_document(state.doc, &resolve_error);
		if (resolve_status != GTEXT_YAML_OK) {
			if (resolve_status == GTEXT_YAML_E_OOM || resolve_status == GTEXT_YAML_E_LIMIT) {
				parser_free(&state.parser);
				for (size_t i = 0; i < state.error_count; i++) {
					gtext_yaml_error_free(&state.errors[i]);
				}
				free(state.errors);
				free(state.top_nodes);
				yaml_context_free(state.ctx);
				if (out_err) {
					*out_err = resolve_error;
					if (out_err->code == GTEXT_YAML_OK) {
						out_err->code = resolve_status;
						out_err->message = "Out of memory resolving document";
					}
				}
				return resolve_status;
			}
			if (resolve_error.code == GTEXT_YAML_OK) {
				partial_error_fallback(&resolve_error, NULL, resolve_status);
			}
			if (!partial_errors_push(&state, &resolve_error)) {
				parser_free(&state.parser);
				for (size_t i = 0; i < state.error_count; i++) {
					gtext_yaml_error_free(&state.errors[i]);
				}
				free(state.errors);
				free(state.top_nodes);
				yaml_context_free(state.ctx);
				if (out_err) {
					out_err->code = GTEXT_YAML_E_OOM;
					out_err->message = "Out of memory storing resolve error";
				}
				return GTEXT_YAML_E_OOM;
			}
		}
	}

	parser_free(&state.parser);
	free(state.top_nodes);

	*out_doc = state.doc;
	*out_errors = state.errors;
	*out_error_count = state.error_count;

	return GTEXT_YAML_OK;
}

/* ============================================================================
 * Multi-Document Parser (gtext_yaml_parse_all)
 * ============================================================================ */

/**
 * @brief State for multi-document parsing.
 */
typedef struct {
	GTEXT_YAML_Document **documents;    /* Array of parsed documents */
	size_t count;                        /* Number of documents parsed */
	size_t capacity;                     /* Capacity of documents array */
	
	parser_state *current_parser;        /* Current document parser */
	yaml_context *current_context;       /* Current document context */
	GTEXT_YAML_Document *current_doc;    /* Current document being built */
	size_t current_doc_index;            /* Index of current document */
	
	const char *input;                   /* Input buffer (for context setup) */
	size_t input_length;                 /* Input buffer length */
	
	const GTEXT_YAML_Parse_Options *options;
	GTEXT_YAML_Error *error;
	bool failed;
	/* Whether the open document exists only because a directive needed
	 * somewhere to be recorded. A '%' line has to open a document, but the
	 * directive is a prologue to the document that follows rather than one of
	 * its own - without this, "%YAML 1.2" over "--- text" produced a null
	 * document in front of the real one. A '---' adopts that document and
	 * clears the flag; if the stream ends with it still set, the directive
	 * had no document to apply to. */
	bool current_from_directive;
} multidoc_state;

/**
 * @brief Finalize the current document and add it to the array.
 */
static bool multidoc_finalize_document(multidoc_state *state) {
	parser_state *p = NULL;

	if (!state->current_parser || !state->current_doc) {
		return true;  /* Nothing to finalize */
	}
	
	/* Finalize any open block collections */
	p = state->current_parser;
	if (close_block_contexts(p, -1) != GTEXT_YAML_OK) {
		state->failed = true;
		return false;
	}
	/* The same check gtext_yaml_parse() makes: a document that ended inside a
	 * flow collection is broken, not empty.  This path had been left out, so
	 * gtext_yaml_parse_all() still accepted what gtext_yaml_parse() refused. */
	if (check_flow_contexts_closed(p) != GTEXT_YAML_OK) {
		state->failed = true;
		return false;
	}
	
	/* Resolve aliases */
	GTEXT_YAML_Status status = resolve_aliases(p);
	if (status != GTEXT_YAML_OK) {
		state->failed = true;
		return false;
	}
	
	/* Set document root */
	state->current_doc->root = p->root;
	state->current_doc->node_count = 1;

	if (!finalize_tag_handles(p, state->current_doc)) {
		state->failed = true;
		if (state->error) {
			state->error->code = GTEXT_YAML_E_OOM;
			state->error->message = "Out of memory finalizing tag handles";
		}
		return false;
	}

	/* Resolve tags and implicit scalar types */
	status = yaml_resolve_document(state->current_doc, state->error);
	if (status != GTEXT_YAML_OK) {
		state->failed = true;
		return false;
	}
	
	/* Add to documents array */
	if (state->count >= state->capacity) {
		size_t new_capacity = state->capacity == 0 ? 4 : state->capacity * 2;
		GTEXT_YAML_Document **new_docs = (GTEXT_YAML_Document **)realloc(
			state->documents, new_capacity * sizeof(GTEXT_YAML_Document *)
		);
		if (!new_docs) {
			state->failed = true;
			if (state->error) {
				state->error->code = GTEXT_YAML_E_OOM;
				state->error->message = "Out of memory growing documents array";
			}
			return false;
		}
		state->documents = new_docs;
		state->capacity = new_capacity;
	}
	
	state->documents[state->count++] = state->current_doc;
	
	/* Clean up parser state (but not context - it's owned by document) */
	parser_free(state->current_parser);
	free(state->current_parser);
	state->current_parser = NULL;
	state->current_context = NULL;
	state->current_doc = NULL;
	
	return true;
}

/**
 * @brief Start a new document in the multi-document stream.
 */
static bool multidoc_start_document(multidoc_state *state, const char *input, size_t length) {
	/* Create context for this document */
	yaml_context *ctx = yaml_context_new();
	if (!ctx) {
		state->failed = true;
		if (state->error) {
			state->error->code = GTEXT_YAML_E_OOM;
			state->error->message = "Out of memory creating context";
		}
		return false;
	}
	
	/* Store input buffer reference */
	yaml_context_set_input_buffer(ctx, input, length);
	
	/* Create document */
	GTEXT_YAML_Document *doc = (GTEXT_YAML_Document *)yaml_context_alloc(
		ctx, sizeof(GTEXT_YAML_Document), 8
	);
	if (!doc) {
		yaml_context_free(ctx);
		state->failed = true;
		if (state->error) {
			state->error->code = GTEXT_YAML_E_OOM;
			state->error->message = "Out of memory creating document";
		}
		return false;
	}
	
	memset(doc, 0, sizeof(*doc));
	doc->ctx = ctx;
	doc->options = *state->options;
	doc->document_index = state->current_doc_index++;
	
	/* Initialize parser state */
	parser_state *parser = (parser_state *)malloc(sizeof(parser_state));
	if (!parser) {
		yaml_context_free(ctx);
		state->failed = true;
		if (state->error) {
			state->error->code = GTEXT_YAML_E_OOM;
			state->error->message = "Out of memory creating parser";
		}
		return false;
	}
	
	if (!parser_init(parser, ctx, state->error)) {
		free(parser);
		yaml_context_free(ctx);
		state->failed = true;
		if (state->error) {
			state->error->code = GTEXT_YAML_E_OOM;
			state->error->message = "Out of memory initializing parser";
		}
		return false;
	}
	parser->doc = doc;
	parser->document_started = true;  /* Mark as started */
	
	state->current_context = ctx;
	state->current_doc = doc;
	state->current_parser = parser;
	state->current_from_directive = false;
	
	return true;
}

/**
 * @brief Whether the open document is nothing but directives.
 *
 * A directive has to be followed by a document (6.8): "%YAML 1.2" on its own
 * is a prologue with nothing to prologue, and was being accepted as a null
 * document. The end of the stream can arrive as either DOCUMENT_END or
 * STREAM_END depending on how the input ends, so both ask.
 */
static bool multidoc_bare_directive(multidoc_state *state) {
	if (!state->current_parser || !state->current_doc) return false;
	if (!state->current_from_directive) return false;
	state->failed = true;
	if (state->error) {
		state->error->code = GTEXT_YAML_E_INVALID;
		state->error->message = "Directive with no document to apply to";
	}
	return true;
}

/**
 * @brief Event callback for multi-document parsing.
 */
static GTEXT_YAML_Status multidoc_callback(
	GTEXT_YAML_Stream *s,
	const void *event_payload,
	void *user_data
) {
	multidoc_state *state = (multidoc_state *)user_data;
	const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)event_payload;
	GTEXT_YAML_Event_Type type = event->type;

	if (state->failed) return GTEXT_YAML_E_STATE;
	
	/* Handle document boundaries */
	if (type == GTEXT_YAML_EVENT_DOCUMENT_START) {
		/* A document opened only to hold a directive is this document, not the
		 * one before it: adopt it rather than closing it and emitting a null. */
		if (state->current_parser && state->current_from_directive) {
			state->current_from_directive = false;
			return GTEXT_YAML_OK;
		}

		/* If we already have a document started, finalize it */
		if (state->current_parser && state->current_parser->document_started) {
			if (!multidoc_finalize_document(state)) {
				return GTEXT_YAML_E_OOM;
			}
		}
		
		/* Start new document */
		if (!multidoc_start_document(state, state->input, state->input_length)) {
			return GTEXT_YAML_E_OOM;
		}
		
		/* Don't pass DOCUMENT_START to the single-doc parser callback */
		/* as it's already marked as started */
		return GTEXT_YAML_OK;
	}
	
	if (type == GTEXT_YAML_EVENT_DOCUMENT_END) {
		if (multidoc_bare_directive(state)) return GTEXT_YAML_E_INVALID;
		/* Finalize current document */
		if (!multidoc_finalize_document(state)) {
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}
	
	if (type == GTEXT_YAML_EVENT_STREAM_END) {
		if (multidoc_bare_directive(state)) return GTEXT_YAML_E_INVALID;
		/* Finalize any remaining document */
		if (state->current_parser) {
			if (!multidoc_finalize_document(state)) {
				return GTEXT_YAML_E_OOM;
			}
		}
		return GTEXT_YAML_OK;
	}
	
	/* If no document started yet, start one (implicit document) */
	if (!state->current_parser) {
		if (!multidoc_start_document(state, state->input, state->input_length)) {
			return GTEXT_YAML_E_OOM;
		}
		/* A directive is the only event that reaches here without a document
		 * boundary ahead of it - comments produce no event and everything
		 * else is preceded by DOCUMENT_START - so this comparison is never
		 * false today and a mutation of it survives the suite. It stays
		 * because the flag has to mean what it is named if that changes. */
		state->current_from_directive = (type == GTEXT_YAML_EVENT_DIRECTIVE);
	}
	
	/* Pass event to current document's parser */
	return parse_callback(s, event_payload, state->current_parser);
}

/**
 * @brief Parse all documents in a YAML stream.
 */
GTEXT_YAML_Document **gtext_yaml_parse_all(
	const char *input,
	size_t length,
	size_t *document_count,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error
) {
	if (!input) {
		if (error) {
			error->code = GTEXT_YAML_E_INVALID;
			error->message = "Input string is NULL";
		}
		return NULL;
	}
	
	if (!document_count) {
		if (error) {
			error->code = GTEXT_YAML_E_INVALID;
			error->message = "document_count parameter is NULL";
		}
		return NULL;
	}
	
	GTEXT_YAML_Parse_Options effective_opts =
		gtext_yaml_parse_options_effective(options);
	const GTEXT_YAML_Parse_Options *opts = &effective_opts;
	
	/* Initialize multidoc state */
	multidoc_state state = {0};
	state.options = opts;
	state.error = error;
	state.input = input;
	state.input_length = length;
	
	/* Create streaming parser */
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(opts, multidoc_callback, &state);
	if (!stream) {
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating stream parser";
		}
		return NULL;
	}
	
	/* Enable synchronous mode so aliases can be processed immediately */
	gtext_yaml_stream_set_sync_mode(stream, true);
	
	/* Feed input to streaming parser */
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, input, length);
	if (status == GTEXT_YAML_OK) {
		status = gtext_yaml_stream_finish(stream);
	}
	
	gtext_yaml_stream_free(stream);
	
	/* Finalize any remaining document (stream doesn't emit STREAM_END) */
	if (status == GTEXT_YAML_OK && !state.failed && state.current_parser) {
		if (multidoc_bare_directive(&state)) {
			status = GTEXT_YAML_E_INVALID;
		}
		else if (!multidoc_finalize_document(&state)) {
			status = GTEXT_YAML_E_OOM;
		}
	}
	
	/* Check if parsing succeeded */
	if (status != GTEXT_YAML_OK || state.failed) {
		/* Clean up any documents that were created */
		for (size_t i = 0; i < state.count; i++) {
			gtext_yaml_free(state.documents[i]);
		}
		free(state.documents);
		
		/* Clean up current parser if still active */
		if (state.current_parser) {
			parser_free(state.current_parser);
			free(state.current_parser);
		}
		if (state.current_context) {
			yaml_context_free(state.current_context);
		}
		
		if (error && error->code == GTEXT_YAML_OK) {
			error->code = status;
			error->message = "Parse error";
		}
		return NULL;
	}
	
	*document_count = state.count;
	return state.documents;
}

GTEXT_API GTEXT_YAML_Document * gtext_yaml_parse_json(
	const char *input,
	size_t length,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error
) {
	if (!input) {
		if (error) {
			error->code = GTEXT_YAML_E_INVALID;
			error->message = "Input string is NULL";
		}
		return NULL;
	}

	return yaml_parse_json_document_internal(input, length, options, error, true);
}

GTEXT_YAML_Document **gtext_yaml_parse_all_safe(
	const char *input,
	size_t length,
	size_t *document_count,
	GTEXT_YAML_Error *error
) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_safe();
	return gtext_yaml_parse_all(input, length, document_count, &opts, error);
}
