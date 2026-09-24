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
 * @file
 *
 * JSONPath (RFC 9535): compiling a query, and evaluating it over a DOM.
 *
 * Two halves that do not know much about each other. The compiler turns the
 * query text into an array of segments, each holding an array of selectors;
 * the evaluator walks the document applying one segment at a time to a node
 * list. Nothing is evaluated during compilation and nothing is parsed during
 * evaluation, which is what makes a compiled query reusable across documents.
 *
 * Three things about this file are decisions rather than mechanics.
 *
 * **The descendant walk is on the heap.** `$..a` visits a node and every node
 * under it, and the obvious way to write that is recursion - which makes the
 * depth of the caller's document the depth of this library's C stack. Three
 * DOM walks in the YAML module were moved off the stack for exactly that
 * reason; this one starts there. The stack is explicit, children are pushed in
 * reverse so they come off in document order, and running out of memory is a
 * status rather than a crash.
 *
 * **A node list may hold the same node twice.** `$[0,0]` selects the first
 * element twice and RFC 9535 says it does (2.3.1.2). So the evaluator appends
 * rather than unions, and a caller counting results gets the number the
 * specification gives.
 *
 * **A filter is refused, not ignored.** The filter selector needs an
 * expression evaluator and, for match() and search(), a regular expression
 * engine; neither is here yet. A query containing one is refused at compile
 * time with GTEXT_JSON_E_PATH_UNSUPPORTED rather than evaluated as though the
 * filter were absent, which would select every element of the array instead of
 * the ones asked for - a wrong answer where the refusal is merely an absent
 * feature.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/macros.h>
#include "json_internal.h"

#include <ghoti.io/text/json/json_path.h>

/* RFC 9535 2.3.3.1: an index or a slice bound is an integer in the
 * interchangeable range, so that every implementation agrees about it. */
#define JSON_PATH_MAX_INT 9007199254740991LL  /* 2^53 - 1 */
#define JSON_PATH_MIN_INT (-9007199254740991LL)

typedef enum {
  JSON_PATH_SEL_NAME,     /* ["a"] and .a */
  JSON_PATH_SEL_INDEX,    /* [3], [-1] */
  JSON_PATH_SEL_WILDCARD, /* [*] and .* */
  JSON_PATH_SEL_SLICE     /* [1:5:2] */
} json_path_selector_kind;

typedef struct {
  json_path_selector_kind kind;
  /* NAME: the decoded name, owned by the query. */
  char * name;
  size_t name_len;
  /* INDEX: the index, which may be negative. */
  int64_t index;
  /* SLICE: the three bounds, each present or not. */
  int64_t start;
  int64_t end;
  int64_t step;
  bool has_start;
  bool has_end;
} json_path_selector;

typedef struct {
  bool descendant; /* ".." rather than "." or "[" */
  json_path_selector * selectors;
  size_t selector_count;
} json_path_segment;

struct GTEXT_JSON_Path {
  const GTEXT_Allocator * alloc;
  json_path_segment * segments;
  size_t segment_count;
};

/* ======================================================================
 * Compilation
 * ====================================================================== */

typedef struct {
  const char * text;
  size_t len;
  size_t at;
  const GTEXT_Allocator * alloc;
  GTEXT_JSON_Status status;
  const char * message;
} json_path_parser;

static void json_path_fail(json_path_parser * p, GTEXT_JSON_Status status,
    const char * message) {
  if (p->status == GTEXT_JSON_OK) { /* keep the first complaint */
    p->status = status;
    p->message = message;
  }
}

static bool json_path_at_end(const json_path_parser * p) {
  return p->at >= p->len;
}

static char json_path_peek(const json_path_parser * p) {
  return json_path_at_end(p) ? '\0' : p->text[p->at];
}

static char json_path_peek_at(const json_path_parser * p, size_t ahead) {
  return (p->at + ahead >= p->len) ? '\0' : p->text[p->at + ahead];
}

/* RFC 9535 2.1: S is blank space, and only these four characters. */
static void json_path_skip_space(json_path_parser * p) {
  while (!json_path_at_end(p)) {
    const char c = p->text[p->at];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      p->at++;
    }
    else {
      break;
    }
  }
}

static bool json_path_is_digit(char c) {
  return c >= '0' && c <= '9';
}

