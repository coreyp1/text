/**
 * @file resolver.c
 * @brief Minimal anchor/alias accounting for YAML streaming parser.
 *
 * This component is intentionally small: it tracks anchors defined with
 * '&name' followed by a simple sequence or mapping literal and records the
 * approximate size (number of child nodes). When an alias '*name' is
 * encountered the resolver adds that size to the expansion counter and
 * enforces the parse option limit.
 *
 * This is conservative and best-effort; a full resolver/DOM builder will
 * eventually replace this.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "yaml_internal.h"

typedef struct AnchorEntry {
  char *name;
  size_t size;
  struct AnchorEntry *next;
} AnchorEntry;

typedef struct AnchorRefs {
  char *name;
  size_t base_size;
  char **refs; /* array of referenced anchor names */
  size_t ref_count;
  struct AnchorRefs *next;
} AnchorRefs;

/* Stack entry used by DFS; declared at file scope so prototypes can use it. */
struct StackEntry { AnchorRefs *node; struct StackEntry *next; };

typedef struct {
  AnchorEntry *anchors;
  size_t total_expanded;
  GTEXT_YAML_Parse_Options opts;
  AnchorRefs *anchor_defs;
} ResolverState;

static AnchorEntry *anchor_find(AnchorEntry *head, const char *name) {
  for (AnchorEntry *e = head; e; e = e->next) {
    if (strcmp(e->name, name) == 0) return e;
  }
  return NULL;
}

static int anchor_add(AnchorEntry **head, const char *name, size_t size) {
  AnchorEntry *e = (AnchorEntry *)malloc(sizeof(*e));
  if (!e) return 0;
  e->name = (char *)malloc(strlen(name) + 1);
  if (!e->name) { free(e); return 0; }
  strcpy(e->name, name);
  e->size = size;
  e->next = *head;
  *head = e;
  return 1;
}

static void anchor_free_all(AnchorEntry *head) {
  while (head) {
    AnchorEntry *n = head->next;
    free(head->name);
    free(head);
    head = n;
  }
}

static void anchor_refs_free_all(AnchorRefs *head) {
  while (head) {
    AnchorRefs *n = head->next;
    free(head->name);
    for (size_t i = 0; i < head->ref_count; ++i) free(head->refs[i]);
    free(head->refs);
    free(head);
    head = n;
  }
}

GTEXT_INTERNAL_API ResolverState *gtext_yaml_resolver_new(const GTEXT_YAML_Parse_Options *opts) {
  ResolverState *r = (ResolverState *)malloc(sizeof(*r));
  if (!r) return NULL;
  r->anchors = NULL;
  r->total_expanded = 0;
  r->anchor_defs = NULL;
  if (opts) r->opts = *opts; else r->opts.max_alias_expansion = 0;
  return r;
}

GTEXT_INTERNAL_API void gtext_yaml_resolver_free(ResolverState *r) {
  if (!r) return;
  anchor_free_all(r->anchors);
  anchor_refs_free_all(r->anchor_defs);
  free(r);
}

/* Inform resolver about an anchor definition and its approximate size */
GTEXT_INTERNAL_API int gtext_yaml_resolver_register_anchor(ResolverState *r, const char *name, size_t size) {
  if (!r || !name) return 0;
  return anchor_add(&r->anchors, name, size);
}

GTEXT_INTERNAL_API int gtext_yaml_resolver_register_anchor_with_refs(ResolverState *r, const char *name, size_t base_size, const char **refs, size_t ref_count) {
  if (!r || !name) return 0;
  AnchorRefs *a = (AnchorRefs *)malloc(sizeof(*a));
  if (!a) return 0;
  a->name = (char *)malloc(strlen(name) + 1);
  if (!a->name) { free(a); return 0; }
  strcpy(a->name, name);
  a->base_size = base_size;
  a->ref_count = ref_count;
  a->refs = NULL;
  if (ref_count > 0) {
    a->refs = (char **)malloc(sizeof(char *) * ref_count);
    if (!a->refs) { free(a->name); free(a); return 0; }
    for (size_t i = 0; i < ref_count; ++i) {
      a->refs[i] = (char *)malloc(strlen(refs[i]) + 1);
      if (!a->refs[i]) { /* cleanup */
        for (size_t j = 0; j < i; ++j) free(a->refs[j]);
        free(a->refs); free(a->name); free(a); return 0;
      }
      strcpy(a->refs[i], refs[i]);
    }
  }
  a->next = r->anchor_defs;
  r->anchor_defs = a;
  return 1;
}

