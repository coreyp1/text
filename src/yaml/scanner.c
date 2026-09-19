/**
 * @file scanner.c
 * @brief Streaming YAML scanner/tokenizer for indicators and plain scalars.
 *
 * This scanner accepts incremental feeds (even one byte at a time).
 * It buffers input internally and exposes tokens via gtext_yaml_scanner_next().
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"
#include <stdio.h>

/* Context types for plain scalar parsing */
typedef enum {
  YAML_CONTEXT_BLOCK,        /* Block context - plain scalars can contain spaces */
  YAML_CONTEXT_FLOW_SEQUENCE, /* Inside [] - plain scalars are space-delimited */
  YAML_CONTEXT_FLOW_MAPPING   /* Inside {} - plain scalars are space-delimited */
} yaml_context_type;

/* Simple context stack (max depth 32 should be more than enough) */
#define MAX_CONTEXT_DEPTH 32

struct GTEXT_YAML_Scanner {
  GTEXT_YAML_DynBuf input; /* buffered input */
  size_t cursor;          /* next byte index to consume */
  size_t offset;          /* total bytes consumed previously (for offsets) */
  int line;
  int col;
  int finished;           /* whether finish() was called */
  int indent_ws;          /* 1 if still in indentation whitespace on this line */
  int line_indent;        /* column of the first non-space on this line, 0-based */
  int suppress_lf;        /* 1 if previous char was CR and LF should not advance line */

  int encoding_determined;
  GTEXT_YAML_Encoding encoding;
  GTEXT_YAML_DynBuf raw_prefix;
  unsigned char decode_pending[4];
  size_t decode_pending_len;
  GTEXT_YAML_Status pending_error;
  const char *pending_error_message;
  
  /* Context stack for tracking block vs flow context */
  yaml_context_type context_stack[MAX_CONTEXT_DEPTH];
  int context_depth;      /* current depth in context stack */
  
  /* Track last indicator character for tag/anchor/alias parsing */
  int last_indicator;

  /* Payload of the token most recently returned, owned here.
     SCALAR and COMMENT tokens point into this buffer; it is released when the
     next token is requested, which is the lifetime yaml_internal.h documents.
     It used to be handed to the caller instead, and every error path that
     abandoned a token without freeing it leaked - a class of bug the fuzzer
     kept finding one site at a time. */
  char *token_payload;
};

static int is_indicator_char(int c)
{
  if (c < 0) return 0;
  switch ((char)c) {
  case '-': case ':': case '?': case '[': case ']': case '{': case '}':
  case ',': case '#': case '&': case '*': case '!': case '|': case '>': case '%':
    return 1;
  default:
    return 0;
  }
}

static int scanner_peek(GTEXT_YAML_Scanner *s)
{
  if (s->cursor >= s->input.len) return -1;
  return (unsigned char)s->input.data[s->cursor];
}

static int scanner_consume(GTEXT_YAML_Scanner *s)
{
  if (s->cursor >= s->input.len) return -1;
  unsigned char c = (unsigned char)s->input.data[s->cursor++];
  s->offset++;
  if (c == '\r') {
    s->line++;
    s->col = 1;
    s->indent_ws = 1;
    s->suppress_lf = 1;
    return '\n';
  }
  if (c == '\n') {
    if (s->suppress_lf) {
      s->suppress_lf = 0;
      return '\n';
    }
    s->line++;
    s->col = 1;
    s->indent_ws = 1;
    return '\n';
  }
  s->suppress_lf = 0;
  if (s->indent_ws && c != ' ') {
    s->indent_ws = 0;
    /* col is 1-based and has not advanced past this character yet, so the
       count of spaces that preceded it on this line is col - 1. A block
       scalar reads this as its parent node's indentation. */
    s->line_indent = s->col - 1;
  }
  s->col++;
  /* When we've consumed enough that we can free the earlier prefix, do so. */
  if (s->cursor > 1024 && s->cursor * 2 > s->input.len) {
    /* drop consumed prefix */
    size_t rem = s->input.len - s->cursor;
    memmove(s->input.data, s->input.data + s->cursor, rem);
    s->input.len = rem;
    s->cursor = 0;
  }
  return c;
}

static GTEXT_YAML_Status scanner_tab_indent_error(
    const GTEXT_YAML_Scanner *s,
    GTEXT_YAML_Error *err,
    size_t rel_offset)
{
  if (err) {
    err->code = GTEXT_YAML_E_INVALID;
    err->message = "tab character used for indentation";
    err->offset = s->offset + rel_offset;
    err->line = s->line;
    err->col = s->col + (int)rel_offset;
  }
  return GTEXT_YAML_E_INVALID;
}

/* convert ASCII hex character to value, or -1 if invalid */
static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static void scanner_set_error(
    GTEXT_YAML_Scanner *s,
    GTEXT_YAML_Status code,
    const char *message)
{
  if (!s || s->pending_error != GTEXT_YAML_OK) {
    return;
  }
  s->pending_error = code;
  s->pending_error_message = message;
}

static int scanner_append_utf8_codepoint(
    GTEXT_YAML_Scanner *s,
    uint32_t codepoint)
{
  char out[4];
  size_t out_len = 0;

  if (codepoint <= 0x7F) {
    out[0] = (char)codepoint;
    out_len = 1;
  } else if (codepoint <= 0x7FF) {
    out[0] = (char)(0xC0 | (codepoint >> 6));
    out[1] = (char)(0x80 | (codepoint & 0x3F));
    out_len = 2;
  } else if (codepoint <= 0xFFFF) {
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
      return 0;
    }
    out[0] = (char)(0xE0 | (codepoint >> 12));
    out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = (char)(0x80 | (codepoint & 0x3F));
    out_len = 3;
  } else if (codepoint <= 0x10FFFF) {
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    out_len = 4;
  } else {
    return 0;
  }

  return gtext_yaml_dynbuf_append(&s->input, out, out_len);
}

