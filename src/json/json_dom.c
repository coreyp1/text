/**
 * @file json_dom.c
 * @brief DOM value structure and memory management for JSON module
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
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
typedef struct json_arena {
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



// Create a new context with an arena
// Allocates a context and arena for a new DOM tree.
// The context is allocated with malloc (not in the arena) so it can
// be accessed to free the arena.
// Returns: New context, or NULL on failure
json_context* json_context_new(void) {
    json_context* ctx = malloc(sizeof(json_context));
    if (!ctx) {
        return NULL;
    }

    ctx->arena = json_arena_new(0);  // Use default block size
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }

    ctx->input_buffer = NULL;
    ctx->input_buffer_len = 0;

    return ctx;
}

// Set input buffer for in-situ mode
// This stores a reference to the input buffer in the context.
// The buffer is caller-owned and must remain valid for the lifetime of the DOM.
// ctx: Context to set input buffer on (must not be NULL)
// input_buffer: Original input buffer (caller-owned, must remain valid)
// input_buffer_len: Length of input buffer
void json_context_set_input_buffer(json_context* ctx, const char* input_buffer, size_t input_buffer_len) {
    if (!ctx) {
        return;
    }
    ctx->input_buffer = input_buffer;
    ctx->input_buffer_len = input_buffer_len;
}

// Free a context and its arena
// ctx: Context to free (can be NULL)
// Note: The input buffer (if set) is caller-owned and is NOT freed here.
void json_context_free(json_context* ctx) {
    if (!ctx) {
        return;
    }

    json_arena_free(ctx->arena);
    free(ctx);
}

// Recursively free child values that have different contexts
static void json_free_children_recursive(text_json_value* v) {
    if (!v) {
        return;
    }

    json_context* parent_ctx = v->ctx;

    if (v->type == TEXT_JSON_ARRAY && v->as.array.elems) {
        for (size_t i = 0; i < v->as.array.count; i++) {
            text_json_value* child = v->as.array.elems[i];
            if (child) {
                if (child->ctx && child->ctx != parent_ctx) {
                    // Child has different context - save context before freeing
                    json_context* child_ctx = child->ctx;
                    // Recursively free child's children first (to find nested values with different contexts)
                    json_free_children_recursive(child);
                    // Now free the child's context (this frees the child value structure itself)
                    json_context_free(child_ctx);
                } else {
                    // Child has same context - still need to recurse to find nested values with different contexts
                    json_free_children_recursive(child);
                }
            }
        }
    } else if (v->type == TEXT_JSON_OBJECT && v->as.object.pairs) {
        for (size_t i = 0; i < v->as.object.count; i++) {
            text_json_value* child = v->as.object.pairs[i].value;
            if (child) {
                if (child->ctx && child->ctx != parent_ctx) {
                    // Child has different context - save context before freeing
                    json_context* child_ctx = child->ctx;
                    // Recursively free child's children first (to find nested values with different contexts)
                    json_free_children_recursive(child);
                    // Now free the child's context (this frees the child value structure itself)
                    json_context_free(child_ctx);
                } else {
                    // Child has same context - still need to recurse to find nested values with different contexts
                    json_free_children_recursive(child);
                }
            }
        }
    }
}

void text_json_free(text_json_value* v) {
    if (!v || !v->ctx) {
        return;
    }

    // First, recursively free any children that have different contexts
    json_free_children_recursive(v);

    // Then free this value's context, which frees the arena and all memory
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

// Internal helper to create a value using an existing context (for parser)
// This allows all values in a parse tree to share the same context
text_json_value* json_value_new_with_existing_context(text_json_type type, json_context* ctx) {
    return json_value_new_with_context(type, ctx);
}

// Internal helper to allocate from an arena (for parser)
void* json_arena_alloc_for_context(json_context* ctx, size_t size, size_t align) {
    if (!ctx || !ctx->arena) {
        return NULL;
    }
    return json_arena_alloc(ctx->arena, size, align);
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

// Accessor functions

TEXT_API text_json_type text_json_typeof(const text_json_value* v) {
    if (!v) {
        return TEXT_JSON_NULL;
    }
    return v->type;
}

TEXT_API text_json_status text_json_get_bool(const text_json_value* v, int* out) {
    if (!v || !out) {
        return TEXT_JSON_E_INVALID;
    }
    if (v->type != TEXT_JSON_BOOL) {
        return TEXT_JSON_E_INVALID;
    }
    *out = v->as.boolean;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_get_string(const text_json_value* v, const char** out, size_t* out_len) {
    if (!v || !out || !out_len) {
        return TEXT_JSON_E_INVALID;
    }
    if (v->type != TEXT_JSON_STRING) {
        return TEXT_JSON_E_INVALID;
    }
    *out = v->as.string.data;
    *out_len = v->as.string.len;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_get_number_lexeme(const text_json_value* v, const char** out, size_t* out_len) {
    if (!v || !out || !out_len) {
        return TEXT_JSON_E_INVALID;
    }
    if (v->type != TEXT_JSON_NUMBER) {
        return TEXT_JSON_E_INVALID;
    }
    if (!v->as.number.lexeme) {
        return TEXT_JSON_E_INVALID;
    }
    *out = v->as.number.lexeme;
    *out_len = v->as.number.lexeme_len;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_get_i64(const text_json_value* v, int64_t* out) {
    if (!v || !out) {
        return TEXT_JSON_E_INVALID;
    }
    if (v->type != TEXT_JSON_NUMBER) {
        return TEXT_JSON_E_INVALID;
    }
    if (!v->as.number.has_i64) {
        return TEXT_JSON_E_INVALID;
    }
    *out = v->as.number.i64;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_get_u64(const text_json_value* v, uint64_t* out) {
    if (!v || !out) {
        return TEXT_JSON_E_INVALID;
    }
    if (v->type != TEXT_JSON_NUMBER) {
        return TEXT_JSON_E_INVALID;
    }
    if (!v->as.number.has_u64) {
        return TEXT_JSON_E_INVALID;
    }
    *out = v->as.number.u64;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_get_double(const text_json_value* v, double* out) {
    if (!v || !out) {
        return TEXT_JSON_E_INVALID;
    }
    if (v->type != TEXT_JSON_NUMBER) {
        return TEXT_JSON_E_INVALID;
    }
    if (!v->as.number.has_dbl) {
        return TEXT_JSON_E_INVALID;
    }
    *out = v->as.number.dbl;
    return TEXT_JSON_OK;
}

TEXT_API size_t text_json_array_size(const text_json_value* v) {
    if (!v || v->type != TEXT_JSON_ARRAY) {
        return 0;
    }
    return v->as.array.count;
}

TEXT_API const text_json_value* text_json_array_get(const text_json_value* v, size_t idx) {
    if (!v || v->type != TEXT_JSON_ARRAY) {
        return NULL;
    }
    if (idx >= v->as.array.count) {
        return NULL;
    }
    return v->as.array.elems[idx];
}

TEXT_API size_t text_json_object_size(const text_json_value* v) {
    if (!v || v->type != TEXT_JSON_OBJECT) {
        return 0;
    }
    return v->as.object.count;
}

TEXT_API const char* text_json_object_key(const text_json_value* v, size_t idx, size_t* key_len) {
    if (!v || v->type != TEXT_JSON_OBJECT) {
        if (key_len) {
            *key_len = 0;
        }
        return NULL;
    }
    if (idx >= v->as.object.count) {
        if (key_len) {
            *key_len = 0;
        }
        return NULL;
    }
    if (key_len) {
        *key_len = v->as.object.pairs[idx].key_len;
    }
    return v->as.object.pairs[idx].key;
}

TEXT_API const text_json_value* text_json_object_value(const text_json_value* v, size_t idx) {
    if (!v || v->type != TEXT_JSON_OBJECT) {
        return NULL;
    }
    if (idx >= v->as.object.count) {
        return NULL;
    }
    return v->as.object.pairs[idx].value;
}

TEXT_API const text_json_value* text_json_object_get(const text_json_value* v, const char* key, size_t key_len) {
    if (!v || !key || v->type != TEXT_JSON_OBJECT) {
        return NULL;
    }

    // Linear search through object pairs
    for (size_t i = 0; i < v->as.object.count; ++i) {
        if (v->as.object.pairs[i].key_len == key_len) {
            if (key_len == 0 || memcmp(v->as.object.pairs[i].key, key, v->as.object.pairs[i].key_len) == 0) {
                return v->as.object.pairs[i].value;
            }
        }
    }

    return NULL;
}

// Internal helper functions for parser

text_json_status json_array_add_element(text_json_value* array, text_json_value* element) {
    if (!array || array->type != TEXT_JSON_ARRAY || !element) {
        return TEXT_JSON_E_INVALID;
    }

    // Grow array if needed
    if (array->as.array.count >= array->as.array.capacity) {
        size_t new_capacity = array->as.array.capacity == 0 ? 8 : array->as.array.capacity * 2;

        // Check for overflow
        if (new_capacity < array->as.array.capacity) {
            return TEXT_JSON_E_LIMIT;
        }

        // Allocate new array (using arena from the array's context)
        // Check for overflow in multiplication
        if (new_capacity > SIZE_MAX / sizeof(text_json_value*)) {
            return TEXT_JSON_E_LIMIT;
        }
        text_json_value** new_elems = (text_json_value**)json_arena_alloc_for_context(
            array->ctx,
            new_capacity * sizeof(text_json_value*),
            sizeof(void*)
        );
        if (!new_elems) {
            return TEXT_JSON_E_OOM;
        }

        // Copy existing elements
        if (array->as.array.elems) {
            memcpy(new_elems, array->as.array.elems, array->as.array.count * sizeof(text_json_value*));
        }

        array->as.array.elems = new_elems;
        array->as.array.capacity = new_capacity;
    }

    // Add element (check for overflow before incrementing)
    if (array->as.array.count == SIZE_MAX) {
        return TEXT_JSON_E_LIMIT;
    }
    array->as.array.elems[array->as.array.count++] = element;
    return TEXT_JSON_OK;
}

text_json_status json_object_add_pair(
    text_json_value* object,
    const char* key,
    size_t key_len,
    text_json_value* value
) {
    if (!object || object->type != TEXT_JSON_OBJECT || !key || !value) {
        return TEXT_JSON_E_INVALID;
    }

    // Grow object if needed
    if (object->as.object.count >= object->as.object.capacity) {
        size_t new_capacity = object->as.object.capacity == 0 ? 8 : object->as.object.capacity * 2;

        // Check for overflow
        if (new_capacity < object->as.object.capacity) {
            return TEXT_JSON_E_LIMIT;
        }

        // Allocate new pairs array (using arena from the object's context)
        // Check for overflow in multiplication
        size_t pair_size = sizeof(*(object->as.object.pairs));
        if (new_capacity > SIZE_MAX / pair_size) {
            return TEXT_JSON_E_LIMIT;
        }
        // Cast through void* to avoid anonymous struct type mismatch issues
        void* new_pairs_ptr = json_arena_alloc_for_context(
            object->ctx,
            new_capacity * pair_size,
            sizeof(void*)
        );
        if (!new_pairs_ptr) {
            return TEXT_JSON_E_OOM;
        }

        // Copy existing pairs
        if (object->as.object.pairs) {
            memcpy(new_pairs_ptr, object->as.object.pairs, object->as.object.count * sizeof(*(object->as.object.pairs)));
        }

        // Assign through void* to avoid type checking (we know the types match)
        object->as.object.pairs = (void*)new_pairs_ptr;
        object->as.object.capacity = new_capacity;
    }

    // Allocate key string in arena
    // Check for overflow in key_len + 1
    if (key_len > SIZE_MAX - 1) {
        return TEXT_JSON_E_LIMIT;
    }
    char* key_copy = (char*)json_arena_alloc_for_context(object->ctx, key_len + 1, 1);
    if (!key_copy) {
        return TEXT_JSON_E_OOM;
    }
    memcpy(key_copy, key, key_len);
    key_copy[key_len] = '\0';

    // Add pair (check for overflow before incrementing)
    if (object->as.object.count == SIZE_MAX) {
        // Note: key_copy was allocated from arena, so it will be freed when arena is freed
        return TEXT_JSON_E_LIMIT;
    }
    size_t idx = object->as.object.count++;
    object->as.object.pairs[idx].key = key_copy;
    object->as.object.pairs[idx].key_len = key_len;
    object->as.object.pairs[idx].value = value;
    return TEXT_JSON_OK;
}

// Public mutation API functions

TEXT_API text_json_status text_json_array_push(text_json_value* arr, text_json_value* child) {
    if (!arr || arr->type != TEXT_JSON_ARRAY || !child) {
        return TEXT_JSON_E_INVALID;
    }

    // Use the internal helper function which handles growing the array
    return json_array_add_element(arr, child);
}

TEXT_API text_json_status text_json_array_set(text_json_value* arr, size_t idx, text_json_value* child) {
    if (!arr || arr->type != TEXT_JSON_ARRAY || !child) {
        return TEXT_JSON_E_INVALID;
    }

    // Check bounds
    if (idx >= arr->as.array.count) {
        return TEXT_JSON_E_INVALID;
    }

    // Get the old value to be replaced
    text_json_value* old_value = arr->as.array.elems[idx];

    // If the old value has a different context, free it recursively
    if (old_value && old_value->ctx && old_value->ctx != arr->ctx) {
        json_context* child_ctx = old_value->ctx;
        // Recursively free child's children first
        json_free_children_recursive(old_value);
        // Now free the child's context (this frees the child value structure itself)
        json_context_free(child_ctx);
    }

    // Set element at index (replacing existing element)
    arr->as.array.elems[idx] = child;

    // Note: If child has a different context, it will be freed when text_json_free()
    // is called on the root, via json_free_children_recursive(). We don't free it here
    // because the child value structure itself is in that context's arena.
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_array_insert(text_json_value* arr, size_t idx, text_json_value* child) {
    if (!arr || arr->type != TEXT_JSON_ARRAY || !child) {
        return TEXT_JSON_E_INVALID;
    }

    // Check bounds - allow inserting at count (same as push)
    if (idx > arr->as.array.count) {
        return TEXT_JSON_E_INVALID;
    }

    // If inserting at the end, use push logic
    if (idx == arr->as.array.count) {
        return json_array_add_element(arr, child);
    }

    // Need to insert in the middle - grow array if needed first
    if (arr->as.array.count >= arr->as.array.capacity) {
        size_t new_capacity = arr->as.array.capacity == 0 ? 8 : arr->as.array.capacity * 2;

        // Check for overflow
        if (new_capacity < arr->as.array.capacity) {
            return TEXT_JSON_E_LIMIT;
        }

        // Allocate new array
        if (new_capacity > SIZE_MAX / sizeof(text_json_value*)) {
            return TEXT_JSON_E_LIMIT;
        }
        text_json_value** new_elems = (text_json_value**)json_arena_alloc_for_context(
            arr->ctx,
            new_capacity * sizeof(text_json_value*),
            sizeof(void*)
        );
        if (!new_elems) {
            return TEXT_JSON_E_OOM;
        }

        // Copy existing elements: [0..idx) to [0..idx), then [idx..count) to [idx+1..count+1)
        if (arr->as.array.elems) {
            // Copy elements before insertion point
            if (idx > 0) {
                memcpy(new_elems, arr->as.array.elems, idx * sizeof(text_json_value*));
            }
            // Copy elements after insertion point, shifted right by one
            if (idx < arr->as.array.count) {
                memcpy(new_elems + idx + 1, arr->as.array.elems + idx,
                       (arr->as.array.count - idx) * sizeof(text_json_value*));
            }
        }

        arr->as.array.elems = new_elems;
        arr->as.array.capacity = new_capacity;
    } else {
        // Have capacity - shift elements in place to make room
        // Shift elements from idx to count-1 one position to the right
        // Note: Writing to elems[count] is safe because we're in the else branch,
        // which means count < capacity, so elems[count] is within allocated bounds.
        for (size_t i = arr->as.array.count; i > idx; --i) {
            arr->as.array.elems[i] = arr->as.array.elems[i - 1];
        }
    }

    // Insert new element at idx
    arr->as.array.elems[idx] = child;

    // Increment count (check for overflow)
    if (arr->as.array.count == SIZE_MAX) {
        return TEXT_JSON_E_LIMIT;
    }
    arr->as.array.count++;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_array_remove(text_json_value* arr, size_t idx) {
    if (!arr || arr->type != TEXT_JSON_ARRAY) {
        return TEXT_JSON_E_INVALID;
    }

    // Check bounds
    if (idx >= arr->as.array.count) {
        return TEXT_JSON_E_INVALID;
    }

    // Get the value to be removed
    text_json_value* removed_value = arr->as.array.elems[idx];

    // If the removed value has a different context, free it recursively
    if (removed_value && removed_value->ctx && removed_value->ctx != arr->ctx) {
        json_context* child_ctx = removed_value->ctx;
        // Recursively free child's children first
        json_free_children_recursive(removed_value);
        // Now free the child's context (this frees the child value structure itself)
        json_context_free(child_ctx);
    }

    // Shift elements to the left to fill the gap
    for (size_t i = idx; i + 1 < arr->as.array.count; ++i) {
        arr->as.array.elems[i] = arr->as.array.elems[i + 1];
    }

    // Decrement count
    arr->as.array.count--;
    return TEXT_JSON_OK;
}

TEXT_API text_json_status text_json_object_put(text_json_value* obj, const char* key, size_t key_len, text_json_value* val) {
    if (!obj || obj->type != TEXT_JSON_OBJECT || !key || !val) {
        return TEXT_JSON_E_INVALID;
    }

    // Check if key already exists - if so, replace the value
    for (size_t i = 0; i < obj->as.object.count; ++i) {
        if (obj->as.object.pairs[i].key_len == key_len) {
            if (key_len == 0 || memcmp(obj->as.object.pairs[i].key, key, key_len) == 0) {
                // Key exists - replace value
                text_json_value* old_value = obj->as.object.pairs[i].value;

                // If the old value has a different context, free it recursively
                if (old_value && old_value->ctx && old_value->ctx != obj->ctx) {
                    json_context* child_ctx = old_value->ctx;
                    // Recursively free child's children first
                    json_free_children_recursive(old_value);
                    // Now free the child's context (this frees the child value structure itself)
                    json_context_free(child_ctx);
                }

                obj->as.object.pairs[i].value = val;
                return TEXT_JSON_OK;
            }
        }
    }

    // Key doesn't exist - add new pair using internal helper
    return json_object_add_pair(obj, key, key_len, val);
}

TEXT_API text_json_status text_json_object_remove(text_json_value* obj, const char* key, size_t key_len) {
    if (!obj || obj->type != TEXT_JSON_OBJECT || !key) {
        return TEXT_JSON_E_INVALID;
    }

    // Find the key
    size_t found_idx = SIZE_MAX;
    for (size_t i = 0; i < obj->as.object.count; ++i) {
        if (obj->as.object.pairs[i].key_len == key_len) {
            if (key_len == 0 || memcmp(obj->as.object.pairs[i].key, key, key_len) == 0) {
                found_idx = i;
                break;
            }
        }
    }

    // Key not found
    if (found_idx == SIZE_MAX) {
        return TEXT_JSON_E_INVALID;
    }

    // Get the value to be removed
    text_json_value* removed_value = obj->as.object.pairs[found_idx].value;

    // If the removed value has a different context, free it recursively
    if (removed_value && removed_value->ctx && removed_value->ctx != obj->ctx) {
        json_context* child_ctx = removed_value->ctx;
        // Recursively free child's children first
        json_free_children_recursive(removed_value);
        // Now free the child's context (this frees the child value structure itself)
        json_context_free(child_ctx);
    }

    // Shift pairs to the left to fill the gap
    for (size_t i = found_idx; i + 1 < obj->as.object.count; ++i) {
        obj->as.object.pairs[i] = obj->as.object.pairs[i + 1];
    }

    // Decrement count
    obj->as.object.count--;
    return TEXT_JSON_OK;
}
