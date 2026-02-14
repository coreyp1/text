/**
 * @file
 *
 * YAML writer infrastructure implementation.
 *
 * This file implements the sink abstraction for writing YAML output
 * to various destinations.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/yaml/yaml_writer.h>
#include "yaml_internal.h"

static int buffer_write_fn(void * user, const char * bytes, size_t len) {
  GTEXT_YAML_Buffer_Sink * buf = (GTEXT_YAML_Buffer_Sink *)user;
  if (!buf || !bytes) {
    return 1;
  }

  if (len > SIZE_MAX - buf->used || buf->used > SIZE_MAX - len - 1) {
    return 1;
  }

  size_t needed = buf->used + len + 1;
  if (needed > buf->size) {
    size_t new_size = buf->size;
    if (new_size == 0) {
      new_size = 256;
    }
    while (new_size < needed) {
      if (new_size > SIZE_MAX / 2) {
        return 1;
      }
      new_size *= 2;
    }

    char * new_data = (char *)realloc(buf->data, new_size);
    if (!new_data) {
      return 1;
    }
    buf->data = new_data;
    buf->size = new_size;
  }

  if (buf->used + len > buf->size - 1) {
    return 1;
  }

  memcpy(buf->data + buf->used, bytes, len);
  buf->used += len;
  if (buf->used < buf->size) {
    buf->data[buf->used] = '\0';
  }

  return 0;
}

static int fixed_buffer_write_fn(void * user, const char * bytes, size_t len) {
  GTEXT_YAML_Fixed_Buffer_Sink * buf = (GTEXT_YAML_Fixed_Buffer_Sink *)user;
  if (!buf || !bytes) {
    return 1;
  }

  size_t available = 0;
  if (buf->size > buf->used) {
    if (buf->size - buf->used > 1) {
      available = buf->size - buf->used - 1;
    }
  }

  size_t to_write = len;
  int truncated = 0;

  if (to_write > available) {
    to_write = available;
    truncated = 1;
    buf->truncated = true;
  }

  if (to_write > 0 && available > 0) {
    if (buf->used <= buf->size - 1 && to_write <= buf->size - 1 - buf->used) {
      memcpy(buf->data + buf->used, bytes, to_write);
      buf->used += to_write;
      if (buf->used < buf->size) {
        buf->data[buf->used] = '\0';
      }
    } else {
      truncated = 1;
      buf->truncated = true;
    }
  }

  return truncated ? 1 : 0;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_sink_buffer(GTEXT_YAML_Sink * sink) {
  if (!sink) {
    return GTEXT_YAML_E_INVALID;
  }

  GTEXT_YAML_Buffer_Sink * buf =
      (GTEXT_YAML_Buffer_Sink *)malloc(sizeof(GTEXT_YAML_Buffer_Sink));
  if (!buf) {
    return GTEXT_YAML_E_OOM;
  }

  buf->data = NULL;
  buf->size = 0;
  buf->used = 0;

  sink->write = buffer_write_fn;
  sink->user = buf;

  return GTEXT_YAML_OK;
}

GTEXT_API const char * gtext_yaml_sink_buffer_data(
    const GTEXT_YAML_Sink * sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return NULL;
  }

  GTEXT_YAML_Buffer_Sink * buf = (GTEXT_YAML_Buffer_Sink *)sink->user;
  if (!buf) {
    return NULL;
  }

  return buf->data ? buf->data : "";
}

GTEXT_API size_t gtext_yaml_sink_buffer_size(const GTEXT_YAML_Sink * sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return 0;
  }

  GTEXT_YAML_Buffer_Sink * buf = (GTEXT_YAML_Buffer_Sink *)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

GTEXT_API void gtext_yaml_sink_buffer_free(GTEXT_YAML_Sink * sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return;
  }

  GTEXT_YAML_Buffer_Sink * buf = (GTEXT_YAML_Buffer_Sink *)sink->user;
  if (buf) {
    free(buf->data);
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_sink_fixed_buffer(
    GTEXT_YAML_Sink * sink, char * buffer, size_t size) {
  if (!sink || !buffer || size == 0) {
    return GTEXT_YAML_E_INVALID;
  }

  GTEXT_YAML_Fixed_Buffer_Sink * buf = (GTEXT_YAML_Fixed_Buffer_Sink *)malloc(
      sizeof(GTEXT_YAML_Fixed_Buffer_Sink));
  if (!buf) {
    return GTEXT_YAML_E_OOM;
  }

  buf->data = buffer;
  buf->size = size;
  buf->used = 0;
  buf->truncated = false;

  if (size > 0) {
    buf->data[0] = '\0';
  }

  sink->write = fixed_buffer_write_fn;
  sink->user = buf;

  return GTEXT_YAML_OK;
}

GTEXT_API size_t gtext_yaml_sink_fixed_buffer_used(
    const GTEXT_YAML_Sink * sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return 0;
  }

  GTEXT_YAML_Fixed_Buffer_Sink * buf = (GTEXT_YAML_Fixed_Buffer_Sink *)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

GTEXT_API bool gtext_yaml_sink_fixed_buffer_truncated(
    const GTEXT_YAML_Sink * sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return false;
  }

  GTEXT_YAML_Fixed_Buffer_Sink * buf = (GTEXT_YAML_Fixed_Buffer_Sink *)sink->user;
  if (!buf) {
    return false;
  }

  return buf->truncated;
}

GTEXT_API void gtext_yaml_sink_fixed_buffer_free(GTEXT_YAML_Sink * sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return;
  }

  GTEXT_YAML_Fixed_Buffer_Sink * buf = (GTEXT_YAML_Fixed_Buffer_Sink *)sink->user;
  if (buf) {
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

typedef struct {
  GTEXT_YAML_Sink * sink;
  const GTEXT_YAML_Write_Options * opts;
} yaml_writer_state;

static GTEXT_YAML_Status write_bytes(
    yaml_writer_state * state, const char * bytes, size_t len) {
  if (!state || !state->sink || !state->sink->write) {
    return GTEXT_YAML_E_INVALID;
  }
  return state->sink->write(state->sink->user, bytes, len) == 0
      ? GTEXT_YAML_OK
      : GTEXT_YAML_E_WRITE;
}

static GTEXT_YAML_Status write_str(
    yaml_writer_state * state, const char * str) {
  if (!str) {
    return GTEXT_YAML_E_INVALID;
  }
  return write_bytes(state, str, strlen(str));
}

static const char *writer_newline(const GTEXT_YAML_Write_Options * opts) {
  if (!opts || !opts->newline) {
    return "\n";
  }
  return opts->newline;
}

static GTEXT_YAML_Status write_indent(
    yaml_writer_state * state, size_t spaces) {
  char pad[64];
  size_t chunk = sizeof(pad);
  memset(pad, ' ', sizeof(pad));
  while (spaces > 0) {
    size_t to_write = spaces > chunk ? chunk : spaces;
    GTEXT_YAML_Status status = write_bytes(state, pad, to_write);
    if (status != GTEXT_YAML_OK) {
      return status;
    }
    spaces -= to_write;
  }
  return GTEXT_YAML_OK;
}

static const char *node_tag(const GTEXT_YAML_Node *node) {
  if (!node) return NULL;
  switch (node->type) {
    case GTEXT_YAML_STRING:
      return node->as.scalar.tag;
    case GTEXT_YAML_SEQUENCE:
      return node->as.sequence.tag;
    case GTEXT_YAML_MAPPING:
      return node->as.mapping.tag;
    default:
      return NULL;
  }
}

static const char *node_anchor(const GTEXT_YAML_Node *node) {
  if (!node) return NULL;
  switch (node->type) {
    case GTEXT_YAML_STRING:
      return node->as.scalar.anchor;
    case GTEXT_YAML_SEQUENCE:
      return node->as.sequence.anchor;
    case GTEXT_YAML_MAPPING:
      return node->as.mapping.anchor;
    default:
      return NULL;
  }
}

static const char *default_tag_for_type(GTEXT_YAML_Node_Type type) {
  switch (type) {
    case GTEXT_YAML_STRING:
      return "!!str";
    case GTEXT_YAML_SEQUENCE:
      return "!!seq";
    case GTEXT_YAML_MAPPING:
      return "!!map";
    default:
      return NULL;
  }
}

static bool scalar_needs_quotes(const char *value) {
  if (!value || value[0] == '\0') return true;
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    unsigned char c = *p;
    if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
      return true;
    }
  }
  return false;
}

static GTEXT_YAML_Status write_escaped_scalar(
    yaml_writer_state * state, const char * value) {
  static const char hex[] = "0123456789ABCDEF";
  GTEXT_YAML_Status status = write_str(state, "\"");
  if (status != GTEXT_YAML_OK) return status;

  if (!value) value = "";
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    unsigned char c = *p;
    switch (c) {
      case '\\':
        status = write_str(state, "\\\\");
        break;
      case '"':
        status = write_str(state, "\\\"");
        break;
      case '\n':
        status = write_str(state, "\\n");
        break;
      case '\r':
        status = write_str(state, "\\r");
        break;
      case '\t':
        status = write_str(state, "\\t");
        break;
      default:
        if (c < 0x20) {
          char buf[6];
          buf[0] = '\\';
          buf[1] = 'u';
          buf[2] = '0';
          buf[3] = '0';
          buf[4] = hex[(c >> 4) & 0x0F];
          buf[5] = hex[c & 0x0F];
          status = write_bytes(state, buf, sizeof(buf));
        } else {
          status = write_bytes(state, (const char *)&c, 1);
        }
        break;
    }
    if (status != GTEXT_YAML_OK) return status;
  }

  return write_str(state, "\"");
}

static GTEXT_YAML_Status write_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
  const char * tag_override,
  bool leading_newline);

static GTEXT_YAML_Status write_node_prefix(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override) {
  const char *anchor = node_anchor(node);
  const char *tag = tag_override ? tag_override : node_tag(node);
  bool canonical = state->opts && state->opts->canonical;

  if (!tag && canonical) {
    tag = default_tag_for_type(node->type);
  }

  if (anchor) {
    GTEXT_YAML_Status status = write_str(state, "&");
    if (status != GTEXT_YAML_OK) return status;
    status = write_str(state, anchor);
    if (status != GTEXT_YAML_OK) return status;
  }

  if (tag) {
    GTEXT_YAML_Status status = GTEXT_YAML_OK;
    if (anchor) {
      status = write_str(state, " ");
      if (status != GTEXT_YAML_OK) return status;
    }
    status = write_str(state, tag);
    if (status != GTEXT_YAML_OK) return status;
  }

  if (anchor || tag) {
    return write_str(state, " ");
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_scalar_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override) {
  GTEXT_YAML_Status status = write_node_prefix(state, node, tag_override);
  if (status != GTEXT_YAML_OK) return status;

  const char *value = node->as.scalar.value;
  bool canonical = state->opts && state->opts->canonical;
  if (canonical || scalar_needs_quotes(value)) {
    return write_escaped_scalar(state, value);
  }

  if (!value) value = "";
  return write_str(state, value);
}

static GTEXT_YAML_Status write_sequence_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
  const char * tag_override,
  bool leading_newline) {
  GTEXT_YAML_Status status = GTEXT_YAML_OK;
  bool pretty = state->opts ? state->opts->pretty : false;

  if (!flow && (node_anchor(node) || node_tag(node) ||
      (state->opts && state->opts->canonical))) {
    flow = true;
  }

  status = write_node_prefix(state, node, tag_override);
  if (status != GTEXT_YAML_OK) return status;

  if (flow || !pretty) {
    status = write_str(state, "[");
    if (status != GTEXT_YAML_OK) return status;
    for (size_t i = 0; i < node->as.sequence.count; i++) {
      if (i > 0) {
        status = write_str(state, ", ");
        if (status != GTEXT_YAML_OK) return status;
      }
      status = write_node(
          state,
          node->as.sequence.children[i],
          indent,
          true,
          NULL,
          false
      );
      if (status != GTEXT_YAML_OK) return status;
    }
    return write_str(state, "]");
  }

  if (node->as.sequence.count == 0) {
    return write_str(state, "[]");
  }

  for (size_t i = 0; i < node->as.sequence.count; i++) {
    if (i > 0 || leading_newline) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
    }
    if (status != GTEXT_YAML_OK) return status;
    status = write_indent(state, indent);
    if (status != GTEXT_YAML_OK) return status;
    status = write_str(state, "-");
    if (status != GTEXT_YAML_OK) return status;

    const GTEXT_YAML_Node *child = node->as.sequence.children[i];
    if (child && (child->type == GTEXT_YAML_SEQUENCE || child->type == GTEXT_YAML_MAPPING)) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
      status = write_node(
          state,
          child,
          indent + state->opts->indent_spaces,
          false,
          NULL,
          false
      );
    } else {
      status = write_str(state, " ");
      if (status != GTEXT_YAML_OK) return status;
      status = write_node(
          state,
          child,
          indent + state->opts->indent_spaces,
          true,
          NULL,
          false
      );
    }
    if (status != GTEXT_YAML_OK) return status;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_mapping_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
  const char * tag_override,
  bool leading_newline) {
  GTEXT_YAML_Status status = GTEXT_YAML_OK;
  bool pretty = state->opts ? state->opts->pretty : false;

  if (!flow && (node_anchor(node) || node_tag(node) ||
      (state->opts && state->opts->canonical))) {
    flow = true;
  }

  status = write_node_prefix(state, node, tag_override);
  if (status != GTEXT_YAML_OK) return status;

  if (flow || !pretty) {
    status = write_str(state, "{");
    if (status != GTEXT_YAML_OK) return status;
    for (size_t i = 0; i < node->as.mapping.count; i++) {
      if (i > 0) {
        status = write_str(state, ", ");
        if (status != GTEXT_YAML_OK) return status;
      }
      status = write_node(
          state,
          node->as.mapping.pairs[i].key,
          indent,
          true,
          node->as.mapping.pairs[i].key_tag,
          false
      );
      if (status != GTEXT_YAML_OK) return status;
      status = write_str(state, ": ");
      if (status != GTEXT_YAML_OK) return status;
      status = write_node(
          state,
          node->as.mapping.pairs[i].value,
          indent,
          true,
          node->as.mapping.pairs[i].value_tag,
          false
      );
      if (status != GTEXT_YAML_OK) return status;
    }
    return write_str(state, "}");
  }

  if (node->as.mapping.count == 0) {
    return write_str(state, "{}");
  }

  for (size_t i = 0; i < node->as.mapping.count; i++) {
    if (i > 0 || leading_newline) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
    }
    if (status != GTEXT_YAML_OK) return status;
    status = write_indent(state, indent);
    if (status != GTEXT_YAML_OK) return status;
    status = write_node(
        state,
        node->as.mapping.pairs[i].key,
        indent,
        true,
        node->as.mapping.pairs[i].key_tag,
        false
    );
    if (status != GTEXT_YAML_OK) return status;
    status = write_str(state, ":");
    if (status != GTEXT_YAML_OK) return status;

    const GTEXT_YAML_Node *value = node->as.mapping.pairs[i].value;
    if (value && (value->type == GTEXT_YAML_SEQUENCE || value->type == GTEXT_YAML_MAPPING)) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
      status = write_node(
          state,
          value,
          indent + state->opts->indent_spaces,
          false,
          node->as.mapping.pairs[i].value_tag,
          false
      );
    } else {
      status = write_str(state, " ");
      if (status != GTEXT_YAML_OK) return status;
      status = write_node(
          state,
          value,
          indent + state->opts->indent_spaces,
          true,
          node->as.mapping.pairs[i].value_tag,
          false
      );
    }
    if (status != GTEXT_YAML_OK) return status;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_alias_node(
    yaml_writer_state * state, const GTEXT_YAML_Node * node) {
  const char *name = node->as.alias.anchor_name;
  if (!name && node->as.alias.target) {
    name = node_anchor(node->as.alias.target);
  }
  if (!name) {
    return GTEXT_YAML_E_INVALID;
  }

  GTEXT_YAML_Status status = write_str(state, "*");
  if (status != GTEXT_YAML_OK) return status;
  return write_str(state, name);
}

static GTEXT_YAML_Status write_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
    const char * tag_override,
    bool leading_newline) {
  if (!node) {
    return GTEXT_YAML_E_INVALID;
  }

  switch (node->type) {
    case GTEXT_YAML_STRING:
      return write_scalar_node(state, node, tag_override);
    case GTEXT_YAML_SEQUENCE:
      return write_sequence_node(state, node, indent, flow, tag_override, leading_newline);
    case GTEXT_YAML_MAPPING:
      return write_mapping_node(state, node, indent, flow, tag_override, leading_newline);
    case GTEXT_YAML_ALIAS:
      return write_alias_node(state, node);
    default:
      return GTEXT_YAML_E_INVALID;
  }
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_write_document(
    const GTEXT_YAML_Document * doc,
    GTEXT_YAML_Sink * sink,
    const GTEXT_YAML_Write_Options * opts) {
  GTEXT_YAML_Write_Options defaults = gtext_yaml_write_options_default();
  yaml_writer_state state;
  GTEXT_YAML_Status status = GTEXT_YAML_OK;
  const GTEXT_YAML_Node *root = NULL;

  if (!doc || !sink || !sink->write) {
    return GTEXT_YAML_E_INVALID;
  }

  if (!opts) {
    opts = &defaults;
  }

  state.sink = sink;
  state.opts = opts;
  root = doc->root;

  if (!root) {
    return GTEXT_YAML_OK;
  }

  status = write_node(&state, root, 0, !opts->pretty, NULL, false);
  if (status != GTEXT_YAML_OK) {
    return status;
  }

  if (opts->trailing_newline) {
    status = write_str(&state, writer_newline(opts));
  }

  return status;
}

// ============================================================================
// Streaming Writer (event -> YAML)
// ============================================================================

typedef enum {
  YAML_WRITER_STACK_SEQUENCE,
  YAML_WRITER_STACK_MAPPING
} yaml_writer_stack_type;

typedef struct {
  yaml_writer_stack_type type;
  bool has_items;
  bool expecting_key;
  bool is_map_key;
} yaml_writer_stack_entry;

struct GTEXT_YAML_Writer {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Write_Options opts;
  yaml_writer_stack_entry *stack;
  size_t stack_size;
  size_t stack_capacity;
  bool in_document;
  bool wrote_doc;
  bool error;
};

#define YAML_WRITER_DEFAULT_STACK_CAPACITY 32

static int writer_stack_grow(GTEXT_YAML_Writer *writer) {
  if (writer->stack_size < writer->stack_capacity) {
    return 0;
  }
  size_t new_capacity = writer->stack_capacity == 0
      ? YAML_WRITER_DEFAULT_STACK_CAPACITY
      : writer->stack_capacity * 2;
  if (new_capacity < writer->stack_capacity) {
    return 1;
  }
  if (new_capacity > 1024 * 1024) {
    return 1;
  }
  size_t entry_size = sizeof(yaml_writer_stack_entry);
  if (entry_size > 0 && new_capacity > SIZE_MAX / entry_size) {
    return 1;
  }
  yaml_writer_stack_entry *new_stack = (yaml_writer_stack_entry *)realloc(
      writer->stack, new_capacity * entry_size);
  if (!new_stack) {
    return 1;
  }
  writer->stack = new_stack;
  writer->stack_capacity = new_capacity;
  return 0;
}

static yaml_writer_stack_entry *writer_stack_top(GTEXT_YAML_Writer *writer) {
  if (!writer || writer->stack_size == 0) {
    return NULL;
  }
  return &writer->stack[writer->stack_size - 1];
}

static int writer_stack_push(
    GTEXT_YAML_Writer *writer,
    yaml_writer_stack_type type,
    bool is_map_key) {
  if (writer_stack_grow(writer) != 0) {
    return 1;
  }
  yaml_writer_stack_entry entry = {
      .type = type,
      .has_items = false,
      .expecting_key = (type == YAML_WRITER_STACK_MAPPING),
      .is_map_key = is_map_key};
  writer->stack[writer->stack_size++] = entry;
  return 0;
}

static int writer_stack_pop(GTEXT_YAML_Writer *writer,
    yaml_writer_stack_entry *out_entry) {
  if (!writer || writer->stack_size == 0) {
    return 1;
  }
  writer->stack_size--;
  if (out_entry) {
    *out_entry = writer->stack[writer->stack_size];
  }
  return 0;
}

static int writer_write_bytes(GTEXT_YAML_Writer *writer,
    const char *bytes, size_t len) {
  if (!writer || !writer->sink.write || !bytes) {
    return 1;
  }
  int result = writer->sink.write(writer->sink.user, bytes, len);
  if (result != 0) {
    writer->error = true;
  }
  return result;
}

static int writer_write_char(GTEXT_YAML_Writer *writer, char c) {
  return writer_write_bytes(writer, &c, 1);
}

static int writer_write_string(GTEXT_YAML_Writer *writer, const char *str) {
  if (!str) {
    return 0;
  }
  return writer_write_bytes(writer, str, strlen(str));
}

static int writer_write_prefix(
    GTEXT_YAML_Writer *writer,
    const char *anchor,
    const char *tag,
    GTEXT_YAML_Node_Type type) {
  if (writer->opts.canonical && !tag) {
    tag = default_tag_for_type(type);
  }
  if (anchor) {
    if (writer_write_char(writer, '&') != 0) return 1;
    if (writer_write_string(writer, anchor) != 0) return 1;
  }
  if (tag) {
    if (anchor) {
      if (writer_write_char(writer, ' ') != 0) return 1;
    }
    if (writer_write_string(writer, tag) != 0) return 1;
  }
  if (anchor || tag) {
    if (writer_write_char(writer, ' ') != 0) return 1;
  }
  return 0;
}

static int writer_write_escaped(
    GTEXT_YAML_Writer *writer,
    const char *value,
    size_t len) {
  static const char hex[] = "0123456789ABCDEF";
  if (writer_write_char(writer, '"') != 0) return 1;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    switch (c) {
      case '\\':
        if (writer_write_string(writer, "\\\\") != 0) return 1;
        break;
      case '"':
        if (writer_write_string(writer, "\\\"") != 0) return 1;
        break;
      case '\n':
        if (writer_write_string(writer, "\\n") != 0) return 1;
        break;
      case '\r':
        if (writer_write_string(writer, "\\r") != 0) return 1;
        break;
      case '\t':
        if (writer_write_string(writer, "\\t") != 0) return 1;
        break;
      default:
        if (c < 0x20) {
          char buf[6];
          buf[0] = '\\';
          buf[1] = 'u';
          buf[2] = '0';
          buf[3] = '0';
          buf[4] = hex[(c >> 4) & 0x0F];
          buf[5] = hex[c & 0x0F];
          if (writer_write_bytes(writer, buf, sizeof(buf)) != 0) return 1;
        } else {
          if (writer_write_bytes(writer, (const char *)&c, 1) != 0) return 1;
        }
        break;
    }
  }
  return writer_write_char(writer, '"');
}

static bool writer_scalar_needs_quotes(const char *value, size_t len) {
  if (!value || len == 0) return true;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
      return true;
    }
  }
  return false;
}

static int writer_prepare_value(GTEXT_YAML_Writer *writer, bool *is_key) {
  yaml_writer_stack_entry *top = writer_stack_top(writer);
  if (!top) {
    if (is_key) *is_key = false;
    return 0;
  }
  if (top->type == YAML_WRITER_STACK_SEQUENCE) {
    if (top->has_items) {
      if (writer_write_string(writer, ", ") != 0) return 1;
    }
    if (is_key) *is_key = false;
    return 0;
  }
  if (top->type == YAML_WRITER_STACK_MAPPING) {
    if (top->expecting_key) {
      if (top->has_items) {
        if (writer_write_string(writer, ", ") != 0) return 1;
      }
      if (is_key) *is_key = true;
      return 0;
    }
    if (writer_write_string(writer, ": ") != 0) return 1;
    if (is_key) *is_key = false;
    return 0;
  }
  return 1;
}

static void writer_finish_value(GTEXT_YAML_Writer *writer, bool is_key) {
  yaml_writer_stack_entry *top = writer_stack_top(writer);
  if (!top) {
    return;
  }
  if (top->type == YAML_WRITER_STACK_SEQUENCE) {
    top->has_items = true;
    return;
  }
  if (top->type == YAML_WRITER_STACK_MAPPING) {
    if (is_key) {
      top->expecting_key = false;
    } else {
      top->expecting_key = true;
      top->has_items = true;
    }
  }
}

static GTEXT_YAML_Status writer_emit_scalar(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event) {
  bool is_key = false;
  if (writer_prepare_value(writer, &is_key) != 0) {
    return GTEXT_YAML_E_STATE;
  }

  if (writer_write_prefix(writer, event->anchor, event->tag, GTEXT_YAML_STRING) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  const char *value = event->data.scalar.ptr ? event->data.scalar.ptr : "";
  size_t len = event->data.scalar.len;
  if (writer->opts.canonical || writer_scalar_needs_quotes(value, len)) {
    if (writer_write_escaped(writer, value, len) != 0) {
      return GTEXT_YAML_E_WRITE;
    }
  } else {
    if (writer_write_bytes(writer, value, len) != 0) {
      return GTEXT_YAML_E_WRITE;
    }
  }

  writer_finish_value(writer, is_key);
  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status writer_emit_alias(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event) {
  bool is_key = false;
  if (writer_prepare_value(writer, &is_key) != 0) {
    return GTEXT_YAML_E_STATE;
  }

  const char *name = event->data.alias_name;
  if (!name) {
    return GTEXT_YAML_E_INVALID;
  }
  if (writer_write_char(writer, '*') != 0) {
    return GTEXT_YAML_E_WRITE;
  }
  if (writer_write_string(writer, name) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  writer_finish_value(writer, is_key);
  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status writer_emit_container_start(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event,
    yaml_writer_stack_type type) {
  bool is_key = false;
  if (writer_prepare_value(writer, &is_key) != 0) {
    return GTEXT_YAML_E_STATE;
  }

  GTEXT_YAML_Node_Type node_type =
      (type == YAML_WRITER_STACK_SEQUENCE) ? GTEXT_YAML_SEQUENCE : GTEXT_YAML_MAPPING;
  if (writer_write_prefix(writer, event->anchor, event->tag, node_type) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  char open_char = (type == YAML_WRITER_STACK_SEQUENCE) ? '[' : '{';
  if (writer_write_char(writer, open_char) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  if (writer_stack_push(writer, type, is_key) != 0) {
    return GTEXT_YAML_E_OOM;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status writer_emit_container_end(
    GTEXT_YAML_Writer *writer,
    yaml_writer_stack_type type) {
  yaml_writer_stack_entry entry;
  if (writer_stack_pop(writer, &entry) != 0) {
    return GTEXT_YAML_E_STATE;
  }
  if (entry.type != type) {
    return GTEXT_YAML_E_STATE;
  }

  char close_char = (type == YAML_WRITER_STACK_SEQUENCE) ? ']' : '}';
  if (writer_write_char(writer, close_char) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  writer_finish_value(writer, entry.is_map_key);
  return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Writer * gtext_yaml_writer_new(
    GTEXT_YAML_Sink sink, const GTEXT_YAML_Write_Options * opts) {
  if (!sink.write) {
    return NULL;
  }

  GTEXT_YAML_Writer *writer = (GTEXT_YAML_Writer *)calloc(1, sizeof(*writer));
  if (!writer) {
    return NULL;
  }

  writer->sink = sink;
  if (opts) {
    writer->opts = *opts;
  } else {
    writer->opts = gtext_yaml_write_options_default();
  }
  writer->stack_capacity = YAML_WRITER_DEFAULT_STACK_CAPACITY;
  writer->stack = (yaml_writer_stack_entry *)calloc(
      writer->stack_capacity, sizeof(yaml_writer_stack_entry));
  if (!writer->stack) {
    free(writer);
    return NULL;
  }

  writer->stack_size = 0;
  writer->in_document = false;
  writer->wrote_doc = false;
  writer->error = false;
  return writer;
}

GTEXT_API void gtext_yaml_writer_free(GTEXT_YAML_Writer *writer) {
  if (!writer) {
    return;
  }
  free(writer->stack);
  free(writer);
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_writer_event(
    GTEXT_YAML_Writer *writer, const GTEXT_YAML_Event *event) {
  if (!writer || !event) {
    return GTEXT_YAML_E_INVALID;
  }
  if (writer->error) {
    return GTEXT_YAML_E_WRITE;
  }

  switch (event->type) {
    case GTEXT_YAML_EVENT_STREAM_START:
      return GTEXT_YAML_OK;
    case GTEXT_YAML_EVENT_STREAM_END:
      return gtext_yaml_writer_finish(writer);
    case GTEXT_YAML_EVENT_DOCUMENT_START: {
      const char *newline = writer_newline(&writer->opts);
      if (writer->in_document) {
        return GTEXT_YAML_E_STATE;
      }
      if (writer->wrote_doc) {
        if (writer_write_string(writer, newline) != 0) return GTEXT_YAML_E_WRITE;
      }
      if (writer_write_string(writer, "---") != 0) return GTEXT_YAML_E_WRITE;
      if (writer_write_string(writer, newline) != 0) return GTEXT_YAML_E_WRITE;
      writer->in_document = true;
      writer->wrote_doc = true;
      return GTEXT_YAML_OK;
    }
    case GTEXT_YAML_EVENT_DOCUMENT_END: {
      const char *newline = writer_newline(&writer->opts);
      if (!writer->in_document) {
        return GTEXT_YAML_E_STATE;
      }
      if (writer_write_string(writer, newline) != 0) return GTEXT_YAML_E_WRITE;
      writer->in_document = false;
      return GTEXT_YAML_OK;
    }
    case GTEXT_YAML_EVENT_SEQUENCE_START:
      return writer_emit_container_start(writer, event, YAML_WRITER_STACK_SEQUENCE);
    case GTEXT_YAML_EVENT_SEQUENCE_END:
      return writer_emit_container_end(writer, YAML_WRITER_STACK_SEQUENCE);
    case GTEXT_YAML_EVENT_MAPPING_START:
      return writer_emit_container_start(writer, event, YAML_WRITER_STACK_MAPPING);
    case GTEXT_YAML_EVENT_MAPPING_END:
      return writer_emit_container_end(writer, YAML_WRITER_STACK_MAPPING);
    case GTEXT_YAML_EVENT_SCALAR:
      return writer_emit_scalar(writer, event);
    case GTEXT_YAML_EVENT_ALIAS:
      return writer_emit_alias(writer, event);
    case GTEXT_YAML_EVENT_INDICATOR:
      return GTEXT_YAML_OK;
    default:
      return GTEXT_YAML_E_INVALID;
  }
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_writer_finish(GTEXT_YAML_Writer *writer) {
  if (!writer) {
    return GTEXT_YAML_E_INVALID;
  }
  if (writer->stack_size != 0) {
    return GTEXT_YAML_E_STATE;
  }
  if (writer->in_document) {
    return GTEXT_YAML_E_STATE;
  }
  return writer->error ? GTEXT_YAML_E_WRITE : GTEXT_YAML_OK;
}