static int scanner_decode_utf16(
    GTEXT_YAML_Scanner *s,
    const unsigned char *data,
    size_t len,
    int big_endian,
    int final)
{
  unsigned char buf[4];
  size_t buf_len = s->decode_pending_len;
  if (buf_len > 0) {
    memcpy(buf, s->decode_pending, buf_len);
  }

  for (size_t i = 0; i < len; i++) {
    if (buf_len < sizeof(buf)) {
      buf[buf_len++] = data[i];
    }

    for (;;) {
      if (buf_len < 2) {
        break;
      }
      uint16_t unit = big_endian
          ? (uint16_t)((buf[0] << 8) | buf[1])
          : (uint16_t)((buf[1] << 8) | buf[0]);
      if (unit >= 0xD800 && unit <= 0xDBFF) {
        if (buf_len < 4) {
          break;
        }
        uint16_t low = big_endian
            ? (uint16_t)((buf[2] << 8) | buf[3])
            : (uint16_t)((buf[3] << 8) | buf[2]);
        if (low < 0xDC00 || low > 0xDFFF) {
          scanner_set_error(s, GTEXT_YAML_E_INVALID, "invalid UTF-16 surrogate pair");
          return 0;
        }
        uint32_t codepoint = 0x10000;
        codepoint += ((uint32_t)(unit - 0xD800) << 10);
        codepoint += (uint32_t)(low - 0xDC00);
        if (!scanner_append_utf8_codepoint(s, codepoint)) {
          scanner_set_error(s, GTEXT_YAML_E_OOM, "out of memory decoding UTF-16");
          return 0;
        }
        memmove(buf, buf + 4, buf_len - 4);
        buf_len -= 4;
      } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
        scanner_set_error(s, GTEXT_YAML_E_INVALID, "invalid UTF-16 surrogate pair");
        return 0;
      } else {
        if (!scanner_append_utf8_codepoint(s, unit)) {
          scanner_set_error(s, GTEXT_YAML_E_OOM, "out of memory decoding UTF-16");
          return 0;
        }
        memmove(buf, buf + 2, buf_len - 2);
        buf_len -= 2;
      }
    }
  }

  if (final && buf_len != 0) {
    scanner_set_error(s, GTEXT_YAML_E_INVALID, "truncated UTF-16 sequence");
    return 0;
  }

  s->decode_pending_len = buf_len;
  if (buf_len > 0) {
    memcpy(s->decode_pending, buf, buf_len);
  }
  return 1;
}

static int scanner_decode_utf32(
    GTEXT_YAML_Scanner *s,
    const unsigned char *data,
    size_t len,
    int big_endian,
    int final)
{
  unsigned char buf[4];
  size_t buf_len = s->decode_pending_len;
  if (buf_len > 0) {
    memcpy(buf, s->decode_pending, buf_len);
  }

  for (size_t i = 0; i < len; i++) {
    if (buf_len < sizeof(buf)) {
      buf[buf_len++] = data[i];
    }
    if (buf_len < 4) {
      continue;
    }

    uint32_t codepoint = big_endian
        ? ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
            ((uint32_t)buf[2] << 8) | (uint32_t)buf[3]
        : ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) |
            ((uint32_t)buf[1] << 8) | (uint32_t)buf[0];

    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
      scanner_set_error(s, GTEXT_YAML_E_INVALID, "invalid UTF-32 codepoint");
      return 0;
    }

    if (!scanner_append_utf8_codepoint(s, codepoint)) {
      scanner_set_error(s, GTEXT_YAML_E_OOM, "out of memory decoding UTF-32");
      return 0;
    }

    buf_len = 0;
  }

  if (final && buf_len != 0) {
    scanner_set_error(s, GTEXT_YAML_E_INVALID, "truncated UTF-32 sequence");
    return 0;
  }

  s->decode_pending_len = buf_len;
  if (buf_len > 0) {
    memcpy(s->decode_pending, buf, buf_len);
  }
  return 1;
}

static int scanner_decode_bytes(
    GTEXT_YAML_Scanner *s,
    const unsigned char *data,
    size_t len,
    int final)
{
  if (s->encoding == GTEXT_YAML_ENCODING_UTF8) {
    if (!gtext_yaml_dynbuf_append(&s->input, (const char *)data, len)) {
      scanner_set_error(s, GTEXT_YAML_E_OOM, "out of memory buffering input");
      return 0;
    }
    return 1;
  }

  if (s->encoding == GTEXT_YAML_ENCODING_UTF16LE) {
    return scanner_decode_utf16(s, data, len, 0, final);
  }
  if (s->encoding == GTEXT_YAML_ENCODING_UTF16BE) {
    return scanner_decode_utf16(s, data, len, 1, final);
  }
  if (s->encoding == GTEXT_YAML_ENCODING_UTF32LE) {
    return scanner_decode_utf32(s, data, len, 0, final);
  }
  if (s->encoding == GTEXT_YAML_ENCODING_UTF32BE) {
    return scanner_decode_utf32(s, data, len, 1, final);
  }

  scanner_set_error(s, GTEXT_YAML_E_INVALID, "unsupported input encoding");
  return 0;
}

static int scanner_determine_encoding(GTEXT_YAML_Scanner *s, int final)
{
  if (!s || s->encoding_determined) {
    return 1;
  }

  if (s->raw_prefix.len >= 4) {
    const unsigned char *b = (const unsigned char *)s->raw_prefix.data;
    if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0xFE && b[3] == 0xFF) {
      s->encoding = GTEXT_YAML_ENCODING_UTF32BE;
      memmove(s->raw_prefix.data, s->raw_prefix.data + 4, s->raw_prefix.len - 4);
      s->raw_prefix.len -= 4;
      s->encoding_determined = 1;
      return 1;
    }
    if (b[0] == 0xFF && b[1] == 0xFE && b[2] == 0x00 && b[3] == 0x00) {
      s->encoding = GTEXT_YAML_ENCODING_UTF32LE;
      memmove(s->raw_prefix.data, s->raw_prefix.data + 4, s->raw_prefix.len - 4);
      s->raw_prefix.len -= 4;
      s->encoding_determined = 1;
      return 1;
    }
  }

  if (s->raw_prefix.len >= 3) {
    const unsigned char *b = (const unsigned char *)s->raw_prefix.data;
    if (b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
      s->encoding = GTEXT_YAML_ENCODING_UTF8;
      memmove(s->raw_prefix.data, s->raw_prefix.data + 3, s->raw_prefix.len - 3);
      s->raw_prefix.len -= 3;
      s->encoding_determined = 1;
      return 1;
    }
  }

  if (s->raw_prefix.len >= 2) {
    const unsigned char *b = (const unsigned char *)s->raw_prefix.data;
    if (b[0] == 0xFE && b[1] == 0xFF) {
      s->encoding = GTEXT_YAML_ENCODING_UTF16BE;
      memmove(s->raw_prefix.data, s->raw_prefix.data + 2, s->raw_prefix.len - 2);
      s->raw_prefix.len -= 2;
      s->encoding_determined = 1;
      return 1;
    }
    if (b[0] == 0xFF && b[1] == 0xFE) {
      s->encoding = GTEXT_YAML_ENCODING_UTF16LE;
      memmove(s->raw_prefix.data, s->raw_prefix.data + 2, s->raw_prefix.len - 2);
      s->raw_prefix.len -= 2;
      s->encoding_determined = 1;
      return 1;
    }
  }

  if (final || s->raw_prefix.len >= 4) {
    s->encoding = GTEXT_YAML_ENCODING_UTF8;
    s->encoding_determined = 1;
    return 1;
  }

  return 0;
}


