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
  int node_indent;        /* indentation of the block node being built, -1 at the root */
  int last_scalar_col;    /* 0-based column the last scalar token started at */
  /* Whether the last token was a JSON-like node - a quoted scalar or a
     closing "]" or "}".  Inside a flow collection a ":" straight after one of
     those is a mapping indicator even with nothing between them
     (c-ns-flow-map-adjacent-value), which is what lets '{"a":1}' parse.

     Only quoted scalars and indicators write this.  A plain scalar does not
     need to clear it: two nodes cannot sit next to each other without an
     indicator between them, and every indicator clears it.  A comment
     deliberately leaves it alone, so the adjacent value still works when a
     comment separates it from its key. */
  bool last_json_like;
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

/**
 * @brief Decide whether a plain scalar continues past the break at @p look.
 *
 * 7.3.3's ns-plain-multi-line: the scalar goes on while the lines below it
 * are indented past the node it belongs to.  A run of empty lines counts, so
 * @p out_breaks is the number of breaks crossed - one folds to a space, and
 * more than one gives a line break each.
 *
 * @p flow tells it that a line beginning with a flow indicator ends the
 * scalar rather than continuing it, which is what keeps "[a - b" over " , c"
 * two entries rather than one.
 */
static bool plain_scalar_continues(
    const GTEXT_YAML_Scanner *s,
    size_t look,
    bool flow,
    size_t *out_breaks,
    size_t *out_continue_at,
    bool *out_need_more)
{
  size_t probe = look;
  size_t breaks = 0;
  *out_need_more = false;

  for (;;) {
    if (s->cursor + probe >= s->input.len) { *out_need_more = true; return false; }
    char bc = s->input.data[s->cursor + probe];
    if (bc == '\r') {
      probe++;
      if (s->cursor + probe < s->input.len
          && s->input.data[s->cursor + probe] == '\n') {
        probe++;
      } else if (s->cursor + probe >= s->input.len) {
        *out_need_more = true;
        return false;
      }
    } else if (bc == '\n') {
      probe++;
    } else {
      return false;
    }
    breaks++;

    /* Indentation is counted in spaces (6.1), and the separation that may
       follow it can hold tabs as well. Only spaces were being stepped over,
       so a line of " \t" was not recognised as empty and its tab was taken
       as the scalar's next character - "foo: 1" over " \t" over "bar: 2"
       gave foo the string "1 " rather than the number 1. */
    size_t sp = 0;
    while (s->cursor + probe + sp < s->input.len
           && s->input.data[s->cursor + probe + sp] == ' ') {
      sp++;
    }
    size_t ws = sp;
    while (s->cursor + probe + ws < s->input.len
           && (s->input.data[s->cursor + probe + ws] == ' '
            || s->input.data[s->cursor + probe + ws] == '\t')) {
      ws++;
    }
    if (s->cursor + probe + ws >= s->input.len) { *out_need_more = true; return false; }
    char nc = s->input.data[s->cursor + probe + ws];
    if (nc == '\n' || nc == '\r') { probe += ws; continue; } /* empty line */
    if ((int)sp <= s->node_indent) return false;  /* dedent ends the scalar */
    if (nc == '#') return false;                  /* a comment, not content */
    /* A "%" is not stopped here. ns-plain-first excludes it, so a scalar
       cannot begin with one, but ns-plain-char does not - a continuation
       line may hold it, and "scalar" over "%YAML 1.2" is the one scalar
       "scalar %YAML 1.2" (suite case XLQ9). A directive line after real
       content is caught by the dedent rule above instead: the content is
       indented past the node it belongs to and the "%" is at column 0. */
    if (flow && (nc == ',' || nc == '[' || nc == ']' || nc == '{' || nc == '}')) {
      return false; /* the collection's own punctuation, not more scalar */
    }
    /* "---" and "..." open and close documents wherever they stand, so a
       scalar never folds across one. */
    if ((nc == '-' || nc == '.') && s->cursor + probe + ws + 2 < s->input.len
        && s->input.data[s->cursor + probe + ws + 1] == nc
        && s->input.data[s->cursor + probe + ws + 2] == nc) {
      const size_t after = s->cursor + probe + ws + 3;
      if (after >= s->input.len || s->input.data[after] == ' '
          || s->input.data[after] == '\t' || s->input.data[after] == '\n'
          || s->input.data[after] == '\r') {
        return false;
      }
    }
    *out_breaks = breaks;
    *out_continue_at = probe + ws;
    return true;
  }
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

/* Count the run of line breaks that a quoted scalar folds away, starting at
 * s->input.data[s->cursor + *p], and step *p past the white space opening the
 * line that follows the run.  A quoted scalar's continuation lines are
 * indented for readability and that indentation is not content (7.3.1,
 * 7.3.2), so it is skipped here rather than at the call sites.
 *
 * Returns false if the input ran out inside the run, with *p left at the end
 * so the caller can take the usual incomplete-or-unterminated path: the fold
 * cannot be decided until the line after the breaks has arrived. */
/**
 * @brief Does a document marker begin at @p p, at the start of a line?
 *
 * c-forbidden is "---" or "..." at the start of a line, followed by white
 * space, a break, or end of input (9.1.2).  A multi-line scalar may not
 * contain one: the marker ends the document wherever it appears, so a quoted
 * scalar spanning it has no closing quote.
 *
 * The three characters have to be followed by white space to count, which is
 * what separates the two halves of suite case 9MQT: "...x" on its own line is
 * ordinary content, "... x" is forbidden.
 */
static bool line_starts_forbidden_marker(
    const GTEXT_YAML_Scanner *s,
    size_t p)
{
  const char *d = NULL;
  char c = 0;
  if (s->cursor + p + 2 >= s->input.len) return false;
  d = s->input.data + s->cursor + p;
  c = d[0];
  if ((c != '-' && c != '.') || d[1] != c || d[2] != c) return false;
  /* c-forbidden allows end of input after the marker, and this is also the
     bounds check for reading the byte below.  No test separates the two
     answers: the only callers are inside a quoted scalar, which is
     unterminated when the input stops here, so the document is refused
     either way. */
  if (s->cursor + p + 3 >= s->input.len) return true;
  return d[3] == ' ' || d[3] == '\t' || d[3] == '\n' || d[3] == '\r';
}

/**
 * @brief Is the whole of a block scalar in the buffer?
 *
 * A block scalar is taken whole or not at all.  Its header is consumed
 * before its body is read, and this scanner cannot rewind, so a body that
 * turns out to be incomplete has nothing to go back to: the loop settled for
 * the lines already in hand and ended the scalar wherever the caller's chunk
 * boundary fell.  "literal: |" over "  some" over "  text" fed a byte at a
 * time came back as "some text" - one line, folded - instead of
 * "some\ntext\n".
 *
 * What ends a block scalar is a non-empty line indented no further than the
 * node that owns it, or a document marker at column 1 (8.1.1, 9.1.2).  Both
 * are conservative here: finding either means the block certainly ended at
 * or before that line, so all of it is in hand.  Finding neither before the
 * buffer runs out means it may still go on.
 *
 * This walks the block on each feed, so a large block scalar delivered in
 * many small pieces is rescanned each time.  Correctness first; it is skipped
 * entirely once the input is finished, which is the case gtext_yaml_parse()
 * takes.
 */
static bool block_scalar_complete(const GTEXT_YAML_Scanner *s)
{
  const int parent = s->node_indent;
  size_t p = s->cursor;

  /* The header runs to the end of its line. */
  while (p < s->input.len
      && s->input.data[p] != '\n' && s->input.data[p] != '\r') {
    p++;
  }
  if (p >= s->input.len) return false;
  if (s->input.data[p] == '\r') {
    p++;
    if (p >= s->input.len) return false;
    if (s->input.data[p] == '\n') p++;
  } else {
    p++;
  }

  for (;;) {
    if (p >= s->input.len) return false;
    size_t sp = p;
    size_t spaces = 0;
    while (sp < s->input.len && s->input.data[sp] == ' ') { sp++; spaces++; }
    if (sp >= s->input.len) return false;
    const char lc = s->input.data[sp];
    if (lc != '\n' && lc != '\r') {
      if ((int)spaces <= parent) return true;
      if (spaces == 0 && s->input.len - sp >= 4) {
        const char m = s->input.data[sp];
        if ((m == '-' || m == '.')
            && s->input.data[sp + 1] == m && s->input.data[sp + 2] == m) {
          const char after = s->input.data[sp + 3];
          if (after == ' ' || after == '\t'
              || after == '\n' || after == '\r') {
            return true;
          }
        }
      }
    }
    while (sp < s->input.len
        && s->input.data[sp] != '\n' && s->input.data[sp] != '\r') {
      sp++;
    }
    if (sp >= s->input.len) return false;
    if (s->input.data[sp] == '\r') {
      sp++;
      if (sp >= s->input.len) return false;
      if (s->input.data[sp] == '\n') sp++;
      p = sp;
    } else {
      p = sp + 1;
    }
  }
}

/**
 * @brief Has the whole of a "&", "*" or "!" property arrived?
 *
 * The scanner hands the indicator back as one token and the name after it as
 * the next, and it consumes destructively: there is no putting the indicator
 * back once it has been taken.  So neither is taken until both are in the
 * buffer.  Without this, a caller feeding the input in small pieces took the
 * "&", found no name behind it yet, and dropped the anchor on the floor; the
 * name then arrived looking like an ordinary scalar.  "First occurrence:
 * &anchor Foo" fed a byte at a time came back with "anchor" as a scalar of
 * its own and "Foo" unanchored - a different document, decided by nothing
 * but where the caller happened to split the input.
 *
 * The name ends at white space, at a flow indicator, or at the end of the
 * input (5.3, 6.9.1, 7.1).  A verbatim tag "!<...>" is the exception: it runs
 * to its ">" and may hold any of those characters on the way.
 *
 * ns-anchor-char and ns-tag-char are both ns-char minus c-flow-indicator, so
 * a "," or a bracket really does end the name.  Listing them changes no
 * answer, only how soon one can be given: without them a property inside
 * "[&a,&b,&c]" would wait for the white space at the end of the collection
 * before the first anchor could be handed over.  Deferring is always safe,
 * which is why no test can tell the two apart.
 */
static bool ends_a_property_name(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'
      || ch == ',' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

static bool property_token_complete(const GTEXT_YAML_Scanner *s)
{
  size_t p = s->cursor + 1; /* past the indicator itself */
  if (p < s->input.len && s->input.data[s->cursor] == '!'
      && s->input.data[p] == '<') {
    for (p++; p < s->input.len; p++) {
      if (s->input.data[p] == '>') return true;
    }
    return false;
  }
  while (p < s->input.len && !ends_a_property_name(s->input.data[p])) p++;
  if (p >= s->input.len) return false;

  /* A bare "!" is the non-specific tag, and the caller only finds that out by
     reading the token after it: "!" then a scalar is a tagged node, while "!"
     then another "!" is the start of "!!str".  That second read has to
     succeed, because the "!" is already gone by the time it happens and
     there is nowhere to put it back - which is how "! a" fed a byte at a
     time lost its tag and came back as a plain "a" (suite cases 52DL and
     S4JQ).  So the node after it has to be here too.

     Only for a bare "!".  Waiting for the node after every tag changes no
     answer - deferring is always safe - and costs one more token of delay,
     so the rule stays where the need is. */
  if (s->input.data[s->cursor] == '!' && p == s->cursor + 1) {
    size_t q = p;
    while (q < s->input.len) {
      const char wc = s->input.data[q];
      if (wc != ' ' && wc != '\t' && wc != '\r' && wc != '\n') break;
      q++;
    }
    if (q >= s->input.len) return false;
    while (q < s->input.len && !ends_a_property_name(s->input.data[q])) q++;
    return q < s->input.len;
  }
  return true;
}

/**
 * @brief What a fold across one or more line breaks turned out to be.
 */
typedef enum {
  FOLD_OK,          /* folded; *p and *out_breaks are updated */
  FOLD_NEED_MORE,   /* not enough input yet to tell which of these it is */
  FOLD_FORBIDDEN,   /* a document marker opened a continuation line */
  FOLD_DEDENTED     /* a continuation line is not indented past the node */
} fold_result;

static fold_result scan_folded_breaks(
    const GTEXT_YAML_Scanner *s,
    size_t *p,
    size_t *out_breaks)
{
  size_t breaks = 0;
  size_t indent = 0;
  for (;;) {
    if (s->cursor + *p >= s->input.len) { *out_breaks = breaks; return FOLD_NEED_MORE; }
    int bc = (unsigned char)s->input.data[s->cursor + *p];
    if (bc == '\r') {
      (*p)++;
      if (s->cursor + *p < s->input.len && s->input.data[s->cursor + *p] == '\n') {
        (*p)++;
      }
    }
    else if (bc == '\n') { (*p)++; }
    else break;
    breaks++;
    /* Now at column 1 of the continuation line, which is the only place a
       document marker can be.  Deciding takes four bytes, not three: "..."
       is only a marker when what follows it is white space or the end of
       input, so "...x" cannot be told from "... x" until that byte is here.
       This is lookahead - the cursor has taken nothing yet - so asking for
       more is safe. */
    if (s->cursor + *p + 3 >= s->input.len && !s->finished) {
      *out_breaks = breaks;
      return FOLD_NEED_MORE;
    }
    if (line_starts_forbidden_marker(s, *p)) {
      *out_breaks = breaks;
      return FOLD_FORBIDDEN;
    }
    /* Indentation is spaces, never tabs (6.1).  A tab on a continuation
       line is separation, so it ends the indentation rather than adding to
       it: "bar" over a tab and "baz" leaves the second line at column 0,
       which is not indented past the mapping (suite case DK95/1). */
    indent = 0;
    while (s->cursor + *p < s->input.len
        && s->input.data[s->cursor + *p] == ' ') {
      (*p)++;
      indent++;
    }
    while (s->cursor + *p < s->input.len
        && (s->input.data[s->cursor + *p] == ' '
         || s->input.data[s->cursor + *p] == '\t')) {
      (*p)++;
    }
  }
  /* The loop leaves *p on the first content of the last continuation line,
     and `indent` is that line's indentation.  s-flow-folded(n) puts
     s-indent(n) in front of it (6.5), so a line at or left of the node the
     scalar belongs to is not part of the scalar at all.  Empty lines are not
     measured - the loop goes round again on them, and only the line that
     ends the fold is left in `indent`. */
  if (breaks > 0 && (int)indent <= s->node_indent) {
    *out_breaks = breaks;
    return FOLD_DEDENTED;
  }
  *out_breaks = breaks;
  return FOLD_OK;
}

/**
 * @brief Whether the tab now at the cursor is standing in for indentation.
 *
 * 6.1 counts indentation in spaces alone, and a tab after it is ordinary
 * separation - "foo:" over " \tbar" is a scalar value reached across one
 * space of indentation and a tab, which is valid. What a tab may not do is
 * sit between the indentation and a block collection entry: l+block-mapping
 * is ( s-indent(n) ns-l-block-map-entry(n) )+ with nothing allowed in
 * between, so "  \tb: 2" under "  a: 1" is malformed.
 *
 * Every tab in leading white space used to be refused, which took six valid
 * documents in yaml-test-suite with it - a line of nothing but a tab, a tab
 * before a flow collection at the root, and a tab before a plain value.
 */
static bool tab_stands_for_indentation(const GTEXT_YAML_Scanner *s)
{
  size_t i = s->cursor;
  while (i < s->input.len
      && (s->input.data[i] == ' ' || s->input.data[i] == '\t')) {
    i++;
  }
  if (i >= s->input.len) return false;
  const char first = s->input.data[i];
  /* A blank line, or one holding only a comment, indents nothing. */
  if (first == '\n' || first == '\r' || first == '#') return false;

  if (s->context_depth != 0) {
    /* Inside a flow collection the continuation lines still need s-indent(n)
       before their separation, and n is one past the node that owns the
       collection (s-l+flow-in-block's n+1). Only the first tab of a line
       reaches here and only when every character before it was a space, so
       the column counts those spaces. */
    return (s->col - 1) < s->node_indent + 1;
  }

  /* A flow collection is a node reached across separation, not an entry. */
  if (first == '[' || first == '{') return false;
  /* "-" or "?" followed by white space opens an entry right here. */
  if (first == '-' || first == '?') {
    const char next = (i + 1 < s->input.len) ? s->input.data[i + 1] : '\n';
    if (next == ' ' || next == '\t' || next == '\n' || next == '\r') return true;
  }

  /* Otherwise this is an entry only if the line carries a ":" that ends a
     key. Quoted spans are stepped over so a colon inside a scalar value
     does not count. */
  for (; i < s->input.len; i++) {
    const char ch = s->input.data[i];
    if (ch == '\n' || ch == '\r') break;
    if (ch == '"' || ch == '\'') {
      const char quote = ch;
      for (i++; i < s->input.len; i++) {
        if (s->input.data[i] == '\n' || s->input.data[i] == '\r') break;
        if (quote == '"' && s->input.data[i] == '\\') { i++; continue; }
        if (s->input.data[i] == quote) break;
      }
      continue;
    }
    if (ch == '#') break;
    if (ch == ':') {
      const char next = (i + 1 < s->input.len) ? s->input.data[i + 1] : '\n';
      if (next == ' ' || next == '\t' || next == '\n' || next == '\r') return true;
    }
  }
  return false;
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
  s->node_indent = -1;  /* nothing open yet: the document root */
  s->last_scalar_col = 0;
  s->last_json_like = false;
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
  /* Whether anything separated this position from the token before it. A
     comment has to be preceded by white space unless it opens the line
     (6.6: c-nb-comment-text follows s-separate-in-line), so "[a, b,#c" and
     'key: "value"# c' are malformed rather than commented. Both were being
     read as comments, which quietly threw the rest of the line away. */
  bool saw_separation = false;
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
    /* A tab is separation, never indentation (6.1), and the check applies
       wherever indentation is what is called for.  That is a line's leading
       white space, and also the gap after a block entry indicator: a nested
       entry reaches its sequence through s-indent(m) in s-l+block-indented,
       which is m spaces, so "-" TAB "-" has no indentation between the two
       and the second one is not nested at all.  It was building one anyway
       (suite cases Y79Y/4 and Y79Y/5).

       "-" TAB "-1" is unaffected: the second "-" is not an indicator there,
       it starts the scalar -1, and an ordinary node after an entry
       indicator is reached across s-separate, where a tab is fine.

       Flow context needs no test of its own.  A "-" inside "[" or "{" is
       refused by the parser when the indicator reaches it, which is before
       the scanner gets as far as the tab. */
    if (c == '\t'
        && (s->indent_ws || s->last_indicator == '-')
        && tab_stands_for_indentation(s)) {
      return scanner_tab_indent_error(s, err, 0);
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      scanner_consume(s);
      saw_separation = true;
      continue;
    }
    if (s->indent_ws && s->context_depth != 0
        && (s->col - 1) <= s->node_indent
        && c != ']' && c != '}') {
      /* A flow collection's continuation lines are indented past the node
         that owns it: s-l+flow-in-block(n) puts the collection at n+1, and
         every line of it needs s-indent of at least that (7.4, 6.1). A line
         at or left of the owner was being folded in regardless, so "k: {"
         over "k" over ":" over "v" at column 0 parsed as a nested mapping.

         The closing bracket is let through wherever it stands. Strictly it
         needs the same indentation, but

             key: [
               a,
               b
             ]

         is how people write this and both references accept it; the suite
         has no case either way. Content lines are the ones that matter. */
      if (err) {
        err->code = GTEXT_YAML_E_INVALID;
        err->message = "Flow collection line indented no further than its node";
        err->offset = s->offset;
        err->line = s->line;
        err->col = s->col;
      }
      return GTEXT_YAML_E_INVALID;
    }
    if (c == '#') {
      if (!saw_separation && s->col != 1) {
        if (err) {
          err->code = GTEXT_YAML_E_INVALID;
          err->message = "Comment must be preceded by white space";
          err->offset = s->offset;
          err->line = s->line;
          err->col = s->col;
        }
        return GTEXT_YAML_E_INVALID;
      }
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

    s->last_json_like = false;
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
    /* Ask for the whole block before taking any of it - see
       block_scalar_complete().  Nothing has been consumed yet, so the next
       call re-scans from the same place. */
    if (!s->finished && !block_scalar_complete(s)) {
      return GTEXT_YAML_E_INCOMPLETE;
    }
    /* consume the indicator */
    scanner_consume(s);
    /* The indentation of the line the header sits on. An indentation
       indicator counts from the parent node, and the parent node begins at
       the first non-space character of this line - the key, the "-" or the
       "?" that owns the scalar.

       At the root there is no such node, and the spec's n is -1 (8.1.2,
       8.1.3, where the content is at n+m). Taking the header's own column
       there made a root block scalar demand content indented past column 0,
       so "--- >" over three lines at column 0 collected nothing and the
       three lines came back as three documents' worth of separate nodes. */
    /* The indentation indicator counts from the block scalar's parent node,
       not from the line the header sits on (8.1.1.1).  Those are the same
       column for "literal: |2" at the root, and are not for

           - aaa: |2
               xxx
             bbb: |
               xxx

       where the line begins at 0 but the mapping holding "aaa" is at 2, so
       the content is at 4 and "bbb" is a sibling key.  Measuring from the
       line put the content at 2, which swallowed the rest of the mapping
       into the scalar (suite case 4WA9).  node_indent is the parent: a ":"
       sets it to its key's column, a "-" to its own. */
    int parent_indent = s->node_indent;
    /* The block header carries a chomping indicator and an indentation
       indicator, either one optional and in either order (8.1.1,
       c-b-block-header). This read the sign first and the digits second, so
       ">1-" left the "-" to be swallowed as part of the header's trailing
       comment and the scalar was chomped clip instead of strip. */
    int ch = scanner_peek(s);
    int chomping = 0; /* 0=clip(default), 1=keep(+), -1=strip(-) */
    size_t explicit_indent = 0; /* 0 == none provided */
    bool bad_header = false;
    for (int field = 0; field < 2; ++field) {
      if (ch == '+' || ch == '-') {
        if (chomping != 0) { bad_header = true; break; }
        chomping = (ch == '+') ? 1 : -1;
        scanner_consume(s);
        ch = scanner_peek(s);
        continue;
      }
      if (ch >= '0' && ch <= '9') {
        /* One digit, and not zero: c-indentation-indicator is
           ns-dec-digit - "0". "|0" and "|10" are both malformed headers and
           were being read as an indentation of 0 and of 10. */
        if (explicit_indent != 0 || ch == '0') { bad_header = true; break; }
        explicit_indent = (size_t)(ch - '0');
        scanner_consume(s);
        ch = scanner_peek(s);
        continue;
      }
      break;
    }
    /* Only white space, then a comment, then the end of the line may follow
       the header (s-b-block-header ends in s-b-comment). Anything else -
       "|1 2", "| junk" - is a malformed header, and both references refuse
       it rather than reading it as a comment. */
    if (!bad_header) {
      size_t look = 0;
      int after = ch;
      bool spaced = false;
      while (after == ' ' || after == '\t') {
        spaced = true;
        look++;
        after = (s->cursor + look < s->input.len)
          ? (unsigned char)s->input.data[s->cursor + look] : -1;
      }
      if (after != -1 && after != '\n' && after != '\r') {
        /* A comment needs white space in front of it (6.6), here as
           anywhere else, so "># comment" is a malformed header rather than
           a folded scalar with a comment. */
        if (after != '#' || !spaced) bad_header = true;
      }
    }
    if (bad_header) {
      if (err) {
        err->code = GTEXT_YAML_E_INVALID;
        err->message = "Malformed block scalar header";
        err->offset = off;
        err->line = line;
        err->col = col;
      }
      return GTEXT_YAML_E_INVALID;
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
      block_indent = (size_t)(parent_indent + (int)explicit_indent);
    } else {
      size_t scan = s->cursor;
      bool detected = false;
      size_t widest_empty = 0;
      while (scan < s->input.len) {
        size_t sp = scan;
        while (sp < s->input.len && s->input.data[sp] == ' ') sp++;
        if (sp >= s->input.len) break;
        char pc = s->input.data[sp];
        if (pc == '\n' || pc == '\r') { /* empty line: carries no indentation */
          if (sp - scan > widest_empty) widest_empty = sp - scan;
          scan = sp + 1;
          if (pc == '\r' && scan < s->input.len && s->input.data[scan] == '\n') scan++;
          continue;
        }
        block_indent = sp - scan;
        detected = true;
        break;
      }
      /* 8.1.1.1: "It is an error for any of the leading empty lines to
         contain more spaces than the first non-empty line." Without the
         rule there is no telling which indentation the block meant, and a
         leading run of wider blank lines was being taken as content. */
      if (detected && widest_empty > block_indent) {
        if (err) {
          err->code = GTEXT_YAML_E_INVALID;
          err->message = "Leading empty line indented past the block scalar";
          err->offset = off;
          err->line = line;
          err->col = col;
        }
        gtext_yaml_dynbuf_free(&scalar);
        return GTEXT_YAML_E_INVALID;
      }
      if (!detected) {
        if (!s->finished) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_INCOMPLETE; }
        /* Nothing but empty lines, so there is no first non-empty line to
           detect the indentation from.  The widest of them is the answer:
           every line is then indentation and the block is a run of breaks,
           which is what both references give.  Zero made those spaces
           content instead, so "- |+" over a line of three spaces came back
           as ["   \n"] where it is ["\n"] (suite case JEF9). */
        block_indent = widest_empty;
      } else if ((int)block_indent <= parent_indent) {
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

      /* "---" and "..." at column 1 end the document wherever they stand, and
         no block scalar reaches past one (9.1.2, 9.2).  The dedent test above
         cannot see that when the block's own indentation is zero, which is
         what "--- |" at the document level gives: the marker then looked like
         ordinary content and the scalar ran on through it, swallowing every
         document after it.  Spec example 9.5 - two documents - came back as
         one scalar holding the whole rest of the stream.

         Telling a marker from content needs the three characters and the
         one after them, and this scanner cannot rewind, so ask before
         taking the line - but only while what is in the buffer could still
         become a marker. A line that begins with anything else is already
         decided, and asking anyway waits for bytes that may never come:
         the empty last line of "|+" over "  a" over "  b" over "" is one
         byte at the end of the input, and deferring on it dropped the very
         break that keep chomping is for. */
      if (spaces == 0 && !s->finished) {
        const size_t avail = s->input.len - s->cursor;
        const char first = s->input.data[s->cursor];
        if (avail < 4 && (first == '-' || first == '.')) {
          size_t run = 0;
          while (run < avail && s->input.data[s->cursor + run] == first) run++;
          if (run == avail) GTEXT_YAML_BLOCK_NEED_MORE();
        }
      }
      if (spaces == 0 && line_starts_forbidden_marker(s, 0)) break;

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
            /* A more-indented line on either side of the run makes the break
               that opens it content too (8.1.3: b-l-spaced, and the
               b-as-line-feed that separates two l-nb-same-lines groups), so
               the run yields one break more than it has empty lines. Only
               the line before the run was being checked, so a blank line in
               front of a more-indented one lost a break - spec example 2.15
               came back with the blank above its indented block missing. */
            if (cur_more || nxt_more) tmp[out_pos++] = '\n';
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

    s->last_scalar_col = col - 1; /* col is 1-based */
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
  /* Both markers are productions of their own line: c-directives-end and
     c-document-end take no s-indent before them (9.1.2, 9.2), so they are
     only markers at column 1.  Without that test "a: --- b" ended the
     document in the middle of the value and left {"a": null} behind, where
     the "---" is plain content and both references read it as one. */
  if ((c == '-' || c == '.') && col == 1) {
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
          /* A "---" opens a document and its node may follow on the same
             line, but a "..." closes one and only a comment may follow:
             l-document-suffix is c-document-end s-l-comments (9.2). Content
             after it was starting a document of its own, so "... invalid"
             gave a second document holding "invalid". */
          if (c == '.') {
            size_t after = s->cursor + 3;
            while (after < s->input.len
                && (s->input.data[after] == ' ' || s->input.data[after] == '\t')) {
              after++;
            }
            if (after >= s->input.len && !s->finished) return GTEXT_YAML_E_INCOMPLETE;
            if (after < s->input.len) {
              const char tail = s->input.data[after];
              if (tail != '#' && tail != '\n' && tail != '\r') {
                if (err) {
                  err->code = GTEXT_YAML_E_INVALID;
                  err->message = "Content after a document-end marker";
                  err->offset = off;
                  err->line = line;
                  err->col = col;
                }
                return GTEXT_YAML_E_INVALID;
              }
            }
          }
          /* Consume all three characters */
          scanner_consume(s);
          scanner_consume(s);
          scanner_consume(s);
          
          /* A new document starts at the root again, with no block node
             open for a plain scalar to be measured against. */
          s->node_indent = -1;
          s->last_json_like = false;
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

  /* ":", "-" and "?" are indicators only where nothing plain-safe follows
     them: white space, the end of the line, or inside a flow collection one
     of the flow indicators (5.3, ns-plain-first, and
     c-ns-flow-map-separate-value's "not followed by ns-plain-safe").
     Anywhere else they open a plain scalar.

     That was already true of a ":" reached part way through a scalar, which
     is why "key: a :b" gives "a :b", but one that *began* a node was taken
     as an indicator - so "- ::vector" was refused for having no key in front
     of the colon. "-" and "?" had the same gap and it cost data rather than
     a refusal: "- !!int -2" gave [1, [2], 33] with -2 read as a nested
     sequence, and "{?foo: bar}" lost its key. */
  if (c == ':' || c == '-' || c == '?') {
    int nc;
    if (s->cursor + 1 < s->input.len) {
      nc = (unsigned char)s->input.data[s->cursor + 1];
    } else if (!s->finished) {
      return GTEXT_YAML_E_INCOMPLETE;
    } else {
      nc = -1;
    }
    const bool in_flow = scanner_current_context(s) != YAML_CONTEXT_BLOCK;
    const bool is_indicator = nc == -1 || nc == ' ' || nc == '\t'
      || nc == '\n' || nc == '\r'
      /* ns-plain-safe(flow-in) excludes every flow indicator, so one of
         these in front of it means the character is not plain: "{a:{b: 1}}"
         is a nested mapping, not the one scalar "a:{b". */
      || (in_flow && (nc == ',' || nc == '[' || nc == ']'
                   || nc == '{' || nc == '}'))
      /* 7.4.2: after a JSON-like key the ":" may be adjacent, which is what
         makes '{"a":1}' a mapping rather than the one scalar '"a":1'. Only
         the ":" has that rule; a "-" or "?" in that position is not an
         indicator. Widening it to all three survives the suite, because the
         only inputs that reach it - '{"a"-b: 1}' and the like - are refused
         for having no separator between two flow entries whichever way this
         goes. It stays narrow because that is what the grammar says. */
      || (c == ':' && in_flow && s->last_json_like);
    if (!is_indicator) goto scan_plain_scalar;
  }

  /* General single-byte indicators (e.g., '-', ':', '*', '&', ',', etc.) */
  if (is_indicator_char(c)) {
    /* A property or an alias is one thing, not two.  Ask for the rest of it
       before taking any of it - see property_token_complete().  This stands
       ahead of everything below because the lines that follow mutate the
       scanner, and there is no undoing them. */
    if ((c == '&' || c == '*' || c == '!') && !s->finished
        && !property_token_complete(s)) {
      return GTEXT_YAML_E_INCOMPLETE;
    }

    /* Update context stack for flow collection boundaries */
    if (c == '[') {
      scanner_push_context(s, YAML_CONTEXT_FLOW_SEQUENCE);
    } else if (c == '{') {
      scanner_push_context(s, YAML_CONTEXT_FLOW_MAPPING);
    } else if (c == ']' || c == '}') {
      scanner_pop_context(s);
    }
    
    const bool opens_block_node = (c == ':' || c == '-' || c == '?')
      && scanner_current_context(s) == YAML_CONTEXT_BLOCK;
    s->last_json_like = (c == ']' || c == '}');

    scanner_consume(s);

    /* ":" and "-" open a block node whose indentation is that of the line
       they appear on.  A plain scalar continues onto later lines only while
       they are indented past it (7.3.3's ns-plain-multi-line).  This is read
       after the consume, not before: an indicator that is itself the first
       non-space character of its line only sets line_indent as it is taken,
       and reading it earlier gave the previous line's indentation. */
    if (opens_block_node) {
      /* A ":" belongs to the node its key began, which is not the start of
         the line when the mapping is a sequence entry: in "- x: 1" the key
         sits at column 2 while the line begins at 0.  A "-" belongs where it
         stands, which nested sequences likewise put past the line's start.

         Unless the ":" opens its own line, with no key in front of it: an
         explicit key's value is written that way, and
         c-l-block-map-explicit-value(n) puts it at s-indent(n) - the
         mapping's indentation, not the key's (8.2.2).  Taking the key's
         column there measured the value's continuation lines against the
         wrong node, so in

             ? a
               true
             : null
               d

         the "  d" was not indented past what it was compared with and did
         not fold into "null d".  It arrived as a scalar of its own and was
         refused for being deeper than its mapping with no key to hold it
         (suite case JTV5). */
      s->node_indent = (c == ':' && (col - 1) != s->line_indent)
        ? s->last_scalar_col
        : (col - 1);
      /* "?" opens an explicit key, whose value arrives on a later line at the
         same column ("? a" over ": 1").  Without this the key's scalar folded
         across that break and swallowed its own ":". */
    }

    tok->type = GTEXT_YAML_TOKEN_INDICATOR;
    tok->u.c = (char)c;
    tok->offset = off;
    tok->line = line;
    tok->col = col;
    
    /* Track if this is an anchor, alias, or tag indicator.  A block entry
       "-" is recorded too, for the tab rule above: what may follow it
       depends on whether the next thing is another entry indicator.  Every
       reader of this field tests for one particular character, so the extra
       value reaches only the code that asks for it. */
    if (c == '&' || c == '*' || c == '!' || c == '-') {
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
    /* Where the run of literal white space now at the end of `scalar` began.
     * Flow folding drops the white space that precedes a line break, but only
     * the white space that was written literally: a `\t` escape is content
     * and survives a break, so escapes reset this to the end of the buffer
     * rather than extending the run. */
    size_t ws_start = 0;
    /* Set when the scanner has looked past the end of what has been fed and
     * still cannot tell where the scalar ends.  `look` alone cannot say so:
     * it is still pointing at the byte that needed a successor - a backslash
     * whose escape is cut off, or a break whose following line has not
     * arrived - and the end-of-buffer checks below would read that byte as
     * the closing quote and consume past it.  There is no rewind once
     * scanner_consume() has run, so the bytes would be gone. */
    bool want_more = false;
    for (;;) {
      int nc;
      if (s->cursor + look >= s->input.len) {
        nc = -1;
      } else {
        nc = (unsigned char)s->input.data[s->cursor + look];
      }
      if (nc == -1) break;

      if (nc == '\n' || nc == '\r') {
        /* Flow folding, for both quote styles (6.5, 7.3.1, 7.3.2): the white
         * space before the break is not content, one break folds to a space,
         * and a run of n breaks folds to n-1 line feeds. */
        size_t breaks = 0;
        size_t p = look;
        scalar.len = ws_start;
        fold_result fr = scan_folded_breaks(s, &p, &breaks);
        if (fr == FOLD_NEED_MORE) { want_more = true; break; }
        if (fr != FOLD_OK) {
          gtext_yaml_dynbuf_free(&scalar);
          if (err) {
            err->code = GTEXT_YAML_E_INVALID;
            err->message = (fr == FOLD_FORBIDDEN)
              ? "Document marker inside a multi-line scalar"
              : "Multi-line scalar not indented past the node it belongs to";
            err->offset = off;
            err->line = line;
            err->col = col;
          }
          return GTEXT_YAML_E_INVALID;
        }
        if (breaks == 1) {
          char sp = ' ';
          if (!gtext_yaml_dynbuf_append(&scalar, &sp, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
        }
        else {
          char lf = '\n';
          for (size_t i = 1; i < breaks; ++i) {
            if (!gtext_yaml_dynbuf_append(&scalar, &lf, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
          }
        }
        ws_start = scalar.len;
        look = p;
        continue;
      }

      if (quote == '\'') {
        /* single-quoted: two single-quotes -> one quote, otherwise end */
        if (nc == '\'') {
          /* check next char to see if it's an escaped single-quote */
          if (s->cursor + look + 1 >= s->input.len) {
            /* A second quote would make this an escaped one, so the decision
             * needs the next byte - unless there is no next byte to come, in
             * which case this quote closes the scalar. */
            if (!s->finished) want_more = true;
            break;
          }
          int nextc = (unsigned char)s->input.data[s->cursor + look + 1];
          if (nextc == '\'') {
            char ch = '\'';
            if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            ws_start = scalar.len;
            look += 2;
            continue;
          }
          /* otherwise a lone quote marks end of scalar */
          break;
        }
        /* normal character inside single-quoted scalar */
        char ch = (char)nc;
        if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
        if (ch != ' ' && ch != '\t') ws_start = scalar.len;
        look++;
        continue;
      }

      /* double-quoted handling */
      if (quote == '"') {
        if (nc == '"') {
          /* end of double-quoted scalar */
          break;
        }
        if (nc == '\\') {
          /* escape sequence; need next char */
          if (s->cursor + look + 1 >= s->input.len) {
            /* incomplete escape */
            want_more = true;
            break;
          }
          int esc = (unsigned char)s->input.data[s->cursor + look + 1];
          if (esc == '\n' || esc == '\r') {
            /* An escaped break (7.3.1 s-double-escaped) is removed rather
             * than folded to a space, and the white space before the
             * backslash stays as content - spec example 7.5 keeps the tab
             * there.  Empty lines after it still fold to line feeds. */
            size_t breaks = 0;
            size_t p = look + 1;
            fold_result fr = scan_folded_breaks(s, &p, &breaks);
            if (fr == FOLD_NEED_MORE) { want_more = true; break; }
            if (fr != FOLD_OK) {
              gtext_yaml_dynbuf_free(&scalar);
              if (err) {
                err->code = GTEXT_YAML_E_INVALID;
                err->message = (fr == FOLD_FORBIDDEN)
                  ? "Document marker inside a multi-line scalar"
                  : "Multi-line scalar not indented past the node it belongs to";
                err->offset = off;
                err->line = line;
                err->col = col;
              }
              return GTEXT_YAML_E_INVALID;
            }
            char lf = '\n';
            for (size_t i = 1; i < breaks; ++i) {
              if (!gtext_yaml_dynbuf_append(&scalar, &lf, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            }
            ws_start = scalar.len;
            look = p;
            continue;
          }
          if (esc == 'n' || esc == 'r' || esc == 't' || esc == '"' || esc == '\\' ||
              esc == '0' || esc == 'a' || esc == 'b' || esc == 'f' || esc == 'v' ||
              esc == 'e' || esc == ' ' || esc == '/' || esc == '\t') {
            char outc;
            switch (esc) {
              case 'n': outc = '\n'; break;
              case 'r': outc = '\r'; break;
              case 't': outc = '\t'; break;
              case '\t': outc = '\t'; break;   /* "\<TAB>" is a tab (5.7) */
              case '"': outc = '"'; break;
              case '\\': outc = '\\'; break;
              case '/': outc = '/'; break;      /* for JSON compatibility */
              case ' ': outc = ' '; break;
              case '0': outc = '\0'; break;
              case 'a': outc = '\a'; break;
              case 'b': outc = '\b'; break;
              case 'f': outc = '\f'; break;
              case 'v': outc = '\v'; break;
              case 'e': outc = '\x1B'; break;  /* ESC character */
              default: outc = (char)esc; break;
            }
            if (!gtext_yaml_dynbuf_append(&scalar, &outc, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            ws_start = scalar.len;
            look += 2;
            continue;
          }
          /* hex escape: \xNN (2 hex digits) */
          if (esc == 'x') {
            /* need two hex digits beyond the 'x' */
            if (s->cursor + look + 3 >= s->input.len) { want_more = true; break; }
            int h1 = (unsigned char)s->input.data[s->cursor + look + 2];
            int h2 = (unsigned char)s->input.data[s->cursor + look + 3];
            int v1 = hexval(h1);
            int v2 = hexval(h2);
            if (v1 < 0 || v2 < 0) {
              /* invalid hex -> conservative treat as literal chars */
              char c1 = (char)h1; char c2 = (char)h2;
              if (!gtext_yaml_dynbuf_append(&scalar, &c1, 1) || !gtext_yaml_dynbuf_append(&scalar, &c2, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
              ws_start = scalar.len;
              look += 4; continue;
            }
            char outc = (char)((v1 << 4) | v2);
            if (!gtext_yaml_dynbuf_append(&scalar, &outc, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            ws_start = scalar.len;
            look += 4; continue;
          }
          /* "\N", "\_", "\L" and "\P" are the named non-ASCII escapes of
             5.7: next line, non-breaking space, line separator and
             paragraph separator. */
          if (esc == 'N' || esc == '_' || esc == 'L' || esc == 'P') {
            static const unsigned int named[] = { 0x85, 0xA0, 0x2028, 0x2029 };
            const unsigned int code =
              named[esc == 'N' ? 0 : esc == '_' ? 1 : esc == 'L' ? 2 : 3];
            char nbuf[4];
            int nlen = 0;
            if (code <= 0x7FF) {
              nbuf[0] = (char)(0xC0 | ((code >> 6) & 0x1F));
              nbuf[1] = (char)(0x80 | (code & 0x3F));
              nlen = 2;
            }
            else {
              nbuf[0] = (char)(0xE0 | ((code >> 12) & 0x0F));
              nbuf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
              nbuf[2] = (char)(0x80 | (code & 0x3F));
              nlen = 3;
            }
            if (!gtext_yaml_dynbuf_append(&scalar, nbuf, nlen)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
            ws_start = scalar.len;
            look += 2;
            continue;
          }
          /* unicode escapes: \uNNNN (4 hex) and \UNNNNNNNN (8 hex) */
          if (esc == 'u' || esc == 'U') {
            int need = (esc == 'u') ? 4 : 8;
            if (s->cursor + look + 1 + need >= s->input.len) { want_more = true; break; }
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
            ws_start = scalar.len;
            look += 2 + need; continue;
          }
          /* 5.7 lists every escape a double-quoted scalar may carry, and
             anything else is malformed rather than a literal. This copied
             the character through, so '"\\."' came back as "." where every
             other parser refuses it. */
          if (err) {
            err->code = GTEXT_YAML_E_BAD_ESCAPE;
            err->message = "Unknown escape in double-quoted scalar";
            err->offset = off;
            err->line = line;
            err->col = col;
          }
          gtext_yaml_dynbuf_free(&scalar);
          return GTEXT_YAML_E_BAD_ESCAPE;
        }
        /* normal character inside double-quoted scalar */
        char ch = (char)nc;
        if (!gtext_yaml_dynbuf_append(&scalar, &ch, 1)) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
        if (ch != ' ' && ch != '\t') ws_start = scalar.len;
        look++;
        continue;
      }
    }

    /* If we reached end of buffer and haven't seen the closing quote, it's incomplete */
    const bool quote_at_end = want_more || (s->cursor + look) >= s->input.len;
    if (quote_at_end && !s->finished) {
      gtext_yaml_dynbuf_free(&scalar);
      return GTEXT_YAML_E_INCOMPLETE;
    }

    /* At this point, s->cursor+look points at either closing-quote or EOF. If EOF and finished==1,
       then we consider it an error (unterminated quote). */
    if (quote_at_end) {
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

    s->last_scalar_col = col - 1; /* col is 1-based */
    s->last_json_like = true;
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
scan_plain_scalar:
  ;
  GTEXT_YAML_DynBuf scalar;
  if (!gtext_yaml_dynbuf_init(&scalar)) return GTEXT_YAML_E_OOM;

  yaml_context_type ctx = scanner_current_context(s);
  
  /* If this scalar follows an anchor/alias indicator, it must be space-delimited
     (anchor/alias names cannot contain spaces per YAML spec) */
  int require_space_delimiter = (s->last_indicator == '&' || s->last_indicator == '*' || s->last_indicator == '!');

  /* A verbatim tag, "!<...>" (5.3, c-verbatim-tag): the URI between the
     brackets is taken as written, and the ordinary plain-scalar rules do not
     apply to it - a ":" inside it is not a key separator. Without this the
     scanner read "!<tag:yaml.org,2002:str> foo" as a plain scalar starting
     part way through the URI. */
  if (s->last_indicator == '!' && scanner_peek(s) == '<') {
    size_t vlen = 1;
    for (;;) {
      if (s->cursor + vlen >= s->input.len) {
        if (!s->finished) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_INCOMPLETE; }
        if (err) {
          err->code = GTEXT_YAML_E_INVALID;
          err->message = "Unterminated verbatim tag";
          err->offset = off;
          err->line = line;
          err->col = col;
        }
        gtext_yaml_dynbuf_free(&scalar);
        return GTEXT_YAML_E_INVALID;
      }
      const char vc = s->input.data[s->cursor + vlen];
      vlen++;
      if (vc == '>') break;
      if (vc == '\n' || vc == '\r') {
        if (err) {
          err->code = GTEXT_YAML_E_INVALID;
          err->message = "Unterminated verbatim tag";
          err->offset = off;
          err->line = line;
          err->col = col;
        }
        gtext_yaml_dynbuf_free(&scalar);
        return GTEXT_YAML_E_INVALID;
      }
    }
    if (!gtext_yaml_dynbuf_append(&scalar, s->input.data + s->cursor, vlen)) {
      gtext_yaml_dynbuf_free(&scalar);
      return GTEXT_YAML_E_OOM;
    }
    for (size_t i = 0; i < vlen; ++i) scanner_consume(s);
    char *vout = (char *)malloc(scalar.len);
    if (!vout) { gtext_yaml_dynbuf_free(&scalar); return GTEXT_YAML_E_OOM; }
    memcpy(vout, scalar.data, scalar.len);
    const size_t vsize = scalar.len;
    gtext_yaml_dynbuf_free(&scalar);
    s->token_payload = vout;
    s->last_scalar_col = col - 1;
    tok->type = GTEXT_YAML_TOKEN_SCALAR;
    tok->scalar_style = GTEXT_YAML_SCALAR_STYLE_PLAIN;
    tok->u.scalar.ptr = vout;
    tok->u.scalar.len = vsize;
    tok->offset = off;
    tok->line = line;
    tok->col = col;
    s->last_indicator = 0;
    return GTEXT_YAML_OK;
  }
  
  size_t look = 0;
  /* The buffer running out is not the input running out.  Every lookahead
     below reads -1 when it falls off the end of what has arrived so far, and
     each of them used to read that as "the token ends here".  It does only
     when the stream is finished; otherwise more may still be coming and the
     token may go on.  Feeding "name: Mark McGwire" one byte at a time came
     back as the two scalars "Mark" and "McGwire", because the space was
     judged a terminator by a lookahead that had simply run out of bytes -
     so what the parser produced depended on how the caller had chopped up
     the input, which is the one thing a streaming parser may not do.

     Asking for more is safe at any point in this loop: `look` is an offset
     into the buffer, nothing has been consumed, and the consume loop runs
     only after the token is settled.  The next call re-scans from here. */
#define GTEXT_YAML_PLAIN_NEED_MORE()                       \
      do {                                                 \
        if (!s->finished) {                                \
          gtext_yaml_dynbuf_free(&scalar);                 \
          return GTEXT_YAML_E_INCOMPLETE;                  \
        }                                                  \
      } while (0)
  while (1) {
    if (s->cursor + look >= s->input.len) {
      c = -1;
    } else {
      c = (unsigned char)s->input.data[s->cursor + look];
    }
    /* Falling off the end here needs no separate ask: `look` is then at the
       end of the buffer, which is exactly what the guard after this loop
       tests. The lookaheads below are the ones that need it, because they
       break with `look` still pointing at a character. */
    if (c == -1) break;
    
    /* Context-aware whitespace handling */
    if (ctx == YAML_CONTEXT_BLOCK && !require_space_delimiter) {
      /* In block context, plain scalars can contain spaces and tabs,
         and continue onto following lines indented past the node they belong
         to (7.3.3 ns-plain-multi-line).  A single break folds to a space; a
         run of blank lines gives one line break each, as flow folding does. */
      if (c == '\r' || c == '\n') {
        size_t breaks = 0;
        size_t continue_at = 0;
        bool need_more = false;
        const bool continues =
          plain_scalar_continues(s, look, false, &breaks, &continue_at, &need_more);

        /* Without the rest of the input there is no telling whether the
           scalar goes on, and this scanner cannot rewind to ask again. */
        if (need_more && !s->finished) {
          gtext_yaml_dynbuf_free(&scalar);
          return GTEXT_YAML_E_INCOMPLETE;
        }
        if (!continues || scalar.len == 0) break;

        const size_t folded = (breaks == 1) ? 1 : (breaks - 1);
        const char fold_ch = (breaks == 1) ? ' ' : '\n';
        for (size_t i = 0; i < folded; i++) {
          if (!gtext_yaml_dynbuf_append(&scalar, &fold_ch, 1)) {
            gtext_yaml_dynbuf_free(&scalar);
            if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; }
            return GTEXT_YAML_E_OOM;
          }
        }
        look = continue_at;
        continue;
      }
      
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
        
        /* Break at the end of input. */
        if (next_c == -1) { GTEXT_YAML_PLAIN_NEED_MORE(); break; }
        /* White space before a line break is separation rather than content.
           Step over it so the break itself decides whether the scalar goes on
           to the next line. */
        if (next_c == '\r' || next_c == '\n') {
          if (scalar.len == 0) break;
          look += ws_len;
          continue;
        }

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
          if (after_colon == -1) GTEXT_YAML_PLAIN_NEED_MORE();
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
        if (next_c == -1) GTEXT_YAML_PLAIN_NEED_MORE();
        if (next_c == -1 || next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c ==  '\n') {
          /* A key on a continuation line is refused by the parser, which
             already rejects a key that is not on the same line as its ':'.
             A check here as well was tried and removed: no input reached it
             that the parser did not catch first, with a better message. */
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
          if (next_c == -1) GTEXT_YAML_PLAIN_NEED_MORE();
          if (next_c == -1 || next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n') {
            break;
          }
        }

        if (c == '#' || c == '&' || c == '*' || c == '!') break;
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') break;
        if (c == '|' || c == '>' || c == '%') break;
      }
    } else if (require_space_delimiter) {
      /* An anchor, alias or tag name really is space-delimited: 5.3 does not
         allow white space in one. What it does allow is everything else
         except a flow indicator - ns-anchor-char is ns-char minus
         c-flow-indicator, and ns-tag-char likewise - so a ":" and the other
         indicators are part of the name. Ending the name at a ":" split
         "&an:chor value" into the anchor "an" and the scalar ":chor value",
         and left the anchor "&:@*!$\"<foo>:" of suite case W5VH empty. */
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
      if (c == ',' || c == '[' || c == ']' || c == '{' || c == '}') break;
      if (scalar.len == 0 && c == '!' && s->last_indicator != '!') break;
    } else {
      /* Flow context.  A plain scalar here may contain white space just as it
         may in block context - 7.3.3's ns-plain-char does not exclude it, and
         only c-flow-indicator is added to what ends the scalar.  Treating a
         space as a delimiter did not merely refuse "[a - b]": it silently
         split valid documents, so "[a b, c]" came out as three entries rather
         than two and "{k: v w, j: x}" was scrambled outright. */
      if (c == '\r' || c == '\n') {
        /* A flow scalar folds across a break the same way a block one does,
           except that a line starting with the collection's own punctuation
           ends it rather than continuing it. */
        size_t breaks = 0;
        size_t continue_at = 0;
        bool need_more = false;
        const bool continues =
          plain_scalar_continues(s, look, true, &breaks, &continue_at, &need_more);

        if (need_more && !s->finished) {
          gtext_yaml_dynbuf_free(&scalar);
          return GTEXT_YAML_E_INCOMPLETE;
        }
        if (!continues || scalar.len == 0) break;

        const size_t folded = (breaks == 1) ? 1 : (breaks - 1);
        const char fold_ch = (breaks == 1) ? ' ' : '\n';
        for (size_t fi = 0; fi < folded; fi++) {
          if (!gtext_yaml_dynbuf_append(&scalar, &fold_ch, 1)) {
            gtext_yaml_dynbuf_free(&scalar);
            if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; }
            return GTEXT_YAML_E_OOM;
          }
        }
        look = continue_at;
        continue;
      }

      if (c == ' ' || c == '\t') {
        /* Look past the run of white space to see whether the scalar goes on.
           Trailing white space is not part of it, so it is only appended once
           something after it turns out to be content. */
        size_t ws_len = 1;
        while (s->cursor + look + ws_len < s->input.len) {
          int peek_c = (unsigned char)s->input.data[s->cursor + look + ws_len];
          if (peek_c != ' ' && peek_c != '\t') break;
          ws_len++;
        }
        int next_c = -1;
        if (s->cursor + look + ws_len < s->input.len) {
          next_c = (unsigned char)s->input.data[s->cursor + look + ws_len];
        }

        if (next_c == -1) { GTEXT_YAML_PLAIN_NEED_MORE(); break; }
        /* White space before a break is separation; step over it and let the
           break decide whether the scalar goes on. */
        if (next_c == '\r' || next_c == '\n') {
          if (scalar.len == 0) break;
          look += ws_len;
          continue;
        }
        if (next_c == '#') break; /* " #" opens a comment */
        if (next_c == ',' || next_c == '[' || next_c == ']'
            || next_c == '{' || next_c == '}') {
          break;
        }
        if (next_c == ':') {
          int after_colon = -1;
          if (s->cursor + look + ws_len + 1 < s->input.len) {
            after_colon =
                (unsigned char)s->input.data[s->cursor + look + ws_len + 1];
          }
          if (after_colon == -1) GTEXT_YAML_PLAIN_NEED_MORE();
          if (after_colon == -1 || after_colon == ' ' || after_colon == '\t'
              || after_colon == '\r' || after_colon == '\n'
              || after_colon == ',' || after_colon == '[' || after_colon == ']'
              || after_colon == '{' || after_colon == '}') {
            break;
          }
        }

        if (scalar.len == 0) break; /* leading white space is separation */

        for (size_t i = 0; i < ws_len; i++) {
          char wch = (char)s->input.data[s->cursor + look + i];
          if (!gtext_yaml_dynbuf_append(&scalar, &wch, 1)) {
            gtext_yaml_dynbuf_free(&scalar);
            if (err) { err->code = GTEXT_YAML_E_OOM; err->message = "out of memory"; }
            return GTEXT_YAML_E_OOM;
          }
        }
        look += ws_len;
        continue;
      }

      /* The flow indicators always end the scalar: without them the
         collection could never be closed. */
      if (c == ',' || c == '[' || c == ']' || c == '{' || c == '}') break;

      /* A ':' ends the scalar only where it separates a key from a value -
         followed by white space, a flow indicator or the end.  Elsewhere it
         is content, so "[a:b, c]" holds "a:b". */
      if (c == ':') {
        int next_c = -1;
        if (s->cursor + look + 1 < s->input.len) {
          next_c = (unsigned char)s->input.data[s->cursor + look + 1];
        }
        if (next_c == -1) GTEXT_YAML_PLAIN_NEED_MORE();
        if (next_c == -1 || next_c == ' ' || next_c == '\t'
            || next_c == '\r' || next_c == '\n'
            || next_c == ',' || next_c == '[' || next_c == ']'
            || next_c == '{' || next_c == '}') {
          break;
        }
      }

      /* The rest are indicators only where a node may begin (5.3).  Mid-scalar
         they are content, so "[a-b, c]" holds "a-b" rather than ending the
         scalar at the dash.

         ":", "-" and "?" are the exception: the rule at the top of the
         scanner has already decided whether this one is an indicator, and
         if it is not then it is a plain character even at the start of the
         scalar - ns-plain-first allows all three when a plain-safe
         character follows. Ending the scalar here instead left it empty, so
         "{x: :x}" and "[-1, 2]" produced no token at all and the collection
         was reported as never closed. */
      if (scalar.len == 0 && c != ':' && c != '-' && c != '?'
          && is_indicator_char(c)) {
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

  /* A plain scalar never ends in white space: nb-ns-plain-in-line(c) is
     ( s-white* ns-plain-char(c) )*, so every space has to be followed by a
     plain character to be content. The same-line case is handled as the
     scalar is collected, but a break that folds to a space and is then
     followed by something that ends the scalar leaves one behind - "{foo"
     over ": bar}" gave the key "foo " with the fold still on it. */
  while (scalar.len > 0
      && (scalar.data[scalar.len - 1] == ' '
       || scalar.data[scalar.len - 1] == '\t')) {
    scalar.len--;
  }

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

#undef GTEXT_YAML_PLAIN_NEED_MORE

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

  s->last_scalar_col = col - 1; /* col is 1-based */
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
