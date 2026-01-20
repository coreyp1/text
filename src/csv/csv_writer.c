/**
 * @file csv_writer.c
 * @brief CSV writer infrastructure implementation
 *
 * This file implements the sink abstraction for writing CSV output
 * to various destinations.
 */

#include <ghoti.io/text/csv/csv_writer.h>
#include <ghoti.io/text/csv/csv_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Internal write callback for growable buffer sink
static text_csv_status buffer_write_fn(void* user, const char* bytes, size_t len) {
  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)user;
  if (!buf || !bytes) {
    return TEXT_CSV_E_INVALID;
  }

  // Check for integer overflow in needed calculation
  if (len > SIZE_MAX - buf->used || buf->used > SIZE_MAX - len - 1) {
    return TEXT_CSV_E_LIMIT; // Overflow
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
        return TEXT_CSV_E_OOM; // Overflow - cannot grow further
      }
      new_size *= 2;
    }

    char* new_data = (char*)realloc(buf->data, new_size);
    if (!new_data) {
      return TEXT_CSV_E_OOM; // Out of memory
    }
    buf->data = new_data;
    buf->size = new_size;
  }

  // Verify bounds before copying (defensive check)
  if (buf->used + len > buf->size - 1) {
    return TEXT_CSV_E_LIMIT; // Should not happen, but be safe
  }

  // Copy data
  memcpy(buf->data + buf->used, bytes, len);
  buf->used += len;
  // Verify bounds before writing null terminator
  if (buf->used < buf->size) {
    buf->data[buf->used] = '\0'; // Null terminate for convenience
  }

  return TEXT_CSV_OK; // Success
}

// Internal write callback for fixed buffer sink
static text_csv_status fixed_buffer_write_fn(void* user, const char* bytes, size_t len) {
  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)user;
  if (!buf || !bytes) {
    return TEXT_CSV_E_INVALID;
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
  bool truncated = false;

  if (to_write > available) {
    to_write = available;
    truncated = true;
    buf->truncated = true;
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
      truncated = true;
      buf->truncated = true;
    }
  }

  // Return error if truncation occurred
  return truncated ? TEXT_CSV_E_WRITE : TEXT_CSV_OK;
}

text_csv_status text_csv_sink_buffer(text_csv_sink* sink) {
  if (!sink) {
    return TEXT_CSV_E_INVALID;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)malloc(sizeof(text_csv_buffer_sink));
  if (!buf) {
    return TEXT_CSV_E_OOM;
  }

  buf->data = NULL;
  buf->size = 0;
  buf->used = 0;

  sink->write = buffer_write_fn;
  sink->user = buf;

  return TEXT_CSV_OK;
}

const char* text_csv_sink_buffer_data(const text_csv_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return NULL;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)sink->user;
  if (!buf) {
    return NULL;
  }

  return buf->data ? buf->data : "";
}

size_t text_csv_sink_buffer_size(const text_csv_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return 0;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

void text_csv_sink_buffer_free(text_csv_sink* sink) {
  if (!sink || sink->write != buffer_write_fn) {
    return;
  }

  text_csv_buffer_sink* buf = (text_csv_buffer_sink*)sink->user;
  if (buf) {
    free(buf->data);
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}

text_csv_status text_csv_sink_fixed_buffer(
  text_csv_sink* sink,
  char* buffer,
  size_t size
) {
  if (!sink || !buffer || size == 0) {
    return TEXT_CSV_E_INVALID;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)malloc(sizeof(text_csv_fixed_buffer_sink));
  if (!buf) {
    return TEXT_CSV_E_OOM;
  }

  buf->data = buffer;
  buf->size = size;
  buf->used = 0;
  buf->truncated = false;

  // Initialize buffer with null terminator
  if (size > 0) {
    buffer[0] = '\0';
  }

  sink->write = fixed_buffer_write_fn;
  sink->user = buf;

  return TEXT_CSV_OK;
}

size_t text_csv_sink_fixed_buffer_used(const text_csv_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return 0;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)sink->user;
  if (!buf) {
    return 0;
  }

  return buf->used;
}

bool text_csv_sink_fixed_buffer_truncated(const text_csv_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return false;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)sink->user;
  if (!buf) {
    return false;
  }

  return buf->truncated;
}

void text_csv_sink_fixed_buffer_free(text_csv_sink* sink) {
  if (!sink || sink->write != fixed_buffer_write_fn) {
    return;
  }

  text_csv_fixed_buffer_sink* buf = (text_csv_fixed_buffer_sink*)sink->user;
  if (buf) {
    free(buf);
    sink->user = NULL;
    sink->write = NULL;
  }
}