/* name-first / name-char (2.5.1.1): ASCII letters, "_", and every
 * non-ASCII character. A shorthand name is *not* a JSON string: it has no
 * escapes, and the bytes go through as they are. Their well-formedness as
 * UTF-8 is the document's business, not this grammar's. */
static bool json_path_name_first(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'
      || c >= 0x80;
}

static bool json_path_name_char(unsigned char c) {
  return json_path_name_first(c) || (c >= '0' && c <= '9');
}

/* One hex digit's value, or -1. */
static int json_path_hex(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/* Write a codepoint as UTF-8; returns the byte count. */
static size_t json_path_utf8(uint32_t cp, char * out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

/*
 * A string literal (2.3.1.1), single- or double-quoted.
 *
 * The escapes are JSON's, with one difference in each direction: a
 * double-quoted string may not hold an unescaped `"` and may hold `'`, and a
 * single-quoted string is the other way round. Everything else - \b \f \n \r
 * \t \/ \\ \uXXXX and surrogate pairs - is the same, and anything else after a
 * backslash is an error rather than the character itself.
 *
 * The decoded bytes are written into a buffer the query owns. Decoding can
 * only shrink, so the source length is a safe capacity.
 */
static char * json_path_string(json_path_parser * p, size_t * out_len) {
  const char quote = json_path_peek(p);
  if (quote != '"' && quote != '\'') {
    json_path_fail(p, GTEXT_JSON_E_PATH, "expected a quoted name");
    return NULL;
  }
  p->at++;

  const size_t capacity = p->len - p->at + 1;
  char * out = (char *)gtext_allocator_malloc(p->alloc, capacity);
  if (!out) {
    json_path_fail(p, GTEXT_JSON_E_OOM, "out of memory");
    return NULL;
  }
  size_t used = 0;

  while (!json_path_at_end(p)) {
    const unsigned char c = (unsigned char)p->text[p->at];
    if (c == (unsigned char)quote) {
      p->at++;
      out[used] = '\0';
      *out_len = used;
      return out;
    }
    if (c < 0x20) {
      json_path_fail(
          p, GTEXT_JSON_E_PATH, "unescaped control character in a name");
      break;
    }
    if (c != '\\') {
      out[used++] = (char)c;
      p->at++;
      continue;
    }

    /* An escape. */
    p->at++;
    const char esc = json_path_peek(p);
    switch (esc) {
    case 'b': out[used++] = '\b'; p->at++; continue;
    case 'f': out[used++] = '\f'; p->at++; continue;
    case 'n': out[used++] = '\n'; p->at++; continue;
    case 'r': out[used++] = '\r'; p->at++; continue;
    case 't': out[used++] = '\t'; p->at++; continue;
    case '/': out[used++] = '/'; p->at++; continue;
    case '\\': out[used++] = '\\'; p->at++; continue;
    case 'u': break; /* below */
    default:
      /* The quote that delimits *this* string may be escaped; the other one
       * may not be, because it needs no escaping and 2.3.1.1 lists only one. */
      if (esc == quote) {
        out[used++] = esc;
        p->at++;
        continue;
      }
      json_path_fail(p, GTEXT_JSON_E_PATH, "unknown escape in a name");
      gtext_allocator_free(p->alloc, out);
      return NULL;
    }

    /* \uXXXX, and a surrogate pair written as two of them. */
    p->at++; /* past the 'u' */
    uint32_t cp = 0;
    for (int i = 0; i < 4; i++) {
      const int digit = json_path_hex(json_path_peek(p));
      if (digit < 0) {
        json_path_fail(p, GTEXT_JSON_E_PATH, "a \\u escape wants four hex digits");
        gtext_allocator_free(p->alloc, out);
        return NULL;
      }
      cp = (cp << 4) | (uint32_t)digit;
      p->at++;
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (json_path_peek(p) != '\\' || json_path_peek_at(p, 1) != 'u') {
        json_path_fail(p, GTEXT_JSON_E_PATH,
            "a high surrogate wants a low surrogate after it");
        gtext_allocator_free(p->alloc, out);
        return NULL;
      }
      p->at += 2;
      uint32_t low = 0;
      for (int i = 0; i < 4; i++) {
        const int digit = json_path_hex(json_path_peek(p));
        if (digit < 0) {
          json_path_fail(
              p, GTEXT_JSON_E_PATH, "a \\u escape wants four hex digits");
          gtext_allocator_free(p->alloc, out);
          return NULL;
        }
        low = (low << 4) | (uint32_t)digit;
        p->at++;
      }
      if (low < 0xDC00 || low > 0xDFFF) {
        json_path_fail(
            p, GTEXT_JSON_E_PATH, "a high surrogate wants a low surrogate");
        gtext_allocator_free(p->alloc, out);
        return NULL;
      }
      cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
    }
    else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      json_path_fail(p, GTEXT_JSON_E_PATH, "a low surrogate on its own");
      gtext_allocator_free(p->alloc, out);
      return NULL;
    }
    used += json_path_utf8(cp, out + used);
  }

  json_path_fail(p, GTEXT_JSON_E_PATH, "a name was never closed");
  gtext_allocator_free(p->alloc, out);
  return NULL;
}

/*
 * An integer (2.3.3.1 int): an optional "-", then "0" alone or a digit 1-9
 * followed by digits. So "01" and "-0" are not integers, which is a rule about
 * the *query* and has nothing to do with what JSON numbers allow.
 */
static bool json_path_int(json_path_parser * p, int64_t * out) {
  const size_t start = p->at;
  bool negative = false;
  if (json_path_peek(p) == '-') {
    negative = true;
    p->at++;
  }
  if (!json_path_is_digit(json_path_peek(p))) {
    p->at = start;
    return false;
  }
  if (json_path_peek(p) == '0') {
    p->at++;
    if (json_path_is_digit(json_path_peek(p))) {
      json_path_fail(p, GTEXT_JSON_E_PATH, "a leading zero is not an integer");
      return false;
    }
    if (negative) {
      json_path_fail(p, GTEXT_JSON_E_PATH, "-0 is not an integer");
      return false;
    }
    *out = 0;
    return true;
  }
  int64_t value = 0;
  while (json_path_is_digit(json_path_peek(p))) {
    const int digit = json_path_peek(p) - '0';
    if (value > (JSON_PATH_MAX_INT - digit) / 10) {
      json_path_fail(p, GTEXT_JSON_E_PATH,
          "an index outside the interchangeable range");
      return false;
    }
    value = value * 10 + digit;
    p->at++;
  }
  *out = negative ? -value : value;
  return true;
}

/* Room for one more selector in a segment. */
static bool json_path_segment_grow(
    json_path_parser * p, json_path_segment * seg, size_t capacity) {
  json_path_selector * grown = (json_path_selector *)gtext_allocator_realloc(
      p->alloc, seg->selectors, capacity * sizeof(json_path_selector));
  if (!grown) {
    json_path_fail(p, GTEXT_JSON_E_OOM, "out of memory");
    return false;
  }
  seg->selectors = grown;
  return true;
}

/* One selector inside brackets. */
static bool json_path_selector_parse(
    json_path_parser * p, json_path_selector * out) {
  memset(out, 0, sizeof(*out));
  out->step = 1;

  const char c = json_path_peek(p);
  if (c == '*') {
    p->at++;
    out->kind = JSON_PATH_SEL_WILDCARD;
    return true;
  }
  if (c == '"' || c == '\'') {
    out->kind = JSON_PATH_SEL_NAME;
    out->name = json_path_string(p, &out->name_len);
    return out->name != NULL;
  }
  if (c == '?') {
    json_path_fail(p, GTEXT_JSON_E_PATH_UNSUPPORTED,
        "the filter selector is not implemented");
    return false;
  }

  /* An index or a slice. The two are told apart by the colon, which may come
   * before the first number: "[:2]" is a slice with no start. */
  bool has_first = false;
  int64_t first = 0;
  if (c == '-' || json_path_is_digit(c)) {
    if (!json_path_int(p, &first)) {
      return false;
    }
    has_first = true;
    json_path_skip_space(p);
  }
  if (json_path_peek(p) != ':') {
    if (!has_first) {
      json_path_fail(p, GTEXT_JSON_E_PATH, "expected a selector");
      return false;
    }
    out->kind = JSON_PATH_SEL_INDEX;
    out->index = first;
    return true;
  }

  out->kind = JSON_PATH_SEL_SLICE;
  out->has_start = has_first;
  out->start = first;
  p->at++; /* the first colon */
  json_path_skip_space(p);
  if (json_path_peek(p) == '-' || json_path_is_digit(json_path_peek(p))) {
    if (!json_path_int(p, &out->end)) {
      return false;
    }
    out->has_end = true;
    json_path_skip_space(p);
  }
  if (json_path_peek(p) == ':') {
    p->at++;
    json_path_skip_space(p);
    if (json_path_peek(p) == '-' || json_path_is_digit(json_path_peek(p))) {
      if (!json_path_int(p, &out->step)) {
        return false;
      }
    }
  }
  return true;
}

/* A bracketed selection: at least one selector, comma-separated. */
static bool json_path_bracket(json_path_parser * p, json_path_segment * seg) {
  p->at++; /* '[' */
  size_t capacity = 0;
  for (;;) {
    json_path_skip_space(p);
    if (seg->selector_count == capacity) {
      capacity = capacity ? capacity * 2 : 4;
      if (!json_path_segment_grow(p, seg, capacity)) {
        return false;
      }
    }
    if (!json_path_selector_parse(p, &seg->selectors[seg->selector_count])) {
      return false;
    }
    seg->selector_count++;
    json_path_skip_space(p);
    if (json_path_peek(p) == ',') {
      p->at++;
      continue;
    }
    if (json_path_peek(p) == ']') {
      p->at++;
      return true;
    }
    json_path_fail(p, GTEXT_JSON_E_PATH, "expected a comma or a closing bracket");
    return false;
  }
}

/* A shorthand name or a wildcard after a dot. */
static bool json_path_shorthand(json_path_parser * p, json_path_segment * seg) {
  if (!json_path_segment_grow(p, seg, 1)) {
    return false;
  }
  json_path_selector * sel = &seg->selectors[0];
  memset(sel, 0, sizeof(*sel));
  sel->step = 1;

  if (json_path_peek(p) == '*') {
    p->at++;
    sel->kind = JSON_PATH_SEL_WILDCARD;
    seg->selector_count = 1;
    return true;
  }
  if (!json_path_name_first((unsigned char)json_path_peek(p))) {
    json_path_fail(p, GTEXT_JSON_E_PATH, "expected a name after '.'");
    return false;
  }
  const size_t start = p->at;
  while (!json_path_at_end(p)
      && json_path_name_char((unsigned char)p->text[p->at])) {
    p->at++;
  }
  const size_t name_len = p->at - start;
  char * name = (char *)gtext_allocator_malloc(p->alloc, name_len + 1);
  if (!name) {
    json_path_fail(p, GTEXT_JSON_E_OOM, "out of memory");
    return false;
  }
  memcpy(name, p->text + start, name_len);
  name[name_len] = '\0';
  sel->kind = JSON_PATH_SEL_NAME;
  sel->name = name;
  sel->name_len = name_len;
  seg->selector_count = 1;
  return true;
}

static void json_path_free_segments(const GTEXT_Allocator * alloc,
    json_path_segment * segments, size_t count) {
  if (!segments) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < segments[i].selector_count; j++) {
      gtext_allocator_free(alloc, segments[i].selectors[j].name);
    }
    gtext_allocator_free(alloc, segments[i].selectors);
  }
  gtext_allocator_free(alloc, segments);
}

