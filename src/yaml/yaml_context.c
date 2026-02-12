/**
 * @file yaml_context.c
 * @brief YAML context management
 *
 * Context owns the arena allocator and tracks document-level state.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "yaml_internal.h"
#include <ghoti.io/text/yaml/yaml_resolver.h>
#include <stdlib.h>

/* Create new context with arena */
yaml_context *yaml_context_new(void) {
	yaml_context *ctx = (yaml_context *)malloc(sizeof(yaml_context));
	if (!ctx) return NULL;
	
	/* Create arena */
	ctx->arena = yaml_arena_new();
	if (!ctx->arena) {
		free(ctx);
		return NULL;
	}
	
	ctx->input_buffer = NULL;
	ctx->input_buffer_len = 0;
	ctx->resolver = NULL;  /* Created when needed during parsing */
	ctx->node_count = 0;
	
	return ctx;
}

/* Free context and arena */
void yaml_context_free(yaml_context *ctx) {
	if (!ctx) return;
	
	/* Free resolver if created */
	if (ctx->resolver) {
		gtext_yaml_resolver_free(ctx->resolver);
	}
	
	/* Free arena (frees all nodes) */
	yaml_arena_free(ctx->arena);
	
	/* Note: input_buffer is NOT freed (caller-owned) */
	
	free(ctx);
}

/* Set input buffer reference (for future in-situ mode) */
void yaml_context_set_input_buffer(yaml_context *ctx, const char *buf, size_t len) {
	if (!ctx) return;
	ctx->input_buffer = buf;
	ctx->input_buffer_len = len;
}

/* Allocate from context's arena */
void *yaml_context_alloc(yaml_context *ctx, size_t size, size_t align) {
	if (!ctx) return NULL;
	return yaml_arena_alloc(ctx->arena, size, align);
}