/* Context stack helpers */
static yaml_context_type scanner_current_context(GTEXT_YAML_Scanner *s)
{
  if (s->context_depth == 0) return YAML_CONTEXT_BLOCK;
  return s->context_stack[s->context_depth - 1];
}

static void scanner_push_context(GTEXT_YAML_Scanner *s, yaml_context_type ctx)
{
  if (s->context_depth < MAX_CONTEXT_DEPTH) {
    s->context_stack[s->context_depth++] = ctx;
  }
}

static void scanner_pop_context(GTEXT_YAML_Scanner *s)
{
  if (s->context_depth > 0) {
    s->context_depth--;
  }
}


GTEXT_INTERNAL_API GTEXT_YAML_Scanner *gtext_yaml_scanner_new(void)
{
  /* Zeroed, so every field has a defined value before the members below set
     the ones that need something other than zero. token_payload in
     particular is freed on the first scanner_next(), which an uninitialised
     pointer would not survive. */
  GTEXT_YAML_Scanner *s = (GTEXT_YAML_Scanner *)calloc(1, sizeof(*s));
  if (!s) return NULL;
  if (!gtext_yaml_dynbuf_init(&s->input)) {
    free(s);
    return NULL;
  }
  if (!gtext_yaml_dynbuf_init(&s->raw_prefix)) {
    gtext_yaml_dynbuf_free(&s->input);
    free(s);
    return NULL;
  }
  s->cursor = 0;
  s->offset = 0;
  s->line = 1;
  s->col = 1;
  s->indent_ws = 1;
  s->suppress_lf = 0;
  s->finished = 0;
  s->encoding_determined = 0;
  s->encoding = GTEXT_YAML_ENCODING_UTF8;
  s->decode_pending_len = 0;
  s->pending_error = GTEXT_YAML_OK;
  s->pending_error_message = NULL;
  s->context_depth = 0; /* Start in block context */
  s->last_indicator = 0;
  return s;
}

GTEXT_INTERNAL_API void gtext_yaml_scanner_free(GTEXT_YAML_Scanner *s)
{
  if (!s) return;
  gtext_yaml_dynbuf_free(&s->input);
  gtext_yaml_dynbuf_free(&s->raw_prefix);
  free(s->token_payload);
  free(s);
}

GTEXT_INTERNAL_API int gtext_yaml_scanner_feed(GTEXT_YAML_Scanner *s, const char *data, size_t len)
{
  if (!s) return 0;
  if (len == 0) return 1;
  if (!s->encoding_determined) {
    if (!gtext_yaml_dynbuf_append(&s->raw_prefix, data, len)) {
      scanner_set_error(s, GTEXT_YAML_E_OOM, "out of memory buffering input");
      return 0;
    }
    if (!scanner_determine_encoding(s, s->finished)) {
      return 1;
    }
    if (!scanner_decode_bytes(s, (const unsigned char *)s->raw_prefix.data,
        s->raw_prefix.len, s->finished)) {
      return 1;
    }
    s->raw_prefix.len = 0;
    return 1;
  }

  return scanner_decode_bytes(s, (const unsigned char *)data, len, s->finished);
}

GTEXT_INTERNAL_API void gtext_yaml_scanner_finish(GTEXT_YAML_Scanner *s)
{
  if (!s) return;
  s->finished = 1;
  if (!s->encoding_determined) {
    if (!scanner_determine_encoding(s, 1)) {
      return;
    }
    if (!scanner_decode_bytes(s, (const unsigned char *)s->raw_prefix.data,
        s->raw_prefix.len, 1)) {
      return;
    }
    s->raw_prefix.len = 0;
  } else if (s->decode_pending_len > 0) {
    scanner_set_error(s, GTEXT_YAML_E_INVALID, "truncated encoded input");
  }
}

