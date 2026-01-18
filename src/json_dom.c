/**
 * @file json_dom.c
 * @brief DOM value structure and memory management for JSON module
 */

#include "json_internal.h"
#include <text/json.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Arena allocator implementation
// Uses a simple linked list of blocks for efficient bulk allocation

/**
 * @brief Arena block structure
 *
 * Each block contains a chunk of memory that can be allocated from.
 * Blocks are linked together to form the arena.
 */
typedef struct json_arena_block {
    struct json_arena_block* next;  ///< Next block in the arena
    size_t used;                     ///< Bytes used in this block
    size_t size;                     ///< Total size of this block
    char data[];                      ///< Flexible array member for block data
} json_arena_block;

/**
 * @brief Arena allocator structure
 *
 * Manages a collection of blocks for efficient bulk allocation.
 * All memory is freed when the arena is destroyed.
 */
typedef struct {
    json_arena_block* first;         ///< First block in the arena
    json_arena_block* current;        ///< Current block being used
    size_t block_size;                ///< Size of each new block
} json_arena;

// Default block size (64KB)
#define JSON_ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

/**
 * @brief Create a new arena allocator
 *
 * @param initial_block_size Initial block size (0 = use default)
 * @return Pointer to arena, or NULL on failure
 */
static json_arena* json_arena_new(size_t initial_block_size) {
    json_arena* arena = malloc(sizeof(json_arena));
    if (!arena) {
        return NULL;
    }

    arena->block_size = initial_block_size > 0 ? initial_block_size : JSON_ARENA_DEFAULT_BLOCK_SIZE;
    arena->first = NULL;
    arena->current = NULL;

    return arena;
}

/**
 * @brief Allocate memory from the arena
 *
 * @param arena Arena to allocate from
 * @param size Size in bytes to allocate
 * @param align Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory, or NULL on failure
 */
static void* json_arena_alloc(json_arena* arena, size_t size, size_t align) {
    if (!arena || size == 0) {
        return NULL;
    }

    // Calculate alignment
    size_t align_mask = align - 1;

    // If we have a current block, try to allocate from it
    if (arena->current) {
        size_t offset = arena->current->used;
        size_t aligned_offset = (offset + align_mask) & ~align_mask;

        if (aligned_offset + size <= arena->current->size) {
            // Can fit in current block
            arena->current->used = aligned_offset + size;
            return arena->current->data + aligned_offset;
        }
    }

    // Need a new block
    // Block size should be at least size + alignment overhead
    size_t block_size = arena->block_size;
    if (size + align > block_size) {
        block_size = size + align;
    }

    json_arena_block* block = malloc(sizeof(json_arena_block) + block_size);
    if (!block) {
        return NULL;
    }

    block->next = NULL;
    block->size = block_size;

    // Align the data pointer
    size_t offset = 0;
    size_t aligned_offset = (offset + align_mask) & ~align_mask;
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
 * @param arena Arena to free
 */
static void json_arena_free(json_arena* arena) {
    if (!arena) {
        return;
    }

    json_arena_block* block = arena->first;
    while (block) {
        json_arena_block* next = block->next;
        free(block);
        block = next;
    }

    free(arena);
}

/**
 * @brief JSON context structure
 *
 * Holds the arena allocator and other context information
 * for a JSON DOM tree.
 */
typedef struct {
    json_arena* arena;                ///< Arena allocator for this DOM
} json_context;

/**
 * @brief JSON value structure
 *
 * The actual DOM node structure. Allocated from the arena.
 */
struct text_json_value {
    text_json_type type;              ///< Type of this value
    json_context* ctx;                ///< Context (arena) for this value tree

    union {
        int boolean;                  ///< For TEXT_JSON_BOOL
        struct {
            char* data;               ///< String data (null-terminated)
            size_t len;               ///< String length in bytes
        } string;                     ///< For TEXT_JSON_STRING
        struct {
            char* lexeme;             ///< Original number lexeme
            size_t lexeme_len;        ///< Length of lexeme
            int64_t i64;              ///< int64 representation (if valid)
            uint64_t u64;             ///< uint64 representation (if valid)
            double dbl;               ///< double representation (if valid)
            int has_i64;              ///< 1 if i64 is valid
            int has_u64;              ///< 1 if u64 is valid
            int has_dbl;              ///< 1 if dbl is valid
        } number;                     ///< For TEXT_JSON_NUMBER
        struct {
            text_json_value** elems;  ///< Array of value pointers
            size_t count;             ///< Number of elements
            size_t capacity;          ///< Allocated capacity
        } array;                      ///< For TEXT_JSON_ARRAY
        struct {
            struct {
                char* key;            ///< Object key
                size_t key_len;       ///< Key length
                text_json_value* value; ///< Object value
            }* pairs;                 ///< Array of key-value pairs
            size_t count;             ///< Number of pairs
            size_t capacity;          ///< Allocated capacity
        } object;                     ///< For TEXT_JSON_OBJECT
    } as;
};

void text_json_free(text_json_value* v) {
    if (!v || !v->ctx) {
        return;
    }

    // Free the arena, which frees all memory
    json_arena_free(v->ctx->arena);
    // Note: the context itself is allocated in the arena, so it's already freed
}
