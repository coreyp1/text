/**
 * @file yaml_parser.c
 * @brief DOM parser - converts SCALAR and INDICATOR events to DOM
 *
 * The streaming parser emits SCALAR and INDICATOR events only (no collection events).
 * We track indicators like [ ] { } , : - to build collections.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "yaml_internal.h"
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
yaml_context *ctx;
GTEXT_YAML_Document *doc;
GTEXT_YAML_Error *error;

/* Stack: each level tracks type and collected items */
struct {
char *types;                    /* '[', '{', or '-' per level */
GTEXT_YAML_Node ***items;       /* Items collected at each level */
size_t *counts, *capacities;    /* Count/capacity per level */
size_t capacity, depth;
} stack;

GTEXT_YAML_Node *pending;           /* Last scalar parsed */
bool expecting_value;               /* In mapping, after ':' */
GTEXT_YAML_Node *key;               /* Mapping key waiting for value */
GTEXT_YAML_Node *root;
bool failed;
} parser_state;

static bool parser_init(parser_state *ps, yaml_context *ctx, GTEXT_YAML_Document *doc, GTEXT_YAML_Error *error) {
memset(ps, 0, sizeof(*ps));
ps->ctx = ctx;
ps->doc = doc;
ps->error = error;
ps->stack.capacity = 16;
ps->stack.types = malloc(ps->stack.capacity);
ps->stack.items = malloc(ps->stack.capacity * sizeof(GTEXT_YAML_Node **));
ps->stack.counts = malloc(ps->stack.capacity * sizeof(size_t));
ps->stack.capacities = malloc(ps->stack.capacity * sizeof(size_t));
if (!ps->stack.types || !ps->stack.items || !ps->stack.counts || !ps->stack.capacities) {
free(ps->stack.types);
free(ps->stack.items);
free(ps->stack.counts);
free(ps->stack.capacities);
return false;
}
return true;
}

static void parser_free(parser_state *ps) {
for (size_t i = 0; i < ps->stack.depth; i++) {
free(ps->stack.items[i]);
}
free(ps->stack.types);
free(ps->stack.items);
free(ps->stack.counts);
free(ps->stack.capacities);
}

static bool stack_push(parser_state *ps, char type) {
if (ps->stack.depth >= ps->stack.capacity) {
size_t new_cap = ps->stack.capacity * 2;
char *new_types = realloc(ps->stack.types, new_cap);
GTEXT_YAML_Node ***new_items = realloc(ps->stack.items, new_cap * sizeof(GTEXT_YAML_Node **));
size_t *new_counts = realloc(ps->stack.counts, new_cap * sizeof(size_t));
size_t *new_capacities = realloc(ps->stack.capacities, new_cap * sizeof(size_t));
if (!new_types || !new_items || !new_counts || !new_capacities) {
return false;
}
ps->stack.types = new_types;
ps->stack.items = new_items;
ps->stack.counts = new_counts;
ps->stack.capacities = new_capacities;
ps->stack.capacity = new_cap;
}
size_t idx = ps->stack.depth++;
ps->stack.types[idx] = type;
ps->stack.counts[idx] = 0;
ps->stack.capacities[idx] = 8;
ps->stack.items[idx] = malloc(8 * sizeof(GTEXT_YAML_Node *));
return ps->stack.items[idx] != NULL;
}

static bool stack_add(parser_state *ps, GTEXT_YAML_Node *node) {
if (ps->stack.depth == 0) {
ps->root = node;
return true;
}
size_t idx = ps->stack.depth - 1;
if (ps->stack.counts[idx] >= ps->stack.capacities[idx]) {
size_t new_cap = ps->stack.capacities[idx] * 2;
GTEXT_YAML_Node **new_items = realloc(ps->stack.items[idx], new_cap * sizeof(GTEXT_YAML_Node *));
if (!new_items) return false;
ps->stack.items[idx] = new_items;
ps->stack.capacities[idx] = new_cap;
}
ps->stack.items[idx][ps->stack.counts[idx]++] = node;
return true;
}

