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
 * @file yaml_context.c
 * @brief YAML context management
 *
 * Context owns the arena allocator and tracks document-level state.
 */

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"
#include <stdlib.h>

/* Create new context with arena.
 *
 * calloc() rather than malloc(): every field below is set by hand, so the
 * zeroing is redundant today and is the whole safety margin the moment a
 * field is added. That is not hypothetical here - this function used to set
 * a "resolver" field to NULL with a comment saying it would be created when
 * needed during parsing, and nothing ever created it, so that initialiser
 * was the only thing between the struct and a wild pointer. */
yaml_context *yaml_context_new(void) {
	yaml_context *ctx = (yaml_context *)calloc(1, sizeof(yaml_context));
	if (!ctx) return NULL;
	
	/* Create arena */
	ctx->arena = yaml_arena_new();
	if (!ctx->arena) {
		free(ctx);
		return NULL;
	}
	
	ctx->decoded_input = NULL;
	ctx->decoded_input_len = 0;
	ctx->node_count = 0;
	
	return ctx;
}

/* Free context and arena */
void yaml_context_free(yaml_context *ctx) {
	if (!ctx) return;
	
	/* Free arena (frees all nodes) */
	yaml_arena_free(ctx->arena);
	
	/* Note: decoded_input is NOT freed (the scanner owns it) */
	
	free(ctx);
}

/* Point at the scanner's decoded stream. See the field in yaml_internal.h. */
void yaml_context_set_decoded_input(yaml_context *ctx, const char *buf, size_t len) {
	if (!ctx) return;
	ctx->decoded_input = buf;
	ctx->decoded_input_len = len;
}

/* Allocate from context's arena */
void *yaml_context_alloc(yaml_context *ctx, size_t size, size_t align) {
	if (!ctx) return NULL;
	return yaml_arena_alloc(ctx->arena, size, align);
}
