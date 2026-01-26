/**
 * @file
 *
 * JSON Pointer (RFC 6901) implementation.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "json_internal.h"

#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_pointer.h>
// Decode a JSON Pointer reference token
// Decodes escape sequences: ~0 -> ~, ~1 -> /
// The output buffer must be large enough (worst case: same size as input)
static GTEXT_JSON_Status json_pointer_decode_token(const char * input,
    size_t input_len, char * output, size_t output_capacity,
    size_t * output_len) {
  size_t out_pos = 0;
  size_t in_pos = 0;

  while (in_pos < input_len) {
    if (out_pos >= output_capacity) {
      return GTEXT_JSON_E_INVALID;
    }

    if (input[in_pos] == '~') {
      // Check for escape sequence
      if (in_pos + 1 >= input_len) {
        // Incomplete escape sequence
        return GTEXT_JSON_E_INVALID;
      }

      char escape_char = input[in_pos + 1];
      if (escape_char == '0') {
        output[out_pos++] = '~';
        in_pos += 2;
      }
      else if (escape_char == '1') {
        output[out_pos++] = '/';
        in_pos += 2;
      }
      else {
        // Invalid escape sequence
        return GTEXT_JSON_E_INVALID;
      }
    }
    else {
      output[out_pos++] = input[in_pos++];
    }
  }

  *output_len = out_pos;
  return GTEXT_JSON_OK;
}

// Check if a string represents a valid array index
// Validates non-negative integer format, rejects leading zeros (except "0")
static int json_pointer_parse_index(
    const char * str, size_t len, size_t * out_idx) {
  if (len == 0) {
    return 0;
  }

  // Check for leading zero (only "0" is allowed)
  if (len > 1 && str[0] == '0') {
    return 0;
  }

  // Parse as unsigned integer
  size_t idx = 0;
  for (size_t i = 0; i < len; ++i) {
    if (!isdigit((unsigned char)str[i])) {
      return 0;
    }

    size_t digit = (size_t)(str[i] - '0');
    // Check for overflow (SIZE_MAX / 10)
    if (idx > SIZE_MAX / 10) {
      return 0;
    }
    idx *= 10;
    if (idx > SIZE_MAX - digit) {
      return 0;
    }
    idx += digit;
  }

  *out_idx = idx;
  return 1;
}

// Internal function that performs the actual pointer evaluation
// Handles both const and non-const versions
static GTEXT_JSON_Value * json_pointer_evaluate(
    GTEXT_JSON_Value * root, const char * ptr, size_t len) {
  if (!root || !ptr) {
    return NULL;
  }

  // Empty pointer refers to root
  if (len == 0) {
    return root;
  }

  // Pointer must start with '/'
  if (ptr[0] != '/') {
    return NULL;
  }

  // Current value being traversed
  GTEXT_JSON_Value * current = root;

  // Parse reference tokens separated by '/'
  // Note: We must only split on unescaped '/' characters (not '~1')
  // We scan the pointer and build tokens, handling escape sequences
  size_t pos = 1; // Skip leading '/'
  while (pos < len) {
    // Find the end of the current reference token
    // Scan forward, but treat '~1' as an escaped '/' (don't split on it)
    size_t token_start = pos;
    size_t token_end = pos;

    while (token_end < len) {
      if (ptr[token_end] == '~') {
        // Check if this is an escape sequence
        if (token_end + 1 < len) {
          if (ptr[token_end + 1] == '0' || ptr[token_end + 1] == '1') {
            // Valid escape sequence, skip both characters
            // Check for integer overflow before incrementing
            if (token_end > SIZE_MAX - 2) {
              return NULL; // Overflow
            }
            token_end += 2;
            continue;
          }
        }
        // Not a valid escape, treat '~' as regular character
        // Check for integer overflow before incrementing
        if (token_end == SIZE_MAX) {
          return NULL; // Overflow
        }
        token_end++;
      }
      else if (ptr[token_end] == '/') {
        // Found an unescaped '/', this is the token separator
        break;
      }
      else {
        // Check for integer overflow before incrementing
        if (token_end == SIZE_MAX) {
          return NULL; // Overflow
        }
        token_end++;
      }
    }

    size_t token_len = token_end - token_start;

    // Verify bounds: ensure token_start + token_len doesn't exceed len
    // This ensures ptr + token_start is within valid range
    // Note: token_start < len from loop condition, but check defensively
    if (token_start >= len) {
      return NULL; // Invalid start position
    }
    // Check for underflow in subtraction (defensive)
    if (token_len > len || token_start > len - token_len) {
      return NULL; // Invalid bounds
    }

    // Empty token (e.g., "//" or trailing "/")
    if (token_len == 0) {
      // Empty reference token is valid (refers to empty string key)
      // Continue to next token
      pos = token_end;
      if (pos < len && ptr[pos] == '/') {
        pos++; // Skip '/'
      }
      else {
        break;
      }
      continue;
    }

    // Decode the reference token (handle escape sequences)
    // Allocate a buffer large enough (worst case: same size as input)
    // Check for integer overflow in allocation size
    if (token_len > SIZE_MAX - 1) {
      return NULL; // Token too large
    }
    char * decoded = (char *)malloc(token_len + 1);
    if (!decoded) {
      return NULL;
    }

    size_t decoded_len;
    GTEXT_JSON_Status status = json_pointer_decode_token(
        ptr + token_start, token_len, decoded, token_len + 1, &decoded_len);

    if (status != GTEXT_JSON_OK) {
      free(decoded);
      return NULL;
    }

    // Ensure null termination (defensive, though text_json_object_get uses
    // length)
    decoded[decoded_len] = '\0';

    // Determine if this is an array index or object key
    size_t array_idx;
    int is_array_index =
        json_pointer_parse_index(decoded, decoded_len, &array_idx);

    if (is_array_index) {
      // Try as array index
      if (current->type != GTEXT_JSON_ARRAY) {
        free(decoded);
        return NULL;
      }

      if (array_idx >= current->as.array.count) {
        free(decoded);
        return NULL;
      }

      current = current->as.array.elems[array_idx];
    }
    else {
      // Try as object key
      if (current->type != GTEXT_JSON_OBJECT) {
        free(decoded);
        return NULL;
      }

      // Search for the key in the object
      const GTEXT_JSON_Value * found = gtext_json_object_get(
          (const GTEXT_JSON_Value *)current, decoded, decoded_len);

      if (!found) {
        free(decoded);
        return NULL;
      }

      // Cast away const for mutable access if needed
      // This is safe because we're traversing a DOM tree that we own
      current = (GTEXT_JSON_Value *)found;
    }

    free(decoded);

    // Move to next token
    pos = token_end;
    if (pos < len && ptr[pos] == '/') {
      pos++; // Skip '/'
    }
    else {
      // End of pointer string, we're done
      break;
    }
  }

  return current;
}

const GTEXT_JSON_Value * gtext_json_pointer_get(
    const GTEXT_JSON_Value * root, const char * ptr, size_t len) {
  // Cast away const for internal evaluation
  // This is safe because we're only reading
  return json_pointer_evaluate((GTEXT_JSON_Value *)root, ptr, len);
}

GTEXT_JSON_Value * gtext_json_pointer_get_mut(
    GTEXT_JSON_Value * root, const char * ptr, size_t len) {
  return json_pointer_evaluate(root, ptr, len);
}
