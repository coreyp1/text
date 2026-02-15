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
  s->col++;
  if (s->indent_ws && c != ' ') {
    s->indent_ws = 0;
  }
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
  GTEXT_YAML_Scanner *s = (GTEXT_YAML_Scanner *)malloc(sizeof(*s));
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
  
  /* Skip whitespace */
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
    while (1) {
      int p = scanner_peek(s);
      if (p == -1) {
        if (!s->finished) return GTEXT_YAML_E_INCOMPLETE;
        s->last_indicator = 0;
      }
      scanner_consume(s);
      if (p == '\n' || p == '\r') break;
    }
    if (scanner_peek(s) == '\n') {
      scanner_consume(s);
    }

  /* Collect following indented lines as scalar. If explicit_indent > 0, use that
     as the indentation requirement for content lines; otherwise collect any
     indented lines and compute min indent from non-blank lines later. */
    GTEXT_YAML_DynBuf scalar;
    if (!gtext_yaml_dynbuf_init(&scalar)) return GTEXT_YAML_E_OOM;

    for (;;) {
      /* Peek using a local position so we only consume from the real cursor
         after we've confirmed the line is complete. */
      if (s->cursor >= s->input.len) {
        /* if we have already collected some lines, accept them; otherwise ask for more */
        if (scalar.len == 0 && !s->finished) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_INCOMPLETE; }
        break;
      }
      size_t pos = s->cursor;
      size_t indent_pos = pos;
      while (indent_pos < s->input.len) {
        char ch = s->input.data[indent_pos];
        if (ch == ' ') {
          indent_pos++;
          continue;
        }
        if (ch == '\t') {
          gtext_yaml_dynbuf_free(&scalar);
          return scanner_tab_indent_error(s, err, indent_pos - s->cursor);
        }
        break;
      }
      int first = (unsigned char)s->input.data[pos];
      if (explicit_indent > 0) {
        size_t colcount = 0;
        size_t pp = pos;
        while (pp < s->input.len && (s->input.data[pp] == ' ' || s->input.data[pp] == '\t')) { colcount++; pp++; }
        if (colcount < explicit_indent) break;
      } else {
        if (first != ' ' && first != '\t' && first != '\n' && first != '\r') break;
      }

      /* Scan forward to the end of this line into pos2 while collecting
         bytes into a temporary buffer; only commit consumption if we saw
         a terminating newline (i.e., the line is complete). */
      size_t pos2 = pos;
      int saw_nl = 0;
      while (pos2 < s->input.len) {
        int cc = (unsigned char)s->input.data[pos2++];
        if (cc == '\r') {
          char nl = '\n';
          if (!gtext_yaml_dynbuf_append(&scalar, &nl, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
          if (pos2 < s->input.len && s->input.data[pos2] == '\n') {
            pos2++;
          }
          saw_nl = 1;
          break;
        }
        char chch = (char)cc;
        if (!gtext_yaml_dynbuf_append(&scalar, &chch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
        if (cc == '\n') { saw_nl = 1; break; }
      }
      if (!saw_nl) {
        /* we reached buffer end without newline; if not finished, ask for more data */
        if (!s->finished) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_INCOMPLETE; }
        /* if finished and no newline, still accept the remaining bytes */
      }

      /* commit consumption up to pos2 */
      while (s->cursor < pos2) {
        int cc = scanner_consume(s);
        (void)cc;
      }
    }

    /* Post-process collected lines:
       - compute minimum indentation from non-blank lines and remove it
       - apply folding if style == '>' (convert line breaks to spaces except between paragraph breaks)
       - apply chomping: already consumed chomping indicator but we recorded none; handle default clip: remove single trailing newline
       Tabs in indentation are rejected; tabs in content are preserved.
    */
    size_t in_len = scalar.len;
    char *in_buf = scalar.data; /* owned by dynbuf */

    /* Quick path: empty collected content -> empty scalar */
    char *out = NULL;
    size_t out_len = 0;
    if (in_len == 0) {
      out = (char *)malloc(0);
      if (!out) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
      out_len = 0;
    } else {
      /* Split into lines and compute min indent */
  size_t min_indent = (size_t)-1;
      size_t line_start = 0;
      int any_non_blank = 0;
      while (line_start < in_len) {
        /* find line end */
        size_t j = line_start;
        while (j < in_len && in_buf[j] != '\n') j++;
        /* compute indent for this line */
        size_t k = line_start;
        while (k < j && (in_buf[k] == ' ' || in_buf[k] == '\t')) k++;
        if (k < j) {
          /* non-blank line */
          any_non_blank = 1;
          size_t indent = k - line_start;
          if (indent < min_indent) min_indent = indent;
        }
        /* advance to next line */
        line_start = (j < in_len) ? (j + 1) : j;
      }
      if (!any_non_blank) min_indent = 0;
      if (min_indent == (size_t)-1) min_indent = 0;

      /* Build output in dynbuf-like temporary allocation: conservative upper bound = in_len */
      char *tmp = (char *)malloc(in_len + 1);
      if (!tmp) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
      size_t out_pos = 0;

      /* Folding state: when style == '>' we replace single newlines with spaces
         except when there is a blank line (preserve as newline). Implement minimal rule. */
  line_start = 0;
      while (line_start < in_len) {
        size_t j = line_start;
        while (j < in_len && in_buf[j] != '\n') j++;
        /* strip min_indent from start of line */
        size_t content_start = line_start + min_indent;
        if (content_start > j) content_start = j; /* line shorter than indent -> becomes blank */
        size_t content_len = j - content_start;
        int is_blank = (content_len == 0);

        /* copy content */
        if (content_len > 0) {
          memcpy(tmp + out_pos, in_buf + content_start, content_len);
          out_pos += content_len;
        }

        /* decide how to handle line break */
        if (j < in_len && in_buf[j] == '\n') {
          /* there is a line break */
          /* lookahead to see if next line is blank */
          size_t next_start = j + 1;
          size_t next_j = next_start;
          while (next_j < in_len && in_buf[next_j] != '\n') next_j++;
          size_t next_k = next_start;
          while (next_k < next_j && (in_buf[next_k] == ' ' || in_buf[next_k] == '\t')) next_k++;
          int next_blank = (next_k >= next_j);

          if (style == '>') {
            if (is_blank) {
              /* blank line -> emit single newline */
              tmp[out_pos++] = '\n';
            } else {
              /* non-blank line: fold to space unless next line is blank */
              if (next_blank) tmp[out_pos++] = '\n'; else tmp[out_pos++] = ' ';
            }
          } else {
            /* literal style '|' keep newline */
            tmp[out_pos++] = '\n';
          }
        }

        line_start = (j < in_len) ? (j + 1) : j;
      }

      /* Chomping: implement according to captured chomping indicator.
         chomping == 1 -> keep all trailing newlines
         chomping == -1 -> strip all trailing newlines
         chomping == 0 -> clip (remove a single trailing newline if present) */
      if (chomping == 1) {
        /* keep: do nothing */
      } else if (chomping == -1) {
        while (out_pos > 0 && tmp[out_pos - 1] == '\n') out_pos--;
      } else {
        if (out_pos > 0 && tmp[out_pos - 1] == '\n') out_pos--;
      }

      /* allocate final output */
      out = (char *)malloc(out_pos);
      if (!out) { free(tmp); gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
      if (out_pos) memcpy(out, tmp, out_pos);
      out_len = out_pos;
      free(tmp);
    }

  gtext_yaml_dynbuf_free(&scalar);

    tok->type = GTEXT_YAML_TOKEN_SCALAR;
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

    /* allocate output buffer */
    size_t slen = scalar.len;
    char *out = (char *)malloc(slen);
    if (!out) { gtext_yaml_dynbuf_free(&scalar); if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; } return GTEXT_YAML_E_OOM; }
    memcpy(out, scalar.data, slen);
    gtext_yaml_dynbuf_free(&scalar);

    tok->type = GTEXT_YAML_TOKEN_SCALAR;
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
        
        /* Always break if followed by structural elements or EOF */
        if (next_c == -1 || next_c == '\r' || next_c == '\n') break;
        if (next_c == ':' || next_c == '-' || next_c == '?') break;
        if (next_c == '#' || next_c == '&' || next_c == '*') break;
        if (next_c == '[' || next_c == ']' || next_c == '{' || next_c == '}' || next_c == ',') break;
        
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
      
      /* Check for list/key indicators */
      if (c == '-' || c == '?') {
        int next_c = -1;
        if (s->cursor + look + 1 < s->input.len) {
          next_c = (unsigned char)s->input.data[s->cursor + look + 1];
        }
        if (next_c == -1 || next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
          break;
        }
      }
      
      /* Other structural indicators */
      if (c == '#' || c == '&' || c == '*' || c == '!') break;
      if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') break;
      if (c == '|' || c == '>' || c == '%') break;
    } else {
      /* In flow context, plain scalars are space-delimited (original behavior)
         Also applies to anchor/alias names which must be space-delimited */
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
      if (is_indicator_char(c)) {
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
