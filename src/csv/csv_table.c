/**
 * @file csv_table.c
 * @brief Table structure and arena allocator for CSV module
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_table.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Global empty string constant for all empty fields
// This avoids allocating 1 byte per empty field in the arena
static const char csv_empty_field_string[] = "";

// Arena allocator implementation
// Uses a simple linked list of blocks for efficient bulk allocation

// Arena block structure (internal)
typedef struct csv_arena_block {
    struct csv_arena_block* next;  ///< Next block in the arena
    size_t used;                   ///< Bytes used in this block
    size_t size;                   ///< Total size of this block
    char data[];                   ///< Flexible array member for block data
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

// Create a new arena allocator
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

// Allocate memory from the arena
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

// Free all memory in the arena
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

// Create a new CSV context with arena
TEXT_INTERNAL_API csv_context* csv_context_new(void) {
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

// Create a new CSV context with arena and specified initial block size
static csv_context* csv_context_new_with_block_size(size_t initial_block_size) {
    csv_context* ctx = malloc(sizeof(csv_context));
    if (!ctx) {
        return NULL;
    }

    ctx->arena = csv_arena_new(initial_block_size);
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }

    ctx->input_buffer = NULL;
    ctx->input_buffer_len = 0;

    return ctx;
}

// Set input buffer for in-situ mode
TEXT_INTERNAL_API void csv_context_set_input_buffer(csv_context* ctx, const char* input_buffer, size_t input_buffer_len) {
    if (!ctx) {
        return;
    }
    ctx->input_buffer = input_buffer;
    ctx->input_buffer_len = input_buffer_len;
}

// Free a CSV context and its arena
void csv_context_free(csv_context* ctx) {
    if (!ctx) {
        return;
    }

    csv_arena_free(ctx->arena);
    free(ctx);
}

// Allocate memory from a context's arena
TEXT_INTERNAL_API void* csv_arena_alloc_for_context(csv_context* ctx, size_t size, size_t align) {
    if (!ctx || !ctx->arena) {
        return NULL;
    }
    return csv_arena_alloc(ctx->arena, size, align);
}

// ============================================================================
// Helper functions for field operations
// ============================================================================

/**
 * @brief Calculate field length from field_data and optional field_lengths array
 *
 * @param field_data Field data (may be NULL for empty fields)
 * @param field_lengths Array of field lengths, or NULL if all fields are null-terminated
 * @param field_index Index of the field
 * @return Calculated field length (0 if field_data is NULL and field_lengths is NULL)
 */
static size_t csv_calculate_field_length(
    const char* field_data,
    const size_t* field_lengths,
    size_t field_index
) {
    if (field_lengths) {
        return field_lengths[field_index];
    } else {
        // Null-terminated string
        if (field_data) {
            return strlen(field_data);
        } else {
            return 0;
        }
    }
}

/**
 * @brief Set up an empty field structure
 *
 * Sets the field to point to the global empty string constant.
 *
 * @param field Field structure to set up
 */
static void csv_setup_empty_field(csv_table_field* field) {
    field->data = csv_empty_field_string;
    field->length = 0;
    field->is_in_situ = false;  // Not in-situ, but points to global constant
}

/**
 * @brief Allocate and copy a single field to the arena
 *
 * Allocates memory from the arena and copies the field data.
 * Handles overflow checks and allocation failures.
 *
 * @param ctx Context with arena
 * @param field_data Field data to copy (must not be NULL)
 * @param field_len Field length in bytes
 * @param field_out Output field structure to populate
 * @return TEXT_CSV_OK on success, error code on failure
 */
static text_csv_status csv_allocate_and_copy_field(
    csv_context* ctx,
    const char* field_data,
    size_t field_len,
    csv_table_field* field_out
) {
    // Check for overflow in field_len + 1
    if (field_len > SIZE_MAX - 1) {
        return TEXT_CSV_E_OOM;
    }

    // Allocate field data in arena
    char* arena_data = (char*)csv_arena_alloc_for_context(
        ctx, field_len + 1, 1
    );
    if (!arena_data) {
        return TEXT_CSV_E_OOM;
    }

    // Copy field data
    memcpy(arena_data, field_data, field_len);
    arena_data[field_len] = '\0';

    // Set field structure
    field_out->data = arena_data;
    field_out->length = field_len;
    field_out->is_in_situ = false;  // All mutations copy to arena

    return TEXT_CSV_OK;
}

// Table structure implementation


// Simple hash function for header names
static size_t csv_header_hash(const char* name, size_t name_len, size_t map_size) {
    size_t hash = 5381;
    for (size_t i = 0; i < name_len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)name[i];
    }
    return hash % map_size;
}

/**
 * @brief Check if a header name already exists in the header map
 *
 * Checks for duplicate header names when uniqueness is required.
 * Optionally excludes a specific column index from the check (useful for rename operations).
 *
 * @param table Table with header map
 * @param name Header name to check
 * @param name_len Length of header name
 * @param exclude_index Column index to exclude from check (SIZE_MAX to exclude none)
 * @return TEXT_CSV_E_INVALID if duplicate found and uniqueness required, TEXT_CSV_OK otherwise
 */
static text_csv_status csv_check_header_uniqueness(
    const text_csv_table* table,
    const char* name,
    size_t name_len,
    size_t exclude_index
) {
    // Only check if uniqueness is required
    if (!table->require_unique_headers) {
        return TEXT_CSV_OK;
    }

    // Table must have headers
    if (!table->has_header || !table->header_map) {
        return TEXT_CSV_OK;
    }

    size_t hash = csv_header_hash(name, name_len, table->header_map_size);
    csv_header_entry* entry = table->header_map[hash];
    while (entry) {
        if (entry->index != exclude_index &&  // Exclude specified index if provided
            entry->name_len == name_len &&
            memcmp(entry->name, name, name_len) == 0) {
            // Duplicate header name found and uniqueness is required
            return TEXT_CSV_E_INVALID;
        }
        entry = entry->next;
    }

    return TEXT_CSV_OK;
}

// Parse context for table building
typedef struct {
    text_csv_table* table;
    csv_table_row* current_row;
    size_t current_field_index;
    size_t current_field_capacity;
    const text_csv_parse_options* opts;
    text_csv_error* err;
    text_csv_status status;
} csv_table_parse_context;

// Event callback for building table from stream
static text_csv_status csv_table_event_callback(
    const text_csv_event* event,
    void* user_data
) {
    csv_table_parse_context* ctx = (csv_table_parse_context*)user_data;
    text_csv_table* table = ctx->table;

    switch (event->type) {
        case TEXT_CSV_EVENT_RECORD_BEGIN: {
            // Allocate new row
            if (table->row_count >= table->row_capacity) {
                size_t new_capacity = table->row_capacity * 2;
                if (new_capacity < table->row_capacity) {
                    ctx->status = TEXT_CSV_E_OOM;
                    return TEXT_CSV_E_OOM;
                }
                csv_table_row* new_rows = (csv_table_row*)csv_arena_alloc_for_context(
                    table->ctx, sizeof(csv_table_row) * new_capacity, 8
                );
                if (!new_rows) {
                    ctx->status = TEXT_CSV_E_OOM;
                    return TEXT_CSV_E_OOM;
                }
                memcpy(new_rows, table->rows, sizeof(csv_table_row) * table->row_count);
                table->rows = new_rows;
                table->row_capacity = new_capacity;
            }

            ctx->current_row = &table->rows[table->row_count];
            ctx->current_row->fields = NULL;
            ctx->current_row->field_count = 0;
            ctx->current_field_index = 0;
            ctx->current_field_capacity = 0;
            return TEXT_CSV_OK;
        }

        case TEXT_CSV_EVENT_FIELD: {
            if (!ctx->current_row) {
                ctx->status = TEXT_CSV_E_INVALID;
                return TEXT_CSV_E_INVALID;
            }

            // Grow field array if needed
            if (ctx->current_field_index >= ctx->current_field_capacity) {
                size_t new_capacity = ctx->current_field_capacity == 0 ? 16 : ctx->current_field_capacity * 2;
                // Check for overflow in multiplication
                if (new_capacity < ctx->current_field_capacity && ctx->current_field_capacity > 0) {
                    ctx->status = TEXT_CSV_E_OOM;
                    return TEXT_CSV_E_OOM;
                }
                csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
                    table->ctx, sizeof(csv_table_field) * new_capacity, 8
                );
                if (!new_fields) {
                    ctx->status = TEXT_CSV_E_OOM;
                    return TEXT_CSV_E_OOM;
                }
                if (ctx->current_row->fields) {
                    memcpy(new_fields, ctx->current_row->fields, sizeof(csv_table_field) * ctx->current_field_index);
                }
                ctx->current_row->fields = new_fields;
                ctx->current_field_capacity = new_capacity;
            }

            csv_table_field* field = &ctx->current_row->fields[ctx->current_field_index];

            // Use global empty string constant for empty fields (saves arena allocation)
            if (event->data_len == 0) {
                field->data = csv_empty_field_string;
                field->length = 0;
                field->is_in_situ = false;  // Not in-situ, but points to global constant
                ctx->current_field_index++;
                ctx->current_row->field_count = ctx->current_field_index;
                return TEXT_CSV_OK;
            }

            // Handle field data - check if we can use in-situ mode
            bool can_use_in_situ = false;
            if (ctx->opts->in_situ_mode && !ctx->opts->validate_utf8) {
                // Check if field data points to the original input buffer
                // This means the field wasn't transformed (no unescaping needed) and wasn't buffered
                if (table->ctx->input_buffer && event->data) {
                    const char* input_start = table->ctx->input_buffer;
                    size_t input_buffer_len = table->ctx->input_buffer_len;
                    const char* field_start = event->data;
                    size_t field_len = event->data_len;

                    // Check for pointer arithmetic overflow safety
                    // Use subtraction to check bounds instead of addition to avoid overflow
                    if (field_start >= input_start) {
                        size_t offset_from_start = (size_t)(field_start - input_start);
                        // Check that field fits within buffer and doesn't overflow
                        if (offset_from_start <= input_buffer_len &&
                            field_len <= input_buffer_len - offset_from_start) {
                            can_use_in_situ = true;
                        }
                    }
                }
            }

            if (can_use_in_situ) {
                // Can use in-situ mode: reference input directly
                // The input buffer is caller-owned and must remain valid for the lifetime of the table
                field->data = event->data;
                field->length = event->data_len;
                field->is_in_situ = true;
            } else {
                // Need to copy (for escaping/unescaping, UTF-8 validation, or when field was transformed)
                // Check for integer overflow in allocation size
                if (event->data_len > SIZE_MAX - 1) {
                    ctx->status = TEXT_CSV_E_OOM;
                    return TEXT_CSV_E_OOM;
                }
                char* field_data = (char*)csv_arena_alloc_for_context(
                    table->ctx, event->data_len + 1, 1
                );
                if (!field_data) {
                    ctx->status = TEXT_CSV_E_OOM;
                    return TEXT_CSV_E_OOM;
                }
                // event->data is checked to be non-NULL above, and event->data_len is checked for overflow
                if (event->data) {
                    memcpy(field_data, event->data, event->data_len);
                }
                field_data[event->data_len] = '\0';
                field->data = field_data;
                field->length = event->data_len;
                field->is_in_situ = false;
            }

            ctx->current_field_index++;
            ctx->current_row->field_count = ctx->current_field_index;
            return TEXT_CSV_OK;
        }

        case TEXT_CSV_EVENT_RECORD_END: {
            if (ctx->current_row) {
                // Only count records that have at least one field
                // This prevents counting empty records (e.g., from trailing newlines)
                if (ctx->current_row->field_count > 0) {
                    table->row_count++;
                }
            }
            ctx->current_row = NULL;
            ctx->current_field_index = 0;
            ctx->current_field_capacity = 0;
            return TEXT_CSV_OK;
        }

        case TEXT_CSV_EVENT_END:
            return TEXT_CSV_OK;
    }

    return TEXT_CSV_OK;
}

// Parse CSV using streaming parser and build table
static text_csv_status csv_table_parse_internal(
    text_csv_table* table,
    const char* input,
    size_t input_len,
    const text_csv_parse_options* opts,
    text_csv_error* err
) {
    // Parse context
    csv_table_parse_context parse_ctx = {
        .table = table,
        .current_row = NULL,
        .current_field_index = 0,
        .current_field_capacity = 0,
        .opts = opts,
        .err = err,
        .status = TEXT_CSV_OK
    };

    // Create streaming parser
    text_csv_stream* stream = text_csv_stream_new(opts, csv_table_event_callback, &parse_ctx);
    if (!stream) {
        if (err) {
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_OOM,
                                        .message = "Failed to create stream parser",
                                        .line = 1,
                                        .column = 1
                                    };
        }
        return TEXT_CSV_E_OOM;
    }

    // Set original input buffer for in-situ mode and error context snippets
    // Always set it for table parsing so we can generate context snippets on errors
    // Use the input parameter directly (which may have been adjusted for BOM)
    csv_stream_set_original_input_buffer(stream, input, input_len);

    // Feed input
    text_csv_status status = text_csv_stream_feed(stream, input, input_len, err);
    if (status == TEXT_CSV_OK) {
        status = text_csv_stream_finish(stream, err);
    }

    if (status == TEXT_CSV_OK && parse_ctx.status != TEXT_CSV_OK) {
        status = parse_ctx.status;
    }

    text_csv_stream_free(stream);
    return status;
}

