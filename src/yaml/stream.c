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
  bool sync_mode; /* If true, call scanner_finish after each feed */
};

/* Helper: parse a node (simple flow sequence/map or scalar) from the scanner,
   emit equivalent events via the stream callback so the overall event
   sequence is preserved, and collect a conservative base_size and a list of
   referenced anchor names found inside the node. On success returns
   GTEXT_YAML_OK and fills out_base/out_refs/out_ref_count (refs allocated
   with malloc; caller is responsible for freeing). If more input is required
   to finish the node, returns GTEXT_YAML_E_INCOMPLETE so the caller can feed
   more data. */
static GTEXT_YAML_Status parse_node_and_collect(GTEXT_YAML_Stream *s, GTEXT_YAML_Token first_tok, size_t *out_base, char ***out_refs, size_t *out_ref_count) {
  if (!s || !out_base || !out_refs || !out_ref_count) return GTEXT_YAML_E_INVALID;
  size_t base = 0;
  char **refs = NULL; size_t refs_cap = 0; size_t refs_n = 0;

  /* Helper to append a ref name (makes a heap copy) */

  /* Emit a token via callback */
  /* Note: first_tok is already available; for subsequent tokens we'll use scanner_next */
  GTEXT_YAML_Token tok = first_tok;
  /* If the first token is an alias indicator, consume the name and add to refs */
  if (tok.type == GTEXT_YAML_TOKEN_INDICATOR && tok.u.c == '*') {
    GTEXT_YAML_Token name_tok; GTEXT_YAML_Error name_err;
    GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
    if (nst == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_E_INCOMPLETE;
    if (nst != GTEXT_YAML_OK) return nst;
    if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
    size_t namelen = name_tok.u.scalar.len;
    char *ncopy = (char *)malloc(namelen + 1);
    if (!ncopy) { free((void *)name_tok.u.scalar.ptr); return GTEXT_YAML_E_OOM; }
    memcpy(ncopy, name_tok.u.scalar.ptr, namelen); ncopy[namelen] = '\0';
    if (refs_n + 1 > refs_cap) {
      size_t nc = refs_cap ? refs_cap * 2 : 4;
      char **tmp = (char **)realloc(refs, nc * sizeof(char *));
      if (!tmp) { free(ncopy); free((void *)name_tok.u.scalar.ptr); return GTEXT_YAML_E_OOM; }
      refs = tmp; refs_cap = nc;
    }
    refs[refs_n++] = ncopy;
    /* Emit a scalar event for the alias name so the event stream matches */
    GTEXT_YAML_Event ev2; memset(&ev2,0,sizeof(ev2)); ev2.type = GTEXT_YAML_EVENT_SCALAR; ev2.data.scalar.ptr = name_tok.u.scalar.ptr; ev2.data.scalar.len = name_tok.u.scalar.len; ev2.offset = name_tok.offset; ev2.line = name_tok.line; ev2.col = name_tok.col;
    if (s->cb) {
      GTEXT_YAML_Status rc = s->cb(s, &ev2, s->user);
      if (rc != GTEXT_YAML_OK) {
        free((void *)name_tok.u.scalar.ptr);
        for (size_t i=0;i<refs_n;i++) free(refs[i]);
        free(refs);
        return rc;
      }
    }
    /* Free the scalar memory AFTER using it in the event */
    free((void *)name_tok.u.scalar.ptr);
    /* set base as one and return */
    *out_base = 1;
    *out_refs = refs; *out_ref_count = refs_n;
    return GTEXT_YAML_OK;
  }
  for (;;) {
    GTEXT_YAML_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.offset = tok.offset; ev.line = tok.line; ev.col = tok.col;

    if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
      ev.type = GTEXT_YAML_EVENT_INDICATOR;
      ev.data.indicator = tok.u.c;
      /* emit indicator */
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      /* handle structure starts */
  if (tok.u.c == '[' || tok.u.c == '{') {
        /* enter flow container parsing */
        int depth = 1;
        for (;;) {
          GTEXT_YAML_Token nt;
          GTEXT_YAML_Error err;
          GTEXT_YAML_Status st = gtext_yaml_scanner_next(s->scanner, &nt, &err);
          if (st == GTEXT_YAML_E_INCOMPLETE) {
            return GTEXT_YAML_E_INCOMPLETE; /* need more data */
          }
          if (st != GTEXT_YAML_OK) return st;
          /* If we hit end, propagate */
          if (nt.type == GTEXT_YAML_TOKEN_EOF) return GTEXT_YAML_E_INVALID;

          /* If indicator: check nesting */
          if (nt.type == GTEXT_YAML_TOKEN_INDICATOR) {
            char c = nt.u.c;
            ev.type = GTEXT_YAML_EVENT_INDICATOR;
            ev.data.indicator = c;
            ev.offset = nt.offset; ev.line = nt.line; ev.col = nt.col;
            if (s->cb) {
              GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
              if (rc != GTEXT_YAML_OK) return rc;
            }
            if (c == '[' || c == '{') { depth++; continue; }
            if (c == ']' || c == '}') { depth--; if (depth == 0) break; continue; }
            /* commas and colons are just emitted */
            if (c == '*') {
              /* alias inside node: next token should be scalar anchor name */
              GTEXT_YAML_Token name_tok; GTEXT_YAML_Error name_err;
              GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &name_tok, &name_err);
              if (nst == GTEXT_YAML_E_INCOMPLETE) return GTEXT_YAML_E_INCOMPLETE;
              if (nst != GTEXT_YAML_OK) return nst;
              if (name_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
              /* copy name into refs list */
              size_t namelen = name_tok.u.scalar.len;
              char *ncopy = (char *)malloc(namelen + 1);
              if (!ncopy) { free((void *)name_tok.u.scalar.ptr); return GTEXT_YAML_E_OOM; }
              memcpy(ncopy, name_tok.u.scalar.ptr, namelen); ncopy[namelen] = '\0';
              if (refs_n + 1 > refs_cap) {
                size_t nc = refs_cap ? refs_cap * 2 : 4;
                char **tmp = (char **)realloc(refs, nc * sizeof(char *));
                if (!tmp) { free(ncopy); free((void *)name_tok.u.scalar.ptr); return GTEXT_YAML_E_OOM; }
                refs = tmp; refs_cap = nc;
              }
              refs[refs_n++] = ncopy;
              /* Also emit the scalar name as event */
              ev.type = GTEXT_YAML_EVENT_SCALAR; ev.data.scalar.ptr = name_tok.u.scalar.ptr; ev.data.scalar.len = name_tok.u.scalar.len; ev.offset = name_tok.offset; ev.line = name_tok.line; ev.col = name_tok.col;
              if (s->cb) {
                GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
                if (rc != GTEXT_YAML_OK) {
                  free((void *)name_tok.u.scalar.ptr);
                  for (size_t i=0;i<refs_n;i++) free(refs[i]);
                  free(refs);
                  return rc;
                }
              }
              /* Free the scalar memory AFTER using it in the event */
              free((void *)name_tok.u.scalar.ptr);
              continue;
            }
            continue;
          }

          /* scalar inside flow container */
          if (nt.type == GTEXT_YAML_TOKEN_SCALAR) {
            /* emit scalar, count toward base */
            ev.type = GTEXT_YAML_EVENT_SCALAR;
            ev.data.scalar.ptr = nt.u.scalar.ptr;
            ev.data.scalar.len = nt.u.scalar.len;
            ev.offset = nt.offset; ev.line = nt.line; ev.col = nt.col;
            if (s->cb) {
              GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
              if (rc != GTEXT_YAML_OK) { free((void *)nt.u.scalar.ptr); for (size_t i=0;i<refs_n;i++) free(refs[i]); free(refs); return rc; }
            }
            free((void *)nt.u.scalar.ptr);
            base++;
            continue;
          }
        }
        /* finished container; return to caller */
        break;
      }
      /* other indicators just emitted */
      break;
    }

    if (tok.type == GTEXT_YAML_TOKEN_SCALAR) {
      ev.type = GTEXT_YAML_EVENT_SCALAR;
      ev.data.scalar.ptr = tok.u.scalar.ptr;
      ev.data.scalar.len = tok.u.scalar.len;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) { free((void *)tok.u.scalar.ptr); for (size_t i=0;i<refs_n;i++) free(refs[i]); free(refs); return rc; }
      }
      free((void *)tok.u.scalar.ptr);
      base++;
      break;
    }

    /* Shouldn't reach here; break defensively */
    break;
  }

  *out_base = base ? base : 1;
  *out_refs = refs;
  *out_ref_count = refs_n;
  return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Stream * gtext_yaml_stream_new(const GTEXT_YAML_Parse_Options * opts, GTEXT_YAML_Event_Callback cb, void * user)
{
  GTEXT_YAML_Stream *s = (GTEXT_YAML_Stream *)malloc(sizeof(*s));
  if (!s) return NULL;
  memset(s, 0, sizeof(*s));
  s->cb = cb;
  s->user = user;
  if (opts) s->opts = *opts;
  s->total_bytes_consumed = 0;
  s->current_depth = 0;
  s->alias_expansion_count = 0;
  s->scanner = gtext_yaml_scanner_new();
  s->resolver = gtext_yaml_resolver_new(&s->opts);
  if (!s->scanner) { free(s); return NULL; }
  if (!s->resolver) { gtext_yaml_scanner_free(s->scanner); free(s); return NULL; }
  return s;
}

