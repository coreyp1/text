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
	bool last_scalar_in_root;           /* True if last scalar stored in root */
	bool last_scalar_in_temp;           /* True if last scalar stored in temp */
	size_t last_scalar_temp_depth;      /* Stack depth when scalar added to temp */
	bool explicit_key_pending;          /* True if '?' indicator seen and key is pending */
	bool explicit_key_active;           /* True if explicit key stored and awaiting ':' */
	int explicit_key_indent;            /* Indent column for explicit key */
	size_t explicit_key_depth;          /* Stack depth for explicit key mapping */
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
	p->stack.temps = (saved_temp *)calloc(p->stack.capacity, sizeof(saved_temp));
	
	if (!p->stack.nodes || !p->stack.states || !p->stack.indents ||
		!p->stack.is_block || !p->stack.temps) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.indents);
		free(p->stack.is_block);
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
	free(p->stack.temps);
	free(p->temp.items);
	
	/* Free anchor map */
	for (size_t i = 0; i < p->anchors.count; i++) {
		free(p->anchors.entries[i].name);
	}
	free(p->anchors.entries);
	
	/* Free alias list */
	free(p->aliases.nodes);

	/* Free tag handles */
	for (size_t i = 0; i < p->tag_handles.count; i++) {
		free(p->tag_handles.entries[i].handle);
		free(p->tag_handles.entries[i].prefix);
	}
	free(p->tag_handles.entries);
}

