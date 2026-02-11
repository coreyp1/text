/**
 * @file yaml_resolver.h
 * @brief Minimal resolver API for anchor/alias accounting.
 */

#ifndef GHOTI_IO_TEXT_YAML_RESOLVER_H
#define GHOTI_IO_TEXT_YAML_RESOLVER_H

#include <ghoti.io/text/yaml/yaml_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ResolverState ResolverState;

GTEXT_INTERNAL_API ResolverState *gtext_yaml_resolver_new(const GTEXT_YAML_Parse_Options *opts);
GTEXT_INTERNAL_API void gtext_yaml_resolver_free(ResolverState *r);
GTEXT_INTERNAL_API int gtext_yaml_resolver_register_anchor(ResolverState *r, const char *name, size_t size);
GTEXT_INTERNAL_API GTEXT_YAML_Status gtext_yaml_resolver_apply_alias(ResolverState *r, const char *name);

/* Register an anchor with a base (non-alias) node count and a list of
 * referenced anchor names that appear inside the anchor's definition. The
 * resolver will use this to compute expansion size precisely with DFS and
 * cycle detection. */
GTEXT_INTERNAL_API int gtext_yaml_resolver_register_anchor_with_refs(ResolverState *r, const char *name, size_t base_size, const char **refs, size_t ref_count);

/* Compute the expansion size for anchor `name`. If the expansion would
 * exceed max_allowed (>0) the function returns GTEXT_YAML_E_LIMIT. On
 * success, *out_size is set to the computed size. */
GTEXT_INTERNAL_API GTEXT_YAML_Status gtext_yaml_resolver_compute_expansion(ResolverState *r, const char *name, size_t max_allowed, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_YAML_RESOLVER_H */
