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
 * @file yaml_arena.c
 * @brief Arena allocator for YAML DOM nodes
 *
 * Provides efficient bulk allocation with O(1) free for entire arena.
 * Blocks grow exponentially: 4KB → 8KB → 16KB → 32KB → 64KB (capped).
 */

#include <ghoti.io/text/macros.h>
#include "yaml_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Initial block size: 4KB */
#define INITIAL_BLOCK_SIZE (4 * 1024)

/* Maximum block size: 64KB */
#define MAX_BLOCK_SIZE (64 * 1024)

/* Align size up to the next multiple of alignment */
static size_t align_size(size_t size, size_t alignment) {
	if (alignment == 0) alignment = 1;
	size_t mask = alignment - 1;
	return (size + mask) & ~mask;
}

/* Create a new arena block */
static yaml_arena_block *block_create(size_t size, const GTEXT_Allocator *alloc) {
	/* Allocate block header + data in one allocation */
	size_t total = sizeof(yaml_arena_block) - 1 + size;  /* -1 for data[1] already counted */
	yaml_arena_block *block = (yaml_arena_block *)gtext_allocator_malloc(alloc, total);
	if (!block) return NULL;
	
	block->next = NULL;
	block->used = 0;
	block->size = size;
	return block;
}

/* Create new arena */
yaml_arena *yaml_arena_new(const GTEXT_Allocator *alloc) {
	yaml_arena *arena = (yaml_arena *)gtext_allocator_malloc(alloc, sizeof(yaml_arena));
	if (!arena) return NULL;

	/* Recorded before the first block, so a failure below still frees through
	   the allocator the block came from. */
	arena->alloc = alloc;

	/* Create initial block */
	yaml_arena_block *block = block_create(INITIAL_BLOCK_SIZE, alloc);
	if (!block) {
		gtext_allocator_free(alloc, arena);
		return NULL;
	}
	
	arena->first = block;
	arena->current = block;
	arena->block_size = INITIAL_BLOCK_SIZE;
	return arena;
}

/* Free arena and all blocks */
void yaml_arena_free(yaml_arena *arena) {
	if (!arena) return;

	/* Captured before the loop: the last free below releases the structure this
	   was read from, so reading it afterwards is a use-after-free. */
	const GTEXT_Allocator *alloc = arena->alloc;

	/* Free all blocks */
	yaml_arena_block *block = arena->first;
	while (block) {
		yaml_arena_block *next = block->next;
		gtext_allocator_free(alloc, block);
		block = next;
	}

	gtext_allocator_free(alloc, arena);
}

/* Allocate from arena with alignment */
void *yaml_arena_alloc(yaml_arena *arena, size_t size, size_t align) {
	if (!arena || size == 0) return NULL;
	
	/* Ensure alignment is power of 2 */
	if (align == 0) align = 8;  /* Default alignment */
	
	/* Align current position based on actual data pointer address */
	yaml_arena_block *block = arena->current;
	uintptr_t data_addr = (uintptr_t)(block->data + block->used);
	uintptr_t aligned_addr = align_size(data_addr, align);
	size_t offset = aligned_addr - (uintptr_t)block->data;
	size_t needed = offset + size;
	
	/* Check if current block has enough space */
	if (needed <= block->size) {
		/* Allocate from current block */
		void *ptr = block->data + offset;
		block->used = needed;
		return ptr;
	}
	
	/* Need a new block */
	/* Decide size: either 2x current block size (capped at MAX), or size if larger */
	size_t next_block_size = arena->block_size * 2;
	if (next_block_size > MAX_BLOCK_SIZE) {
		next_block_size = MAX_BLOCK_SIZE;
	}
	
	/* If allocation is larger than default next size, use it */
	if (size > next_block_size) {
		next_block_size = align_size(size, 1024);  /* Round up to KB boundary */
	}
	
	/* Create new block */
	yaml_arena_block *new_block = block_create(next_block_size, arena->alloc);
	if (!new_block) return NULL;  /* OOM */
	
	/* Link new block */
	block->next = new_block;
	arena->current = new_block;
	arena->block_size = next_block_size;
	
	/* Allocate from new block */
	/* Align the pointer itself, not just the offset */
	data_addr = (uintptr_t)new_block->data;
	aligned_addr = align_size(data_addr, align);
	offset = aligned_addr - data_addr;
	new_block->used = offset + size;
	return new_block->data + offset;
}
