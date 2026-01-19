/**
 * @file json_writer.c
 * @brief JSON writer infrastructure implementation
 *
 * This file implements the sink abstraction for writing JSON output
 * to various destinations.
 */

#include <ghoti.io/text/json/json_writer.h>
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include "json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

// Internal write callback for growable buffer sink
static int buffer_write_fn(void* user, const char* bytes, size_t len) {
  text_json_buffer_sink* buf = (text_json_buffer_sink*)user;
  if (!buf || !bytes) {
    return 1; // Error
  }

  // Check for integer overflow in needed calculation
  if (len > SIZE_MAX - buf->used || buf->used > SIZE_MAX - len - 1) {
    return 1; // Overflow
  }

  // Check if we need to grow the buffer
  size_t needed = buf->used + len + 1; // +1 for null terminator
  if (needed > buf->size) {
    // Grow buffer (double size strategy, with minimum growth)
    size_t new_size = buf->size;
    if (new_size == 0) {
      new_size = 256; // Initial size
    }
    while (new_size < needed) {
      // Check for overflow before doubling
      if (new_size > SIZE_MAX / 2) {
        return 1; // Overflow - cannot grow further
      }
      new_size *= 2;
    }

    char* new_data = (char*)realloc(buf->data, new_size);
    if (!new_data) {
      return 1; // Out of memory
    }
    buf->data = new_data;
    buf->size = new_size;
  }

  // Verify bounds before copying (defensive check)
  if (buf->used + len > buf->size - 1) {
    return 1; // Should not happen, but be safe
  }

  // Copy data
  memcpy(buf->data + buf->used, bytes, len);
  buf->used += len;
  // Verify bounds before writing null terminator
  if (buf->used < buf->size) {
    buf->data[buf->used] = '\0'; // Null terminate for convenience
  }

  return 0; // Success
}

// Internal write callback for fixed buffer sink
static int fixed_buffer_write_fn(void* user, const char* bytes, size_t len) {
  text_json_fixed_buffer_sink* buf = (text_json_fixed_buffer_sink*)user;
  if (!buf || !bytes) {
    return 1; // Error
  }

  // Calculate how much we can write (leave room for null terminator)
  // We can write up to (size - used - 1) bytes to leave room for null terminator
  // If used >= size, available = 0 (no underflow possible since size_t is unsigned)
  size_t available = 0;
  if (buf->size > buf->used) {
    // Check for underflow: if size - used would underflow, but since size > used,
    // we know size - used is valid. Then check if we can subtract 1.
    if (buf->size - buf->used > 1) {
      available = buf->size - buf->used - 1;
    }
    // else: available remains 0 (only room for null terminator, no data)
  }

  size_t to_write = len;
  int truncated = 0;

  if (to_write > available) {
    to_write = available;
    truncated = 1;
    buf->truncated = 1;
  }

  // Copy data - verify bounds before copying
  if (to_write > 0 && available > 0) {
    // Verify bounds: used + to_write must not exceed size - 1
    // This check ensures we don't write beyond buffer bounds
    if (buf->used <= buf->size - 1 && to_write <= buf->size - 1 - buf->used) {
      memcpy(buf->data + buf->used, bytes, to_write);
      buf->used += to_write;
      // Verify bounds before writing null terminator
      if (buf->used < buf->size) {
        buf->data[buf->used] = '\0';
      }
    } else {
      // Should not happen if logic is correct, but be safe
      truncated = 1;
      buf->truncated = 1;
    }
  }

  // Return error if truncation occurred
  return truncated ? 1 : 0;
}

text_json_status text_json_sink_buffer(text_json_sink* sink) {
  if (!sink) {
    return TEXT_JSON_E_INVALID;
  }

  text_json_buffer_sink* buf = (text_json_buffer_sink*)malloc(sizeof(text_json_buffer_sink));
  if (!buf) {
    return TEXT_JSON_E_OOM;
  }

  buf->data = NULL;
  buf->size = 0;
  buf->used = 0;

  sink->write = buffer_write_fn;
  sink->user = buf;

  return TEXT_JSON_OK;
}

const char* text_json_sink_buffer_data(const text_json_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return NULL;
  }

  text_json_buffer_sink* buf = (text_json_buffer_sink*)sink->user;
  if (!buf) {
    return NULL;
  }

  return buf->data ? buf->data : "";
}

