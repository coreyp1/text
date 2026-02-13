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

/* Parser state for building DOM from events */
typedef struct {
	yaml_context *ctx;                  /* Context owns arena */
	GTEXT_YAML_Document *doc;           /* Document being built */
	GTEXT_YAML_Error *error;            /* Error output */
	
	/* Stack for tracking nesting (sequences/mappings in progress) */
	struct {
		GTEXT_YAML_Node **nodes;        /* Stack of nodes being built */
		int *states;                    /* State per level: 0=seq, 1=map_key, 2=map_value */
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
	
	GTEXT_YAML_Node *root;              /* Root node once complete */
	bool failed;                        /* True if error occurred */
	size_t document_count;              /* Number of documents seen (0-based) */
	bool document_started;              /* True if inside a document */
	bool first_document_complete;       /* True when first document is done */
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
	p->stack.temps = (saved_temp *)calloc(p->stack.capacity, sizeof(saved_temp));
	
	if (!p->stack.nodes || !p->stack.states || !p->stack.temps) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.temps);
		return false;
	}
	
	/* Allocate temporary storage for collection children */
	p->temp.capacity = 16;
	p->temp.items = (GTEXT_YAML_Node **)malloc(p->temp.capacity * sizeof(GTEXT_YAML_Node *));
	if (!p->temp.items) {
		free(p->stack.nodes);
		free(p->stack.states);
		free(p->stack.temps);
		return false;
	}
	
	/* Allocate anchor map */
	p->anchors.capacity = 16;
	p->anchors.entries = (anchor_entry *)calloc(p->anchors.capacity, sizeof(anchor_entry));
	if (!p->anchors.entries) {
		free(p->stack.nodes);
		free(p->stack.states);
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
		free(p->stack.temps);
		free(p->temp.items);
		free(p->anchors.entries);
		return false;
	}
	
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
	free(p->stack.temps);
	free(p->temp.items);
	
	/* Free anchor map */
	for (size_t i = 0; i < p->anchors.count; i++) {
		free(p->anchors.entries[i].name);
	}
	free(p->anchors.entries);
	
	/* Free alias list */
	free(p->aliases.nodes);
}

/**
 * @brief Register an anchor name with its node.
 */
