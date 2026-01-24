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

TEXT_API text_csv_status text_csv_row_append(
    text_csv_table* table,
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

    // Adjust row_count for header if present
    size_t data_row_count = table->row_count;
    if (table->has_header && table->row_count > 0) {
        data_row_count = table->row_count - 1;
    }

    // For first data row: set column count (if not already set from header)
    // For subsequent rows: validate field count matches
    if (data_row_count == 0 && table->column_count == 0) {
        // First data row sets column count (table has no headers or column_count not set)
        table->column_count = field_count;
    } else {
        // Subsequent rows (or first data row when header exists) must match column count
        if (field_count != table->column_count) {
            return TEXT_CSV_E_INVALID;
        }
    }

    // Grow row capacity if needed
    if (table->row_count >= table->row_capacity) {
        size_t new_capacity = table->row_capacity * 2;
        // Check for overflow
        if (new_capacity < table->row_capacity) {
            return TEXT_CSV_E_OOM;
        }
        csv_table_row* new_rows = (csv_table_row*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_row) * new_capacity, 8
        );
        if (!new_rows) {
            return TEXT_CSV_E_OOM;
        }
        // Copy existing rows
        memcpy(new_rows, table->rows, sizeof(csv_table_row) * table->row_count);
        table->rows = new_rows;
        table->row_capacity = new_capacity;
    }

    // Allocate new row structure from arena
    csv_table_row* new_row = &table->rows[table->row_count];

    // Allocate field array from arena
    csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_table_field) * field_count, 8
    );
    if (!new_fields) {
        return TEXT_CSV_E_OOM;
    }

    // Copy each field data to arena
    for (size_t i = 0; i < field_count; i++) {
        csv_table_field* field = &new_fields[i];
        const char* field_data = fields[i];

        // Determine field length
        size_t field_len;
        if (field_lengths) {
            field_len = field_lengths[i];
        } else {
            // Null-terminated string
            if (field_data) {
                field_len = strlen(field_data);
            } else {
                field_len = 0;
            }
        }

        // Use global empty string constant for empty fields (saves arena allocation)
        if (field_len == 0) {
            field->data = csv_empty_field_string;
            field->length = 0;
            field->is_in_situ = false;  // Not in-situ, but points to global constant
            continue;
        }

        // Allocate field data in arena for non-empty fields (always copy, never reference external)
        // Check for overflow in field_len + 1
        if (field_len > SIZE_MAX - 1) {
            return TEXT_CSV_E_OOM;
        }
        char* arena_data = (char*)csv_arena_alloc_for_context(
            table->ctx, field_len + 1, 1
        );
        if (!arena_data) {
            return TEXT_CSV_E_OOM;
        }

        // Copy field data
        memcpy(arena_data, field_data, field_len);
        arena_data[field_len] = '\0';

        // Set field structure
        field->data = arena_data;
        field->length = field_len;
        field->is_in_situ = false;  // All mutations copy to arena
    }

    // Set row structure
    new_row->fields = new_fields;
    new_row->field_count = field_count;

    // Update row count
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

    // For first data row: set column count (if not already set from header)
    // For subsequent rows: validate field count matches
    if (data_row_count == 0 && table->column_count == 0) {
        // First data row sets column count (table has no headers or column_count not set)
        table->column_count = field_count;
    } else {
        // Subsequent rows (or first data row when header exists) must match column count
        if (field_count != table->column_count) {
            return TEXT_CSV_E_INVALID;
        }
    }

    // Check if inserting at end (equivalent to append)
    bool is_append = (row_idx == data_row_count);

    // Grow row capacity if needed
    if (table->row_count >= table->row_capacity) {
        size_t new_capacity = table->row_capacity * 2;
        // Check for overflow
        if (new_capacity < table->row_capacity) {
            return TEXT_CSV_E_OOM;
        }
        csv_table_row* new_rows = (csv_table_row*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_row) * new_capacity, 8
        );
        if (!new_rows) {
            return TEXT_CSV_E_OOM;
        }
        // Copy existing rows
        memcpy(new_rows, table->rows, sizeof(csv_table_row) * table->row_count);
        table->rows = new_rows;
        table->row_capacity = new_capacity;
    }

    // If inserting at end, use append logic (no shifting needed)
    if (is_append) {
        // Use append logic - just add at the end
        csv_table_row* new_row = &table->rows[table->row_count];

        // Allocate field array from arena
        csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * field_count, 8
        );
        if (!new_fields) {
            return TEXT_CSV_E_OOM;
        }

        // Copy each field data to arena
        for (size_t i = 0; i < field_count; i++) {
            csv_table_field* field = &new_fields[i];
            const char* field_data = fields[i];

            // Determine field length
            size_t field_len;
            if (field_lengths) {
                field_len = field_lengths[i];
            } else {
                // Null-terminated string
                if (field_data) {
                    field_len = strlen(field_data);
                } else {
                    field_len = 0;
                }
            }

            // Use global empty string constant for empty fields (saves arena allocation)
            if (field_len == 0) {
                field->data = csv_empty_field_string;
                field->length = 0;
                field->is_in_situ = false;  // Not in-situ, but points to global constant
                continue;
            }

            // Allocate field data in arena for non-empty fields (always copy, never reference external)
            // Check for overflow in field_len + 1
            if (field_len > SIZE_MAX - 1) {
                return TEXT_CSV_E_OOM;
            }
            char* arena_data = (char*)csv_arena_alloc_for_context(
                table->ctx, field_len + 1, 1
            );
            if (!arena_data) {
                return TEXT_CSV_E_OOM;
            }

            // Copy field data
            memcpy(arena_data, field_data, field_len);
            arena_data[field_len] = '\0';

            // Set field structure
            field->data = arena_data;
            field->length = field_len;
            field->is_in_situ = false;  // All mutations copy to arena
        }

        // Set row structure
        new_row->fields = new_fields;
        new_row->field_count = field_count;

        // Update row count
        table->row_count++;

        return TEXT_CSV_OK;
    }

    // Otherwise, shift rows from adjusted_row_idx to row_count-1 one position right
    // Shift in reverse order to avoid overwriting
    for (size_t i = table->row_count; i > adjusted_row_idx; i--) {
        table->rows[i] = table->rows[i - 1];
    }

    // Allocate new row structure from arena
    csv_table_row* new_row = &table->rows[adjusted_row_idx];

    // Allocate field array from arena
    csv_table_field* new_fields = (csv_table_field*)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_table_field) * field_count, 8
    );
    if (!new_fields) {
        return TEXT_CSV_E_OOM;
    }

    // Copy each field data to arena
    for (size_t i = 0; i < field_count; i++) {
        csv_table_field* field = &new_fields[i];
        const char* field_data = fields[i];

        // Determine field length
        size_t field_len;
        if (field_lengths) {
            field_len = field_lengths[i];
        } else {
            // Null-terminated string
            if (field_data) {
                field_len = strlen(field_data);
            } else {
                field_len = 0;
            }
        }

        // Use global empty string constant for empty fields (saves arena allocation)
        if (field_len == 0) {
            field->data = csv_empty_field_string;
            field->length = 0;
            field->is_in_situ = false;  // Not in-situ, but points to global constant
            continue;
        }

        // Allocate field data in arena for non-empty fields (always copy, never reference external)
        // Check for overflow in field_len + 1
        if (field_len > SIZE_MAX - 1) {
            return TEXT_CSV_E_OOM;
        }
        char* arena_data = (char*)csv_arena_alloc_for_context(
            table->ctx, field_len + 1, 1
        );
        if (!arena_data) {
            return TEXT_CSV_E_OOM;
        }

        // Copy field data
        memcpy(arena_data, field_data, field_len);
        arena_data[field_len] = '\0';

        // Set field structure
        field->data = arena_data;
        field->length = field_len;
        field->is_in_situ = false;  // All mutations copy to arena
    }

    // Set row structure
    new_row->fields = new_fields;
    new_row->field_count = field_count;

    // Update row count
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
        field->data = csv_empty_field_string;
        field->length = 0;
        field->is_in_situ = false;  // Not in-situ, but points to global constant
        return TEXT_CSV_OK;
    }

    // Allocate new field data from arena for non-empty fields
    // Check for overflow in field_len + 1
    if (field_len > SIZE_MAX - 1) {
        return TEXT_CSV_E_OOM;
    }
    char* arena_data = (char*)csv_arena_alloc_for_context(
        table->ctx, field_len + 1, 1
    );
    if (!arena_data) {
        return TEXT_CSV_E_OOM;
    }

    // Copy field data to arena
    memcpy(arena_data, field_data, field_len);
    arena_data[field_len] = '\0';

    // Update field structure (data pointer, length, is_in_situ = false)
    // Note: Old field data remains in arena (no individual cleanup needed)
    field->data = arena_data;
    field->length = field_len;
    field->is_in_situ = false;  // All mutations copy to arena

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
