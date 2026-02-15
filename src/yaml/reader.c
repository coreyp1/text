/**
 * @file
 * @brief Minimal reader abstraction for YAML position tracking.
 *
 * This utility provides a streaming reader that tracks byte offset,
 * line, and column. It's intentionally minimal to bootstrap scanner work.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <ghoti.io/text/yaml/yaml_internal.h>

typedef struct GTEXT_YAML_CharReader {
  const char *data;
  size_t len;
  size_t pos; /* byte offset */
  int line;
  int col;
  int suppress_lf; /* 1 if previous char was CR and LF should not advance line */
} GTEXT_YAML_CharReader;

GTEXT_INTERNAL_API GTEXT_YAML_CharReader *gtext_yaml_char_reader_new(
  const char *data,
  size_t len
)
{
  GTEXT_YAML_CharReader *r = (GTEXT_YAML_CharReader *)malloc(sizeof(*r));
  if (!r) {
    return NULL;
  }

  r->data = data;
  r->len = len;
  r->pos = 0;
  r->line = 1;
  r->col = 1;
  r->suppress_lf = 0;
  return r;
}

GTEXT_INTERNAL_API void gtext_yaml_char_reader_free(GTEXT_YAML_CharReader *r)
{
  if (!r) {
    return;
  }

  free(r);
}

GTEXT_INTERNAL_API int gtext_yaml_char_reader_peek(GTEXT_YAML_CharReader *r)
{
  if (!r) {
    return -1;
  }

  if (r->pos >= r->len) {
    return -1;
  }

  return (unsigned char)r->data[r->pos];
}

GTEXT_INTERNAL_API int gtext_yaml_char_reader_consume(GTEXT_YAML_CharReader *r)
{
  if (!r) {
    return -1;
  }

  if (r->pos >= r->len) {
    return -1;
  }

  unsigned char c = (unsigned char)r->data[r->pos++];
  if (c == '\r') {
    r->line++;
    r->col = 1;
    r->suppress_lf = 1;
    return '\n';
  }
  if (c == '\n') {
    if (r->suppress_lf) {
      r->suppress_lf = 0;
      return '\n';
    }
    r->line++;
    r->col = 1;
    return '\n';
  }
  r->suppress_lf = 0;
  r->col++;

  return c;
}

GTEXT_INTERNAL_API size_t gtext_yaml_char_reader_offset(
  const GTEXT_YAML_CharReader *r
)
{
  return r ? r->pos : 0;
}

GTEXT_INTERNAL_API void gtext_yaml_char_reader_position(
  const GTEXT_YAML_CharReader *r,
  int *line,
  int *col
)
{
  if (!r) {
    if (line) {
      *line = 0;
    }

    if (col) {
      *col = 0;
    }

    return;
  }

  if (line) {
    *line = r->line;
  }

  if (col) {
    *col = r->col;
  }
}