static GTEXT_YAML_Node *stack_pop_sequence(parser_state *ps) {
if (ps->stack.depth == 0) return NULL;
size_t idx = --ps->stack.depth;
size_t count = ps->stack.counts[idx];
GTEXT_YAML_Node **items = ps->stack.items[idx];
GTEXT_YAML_Node *seq = yaml_node_new_sequence(ps->ctx, count, NULL, NULL);
if (seq) {
for (size_t i = 0; i < count; i++) {
seq->as.sequence.children[i] = items[i];
}
}
free(items);
return seq;
}

static GTEXT_YAML_Node *stack_pop_mapping(parser_state *ps) {
if (ps->stack.depth == 0) return NULL;
size_t idx = --ps->stack.depth;
size_t count = ps->stack.counts[idx];
GTEXT_YAML_Node **items = ps->stack.items[idx];
size_t pair_count = count / 2;
GTEXT_YAML_Node *map = yaml_node_new_mapping(ps->ctx, pair_count, NULL, NULL);
if (map) {
for (size_t i = 0; i < pair_count; i++) {
map->as.mapping.pairs[i].key = items[i * 2];
map->as.mapping.pairs[i].value = items[i * 2 + 1];
map->as.mapping.pairs[i].key_tag = NULL;
map->as.mapping.pairs[i].value_tag = NULL;
}
}
free(items);
return map;
}

static GTEXT_YAML_Status parse_callback(GTEXT_YAML_Stream *s, const void *event_payload, void *user) {
(void)s;
parser_state *ps = (parser_state *)user;
const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)event_payload;
if (ps->failed) return GTEXT_YAML_E_INVALID;

switch (event->type) {
case GTEXT_YAML_EVENT_SCALAR: {
GTEXT_YAML_Node *node = yaml_node_new_scalar(ps->ctx, event->data.scalar.ptr, event->data.scalar.len, NULL, NULL);
if (!node) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->pending = node;
if (ps->expecting_value) {
if (!stack_add(ps, ps->key) || !stack_add(ps, node)) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->expecting_value = false;
ps->key = NULL;
ps->pending = NULL;
}
return GTEXT_YAML_OK;
}

case GTEXT_YAML_EVENT_INDICATOR: {
char indicator = event->data.indicator;
switch (indicator) {
case '[':
if (!stack_push(ps, '[')) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->pending = NULL;
break;
case ']':
if (ps->pending) {
if (!stack_add(ps, ps->pending)) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->pending = NULL;
}
{
GTEXT_YAML_Node *seq = stack_pop_sequence(ps);
if (!seq) {
ps->failed = true;
return GTEXT_YAML_E_INVALID;
}
if (ps->stack.depth == 0) {
ps->root = seq;
} else {
ps->pending = seq;
}
}
break;
case '{':
if (!stack_push(ps, '{')) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->pending = NULL;
break;
case '}':
if (ps->pending) {
if (!stack_add(ps, ps->pending)) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->pending = NULL;
}
{
GTEXT_YAML_Node *map = stack_pop_mapping(ps);
if (!map) {
ps->failed = true;
return GTEXT_YAML_E_INVALID;
}
if (ps->stack.depth == 0) {
ps->root = map;
} else {
ps->pending = map;
}
}
break;
case ':':
if (ps->pending) {
ps->key = ps->pending;
ps->pending = NULL;
ps->expecting_value = true;
} else if (ps->stack.depth == 0 || ps->stack.types[ps->stack.depth - 1] != '{') {
if (ps->stack.depth == 0) {
if (!stack_push(ps, '{')) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
}
}
break;
case ',':
if (ps->pending) {
if (!stack_add(ps, ps->pending)) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
ps->pending = NULL;
}
break;
case '-':
if (ps->stack.depth == 0 || ps->stack.types[ps->stack.depth - 1] != '-') {
if (!stack_push(ps, '-')) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
}
break;
default:
break;
}
return GTEXT_YAML_OK;
}

case GTEXT_YAML_EVENT_DOCUMENT_END:
if (ps->pending) {
if (ps->stack.depth == 0) {
ps->root = ps->pending;
} else {
if (!stack_add(ps, ps->pending)) {
ps->failed = true;
return GTEXT_YAML_E_OOM;
}
}
ps->pending = NULL;
}
while (ps->stack.depth > 0) {
char type = ps->stack.types[ps->stack.depth - 1];
GTEXT_YAML_Node *node;
if (type == '[' || type == '-') {
node = stack_pop_sequence(ps);
} else {
node = stack_pop_mapping(ps);
}
if (!node) {
ps->failed = true;
return GTEXT_YAML_E_INVALID;
}
if (ps->stack.depth == 0) {
ps->root = node;
} else {
ps->pending = node;
}
}
break;

case GTEXT_YAML_EVENT_STREAM_START:
case GTEXT_YAML_EVENT_STREAM_END:
case GTEXT_YAML_EVENT_DOCUMENT_START:
case GTEXT_YAML_EVENT_SEQUENCE_START:
case GTEXT_YAML_EVENT_SEQUENCE_END:
case GTEXT_YAML_EVENT_MAPPING_START:
case GTEXT_YAML_EVENT_MAPPING_END:
break;
case GTEXT_YAML_EVENT_ALIAS:
ps->failed = true;
return GTEXT_YAML_E_INVALID;
}
return GTEXT_YAML_OK;
}