GTEXT_API GTEXT_JSON_Path * gtext_json_path_compile(const char * query,
    size_t len, const GTEXT_Allocator * alloc, GTEXT_JSON_Error * err) {
  if (err) {
    memset(err, 0, sizeof(*err));
    err->line = 1;
    err->col = 1;
  }
  if (!query) {
    if (err) {
      err->code = GTEXT_JSON_E_INVALID;
      err->message = "no query";
    }
    return NULL;
  }

  json_path_parser p;
  memset(&p, 0, sizeof(p));
  p.text = query;
  p.len = (len == SIZE_MAX) ? strlen(query) : len;
  p.alloc = alloc;
  p.status = GTEXT_JSON_OK;

  json_path_segment * segments = NULL;
  size_t count = 0;
  size_t capacity = 0;

  if (json_path_peek(&p) != '$') {
    json_path_fail(&p, GTEXT_JSON_E_PATH, "a query begins with '$'");
    goto failed;
  }
  p.at++;

  for (;;) {
    json_path_skip_space(&p);
    if (json_path_at_end(&p)) {
      break;
    }
    const char c = json_path_peek(&p);
    if (c != '.' && c != '[') {
      json_path_fail(&p, GTEXT_JSON_E_PATH, "expected '.', '..' or '['");
      goto failed;
    }

    if (count == capacity) {
      const size_t grown_capacity = capacity ? capacity * 2 : 4;
      json_path_segment * grown = (json_path_segment *)gtext_allocator_realloc(
          alloc, segments, grown_capacity * sizeof(json_path_segment));
      if (!grown) {
        json_path_fail(&p, GTEXT_JSON_E_OOM, "out of memory");
        goto failed;
      }
      segments = grown;
      capacity = grown_capacity;
    }
    json_path_segment * seg = &segments[count];
    memset(seg, 0, sizeof(*seg));

    if (c == '[') {
      if (!json_path_bracket(&p, seg)) {
        goto failed_with_segment;
      }
    }
    else {
      p.at++; /* the first '.' */
      if (json_path_peek(&p) == '.') {
        p.at++;
        seg->descendant = true;
        if (json_path_peek(&p) == '[') {
          if (!json_path_bracket(&p, seg)) {
            goto failed_with_segment;
          }
        }
        else if (!json_path_shorthand(&p, seg)) {
          goto failed_with_segment;
        }
      }
      else if (!json_path_shorthand(&p, seg)) {
        goto failed_with_segment;
      }
    }
    count++;
    continue;

  failed_with_segment:
    /* The half-built segment has to be released before the array is, or its
     * selector array and any name in it leak. */
    for (size_t j = 0; j < seg->selector_count; j++) {
      gtext_allocator_free(alloc, seg->selectors[j].name);
    }
    gtext_allocator_free(alloc, seg->selectors);
    goto failed;
  }

  {
    GTEXT_JSON_Path * path = (GTEXT_JSON_Path *)gtext_allocator_calloc(
        alloc, 1, sizeof(GTEXT_JSON_Path));
    if (!path) {
      json_path_fail(&p, GTEXT_JSON_E_OOM, "out of memory");
      goto failed;
    }
    path->alloc = alloc;
    path->segments = segments;
    path->segment_count = count;
    return path;
  }

failed:
  json_path_free_segments(alloc, segments, count);
  if (err) {
    err->code = p.status == GTEXT_JSON_OK ? GTEXT_JSON_E_PATH : p.status;
    err->message = p.message ? p.message : "invalid query";
    err->offset = p.at;
  }
  return NULL;
}