static bool register_anchor(parser_state *p, const char *name, GTEXT_YAML_Node *node) {
	if (!name || !node) return true;  /* No anchor to register */
	
	/* Check if we need to grow the anchor map */
	if (p->anchors.count >= p->anchors.capacity) {
		size_t new_cap = p->anchors.capacity * 2;
		anchor_entry *new_entries = (anchor_entry *)realloc(
			p->anchors.entries, new_cap * sizeof(anchor_entry)
		);
		if (!new_entries) return false;
		memset(new_entries + p->anchors.capacity, 0, (new_cap - p->anchors.capacity) * sizeof(anchor_entry));
		p->anchors.entries = new_entries;
		p->anchors.capacity = new_cap;
	}
	
	/* Add the anchor */
	p->anchors.entries[p->anchors.count].name = strdup(name);
	p->anchors.entries[p->anchors.count].node = node;
	if (!p->anchors.entries[p->anchors.count].name) return false;
	p->anchors.count++;
	
	return true;
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

/**
 * @brief Resolve all alias nodes.
 */
static GTEXT_YAML_Status resolve_aliases(parser_state *p) {
	for (size_t i = 0; i < p->aliases.count; i++) {
		GTEXT_YAML_Node *alias = p->aliases.nodes[i];
		if (alias->type != GTEXT_YAML_ALIAS) continue;  /* Shouldn't happen */
		
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

/**
 * @brief Push a node onto the stack (for tracking nesting).
 * Saves the current temp state and clears temp for the new level.
 */
static bool stack_push(parser_state *p, GTEXT_YAML_Node *node, int state, const char *anchor, const char *tag) {
	if (p->stack.depth >= p->stack.capacity) {
		/* Grow stack */
		size_t new_cap = p->stack.capacity * 2;
		GTEXT_YAML_Node **new_nodes = (GTEXT_YAML_Node **)realloc(
			p->stack.nodes, new_cap * sizeof(GTEXT_YAML_Node *)
		);
		int *new_states = (int *)realloc(p->stack.states, new_cap * sizeof(int));
		saved_temp *new_temps = (saved_temp *)realloc(p->stack.temps, new_cap * sizeof(saved_temp));
		
		if (!new_nodes || !new_states || !new_temps) {
			free(new_nodes);
			free(new_states);
			free(new_temps);
			return false;
		}
		
		/* Zero-initialize new slots */
		memset(new_temps + p->stack.capacity, 0, (new_cap - p->stack.capacity) * sizeof(saved_temp));
		
		p->stack.nodes = new_nodes;
		p->stack.states = new_states;
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

/**
 * @brief Clear temporary storage.
 */
static void temp_clear(parser_state *p) {
	p->temp.count = 0;
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
				if (!register_anchor(p, event->anchor, node)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory registering anchor";
					}
					return GTEXT_YAML_E_OOM;
				}
			}
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				p->root = node;
			} else {
				if (!temp_add(p, node)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory adding child node";
					}
					return GTEXT_YAML_E_OOM;
				}
			}
			break;
		}
		
		case GTEXT_YAML_EVENT_SEQUENCE_START: {
			/* Start building a sequence - we don't know the size yet */
			const GTEXT_YAML_Event *evt = (const GTEXT_YAML_Event *)event;
			
			/* Push placeholder (we'll create the actual node on SEQUENCE_END) */
			/* Store anchor and tag from event for later use */
			if (!stack_push(p, NULL, STATE_SEQUENCE, evt->anchor, evt->tag)) {
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
				register_anchor(p, node->as.sequence.anchor, node);
			}
			
			/* Pop sequence from stack (restores parent temp) */
			stack_pop(p);
			
			/* Add to parent or set as root */
			if (p->stack.depth == 0) {
				p->root = node;
			} else {
				/* Add to parent's temp */
				if (!temp_add(p, node)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory nesting sequence";
					}
					return GTEXT_YAML_E_OOM;
				}
			}
			
			break;
		}
		
		case GTEXT_YAML_EVENT_MAPPING_START: {
			/* Start building a mapping */
			const GTEXT_YAML_Event *evt = (const GTEXT_YAML_Event *)event;
			
			if (!stack_push(p, NULL, STATE_MAPPING_KEY, evt->anchor, evt->tag)) {
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
				register_anchor(p, node->as.mapping.anchor, node);
			}
			
			/* Pop mapping from stack (restores parent temp) */
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
			}
			
			break;
		}
		
		case GTEXT_YAML_EVENT_STREAM_END:
			/* End of stream */
			break;
			
		case GTEXT_YAML_EVENT_DOCUMENT_END:
			/* End of document */
			if (p->document_started && !p->first_document_complete) {
				/* First document is complete */
				p->first_document_complete = true;
				p->document_count = 1;
			}
			break;
			
		case GTEXT_YAML_EVENT_ALIAS: {
			/* Create alias node */
			const char *anchor_name = event->data.alias_name;
			if (!anchor_name) {
				p->failed = true;
				if (p->error) {
					p->error->code = GTEXT_YAML_E_INVALID;
					p->error->message = "Alias event missing anchor name";
				}
				return GTEXT_YAML_E_INVALID;
			}
			
			GTEXT_YAML_Node *node = yaml_node_new_alias(p->ctx, anchor_name);
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
				if (!temp_add(p, node)) {
					p->failed = true;
					if (p->error) {
						p->error->code = GTEXT_YAML_E_OOM;
						p->error->message = "Out of memory adding alias node";
					}
					return GTEXT_YAML_E_OOM;
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
					if (!stack_push(p, NULL, STATE_SEQUENCE, NULL, NULL)) {
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
					}
					break;
				}
				
				case '{':
					/* Start flow mapping (fallback if START event not emitted) */
					if (!stack_push(p, NULL, STATE_MAPPING_KEY, NULL, NULL)) {
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
					}
					break;
				}
				
				case ':':
					/* Mapping key-value separator - flip state */
					if (p->stack.depth > 0 && p->stack.states[p->stack.depth - 1] == STATE_MAPPING_KEY) {
						p->stack.states[p->stack.depth - 1] = STATE_MAPPING_VALUE;
					}
					break;
					
				case ',':
					/* Item separator - handle mapping state flip */
					if (p->stack.depth > 0 && p->stack.states[p->stack.depth - 1] == STATE_MAPPING_VALUE) {
						p->stack.states[p->stack.depth - 1] = STATE_MAPPING_KEY;
					}
					break;
					
				case '-':
					/* Block sequence indicator - TODO: implement block style parsing */
					break;
					
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
	
	/* Use default options if none provided */
	GTEXT_YAML_Parse_Options default_opts = gtext_yaml_parse_options_default();
	if (!options) options = &default_opts;
	
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
	doc->options = *options;
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
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(options, parse_callback, &parser);
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
	
	parser_free(&parser);
	return doc;
}