/* Forward declarations */
static GTEXT_YAML_Node *lookup_anchor(parser_state *p, const char *name);

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
	bool is_block
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
		saved_temp *new_temps = (saved_temp *)realloc(p->stack.temps, new_cap * sizeof(saved_temp));
		
		if (!new_nodes || !new_states || !new_indents || !new_is_block || !new_temps) {
			free(new_nodes);
			free(new_states);
			free(new_indents);
			free(new_is_block);
			free(new_temps);
			return false;
		}
		
		/* Zero-initialize new slots */
		memset(new_temps + p->stack.capacity, 0, (new_cap - p->stack.capacity) * sizeof(saved_temp));
		
		p->stack.nodes = new_nodes;
		p->stack.states = new_states;
		p->stack.indents = new_indents;
		p->stack.is_block = new_is_block;
		p->stack.temps = new_temps;
		p->stack.capacity = new_cap;
	}
	
	/* Save current temp state to the stack */
	p->stack.temps[p->stack.depth].items = p->temp.items;
	p->stack.temps[p->stack.depth].count = p->temp.count;
	p->stack.temps[p->stack.depth].capacity = p->temp.capacity;
	p->stack.temps[p->stack.depth].anchor = anchor ? strdup(anchor) : NULL;
	p->stack.temps[p->stack.depth].tag = tag ? strdup(tag) : NULL;
	
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
static void stack_get_and_clear_metadata(parser_state *p, char **anchor, char **tag) {
	if (p->stack.depth > 0 && p->stack.depth <= p->stack.capacity) {
		size_t idx = p->stack.depth - 1;
		*anchor = p->stack.temps[idx].anchor;
		*tag = p->stack.temps[idx].tag;
		p->stack.temps[idx].anchor = NULL;
		p->stack.temps[idx].tag = NULL;
	} else {
		*anchor = NULL;
		*tag = NULL;
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

static void maybe_finish_block_mapping_value(parser_state *p) {
	if (!p || p->stack.depth == 0) return;
	if (!p->stack.is_block[p->stack.depth - 1]) return;
	if (p->stack.states[p->stack.depth - 1] != STATE_MAPPING_VALUE) return;

	p->stack.states[p->stack.depth - 1] = STATE_MAPPING_KEY;
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

	if (!temp_add(p, node)) {
		if (p->error) {
			p->error->code = GTEXT_YAML_E_OOM;
			p->error->message = "Out of memory adding explicit key";
		}
		return GTEXT_YAML_E_OOM;
	}

	p->explicit_key_pending = false;
	p->explicit_key_active = true;
	p->stack.states[top] = STATE_MAPPING_KEY;
	*handled = true;
	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status finalize_top_collection(parser_state *p) {
	GTEXT_YAML_Node *node = NULL;
	char *anchor = NULL;
	char *tag = NULL;
	int state = 0;

	if (!p || p->stack.depth == 0) return GTEXT_YAML_OK;

	state = p->stack.states[p->stack.depth - 1];
	stack_get_and_clear_metadata(p, &anchor, &tag);

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
		p->root = node;
	} else {
		if (!temp_add(p, node)) {
			if (p->error) {
				p->error->code = GTEXT_YAML_E_OOM;
				p->error->message = "Out of memory nesting collection";
			}
			return GTEXT_YAML_E_OOM;
		}
		maybe_finish_block_mapping_value(p);
	}

	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status close_block_contexts(parser_state *p, int new_indent) {
	if (!p) return GTEXT_YAML_E_INVALID;

	while (p->stack.depth > 0) {
		if (!stack_top_is_block(p)) break;
		if (new_indent >= stack_top_indent(p)) break;
		
		GTEXT_YAML_Status status = finalize_top_collection(p);
		if (status != GTEXT_YAML_OK) return status;
	}

	return GTEXT_YAML_OK;
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
		GTEXT_YAML_Status close_status = close_block_contexts(p, event->col);
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
				break;
			}
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				p->root = node;
				p->last_scalar_in_root = true;
				p->last_scalar_in_temp = false;
			} else {
				if (!temp_add(p, node)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory adding child node";
					}
					return GTEXT_YAML_E_OOM;
				}

				p->last_scalar_in_root = false;
				p->last_scalar_in_temp = true;
				p->last_scalar_temp_depth = p->stack.depth;

				maybe_finish_block_mapping_value(p);
			}

			p->last_scalar_node = node;
			p->last_scalar_line = event->line;
			p->last_scalar_col = event->col;
			p->last_scalar_key_col = line_key_col_from_offset(p, event->offset);
			break;
		}
		
		case GTEXT_YAML_EVENT_SEQUENCE_START: {
			/* Start building a sequence - we don't know the size yet */
			const GTEXT_YAML_Event *evt = (const GTEXT_YAML_Event *)event;
			
			/* Push placeholder (we'll create the actual node on SEQUENCE_END) */
			/* Store anchor and tag from event for later use */
			if (!stack_push(p, NULL, STATE_SEQUENCE, evt->anchor, evt->tag, -1, false)) {
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
			/* Get anchor and tag from saved stack state */
			char *anchor = NULL;
			char *tag = NULL;
			stack_get_and_clear_metadata(p, &anchor, &tag);
			
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
			
			/* Copy children into node */
			for (size_t i = 0; i < p->temp.count; i++) {
				node->as.sequence.children[i] = p->temp.items[i];
			}
			node->as.sequence.count = p->temp.count;
			
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
				p->root = node;
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
					maybe_finish_block_mapping_value(p);
				}
			}
			
			break;
		}
		
		case GTEXT_YAML_EVENT_MAPPING_START: {
			/* Start building a mapping */
			const GTEXT_YAML_Event *evt = (const GTEXT_YAML_Event *)event;
			
			if (!stack_push(p, NULL, STATE_MAPPING_KEY, evt->anchor, evt->tag, -1, false)) {
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
			stack_get_and_clear_metadata(p, &anchor, &tag);
			
			/* Create mapping node with collected key-value pairs */
			/* temp.items should have [key0, val0, key1, val1, ...] */
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
			
			/* Copy pairs into node */
			for (size_t i = 0; i < pair_count; i++) {
				node->as.mapping.pairs[i].key = p->temp.items[i * 2];
				node->as.mapping.pairs[i].value = p->temp.items[i * 2 + 1];
				node->as.mapping.pairs[i].key_tag = NULL;
				node->as.mapping.pairs[i].value_tag = NULL;
			}
			node->as.mapping.count = pair_count;
			
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
				p->root = node;
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
					maybe_finish_block_mapping_value(p);
				}
			}
			
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
				p->root = node;
			} else {
				explicit_status = capture_explicit_key(p, node, &explicit_handled);
				if (explicit_status != GTEXT_YAML_OK) {
					return explicit_status;
				}
				if (!explicit_handled) {
					if (!temp_add(p, node)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory adding alias node";
						}
						return GTEXT_YAML_E_OOM;
					}
					maybe_finish_block_mapping_value(p);
				}
			}
			break;
		}
			
		case GTEXT_YAML_EVENT_INDICATOR: {
			/* Handle structural indicators: [ ] { } , : - */
			char ch = event->data.indicator;
			
			switch (ch) {
				case '[':
					/* Start flow sequence (fallback if START event not emitted) */
					if (!stack_push(p, NULL, STATE_SEQUENCE, NULL, NULL, -1, false)) {
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
					if (p->stack.depth == 0 || p->stack.states[p->stack.depth - 1] != STATE_SEQUENCE) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Unexpected ] without matching [";
						}
						return GTEXT_YAML_E_INVALID;
					}
					
					GTEXT_YAML_Node *node = yaml_node_new_sequence(
						p->ctx, p->temp.count, NULL, NULL
					);
					if (!node) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory creating sequence";
						}
						return GTEXT_YAML_E_OOM;
					}
					
					/* Set the count */
					node->as.sequence.count = p->temp.count;
					
					/* Copy collected items */
					for (size_t i = 0; i < p->temp.count; i++) {
						node->as.sequence.children[i] = p->temp.items[i];
					}
					
					stack_pop(p);
					
					/* Add to parent or set as root */
					if (p->stack.depth == 0) {
						p->root = node;
					} else {
						if (!temp_add(p, node)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory nesting sequence";
							}
							return GTEXT_YAML_E_OOM;
						}
						maybe_finish_block_mapping_value(p);
					}
					break;
				}
				
				case '{':
					/* Start flow mapping (fallback if START event not emitted) */
					if (!stack_push(p, NULL, STATE_MAPPING_KEY, NULL, NULL, -1, false)) {
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
					
					/* temp.count should be even (key-value pairs) */
					size_t pair_count = p->temp.count / 2;
					GTEXT_YAML_Node *node = yaml_node_new_mapping(
						p->ctx, pair_count, NULL, NULL
					);
					if (!node) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory creating mapping";
						}
						return GTEXT_YAML_E_OOM;
					}
					
					/* Set the count */
					node->as.mapping.count = pair_count;
					
					/* Copy key-value pairs */
					for (size_t i = 0; i < pair_count; i++) {
						node->as.mapping.pairs[i].key = p->temp.items[i * 2];
						node->as.mapping.pairs[i].value = p->temp.items[i * 2 + 1];
					}
					
					stack_pop(p);
					
					/* Add to parent or set as root */
					if (p->stack.depth == 0) {
						p->root = node;
					} else {
						if (!temp_add(p, node)) {
							p->failed = true;
							if (p->error) {
								p->error->code = GTEXT_YAML_E_OOM;
								p->error->message = "Out of memory nesting mapping";
							}
							return GTEXT_YAML_E_OOM;
						}
						maybe_finish_block_mapping_value(p);
					}
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

					if (!stack_push(p, NULL, STATE_MAPPING_KEY, NULL, NULL, key_indent, true)) {
						p->failed = true;
						if (p->error) {
							p->error->code = GTEXT_YAML_E_OOM;
							p->error->message = "Out of memory tracking block mapping";
						}
						return GTEXT_YAML_E_OOM;
					}

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

					if (p->explicit_key_pending || p->explicit_key_active) {
						if (p->error) {
							p->error->code = GTEXT_YAML_E_INVALID;
							p->error->message = "Explicit key already pending";
						}
						return GTEXT_YAML_E_INVALID;
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
						if (!stack_push(p, NULL, STATE_MAPPING_KEY, NULL, NULL, indent, true)) {
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
					if (p->stack.depth > 0 && p->stack.states[p->stack.depth - 1] == STATE_MAPPING_VALUE) {
						p->stack.states[p->stack.depth - 1] = STATE_MAPPING_KEY;
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

					if (start_new) {
						if (!stack_push(p, NULL, STATE_SEQUENCE, NULL, NULL, indent, true)) {
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
		/* Finalize current document */
		if (!multidoc_finalize_document(state)) {
			return GTEXT_YAML_E_OOM;
		}
		return GTEXT_YAML_OK;
	}
	
	if (type == GTEXT_YAML_EVENT_STREAM_END) {
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
		if (!multidoc_finalize_document(&state)) {
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
