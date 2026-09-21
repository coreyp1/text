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

#include <ghoti.io/text/macros.h>
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

  buf->data[0] = '\0';

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
  GTEXT_YAML_Encoding encoding;
  bool emit_bom;
  bool bom_written;
  unsigned char pending_utf8[4];
  size_t pending_utf8_len;
} yaml_encoding_state;

typedef struct {
  GTEXT_YAML_Sink * sink;
  const GTEXT_YAML_Write_Options * opts;
  /* Borrowed: the streaming writer keeps its own encoder across calls and
     hands this state a view of it, so the two writers cannot drift apart on
     the half of a UTF-8 sequence that is still waiting for its other half. */
  yaml_encoding_state * encoding;
  /* 8.1.1.1 measures a block scalar's indentation indicator from the
     indentation of the node that holds it - which is -1 for the root of a
     document, and the container's own indent everywhere else.  The writer
     cannot work it out from the indent it is passed, so each caller says. */
  int block_parent_indent;
  /* 7.2 lets an empty node stand wherever a node is expected, with one
     exception: a flow sequence entry needs properties or content, so "[,]"
     is not a sequence of one empty node.  Everywhere else "a:" is how an
     empty node is spelled, and writing "a: ~" instead put a scalar into the
     document that the document never held. */
  bool empty_scalar_ok;
  /* 6.9.2 and 5.6 stop an anchor name and a tag at a flow indicator and
     nowhere else, so ':' belongs to whichever of them came last: "*b: 1"
     names the anchor "b:", "&a: 1" the anchor "a:", "!!null: 1" the tag
     "!!null:".  A key that ends in a property has to be separated from its
     colon; a key that ends in content does not. */
  bool key_absorbs_colon;
  /* A block scalar writes the line break that terminates its last line,
     because with '+' chomping that break is part of the value.  Whoever
     would have written the separator break next skips it. */
  bool line_terminated;
} yaml_writer_state;

static GTEXT_YAML_Encoding writer_encoding(const GTEXT_YAML_Write_Options *opts) {
  return opts ? opts->encoding : GTEXT_YAML_ENCODING_UTF8;
}

static bool writer_emit_bom(const GTEXT_YAML_Write_Options *opts) {
  return opts ? opts->emit_bom : false;
}

static void writer_encoding_init(
    yaml_encoding_state *state,
    const GTEXT_YAML_Write_Options *opts) {
  if (!state) {
    return;
  }
  state->encoding = writer_encoding(opts);
  state->emit_bom = writer_emit_bom(opts);
  state->bom_written = false;
  state->pending_utf8_len = 0;
}

static int writer_should_emit_bom(const yaml_encoding_state *state) {
  if (!state) {
    return 0;
  }
  if (state->emit_bom) {
    return 1;
  }
  return state->encoding != GTEXT_YAML_ENCODING_UTF8;
}

static int writer_emit_bom_bytes(
    GTEXT_YAML_Sink *sink,
    yaml_encoding_state *state) {
  if (!sink || !sink->write || !state) {
    return 1;
  }
  if (state->bom_written) {
    return 0;
  }
  if (!writer_should_emit_bom(state)) {
    state->bom_written = true;
    return 0;
  }

  const unsigned char *bom = NULL;
  size_t bom_len = 0;
  switch (state->encoding) {
    case GTEXT_YAML_ENCODING_UTF8: {
      static const unsigned char utf8_bom[] = {0xEF, 0xBB, 0xBF};
      bom = utf8_bom;
      bom_len = sizeof(utf8_bom);
      break;
    }
    case GTEXT_YAML_ENCODING_UTF16LE: {
      static const unsigned char utf16le_bom[] = {0xFF, 0xFE};
      bom = utf16le_bom;
      bom_len = sizeof(utf16le_bom);
      break;
    }
    case GTEXT_YAML_ENCODING_UTF16BE: {
      static const unsigned char utf16be_bom[] = {0xFE, 0xFF};
      bom = utf16be_bom;
      bom_len = sizeof(utf16be_bom);
      break;
    }
    case GTEXT_YAML_ENCODING_UTF32LE: {
      static const unsigned char utf32le_bom[] = {0xFF, 0xFE, 0x00, 0x00};
      bom = utf32le_bom;
      bom_len = sizeof(utf32le_bom);
      break;
    }
    case GTEXT_YAML_ENCODING_UTF32BE: {
      static const unsigned char utf32be_bom[] = {0x00, 0x00, 0xFE, 0xFF};
      bom = utf32be_bom;
      bom_len = sizeof(utf32be_bom);
      break;
    }
    default:
      return 1;
  }

  if (sink->write(sink->user, (const char *)bom, bom_len) != 0) {
    return 1;
  }
  state->bom_written = true;
  return 0;
}

static int writer_encode_codepoint(
    GTEXT_YAML_Sink *sink,
    yaml_encoding_state *state,
    uint32_t codepoint) {
  unsigned char out[4];
  size_t out_len = 0;

  if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    return 1;
  }

  switch (state->encoding) {
    case GTEXT_YAML_ENCODING_UTF16LE:
    case GTEXT_YAML_ENCODING_UTF16BE: {
      if (codepoint <= 0xFFFF) {
        uint16_t unit = (uint16_t)codepoint;
        if (state->encoding == GTEXT_YAML_ENCODING_UTF16BE) {
          out[0] = (unsigned char)((unit >> 8) & 0xFF);
          out[1] = (unsigned char)(unit & 0xFF);
        } else {
          out[0] = (unsigned char)(unit & 0xFF);
          out[1] = (unsigned char)((unit >> 8) & 0xFF);
        }
        out_len = 2;
        if (sink->write(sink->user, (const char *)out, out_len) != 0) {
          return 1;
        }
        return 0;
      }

      uint32_t value = codepoint - 0x10000;
      uint16_t high = (uint16_t)(0xD800 | ((value >> 10) & 0x3FF));
      uint16_t low = (uint16_t)(0xDC00 | (value & 0x3FF));
      if (state->encoding == GTEXT_YAML_ENCODING_UTF16BE) {
        out[0] = (unsigned char)((high >> 8) & 0xFF);
        out[1] = (unsigned char)(high & 0xFF);
        out[2] = (unsigned char)((low >> 8) & 0xFF);
        out[3] = (unsigned char)(low & 0xFF);
      } else {
        out[0] = (unsigned char)(high & 0xFF);
        out[1] = (unsigned char)((high >> 8) & 0xFF);
        out[2] = (unsigned char)(low & 0xFF);
        out[3] = (unsigned char)((low >> 8) & 0xFF);
      }
      out_len = 4;
      return sink->write(sink->user, (const char *)out, out_len) == 0 ? 0 : 1;
    }
    case GTEXT_YAML_ENCODING_UTF32LE:
    case GTEXT_YAML_ENCODING_UTF32BE: {
      uint32_t unit = codepoint;
      if (state->encoding == GTEXT_YAML_ENCODING_UTF32BE) {
        out[0] = (unsigned char)((unit >> 24) & 0xFF);
        out[1] = (unsigned char)((unit >> 16) & 0xFF);
        out[2] = (unsigned char)((unit >> 8) & 0xFF);
        out[3] = (unsigned char)(unit & 0xFF);
      } else {
        out[0] = (unsigned char)(unit & 0xFF);
        out[1] = (unsigned char)((unit >> 8) & 0xFF);
        out[2] = (unsigned char)((unit >> 16) & 0xFF);
        out[3] = (unsigned char)((unit >> 24) & 0xFF);
      }
      out_len = 4;
      return sink->write(sink->user, (const char *)out, out_len) == 0 ? 0 : 1;
    }
    default:
      return 1;
  }
}

static int utf8_decode_one(
    const unsigned char *buf,
    size_t len,
    uint32_t *out_codepoint,
    size_t *out_len) {
  if (len == 0) {
    return 0;
  }

  unsigned char c = buf[0];
  if (c < 0x80) {
    *out_codepoint = c;
    *out_len = 1;
    return 1;
  }

  if ((c & 0xE0) == 0xC0) {
    if (len < 2) return 0;
    if ((buf[1] & 0xC0) != 0x80) return -1;
    uint32_t code = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(buf[1] & 0x3F);
    if (code < 0x80) return -1;
    *out_codepoint = code;
    *out_len = 2;
    return 1;
  }

  if ((c & 0xF0) == 0xE0) {
    if (len < 3) return 0;
    if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80) return -1;
    uint32_t code = ((uint32_t)(c & 0x0F) << 12) |
        ((uint32_t)(buf[1] & 0x3F) << 6) |
        (uint32_t)(buf[2] & 0x3F);
    if (code < 0x800) return -1;
    if (code >= 0xD800 && code <= 0xDFFF) return -1;
    *out_codepoint = code;
    *out_len = 3;
    return 1;
  }

  if ((c & 0xF8) == 0xF0) {
    if (len < 4) return 0;
    if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80 ||
        (buf[3] & 0xC0) != 0x80) return -1;
    uint32_t code = ((uint32_t)(c & 0x07) << 18) |
        ((uint32_t)(buf[1] & 0x3F) << 12) |
        ((uint32_t)(buf[2] & 0x3F) << 6) |
        (uint32_t)(buf[3] & 0x3F);
    if (code < 0x10000 || code > 0x10FFFF) return -1;
    *out_codepoint = code;
    *out_len = 4;
    return 1;
  }

  return -1;
}

