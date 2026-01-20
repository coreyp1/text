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

// Set input buffer for in-situ mode
void csv_context_set_input_buffer(csv_context* ctx, const char* input_buffer, size_t input_buffer_len) {
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
void* csv_arena_alloc_for_context(csv_context* ctx, size_t size, size_t align) {
    if (!ctx || !ctx->arena) {
        return NULL;
    }
    return csv_arena_alloc(ctx->arena, size, align);
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
                if (event->data_len > 0 && event->data) {
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
            err->code = TEXT_CSV_E_OOM;
            err->message = "Failed to create stream parser";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
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
            err->code = TEXT_CSV_E_INVALID;
            err->message = "Input data must not be NULL";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
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
                err->code = TEXT_CSV_E_OOM;
                err->message = "Failed to create context";
                err->byte_offset = 0;
                err->line = 1;
                err->column = 1;
                err->row_index = 0;
                err->col_index = 0;
                err->context_snippet = NULL;
                err->context_snippet_len = 0;
                err->caret_offset = 0;
            }
            return NULL;
        }

        text_csv_table* table = (text_csv_table*)malloc(sizeof(text_csv_table));
        if (!table) {
            csv_context_free(ctx);
            if (err) {
                err->code = TEXT_CSV_E_OOM;
                err->message = "Failed to allocate table";
                err->byte_offset = 0;
                err->line = 1;
                err->column = 1;
                err->row_index = 0;
                err->col_index = 0;
                err->context_snippet = NULL;
                err->context_snippet_len = 0;
                err->caret_offset = 0;
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
                err->code = TEXT_CSV_E_OOM;
                err->message = "Failed to allocate rows";
                err->byte_offset = 0;
                err->line = 1;
                err->column = 1;
                err->row_index = 0;
                err->col_index = 0;
                err->context_snippet = NULL;
                err->context_snippet_len = 0;
                err->caret_offset = 0;
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
            err->code = TEXT_CSV_E_OOM;
            err->message = "Failed to create context";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
        }
        return NULL;
    }

    // Allocate table structure
    text_csv_table* table = (text_csv_table*)malloc(sizeof(text_csv_table));
    if (!table) {
        csv_context_free(ctx);
        if (err) {
            err->code = TEXT_CSV_E_OOM;
            err->message = "Failed to allocate table";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
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
            err->code = TEXT_CSV_E_OOM;
            err->message = "Failed to allocate rows";
            err->byte_offset = 0;
            err->line = 1;
            err->column = 1;
            err->row_index = 0;
            err->col_index = 0;
            err->context_snippet = NULL;
            err->context_snippet_len = 0;
            err->caret_offset = 0;
        }
        return NULL;
    }

    // Handle BOM (must be done before setting input buffer for in-situ mode)
    const char* input = (const char*)data;
    size_t input_len = len;
    csv_position pos = {0, 1, 1};
    if (!opts->keep_bom) {
        csv_strip_bom(&input, &input_len, &pos, true);
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

    // Process header if enabled
    if (opts->dialect.treat_first_row_as_header && table->row_count > 0) {
        // Build header map from first row
        table->header_map_size = 16;
        table->header_map = (csv_header_entry**)calloc(table->header_map_size, sizeof(csv_header_entry*));
        if (table->header_map) {
            csv_table_row* header_row = &table->rows[0];
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
                                err->code = TEXT_CSV_E_INVALID;
                                err->message = "Duplicate column name in header";
                                err->byte_offset = 0;
                                err->line = 1;
                                err->column = 1;
                                err->row_index = 0;
                                err->col_index = i;
                                err->context_snippet = NULL;
                                err->context_snippet_len = 0;
                                err->caret_offset = 0;
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
