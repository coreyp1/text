/**
 * @file stream.c
 * @brief Minimal streaming parser that wraps the scanner and emits events.
 *
 * This is a skeleton implementation that converts scanner tokens into the
 * streaming event callback. It supports emitting scalar and indicator events.
 */

#define _POSIX_C_SOURCE 200809L  /* for strdup */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_resolver.h>

/* Forward-declare scanner type in case internal header isn't visible at this
  inclusion point due to include-path differences. */
typedef struct GTEXT_YAML_Scanner GTEXT_YAML_Scanner;

static void directive_split(
  const char *line,
  size_t len,
  char *name,
  size_t name_cap,
  char *arg1,
  size_t arg1_cap,
  char *arg2,
  size_t arg2_cap
) {
  size_t i = 0;
  size_t out = 0;

  if (name_cap > 0) name[0] = '\0';
  if (arg1_cap > 0) arg1[0] = '\0';
  if (arg2_cap > 0) arg2[0] = '\0';

  while (i < len && line[i] == ' ') i++;

  out = 0;
  while (i < len && line[i] != ' ') {
    if (out + 1 < name_cap) name[out++] = line[i];
    i++;
  }
  if (name_cap > 0) name[out < name_cap ? out : name_cap - 1] = '\0';

  while (i < len && line[i] == ' ') i++;

  out = 0;
  while (i < len && line[i] != ' ') {
    if (out + 1 < arg1_cap) arg1[out++] = line[i];
    i++;
  }
  if (arg1_cap > 0) arg1[out < arg1_cap ? out : arg1_cap - 1] = '\0';

  while (i < len && line[i] == ' ') i++;

  out = 0;
  while (i < len && line[i] != ' ') {
    if (out + 1 < arg2_cap) arg2[out++] = line[i];
    i++;
  }
  if (arg2_cap > 0) arg2[out < arg2_cap ? out : arg2_cap - 1] = '\0';
}

struct GTEXT_YAML_Stream {
  GTEXT_YAML_Scanner *scanner;
  GTEXT_YAML_Event_Callback cb;
  void *user;
  GTEXT_YAML_Parse_Options opts;
  size_t total_bytes_consumed;
  size_t current_depth;
  size_t alias_expansion_count;
  ResolverState *resolver;
  char *pending_anchor;  /* Anchor name to attach to next node (malloc'd, NULL if none) */
  char *pending_tag;  /* Tag to attach to next node (malloc'd, NULL if none) */
  int pending_tag_line; /* 1-based line pending_tag was written on */
  int pending_anchor_line; /* 1-based line pending_anchor was written on */
  /* Where the pending anchor or tag was written. An empty node made out of
     them has to be reported there and not at the token that proved them
     unclaimed, or the parser measures its indentation from the wrong line
     and closes the collection the node belongs to. */
  size_t pending_prop_offset;
  int pending_prop_line;
  int pending_prop_col;
  /* The leftmost property recorded for the node being built, and the line it
     was written on.  Several properties may apply to one node ("&a !!str x",
     or an anchor and a tag on separate lines); the one furthest left is the
     one that has to clear the open collection's indentation. */
  int pending_prop_min_col;
  int pending_prop_min_line;
  /* Where the property's line began, and whether the property was the first
     thing on it. Together these say whether a later line has left the
     position the property was written in. */
  int pending_prop_line_start;
  bool pending_prop_opens_line;
  bool pending_prop_line_dash;
  /* The line the stream is on, the column its first token stood at, whether
     everything on it so far has been a property or a document marker, and
     whether it opened with a "-". A line of nothing but properties
     introduces whatever follows it, however that is indented; a "-" line
     is a sequence entry, which matters because a block sequence may sit at
     its owning key's column but not at a sibling entry's. */
  int cur_line;
  int cur_line_start;
  bool cur_line_only_props;
  bool cur_line_opens_with_dash;
  bool pending_alias; /* True if alias indicator seen and name is pending */
  bool sync_mode; /* If true, call scanner_finish after each feed */
  bool document_started; /* True if we've emitted DOCUMENT_START */
  bool document_closed; /* True if current document is closed */
};

static GTEXT_YAML_Status stream_apply_alias_limit(GTEXT_YAML_Stream *s) {
  if (!s) return GTEXT_YAML_E_INVALID;
  if (s->opts.max_alias_expansion > 0) {
    if (s->alias_expansion_count + 1 > s->opts.max_alias_expansion) {
      return GTEXT_YAML_E_LIMIT;
    }
  }
  s->alias_expansion_count++;
  return GTEXT_YAML_OK;
}

/**
 * @brief Emit the empty node that an unclaimed anchor or tag belongs to.
 *
 * Properties without a node are properties of the empty node (7.2, e-node),
 * which resolves to null - or to the empty string where the tag says str.
 * The stream held them until something arrived that could take them, so
 * "- !!str" lost its entry entirely and "a: &anchor" over "b: *anchor"
 * handed the anchor to b, which then aliased to itself.
 *
 * @p at is the token that proved the properties had no node of their own,
 * and is used only for the position on the event.
 */
/**
 * @brief Whether the pending properties belong to a node that never arrived.
 *
 * A property that opened its own line introduces whatever follows, however
 * that is indented. One written part way along a line belongs to the position
 * it stands in, and a line that begins no further right has left that
 * position:
 *
 *     x: !custom      the "-" line begins further right, so the tag is the
 *       - 1           sequence's
 *
 *     a: !!str        the "b" line begins at the same column, so a's value
 *     b: 1            was never written and the tag is its
 */