TEXT_API text_csv_table* text_csv_parse_table(
    const void* data,
    size_t len,
    const text_csv_parse_options* opts,
    text_csv_error* err
) {
    if (!data) {
        if (err) {
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_INVALID,
                                        .message = "Input data must not be NULL",
                                        .line = 1,
                                        .column = 1
                                    };
        }
        return NULL;
    }

    // Empty input is valid - return empty table
    if (len == 0) {
        // Create empty table
        text_csv_parse_options default_opts;
        if (!opts) {
            default_opts = text_csv_parse_options_default();
            opts = &default_opts;
        }

        csv_context* ctx = csv_context_new();
        if (!ctx) {
            if (err) {
                *err = (text_csv_error){
                                            .code = TEXT_CSV_E_OOM,
                                            .message = "Failed to create context",
                                            .line = 1,
                                            .column = 1
                                        };
            }
            return NULL;
        }

        text_csv_table* table = (text_csv_table*)malloc(sizeof(text_csv_table));
        if (!table) {
            csv_context_free(ctx);
            if (err) {
                *err = (text_csv_error){
                                            .code = TEXT_CSV_E_OOM,
                                            .message = "Failed to allocate table",
                                            .line = 1,
                                            .column = 1
                                        };
            }
            return NULL;
        }

        memset(table, 0, sizeof(text_csv_table));
        table->ctx = ctx;
        table->row_capacity = 16;
        table->rows = (csv_table_row*)csv_arena_alloc_for_context(
            ctx, sizeof(csv_table_row) * table->row_capacity, 8
        );
        if (!table->rows) {
            free(table);
            csv_context_free(ctx);
            if (err) {
                *err = (text_csv_error){
                                            .code = TEXT_CSV_E_OOM,
                                            .message = "Failed to allocate rows",
                                            .line = 1,
                                            .column = 1
                                        };
            }
            return NULL;
        }

        return table;
    }

    text_csv_parse_options default_opts;
    if (!opts) {
        default_opts = text_csv_parse_options_default();
        opts = &default_opts;
    }

    // Create context
    csv_context* ctx = csv_context_new();
    if (!ctx) {
        if (err) {
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_OOM,
                                        .message = "Failed to create context",
                                        .line = 1,
                                        .column = 1
                                    };
        }
        return NULL;
    }

    // Allocate table structure
    text_csv_table* table = (text_csv_table*)malloc(sizeof(text_csv_table));
    if (!table) {
        csv_context_free(ctx);
        if (err) {
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_OOM,
                                        .message = "Failed to allocate table",
                                        .line = 1,
                                        .column = 1
                                    };
        }
        return NULL;
    }

    memset(table, 0, sizeof(text_csv_table));
    table->ctx = ctx;
    table->row_capacity = 16;
    table->rows = (csv_table_row*)csv_arena_alloc_for_context(
        ctx, sizeof(csv_table_row) * table->row_capacity, 8
    );
    if (!table->rows) {
        free(table);
        csv_context_free(ctx);
        if (err) {
            *err = (text_csv_error){
                                        .code = TEXT_CSV_E_OOM,
                                        .message = "Failed to allocate rows",
                                        .line = 1,
                                        .column = 1
                                    };
        }
        return NULL;
    }

    // Handle BOM (must be done before setting input buffer for in-situ mode)
    const char* input = (const char*)data;
    size_t input_len = len;
    csv_position pos = {0, 1, 1};
    if (!opts->keep_bom) {
        bool was_stripped = false;
        text_csv_status status = csv_strip_bom(&input, &input_len, &pos, true, &was_stripped);
        if (status != TEXT_CSV_OK) {
            if (err) {
                *err = (text_csv_error){
                                            .code = status,
                                            .message = "Overflow in BOM stripping",
                                            .line = 1,
                                            .column = 1
                                        };
            }
            free(table);
            csv_context_free(ctx);
            return NULL;
        }
    }

    // Set input buffer for in-situ mode (use adjusted input after BOM stripping)
    if (opts->in_situ_mode) {
        csv_context_set_input_buffer(ctx, input, input_len);
    }

    // Parse
    text_csv_status status = csv_table_parse_internal(table, input, input_len, opts, err);
    if (status != TEXT_CSV_OK) {
        text_csv_free_table(table);
        return NULL;
    }

    // Set column count from first row (if table has rows)
    // This is needed for tables without headers, and will be overridden for tables with headers
    if (table->row_count > 0) {
        csv_table_row* first_row = &table->rows[0];
        table->column_count = first_row->field_count;
    }

    // Process header if enabled
    if (opts->dialect.treat_first_row_as_header && table->row_count > 0) {
        // Build header map from first row
        table->header_map_size = 16;
        table->header_map = (csv_header_entry**)calloc(table->header_map_size, sizeof(csv_header_entry*));
        if (table->header_map) {
            csv_table_row* header_row = &table->rows[0];
            // Column count already set above, but ensure it's correct
            table->column_count = header_row->field_count;
            for (size_t i = 0; i < header_row->field_count; i++) {
                csv_table_field* field = &header_row->fields[i];
                size_t hash = csv_header_hash(field->data, field->length, table->header_map_size);

                // Check for duplicates
                csv_header_entry* entry = table->header_map[hash];
                bool found_duplicate = false;
                while (entry) {
                    if (entry->name_len == field->length &&
                        memcmp(entry->name, field->data, field->length) == 0) {
                        found_duplicate = true;
                        break;
                    }
                    entry = entry->next;
                }

                if (found_duplicate) {
                    switch (opts->dialect.header_dup_mode) {
                        case TEXT_CSV_DUPCOL_ERROR:
                            free(table->header_map);
                            table->header_map = NULL;
                            text_csv_free_table(table);
                            if (err) {
                                *err = (text_csv_error){
                                                            .code = TEXT_CSV_E_INVALID,
                                                            .message = "Duplicate column name in header",
                                                            .line = 1,
                                                            .column = 1,
                                                            .col_index = i
                                                        };
                            }
                            return NULL;
                        case TEXT_CSV_DUPCOL_FIRST_WINS:
                            // Skip this duplicate
                            continue;
                        case TEXT_CSV_DUPCOL_LAST_WINS:
                            // Remove old entry, add new one
                            // Simplified: just add new entry
                            break;
                        case TEXT_CSV_DUPCOL_COLLECT:
                            // Store multiple indices (simplified: just add)
                            break;
                    }
                }

                // Create header entry
                csv_header_entry* new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
                    ctx, sizeof(csv_header_entry), 8
                );
                if (new_entry) {
                    new_entry->name = field->data;
                    new_entry->name_len = field->length;
                    new_entry->index = i;
                    new_entry->next = table->header_map[hash];
                    table->header_map[hash] = new_entry;
                }
            }
            table->has_header = true;
        }
    }

    return table;
}

TEXT_API void text_csv_free_table(text_csv_table* table) {
    if (!table) {
        return;
    }

    if (table->header_map) {
        free(table->header_map);
    }

    csv_context_free(table->ctx);
    free(table);
}

TEXT_API size_t text_csv_row_count(const text_csv_table* table) {
    if (!table) {
        return 0;
    }

    // Exclude header row if present
    if (table->has_header && table->row_count > 0) {
        return table->row_count - 1;
    }
    return table->row_count;
}

TEXT_API size_t text_csv_col_count(const text_csv_table* table, size_t row) {
    if (!table) {
        return 0;
    }

    // Adjust for header row
    size_t adjusted_row = row;
    if (table->has_header) {
        adjusted_row = row + 1;  // Skip header row
    }

    if (adjusted_row >= table->row_count) {
        return 0;
    }

    return table->rows[adjusted_row].field_count;
}

TEXT_API const char* text_csv_field(
    const text_csv_table* table,
    size_t row,
    size_t col,
    size_t* len
) {
    if (!table) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    // Adjust for header row
    size_t adjusted_row = row;
    if (table->has_header) {
        adjusted_row = row + 1;  // Skip header row
    }

    if (adjusted_row >= table->row_count) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    csv_table_row* table_row = &table->rows[adjusted_row];
    if (col >= table_row->field_count) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    csv_table_field* field = &table_row->fields[col];
    if (len) {
        *len = field->length;
    }
    return field->data;
}

/**
 * @brief Prepare fields for row operations
 *
 * Calculates field lengths, validates inputs, and allocates bulk field data.
 * This is the common logic shared by text_csv_row_append and text_csv_row_insert.
 *
 * @param table Table (must not be NULL)
 * @param fields Array of field data pointers (must not be NULL)
 * @param field_lengths Array of field lengths, or NULL if all are null-terminated
 * @param field_count Number of fields (must be > 0)
 * @param allocated_data Output array to store allocated field data pointers (must have size field_count)
 * @param allocated_lengths Output array to store field lengths (must have size field_count)
 * @param bulk_arena_data_out Output parameter for bulk allocated data block (or NULL if all fields empty)
 * @return TEXT_CSV_OK on success, error code on failure
 */
static text_csv_status csv_row_prepare_fields(
    text_csv_table* table,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count,
    const char** allocated_data,
    size_t* allocated_lengths,
    char** bulk_arena_data_out
) {
    // Phase 2: Calculate total size needed for all field data (bulk allocation)
    // This ensures atomic operation - if validation or allocation fails, table remains unchanged
    size_t total_size = 0;                    // Total bytes needed for all fields

    // Single pass: validate and calculate total size needed
    for (size_t i = 0; i < field_count; i++) {
        const char* field_data = fields[i];

        // Calculate field length using helper function
        size_t field_len = csv_calculate_field_length(field_data, field_lengths, i);
        allocated_lengths[i] = field_len;

        // Validate: if explicit length is provided and non-zero, field_data must not be NULL
        if (field_lengths && field_len > 0 && !field_data) {
            return TEXT_CSV_E_INVALID;
        }

        // Use global empty string constant for empty fields (saves arena allocation)
        if (field_len == 0) {
            allocated_data[i] = csv_empty_field_string;
            continue;
        }

        // Check for overflow in field_len + 1
        if (field_len > SIZE_MAX - 1) {
            return TEXT_CSV_E_OOM;
        }

        // Check for overflow in total_size accumulation
        size_t field_size = field_len + 1;  // +1 for null terminator
        if (total_size > SIZE_MAX - field_size) {
            return TEXT_CSV_E_OOM;
        }

        total_size += field_size;
    }

    // Phase 3: Bulk Field Data Allocation
    // Allocate one contiguous block for all non-empty fields
    char* bulk_arena_data = NULL;
    if (total_size > 0) {
        bulk_arena_data = (char*)csv_arena_alloc_for_context(
            table->ctx, total_size, 1
        );
        if (!bulk_arena_data) {
            // Allocation failed - table remains unchanged (atomic operation)
            return TEXT_CSV_E_OOM;
        }
    }

    // Copy field data into the allocated block and set pointers
    char* current_ptr = bulk_arena_data;
    for (size_t i = 0; i < field_count; i++) {
        size_t field_len = allocated_lengths[i];

        // Empty fields already have pointers set to csv_empty_field_string
        if (field_len == 0) {
            continue;
        }

        // Copy field data (field_data is guaranteed to be non-NULL here due to validation above)
        const char* field_data = fields[i];
        memcpy(current_ptr, field_data, field_len);
        current_ptr[field_len] = '\0';

        allocated_data[i] = current_ptr;
        current_ptr += field_len + 1;  // Move to next field position
    }

    *bulk_arena_data_out = bulk_arena_data;
    return TEXT_CSV_OK;
}

/**
 * @brief Allocate structures for row operations
 *
 * Allocates field array and handles row capacity growth if needed.
 * This is the common logic shared by text_csv_row_append and text_csv_row_insert.
 *
 * @param table Table (must not be NULL)
 * @param field_count Number of fields (must be > 0)
 * @param new_fields_out Output parameter for allocated field array
 * @param new_rows_out Output parameter for row array (may be same as table->rows if no growth)
 * @param new_capacity_out Output parameter for new row capacity (may be same as table->row_capacity)
 * @return TEXT_CSV_OK on success, error code on failure
 */