GTEXT_INTERNAL_API GTEXT_YAML_Status gtext_yaml_scanner_next(GTEXT_YAML_Scanner *s, GTEXT_YAML_Token *tok, GTEXT_YAML_Error *err)
{
  /* The previous token's payload dies here: the caller asked for another
     token, so it is done with the last one. */
  free(s->token_payload);
  s->token_payload = NULL;

  if (!s || !tok) return GTEXT_YAML_E_INVALID;

  if (!s->encoding_determined && s->finished) {
    scanner_determine_encoding(s, 1);
    if (s->encoding_determined && s->raw_prefix.len > 0) {
      scanner_decode_bytes(s, (const unsigned char *)s->raw_prefix.data,
          s->raw_prefix.len, 1);
      s->raw_prefix.len = 0;
    }
  }

  if (s->pending_error != GTEXT_YAML_OK) {
    if (err) {
      err->code = s->pending_error;
      err->message = s->pending_error_message;
      err->offset = s->offset;
      err->line = s->line;
      err->col = s->col;
    }
    return s->pending_error;
  }
  
  /* Skip whitespace and comments */
  int c;
  do {
    c = scanner_peek(s);
    if (c == -1) {
      if (!s->finished) return GTEXT_YAML_E_INCOMPLETE;
      tok->type = GTEXT_YAML_TOKEN_EOF;
      tok->offset = s->offset;
      tok->line = s->line;
      tok->col = s->col;
      s->last_indicator = 0;
      return GTEXT_YAML_OK;
    }
    if (c == '\t' && s->indent_ws) {
      return scanner_tab_indent_error(s, err, 0);
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      scanner_consume(s);
      continue;
    }
    if (c == '#') {
      size_t look = 0;
      while (s->cursor + look < s->input.len) {
        int nc = (unsigned char)s->input.data[s->cursor + look];
        if (nc == '\n' || nc == '\r') break;
        look++;
      }

      if (s->cursor + look >= s->input.len && !s->finished) {
        return GTEXT_YAML_E_INCOMPLETE;
      }

      size_t off = s->offset;
      int line = s->line;
      int col = s->col;
      bool inline_comment = s->indent_ws == 0;

      const char *raw = s->input.data + s->cursor + 1;
      size_t raw_len = look > 0 ? look - 1 : 0;

      size_t start = 0;
      while (start < raw_len && (raw[start] == ' ' || raw[start] == '\t')) {
        start++;
      }
      size_t out_len = raw_len > start ? raw_len - start : 0;
      size_t alloc_len = out_len + 1;
      char *out = (char *)malloc(alloc_len);
      if (!out) {
        if (err) {
          err->code = GTEXT_YAML_E_OOM;
          err->message = "out of memory";
        }
        return GTEXT_YAML_E_OOM;
      }
      if (out_len > 0) {
        memcpy(out, raw + start, out_len);
      }
      out[out_len] = '\0';

      for (size_t i = 0; i < look; i++) {
        scanner_consume(s);
      }

      tok->type = GTEXT_YAML_TOKEN_COMMENT;
      s->token_payload = out;
      tok->u.comment.ptr = out;
      tok->u.comment.len = out_len;
      tok->u.comment.inline_comment = inline_comment;
      tok->offset = off;
      tok->line = line;
      tok->col = col;
      s->last_indicator = 0;
      return GTEXT_YAML_OK;
    }
    break;
  } while (1);

  size_t off = s->offset;
  int line = s->line, col = s->col;

  /* Buffer state (debug prints removed) */

  /* Directive lines start with '%' at column 1. */
  if (c == '%' && col == 1) {
    size_t look = 1;
    size_t end = 0;
    while (s->cursor + look < s->input.len) {
      int nc = (unsigned char)s->input.data[s->cursor + look];
      if (nc == '\n' || nc == '\r') break;
      look++;
    }

    if (s->cursor + look >= s->input.len && !s->finished) {
      return GTEXT_YAML_E_INCOMPLETE;
    }

    end = look;

    /* Copy directive line (excluding leading '%') and trim comments/whitespace. */
    size_t raw_len = end > 1 ? end - 1 : 0;
    const char *raw = s->input.data + s->cursor + 1;
    size_t trim_end = raw_len;

    for (size_t i = 0; i < raw_len; i++) {
      if (raw[i] == '#') {
        if (i == 0 || isspace((unsigned char)raw[i - 1])) {
          trim_end = i;
          break;
        }
      }
    }

    while (trim_end > 0 && isspace((unsigned char)raw[trim_end - 1])) {
      trim_end--;
    }

    size_t start = 0;
    while (start < trim_end && isspace((unsigned char)raw[start])) {
      start++;
    }

    size_t out_len = trim_end > start ? trim_end - start : 0;
    size_t alloc_len = out_len > 0 ? out_len : 1;
    char *out = (char *)malloc(alloc_len);
    if (!out) {
      if (err) {
        err->code = GTEXT_YAML_E_OOM;
        err->message = "out of memory";
      }
      return GTEXT_YAML_E_OOM;
    }
    if (out_len > 0) {
      memcpy(out, raw + start, out_len);
    } else {
      out[0] = '\0';
    }

    /* Consume the directive line and line break. */
    for (size_t i = 0; i < end; i++) {
      scanner_consume(s);
    }
    if (scanner_peek(s) == '\r') {
      scanner_consume(s);
    }
    if (scanner_peek(s) == '\n') {
      scanner_consume(s);
    }

    tok->type = GTEXT_YAML_TOKEN_DIRECTIVE;
    s->token_payload = out;
    tok->u.scalar.ptr = out;
    tok->u.scalar.len = out_len;
    tok->offset = off;
    tok->line = line;
    tok->col = col;
    s->last_indicator = 0;
    return GTEXT_YAML_OK;
  }

  /* Special-case block scalars '|' and '>' to parse them into a scalar token. */
  if (c == '|' || c == '>') {
    int style = c; /* '|' literal, '>' folded */
    /* consume the indicator */
    scanner_consume(s);
    /* The indentation of the line the header sits on. An indentation
       indicator counts from the parent node, and the parent node begins at
       the first non-space character of this line - the key, the "-" or the
       "?" that owns the scalar, or the indicator itself at the root. */
    size_t parent_indent = (size_t)s->line_indent;
    /* optional chomping/indent indicator: ([+-])?(\d+)?
       capture values so we can implement chomping behavior and explicit indent. */
    int ch = scanner_peek(s);
    int chomping = 0; /* 0=clip(default), 1=keep(+), -1=strip(-) */
    size_t explicit_indent = 0; /* 0 == none provided */
    if (ch == '+' || ch == '-') {
      if (ch == '+') chomping = 1; else chomping = -1;
      scanner_consume(s);
      ch = scanner_peek(s);
    }
    /* optional indentation indicator (one or more digits) */
    if (ch >= '0' && ch <= '9') {
      size_t val = 0;
      while ((ch = scanner_peek(s)) >= '0' && ch <= '9') {
        val = val * 10 + (ch - '0');
        scanner_consume(s);
      }
      if (val > 0) explicit_indent = val;
    }
    /* consume the rest of the line (possible comments) up to newline */
    int header_break = 0;
    while (1) {
      int p = scanner_peek(s);
      if (p == -1) {
        if (!s->finished) return GTEXT_YAML_E_INCOMPLETE;
        /* End of input inside the block scalar header. There is no newline
           left to find and nothing left to consume, so stop: falling through
           to scanner_consume() left the cursor where it was and peeked -1
           again on the next pass, which spun forever. Two bytes - ">[" -
           were enough to reach it. */
        s->last_indicator = 0;
        break;
      }
      scanner_consume(s);
      if (p == '\n' || p == '\r') { header_break = p; break; }
    }
    /* Only a CR needs its LF skipped. Consuming a newline unconditionally here
       swallowed the block's first line whenever that line was empty, so
       "a: |" followed by a blank line lost the break the blank stands for. */
    if (header_break == '\r' && scanner_peek(s) == '\n') {
      scanner_consume(s);
    }

  /* Collect the block scalar's lines verbatim, with their breaks normalised
     to LF. Indentation is stripped and the breaks folded afterwards. */
    GTEXT_YAML_DynBuf scalar;
    if (!gtext_yaml_dynbuf_init(&scalar)) return GTEXT_YAML_E_OOM;

    /* YAML 1.2.2 8.1.1.1. With an indentation indicator the block's
       indentation is the parent node's plus the indicator. With none it is
       the indentation of the first non-empty line. An empty line may be
       indented less than the block; a non-empty line indented less ends it. */
    size_t block_indent = 0;
    if (explicit_indent > 0) {
      block_indent = parent_indent + explicit_indent;
    } else {
      size_t scan = s->cursor;
      bool detected = false;
      while (scan < s->input.len) {
        size_t sp = scan;
        while (sp < s->input.len && s->input.data[sp] == ' ') sp++;
        if (sp >= s->input.len) break;
        char pc = s->input.data[sp];
        if (pc == '\n' || pc == '\r') { /* empty line: carries no indentation */
          scan = sp + 1;
          if (pc == '\r' && scan < s->input.len && s->input.data[scan] == '\n') scan++;
          continue;
        }
        block_indent = sp - scan;
        detected = true;
        break;
      }
      if (!detected) {
        if (!s->finished) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_INCOMPLETE; }
        block_indent = 0;
      } else if (block_indent <= parent_indent) {
        /* The content of a block scalar is indented further than the node
           that owns it. A first non-empty line at or left of the parent means
           this scalar is empty and that line belongs to what follows, so set
           a requirement no line can meet. */
        block_indent = (size_t)-1;
      }
    }

    /* This scanner consumes destructively and cannot rewind, so asking for
       more input after a line has been taken would discard that line. Only a
       block that has yielded nothing yet can be deferred; past that point the
       lines already in hand are accepted. */
#define GTEXT_YAML_BLOCK_NEED_MORE() \
      do { \
        if (scalar.len == 0) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_INCOMPLETE; } \
        goto block_scalar_collected; \
      } while (0)

    for (;;) {
      if (s->cursor >= s->input.len) {
        if (!s->finished) GTEXT_YAML_BLOCK_NEED_MORE();
        break;
      }
      /* Measure the line before taking any of it: it is only consumed once
         it is known to belong to the block and to be complete. */
      size_t sp = s->cursor;
      size_t spaces = 0;
      while (sp < s->input.len && s->input.data[sp] == ' ') { sp++; spaces++; }
      if (sp >= s->input.len && !s->finished) GTEXT_YAML_BLOCK_NEED_MORE();
      bool line_empty = (sp >= s->input.len)
        || s->input.data[sp] == '\n' || s->input.data[sp] == '\r';
      if (!line_empty && spaces < block_indent) {
        /* A tab where the block's indentation should be is a tab used for
           indentation. Past that column a tab is ordinary content. */
        if (s->input.data[sp] == '\t') {
          gtext_yaml_dynbuf_free(&scalar);
          return scanner_tab_indent_error(s, err, spaces);
        }
        break; /* dedent ends the block scalar */
      }

      size_t take = 0;
      size_t scan = s->cursor;
      bool saw_break = false;
      while (scan < s->input.len) {
        char cc = s->input.data[scan];
        if (cc == '\n') { take = scan + 1 - s->cursor; saw_break = true; break; }
        if (cc == '\r') {
          take = scan + 1 - s->cursor;
          if (scan + 1 < s->input.len) {
            if (s->input.data[scan + 1] == '\n') take++;
          } else if (!s->finished) {
            GTEXT_YAML_BLOCK_NEED_MORE();
          }
          saw_break = true;
          break;
        }
        scan++;
      }
      if (!saw_break) {
        if (!s->finished) GTEXT_YAML_BLOCK_NEED_MORE();
        take = s->input.len - s->cursor;
      }

      size_t content_bytes = take;
      if (saw_break) {
        content_bytes = take - 1;
        if (content_bytes > 0 && s->input.data[s->cursor + content_bytes - 1] == '\r') {
          content_bytes--;
        }
      }
      if (content_bytes > 0
          && !gtext_yaml_dynbuf_append(&scalar, s->input.data + s->cursor, content_bytes)) {
        gtext_yaml_dynbuf_free(&scalar);
        return GTEXT_YAML_E_OOM;
      }
      if (saw_break) {
        char nl = '\n';
        if (!gtext_yaml_dynbuf_append(&scalar, &nl, 1)) {
          gtext_yaml_dynbuf_free(&scalar);
          return GTEXT_YAML_E_OOM;
        }
      }
      /* Consume a counted number of bytes rather than up to an absolute
         position: scanner_consume() may compact the buffer and reset the
         cursor, which would make any position recorded above meaningless. */
      for (size_t i = 0; i < take; i++) (void)scanner_consume(s);
    }