GTEXT_API void gtext_json_path_free(GTEXT_JSON_Path * path) {
  if (!path) {
    return;
  }
  const GTEXT_Allocator * alloc = path->alloc;
  json_path_free_segments(alloc, path->segments, path->segment_count);
  gtext_allocator_free(alloc, path);
}

/* ======================================================================
 * Evaluation
 * ====================================================================== */

typedef struct {
  const GTEXT_JSON_Value ** items;
  size_t count;
  size_t capacity;
  const GTEXT_Allocator * alloc;
} json_path_list;

static bool json_path_list_push(
    json_path_list * list, const GTEXT_JSON_Value * node) {
  if (list->count == list->capacity) {
    const size_t capacity = list->capacity ? list->capacity * 2 : 8;
    const GTEXT_JSON_Value ** grown =
        (const GTEXT_JSON_Value **)gtext_allocator_realloc(list->alloc,
            (void *)list->items, capacity * sizeof(const GTEXT_JSON_Value *));
    if (!grown) {
      return false;
    }
    list->items = grown;
    list->capacity = capacity;
  }
  list->items[list->count++] = node;
  return true;
}

static void json_path_list_free(json_path_list * list) {
  gtext_allocator_free(list->alloc, (void *)list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

/*
 * The bounds of a slice (2.3.4.2.2), which is Python's rule written out.
 *
 * A negative bound counts from the end; a negative step runs backwards and
 * swaps which end is inclusive. A step of zero selects nothing, which the
 * specification says explicitly rather than leaving to a division.
 */
static void json_path_slice_bounds(const json_path_selector * sel, size_t len,
    int64_t * out_lower, int64_t * out_upper) {
  const int64_t length = (int64_t)len;
  const int64_t step = sel->step;

  int64_t start = sel->has_start ? sel->start : (step >= 0 ? 0 : length - 1);
  int64_t end = sel->has_end ? sel->end : (step >= 0 ? length : -length - 1);

  if (start < 0) {
    start += length;
  }
  if (end < 0) {
    end += length;
  }

  if (step >= 0) {
    if (start < 0) {
      start = 0;
    }
    if (start > length) {
      start = length;
    }
    if (end < 0) {
      end = 0;
    }
    if (end > length) {
      end = length;
    }
  }
  else {
    if (start < -1) {
      start = -1;
    }
    if (start > length - 1) {
      start = length - 1;
    }
    if (end < -1) {
      end = -1;
    }
    if (end > length - 1) {
      end = length - 1;
    }
  }
  *out_lower = start;
  *out_upper = end;
}

/* Apply one selector to one node, appending what it selects. */
static GTEXT_JSON_Status json_path_apply_selector(
    const json_path_selector * sel, const GTEXT_JSON_Value * node,
    json_path_list * out) {
  const GTEXT_JSON_Type type = gtext_json_typeof(node);

  switch (sel->kind) {
  case JSON_PATH_SEL_NAME: {
    if (type != GTEXT_JSON_OBJECT) {
      return GTEXT_JSON_OK;
    }
    const GTEXT_JSON_Value * found =
        gtext_json_object_get(node, sel->name, sel->name_len);
    if (found && !json_path_list_push(out, found)) {
      return GTEXT_JSON_E_OOM;
    }
    return GTEXT_JSON_OK;
  }

  case JSON_PATH_SEL_INDEX: {
    if (type != GTEXT_JSON_ARRAY) {
      return GTEXT_JSON_OK;
    }
    const size_t len = gtext_json_array_size(node);
    int64_t index = sel->index;
    if (index < 0) {
      index += (int64_t)len;
    }
    if (index < 0 || index >= (int64_t)len) {
      return GTEXT_JSON_OK;
    }
    const GTEXT_JSON_Value * found = gtext_json_array_get(node, (size_t)index);
    if (found && !json_path_list_push(out, found)) {
      return GTEXT_JSON_E_OOM;
    }
    return GTEXT_JSON_OK;
  }

  case JSON_PATH_SEL_WILDCARD: {
    if (type == GTEXT_JSON_ARRAY) {
      const size_t len = gtext_json_array_size(node);
      for (size_t i = 0; i < len; i++) {
        if (!json_path_list_push(out, gtext_json_array_get(node, i))) {
          return GTEXT_JSON_E_OOM;
        }
      }
    }
    else if (type == GTEXT_JSON_OBJECT) {
      /* 2.3.2.2: the order of an object's members is implementation-defined,
       * and this DOM keeps them in document order, so that is what comes out. */
      const size_t len = gtext_json_object_size(node);
      for (size_t i = 0; i < len; i++) {
        if (!json_path_list_push(out, gtext_json_object_value(node, i))) {
          return GTEXT_JSON_E_OOM;
        }
      }
    }
    return GTEXT_JSON_OK;
  }

  case JSON_PATH_SEL_SLICE: {
    if (type != GTEXT_JSON_ARRAY) {
      return GTEXT_JSON_OK;
    }
    if (sel->step == 0) {
      return GTEXT_JSON_OK; /* 2.3.4.2.2: a zero step selects nothing. */
    }
    const size_t len = gtext_json_array_size(node);
    int64_t lower = 0;
    int64_t upper = 0;
    json_path_slice_bounds(sel, len, &lower, &upper);
    if (sel->step > 0) {
      for (int64_t i = lower; i < upper; i += sel->step) {
        if (!json_path_list_push(out, gtext_json_array_get(node, (size_t)i))) {
          return GTEXT_JSON_E_OOM;
        }
      }
    }
    else {
      for (int64_t i = lower; i > upper; i += sel->step) {
        if (!json_path_list_push(out, gtext_json_array_get(node, (size_t)i))) {
          return GTEXT_JSON_E_OOM;
        }
      }
    }
    return GTEXT_JSON_OK;
  }
  }
  return GTEXT_JSON_OK;
}

/*
 * Every node at or under `root`, in document order, appended to `out`.
 *
 * An explicit stack rather than recursion: the depth here is the caller's
 * document's depth, and a query is not a reason to put that on the C stack.
 * Children are pushed in reverse so they are visited front to back.
 */
static GTEXT_JSON_Status json_path_collect_descendants(
    const GTEXT_JSON_Value * root, json_path_list * out) {
  json_path_list stack;
  memset(&stack, 0, sizeof(stack));
  stack.alloc = out->alloc;

  if (!json_path_list_push(&stack, root)) {
    return GTEXT_JSON_E_OOM;
  }

  GTEXT_JSON_Status status = GTEXT_JSON_OK;
  while (stack.count > 0) {
    const GTEXT_JSON_Value * node = stack.items[--stack.count];
    if (!json_path_list_push(out, node)) {
      status = GTEXT_JSON_E_OOM;
      break;
    }
    const GTEXT_JSON_Type type = gtext_json_typeof(node);
    if (type == GTEXT_JSON_ARRAY) {
      const size_t len = gtext_json_array_size(node);
      for (size_t i = len; i > 0; i--) {
        if (!json_path_list_push(&stack, gtext_json_array_get(node, i - 1))) {
          status = GTEXT_JSON_E_OOM;
          break;
        }
      }
    }
    else if (type == GTEXT_JSON_OBJECT) {
      const size_t len = gtext_json_object_size(node);
      for (size_t i = len; i > 0; i--) {
        if (!json_path_list_push(&stack, gtext_json_object_value(node, i - 1))) {
          status = GTEXT_JSON_E_OOM;
          break;
        }
      }
    }
    if (status != GTEXT_JSON_OK) {
      break;
    }
  }

  json_path_list_free(&stack);
  return status;
}

GTEXT_API GTEXT_JSON_Status gtext_json_path_select(const GTEXT_JSON_Path * path,
    const GTEXT_JSON_Value * root, GTEXT_JSON_Path_Result * out) {
  if (!path || !root || !out) {
    return GTEXT_JSON_E_INVALID;
  }
  memset(out, 0, sizeof(*out));
  out->alloc = path->alloc;

  json_path_list current;
  memset(&current, 0, sizeof(current));
  current.alloc = path->alloc;
  if (!json_path_list_push(&current, root)) {
    return GTEXT_JSON_E_OOM;
  }

  GTEXT_JSON_Status status = GTEXT_JSON_OK;
  for (size_t s = 0; s < path->segment_count && status == GTEXT_JSON_OK; s++) {
    const json_path_segment * seg = &path->segments[s];
    json_path_list next;
    memset(&next, 0, sizeof(next));
    next.alloc = path->alloc;

    for (size_t i = 0; i < current.count && status == GTEXT_JSON_OK; i++) {
      if (!seg->descendant) {
        for (size_t k = 0; k < seg->selector_count; k++) {
          status = json_path_apply_selector(
              &seg->selectors[k], current.items[i], &next);
          if (status != GTEXT_JSON_OK) {
            break;
          }
        }
        continue;
      }

      /* A descendant segment applies its selectors to the node and to every
       * node under it (2.5.2.2), in document order. */
      json_path_list visited;
      memset(&visited, 0, sizeof(visited));
      visited.alloc = path->alloc;
      status = json_path_collect_descendants(current.items[i], &visited);
      for (size_t v = 0; v < visited.count && status == GTEXT_JSON_OK; v++) {
        for (size_t k = 0; k < seg->selector_count; k++) {
          status = json_path_apply_selector(
              &seg->selectors[k], visited.items[v], &next);
          if (status != GTEXT_JSON_OK) {
            break;
          }
        }
      }
      json_path_list_free(&visited);
    }

    json_path_list_free(&current);
    current = next;
  }

  if (status != GTEXT_JSON_OK) {
    json_path_list_free(&current);
    return status;
  }

  out->nodes = current.items;
  out->count = current.count;
  return GTEXT_JSON_OK;
}

GTEXT_API GTEXT_JSON_Status gtext_json_path_query(const GTEXT_JSON_Value * root,
    const char * query, size_t len, const GTEXT_Allocator * alloc,
    GTEXT_JSON_Path_Result * out, GTEXT_JSON_Error * err) {
  if (!root || !query || !out) {
    return GTEXT_JSON_E_INVALID;
  }
  GTEXT_JSON_Path * path = gtext_json_path_compile(query, len, alloc, err);
  if (!path) {
    memset(out, 0, sizeof(*out));
    return err ? err->code : GTEXT_JSON_E_PATH;
  }
  const GTEXT_JSON_Status status = gtext_json_path_select(path, root, out);
  gtext_json_path_free(path);
  return status;
}

GTEXT_API void gtext_json_path_result_free(GTEXT_JSON_Path_Result * result) {
  if (!result) {
    return;
  }
  gtext_allocator_free(result->alloc, (void *)result->nodes);
  result->nodes = NULL;
  result->count = 0;
}
