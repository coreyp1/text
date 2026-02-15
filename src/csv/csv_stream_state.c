/**
 * @file
 *
 * State machine handlers for CSV streaming parser.
 *
 * Handles all state transitions and character processing in the CSV parser
 * state machine.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <string.h>

#include "csv_stream_internal.h"

// Advance position tracking
GTEXT_CSV_Status csv_stream_advance_position(
    GTEXT_CSV_Stream * stream, size_t * offset, size_t bytes) {
  // Check all overflow conditions upfront before performing any operations
  if (*offset > SIZE_MAX - bytes) {
    return csv_stream_set_error(stream, GTEXT_CSV_E_LIMIT, "Offset overflow");
  }
  if (stream->pos.offset > SIZE_MAX - bytes) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_LIMIT, "Position offset overflow");
  }
  if (stream->pos.column > INT_MAX - (int)bytes) {
    return csv_stream_set_error(stream, GTEXT_CSV_E_LIMIT, "Column overflow");
  }
  if (stream->total_bytes_consumed > SIZE_MAX - bytes) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_LIMIT, "Total bytes consumed overflow");
  }

  // All checks passed, perform all increments
  *offset += bytes;
  stream->pos.offset += bytes;
  stream->pos.column += (int)bytes;
  stream->total_bytes_consumed += bytes;

  return GTEXT_CSV_OK;
}

// Handle newline detection and position update
// Returns GTEXT_CSV_OK if newline detected and processed, GTEXT_CSV_E_LIMIT on
// overflow, or GTEXT_CSV_OK with CSV_NEWLINE_NONE if no newline
GTEXT_CSV_Status csv_stream_handle_newline(GTEXT_CSV_Stream * stream,
    const char * input, size_t input_len, size_t * offset, size_t byte_pos,
    csv_newline_type * nl_out) {
  csv_position pos_before = stream->pos;
  pos_before.offset = byte_pos;
  GTEXT_CSV_Status detect_error = GTEXT_CSV_OK;
  csv_newline_type nl = csv_detect_newline(
      input, input_len, &pos_before, &stream->opts.dialect, &detect_error);

  if (detect_error != GTEXT_CSV_OK) {
    return csv_stream_set_error(
        stream, detect_error, "Overflow in newline detection");
  }

  if (nl == CSV_NEWLINE_NONE) {
    *nl_out = CSV_NEWLINE_NONE;
    return GTEXT_CSV_OK;
  }

  size_t newline_bytes = (nl == CSV_NEWLINE_CRLF ? 2 : 1);

  // Check all overflow conditions upfront before performing any operations
  size_t offset_delta = pos_before.offset - byte_pos;
  if (stream->pos.offset > SIZE_MAX - offset_delta) {
    return csv_stream_set_error(stream, GTEXT_CSV_E_LIMIT,
        "Position offset overflow in newline handling");
  }
  if (stream->total_bytes_consumed > SIZE_MAX - newline_bytes) {
    return csv_stream_set_error(stream, GTEXT_CSV_E_LIMIT,
        "Total bytes consumed overflow in newline handling");
  }

  // All checks passed, perform all updates
  stream->pos.offset = pos_before.offset;
  stream->pos.line = pos_before.line;
  stream->pos.column = pos_before.column;
  stream->total_bytes_consumed += newline_bytes;
  *offset = pos_before.offset;
  *nl_out = nl;

  return GTEXT_CSV_OK;
}
bool csv_stream_is_comment_start(GTEXT_CSV_Stream * stream, const char * input,
    size_t input_len, size_t offset) {
  if (!stream->opts.dialect.allow_comments || stream->comment_prefix_len == 0) {
    return false;
  }

  if (stream->field_count > 0 || stream->in_record) {
    return false;
  }

  if (offset + stream->comment_prefix_len > input_len) {
    return false;
  }

  // Early exit if first character doesn't match
  if (input[offset] != stream->opts.dialect.comment_prefix[0]) {
    return false;
  }

  return memcmp(input + offset, stream->opts.dialect.comment_prefix,
             stream->comment_prefix_len) == 0;
}
// Process START_OF_RECORD state
GTEXT_CSV_Status csv_stream_process_start_of_record(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  (void)c; // Not used in this state, but kept for consistent signature
  // Check for comment
  if (csv_stream_is_comment_start(
          stream, process_input, process_len, byte_pos)) {
    stream->state = CSV_STREAM_STATE_COMMENT;
    stream->in_comment = true;
    return csv_stream_advance_position(stream, offset, 1);
  }

  // Check for newline at start of record - skip trailing empty records
  csv_newline_type nl;
  GTEXT_CSV_Status status = csv_stream_handle_newline(
      stream, process_input, process_len, offset, byte_pos, &nl);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  if (nl != CSV_NEWLINE_NONE) {
    // Skip the newline without creating a record
    // Position already updated by csv_stream_handle_newline
    return GTEXT_CSV_OK;
  }

  // Emit RECORD_BEGIN
  status = csv_stream_emit_event(stream, GTEXT_CSV_EVENT_RECORD_BEGIN, NULL, 0);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  stream->state = CSV_STREAM_STATE_START_OF_FIELD;
  stream->in_record = true;
  stream->current_record_bytes = 0;
  stream->field_count = 0;
  // Fall through to START_OF_FIELD
  return GTEXT_CSV_OK;
}

// Process START_OF_FIELD state
GTEXT_CSV_Status csv_stream_process_start_of_field(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  if (stream->field_count >= stream->max_cols) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_TOO_MANY_COLS, "Too many columns in record");
  }

  // Clear any previous field buffering
  csv_field_buffer_clear(&stream->field);
  stream->just_processed_doubled_quote = false;
  stream->quote_in_quoted_at_chunk_boundary = false;
  // Validate input parameters (consolidated safety checks)
  GTEXT_CSV_Status validate_status = csv_stream_validate_field_input(
      stream, process_input, process_len, byte_pos);
  if (validate_status != GTEXT_CSV_OK) {
    return validate_status;
  }
  csv_field_buffer_set_from_input(
      &stream->field, process_input + byte_pos, 0, false, byte_pos);

  if (c == stream->opts.dialect.quote) {
    stream->state = CSV_STREAM_STATE_QUOTED_FIELD;
    stream->field.is_quoted = true;
    csv_field_buffer_set_from_input(
        &stream->field, process_input + byte_pos + 1, 0, true, byte_pos + 1);
    GTEXT_CSV_Status status = csv_stream_advance_position(stream, offset, 1);
    if (status != GTEXT_CSV_OK) {
      return status;
    }

    // If we're at the end of the chunk, initialize buffer for the next chunk
    if (*offset >= process_len) {
      // field.data is past the chunk (e.g., quoted field started but no content
      // yet) Initialize empty buffer so we can append to it in the next chunk
      if (!stream->field.is_buffered) {
        status = csv_field_buffer_grow(
            &stream->field, CSV_FIELD_BUFFER_INITIAL_SIZE);
        if (status != GTEXT_CSV_OK) {
          return status;
        }
        stream->field.buffer_used = 0;
        stream->field.is_buffered = true;
        stream->field.data = stream->field.buffer;
        stream->field.length = 0;
      }
      return GTEXT_CSV_OK; // Wait for next chunk
    }
    return GTEXT_CSV_OK;
  }

  if (c == stream->opts.dialect.delimiter) {
    // Empty field
    GTEXT_CSV_Status status =
        csv_stream_emit_event(stream, GTEXT_CSV_EVENT_FIELD, "", 0);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    if (stream->field_count >= SIZE_MAX) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Field count overflow");
    }
    stream->field_count++;
    stream->state = CSV_STREAM_STATE_START_OF_FIELD;
    return csv_stream_advance_position(stream, offset, 1);
  }

  // Check for newline
  csv_newline_type nl;
  GTEXT_CSV_Status status = csv_stream_handle_newline(
      stream, process_input, process_len, offset, byte_pos, &nl);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  if (nl != CSV_NEWLINE_NONE) {
    // Empty field, end of record
    status = csv_stream_emit_event(stream, GTEXT_CSV_EVENT_FIELD, "", 0);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    status = csv_stream_emit_event(stream, GTEXT_CSV_EVENT_RECORD_END, NULL, 0);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    stream->field_count = 0;
    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
    stream->in_record = false;
    if (stream->row_count >= SIZE_MAX) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Row count overflow");
    }
    stream->row_count++;
    // Position already updated by csv_stream_handle_newline
    return GTEXT_CSV_OK;
  }

  // Start unquoted field
  stream->state = CSV_STREAM_STATE_UNQUOTED_FIELD;
  // Validate input parameters (consolidated safety checks)
  GTEXT_CSV_Status validate_status2 = csv_stream_validate_field_input(
      stream, process_input, process_len, byte_pos);
  if (validate_status2 != GTEXT_CSV_OK) {
    return validate_status2;
  }
  // Set field.data to current position - we'll only buffer when necessary:
  // 1. When field spans chunk boundary (handled in UNQUOTED_FIELD state)
  // 2. When field completes and in-situ mode can't be used (handled when
  // emitting)
  // 3. When in-situ mode is disabled
  csv_field_buffer_set_from_input(
      &stream->field, process_input + byte_pos, 1, false, byte_pos);
  return csv_stream_advance_position(stream, offset, 1);
}

// Helper: Handle delimiter in unquoted field
GTEXT_CSV_Status csv_stream_unquoted_handle_delimiter(
    GTEXT_CSV_Stream * stream, size_t * offset) {
  return csv_stream_complete_field_at_delimiter(stream, offset);
}

// Helper: Handle newline in unquoted field
GTEXT_CSV_Status csv_stream_unquoted_handle_newline(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos) {
  csv_newline_type nl;
  GTEXT_CSV_Status status = csv_stream_handle_newline(
      stream, process_input, process_len, offset, byte_pos, &nl);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  if (nl == CSV_NEWLINE_NONE) {
    return GTEXT_CSV_OK;
  }

  // Field complete, end of record
  // Buffer field if needed
  status = csv_stream_buffer_unquoted_field_if_needed(stream);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Position already updated by csv_stream_handle_newline
  status = csv_stream_emit_field(stream, true);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  csv_stream_clear_field_state(stream);
  stream->field_count = 0;
  stream->state = CSV_STREAM_STATE_START_OF_RECORD;
  stream->in_record = false;
  if (stream->row_count >= SIZE_MAX) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_LIMIT, "Row count overflow");
  }
  stream->row_count++;
  // Position already updated by csv_stream_handle_newline
  return GTEXT_CSV_OK;
}

// Helper: Validate character in unquoted field
GTEXT_CSV_Status csv_stream_unquoted_validate_char(
    GTEXT_CSV_Stream * stream, char c) {
  if (c == stream->opts.dialect.quote &&
      !stream->opts.dialect.allow_unquoted_quotes) {
    return csv_stream_set_error(stream, GTEXT_CSV_E_UNEXPECTED_QUOTE,
        "Unexpected quote in unquoted field");
  }

  if ((c == '\n' || c == '\r') &&
      !stream->opts.dialect.allow_unquoted_newlines) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_INVALID, "Newline in unquoted field");
  }

  return GTEXT_CSV_OK;
}

// Helper: Handle special character found during bulk processing
GTEXT_CSV_Status csv_stream_unquoted_handle_special_char(
    GTEXT_CSV_Stream * stream, const char * process_input, size_t process_len,
    size_t * offset, size_t special_pos, char special_char) {
  // Update offset to point to the special character
  *offset = special_pos;

  // Handle the special character based on what it is
  if (special_char == stream->opts.dialect.delimiter) {
    // Field complete
    return csv_stream_complete_field_at_delimiter(stream, offset);
  }

  if (special_char == '\n' || special_char == '\r') {
    // Check for newline (may have been detected during scan)
    csv_newline_type nl;
    GTEXT_CSV_Status status = csv_stream_handle_newline(
        stream, process_input, process_len, offset, special_pos, &nl);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    if (nl != CSV_NEWLINE_NONE) {
      // Field complete, end of record
      GTEXT_CSV_Status buffer_status =
          csv_stream_buffer_unquoted_field_if_needed(stream);
      if (buffer_status != GTEXT_CSV_OK) {
        return buffer_status;
      }
      // Position already updated by csv_stream_handle_newline
      buffer_status = csv_stream_emit_field(stream, true);
      if (buffer_status != GTEXT_CSV_OK) {
        return buffer_status;
      }
      csv_stream_clear_field_state(stream);
      stream->field_count = 0;
      stream->state = CSV_STREAM_STATE_START_OF_RECORD;
      stream->in_record = false;
      if (stream->row_count >= SIZE_MAX) {
        return csv_stream_set_error(
            stream, GTEXT_CSV_E_LIMIT, "Row count overflow");
      }
      stream->row_count++;
      // Position already updated by csv_stream_handle_newline
      return GTEXT_CSV_OK;
    }

    // If newlines are allowed, continue processing
    if (!stream->opts.dialect.allow_unquoted_newlines) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_INVALID, "Newline in unquoted field");
    }

    // Process the newline character as part of the field
    if (stream->field.is_buffered) {
      GTEXT_CSV_Status append_status =
          csv_stream_append_to_field_buffer(stream, &special_char, 1);
      if (append_status != GTEXT_CSV_OK) {
        return append_status;
      }
      stream->field.length = stream->field.buffer_used;
    }
    else {
      if (stream->field.length >= SIZE_MAX) {
        return csv_stream_set_error(
            stream, GTEXT_CSV_E_LIMIT, "Field length overflow");
      }
      stream->field.length++;
    }
    return csv_stream_advance_position(stream, offset, 1);
  }

  if (special_char == stream->opts.dialect.quote) {
    // Quote not allowed in unquoted field
    return csv_stream_set_error(stream, GTEXT_CSV_E_UNEXPECTED_QUOTE,
        "Unexpected quote in unquoted field");
  }

  return GTEXT_CSV_OK;
}

// Helper: Process bulk content in unquoted field
GTEXT_CSV_Status csv_stream_unquoted_process_bulk(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos) {
  bool found_special;
  char special_char;
  size_t special_pos;

  size_t safe_chars =
      csv_stream_scan_unquoted_field_ahead(stream, process_input, process_len,
          byte_pos, &found_special, &special_char, &special_pos);

  // Respect field length limit
  size_t remaining_capacity = stream->max_field_bytes - stream->field.length;
  if (safe_chars > remaining_capacity) {
    safe_chars = remaining_capacity;
    found_special = false; // We'll hit the limit instead
  }

  // Process safe characters in bulk
  if (safe_chars > 0) {
    if (stream->field.is_buffered) {
      // Append bulk data to buffer
      GTEXT_CSV_Status status = csv_stream_append_to_field_buffer(
          stream, process_input + byte_pos, safe_chars);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      stream->field.length = stream->field.buffer_used;
    }
    else {
      // Just update length - field.data already points to the data
      stream->field.length += safe_chars;
    }
    GTEXT_CSV_Status status =
        csv_stream_advance_position(stream, offset, safe_chars);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
  }

  // If we found a special character, handle it
  if (found_special) {
    return csv_stream_unquoted_handle_special_char(
        stream, process_input, process_len, offset, special_pos, special_char);
  }

  return GTEXT_CSV_OK;
}

// Process UNQUOTED_FIELD state
GTEXT_CSV_Status csv_stream_process_unquoted_field(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  // Check field length limit
  if (stream->field.length >= stream->max_field_bytes) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_LIMIT, "Maximum field bytes exceeded");
  }

  // Handle delimiter
  if (c == stream->opts.dialect.delimiter) {
    return csv_stream_unquoted_handle_delimiter(stream, offset);
  }

  // Handle newline
  csv_newline_type nl;
  GTEXT_CSV_Status status = csv_stream_handle_newline(
      stream, process_input, process_len, offset, byte_pos, &nl);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  if (nl != CSV_NEWLINE_NONE) {
    return csv_stream_unquoted_handle_newline(
        stream, process_input, process_len, offset, byte_pos);
  }

  // Validate character
  status = csv_stream_unquoted_validate_char(stream, c);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Process bulk content
  status = csv_stream_unquoted_process_bulk(
      stream, process_input, process_len, offset, byte_pos);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // Handle chunk boundary
  if (*offset >= process_len &&
      stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD) {
    return csv_stream_handle_chunk_boundary(stream);
  }

  return GTEXT_CSV_OK;
}

// Process QUOTED_FIELD state
GTEXT_CSV_Status csv_stream_process_quoted_field(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  // Ensure field.data and field.length are correct if buffered
  if (stream->field.is_buffered) {
    stream->field.data = stream->field.buffer;
    stream->field.length = stream->field.buffer_used;
  }

  if (stream->field.length >= stream->max_field_bytes) {
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_LIMIT, "Maximum field bytes exceeded");
  }

  // If we just processed a doubled quote and see a delimiter, end the field
  // This handles the case: "text"",field2 where the doubled quote is followed
  // by delimiter
  if (stream->just_processed_doubled_quote &&
      c == stream->opts.dialect.delimiter) {
    // End of quoted field - emit field
    // Ensure field is buffered if needed
    GTEXT_CSV_Status buffer_status = csv_stream_ensure_field_buffered(
        stream, process_input, process_len, *offset);
    if (buffer_status != GTEXT_CSV_OK) {
      return buffer_status;
    }

    return csv_stream_complete_field_at_delimiter(stream, offset);
  }

  // If we just processed a doubled quote and see a newline, end the field and
  // record
  if (stream->just_processed_doubled_quote && (c == '\n' || c == '\r')) {
    csv_newline_type nl;
    GTEXT_CSV_Status newline_status = csv_stream_handle_newline(
        stream, process_input, process_len, offset, byte_pos, &nl);
    if (newline_status != GTEXT_CSV_OK) {
      return newline_status;
    }
    if (nl == CSV_NEWLINE_NONE) {
      // Not a complete newline sequence, continue processing
      // Fall through to regular character handling
    }
    else {
      // End of quoted field, end of record
      // Ensure field is buffered if needed
      GTEXT_CSV_Status ensure_status = csv_stream_ensure_field_buffered(
          stream, process_input, process_len, *offset);
      if (ensure_status != GTEXT_CSV_OK) {
        return ensure_status;
      }

      // Position already updated by csv_stream_handle_newline
      ensure_status = csv_stream_emit_field(stream, true);
      if (ensure_status != GTEXT_CSV_OK) {
        return ensure_status;
      }
      csv_stream_clear_field_state(stream);
      stream->field_count = 0;
      stream->state = CSV_STREAM_STATE_START_OF_RECORD;
      stream->in_record = false;
      if (stream->row_count >= SIZE_MAX) {
        return csv_stream_set_error(
            stream, GTEXT_CSV_E_LIMIT, "Row count overflow");
      }
      stream->row_count++;
      // Position already updated by csv_stream_handle_newline
      return GTEXT_CSV_OK;
    }
  }

  if (stream->opts.dialect.escape == GTEXT_CSV_ESCAPE_BACKSLASH && c == '\\') {
    stream->state = CSV_STREAM_STATE_ESCAPE_IN_QUOTED;
    GTEXT_CSV_Status advance_status =
        csv_stream_advance_position(stream, offset, 1);
    if (advance_status != GTEXT_CSV_OK) {
      return advance_status;
    }
    return GTEXT_CSV_OK;
  }

  if (c == stream->opts.dialect.quote) {
    // Don't append the quote yet - we need to check if it's doubled or closing
    // Transition to QUOTE_IN_QUOTED to check next character
    stream->state = CSV_STREAM_STATE_QUOTE_IN_QUOTED;
    GTEXT_CSV_Status advance_status =
        csv_stream_advance_position(stream, offset, 1);
    if (advance_status != GTEXT_CSV_OK) {
      return advance_status;
    }

    // If we're at the end of the chunk, buffer the field data (up to but not
    // including the quote) The quote will be handled in the next chunk when we
    // see what follows it
    if (*offset >= process_len) {
      // Buffer field data from field_start to offset-1 (before the quote)
      // The quote is at position offset-1 after we advanced offset
      // We need to buffer everything up to (but not including) the quote
      size_t quote_pos = *offset - 1;
      GTEXT_CSV_Status buffer_status = csv_stream_ensure_field_buffered(
          stream, process_input, process_len, quote_pos);
      if (buffer_status != GTEXT_CSV_OK) {
        return buffer_status;
      }
      // Mark that we transitioned to QUOTE_IN_QUOTED at chunk boundary
      stream->quote_in_quoted_at_chunk_boundary = true;
      return GTEXT_CSV_OK; // Wait for next chunk
    }

    // Not at chunk boundary - clear the flag
    stream->quote_in_quoted_at_chunk_boundary = false;
    return GTEXT_CSV_OK;
  }

  // CRITICAL: In a quoted field, a delimiter or newline can only appear after a
  // closing quote. If we see a delimiter or newline directly in QUOTED_FIELD
  // state, it means we're missing a closing quote. However, if we just
  // processed a doubled quote and are at a chunk boundary, we might be in
  // QUOTED_FIELD state when we should actually be looking for a closing quote.
  // This case is handled by checking if we're at the start of a new chunk with
  // a buffered field. For now, treat delimiter/newline as field content
  // (they're valid inside quoted fields). Regular character in quoted field -
  // accumulate it
  if (stream->field.is_buffered) {
    // Append to field buffer
    GTEXT_CSV_Status append_status =
        csv_stream_append_to_field_buffer(stream, &c, 1);
    if (append_status != GTEXT_CSV_OK) {
      return append_status;
    }
    stream->field.data = stream->field.buffer;
    stream->field.length = stream->field.buffer_used;
  }
  else {
    // Track in current chunk - set field.data if not set
    if (!stream->field.data) {
      stream->field.data = process_input + *offset;
      stream->field.start_offset = *offset;
      stream->field.length = 0;
    }
    if (stream->field.length >= SIZE_MAX) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Field length overflow");
    }
    stream->field.length++;
  }
  GTEXT_CSV_Status advance_status =
      csv_stream_advance_position(stream, offset, 1);
  if (advance_status != GTEXT_CSV_OK) {
    return advance_status;
  }

  // If we're at the end of the chunk, buffer the field data
  if (*offset >= process_len) {
    GTEXT_CSV_Status ensure_status = csv_stream_ensure_field_buffered(
        stream, process_input, process_len, *offset);
    if (ensure_status != GTEXT_CSV_OK) {
      return ensure_status;
    }
    return GTEXT_CSV_OK; // Wait for next chunk
  }
  return GTEXT_CSV_OK;
}

// Process QUOTE_IN_QUOTED state
GTEXT_CSV_Status csv_stream_process_quote_in_quoted(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  // We saw a quote - check if it's doubled (next char is quote) or closing
  // (next char is delimiter/newline)

  if (stream->opts.dialect.escape == GTEXT_CSV_ESCAPE_DOUBLED_QUOTE &&
      c == stream->opts.dialect.quote) {
    // Doubled quote escape - append both quotes to field data
    // Ensure field is buffered
    // Buffer up to offset - 1 (before the second quote)
    GTEXT_CSV_Status status = csv_stream_ensure_field_buffered(
        stream, process_input, process_len, *offset - 1);
    if (status != GTEXT_CSV_OK) {
      return status;
    }

    // Append both quotes (the one that put us in QUOTE_IN_QUOTED + this one)
    char quote_char = stream->opts.dialect.quote;
    status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    stream->field.data = stream->field.buffer;
    stream->field.length = stream->field.buffer_used;
    // Mark that field needs unescaping (doubled quotes need to be converted to
    // single quotes)
    stream->field.needs_unescape = true;

    // Doubled quote processed - return to QUOTED_FIELD state to continue field
    stream->state = CSV_STREAM_STATE_QUOTED_FIELD;
    stream->just_processed_doubled_quote =
        true; // Mark that we just processed a doubled quote
    stream->quote_in_quoted_at_chunk_boundary = false; // Clear flag
    // Field is already buffered, so we're good (whether at chunk boundary or
    // not)
    return csv_stream_advance_position(stream, offset, 1);
  }

  if (c == stream->opts.dialect.delimiter) {
    // End of quoted field - emit field
    // Save quote position (quote is at byte_pos - 1 since we advanced past it
    // when entering QUOTE_IN_QUOTED)
    size_t quote_pos = byte_pos - 1;

    // Special case: if we transitioned to QUOTE_IN_QUOTED at chunk boundary
    // with empty field, and we're using doubled quote escape, treat "" as
    // doubled quote (literal quote)
    if (stream->quote_in_quoted_at_chunk_boundary &&
        stream->opts.dialect.escape == GTEXT_CSV_ESCAPE_DOUBLED_QUOTE) {
      bool is_empty = stream->field.is_buffered
          ? (stream->field.buffer_used == 0)
          : (stream->field.length == 0);
      if (is_empty) {
        // Treat as doubled quote - ensure buffer is ready
        if (!stream->field.is_buffered) {
          GTEXT_CSV_Status status = csv_field_buffer_grow(&stream->field, 2);
          if (status != GTEXT_CSV_OK) {
            return status;
          }
          stream->field.buffer_used = 0;
          stream->field.is_buffered = true;
          stream->field.data = stream->field.buffer;
          stream->field.length = 0;
        }
        // Append both quotes
        char quote_char = stream->opts.dialect.quote;
        GTEXT_CSV_Status status =
            csv_stream_append_to_field_buffer(stream, &quote_char, 1);
        if (status != GTEXT_CSV_OK) {
          return status;
        }
        status = csv_stream_append_to_field_buffer(stream, &quote_char, 1);
        if (status != GTEXT_CSV_OK) {
          return status;
        }
        stream->field.needs_unescape = true;
      }
    }
    // Clear the flag
    stream->quote_in_quoted_at_chunk_boundary = false;

    // Ensure field is buffered if needed
    // Buffer up to (but not including) the quote position
    GTEXT_CSV_Status status = csv_stream_ensure_field_buffered(
        stream, process_input, process_len, quote_pos);
    if (status != GTEXT_CSV_OK) {
      return status;
    }

    return csv_stream_complete_field_at_delimiter(stream, offset);
  }

  // Check for newline
  if (c == '\n' || c == '\r') {
    // Save quote position before processing newline (quote is at byte_pos - 1
    // since we advanced past it)
    size_t quote_pos = byte_pos - 1;
    csv_newline_type nl;
    GTEXT_CSV_Status status = csv_stream_handle_newline(
        stream, process_input, process_len, offset, byte_pos, &nl);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    if (nl == CSV_NEWLINE_NONE) {
      return GTEXT_CSV_OK;
    }

    // End of quoted field, end of record
    // Ensure field is buffered if needed
    // Buffer up to (but not including) the quote position
    status = csv_stream_ensure_field_buffered(
        stream, process_input, process_len, quote_pos);
    if (status != GTEXT_CSV_OK) {
      return status;
    }

    // Position already updated by csv_stream_handle_newline
    status = csv_stream_emit_field(stream, true);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    csv_stream_clear_field_state(stream);
    stream->field_count = 0;
    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
    stream->in_record = false;
    if (stream->row_count >= SIZE_MAX) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Row count overflow");
    }
    stream->row_count++;
    // Position already updated by csv_stream_handle_newline
    return GTEXT_CSV_OK;
  }

  // Regular character after quote - invalid quote usage
  // In a quoted field, a quote must be followed by:
  // 1. Another quote (doubled quote escape)
  // 2. A delimiter (end of field)
  // 3. A newline (end of field and record)
  // Anything else is an error
  return csv_stream_set_error(stream, GTEXT_CSV_E_INVALID,
      "Quote in quoted field must be followed by quote, delimiter, or newline");
}

// Process ESCAPE_IN_QUOTED state
GTEXT_CSV_Status csv_stream_process_escape_in_quoted(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  (void)byte_pos; // Not used in this state, but kept for consistent signature
  switch (c) {
  case 'n':
  case 'r':
  case 't':
  case '\\':
  case '"':
    // Valid escape sequence
    break;
  default:
    return csv_stream_set_error(
        stream, GTEXT_CSV_E_INVALID_ESCAPE, "Invalid escape sequence");
  }

  stream->field.needs_unescape = true;
  stream->state = CSV_STREAM_STATE_QUOTED_FIELD;

  // Append escaped character to field buffer
  if (stream->field.is_buffered) {
    char escaped_char = c;
    if (c == 'n')
      escaped_char = '\n';
    else if (c == 'r')
      escaped_char = '\r';
    else if (c == 't')
      escaped_char = '\t';
    // '\\' and '"' stay as-is
    GTEXT_CSV_Status status =
        csv_stream_append_to_field_buffer(stream, &escaped_char, 1);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    stream->field.data = stream->field.buffer;
    stream->field.length = stream->field.buffer_used;
  }
  else {
    // If not buffered, we need to buffer when we see escape sequences
    // since we need to transform them
    if (!stream->field.data) {
      csv_field_buffer_set_from_input(
          &stream->field, process_input + *offset - 1, 0, true, *offset - 1);
    }
    stream->field.length += 2; // Backslash + escaped char
  }

  GTEXT_CSV_Status status = csv_stream_advance_position(stream, offset, 1);
  if (status != GTEXT_CSV_OK) {
    return status;
  }

  // If at end of chunk, buffer field data
  if (*offset >= process_len) {
    GTEXT_CSV_Status ensure_status = csv_stream_ensure_field_buffered(
        stream, process_input, process_len, *offset);
    if (ensure_status != GTEXT_CSV_OK) {
      return ensure_status;
    }
    return GTEXT_CSV_OK; // Wait for next chunk
  }
  return GTEXT_CSV_OK;
}

// Process COMMENT state
GTEXT_CSV_Status csv_stream_process_comment(GTEXT_CSV_Stream * stream,
    const char * process_input, size_t process_len, size_t * offset,
    size_t byte_pos, char c) {
  (void)c; // Not used in this state, but kept for consistent signature
  csv_newline_type nl;
  GTEXT_CSV_Status status = csv_stream_handle_newline(
      stream, process_input, process_len, offset, byte_pos, &nl);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  if (nl != CSV_NEWLINE_NONE) {
    stream->state = CSV_STREAM_STATE_START_OF_RECORD;
    stream->in_comment = false;
    if (stream->row_count >= SIZE_MAX) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Row count overflow");
    }
    stream->row_count++;
    // Position already updated by csv_stream_handle_newline
    return GTEXT_CSV_OK;
  }
  GTEXT_CSV_Status advance_status =
      csv_stream_advance_position(stream, offset, 1);
  if (advance_status != GTEXT_CSV_OK) {
    return advance_status;
  }
  return GTEXT_CSV_OK;
}

// Process a chunk of input data
// Algorithm:
// 1. Always process the chunk directly (no input buffering for combining
// chunks)
// 2. Process character-by-character according to the state machine
// 3. When a field is complete, emit it (copying to field_buffer if needed for
// unescaping)
// 4. If end of chunk is reached while in the middle of a field:
//    - Remember the current state
//    - Copy field data from field_start to end of chunk into field_buffer
//    - Set field_is_buffered = true
//    - Return to wait for next chunk
// 5. When next chunk arrives:
//    - Start processing from the saved state
//    - Continue accumulating into field_buffer (if field spans chunks)
//    - When field ends, emit the complete field from field_buffer
//    - Clear field_buffer and continue processing the rest of the chunk
//
// Key insight: We only buffer FIELD DATA when it spans chunks, not the input
// chunks themselves. This avoids the complexity of reprocessing and state
// conflicts.
GTEXT_CSV_Status csv_stream_process_chunk(
    GTEXT_CSV_Stream * stream, const char * input, size_t input_len) {
  // Always process the chunk directly - no input buffering
  const char * process_input = input;
  size_t process_len = input_len;
  size_t offset = 0;

  while (offset < process_len && stream->state != CSV_STREAM_STATE_END) {
    char c = process_input[offset];
    size_t byte_pos = offset;

    // Check limits
    if (stream->total_bytes_consumed >= stream->max_total_bytes) {
      return csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Maximum total bytes exceeded");
    }

    if (stream->in_record) {
      if (stream->current_record_bytes >= SIZE_MAX) {
        return csv_stream_set_error(
            stream, GTEXT_CSV_E_LIMIT, "Current record bytes overflow");
      }
      stream->current_record_bytes++;
      if (stream->current_record_bytes > stream->max_record_bytes) {
        return csv_stream_set_error(
            stream, GTEXT_CSV_E_LIMIT, "Maximum record bytes exceeded");
      }
    }

    // Call appropriate state handler
    GTEXT_CSV_Status status;
    switch (stream->state) {
    case CSV_STREAM_STATE_START_OF_RECORD:
      status = csv_stream_process_start_of_record(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // If state changed to START_OF_FIELD, fall through to process it
      if (stream->state != CSV_STREAM_STATE_START_OF_FIELD) {
        continue;
      }
      // Fall through
    case CSV_STREAM_STATE_START_OF_FIELD: {
      status = csv_stream_process_start_of_field(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // If at end of chunk, wait for next chunk
      if (offset >= process_len) {
        return GTEXT_CSV_OK;
      }
      continue;
    }

    case CSV_STREAM_STATE_UNQUOTED_FIELD:
      status = csv_stream_process_unquoted_field(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // If at end of chunk, wait for next chunk
      if (offset >= process_len) {
        return GTEXT_CSV_OK;
      }
      continue;

    case CSV_STREAM_STATE_QUOTED_FIELD:
      status = csv_stream_process_quoted_field(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // If at end of chunk, wait for next chunk
      if (offset >= process_len) {
        return GTEXT_CSV_OK;
      }
      continue;

    case CSV_STREAM_STATE_QUOTE_IN_QUOTED:
      status = csv_stream_process_quote_in_quoted(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // If at end of chunk, wait for next chunk
      if (offset >= process_len) {
        return GTEXT_CSV_OK;
      }
      continue;

    case CSV_STREAM_STATE_ESCAPE_IN_QUOTED:
      status = csv_stream_process_escape_in_quoted(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // If at end of chunk, wait for next chunk
      if (offset >= process_len) {
        return GTEXT_CSV_OK;
      }
      continue;

    case CSV_STREAM_STATE_COMMENT:
      status = csv_stream_process_comment(
          stream, process_input, process_len, &offset, byte_pos, c);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      continue;

    case CSV_STREAM_STATE_END:
      return GTEXT_CSV_OK;
    }
  }

  // Check if we exited the loop while in a state that requires buffering at
  // chunk boundaries These states require seeing the next character to
  // determine completion:
  // - QUOTE_IN_QUOTED: need to see if next char is quote (doubled), delimiter,
  // or newline
  // - ESCAPE_IN_QUOTED: need to see the escaped character
  if ((stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED ||
          stream->state == CSV_STREAM_STATE_ESCAPE_IN_QUOTED) &&
      offset >= process_len) {
    // At end of chunk, wait for more data to determine completion:
    // - QUOTE_IN_QUOTED: need to see if next char is quote (doubled),
    // delimiter, or newline
    // - ESCAPE_IN_QUOTED: need to see the escaped character
    // CRITICAL: We must buffer the field data because we don't know if the
    // sequence is complete until we see the next chunk. The field data might be
    // pointing into the input buffer which will be cleared or reused.
    if (stream->field.is_buffered) {
      stream->field.data = stream->field.buffer;
      stream->field.length = stream->field.buffer_used;
      // Note: We do NOT append the quote here even if in QUOTE_IN_QUOTED state.
      // We need to wait for the next chunk to see if it's a doubled quote (next
      // char is quote) or a closing quote (next char is delimiter/newline).
    }
    else {
      // Field is not buffered yet - we need to buffer it now because we're at a
      // chunk boundary and need to preserve the field data until we see the
      // next character Use ensure_buffered which will handle copying existing
      // data correctly
      GTEXT_CSV_Status status =
          csv_field_buffer_ensure_buffered(&stream->field);
      if (status != GTEXT_CSV_OK) {
        return status;
      }
      // Note: We do NOT append the quote here even if in QUOTE_IN_QUOTED state.
      // We need to wait for the next chunk to see if it's a doubled quote (next
      // char is quote) or a closing quote (next char is delimiter/newline).
    }
    // Field data is now buffered - return to wait for next chunk
    return GTEXT_CSV_OK;
  }

  return GTEXT_CSV_OK;
}