size_t text_json_sink_buffer_size(const text_json_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return 0;
  }

  text_json_buffer_sink* buf = (text_json_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

void text_json_sink_buffer_free(text_json_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return;
  }

  text_json_buffer_sink* buf = (text_json_buffer_sink*)sink->user;
  if (buf) {
    free(buf->data);
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

text_json_status text_json_sink_fixed_buffer(
  text_json_sink* sink,
  char* buffer,
  size_t size
) {
  if (!sink || !buffer || size == 0) {
    return TEXT_JSON_E_INVALID;
  }

  text_json_fixed_buffer_sink* buf = (text_json_fixed_buffer_sink*)malloc(sizeof(text_json_fixed_buffer_sink));
  if (!buf) {
    return TEXT_JSON_E_OOM;
  }

  buf->data = buffer;
  buf->size = size;
  buf->used = 0;
  buf->truncated = 0;

  // Initialize buffer with null terminator
  if (size > 0) {
    buffer[0] = '\0';
  }

  sink->write = fixed_buffer_write_fn;
  sink->user = buf;

  return TEXT_JSON_OK;
}

size_t text_json_sink_fixed_buffer_used(const text_json_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return 0;
  }

  text_json_fixed_buffer_sink* buf = (text_json_fixed_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

int text_json_sink_fixed_buffer_truncated(const text_json_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return 0;
  }

  text_json_fixed_buffer_sink* buf = (text_json_fixed_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->truncated;
}

void text_json_sink_fixed_buffer_free(text_json_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return;
  }

  text_json_fixed_buffer_sink* buf = (text_json_fixed_buffer_sink*)sink->user;
  if (buf) {
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

// Helper function to write bytes to sink
static int write_bytes(text_json_sink* sink, const char* bytes, size_t len) {
  if (!sink || !sink->write || !bytes) {
    return 1;
  }
  return sink->write(sink->user, bytes, len);
}

// Helper function to write a single character
static int write_char(text_json_sink* sink, char c) {
  return write_bytes(sink, &c, 1);
}

// Helper function to write a string
static int write_string(text_json_sink* sink, const char* s) {
  if (!s) {
    return 0;
  }
  return write_bytes(sink, s, strlen(s));
}

// Write Unicode escape sequence \uXXXX
static int write_unicode_escape(text_json_sink* sink, unsigned int codepoint) {
  char buf[7];
  int len = snprintf(buf, sizeof(buf), "\\u%04X", codepoint);
  if (len < 0 || (size_t)len >= sizeof(buf)) {
    return 1;
  }
  return write_bytes(sink, buf, (size_t)len);
}

// Escape and write a string value
static int write_escaped_string(
  text_json_sink* sink,
  const char* str,
  size_t len,
  const text_json_write_options* opt
) {
  if (write_char(sink, '"') != 0) {
    return 1;
  }

  const text_json_write_options* opts = opt ? opt : &(text_json_write_options){0};
  int escape_solidus = opts->escape_solidus;
  int escape_unicode = opts->escape_unicode;
  int escape_all_non_ascii = opts->escape_all_non_ascii;

  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];

    // Standard escape sequences
    switch (c) {
      case '"':
        if (write_string(sink, "\\\"") != 0) return 1;
        continue;
      case '\\':
        if (write_string(sink, "\\\\") != 0) return 1;
        continue;
      case '/':
        if (escape_solidus) {
          if (write_string(sink, "\\/") != 0) return 1;
        } else {
          if (write_char(sink, '/') != 0) return 1;
        }
        continue;
      case '\b':
        if (write_string(sink, "\\b") != 0) return 1;
        continue;
      case '\f':
        if (write_string(sink, "\\f") != 0) return 1;
        continue;
      case '\n':
        if (write_string(sink, "\\n") != 0) return 1;
        continue;
      case '\r':
        if (write_string(sink, "\\r") != 0) return 1;
        continue;
      case '\t':
        if (write_string(sink, "\\t") != 0) return 1;
        continue;
    }

    // Control characters (0x00-0x1F) must be escaped as \uXXXX
    if (c < 0x20) {
      if (write_unicode_escape(sink, c) != 0) return 1;
      continue;
    }

    // Non-ASCII characters
    if (c >= 0x80) {
      if (escape_all_non_ascii) {
        // Escape all non-ASCII as \uXXXX
        if (write_unicode_escape(sink, c) != 0) return 1;
        continue;
      } else if (escape_unicode) {
        // For escape_unicode, we need to handle UTF-8 sequences properly
        // For now, escape individual bytes if they're >= 0x80
        // A more sophisticated implementation would decode UTF-8 and escape codepoints
        if (write_unicode_escape(sink, c) != 0) return 1;
        continue;
      }
    }

    // Regular character - write as-is
    if (write_char(sink, (char)c) != 0) return 1;
  }

  if (write_char(sink, '"') != 0) {
    return 1;
  }

  return 0;
}

// Write indentation for pretty printing
static int write_indent(text_json_sink* sink, int depth, const text_json_write_options* opt) {
  if (!opt || !opt->pretty) {
    return 0;
  }

  const char* newline = opt->newline ? opt->newline : "\n";
  if (write_string(sink, newline) != 0) {
    return 1;
  }

  int spaces = opt->indent_spaces > 0 ? opt->indent_spaces : 2;

  // Check for integer overflow: depth * spaces
  // INT_MAX / spaces gives max safe depth
  // Avoid division by zero and check overflow correctly
  if (spaces > 0 && depth > INT_MAX / spaces) {
    return 1; // Overflow would occur
  }

  int total_spaces = depth * spaces;

  for (int i = 0; i < total_spaces; i++) {
    if (write_char(sink, ' ') != 0) {
      return 1;
    }
  }

  return 0;
}

// Write a number value
static int write_number(
  text_json_sink* sink,
  const text_json_value* v,
  const text_json_write_options* opt
) {
  const text_json_write_options* opts = opt ? opt : &(text_json_write_options){0};

  // Check for nonfinite numbers
  if (v->as.number.has_dbl) {
    double d = v->as.number.dbl;
    if (!isfinite(d)) {
      if (!opts->allow_nonfinite_numbers) {
        return 1; // Error: nonfinite not allowed
      }
      if (isnan(d)) {
        return write_string(sink, "NaN");
      } else if (isinf(d)) {
        if (d < 0) {
          return write_string(sink, "-Infinity");
        } else {
          return write_string(sink, "Infinity");
        }
      }
    }
  }

  // Prefer lexeme if available and canonical_numbers is off
  if (v->as.number.lexeme && v->as.number.lexeme_len > 0 && !opts->canonical_numbers) {
    // Check for integer overflow in lexeme_len (defensive, though unlikely)
    if (v->as.number.lexeme_len > SIZE_MAX) {
      return 1; // Invalid length
    }
    return write_bytes(sink, v->as.number.lexeme, v->as.number.lexeme_len);
  }

  // Format from available representation
  char num_buf[64];
  int len = 0;

  // Try int64 first (if available and fits)
  if (v->as.number.has_i64) {
    int64_t i64 = v->as.number.i64;
    len = snprintf(num_buf, sizeof(num_buf), "%lld", (long long)i64);
    if (len > 0 && (size_t)len < sizeof(num_buf)) {
      return write_bytes(sink, num_buf, (size_t)len);
    }
  }

  // Try uint64 next
  if (v->as.number.has_u64) {
    uint64_t u64 = v->as.number.u64;
    len = snprintf(num_buf, sizeof(num_buf), "%llu", (unsigned long long)u64);
    if (len > 0 && (size_t)len < sizeof(num_buf)) {
      return write_bytes(sink, num_buf, (size_t)len);
    }
  }

  // Use double (or format from lexeme if available)
  if (v->as.number.has_dbl) {
    double d = v->as.number.dbl;
    len = snprintf(num_buf, sizeof(num_buf), "%.17g", d);
    if (len > 0 && (size_t)len < sizeof(num_buf)) {
      return write_bytes(sink, num_buf, (size_t)len);
    }
  }

  // Fallback: use lexeme if available
  if (v->as.number.lexeme && v->as.number.lexeme_len > 0) {
    return write_bytes(sink, v->as.number.lexeme, v->as.number.lexeme_len);
  }

  // No valid representation
  return 1;
}

// Recursive write function
static int write_value_recursive(
  text_json_sink* sink,
  const text_json_value* v,
  const text_json_write_options* opt,
  int depth
) {
  if (!v) {
    return write_string(sink, "null");
  }

  const text_json_write_options* opts = opt ? opt : &(text_json_write_options){0};

  switch (v->type) {
    case TEXT_JSON_NULL:
      return write_string(sink, "null");

    case TEXT_JSON_BOOL:
      return write_string(sink, v->as.boolean ? "true" : "false");

    case TEXT_JSON_NUMBER:
      return write_number(sink, v, opts);

    case TEXT_JSON_STRING:
      // Check for NULL string data (empty string is valid, but NULL pointer is not)
      if (!v->as.string.data && v->as.string.len > 0) {
        return 1; // Invalid string state
      }
      return write_escaped_string(sink, v->as.string.data, v->as.string.len, opts);

    case TEXT_JSON_ARRAY: {
      if (write_char(sink, '[') != 0) return 1;

      size_t size = v->as.array.count;

      // Check for NULL elems pointer (defensive)
      if (size > 0 && !v->as.array.elems) {
        return 1; // Invalid array state
      }

      for (size_t i = 0; i < size; i++) {
        if (i > 0) {
          if (write_char(sink, ',') != 0) return 1;
        }

        if (opts->pretty) {
          if (write_indent(sink, depth + 1, opts) != 0) return 1;
        }

        // Bounds check: i < size already checked, but verify elems[i] is valid
        if (!v->as.array.elems || i >= v->as.array.capacity) {
          return 1; // Out of bounds
        }

        if (write_value_recursive(sink, v->as.array.elems[i], opts, depth + 1) != 0) {
          return 1;
        }
      }

      if (opts->pretty && size > 0) {
        if (write_indent(sink, depth, opts) != 0) return 1;
      }

      return write_char(sink, ']');
    }

    case TEXT_JSON_OBJECT: {
      if (write_char(sink, '{') != 0) return 1;

      size_t size = v->as.object.count;

      // Check for NULL pairs pointer (defensive)
      if (size > 0 && !v->as.object.pairs) {
        return 1; // Invalid object state
      }

      // Create index array for sorting if needed
      size_t* indices = NULL;
      if (opts->sort_object_keys && size > 0) {
        // Check for integer overflow in malloc
        if (size > SIZE_MAX / sizeof(size_t)) {
          return 1; // Overflow
        }

        indices = (size_t*)malloc(size * sizeof(size_t));
        if (!indices) {
          return 1; // Out of memory
        }
        for (size_t i = 0; i < size; i++) {
          indices[i] = i;
        }
        // Sort indices by key - use a wrapper function
        // We need to pass the pairs array to the comparison function
        // Since qsort doesn't support context, we'll use a different approach
        // For now, we'll do a simple bubble sort (not optimal but works)
        for (size_t i = 0; i < size - 1; i++) {
          for (size_t j = 0; j < size - 1 - i; j++) {
            size_t idx_a = indices[j];
            size_t idx_b = indices[j + 1];

            // Bounds check indices
            if (idx_a >= size || idx_b >= size || idx_a >= v->as.object.capacity || idx_b >= v->as.object.capacity) {
              free(indices);
              return 1; // Out of bounds
            }

            const char* key_a = v->as.object.pairs[idx_a].key;
            size_t len_a = v->as.object.pairs[idx_a].key_len;
            const char* key_b = v->as.object.pairs[idx_b].key;
            size_t len_b = v->as.object.pairs[idx_b].key_len;

            // Check for NULL keys
            if (!key_a || !key_b) {
              free(indices);
              return 1; // Invalid key
            }

            size_t min_len = len_a < len_b ? len_a : len_b;
            int cmp = memcmp(key_a, key_b, min_len);
            if (cmp > 0 || (cmp == 0 && len_a > len_b)) {
              size_t tmp = indices[j];
              indices[j] = indices[j + 1];
              indices[j + 1] = tmp;
            }
          }
        }
      }

      for (size_t i = 0; i < size; i++) {
        size_t idx = indices ? indices[i] : i;

        // Bounds check index
        if (idx >= size || idx >= v->as.object.capacity) {
          if (indices) free(indices);
          return 1; // Out of bounds
        }

        if (i > 0) {
          if (write_char(sink, ',') != 0) {
            if (indices) free(indices);
            return 1;
          }
        }

        if (opts->pretty) {
          if (write_indent(sink, depth + 1, opts) != 0) {
            if (indices) free(indices);
            return 1;
          }
        }

        // Check for NULL key
        if (!v->as.object.pairs[idx].key) {
          if (indices) free(indices);
          return 1; // Invalid key
        }

        // Write key
        if (write_escaped_string(sink, v->as.object.pairs[idx].key, v->as.object.pairs[idx].key_len, opts) != 0) {
          if (indices) free(indices);
          return 1;
        }

        if (opts->pretty) {
          if (write_string(sink, ": ") != 0) {
            if (indices) free(indices);
            return 1;
          }
        } else {
          if (write_char(sink, ':') != 0) {
            if (indices) free(indices);
            return 1;
          }
        }

        // Write value
        if (write_value_recursive(sink, v->as.object.pairs[idx].value, opts, depth + 1) != 0) {
          if (indices) free(indices);
          return 1;
        }
      }

      if (indices) {
        free(indices);
      }

      if (opts->pretty && size > 0) {
        if (write_indent(sink, depth, opts) != 0) return 1;
      }

      return write_char(sink, '}');
    }

    default:
      return 1; // Unknown type
  }
}

text_json_status text_json_write_value(
  text_json_sink* sink,
  const text_json_write_options* opt,
  const text_json_value* v,
  text_json_error* err
) {
  if (!sink || !v) {
    if (err) {
      err->code = TEXT_JSON_E_INVALID;
      err->message = "Invalid arguments: sink and value must not be NULL";
      err->offset = 0;
      err->line = 0;
      err->col = 0;
    }
    return TEXT_JSON_E_INVALID;
  }

  if (!sink->write) {
    if (err) {
      err->code = TEXT_JSON_E_INVALID;
      err->message = "Invalid sink: write callback is NULL";
      err->offset = 0;
      err->line = 0;
      err->col = 0;
    }
    return TEXT_JSON_E_INVALID;
  }

  int result = write_value_recursive(sink, v, opt, 0);
  if (result != 0) {
    if (err) {
      err->code = TEXT_JSON_E_WRITE;
      err->message = "Write operation failed";
      err->offset = 0;
      err->line = 0;
      err->col = 0;
    }
    return TEXT_JSON_E_WRITE;
  }

  return TEXT_JSON_OK;
}

// ============================================================================
// Streaming Writer Implementation
// ============================================================================

// Stack entry type for tracking nesting
typedef enum {
  JSON_WRITER_STACK_OBJECT,
  JSON_WRITER_STACK_ARRAY
} json_writer_stack_type;

// Stack entry for tracking nesting
typedef struct {
  json_writer_stack_type type;  // Object or array
  int has_elements;              // Whether any elements have been written
  int expecting_key;             // For objects: 1 if expecting key, 0 if expecting value
} json_writer_stack_entry;

// Streaming writer structure
struct text_json_writer {
  text_json_sink sink;                    // Output sink
  text_json_write_options opts;          // Write options (copy)
  json_writer_stack_entry* stack;         // Stack for tracking nesting
  size_t stack_capacity;                  // Stack capacity
  size_t stack_size;                      // Current stack depth
  int error;                              // Error flag (1 if error occurred)
};

// Default stack capacity
#define JSON_WRITER_DEFAULT_STACK_CAPACITY 32

// Helper to write bytes through writer's sink
static int writer_write_bytes(text_json_writer* w, const char* bytes, size_t len) {
  if (!w || !w->sink.write || !bytes) {
    return 1;
  }
  int result = w->sink.write(w->sink.user, bytes, len);
  if (result != 0) {
    w->error = 1;
  }
  return result;
}

// Helper to write a single character
static int writer_write_char(text_json_writer* w, char c) {
  return writer_write_bytes(w, &c, 1);
}

// Helper to write a string
static int writer_write_string(text_json_writer* w, const char* s) {
  if (!s) {
    return 0;
  }
  return writer_write_bytes(w, s, strlen(s));
}

// Write indentation for pretty printing
static int writer_write_indent(text_json_writer* w, int depth) {
  if (!w->opts.pretty) {
    return 0;
  }

  const char* newline = w->opts.newline ? w->opts.newline : "\n";
  if (writer_write_string(w, newline) != 0) {
    return 1;
  }

  int spaces = w->opts.indent_spaces > 0 ? w->opts.indent_spaces : 2;

  // Check for integer overflow: depth * spaces
  if (spaces > 0 && depth > INT_MAX / spaces) {
    return 1; // Overflow would occur
  }

  int total_spaces = depth * spaces;
  for (int i = 0; i < total_spaces; i++) {
    if (writer_write_char(w, ' ') != 0) {
      return 1;
    }
  }

  return 0;
}

// Ensure stack has capacity for at least one more entry
static int writer_ensure_stack(text_json_writer* w) {
  if (w->stack_size >= w->stack_capacity) {
    size_t new_capacity = w->stack_capacity == 0
      ? JSON_WRITER_DEFAULT_STACK_CAPACITY
      : w->stack_capacity * 2;

    // Check for overflow
    if (new_capacity < w->stack_capacity) {
      return 1; // Overflow
    }

    // Check for reasonable maximum (prevent excessive allocation)
    if (new_capacity > 1024 * 1024) { // 1M entries is more than enough
      return 1;
    }

    // Check for overflow in multiplication: new_capacity * sizeof(json_writer_stack_entry)
    size_t entry_size = sizeof(json_writer_stack_entry);
    if (entry_size > 0 && new_capacity > SIZE_MAX / entry_size) {
      return 1; // Overflow
    }

    json_writer_stack_entry* new_stack = (json_writer_stack_entry*)realloc(
      w->stack,
      new_capacity * entry_size
    );
    if (!new_stack) {
      return 1; // Out of memory
    }

    w->stack = new_stack;
    w->stack_capacity = new_capacity;
  }
  return 0;
}

// Push a stack entry
static int writer_push_stack(text_json_writer* w, json_writer_stack_type type) {
  if (writer_ensure_stack(w) != 0) {
    return 1;
  }

  json_writer_stack_entry entry = {
    .type = type,
    .has_elements = 0,
    .expecting_key = (type == JSON_WRITER_STACK_OBJECT) ? 1 : 0
  };

  w->stack[w->stack_size++] = entry;
  return 0;
}

// Pop a stack entry
static int writer_pop_stack(text_json_writer* w) {
  if (w->stack_size == 0) {
    return 1; // Stack underflow
  }
  w->stack_size--;
  return 0;
}

// Get top stack entry (or NULL if empty)
static json_writer_stack_entry* writer_top_stack(text_json_writer* w) {
  if (w->stack_size == 0) {
    return NULL;
  }
  return &w->stack[w->stack_size - 1];
}

// Write comma if needed (before next element)
static int writer_write_comma_if_needed(text_json_writer* w) {
  json_writer_stack_entry* top = writer_top_stack(w);
  if (!top) {
    return 0; // No stack, no comma needed
  }

  if (top->has_elements) {
    if (writer_write_char(w, ',') != 0) {
      return 1;
    }
    if (w->opts.pretty) {
      // Write newline and indent for next element
      // Check that stack_size fits in int (defensive, should always be true due to capacity limit)
      if (w->stack_size > (size_t)INT_MAX) {
        return 1; // Stack size too large
      }
      if (writer_write_indent(w, (int)w->stack_size) != 0) {
        return 1;
      }
    } else {
      // In compact mode, just add space after comma
      if (writer_write_char(w, ' ') != 0) {
        return 1;
      }
    }
  } else {
    // First element - write indent if pretty
    if (w->opts.pretty) {
      // Check that stack_size fits in int (defensive, should always be true due to capacity limit)
      if (w->stack_size > (size_t)INT_MAX) {
        return 1; // Stack size too large
      }
      if (writer_write_indent(w, (int)w->stack_size) != 0) {
        return 1;
      }
    }
  }
  return 0;
}

text_json_writer* text_json_writer_new(
  text_json_sink sink,
  const text_json_write_options* opt
) {
  if (!sink.write) {
    return NULL;
  }

  text_json_writer* w = (text_json_writer*)calloc(1, sizeof(text_json_writer));
  if (!w) {
    return NULL;
  }

  w->sink = sink;
  if (opt) {
    w->opts = *opt;
  } else {
    w->opts = text_json_write_options_default();
  }

  w->stack_capacity = JSON_WRITER_DEFAULT_STACK_CAPACITY;
  w->stack = (json_writer_stack_entry*)calloc(
    w->stack_capacity,
    sizeof(json_writer_stack_entry)
  );
  if (!w->stack) {
    free(w);
    return NULL;
  }

  w->stack_size = 0;
  w->error = 0;

  return w;
}

void text_json_writer_free(text_json_writer* w) {
  if (!w) {
    return;
  }

  if (w->stack) {
    free(w->stack);
  }
  free(w);
}

text_json_status text_json_writer_object_begin(text_json_writer* w) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  // Check if we're in an object expecting a key (can't write value without key)
  json_writer_stack_entry* top = writer_top_stack(w);
  if (top && top->type == JSON_WRITER_STACK_OBJECT && top->expecting_key) {
    return TEXT_JSON_E_STATE; // Can't start object as value - need key first
  }

  // Write comma if needed (for arrays) or handle first element
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write opening brace
  if (writer_write_char(w, '{') != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Push object onto stack
  if (writer_push_stack(w, JSON_WRITER_STACK_OBJECT) != 0) {
    w->error = 1;
    return TEXT_JSON_E_OOM;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_object_end(text_json_writer* w) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);
  if (!top || top->type != JSON_WRITER_STACK_OBJECT) {
    return TEXT_JSON_E_STATE; // Not in an object
  }

  if (!top->expecting_key) {
    return TEXT_JSON_E_STATE; // Incomplete: expecting value after key
  }

  // Write closing brace
  if (w->opts.pretty && top->has_elements) {
    // Indent to parent level (stack_size - 1)
    int indent_depth = (int)w->stack_size - 1;
    if (indent_depth < 0) indent_depth = 0;
    if (writer_write_indent(w, indent_depth) != 0) {
      w->error = 1;
      return TEXT_JSON_E_WRITE;
    }
  }

  if (writer_write_char(w, '}') != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Pop object from stack
  if (writer_pop_stack(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_STATE;
  }

  // Mark parent as having elements and reset expecting_key if it's an object
  top = writer_top_stack(w);
  if (top) {
    top->has_elements = 1;
    // If parent is an object, we just finished writing a value, so reset to expecting key
    if (top->type == JSON_WRITER_STACK_OBJECT) {
      top->expecting_key = 1;
    }
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_array_begin(text_json_writer* w) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  // Check if we're in an object expecting a key (can't write value without key)
  json_writer_stack_entry* top = writer_top_stack(w);
  if (top && top->type == JSON_WRITER_STACK_OBJECT && top->expecting_key) {
    return TEXT_JSON_E_STATE; // Can't start array as value - need key first
  }

  // Write comma if needed (for arrays) or handle first element
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write opening bracket
  if (writer_write_char(w, '[') != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Push array onto stack
  if (writer_push_stack(w, JSON_WRITER_STACK_ARRAY) != 0) {
    w->error = 1;
    return TEXT_JSON_E_OOM;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_array_end(text_json_writer* w) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);
  if (!top || top->type != JSON_WRITER_STACK_ARRAY) {
    return TEXT_JSON_E_STATE; // Not in an array
  }

  // Write closing bracket
  if (w->opts.pretty && top->has_elements) {
    // Indent to parent level (stack_size - 1)
    int indent_depth = (int)w->stack_size - 1;
    if (indent_depth < 0) indent_depth = 0;
    if (writer_write_indent(w, indent_depth) != 0) {
      w->error = 1;
      return TEXT_JSON_E_WRITE;
    }
  }

  if (writer_write_char(w, ']') != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Pop array from stack
  if (writer_pop_stack(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_STATE;
  }

  // Mark parent as having elements and reset expecting_key if it's an object
  top = writer_top_stack(w);
  if (top) {
    top->has_elements = 1;
    // If parent is an object, we just finished writing a value, so reset to expecting key
    if (top->type == JSON_WRITER_STACK_OBJECT) {
      top->expecting_key = 1;
    }
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_key(
  text_json_writer* w,
  const char* key,
  size_t len
) {
  if (!w || !key) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);
  if (!top || top->type != JSON_WRITER_STACK_OBJECT) {
    return TEXT_JSON_E_STATE; // Not in an object
  }

  if (!top->expecting_key) {
    return TEXT_JSON_E_STATE; // Not expecting a key
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write key
  if (write_escaped_string(&w->sink, key, len, &w->opts) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write colon
  if (w->opts.pretty) {
    if (writer_write_string(w, ": ") != 0) {
      w->error = 1;
      return TEXT_JSON_E_WRITE;
    }
  } else {
    if (writer_write_char(w, ':') != 0) {
      w->error = 1;
      return TEXT_JSON_E_WRITE;
    }
  }

  // Now expecting value
  top->expecting_key = 0;

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_null(text_json_writer* w) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write null
  if (writer_write_string(w, "null") != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_bool(text_json_writer* w, int b) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write boolean
  if (writer_write_string(w, b ? "true" : "false") != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_number_lexeme(
  text_json_writer* w,
  const char* s,
  size_t len
) {
  if (!w || !s) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write number lexeme
  if (writer_write_bytes(w, s, len) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_number_i64(
  text_json_writer* w,
  long long x
) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Format number
  char num_buf[64];
  int len = snprintf(num_buf, sizeof(num_buf), "%lld", x);
  if (len < 0 || (size_t)len >= sizeof(num_buf)) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  if (writer_write_bytes(w, num_buf, (size_t)len) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_number_u64(
  text_json_writer* w,
  unsigned long long x
) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Format number
  char num_buf[64];
  int len = snprintf(num_buf, sizeof(num_buf), "%llu", x);
  if (len < 0 || (size_t)len >= sizeof(num_buf)) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  if (writer_write_bytes(w, num_buf, (size_t)len) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_number_double(
  text_json_writer* w,
  double x
) {
  if (!w) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Check for nonfinite numbers
  if (!isfinite(x)) {
    if (!w->opts.allow_nonfinite_numbers) {
      w->error = 1;
      return TEXT_JSON_E_NONFINITE;
    }
    if (isnan(x)) {
      if (writer_write_string(w, "NaN") != 0) {
        w->error = 1;
        return TEXT_JSON_E_WRITE;
      }
    } else if (isinf(x)) {
      if (x < 0) {
        if (writer_write_string(w, "-Infinity") != 0) {
          w->error = 1;
          return TEXT_JSON_E_WRITE;
        }
      } else {
        if (writer_write_string(w, "Infinity") != 0) {
          w->error = 1;
          return TEXT_JSON_E_WRITE;
        }
      }
    }
  } else {
    // Format finite number
    char num_buf[64];
    int len = snprintf(num_buf, sizeof(num_buf), "%.17g", x);
    if (len < 0 || (size_t)len >= sizeof(num_buf)) {
      w->error = 1;
      return TEXT_JSON_E_WRITE;
    }

    if (writer_write_bytes(w, num_buf, (size_t)len) != 0) {
      w->error = 1;
      return TEXT_JSON_E_WRITE;
    }
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_string(
  text_json_writer* w,
  const char* s,
  size_t len
) {
  if (!w || !s) {
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    return TEXT_JSON_E_STATE;
  }

  json_writer_stack_entry* top = writer_top_stack(w);

  // If in object, must have written key first
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    if (top->expecting_key) {
      w->error = 1;
      return TEXT_JSON_E_STATE; // Must write key before value
    }
  }

  // Write comma if needed
  if (writer_write_comma_if_needed(w) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Write escaped string
  if (write_escaped_string(&w->sink, s, len, &w->opts) != 0) {
    w->error = 1;
    return TEXT_JSON_E_WRITE;
  }

  // Mark current container as having elements
  if (top) {
    top->has_elements = 1;
  }

  // If in object, reset to expecting key
  if (top && top->type == JSON_WRITER_STACK_OBJECT) {
    top->expecting_key = 1;
  }

  return TEXT_JSON_OK;
}

text_json_status text_json_writer_finish(
  text_json_writer* w,
  text_json_error* err
) {
  if (!w) {
    if (err) {
      err->code = TEXT_JSON_E_INVALID;
      err->message = "Writer is NULL";
      err->offset = 0;
      err->line = 0;
      err->col = 0;
    }
    return TEXT_JSON_E_INVALID;
  }

  if (w->error) {
    if (err) {
      err->code = TEXT_JSON_E_STATE;
      err->message = "Writer is in error state";
      err->offset = 0;
      err->line = 0;
      err->col = 0;
    }
    return TEXT_JSON_E_STATE;
  }

  // Check if stack is empty (structure is complete)
  if (w->stack_size != 0) {
    if (err) {
      err->code = TEXT_JSON_E_INCOMPLETE;
      err->message = "Incomplete JSON structure: unclosed containers";
      err->offset = 0;
      err->line = 0;
      err->col = 0;
    }
    return TEXT_JSON_E_INCOMPLETE;
  }

  return TEXT_JSON_OK;
}
