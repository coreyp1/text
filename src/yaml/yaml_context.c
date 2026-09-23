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
yaml_context *yaml_context_new(const GTEXT_Allocator *alloc) {
	yaml_context *ctx = (yaml_context *)gtext_allocator_calloc(alloc, 1, sizeof(yaml_context));
	if (!ctx) return NULL;

	/* Set before the arena, so the unwind below frees through it. */
	ctx->alloc = alloc;

	/* Create arena */
	ctx->arena = yaml_arena_new(alloc);
	if (!ctx->arena) {
		gtext_allocator_free(alloc, ctx);
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

	/* Read before yaml_arena_free() releases the arena, and before the free
	   below releases the structure this lives in. */
	const GTEXT_Allocator *alloc = ctx->alloc;

	/* Free arena (frees all nodes) */
	yaml_arena_free(ctx->arena);
	
	/* Note: decoded_input is NOT freed (the scanner owns it) */
	
	gtext_allocator_free(alloc, ctx);
}

/* Point at the scanner's decoded stream. See the field in yaml_internal.h. */
void yaml_context_set_decoded_input(yaml_context *ctx, const char *buf, size_t len) {
	if (!ctx) return;
	/* The scanner grows this buffer by appending, so what is already cached
	   still describes it - unless the buffer moved, or got shorter, which an
	   append never does. Either of those and the cache is about something
	   else. */
	if (buf != ctx->line_cache_buffer || len < ctx->line_cache_upto) {
		ctx->line_cache_buffer = NULL;
		ctx->line_cache_start = 0;
		ctx->line_cache_upto = 0;
	}
	ctx->decoded_input = buf;
	ctx->decoded_input_len = len;
}

size_t yaml_context_line_start(yaml_context *ctx, size_t offset) {
	const char *buffer;
	size_t i;

	if (!ctx || !ctx->decoded_input) return 0;
	buffer = ctx->decoded_input;
	if (offset > ctx->decoded_input_len) offset = ctx->decoded_input_len;

	if (buffer == ctx->line_cache_buffer && offset >= ctx->line_cache_start) {
		if (offset <= ctx->line_cache_upto) {
			/* Inside the verified span: no break between the cached start
			   and here, so the cached start is this offset's too. */
			return ctx->line_cache_start;
		}
		/* Past it. Extend forwards, which costs each byte of the input once
		   across the whole parse rather than once per question.
		   
		   A deeply nested flow document does not come this way: collections
		   close innermost first, and an outer collection begins earlier in
		   the buffer, so the offsets asked about *descend* and land inside
		   the span the first question established. Measured on 50000 nested
		   sequences: one backwards walk, 50000 hits, zero iterations here.
		   Which is why removing the line below - so the span never grows -
		   is caught by no test in the suite. It stays because it is what
		   makes the invariant above true, not because anything measured
		   it. */
		for (i = ctx->line_cache_upto; i < offset; i++) {
			if (buffer[i] == '\n' || buffer[i] == '\r') {
				ctx->line_cache_start = i + 1;
			}
		}
		ctx->line_cache_upto = offset;
		return ctx->line_cache_start;
	}

	/* Backwards, as it always was - for the first question, and for any that
	   goes back before what is cached. */
	i = offset;
	while (i > 0) {
		const char ch = buffer[i - 1];
		if (ch == '\n' || ch == '\r') break;
		i--;
	}
	ctx->line_cache_buffer = buffer;
	ctx->line_cache_start = i;
	ctx->line_cache_upto = offset;
	return i;
}

/* Allocate from context's arena */
void *yaml_context_alloc(yaml_context *ctx, size_t size, size_t align) {
	if (!ctx) return NULL;
	return yaml_arena_alloc(ctx->arena, size, align);
}
