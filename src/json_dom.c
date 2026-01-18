/**
 * @file json_dom.c
 * @brief DOM value structure and memory management for JSON module
 */

#include "json_internal.h"
#include <text/json.h>
#include <text/json_dom.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

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

// Create a new arena allocator
// initial_block_size: Initial block size (0 = use default)
// Returns: Pointer to arena, or NULL on failure
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

// Allocate memory from the arena
// arena: Arena to allocate from
// size: Size in bytes to allocate
// align: Alignment requirement (must be power of 2, not 0)
// Returns: Pointer to allocated memory, or NULL on failure
static void* json_arena_alloc(json_arena* arena, size_t size, size_t align) {
    if (!arena || size == 0) {
        return NULL;
    }

    // Validate alignment: must be power of 2 and not 0
    // Check if align is 0 or not a power of 2
    if (align == 0 || (align & (align - 1)) != 0) {
        return NULL;  // Invalid alignment
    }

    // Calculate alignment mask
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
    // Check for overflow in size + align
    if (size > SIZE_MAX - align) {
        return NULL;  // Overflow
    }
    if (size + align > block_size) {
        block_size = size + align;
    }

    // Check for overflow in sizeof + block_size
    size_t block_alloc_size = sizeof(json_arena_block) + block_size;
    if (block_alloc_size < block_size) {  // Overflow check
        return NULL;
    }
    json_arena_block* block = malloc(block_alloc_size);
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

// Free all memory in the arena
// arena: Arena to free (can be NULL)
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

// JSON context structure
// Holds the arena allocator and other context information
// for a JSON DOM tree.
typedef struct {
    json_arena* arena;                ///< Arena allocator for this DOM
} json_context;

// JSON value structure
// The actual DOM node structure. Allocated from the arena.
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

// Create a new context with an arena
// Allocates a context and arena for a new DOM tree.
// The context is allocated with malloc (not in the arena) so it can
// be accessed to free the arena.
// Returns: New context, or NULL on failure
static json_context* json_context_new(void) {
    json_context* ctx = malloc(sizeof(json_context));
    if (!ctx) {
        return NULL;
    }

    ctx->arena = json_arena_new(0);  // Use default block size
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

// Free a context and its arena
// ctx: Context to free (can be NULL)
static void json_context_free(json_context* ctx) {
    if (!ctx) {
        return;
    }

    json_arena_free(ctx->arena);
    free(ctx);
}

void text_json_free(text_json_value* v) {
    if (!v || !v->ctx) {
        return;
    }

    // Free the context, which frees the arena and all memory
    json_context* ctx = v->ctx;
    json_context_free(ctx);
}

// Alignment for text_json_value (align to pointer size, typically 8 bytes)
#define JSON_VALUE_ALIGN 8

// Helper to create a value with a new context
static text_json_value* json_value_new_with_context(text_json_type type, json_context* ctx) {
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = (text_json_value*)json_arena_alloc(ctx->arena, sizeof(text_json_value), JSON_VALUE_ALIGN);
    if (!val) {
        return NULL;
    }

    val->type = type;
    val->ctx = ctx;
    memset(&val->as, 0, sizeof(val->as));

    return val;
}

TEXT_API text_json_value* text_json_new_null(void) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_NULL, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    return val;
}

TEXT_API text_json_value* text_json_new_bool(int b) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_BOOL, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    val->as.boolean = b ? 1 : 0;
    return val;
}

TEXT_API text_json_value* text_json_new_string(const char* s, size_t len) {
    // Allow NULL pointer only if len is 0 (empty string)
    if (!s && len > 0) {
        return NULL;
    }

    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_STRING, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Allocate string data (len + 1 for null terminator)
    if (len > SIZE_MAX - 1) {
        json_context_free(ctx);
        return NULL;
    }
    char* str_data = (char*)json_arena_alloc(ctx->arena, len + 1, 1);
    if (!str_data) {
        json_context_free(ctx);
        return NULL;
    }

    // Copy string data (safe even if s is NULL and len is 0)
    if (len > 0 && s != NULL) {
        memcpy(str_data, s, len);
    }
    str_data[len] = '\0';  // Null terminator

    val->as.string.data = str_data;
    val->as.string.len = len;
    return val;
}