static text_csv_status csv_row_allocate_structures(
    text_csv_table* table,
    size_t field_count,
    csv_table_field** new_fields_out,
    csv_table_row** new_rows_out,
    size_t* new_capacity_out
) {
    // Phase 4: Field Array Allocation
    csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_table_field) * field_count, 8
    );
    if (!new_fields) {
        // Allocation failed - bulk data already allocated, but that's okay (in arena, will be freed with table)
        return TEXT_CSV_E_OOM;
    }

    // Phase 5: Row Capacity Growth (if needed)
    // Pre-allocate row capacity but don't update table->rows yet
    csv_table_row* new_rows = table->rows;
    size_t new_capacity = table->row_capacity;
    if (table->row_count >= table->row_capacity) {
        new_capacity = table->row_capacity * 2;
        // Check for overflow
        if (new_capacity < table->row_capacity) {
            return TEXT_CSV_E_OOM;
        }
        new_rows = (csv_table_row*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_row) * new_capacity, 8
        );
        if (!new_rows) {
            // Allocation failed - previous allocations remain in arena
            return TEXT_CSV_E_OOM;
        }
        // Copy existing rows (but don't update table->rows yet)
        memcpy(new_rows, table->rows, sizeof(csv_table_row) * table->row_count);
    }

    *new_fields_out = new_fields;
    *new_rows_out = new_rows;
    *new_capacity_out = new_capacity;
    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_row_append(
    text_csv_table* table,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count
) {
    // Phase 1: Validation
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }
    if (!fields) {
        return TEXT_CSV_E_INVALID;
    }
    if (field_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Adjust row_count for header if present
    size_t data_row_count = table->row_count;
    if (table->has_header && table->row_count > 0) {
        data_row_count = table->row_count - 1;
    }

    // Validate column count consistency (but don't update column_count yet)
    if (data_row_count == 0 && table->column_count == 0) {
        // First data row will set column count - validation passes
    } else {
        // Subsequent rows (or first data row when header exists) must match column count
        if (field_count != table->column_count) {
            return TEXT_CSV_E_INVALID;
        }
    }

    // Phase 2-3: Prepare fields (calculate lengths, validate, allocate bulk data)
    const char* allocated_data[field_count];  // VLA to store allocated pointers
    size_t allocated_lengths[field_count];    // VLA to store lengths
    char* bulk_arena_data = NULL;
    text_csv_status status = csv_row_prepare_fields(
        table, fields, field_lengths, field_count,
        allocated_data, allocated_lengths, &bulk_arena_data
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Phase 4-5: Allocate structures (field array, row capacity growth if needed)
    csv_table_field* new_fields;
    csv_table_row* new_rows;
    size_t new_capacity;
    status = csv_row_allocate_structures(
        table, field_count, &new_fields, &new_rows, &new_capacity
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Phase 6: Atomic State Update
    // Only after all allocations succeed:
    // 1. Update row capacity if it was grown
    if (new_rows != table->rows) {
        table->rows = new_rows;
        table->row_capacity = new_capacity;
    }

    // 2. Get pointer to new row (now safe since row capacity is updated if needed)
    csv_table_row* new_row = &table->rows[table->row_count];

    // 3. Set up field structures
    for (size_t i = 0; i < field_count; i++) {
        csv_table_field* field = &new_fields[i];
        field->data = allocated_data[i];
        field->length = allocated_lengths[i];
        // For empty fields: points to global constant, not in-situ
        // For non-empty fields: copied to arena, not in-situ
        field->is_in_situ = false;
    }

    // 4. Set row structure
    new_row->fields = new_fields;
    new_row->field_count = field_count;

    // 5. Update column_count if this is the first row
    if (data_row_count == 0 && table->column_count == 0) {
        table->column_count = field_count;
    }

    // 6. Increment row count
    table->row_count++;

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_row_insert(
    text_csv_table* table,
    size_t row_idx,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count
) {
    // Phase 1: Validation
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }
    if (!fields) {
        return TEXT_CSV_E_INVALID;
    }
    if (field_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Calculate data row count (excluding header if present)
    size_t data_row_count = table->row_count;
    if (table->has_header && table->row_count > 0) {
        data_row_count = table->row_count - 1;
    }

    // Validate row_idx (must be <= data_row_count, allowing append)
    if (row_idx > data_row_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Adjust row_idx for header if present (header is at index 0, data starts at 1)
    // row_idx is 0-based for data rows only, so we add 1 if header exists
    size_t adjusted_row_idx = row_idx;
    if (table->has_header) {
        adjusted_row_idx = row_idx + 1;
    }

    // Validate column count consistency (but don't update column_count yet)
    if (data_row_count == 0 && table->column_count == 0) {
        // First data row will set column count - validation passes
    } else {
        // Subsequent rows (or first data row when header exists) must match column count
        if (field_count != table->column_count) {
            return TEXT_CSV_E_INVALID;
        }
    }

    // Check if inserting at end (equivalent to append)
    bool is_append = (row_idx == data_row_count);

    // Phase 2-3: Prepare fields (calculate lengths, validate, allocate bulk data)
    const char* allocated_data[field_count];  // VLA to store allocated pointers
    size_t allocated_lengths[field_count];    // VLA to store lengths
    char* bulk_arena_data = NULL;
    text_csv_status status = csv_row_prepare_fields(
        table, fields, field_lengths, field_count,
        allocated_data, allocated_lengths, &bulk_arena_data
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Phase 4-5: Allocate structures (field array, row capacity growth if needed)
    csv_table_field* new_fields;
    csv_table_row* new_rows;
    size_t new_capacity;
    status = csv_row_allocate_structures(
        table, field_count, &new_fields, &new_rows, &new_capacity
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Phase 6: Row Shifting (if not appending)
    // Critical: Only shift rows AFTER all allocations succeed
    // This creates the gap for the new row
    if (!is_append) {
        // Shift rows from adjusted_row_idx to row_count-1 one position right
        // Use new_rows array (which may be the same as table->rows if capacity didn't grow)
        // Shift in reverse order to avoid overwriting
        for (size_t i = table->row_count; i > adjusted_row_idx; i--) {
            new_rows[i] = new_rows[i - 1];
        }
    }

    // Phase 7: Atomic State Update
    // Only after all allocations and shifting succeed:
    // 1. Update row capacity if it was grown
    if (new_rows != table->rows) {
        table->rows = new_rows;
        table->row_capacity = new_capacity;
    }

    // 2. Get pointer to new row (now safe since row capacity is updated if needed)
    csv_table_row* new_row;
    if (is_append) {
        new_row = &table->rows[table->row_count];
    } else {
        new_row = &table->rows[adjusted_row_idx];
    }

    // 3. Set up field structures
    for (size_t i = 0; i < field_count; i++) {
        csv_table_field* field = &new_fields[i];
        field->data = allocated_data[i];
        field->length = allocated_lengths[i];
        // For empty fields: points to global constant, not in-situ
        // For non-empty fields: copied to arena, not in-situ
        field->is_in_situ = false;
    }

    // 4. Set row structure
    new_row->fields = new_fields;
    new_row->field_count = field_count;

    // 5. Update column_count if this is the first row
    if (data_row_count == 0 && table->column_count == 0) {
        table->column_count = field_count;
    }

    // 6. Increment row count
    table->row_count++;

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_row_remove(
    text_csv_table* table,
    size_t row_idx
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Calculate data row count (excluding header if present)
    size_t data_row_count = table->row_count;
    if (table->has_header && table->row_count > 0) {
        data_row_count = table->row_count - 1;
    }

    // Validate row_idx (must be < data_row_count)
    if (row_idx >= data_row_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Adjust row_idx for header if present (header is at index 0, data starts at 1)
    // Note: Header row is protected because it's not accessible via external API
    // (row_idx is 0-based for data rows only)
    size_t adjusted_row_idx = row_idx;
    if (table->has_header) {
        adjusted_row_idx = row_idx + 1;
    }

    // Validate adjusted row_idx < row_count
    if (adjusted_row_idx >= table->row_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Shift rows from adjusted_row_idx+1 to row_count-1 one position left
    // Shift in forward order (left shift)
    for (size_t i = adjusted_row_idx; i < table->row_count - 1; i++) {
        table->rows[i] = table->rows[i + 1];
    }

    // Decrement row count
    table->row_count--;

    // Note: Field data remains in arena (no individual cleanup needed)

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_row_set(
    text_csv_table* table,
    size_t row_idx,
    const char* const* fields,
    const size_t* field_lengths,
    size_t field_count
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }
    if (!fields) {
        return TEXT_CSV_E_INVALID;
    }
    if (field_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Calculate data row count (excluding header if present)
    size_t data_row_count = table->row_count;
    if (table->has_header && table->row_count > 0) {
        data_row_count = table->row_count - 1;
    }

    // Validate row_idx (must be < data_row_count)
    if (row_idx >= data_row_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Adjust row_idx for header if present (header is at index 0, data starts at 1)
    // row_idx is 0-based for data rows only, so we add 1 if header exists
    size_t adjusted_row_idx = row_idx;
    if (table->has_header) {
        adjusted_row_idx = row_idx + 1;
    }

    // Validate field_count matches table column count
    // If column_count is 0 but table has headers, get column count from header row
    size_t expected_column_count = table->column_count;
    if (expected_column_count == 0 && table->has_header && table->row_count > 0) {
        expected_column_count = table->rows[0].field_count;
    }
    if (field_count != expected_column_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Get existing row
    csv_table_row* existing_row = &table->rows[adjusted_row_idx];

    // Phase 1: Validate fields and calculate total size needed in one pass
    // This ensures atomic operation - if validation or allocation fails, row remains unchanged
    // Also more efficient: one allocation call instead of N calls
    const char* allocated_data[field_count];  // VLA to store allocated pointers
    size_t allocated_lengths[field_count];    // VLA to store lengths
    size_t total_size = 0;                    // Total bytes needed for all fields

    // Single pass: validate and calculate total size needed
    for (size_t i = 0; i < field_count; i++) {
        const char* field_data = fields[i];

        // Calculate field length using helper function
        size_t field_len = csv_calculate_field_length(field_data, field_lengths, i);
        allocated_lengths[i] = field_len;

        // Validate: if explicit length is provided and non-zero, field_data must not be NULL
        if (field_lengths && field_len > 0 && !field_data) {
            return TEXT_CSV_E_INVALID;
        }

        // Use global empty string constant for empty fields (saves arena allocation)
        if (field_len == 0) {
            allocated_data[i] = csv_empty_field_string;
            continue;
        }

        // Check for overflow in field_len + 1
        if (field_len > SIZE_MAX - 1) {
            return TEXT_CSV_E_OOM;
        }

        // Check for overflow in total_size accumulation
        size_t field_size = field_len + 1;  // +1 for null terminator
        if (total_size > SIZE_MAX - field_size) {
            return TEXT_CSV_E_OOM;
        }

        total_size += field_size;
    }

    // Allocate one contiguous block for all non-empty fields
    char* bulk_arena_data = NULL;
    if (total_size > 0) {
        bulk_arena_data = (char*)csv_arena_alloc_for_context(
            table->ctx, total_size, 1
        );
        if (!bulk_arena_data) {
            // Allocation failed - row remains unchanged (atomic operation)
            return TEXT_CSV_E_OOM;
        }
    }

    // Second pass: copy field data into the allocated block and set pointers
    char* current_ptr = bulk_arena_data;
    for (size_t i = 0; i < field_count; i++) {
        size_t field_len = allocated_lengths[i];

        // Empty fields already have pointers set to csv_empty_field_string
        if (field_len == 0) {
            continue;
        }

        // Copy field data (field_data is guaranteed to be non-NULL here due to validation above)
        const char* field_data = fields[i];
        memcpy(current_ptr, field_data, field_len);
        current_ptr[field_len] = '\0';

        allocated_data[i] = current_ptr;
        current_ptr += field_len + 1;  // Move to next field position
    }

    // Phase 2: Update all field structures atomically (all allocations succeeded)
    for (size_t i = 0; i < field_count; i++) {
        csv_table_field* field = &existing_row->fields[i];
        // Update field structure (replace field data pointers)
        // Note: Old field data remains in arena (no individual cleanup needed)
        field->data = allocated_data[i];
        field->length = allocated_lengths[i];
        // For empty fields: points to global constant, not in-situ
        // For non-empty fields: copied to arena, not in-situ
        field->is_in_situ = false;
    }

    // Note: Field count is already set correctly (existing_row->field_count == field_count)
    // No need to update it since we're replacing fields in place

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_field_set(
    text_csv_table* table,
    size_t row,
    size_t col,
    const char* field_data,
    size_t field_length
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }
    // Allow NULL field_data only when field_length is 0 (empty field)
    // This is consistent with the writer API which allows NULL for empty fields
    if (!field_data && field_length != 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Adjust row index for header if present (header is at index 0, data starts at 1)
    size_t adjusted_row = row;
    if (table->has_header) {
        adjusted_row = row + 1;
    }

    // Validate row index
    if (adjusted_row >= table->row_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Get the row
    csv_table_row* table_row = &table->rows[adjusted_row];

    // Validate column index
    if (col >= table_row->field_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Get existing field
    csv_table_field* field = &table_row->fields[col];

    // Determine field length
    // Note: text_csv_field_set uses field_length parameter directly (not an array)
    size_t field_len;
    if (field_length == 0) {
        if (field_data) {
            // Null-terminated string
            field_len = strlen(field_data);
        } else {
            // NULL field_data with field_length=0 means empty field
            field_len = 0;
        }
    } else {
        field_len = field_length;
    }

    // Use global empty string constant for empty fields (saves arena allocation)
    if (field_len == 0) {
        csv_setup_empty_field(field);
        return TEXT_CSV_OK;
    }

    // Allocate and copy field data to arena
    text_csv_status status = csv_allocate_and_copy_field(
        table->ctx, field_data, field_len, field
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Note: Old field data remains in arena (no individual cleanup needed)

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_table_compact(text_csv_table* table) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Calculate total size needed for compaction
    // This allows us to pre-allocate a single block if possible
    size_t total_size = 0;

    // Rows array (aligned to 8 bytes)
    size_t rows_array_size = sizeof(csv_table_row) * table->row_capacity;
    size_t rows_array_aligned = (rows_array_size + 7) & ~7;  // Align to 8
    total_size = rows_array_aligned;

    // Calculate size for all rows and fields
    for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
        csv_table_row* old_row = &table->rows[row_idx];

        if (old_row->field_count == 0) {
            continue;
        }

        // Field array (aligned to 8 bytes)
        size_t field_array_size = sizeof(csv_table_field) * old_row->field_count;
        size_t field_array_aligned = (field_array_size + 7) & ~7;
        if (total_size > SIZE_MAX - field_array_aligned) {
            return TEXT_CSV_E_OOM;
        }
        total_size += field_array_aligned;

        // Field data (only for non-empty, non-in-situ fields)
        for (size_t i = 0; i < old_row->field_count; i++) {
            csv_table_field* old_field = &old_row->fields[i];

            // Skip empty fields (use global constant) and in-situ fields (reference input buffer)
            if (old_field->length == 0 || old_field->is_in_situ) {
                continue;
            }

            // Field data: length + 1 (null terminator), aligned to 1 byte
            size_t field_data_size = old_field->length + 1;
            if (total_size > SIZE_MAX - field_data_size) {
                return TEXT_CSV_E_OOM;
            }
            total_size += field_data_size;
        }
    }

    // Header map entries (if present)
    if (table->has_header && table->header_map) {
        for (size_t i = 0; i < table->header_map_size; i++) {
            csv_header_entry* entry = table->header_map[i];
            while (entry) {
                // Entry structure (aligned to 8 bytes)
                size_t entry_size = sizeof(csv_header_entry);
                size_t entry_aligned = (entry_size + 7) & ~7;
                if (total_size > SIZE_MAX - entry_aligned) {
                    return TEXT_CSV_E_OOM;
                }
                total_size += entry_aligned;

                // Entry name (only if not empty and not in-situ)
                // Note: Header map entry names typically come from header row fields,
                // so if the field is in-situ, the name is also in-situ
                // For simplicity, we'll check if name_len > 0
                if (entry->name_len > 0) {
                    // Check if name points to input buffer (in-situ)
                    // If it's in the input buffer range, it's in-situ
                    bool is_in_situ = false;
                    if (table->ctx->input_buffer && entry->name) {
                        const char* input_start = table->ctx->input_buffer;
                        const char* input_end = input_start + table->ctx->input_buffer_len;
                        if (entry->name >= input_start && entry->name < input_end) {
                            is_in_situ = true;
                        }
                    }

                    if (!is_in_situ) {
                        size_t name_size = entry->name_len + 1;
                        if (total_size > SIZE_MAX - name_size) {
                            return TEXT_CSV_E_OOM;
                        }
                        total_size += name_size;
                    }
                }

                entry = entry->next;
            }
        }
    }

    // Add some overhead for alignment and safety margin (10% or 1KB, whichever is larger)
    size_t overhead = total_size / 10;
    if (overhead < 1024) {
        overhead = 1024;
    }
    if (total_size > SIZE_MAX - overhead) {
        return TEXT_CSV_E_OOM;
    }
    total_size += overhead;

    // Create new context/arena with calculated block size
    csv_context* new_ctx = csv_context_new_with_block_size(total_size);
    if (!new_ctx) {
        return TEXT_CSV_E_OOM;
    }

    // Phase 3: Pre-allocate All Structures in New Arena
    // Allocate new rows array in new arena
    csv_table_row* new_rows = (csv_table_row*)csv_arena_alloc_for_context(
        new_ctx, sizeof(csv_table_row) * table->row_capacity, 8
    );
    if (!new_rows) {
        csv_context_free(new_ctx);
        return TEXT_CSV_E_OOM;
    }

    // Initialize new rows array (zero out unused entries)
    memset(new_rows, 0, sizeof(csv_table_row) * table->row_capacity);

    // Pre-allocate all field arrays and field data
    // Store pointers in temporary arrays
    csv_table_field** new_field_arrays = NULL;
    char*** new_field_data_ptrs = NULL;  // Array of arrays of char* pointers
    if (table->row_count > 0) {
        new_field_arrays = (csv_table_field**)malloc(sizeof(csv_table_field*) * table->row_count);
        new_field_data_ptrs = (char***)malloc(sizeof(char**) * table->row_count);
        if (!new_field_arrays || !new_field_data_ptrs) {
            free(new_field_arrays);
            free(new_field_data_ptrs);
            csv_context_free(new_ctx);
            return TEXT_CSV_E_OOM;
        }
    }

    // Pre-allocate all field arrays and field data blocks
    for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
        csv_table_row* old_row = &table->rows[row_idx];
        new_field_arrays[row_idx] = NULL;
        new_field_data_ptrs[row_idx] = NULL;

        // Skip empty rows
        if (old_row->field_count == 0) {
            continue;
        }

        // Allocate field array in new arena
        csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
            new_ctx, sizeof(csv_table_field) * old_row->field_count, 8
        );
        if (!new_fields) {
            // Free temporary arrays
            for (size_t j = 0; j < row_idx; j++) {
                free(new_field_data_ptrs[j]);
            }
            free(new_field_arrays);
            free(new_field_data_ptrs);
            csv_context_free(new_ctx);
            return TEXT_CSV_E_OOM;
        }
        new_field_arrays[row_idx] = new_fields;

        // Pre-allocate field data for non-empty, non-in-situ fields
        char** field_data_ptrs = NULL;
        size_t non_in_situ_count = 0;
        for (size_t i = 0; i < old_row->field_count; i++) {
            csv_table_field* old_field = &old_row->fields[i];
            if (old_field->length > 0 && !old_field->is_in_situ) {
                non_in_situ_count++;
            }
        }

        if (non_in_situ_count > 0) {
            field_data_ptrs = (char**)malloc(sizeof(char*) * old_row->field_count);
            if (!field_data_ptrs) {
                for (size_t j = 0; j < row_idx; j++) {
                    free(new_field_data_ptrs[j]);
                }
                free(new_field_arrays);
                free(new_field_data_ptrs);
                csv_context_free(new_ctx);
                return TEXT_CSV_E_OOM;
            }

            // Pre-allocate all field data blocks
            for (size_t i = 0; i < old_row->field_count; i++) {
                csv_table_field* old_field = &old_row->fields[i];
                field_data_ptrs[i] = NULL;

                if (old_field->length == 0 || old_field->is_in_situ) {
                    continue;  // Empty or in-situ, no allocation needed
                }

                // Check for overflow
                if (old_field->length > SIZE_MAX - 1) {
                    for (size_t j = 0; j < i; j++) {
                        // Field data already allocated, but that's okay (in arena)
                    }
                    free(field_data_ptrs);
                    for (size_t j = 0; j < row_idx; j++) {
                        free(new_field_data_ptrs[j]);
                    }
                    free(new_field_arrays);
                    free(new_field_data_ptrs);
                    csv_context_free(new_ctx);
                    return TEXT_CSV_E_OOM;
                }

                char* field_data = (char*)csv_arena_alloc_for_context(
                    new_ctx, old_field->length + 1, 1
                );
                if (!field_data) {
                    // Free temporary arrays
                    free(field_data_ptrs);
                    for (size_t j = 0; j < row_idx; j++) {
                        free(new_field_data_ptrs[j]);
                    }
                    free(new_field_arrays);
                    free(new_field_data_ptrs);
                    csv_context_free(new_ctx);
                    return TEXT_CSV_E_OOM;
                }
                field_data_ptrs[i] = field_data;
            }
        }
        new_field_data_ptrs[row_idx] = field_data_ptrs;
    }

    // Phase 4: Copy All Data (no allocations, just memory operations)
    for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
        csv_table_row* old_row = &table->rows[row_idx];
        csv_table_row* new_row = &new_rows[row_idx];

        // Skip empty rows
        if (old_row->field_count == 0) {
            new_row->fields = NULL;
            new_row->field_count = 0;
            continue;
        }

        csv_table_field* new_fields = new_field_arrays[row_idx];
        char** field_data_ptrs = new_field_data_ptrs[row_idx];

        // Copy each field
        for (size_t i = 0; i < old_row->field_count; i++) {
            csv_table_field* old_field = &old_row->fields[i];
            csv_table_field* new_field = &new_fields[i];

            // Copy field data
            if (old_field->length == 0) {
                // Empty field - use global constant
                new_field->data = csv_empty_field_string;
                new_field->length = 0;
                new_field->is_in_situ = false;
            } else if (old_field->is_in_situ) {
                // In-situ field: preserve reference to input buffer (caller-owned)
                new_field->data = old_field->data;
                new_field->length = old_field->length;
                new_field->is_in_situ = true;
            } else {
                // Arena-allocated field: copy to pre-allocated block
                char* field_data = field_data_ptrs[i];
                memcpy(field_data, old_field->data, old_field->length);
                field_data[old_field->length] = '\0';
                new_field->data = field_data;
                new_field->length = old_field->length;
                new_field->is_in_situ = false;
            }
        }

        new_row->fields = new_fields;
        new_row->field_count = old_row->field_count;
    }

    // Free temporary arrays (field data pointers)
    for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
        free(new_field_data_ptrs[row_idx]);
    }
    free(new_field_arrays);
    free(new_field_data_ptrs);

    // Save old context for header map copying (need input_buffer reference)
    csv_context* old_ctx = table->ctx;

    // Phase 3 (continued): Pre-allocate Header Map Structures
    csv_header_entry** new_header_map = NULL;
    csv_header_entry** new_entry_ptrs = NULL;  // Temporary array to store all new entries
    char** new_name_ptrs = NULL;  // Temporary array to store all name strings
    size_t total_header_entries = 0;

    if (table->has_header && table->header_map) {
        // Count total header map entries
        for (size_t i = 0; i < table->header_map_size; i++) {
            csv_header_entry* entry = table->header_map[i];
            while (entry) {
                total_header_entries++;
                entry = entry->next;
            }
        }

        // Allocate new header map array (malloc'd, not in arena)
        // Always allocate, even if empty, to replace old map
        new_header_map = (csv_header_entry**)calloc(
            table->header_map_size, sizeof(csv_header_entry*)
        );
        if (!new_header_map) {
            csv_context_free(new_ctx);
            return TEXT_CSV_E_OOM;
        }

        if (total_header_entries > 0) {
            new_entry_ptrs = (csv_header_entry**)malloc(sizeof(csv_header_entry*) * total_header_entries);
            new_name_ptrs = (char**)malloc(sizeof(char*) * total_header_entries);
            if (!new_entry_ptrs || !new_name_ptrs) {
                free(new_header_map);
                free(new_entry_ptrs);
                free(new_name_ptrs);
                csv_context_free(new_ctx);
                return TEXT_CSV_E_OOM;
            }

            // Pre-allocate all header map entries and name strings
            size_t entry_idx = 0;
            for (size_t i = 0; i < table->header_map_size; i++) {
                csv_header_entry* old_entry = table->header_map[i];

                while (old_entry) {
                    // Allocate new entry in new arena
                    csv_header_entry* new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
                        new_ctx, sizeof(csv_header_entry), 8
                    );
                    if (!new_entry) {
                        // Free temporary arrays
                        free(new_header_map);
                        free(new_entry_ptrs);
                        free(new_name_ptrs);
                        csv_context_free(new_ctx);
                        return TEXT_CSV_E_OOM;
                    }
                    new_entry_ptrs[entry_idx] = new_entry;
                    new_name_ptrs[entry_idx] = NULL;

                    // Pre-allocate name string if needed
                    if (old_entry->name_len == 0) {
                        // Empty name - will use csv_empty_field_string
                    } else {
                        // Check if name is in-situ (points to input buffer)
                        bool name_is_in_situ = false;
                        if (old_ctx->input_buffer && old_entry->name) {
                            const char* input_start = old_ctx->input_buffer;
                            const char* input_end = input_start + old_ctx->input_buffer_len;
                            if (old_entry->name >= input_start && old_entry->name < input_end) {
                                name_is_in_situ = true;
                            }
                        }

                        if (!name_is_in_situ) {
                            // Copy name to new arena
                            // Check for overflow
                            if (old_entry->name_len > SIZE_MAX - 1) {
                                free(new_header_map);
                                free(new_entry_ptrs);
                                free(new_name_ptrs);
                                csv_context_free(new_ctx);
                                return TEXT_CSV_E_OOM;
                            }
                            char* name_data = (char*)csv_arena_alloc_for_context(
                                new_ctx, old_entry->name_len + 1, 1
                            );
                            if (!name_data) {
                                free(new_header_map);
                                free(new_entry_ptrs);
                                free(new_name_ptrs);
                                csv_context_free(new_ctx);
                                return TEXT_CSV_E_OOM;
                            }
                            new_name_ptrs[entry_idx] = name_data;
                        }
                    }

                    entry_idx++;
                    old_entry = old_entry->next;
                }
            }
        }
    }

    // Phase 4 (continued): Copy Header Map Data
    if (table->has_header && table->header_map && total_header_entries > 0) {
        size_t entry_idx = 0;
        for (size_t i = 0; i < table->header_map_size; i++) {
            csv_header_entry* old_entry = table->header_map[i];
            csv_header_entry** new_chain = &new_header_map[i];

            while (old_entry) {
                csv_header_entry* new_entry = new_entry_ptrs[entry_idx];
                char* name_data = new_name_ptrs[entry_idx];

                // Copy name string
                if (old_entry->name_len == 0) {
                    new_entry->name = csv_empty_field_string;
                } else {
                    // Check if name is in-situ
                    bool name_is_in_situ = false;
                    if (old_ctx->input_buffer && old_entry->name) {
                        const char* input_start = old_ctx->input_buffer;
                        const char* input_end = input_start + old_ctx->input_buffer_len;
                        if (old_entry->name >= input_start && old_entry->name < input_end) {
                            name_is_in_situ = true;
                        }
                    }

                    if (name_is_in_situ) {
                        // Preserve in-situ reference
                        new_entry->name = old_entry->name;
                    } else {
                        // Copy name to pre-allocated block
                        memcpy(name_data, old_entry->name, old_entry->name_len);
                        name_data[old_entry->name_len] = '\0';
                        new_entry->name = name_data;
                    }
                }

                new_entry->name_len = old_entry->name_len;
                new_entry->index = old_entry->index;
                new_entry->next = NULL;

                // Add to hash chain
                if (*new_chain == NULL) {
                    *new_chain = new_entry;
                } else {
                    // Find end of chain
                    csv_header_entry* chain_end = *new_chain;
                    while (chain_end->next) {
                        chain_end = chain_end->next;
                    }
                    chain_end->next = new_entry;
                }

                entry_idx++;
                old_entry = old_entry->next;
            }
        }

        // Free temporary arrays
        free(new_entry_ptrs);
        free(new_name_ptrs);
    }

    // Phase 5: Atomic Context Switch
    // Only after all allocations and copies succeed:
    // 1. Preserve input buffer reference (for in-situ mode, caller-owned)
    new_ctx->input_buffer = old_ctx->input_buffer;
    new_ctx->input_buffer_len = old_ctx->input_buffer_len;

    // 2. Atomically update table structure
    table->ctx = new_ctx;
    table->rows = new_rows;
    if (new_header_map) {
        // Free old header map array before updating pointer
        free(table->header_map);
        table->header_map = new_header_map;
    }

    // 3. Free old context (which frees old arena)
    // Note: This frees all old structures in the old arena:
    // - Old rows array (even if it was reallocated/grown, all old versions
    //   are in arena blocks that are tracked and freed)
    // - Old field arrays
    // - Old field data (non-in-situ fields)
    // - Old header map entries
    // The arena tracks blocks, not individual allocations, so overwriting
    // pointers (like when rows array grows) doesn't cause leaks - all blocks
    // in the arena's linked list are freed.
    // Note: input_buffer is caller-owned, so we don't free it
    csv_context_free(old_ctx);

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_table_clear(text_csv_table* table) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Phase 1: Save Original State
    // Save original row_count to allow rollback if compaction fails
    size_t original_row_count = table->row_count;

    // Phase 2: Update Row Count
    // If table has headers, keep the header row (set row_count to 1)
    // Otherwise, set row_count to 0 (all rows cleared)
    // This is a simple assignment, no allocation, so it's safe
    if (table->has_header) {
        table->row_count = 1;  // Keep header row at index 0
    } else {
        table->row_count = 0;  // Clear all rows
    }

    // Keep row_capacity (for efficiency)
    // Keep column_count (table structure preserved)
    // Keep header_map (if present, table structure preserved)

    // Phase 3: Compact Table
    // Compact table to free memory from cleared data rows
    // If compaction fails, the table is still valid (just has unused memory)
    // We can either restore the original state or make compaction failure non-fatal
    // For atomicity, we'll restore the original state if compaction fails
    text_csv_status compact_status = text_csv_table_compact(table);
    if (compact_status != TEXT_CSV_OK) {
        // Restore original row_count if compaction failed
        table->row_count = original_row_count;
        return compact_status;
    }

    return TEXT_CSV_OK;
}

TEXT_API text_csv_table* text_csv_clone(const text_csv_table* source) {
    // Validate inputs
    if (!source) {
        return NULL;
    }

    // Calculate total size needed for clone
    // This allows us to pre-allocate a single block if possible
    size_t total_size = 0;

    // Table structure (aligned to 8 bytes)
    size_t table_size = sizeof(text_csv_table);
    size_t table_aligned = (table_size + 7) & ~7;  // Align to 8
    total_size = table_aligned;

    // Rows array (aligned to 8 bytes)
    size_t rows_array_size = sizeof(csv_table_row) * source->row_capacity;
    size_t rows_array_aligned = (rows_array_size + 7) & ~7;  // Align to 8
    if (total_size > SIZE_MAX - rows_array_aligned) {
        return NULL;
    }
    total_size += rows_array_aligned;

    // Calculate size for all rows and fields
    for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
        csv_table_row* old_row = &source->rows[row_idx];

        if (old_row->field_count == 0) {
            continue;
        }

        // Field array (aligned to 8 bytes)
        size_t field_array_size = sizeof(csv_table_field) * old_row->field_count;
        size_t field_array_aligned = (field_array_size + 7) & ~7;
        if (total_size > SIZE_MAX - field_array_aligned) {
            return NULL;
        }
        total_size += field_array_aligned;

        // Field data (copy ALL fields, including in-situ ones)
        for (size_t i = 0; i < old_row->field_count; i++) {
            csv_table_field* old_field = &old_row->fields[i];

            // Skip empty fields (use global constant)
            if (old_field->length == 0) {
                continue;
            }

            // Field data: length + 1 (null terminator), aligned to 1 byte
            // Note: We copy in-situ fields too (unlike compact which preserves them)
            size_t field_data_size = old_field->length + 1;
            if (total_size > SIZE_MAX - field_data_size) {
                return NULL;
            }
            total_size += field_data_size;
        }
    }

    // Header map entries (if present)
    if (source->has_header && source->header_map) {
        for (size_t i = 0; i < source->header_map_size; i++) {
            csv_header_entry* entry = source->header_map[i];
            while (entry) {
                // Entry structure (aligned to 8 bytes)
                size_t entry_size = sizeof(csv_header_entry);
                size_t entry_aligned = (entry_size + 7) & ~7;
                if (total_size > SIZE_MAX - entry_aligned) {
                    return NULL;
                }
                total_size += entry_aligned;

                // Entry name (copy ALL names, including in-situ ones)
                if (entry->name_len > 0) {
                    size_t name_size = entry->name_len + 1;
                    if (total_size > SIZE_MAX - name_size) {
                        return NULL;
                    }
                    total_size += name_size;
                }

                entry = entry->next;
            }
        }
    }

    // Add some overhead for alignment and safety margin (10% or 1KB, whichever is larger)
    size_t overhead = total_size / 10;
    if (overhead < 1024) {
        overhead = 1024;
    }
    if (total_size > SIZE_MAX - overhead) {
        return NULL;
    }
    total_size += overhead;

    // Create new context/arena with calculated block size
    csv_context* new_ctx = csv_context_new_with_block_size(total_size);
    if (!new_ctx) {
        return NULL;
    }

    // Allocate new table structure (not in arena, use malloc)
    text_csv_table* new_table = (text_csv_table*)malloc(sizeof(text_csv_table));
    if (!new_table) {
        csv_context_free(new_ctx);
        return NULL;
    }

    // Initialize table structure
    new_table->ctx = new_ctx;
    new_table->row_count = source->row_count;
    new_table->row_capacity = source->row_capacity;
    new_table->column_count = source->column_count;
    new_table->has_header = source->has_header;
    new_table->header_map = NULL;
    new_table->header_map_size = source->header_map_size;

    // Allocate new rows array in new arena
    csv_table_row* new_rows = (csv_table_row*)csv_arena_alloc_for_context(
        new_ctx, sizeof(csv_table_row) * source->row_capacity, 8
    );
    if (!new_rows) {
        free(new_table);
        csv_context_free(new_ctx);
        return NULL;
    }

    // Initialize new rows array (zero out unused entries)
    memset(new_rows, 0, sizeof(csv_table_row) * source->row_capacity);
    new_table->rows = new_rows;

    // Pre-allocate all field arrays and field data
    // Store pointers in temporary arrays
    csv_table_field** new_field_arrays = NULL;
    char*** new_field_data_ptrs = NULL;  // Array of arrays of char* pointers
    if (source->row_count > 0) {
        new_field_arrays = (csv_table_field**)malloc(sizeof(csv_table_field*) * source->row_count);
        new_field_data_ptrs = (char***)malloc(sizeof(char**) * source->row_count);
        if (!new_field_arrays || !new_field_data_ptrs) {
            free(new_field_arrays);
            free(new_field_data_ptrs);
            free(new_table);
            csv_context_free(new_ctx);
            return NULL;
        }
    }

    // Pre-allocate all field arrays and field data blocks
    for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
        csv_table_row* old_row = &source->rows[row_idx];
        new_field_arrays[row_idx] = NULL;
        new_field_data_ptrs[row_idx] = NULL;

        // Skip empty rows
        if (old_row->field_count == 0) {
            continue;
        }

        // Allocate field array in new arena
        csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
            new_ctx, sizeof(csv_table_field) * old_row->field_count, 8
        );
        if (!new_fields) {
            // Free temporary arrays
            for (size_t j = 0; j < row_idx; j++) {
                free(new_field_data_ptrs[j]);
            }
            free(new_field_arrays);
            free(new_field_data_ptrs);
            free(new_table);
            csv_context_free(new_ctx);
            return NULL;
        }
        new_field_arrays[row_idx] = new_fields;

        // Pre-allocate field data for ALL non-empty fields (including in-situ)
        char** field_data_ptrs = NULL;
        size_t non_empty_count = 0;
        for (size_t i = 0; i < old_row->field_count; i++) {
            csv_table_field* old_field = &old_row->fields[i];
            if (old_field->length > 0) {
                non_empty_count++;
            }
        }

        if (non_empty_count > 0) {
            field_data_ptrs = (char**)malloc(sizeof(char*) * old_row->field_count);
            if (!field_data_ptrs) {
                for (size_t j = 0; j < row_idx; j++) {
                    free(new_field_data_ptrs[j]);
                }
                free(new_field_arrays);
                free(new_field_data_ptrs);
                free(new_table);
                csv_context_free(new_ctx);
                return NULL;
            }

            // Pre-allocate all field data blocks
            for (size_t i = 0; i < old_row->field_count; i++) {
                csv_table_field* old_field = &old_row->fields[i];
                field_data_ptrs[i] = NULL;

                if (old_field->length == 0) {
                    continue;  // Empty, no allocation needed
                }

                // Check for overflow
                if (old_field->length > SIZE_MAX - 1) {
                    free(field_data_ptrs);
                    for (size_t j = 0; j < row_idx; j++) {
                        free(new_field_data_ptrs[j]);
                    }
                    free(new_field_arrays);
                    free(new_field_data_ptrs);
                    free(new_table);
                    csv_context_free(new_ctx);
                    return NULL;
                }

                char* field_data = (char*)csv_arena_alloc_for_context(
                    new_ctx, old_field->length + 1, 1
                );
                if (!field_data) {
                    // Free temporary arrays
                    free(field_data_ptrs);
                    for (size_t j = 0; j < row_idx; j++) {
                        free(new_field_data_ptrs[j]);
                    }
                    free(new_field_arrays);
                    free(new_field_data_ptrs);
                    free(new_table);
                    csv_context_free(new_ctx);
                    return NULL;
                }
                field_data_ptrs[i] = field_data;
            }
        }
        new_field_data_ptrs[row_idx] = field_data_ptrs;
    }

    // Copy All Data (no allocations, just memory operations)
    for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
        csv_table_row* old_row = &source->rows[row_idx];
        csv_table_row* new_row = &new_rows[row_idx];

        // Skip empty rows
        if (old_row->field_count == 0) {
            new_row->fields = NULL;
            new_row->field_count = 0;
            continue;
        }

        csv_table_field* new_fields = new_field_arrays[row_idx];
        char** field_data_ptrs = new_field_data_ptrs[row_idx];

        // Copy each field
        for (size_t i = 0; i < old_row->field_count; i++) {
            csv_table_field* old_field = &old_row->fields[i];
            csv_table_field* new_field = &new_fields[i];

            // Copy field data
            if (old_field->length == 0) {
                // Empty field - use global constant
                new_field->data = csv_empty_field_string;
                new_field->length = 0;
                new_field->is_in_situ = false;
            } else {
                // Copy field data to pre-allocated block (including in-situ fields)
                char* field_data = field_data_ptrs[i];
                memcpy(field_data, old_field->data, old_field->length);
                field_data[old_field->length] = '\0';
                new_field->data = field_data;
                new_field->length = old_field->length;
                new_field->is_in_situ = false;  // All fields in clone are in arena, not in-situ
            }
        }

        new_row->fields = new_fields;
        new_row->field_count = old_row->field_count;
    }

    // Free temporary arrays (field data pointers)
    for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
        free(new_field_data_ptrs[row_idx]);
    }
    free(new_field_arrays);
    free(new_field_data_ptrs);

    // Pre-allocate Header Map Structures
    csv_header_entry** new_header_map = NULL;
    csv_header_entry** new_entry_ptrs = NULL;  // Temporary array to store all new entries
    char** new_name_ptrs = NULL;  // Temporary array to store all name strings
    size_t total_header_entries = 0;

    if (source->has_header && source->header_map) {
        // Count total header map entries
        for (size_t i = 0; i < source->header_map_size; i++) {
            csv_header_entry* entry = source->header_map[i];
            while (entry) {
                total_header_entries++;
                entry = entry->next;
            }
        }

        // Allocate new header map array (malloc'd, not in arena)
        new_header_map = (csv_header_entry**)calloc(
            source->header_map_size, sizeof(csv_header_entry*)
        );
        if (!new_header_map) {
            free(new_table);
            csv_context_free(new_ctx);
            return NULL;
        }

        if (total_header_entries > 0) {
            new_entry_ptrs = (csv_header_entry**)malloc(sizeof(csv_header_entry*) * total_header_entries);
            new_name_ptrs = (char**)malloc(sizeof(char*) * total_header_entries);
            if (!new_entry_ptrs || !new_name_ptrs) {
                free(new_header_map);
                free(new_entry_ptrs);
                free(new_name_ptrs);
                free(new_table);
                csv_context_free(new_ctx);
                return NULL;
            }

            // Pre-allocate all header map entries and name strings
            size_t entry_idx = 0;
            for (size_t i = 0; i < source->header_map_size; i++) {
                csv_header_entry* old_entry = source->header_map[i];

                while (old_entry) {
                    // Allocate new entry in new arena
                    csv_header_entry* new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
                        new_ctx, sizeof(csv_header_entry), 8
                    );
                    if (!new_entry) {
                        // Free temporary arrays
                        free(new_header_map);
                        free(new_entry_ptrs);
                        free(new_name_ptrs);
                        free(new_table);
                        csv_context_free(new_ctx);
                        return NULL;
                    }
                    new_entry_ptrs[entry_idx] = new_entry;
                    new_name_ptrs[entry_idx] = NULL;

                    // Pre-allocate name string if needed
                    if (old_entry->name_len == 0) {
                        // Empty name - will use csv_empty_field_string
                    } else {
                        // Copy name to new arena (including in-situ names)
                        // Check for overflow
                        if (old_entry->name_len > SIZE_MAX - 1) {
                            free(new_header_map);
                            free(new_entry_ptrs);
                            free(new_name_ptrs);
                            free(new_table);
                            csv_context_free(new_ctx);
                            return NULL;
                        }
                        char* name_data = (char*)csv_arena_alloc_for_context(
                            new_ctx, old_entry->name_len + 1, 1
                        );
                        if (!name_data) {
                            free(new_header_map);
                            free(new_entry_ptrs);
                            free(new_name_ptrs);
                            free(new_table);
                            csv_context_free(new_ctx);
                            return NULL;
                        }
                        new_name_ptrs[entry_idx] = name_data;
                    }

                    entry_idx++;
                    old_entry = old_entry->next;
                }
            }
        }
    }

    // Copy Header Map Data
    if (source->has_header && source->header_map && total_header_entries > 0) {
        size_t entry_idx = 0;
        for (size_t i = 0; i < source->header_map_size; i++) {
            csv_header_entry* old_entry = source->header_map[i];
            csv_header_entry** new_chain = &new_header_map[i];

            while (old_entry) {
                csv_header_entry* new_entry = new_entry_ptrs[entry_idx];
                char* name_data = new_name_ptrs[entry_idx];

                // Copy name string
                if (old_entry->name_len == 0) {
                    new_entry->name = csv_empty_field_string;
                } else {
                    // Copy name to pre-allocated block (including in-situ names)
                    memcpy(name_data, old_entry->name, old_entry->name_len);
                    name_data[old_entry->name_len] = '\0';
                    new_entry->name = name_data;
                }

                new_entry->name_len = old_entry->name_len;
                new_entry->index = old_entry->index;
                new_entry->next = NULL;

                // Add to hash chain
                if (*new_chain == NULL) {
                    *new_chain = new_entry;
                } else {
                    // Find end of chain
                    csv_header_entry* chain_end = *new_chain;
                    while (chain_end->next) {
                        chain_end = chain_end->next;
                    }
                    chain_end->next = new_entry;
                }

                entry_idx++;
                old_entry = old_entry->next;
            }
        }

        // Free temporary arrays
        free(new_entry_ptrs);
        free(new_name_ptrs);
    }

    // Set header map in new table
    new_table->header_map = new_header_map;

    // Note: input_buffer is NOT copied - cloned table is independent
    // and doesn't reference the original input buffer
    new_ctx->input_buffer = NULL;
    new_ctx->input_buffer_len = 0;

    return new_table;
}

/**
 * @brief Reindex header map entries by incrementing indices
 *
 * Increments the index of all header map entries with index >= start_index.
 * Used when inserting columns to shift existing column indices.
 *
 * @param table Table (must not be NULL)
 * @param start_index Starting index - entries with index >= this are incremented
 */
static void csv_header_map_reindex_increment(
    text_csv_table* table,
    size_t start_index
) {
    if (!table->has_header || !table->header_map || table->row_count == 0) {
        return;
    }

    // Reindex all header map entries with index >= start_index (increment by 1)
    for (size_t i = 0; i < table->header_map_size; i++) {
        csv_header_entry* entry = table->header_map[i];
        while (entry) {
            if (entry->index >= start_index) {
                entry->index++;
            }
            entry = entry->next;
        }
    }
}

/**
 * @brief Reindex header map entries by decrementing indices
 *
 * Decrements the index of all header map entries with index > start_index.
 * Used when removing columns to shift remaining column indices.
 *
 * @param table Table (must not be NULL)
 * @param start_index Starting index - entries with index > this are decremented
 */
static void csv_header_map_reindex_decrement(
    text_csv_table* table,
    size_t start_index
) {
    if (!table->has_header || !table->header_map || table->row_count == 0) {
        return;
    }

    // Reindex all header map entries with index > start_index (decrement by 1)
    for (size_t i = 0; i < table->header_map_size; i++) {
        csv_header_entry* entry = table->header_map[i];
        while (entry) {
            if (entry->index > start_index) {
                entry->index--;
            }
            entry = entry->next;
        }
    }
}

/**
 * @brief Internal helper for column append/insert operations
 *
 * Handles common logic for column operations including validation,
 * field array pre-allocation, header field data allocation, header map
 * entry allocation, and data copying. Parameterized by insertion index
 * (use SIZE_MAX for append operations). Can optionally handle provided
 * values for all fields (when values is not NULL).
 *
 * @param table Table (must not be NULL)
 * @param col_idx Column index where to insert (SIZE_MAX for append, must be <= column_count for insert)
 * @param header_name Header name for the new column (required if table has headers when values is NULL, ignored otherwise)
 * @param header_name_len Length of header name, or 0 if null-terminated
 * @param values Optional array of field values (NULL for empty fields, must match row count if provided)
 * @param value_lengths Optional array of value lengths, or NULL if all are null-terminated
 * @param new_field_arrays_out Output parameter for allocated field arrays (caller must free with free())
 * @param old_field_counts_out Output parameter for old field counts (caller must free with free())
 * @param rows_to_modify_out Output parameter for number of rows to modify
 * @param header_field_data_out Output parameter for header field data (or NULL if empty)
 * @param header_field_data_len_out Output parameter for header field data length
 * @param new_entry_out Output parameter for header map entry (or NULL if no headers)
 * @param header_hash_out Output parameter for header hash (if headers present)
 * @param new_header_fields_out Output parameter for new header fields array (or NULL if no headers)
 * @param old_header_field_count_out Output parameter for old header field count
 * @param field_data_array_out Output parameter for allocated field data array (caller must free with free(), NULL if values is NULL)
 * @param field_data_lengths_out Output parameter for field data lengths (caller must free with free(), NULL if values is NULL)
 * @return TEXT_CSV_OK on success, error code on failure
 */
static text_csv_status csv_column_operation_internal(
    text_csv_table* table,
    size_t col_idx,
    const char* header_name,
    size_t header_name_len,
    const char* const* values,
    const size_t* value_lengths,
    csv_table_field*** new_field_arrays_out,
    size_t** old_field_counts_out,
    size_t* rows_to_modify_out,
    char** header_field_data_out,
    size_t* header_field_data_len_out,
    csv_header_entry** new_entry_out,
    size_t* header_hash_out,
    csv_table_field** new_header_fields_out,
    size_t* old_header_field_count_out,
    char*** field_data_array_out,
    size_t** field_data_lengths_out
) {
    // Determine if this is append (col_idx == SIZE_MAX) or insert
    bool is_append = (col_idx == SIZE_MAX);

    // Mark unused parameters (values handling to be implemented for _with_values variants)
    (void)values;
    (void)value_lengths;

    // Validate col_idx for insert operations
    if (!is_append && col_idx > table->column_count) {
        return TEXT_CSV_E_INVALID;
    }

    // If table is empty: just update column count (no rows to modify)
    if (table->row_count == 0) {
        // Check for overflow when incrementing column_count
        if (table->column_count == SIZE_MAX) {
            return TEXT_CSV_E_OOM;
        }
        table->column_count++;
        *new_field_arrays_out = NULL;
        *old_field_counts_out = NULL;
        *rows_to_modify_out = 0;
        *header_field_data_out = NULL;
        *header_field_data_len_out = 0;
        *new_entry_out = NULL;
        *header_hash_out = 0;
        *new_header_fields_out = NULL;
        *old_header_field_count_out = 0;
        *field_data_array_out = NULL;
        *field_data_lengths_out = NULL;
        return TEXT_CSV_OK;
    }

    // Handle headers if present
    size_t name_len = 0;
    if (table->has_header && table->header_map) {
        // Validate header_name is not NULL when table has headers
        if (!header_name) {
            return TEXT_CSV_E_INVALID;
        }

        // Calculate header name length if not provided
        name_len = header_name_len;
        if (name_len == 0) {
            name_len = strlen(header_name);
        }

        // Check for duplicate header name if uniqueness is required
        text_csv_status uniqueness_status = csv_check_header_uniqueness(
            table, header_name, name_len, SIZE_MAX  // Don't exclude any column
        );
        if (uniqueness_status != TEXT_CSV_OK) {
            return uniqueness_status;
        }
    }

    // Phase 2: Pre-allocate All New Field Arrays
    // Calculate how many rows need new field arrays (all data rows + header row if present)
    // Determine start row index (skip header row if present)
    size_t start_row_idx = 0;
    if (table->has_header && table->row_count > 0) {
        start_row_idx = 1;  // Skip header row at index 0
    }

    // Count rows that need new field arrays
    size_t rows_to_modify = table->row_count - start_row_idx;  // Data rows
    if (table->has_header && table->row_count > 0) {
        rows_to_modify++;  // Include header row
    }

    // Pre-allocate all new field arrays before updating any row structures
    csv_table_field** new_field_arrays = NULL;
    size_t* old_field_counts = NULL;
    if (rows_to_modify > 0) {
        new_field_arrays = (csv_table_field**)malloc(sizeof(csv_table_field*) * rows_to_modify);
        old_field_counts = (size_t*)malloc(sizeof(size_t) * rows_to_modify);
        if (!new_field_arrays || !old_field_counts) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }
    }

    // Pre-allocate field arrays for data rows
    size_t array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = row->field_count;
        size_t new_field_count = old_field_count + 1;

        // Check for overflow
        if (new_field_count < old_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        // Validate col_idx is within bounds for insert operations
        if (!is_append && col_idx > old_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_INVALID;
        }

        old_field_counts[array_idx] = old_field_count;

        // Allocate new field array with one more field
        csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_field_count, 8
        );
        if (!new_fields) {
            // Free temporary arrays
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        new_field_arrays[array_idx] = new_fields;
        array_idx++;
    }

    // Phase 3: Allocate Header Field Data (if needed)
    char* header_field_data = NULL;
    size_t header_field_data_len = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        // Validate col_idx is within bounds for header row (insert operations)
        if (!is_append) {
            csv_table_row* header_row = &table->rows[0];
            if (col_idx > header_row->field_count) {
                free(new_field_arrays);
                free(old_field_counts);
                return TEXT_CSV_E_INVALID;
            }
        }

        if (name_len == 0) {
            // Empty header - will use csv_empty_field_string
            header_field_data = NULL;
            header_field_data_len = 0;
        } else {
            // Allocate header name field data
            if (name_len > SIZE_MAX - 1) {
                free(new_field_arrays);
                free(old_field_counts);
                return TEXT_CSV_E_OOM;
            }
            header_field_data = (char*)csv_arena_alloc_for_context(
                table->ctx, name_len + 1, 1
            );
            if (!header_field_data) {
                free(new_field_arrays);
                free(old_field_counts);
                return TEXT_CSV_E_OOM;
            }
            memcpy(header_field_data, header_name, name_len);
            header_field_data[name_len] = '\0';
            header_field_data_len = name_len;
        }
    }

    // Phase 4: Allocate Header Map Entry (if needed)
    csv_header_entry* new_entry = NULL;
    size_t header_hash = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        header_hash = csv_header_hash(header_name, name_len, table->header_map_size);
        new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_header_entry), 8
        );
        if (!new_entry) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }
    }

    // Phase 5: Copy Data to New Field Arrays
    // Copy data rows
    array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        if (is_append) {
            // Append: copy all existing fields, add empty field at end
            if (old_field_count > 0) {
                memcpy(new_fields, row->fields, sizeof(csv_table_field) * old_field_count);
            }
            csv_setup_empty_field(&new_fields[old_field_count]);
        } else {
            // Insert: copy fields before insertion point, add empty field, copy fields after
            if (col_idx > 0) {
                memcpy(new_fields, row->fields, sizeof(csv_table_field) * col_idx);
            }
            csv_setup_empty_field(&new_fields[col_idx]);
            if (col_idx < old_field_count) {
                memcpy(new_fields + col_idx + 1, row->fields + col_idx,
                       sizeof(csv_table_field) * (old_field_count - col_idx));
            }
        }

        array_idx++;
    }

    // Copy header row (if present)
    csv_table_field* new_header_fields = NULL;
    size_t old_header_field_count = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        old_header_field_count = header_row->field_count;
        size_t new_header_field_count = old_header_field_count + 1;

        // Check for overflow
        if (new_header_field_count < old_header_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        // Allocate new field array for header row
        new_header_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_header_field_count, 8
        );
        if (!new_header_fields) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        if (is_append) {
            // Append: copy all existing header fields, add new header field at end
            if (old_header_field_count > 0) {
                memcpy(new_header_fields, header_row->fields, sizeof(csv_table_field) * old_header_field_count);
            }
            csv_table_field* new_header_field = &new_header_fields[old_header_field_count];
            if (name_len == 0) {
                csv_setup_empty_field(new_header_field);
            } else {
                new_header_field->data = header_field_data;
                new_header_field->length = header_field_data_len;
                new_header_field->is_in_situ = false;
            }
        } else {
            // Insert: copy header fields before insertion point, add new header field, copy after
            if (col_idx > 0) {
                memcpy(new_header_fields, header_row->fields, sizeof(csv_table_field) * col_idx);
            }
            csv_table_field* new_header_field = &new_header_fields[col_idx];
            if (name_len == 0) {
                csv_setup_empty_field(new_header_field);
            } else {
                new_header_field->data = header_field_data;
                new_header_field->length = header_field_data_len;
                new_header_field->is_in_situ = false;
            }
            if (col_idx < old_header_field_count) {
                memcpy(new_header_fields + col_idx + 1, header_row->fields + col_idx,
                       sizeof(csv_table_field) * (old_header_field_count - col_idx));
            }
        }
    }

    // Initialize field data arrays (will be set if values provided)
    *field_data_array_out = NULL;
    *field_data_lengths_out = NULL;

    // Set output parameters
    *new_field_arrays_out = new_field_arrays;
    *old_field_counts_out = old_field_counts;
    *rows_to_modify_out = rows_to_modify;
    *header_field_data_out = header_field_data;
    *header_field_data_len_out = header_field_data_len;
    *new_entry_out = new_entry;
    *header_hash_out = header_hash;
    *new_header_fields_out = new_header_fields;
    *old_header_field_count_out = old_header_field_count;

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_column_append(
    text_csv_table* table,
    const char* header_name,
    size_t header_name_len
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Use helper function for common column operation logic (SIZE_MAX = append)
    csv_table_field** new_field_arrays = NULL;
    size_t* old_field_counts = NULL;
    size_t rows_to_modify = 0;
    char* header_field_data = NULL;
    size_t header_field_data_len = 0;
    csv_header_entry* new_entry = NULL;
    size_t header_hash = 0;
    csv_table_field* new_header_fields = NULL;
    size_t old_header_field_count = 0;

    char** field_data_array = NULL;
    size_t* field_data_lengths = NULL;
    text_csv_status status = csv_column_operation_internal(
        table, SIZE_MAX, header_name, header_name_len,
        NULL, NULL,  // No values - empty fields
        &new_field_arrays, &old_field_counts, &rows_to_modify,
        &header_field_data, &header_field_data_len,
        &new_entry, &header_hash,
        &new_header_fields, &old_header_field_count,
        &field_data_array, &field_data_lengths
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // If table was empty, helper already updated column_count
    if (rows_to_modify == 0) {
        return TEXT_CSV_OK;
    }

    // Phase 6: Atomic State Update
    // Only after all allocations and copies succeed:
    // 1. Update column_count
    if (table->column_count == SIZE_MAX) {
        free(new_field_arrays);
        free(old_field_counts);
        return TEXT_CSV_E_OOM;
    }
    size_t new_column_index = table->column_count;
    table->column_count++;

    // 2. Update all data row structures
    size_t start_row_idx = 0;
    if (table->has_header && table->row_count > 0) {
        start_row_idx = 1;  // Skip header row at index 0
    }
    size_t array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        row->fields = new_fields;
        row->field_count = old_field_count + 1;

        array_idx++;
    }

    // 3. Update header row structure (if present)
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        header_row->fields = new_header_fields;
        header_row->field_count = old_header_field_count + 1;

        // 4. Add header map entry
        new_entry->name = new_header_fields[old_header_field_count].data;
        new_entry->name_len = new_header_fields[old_header_field_count].length;
        new_entry->index = new_column_index;
        new_entry->next = table->header_map[header_hash];
        table->header_map[header_hash] = new_entry;
    }

    // Free temporary arrays
    free(new_field_arrays);
    free(old_field_counts);

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_column_append_with_values(
    text_csv_table* table,
    const char* header_name,
    size_t header_name_len,
    const char* const* values,
    const size_t* value_lengths
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Empty table check: if row_count == 0, return error
    if (table->row_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Determine expected value count
    // Note: table->row_count already includes the header row (if present) in the internal structure
    // So we don't need to add 1 when has_header is true
    size_t expected_value_count = table->row_count;

    // Handle empty column case (values is NULL)
    bool is_empty_column = (values == NULL);
    if (is_empty_column) {
        // If table has headers, header_name must not be NULL
        if (table->has_header && table->header_map && !header_name) {
            return TEXT_CSV_E_INVALID;
        }
        // If table has no headers, header_name is ignored
    } else {
        // Validate value count matches expected count
        // We need to count the values - iterate until we find NULL or count matches expected
        size_t actual_value_count = 0;
        while (actual_value_count < expected_value_count && values[actual_value_count] != NULL) {
            actual_value_count++;
        }
        // Check if we stopped because we hit NULL (too few) or reached expected count
        if (actual_value_count != expected_value_count) {
            return TEXT_CSV_E_INVALID;
        }
    }

    // Determine header value and name for header map
    const char* header_value = NULL;
    size_t header_value_len = 0;
    const char* header_map_name = NULL;
    size_t header_map_name_len = 0;

    if (table->has_header && table->header_map) {
        if (is_empty_column) {
            // Empty column: use header_name
            header_value = header_name;
            header_value_len = header_name_len;
            if (header_value_len == 0 && header_value) {
                header_value_len = strlen(header_value);
            }
            header_map_name = header_name;
            header_map_name_len = header_value_len;
        } else {
            // Use values[0] for both header field and header map entry
            header_value = values[0];
            if (value_lengths && value_lengths[0] > 0) {
                header_value_len = value_lengths[0];
            } else if (header_value) {
                header_value_len = strlen(header_value);
            }
            header_map_name = values[0];
            header_map_name_len = header_value_len;
        }

        // Check for duplicate header name if uniqueness is required
        text_csv_status uniqueness_status = csv_check_header_uniqueness(
            table, header_map_name, header_map_name_len, SIZE_MAX  // Don't exclude any column
        );
        if (uniqueness_status != TEXT_CSV_OK) {
            return uniqueness_status;
        }
    }

    // Phase 2: Pre-allocate All New Field Arrays
    // Calculate how many rows need new field arrays (all data rows + header row if present)
    size_t start_row_idx = 0;
    if (table->has_header && table->row_count > 0) {
        start_row_idx = 1;  // Skip header row at index 0
    }

    // Count rows that need new field arrays
    size_t rows_to_modify = table->row_count - start_row_idx;  // Data rows
    if (table->has_header && table->row_count > 0) {
        rows_to_modify++;  // Include header row
    }

    // Pre-allocate all new field arrays before updating any row structures
    csv_table_field** new_field_arrays = NULL;
    size_t* old_field_counts = NULL;
    if (rows_to_modify > 0) {
        new_field_arrays = (csv_table_field**)malloc(sizeof(csv_table_field*) * rows_to_modify);
        old_field_counts = (size_t*)malloc(sizeof(size_t) * rows_to_modify);
        if (!new_field_arrays || !old_field_counts) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }
    }

    // Pre-allocate field arrays for data rows
    size_t array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = row->field_count;
        size_t new_field_count = old_field_count + 1;

        // Check for overflow
        if (new_field_count < old_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        old_field_counts[array_idx] = old_field_count;

        // Allocate new field array with one more field
        csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_field_count, 8
        );
        if (!new_fields) {
            // Free temporary arrays
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        new_field_arrays[array_idx] = new_fields;
        array_idx++;
    }

    // Phase 3: Pre-allocate All Field Data
    // Allocate all field data before copying to ensure atomicity
    char** field_data_array = NULL;
    size_t* field_data_lengths = NULL;
    if (!is_empty_column && rows_to_modify > 0) {
        field_data_array = (char**)malloc(sizeof(char*) * rows_to_modify);
        field_data_lengths = (size_t*)malloc(sizeof(size_t) * rows_to_modify);
        if (!field_data_array || !field_data_lengths) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }

        // Allocate field data for all rows (header + data rows)
        // Mapping: array_idx maps to values array index
        // - If has_header: array_idx 0 = values[0] (header), array_idx 1..N = values[1..N] (data rows)
        // - If no headers: array_idx 0..N-1 = values[0..N-1] (data rows)
        for (size_t i = 0; i < rows_to_modify; i++) {
            size_t value_idx = i;  // Direct mapping: array_idx == value_idx

            const char* value = values[value_idx];
            size_t value_len = 0;
            if (value_lengths && value_lengths[value_idx] > 0) {
                value_len = value_lengths[value_idx];
            } else if (value) {
                value_len = strlen(value);
            }

            if (value_len == 0) {
                // Empty field - will use csv_empty_field_string
                field_data_array[i] = NULL;
                field_data_lengths[i] = 0;
            } else {
                // Allocate and copy field data
                if (value_len > SIZE_MAX - 1) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                char* field_data = (char*)csv_arena_alloc_for_context(
                    table->ctx, value_len + 1, 1
                );
                if (!field_data) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                memcpy(field_data, value, value_len);
                field_data[value_len] = '\0';
                field_data_array[i] = field_data;
                field_data_lengths[i] = value_len;
            }
        }
    }

    // Phase 4: Determine Header Field Data (if needed)
    // If values is provided, reuse field_data_array[0] for header field
    // Otherwise, allocate separately for empty column case
    char* header_field_data = NULL;
    size_t header_field_data_len = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        if (is_empty_column) {
            // Empty column: allocate header name separately
            if (header_value_len == 0) {
                // Empty header - will use csv_empty_field_string
                header_field_data = NULL;
                header_field_data_len = 0;
            } else {
                // Allocate header name field data
                if (header_value_len > SIZE_MAX - 1) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                header_field_data = (char*)csv_arena_alloc_for_context(
                    table->ctx, header_value_len + 1, 1
                );
                if (!header_field_data) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                memcpy(header_field_data, header_value, header_value_len);
                header_field_data[header_value_len] = '\0';
                header_field_data_len = header_value_len;
            }
        } else {
            // Reuse field_data_array[0] for header field
            header_field_data = field_data_array[0];
            header_field_data_len = field_data_lengths[0];
        }
    }

    // Phase 5: Allocate Header Map Entry (if needed)
    csv_header_entry* new_entry = NULL;
    size_t header_hash = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        header_hash = csv_header_hash(header_map_name, header_map_name_len, table->header_map_size);
        new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_header_entry), 8
        );
        if (!new_entry) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }
    }

    // Phase 6: Copy Data to New Field Arrays
    // Copy data rows (skip header row, it's handled separately)
    array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        // Copy existing fields
        if (old_field_count > 0) {
            memcpy(new_fields, row->fields, sizeof(csv_table_field) * old_field_count);
        }

        // Add new field at the end
        csv_table_field* new_field = &new_fields[old_field_count];
        if (is_empty_column) {
            csv_setup_empty_field(new_field);
        } else {
            // Map array_idx to value index in field_data_array
            // field_data_array was allocated for all rows_to_modify
            // - If has_header: field_data_array[0] = header (values[0]), field_data_array[1..N] = data rows (values[1..N])
            // - If no headers: field_data_array[0..N-1] = data rows (values[0..N-1])
            // array_idx here is for data rows only (header row handled separately)
            // So: value_idx = array_idx + (has_header ? 1 : 0)
            size_t value_idx = table->has_header ? (array_idx + 1) : array_idx;
            if (field_data_array[value_idx] == NULL) {
                csv_setup_empty_field(new_field);
            } else {
                new_field->data = field_data_array[value_idx];
                new_field->length = field_data_lengths[value_idx];
                new_field->is_in_situ = false;
            }
        }

        array_idx++;
    }

    // Copy header row (if present)
    csv_table_field* new_header_fields = NULL;
    size_t old_header_field_count = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        old_header_field_count = header_row->field_count;
        size_t new_header_field_count = old_header_field_count + 1;

        // Check for overflow
        if (new_header_field_count < old_header_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }

        // Allocate new field array for header row
        new_header_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_header_field_count, 8
        );
        if (!new_header_fields) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }

        // Copy existing header fields
        if (old_header_field_count > 0) {
            memcpy(new_header_fields, header_row->fields, sizeof(csv_table_field) * old_header_field_count);
        }

        // Set up new header field
        csv_table_field* new_header_field = &new_header_fields[old_header_field_count];
        if (header_field_data == NULL || header_field_data_len == 0) {
            csv_setup_empty_field(new_header_field);
        } else {
            new_header_field->data = header_field_data;
            new_header_field->length = header_field_data_len;
            new_header_field->is_in_situ = false;
        }
    }

    // Phase 7: Atomic State Update
    // Only after all allocations and copies succeed:
    // 1. Update column_count
    if (table->column_count == SIZE_MAX) {
        free(new_field_arrays);
        free(old_field_counts);
        free(field_data_array);
        free(field_data_lengths);
        return TEXT_CSV_E_OOM;
    }
    size_t new_column_index = table->column_count;
    table->column_count++;

    // 2. Update all data row structures
    array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        row->fields = new_fields;
        row->field_count = old_field_count + 1;

        array_idx++;
    }

    // 3. Update header row structure (if present)
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        header_row->fields = new_header_fields;
        header_row->field_count = old_header_field_count + 1;

        // 4. Add header map entry
        new_entry->name = new_header_fields[old_header_field_count].data;
        new_entry->name_len = new_header_fields[old_header_field_count].length;
        new_entry->index = new_column_index;
        new_entry->next = table->header_map[header_hash];
        table->header_map[header_hash] = new_entry;
    }

    // Free temporary arrays
    free(new_field_arrays);
    free(old_field_counts);
    free(field_data_array);
    free(field_data_lengths);

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_column_insert_with_values(
    text_csv_table* table,
    size_t col_idx,
    const char* header_name,
    size_t header_name_len,
    const char* const* values,
    const size_t* value_lengths
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Validate col_idx (must be <= column count)
    if (col_idx > table->column_count) {
        return TEXT_CSV_E_INVALID;
    }

    // If inserting at end (col_idx == column_count), use append logic
    if (col_idx == table->column_count) {
        return text_csv_column_append_with_values(table, header_name, header_name_len, values, value_lengths);
    }

    // Empty table check: if row_count == 0, return error
    if (table->row_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Determine expected value count
    // Note: table->row_count already includes the header row (if present) in the internal structure
    // So we don't need to add 1 when has_header is true
    size_t expected_value_count = table->row_count;

    // Handle empty column case (values is NULL)
    bool is_empty_column = (values == NULL);
    if (is_empty_column) {
        // If table has headers, header_name must not be NULL
        if (table->has_header && table->header_map && !header_name) {
            return TEXT_CSV_E_INVALID;
        }
        // If table has no headers, header_name is ignored
    } else {
        // Validate value count matches expected count
        size_t actual_value_count = 0;
        while (actual_value_count < expected_value_count && values[actual_value_count] != NULL) {
            actual_value_count++;
        }
        if (actual_value_count != expected_value_count) {
            return TEXT_CSV_E_INVALID;
        }
    }

    // Determine header value and name for header map
    const char* header_value = NULL;
    size_t header_value_len = 0;
    const char* header_map_name = NULL;
    size_t header_map_name_len = 0;

    if (table->has_header && table->header_map) {
        if (is_empty_column) {
            // Empty column: use header_name
            header_value = header_name;
            header_value_len = header_name_len;
            if (header_value_len == 0 && header_value) {
                header_value_len = strlen(header_value);
            }
            header_map_name = header_name;
            header_map_name_len = header_value_len;
        } else {
            // Use values[0] for both header field and header map entry
            header_value = values[0];
            if (value_lengths && value_lengths[0] > 0) {
                header_value_len = value_lengths[0];
            } else if (header_value) {
                header_value_len = strlen(header_value);
            }
            header_map_name = values[0];
            header_map_name_len = header_value_len;
        }

        // Check for duplicate header name if uniqueness is required
        text_csv_status uniqueness_status = csv_check_header_uniqueness(
            table, header_map_name, header_map_name_len, SIZE_MAX  // Don't exclude any column
        );
        if (uniqueness_status != TEXT_CSV_OK) {
            return uniqueness_status;
        }
    }

    // Phase 2: Pre-allocate All New Field Arrays
    size_t start_row_idx = 0;
    if (table->has_header && table->row_count > 0) {
        start_row_idx = 1;  // Skip header row at index 0
    }

    // Count rows that need new field arrays
    size_t rows_to_modify = table->row_count - start_row_idx;  // Data rows
    if (table->has_header && table->row_count > 0) {
        rows_to_modify++;  // Include header row
    }

    // Pre-allocate all new field arrays before updating any row structures
    csv_table_field** new_field_arrays = NULL;
    size_t* old_field_counts = NULL;
    if (rows_to_modify > 0) {
        new_field_arrays = (csv_table_field**)malloc(sizeof(csv_table_field*) * rows_to_modify);
        old_field_counts = (size_t*)malloc(sizeof(size_t) * rows_to_modify);
        if (!new_field_arrays || !old_field_counts) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }
    }

    // Pre-allocate field arrays for data rows
    size_t array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = row->field_count;
        size_t new_field_count = old_field_count + 1;

        // Check for overflow
        if (new_field_count < old_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        old_field_counts[array_idx] = old_field_count;

        // Allocate new field array with one more field
        csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_field_count, 8
        );
        if (!new_fields) {
            free(new_field_arrays);
            free(old_field_counts);
            return TEXT_CSV_E_OOM;
        }

        new_field_arrays[array_idx] = new_fields;
        array_idx++;
    }

    // Phase 3: Pre-allocate All Field Data
    char** field_data_array = NULL;
    size_t* field_data_lengths = NULL;
    if (!is_empty_column && rows_to_modify > 0) {
        field_data_array = (char**)malloc(sizeof(char*) * rows_to_modify);
        field_data_lengths = (size_t*)malloc(sizeof(size_t) * rows_to_modify);
        if (!field_data_array || !field_data_lengths) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }

        // Allocate field data for all rows (header + data rows)
        for (size_t i = 0; i < rows_to_modify; i++) {
            size_t value_idx = i;  // Direct mapping: array_idx == value_idx

            const char* value = values[value_idx];
            size_t value_len = 0;
            if (value_lengths && value_lengths[value_idx] > 0) {
                value_len = value_lengths[value_idx];
            } else if (value) {
                value_len = strlen(value);
            }

            if (value_len == 0) {
                field_data_array[i] = NULL;
                field_data_lengths[i] = 0;
            } else {
                if (value_len > SIZE_MAX - 1) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                char* field_data = (char*)csv_arena_alloc_for_context(
                    table->ctx, value_len + 1, 1
                );
                if (!field_data) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                memcpy(field_data, value, value_len);
                field_data[value_len] = '\0';
                field_data_array[i] = field_data;
                field_data_lengths[i] = value_len;
            }
        }
    }

    // Phase 4: Determine Header Field Data (if needed)
    char* header_field_data = NULL;
    size_t header_field_data_len = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        if (is_empty_column) {
            if (header_value_len == 0) {
                header_field_data = NULL;
                header_field_data_len = 0;
            } else {
                if (header_value_len > SIZE_MAX - 1) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                header_field_data = (char*)csv_arena_alloc_for_context(
                    table->ctx, header_value_len + 1, 1
                );
                if (!header_field_data) {
                    free(new_field_arrays);
                    free(old_field_counts);
                    free(field_data_array);
                    free(field_data_lengths);
                    return TEXT_CSV_E_OOM;
                }
                memcpy(header_field_data, header_value, header_value_len);
                header_field_data[header_value_len] = '\0';
                header_field_data_len = header_value_len;
            }
        } else {
            // Reuse field_data_array[0] for header field
            header_field_data = field_data_array[0];
            header_field_data_len = field_data_lengths[0];
        }
    }

    // Phase 5: Allocate Header Map Entry (if needed)
    csv_header_entry* new_entry = NULL;
    size_t header_hash = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        header_hash = csv_header_hash(header_map_name, header_map_name_len, table->header_map_size);
        new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_header_entry), 8
        );
        if (!new_entry) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }
    }

    // Phase 6: Copy Data to New Field Arrays (with insertion)
    // Copy data rows (skip header row, it's handled separately)
    array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        // Copy fields before insertion point
        if (col_idx > 0) {
            memcpy(new_fields, row->fields, sizeof(csv_table_field) * col_idx);
        }

        // Insert new field at col_idx
        csv_table_field* new_field = &new_fields[col_idx];
        if (is_empty_column) {
            csv_setup_empty_field(new_field);
        } else {
            size_t value_idx = table->has_header ? (array_idx + 1) : array_idx;
            if (field_data_array[value_idx] == NULL) {
                csv_setup_empty_field(new_field);
            } else {
                new_field->data = field_data_array[value_idx];
                new_field->length = field_data_lengths[value_idx];
                new_field->is_in_situ = false;
            }
        }

        // Copy fields after insertion point (shift right)
        if (col_idx < old_field_count) {
            memcpy(new_fields + col_idx + 1, row->fields + col_idx,
                   sizeof(csv_table_field) * (old_field_count - col_idx));
        }

        array_idx++;
    }

    // Copy header row (if present)
    csv_table_field* new_header_fields = NULL;
    size_t old_header_field_count = 0;
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        old_header_field_count = header_row->field_count;
        size_t new_header_field_count = old_header_field_count + 1;

        // Check for overflow
        if (new_header_field_count < old_header_field_count) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }

        // Allocate new field array for header row
        new_header_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_header_field_count, 8
        );
        if (!new_header_fields) {
            free(new_field_arrays);
            free(old_field_counts);
            free(field_data_array);
            free(field_data_lengths);
            return TEXT_CSV_E_OOM;
        }

        // Copy existing header fields before insertion point
        if (col_idx > 0) {
            memcpy(new_header_fields, header_row->fields, sizeof(csv_table_field) * col_idx);
        }

        // Set up new header field at col_idx
        csv_table_field* new_header_field = &new_header_fields[col_idx];
        if (header_field_data == NULL || header_field_data_len == 0) {
            csv_setup_empty_field(new_header_field);
        } else {
            new_header_field->data = header_field_data;
            new_header_field->length = header_field_data_len;
            new_header_field->is_in_situ = false;
        }

        // Copy header fields after insertion point (shift right)
        if (col_idx < old_header_field_count) {
            memcpy(new_header_fields + col_idx + 1, header_row->fields + col_idx,
                   sizeof(csv_table_field) * (old_header_field_count - col_idx));
        }
    }

    // Phase 7: Reindex Header Map (if needed)
    // Critical: Only reindex header map entries AFTER all allocations succeed
    csv_header_map_reindex_increment(table, col_idx);

    // Phase 8: Atomic State Update
    // Only after all allocations and copies succeed:
    // 1. Update column_count
    if (table->column_count == SIZE_MAX) {
        free(new_field_arrays);
        free(old_field_counts);
        free(field_data_array);
        free(field_data_lengths);
        return TEXT_CSV_E_OOM;
    }
    table->column_count++;

    // 2. Update all data row structures
    array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        row->fields = new_fields;
        row->field_count = old_field_count + 1;

        array_idx++;
    }

    // 3. Update header row structure (if present)
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        header_row->fields = new_header_fields;
        header_row->field_count = old_header_field_count + 1;

        // 4. Add header map entry
        new_entry->name = new_header_fields[col_idx].data;
        new_entry->name_len = new_header_fields[col_idx].length;
        new_entry->index = col_idx;
        new_entry->next = table->header_map[header_hash];
        table->header_map[header_hash] = new_entry;
    }

    // Free temporary arrays
    free(new_field_arrays);
    free(old_field_counts);
    free(field_data_array);
    free(field_data_lengths);

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_column_insert(
    text_csv_table* table,
    size_t col_idx,
    const char* header_name,
    size_t header_name_len
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Validate col_idx (must be <= column count)
    if (col_idx > table->column_count) {
        return TEXT_CSV_E_INVALID;
    }

    // If inserting at end (col_idx == column_count), use append logic
    if (col_idx == table->column_count) {
        return text_csv_column_append(table, header_name, header_name_len);
    }

    // Use helper function for common column operation logic
    csv_table_field** new_field_arrays = NULL;
    size_t* old_field_counts = NULL;
    size_t rows_to_modify = 0;
    char* header_field_data = NULL;
    size_t header_field_data_len = 0;
    csv_header_entry* new_entry = NULL;
    size_t header_hash = 0;
    csv_table_field* new_header_fields = NULL;
    size_t old_header_field_count = 0;

    char** field_data_array = NULL;
    size_t* field_data_lengths = NULL;
    text_csv_status status = csv_column_operation_internal(
        table, col_idx, header_name, header_name_len,
        NULL, NULL,  // No values - empty fields
        &new_field_arrays, &old_field_counts, &rows_to_modify,
        &header_field_data, &header_field_data_len,
        &new_entry, &header_hash,
        &new_header_fields, &old_header_field_count,
        &field_data_array, &field_data_lengths
    );
    if (status != TEXT_CSV_OK) {
        return status;
    }

    // Phase 6: Reindex Header Map (if needed)
    // Critical: Only reindex header map entries AFTER all allocations succeed
    csv_header_map_reindex_increment(table, col_idx);

    // Phase 7: Atomic State Update
    // Only after all allocations, copies, and reindexing succeed:
    // 1. Update column_count
    if (table->column_count == SIZE_MAX) {
        free(new_field_arrays);
        free(old_field_counts);
        return TEXT_CSV_E_OOM;
    }
    table->column_count++;

    // 2. Update all data row structures
    size_t start_row_idx = 0;
    if (table->has_header && table->row_count > 0) {
        start_row_idx = 1;  // Skip header row at index 0
    }
    size_t array_idx = 0;
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];
        size_t old_field_count = old_field_counts[array_idx];
        csv_table_field* new_fields = new_field_arrays[array_idx];

        row->fields = new_fields;
        row->field_count = old_field_count + 1;

        array_idx++;
    }

    // 3. Update header row structure (if present)
    if (table->has_header && table->header_map && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];
        header_row->fields = new_header_fields;
        header_row->field_count = old_header_field_count + 1;

        // 4. Add header map entry
        new_entry->name = new_header_fields[col_idx].data;
        new_entry->name_len = new_header_fields[col_idx].length;
        new_entry->index = col_idx;
        new_entry->next = table->header_map[header_hash];
        table->header_map[header_hash] = new_entry;
    }

    // Free temporary arrays
    free(new_field_arrays);
    free(old_field_counts);

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_column_remove(
    text_csv_table* table,
    size_t col_idx
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    // Validate col_idx (must be < column count)
    if (col_idx >= table->column_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Cannot remove from empty table
    if (table->column_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Determine start row index (skip header row if present)
    size_t start_row_idx = 0;
    if (table->has_header && table->row_count > 0) {
        start_row_idx = 1;  // Skip header row at index 0
    }

    // Phase 1: Remove Header Map Entry (if needed)
    // Find and remove the header map entry for col_idx
    if (table->has_header && table->header_map && table->row_count > 0) {
        // Find the entry with index == col_idx
        csv_header_entry* entry_to_remove = NULL;
        csv_header_entry** prev_ptr = NULL;

        // Search all hash buckets to find the entry with matching index
        for (size_t i = 0; i < table->header_map_size; i++) {
            csv_header_entry** chain = &table->header_map[i];
            csv_header_entry* entry = *chain;
            csv_header_entry* prev = NULL;

            while (entry) {
                if (entry->index == col_idx) {
                    entry_to_remove = entry;
                    prev_ptr = prev ? &prev->next : chain;
                    break;
                }
                prev = entry;
                entry = entry->next;
            }

            if (entry_to_remove) {
                break;
            }
        }

        // Remove entry from hash chain if found
        if (entry_to_remove) {
            // Update the chain pointer to skip the entry
            *prev_ptr = entry_to_remove->next;
        }

        // Phase 2: Reindex Header Map Entries
        csv_header_map_reindex_decrement(table, col_idx);
    }

    // Phase 3: Shift Fields in All Rows
    // For each row, shift fields from col_idx+1 to field_count-1 left by one position
    for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
        csv_table_row* row = &table->rows[row_idx];

        // Validate col_idx is within bounds for this row
        if (col_idx >= row->field_count) {
            // This should not happen if column_count is consistent, but check anyway
            continue;
        }

        // Shift fields left: move fields from col_idx+1 to end one position left
        if (col_idx + 1 < row->field_count) {
            memmove(
                row->fields + col_idx,
                row->fields + col_idx + 1,
                sizeof(csv_table_field) * (row->field_count - col_idx - 1)
            );
        }

        // Decrement field count
        row->field_count--;
    }

    // Phase 4: Shift Header Row Fields (if present)
    if (table->has_header && table->row_count > 0) {
        csv_table_row* header_row = &table->rows[0];

        // Validate col_idx is within bounds for header row
        if (col_idx < header_row->field_count) {
            // Shift header fields left
            if (col_idx + 1 < header_row->field_count) {
                memmove(
                    header_row->fields + col_idx,
                    header_row->fields + col_idx + 1,
                    sizeof(csv_table_field) * (header_row->field_count - col_idx - 1)
                );
            }

            // Decrement header row field count
            header_row->field_count--;
        }
    }

    // Phase 5: Atomic State Update
    // Decrement column count
    table->column_count--;

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_column_rename(
    text_csv_table* table,
    size_t col_idx,
    const char* new_name,
    size_t new_name_length
) {
    // Validate inputs
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    if (!new_name) {
        return TEXT_CSV_E_INVALID;
    }

    // Check table has headers (return error if not)
    if (!table->has_header || !table->header_map) {
        return TEXT_CSV_E_INVALID;
    }

    // Validate col_idx (must be < column count)
    if (col_idx >= table->column_count) {
        return TEXT_CSV_E_INVALID;
    }

    // Cannot rename in empty table
    if (table->column_count == 0 || table->row_count == 0) {
        return TEXT_CSV_E_INVALID;
    }

    // Calculate new_name length if not provided
    size_t name_len = new_name_length;
    if (name_len == 0) {
        name_len = strlen(new_name);
    }

    // Calculate hash for new name (needed for later insertion)
    size_t hash = csv_header_hash(new_name, name_len, table->header_map_size);

    // Check for duplicate header name if uniqueness is required
    // Exclude the column being renamed from the check
    text_csv_status uniqueness_status = csv_check_header_uniqueness(
        table, new_name, name_len, col_idx  // Exclude the column being renamed
    );
    if (uniqueness_status != TEXT_CSV_OK) {
        return uniqueness_status;
    }

    // Phase 1: Find Old Header Map Entry
    // Find the entry with index == col_idx
    csv_header_entry* entry_to_remove = NULL;
    csv_header_entry** prev_ptr = NULL;

    // Search all hash buckets to find the entry with matching index
    for (size_t i = 0; i < table->header_map_size; i++) {
        csv_header_entry** chain = &table->header_map[i];
        csv_header_entry* search_entry = *chain;
        csv_header_entry* prev = NULL;

        while (search_entry) {
            if (search_entry->index == col_idx) {
                entry_to_remove = search_entry;
                prev_ptr = prev ? &prev->next : chain;
                break;
            }
            prev = search_entry;
            search_entry = search_entry->next;
        }

        if (entry_to_remove) {
            break;
        }
    }

    // If entry not found, this is an error (should not happen if table is consistent)
    if (!entry_to_remove) {
        return TEXT_CSV_E_INVALID;
    }

    // Phase 3: Pre-allocate New Name String
    // Allocate new name string in arena (before any state changes)
    char* new_name_data = NULL;
    if (name_len == 0) {
        // Empty name - use global empty string constant
        new_name_data = (char*)csv_empty_field_string;
    } else {
        // Check for overflow in name_len + 1
        if (name_len > SIZE_MAX - 1) {
            return TEXT_CSV_E_OOM;
        }

        // Allocate name data in arena
        new_name_data = (char*)csv_arena_alloc_for_context(
            table->ctx, name_len + 1, 1
        );
        if (!new_name_data) {
            return TEXT_CSV_E_OOM;
        }

        // Copy name data
        memcpy(new_name_data, new_name, name_len);
        new_name_data[name_len] = '\0';
    }

    // Phase 4: Pre-allocate New Header Map Entry
    // Allocate new header entry in arena (before any state changes)
    csv_header_entry* new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_header_entry), 8
    );
    if (!new_entry) {
        // Note: new_name_data remains in arena but won't be referenced
        // This is acceptable - arena cleanup will handle it
        return TEXT_CSV_E_OOM;
    }

    // Phase 5: Update Header Field in Header Row
    // Update the header field at col_idx in the header row
    csv_table_row* header_row = &table->rows[0];
    if (col_idx >= header_row->field_count) {
        // This should not happen if table is consistent, but check anyway
        return TEXT_CSV_E_INVALID;
    }

    csv_table_field* header_field = &header_row->fields[col_idx];
    if (name_len == 0) {
        csv_setup_empty_field(header_field);
    } else {
        header_field->data = new_name_data;
        header_field->length = name_len;
        header_field->is_in_situ = false;  // Always in arena after rename
    }

    // Phase 6: Remove Old Entry from Header Map
    // Remove old entry from hash chain
    *prev_ptr = entry_to_remove->next;

    // Phase 7: Add New Entry to Header Map
    // Initialize new entry
    new_entry->name = new_name_data;
    new_entry->name_len = name_len;
    new_entry->index = col_idx;  // Same index as before
    new_entry->next = table->header_map[hash];
    table->header_map[hash] = new_entry;

    // Note: Old entry structure remains in arena (no individual cleanup needed)
    // Old name string also remains in arena (no individual cleanup needed)

    return TEXT_CSV_OK;
}