static GTEXT_YAML_Status write_encoded_bytes(
    GTEXT_YAML_Sink *sink,
    yaml_encoding_state *state,
    const char *bytes,
    size_t len) {
  if (!sink || !sink->write || !bytes || !state) {
    return GTEXT_YAML_E_INVALID;
  }

  if (writer_emit_bom_bytes(sink, state) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  if (state->encoding == GTEXT_YAML_ENCODING_UTF8) {
    return sink->write(sink->user, bytes, len) == 0
        ? GTEXT_YAML_OK
        : GTEXT_YAML_E_WRITE;
  }

  size_t i = 0;
  if (state->pending_utf8_len > 0) {
    while (state->pending_utf8_len < sizeof(state->pending_utf8) && i < len) {
      state->pending_utf8[state->pending_utf8_len++] =
          (unsigned char)bytes[i++];
    }
    uint32_t codepoint = 0;
    size_t used = 0;
    int rc = utf8_decode_one(state->pending_utf8, state->pending_utf8_len,
        &codepoint, &used);
    if (rc < 0) {
      return GTEXT_YAML_E_INVALID;
    }
    if (rc == 1) {
      if (writer_encode_codepoint(sink, state, codepoint) != 0) {
        return GTEXT_YAML_E_WRITE;
      }
      size_t remaining = state->pending_utf8_len - used;
      if (remaining > 0) {
        memmove(state->pending_utf8, state->pending_utf8 + used, remaining);
      }
      state->pending_utf8_len = remaining;
    }
  }

  while (i < len) {
    uint32_t codepoint = 0;
    size_t used = 0;
    int rc = utf8_decode_one((const unsigned char *)bytes + i, len - i,
        &codepoint, &used);
    if (rc < 0) {
      return GTEXT_YAML_E_INVALID;
    }
    if (rc == 0) {
      size_t remaining = len - i;
      if (remaining > sizeof(state->pending_utf8)) {
        return GTEXT_YAML_E_INVALID;
      }
      memcpy(state->pending_utf8, bytes + i, remaining);
      state->pending_utf8_len = remaining;
      break;
    }
    if (writer_encode_codepoint(sink, state, codepoint) != 0) {
      return GTEXT_YAML_E_WRITE;
    }
    i += used;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_bytes(
    yaml_writer_state * state, const char * bytes, size_t len) {
  if (!state || !state->sink || !state->sink->write) {
    return GTEXT_YAML_E_INVALID;
  }
  return write_encoded_bytes(state->sink, state->encoding, bytes, len);
}

static GTEXT_YAML_Status write_str(
    yaml_writer_state * state, const char * str) {
  if (!str) {
    return GTEXT_YAML_E_INVALID;
  }
  return write_bytes(state, str, strlen(str));
}

/* The style the root node starts in.  Block style has to win here, because
   nothing below the root can turn flow back into block. */
static bool writer_root_is_flow(const GTEXT_YAML_Write_Options * opts) {
  if (!opts) return true;
  if (opts->flow_style == GTEXT_YAML_FLOW_STYLE_FLOW) return true;
  if (opts->flow_style == GTEXT_YAML_FLOW_STYLE_BLOCK) return false;
  return !opts->pretty;
}

static const char *writer_newline(const GTEXT_YAML_Write_Options * opts) {
  if (!opts || !opts->newline) {
    return "\n";
  }
  return opts->newline;
}

static int writer_indent_spaces(const GTEXT_YAML_Write_Options * opts) {
  if (!opts || opts->indent_spaces <= 0) {
    return 2;
  }
  return opts->indent_spaces;
}

static int writer_line_width(const GTEXT_YAML_Write_Options * opts) {
  if (!opts || opts->line_width <= 0) {
    return 0;
  }
  return opts->line_width;
}

/* The break that separates one node from the next - unless a block scalar
   has already written it, in which case writing another would leave a blank
   line that '+' chomping would keep as part of the value. */
static GTEXT_YAML_Status write_separator(yaml_writer_state * state) {
  if (state->line_terminated) {
    state->line_terminated = false;
    return GTEXT_YAML_OK;
  }
  return write_str(state, writer_newline(state->opts));
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
    case GTEXT_YAML_BOOL:
    case GTEXT_YAML_INT:
    case GTEXT_YAML_FLOAT:
    case GTEXT_YAML_NULL:
      return node->as.scalar.tag;
    case GTEXT_YAML_SEQUENCE:
    case GTEXT_YAML_OMAP:
    case GTEXT_YAML_PAIRS:
      return node->as.sequence.tag;
    case GTEXT_YAML_MAPPING:
    case GTEXT_YAML_SET:
      return node->as.mapping.tag;
    default:
      return NULL;
  }
}

static const char *node_anchor(const GTEXT_YAML_Node *node) {
  if (!node) return NULL;
  switch (node->type) {
    case GTEXT_YAML_STRING:
    case GTEXT_YAML_BOOL:
    case GTEXT_YAML_INT:
    case GTEXT_YAML_FLOAT:
    case GTEXT_YAML_NULL:
      return node->as.scalar.anchor;
    case GTEXT_YAML_SEQUENCE:
    case GTEXT_YAML_OMAP:
    case GTEXT_YAML_PAIRS:
      return node->as.sequence.anchor;
    case GTEXT_YAML_MAPPING:
    case GTEXT_YAML_SET:
      return node->as.mapping.anchor;
    default:
      return NULL;
  }
}

static bool node_is_sequence_type(const GTEXT_YAML_Node *node) {
  if (!node) return false;
  return node->type == GTEXT_YAML_SEQUENCE ||
      node->type == GTEXT_YAML_OMAP ||
      node->type == GTEXT_YAML_PAIRS;
}

static bool node_is_mapping_type(const GTEXT_YAML_Node *node) {
  if (!node) return false;
  return node->type == GTEXT_YAML_MAPPING ||
      node->type == GTEXT_YAML_SET;
}

/* "key:" followed by a block collection puts the collection on the next
   line - but an empty one is written "[]" or "{}" with no indent of its own,
   which lands in column zero and ends the mapping.  An empty collection is a
   value like any other and belongs on the line it was introduced on. */
static bool node_scalar_block_style(const GTEXT_YAML_Node *node) {
  if (!node) return false;
  switch (node->type) {
    case GTEXT_YAML_STRING:
    case GTEXT_YAML_BOOL:
    case GTEXT_YAML_INT:
    case GTEXT_YAML_FLOAT:
    case GTEXT_YAML_NULL:
      return node->as.scalar.scalar_style == GTEXT_YAML_SCALAR_STYLE_LITERAL ||
        node->as.scalar.scalar_style == GTEXT_YAML_SCALAR_STYLE_FOLDED;
    default:
      return false;
  }
}

static const char *default_tag_for_type(GTEXT_YAML_Node_Type type) {
  switch (type) {
    case GTEXT_YAML_STRING:
      return "!!str";
    case GTEXT_YAML_NULL:
      return "!!null";
    case GTEXT_YAML_BOOL:
      return "!!bool";
    case GTEXT_YAML_INT:
      return "!!int";
    case GTEXT_YAML_FLOAT:
      return "!!float";
    case GTEXT_YAML_SEQUENCE:
      return "!!seq";
    case GTEXT_YAML_MAPPING:
      return "!!map";
    case GTEXT_YAML_SET:
      return "!!set";
    case GTEXT_YAML_OMAP:
      return "!!omap";
    case GTEXT_YAML_PAIRS:
      return "!!pairs";
    default:
      return NULL;
  }
}

static bool node_requires_tag(GTEXT_YAML_Node_Type type) {
  return type == GTEXT_YAML_SET ||
      type == GTEXT_YAML_OMAP ||
      type == GTEXT_YAML_PAIRS;
}

static bool tag_is_binary(const char *tag) {
  static const char yaml_prefix[] = "tag:yaml.org,2002:";
  if (!tag) return false;
  if (strcmp(tag, "!!binary") == 0) return true;
  if (strncmp(tag, yaml_prefix, sizeof(yaml_prefix) - 1) == 0) {
    return strcmp(tag + (sizeof(yaml_prefix) - 1), "binary") == 0;
  }
  return false;
}

/* A tag reaches the writer in one of two spellings: the shorthand the parser
   never had to expand ("!local", "!!str"), or the URI a %TAG directive
   resolved it to.  The second kind used to go out as bare text, so
   "tag:example.com,2000:app/foo" left the writer looking like a plain scalar -
   and came back as a mapping key, or as two nodes either side of the comma.
   5.6 gives three spellings and every tag has to go out as one of them. */
static bool tag_char_is_safe(unsigned char c, bool shorthand) {
  if (isalnum(c) || c == '-' || c == '_') return true;
  switch (c) {
    case '#': case ';': case '/': case '?': case ':': case '@':
    case '&': case '=': case '+': case '$': case '.': case '~':
    case '*': case '\'': case '(': case ')':
      return true;
    /* ns-tag-char is ns-uri-char less '!' and the flow indicators, because a
       shorthand ends where a flow collection could begin. */
    case ',': case '[': case ']': case '!':
      return !shorthand;
    default:
      return false;
  }
}

static bool tag_suffix_is_safe(const char *suffix) {
  if (!suffix || !*suffix) return false;
  for (const unsigned char *p = (const unsigned char *)suffix; *p; p++) {
    if (!tag_char_is_safe(*p, true)) return false;
  }
  return true;
}

/* "!", "!local", "!!str", "!handle!suffix".  Anything else - a space, a
   comma, a brace - has to go out verbatim instead. */
static bool tag_is_writable_shorthand(const char *tag) {
  if (!tag || tag[0] != '!') return false;
  const unsigned char *p = (const unsigned char *)tag + 1;
  if (*p == '\0') return true;
  if (*p == '!') {
    p++;
  } else {
    const unsigned char *scan = p;
    while (isalnum(*scan) || *scan == '-') scan++;
    if (*scan == '!') p = scan + 1;
  }
  return tag_suffix_is_safe((const char *)p);
}

static GTEXT_YAML_Status write_tag_verbatim(
    yaml_writer_state * state, const char * tag) {
  static const char hex[] = "0123456789ABCDEF";
  GTEXT_YAML_Status status = write_str(state, "!<");
  if (status != GTEXT_YAML_OK) return status;
  for (const unsigned char *p = (const unsigned char *)tag; *p; p++) {
    /* '%' is escaped along with the unsafe bytes: the parser decoded the
       escapes on the way in, so a literal '%' in the tag we hold has to come
       back out as %25 or the next read will decode something that was never
       written. */
    if (*p != '%' && tag_char_is_safe(*p, false)) {
      status = write_bytes(state, (const char *)p, 1);
    } else {
      char buf[3];
      buf[0] = '%';
      buf[1] = hex[(*p >> 4) & 0x0F];
      buf[2] = hex[*p & 0x0F];
      status = write_bytes(state, buf, sizeof(buf));
    }
    if (status != GTEXT_YAML_OK) return status;
  }
  return write_str(state, ">");
}

static GTEXT_YAML_Status write_tag(
    yaml_writer_state * state, const char * tag) {
  static const char yaml_prefix[] = "tag:yaml.org,2002:";
  if (!tag || !*tag) return GTEXT_YAML_OK;

  if (tag[0] == '!') {
    if (tag_is_writable_shorthand(tag)) {
      return write_str(state, tag);
    }
  } else if (strncmp(tag, yaml_prefix, sizeof(yaml_prefix) - 1) == 0 &&
      tag_suffix_is_safe(tag + sizeof(yaml_prefix) - 1)) {
    GTEXT_YAML_Status status = write_str(state, "!!");
    if (status != GTEXT_YAML_OK) return status;
    return write_str(state, tag + sizeof(yaml_prefix) - 1);
  }

  return write_tag_verbatim(state, tag);
}

static bool scalar_needs_quotes(const char *value, size_t len) {
  if (!value || len == 0) return true;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
      return true;
    }
  }
  return false;
}

