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

static GTEXT_YAML_Status stream_emit_alias(GTEXT_YAML_Stream *s, GTEXT_YAML_Token *tok) {
  if (!s || !tok) return GTEXT_YAML_E_INVALID;
  if (tok->type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;

  char *name = (char *)tok->u.scalar.ptr;
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
    free(name);
    return alias_limit;
  }

  if (s->cb) {
    GTEXT_YAML_Status cb_rc = s->cb(s, &alias_ev, s->user);
    free(name);
    if (cb_rc != GTEXT_YAML_OK) return cb_rc;
  } else {
    free(name);
  }

  if (s->pending_tag) {
    free(s->pending_tag);
    s->pending_tag = NULL;
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
    if (tok.type == GTEXT_YAML_TOKEN_EOF) break;

    if (s->pending_alias) {
      if (tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
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
      if (s->document_started && !s->document_closed) {
        GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      GTEXT_YAML_Status rc = stream_emit_document_start(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END) {
      if (!s->document_started || s->document_closed) {
        GTEXT_YAML_Status rc = stream_emit_document_start(s, &tok);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type != GTEXT_YAML_TOKEN_DIRECTIVE) {
      GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &tok);
      if (doc_rc != GTEXT_YAML_OK) return doc_rc;
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
      free((void *)tok.u.scalar.ptr);
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
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
        start_ev.tag = s->pending_tag;
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
        }
        if (s->pending_tag) {
          free(s->pending_tag);
          s->pending_tag = NULL;
        }
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
        
        free((void *)name_tok.u.scalar.ptr);
        continue;
      } else if (tok.u.c == '!') {
        GTEXT_YAML_Token tag_tok;
        GTEXT_YAML_Error tag_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &tag_tok, &tag_err);
        if (nst == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_OK;
        if (nst != GTEXT_YAML_OK) return nst;

        char buf[256];
        size_t tag_len = 0;

        if (tag_tok.type == GTEXT_YAML_TOKEN_INDICATOR && tag_tok.u.c == '!') {
          GTEXT_YAML_Token name_tok;
          GTEXT_YAML_Error name_err;
          nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
          if (nst != GTEXT_YAML_OK) return nst;
          if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
          tag_len = name_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 3) tag_len = sizeof(buf) - 3;
          buf[0] = '!';
          buf[1] = '!';
          memcpy(buf + 2, name_tok.u.scalar.ptr, tag_len);
          tag_len += 2;
          buf[tag_len] = '\0';
          free((void *)name_tok.u.scalar.ptr);
        } else if (tag_tok.type == GTEXT_YAML_TOKEN_SCALAR) {
          tag_len = tag_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 2) tag_len = sizeof(buf) - 2;
          buf[0] = '!';
          memcpy(buf + 1, tag_tok.u.scalar.ptr, tag_len);
          tag_len += 1;
          buf[tag_len] = '\0';
          free((void *)tag_tok.u.scalar.ptr);
        } else {
          return GTEXT_YAML_E_BAD_TOKEN;
        }

        if (s->pending_tag) free(s->pending_tag);
        s->pending_tag = strdup(buf);
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
      free((void *)tok.u.scalar.ptr);
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_SCALAR) {
      /* scalar event */
      ev.type = GTEXT_YAML_EVENT_SCALAR;
      ev.data.scalar.ptr = tok.u.scalar.ptr;
      ev.data.scalar.len = tok.u.scalar.len;
      /* Attach pending anchor if any */
      ev.anchor = s->pending_anchor;
      ev.tag = s->pending_tag;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) {
          free((void *)tok.u.scalar.ptr);
          return rc;
        }
      }
      /* Clear pending anchor after attaching */
      if (s->pending_anchor) {
        free(s->pending_anchor);
        s->pending_anchor = NULL;
      }
      if (s->pending_tag) {
        free(s->pending_tag);
        s->pending_tag = NULL;
      }
      free((void *)tok.u.scalar.ptr);
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
    if (tok.type == GTEXT_YAML_TOKEN_EOF) break;

    if (s->pending_alias) {
      if (tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
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
      if (s->document_started && !s->document_closed) {
        GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      GTEXT_YAML_Status rc = stream_emit_document_start(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END) {
      if (!s->document_started || s->document_closed) {
        GTEXT_YAML_Status rc = stream_emit_document_start(s, &tok);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      GTEXT_YAML_Status rc = stream_emit_document_end(s, &tok);
      if (rc != GTEXT_YAML_OK) return rc;
      continue;
    }

    if (tok.type != GTEXT_YAML_TOKEN_DIRECTIVE) {
      GTEXT_YAML_Status doc_rc = stream_ensure_document_started(s, &tok);
      if (doc_rc != GTEXT_YAML_OK) return doc_rc;
    }

    if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
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
        start_ev.tag = s->pending_tag;
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
        }
        if (s->pending_tag) {
          free(s->pending_tag);
          s->pending_tag = NULL;
        }
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
        
        free((void *)name_tok.u.scalar.ptr);
        continue;
      }

      if (tok.u.c == '!') {
        GTEXT_YAML_Token tag_tok;
        GTEXT_YAML_Error tag_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &tag_tok, &tag_err);
        if (nst != GTEXT_YAML_OK) return nst;

        char buf[256];
        size_t tag_len = 0;

        if (tag_tok.type == GTEXT_YAML_TOKEN_INDICATOR && tag_tok.u.c == '!') {
          GTEXT_YAML_Token name_tok;
          GTEXT_YAML_Error name_err;
          nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
          if (nst != GTEXT_YAML_OK) return nst;
          if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
          tag_len = name_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 3) tag_len = sizeof(buf) - 3;
          buf[0] = '!';
          buf[1] = '!';
          memcpy(buf + 2, name_tok.u.scalar.ptr, tag_len);
          tag_len += 2;
          buf[tag_len] = '\0';
          free((void *)name_tok.u.scalar.ptr);
        } else if (tag_tok.type == GTEXT_YAML_TOKEN_SCALAR) {
          tag_len = tag_tok.u.scalar.len;
          if (tag_len > sizeof(buf) - 2) tag_len = sizeof(buf) - 2;
          buf[0] = '!';
          memcpy(buf + 1, tag_tok.u.scalar.ptr, tag_len);
          tag_len += 1;
          buf[tag_len] = '\0';
          free((void *)tag_tok.u.scalar.ptr);
        } else {
          return GTEXT_YAML_E_BAD_TOKEN;
        }

        if (s->pending_tag) free(s->pending_tag);
        s->pending_tag = strdup(buf);
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
      ev.type = GTEXT_YAML_EVENT_SCALAR;
      ev.data.scalar.ptr = tok.u.scalar.ptr;
      ev.data.scalar.len = tok.u.scalar.len;
      ev.anchor = s->pending_anchor;  /* Attach pending anchor */
      ev.tag = s->pending_tag;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) {
          free((void *)tok.u.scalar.ptr);
          return rc;
        }
      }
      /* Clear pending anchor after use */
      if (s->pending_anchor) {
        free(s->pending_anchor);
        s->pending_anchor = NULL;
      }
      if (s->pending_tag) {
        free(s->pending_tag);
        s->pending_tag = NULL;
      }
      free((void *)tok.u.scalar.ptr);
      continue;
    }
  }

  if (s->document_started && !s->document_closed) {
    GTEXT_YAML_Status rc = stream_emit_document_end(s, NULL);
    if (rc != GTEXT_YAML_OK) return rc;
  }

  return GTEXT_YAML_OK;
}