TEXT_API text_csv_status text_csv_header_index(
    const text_csv_table* table,
    const char* name,
    size_t* out_idx
) {
    if (!table || !name || !out_idx) {
        return TEXT_CSV_E_INVALID;
    }

    if (!table->has_header || !table->header_map) {
        return TEXT_CSV_E_INVALID;
    }

    size_t name_len = strlen(name);
    size_t hash = csv_header_hash(name, name_len, table->header_map_size);

    csv_header_entry* entry = table->header_map[hash];
    while (entry) {
        if (entry->name_len == name_len && memcmp(entry->name, name, name_len) == 0) {
            *out_idx = entry->index;
            return TEXT_CSV_OK;
        }
        entry = entry->next;
    }

    return TEXT_CSV_E_INVALID;
}

TEXT_API text_csv_status text_csv_header_index_next(
    const text_csv_table* table,
    const char* name,
    size_t current_idx,
    size_t* out_idx
) {
    if (!table || !name || !out_idx) {
        return TEXT_CSV_E_INVALID;
    }

    if (!table->has_header || !table->header_map) {
        return TEXT_CSV_E_INVALID;
    }

    // Validate current_idx is within valid column range
    if (current_idx >= table->column_count) {
        return TEXT_CSV_E_INVALID;
    }

    size_t name_len = strlen(name);
    size_t hash = csv_header_hash(name, name_len, table->header_map_size);

    // Search through all entries in the hash bucket
    csv_header_entry* entry = table->header_map[hash];
    bool found_current = false;
    size_t min_next_idx = SIZE_MAX;

    while (entry) {
        if (entry->name_len == name_len && memcmp(entry->name, name, name_len) == 0) {
            // Verify that current_idx exists for this header name
            if (entry->index == current_idx) {
                found_current = true;
            }
            // Find the smallest index greater than current_idx
            if (entry->index > current_idx) {
                if (entry->index < min_next_idx) {
                    min_next_idx = entry->index;
                }
            }
        }
        entry = entry->next;
    }

    // If current_idx doesn't match any entry with this name, return error
    if (!found_current) {
        return TEXT_CSV_E_INVALID;
    }

    // If we found a next index, return it
    if (min_next_idx != SIZE_MAX && min_next_idx > current_idx) {
        *out_idx = min_next_idx;
        return TEXT_CSV_OK;
    }

    // No more matches found
    return TEXT_CSV_E_INVALID;
}

