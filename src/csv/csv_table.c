/**
 * @file csv_table.c
 * @brief Table structure and arena allocator for CSV module
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Arena allocator implementation
// Uses a simple linked list of blocks for efficient bulk allocation

/**
 * @brief Arena block structure
 *
 * Each block contains a chunk of memory that can be allocated from.
 * Blocks are linked together to form the arena.
 */
typedef struct csv_arena_block {
    struct csv_arena_block* next;  ///< Next block in the arena
    size_t used;                     ///< Bytes used in this block
    size_t size;                     ///< Total size of this block
    char data[];                      ///< Flexible array member for block data
} csv_arena_block;

/**
 * @brief Arena allocator structure
 *
 * Manages a collection of blocks for efficient bulk allocation.
 * All memory is freed when the arena is destroyed.
 */
struct csv_arena {
    csv_arena_block* first;         ///< First block in the arena
    csv_arena_block* current;       ///< Current block being used
    size_t block_size;              ///< Size of each new block
};

// Default block size (64KB)
#define CSV_ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

/**
 * @brief Create a new arena allocator
 *
 * @param initial_block_size Initial block size (0 = use default)
 * @return Pointer to arena, or NULL on failure
 */
static csv_arena* csv_arena_new(size_t initial_block_size) {
    csv_arena* arena = malloc(sizeof(csv_arena));
    if (!arena) {
        return NULL;
    }

    arena->block_size = initial_block_size > 0 ? initial_block_size : CSV_ARENA_DEFAULT_BLOCK_SIZE;
    arena->first = NULL;
    arena->current = NULL;

    return arena;
}

/**
 * @brief Allocate memory from the arena
 *
 * @param arena Arena to allocate from
 * @param size Size in bytes to allocate
 * @param align Alignment requirement (must be power of 2, not 0)
 * @return Pointer to allocated memory, or NULL on failure
 */
static void* csv_arena_alloc(csv_arena* arena, size_t size, size_t align) {
    if (!arena || size == 0) {
        return NULL;
    }

    // Validate alignment: must be power of 2 and not 0
    if (align == 0 || (align & (align - 1)) != 0) {
        return NULL;  // Invalid alignment
    }

    // Calculate alignment mask
    size_t align_mask = align - 1;

    // If we have a current block, try to allocate from it
    if (arena->current) {
        size_t offset = arena->current->used;
        size_t aligned_offset = (offset + align_mask) & ~align_mask;

        // Check for overflow in aligned_offset + size before comparison
        if (aligned_offset <= SIZE_MAX - size && aligned_offset + size <= arena->current->size) {
            // Can fit in current block
            arena->current->used = aligned_offset + size;
            return arena->current->data + aligned_offset;
        }
    }

    // Need a new block
    // Block size should be at least size + alignment overhead
    size_t block_size = arena->block_size;
    // Check for overflow in size + align
    if (size > SIZE_MAX - align) {
        return NULL;  // Overflow
    }
    if (size + align > block_size) {
        block_size = size + align;
    }

    // Check for overflow in sizeof + block_size
    size_t block_alloc_size = sizeof(csv_arena_block) + block_size;
    if (block_alloc_size < block_size) {  // Overflow check
        return NULL;
    }
    csv_arena_block* block = malloc(block_alloc_size);
    if (!block) {
        return NULL;
    }

    block->next = NULL;
    block->size = block_size;

    // Align the data pointer
    size_t offset = 0;
    size_t aligned_offset = (offset + align_mask) & ~align_mask;
    // Check for overflow before assignment
    if (aligned_offset > SIZE_MAX - size) {
        free(block);
        return NULL;  // Overflow
    }
    block->used = aligned_offset + size;

    // Link into arena
    if (arena->first == NULL) {
        arena->first = block;
    } else {
        arena->current->next = block;
    }
    arena->current = block;

    return block->data + aligned_offset;
}

/**
 * @brief Free all memory in the arena
 *
 * @param arena Arena to free (can be NULL)
 */
static void csv_arena_free(csv_arena* arena) {
    if (!arena) {
        return;
    }

    csv_arena_block* block = arena->first;
    while (block) {
        csv_arena_block* next = block->next;
        free(block);
        block = next;
    }

    free(arena);
}

/**
 * @brief Create a new CSV context with arena
 *
 * @return New context, or NULL on failure
 */
csv_context* csv_context_new(void) {
    csv_context* ctx = malloc(sizeof(csv_context));
    if (!ctx) {
        return NULL;
    }

    ctx->arena = csv_arena_new(0);  // Use default block size
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }

    ctx->input_buffer = NULL;
    ctx->input_buffer_len = 0;

    return ctx;
}

/**
 * @brief Set input buffer for in-situ mode
 *
 * @param ctx Context to set input buffer on (must not be NULL)
 * @param input_buffer Original input buffer (caller-owned, must remain valid)
 * @param input_buffer_len Length of input buffer
 */
void csv_context_set_input_buffer(csv_context* ctx, const char* input_buffer, size_t input_buffer_len) {
    if (!ctx) {
        return;
    }
    ctx->input_buffer = input_buffer;
    ctx->input_buffer_len = input_buffer_len;
}

/**
 * @brief Free a CSV context and its arena
 *
 * @param ctx Context to free (can be NULL)
 */
void csv_context_free(csv_context* ctx) {
    if (!ctx) {
        return;
    }

    csv_arena_free(ctx->arena);
    free(ctx);
}

/**
 * @brief Allocate memory from a context's arena
 *
 * @param ctx Context containing the arena
 * @param size Size in bytes to allocate
 * @param align Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* csv_arena_alloc_for_context(csv_context* ctx, size_t size, size_t align) {
    if (!ctx || !ctx->arena) {
        return NULL;
    }
    return csv_arena_alloc(ctx->arena, size, align);
}