TEXT_API text_json_value* text_json_new_number_from_lexeme(const char* s, size_t len) {
    if (!s || len == 0) {
        return NULL;
    }

    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_NUMBER, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Allocate lexeme
    char* lexeme = (char*)json_arena_alloc(ctx->arena, len + 1, 1);
    if (!lexeme) {
        json_context_free(ctx);
        return NULL;
    }

    memcpy(lexeme, s, len);
    lexeme[len] = '\0';

    val->as.number.lexeme = lexeme;
    val->as.number.lexeme_len = len;
    val->as.number.has_i64 = 0;
    val->as.number.has_u64 = 0;
    val->as.number.has_dbl = 0;
    return val;
}

TEXT_API text_json_value* text_json_new_number_i64(int64_t x) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_NUMBER, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Generate lexeme from int64
    char lexeme_buf[32];  // Enough for any int64
    int snprintf_result = snprintf(lexeme_buf, sizeof(lexeme_buf), "%lld", (long long)x);
    if (snprintf_result < 0 || (size_t)snprintf_result >= sizeof(lexeme_buf)) {
        json_context_free(ctx);
        return NULL;
    }
    size_t lexeme_len = (size_t)snprintf_result;

    // Allocate lexeme in arena
    char* lexeme = (char*)json_arena_alloc(ctx->arena, lexeme_len + 1, 1);
    if (!lexeme) {
        json_context_free(ctx);
        return NULL;
    }
    memcpy(lexeme, lexeme_buf, lexeme_len + 1);

    val->as.number.lexeme = lexeme;
    val->as.number.lexeme_len = lexeme_len;
    val->as.number.i64 = x;
    val->as.number.has_i64 = 1;
    val->as.number.has_u64 = 0;
    val->as.number.has_dbl = 0;
    return val;
}

TEXT_API text_json_value* text_json_new_number_u64(uint64_t x) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_NUMBER, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Generate lexeme from uint64
    char lexeme_buf[32];  // Enough for any uint64
    int snprintf_result = snprintf(lexeme_buf, sizeof(lexeme_buf), "%llu", (unsigned long long)x);
    if (snprintf_result < 0 || (size_t)snprintf_result >= sizeof(lexeme_buf)) {
        json_context_free(ctx);
        return NULL;
    }
    size_t lexeme_len = (size_t)snprintf_result;

    // Allocate lexeme in arena
    char* lexeme = (char*)json_arena_alloc(ctx->arena, lexeme_len + 1, 1);
    if (!lexeme) {
        json_context_free(ctx);
        return NULL;
    }
    memcpy(lexeme, lexeme_buf, lexeme_len + 1);

    val->as.number.lexeme = lexeme;
    val->as.number.lexeme_len = lexeme_len;
    val->as.number.u64 = x;
    val->as.number.has_i64 = 0;
    val->as.number.has_u64 = 1;
    val->as.number.has_dbl = 0;
    return val;
}

TEXT_API text_json_value* text_json_new_number_double(double x) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_NUMBER, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Generate lexeme from double
    // Use %g for compact representation, but %f might be better for round-trip
    // For now, use %g and let the user specify precision if needed
    char lexeme_buf[64];  // Enough for any double
    int snprintf_result = snprintf(lexeme_buf, sizeof(lexeme_buf), "%.17g", x);
    if (snprintf_result < 0 || (size_t)snprintf_result >= sizeof(lexeme_buf)) {
        json_context_free(ctx);
        return NULL;
    }
    size_t lexeme_len = (size_t)snprintf_result;

    // Allocate lexeme in arena
    char* lexeme = (char*)json_arena_alloc(ctx->arena, lexeme_len + 1, 1);
    if (!lexeme) {
        json_context_free(ctx);
        return NULL;
    }
    memcpy(lexeme, lexeme_buf, lexeme_len + 1);

    val->as.number.lexeme = lexeme;
    val->as.number.lexeme_len = lexeme_len;
    val->as.number.dbl = x;
    val->as.number.has_i64 = 0;
    val->as.number.has_u64 = 0;
    val->as.number.has_dbl = 1;
    return val;
}

TEXT_API text_json_value* text_json_new_array(void) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_ARRAY, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Initialize empty array
    val->as.array.elems = NULL;
    val->as.array.count = 0;
    val->as.array.capacity = 0;
    return val;
}

TEXT_API text_json_value* text_json_new_object(void) {
    json_context* ctx = json_context_new();
    if (!ctx) {
        return NULL;
    }

    text_json_value* val = json_value_new_with_context(TEXT_JSON_OBJECT, ctx);
    if (!val) {
        json_context_free(ctx);
        return NULL;
    }

    // Initialize empty object
    val->as.object.pairs = NULL;
    val->as.object.count = 0;
    val->as.object.capacity = 0;
    return val;
}