TEXT_API text_csv_status text_csv_set_require_unique_headers(
    text_csv_table* table,
    bool require
) {
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    table->require_unique_headers = require;
    return TEXT_CSV_OK;
}

TEXT_API bool text_csv_can_have_unique_headers(const text_csv_table* table) {
    // Handle NULL table gracefully (return false)
    if (!table) {
        return false;
    }

    // Check if table has headers
    if (!table->has_header || table->row_count == 0) {
        return false;
    }

    // Get the header row (first row when has_header is true)
    csv_table_row* header_row = &table->rows[0];
    if (!header_row || header_row->field_count == 0) {
        return false;
    }

    // Check for duplicate header names by comparing all fields in the header row
    // For each field, check if there's another field with the same name
    for (size_t i = 0; i < header_row->field_count; i++) {
        csv_table_field* field = &header_row->fields[i];
        const char* name = field->data;
        size_t name_len = field->length;

        // Count occurrences of this name in the header row
        size_t count = 0;
        for (size_t j = 0; j < header_row->field_count; j++) {
            csv_table_field* check_field = &header_row->fields[j];
            if (check_field->length == name_len &&
                memcmp(check_field->data, name, name_len) == 0) {
                count++;
            }
        }

        // If we found more than one occurrence, headers are not unique
        if (count > 1) {
            return false;
        }
    }

    // All header names are unique
    return true;
}