static bool stream_props_left_behind(
  const GTEXT_YAML_Stream *s,
  const GTEXT_YAML_Token *tok
) {
  if (!s->pending_anchor && !s->pending_tag) return false;
  /* Inside a flow collection neither the line nor the indentation says
     anything, so the token settles it: a ",", a ":" or the collection's own
     closing bracket cannot be the node the properties name, which means that
     node is empty and they belong to it.  "{a: !!str}" is a's value being
     the empty string, "[&x]" an anchored null entry, and "{!!str : bar}" an
     empty key (spec example 7.2, suite case WZ62).  None of them reached the
     flush: the line tests below all say "same line" for a flow collection
     written on one, so the properties were carried past the end of the
     collection instead. */
  if (s->current_depth > 0 && tok->type == GTEXT_YAML_TOKEN_INDICATOR
      && (tok->u.c == ',' || tok->u.c == ':'
       || tok->u.c == ']' || tok->u.c == '}')) {
    return true;
  }
  if (tok->line == s->pending_prop_line) return false;
  if (s->pending_prop_opens_line) return false;
  if (s->cur_line_start > s->pending_prop_line_start) return false;
  /* A block sequence may stand at the column of the key that owns it, so a
     "-" there is the value position rather than a sibling - unless the
     property's own line was a sequence entry, in which case it is the next
     entry and the one before it was empty:

         sequence: !!seq      the "-" is sequence's value, so the tag is
         - entry              the sequence's

         - &a                 the "-" is the next entry, so the anchor
         - b                  belongs to the empty one above it */
  if (tok->type == GTEXT_YAML_TOKEN_INDICATOR && tok->u.c == '-'
      && s->cur_line_start == s->pending_prop_line_start
      && !s->pending_prop_line_dash) {
    return false;
  }
  return true;
}

/**
 * @brief Note that a "-" has taken the pending properties for its sequence.
 *
 * From here the properties introduce that sequence, so nothing later on the
 * line can leave them behind - without this the entry's own scalar looked
 * like a line that had, and "sequence: !!seq" over "- entry" gained an empty
 * first entry.
 */
static void stream_props_claimed_by_sequence(GTEXT_YAML_Stream *s) {
  if (!s->pending_anchor && !s->pending_tag) return;
  if (s->cur_line_start != s->pending_prop_line_start) return;
  if (s->pending_prop_line_dash) return;
  s->pending_prop_opens_line = true;
}

static GTEXT_YAML_Status stream_flush_empty_node(
  GTEXT_YAML_Stream *s,
  const GTEXT_YAML_Token *at
) {
  if (!s->pending_anchor && !s->pending_tag) return GTEXT_YAML_OK;

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "";
  ev.data.scalar.len = 0;
  ev.scalar_style = GTEXT_YAML_SCALAR_STYLE_PLAIN;
  ev.anchor = s->pending_anchor;
  ev.anchor_line = s->pending_anchor ? s->pending_anchor_line : 0;
  ev.tag = s->pending_tag;
  ev.tag_line = s->pending_tag_line;
  ev.prop_line = s->pending_prop_min_line;
  ev.prop_col = s->pending_prop_min_col;
  ev.offset = s->pending_prop_offset;
  ev.line = s->pending_prop_line;
  ev.col = s->pending_prop_col;
  (void)at;

  GTEXT_YAML_Status rc = GTEXT_YAML_OK;
  if (s->cb) rc = s->cb(s, &ev, s->user);

  free(s->pending_anchor);
  s->pending_anchor = NULL;
  s->pending_anchor_line = 0;
  free(s->pending_tag);
  s->pending_tag = NULL;
  s->pending_tag_line = 0;
  s->pending_prop_min_col = -1;
  s->pending_prop_min_line = 0;
  return rc;
}