GTEXT_API void gtext_yaml_stream_free(GTEXT_YAML_Stream * s)
{
  if (!s) return;
  if (s->scanner) gtext_yaml_scanner_free(s->scanner);
  if (s->resolver) gtext_yaml_resolver_free(s->resolver);
  if (s->pending_anchor) free(s->pending_anchor);
  free(s);
}

/* Internal: Set synchronous mode (for use by gtext_yaml_parse) */
GTEXT_INTERNAL_API void gtext_yaml_stream_set_sync_mode(GTEXT_YAML_Stream *s, bool sync) {
  if (s) s->sync_mode = sync;
}

GTEXT_API GTEXT_API GTEXT_YAML_Status gtext_yaml_stream_feed(GTEXT_YAML_Stream * s, const char * data, size_t len)
{
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

    GTEXT_YAML_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.offset = tok.offset;
    ev.line = tok.line;
    ev.col = tok.col;

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_START) {
      ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_DOCUMENT_END) {
      ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
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
        start_ev.type = (tok.u.c == '[') ? GTEXT_YAML_EVENT_SEQUENCE_START : GTEXT_YAML_EVENT_MAPPING_START;
        start_ev.anchor = s->pending_anchor;  /* Attach pending anchor if any */
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
        continue;
      } else if (tok.u.c == ']' || tok.u.c == '}') {
        if (s->current_depth > 0) s->current_depth--;
        /* Emit collection END event instead of INDICATOR */
        GTEXT_YAML_Event end_ev;
        memset(&end_ev, 0, sizeof(end_ev));
        end_ev.type = (tok.u.c == ']') ? GTEXT_YAML_EVENT_SEQUENCE_END : GTEXT_YAML_EVENT_MAPPING_END;
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
      } else if (tok.u.c == '*') {
        /* In sync mode, handle alias here; otherwise skip for finish() */
        if (!s->sync_mode) {
          /* Async mode: can't read ahead, emit as indicator */
          ev.type = GTEXT_YAML_EVENT_INDICATOR;
          ev.data.indicator = '*';
          if (s->cb) {
            GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
            if (rc != GTEXT_YAML_OK) return rc;
          }
          continue;
        }
        
        /* Sync mode: process alias immediately */
        GTEXT_YAML_Token next_tok;
        GTEXT_YAML_Error next_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &next_tok, &next_err);
        if (nst != GTEXT_YAML_OK) return nst;
        if (next_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
        
        char *name = (char *)next_tok.u.scalar.ptr;
        size_t namelen = next_tok.u.scalar.len;
        char buf[256];
        if (namelen >= sizeof(buf)) namelen = sizeof(buf)-1;
        memcpy(buf, name, namelen);
        buf[namelen] = '\0';
        
        /* Emit ALIAS event */
        GTEXT_YAML_Event alias_ev;
        memset(&alias_ev, 0, sizeof(alias_ev));
        alias_ev.type = GTEXT_YAML_EVENT_ALIAS;
        alias_ev.data.alias_name = buf;
        alias_ev.offset = next_tok.offset;
        alias_ev.line = next_tok.line;
        alias_ev.col = next_tok.col;
        
        if (s->cb) {
          GTEXT_YAML_Status cb_rc = s->cb(s, &alias_ev, s->user);
          free(name);
          if (cb_rc != GTEXT_YAML_OK) return cb_rc;
        } else {
          free(name);
        }
        continue;
      }
      /* Emit remaining indicators (commas, colons, etc.) */
      if (s->cb) {
        GTEXT_YAML_Status rc = s->cb(s, &ev, s->user);
        if (rc != GTEXT_YAML_OK) return rc;
      }
      continue;
    }

    if (tok.type == GTEXT_YAML_TOKEN_SCALAR) {
  /* scalar event */
  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = tok.u.scalar.ptr;
  ev.data.scalar.len = tok.u.scalar.len;
  /* Attach pending anchor if any */
  ev.anchor = s->pending_anchor;
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

    GTEXT_YAML_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.offset = tok.offset;
    ev.line = tok.line;
    ev.col = tok.col;

    if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
      ev.type = GTEXT_YAML_EVENT_INDICATOR;
      ev.data.indicator = tok.u.c;
      
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
      
      /* Handle alias reference */
      if (tok.u.c == '*') {
        GTEXT_YAML_Token next_tok;
        GTEXT_YAML_Error next_err;
        GTEXT_YAML_Status nst = gtext_yaml_scanner_next(s->scanner, &next_tok, &next_err);
        if (nst != GTEXT_YAML_OK) return nst;
        if (next_tok.type != GTEXT_YAML_TOKEN_SCALAR) return GTEXT_YAML_E_BAD_TOKEN;
        
        char *name = (char *)next_tok.u.scalar.ptr; 
        size_t namelen = next_tok.u.scalar.len;
        char buf[256];
        if (namelen >= sizeof(buf)) namelen = sizeof(buf)-1;
        memcpy(buf, name, namelen); 
        buf[namelen] = '\0';
        
        /* Emit ALIAS event */
        GTEXT_YAML_Event alias_ev;
        memset(&alias_ev, 0, sizeof(alias_ev));
        alias_ev.type = GTEXT_YAML_EVENT_ALIAS;
        alias_ev.data.alias_name = buf;
        alias_ev.offset = next_tok.offset;
        alias_ev.line = next_tok.line;
        alias_ev.col = next_tok.col;
        
        if (s->cb) {
          GTEXT_YAML_Status cb_rc = s->cb(s, &alias_ev, s->user);
          free(name);
          if (cb_rc != GTEXT_YAML_OK) return cb_rc;
        } else {
          free(name);
        }
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
      free((void *)tok.u.scalar.ptr);
      continue;
    }
  }

  return GTEXT_YAML_OK;
}