TEXT_API text_csv_status text_csv_set_header_row(
    text_csv_table* table,
    bool enable
) {
    if (!table) {
        return TEXT_CSV_E_INVALID;
    }

    if (enable) {
        // Enable headers: first row becomes header row
        // Validate: table must not be empty
        if (table->row_count == 0) {
            return TEXT_CSV_E_INVALID;
        }

        // Validate: headers must not already exist
        if (table->has_header) {
            return TEXT_CSV_E_INVALID;
        }

        // Get first row (will become header row)
        csv_table_row* first_row = &table->rows[0];
        size_t first_row_cols = first_row->field_count;

        // Handle column count:
        // - If first row has fewer columns than column_count: pad with empty strings
        // - If first row has more columns than column_count: update column_count
        size_t target_col_count = first_row_cols;
        if (first_row_cols < table->column_count) {
            // Pad first row with empty fields
            target_col_count = table->column_count;
            csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
                table->ctx, sizeof(csv_table_field) * target_col_count, 8
            );
            if (!new_fields) {
                return TEXT_CSV_E_OOM;
            }

            // Copy existing fields
            if (first_row->fields && first_row_cols > 0) {
                memcpy(new_fields, first_row->fields, sizeof(csv_table_field) * first_row_cols);
            }

            // Pad with empty fields
            for (size_t i = first_row_cols; i < target_col_count; i++) {
                csv_setup_empty_field(&new_fields[i]);
            }

            first_row->fields = new_fields;
            first_row->field_count = target_col_count;
        } else if (first_row_cols > table->column_count) {
            // Update column_count to match first row
            table->column_count = first_row_cols;
        }

        // If require_unique_headers is true, validate uniqueness
        if (table->require_unique_headers) {
            csv_table_row* header_row = &table->rows[0];
            for (size_t i = 0; i < header_row->field_count; i++) {
                csv_table_field* field = &header_row->fields[i];
                const char* name = field->data;
                size_t name_len = field->length;

                // Check for duplicates in the header row
                for (size_t j = i + 1; j < header_row->field_count; j++) {
                    csv_table_field* check_field = &header_row->fields[j];
                    if (check_field->length == name_len &&
                        memcmp(check_field->data, name, name_len) == 0) {
                        // Duplicate found and uniqueness is required
                        return TEXT_CSV_E_INVALID;
                    }
                }
            }
        }

        // Clear existing header map (if any) - must be done atomically
        if (table->header_map) {
            free(table->header_map);
            table->header_map = NULL;
            table->header_map_size = 0;
        }

        // Build new header map from first row
        // All allocations must succeed before state changes
        table->header_map_size = 16;
        table->header_map = (csv_header_entry**)calloc(table->header_map_size, sizeof(csv_header_entry*));
        if (!table->header_map) {
            return TEXT_CSV_E_OOM;
        }

        csv_table_row* header_row = &table->rows[0];
        for (size_t i = 0; i < header_row->field_count; i++) {
            csv_table_field* field = &header_row->fields[i];
            size_t hash = csv_header_hash(field->data, field->length, table->header_map_size);

            // Check for duplicates (for FIRST_WINS mode, skip duplicates)
            csv_header_entry* entry = table->header_map[hash];
            bool found_duplicate = false;
            while (entry) {
                if (entry->name_len == field->length &&
                    memcmp(entry->name, field->data, field->length) == 0) {
                    found_duplicate = true;
                    break;
                }
                entry = entry->next;
            }

            // For FIRST_WINS mode (default), skip duplicates
            if (found_duplicate) {
                continue;
            }

            // Create header entry
            csv_header_entry* new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
                table->ctx, sizeof(csv_header_entry), 8
            );
            if (!new_entry) {
                // Allocation failed - clean up and return error
                free(table->header_map);
                table->header_map = NULL;
                table->header_map_size = 0;
                return TEXT_CSV_E_OOM;
            }

            new_entry->name = field->data;
            new_entry->name_len = field->length;
            new_entry->index = i;
            new_entry->next = table->header_map[hash];
            table->header_map[hash] = new_entry;
        }

        // Set has_header = true (atomic state change)
        table->has_header = true;
        // Note: row_count stays the same (first row was already counted)

    } else {
        // Disable headers: header row becomes first data row
        // Validate: headers must exist
        if (!table->has_header) {
            return TEXT_CSV_E_INVALID;
        }

        // Clear header map (atomic - must complete before state changes)
        if (table->header_map) {
            free(table->header_map);
            table->header_map = NULL;
            table->header_map_size = 0;
        }

        // Set has_header = false (atomic state change)
        table->has_header = false;
        // Note: row_count stays the same (header row becomes data row)
    }

    return TEXT_CSV_OK;
}