static GTEXT_YAML_Status write_escaped_scalar(
    yaml_writer_state * state, const char * value, size_t len) {
  static const char hex[] = "0123456789ABCDEF";
  GTEXT_YAML_Status status = write_str(state, "\"");
  if (status != GTEXT_YAML_OK) return status;

  if (!value) len = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
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

/* Single quotes have no escapes at all, so the style can only be used for
   content it can hold verbatim: a line break inside one folds away to a
   space on the way back in, and there is no spelling for a control
   character.  Both cases go out double quoted instead. */
static bool scalar_fits_single_quotes(const char *value, size_t len) {
  if (!value) return true;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c == '\t') continue;
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

static GTEXT_YAML_Status write_single_quoted_scalar(
    yaml_writer_state * state, const char * value, size_t len) {
  GTEXT_YAML_Status status = write_str(state, "'");
  if (status != GTEXT_YAML_OK) return status;

  if (!value) len = 0;
  for (size_t i = 0; i < len; i++) {
    if (value[i] == '\'') {
      status = write_str(state, "''");
    } else {
      status = write_bytes(state, value + i, 1);
    }
    if (status != GTEXT_YAML_OK) return status;
  }

  return write_str(state, "'");
}

/* Everything a block scalar has to decide before a byte of it is written.
   The old writer decided none of it: it always wrote '|' or '>' with no
   chomping indicator and no indentation indicator, so a value with no
   trailing break came back with one, a value with several came back with
   one, a first line that began with a space lost the space, and folding
   turned every line break in the content into a space. */
typedef struct {
  bool usable;      /* false: the value has no faithful block spelling */
  bool folded;      /* '>' rather than '|' */
  char chomp;       /* '-' strip, '+' keep, '\0' clip */
  int indent_digit; /* 0 when auto-detection reads the right indentation */
  size_t body_len;  /* the value with its trailing line breaks removed */
  size_t trailing;  /* how many of those there were */
} yaml_block_plan;

static void plan_block_scalar(
    const char * value,
    size_t len,
    size_t content_indent,
    int parent_indent,
    bool folded_wanted,
    yaml_block_plan * plan) {
  memset(plan, 0, sizeof(*plan));
  if (!value || len == 0) return;

  size_t trailing = 0;
  while (trailing < len && value[len - 1 - trailing] == '\n') trailing++;
  size_t body_len = len - trailing;
  /* A value that is nothing but line breaks has no first line to set the
     indentation from, so it has no block spelling at all. */
  if (body_len == 0) return;

  for (size_t i = 0; i < body_len; i++) {
    unsigned char c = (unsigned char)value[i];
    /* A carriage return would be read back as a line break, and a control
       character has no literal spelling.  Both need quotes. */
    if (c == '\r') return;
    if (c < 0x20 && c != '\n' && c != '\t') return;
    if (c == 0x7F) return;
  }

  plan->body_len = body_len;
  plan->trailing = trailing;
  plan->chomp = trailing == 0 ? '-' : (trailing == 1 ? '\0' : '+');

  /* 8.1.1.1: auto-detection takes the indentation from the first non-empty
     line, so a first line that begins with a space needs the indicator. */
  size_t first = 0;
  while (first < body_len && value[first] == '\n') first++;
  if (first < body_len && value[first] == ' ') {
    long digit = (long)content_indent - (long)parent_indent;
    if (digit < 1 || digit > 9) return;
    plan->indent_digit = (int)digit;
  }

  if (folded_wanted) {
    /* 8.1.3 keeps the break after a more-indented line and folds every other
       one, so a folded scalar can only hold lines that neither begin nor end
       with a space or a tab. */
    bool ok = true;
    size_t line = 0;
    while (ok && line < body_len) {
      size_t stop = line;
      while (stop < body_len && value[stop] != '\n') stop++;
      if (stop > line) {
        char head = value[line];
        char tail = value[stop - 1];
        if (head == ' ' || head == '\t' || tail == ' ' || tail == '\t') {
          ok = false;
        }
      }
      line = stop + 1;
    }
    plan->folded = ok;
  }

  plan->usable = true;
}

/* A folded line may be broken at a single space, because the break folds
   back to that space.  Two spaces in a row cannot: the fold would return
   only one of them. */
static GTEXT_YAML_Status write_folded_line(
    yaml_writer_state * state,
    const char * line,
    size_t len,
    size_t indent,
    int line_width) {
  size_t width = line_width > 0 ? (size_t)line_width : 0;
  size_t pos = 0;
  bool first = true;

  while (pos < len) {
    size_t cut = len;
    if (width > 0 && len - pos > width) {
      for (size_t i = 1; i < width && pos + i < len; i++) {
        if (line[pos + i] == ' ' && line[pos + i - 1] != ' ' &&
            pos + i + 1 < len && line[pos + i + 1] != ' ') {
          cut = pos + i;
        }
      }
    }
    if (!first) {
      GTEXT_YAML_Status status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
    }
    GTEXT_YAML_Status status = write_indent(state, indent);
    if (status != GTEXT_YAML_OK) return status;
    status = write_bytes(state, line + pos, cut - pos);
    if (status != GTEXT_YAML_OK) return status;
    pos = cut < len ? cut + 1 : len;
    first = false;
  }
  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_block_scalar(
    yaml_writer_state * state,
    const char * value,
    size_t indent,
    const yaml_block_plan * plan) {
  size_t child_indent = indent + (size_t)writer_indent_spaces(state->opts);
  int line_width = writer_line_width(state->opts);

  GTEXT_YAML_Status status = write_str(state, plan->folded ? ">" : "|");
  if (status != GTEXT_YAML_OK) return status;
  if (plan->indent_digit) {
    char digit = (char)('0' + plan->indent_digit);
    status = write_bytes(state, &digit, 1);
    if (status != GTEXT_YAML_OK) return status;
  }
  if (plan->chomp) {
    status = write_bytes(state, &plan->chomp, 1);
    if (status != GTEXT_YAML_OK) return status;
  }

  size_t pos = 0;

  /* 8.1.3 folds a lone break away to a space and keeps one line feed for
     every empty line after it, so a run of k line feeds in the content is
     written as k+1 breaks.  A literal scalar keeps every break as it is, so
     there each line is simply one break. */
  if (plan->folded) {
    size_t lead = 0;
    while (pos < plan->body_len && value[pos] == '\n') {
      lead++;
      pos++;
    }
    /* Empty lines at the head of a folded scalar come before any folding and
       are taken one line feed each. */
    for (size_t i = 0; i <= lead; i++) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
    }
    while (pos < plan->body_len) {
      size_t stop = pos;
      while (stop < plan->body_len && value[stop] != '\n') stop++;
      status = write_folded_line(
          state, value + pos, stop - pos, child_indent, line_width);
      if (status != GTEXT_YAML_OK) return status;
      size_t gap = 0;
      pos = stop;
      while (pos < plan->body_len && value[pos] == '\n') {
        gap++;
        pos++;
      }
      if (pos < plan->body_len) {
        for (size_t i = 0; i <= gap; i++) {
          status = write_str(state, writer_newline(state->opts));
          if (status != GTEXT_YAML_OK) return status;
        }
      }
    }
  } else {
    while (pos <= plan->body_len) {
      size_t stop = pos;
      while (stop < plan->body_len && value[stop] != '\n') stop++;

      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
      if (stop > pos) {
        status = write_indent(state, child_indent);
        if (status == GTEXT_YAML_OK) {
          status = write_bytes(state, value + pos, stop - pos);
        }
        if (status != GTEXT_YAML_OK) return status;
      }
      if (stop == plan->body_len) break;
      pos = stop + 1;
    }
  }

  /* One break terminates the last line; the rest are the value's own, kept
     by '+' and thrown away by '|' and '-' alike. */
  size_t breaks = plan->trailing > 0 ? plan->trailing : 1;
  for (size_t i = 0; i < breaks; i++) {
    status = write_str(state, writer_newline(state->opts));
    if (status != GTEXT_YAML_OK) return status;
  }
  state->line_terminated = true;
  return GTEXT_YAML_OK;
}

static const char *node_leading_comment(const GTEXT_YAML_Node *node) {
  if (!node) return NULL;
  switch (node->type) {
    case GTEXT_YAML_STRING:
    case GTEXT_YAML_BOOL:
    case GTEXT_YAML_INT:
    case GTEXT_YAML_FLOAT:
    case GTEXT_YAML_NULL:
      return node->as.scalar.leading_comment;
    case GTEXT_YAML_SEQUENCE:
    case GTEXT_YAML_OMAP:
    case GTEXT_YAML_PAIRS:
      return node->as.sequence.leading_comment;
    case GTEXT_YAML_MAPPING:
    case GTEXT_YAML_SET:
      return node->as.mapping.leading_comment;
    case GTEXT_YAML_ALIAS:
      return node->as.alias.leading_comment;
    default:
      return NULL;
  }
}

static const char *node_inline_comment(const GTEXT_YAML_Node *node) {
  if (!node) return NULL;
  switch (node->type) {
    case GTEXT_YAML_STRING:
    case GTEXT_YAML_BOOL:
    case GTEXT_YAML_INT:
    case GTEXT_YAML_FLOAT:
    case GTEXT_YAML_NULL:
      return node->as.scalar.inline_comment;
    case GTEXT_YAML_SEQUENCE:
    case GTEXT_YAML_OMAP:
    case GTEXT_YAML_PAIRS:
      return node->as.sequence.inline_comment;
    case GTEXT_YAML_MAPPING:
    case GTEXT_YAML_SET:
      return node->as.mapping.inline_comment;
    case GTEXT_YAML_ALIAS:
      return node->as.alias.inline_comment;
    default:
      return NULL;
  }
}

/* The one place that decides whether a collection is written "[a, b]" or as
   indented block lines.  A container has to ask before it writes "-" and a
   line break, because a child that turns out flow after all would then start
   in column zero: "- !!map" over "{foo: bar}" is two top-level nodes, not
   one tagged mapping. */
static bool collection_is_flow(
    const yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override,
    bool flow) {
  const GTEXT_YAML_Write_Options * opts = state->opts;
  bool pretty = opts ? opts->pretty : false;
  GTEXT_YAML_Flow_Style flow_style = opts
      ? opts->flow_style
      : GTEXT_YAML_FLOW_STYLE_AUTO;

  if (flow_style == GTEXT_YAML_FLOW_STYLE_FLOW) return true;
  /* Block style asked for at the root, not once inside a flow collection:
     7.3 has no block spelling there. */
  if (flow_style == GTEXT_YAML_FLOW_STYLE_BLOCK && !flow) pretty = true;
  if (flow) return true;
  if (opts && opts->canonical) return true;
  /* Properties are written in front of the node, and "&a" cannot share a
     line with block content, so an anchored or tagged collection goes out
     flow whatever was asked for. */
  if (node_anchor(node) || tag_override || node_tag(node)) return true;
  if (node_requires_tag(node->type)) return true;
  return !pretty;
}

static bool node_opens_block(
    const yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override) {
  if (node_is_sequence_type(node)) {
    if (node->as.sequence.count == 0) return false;
  } else if (node_is_mapping_type(node)) {
    if (node->as.mapping.count == 0) return false;
  } else {
    return false;
  }
  return !collection_is_flow(state, node, tag_override, false);
}

/* True when this node will be written as nothing at all, so the space that
   would have separated it from its '-' or ':' has nothing left to separate. */
static bool node_writes_nothing(
    const yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override) {
  if (!state->empty_scalar_ok) return false;
  if (!node || node->type != GTEXT_YAML_NULL) return false;
  if (node->as.scalar.value && node->as.scalar.value[0]) return false;
  if (!state->opts) return false;
  if (state->opts->canonical) return false;
  if (state->opts->scalar_style != GTEXT_YAML_SCALAR_STYLE_PLAIN) return false;
  if (node->as.scalar.scalar_style != GTEXT_YAML_SCALAR_STYLE_PLAIN) return false;
  if (node_anchor(node) || tag_override || node_tag(node)) return false;
  return node_inline_comment(node) == NULL;
}

static GTEXT_YAML_Status write_comment_lines(
    yaml_writer_state *state,
    const char *comment,
    size_t indent) {
  if (!comment || !state) return GTEXT_YAML_OK;

  const char *line = comment;
  const char *cursor = comment;

  for (;;) {
    if (*cursor == '\n' || *cursor == '\0') {
      size_t len = (size_t)(cursor - line);
      GTEXT_YAML_Status status = write_indent(state, indent);
      if (status != GTEXT_YAML_OK) return status;
      status = write_str(state, "#");
      if (status != GTEXT_YAML_OK) return status;
      if (len > 0) {
        status = write_str(state, " ");
        if (status != GTEXT_YAML_OK) return status;
        status = write_bytes(state, line, len);
        if (status != GTEXT_YAML_OK) return status;
      }
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
      if (*cursor == '\0') break;
      cursor++;
      line = cursor;
      continue;
    }
    cursor++;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_inline_comment(
    yaml_writer_state *state,
    const char *comment) {
  if (!comment || !state) return GTEXT_YAML_OK;
  GTEXT_YAML_Status status = write_str(state, " # ");
  if (status != GTEXT_YAML_OK) return status;
  return write_str(state, comment);
}

static GTEXT_YAML_Status write_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
  const char * tag_override,
  bool leading_newline);

static GTEXT_YAML_Status write_properties(
    yaml_writer_state * state,
    const char * anchor,
    const char * tag,
    GTEXT_YAML_Node_Type type,
    bool trailing_space) {
  bool canonical = state->opts && state->opts->canonical;

  if (!tag && node_requires_tag(type)) {
    tag = default_tag_for_type(type);
  }

  if (!tag && canonical) {
    tag = default_tag_for_type(type);
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
    status = write_tag(state, tag);
    if (status != GTEXT_YAML_OK) return status;
  }

  if ((anchor || tag) && trailing_space) {
    return write_str(state, " ");
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_node_prefix(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override,
    bool trailing_space) {
  return write_properties(
      state,
      node_anchor(node),
      tag_override ? tag_override : node_tag(node),
      node->type,
      trailing_space);
}

static GTEXT_YAML_Status resolve_custom_write_tag(
    const yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    const char * tag_override,
    const char ** out_tag_override) {
  const char *tag = tag_override ? tag_override : node_tag(node);
  if (out_tag_override) {
    *out_tag_override = tag_override;
  }
  if (!state || !state->opts || !state->opts->enable_custom_tags) {
    return GTEXT_YAML_OK;
  }
  if (!state->opts->custom_tags || state->opts->custom_tag_count == 0) {
    return GTEXT_YAML_OK;
  }
  if (!tag) {
    return GTEXT_YAML_OK;
  }

  for (size_t i = 0; i < state->opts->custom_tag_count; i++) {
    const GTEXT_YAML_Custom_Tag *handler = &state->opts->custom_tags[i];
    if (!handler->tag || !handler->represent) continue;
    if (strcmp(handler->tag, tag) != 0) continue;

    const char *custom_tag = NULL;
    GTEXT_YAML_Status st = handler->represent(node, tag, handler->user, &custom_tag, NULL);
    if (st != GTEXT_YAML_OK) return st;
    if (custom_tag && out_tag_override) {
      *out_tag_override = custom_tag;
    }
    return GTEXT_YAML_OK;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status write_scalar_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
    const char * tag_override) {
  const char *resolved_tag = tag_override;
  GTEXT_YAML_Status status = resolve_custom_write_tag(
      state, node, tag_override, &resolved_tag);
  if (status != GTEXT_YAML_OK) return status;

  const char *value = node->as.scalar.value;
  /* A scalar may hold a NUL, so its length is what counts, not its
     terminator. */
  size_t value_len = value ? node->as.scalar.length : 0;
  bool is_binary = false;
  bool canonical = state->opts && state->opts->canonical;
  GTEXT_YAML_Scalar_Style style = state->opts
      ? state->opts->scalar_style
      : GTEXT_YAML_SCALAR_STYLE_PLAIN;

  if (node->type == GTEXT_YAML_STRING && node->as.scalar.has_binary) {
    is_binary = true;
  }
  if (!is_binary) {
    const char *tag = resolved_tag ? resolved_tag : node_tag(node);
    is_binary = tag_is_binary(tag);
  }

  if (canonical) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  } else if (style == GTEXT_YAML_SCALAR_STYLE_PLAIN) {
    style = node->as.scalar.scalar_style;
  }

  if (!canonical && style == GTEXT_YAML_SCALAR_STYLE_PLAIN &&
      node->type == GTEXT_YAML_NULL) {
    if (!value || value[0] == '\0') {
      /* Properties are enough to make a flow sequence entry a node, so an
         anchored or tagged empty scalar can stay empty even there. */
      bool has_properties =
          node_anchor(node) || resolved_tag || node_tag(node);
      if (state->empty_scalar_ok || has_properties) {
        status = write_node_prefix(state, node, resolved_tag, false);
        if (status != GTEXT_YAML_OK) return status;
        state->key_absorbs_colon = has_properties;
        return write_inline_comment(state, node_inline_comment(node));
      }
      status = write_node_prefix(state, node, resolved_tag, true);
      if (status != GTEXT_YAML_OK) return status;
      return write_str(state, "~");
    }
    status = write_node_prefix(state, node, resolved_tag, true);
    if (status != GTEXT_YAML_OK) return status;
    return write_str(state, value);
  }

  status = write_node_prefix(state, node, resolved_tag, true);
  if (status != GTEXT_YAML_OK) return status;

  if (style == GTEXT_YAML_SCALAR_STYLE_PLAIN) {
    if (!is_binary && scalar_needs_quotes(value, value_len)) {
      style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
    } else if (!flow && state->opts && state->opts->pretty) {
      int line_width = writer_line_width(state->opts);
      if (line_width > 0 && value_len > (size_t)line_width) {
        style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
      }
    }
  }

  if (flow && (style == GTEXT_YAML_SCALAR_STYLE_LITERAL ||
      style == GTEXT_YAML_SCALAR_STYLE_FOLDED)) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  }

  /* A block style is only a spelling of the value if the value has one.  Ask
     before committing to it, and fall back to quotes when the answer is no -
     rather than writing something that reads back as a different string. */
  yaml_block_plan block;
  memset(&block, 0, sizeof(block));
  if (style == GTEXT_YAML_SCALAR_STYLE_LITERAL ||
      style == GTEXT_YAML_SCALAR_STYLE_FOLDED) {
    plan_block_scalar(
        value,
        value_len,
        indent + (size_t)writer_indent_spaces(state->opts),
        state->block_parent_indent,
        style == GTEXT_YAML_SCALAR_STYLE_FOLDED,
        &block);
    if (!block.usable) {
      style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
    }
  }

  if (style == GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED &&
      !scalar_fits_single_quotes(value, value_len)) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  }

  switch (style) {
    case GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED:
      status = write_single_quoted_scalar(state, value, value_len);
      break;
    case GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED:
      status = write_escaped_scalar(state, value, value_len);
      break;
    case GTEXT_YAML_SCALAR_STYLE_LITERAL:
    case GTEXT_YAML_SCALAR_STYLE_FOLDED:
      return write_block_scalar(state, value, indent, &block);
    case GTEXT_YAML_SCALAR_STYLE_PLAIN:
    default:
      status = GTEXT_YAML_OK;
      break;
  }

  if (status != GTEXT_YAML_OK) return status;

  if (style == GTEXT_YAML_SCALAR_STYLE_PLAIN) {
    status = write_bytes(state, value ? value : "", value_len);
    if (status != GTEXT_YAML_OK) return status;
  }

  return write_inline_comment(state, node_inline_comment(node));
}

static GTEXT_YAML_Status write_sequence_node(
    yaml_writer_state * state,
    const GTEXT_YAML_Node * node,
    size_t indent,
    bool flow,
  const char * tag_override,
  bool leading_newline) {
  GTEXT_YAML_Status status = GTEXT_YAML_OK;
  flow = collection_is_flow(state, node, tag_override, flow);

  const char *resolved_tag = tag_override;
  status = resolve_custom_write_tag(state, node, tag_override, &resolved_tag);
  if (status != GTEXT_YAML_OK) return status;
  if (!resolved_tag && !node_tag(node) &&
      (node->type == GTEXT_YAML_OMAP || node->type == GTEXT_YAML_PAIRS)) {
    resolved_tag = default_tag_for_type(node->type);
  }
  if (node->type == GTEXT_YAML_OMAP || node->type == GTEXT_YAML_PAIRS) {
    for (size_t i = 0; i < node->as.sequence.count; i++) {
      const GTEXT_YAML_Node *entry = node->as.sequence.children[i];
      if (!entry || entry->type != GTEXT_YAML_MAPPING || entry->as.mapping.count != 1) {
        return GTEXT_YAML_E_INVALID;
      }
    }
  }
  status = write_node_prefix(state, node, resolved_tag, true);
  if (status != GTEXT_YAML_OK) return status;

  if (flow) {
    status = write_str(state, "[");
    if (status != GTEXT_YAML_OK) return status;
    for (size_t i = 0; i < node->as.sequence.count; i++) {
      if (i > 0) {
        status = write_str(state, ", ");
        if (status != GTEXT_YAML_OK) return status;
      }
      /* ns-flow-seq-entry has no empty alternative, so an entry with nothing
         in it and no properties has to be written "~". */
      state->empty_scalar_ok = false;
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
    status = write_str(state, "]");
    if (status != GTEXT_YAML_OK) return status;
    state->key_absorbs_colon = false;
    return write_inline_comment(state, node_inline_comment(node));
  }

  if (node->as.sequence.count == 0) {
    return write_str(state, "[]");
  }

  for (size_t i = 0; i < node->as.sequence.count; i++) {
    const GTEXT_YAML_Node *child = node->as.sequence.children[i];
    const char *child_comment = node_leading_comment(child);
    if (i > 0 || leading_newline) {
      status = write_separator(state);
      if (status != GTEXT_YAML_OK) return status;
    } else {
      state->line_terminated = false;
    }
    if (child_comment) {
      status = write_comment_lines(state, child_comment, indent);
      if (status != GTEXT_YAML_OK) return status;
    }
    if (status != GTEXT_YAML_OK) return status;
    status = write_indent(state, indent);
    if (status != GTEXT_YAML_OK) return status;
    status = write_str(state, "-");
    if (status != GTEXT_YAML_OK) return status;

    state->block_parent_indent = (int)indent;
    state->empty_scalar_ok = true;
    if (node_opens_block(state, child, NULL)) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
        status = write_node(
          state,
          child,
          indent + (size_t)writer_indent_spaces(state->opts),
          false,
          NULL,
          false
        );
    } else {
      status = write_str(
          state, node_writes_nothing(state, child, NULL) ? "" : " ");
      if (status != GTEXT_YAML_OK) return status;
        status = write_node(
          state,
          child,
          indent + (size_t)writer_indent_spaces(state->opts),
        !node_scalar_block_style(child),
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
  flow = collection_is_flow(state, node, tag_override, flow);

  const char *resolved_tag = tag_override;
  status = resolve_custom_write_tag(state, node, tag_override, &resolved_tag);
  if (status != GTEXT_YAML_OK) return status;
  if (!resolved_tag && !node_tag(node) && node->type == GTEXT_YAML_SET) {
    resolved_tag = default_tag_for_type(node->type);
  }
  if (node->type == GTEXT_YAML_SET) {
    for (size_t i = 0; i < node->as.mapping.count; i++) {
      const GTEXT_YAML_Node *value = node->as.mapping.pairs[i].value;
      if (value && value->type == GTEXT_YAML_ALIAS) {
        value = value->as.alias.target;
      }
      if (!value || value->type != GTEXT_YAML_NULL) {
        return GTEXT_YAML_E_INVALID;
      }
    }
  }
  status = write_node_prefix(state, node, resolved_tag, true);
  if (status != GTEXT_YAML_OK) return status;

  if (flow) {
    status = write_str(state, "{");
    if (status != GTEXT_YAML_OK) return status;
    for (size_t i = 0; i < node->as.mapping.count; i++) {
      if (i > 0) {
        status = write_str(state, ", ");
        if (status != GTEXT_YAML_OK) return status;
      }
      state->empty_scalar_ok = true;
      status = write_node(
          state,
          node->as.mapping.pairs[i].key,
          indent,
          true,
          node->as.mapping.pairs[i].key_tag,
          false
      );
      if (status != GTEXT_YAML_OK) return status;
      status = write_str(state, state->key_absorbs_colon ? " : " : ": ");
      if (status != GTEXT_YAML_OK) return status;
      state->empty_scalar_ok = true;
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
    status = write_str(state, "}");
    if (status != GTEXT_YAML_OK) return status;
    state->key_absorbs_colon = false;
    return write_inline_comment(state, node_inline_comment(node));
  }

  if (node->as.mapping.count == 0) {
    return write_str(state, "{}");
  }

  for (size_t i = 0; i < node->as.mapping.count; i++) {
    const GTEXT_YAML_Node *key_node = node->as.mapping.pairs[i].key;
    const char *key_comment = node_leading_comment(key_node);
    if (i > 0 || leading_newline) {
      status = write_separator(state);
      if (status != GTEXT_YAML_OK) return status;
    } else {
      state->line_terminated = false;
    }
    if (key_comment) {
      status = write_comment_lines(state, key_comment, indent);
      if (status != GTEXT_YAML_OK) return status;
    }
    if (status != GTEXT_YAML_OK) return status;
    status = write_indent(state, indent);
    if (status != GTEXT_YAML_OK) return status;
    state->empty_scalar_ok = true;
    status = write_node(
        state,
        key_node,
        indent,
        true,
        node->as.mapping.pairs[i].key_tag,
        false
    );
    if (status != GTEXT_YAML_OK) return status;
    status = write_str(state, state->key_absorbs_colon ? " :" : ":");
    if (status != GTEXT_YAML_OK) return status;

    const GTEXT_YAML_Node *value = node->as.mapping.pairs[i].value;
    state->block_parent_indent = (int)indent;
    state->empty_scalar_ok = true;
    if (node_opens_block(state, value, node->as.mapping.pairs[i].value_tag)) {
      status = write_str(state, writer_newline(state->opts));
      if (status != GTEXT_YAML_OK) return status;
        status = write_node(
          state,
          value,
          indent + (size_t)writer_indent_spaces(state->opts),
          false,
          node->as.mapping.pairs[i].value_tag,
          false
        );
    } else {
      status = write_str(
          state,
          node_writes_nothing(state, value, node->as.mapping.pairs[i].value_tag)
              ? "" : " ");
      if (status != GTEXT_YAML_OK) return status;
        status = write_node(
          state,
          value,
          indent + (size_t)writer_indent_spaces(state->opts),
        !node_scalar_block_style(value),
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
  status = write_str(state, name);
  if (status != GTEXT_YAML_OK) return status;
  state->key_absorbs_colon = true;
  return write_inline_comment(state, node_inline_comment(node));
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

  state->key_absorbs_colon = false;

  switch (node->type) {
    case GTEXT_YAML_STRING:
    case GTEXT_YAML_BOOL:
    case GTEXT_YAML_INT:
    case GTEXT_YAML_FLOAT:
    case GTEXT_YAML_NULL:
      return write_scalar_node(state, node, indent, flow, tag_override);
    case GTEXT_YAML_SEQUENCE:
    case GTEXT_YAML_OMAP:
    case GTEXT_YAML_PAIRS:
      return write_sequence_node(state, node, indent, flow, tag_override, leading_newline);
    case GTEXT_YAML_MAPPING:
    case GTEXT_YAML_SET:
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
  yaml_encoding_state encoding;
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
  writer_encoding_init(&encoding, opts);
  state.encoding = &encoding;
  /* s-l+block-node(-1, block-in): the root of a document sits one column to
     the left of column zero, as far as 8.1.1.1 is concerned. */
  state.block_parent_indent = -1;
  state.line_terminated = false;
  /* Nothing holds the root of a document, so an empty root has to be "~". */
  state.empty_scalar_ok = false;
  state.key_absorbs_colon = false;
  root = doc->root;

  if (!root) {
    /* A document with no content is still a document, and the empty node it
       holds resolves to null.  Writing nothing at all said "no documents"
       instead, which is a different stream. */
    status = write_str(&state, "---");
    if (status != GTEXT_YAML_OK) {
      return status;
    }
    if (opts->trailing_newline) {
      status = write_str(&state, writer_newline(opts));
    }
    return status;
  }

  status = write_comment_lines(&state, node_leading_comment(root), 0);
  if (status != GTEXT_YAML_OK) {
    return status;
  }

  status = write_node(&state, root, 0, writer_root_is_flow(opts), NULL, false);
  if (status != GTEXT_YAML_OK) {
    return status;
  }

  if (opts->trailing_newline) {
    status = write_separator(&state);
    if (status != GTEXT_YAML_OK) {
      return status;
    }
  }
  if (encoding.pending_utf8_len != 0) {
    return GTEXT_YAML_E_INVALID;
  }

  return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_write_documents(
    GTEXT_YAML_Document * const * docs,
    size_t count,
    GTEXT_YAML_Sink * sink,
    const GTEXT_YAML_Write_Options * opts) {
  GTEXT_YAML_Write_Options defaults = gtext_yaml_write_options_default();
  yaml_writer_state state;
  yaml_encoding_state encoding;
  GTEXT_YAML_Status status = GTEXT_YAML_OK;
  bool wrote_doc = false;

  if (!sink || !sink->write) {
    return GTEXT_YAML_E_INVALID;
  }
  /* An empty stream is a stream.  "# comment only", "..." and a file of one
     blank line all parse to no documents at all, and refusing to write them
     made a legal round trip look like a writer failure. */
  if (count == 0) {
    return GTEXT_YAML_OK;
  }
  if (!docs) {
    return GTEXT_YAML_E_INVALID;
  }
  if (!opts) {
    opts = &defaults;
  }

  state.sink = sink;
  state.opts = opts;
  writer_encoding_init(&encoding, opts);
  state.encoding = &encoding;
  state.block_parent_indent = -1;
  state.line_terminated = false;
  /* Nothing holds the root of a document, so an empty root has to be "~". */
  state.empty_scalar_ok = false;
  state.key_absorbs_colon = false;

  for (size_t i = 0; i < count; i++) {
    const GTEXT_YAML_Document *doc = docs[i];
    const GTEXT_YAML_Node *root = NULL;

    if (!doc) {
      return GTEXT_YAML_E_INVALID;
    }

    if (wrote_doc) {
      status = write_separator(&state);
      if (status != GTEXT_YAML_OK) return status;
    }

    state.block_parent_indent = -1;
    status = write_str(&state, "---");
    if (status != GTEXT_YAML_OK) return status;
    status = write_str(&state, writer_newline(opts));
    if (status != GTEXT_YAML_OK) return status;

    root = doc->root;
    if (root) {
      status = write_comment_lines(&state, node_leading_comment(root), 0);
      if (status != GTEXT_YAML_OK) return status;
      status = write_node(&state, root, 0, writer_root_is_flow(opts), NULL, false);
      if (status != GTEXT_YAML_OK) return status;
    }

    if (opts->trailing_newline) {
      status = write_separator(&state);
      if (status != GTEXT_YAML_OK) return status;
    }

    wrote_doc = true;
  }

  if (encoding.pending_utf8_len != 0) {
    return GTEXT_YAML_E_INVALID;
  }

  return GTEXT_YAML_OK;
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
  bool flow;
  bool has_items;
  bool expecting_key;
  bool explicit_key;
  bool is_map_key;
  size_t indent;
} yaml_writer_stack_entry;

struct GTEXT_YAML_Writer {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Write_Options opts;
  yaml_encoding_state encoding;
  /* Set when the node just written ended in an anchor, a tag or an alias
     name, all of which would swallow a ':' written straight after them. */
  bool key_absorbs_colon;
  /* Set when a block scalar has already written the break that ends its last
     line, so the next separator must not write a second one. */
  bool line_terminated;
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
    bool is_map_key,
    bool flow,
    size_t indent) {
  if (writer_stack_grow(writer) != 0) {
    return 1;
  }
  yaml_writer_stack_entry entry = {
      .type = type,
      .flow = flow,
      .has_items = false,
      .expecting_key = (type == YAML_WRITER_STACK_MAPPING),
      .explicit_key = false,
      .is_map_key = is_map_key,
      .indent = indent};
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

/* The two writers share a sink, a set of options and an encoder, so the
   streaming one borrows the spelling helpers above by handing them a view of
   itself rather than keeping a second copy of every rule.  The two used to
   be written out twice and had drifted: only one of them knew how to spell a
   resolved tag, and neither knew how to chomp a block scalar. */
static void writer_view(GTEXT_YAML_Writer *writer, yaml_writer_state *state) {
  memset(state, 0, sizeof(*state));
  state->sink = &writer->sink;
  state->opts = &writer->opts;
  state->encoding = &writer->encoding;
  state->block_parent_indent = -1;
  state->empty_scalar_ok = false;
}

static int writer_write_bytes(GTEXT_YAML_Writer *writer,
    const char *bytes, size_t len) {
  if (!writer || !writer->sink.write || !bytes) {
    return 1;
  }
  GTEXT_YAML_Status status = write_encoded_bytes(
      &writer->sink, &writer->encoding, bytes, len);
  if (status != GTEXT_YAML_OK) {
    writer->error = true;
    return 1;
  }
  return 0;
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

static int writer_write_indent(GTEXT_YAML_Writer *writer, size_t spaces) {
  char pad[64];
  size_t chunk = sizeof(pad);
  memset(pad, ' ', sizeof(pad));
  while (spaces > 0) {
    size_t to_write = spaces > chunk ? chunk : spaces;
    if (writer_write_bytes(writer, pad, to_write) != 0) return 1;
    spaces -= to_write;
  }
  return 0;
}

/* Thin shims: the rules live in the shared helpers above, and these only
   carry the streaming writer's int-returning convention across. */
static int writer_write_prefix(
    GTEXT_YAML_Writer *writer,
    const char *anchor,
    const char *tag,
    GTEXT_YAML_Node_Type type,
    bool trailing_space) {
  yaml_writer_state view;
  writer_view(writer, &view);
  if (write_properties(&view, anchor, tag, type, trailing_space)
      != GTEXT_YAML_OK) {
    writer->error = true;
    return 1;
  }
  /* An anchor or a tag with nothing after it would swallow a ':'. */
  writer->key_absorbs_colon = !trailing_space && (anchor || tag);
  return 0;
}

/* The break that separates one node from the next - unless a block scalar
   has already written it, in which case a second would leave a blank line
   that '+' chomping would keep as part of the value. */
static int writer_write_separator(GTEXT_YAML_Writer *writer) {
  if (writer->line_terminated) {
    writer->line_terminated = false;
    return 0;
  }
  return writer_write_string(writer, writer_newline(&writer->opts));
}

/* @p empty says the node about to be written puts nothing on the page, so
   the space that would have separated it from its "-" or ":" has nothing
   left to separate. */
static int writer_prepare_scalar(
    GTEXT_YAML_Writer *writer, bool *is_key, bool empty) {
  yaml_writer_stack_entry *top = writer_stack_top(writer);
  if (!top) {
    if (is_key) *is_key = false;
    return 0;
  }

  if (top->flow) {
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
      if (writer_write_string(writer, writer->key_absorbs_colon
              ? (empty ? " :" : " : ") : (empty ? ":" : ": ")) != 0) return 1;
      writer->key_absorbs_colon = false;
      if (is_key) *is_key = false;
      return 0;
    }
    return 1;
  }

  if (top->type == YAML_WRITER_STACK_SEQUENCE) {
    if (top->has_items) {
      if (writer_write_separator(writer) != 0) return 1;
    }
    if (writer_write_indent(writer, top->indent) != 0) return 1;
    if (writer_write_string(writer, empty ? "-" : "- ") != 0) return 1;
    if (is_key) *is_key = false;
    return 0;
  }

  if (top->type == YAML_WRITER_STACK_MAPPING) {
    if (top->expecting_key) {
      if (top->has_items) {
        if (writer_write_separator(writer) != 0) return 1;
      }
      if (writer_write_indent(writer, top->indent) != 0) return 1;
      if (is_key) *is_key = true;
      return 0;
    }

    if (top->explicit_key) {
      if (writer_write_separator(writer) != 0) return 1;
      if (writer_write_indent(writer, top->indent) != 0) return 1;
      top->explicit_key = false;
    }
    if (writer_write_string(writer, writer->key_absorbs_colon
            ? (empty ? " :" : " : ") : (empty ? ":" : ": ")) != 0) return 1;
    writer->key_absorbs_colon = false;
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
      top->explicit_key = false;
    }
  }
}

static GTEXT_YAML_Status writer_emit_scalar(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event) {
  bool is_key = false;
  const char *value = event->data.scalar.ptr ? event->data.scalar.ptr : "";
  size_t len = event->data.scalar.len;
  GTEXT_YAML_Scalar_Style style = writer->opts.scalar_style;
  yaml_writer_stack_entry *top = writer_stack_top(writer);
  bool in_flow = top ? top->flow : false;
  size_t base_indent = top ? top->indent : 0;
  bool has_properties = event->anchor || event->tag;
  yaml_writer_state view;
  writer_view(writer, &view);

  /* An empty plain scalar is the empty node of 7.2, not the empty string:
     "a:" and "-" are how it is written, and writing "" instead put a string
     into the document that the document never held.  One position has no
     empty alternative - a flow sequence entry with no properties, which
     needs "~" - and the writer's own "---" holds an empty root. */
  bool empty_node = (len == 0)
      && !writer->opts.canonical
      && writer->opts.scalar_style == GTEXT_YAML_SCALAR_STYLE_PLAIN
      && event->scalar_style == GTEXT_YAML_SCALAR_STYLE_PLAIN;
  bool needs_tilde = empty_node && !has_properties && in_flow && top
      && top->type == YAML_WRITER_STACK_SEQUENCE;
  if (needs_tilde) empty_node = false;

  if (writer_prepare_scalar(
          writer, &is_key, empty_node && !has_properties) != 0) {
    return GTEXT_YAML_E_STATE;
  }

  if (writer_write_prefix(
          writer,
          event->anchor,
          event->tag,
          GTEXT_YAML_STRING,
          !empty_node) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  if (empty_node) {
    writer_finish_value(writer, is_key);
    return GTEXT_YAML_OK;
  }
  if (needs_tilde) {
    if (writer_write_string(writer, "~") != 0) return GTEXT_YAML_E_WRITE;
    writer->key_absorbs_colon = false;
    writer_finish_value(writer, is_key);
    return GTEXT_YAML_OK;
  }
  /* 8.1.1.1 measures the indentation indicator from the node that holds the
     scalar: the container's own indent, or -1 at the root of a document. */
  view.block_parent_indent = top ? (int)top->indent : -1;

  if (writer->opts.canonical) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  }
  if (!writer->opts.canonical && style == GTEXT_YAML_SCALAR_STYLE_PLAIN) {
    style = event->scalar_style;
  }
  if (style == GTEXT_YAML_SCALAR_STYLE_PLAIN && scalar_needs_quotes(value, len)) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  } else if (style == GTEXT_YAML_SCALAR_STYLE_PLAIN && !in_flow && writer->opts.pretty) {
    int width = writer_line_width(&writer->opts);
    if (width > 0 && len > (size_t)width) {
      style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
    }
  }

  if (in_flow && (style == GTEXT_YAML_SCALAR_STYLE_LITERAL ||
      style == GTEXT_YAML_SCALAR_STYLE_FOLDED)) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  }

  yaml_block_plan block;
  memset(&block, 0, sizeof(block));
  if (style == GTEXT_YAML_SCALAR_STYLE_LITERAL ||
      style == GTEXT_YAML_SCALAR_STYLE_FOLDED) {
    plan_block_scalar(
        value,
        len,
        base_indent + (size_t)writer_indent_spaces(&writer->opts),
        view.block_parent_indent,
        style == GTEXT_YAML_SCALAR_STYLE_FOLDED,
        &block);
    if (!block.usable) style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  }

  if (style == GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED &&
      !scalar_fits_single_quotes(value, len)) {
    style = GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
  }

  GTEXT_YAML_Status st = GTEXT_YAML_OK;
  switch (style) {
    case GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED:
      st = write_single_quoted_scalar(&view, value, len);
      break;
    case GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED:
      st = write_escaped_scalar(&view, value, len);
      break;
    case GTEXT_YAML_SCALAR_STYLE_LITERAL:
    case GTEXT_YAML_SCALAR_STYLE_FOLDED:
      st = write_block_scalar(&view, value, base_indent, &block);
      break;
    default:
      st = write_bytes(&view, value, len);
      break;
  }
  if (st != GTEXT_YAML_OK) {
    writer->error = true;
    return GTEXT_YAML_E_WRITE;
  }
  /* A block scalar ends its own last line, so whatever writes the next
     separator has to know not to write another. */
  writer->line_terminated = view.line_terminated;
  /* Content of any kind stands between a property and a following ':'. */
  if (len > 0 || style != GTEXT_YAML_SCALAR_STYLE_PLAIN) {
    writer->key_absorbs_colon = false;
  }

  writer_finish_value(writer, is_key);
  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status writer_emit_alias(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event) {
  bool is_key = false;
  if (writer_prepare_scalar(writer, &is_key, false) != 0) {
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
  /* 6.9.2 stops an anchor name only at a flow indicator, so "*b: 1" names
     the anchor "b:".  The colon has to be kept off it. */
  writer->key_absorbs_colon = true;

  writer_finish_value(writer, is_key);
  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status writer_emit_comment(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event) {
  if (!writer || !event) return GTEXT_YAML_E_INVALID;
  const char *comment = event->data.comment.ptr;
  if (!comment) return GTEXT_YAML_OK;

  yaml_writer_stack_entry *top = writer_stack_top(writer);
  size_t indent = top ? top->indent : 0;

  if (writer_write_indent(writer, indent) != 0) return GTEXT_YAML_E_WRITE;
  if (writer_write_string(writer, "#") != 0) return GTEXT_YAML_E_WRITE;
  if (comment[0] != '\0') {
    if (writer_write_string(writer, " ") != 0) return GTEXT_YAML_E_WRITE;
    if (writer_write_string(writer, comment) != 0) return GTEXT_YAML_E_WRITE;
  }
  if (writer_write_string(writer, writer_newline(&writer->opts)) != 0) {
    return GTEXT_YAML_E_WRITE;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status writer_emit_container_start(
    GTEXT_YAML_Writer *writer,
    const GTEXT_YAML_Event *event,
    yaml_writer_stack_type type) {
  yaml_writer_stack_entry *parent = writer_stack_top(writer);
  bool is_key = false;
  bool flow = false;
  bool force_flow = false;
  size_t parent_indent = parent ? parent->indent : 0;
  size_t indent_step = (size_t)writer_indent_spaces(&writer->opts);
  size_t child_indent = parent ? parent_indent + (parent->flow ? 0 : indent_step) : 0;
  bool has_prefix = event->anchor || event->tag;

  if (writer->opts.flow_style == GTEXT_YAML_FLOW_STYLE_FLOW) {
    force_flow = true;
  } else if (writer->opts.flow_style == GTEXT_YAML_FLOW_STYLE_BLOCK) {
    force_flow = false;
  } else {
    force_flow = !writer->opts.pretty;
  }

  flow = force_flow;
  if (writer->opts.canonical || has_prefix) {
    flow = true;
  }
  if (parent && parent->flow) {
    flow = true;
  }

  if (!parent) {
    if (writer_write_prefix(
            writer,
            event->anchor,
            event->tag,
            (type == YAML_WRITER_STACK_SEQUENCE)
                ? GTEXT_YAML_SEQUENCE
                : GTEXT_YAML_MAPPING,
            flow) != 0) {
      return GTEXT_YAML_E_WRITE;
    }
    if (flow) {
      char open_char = (type == YAML_WRITER_STACK_SEQUENCE) ? '[' : '{';
      if (writer_write_char(writer, open_char) != 0) return GTEXT_YAML_E_WRITE;
    } else if (has_prefix) {
      if (writer_write_string(writer, writer_newline(&writer->opts)) != 0) {
        return GTEXT_YAML_E_WRITE;
      }
    }
  } else if (parent->flow) {
    if (writer_prepare_scalar(writer, &is_key, false) != 0) {
      return GTEXT_YAML_E_STATE;
    }
    if (writer_write_prefix(
            writer,
            event->anchor,
            event->tag,
            (type == YAML_WRITER_STACK_SEQUENCE)
                ? GTEXT_YAML_SEQUENCE
                : GTEXT_YAML_MAPPING,
            true) != 0) {
      return GTEXT_YAML_E_WRITE;
    }
    char open_char = (type == YAML_WRITER_STACK_SEQUENCE) ? '[' : '{';
    if (writer_write_char(writer, open_char) != 0) return GTEXT_YAML_E_WRITE;
  } else if (parent->type == YAML_WRITER_STACK_SEQUENCE) {
    if (parent->has_items) {
      if (writer_write_separator(writer) != 0) return GTEXT_YAML_E_WRITE;
    }
    if (writer_write_indent(writer, parent_indent) != 0) return GTEXT_YAML_E_WRITE;
    if (flow) {
      if (writer_write_string(writer, "- ") != 0) return GTEXT_YAML_E_WRITE;
      if (writer_write_prefix(
              writer,
              event->anchor,
              event->tag,
              (type == YAML_WRITER_STACK_SEQUENCE)
                  ? GTEXT_YAML_SEQUENCE
                  : GTEXT_YAML_MAPPING,
              true) != 0) {
        return GTEXT_YAML_E_WRITE;
      }
      char open_char = (type == YAML_WRITER_STACK_SEQUENCE) ? '[' : '{';
      if (writer_write_char(writer, open_char) != 0) return GTEXT_YAML_E_WRITE;
    } else {
      if (has_prefix) {
        if (writer_write_string(writer, "- ") != 0) return GTEXT_YAML_E_WRITE;
        if (writer_write_prefix(
                writer,
                event->anchor,
                event->tag,
                (type == YAML_WRITER_STACK_SEQUENCE)
                    ? GTEXT_YAML_SEQUENCE
                    : GTEXT_YAML_MAPPING,
                false) != 0) {
          return GTEXT_YAML_E_WRITE;
        }
      } else {
        if (writer_write_string(writer, "-") != 0) return GTEXT_YAML_E_WRITE;
      }
      if (writer_write_string(writer, writer_newline(&writer->opts)) != 0) {
        return GTEXT_YAML_E_WRITE;
      }
    }
  } else if (parent->type == YAML_WRITER_STACK_MAPPING) {
    if (parent->expecting_key) {
      if (parent->has_items) {
        if (writer_write_separator(writer) != 0) return GTEXT_YAML_E_WRITE;
      }
      if (writer_write_indent(writer, parent_indent) != 0) return GTEXT_YAML_E_WRITE;
      if (writer_write_string(writer, "? ") != 0) return GTEXT_YAML_E_WRITE;
      parent->explicit_key = true;
      is_key = true;

      if (flow) {
        if (writer_write_prefix(
                writer,
                event->anchor,
                event->tag,
                (type == YAML_WRITER_STACK_SEQUENCE)
                    ? GTEXT_YAML_SEQUENCE
                    : GTEXT_YAML_MAPPING,
                true) != 0) {
          return GTEXT_YAML_E_WRITE;
        }
        char open_char = (type == YAML_WRITER_STACK_SEQUENCE) ? '[' : '{';
        if (writer_write_char(writer, open_char) != 0) return GTEXT_YAML_E_WRITE;
      } else {
        if (has_prefix) {
          if (writer_write_prefix(
                  writer,
                  event->anchor,
                  event->tag,
                  (type == YAML_WRITER_STACK_SEQUENCE)
                      ? GTEXT_YAML_SEQUENCE
                      : GTEXT_YAML_MAPPING,
                  false) != 0) {
            return GTEXT_YAML_E_WRITE;
          }
        }
        if (writer_write_string(writer, writer_newline(&writer->opts)) != 0) {
          return GTEXT_YAML_E_WRITE;
        }
      }
    } else {
      if (parent->explicit_key) {
        if (writer_write_separator(writer) != 0) return GTEXT_YAML_E_WRITE;
        if (writer_write_indent(writer, parent_indent) != 0) {
          return GTEXT_YAML_E_WRITE;
        }
        parent->explicit_key = false;
      }
      if (writer_write_string(writer, ":") != 0) return GTEXT_YAML_E_WRITE;
      if (flow || has_prefix) {
        if (writer_write_char(writer, ' ') != 0) return GTEXT_YAML_E_WRITE;
      }

      if (has_prefix) {
        if (writer_write_prefix(
                writer,
                event->anchor,
                event->tag,
                (type == YAML_WRITER_STACK_SEQUENCE)
                    ? GTEXT_YAML_SEQUENCE
                    : GTEXT_YAML_MAPPING,
                flow) != 0) {
          return GTEXT_YAML_E_WRITE;
        }
      }

      if (flow) {
        char open_char = (type == YAML_WRITER_STACK_SEQUENCE) ? '[' : '{';
        if (writer_write_char(writer, open_char) != 0) return GTEXT_YAML_E_WRITE;
      } else {
        if (writer_write_string(writer, writer_newline(&writer->opts)) != 0) {
          return GTEXT_YAML_E_WRITE;
        }
      }
    }
  }

  if (writer_stack_push(writer, type, is_key, flow, child_indent) != 0) {
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

  if (entry.flow) {
    char close_char = (type == YAML_WRITER_STACK_SEQUENCE) ? ']' : '}';
    if (writer_write_char(writer, close_char) != 0) {
      return GTEXT_YAML_E_WRITE;
    }
  }
  writer->key_absorbs_colon = false;

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
  writer_encoding_init(&writer->encoding, &writer->opts);
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
      if (!writer->in_document) {
        return GTEXT_YAML_E_STATE;
      }
      if (writer->opts.trailing_newline) {
        /* Through the separator, so a block scalar that has already ended
           its own last line does not gain a blank one - which "+" chomping
           would keep as part of the value. */
        if (writer_write_separator(writer) != 0) return GTEXT_YAML_E_WRITE;
      }
      writer->in_document = false;
      return GTEXT_YAML_OK;
    }
    case GTEXT_YAML_EVENT_DIRECTIVE:
      return GTEXT_YAML_OK;
    case GTEXT_YAML_EVENT_COMMENT:
      return writer_emit_comment(writer, event);
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
  if (writer->encoding.pending_utf8_len != 0) {
    return GTEXT_YAML_E_INVALID;
  }
  return writer->error ? GTEXT_YAML_E_WRITE : GTEXT_YAML_OK;
}