/* Helper to find AnchorRefs by name */
static AnchorRefs *anchor_refs_find(AnchorRefs *head, const char *name) {
  for (AnchorRefs *a = head; a; a = a->next) {
    if (strcmp(a->name, name) == 0) return a;
  }
  return NULL;
}

/* forward declare dfs_impl */
static GTEXT_YAML_Status dfs_impl(AnchorRefs *a, AnchorRefs *defs, struct StackEntry **stack_ptr, size_t max_allowed, size_t *out_acc);

/* Compute expansion size with DFS and detect cycles. Returns GTEXT_YAML_OK and sets *out_size on success, or GTEXT_YAML_E_LIMIT if the size exceeds max_allowed. */
GTEXT_INTERNAL_API GTEXT_YAML_Status gtext_yaml_resolver_compute_expansion(ResolverState *r, const char *name, size_t max_allowed, size_t *out_size) {
  if (!r || !name || !out_size) return GTEXT_YAML_E_INVALID;
  /* Simple DFS with recursion guard via visited list */
  AnchorRefs *start = anchor_refs_find(r->anchor_defs, name);
  if (!start) {
    /* unknown anchor: treat as size 1 */
    *out_size = 1;
    return GTEXT_YAML_OK;
  }

  /* recursion stack as linked list */
  struct StackEntry *stack = NULL;
    size_t result = 0;
    GTEXT_YAML_Status st = dfs_impl(start, r->anchor_defs, &stack, max_allowed, &result);
    if (st == GTEXT_YAML_OK) *out_size = result;
    return st;
}

  /* File-scope DFS implementation to compute expansion sizes (avoids nested functions). */
  static GTEXT_YAML_Status dfs_impl(AnchorRefs *a, AnchorRefs *defs, struct StackEntry **stack_ptr, size_t max_allowed, size_t *out_acc) {
    /* detect cycle */
    for (struct StackEntry *p = *stack_ptr; p; p = p->next) {
      if (p->node == a) return GTEXT_YAML_E_INVALID;
    }
    /* push */
    struct StackEntry se = { a, *stack_ptr };
    *stack_ptr = &se;

    size_t total = a->base_size;
    for (size_t i = 0; i < a->ref_count; ++i) {
      AnchorRefs *sub = anchor_refs_find(defs, a->refs[i]);
      size_t subsize = 1;
      if (sub) {
        GTEXT_YAML_Status rc = dfs_impl(sub, defs, stack_ptr, max_allowed, &subsize);
        if (rc != GTEXT_YAML_OK) { *stack_ptr = se.next; return rc; }
      }
      /* overflow check */
      if (total > (size_t)-1 - subsize) total = (size_t)-1; else total += subsize;
      if (max_allowed > 0 && total > max_allowed) { *stack_ptr = se.next; return GTEXT_YAML_E_LIMIT; }
    }

    *out_acc = total;
    /* pop */
    *stack_ptr = se.next;
    return GTEXT_YAML_OK;
  }

/* When an alias is seen, increment expanded count and enforce limit. */
GTEXT_INTERNAL_API GTEXT_YAML_Status gtext_yaml_resolver_apply_alias(ResolverState *r, const char *name) {
  if (!r || !name) return GTEXT_YAML_E_INVALID;
  AnchorEntry *e = anchor_find(r->anchors, name);
  size_t add = 1; /* default conservative */
  if (e) add = e->size ? e->size : 1;
  /* Detect overflow (portable): if sum wraps, clamp to max. */
  size_t new_total = r->total_expanded + add;
  if (new_total < r->total_expanded) {
    r->total_expanded = (size_t)-1;
  } else {
    r->total_expanded = new_total;
  }
  if (r->opts.max_alias_expansion > 0 && r->total_expanded > r->opts.max_alias_expansion) {
    return GTEXT_YAML_E_LIMIT;
  }
  return GTEXT_YAML_OK;
}
