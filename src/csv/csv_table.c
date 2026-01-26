/**
 * @file
 *
 * Table structure and arena allocator for CSV module.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <ghoti.io/text/csv/csv_table.h>
#include <limits.h>

// Global empty string constant for all empty fields
// This avoids allocating 1 byte per empty field in the arena
static const char csv_empty_field_string[] = "";

// Arena allocator implementation
// Uses a simple linked list of blocks for efficient bulk allocation

// Default block size (64KB)
#define CSV_ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

// Create a new arena allocator
static csv_arena * csv_arena_new(size_t initial_block_size) {
  csv_arena * arena = malloc(sizeof(csv_arena));
  if (!arena) {
    return NULL;
  }

  arena->block_size = initial_block_size > 0 ? initial_block_size
                                             : CSV_ARENA_DEFAULT_BLOCK_SIZE;
  arena->first = NULL;
  arena->current = NULL;

  return arena;
}

// Allocate memory from the arena
static void * csv_arena_alloc(csv_arena * arena, size_t size, size_t align) {
  if (!arena || size == 0) {
    return NULL;
  }

  // Validate alignment: must be power of 2 and not 0
  if (align == 0 || (align & (align - 1)) != 0) {
    return NULL; // Invalid alignment
  }

  // Calculate alignment mask
  size_t align_mask = align - 1;

  // If we have a current block, try to allocate from it
  if (arena->current) {
    size_t offset = arena->current->used;
    size_t aligned_offset = (offset + align_mask) & ~align_mask;

    // Check for overflow in aligned_offset + size before comparison
    if (aligned_offset <= SIZE_MAX - size &&
        aligned_offset + size <= arena->current->size) {
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
    return NULL; // Overflow
  }
  if (size + align > block_size) {
    block_size = size + align;
  }

  // Check for overflow in sizeof + block_size
  size_t block_alloc_size = sizeof(csv_arena_block) + block_size;
  if (block_alloc_size < block_size) { // Overflow check
    return NULL;
  }
  csv_arena_block * block = malloc(block_alloc_size);
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
    return NULL; // Overflow
  }
  block->used = aligned_offset + size;

  // Link into arena
  if (arena->first == NULL) {
    arena->first = block;
  }
  else {
    arena->current->next = block;
  }
  arena->current = block;

  return block->data + aligned_offset;
}

// Free all memory in the arena
static void csv_arena_free(csv_arena * arena) {
  if (!arena) {
    return;
  }

  csv_arena_block * block = arena->first;
  while (block) {
    csv_arena_block * next = block->next;
    free(block);
    block = next;
  }

  free(arena);
}

// Create a new CSV context with arena
GTEXT_INTERNAL_API csv_context * csv_context_new(void) {
  csv_context * ctx = malloc(sizeof(csv_context));
  if (!ctx) {
    return NULL;
  }

  ctx->arena = csv_arena_new(0); // Use default block size
  if (!ctx->arena) {
    free(ctx);
    return NULL;
  }

  ctx->input_buffer = NULL;
  ctx->input_buffer_len = 0;

  return ctx;
}

// Create a new CSV context with arena and specified initial block size
static csv_context * csv_context_new_with_block_size(
    size_t initial_block_size) {
  csv_context * ctx = malloc(sizeof(csv_context));
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
GTEXT_INTERNAL_API void csv_context_set_input_buffer(
    csv_context * ctx, const char * input_buffer, size_t input_buffer_len) {
  if (!ctx) {
    return;
  }
  ctx->input_buffer = input_buffer;
  ctx->input_buffer_len = input_buffer_len;
}

// Free a CSV context and its arena
void csv_context_free(csv_context * ctx) {
  if (!ctx) {
    return;
  }

  csv_arena_free(ctx->arena);
  free(ctx);
}

// Allocate memory from a context's arena
GTEXT_INTERNAL_API void * csv_arena_alloc_for_context(
    csv_context * ctx, size_t size, size_t align) {
  if (!ctx || !ctx->arena) {
    return NULL;
  }
  return csv_arena_alloc(ctx->arena, size, align);
}

// ============================================================================
// Helper functions for field operations
// ============================================================================

static size_t csv_calculate_field_length(
    const char * field_data, const size_t * field_lengths, size_t field_index) {
  if (field_lengths) {
    return field_lengths[field_index];
  }
  else {
    // Null-terminated string
    if (field_data) {
      return strlen(field_data);
    }
    else {
      return 0;
    }
  }
}

static void csv_setup_empty_field(csv_table_field * field) {
  field->data = csv_empty_field_string;
  field->length = 0;
  field->is_in_situ = false; // Not in-situ, but points to global constant
}

static void csv_set_field_count_error(GTEXT_CSV_Error * err,
    size_t expected_count, size_t actual_count, size_t row_index) {
  if (!err) {
    return;
  }

  // Free any existing context snippet
  if (err->context_snippet) {
    free(err->context_snippet);
    err->context_snippet = NULL;
  }

  err->code = GTEXT_CSV_E_INVALID;
  err->byte_offset = 0;
  err->line = 0;
  err->column = 0;
  err->row_index = row_index;
  err->col_index = actual_count; // Store actual count in col_index
  err->caret_offset = 0;

  // Allocate and format error message with expected and actual counts
  size_t msg_len = 128; // Sufficient for formatted message
  char * msg = (char *)malloc(msg_len);
  if (msg) {
    if (row_index != SIZE_MAX) {
      snprintf(msg, msg_len,
          "Field count mismatch at row %zu: expected %zu fields, got %zu",
          row_index, expected_count, actual_count);
    } else {
      snprintf(msg, msg_len,
          "Field count mismatch on append: expected %zu fields, got %zu",
          expected_count, actual_count);
    }
    err->context_snippet = msg;
    err->context_snippet_len = strlen(msg);
    // Point message to the formatted string (caller must free via gtext_csv_error_free)
    err->message = err->context_snippet;
  } else {
    // Fallback to static message if allocation fails
    err->message = "Field count mismatch: row field count does not match table column count";
    err->context_snippet = NULL;
    err->context_snippet_len = 0;
  }
}

// Forward declarations for table calculation helpers
static size_t csv_get_data_row_count(const GTEXT_CSV_Table * table);
static size_t csv_get_start_row_idx(const GTEXT_CSV_Table * table);
static size_t csv_get_rows_to_modify(const GTEXT_CSV_Table * table);

// Forward declarations for column operation helpers
static GTEXT_CSV_Status csv_validate_column_values(
    const GTEXT_CSV_Table * table, const char * const * values);
static GTEXT_CSV_Status csv_determine_header_value(
    const GTEXT_CSV_Table * table, bool is_empty_column,
    const char * header_name, size_t header_name_len,
    const char * const * values, const size_t * value_lengths,
    const char ** header_value_out, size_t * header_value_len_out,
    const char ** header_map_name_out, size_t * header_map_name_len_out,
    size_t * name_len_out);
static GTEXT_CSV_Status csv_preallocate_column_field_data(
    GTEXT_CSV_Table * table, bool is_empty_column, size_t rows_to_modify,
    const char * const * values, const size_t * value_lengths,
    char *** field_data_array_out, size_t ** field_data_lengths_out);

static GTEXT_CSV_Status csv_column_op_alloc_temp_arrays(
    size_t rows_to_modify, csv_column_op_temp_arrays * temp_arrays_out);

static void csv_column_op_cleanup_temp_arrays(
    csv_column_op_temp_arrays * temp_arrays);

static GTEXT_CSV_Status csv_allocate_and_copy_field(csv_context * ctx,
    const char * field_data, size_t field_len, csv_table_field * field_out) {
  // Check for overflow in field_len + 1
  if (field_len > SIZE_MAX - 1) {
    return GTEXT_CSV_E_OOM;
  }

  // Allocate field data in arena
  char * arena_data =
      (char *)csv_arena_alloc_for_context(ctx, field_len + 1, 1);
  if (!arena_data) {
    return GTEXT_CSV_E_OOM;
  }

  // Copy field data
  memcpy(arena_data, field_data, field_len);
  arena_data[field_len] = '\0';

  // Set field structure
  field_out->data = arena_data;
  field_out->length = field_len;
  field_out->is_in_situ = false; // All mutations copy to arena

  return GTEXT_CSV_OK;
}

// Table structure implementation


// Simple hash function for header names
static size_t csv_header_hash(
    const char * name, size_t name_len, size_t map_size) {
  size_t hash = 5381;
  for (size_t i = 0; i < name_len; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)name[i];
  }
  return hash % map_size;
}

static GTEXT_CSV_Status csv_check_header_uniqueness(
    const GTEXT_CSV_Table * table, const char * name, size_t name_len,
    size_t exclude_index) {
  // Only check if uniqueness is required
  if (!table->require_unique_headers) {
    return GTEXT_CSV_OK;
  }

  // Table must have headers
  if (!table->has_header || !table->header_map) {
    return GTEXT_CSV_OK;
  }

  size_t hash = csv_header_hash(name, name_len, table->header_map_size);
  csv_header_entry * entry = table->header_map[hash];
  while (entry) {
    if (entry->index != exclude_index && // Exclude specified index if provided
        entry->name_len == name_len &&
        memcmp(entry->name, name, name_len) == 0) {
      // Duplicate header name found and uniqueness is required
      return GTEXT_CSV_E_INVALID;
    }
    entry = entry->next;
  }

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_ensure_index_to_entry_capacity(
    GTEXT_CSV_Table * table, size_t required_index) {
  if (!table->has_header) {
    return GTEXT_CSV_OK; // No header map, no reverse mapping needed
  }

  // Calculate required capacity (index + 1, since indices are 0-based)
  size_t required_capacity = required_index + 1;

  // Check for overflow
  if (required_capacity <= required_index) {
    return GTEXT_CSV_E_OOM;
  }

  // If current capacity is sufficient, nothing to do
  if (table->index_to_entry_capacity >= required_capacity) {
    return GTEXT_CSV_OK;
  }

  // Calculate new capacity (grow by at least 2x, or to required size)
  size_t new_capacity = table->index_to_entry_capacity;
  if (new_capacity == 0) {
    new_capacity = 16; // Initial capacity
  }
  while (new_capacity < required_capacity) {
    size_t next_capacity = new_capacity * 2;
    if (next_capacity <= new_capacity) {
      // Overflow - use required capacity directly
      new_capacity = required_capacity;
      break;
    }
    new_capacity = next_capacity;
  }

  // Allocate new array in arena
  csv_header_entry ** new_array =
      (csv_header_entry **)csv_arena_alloc_for_context(
          table->ctx, sizeof(csv_header_entry *) * new_capacity, 8);
  if (!new_array) {
    return GTEXT_CSV_E_OOM;
  }

  // Copy existing entries (if any)
  if (table->index_to_entry) {
    memcpy(new_array, table->index_to_entry,
        sizeof(csv_header_entry *) * table->index_to_entry_capacity);
    // Zero out the rest
    memset(new_array + table->index_to_entry_capacity, 0,
        sizeof(csv_header_entry *) *
            (new_capacity - table->index_to_entry_capacity));
  }
  else {
    // Initialize new array to all NULL
    memset(new_array, 0, sizeof(csv_header_entry *) * new_capacity);
  }

  table->index_to_entry = new_array;
  table->index_to_entry_capacity = new_capacity;

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_set_index_to_entry(
    GTEXT_CSV_Table * table, size_t col_idx, csv_header_entry * entry) {
  if (!table->has_header) {
    return GTEXT_CSV_OK; // No header map, no reverse mapping needed
  }

  // Ensure capacity
  GTEXT_CSV_Status status = csv_ensure_index_to_entry_capacity(table, col_idx);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Set the entry
  table->index_to_entry[col_idx] = entry;

  return GTEXT_CSV_OK;
}

static bool csv_find_header_entry_by_index(const GTEXT_CSV_Table * table,
    size_t col_idx, csv_header_entry ** entry_out,
    csv_header_entry *** prev_ptr_out) {
  if (!table->has_header || !table->header_map) {
    *entry_out = NULL;
    return false;
  }

  // Try O(1) lookup using reverse mapping
  if (table->index_to_entry && col_idx < table->index_to_entry_capacity) {
    csv_header_entry * entry = table->index_to_entry[col_idx];
    if (entry && entry->index == col_idx) {
      // Found via reverse mapping - now find prev_ptr for removal
      size_t hash =
          csv_header_hash(entry->name, entry->name_len, table->header_map_size);
      csv_header_entry ** chain = &table->header_map[hash];
      csv_header_entry * search_entry = *chain;
      csv_header_entry * prev = NULL;

      while (search_entry) {
        if (search_entry == entry) {
          *entry_out = entry;
          *prev_ptr_out = prev ? &prev->next : chain;
          return true;
        }
        prev = search_entry;
        search_entry = search_entry->next;
      }
    }
  }

  // Fallback to O(n) search through all hash buckets
  for (size_t i = 0; i < table->header_map_size; i++) {
    csv_header_entry ** chain = &table->header_map[i];
    csv_header_entry * search_entry = *chain;
    csv_header_entry * prev = NULL;

    while (search_entry) {
      if (search_entry->index == col_idx) {
        *entry_out = search_entry;
        *prev_ptr_out = prev ? &prev->next : chain;
        return true;
      }
      prev = search_entry;
      search_entry = search_entry->next;
    }
  }

  *entry_out = NULL;
  return false;
}

static GTEXT_CSV_Status csv_rebuild_index_to_entry(GTEXT_CSV_Table * table) {
  if (!table->has_header || !table->header_map) {
    // Clear reverse mapping if no header map
    table->index_to_entry = NULL;
    table->index_to_entry_capacity = 0;
    return GTEXT_CSV_OK;
  }

  // Find maximum index to determine required capacity
  size_t max_index = 0;
  for (size_t i = 0; i < table->header_map_size; i++) {
    csv_header_entry * entry = table->header_map[i];
    while (entry) {
      if (entry->index > max_index) {
        max_index = entry->index;
      }
      entry = entry->next;
    }
  }

  // Ensure capacity
  GTEXT_CSV_Status status =
      csv_ensure_index_to_entry_capacity(table, max_index);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Clear existing reverse mapping
  if (table->index_to_entry) {
    memset(table->index_to_entry, 0,
        sizeof(csv_header_entry *) * table->index_to_entry_capacity);
  }

  // Build reverse mapping
  for (size_t i = 0; i < table->header_map_size; i++) {
    csv_header_entry * entry = table->header_map[i];
    while (entry) {
      csv_set_index_to_entry(table, entry->index, entry);
      entry = entry->next;
    }
  }

  return GTEXT_CSV_OK;
}

// Event callback for building table from stream
static GTEXT_CSV_Status csv_table_event_callback(
    const GTEXT_CSV_Event * event, void * user_data) {
  csv_table_parse_context * ctx = (csv_table_parse_context *)user_data;
  GTEXT_CSV_Table * table = ctx->table;

  switch (event->type) {
  case GTEXT_CSV_EVENT_RECORD_BEGIN: {
    // Allocate new row
    if (table->row_count >= table->row_capacity) {
      size_t new_capacity = table->row_capacity * 2;
      if (new_capacity < table->row_capacity) {
        ctx->status = GTEXT_CSV_E_OOM;
        return GTEXT_CSV_E_OOM;
      }
      csv_table_row * new_rows = (csv_table_row *)csv_arena_alloc_for_context(
          table->ctx, sizeof(csv_table_row) * new_capacity, 8);
      if (!new_rows) {
        ctx->status = GTEXT_CSV_E_OOM;
        return GTEXT_CSV_E_OOM;
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
    return GTEXT_CSV_OK;
  }

  case GTEXT_CSV_EVENT_FIELD: {
    if (!ctx->current_row) {
      ctx->status = GTEXT_CSV_E_INVALID;
      return GTEXT_CSV_E_INVALID;
    }

    // Grow field array if needed
    if (ctx->current_field_index >= ctx->current_field_capacity) {
      size_t new_capacity = ctx->current_field_capacity == 0
          ? 16
          : ctx->current_field_capacity * 2;
      // Check for overflow in multiplication
      if (new_capacity < ctx->current_field_capacity &&
          ctx->current_field_capacity > 0) {
        ctx->status = GTEXT_CSV_E_OOM;
        return GTEXT_CSV_E_OOM;
      }
      csv_table_field * new_fields =
          (csv_table_field *)csv_arena_alloc_for_context(
              table->ctx, sizeof(csv_table_field) * new_capacity, 8);
      if (!new_fields) {
        ctx->status = GTEXT_CSV_E_OOM;
        return GTEXT_CSV_E_OOM;
      }
      if (ctx->current_row->fields) {
        memcpy(new_fields, ctx->current_row->fields,
            sizeof(csv_table_field) * ctx->current_field_index);
      }
      ctx->current_row->fields = new_fields;
      ctx->current_field_capacity = new_capacity;
    }

    csv_table_field * field =
        &ctx->current_row->fields[ctx->current_field_index];

    // Use global empty string constant for empty fields (saves arena
    // allocation)
    if (event->data_len == 0) {
      field->data = csv_empty_field_string;
      field->length = 0;
      field->is_in_situ = false; // Not in-situ, but points to global constant
      ctx->current_field_index++;
      ctx->current_row->field_count = ctx->current_field_index;
      return GTEXT_CSV_OK;
    }

    // Handle field data - check if we can use in-situ mode
    bool can_use_in_situ = false;
    if (ctx->opts->in_situ_mode && !ctx->opts->validate_utf8 &&
        table->ctx->input_buffer && event->data) {
      // Check if field data points to the original input buffer
      // This means the field wasn't transformed (no unescaping needed) and
      // wasn't buffered
      const char * input_start = table->ctx->input_buffer;
      size_t input_buffer_len = table->ctx->input_buffer_len;
      const char * field_start = event->data;
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

    if (can_use_in_situ) {
      // Can use in-situ mode: reference input directly
      // The input buffer is caller-owned and must remain valid for the lifetime
      // of the table
      field->data = event->data;
      field->length = event->data_len;
      field->is_in_situ = true;
      ctx->current_field_index++;
      ctx->current_row->field_count = ctx->current_field_index;
      return GTEXT_CSV_OK;
    }

    // Need to copy (for escaping/unescaping, UTF-8 validation, or when field
    // was transformed) Check for integer overflow in allocation size
    if (event->data_len > SIZE_MAX - 1) {
      ctx->status = GTEXT_CSV_E_OOM;
      return GTEXT_CSV_E_OOM;
    }
    char * field_data =
        (char *)csv_arena_alloc_for_context(table->ctx, event->data_len + 1, 1);
    if (!field_data) {
      ctx->status = GTEXT_CSV_E_OOM;
      return GTEXT_CSV_E_OOM;
    }
    // event->data is checked to be non-NULL above, and event->data_len is
    // checked for overflow
    if (event->data) {
      memcpy(field_data, event->data, event->data_len);
    }
    field_data[event->data_len] = '\0';
    field->data = field_data;
    field->length = event->data_len;
    field->is_in_situ = false;

    ctx->current_field_index++;
    ctx->current_row->field_count = ctx->current_field_index;
    return GTEXT_CSV_OK;
  }

  case GTEXT_CSV_EVENT_RECORD_END: {
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
    return GTEXT_CSV_OK;
  }

  case GTEXT_CSV_EVENT_END:
    return GTEXT_CSV_OK;
  }

  return GTEXT_CSV_OK;
}

// Helper function to create an empty table
static GTEXT_CSV_Table * csv_create_empty_table(GTEXT_CSV_Error * err) {
  csv_context * ctx = csv_context_new();
  if (!ctx) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to create context");
    return NULL;
  }

  GTEXT_CSV_Table * table = (GTEXT_CSV_Table *)malloc(sizeof(GTEXT_CSV_Table));
  if (!table) {
    csv_context_free(ctx);
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to allocate table");
    return NULL;
  }

  memset(table, 0, sizeof(GTEXT_CSV_Table));
  table->ctx = ctx;
  table->row_capacity = 16;
  table->rows = (csv_table_row *)csv_arena_alloc_for_context(
      ctx, sizeof(csv_table_row) * table->row_capacity, 8);
  if (!table->rows) {
    free(table);
    csv_context_free(ctx);
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to allocate rows");
    return NULL;
  }

  return table;
}

// Parse CSV using streaming parser and build table
static GTEXT_CSV_Status csv_table_parse_internal(GTEXT_CSV_Table * table,
    const char * input, size_t input_len, const GTEXT_CSV_Parse_Options * opts,
    GTEXT_CSV_Error * err) {
  // Parse context
  csv_table_parse_context parse_ctx = {.table = table,
      .current_row = NULL,
      .current_field_index = 0,
      .current_field_capacity = 0,
      .opts = opts,
      .err = err,
      .status = GTEXT_CSV_OK};

  // Create streaming parser
  GTEXT_CSV_Stream * stream =
      gtext_csv_stream_new(opts, csv_table_event_callback, &parse_ctx);
  if (!stream) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to create stream parser");
    return GTEXT_CSV_E_OOM;
  }

  // Set original input buffer for in-situ mode and error context snippets
  // Always set it for table parsing so we can generate context snippets on
  // errors Use the input parameter directly (which may have been adjusted for
  // BOM)
  csv_stream_set_original_input_buffer(stream, input, input_len);

  // Feed input
  GTEXT_CSV_Status status =
      gtext_csv_stream_feed(stream, input, input_len, err);
  if (status == GTEXT_CSV_OK) {
    status = gtext_csv_stream_finish(stream, err);
  }

  if (status == GTEXT_CSV_OK && parse_ctx.status != GTEXT_CSV_OK) {
    status = parse_ctx.status;
  }

  gtext_csv_stream_free(stream);
  return status;
}

GTEXT_API GTEXT_CSV_Table * gtext_csv_parse_table(const void * data, size_t len,
    const GTEXT_CSV_Parse_Options * opts, GTEXT_CSV_Error * err) {
  if (!data) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Input data must not be NULL");
    return NULL;
  }

  // Empty input is valid - return empty table
  if (len == 0) {
    return csv_create_empty_table(err);
  }

  GTEXT_CSV_Parse_Options default_opts;
  if (!opts) {
    default_opts = gtext_csv_parse_options_default();
    opts = &default_opts;
  }

  // Create context and allocate table structure
  csv_context * ctx = csv_context_new();
  if (!ctx) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to create context");
    return NULL;
  }

  GTEXT_CSV_Table * table = (GTEXT_CSV_Table *)malloc(sizeof(GTEXT_CSV_Table));
  if (!table) {
    csv_context_free(ctx);
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to allocate table");
    return NULL;
  }

  memset(table, 0, sizeof(GTEXT_CSV_Table));
  table->ctx = ctx;
  table->row_capacity = 16;
  table->rows = (csv_table_row *)csv_arena_alloc_for_context(
      ctx, sizeof(csv_table_row) * table->row_capacity, 8);
  if (!table->rows) {
    free(table);
    csv_context_free(ctx);
    CSV_SET_ERROR(err, GTEXT_CSV_E_OOM, "Failed to allocate rows");
    return NULL;
  }

  // Handle BOM (must be done before setting input buffer for in-situ mode)
  const char * input = (const char *)data;
  size_t input_len = len;
  csv_position pos = {0, 1, 1};
  if (!opts->keep_bom) {
    bool was_stripped = false;
    GTEXT_CSV_Status status =
        csv_strip_bom(&input, &input_len, &pos, true, &was_stripped);
    if (status != GTEXT_CSV_OK) {
      CSV_SET_ERROR(err, status, "Overflow in BOM stripping");
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
  GTEXT_CSV_Status status =
      csv_table_parse_internal(table, input, input_len, opts, err);
  if (status != GTEXT_CSV_OK) {
    gtext_csv_free_table(table);
    return NULL;
  }

  // Set column count from first row (if table has rows)
  // This is needed for tables without headers, and will be overridden for
  // tables with headers
  if (table->row_count > 0) {
    csv_table_row * first_row = &table->rows[0];
    table->column_count = first_row->field_count;
  }

  // Process header if enabled
  if (!opts->dialect.treat_first_row_as_header || table->row_count == 0) {
    return table;
  }

  // Build header map from first row
  table->header_map_size = 16;
  table->header_map = (csv_header_entry **)calloc(
      table->header_map_size, sizeof(csv_header_entry *));
  if (!table->header_map) {
    gtext_csv_free_table(table);
    return NULL;
  }

  csv_table_row * header_row = &table->rows[0];
  // Column count already set above, but ensure it's correct
  table->column_count = header_row->field_count;
  for (size_t i = 0; i < header_row->field_count; i++) {
    csv_table_field * field = &header_row->fields[i];
    size_t hash =
        csv_header_hash(field->data, field->length, table->header_map_size);

    // Check for duplicates
    csv_header_entry * entry = table->header_map[hash];
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
      case GTEXT_CSV_DUPCOL_ERROR:
        free(table->header_map);
        table->header_map = NULL;
        gtext_csv_free_table(table);
        CSV_SET_ERROR(
            err, GTEXT_CSV_E_INVALID, "Duplicate column name in header");
        if (err) {
          err->col_index = i;
        }
        return NULL;
      case GTEXT_CSV_DUPCOL_FIRST_WINS:
        // Skip this duplicate
        continue;
      case GTEXT_CSV_DUPCOL_LAST_WINS:
        // Remove old entry, add new one
        // Simplified: just add new entry
        break;
      case GTEXT_CSV_DUPCOL_COLLECT:
        // Store multiple indices (simplified: just add)
        break;
      }
    }

    // Create header entry
    csv_header_entry * new_entry =
        (csv_header_entry *)csv_arena_alloc_for_context(
            ctx, sizeof(csv_header_entry), 8);
    if (new_entry) {
      new_entry->name = field->data;
      new_entry->name_len = field->length;
      new_entry->index = i;
      new_entry->next = table->header_map[hash];
      table->header_map[hash] = new_entry;

      // Update reverse mapping
      csv_set_index_to_entry(table, i, new_entry);
    }
  }
  table->has_header = true;

  return table;
}

GTEXT_API void gtext_csv_free_table(GTEXT_CSV_Table * table) {
  if (!table) {
    return;
  }

  if (table->header_map) {
    free(table->header_map);
  }

  csv_context_free(table->ctx);
  free(table);
}

GTEXT_API size_t gtext_csv_row_count(const GTEXT_CSV_Table * table) {
  if (!table) {
    return 0;
  }

  // Exclude header row if present
  if (table->has_header && table->row_count > 0) {
    return table->row_count - 1;
  }
  return table->row_count;
}

GTEXT_API size_t gtext_csv_col_count(
    const GTEXT_CSV_Table * table, size_t row) {
  if (!table) {
    return 0;
  }

  // Adjust for header row
  size_t adjusted_row = row;
  if (table->has_header) {
    adjusted_row = row + 1; // Skip header row
  }

  if (adjusted_row >= table->row_count) {
    return 0;
  }

  return table->rows[adjusted_row].field_count;
}

GTEXT_API const char * gtext_csv_field(
    const GTEXT_CSV_Table * table, size_t row, size_t col, size_t * len) {
  if (!table) {
    if (len) {
      *len = 0;
    }
    return NULL;
  }

  // Adjust for header row
  size_t adjusted_row = row;
  if (table->has_header) {
    adjusted_row = row + 1; // Skip header row
  }

  if (adjusted_row >= table->row_count) {
    if (len) {
      *len = 0;
    }
    return NULL;
  }

  csv_table_row * table_row = &table->rows[adjusted_row];
  if (col >= table_row->field_count) {
    if (len) {
      *len = 0;
    }
    return NULL;
  }

  csv_table_field * field = &table_row->fields[col];
  if (len) {
    *len = field->length;
  }
  return field->data;
}

static GTEXT_CSV_Status csv_row_prepare_fields(GTEXT_CSV_Table * table,
    const char * const * fields, const size_t * field_lengths,
    size_t field_count, const char ** allocated_data,
    size_t * allocated_lengths, char ** bulk_arena_data_out) {
  // Phase 2: Calculate total size needed for all field data (bulk allocation)
  // This ensures atomic operation - if validation or allocation fails, table
  // remains unchanged
  size_t total_size = 0; // Total bytes needed for all fields

  // Single pass: validate and calculate total size needed
  for (size_t i = 0; i < field_count; i++) {
    const char * field_data = fields[i];

    // Calculate field length using helper function
    size_t field_len = csv_calculate_field_length(field_data, field_lengths, i);
    allocated_lengths[i] = field_len;

    // Validate: if explicit length is provided and non-zero, field_data must
    // not be NULL
    if (field_lengths && field_len > 0 && !field_data) {
      return GTEXT_CSV_E_INVALID;
    }

    // Use global empty string constant for empty fields (saves arena
    // allocation)
    if (field_len == 0) {
      allocated_data[i] = csv_empty_field_string;
      continue;
    }

    // Check for overflow in field_len + 1
    if (field_len > SIZE_MAX - 1) {
      return GTEXT_CSV_E_OOM;
    }

    // Check for overflow in total_size accumulation
    size_t field_size = field_len + 1; // +1 for null terminator
    if (total_size > SIZE_MAX - field_size) {
      return GTEXT_CSV_E_OOM;
    }

    total_size += field_size;
  }

  // Phase 3: Bulk Field Data Allocation
  // Allocate one contiguous block for all non-empty fields
  char * bulk_arena_data = NULL;
  if (total_size > 0) {
    bulk_arena_data =
        (char *)csv_arena_alloc_for_context(table->ctx, total_size, 1);
    if (!bulk_arena_data) {
      // Allocation failed - table remains unchanged (atomic operation)
      return GTEXT_CSV_E_OOM;
    }
  }

  // Copy field data into the allocated block and set pointers
  char * current_ptr = bulk_arena_data;
  for (size_t i = 0; i < field_count; i++) {
    size_t field_len = allocated_lengths[i];

    // Empty fields already have pointers set to csv_empty_field_string
    if (field_len == 0) {
      continue;
    }

    // Copy field data (field_data is guaranteed to be non-NULL here due to
    // validation above)
    const char * field_data = fields[i];
    memcpy(current_ptr, field_data, field_len);
    current_ptr[field_len] = '\0';

    allocated_data[i] = current_ptr;
    current_ptr += field_len + 1; // Move to next field position
  }

  *bulk_arena_data_out = bulk_arena_data;
  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_row_allocate_structures(GTEXT_CSV_Table * table,
    size_t field_count, csv_table_field ** new_fields_out,
    csv_table_row ** new_rows_out, size_t * new_capacity_out) {
  // Phase 4: Field Array Allocation
  csv_table_field * new_fields = (csv_table_field *)csv_arena_alloc_for_context(
      table->ctx, sizeof(csv_table_field) * field_count, 8);
  if (!new_fields) {
    // Allocation failed - bulk data already allocated, but that's okay (in
    // arena, will be freed with table)
    return GTEXT_CSV_E_OOM;
  }

  // Phase 5: Row Capacity Growth (if needed)
  // Pre-allocate row capacity but don't update table->rows yet
  csv_table_row * new_rows = table->rows;
  size_t new_capacity = table->row_capacity;
  if (table->row_count >= table->row_capacity) {
    new_capacity = table->row_capacity * 2;
    // Check for overflow
    if (new_capacity < table->row_capacity) {
      return GTEXT_CSV_E_OOM;
    }
    new_rows = (csv_table_row *)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_table_row) * new_capacity, 8);
    if (!new_rows) {
      // Allocation failed - previous allocations remain in arena
      return GTEXT_CSV_E_OOM;
    }
    // Copy existing rows (but don't update table->rows yet)
    memcpy(new_rows, table->rows, sizeof(csv_table_row) * table->row_count);
  }

  *new_fields_out = new_fields;
  *new_rows_out = new_rows;
  *new_capacity_out = new_capacity;
  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_row_append(GTEXT_CSV_Table * table,
    const char * const * fields, const size_t * field_lengths,
    size_t field_count, GTEXT_CSV_Error * err) {
  // Phase 1: Validation
  if (!table) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Table must not be NULL");
    }
    return GTEXT_CSV_E_INVALID;
  }
  if (!fields) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Fields array must not be NULL");
    }
    return GTEXT_CSV_E_INVALID;
  }
  if (field_count == 0) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Field count must be greater than 0");
    }
    return GTEXT_CSV_E_INVALID;
  }

  // Adjust row_count for header if present
  size_t data_row_count = csv_get_data_row_count(table);

  // Validate column count consistency (but don't update column_count yet)
  if (!table->allow_irregular_rows) {
    // Strict mode: enforce rectangular structure
    if (data_row_count == 0 && table->column_count == 0) {
      // First data row will set column count - validation passes
    }
    else {
      // Subsequent rows (or first data row when header exists) must match
      // column count
      if (field_count != table->column_count) {
        size_t expected_count = table->column_count;
        if (table->has_header && table->row_count > 0) {
          // Use header row field count if column_count is 0
          expected_count = table->rows[0].field_count;
        }
        csv_set_field_count_error(err, expected_count, field_count, SIZE_MAX);
        return GTEXT_CSV_E_INVALID;
      }
    }
  }
  else {
    // Irregular mode: accept any field count, will update column_count to max
    // No validation needed here - all field counts are accepted
  }

  // Phase 2-3: Prepare fields (calculate lengths, validate, allocate bulk data)
  const char * allocated_data[field_count]; // VLA to store allocated pointers
  size_t allocated_lengths[field_count];    // VLA to store lengths
  char * bulk_arena_data = NULL;
  GTEXT_CSV_Status status = csv_row_prepare_fields(table, fields, field_lengths,
      field_count, allocated_data, allocated_lengths, &bulk_arena_data);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Phase 4-5: Allocate structures (field array, row capacity growth if needed)
  csv_table_field * new_fields;
  csv_table_row * new_rows;
  size_t new_capacity;
  status = csv_row_allocate_structures(
      table, field_count, &new_fields, &new_rows, &new_capacity);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Phase 6: Atomic State Update
  // Only after all allocations succeed:
  // 1. Update row capacity if it was grown
  if (new_rows != table->rows) {
    table->rows = new_rows;
    table->row_capacity = new_capacity;
  }

  // 2. Get pointer to new row (now safe since row capacity is updated if
  // needed)
  csv_table_row * new_row = &table->rows[table->row_count];

  // 3. Set up field structures
  for (size_t i = 0; i < field_count; i++) {
    csv_table_field * field = &new_fields[i];
    field->data = allocated_data[i];
    field->length = allocated_lengths[i];
    // For empty fields: points to global constant, not in-situ
    // For non-empty fields: copied to arena, not in-situ
    field->is_in_situ = false;
  }

  // 4. Set row structure
  new_row->fields = new_fields;
  new_row->field_count = field_count;

  // 5. Update column_count
  if (data_row_count == 0 && table->column_count == 0) {
    // First data row sets column count
    table->column_count = field_count;
  }
  else if (table->allow_irregular_rows) {
    // Irregular mode: update column_count to maximum
    if (field_count > table->column_count) {
      table->column_count = field_count;
    }
  }

  // 6. Increment row count
  table->row_count++;

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_row_insert(GTEXT_CSV_Table * table,
    size_t row_idx, const char * const * fields, const size_t * field_lengths,
    size_t field_count, GTEXT_CSV_Error * err) {
  // Phase 1: Validation
  if (!table) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Table must not be NULL");
    }
    return GTEXT_CSV_E_INVALID;
  }
  if (!fields) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Fields array must not be NULL");
    }
    return GTEXT_CSV_E_INVALID;
  }
  if (field_count == 0) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Field count must be greater than 0");
    }
    return GTEXT_CSV_E_INVALID;
  }

  // Calculate data row count (excluding header if present)
  size_t data_row_count = csv_get_data_row_count(table);

  // Validate row_idx (must be <= data_row_count, allowing append)
  if (row_idx > data_row_count) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Row index out of bounds");
    }
    return GTEXT_CSV_E_INVALID;
  }

  // Adjust row_idx for header if present (header is at index 0, data starts at
  // 1) row_idx is 0-based for data rows only, so we add 1 if header exists
  size_t adjusted_row_idx = row_idx;
  if (table->has_header) {
    adjusted_row_idx = row_idx + 1;
  }

  // Validate column count consistency (but don't update column_count yet)
  if (!table->allow_irregular_rows) {
    // Strict mode: enforce rectangular structure
    if (data_row_count == 0 && table->column_count == 0) {
      // First data row will set column count - validation passes
    }
    else {
      // Subsequent rows (or first data row when header exists) must match
      // column count
      if (field_count != table->column_count) {
        size_t expected_count = table->column_count;
        if (table->has_header && table->row_count > 0) {
          // Use header row field count if column_count is 0
          expected_count = table->rows[0].field_count;
        }
        csv_set_field_count_error(err, expected_count, field_count, row_idx);
        return GTEXT_CSV_E_INVALID;
      }
    }
  }
  else {
    // Irregular mode: accept any field count, will update column_count to max
    // No validation needed here - all field counts are accepted
  }

  // Check if inserting at end (equivalent to append)
  bool is_append = (row_idx == data_row_count);

  // Phase 2-3: Prepare fields (calculate lengths, validate, allocate bulk data)
  const char * allocated_data[field_count]; // VLA to store allocated pointers
  size_t allocated_lengths[field_count];    // VLA to store lengths
  char * bulk_arena_data = NULL;
  GTEXT_CSV_Status status = csv_row_prepare_fields(table, fields, field_lengths,
      field_count, allocated_data, allocated_lengths, &bulk_arena_data);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Phase 4-5: Allocate structures (field array, row capacity growth if needed)
  csv_table_field * new_fields;
  csv_table_row * new_rows;
  size_t new_capacity;
  status = csv_row_allocate_structures(
      table, field_count, &new_fields, &new_rows, &new_capacity);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Phase 6: Row Shifting (if not appending)
  // Critical: Only shift rows AFTER all allocations succeed
  // This creates the gap for the new row
  if (!is_append) {
    // Shift rows from adjusted_row_idx to row_count-1 one position right
    // Use new_rows array (which may be the same as table->rows if capacity
    // didn't grow) Shift in reverse order to avoid overwriting
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

  // 2. Get pointer to new row (now safe since row capacity is updated if
  // needed)
  csv_table_row * new_row;
  if (is_append) {
    new_row = &table->rows[table->row_count];
  }
  else {
    new_row = &table->rows[adjusted_row_idx];
  }

  // 3. Set up field structures
  for (size_t i = 0; i < field_count; i++) {
    csv_table_field * field = &new_fields[i];
    field->data = allocated_data[i];
    field->length = allocated_lengths[i];
    // For empty fields: points to global constant, not in-situ
    // For non-empty fields: copied to arena, not in-situ
    field->is_in_situ = false;
  }

  // 4. Set row structure
  new_row->fields = new_fields;
  new_row->field_count = field_count;

  // 5. Update column_count
  if (data_row_count == 0 && table->column_count == 0) {
    // First data row sets column count
    table->column_count = field_count;
  }
  else if (table->allow_irregular_rows) {
    // Irregular mode: update column_count to maximum
    if (field_count > table->column_count) {
      table->column_count = field_count;
    }
  }

  // 6. Increment row count
  table->row_count++;

  return GTEXT_CSV_OK;
}

static size_t csv_recalculate_max_column_count(const GTEXT_CSV_Table * table) {
  if (!table || table->row_count == 0) {
    return 0;
  }

  size_t max_count = 0;
  for (size_t i = 0; i < table->row_count; i++) {
    if (table->rows[i].field_count > max_count) {
      max_count = table->rows[i].field_count;
    }
  }

  return max_count;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_row_remove(
    GTEXT_CSV_Table * table, size_t row_idx) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Calculate data row count (excluding header if present)
  size_t data_row_count = csv_get_data_row_count(table);

  // Validate row_idx (must be < data_row_count)
  if (row_idx >= data_row_count) {
    return GTEXT_CSV_E_INVALID;
  }

  // Adjust row_idx for header if present (header is at index 0, data starts at
  // 1) Note: Header row is protected because it's not accessible via external
  // API (row_idx is 0-based for data rows only)
  size_t adjusted_row_idx = row_idx;
  if (table->has_header) {
    adjusted_row_idx = row_idx + 1;
  }

  // Validate adjusted row_idx < row_count
  if (adjusted_row_idx >= table->row_count) {
    return GTEXT_CSV_E_INVALID;
  }

  // Store the removed row's field_count before shifting (needed for column_count
  // recalculation in irregular mode)
  size_t removed_row_field_count = table->rows[adjusted_row_idx].field_count;

  // Shift rows from adjusted_row_idx+1 to row_count-1 one position left
  // Shift in forward order (left shift)
  for (size_t i = adjusted_row_idx; i < table->row_count - 1; i++) {
    table->rows[i] = table->rows[i + 1];
  }

  // Decrement row count
  table->row_count--;

  // Recalculate column_count if irregular rows are allowed and the removed row
  // had the maximum field_count
  if (table->allow_irregular_rows &&
      removed_row_field_count == table->column_count) {
    // The removed row had the maximum, need to recalculate
    table->column_count = csv_recalculate_max_column_count(table);
  }

  // Note: Field data remains in arena (no individual cleanup needed)

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_row_set(GTEXT_CSV_Table * table,
    size_t row_idx, const char * const * fields, const size_t * field_lengths,
    size_t field_count, GTEXT_CSV_Error * err) {
  // Validate inputs
  if (!table) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Table must not be NULL");
    }
    return GTEXT_CSV_E_INVALID;
  }
  if (!fields) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Fields array must not be NULL");
    }
    return GTEXT_CSV_E_INVALID;
  }
  if (field_count == 0) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Field count must be greater than 0");
    }
    return GTEXT_CSV_E_INVALID;
  }

  // Calculate data row count (excluding header if present)
  size_t data_row_count = csv_get_data_row_count(table);

  // Validate row_idx (must be < data_row_count)
  if (row_idx >= data_row_count) {
    if (err) {
      CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Row index out of bounds");
    }
    return GTEXT_CSV_E_INVALID;
  }

  // Adjust row_idx for header if present (header is at index 0, data starts at
  // 1) row_idx is 0-based for data rows only, so we add 1 if header exists
  size_t adjusted_row_idx = row_idx;
  if (table->has_header) {
    adjusted_row_idx = row_idx + 1;
  }

  // Validate field_count matches table column count (unless irregular rows
  // allowed)
  if (!table->allow_irregular_rows) {
    // Strict mode: enforce rectangular structure
    size_t expected_column_count = table->column_count;
    if (expected_column_count == 0 && table->has_header &&
        table->row_count > 0) {
      expected_column_count = table->rows[0].field_count;
    }
    if (field_count != expected_column_count) {
      csv_set_field_count_error(err, expected_column_count, field_count, row_idx);
      return GTEXT_CSV_E_INVALID;
    }
  }
  // Irregular mode: accept any field count, will update row's field_count and
  // column_count to max if needed

  // Get existing row
  csv_table_row * existing_row = &table->rows[adjusted_row_idx];

  // Phase 1: Validate fields and calculate total size needed in one pass
  // This ensures atomic operation - if validation or allocation fails, row
  // remains unchanged Also more efficient: one allocation call instead of N
  // calls
  const char * allocated_data[field_count]; // VLA to store allocated pointers
  size_t allocated_lengths[field_count];    // VLA to store lengths
  size_t total_size = 0;                    // Total bytes needed for all fields

  // Single pass: validate and calculate total size needed
  for (size_t i = 0; i < field_count; i++) {
    const char * field_data = fields[i];

    // Calculate field length using helper function
    size_t field_len = csv_calculate_field_length(field_data, field_lengths, i);
    allocated_lengths[i] = field_len;

    // Validate: if explicit length is provided and non-zero, field_data must
    // not be NULL
    if (field_lengths && field_len > 0 && !field_data) {
      return GTEXT_CSV_E_INVALID;
    }

    // Use global empty string constant for empty fields (saves arena
    // allocation)
    if (field_len == 0) {
      allocated_data[i] = csv_empty_field_string;
      continue;
    }

    // Check for overflow in field_len + 1
    if (field_len > SIZE_MAX - 1) {
      return GTEXT_CSV_E_OOM;
    }

    // Check for overflow in total_size accumulation
    size_t field_size = field_len + 1; // +1 for null terminator
    if (total_size > SIZE_MAX - field_size) {
      return GTEXT_CSV_E_OOM;
    }

    total_size += field_size;
  }

  // Allocate one contiguous block for all non-empty fields
  char * bulk_arena_data = NULL;
  if (total_size > 0) {
    bulk_arena_data =
        (char *)csv_arena_alloc_for_context(table->ctx, total_size, 1);
    if (!bulk_arena_data) {
      // Allocation failed - row remains unchanged (atomic operation)
      return GTEXT_CSV_E_OOM;
    }
  }

  // Second pass: copy field data into the allocated block and set pointers
  char * current_ptr = bulk_arena_data;
  for (size_t i = 0; i < field_count; i++) {
    size_t field_len = allocated_lengths[i];

    // Empty fields already have pointers set to csv_empty_field_string
    if (field_len == 0) {
      continue;
    }

    // Copy field data (field_data is guaranteed to be non-NULL here due to
    // validation above)
    const char * field_data = fields[i];
    memcpy(current_ptr, field_data, field_len);
    current_ptr[field_len] = '\0';

    allocated_data[i] = current_ptr;
    current_ptr += field_len + 1; // Move to next field position
  }

  // Phase 2: Update all field structures atomically (all allocations succeeded)
  size_t old_field_count = existing_row->field_count;
  bool field_count_changed = (field_count != old_field_count);

  if (field_count_changed && table->allow_irregular_rows) {
    // Field count changed: need to allocate new field array
    csv_table_field * new_fields =
        (csv_table_field *)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * field_count, 8);
    if (!new_fields) {
      // Allocation failed - row remains unchanged (atomic operation)
      return GTEXT_CSV_E_OOM;
    }

    // Copy existing fields (up to min(old_count, new_count))
    size_t fields_to_copy =
        (old_field_count < field_count) ? old_field_count : field_count;
    if (fields_to_copy > 0 && existing_row->fields) {
      memcpy(new_fields, existing_row->fields,
          sizeof(csv_table_field) * fields_to_copy);
    }

    // Update all fields with new data
    for (size_t i = 0; i < field_count; i++) {
      csv_table_field * field = &new_fields[i];
      // Update field structure (replace field data pointers)
      // Note: Old field data remains in arena (no individual cleanup needed)
      field->data = allocated_data[i];
      field->length = allocated_lengths[i];
      // For empty fields: points to global constant, not in-situ
      // For non-empty fields: copied to arena, not in-situ
      field->is_in_situ = false;
    }

    // Update row structure with new field array
    existing_row->fields = new_fields;
    existing_row->field_count = field_count;
  }
  else {
    // Field count unchanged: update fields in place
    for (size_t i = 0; i < field_count; i++) {
      csv_table_field * field = &existing_row->fields[i];
      // Update field structure (replace field data pointers)
      // Note: Old field data remains in arena (no individual cleanup needed)
      field->data = allocated_data[i];
      field->length = allocated_lengths[i];
      // For empty fields: points to global constant, not in-situ
      // For non-empty fields: copied to arena, not in-situ
      field->is_in_situ = false;
    }
    // Field count is already set correctly (existing_row->field_count ==
    // field_count) No need to update it since we're replacing fields in place
  }

  // Update column_count to max if irregular rows are allowed
  if (table->allow_irregular_rows && field_count > table->column_count) {
    table->column_count = field_count;
  }

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_field_set(GTEXT_CSV_Table * table,
    size_t row, size_t col, const char * field_data, size_t field_length) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }
  // Allow NULL field_data only when field_length is 0 (empty field)
  // This is consistent with the writer API which allows NULL for empty fields
  if (!field_data && field_length != 0) {
    return GTEXT_CSV_E_INVALID;
  }

  // Adjust row index for header if present (header is at index 0, data starts
  // at 1)
  size_t adjusted_row = row;
  if (table->has_header) {
    adjusted_row = row + 1;
  }

  // Validate row index
  if (adjusted_row >= table->row_count) {
    return GTEXT_CSV_E_INVALID;
  }

  // Get the row
  csv_table_row * table_row = &table->rows[adjusted_row];

  // Validate column index
  if (col >= table_row->field_count) {
    return GTEXT_CSV_E_INVALID;
  }

  // Get existing field
  csv_table_field * field = &table_row->fields[col];

  // Determine field length
  // Note: gtext_csv_field_set uses field_length parameter directly (not an
  // array)
  size_t field_len;
  if (field_length == 0) {
    field_len = csv_calculate_field_length(field_data, NULL, 0);
  }
  else {
    field_len = field_length;
  }

  // Use global empty string constant for empty fields (saves arena allocation)
  if (field_len == 0) {
    csv_setup_empty_field(field);
    return GTEXT_CSV_OK;
  }

  // Allocate and copy field data to arena
  GTEXT_CSV_Status status =
      csv_allocate_and_copy_field(table->ctx, field_data, field_len, field);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Note: Old field data remains in arena (no individual cleanup needed)

  return GTEXT_CSV_OK;
}

// ============================================================================
// Compact operation helper functions
// ============================================================================

static GTEXT_CSV_Status csv_calculate_compact_size(
    const GTEXT_CSV_Table * table, size_t * total_size_out) {
  if (!table || !total_size_out) {
    return GTEXT_CSV_E_INVALID;
  }

  size_t total_size = 0;

  // Rows array (aligned to 8 bytes)
  size_t rows_array_size = sizeof(csv_table_row) * table->row_capacity;
  size_t rows_array_aligned = (rows_array_size + 7) & ~7; // Align to 8
  total_size = rows_array_aligned;

  // Calculate size for all rows and fields
  for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
    csv_table_row * old_row = &table->rows[row_idx];

    if (old_row->field_count == 0) {
      continue;
    }

    // Field array (aligned to 8 bytes)
    size_t field_array_size = sizeof(csv_table_field) * old_row->field_count;
    size_t field_array_aligned = (field_array_size + 7) & ~7;
    if (total_size > SIZE_MAX - field_array_aligned) {
      return GTEXT_CSV_E_OOM;
    }
    total_size += field_array_aligned;

    // Field data (only for non-empty, non-in-situ fields)
    for (size_t i = 0; i < old_row->field_count; i++) {
      csv_table_field * old_field = &old_row->fields[i];

      // Skip empty fields (use global constant) and in-situ fields (reference
      // input buffer)
      if (old_field->length == 0 || old_field->is_in_situ) {
        continue;
      }

      // Field data: length + 1 (null terminator), aligned to 1 byte
      size_t field_data_size = old_field->length + 1;
      if (total_size > SIZE_MAX - field_data_size) {
        return GTEXT_CSV_E_OOM;
      }
      total_size += field_data_size;
    }
  }

  // Header map entries (if present)
  if (table->has_header && table->header_map) {
    for (size_t i = 0; i < table->header_map_size; i++) {
      csv_header_entry * entry = table->header_map[i];
      while (entry) {
        // Entry structure (aligned to 8 bytes)
        size_t entry_size = sizeof(csv_header_entry);
        size_t entry_aligned = (entry_size + 7) & ~7;
        if (total_size > SIZE_MAX - entry_aligned) {
          return GTEXT_CSV_E_OOM;
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
            const char * input_start = table->ctx->input_buffer;
            const char * input_end = input_start + table->ctx->input_buffer_len;
            if (entry->name >= input_start && entry->name < input_end) {
              is_in_situ = true;
            }
          }

          if (!is_in_situ) {
            size_t name_size = entry->name_len + 1;
            if (total_size > SIZE_MAX - name_size) {
              return GTEXT_CSV_E_OOM;
            }
            total_size += name_size;
          }
        }

        entry = entry->next;
      }
    }
  }

  // Add some overhead for alignment and safety margin (10% or 1KB, whichever is
  // larger)
  size_t overhead = total_size / 10;
  if (overhead < 1024) {
    overhead = 1024;
  }
  if (total_size > SIZE_MAX - overhead) {
    return GTEXT_CSV_E_OOM;
  }
  total_size += overhead;

  *total_size_out = total_size;
  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_preallocate_compact_structures(
    const GTEXT_CSV_Table * table, csv_context * new_ctx,
    csv_compact_structures * structures_out) {
  if (!table || !new_ctx || !structures_out) {
    return GTEXT_CSV_E_INVALID;
  }

  // Initialize output structure
  structures_out->new_ctx = new_ctx;
  structures_out->new_rows = NULL;
  structures_out->new_field_arrays = NULL;
  structures_out->new_field_data_ptrs = NULL;

  // Allocate new rows array in new arena
  csv_table_row * new_rows = (csv_table_row *)csv_arena_alloc_for_context(
      new_ctx, sizeof(csv_table_row) * table->row_capacity, 8);
  if (!new_rows) {
    return GTEXT_CSV_E_OOM;
  }

  // Initialize new rows array (zero out unused entries)
  memset(new_rows, 0, sizeof(csv_table_row) * table->row_capacity);
  structures_out->new_rows = new_rows;

  // Pre-allocate all field arrays and field data
  // Store pointers in temporary arrays
  csv_table_field ** new_field_arrays = NULL;
  char *** new_field_data_ptrs = NULL; // Array of arrays of char * pointers
  if (table->row_count > 0) {
    new_field_arrays = (csv_table_field **)malloc(
        sizeof(csv_table_field *) * table->row_count);
    new_field_data_ptrs = (char ***)malloc(sizeof(char **) * table->row_count);
    if (!new_field_arrays || !new_field_data_ptrs) {
      free(new_field_arrays);
      free(new_field_data_ptrs);
      return GTEXT_CSV_E_OOM;
    }
  }
  structures_out->new_field_arrays = new_field_arrays;
  structures_out->new_field_data_ptrs = new_field_data_ptrs;

  // Pre-allocate all field arrays and field data blocks
  for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
    csv_table_row * old_row = &table->rows[row_idx];
    new_field_arrays[row_idx] = NULL;
    new_field_data_ptrs[row_idx] = NULL;

    // Skip empty rows
    if (old_row->field_count == 0) {
      continue;
    }

    // Allocate field array in new arena
    csv_table_field * new_fields =
        (csv_table_field *)csv_arena_alloc_for_context(
            new_ctx, sizeof(csv_table_field) * old_row->field_count, 8);
    if (!new_fields) {
      // Free temporary arrays
      for (size_t j = 0; j < row_idx; j++) {
        free(new_field_data_ptrs[j]);
      }
      free(new_field_arrays);
      free(new_field_data_ptrs);
      return GTEXT_CSV_E_OOM;
    }
    new_field_arrays[row_idx] = new_fields;

    // Pre-allocate field data for non-empty, non-in-situ fields
    char ** field_data_ptrs = NULL;
    size_t non_in_situ_count = 0;
    for (size_t i = 0; i < old_row->field_count; i++) {
      csv_table_field * old_field = &old_row->fields[i];
      if (old_field->length > 0 && !old_field->is_in_situ) {
        non_in_situ_count++;
      }
    }

    if (non_in_situ_count > 0) {
      field_data_ptrs = (char **)malloc(sizeof(char *) * old_row->field_count);
      if (!field_data_ptrs) {
        for (size_t j = 0; j < row_idx; j++) {
          free(new_field_data_ptrs[j]);
        }
        free(new_field_arrays);
        free(new_field_data_ptrs);
        return GTEXT_CSV_E_OOM;
      }

      // Pre-allocate all field data blocks
      for (size_t i = 0; i < old_row->field_count; i++) {
        csv_table_field * old_field = &old_row->fields[i];
        field_data_ptrs[i] = NULL;

        if (old_field->length == 0 || old_field->is_in_situ) {
          continue; // Empty or in-situ, no allocation needed
        }

        // Check for overflow
        if (old_field->length > SIZE_MAX - 1) {
          free(field_data_ptrs);
          for (size_t j = 0; j < row_idx; j++) {
            free(new_field_data_ptrs[j]);
          }
          free(new_field_arrays);
          free(new_field_data_ptrs);
          return GTEXT_CSV_E_OOM;
        }

        char * field_data = (char *)csv_arena_alloc_for_context(
            new_ctx, old_field->length + 1, 1);
        if (!field_data) {
          // Free temporary arrays
          free(field_data_ptrs);
          for (size_t j = 0; j < row_idx; j++) {
            free(new_field_data_ptrs[j]);
          }
          free(new_field_arrays);
          free(new_field_data_ptrs);
          return GTEXT_CSV_E_OOM;
        }
        field_data_ptrs[i] = field_data;
      }
    }
    new_field_data_ptrs[row_idx] = field_data_ptrs;
  }

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_copy_data_to_new_arena(
    const GTEXT_CSV_Table * table, const csv_compact_structures * structures) {
  if (!table || !structures) {
    return GTEXT_CSV_E_INVALID;
  }

  // Copy all row and field data
  for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
    csv_table_row * old_row = &table->rows[row_idx];
    csv_table_row * new_row = &structures->new_rows[row_idx];

    // Skip empty rows
    if (old_row->field_count == 0) {
      new_row->fields = NULL;
      new_row->field_count = 0;
      continue;
    }

    csv_table_field * new_fields = structures->new_field_arrays[row_idx];
    char ** field_data_ptrs = structures->new_field_data_ptrs[row_idx];

    // Copy each field
    for (size_t i = 0; i < old_row->field_count; i++) {
      csv_table_field * old_field = &old_row->fields[i];
      csv_table_field * new_field = &new_fields[i];

      // Copy field data
      if (old_field->length == 0) {
        // Empty field - use global constant
        new_field->data = csv_empty_field_string;
        new_field->length = 0;
        new_field->is_in_situ = false;
      }
      else if (old_field->is_in_situ) {
        // In-situ field: preserve reference to input buffer (caller-owned)
        new_field->data = old_field->data;
        new_field->length = old_field->length;
        new_field->is_in_situ = true;
      }
      else {
        // Arena-allocated field: copy to pre-allocated block
        char * field_data = field_data_ptrs[i];
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

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_rebuild_header_map(const GTEXT_CSV_Table * table,
    const csv_context * old_ctx, csv_context * new_ctx,
    csv_compact_header_map * header_map_out) {
  if (!table || !old_ctx || !new_ctx || !header_map_out) {
    return GTEXT_CSV_E_INVALID;
  }

  // Initialize output structure
  header_map_out->new_header_map = NULL;
  header_map_out->new_entry_ptrs = NULL;
  header_map_out->new_name_ptrs = NULL;
  header_map_out->total_header_entries = 0;

  if (!table->has_header || !table->header_map) {
    return GTEXT_CSV_OK; // No header map to rebuild
  }

  // Count total header map entries
  size_t total_header_entries = 0;
  for (size_t i = 0; i < table->header_map_size; i++) {
    csv_header_entry * entry = table->header_map[i];
    while (entry) {
      total_header_entries++;
      entry = entry->next;
    }
  }

  // Allocate new header map array (malloc'd, not in arena)
  // Always allocate, even if empty, to replace old map
  csv_header_entry ** new_header_map = (csv_header_entry **)calloc(
      table->header_map_size, sizeof(csv_header_entry *));
  if (!new_header_map) {
    return GTEXT_CSV_E_OOM;
  }

  csv_header_entry ** new_entry_ptrs = NULL;
  char ** new_name_ptrs = NULL;

  if (total_header_entries > 0) {
    new_entry_ptrs = (csv_header_entry **)malloc(
        sizeof(csv_header_entry *) * total_header_entries);
    new_name_ptrs = (char **)malloc(sizeof(char *) * total_header_entries);
    if (!new_entry_ptrs || !new_name_ptrs) {
      free(new_header_map);
      free(new_entry_ptrs);
      free(new_name_ptrs);
      return GTEXT_CSV_E_OOM;
    }

    // Pre-allocate all header map entries and name strings
    size_t entry_idx = 0;
    for (size_t i = 0; i < table->header_map_size; i++) {
      csv_header_entry * old_entry = table->header_map[i];

      while (old_entry) {
        // Allocate new entry in new arena
        csv_header_entry * new_entry =
            (csv_header_entry *)csv_arena_alloc_for_context(
                new_ctx, sizeof(csv_header_entry), 8);
        if (!new_entry) {
          // Free temporary arrays
          free(new_header_map);
          free(new_entry_ptrs);
          free(new_name_ptrs);
          return GTEXT_CSV_E_OOM;
        }
        new_entry_ptrs[entry_idx] = new_entry;
        new_name_ptrs[entry_idx] = NULL;

        // Pre-allocate name string if needed
        if (old_entry->name_len == 0) {
          // Empty name - will use csv_empty_field_string
        }
        else {
          // Check if name is in-situ (points to input buffer)
          bool name_is_in_situ = false;
          if (old_ctx->input_buffer && old_entry->name) {
            const char * input_start = old_ctx->input_buffer;
            const char * input_end = input_start + old_ctx->input_buffer_len;
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
              return GTEXT_CSV_E_OOM;
            }
            char * name_data = (char *)csv_arena_alloc_for_context(
                new_ctx, old_entry->name_len + 1, 1);
            if (!name_data) {
              free(new_header_map);
              free(new_entry_ptrs);
              free(new_name_ptrs);
              return GTEXT_CSV_E_OOM;
            }
            new_name_ptrs[entry_idx] = name_data;
          }
        }

        entry_idx++;
        old_entry = old_entry->next;
      }
    }
  }

  // Copy header map data
  if (total_header_entries > 0) {
    size_t entry_idx = 0;
    for (size_t i = 0; i < table->header_map_size; i++) {
      csv_header_entry * old_entry = table->header_map[i];
      csv_header_entry ** new_chain = &new_header_map[i];

      while (old_entry) {
        csv_header_entry * new_entry = new_entry_ptrs[entry_idx];
        char * name_data = new_name_ptrs[entry_idx];

        // Copy name string
        if (old_entry->name_len == 0) {
          new_entry->name = csv_empty_field_string;
        }
        else {
          // Check if name is in-situ
          bool name_is_in_situ = false;
          if (old_ctx->input_buffer && old_entry->name) {
            const char * input_start = old_ctx->input_buffer;
            const char * input_end = input_start + old_ctx->input_buffer_len;
            if (old_entry->name >= input_start && old_entry->name < input_end) {
              name_is_in_situ = true;
            }
          }

          if (name_is_in_situ) {
            // Preserve in-situ reference
            new_entry->name = old_entry->name;
          }
          else {
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
        }
        else {
          // Find end of chain
          csv_header_entry * chain_end = *new_chain;
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

  header_map_out->new_header_map = new_header_map;
  header_map_out->total_header_entries = total_header_entries;
  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_table_compact(GTEXT_CSV_Table * table) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Calculate total size needed for compaction
  size_t total_size = 0;
  GTEXT_CSV_Status status = csv_calculate_compact_size(table, &total_size);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Create new context/arena with calculated block size
  csv_context * new_ctx = csv_context_new_with_block_size(total_size);
  if (!new_ctx) {
    return GTEXT_CSV_E_OOM;
  }

  // Pre-allocate all structures in new arena
  csv_compact_structures structures;
  status = csv_preallocate_compact_structures(table, new_ctx, &structures);
  if (status != GTEXT_CSV_OK) {
    csv_context_free(new_ctx);
    return status;
  }

  // Copy all data to pre-allocated structures
  status = csv_copy_data_to_new_arena(table, &structures);
  if (status != GTEXT_CSV_OK) {
    // Free temporary arrays
    if (structures.new_field_data_ptrs) {
      for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
        free(structures.new_field_data_ptrs[row_idx]);
      }
    }
    free(structures.new_field_arrays);
    free(structures.new_field_data_ptrs);
    csv_context_free(new_ctx);
    return status;
  }

  // Free temporary arrays (field data pointers)
  if (structures.new_field_data_ptrs) {
    for (size_t row_idx = 0; row_idx < table->row_count; row_idx++) {
      free(structures.new_field_data_ptrs[row_idx]);
    }
  }
  free(structures.new_field_arrays);
  free(structures.new_field_data_ptrs);

  // Save old context for header map copying (need input_buffer reference)
  csv_context * old_ctx = table->ctx;

  // Rebuild header map in new arena
  csv_compact_header_map header_map;
  status = csv_rebuild_header_map(table, old_ctx, new_ctx, &header_map);
  if (status != GTEXT_CSV_OK) {
    csv_context_free(new_ctx);
    return status;
  }

  // Phase 5: Atomic Context Switch
  // Only after all allocations and copies succeed:
  // 1. Preserve input buffer reference (for in-situ mode, caller-owned)
  new_ctx->input_buffer = old_ctx->input_buffer;
  new_ctx->input_buffer_len = old_ctx->input_buffer_len;

  // 2. Atomically update table structure
  table->ctx = new_ctx;
  table->rows = structures.new_rows;
  if (header_map.new_header_map) {
    // Free old header map array before updating pointer
    free(table->header_map);
    table->header_map = header_map.new_header_map;

    // Rebuild reverse mapping
    csv_rebuild_index_to_entry(table);
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

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_table_clear(GTEXT_CSV_Table * table) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Phase 1: Save Original State
  // Save original row_count to allow rollback if compaction fails
  size_t original_row_count = table->row_count;

  // Phase 2: Update Row Count
  // If table has headers, keep the header row (set row_count to 1)
  // Otherwise, set row_count to 0 (all rows cleared)
  // This is a simple assignment, no allocation, so it's safe
  if (table->has_header) {
    table->row_count = 1; // Keep header row at index 0
  }
  else {
    table->row_count = 0; // Clear all rows
  }

  // Keep row_capacity (for efficiency)
  // Keep column_count (table structure preserved)
  // Keep header_map (if present, table structure preserved)

  // Phase 3: Compact Table
  // Compact table to free memory from cleared data rows
  // If compaction fails, the table is still valid (just has unused memory)
  // We can either restore the original state or make compaction failure
  // non-fatal For atomicity, we'll restore the original state if compaction
  // fails
  GTEXT_CSV_Status compact_status = gtext_csv_table_compact(table);
  if (compact_status != GTEXT_CSV_OK) {
    // Restore original row_count if compaction failed
    table->row_count = original_row_count;
    return compact_status;
  }

  return GTEXT_CSV_OK;
}

// ============================================================================
// Clone helper structures and functions
// ============================================================================

static GTEXT_CSV_Status csv_clone_calculate_size(
    const GTEXT_CSV_Table * source, size_t * total_size_out) {
  if (!source || !total_size_out) {
    return GTEXT_CSV_E_INVALID;
  }

  size_t total_size = 0;

  // Table structure (aligned to 8 bytes)
  size_t table_size = sizeof(GTEXT_CSV_Table);
  size_t table_aligned = (table_size + 7) & ~7; // Align to 8
  total_size = table_aligned;

  // Rows array (aligned to 8 bytes)
  size_t rows_array_size = sizeof(csv_table_row) * source->row_capacity;
  size_t rows_array_aligned = (rows_array_size + 7) & ~7; // Align to 8
  if (total_size > SIZE_MAX - rows_array_aligned) {
    return GTEXT_CSV_E_OOM;
  }
  total_size += rows_array_aligned;

  // Calculate size for all rows and fields
  for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
    csv_table_row * old_row = &source->rows[row_idx];

    if (old_row->field_count == 0) {
      continue;
    }

    // Field array (aligned to 8 bytes)
    size_t field_array_size = sizeof(csv_table_field) * old_row->field_count;
    size_t field_array_aligned = (field_array_size + 7) & ~7;
    if (total_size > SIZE_MAX - field_array_aligned) {
      return GTEXT_CSV_E_OOM;
    }
    total_size += field_array_aligned;

    // Field data (copy ALL fields, including in-situ ones)
    for (size_t i = 0; i < old_row->field_count; i++) {
      csv_table_field * old_field = &old_row->fields[i];

      // Skip empty fields (use global constant)
      if (old_field->length == 0) {
        continue;
      }

      // Field data: length + 1 (null terminator), aligned to 1 byte
      // Note: We copy in-situ fields too (unlike compact which preserves them)
      size_t field_data_size = old_field->length + 1;
      if (total_size > SIZE_MAX - field_data_size) {
        return GTEXT_CSV_E_OOM;
      }
      total_size += field_data_size;
    }
  }

  // Header map entries (if present)
  if (source->has_header && source->header_map) {
    for (size_t i = 0; i < source->header_map_size; i++) {
      csv_header_entry * entry = source->header_map[i];
      while (entry) {
        // Entry structure (aligned to 8 bytes)
        size_t entry_size = sizeof(csv_header_entry);
        size_t entry_aligned = (entry_size + 7) & ~7;
        if (total_size > SIZE_MAX - entry_aligned) {
          return GTEXT_CSV_E_OOM;
        }
        total_size += entry_aligned;

        // Entry name (copy ALL names, including in-situ ones)
        if (entry->name_len > 0) {
          size_t name_size = entry->name_len + 1;
          if (total_size > SIZE_MAX - name_size) {
            return GTEXT_CSV_E_OOM;
          }
          total_size += name_size;
        }

        entry = entry->next;
      }
    }
  }

  // Add some overhead for alignment and safety margin (10% or 1KB, whichever is
  // larger)
  size_t overhead = total_size / 10;
  if (overhead < 1024) {
    overhead = 1024;
  }
  if (total_size > SIZE_MAX - overhead) {
    return GTEXT_CSV_E_OOM;
  }
  total_size += overhead;

  *total_size_out = total_size;
  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_clone_preallocate_structures(
    const GTEXT_CSV_Table * source, csv_context * new_ctx,
    csv_clone_structures * structures_out,
    csv_clone_header_map * header_map_out) {
  if (!source || !new_ctx || !structures_out || !header_map_out) {
    return GTEXT_CSV_E_INVALID;
  }

  // Initialize output structures
  structures_out->new_ctx = new_ctx;
  structures_out->new_table = NULL;
  structures_out->new_rows = NULL;
  structures_out->new_field_arrays = NULL;
  structures_out->new_field_data_ptrs = NULL;

  header_map_out->new_header_map = NULL;
  header_map_out->new_entry_ptrs = NULL;
  header_map_out->new_name_ptrs = NULL;
  header_map_out->total_header_entries = 0;

  // Allocate new table structure (not in arena, use calloc to ensure
  // zero-initialization)
  GTEXT_CSV_Table * new_table =
      (GTEXT_CSV_Table *)calloc(1, sizeof(GTEXT_CSV_Table));
  if (!new_table) {
    return GTEXT_CSV_E_OOM;
  }

  // Initialize table structure
  new_table->ctx = new_ctx;
  new_table->row_count = source->row_count;
  new_table->row_capacity = source->row_capacity;
  new_table->column_count = source->column_count;
  new_table->has_header = source->has_header;
  new_table->header_map = NULL;
  new_table->header_map_size = source->header_map_size;
  structures_out->new_table = new_table;

  // Allocate new rows array in new arena
  csv_table_row * new_rows = (csv_table_row *)csv_arena_alloc_for_context(
      new_ctx, sizeof(csv_table_row) * source->row_capacity, 8);
  if (!new_rows) {
    free(new_table);
    return GTEXT_CSV_E_OOM;
  }

  // Initialize new rows array (zero out unused entries)
  memset(new_rows, 0, sizeof(csv_table_row) * source->row_capacity);
  new_table->rows = new_rows;
  structures_out->new_rows = new_rows;

  // Pre-allocate all field arrays and field data
  // Store pointers in temporary arrays
  csv_table_field ** new_field_arrays = NULL;
  char *** new_field_data_ptrs = NULL; // Array of arrays of char * pointers
  if (source->row_count > 0) {
    new_field_arrays = (csv_table_field **)calloc(
        source->row_count, sizeof(csv_table_field *));
    new_field_data_ptrs = (char ***)calloc(source->row_count, sizeof(char **));
    if (!new_field_arrays || !new_field_data_ptrs) {
      free(new_field_arrays);
      free(new_field_data_ptrs);
      free(new_table);
      return GTEXT_CSV_E_OOM;
    }
  }
  structures_out->new_field_arrays = new_field_arrays;
  structures_out->new_field_data_ptrs = new_field_data_ptrs;

  // Pre-allocate all field arrays and field data blocks
  for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
    csv_table_row * old_row = &source->rows[row_idx];
    new_field_arrays[row_idx] = NULL;
    new_field_data_ptrs[row_idx] = NULL;

    // Skip empty rows
    if (old_row->field_count == 0) {
      continue;
    }

    // Allocate field array in new arena
    csv_table_field * new_fields =
        (csv_table_field *)csv_arena_alloc_for_context(
            new_ctx, sizeof(csv_table_field) * old_row->field_count, 8);
    if (!new_fields) {
      // Free temporary arrays
      for (size_t j = 0; j < row_idx; j++) {
        free(new_field_data_ptrs[j]);
      }
      free(new_field_arrays);
      free(new_field_data_ptrs);
      free(new_table);
      return GTEXT_CSV_E_OOM;
    }
    new_field_arrays[row_idx] = new_fields;

    // Pre-allocate field data for ALL non-empty fields (including in-situ)
    char ** field_data_ptrs = NULL;
    size_t non_empty_count = 0;
    for (size_t i = 0; i < old_row->field_count; i++) {
      csv_table_field * old_field = &old_row->fields[i];
      if (old_field->length > 0) {
        non_empty_count++;
      }
    }

    if (non_empty_count > 0) {
      field_data_ptrs = (char **)calloc(old_row->field_count, sizeof(char *));
      if (!field_data_ptrs) {
        for (size_t j = 0; j < row_idx; j++) {
          free(new_field_data_ptrs[j]);
        }
        free(new_field_arrays);
        free(new_field_data_ptrs);
        free(new_table);
        return GTEXT_CSV_E_OOM;
      }

      // Pre-allocate all field data blocks
      for (size_t i = 0; i < old_row->field_count; i++) {
        csv_table_field * old_field = &old_row->fields[i];
        field_data_ptrs[i] = NULL;

        if (old_field->length == 0) {
          continue; // Empty, no allocation needed
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
          return GTEXT_CSV_E_OOM;
        }

        char * field_data = (char *)csv_arena_alloc_for_context(
            new_ctx, old_field->length + 1, 1);
        if (!field_data) {
          // Free temporary arrays
          free(field_data_ptrs);
          for (size_t j = 0; j < row_idx; j++) {
            free(new_field_data_ptrs[j]);
          }
          free(new_field_arrays);
          free(new_field_data_ptrs);
          free(new_table);
          return GTEXT_CSV_E_OOM;
        }
        field_data_ptrs[i] = field_data;
      }
    }
    new_field_data_ptrs[row_idx] = field_data_ptrs;
  }

  // Pre-allocate Header Map Structures
  if (source->has_header && source->header_map) {
    // Count total header map entries
    size_t total_header_entries = 0;
    for (size_t i = 0; i < source->header_map_size; i++) {
      csv_header_entry * entry = source->header_map[i];
      while (entry) {
        total_header_entries++;
        entry = entry->next;
      }
    }
    header_map_out->total_header_entries = total_header_entries;

    // Allocate new header map array (malloc'd, not in arena)
    csv_header_entry ** new_header_map = (csv_header_entry **)calloc(
        source->header_map_size, sizeof(csv_header_entry *));
    if (!new_header_map) {
      // Free temporary arrays
      for (size_t j = 0; j < source->row_count; j++) {
        free(new_field_data_ptrs[j]);
      }
      free(new_field_arrays);
      free(new_field_data_ptrs);
      free(new_table);
      return GTEXT_CSV_E_OOM;
    }
    header_map_out->new_header_map = new_header_map;

    if (total_header_entries > 0) {
      csv_header_entry ** new_entry_ptrs = (csv_header_entry **)malloc(
          sizeof(csv_header_entry *) * total_header_entries);
      char ** new_name_ptrs =
          (char **)malloc(sizeof(char *) * total_header_entries);
      if (!new_entry_ptrs || !new_name_ptrs) {
        free(new_header_map);
        free(new_entry_ptrs);
        free(new_name_ptrs);
        // Free temporary arrays
        for (size_t j = 0; j < source->row_count; j++) {
          free(new_field_data_ptrs[j]);
        }
        free(new_field_arrays);
        free(new_field_data_ptrs);
        free(new_table);
        return GTEXT_CSV_E_OOM;
      }
      header_map_out->new_entry_ptrs = new_entry_ptrs;
      header_map_out->new_name_ptrs = new_name_ptrs;

      // Pre-allocate all header map entries and name strings
      size_t entry_idx = 0;
      for (size_t i = 0; i < source->header_map_size; i++) {
        csv_header_entry * old_entry = source->header_map[i];

        while (old_entry) {
          // Allocate new entry in new arena
          csv_header_entry * new_entry =
              (csv_header_entry *)csv_arena_alloc_for_context(
                  new_ctx, sizeof(csv_header_entry), 8);
          if (!new_entry) {
            // Free temporary arrays
            free(new_header_map);
            free(new_entry_ptrs);
            free(new_name_ptrs);
            for (size_t j = 0; j < source->row_count; j++) {
              free(new_field_data_ptrs[j]);
            }
            free(new_field_arrays);
            free(new_field_data_ptrs);
            free(new_table);
            return GTEXT_CSV_E_OOM;
          }
          new_entry_ptrs[entry_idx] = new_entry;
          new_name_ptrs[entry_idx] = NULL;

          // Pre-allocate name string if needed
          if (old_entry->name_len == 0) {
            // Empty name - will use csv_empty_field_string
          }
          else {
            // Copy name to new arena (including in-situ names)
            // Check for overflow
            if (old_entry->name_len > SIZE_MAX - 1) {
              free(new_header_map);
              free(new_entry_ptrs);
              free(new_name_ptrs);
              for (size_t j = 0; j < source->row_count; j++) {
                free(new_field_data_ptrs[j]);
              }
              free(new_field_arrays);
              free(new_field_data_ptrs);
              free(new_table);
              return GTEXT_CSV_E_OOM;
            }
            char * name_data = (char *)csv_arena_alloc_for_context(
                new_ctx, old_entry->name_len + 1, 1);
            if (!name_data) {
              free(new_header_map);
              free(new_entry_ptrs);
              free(new_name_ptrs);
              for (size_t j = 0; j < source->row_count; j++) {
                free(new_field_data_ptrs[j]);
              }
              free(new_field_arrays);
              free(new_field_data_ptrs);
              free(new_table);
              return GTEXT_CSV_E_OOM;
            }
            new_name_ptrs[entry_idx] = name_data;
          }

          entry_idx++;
          old_entry = old_entry->next;
        }
      }
    }
  }

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_clone_copy_data(const GTEXT_CSV_Table * source,
    csv_clone_structures * structures, csv_clone_header_map * header_map) {
  if (!source || !structures || !header_map) {
    return GTEXT_CSV_E_INVALID;
  }

  GTEXT_CSV_Table * new_table = structures->new_table;
  csv_table_row * new_rows = structures->new_rows;
  csv_table_field ** new_field_arrays = structures->new_field_arrays;
  char *** new_field_data_ptrs = structures->new_field_data_ptrs;

  // Copy All Data (no allocations, just memory operations)
  for (size_t row_idx = 0; row_idx < source->row_count; row_idx++) {
    csv_table_row * old_row = &source->rows[row_idx];
    csv_table_row * new_row = &new_rows[row_idx];

    // Skip empty rows
    if (old_row->field_count == 0) {
      new_row->fields = NULL;
      new_row->field_count = 0;
      continue;
    }

    csv_table_field * new_fields = new_field_arrays[row_idx];
    char ** field_data_ptrs = new_field_data_ptrs[row_idx];

    // Copy each field
    for (size_t i = 0; i < old_row->field_count; i++) {
      csv_table_field * old_field = &old_row->fields[i];
      csv_table_field * new_field = &new_fields[i];

      // Copy field data
      if (old_field->length == 0) {
        // Empty field - use global constant
        new_field->data = csv_empty_field_string;
        new_field->length = 0;
        new_field->is_in_situ = false;
      }
      else {
        // Copy field data to pre-allocated block (including in-situ fields)
        // field_data_ptrs should always be allocated if there are any non-empty
        // fields
        if (!field_data_ptrs || !field_data_ptrs[i]) {
          // This should not happen if pre-allocation worked correctly
          return GTEXT_CSV_E_INVALID;
        }
        char * field_data = field_data_ptrs[i];
        memcpy(field_data, old_field->data, old_field->length);
        field_data[old_field->length] = '\0';
        new_field->data = field_data;
        new_field->length = old_field->length;
        new_field->is_in_situ =
            false; // All fields in clone are in arena, not in-situ
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

  // Copy Header Map Data
  if (source->has_header && source->header_map &&
      header_map->total_header_entries > 0) {
    csv_header_entry ** new_header_map = header_map->new_header_map;
    csv_header_entry ** new_entry_ptrs = header_map->new_entry_ptrs;
    char ** new_name_ptrs = header_map->new_name_ptrs;

    size_t entry_idx = 0;
    for (size_t i = 0; i < source->header_map_size; i++) {
      csv_header_entry * old_entry = source->header_map[i];
      csv_header_entry ** new_chain = &new_header_map[i];

      while (old_entry) {
        csv_header_entry * new_entry = new_entry_ptrs[entry_idx];
        char * name_data = new_name_ptrs[entry_idx];

        // Copy name string
        if (old_entry->name_len == 0) {
          new_entry->name = csv_empty_field_string;
        }
        else {
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
        }
        else {
          // Find end of chain
          csv_header_entry * chain_end = *new_chain;
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

    // Set header map in new table
    new_table->header_map = new_header_map;

    // Rebuild reverse mapping
    csv_rebuild_index_to_entry(new_table);
  }

  // Note: input_buffer is NOT copied - cloned table is independent
  // and doesn't reference the original input buffer
  structures->new_ctx->input_buffer = NULL;
  structures->new_ctx->input_buffer_len = 0;

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Table * gtext_csv_clone(const GTEXT_CSV_Table * source) {
  // Validate inputs
  if (!source) {
    return NULL;
  }

  // Calculate total size needed for clone
  size_t total_size = 0;
  GTEXT_CSV_Status status = csv_clone_calculate_size(source, &total_size);
  if (status != GTEXT_CSV_OK) {
    return NULL;
  }

  // Create new context/arena with calculated block size
  csv_context * new_ctx = csv_context_new_with_block_size(total_size);
  if (!new_ctx) {
    return NULL;
  }

  // Pre-allocate all structures
  csv_clone_structures structures;
  csv_clone_header_map header_map;
  status = csv_clone_preallocate_structures(
      source, new_ctx, &structures, &header_map);
  if (status != GTEXT_CSV_OK) {
    // Cleanup: free table if allocated, then free context
    if (structures.new_table) {
      free(structures.new_table);
    }
    csv_context_free(new_ctx);
    return NULL;
  }

  // Copy all data to pre-allocated structures
  status = csv_clone_copy_data(source, &structures, &header_map);
  if (status != GTEXT_CSV_OK) {
    // Cleanup: free table and context
    free(structures.new_table);
    csv_context_free(new_ctx);
    return NULL;
  }

  // Return the cloned table
  return structures.new_table;
}

static void csv_header_map_reindex_increment(
    GTEXT_CSV_Table * table, size_t start_index) {
  if (!table->has_header || !table->header_map || table->row_count == 0) {
    return;
  }

  // Reindex all header map entries with index >= start_index (increment by 1)
  // Also update reverse mapping
  for (size_t i = 0; i < table->header_map_size; i++) {
    csv_header_entry * entry = table->header_map[i];
    while (entry) {
      if (entry->index >= start_index) {
        size_t old_index = entry->index;
        entry->index++;

        // Update reverse mapping: move entry from old_index to new_index
        if (table->index_to_entry &&
            old_index < table->index_to_entry_capacity) {
          // Clear old position
          if (table->index_to_entry[old_index] == entry) {
            table->index_to_entry[old_index] = NULL;
          }
          // Set new position
          csv_set_index_to_entry(table, entry->index, entry);
        }
      }
      entry = entry->next;
    }
  }
}

static void csv_header_map_reindex_decrement(
    GTEXT_CSV_Table * table, size_t start_index) {
  if (!table->has_header || !table->header_map || table->row_count == 0) {
    return;
  }

  // Reindex all header map entries with index > start_index (decrement by 1)
  // Also update reverse mapping
  for (size_t i = 0; i < table->header_map_size; i++) {
    csv_header_entry * entry = table->header_map[i];
    while (entry) {
      if (entry->index > start_index) {
        size_t old_index = entry->index;
        entry->index--;

        // Update reverse mapping: move entry from old_index to new_index
        if (table->index_to_entry &&
            old_index < table->index_to_entry_capacity) {
          // Clear old position
          if (table->index_to_entry[old_index] == entry) {
            table->index_to_entry[old_index] = NULL;
          }
          // Set new position
          csv_set_index_to_entry(table, entry->index, entry);
        }
      }
      entry = entry->next;
    }
  }
}

static size_t csv_get_data_row_count(const GTEXT_CSV_Table * table) {
  if (table->has_header && table->row_count > 0) {
    return table->row_count - 1;
  }
  return table->row_count;
}

static size_t csv_get_start_row_idx(const GTEXT_CSV_Table * table) {
  if (table->has_header && table->row_count > 0) {
    return 1;
  }
  return 0;
}

static size_t csv_get_rows_to_modify(const GTEXT_CSV_Table * table) {
  size_t start_row_idx = csv_get_start_row_idx(table);
  size_t rows_to_modify = table->row_count - start_row_idx; // Data rows
  if (table->has_header && table->row_count > 0) {
    rows_to_modify++; // Include header row
  }
  return rows_to_modify;
}

static GTEXT_CSV_Status csv_validate_column_values(
    const GTEXT_CSV_Table * table, const char * const * values) {
  if (!table || !values) {
    return GTEXT_CSV_E_INVALID;
  }

  size_t expected_value_count = table->row_count;
  size_t actual_value_count = 0;
  while (actual_value_count < expected_value_count &&
      values[actual_value_count] != NULL) {
    actual_value_count++;
  }
  if (actual_value_count != expected_value_count) {
    return GTEXT_CSV_E_INVALID;
  }

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_determine_header_value(
    const GTEXT_CSV_Table * table, bool is_empty_column,
    const char * header_name, size_t header_name_len,
    const char * const * values, const size_t * value_lengths,
    const char ** header_value_out, size_t * header_value_len_out,
    const char ** header_map_name_out, size_t * header_map_name_len_out,
    size_t * name_len_out) {
  if (!table || !header_value_out || !header_value_len_out ||
      !header_map_name_out || !header_map_name_len_out || !name_len_out) {
    return GTEXT_CSV_E_INVALID;
  }

  // Early return: No headers case - all outputs remain NULL/0
  if (!table->has_header || !table->header_map) {
    *header_value_out = NULL;
    *header_value_len_out = 0;
    *header_map_name_out = NULL;
    *header_map_name_len_out = 0;
    *name_len_out = 0;
    return GTEXT_CSV_OK;
  }

  // Table has headers - determine header value
  const char * header_value = NULL;
  size_t header_value_len = 0;
  const char * header_map_name = NULL;
  size_t header_map_name_len = 0;
  size_t name_len = 0;

  if (is_empty_column) {
    // Empty column: use header_name
    if (!header_name) {
      return GTEXT_CSV_E_INVALID;
    }
    header_value = header_name;
    header_value_len = header_name_len;
    if (header_value_len == 0 && header_value) {
      header_value_len = csv_calculate_field_length(header_value, NULL, 0);
    }
    header_map_name = header_name;
    header_map_name_len = header_value_len;
    name_len = header_value_len;
  }
  else {
    // Use values[0] for both header field and header map entry
    header_value = values[0];
    header_value_len =
        csv_calculate_field_length(header_value, value_lengths, 0);
    header_map_name = values[0];
    header_map_name_len = header_value_len;
    name_len = header_value_len;
  }

  // Check for duplicate header name if uniqueness is required
  GTEXT_CSV_Status uniqueness_status = csv_check_header_uniqueness(table,
      header_map_name, header_map_name_len, SIZE_MAX // Don't exclude any column
  );
  if (uniqueness_status != GTEXT_CSV_OK) {
    return uniqueness_status;
  }

  *header_value_out = header_value;
  *header_value_len_out = header_value_len;
  *header_map_name_out = header_map_name;
  *header_map_name_len_out = header_map_name_len;
  *name_len_out = name_len;

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_preallocate_column_field_data(
    GTEXT_CSV_Table * table, bool is_empty_column, size_t rows_to_modify,
    const char * const * values, const size_t * value_lengths,
    char *** field_data_array_out, size_t ** field_data_lengths_out) {
  if (!table || !field_data_array_out || !field_data_lengths_out) {
    return GTEXT_CSV_E_INVALID;
  }

  char ** field_data_array = NULL;
  size_t * field_data_lengths = NULL;

  if (!is_empty_column && rows_to_modify > 0) {
    field_data_array = (char **)malloc(sizeof(char *) * rows_to_modify);
    field_data_lengths = (size_t *)malloc(sizeof(size_t) * rows_to_modify);
    if (!field_data_array || !field_data_lengths) {
      free(field_data_array);
      free(field_data_lengths);
      return GTEXT_CSV_E_OOM;
    }

    // Allocate field data for all rows (header + data rows)
    // Mapping: array_idx maps to values array index
    // - If has_header: array_idx 0 = values[0] (header), array_idx 1..N =
    // values[1..N] (data rows)
    // - If no headers: array_idx 0..N-1 = values[0..N-1] (data rows)
    for (size_t i = 0; i < rows_to_modify; i++) {
      size_t value_idx = i; // Direct mapping: array_idx == value_idx

      const char * value = values[value_idx];
      size_t value_len =
          csv_calculate_field_length(value, value_lengths, value_idx);

      if (value_len == 0) {
        // Empty field - will use csv_empty_field_string
        field_data_array[i] = NULL;
        field_data_lengths[i] = 0;
      }
      else {
        // Allocate and copy field data
        if (value_len > SIZE_MAX - 1) {
          free(field_data_array);
          free(field_data_lengths);
          return GTEXT_CSV_E_OOM;
        }
        char * field_data =
            (char *)csv_arena_alloc_for_context(table->ctx, value_len + 1, 1);
        if (!field_data) {
          free(field_data_array);
          free(field_data_lengths);
          return GTEXT_CSV_E_OOM;
        }
        memcpy(field_data, value, value_len);
        field_data[value_len] = '\0';
        field_data_array[i] = field_data;
        field_data_lengths[i] = value_len;
      }
    }
  }

  *field_data_array_out = field_data_array;
  *field_data_lengths_out = field_data_lengths;

  return GTEXT_CSV_OK;
}

static GTEXT_CSV_Status csv_column_op_alloc_temp_arrays(
    size_t rows_to_modify, csv_column_op_temp_arrays * temp_arrays_out) {
  if (!temp_arrays_out) {
    return GTEXT_CSV_E_INVALID;
  }

  // Initialize all pointers to NULL
  temp_arrays_out->new_field_arrays = NULL;
  temp_arrays_out->old_field_counts = NULL;
  temp_arrays_out->field_data_array = NULL;
  temp_arrays_out->field_data_lengths = NULL;

  // Allocate new_field_arrays and old_field_counts if we have rows to modify
  if (rows_to_modify > 0) {
    temp_arrays_out->new_field_arrays =
        (csv_table_field **)malloc(sizeof(csv_table_field *) * rows_to_modify);
    temp_arrays_out->old_field_counts =
        (size_t *)malloc(sizeof(size_t) * rows_to_modify);
    if (!temp_arrays_out->new_field_arrays ||
        !temp_arrays_out->old_field_counts) {
      csv_column_op_cleanup_temp_arrays(temp_arrays_out);
      return GTEXT_CSV_E_OOM;
    }
  }

  // field_data_array and field_data_lengths are allocated by
  // csv_preallocate_column_field_data if has_field_data is true, but we don't
  // allocate them here - they're handled separately This function only
  // allocates new_field_arrays and old_field_counts

  return GTEXT_CSV_OK;
}

static void csv_column_op_cleanup_temp_arrays(
    csv_column_op_temp_arrays * temp_arrays) {
  if (!temp_arrays) {
    return;
  }

  free(temp_arrays->new_field_arrays);
  temp_arrays->new_field_arrays = NULL;

  free(temp_arrays->old_field_counts);
  temp_arrays->old_field_counts = NULL;

  free(temp_arrays->field_data_array);
  temp_arrays->field_data_array = NULL;

  free(temp_arrays->field_data_lengths);
  temp_arrays->field_data_lengths = NULL;
}

static void csv_column_op_cleanup_individual(
    csv_table_field ** new_field_arrays, size_t * old_field_counts,
    char ** field_data_array, size_t * field_data_lengths) {
  free(new_field_arrays);
  free(old_field_counts);
  free(field_data_array);
  free(field_data_lengths);
}

static GTEXT_CSV_Status csv_column_operation_internal(GTEXT_CSV_Table * table,
    size_t col_idx, const char * header_name, size_t header_name_len,
    const char * const * values, const size_t * value_lengths,
    csv_table_field *** new_field_arrays_out, size_t ** old_field_counts_out,
    size_t * rows_to_modify_out, char ** header_field_data_out,
    size_t * header_field_data_len_out, csv_header_entry ** new_entry_out,
    size_t * header_hash_out, csv_table_field ** new_header_fields_out,
    size_t * old_header_field_count_out, char *** field_data_array_out,
    size_t ** field_data_lengths_out) {
  // Determine if this is append (col_idx == SIZE_MAX) or insert
  bool is_append = (col_idx == SIZE_MAX);

  // Handle empty column case (values is NULL)
  bool is_empty_column = (values == NULL);

  // Validate col_idx for insert operations
  // When allow_irregular_rows is true, allow insertion beyond column_count
  // (will update column_count and pad rows as needed)
  if (!is_append && col_idx > table->column_count) {
    if (!table->allow_irregular_rows) {
      // Strict mode: require col_idx <= column_count
      return GTEXT_CSV_E_INVALID;
    }
    // Irregular mode: will update column_count and pad rows - continue
  }

  // If table is empty: just update column count (no rows to modify)
  // Note: _with_values variants require non-empty table, but this helper
  // handles both cases
  if (table->row_count == 0) {
    // Check for overflow when incrementing column_count
    if (table->column_count == SIZE_MAX) {
      return GTEXT_CSV_E_OOM;
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
    return GTEXT_CSV_OK;
  }

  // Validate values if provided
  if (!is_empty_column) {
    GTEXT_CSV_Status validation_status =
        csv_validate_column_values(table, values);
    if (validation_status != GTEXT_CSV_OK) {
      return validation_status;
    }
  }

  // Determine header value and name for header map
  const char * header_value = NULL;
  size_t header_value_len = 0;
  const char * header_map_name = NULL;
  size_t header_map_name_len = 0;
  size_t name_len = 0;

  GTEXT_CSV_Status header_status =
      csv_determine_header_value(table, is_empty_column, header_name,
          header_name_len, values, value_lengths, &header_value,
          &header_value_len, &header_map_name, &header_map_name_len, &name_len);
  if (header_status != GTEXT_CSV_OK) {
    return header_status;
  }

  // Phase 2: Pre-allocate All New Field Arrays
  // Calculate how many rows need new field arrays (all data rows + header row
  // if present) Determine start row index (skip header row if present)
  size_t start_row_idx = csv_get_start_row_idx(table);

  // Count rows that need new field arrays
  size_t rows_to_modify = csv_get_rows_to_modify(table);

  // Pre-allocate all new field arrays before updating any row structures
  csv_column_op_temp_arrays temp_arrays;
  GTEXT_CSV_Status alloc_status =
      csv_column_op_alloc_temp_arrays(rows_to_modify, &temp_arrays);
  if (alloc_status != GTEXT_CSV_OK) {
    return alloc_status;
  }
  csv_table_field ** new_field_arrays = temp_arrays.new_field_arrays;
  size_t * old_field_counts = temp_arrays.old_field_counts;

  // Pre-allocate field arrays for data rows
  size_t array_idx = 0;
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];
    size_t old_field_count = row->field_count;

    // Calculate new field count: if inserting beyond row length, need col_idx +
    // 1 fields Otherwise, need old_field_count + 1 fields
    size_t new_field_count;
    if (!is_append && col_idx > old_field_count) {
      // Inserting beyond row length: need col_idx + 1 fields (old fields +
      // padding + new field)
      if (col_idx == SIZE_MAX) {
        csv_column_op_cleanup_temp_arrays(&temp_arrays);
        return GTEXT_CSV_E_OOM;
      }
      new_field_count = col_idx + 1;

      // Validate irregular rows are allowed
      if (!table->allow_irregular_rows) {
        // Strict mode: require all rows to be long enough
        csv_column_op_cleanup_temp_arrays(&temp_arrays);
        return GTEXT_CSV_E_INVALID;
      }
    }
    else {
      // Normal case: one more field
      new_field_count = old_field_count + 1;

      // Check for overflow
      if (new_field_count < old_field_count) {
        csv_column_op_cleanup_temp_arrays(&temp_arrays);
        return GTEXT_CSV_E_OOM;
      }
    }

    old_field_counts[array_idx] = old_field_count;

    // Allocate new field array
    csv_table_field * new_fields =
        (csv_table_field *)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * new_field_count, 8);
    if (!new_fields) {
      // Free temporary arrays
      csv_column_op_cleanup_temp_arrays(&temp_arrays);
      return GTEXT_CSV_E_OOM;
    }

    new_field_arrays[array_idx] = new_fields;
    array_idx++;
  }

  // Phase 3: Pre-allocate All Field Data (if values provided)
  char ** field_data_array = NULL;
  size_t * field_data_lengths = NULL;
  GTEXT_CSV_Status field_data_status =
      csv_preallocate_column_field_data(table, is_empty_column, rows_to_modify,
          values, value_lengths, &field_data_array, &field_data_lengths);
  if (field_data_status != GTEXT_CSV_OK) {
    csv_column_op_cleanup_temp_arrays(&temp_arrays);
    return field_data_status;
  }
  // Store field_data arrays in temp_arrays for unified cleanup
  temp_arrays.field_data_array = field_data_array;
  temp_arrays.field_data_lengths = field_data_lengths;

  // Phase 4: Determine Header Field Data (if needed)
  char * header_field_data = NULL;
  size_t header_field_data_len = 0;
  if (table->has_header && table->header_map && table->row_count > 0) {
    // Validate col_idx is within bounds for header row (insert operations)
    // When allow_irregular_rows is true, allow insertion beyond header row
    // length (padding will happen in Task 2.2)
    if (!is_append) {
      csv_table_row * header_row = &table->rows[0];
      if (col_idx > header_row->field_count) {
        if (!table->allow_irregular_rows) {
          // Strict mode: require header row to be long enough
          csv_column_op_cleanup_temp_arrays(&temp_arrays);
          return GTEXT_CSV_E_INVALID;
        }
        // Irregular mode: will pad header row - continue
      }
    }

    if (is_empty_column) {
      // Empty column: allocate header name separately
      if (name_len == 0) {
        // Empty header - will use csv_empty_field_string
        header_field_data = NULL;
        header_field_data_len = 0;
      }
      else {
        // Allocate header name field data
        if (name_len > SIZE_MAX - 1) {
          csv_column_op_cleanup_temp_arrays(&temp_arrays);
          return GTEXT_CSV_E_OOM;
        }
        header_field_data =
            (char *)csv_arena_alloc_for_context(table->ctx, name_len + 1, 1);
        if (!header_field_data) {
          csv_column_op_cleanup_temp_arrays(&temp_arrays);
          return GTEXT_CSV_E_OOM;
        }
        memcpy(header_field_data, header_value, name_len);
        header_field_data[name_len] = '\0';
        header_field_data_len = name_len;
      }
    }
    else {
      // Reuse field_data_array[0] for header field
      header_field_data = field_data_array[0];
      header_field_data_len = field_data_lengths[0];
    }
  }

  // Phase 5: Allocate Header Map Entry (if needed)
  csv_header_entry * new_entry = NULL;
  size_t header_hash = 0;
  if (table->has_header && table->header_map && table->row_count > 0) {
    header_hash = csv_header_hash(
        header_map_name, header_map_name_len, table->header_map_size);
    new_entry = (csv_header_entry *)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_header_entry), 8);
    if (!new_entry) {
      csv_column_op_cleanup_temp_arrays(&temp_arrays);
      return GTEXT_CSV_E_OOM;
    }
  }

  // Phase 6: Copy Data to New Field Arrays
  // Copy data rows
  array_idx = 0;
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];
    size_t old_field_count = old_field_counts[array_idx];
    csv_table_field * new_fields = new_field_arrays[array_idx];

    if (is_append) {
      // Append: copy all existing fields, add new field at end
      if (old_field_count > 0) {
        memcpy(
            new_fields, row->fields, sizeof(csv_table_field) * old_field_count);
      }
      csv_table_field * new_field = &new_fields[old_field_count];
      if (is_empty_column) {
        csv_setup_empty_field(new_field);
      }
      else {
        // Map array_idx to value index in field_data_array
        // field_data_array was allocated for all rows_to_modify
        // - If has_header: field_data_array[0] = header (values[0]),
        // field_data_array[1..N] = data rows (values[1..N])
        // - If no headers: field_data_array[0..N-1] = data rows
        // (values[0..N-1]) array_idx here is for data rows only (header row
        // handled separately) So: value_idx = array_idx + (has_header ? 1 : 0)
        size_t value_idx = table->has_header ? (array_idx + 1) : array_idx;
        if (field_data_array[value_idx] == NULL) {
          csv_setup_empty_field(new_field);
        }
        else {
          new_field->data = field_data_array[value_idx];
          new_field->length = field_data_lengths[value_idx];
          new_field->is_in_situ = false;
        }
      }
    }
    else {
      // Insert: copy fields before insertion point, add new field, copy fields
      // after
      // Handle padding when col_idx > old_field_count (irregular rows mode)
      if (col_idx > old_field_count) {
        // Row is too short: copy existing fields, pad with empty fields, insert
        // new field
        if (old_field_count > 0) {
          memcpy(new_fields, row->fields,
              sizeof(csv_table_field) * old_field_count);
        }
        // Pad with empty fields from old_field_count to col_idx
        for (size_t i = old_field_count; i < col_idx; i++) {
          csv_setup_empty_field(&new_fields[i]);
        }
        // Insert new field at col_idx
        csv_table_field * new_field = &new_fields[col_idx];
        if (is_empty_column) {
          csv_setup_empty_field(new_field);
        }
        else {
          size_t value_idx = table->has_header ? (array_idx + 1) : array_idx;
          if (field_data_array[value_idx] == NULL) {
            csv_setup_empty_field(new_field);
          }
          else {
            new_field->data = field_data_array[value_idx];
            new_field->length = field_data_lengths[value_idx];
            new_field->is_in_situ = false;
          }
        }
        // No fields to copy after (row was too short)
      }
      else {
        // Normal insertion: row is long enough
        if (col_idx > 0) {
          memcpy(new_fields, row->fields, sizeof(csv_table_field) * col_idx);
        }
        csv_table_field * new_field = &new_fields[col_idx];
        if (is_empty_column) {
          csv_setup_empty_field(new_field);
        }
        else {
          size_t value_idx = table->has_header ? (array_idx + 1) : array_idx;
          if (field_data_array[value_idx] == NULL) {
            csv_setup_empty_field(new_field);
          }
          else {
            new_field->data = field_data_array[value_idx];
            new_field->length = field_data_lengths[value_idx];
            new_field->is_in_situ = false;
          }
        }
        if (col_idx < old_field_count) {
          memcpy(new_fields + col_idx + 1, row->fields + col_idx,
              sizeof(csv_table_field) * (old_field_count - col_idx));
        }
      }
    }

    array_idx++;
  }

  // Copy header row (if present)
  csv_table_field * new_header_fields = NULL;
  size_t old_header_field_count = 0;
  if (table->has_header && table->header_map && table->row_count > 0) {
    csv_table_row * header_row = &table->rows[0];
    old_header_field_count = header_row->field_count;

    // Calculate new header field count: if inserting beyond header row length,
    // need col_idx + 1 fields (old fields + padding + new field)
    // Otherwise, need old_header_field_count + 1 fields
    size_t new_header_field_count;
    if (!is_append && col_idx > old_header_field_count) {
      // Inserting beyond header row length: need col_idx + 1 fields
      if (col_idx == SIZE_MAX) {
        csv_column_op_cleanup_temp_arrays(&temp_arrays);
        return GTEXT_CSV_E_OOM;
      }
      new_header_field_count = col_idx + 1;
    }
    else {
      // Normal case: one more field
      new_header_field_count = old_header_field_count + 1;

      // Check for overflow
      if (new_header_field_count < old_header_field_count) {
        csv_column_op_cleanup_temp_arrays(&temp_arrays);
        return GTEXT_CSV_E_OOM;
      }
    }

    // Allocate new field array for header row
    new_header_fields = (csv_table_field *)csv_arena_alloc_for_context(
        table->ctx, sizeof(csv_table_field) * new_header_field_count, 8);
    if (!new_header_fields) {
      csv_column_op_cleanup_temp_arrays(&temp_arrays);
      return GTEXT_CSV_E_OOM;
    }

    if (is_append) {
      // Append: copy all existing header fields, add new header field at end
      if (old_header_field_count > 0) {
        memcpy(new_header_fields, header_row->fields,
            sizeof(csv_table_field) * old_header_field_count);
      }
      csv_table_field * new_header_field =
          &new_header_fields[old_header_field_count];
      if (header_field_data == NULL || header_field_data_len == 0) {
        csv_setup_empty_field(new_header_field);
      }
      else {
        new_header_field->data = header_field_data;
        new_header_field->length = header_field_data_len;
        new_header_field->is_in_situ = false;
      }
    }
    else {
      // Insert: copy header fields before insertion point, add new header
      // field, copy after
      // Handle padding when col_idx > old_header_field_count (irregular rows
      // mode)
      if (col_idx > old_header_field_count) {
        // Header row is too short: copy existing fields, pad with empty fields,
        // insert new field
        if (old_header_field_count > 0) {
          memcpy(new_header_fields, header_row->fields,
              sizeof(csv_table_field) * old_header_field_count);
        }
        // Pad with empty fields from old_header_field_count to col_idx
        for (size_t i = old_header_field_count; i < col_idx; i++) {
          csv_setup_empty_field(&new_header_fields[i]);
        }
        // Insert new header field at col_idx
        csv_table_field * new_header_field = &new_header_fields[col_idx];
        if (header_field_data == NULL || header_field_data_len == 0) {
          csv_setup_empty_field(new_header_field);
        }
        else {
          new_header_field->data = header_field_data;
          new_header_field->length = header_field_data_len;
          new_header_field->is_in_situ = false;
        }
        // No fields to copy after (header row was too short)
      }
      else {
        // Normal insertion: header row is long enough
        if (col_idx > 0) {
          memcpy(new_header_fields, header_row->fields,
              sizeof(csv_table_field) * col_idx);
        }
        csv_table_field * new_header_field = &new_header_fields[col_idx];
        if (header_field_data == NULL || header_field_data_len == 0) {
          csv_setup_empty_field(new_header_field);
        }
        else {
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
  }

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
  *field_data_array_out = field_data_array;
  *field_data_lengths_out = field_data_lengths;

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_column_append(
    GTEXT_CSV_Table * table, const char * header_name, size_t header_name_len) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Use helper function for common column operation logic (SIZE_MAX = append)
  csv_table_field ** new_field_arrays = NULL;
  size_t * old_field_counts = NULL;
  size_t rows_to_modify = 0;
  char * header_field_data = NULL;
  size_t header_field_data_len = 0;
  csv_header_entry * new_entry = NULL;
  size_t header_hash = 0;
  csv_table_field * new_header_fields = NULL;
  size_t old_header_field_count = 0;

  char ** field_data_array = NULL;
  size_t * field_data_lengths = NULL;
  GTEXT_CSV_Status status = csv_column_operation_internal(table, SIZE_MAX,
      header_name, header_name_len, NULL, NULL, // No values - empty fields
      &new_field_arrays, &old_field_counts, &rows_to_modify, &header_field_data,
      &header_field_data_len, &new_entry, &header_hash, &new_header_fields,
      &old_header_field_count, &field_data_array, &field_data_lengths);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // If table was empty, helper already updated column_count
  if (rows_to_modify == 0) {
    return GTEXT_CSV_OK;
  }

  // Phase 6: Atomic State Update
  // Only after all allocations and copies succeed:
  // 1. Update column_count
  if (table->column_count == SIZE_MAX) {
    csv_column_op_cleanup_individual(new_field_arrays, old_field_counts,
        field_data_array, field_data_lengths);
    return GTEXT_CSV_E_OOM;
  }
  size_t new_column_index = table->column_count;
  table->column_count++;

  // 2. Update all data row structures
  size_t start_row_idx = csv_get_start_row_idx(table);
  size_t array_idx = 0;
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];
    size_t old_field_count = old_field_counts[array_idx];
    csv_table_field * new_fields = new_field_arrays[array_idx];

    row->fields = new_fields;
    row->field_count = old_field_count + 1;

    array_idx++;
  }

  // 3. Update header row structure (if present)
  if (table->has_header && table->header_map && table->row_count > 0) {
    csv_table_row * header_row = &table->rows[0];
    header_row->fields = new_header_fields;
    header_row->field_count = old_header_field_count + 1;

    // 4. Add header map entry
    new_entry->name = new_header_fields[old_header_field_count].data;
    new_entry->name_len = new_header_fields[old_header_field_count].length;
    new_entry->index = new_column_index;
    new_entry->next = table->header_map[header_hash];
    table->header_map[header_hash] = new_entry;

    // Update reverse mapping
    csv_set_index_to_entry(table, new_column_index, new_entry);
  }

  // Free temporary arrays
  csv_column_op_cleanup_individual(
      new_field_arrays, old_field_counts, field_data_array, field_data_lengths);

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_column_append_with_values(
    GTEXT_CSV_Table * table, const char * header_name, size_t header_name_len,
    const char * const * values, const size_t * value_lengths) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Empty table check: if row_count == 0, return error
  if (table->row_count == 0) {
    return GTEXT_CSV_E_INVALID;
  }

  // Use helper function for common column operation logic (SIZE_MAX = append)
  csv_table_field ** new_field_arrays = NULL;
  size_t * old_field_counts = NULL;
  size_t rows_to_modify = 0;
  char * header_field_data = NULL;
  size_t header_field_data_len = 0;
  csv_header_entry * new_entry = NULL;
  size_t header_hash = 0;
  csv_table_field * new_header_fields = NULL;
  size_t old_header_field_count = 0;

  char ** field_data_array = NULL;
  size_t * field_data_lengths = NULL;
  GTEXT_CSV_Status status = csv_column_operation_internal(table, SIZE_MAX,
      header_name, header_name_len, values, value_lengths, &new_field_arrays,
      &old_field_counts, &rows_to_modify, &header_field_data,
      &header_field_data_len, &new_entry, &header_hash, &new_header_fields,
      &old_header_field_count, &field_data_array, &field_data_lengths);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // If table was empty, helper already updated column_count
  if (rows_to_modify == 0) {
    return GTEXT_CSV_OK;
  }

  // Phase 7: Atomic State Update
  // Only after all allocations and copies succeed:
  // 1. Update column_count
  if (table->column_count == SIZE_MAX) {
    csv_column_op_cleanup_individual(new_field_arrays, old_field_counts,
        field_data_array, field_data_lengths);
    return GTEXT_CSV_E_OOM;
  }
  size_t new_column_index = table->column_count;
  table->column_count++;

  // 2. Update all data row structures
  size_t start_row_idx = csv_get_start_row_idx(table);
  size_t array_idx = 0;
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];
    size_t old_field_count = old_field_counts[array_idx];
    csv_table_field * new_fields = new_field_arrays[array_idx];

    row->fields = new_fields;
    row->field_count = old_field_count + 1;

    array_idx++;
  }

  // 3. Update header row structure (if present)
  if (table->has_header && table->header_map && table->row_count > 0) {
    csv_table_row * header_row = &table->rows[0];
    header_row->fields = new_header_fields;
    header_row->field_count = old_header_field_count + 1;

    // 4. Add header map entry
    new_entry->name = new_header_fields[old_header_field_count].data;
    new_entry->name_len = new_header_fields[old_header_field_count].length;
    new_entry->index = new_column_index;
    new_entry->next = table->header_map[header_hash];
    table->header_map[header_hash] = new_entry;

    // Update reverse mapping
    csv_set_index_to_entry(table, new_column_index, new_entry);
  }

  // Free temporary arrays
  csv_column_op_cleanup_individual(
      new_field_arrays, old_field_counts, field_data_array, field_data_lengths);

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_column_insert_with_values(
    GTEXT_CSV_Table * table, size_t col_idx, const char * header_name,
    size_t header_name_len, const char * const * values,
    const size_t * value_lengths) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Validate col_idx (must be <= column count, unless irregular rows allowed)
  if (col_idx > table->column_count) {
    if (!table->allow_irregular_rows) {
      // Strict mode: require col_idx <= column_count
      return GTEXT_CSV_E_INVALID;
    }
    // Irregular mode: will update column_count and pad rows - continue
  }

  // If inserting at end (col_idx == column_count), check if we can use append
  // logic (only if all rows have column_count fields)
  if (col_idx == table->column_count) {
    // Check if all rows have column_count fields
    bool all_rows_at_column_count = true;
    if (table->row_count > 0) {
      size_t start_row_idx = csv_get_start_row_idx(table);
      for (size_t row_idx = start_row_idx; row_idx < table->row_count;
           row_idx++) {
        if (table->rows[row_idx].field_count < table->column_count) {
          all_rows_at_column_count = false;
          break;
        }
      }
    }

    // If all rows are at column_count, use append (no padding needed)
    if (all_rows_at_column_count) {
      return gtext_csv_column_append_with_values(
          table, header_name, header_name_len, values, value_lengths);
    }

    // If irregular rows are allowed but some rows are shorter, we need to use
    // insert logic with padding (don't use append)
    if (!table->allow_irregular_rows) {
      // Strict mode: some rows are shorter, would need padding - fail
      return GTEXT_CSV_E_INVALID;
    }
    // Irregular mode: continue with insert logic (will pad short rows)
  }

  // Empty table check: if row_count == 0, return error
  if (table->row_count == 0) {
    return GTEXT_CSV_E_INVALID;
  }

  // Use helper function for common column operation logic
  csv_table_field ** new_field_arrays = NULL;
  size_t * old_field_counts = NULL;
  size_t rows_to_modify = 0;
  char * header_field_data = NULL;
  size_t header_field_data_len = 0;
  csv_header_entry * new_entry = NULL;
  size_t header_hash = 0;
  csv_table_field * new_header_fields = NULL;
  size_t old_header_field_count = 0;

  char ** field_data_array = NULL;
  size_t * field_data_lengths = NULL;
  GTEXT_CSV_Status status = csv_column_operation_internal(table, col_idx,
      header_name, header_name_len, values, value_lengths, &new_field_arrays,
      &old_field_counts, &rows_to_modify, &header_field_data,
      &header_field_data_len, &new_entry, &header_hash, &new_header_fields,
      &old_header_field_count, &field_data_array, &field_data_lengths);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Phase 7: Reindex Header Map (if needed)
  // Critical: Only reindex header map entries AFTER all allocations succeed
  csv_header_map_reindex_increment(table, col_idx);

  // Phase 8: Atomic State Update
  // Only after all allocations, copies, and reindexing succeed:
  // 1. Update column_count
  // When inserting beyond column_count, set to col_idx + 1
  // Otherwise, just increment
  if (col_idx > table->column_count) {
    // Inserting beyond current max: new max is col_idx + 1
    if (col_idx == SIZE_MAX) {
      csv_column_op_cleanup_individual(new_field_arrays, old_field_counts,
          field_data_array, field_data_lengths);
      return GTEXT_CSV_E_OOM;
    }
    table->column_count = col_idx + 1;
  }
  else {
    // Normal case: increment column_count
    if (table->column_count == SIZE_MAX) {
      csv_column_op_cleanup_individual(new_field_arrays, old_field_counts,
          field_data_array, field_data_lengths);
      return GTEXT_CSV_E_OOM;
    }
    table->column_count++;
  }

  // 2. Update all data row structures
  size_t start_row_idx = csv_get_start_row_idx(table);
  size_t array_idx = 0;
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];
    size_t old_field_count = old_field_counts[array_idx];
    csv_table_field * new_fields = new_field_arrays[array_idx];

    row->fields = new_fields;
    // When padding occurred (col_idx > old_field_count), new field_count is
    // col_idx + 1 Otherwise, it's old_field_count + 1
    if (col_idx > old_field_count) {
      row->field_count = col_idx + 1;
    }
    else {
      row->field_count = old_field_count + 1;
    }

    array_idx++;
  }

  // 3. Update header row structure (if present)
  if (table->has_header && table->header_map && table->row_count > 0) {
    csv_table_row * header_row = &table->rows[0];
    header_row->fields = new_header_fields;
    // When padding occurred (col_idx > old_header_field_count), new field_count
    // is col_idx + 1 Otherwise, it's old_header_field_count + 1
    if (col_idx > old_header_field_count) {
      header_row->field_count = col_idx + 1;
    }
    else {
      header_row->field_count = old_header_field_count + 1;
    }

    // 4. Add header map entry
    new_entry->name = new_header_fields[col_idx].data;
    new_entry->name_len = new_header_fields[col_idx].length;
    new_entry->index = col_idx;
    new_entry->next = table->header_map[header_hash];
    table->header_map[header_hash] = new_entry;

    // Update reverse mapping
    csv_set_index_to_entry(table, col_idx, new_entry);
  }

  // Free temporary arrays
  csv_column_op_cleanup_individual(
      new_field_arrays, old_field_counts, field_data_array, field_data_lengths);

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_column_insert(GTEXT_CSV_Table * table,
    size_t col_idx, const char * header_name, size_t header_name_len) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Validate col_idx (must be <= column count, unless irregular rows allowed)
  if (col_idx > table->column_count) {
    if (!table->allow_irregular_rows) {
      // Strict mode: require col_idx <= column_count
      return GTEXT_CSV_E_INVALID;
    }
    // Irregular mode: will update column_count and pad rows - continue
  }

  // If inserting at end (col_idx == column_count), check if we can use append
  // logic (only if all rows have column_count fields, or if irregular rows
  // are allowed and all rows are already at column_count)
  if (col_idx == table->column_count) {
    // Check if all rows have column_count fields
    bool all_rows_at_column_count = true;
    if (table->row_count > 0) {
      size_t start_row_idx = csv_get_start_row_idx(table);
      for (size_t row_idx = start_row_idx; row_idx < table->row_count;
           row_idx++) {
        if (table->rows[row_idx].field_count < table->column_count) {
          all_rows_at_column_count = false;
          break;
        }
      }
    }

    // If all rows are at column_count, use append (no padding needed)
    if (all_rows_at_column_count) {
      return gtext_csv_column_append(table, header_name, header_name_len);
    }

    // If irregular rows are allowed but some rows are shorter, we need to use
    // insert logic with padding (don't use append)
    if (!table->allow_irregular_rows) {
      // Strict mode: some rows are shorter, would need padding - fail
      return GTEXT_CSV_E_INVALID;
    }
    // Irregular mode: continue with insert logic (will pad short rows)
  }

  // Use helper function for common column operation logic
  csv_table_field ** new_field_arrays = NULL;
  size_t * old_field_counts = NULL;
  size_t rows_to_modify = 0;
  char * header_field_data = NULL;
  size_t header_field_data_len = 0;
  csv_header_entry * new_entry = NULL;
  size_t header_hash = 0;
  csv_table_field * new_header_fields = NULL;
  size_t old_header_field_count = 0;

  char ** field_data_array = NULL;
  size_t * field_data_lengths = NULL;
  GTEXT_CSV_Status status = csv_column_operation_internal(table, col_idx,
      header_name, header_name_len, NULL, NULL, // No values - empty fields
      &new_field_arrays, &old_field_counts, &rows_to_modify, &header_field_data,
      &header_field_data_len, &new_entry, &header_hash, &new_header_fields,
      &old_header_field_count, &field_data_array, &field_data_lengths);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Phase 6: Reindex Header Map (if needed)
  // Critical: Only reindex header map entries AFTER all allocations succeed
  csv_header_map_reindex_increment(table, col_idx);

  // Phase 7: Atomic State Update
  // Only after all allocations, copies, and reindexing succeed:
  // 1. Update column_count
  // When inserting beyond column_count, set to col_idx + 1
  // Otherwise, just increment
  if (col_idx > table->column_count) {
    // Inserting beyond current max: new max is col_idx + 1
    if (col_idx == SIZE_MAX) {
      csv_column_op_cleanup_individual(new_field_arrays, old_field_counts,
          field_data_array, field_data_lengths);
      return GTEXT_CSV_E_OOM;
    }
    table->column_count = col_idx + 1;
  }
  else {
    // Normal case: increment column_count
    if (table->column_count == SIZE_MAX) {
      csv_column_op_cleanup_individual(new_field_arrays, old_field_counts,
          field_data_array, field_data_lengths);
      return GTEXT_CSV_E_OOM;
    }
    table->column_count++;
  }

  // 2. Update all data row structures
  size_t start_row_idx = csv_get_start_row_idx(table);
  size_t array_idx = 0;
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];
    size_t old_field_count = old_field_counts[array_idx];
    csv_table_field * new_fields = new_field_arrays[array_idx];

    row->fields = new_fields;
    // When padding occurred (col_idx > old_field_count), new field_count is
    // col_idx + 1 Otherwise, it's old_field_count + 1
    if (col_idx > old_field_count) {
      row->field_count = col_idx + 1;
    }
    else {
      row->field_count = old_field_count + 1;
    }

    array_idx++;
  }

  // 3. Update header row structure (if present)
  if (table->has_header && table->header_map && table->row_count > 0) {
    csv_table_row * header_row = &table->rows[0];
    header_row->fields = new_header_fields;
    // When padding occurred (col_idx > old_header_field_count), new field_count
    // is col_idx + 1 Otherwise, it's old_header_field_count + 1
    if (col_idx > old_header_field_count) {
      header_row->field_count = col_idx + 1;
    }
    else {
      header_row->field_count = old_header_field_count + 1;
    }

    // 4. Add header map entry
    new_entry->name = new_header_fields[col_idx].data;
    new_entry->name_len = new_header_fields[col_idx].length;
    new_entry->index = col_idx;
    new_entry->next = table->header_map[header_hash];
    table->header_map[header_hash] = new_entry;

    // Update reverse mapping
    csv_set_index_to_entry(table, col_idx, new_entry);
  }

  // Free temporary arrays
  csv_column_op_cleanup_individual(
      new_field_arrays, old_field_counts, field_data_array, field_data_lengths);

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_column_remove(
    GTEXT_CSV_Table * table, size_t col_idx) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Validate col_idx (must be < column count)
  if (col_idx >= table->column_count) {
    return GTEXT_CSV_E_INVALID;
  }

  // Cannot remove from empty table
  if (table->column_count == 0) {
    return GTEXT_CSV_E_INVALID;
  }

  // Determine start row index (skip header row if present)
  size_t start_row_idx = csv_get_start_row_idx(table);

  // Phase 1: Remove Header Map Entry (if needed)
  // Find and remove the header map entry for col_idx
  if (table->has_header && table->header_map && table->row_count > 0) {
    // Find the entry with index == col_idx using O(1) reverse mapping
    csv_header_entry * entry_to_remove = NULL;
    csv_header_entry ** prev_ptr = NULL;

    if (csv_find_header_entry_by_index(
            table, col_idx, &entry_to_remove, &prev_ptr)) {
      // Remove entry from hash chain
      *prev_ptr = entry_to_remove->next;

      // Clear reverse mapping
      csv_set_index_to_entry(table, col_idx, NULL);
    }

    // Phase 2: Reindex Header Map Entries
    csv_header_map_reindex_decrement(table, col_idx);
  }

  // Phase 3: Shift Fields in All Rows
  // For each row, shift fields from col_idx+1 to field_count-1 left by one
  // position
  for (size_t row_idx = start_row_idx; row_idx < table->row_count; row_idx++) {
    csv_table_row * row = &table->rows[row_idx];

    // Validate col_idx is within bounds for this row
    if (col_idx >= row->field_count) {
      // This should not happen if column_count is consistent, but check anyway
      continue;
    }

    // Shift fields left: move fields from col_idx+1 to end one position left
    if (col_idx + 1 < row->field_count) {
      memmove(row->fields + col_idx, row->fields + col_idx + 1,
          sizeof(csv_table_field) * (row->field_count - col_idx - 1));
    }

    // Decrement field count
    row->field_count--;
  }

  // Phase 4: Shift Header Row Fields (if present)
  if (table->has_header && table->row_count > 0) {
    csv_table_row * header_row = &table->rows[0];

    // Validate col_idx is within bounds for header row
    if (col_idx < header_row->field_count) {
      // Shift header fields left
      if (col_idx + 1 < header_row->field_count) {
        memmove(header_row->fields + col_idx, header_row->fields + col_idx + 1,
            sizeof(csv_table_field) * (header_row->field_count - col_idx - 1));
      }

      // Decrement header row field count
      header_row->field_count--;
    }
  }

  // Phase 5: Atomic State Update
  // Decrement column count
  table->column_count--;

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_column_rename(GTEXT_CSV_Table * table,
    size_t col_idx, const char * new_name, size_t new_name_length) {
  // Validate inputs
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  if (!new_name) {
    return GTEXT_CSV_E_INVALID;
  }

  // Check table has headers (return error if not)
  if (!table->has_header || !table->header_map) {
    return GTEXT_CSV_E_INVALID;
  }

  // Validate col_idx (must be < column count)
  if (col_idx >= table->column_count) {
    return GTEXT_CSV_E_INVALID;
  }

  // Cannot rename in empty table
  if (table->column_count == 0 || table->row_count == 0) {
    return GTEXT_CSV_E_INVALID;
  }

  // Calculate new_name length if not provided
  size_t name_len = new_name_length;
  if (name_len == 0) {
    name_len = csv_calculate_field_length(new_name, NULL, 0);
  }

  // Calculate hash for new name (needed for later insertion)
  size_t hash = csv_header_hash(new_name, name_len, table->header_map_size);

  // Check for duplicate header name if uniqueness is required
  // Exclude the column being renamed from the check
  GTEXT_CSV_Status uniqueness_status = csv_check_header_uniqueness(
      table, new_name, name_len, col_idx // Exclude the column being renamed
  );
  if (uniqueness_status != GTEXT_CSV_OK) {
    return uniqueness_status;
  }

  // Phase 1: Find Old Header Map Entry
  // Find the entry with index == col_idx using O(1) reverse mapping
  csv_header_entry * entry_to_remove = NULL;
  csv_header_entry ** prev_ptr = NULL;

  if (!csv_find_header_entry_by_index(
          table, col_idx, &entry_to_remove, &prev_ptr)) {
    // Entry not found - this is an error (should not happen if table is
    // consistent)
    return GTEXT_CSV_E_INVALID;
  }

  // Phase 3: Pre-allocate New Name String
  // Allocate new name string in arena (before any state changes)
  char * new_name_data = NULL;
  if (name_len == 0) {
    // Empty name - use global empty string constant
    new_name_data = (char *)csv_empty_field_string;
  }
  else {
    // Check for overflow in name_len + 1
    if (name_len > SIZE_MAX - 1) {
      return GTEXT_CSV_E_OOM;
    }

    // Allocate name data in arena
    new_name_data =
        (char *)csv_arena_alloc_for_context(table->ctx, name_len + 1, 1);
    if (!new_name_data) {
      return GTEXT_CSV_E_OOM;
    }

    // Copy name data
    memcpy(new_name_data, new_name, name_len);
    new_name_data[name_len] = '\0';
  }

  // Phase 4: Pre-allocate New Header Map Entry
  // Allocate new header entry in arena (before any state changes)
  csv_header_entry * new_entry =
      (csv_header_entry *)csv_arena_alloc_for_context(
          table->ctx, sizeof(csv_header_entry), 8);
  if (!new_entry) {
    // Note: new_name_data remains in arena but won't be referenced
    // This is acceptable - arena cleanup will handle it
    return GTEXT_CSV_E_OOM;
  }

  // Phase 5: Update Header Field in Header Row
  // Update the header field at col_idx in the header row
  csv_table_row * header_row = &table->rows[0];
  if (col_idx >= header_row->field_count) {
    // This should not happen if table is consistent, but check anyway
    return GTEXT_CSV_E_INVALID;
  }

  csv_table_field * header_field = &header_row->fields[col_idx];
  if (name_len == 0) {
    csv_setup_empty_field(header_field);
  }
  else {
    header_field->data = new_name_data;
    header_field->length = name_len;
    header_field->is_in_situ = false; // Always in arena after rename
  }

  // Phase 6: Remove Old Entry from Header Map
  // Remove old entry from hash chain
  *prev_ptr = entry_to_remove->next;

  // Clear old entry from reverse mapping
  csv_set_index_to_entry(table, col_idx, NULL);

  // Phase 7: Add New Entry to Header Map
  // Initialize new entry
  new_entry->name = new_name_data;
  new_entry->name_len = name_len;
  new_entry->index = col_idx; // Same index as before
  new_entry->next = table->header_map[hash];
  table->header_map[hash] = new_entry;

  // Update reverse mapping with new entry
  csv_set_index_to_entry(table, col_idx, new_entry);

  // Note: Old entry structure remains in arena (no individual cleanup needed)
  // Old name string also remains in arena (no individual cleanup needed)

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_header_index(
    const GTEXT_CSV_Table * table, const char * name, size_t * out_idx) {
  if (!table || !name || !out_idx) {
    return GTEXT_CSV_E_INVALID;
  }

  if (!table->has_header || !table->header_map) {
    return GTEXT_CSV_E_INVALID;
  }

  size_t name_len = csv_calculate_field_length(name, NULL, 0);
  size_t hash = csv_header_hash(name, name_len, table->header_map_size);

  csv_header_entry * entry = table->header_map[hash];
  while (entry) {
    if (entry->name_len == name_len &&
        memcmp(entry->name, name, name_len) == 0) {
      *out_idx = entry->index;
      return GTEXT_CSV_OK;
    }
    entry = entry->next;
  }

  return GTEXT_CSV_E_INVALID;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_header_index_next(
    const GTEXT_CSV_Table * table, const char * name, size_t current_idx,
    size_t * out_idx) {
  if (!table || !name || !out_idx) {
    return GTEXT_CSV_E_INVALID;
  }

  if (!table->has_header || !table->header_map) {
    return GTEXT_CSV_E_INVALID;
  }

  // Validate current_idx is within valid column range
  if (current_idx >= table->column_count) {
    return GTEXT_CSV_E_INVALID;
  }

  size_t name_len = csv_calculate_field_length(name, NULL, 0);
  size_t hash = csv_header_hash(name, name_len, table->header_map_size);

  // Search through all entries in the hash bucket
  csv_header_entry * entry = table->header_map[hash];
  bool found_current = false;
  size_t min_next_idx = SIZE_MAX;

  while (entry) {
    if (entry->name_len == name_len &&
        memcmp(entry->name, name, name_len) == 0) {
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
    return GTEXT_CSV_E_INVALID;
  }

  // If we found a next index, return it
  if (min_next_idx != SIZE_MAX && min_next_idx > current_idx) {
    *out_idx = min_next_idx;
    return GTEXT_CSV_OK;
  }

  // No more matches found
  return GTEXT_CSV_E_INVALID;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_set_require_unique_headers(
    GTEXT_CSV_Table * table, bool require) {
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  table->require_unique_headers = require;
  return GTEXT_CSV_OK;
}

GTEXT_API bool gtext_csv_can_have_unique_headers(
    const GTEXT_CSV_Table * table) {
  // Handle NULL table gracefully (return false)
  if (!table) {
    return false;
  }

  // Check if table has headers
  if (!table->has_header || table->row_count == 0) {
    return false;
  }

  // Get the header row (first row when has_header is true)
  csv_table_row * header_row = &table->rows[0];
  if (!header_row || header_row->field_count == 0) {
    return false;
  }

  // Check for duplicate header names by comparing all fields in the header row
  // For each field, check if there's another field with the same name
  for (size_t i = 0; i < header_row->field_count; i++) {
    csv_table_field * field = &header_row->fields[i];
    const char * name = field->data;
    size_t name_len = field->length;

    // Count occurrences of this name in the header row
    size_t count = 0;
    for (size_t j = 0; j < header_row->field_count; j++) {
      csv_table_field * check_field = &header_row->fields[j];
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

GTEXT_API GTEXT_CSV_Status gtext_csv_set_allow_irregular_rows(
    GTEXT_CSV_Table * table, bool allow) {
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  table->allow_irregular_rows = allow;
  return GTEXT_CSV_OK;
}

GTEXT_API bool gtext_csv_get_allow_irregular_rows(
    const GTEXT_CSV_Table * table) {
  // Handle NULL table gracefully (return false)
  if (!table) {
    return false;
  }

  return table->allow_irregular_rows;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_normalize_rows(GTEXT_CSV_Table * table,
    size_t target_column_count, bool truncate_long_rows) {
  // Phase 1: Validation
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Handle empty table
  if (table->row_count == 0) {
    // Empty table: set column_count to target if specified, otherwise no-op
    if (target_column_count != 0 && target_column_count != SIZE_MAX) {
      table->column_count = target_column_count;
    }
    return GTEXT_CSV_OK;
  }

  // Phase 2: Calculate target column count
  size_t actual_target = target_column_count;
  if (target_column_count == 0) {
    // Find maximum column count across all rows
    actual_target = csv_recalculate_max_column_count(table);
  }
  else if (target_column_count == SIZE_MAX) {
    // Find minimum column count across all rows
    size_t min_count = SIZE_MAX;
    for (size_t i = 0; i < table->row_count; i++) {
      if (table->rows[i].field_count < min_count) {
        min_count = table->rows[i].field_count;
      }
    }
    if (min_count == SIZE_MAX) {
      // All rows empty or no rows (shouldn't happen due to empty check above)
      return GTEXT_CSV_E_INVALID;
    }
    actual_target = min_count;
  }

  // Optimization: Check if already normalized (no-op)
  bool already_normalized = true;
  for (size_t i = 0; i < table->row_count; i++) {
    if (table->rows[i].field_count != actual_target) {
      already_normalized = false;
      break;
    }
  }
  if (already_normalized) {
    table->column_count = actual_target;
    return GTEXT_CSV_OK;
  }

  // Phase 3: Validate all rows (if truncate_long_rows is false)
  if (!truncate_long_rows) {
    for (size_t i = 0; i < table->row_count; i++) {
      if (table->rows[i].field_count > actual_target) {
        return GTEXT_CSV_E_INVALID;
      }
    }
  }

  // Phase 4: Pre-allocate all new field arrays (atomic)
  csv_table_field ** new_field_arrays =
      (csv_table_field **)calloc(table->row_count, sizeof(csv_table_field *));
  if (!new_field_arrays) {
    return GTEXT_CSV_E_OOM;
  }

  // Allocate each row's new field array
  for (size_t i = 0; i < table->row_count; i++) {
    // Allocate new field array
    csv_table_field * new_fields =
        (csv_table_field *)csv_arena_alloc_for_context(
            table->ctx, sizeof(csv_table_field) * actual_target, 8);
    if (!new_fields) {
      // Cleanup: free already allocated arrays
      for (size_t j = 0; j < i; j++) {
        // Arrays are in arena, no individual free needed
      }
      free(new_field_arrays);
      return GTEXT_CSV_E_OOM;
    }
    new_field_arrays[i] = new_fields;
  }

  // Phase 5: Copy/pad/truncate all rows
  for (size_t i = 0; i < table->row_count; i++) {
    csv_table_row * row = &table->rows[i];
    size_t old_field_count = row->field_count;
    csv_table_field * new_fields = new_field_arrays[i];

    // Copy existing fields (up to min(old_count, target))
    size_t fields_to_copy =
        old_field_count < actual_target ? old_field_count : actual_target;
    if (fields_to_copy > 0) {
      memcpy(new_fields, row->fields, sizeof(csv_table_field) * fields_to_copy);
    }

    // Pad with empty fields if short
    for (size_t j = old_field_count; j < actual_target; j++) {
      csv_setup_empty_field(&new_fields[j]);
    }
    // Truncation is handled by only copying up to actual_target
  }

  // Phase 6: Atomic state update
  // Only after all allocations and copies succeed:
  for (size_t i = 0; i < table->row_count; i++) {
    table->rows[i].fields = new_field_arrays[i];
    table->rows[i].field_count = actual_target;
  }
  table->column_count = actual_target;

  // Free temporary array (field arrays themselves are in arena)
  free(new_field_arrays);

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_normalize_to_max(GTEXT_CSV_Table * table) {
  return gtext_csv_normalize_rows(table, 0, false);
}

GTEXT_API bool gtext_csv_has_irregular_rows(const GTEXT_CSV_Table * table) {
  if (!table) {
    return false;
  }

  // Empty table has no irregular rows
  if (table->row_count == 0) {
    return false;
  }

  // Check all rows (including header row if present)
  for (size_t i = 0; i < table->row_count; i++) {
    if (table->rows[i].field_count != table->column_count) {
      return true;
    }
  }

  return false;
}

GTEXT_API size_t gtext_csv_max_col_count(const GTEXT_CSV_Table * table) {
  if (!table || table->row_count == 0) {
    return 0;
  }

  size_t max_count = 0;
  for (size_t i = 0; i < table->row_count; i++) {
    if (table->rows[i].field_count > max_count) {
      max_count = table->rows[i].field_count;
    }
  }

  return max_count;
}

GTEXT_API size_t gtext_csv_min_col_count(const GTEXT_CSV_Table * table) {
  if (!table || table->row_count == 0) {
    return 0;
  }

  size_t min_count = SIZE_MAX;
  for (size_t i = 0; i < table->row_count; i++) {
    if (table->rows[i].field_count < min_count) {
      min_count = table->rows[i].field_count;
    }
  }

  // If min_count is still SIZE_MAX, all rows were empty (shouldn't happen)
  // but return 0 for safety
  if (min_count == SIZE_MAX) {
    return 0;
  }

  return min_count;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_validate_table(
    const GTEXT_CSV_Table * table) {
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  // Check row_count <= row_capacity
  if (table->row_count > table->row_capacity) {
    return GTEXT_CSV_E_INVALID;
  }

  // Check each row
  for (size_t i = 0; i < table->row_count; i++) {
    csv_table_row * row = &table->rows[i];

    // Check field_count > 0 implies fields != NULL
    if (row->field_count > 0 && row->fields == NULL) {
      return GTEXT_CSV_E_INVALID;
    }

    // Check field data pointers (if length > 0, data != NULL)
    for (size_t j = 0; j < row->field_count; j++) {
      csv_table_field * field = &row->fields[j];
      if (field->length > 0 && field->data == NULL) {
        return GTEXT_CSV_E_INVALID;
      }
    }
  }

  // Check header map consistency (if present)
  if (table->has_header && table->header_map) {
    if (table->row_count == 0) {
      // Headers but no rows is invalid
      return GTEXT_CSV_E_INVALID;
    }

    csv_table_row * header_row = &table->rows[0];
    // Header row should have fields if field_count > 0
    if (header_row->field_count > 0 && header_row->fields == NULL) {
      return GTEXT_CSV_E_INVALID;
    }
  }

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_set_header_row(
    GTEXT_CSV_Table * table, bool enable) {
  if (!table) {
    return GTEXT_CSV_E_INVALID;
  }

  if (enable) {
    // Enable headers: first row becomes header row
    // Validate: table must not be empty
    if (table->row_count == 0) {
      return GTEXT_CSV_E_INVALID;
    }

    // Validate: headers must not already exist
    if (table->has_header) {
      return GTEXT_CSV_E_INVALID;
    }

    // Get first row (will become header row)
    csv_table_row * first_row = &table->rows[0];
    size_t first_row_cols = first_row->field_count;

    // Handle column count:
    // - If first row has fewer columns than column_count: pad with empty
    // strings
    // - If first row has more columns than column_count: update column_count
    size_t target_col_count = first_row_cols;
    if (first_row_cols < table->column_count) {
      // Pad first row with empty fields
      target_col_count = table->column_count;
      csv_table_field * new_fields =
          (csv_table_field *)csv_arena_alloc_for_context(
              table->ctx, sizeof(csv_table_field) * target_col_count, 8);
      if (!new_fields) {
        return GTEXT_CSV_E_OOM;
      }

      // Copy existing fields
      if (first_row->fields && first_row_cols > 0) {
        memcpy(new_fields, first_row->fields,
            sizeof(csv_table_field) * first_row_cols);
      }

      // Pad with empty fields
      for (size_t i = first_row_cols; i < target_col_count; i++) {
        csv_setup_empty_field(&new_fields[i]);
      }

      first_row->fields = new_fields;
      first_row->field_count = target_col_count;
    }
    else if (first_row_cols > table->column_count) {
      // Update column_count to match first row
      table->column_count = first_row_cols;
    }

    // If require_unique_headers is true, validate uniqueness
    if (table->require_unique_headers) {
      csv_table_row * header_row = &table->rows[0];
      for (size_t i = 0; i < header_row->field_count; i++) {
        csv_table_field * field = &header_row->fields[i];
        const char * name = field->data;
        size_t name_len = field->length;

        // Check for duplicates in the header row
        for (size_t j = i + 1; j < header_row->field_count; j++) {
          csv_table_field * check_field = &header_row->fields[j];
          if (check_field->length == name_len &&
              memcmp(check_field->data, name, name_len) == 0) {
            // Duplicate found and uniqueness is required
            return GTEXT_CSV_E_INVALID;
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
    table->header_map = (csv_header_entry **)calloc(
        table->header_map_size, sizeof(csv_header_entry *));
    if (!table->header_map) {
      return GTEXT_CSV_E_OOM;
    }

    csv_table_row * header_row = &table->rows[0];
    for (size_t i = 0; i < header_row->field_count; i++) {
      csv_table_field * field = &header_row->fields[i];
      size_t hash =
          csv_header_hash(field->data, field->length, table->header_map_size);

      // Check for duplicates (for FIRST_WINS mode, skip duplicates)
      csv_header_entry * entry = table->header_map[hash];
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
      csv_header_entry * new_entry =
          (csv_header_entry *)csv_arena_alloc_for_context(
              table->ctx, sizeof(csv_header_entry), 8);
      if (!new_entry) {
        // Allocation failed - clean up and return error
        free(table->header_map);
        table->header_map = NULL;
        table->header_map_size = 0;
        return GTEXT_CSV_E_OOM;
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
  }
  else {
    // Disable headers: header row becomes first data row
    // Validate: headers must exist
    if (!table->has_header) {
      return GTEXT_CSV_E_INVALID;
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

  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Table * gtext_csv_new_table(void) {
  // Create context with arena
  csv_context * ctx = csv_context_new();
  if (!ctx) {
    return NULL;
  }

  // Allocate table structure
  GTEXT_CSV_Table * table = (GTEXT_CSV_Table *)malloc(sizeof(GTEXT_CSV_Table));
  if (!table) {
    csv_context_free(ctx);
    return NULL;
  }

  // Initialize table structure
  memset(table, 0, sizeof(GTEXT_CSV_Table));
  table->ctx = ctx;
  table->row_capacity = 16;
  table->row_count = 0;
  table->column_count = 0; // No columns defined until first row
  table->has_header = false;
  table->header_map = NULL;
  table->header_map_size = 0;

  // Allocate initial rows array
  table->rows = (csv_table_row *)csv_arena_alloc_for_context(
      ctx, sizeof(csv_table_row) * table->row_capacity, 8);
  if (!table->rows) {
    free(table);
    csv_context_free(ctx);
    return NULL;
  }

  return table;
}

GTEXT_API GTEXT_CSV_Table * gtext_csv_new_table_with_headers(
    const char * const * headers, const size_t * header_lengths,
    size_t header_count) {
  // Validate inputs
  if (!headers || header_count == 0) {
    return NULL;
  }

  // Create context with arena
  csv_context * ctx = csv_context_new();
  if (!ctx) {
    return NULL;
  }

  // Allocate table structure
  GTEXT_CSV_Table * table = (GTEXT_CSV_Table *)malloc(sizeof(GTEXT_CSV_Table));
  if (!table) {
    csv_context_free(ctx);
    return NULL;
  }

  // Initialize table structure
  memset(table, 0, sizeof(GTEXT_CSV_Table));
  table->ctx = ctx;
  table->row_capacity = 16;
  table->row_count = 0;
  table->column_count = header_count;
  table->has_header = false; // Will be set to true after header row is created
  table->header_map = NULL;
  table->header_map_size = 0;

  // Allocate initial rows array
  table->rows = (csv_table_row *)csv_arena_alloc_for_context(
      ctx, sizeof(csv_table_row) * table->row_capacity, 8);
  if (!table->rows) {
    free(table);
    csv_context_free(ctx);
    return NULL;
  }

  // Allocate header row structure
  csv_table_row * header_row = &table->rows[0];

  // Allocate field array for header row
  csv_table_field * header_fields =
      (csv_table_field *)csv_arena_alloc_for_context(
          ctx, sizeof(csv_table_field) * header_count, 8);
  if (!header_fields) {
    free(table);
    csv_context_free(ctx);
    return NULL;
  }

  // Copy each header name to arena
  for (size_t i = 0; i < header_count; i++) {
    csv_table_field * field = &header_fields[i];
    const char * header_data = headers[i];

    // Calculate header length
    size_t header_len =
        csv_calculate_field_length(header_data, header_lengths, i);

    // Use global empty string constant for empty headers
    if (header_len == 0) {
      csv_setup_empty_field(field);
      continue;
    }

    // Allocate and copy header data to arena
    GTEXT_CSV_Status status =
        csv_allocate_and_copy_field(ctx, header_data, header_len, field);
    if (status != GTEXT_CSV_OK) {
      free(table);
      csv_context_free(ctx);
      return NULL;
    }
  }

  // Set header row structure
  header_row->fields = header_fields;
  header_row->field_count = header_count;
  table->row_count = 1; // Header row is at index 0

  // Build header map (hash table) for lookup
  table->header_map_size = 16;
  table->header_map = (csv_header_entry **)calloc(
      table->header_map_size, sizeof(csv_header_entry *));
  if (!table->header_map) {
    free(table);
    csv_context_free(ctx);
    return NULL;
  }

  // Add each header to the map
  for (size_t i = 0; i < header_count; i++) {
    csv_table_field * field = &header_fields[i];
    size_t hash =
        csv_header_hash(field->data, field->length, table->header_map_size);

    // Check for duplicates
    csv_header_entry * entry = table->header_map[hash];
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
    csv_header_entry * new_entry =
        (csv_header_entry *)csv_arena_alloc_for_context(
            ctx, sizeof(csv_header_entry), 8);
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