TEXT_API text_csv_table* text_csv_new_table(void) {
    // Create context with arena
    csv_context* ctx = csv_context_new();
    if (!ctx) {
        return NULL;
    }

    // Allocate table structure
    text_csv_table* table = (text_csv_table*)malloc(sizeof(text_csv_table));
    if (!table) {
        csv_context_free(ctx);
        return NULL;
    }

    // Initialize table structure
    memset(table, 0, sizeof(text_csv_table));
    table->ctx = ctx;
    table->row_capacity = 16;
    table->row_count = 0;
    table->column_count = 0;  // No columns defined until first row
    table->has_header = false;
    table->header_map = NULL;
    table->header_map_size = 0;

    // Allocate initial rows array
    table->rows = (csv_table_row*)csv_arena_alloc_for_context(
        ctx, sizeof(csv_table_row) * table->row_capacity, 8
    );
    if (!table->rows) {
        free(table);
        csv_context_free(ctx);
        return NULL;
    }

    return table;
}

TEXT_API text_csv_table* text_csv_new_table_with_headers(
    const char* const* headers,
    const size_t* header_lengths,
    size_t header_count
) {
    // Validate inputs
    if (!headers || header_count == 0) {
        return NULL;
    }

    // Create context with arena
    csv_context* ctx = csv_context_new();
    if (!ctx) {
        return NULL;
    }

    // Allocate table structure
    text_csv_table* table = (text_csv_table*)malloc(sizeof(text_csv_table));
    if (!table) {
        csv_context_free(ctx);
        return NULL;
    }

    // Initialize table structure
    memset(table, 0, sizeof(text_csv_table));
    table->ctx = ctx;
    table->row_capacity = 16;
    table->row_count = 0;
    table->column_count = header_count;
    table->has_header = false;  // Will be set to true after header row is created
    table->header_map = NULL;
    table->header_map_size = 0;

    // Allocate initial rows array
    table->rows = (csv_table_row*)csv_arena_alloc_for_context(
        ctx, sizeof(csv_table_row) * table->row_capacity, 8
    );
    if (!table->rows) {
        free(table);
        csv_context_free(ctx);
        return NULL;
    }

    // Allocate header row structure
    csv_table_row* header_row = &table->rows[0];

    // Allocate field array for header row
    csv_table_field* header_fields = (csv_table_field*)csv_arena_alloc_for_context(
        ctx, sizeof(csv_table_field) * header_count, 8
    );
    if (!header_fields) {
        free(table);
        csv_context_free(ctx);
        return NULL;
    }

    // Copy each header name to arena
    for (size_t i = 0; i < header_count; i++) {
        csv_table_field* field = &header_fields[i];
        const char* header_data = headers[i];

        // Calculate header length
        size_t header_len = csv_calculate_field_length(header_data, header_lengths, i);

        // Use global empty string constant for empty headers
        if (header_len == 0) {
            csv_setup_empty_field(field);
            continue;
        }

        // Allocate and copy header data to arena
        text_csv_status status = csv_allocate_and_copy_field(
            ctx, header_data, header_len, field
        );
        if (status != TEXT_CSV_OK) {
            free(table);
            csv_context_free(ctx);
            return NULL;
        }
    }

    // Set header row structure
    header_row->fields = header_fields;
    header_row->field_count = header_count;
    table->row_count = 1;  // Header row is at index 0

    // Build header map (hash table) for lookup
    table->header_map_size = 16;
    table->header_map = (csv_header_entry**)calloc(table->header_map_size, sizeof(csv_header_entry*));
    if (!table->header_map) {
        free(table);
        csv_context_free(ctx);
        return NULL;
    }

    // Add each header to the map
    for (size_t i = 0; i < header_count; i++) {
        csv_table_field* field = &header_fields[i];
        size_t hash = csv_header_hash(field->data, field->length, table->header_map_size);

        // Check for duplicates
        csv_header_entry* entry = table->header_map[hash];
        bool found_duplicate = false;
        while (entry) {
            if (entry->name_len == field->length &&
                memcmp(entry->name, field->data, field->length) == 0) {
                found_duplicate = true;
                break;
            }
            entry = entry->next;
        }

        if (found_duplicate) {
            // Duplicate header name - free resources and return NULL
            free(table->header_map);
            table->header_map = NULL;
            free(table);
            csv_context_free(ctx);
            return NULL;
        }

        // Create header entry
        csv_header_entry* new_entry = (csv_header_entry*)csv_arena_alloc_for_context(
            ctx, sizeof(csv_header_entry), 8
        );
        if (!new_entry) {
            free(table->header_map);
            table->header_map = NULL;
            free(table);
            csv_context_free(ctx);
            return NULL;
        }

        new_entry->name = field->data;
        new_entry->name_len = field->length;
        new_entry->index = i;
        new_entry->next = table->header_map[hash];
        table->header_map[hash] = new_entry;
    }

    table->has_header = true;
    return table;
}