static GTEXT_YAML_Status stream_emit_alias(GTEXT_YAML_Stream *s, GTEXT_YAML_Token *tok) {
  if (!s || !tok) return GTEXT_YAML_E_INVALID;
  if (tok->type != GTEXT_YAML_TOKEN_SCALAR) {
    return GTEXT_YAML_E_BAD_TOKEN;
  }

  /* Borrowed from the scanner, valid until the next token is requested. */
  const char *name = tok->u.scalar.ptr;
  size_t namelen = tok->u.scalar.len;
  char buf[256];
  if (namelen >= sizeof(buf)) namelen = sizeof(buf) - 1;
  memcpy(buf, name, namelen);
  buf[namelen] = '\0';

  GTEXT_YAML_Event alias_ev;
  memset(&alias_ev, 0, sizeof(alias_ev));
  alias_ev.type = GTEXT_YAML_EVENT_ALIAS;
  alias_ev.data.alias_name = buf;
  alias_ev.offset = tok->offset;
  alias_ev.line = tok->line;
  alias_ev.col = tok->col;

  GTEXT_YAML_Status alias_limit = stream_apply_alias_limit(s);
  if (alias_limit != GTEXT_YAML_OK) {
    return alias_limit;
  }

  if (s->cb) {
    GTEXT_YAML_Status cb_rc = s->cb(s, &alias_ev, s->user);
    if (cb_rc != GTEXT_YAML_OK) return cb_rc;
  }

  if (s->pending_tag) {
    free(s->pending_tag);
    s->pending_tag = NULL;
    s->pending_tag_line = 0;
  }

  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status stream_emit_document_start(
  GTEXT_YAML_Stream *s,
  const GTEXT_YAML_Token *tok
) {
  if (!s) return GTEXT_YAML_E_INVALID;

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  if (tok) {
    ev.offset = tok->offset;
    ev.line = tok->line;
    ev.col = tok->col;
  }

  if (s->cb) {
    GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
    if (rc != GTEXT_YAML_OK) return rc;
  }

  s->document_started = true;
  s->document_closed = false;
  return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status stream_emit_document_end(
  GTEXT_YAML_Stream *s,
  const GTEXT_YAML_Token *tok
) {
  if (!s) return GTEXT_YAML_E_INVALID;

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  if (tok) {
    ev.offset = tok->offset;
    ev.line = tok->line;
    ev.col = tok->col;
  }

  if (s->cb) {
    GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
    if (rc != GTEXT_YAML_OK) return rc;
  }

  s->document_closed = true;
  return GTEXT_YAML_OK;
}

/**
 * @brief Whether a "!" stands alone as the non-specific tag.
 *
 * "!" on its own is a whole tag property (5.3, c-non-specific-tag) and the
 * node follows it; "!foo" is a shorthand whose name begins at the very next
 * byte. The stream read the token after the "!" as the name either way, so
 * "! a" used the node as its own tag and came back as null.
 */
static bool stream_tag_is_non_specific(
  const GTEXT_YAML_Token *bang,
  const GTEXT_YAML_Token *next
) {
  return next->offset != bang->offset + 1;
}

static GTEXT_YAML_Status stream_ensure_document_started(
  GTEXT_YAML_Stream *s,
  const GTEXT_YAML_Token *tok
) {
  if (!s->document_started || s->document_closed) {
    return stream_emit_document_start(s, tok);
  }
  return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Stream * gtext_yaml_stream_new(
  const GTEXT_YAML_Parse_Options * opts,
  GTEXT_YAML_Event_Callback cb,
  void * user
) {
  GTEXT_YAML_Stream *s = (GTEXT_YAML_Stream *)malloc(sizeof(*s));
  if (!s) return NULL;
  memset(s, 0, sizeof(*s));
  s->cb = cb;
  s->user = user;
  s->opts = gtext_yaml_parse_options_effective(opts);
  s->total_bytes_consumed = 0;
  s->current_depth = 0;
  s->alias_expansion_count = 0;
  /* No property pending: column 0 is a real column, so "none" has to be -1
     rather than the zero memset() left here. */
  s->pending_prop_min_col = -1;
  s->pending_prop_min_line = 0;
  s->scanner = gtext_yaml_scanner_new();
  s->resolver = gtext_yaml_resolver_new(&s->opts);
  if (!s->scanner) { free(s); return NULL; }
  if (!s->resolver) {
    gtext_yaml_scanner_free(s->scanner);
    free(s);
    return NULL;
  }
  return s;
}

GTEXT_API void gtext_yaml_stream_free(GTEXT_YAML_Stream * s)
{
  if (!s) return;
  if (s->scanner) gtext_yaml_scanner_free(s->scanner);
  if (s->resolver) gtext_yaml_resolver_free(s->resolver);
  if (s->pending_anchor) free(s->pending_anchor);
  if (s->pending_tag) free(s->pending_tag);
  free(s);
}

/* Internal: Set synchronous mode (for use by gtext_yaml_parse) */
GTEXT_INTERNAL_API void gtext_yaml_stream_set_sync_mode(
  GTEXT_YAML_Stream *s,
  bool sync
) {
  if (s) s->sync_mode = sync;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_stream_feed(
  GTEXT_YAML_Stream * s,
  const char * data,
  size_t len
) {
  if (!s) return GTEXT_YAML_E_INVALID;
  /* Enforce total-bytes limit if set (0 means use library default already applied in opts) */
  if (s->opts.max_total_bytes > 0) {
    if (s->total_bytes_consumed + len > s->opts.max_total_bytes) return GTEXT_YAML_E_LIMIT;
    s->total_bytes_consumed += len;
  }

  if (!gtext_yaml_scanner_feed(s->scanner, data, len)) return GTEXT_YAML_E_OOM;
  
  /* In sync mode, mark scanner as finished immediately so we can process aliases */
  if (s->sync_mode) {
    gtext_yaml_scanner_finish(s->scanner);
  }

  GTEXT_YAML_Token tok;
  GTEXT_YAML_Error err;
  for (;;) {
    GTEXT_YAML_Status st = gtext_yaml_scanner_next(s->scanner, &tok, &err);
    if (st == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_OK; /* need more data */
    if (st != GTEXT_YAML_OK) return st;
    if (tok.type == GTEXT_YAML_TOKEN_EOF) {
      GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
      if (flush != GTEXT_YAML_OK) return flush;
      break;
    }
    if (tok.line != s->cur_line) {
      s->cur_line = tok.line;
      s->cur_line_start = tok.col;
      s->cur_line_only_props = true;
      s->cur_line_opens_with_dash =
        tok.type == GTEXT_YAML_TOKEN_INDICATOR && tok.u.c == '-';
    }
    if (!(tok.type == GTEXT_YAML_TOKEN_DOCUMENT_START
        || tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END
        || tok.type == GTEXT_YAML_TOKEN_COMMENT
        || (tok.type == GTEXT_YAML_TOKEN_INDICATOR
            && (tok.u.c == '&' || tok.u.c == '!')))) {
      s->cur_line_only_props = false;
    }

process_token:
    if (s->pending_alias) {
      if (tok.type != GTEXT_YAML_TOKEN_SCALAR) {
        return GTEXT_YAML_E_BAD_TOKEN;
      }
      s->pending_alias = false;
      GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &tok);
      if (doc_rc != GTEXT_YAML_OK) return doc_rc;
      GTEXT_YAML_Status alias_rc = stream_emit_alias(s, &tok);
      if (alias_rc != GTEXT_YAML_OK) return alias_rc;
      continue;
    }

    GTEXT_YAML_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.offset = tok.offset;
    ev.line = tok.line;
    ev.col = tok.col;

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_START) {
      {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      if (s->document_started && !s->document_closed) {
        GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      GTEXT_YAML_Status rc = stream_emit_document_start(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END) {
      {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      /* A "..." with no document open closes nothing: l-document-suffix
         stands on its own in a stream (9.2), and "..." by itself is a
         stream with no documents in it. Opening one here so that it could
         be closed gave every such suffix a null document of its own - a
         bare "..." parsed as one null, and one between two documents put a
         third between them. */
      if (!s->document_started || s->document_closed) {
        continue;
      }
      GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type != GTEXT_YAML_TOKEN_DIRECTIVE && tok.type != GTEXT_YAML_TOKEN_COMMENT) {
      GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &tok);
      if (doc_rc != GTEXT_YAML_OK) return doc_rc;
    }

    if (tok.type == GTEXT_YAML_TOKEN_COMMENT) {
      if (s->opts.retain_comments) {
        ev.type = GTEXT_YAML_EVENT_COMMENT;
        ev.data.comment.ptr = tok.u.comment.ptr;
        ev.data.comment.len = tok.u.comment.len;
        ev.data.comment.inline_comment = tok.u.comment.inline_comment;
        if (s->cb) {
          GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
          if (rc != GTEXT_YAML_OK) return rc;
        } else {
        }
      } else {
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DIRECTIVE) {
      char name[64];
      char arg1[128];
      char arg2[128];

      directive_split(tok.u.scalar.ptr, tok.u.scalar.len, name, sizeof(name), arg1, sizeof(arg1), arg2, sizeof(arg2));
      ev.type = GTEXT_YAML_EVENT_DIRECTIVE;
      ev.data.directive.name = name[0] ? name : NULL;
      ev.data.directive.value = arg1[0] ? arg1 : NULL;
      ev.data.directive.value2 = arg2[0] ? arg2 : NULL;

      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
      /* These end the node position rather than filling it, so properties
         still waiting belong to the empty node - but only where they were
         inside the position this indicator closes. A tag on a line of its
         own introduces the collection that follows it:

             !!seq        the tag belongs to the sequence
             - a

             - &a         the anchor belongs to the first entry, which is
             - b          empty, and the second "-" is what proves it

         The two are told apart by where their lines begin. A property that
         opened its own line introduces whatever follows, however that is
         indented. One written part way along a line belongs to the position
         it stands in, and a later line that begins no further right has
         left that position:

             x: !custom      the "-" line begins further right, so the tag
               - 1           is the sequence's

             a: !!str        the "b" line begins at the same column, so a's
             b: 1            value was never written and the tag is its */
      if ((tok.u.c == '-' || tok.u.c == ':' || tok.u.c == '?'
           || tok.u.c == ',' || tok.u.c == ']' || tok.u.c == '}')
          && (s->pending_anchor || s->pending_tag)
          && stream_props_left_behind(s, &tok)) {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      if (tok.u.c == '-') stream_props_claimed_by_sequence(s);
      ev.type = GTEXT_YAML_EVENT_INDICATOR;
      ev.data.indicator = tok.u.c;
      /* indicator event */
      /* Adjust depth for simple flow indicators and enforce max_depth */
      if (tok.u.c == '[' || tok.u.c == '{') {
        s->current_depth++;
        if (s->opts.max_depth > 0 && s->current_depth > s->opts.max_depth) {
          return GTEXT_YAML_E_DEPTH;
        }
        /* Emit collection START event instead of INDICATOR */
        GTEXT_YAML_Event start_ev;
        memset(&start_ev, 0, sizeof(start_ev));
        start_ev.type = (tok.u.c == '[')
          ? GTEXT_YAML_EVENT_SEQUENCE_START
          : GTEXT_YAML_EVENT_MAPPING_START;
        start_ev.anchor = s->pending_anchor;  /* Attach pending anchor if any */
        start_ev.anchor_line = s->pending_anchor ? s->pending_anchor_line : 0;
        start_ev.tag = s->pending_tag;
        start_ev.tag_line = s->pending_tag ? s->pending_tag_line : 0;
        start_ev.prop_line = s->pending_prop_min_line;
        start_ev.prop_col = s->pending_prop_min_col;
        start_ev.offset = tok.offset;
        start_ev.line = tok.line;
        start_ev.col = tok.col;
        
        if (s->cb) {
          GTEXT_YAML_Status rc = s->cb(s, &start_ev, s->user);
          if (rc != GTEXT_YAML_OK) return rc;
        }
        
        /* Clear pending anchor after attaching */
        if (s->pending_anchor) {
          free(s->pending_anchor);
          s->pending_anchor = NULL;
          s->pending_anchor_line = 0;
        }
        if (s->pending_tag) {
          free(s->pending_tag);
          s->pending_tag = NULL;
          s->pending_tag_line = 0;
        }
        s->pending_prop_min_col = -1;
        s->pending_prop_min_line = 0;
        continue;
      } else if (tok.u.c == ']' || tok.u.c == '}') {
        if (s->current_depth > 0) s->current_depth--;
        /* Emit collection END event instead of INDICATOR */
        GTEXT_YAML_Event end_ev;
        memset(&end_ev, 0, sizeof(end_ev));
        end_ev.type = (tok.u.c == ']')
          ? GTEXT_YAML_EVENT_SEQUENCE_END
          : GTEXT_YAML_EVENT_MAPPING_END;
        end_ev.offset = tok.offset;
        end_ev.line = tok.line;
        end_ev.col = tok.col;
        
        if (s->cb) {
          GTEXT_YAML_Status rc = s->cb(s, &end_ev, s->user);
          if (rc != GTEXT_YAML_OK) return rc;
        }
        continue;
      } else if (tok.u.c == '&') {
        /* Anchor definition: read anchor name and store it.
           The next token (handled by subsequent iteration) will pick it up. */
        GTEXT_YAML_Token name_tok;
        GTEXT_YAML_Error name_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
        if (nst == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_OK;
        if (nst != GTEXT_YAML_OK) return nst;
        if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
        
        size_t namelen = name_tok.u.scalar.len;
        char buf[256];
        if (namelen >= sizeof(buf)) namelen = sizeof(buf)-1;
        memcpy(buf, name_tok.u.scalar.ptr, namelen);
        buf[namelen] = '\0';
        
        /* Store anchor name - it will be attached to the next node event */
        if (s->pending_anchor) free(s->pending_anchor);
        s->pending_anchor = strdup(buf);
        s->pending_anchor_line = tok.line;
        s->pending_prop_offset = tok.offset;
        s->pending_prop_line = tok.line;
        s->pending_prop_col = tok.col;
        if (s->pending_prop_min_col < 0 || tok.col < s->pending_prop_min_col) {
          s->pending_prop_min_col = tok.col;
          s->pending_prop_min_line = tok.line;
        }
        s->pending_prop_line_start = s->cur_line_start;
        s->pending_prop_opens_line = s->cur_line_only_props;
        s->pending_prop_line_dash = s->cur_line_opens_with_dash;
        
        continue;
      } else if (tok.u.c == '!') {
        GTEXT_YAML_Token tag_tok;
        GTEXT_YAML_Error tag_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &tag_tok, &tag_err);
        if (nst == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_OK;
        if (nst != GTEXT_YAML_OK) return nst;

        char buf[256];
        size_t tag_len = 0;

        if (stream_tag_is_non_specific(&tok, &tag_tok)) {
          /* The "!" was the whole property. Record it and go round again
             with the token just read, which is the node it applies to. */
          if (s->pending_tag) free(s->pending_tag);
          s->pending_tag = strdup("!");
          s->pending_tag_line = tok.line;
          tok = tag_tok;
          goto process_token;
        }

        if (tag_tok.type == GTEXT_YAML_TOKEN_INDICATOR && tag_tok.u.c == '!') {
          GTEXT_YAML_Token name_tok;
          GTEXT_YAML_Error name_err;
          nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
          if (nst != GTEXT_YAML_OK) return nst;
          if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) {
            return GTEXT_YAML_E_BAD_TOKEN;
          }
          tag_len = name_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 3) tag_len = sizeof(buf) - 3;
          buf[0] = '!';
          buf[1] = '!';
          memcpy(buf + 2, name_tok.u.scalar.ptr, tag_len);
          tag_len += 2;
          buf[tag_len] = '\0';
        } else if (tag_tok.type == GTEXT_YAML_TOKEN_SCALAR
            && tag_tok.u.scalar.len >= 2
            && tag_tok.u.scalar.ptr[0] == '<'
            && tag_tok.u.scalar.ptr[tag_tok.u.scalar.len - 1] == '>') {
          /* A verbatim tag: "!<X>" is the tag X exactly as written, with no
             handle to expand (5.3). Keep the URI and drop the brackets. */
          tag_len = tag_tok.u.scalar.len - 2;
          if (tag_len > sizeof(buf) - 1) tag_len = sizeof(buf) - 1;
          memcpy(buf, tag_tok.u.scalar.ptr + 1, tag_len);
          buf[tag_len] = '\0';
        } else if (tag_tok.type == GTEXT_YAML_TOKEN_SCALAR) {
          tag_len = tag_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 2) tag_len = sizeof(buf) - 2;
          buf[0] = '!';
          memcpy(buf + 1, tag_tok.u.scalar.ptr, tag_len);
          tag_len += 1;
          buf[tag_len] = '\0';
        } else {
          return GTEXT_YAML_E_BAD_TOKEN;
        }

        if (s->pending_tag) free(s->pending_tag);
        s->pending_tag = strdup(buf);
        /* tok is the '!' that introduced the tag. */
        s->pending_tag_line = tok.line;
        s->pending_prop_offset = tok.offset;
        s->pending_prop_line = tok.line;
        s->pending_prop_col = tok.col;
        if (s->pending_prop_min_col < 0 || tok.col < s->pending_prop_min_col) {
          s->pending_prop_min_col = tok.col;
          s->pending_prop_min_line = tok.line;
        }
        s->pending_prop_line_start = s->cur_line_start;
        s->pending_prop_opens_line = s->cur_line_only_props;
        s->pending_prop_line_dash = s->cur_line_opens_with_dash;
        continue;
      } else if (tok.u.c == '*') {
        /* Process alias immediately; if name incomplete, defer to next feed */
        GTEXT_YAML_Token next_tok;
        GTEXT_YAML_Error next_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &next_tok, &next_err);
        if (nst == GTEXT_YAML_E_INCOMPLETE) {
          s->pending_alias = true;
          return GTEXT_YAML_OK;
        }
        if (nst != GTEXT_YAML_OK) return nst;
        GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &next_tok);
        if (doc_rc != GTEXT_YAML_OK) return doc_rc;
        return stream_emit_alias(s, &next_tok);
      }
      /* Emit remaining indicators (commas, colons, etc.) */
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DIRECTIVE) {
      char name[64];
      char arg1[128];
      char arg2[128];

      directive_split(tok.u.scalar.ptr, tok.u.scalar.len, name, sizeof(name), arg1, sizeof(arg1), arg2, sizeof(arg2));
      ev.type = GTEXT_YAML_EVENT_DIRECTIVE;
      ev.data.directive.name = name[0] ? name : NULL;
      ev.data.directive.value = arg1[0] ? arg1 : NULL;
      ev.data.directive.value2 = arg2[0] ? arg2 : NULL;

      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_SCALAR) {
      /* scalar event */
      if (stream_props_left_behind(s, &tok)) {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      ev.type = GTEXT_YAML_EVENT_SCALAR;
      ev.data.scalar.ptr = tok.u.scalar.ptr;
      ev.data.scalar.len = tok.u.scalar.len;
      ev.scalar_style = tok.scalar_style;
      /* Attach pending anchor if any */
      ev.anchor = s->pending_anchor;
      ev.anchor_line = s->pending_anchor ? s->pending_anchor_line : 0;
      ev.tag = s->pending_tag;
      ev.tag_line = s->pending_tag ? s->pending_tag_line : 0;
      ev.prop_line = s->pending_prop_min_line;
      ev.prop_col = s->pending_prop_min_col;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) {
          return rc;
        }
      }
      /* Clear pending anchor after attaching */
      if (s->pending_anchor) {
        free(s->pending_anchor);
        s->pending_anchor = NULL;
        s->pending_anchor_line = 0;
      }
      if (s->pending_tag) {
        free(s->pending_tag);
        s->pending_tag = NULL;
        s->pending_tag_line = 0;
      }
      s->pending_prop_min_col = -1;
      s->pending_prop_min_line = 0;
      continue;
    }
  }

  return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_stream_finish(GTEXT_YAML_Stream * s)
{
  if (!s) return GTEXT_YAML_E_INVALID;
  if (!s->scanner) return GTEXT_YAML_OK;
  gtext_yaml_scanner_finish(s->scanner);

  /* Drain any remaining tokens now that the scanner is finished. */
  for (;;) {
    GTEXT_YAML_Token tok;
    GTEXT_YAML_Error err;
    
    GTEXT_YAML_Status st = gtext_yaml_scanner_next(s->scanner, &tok, &err);
    if (st == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_OK;
    if (st != GTEXT_YAML_OK) return st;
    if (tok.type == GTEXT_YAML_TOKEN_EOF) {
      GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
      if (flush != GTEXT_YAML_OK) return flush;
      break;
    }
    if (tok.line != s->cur_line) {
      s->cur_line = tok.line;
      s->cur_line_start = tok.col;
      s->cur_line_only_props = true;
      s->cur_line_opens_with_dash =
        tok.type == GTEXT_YAML_TOKEN_INDICATOR && tok.u.c == '-';
    }
    if (!(tok.type == GTEXT_YAML_TOKEN_DOCUMENT_START
        || tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END
        || tok.type == GTEXT_YAML_TOKEN_COMMENT
        || (tok.type == GTEXT_YAML_TOKEN_INDICATOR
            && (tok.u.c == '&' || tok.u.c == '!')))) {
      s->cur_line_only_props = false;
    }

process_token_finish:
    if (s->pending_alias) {
      if (tok.type != GTEXT_YAML_TOKEN_SCALAR) {
        return GTEXT_YAML_E_BAD_TOKEN;
      }
      s->pending_alias = false;
      GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &tok);
      if (doc_rc != GTEXT_YAML_OK) return doc_rc;
      GTEXT_YAML_Status alias_rc = stream_emit_alias(s, &tok);
      if (alias_rc != GTEXT_YAML_OK) return alias_rc;
      continue;
    }

    GTEXT_YAML_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.offset = tok.offset;
    ev.line = tok.line;
    ev.col = tok.col;

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_START) {
      {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      if (s->document_started && !s->document_closed) {
        GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      GTEXT_YAML_Status rc = stream_emit_document_start(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END) {
      {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      /* A "..." with no document open closes nothing: l-document-suffix
         stands on its own in a stream (9.2), and "..." by itself is a
         stream with no documents in it. Opening one here so that it could
         be closed gave every such suffix a null document of its own - a
         bare "..." parsed as one null, and one between two documents put a
         third between them. */
      if (!s->document_started || s->document_closed) {
        continue;
      }
      GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type != GTEXT_YAML_TOKEN_DIRECTIVE && tok.type != GTEXT_YAML_TOKEN_COMMENT) {
      GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &tok);
      if (doc_rc != GTEXT_YAML_OK) return doc_rc;
    }

    if (tok.type == GTEXT_YAML_TOKEN_COMMENT) {
      if (s->opts.retain_comments) {
        ev.type = GTEXT_YAML_EVENT_COMMENT;
        ev.data.comment.ptr = tok.u.comment.ptr;
        ev.data.comment.len = tok.u.comment.len;
        ev.data.comment.inline_comment = tok.u.comment.inline_comment;
        if (s->cb) {
          GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
          if (rc != GTEXT_YAML_OK) return rc;
        } else {
        }
      } else {
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
      /* These end the node position rather than filling it, so properties
         still waiting belong to the empty node - but only where they were
         inside the position this indicator closes. A tag on a line of its
         own introduces the collection that follows it:

             !!seq        the tag belongs to the sequence
             - a

             - &a         the anchor belongs to the first entry, which is
             - b          empty, and the second "-" is what proves it

         The two are told apart by where their lines begin. A property that
         opened its own line introduces whatever follows, however that is
         indented. One written part way along a line belongs to the position
         it stands in, and a later line that begins no further right has
         left that position:

             x: !custom      the "-" line begins further right, so the tag
               - 1           is the sequence's

             a: !!str        the "b" line begins at the same column, so a's
             b: 1            value was never written and the tag is its */
      if ((tok.u.c == '-' || tok.u.c == ':' || tok.u.c == '?'
           || tok.u.c == ',' || tok.u.c == ']' || tok.u.c == '}')
          && (s->pending_anchor || s->pending_tag)
          && stream_props_left_behind(s, &tok)) {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      if (tok.u.c == '-') stream_props_claimed_by_sequence(s);
      ev.type = GTEXT_YAML_EVENT_INDICATOR;
      ev.data.indicator = tok.u.c;

      if (tok.u.c == '[' || tok.u.c == '{') {
        s->current_depth++;
        if (s->opts.max_depth > 0 && s->current_depth > s->opts.max_depth) {
          return GTEXT_YAML_E_DEPTH;
        }

        GTEXT_YAML_Event start_ev;
        memset(&start_ev, 0, sizeof(start_ev));
        start_ev.type = (tok.u.c == '[')
          ? GTEXT_YAML_EVENT_SEQUENCE_START
          : GTEXT_YAML_EVENT_MAPPING_START;
        start_ev.anchor = s->pending_anchor;
        start_ev.anchor_line = s->pending_anchor ? s->pending_anchor_line : 0;
        start_ev.tag = s->pending_tag;
        start_ev.prop_line = s->pending_prop_min_line;
        start_ev.prop_col = s->pending_prop_min_col;
        start_ev.offset = tok.offset;
        start_ev.line = tok.line;
        start_ev.col = tok.col;

        if (s->cb) {
          GTEXT_YAML_Status rc = s->cb(s, &start_ev, s->user);
          if (rc != GTEXT_YAML_OK) return rc;
        }

        if (s->pending_anchor) {
          free(s->pending_anchor);
          s->pending_anchor = NULL;
          s->pending_anchor_line = 0;
        }
        if (s->pending_tag) {
          free(s->pending_tag);
          s->pending_tag = NULL;
          s->pending_tag_line = 0;
        }
        s->pending_prop_min_col = -1;
        s->pending_prop_min_line = 0;
        continue;
      } else if (tok.u.c == ']' || tok.u.c == '}') {
        if (s->current_depth > 0) s->current_depth--;

        GTEXT_YAML_Event end_ev;
        memset(&end_ev, 0, sizeof(end_ev));
        end_ev.type = (tok.u.c == ']')
          ? GTEXT_YAML_EVENT_SEQUENCE_END
          : GTEXT_YAML_EVENT_MAPPING_END;
        end_ev.offset = tok.offset;
        end_ev.line = tok.line;
        end_ev.col = tok.col;

        if (s->cb) {
          GTEXT_YAML_Status rc = s->cb(s, &end_ev, s->user);
          if (rc != GTEXT_YAML_OK) return rc;
        }
        continue;
      }
      
      /* Handle anchor definition */
      if (tok.u.c == '&') {
        GTEXT_YAML_Token name_tok;
        GTEXT_YAML_Error name_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
        if (nst != GTEXT_YAML_OK) return nst;
        if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
        
        size_t namelen = name_tok.u.scalar.len;
        char buf[256];
        if (namelen >= sizeof(buf)) namelen = sizeof(buf)-1;
        memcpy(buf, name_tok.u.scalar.ptr, namelen);
        buf[namelen] = '\0';
        
        if (s->pending_anchor) free(s->pending_anchor);
        s->pending_anchor = strdup(buf);
        s->pending_anchor_line = tok.line;
        s->pending_prop_offset = tok.offset;
        s->pending_prop_line = tok.line;
        s->pending_prop_col = tok.col;
        if (s->pending_prop_min_col < 0 || tok.col < s->pending_prop_min_col) {
          s->pending_prop_min_col = tok.col;
          s->pending_prop_min_line = tok.line;
        }
        s->pending_prop_line_start = s->cur_line_start;
        s->pending_prop_opens_line = s->cur_line_only_props;
        s->pending_prop_line_dash = s->cur_line_opens_with_dash;
        
        continue;
      }

      if (tok.u.c == '!') {
        GTEXT_YAML_Token tag_tok;
        GTEXT_YAML_Error tag_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &tag_tok, &tag_err);
        if (nst != GTEXT_YAML_OK) return nst;

        char buf[256];
        size_t tag_len = 0;

        if (stream_tag_is_non_specific(&tok, &tag_tok)) {
          /* The "!" was the whole property. Record it and go round again
             with the token just read, which is the node it applies to. */
          if (s->pending_tag) free(s->pending_tag);
          s->pending_tag = strdup("!");
          s->pending_tag_line = tok.line;
          tok = tag_tok;
          goto process_token_finish;
        }

        if (tag_tok.type == GTEXT_YAML_TOKEN_INDICATOR && tag_tok.u.c == '!') {
          GTEXT_YAML_Token name_tok;
          GTEXT_YAML_Error name_err;
          nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
          if (nst != GTEXT_YAML_OK) return nst;
          if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) {
            return GTEXT_YAML_E_BAD_TOKEN;
          }
          tag_len = name_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 3) tag_len = sizeof(buf) - 3;
          buf[0] = '!';
          buf[1] = '!';
          memcpy(buf + 2, name_tok.u.scalar.ptr, tag_len);
          tag_len += 2;
          buf[tag_len] = '\0';
        } else if (tag_tok.type == GTEXT_YAML_TOKEN_SCALAR
            && tag_tok.u.scalar.len >= 2
            && tag_tok.u.scalar.ptr[0] == '<'
            && tag_tok.u.scalar.ptr[tag_tok.u.scalar.len - 1] == '>') {
          /* A verbatim tag: "!<X>" is the tag X exactly as written, with no
             handle to expand (5.3). Keep the URI and drop the brackets. */
          tag_len = tag_tok.u.scalar.len - 2;
          if (tag_len > sizeof(buf) - 1) tag_len = sizeof(buf) - 1;
          memcpy(buf, tag_tok.u.scalar.ptr + 1, tag_len);
          buf[tag_len] = '\0';
        } else if (tag_tok.type == GTEXT_YAML_TOKEN_SCALAR) {
          tag_len = tag_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 2) tag_len = sizeof(buf) - 2;
          buf[0] = '!';
          memcpy(buf + 1, tag_tok.u.scalar.ptr, tag_len);
          tag_len += 1;
          buf[tag_len] = '\0';
        } else {
          return GTEXT_YAML_E_BAD_TOKEN;
        }

        if (s->pending_tag) free(s->pending_tag);
        s->pending_tag = strdup(buf);
        s->pending_tag_line = tok.line;
        s->pending_prop_offset = tok.offset;
        s->pending_prop_line = tok.line;
        s->pending_prop_col = tok.col;
        if (s->pending_prop_min_col < 0 || tok.col < s->pending_prop_min_col) {
          s->pending_prop_min_col = tok.col;
          s->pending_prop_min_line = tok.line;
        }
        s->pending_prop_line_start = s->cur_line_start;
        s->pending_prop_opens_line = s->cur_line_only_props;
        s->pending_prop_line_dash = s->cur_line_opens_with_dash;
        continue;
      }
      
      /* Handle alias reference */
      if (tok.u.c == '*') {
        GTEXT_YAML_Token next_tok;
        GTEXT_YAML_Error next_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &next_tok, &next_err);
        if (nst == GTEXT_YAML_E_INCOMPLETE) {
          s->pending_alias = true;
          return GTEXT_YAML_OK;
        }
        if (nst != GTEXT_YAML_OK) return nst;
        GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &next_tok);
        if (doc_rc != GTEXT_YAML_OK) return doc_rc;
        GTEXT_YAML_Status alias_rc = stream_emit_alias(s, &next_tok);
        if (alias_rc != GTEXT_YAML_OK) return alias_rc;
        continue;
      }
      
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_SCALAR) {
      if (stream_props_left_behind(s, &tok)) {
        GTEXT_YAML_Status flush = stream_flush_empty_node(s, &tok);
        if (flush != GTEXT_YAML_OK) return flush;
      }
      ev.type = GTEXT_YAML_EVENT_SCALAR;
      ev.data.scalar.ptr = tok.u.scalar.ptr;
      ev.data.scalar.len = tok.u.scalar.len;
      ev.scalar_style = tok.scalar_style;
      ev.anchor = s->pending_anchor;  /* Attach pending anchor */
      ev.anchor_line = s->pending_anchor ? s->pending_anchor_line : 0;
      ev.tag = s->pending_tag;
      ev.prop_line = s->pending_prop_min_line;
      ev.prop_col = s->pending_prop_min_col;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) {
          return rc;
        }
      }
      /* Clear pending anchor after use */
      if (s->pending_anchor) {
        free(s->pending_anchor);
        s->pending_anchor = NULL;
        s->pending_anchor_line = 0;
      }
      if (s->pending_tag) {
        free(s->pending_tag);
        s->pending_tag = NULL;
        s->pending_tag_line = 0;
      }
      s->pending_prop_min_col = -1;
      s->pending_prop_min_line = 0;
      continue;
    }
  }

  if (s->document_started && !s->document_closed) {
    GTEXT_YAML_Status rc = stream_emit_document_end(s, NULL);
    if (rc != GTEXT_YAML_OK) return rc;
  }

  return GTEXT_YAML_OK;
}
