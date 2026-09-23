/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file utf8.c
 * @brief UTF-8 validation and small dynamic buffer used by YAML scanner.
 *
 * Minimal, well-tested utilities: a fast UTF-8 validator and a tiny
 * growable buffer for assembling scalars that cross feed boundaries.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"

/* UTF-8 validation: iterate bytes and ensure valid sequences. Returns 1 on valid, 0 on invalid. */
GTEXT_INTERNAL_API int gtext_utf8_validate(const char *buf, size_t len)
{
  const unsigned char *s = (const unsigned char *)buf;
  size_t i = 0;

  while (i < len) {
    unsigned char c = s[i];
    if (c < 0x80) {
      /* ASCII */
      i++;
      continue;
    }

    if ((c >> 5) == 0x6) {
      /* 2-byte sequence: 110xxxxx 10xxxxxx */
      if (i + 1 >= len) return 0;
      if ((s[i+1] & 0xC0) != 0x80) return 0;
      unsigned int code = ((c & 0x1F) << 6) | (s[i+1] & 0x3F);
      if (code < 0x80) return 0; /* overlong */
      i += 2;
      continue;
    }

    if ((c >> 4) == 0xE) {
      /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
      if (i + 2 >= len) return 0;
      if (((s[i+1] & 0xC0) != 0x80) || ((s[i+2] & 0xC0) != 0x80)) return 0;
      unsigned int code = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F);
      if (code < 0x800) return 0; /* overlong */
      /* Surrogate halves are invalid in UTF-8 */
      if (code >= 0xD800 && code <= 0xDFFF) return 0;
      i += 3;
      continue;
    }

    if ((c >> 3) == 0x1E) {
      /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
      if (i + 3 >= len) return 0;
      if (((s[i+1] & 0xC0) != 0x80) || ((s[i+2] & 0xC0) != 0x80) || ((s[i+3] & 0xC0) != 0x80)) return 0;
      unsigned int code = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F);
      if (code < 0x10000 || code > 0x10FFFF) return 0; /* overlong or out of range */
      i += 4;
      continue;
    }

    return 0; /* invalid leading byte */
  }

  return 1;
}

/* Dynamic buffer implementation */
GTEXT_INTERNAL_API char *gtext_yaml_strdup(const char *s, const GTEXT_Allocator *alloc)
{
  if (!s) return NULL;
  size_t len = strlen(s);
  char *out = (char *)gtext_allocator_malloc(alloc, len + 1);
  if (!out) return NULL;
  memcpy(out, s, len + 1);
  return out;
}

GTEXT_INTERNAL_API int gtext_yaml_dynbuf_init(GTEXT_YAML_DynBuf *b, const GTEXT_Allocator *alloc)
{
  if (!b) return 0;
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
  b->alloc = alloc;
  return 1;
}

GTEXT_INTERNAL_API void gtext_yaml_dynbuf_free(GTEXT_YAML_DynBuf *b)
{
  if (!b) return;
  if (b->data) {
    gtext_allocator_free(b->alloc, b->data);
    b->data = NULL;
  }
  b->len = 0;
  b->cap = 0;
}

static int dyn_grow(GTEXT_YAML_DynBuf *b, size_t min_cap)
{
  size_t new_cap = b->cap ? b->cap * 2 : 64;
  if (new_cap < min_cap) new_cap = min_cap;
  char *p = (char *)gtext_allocator_realloc(b->alloc, b->data, new_cap);
  if (!p) return 0;
  b->data = p;
  b->cap = new_cap;
  return 1;
}

GTEXT_INTERNAL_API int gtext_yaml_dynbuf_append(GTEXT_YAML_DynBuf *b, const char *data, size_t len)
{
  if (!b) return 0;
  if (len == 0) return 1;

  size_t need = b->len + len;
  if (need > b->cap) {
    if (!dyn_grow(b, need)) return 0;
  }

  memcpy(b->data + b->len, data, len);
  b->len += len;
  return 1;
}