GTEXT_YAML_Document *yaml_parse_document(const char *input, size_t length, const GTEXT_YAML_Parse_Options *options, GTEXT_YAML_Error *error) {
yaml_context *ctx = yaml_context_new();
if (!ctx) {
if (error) {
error->code = GTEXT_YAML_E_OOM;
error->message = "Failed to create context";
}
return NULL;
}
GTEXT_YAML_Document *doc = yaml_context_alloc(ctx, sizeof(GTEXT_YAML_Document), _Alignof(GTEXT_YAML_Document));
if (!doc) {
yaml_context_free(ctx);
if (error) {
error->code = GTEXT_YAML_E_OOM;
error->message = "Failed to allocate document";
}
return NULL;
}
memset(doc, 0, sizeof(*doc));
doc->ctx = ctx;
if (options) {
doc->options = *options;
}
parser_state parser;
if (!parser_init(&parser, ctx, doc, error)) {
yaml_context_free(ctx);
if (error) {
error->code = GTEXT_YAML_E_OOM;
error->message = "Failed to init parser";
}
return NULL;
}
GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(options, parse_callback, &parser);
if (!stream) {
parser_free(&parser);
yaml_context_free(ctx);
if (error) {
error->code = GTEXT_YAML_E_OOM;
error->message = "Failed to create stream";
}
return NULL;
}
GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, input, length);
if (status == GTEXT_YAML_OK) {
status = gtext_yaml_stream_finish(stream);
}
gtext_yaml_stream_free(stream);

/* Finalize: if no DOCUMENT_END, handle pending/stack */
if (status == GTEXT_YAML_OK && !parser.failed && parser.pending && !parser.root) {
parser.root = parser.pending;
parser.pending = NULL;
}
if (status == GTEXT_YAML_OK && !parser.failed && !parser.root && parser.stack.depth > 0) {
while (parser.stack.depth > 0) {
char type = parser.stack.types[parser.stack.depth - 1];
GTEXT_YAML_Node *node = (type == '[' || type == '-') ? stack_pop_sequence(&parser) : stack_pop_mapping(&parser);
if (!node) {
parser.failed = true;
break;
}
if (parser.stack.depth == 0) {
parser.root = node;
} else {
parser.pending = node;
}
}
}

parser_free(&parser);
if (status != GTEXT_YAML_OK || parser.failed) {
yaml_context_free(ctx);
if (error && error->code == GTEXT_YAML_OK) {
error->code = status;
error->message = "Parse error";
}
return NULL;
}
doc->root = parser.root;
doc->node_count = 1;
return doc;
}