block_scalar_collected:
#undef GTEXT_YAML_BLOCK_NEED_MORE
    ; /* C17 requires a statement after a label */

    /* Apply 8.1.2 (literal) or 8.1.3 (folded) to the interior breaks, then
       8.1.1.2 chomping to the trailing ones. */
    char *out = NULL;
    size_t out_len = 0;
    {
      const size_t in_len = scalar.len;
      const char *in_buf = scalar.data;

      size_t line_count = 0;
      for (size_t i = 0; i < in_len; i++) {
        if (in_buf[i] == '\n') line_count++;
      }
      if (in_len > 0 && in_buf[in_len - 1] != '\n') line_count++;

      size_t *starts = NULL;
      size_t *lens = NULL;
      if (line_count > 0) {
        starts = (size_t *)malloc(line_count * sizeof *starts);
        lens = (size_t *)malloc(line_count * sizeof *lens);
        if (!starts || !lens) {
          free(starts); free(lens);
          gtext_yaml_dynbuf_free(&scalar);
          return GTEXT_YAML_E_OOM;
        }
      }

      size_t pos = 0;
      for (size_t li = 0; li < line_count; li++) {
        size_t end = pos;
        while (end < in_len && in_buf[end] != '\n') end++;
        size_t skip = 0;
        while (skip < block_indent && pos + skip < end && in_buf[pos + skip] == ' ') skip++;
        starts[li] = pos + skip;
        lens[li] = end - (pos + skip);
        pos = (end < in_len) ? end + 1 : end;
      }

      size_t last_content = (size_t)-1;
      for (size_t li = 0; li < line_count; li++) {
        if (lens[li] > 0) last_content = li;
      }

      /* Content, one separator per line, and the trailing breaks. */
      char *tmp = (char *)malloc(in_len + 4 * line_count + 8);
      if (!tmp) {
        free(starts); free(lens);
        gtext_yaml_dynbuf_free(&scalar);
        return GTEXT_YAML_E_OOM;
      }
      size_t out_pos = 0;

      if (last_content != (size_t)-1) {
        size_t li = 0;
        /* Leading empty lines each stand for one break in both styles. */
        while (li <= last_content && lens[li] == 0) { tmp[out_pos++] = '\n'; li++; }
        for (; li <= last_content; li++) {
          memcpy(tmp + out_pos, in_buf + starts[li], lens[li]);
          out_pos += lens[li];
          if (li == last_content) break;
          if (style != '>') { tmp[out_pos++] = '\n'; continue; }

          /* Folding: a run of empty lines yields one break each rather than
             a space, and a break next to a more-indented line is kept. */
          size_t blanks = 0;
          size_t nxt = li + 1;
          while (nxt <= last_content && lens[nxt] == 0) { blanks++; nxt++; }
          const bool cur_more = in_buf[starts[li]] == ' ' || in_buf[starts[li]] == '\t';
          const bool nxt_more = nxt <= last_content
            && (in_buf[starts[nxt]] == ' ' || in_buf[starts[nxt]] == '\t');
          if (blanks > 0) {
            if (cur_more) tmp[out_pos++] = '\n';
            for (size_t b = 0; b < blanks; b++) tmp[out_pos++] = '\n';
          } else if (cur_more || nxt_more) {
            tmp[out_pos++] = '\n';
          } else {
            tmp[out_pos++] = ' ';
          }
          li = nxt - 1; /* the loop's own increment moves to nxt */
        }
      }

      /* The final line break plus any trailing empty lines. With no content
         at all there is no final break, only the empty lines themselves. */
      size_t trailing = (last_content != (size_t)-1)
        ? 1 + (line_count - 1 - last_content)
        : line_count;
      if (chomping == -1) {
        trailing = 0; /* strip */
      } else if (chomping == 0) {
        trailing = (last_content != (size_t)-1) ? 1 : 0; /* clip */
      }
      for (size_t i = 0; i < trailing; i++) tmp[out_pos++] = '\n';

      free(starts);
      free(lens);

      /* malloc(0) may legally return NULL; ask for a byte so the check below
         only ever signals a genuine allocation failure. */
      out = (char *)malloc(out_pos ? out_pos : 1);
      if (!out) { free(tmp); gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
      if (out_pos) memcpy(out, tmp, out_pos);
      out_len = out_pos;
      free(tmp);
    }

  gtext_yaml_dynbuf_free(&scalar);

    tok->type = GTEXT_YAML_TOKEN_SCALAR;
    tok->scalar_style = (style == '>')
      ? GTEXT_YAML_SCALAR_STYLE_FOLDED
      : GTEXT_YAML_SCALAR_STYLE_LITERAL;
  s->token_payload = out;
  tok->u.scalar.ptr = out;
  tok->u.scalar.len = out_len;
    tok->offset = off;
    tok->line = line;
    tok->col = col;
  /* finished block scalar */
    return GTEXT_YAML_OK;
  }

  /* Check for document markers: "---" and "..." */
  if (c == '-' || c == '.') {
    /* Need to peek ahead for 3 characters total */
    if (s->cursor + 2 < s->input.len) {
      int c1 = (unsigned char)s->input.data[s->cursor + 1];
      int c2 = (unsigned char)s->input.data[s->cursor + 2];
      
      if (c1 == c && c2 == c) {
        /* We have three identical '-' or '.' chars. Check if followed by whitespace or EOF. */
        int c3 = (s->cursor + 3 < s->input.len) ? (unsigned char)s->input.data[s->cursor + 3] : -1;
        
        /* If c3 is -1 and we're not finished, we need more data to decide */
        if (c3 == -1 && !s->finished) {
          return GTEXT_YAML_E_INCOMPLETE;
        }
        
        /* Document markers must be followed by whitespace, newline, or EOF */
        if (c3 == -1 || c3 == ' ' || c3 == '\t' || c3 == '\r' || c3 == '\n') {
          /* Consume all three characters */
          scanner_consume(s);
          scanner_consume(s);
          scanner_consume(s);
          
          tok->type = (c == '-') ? GTEXT_YAML_TOKEN_DOCUMENT_START : GTEXT_YAML_TOKEN_DOCUMENT_END;
          tok->offset = off;
          tok->line = line;
          tok->col = col;
          s->last_indicator = 0;
          return GTEXT_YAML_OK;
        }
        /* Otherwise, it's not a document marker (e.g., "---abc"), fall through to indicator handling */
      }
    } else if (!s->finished) {
      /* Need more data to determine if this is a document marker */
      return GTEXT_YAML_E_INCOMPLETE;
    }
  }

  /* General single-byte indicators (e.g., '-', ':', '*', '&', ',', etc.) */
  if (is_indicator_char(c)) {
    /* Update context stack for flow collection boundaries */
    if (c == '[') {
      scanner_push_context(s, YAML_CONTEXT_FLOW_SEQUENCE);
    } else if (c == '{') {
      scanner_push_context(s, YAML_CONTEXT_FLOW_MAPPING);
    } else if (c == ']' || c == '}') {
      scanner_pop_context(s);
    }
    
    scanner_consume(s);
    tok->type = GTEXT_YAML_TOKEN_INDICATOR;
    tok->u.c = (char)c;
    tok->offset = off;
    tok->line = line;
    tok->col = col;
    
    /* Track if this is an anchor, alias, or tag indicator */
    if (c == '&' || c == '*' || c == '!') {
      s->last_indicator = c;
    } else {
      s->last_indicator = 0;
    }
    
    return GTEXT_YAML_OK;
  }

  /* Quoted scalars: single-quoted ('') and double-quoted (") */
  if (c == '\'' || c == '"') {
    int quote = c;
    GTEXT_YAML_DynBuf scalar;
    if (!gtext_yaml_dynbuf_init(&scalar)) return GTEXT_YAML_E_OOM;

    size_t look = 1; /* we will peek starting after the opening quote */
    for (;;) {
      int nc;
      if (s->cursor + look >= s->input.len) {
        nc = -1;
      } else {
        nc = (unsigned char)s->input.data[s->cursor + look];
      }
      if (nc == -1) break;

      if (quote == '\'') {
        /* single-quoted: two single-quotes -> one quote, otherwise end */
        if (nc == '\'') {
          /* check next char to see if it's an escaped single-quote */
          if (s->cursor + look + 1 >= s->input.len) {
            /* need one more byte to decide */
            break;
          }
          int nextc = (unsigned char)s->input.data[s->cursor + look + 1];
          if (nextc == '\'') {
            char ch = '\'';
            if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            look += 2;
            continue;
          }
          /* otherwise a lone quote marks end of scalar */
          break;
        }
        if (nc == '\r') {
          char ch = '\n';
          if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
          if (s->cursor + look + 1 < s->input.len && s->input.data[s->cursor + look + 1] == '\n') {
            look += 2;
          } else {
            look++;
          }
          continue;
        }
        /* normal character inside single-quoted scalar */
        char ch = (char)nc;
        if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
        look++;
        continue;
      }

      /* double-quoted handling */
      if (quote == '"') {
        if (nc == '\r') {
          char ch = '\n';
          if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
          if (s->cursor + look + 1 < s->input.len && s->input.data[s->cursor + look + 1] == '\n') {
            look += 2;
          } else {
            look++;
          }
          continue;
        }
        if (nc == '"') {
          /* end of double-quoted scalar */
          break;
        }
        if (nc == '\\') {
          /* escape sequence; need next char */
          if (s->cursor + look + 1 >= s->input.len) {
            /* incomplete escape */
            break;
          }
          int esc = (unsigned char)s->input.data[s->cursor + look + 1];
          if (esc == 'n' || esc == 'r' || esc == 't' || esc == '"' || esc == '\\' ||
              esc == '0' || esc == 'a' || esc == 'b' || esc == 'f' || esc == 'v' || esc == 'e') {
            char outc;
            switch (esc) {
              case 'n': outc = '\n'; break;
              case 'r': outc = '\r'; break;
              case 't': outc = '\t'; break;
              case '"': outc = '"'; break;
              case '\\': outc = '\\'; break;
              case '0': outc = '\0'; break;
              case 'a': outc = '\a'; break;
              case 'b': outc = '\b'; break;
              case 'f': outc = '\f'; break;
              case 'v': outc = '\v'; break;
              case 'e': outc = '\x1B'; break;  /* ESC character */
              default: outc = (char)esc; break;
            }
            if (!gtext_yaml_dynbuf_append(&scalar, &outc, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            look += 2;
            continue;
          }
          /* hex escape: \xNN (2 hex digits) */
          if (esc == 'x') {
            /* need two hex digits beyond the 'x' */
            if (s->cursor + look + 3 >= s->input.len) break;
            int h1 = (unsigned char)s->input.data[s->cursor + look + 2];
            int h2 = (unsigned char)s->input.data[s->cursor + look + 3];
            int v1 = hexval(h1);
            int v2 = hexval(h2);
            if (v1 < 0 || v2 < 0) {
              /* invalid hex -> conservative treat as literal chars */
              char c1 = (char)h1; char c2 = (char)h2;
              if (!gtext_yaml_dynbuf_append(&scalar, &c1, 1) || !gtext_yaml_dynbuf_append(&scalar, &c2, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
              look += 4; continue;
            }
            char outc = (char)((v1 << 4) | v2);
            if (!gtext_yaml_dynbuf_append(&scalar, &outc, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            look += 4; continue;
          }
          /* unicode escapes: \uNNNN (4 hex) and \UNNNNNNNN (8 hex) */
          if (esc == 'u' || esc == 'U') {
            int need = (esc == 'u') ? 4 : 8;
            if (s->cursor + look + 1 + need >= s->input.len) break;
            unsigned int code = 0;
            for (int i = 0; i < need; ++i) {
              int h = (unsigned char)s->input.data[s->cursor + look + 2 + i];
              int v = hexval(h);
              if (v < 0) { code = 0xFFFD; break; } /* replacement char on invalid hex */
              code = (code << 4) | (unsigned int)v;
            }
            /* encode codepoint into UTF-8 bytes */
            char utf8buf[4]; int utf8len = 0;
            if (code <= 0x7F) { utf8buf[0] = (char)code; utf8len = 1; }
            else if (code <= 0x7FF) { utf8buf[0] = (char)(0xC0 | ((code >> 6) & 0x1F)); utf8buf[1] = (char)(0x80 | (code & 0x3F)); utf8len = 2; }
            else if (code <= 0xFFFF) { utf8buf[0] = (char)(0xE0 | ((code >> 12) & 0x0F)); utf8buf[1] = (char)(0x80 | ((code >> 6) & 0x3F)); utf8buf[2] = (char)(0x80 | (code & 0x3F)); utf8len = 3; }
            else { utf8buf[0] = (char)(0xF0 | ((code >> 18) & 0x07)); utf8buf[1] = (char)(0x80 | ((code >> 12) & 0x3F)); utf8buf[2] = (char)(0x80 | ((code >> 6) & 0x3F)); utf8buf[3] = (char)(0x80 | (code & 0x3F)); utf8len = 4; }
            if (!gtext_yaml_dynbuf_append(&scalar, utf8buf, utf8len)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            look += 2 + need; continue;
          }
          /* Unknown escape form: conservatively copy escaped char verbatim */
          char outc = (char)esc;
          if (!gtext_yaml_dynbuf_append(&scalar, &outc, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
          look += 2;
          continue;
        }
        /* normal character inside double-quoted scalar */
        char ch = (char)nc;
        if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
        look++;
        continue;
      }
    }

    /* If we reached end of buffer and haven't seen the closing quote, it's incomplete */
    if ((s->cursor + look) >= s->input.len && !s->finished) {
      gtext_yaml_dynbuf_free(&scalar);
      return GTEXT_YAML_E_INCOMPLETE;
    }

    /* At this point, s->cursor+look points at either closing-quote or EOF. If EOF and finished==1,
       then we consider it an error (unterminated quote). */
    if (s->cursor + look >= s->input.len) {
      gtext_yaml_dynbuf_free(&scalar);
      if (err) { err->code = GTEXT_YAML_E_INVALID; err->message = "unterminated quoted scalar"; err->offset = off; err->line = line; err->col = col; }
      return GTEXT_YAML_E_INVALID;
    }

    /* s->input.data[s->cursor + look] is the closing quote; consume opening quote + content + closing quote */
    size_t total_consume = look + 1; /* includes opening quote at cursor */
    for (size_t i = 0; i < total_consume; ++i) scanner_consume(s);

    /* Validate UTF-8 of assembled scalar */
    if (!gtext_utf8_validate(scalar.data, scalar.len)) {
      if (err) { err->code = GTEXT_YAML_E_INVALID; err->message = "invalid UTF-8 in quoted scalar"; err->offset = off; err->line = line; err->col = col; }
      gtext_yaml_dynbuf_free(&scalar);
      return GTEXT_YAML_E_INVALID;
    }

    /* Allocate the output buffer.
       An empty quoted scalar - `a: ""`, which is ordinary YAML rather than
       anything malformed - leaves scalar.data NULL and scalar.len 0. Two
       things went wrong there: malloc(0) may return NULL, which this would
       have reported as an allocation failure, and memcpy() declares both
       pointers non-null even for a zero length, so passing the NULL was
       undefined behavior. Ask for at least one byte, and skip the copy when
       there is nothing to copy. */
    size_t slen = scalar.len;
    char *out = (char *)malloc(slen ? slen : 1);
    if (!out) { gtext_yaml_dynbuf_free(&scalar); if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; } return GTEXT_YAML_E_OOM; }
    if (slen) { memcpy(out, scalar.data, slen); }
    gtext_yaml_dynbuf_free(&scalar);

    tok->type = GTEXT_YAML_TOKEN_SCALAR;
    tok->scalar_style = (quote == '\'')
      ? GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED
      : GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED;
    s->token_payload = out;
    tok->u.scalar.ptr = out;
    tok->u.scalar.len = slen;
    tok->offset = off;
    tok->line = line;
    tok->col = col;
    s->last_indicator = 0;
    return GTEXT_YAML_OK;
  }

  /* collect scalar into temp dynbuf without consuming input yet. We peek
     ahead to determine token completeness; only consume when token is
     confirmed complete to avoid losing bytes on incremental feeds. */
  GTEXT_YAML_DynBuf scalar;
  if (!gtext_yaml_dynbuf_init(&scalar)) return GTEXT_YAML_E_OOM;

  yaml_context_type ctx = scanner_current_context(s);
  
  /* If this scalar follows an anchor/alias indicator, it must be space-delimited
     (anchor/alias names cannot contain spaces per YAML spec) */
  int require_space_delimiter = (s->last_indicator == '&' || s->last_indicator == '*' || s->last_indicator == '!');
  
  size_t look = 0;
  while (1) {
    if (s->cursor + look >= s->input.len) {
      c = -1;
    } else {
      c = (unsigned char)s->input.data[s->cursor + look];
    }
    if (c == -1) break;
    
    /* Context-aware whitespace handling */
    if (ctx == YAML_CONTEXT_BLOCK && !require_space_delimiter) {
      /* In block context, plain scalars can contain spaces and tabs,
         but end at newlines or when followed by structural indicators */
      if (c == '\r' || c == '\n') break;
      
      /* Handle spaces: look ahead to determine if this is a separator or part of value */
      if (c == ' ' || c == '\t') {
        /* Skip whitespace to see what's after */
        size_t ws_len = 1;
        while (s->cursor + look + ws_len < s->input.len) {
          int peek_c = (unsigned char)s->input.data[s->cursor + look + ws_len];
          if (peek_c != ' ' && peek_c != '\t') break;
          ws_len++;
        }
        
        /* Check what follows the whitespace */
        int next_c = -1;
        if (s->cursor + look + ws_len < s->input.len) {
          next_c = (unsigned char)s->input.data[s->cursor + look + ws_len];
        }
        
        /* Break at end of input or end of line. */
        if (next_c == -1 || next_c == '\r' || next_c == '\n') break;

        /* " #" starts a comment (7.3.3): a '#' only does so when it follows
           whitespace, which is exactly the case being looked at here. */
        if (next_c == '#') break;

        /* ": " is the value indicator and ends the scalar.  A ':' followed by
           anything else is ordinary content, so " :b" is not a terminator. */
        if (next_c == ':') {
          int after_colon = -1;
          if (s->cursor + look + ws_len + 1 < s->input.len) {
            after_colon =
                (unsigned char)s->input.data[s->cursor + look + ws_len + 1];
          }
          if (after_colon == -1 || after_colon == ' ' || after_colon == '\t'
              || after_colon == '\r' || after_colon == '\n') {
            break;
          }
        }

        /* Everything else - '-', '?', ',', '[', ']', '{', '}', '&', '*', '!',
           '|', '>', '%' - is an indicator only where a node may begin, not in
           the middle of one.  In block context they are plain content, so
           "a - b", "a, b" and "1 - 2" keep their whole value.  Breaking here
           is what silently truncated them. */
        
        /* Only include space if we've already collected significant content
           AND what follows looks like continuation of the same value */
        if (scalar.len > 0) {
          char ch = (char)c;
          if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) {
            gtext_yaml_dynbuf_free(&scalar);
            if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; }
            return GTEXT_YAML_E_OOM;
          }
          look++;
          continue;
        } else {
          /* Leading space - break */
          break;
        }
      }
      
      /* Check for key-value separator */
      if (c == ':') {
        int next_c = -1;
        if (s->cursor + look + 1 < s->input.len) {
          next_c = (unsigned char)s->input.data[s->cursor + look + 1];
        }
        if (next_c == -1 || next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c ==  '\n') {
          break;
        }
      }
      
      /* The remaining indicators matter only at the point where a node may
         begin.  Once the scalar has content they are plain characters, so
         "a#b", "end-" and "a ! b" are whole values.  c-indicator (5.3)
         applies to the first character; ns-plain-char (7.3.3) to the rest. */
      if (scalar.len == 0) {
        /* "- " and "? " begin a block entry or an explicit key. */
        if (c == '-' || c == '?') {
          int next_c = -1;
          if (s->cursor + look + 1 < s->input.len) {
            next_c = (unsigned char)s->input.data[s->cursor + look + 1];
          }
          if (next_c == -1 || next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
            break;
          }
        }

        if (c == '#' || c == '&' || c == '*' || c == '!') break;
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') break;
        if (c == '|' || c == '>' || c == '%') break;
      }
    } else {
      /* In flow context, and for anchor/alias names, a plain scalar is
         space-delimited. */
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;

      /* The flow indicators always end the scalar: without them the
         collection could never be closed. */
      if (c == ',' || c == '[' || c == ']' || c == '{' || c == '}') break;

      /* ':' separates a key from its value here regardless of what follows. */
      if (c == ':') break;

      /* The rest are indicators only where a node may begin (5.3).  Mid-scalar
         they are content, so "[a-b, c]" holds "a-b" rather than ending the
         scalar at the dash. */
      if (scalar.len == 0 && is_indicator_char(c)) {
        if (!(s->last_indicator == '!' && c == '!')) break;
      }
    }
    
    char ch = (char)c;
    if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) {
      gtext_yaml_dynbuf_free(&scalar);
      if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; }
      return GTEXT_YAML_E_OOM;
    }
    look++;
  }

  /* Debug printing removed; scanner emits tokens without runtime diagnostics */

  if (scalar.len == 0) {
    gtext_yaml_dynbuf_free(&scalar);
    /* This can happen if we hit EOF mid-scalar and finished not set */
    if (!s->finished && scanner_peek(s) == -1) {
      return GTEXT_YAML_E_INCOMPLETE;
    }
    /* otherwise, treat as EOF */
    tok->type = GTEXT_YAML_TOKEN_EOF;
    tok->offset = s->offset;
    tok->line = s->line;
    tok->col = s->col;
    s->last_indicator = 0;
    return GTEXT_YAML_OK;
  }

  /* If our lookahead reached the end of the current input buffer and the
     scanner hasn't been marked finished, it's a partial scalar. Signal
     INCOMPLETE so the caller will provide more data before we emit. */
  if ((s->cursor + look) >= s->input.len && !s->finished) {
    gtext_yaml_dynbuf_free(&scalar);
    return GTEXT_YAML_E_INCOMPLETE;
  }

  /* Validate UTF-8 */
  if (!gtext_utf8_validate(scalar.data, scalar.len)) {
    if (err) {
      err->code = GTEXT_YAML_E_INVALID;
      err->message = "invalid UTF-8 in scalar";
      err->offset = off;
      err->line = line;
      err->col = col;
    }
    gtext_yaml_dynbuf_free(&scalar);
    return GTEXT_YAML_E_INVALID;
  }

  /* On success, allocate heap buffer to hold scalar for token lifetime. */
  size_t slen = scalar.len;
  char *out = (char *)malloc(slen);
  if (!out) {
    gtext_yaml_dynbuf_free(&scalar);
    if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; }
    return GTEXT_YAML_E_OOM;
  }
  memcpy(out, scalar.data, slen);
  gtext_yaml_dynbuf_free(&scalar);

  /* Now actually consume the bytes we peeked earlier so scanner state
     advances consistently with what we emitted. */
  /* show cursor/len before consuming scalar (quiet) */
  for (size_t i = 0; i < look; ++i) {
    scanner_consume(s);
  }
  /* after consume (quiet) */

  tok->type = GTEXT_YAML_TOKEN_SCALAR;
  tok->scalar_style = GTEXT_YAML_SCALAR_STYLE_PLAIN;
  s->token_payload = out;
  tok->u.scalar.ptr = out;
  tok->u.scalar.len = slen;
  /* scalar emitted */
  tok->offset = off;
  tok->line = line;
  tok->col = col;
  
  /* Reset anchor/alias flag after emitting any scalar */
  s->last_indicator = 0;
  
  return GTEXT_YAML_OK;
}
