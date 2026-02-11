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

typedef struct GTEXT_YAML_Reader {
  const char *data;
  size_t len;
  size_t pos; /* byte offset */
  int line;
  int col;
} GTEXT_YAML_Reader;

GTEXT_INTERNAL_API GTEXT_YAML_Reader *gtext_yaml_reader_new(const char *data,
                                                            size_t len)
{
  GTEXT_YAML_Reader *r = (GTEXT_YAML_Reader *)malloc(sizeof(*r));
  if (!r) {
    return NULL;
  }

  r->data = data;
  r->len = len;
  r->pos = 0;
  r->line = 1;
  r->col = 1;
  return r;
}

GTEXT_INTERNAL_API void gtext_yaml_reader_free(GTEXT_YAML_Reader *r)
{
  if (!r) {
    return;
  }

  free(r);
}

GTEXT_INTERNAL_API int gtext_yaml_reader_peek(GTEXT_YAML_Reader *r)
{
  if (!r) {
    return -1;
  }

  if (r->pos >= r->len) {
    return -1;
  }

  return (unsigned char)r->data[r->pos];
}

GTEXT_INTERNAL_API int gtext_yaml_reader_consume(GTEXT_YAML_Reader *r)
{
  if (!r) {
    return -1;
  }

  if (r->pos >= r->len) {
    return -1;
  }

  unsigned char c = (unsigned char)r->data[r->pos++];
  if (c == '\n') {
    r->line++;
    r->col = 1;
  } else {
    r->col++;
  }

  return c;
}

GTEXT_INTERNAL_API size_t gtext_yaml_reader_offset(const GTEXT_YAML_Reader *r)
{
  return r ? r->pos : 0;
}

GTEXT_INTERNAL_API void gtext_yaml_reader_position(const GTEXT_YAML_Reader *r,
                                                  int *line, int *col)
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
