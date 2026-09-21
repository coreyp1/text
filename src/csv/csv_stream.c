/**
 * @file
 *
 * Streaming CSV parser implementation.
 *
 * Implements an event-based streaming parser that accepts input in chunks
 * and emits events for each CSV record/field encountered.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/macros.h>
#include "csv_stream_internal.h"

#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_stream.h>
// Emit an event
GTEXT_CSV_Status csv_stream_emit_event(GTEXT_CSV_Stream * stream,
    GTEXT_CSV_Event_Type type, const char * data, size_t data_len) {
  if (!stream->callback) {
    return GTEXT_CSV_OK;
  }

  GTEXT_CSV_Event event;
  event.type = type;
  event.data = data;
  event.data_len = data_len;
  event.row_index = stream->row_count;
  event.col_index = stream->field_count;

  return stream->callback(&event, stream->user_data);
}

// Set stream error
GTEXT_CSV_Status csv_stream_set_error(
    GTEXT_CSV_Stream * stream, GTEXT_CSV_Status code, const char * message) {
  // Release whatever the error already owns before overwriting it.
  gtext_csv_error_free(&stream->error);

  // Set error fields first
  stream->error.code = code;
  stream->error.message = message;
  // Every message reaching this function is a static string.
  stream->error.message_is_owned = false;
  stream->error.byte_offset = stream->pos.offset;
  stream->error.line = stream->pos.line;
  stream->error.column = stream->pos.column;
  stream->error.row_index = stream->row_count;
  stream->error.col_index = stream->field_count;
  stream->error.context_snippet = NULL;
  stream->error.context_snippet_len = 0;
  stream->error.caret_offset = 0;

  // Generate context snippet if we have input buffer access
  const char * input_for_snippet = NULL;
  size_t input_len_for_snippet = 0;
  size_t error_offset = stream->error.byte_offset;

  // Prefer original_input_buffer (full input for table parsing)
  if (stream->original_input_buffer && stream->original_input_buffer_len > 0) {
    input_for_snippet = stream->original_input_buffer;
    input_len_for_snippet = stream->original_input_buffer_len;
    // Error offset is already relative to the original input buffer
  }
  // Fall back to buffered input if available
  else if (stream->input_buffer && stream->input_buffer_used > 0) {
    input_for_snippet = stream->input_buffer;
    input_len_for_snippet = stream->input_buffer_used;
    // For buffered input, we need to adjust the error offset
    // The error offset is relative to the total bytes consumed, but the buffer
    // only contains recent data. For now, clamp to buffer bounds.
    if (error_offset > input_len_for_snippet) {
      error_offset = input_len_for_snippet;
    }
  }

  // Both of these options were set by gtext_csv_parse_options_default() and
  // then read by nothing: the snippet was generated unconditionally, at a
  // hardcoded radius.  Turning enable_context_snippet off did not stop the
  // allocation, and context_radius_bytes did not change its size.
  if (stream->opts.enable_context_snippet && input_for_snippet &&
      input_len_for_snippet > 0 && error_offset <= input_len_for_snippet) {
    char * snippet = NULL;
    size_t snippet_len = 0;
    size_t caret_offset = 0;
    // 0 means "library default", as it does for every other size_t in this
    // options struct.  Callers who want no snippet clear
    // enable_context_snippet rather than asking for a radius of zero.
    const size_t radius = csv_get_limit(
        stream->opts.context_radius_bytes, CSV_DEFAULT_CONTEXT_RADIUS_BYTES);

    GTEXT_CSV_Status snippet_status = csv_error_generate_context_snippet(
        input_for_snippet, input_len_for_snippet, error_offset, radius, radius,
        &snippet, &snippet_len, &caret_offset);

    if (snippet_status == GTEXT_CSV_OK && snippet) {
      stream->error.context_snippet = snippet;
      stream->error.context_snippet_len = snippet_len;
      stream->error.caret_offset = caret_offset;
    }
  }

  stream->state = CSV_STREAM_STATE_END;
  return code;
}

GTEXT_API GTEXT_CSV_Stream * gtext_csv_stream_new(
    const GTEXT_CSV_Parse_Options * opts, GTEXT_CSV_Event_cb callback,
    void * user_data) {
  if (!callback) {
    return NULL;
  }

  GTEXT_CSV_Stream * stream = calloc(1, sizeof(GTEXT_CSV_Stream));
  if (!stream) {
    return NULL;
  }

  if (opts) {
    stream->opts = *opts;
  }
  else {
    stream->opts = gtext_csv_parse_options_default();
  }

  stream->callback = callback;
  stream->user_data = user_data;
  stream->state = CSV_STREAM_STATE_START_OF_RECORD;
  stream->in_record = false;
  stream->field_count = 0;
  stream->row_count = 0;
  stream->pos.offset = 0;
  stream->pos.line = 1;
  stream->pos.column = 1;
  stream->total_bytes_consumed = 0;
  stream->original_input_buffer = NULL;
  csv_field_buffer_init(&stream->field);
  stream->original_input_buffer_len = 0;

  // Set limits
  stream->max_rows = csv_get_limit(stream->opts.max_rows, CSV_DEFAULT_MAX_ROWS);
  stream->max_cols = csv_get_limit(stream->opts.max_cols, CSV_DEFAULT_MAX_COLS);
  stream->max_field_bytes =
      csv_get_limit(stream->opts.max_field_bytes, CSV_DEFAULT_MAX_FIELD_BYTES);
  stream->max_record_bytes = csv_get_limit(
      stream->opts.max_record_bytes, CSV_DEFAULT_MAX_RECORD_BYTES);
  stream->max_total_bytes =
      csv_get_limit(stream->opts.max_total_bytes, CSV_DEFAULT_MAX_TOTAL_BYTES);

  // Comment prefix
  if (stream->opts.dialect.allow_comments &&
      stream->opts.dialect.comment_prefix) {
    stream->comment_prefix_len = strlen(stream->opts.dialect.comment_prefix);
  }
  else {
    stream->comment_prefix_len = 0;
  }

  // Initialize error
  memset(&stream->error, 0, sizeof(stream->error));

  return stream;
}

/**
 * @brief Feed bytes that are known to be safe to process now.
 *
 * Everything the caller passes reaches the state machine through here, after
 * gtext_csv_stream_feed() has settled the BOM and held back any trailing CR.
 * It never buffers on its own account.
 */
/*
 * Incremental UTF-8 validation.
 *
 * The table parser validates the whole buffer in one pass before tokenizing.
 * A stream cannot: a sequence may be split across feeds, so the bytes seen so
 * far are carried in the stream and the verdict is deferred until the rest
 * arrives.  The rules are RFC 3629's and match csv_validate_utf8() exactly -
 * the two must agree, or the same document would be accepted one way and
 * refused the other, which is precisely what the differential fuzzer looks
 * for.
 *
 * Returns the offset of the offending byte in *bad_offset when it fails.
 */
static bool csv_utf8_lead_length(unsigned char b, int * need) {
  if ((b & 0x80) == 0) {
    *need = 1;
  }
  else if ((b & 0xE0) == 0xC0) {
    *need = 2;
  }
  else if ((b & 0xF0) == 0xE0) {
    *need = 3;
  }
  else if ((b & 0xF8) == 0xF0) {
    *need = 4;
  }
  else {
    return false; /* continuation byte or 0xF8..0xFF with no lead */
  }
  return true;
}

/* Check a complete sequence for the things the length alone does not catch. */
static bool csv_utf8_sequence_ok(const unsigned char * b, int n) {
  for (int i = 1; i < n; i++) {
    if ((b[i] & 0xC0) != 0x80) {
      return false;
    }
  }
  if (n == 2) {
    return (b[0] & 0x1E) != 0; /* overlong */
  }
  if (n == 3) {
    if ((b[0] & 0x0F) == 0 && (b[1] & 0x20) == 0) {
      return false; /* overlong */
    }
    /* Surrogate halves, U+D800..U+DFFF, are excluded by RFC 3629 section 3. */
    return !(b[0] == 0xED && b[1] >= 0xA0);
  }
  if (n == 4) {
    if ((b[0] & 0x07) == 0 && (b[1] & 0x30) == 0) {
      return false; /* overlong */
    }
    /* After F4 the second byte runs 80..8F and no further: 8F BF BF is
       U+10FFFF. The test used to be (b[1] & 0xF0) != 0, which is true of
       every continuation byte, so the whole of plane 16 was refused along
       with the sequences it meant to catch. csv_validate_utf8() had the
       same line - the comment above promising the two agree was right, and
       they agreed on being wrong. */
    if (b[0] > 0xF4 || (b[0] == 0xF4 && b[1] > 0x8F)) {
      return false; /* beyond U+10FFFF */
    }
  }
  return true;
}

static bool csv_stream_validate_utf8_chunk(GTEXT_CSV_Stream * stream,
    const unsigned char * data, size_t len, size_t base_offset,
    size_t * bad_offset) {
  for (size_t i = 0; i < len; i++) {
    unsigned char b = data[i];

    if (stream->u8_need == 0) {
      int need = 0;
      if (!csv_utf8_lead_length(b, &need)) {
        *bad_offset = base_offset + i;
        return false;
      }
      if (need == 1) {
        continue; /* ASCII, nothing to carry */
      }
      stream->u8_bytes[0] = b;
      stream->u8_have = 1;
      stream->u8_need = need;
      stream->u8_offset = base_offset + i;
      continue;
    }

    stream->u8_bytes[stream->u8_have++] = b;
    if (stream->u8_have < stream->u8_need) {
      continue; /* still incomplete, wait for more */
    }

    bool ok = csv_utf8_sequence_ok(stream->u8_bytes, stream->u8_need);
    size_t seq_offset = stream->u8_offset;
    stream->u8_need = 0;
    stream->u8_have = 0;
    if (!ok) {
      *bad_offset = seq_offset;
      return false;
    }
  }
  return true;
}

static GTEXT_CSV_Status csv_stream_feed_settled(GTEXT_CSV_Stream * stream,
    const void * data, size_t len, GTEXT_CSV_Error * err) {
  if (!data || len == 0) {
    return GTEXT_CSV_OK;
  }

  // Validate the encoding before tokenizing, as the table parser does.  The
  // option was accepted and ignored here for as long as the streaming parser
  // has existed, so a caller who asked for validation and fed the document in
  // pieces got none.
  if (stream->opts.validate_utf8) {
    size_t bad = 0;
    if (!csv_stream_validate_utf8_chunk(
            stream, (const unsigned char *)data, len, stream->pos.offset, &bad)) {
      GTEXT_CSV_Status status = csv_stream_set_error(stream,
          GTEXT_CSV_E_INVALID_UTF8, "Invalid UTF-8 sequence in input");
      stream->error.byte_offset = bad;
      if (err) {
        csv_error_copy(err, &stream->error);
      }
      return status;
    }
  }

  // If we have a field in progress, we need to continue it
  // CRITICAL: If we're in QUOTE_IN_QUOTED state and the field is not buffered,
  // this should not happen - the field should have been buffered at the end of
  // the previous chunk. However, if it wasn't (due to a bug), we need to handle
  // it gracefully. Since we can't access the previous chunk's data, we'll
  // create an empty buffer. This will cause the field to be empty, which is
  // incorrect but better than crashing.
  if (stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED &&
      !stream->field.is_buffered) {
    // Field should have been buffered at the end of the previous chunk, but it
    // wasn't. This is a bug in the previous chunk processing. We can't recover
    // the field data, but we can at least ensure the parser doesn't crash.
    // Reached by no test, by none of the CSV fuzzer's 5,164,660 executions
    // and by no csv-spectrum case - a probe placed here fired on nothing.
    // It is kept rather than deleted because, unlike the dead branch removed
    // from csv_stream_buffer.c, no argument shows it cannot happen: it rests
    // on the previous chunk having buffered the field, which is decided in
    // another file.  Crashing would be the alternative.
    if (!stream->field.buffer) {
      GTEXT_CSV_Status status =
          csv_field_buffer_grow(&stream->field, CSV_FIELD_BUFFER_INITIAL_SIZE);
      if (status != GTEXT_CSV_OK) {
        if (err) {
          csv_error_copy(err, &stream->error);
        }
        return status;
      }
    }
    stream->field.buffer_used = 0;
    stream->field.is_buffered = true;
    stream->field.data = stream->field.buffer;
    stream->field.length = 0;
    // Note: The field data from the previous chunk is lost. This should not
    // happen if the previous chunk processing worked correctly.
  }

  // If we have a field in progress that's buffered, we need to continue it
  // Otherwise, process the chunk normally
  if (stream->field.is_buffered &&
      (stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD ||
          stream->state == CSV_STREAM_STATE_QUOTED_FIELD ||
          stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED)) {
    // The field buffer already contains the partial field from previous chunks.
    // We need to process the new chunk data, appending field content to the
    // buffer as we encounter it, until the field completes.

    // CRITICAL: Ensure field.data points to field.buffer when field is buffered
    // This prevents field.data from pointing into invalid input buffer memory
    if (stream->field.data != stream->field.buffer) {
      stream->field.data = stream->field.buffer;
      stream->field.length = stream->field.buffer_used;
    }

    // Process the new chunk - it will append to field.buffer as needed
    GTEXT_CSV_Status status =
        csv_stream_process_chunk(stream, (const char *)data, len);

    // Reset field buffering state after processing if field completed.
    //
    // ESCAPE_IN_QUOTED belongs in this list for the same reason as the other
    // three: the field is still open, waiting for the character after the
    // backslash.  While it was missing, a chunk that ended on a backslash had
    // everything accumulated so far thrown away, so "x\"y" fed one byte at a
    // time produced "y rather than x"y.  It only showed up at chunk sizes
    // that put the backslash last, which is why feeding whole documents never
    // found it.
    if (stream->state != CSV_STREAM_STATE_UNQUOTED_FIELD &&
        stream->state != CSV_STREAM_STATE_QUOTED_FIELD &&
        stream->state != CSV_STREAM_STATE_QUOTE_IN_QUOTED &&
        stream->state != CSV_STREAM_STATE_ESCAPE_IN_QUOTED) {
      // Field completed, clear buffer
      csv_field_buffer_clear(&stream->field);
    }

    if (status != GTEXT_CSV_OK && err) {
      csv_error_copy(err, &stream->error);
    }
    return status;
  }

  GTEXT_CSV_Status status =
      csv_stream_process_chunk(stream, (const char *)data, len);

  if (status != GTEXT_CSV_OK && err) {
    csv_error_copy(err, &stream->error);
  }

  return status;
}

/**
 * @brief Number of bytes at the tail of a chunk that cannot be decided yet.
 *
 * A trailing CR is the only such case once the BOM is settled: whether it is a
 * lone CR or the first half of a CRLF depends on the next byte, and
 * csv_detect_newline() needs both in one buffer to see the pair.
 */
static size_t csv_stream_undecided_tail(
    const GTEXT_CSV_Stream * stream, const char * data, size_t len) {
  if (len == 0 || !stream->opts.dialect.accept_crlf) {
    return 0;
  }
  return data[len - 1] == '\r' ? 1 : 0;
}

/**
 * @brief Settle the leading BOM, buffering until enough bytes have arrived.
 *
 * Returns true when the caller may proceed; *consumed is the number of input
 * bytes absorbed into the carry. While the answer is still unknown - fewer
 * than three bytes seen, and more input promised - everything is carried and
 * the caller returns.
 */
static bool csv_stream_settle_bom(GTEXT_CSV_Stream * stream, const char ** data,
    size_t * len, bool final, GTEXT_CSV_Status * status) {
  static const char BOM[3] = {'\xEF', '\xBB', '\xBF'};

  *status = GTEXT_CSV_OK;
  if (stream->bom_resolved) {
    return true;
  }

  // The common case: the first feed already carries enough to decide, so the
  // answer is read straight out of the caller's buffer and the data pointer is
  // left alone.  Copying here instead would break in-situ parsing, whose whole
  // point is that a field points into the buffer the caller supplied.
  if (stream->carry_len == 0 && *len >= sizeof BOM) {
    stream->bom_resolved = true;
    if (!stream->opts.keep_bom && memcmp(*data, BOM, sizeof BOM) == 0) {
      if (stream->pos.offset > SIZE_MAX - sizeof BOM) {
        *status = csv_stream_set_error(
            stream, GTEXT_CSV_E_LIMIT, "Overflow in BOM stripping");
        return false;
      }
      *data += sizeof BOM;
      *len -= sizeof BOM;
      stream->pos.offset += sizeof BOM;
    }
    return true;
  }

  // Otherwise the chunk is too short to tell: hold what there is.
  while (stream->carry_len < sizeof BOM && *len > 0) {
    stream->carry[stream->carry_len++] = **data;
    (*data)++;
    (*len)--;
  }

  if (stream->carry_len < sizeof BOM && !final) {
    return false; // Undecidable, and more input is coming.
  }

  stream->bom_resolved = true;

  if (!stream->opts.keep_bom && stream->carry_len >= sizeof BOM &&
      memcmp(stream->carry, BOM, sizeof BOM) == 0) {
    // Drop it, and account for it the way csv_strip_bom() would have.
    stream->carry_len = 0;
    if (stream->pos.offset > SIZE_MAX - sizeof BOM) {
      *status = csv_stream_set_error(
          stream, GTEXT_CSV_E_LIMIT, "Overflow in BOM stripping");
      return false;
    }
    stream->pos.offset += sizeof BOM;
  }

  return true;
}

/**
 * @brief Feed a chunk, honoring and refreshing the carry.
 *
 * Carried bytes are joined to the front of the chunk and fed as one short
 * buffer, so the state machine always sees a CRLF pair together rather than
 * split across two calls. Only the join is copied - once the carry is drained
 * the rest of the chunk is passed through untouched, so a large feed stays a
 * single pass.
 *
 * @param final True when no more input will follow, so nothing may be held
 *              back; gtext_csv_stream_finish() passes true.
 */
static GTEXT_CSV_Status csv_stream_feed_carried(GTEXT_CSV_Stream * stream,
    const char * data, size_t len, bool final, GTEXT_CSV_Error * err) {
  GTEXT_CSV_Status status = GTEXT_CSV_OK;

  if (!csv_stream_settle_bom(stream, &data, &len, final, &status)) {
    if (status != GTEXT_CSV_OK && err) {
      csv_error_copy(err, &stream->error);
    }
    return status;
  }

  // Drain the carry against the head of the chunk before touching the bulk of
  // it. Each pass takes at most one byte from the chunk, so this terminates.
  while (stream->carry_len > 0) {
    char * joined = stream->join;
    size_t n = stream->carry_len;
    memcpy(joined, stream->carry, n);
    stream->carry_len = 0;

    if (len > 0) {
      joined[n++] = *data;
      data++;
      len--;
    }

    size_t held = final ? 0 : csv_stream_undecided_tail(stream, joined, n);
    if (held == n) {
      // Still undecidable and nothing was gained: wait for the next feed.
      memcpy(stream->carry, joined, held);
      stream->carry_len = held;
      return GTEXT_CSV_OK;
    }

    status = csv_stream_feed_settled(stream, joined, n - held, err);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
    if (held > 0) {
      memcpy(stream->carry, joined + n - held, held);
      stream->carry_len = held;
      if (len == 0) {
        return GTEXT_CSV_OK; // Nothing left to decide it against.
      }
    }
  }

  if (len == 0) {
    return GTEXT_CSV_OK;
  }

  size_t held = final ? 0 : csv_stream_undecided_tail(stream, data, len);
  status = csv_stream_feed_settled(stream, data, len - held, err);
  if (status != GTEXT_CSV_OK) {
    return status;
  }
  if (held > 0) {
    memcpy(stream->carry, data + len - held, held);
    stream->carry_len = held;
  }
  return GTEXT_CSV_OK;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_stream_feed(GTEXT_CSV_Stream * stream,
    const void * data, size_t len, GTEXT_CSV_Error * err) {
  if (!stream) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Stream must not be NULL");
    return GTEXT_CSV_E_INVALID;
  }

  if (stream->state == CSV_STREAM_STATE_END) {
    if (err) {
      csv_error_copy(err, &stream->error);
    }
    return stream->error.code != GTEXT_CSV_OK ? stream->error.code
                                              : GTEXT_CSV_E_INVALID;
  }

  if (!data || len == 0) {
    return GTEXT_CSV_OK;
  }

  return csv_stream_feed_carried(stream, (const char *)data, len, false, err);
}

GTEXT_API GTEXT_CSV_Status gtext_csv_stream_finish(
    GTEXT_CSV_Stream * stream, GTEXT_CSV_Error * err) {
  if (!stream) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "Stream must not be NULL");
    return GTEXT_CSV_E_INVALID;
  }

  // Nothing more is coming, so anything held back is now decidable: a carried
  // CR is a lone CR, and fewer than three leading bytes are not a BOM.  Feed
  // them before drawing any conclusion about the parser's final state.
  if ((stream->carry_len > 0 || !stream->bom_resolved) &&
      stream->state != CSV_STREAM_STATE_END) {
    GTEXT_CSV_Status status =
        csv_stream_feed_carried(stream, NULL, 0, true, err);
    if (status != GTEXT_CSV_OK) {
      return status;
    }
  }

  // A UTF-8 sequence still waiting for its continuation bytes when the input
  // ends is truncated, which the table parser reports as invalid too.
  if (stream->opts.validate_utf8 && stream->u8_need != 0) {
    GTEXT_CSV_Status status = csv_stream_set_error(stream,
        GTEXT_CSV_E_INVALID_UTF8, "Truncated UTF-8 sequence at end of input");
    stream->error.byte_offset = stream->u8_offset;
    if (err) {
      csv_error_copy(err, &stream->error);
    }
    return status;
  }

  // Check for unterminated quote.
  //
  // CSV_STREAM_STATE_QUOTE_IN_QUOTED is deliberately not in this list.  That
  // state means a quote was seen inside a quoted field and the parser is
  // waiting for the next character to say which quote it was: another quote
  // makes it a doubled escape, anything else makes it the closing quote.  When
  // the input ends there is no next character, so it was the closing quote and
  // the field is complete.
  //
  // Treating it as unterminated rejected every document whose last field is
  // quoted and which has no trailing newline - `"a"`, `a,"b"`, `"a","b"` -
  // while the same documents with a newline parsed fine, and unquoted last
  // fields parsed fine without one.  RFC 4180 section 2 rule 2 says the last
  // record may or may not have an ending line break, and a quoted final field
  // is ordinary in exported data, so this rejected a large class of real
  // files.
  //
  // CSV_STREAM_STATE_QUOTED_FIELD really is unterminated: no closing quote was
  // seen at all.  So is ESCAPE_IN_QUOTED, which is a backslash with nothing
  // after it.
  if (stream->state == CSV_STREAM_STATE_QUOTED_FIELD ||
      stream->state == CSV_STREAM_STATE_ESCAPE_IN_QUOTED) {
    GTEXT_CSV_Status status = csv_stream_set_error(
        stream, GTEXT_CSV_E_UNTERMINATED_QUOTE, "Unterminated quoted field");
    if (err) {
      csv_error_copy(err, &stream->error);
    }
    return status;
  }

  // Emit final record end if in record
  if (stream->in_record) {
    // Emit current field if any (only if we're actually in a field, not just at
    // start)
    // QUOTE_IN_QUOTED is a completed quoted field, as above.  The closing
    // quote was never appended to the field data and the content was buffered
    // up to it, so what is held is already the field's value.
    if (stream->state == CSV_STREAM_STATE_UNQUOTED_FIELD ||
        stream->state == CSV_STREAM_STATE_QUOTED_FIELD ||
        stream->state == CSV_STREAM_STATE_QUOTE_IN_QUOTED) {
      // Ensure field.data is correct for buffered fields
      if (stream->field.is_buffered) {
        stream->field.data = stream->field.buffer;
        stream->field.length = stream->field.buffer_used;
      }
      const char * field_data = stream->field.data;
      // For buffered fields, use buffer_used as the source of truth
      size_t actual_field_len = stream->field.is_buffered
          ? stream->field.buffer_used
          : stream->field.length;
      const char * unescaped_data;
      size_t unescaped_len;
      GTEXT_CSV_Status status = csv_stream_unescape_field(stream, field_data,
          actual_field_len, &unescaped_data, &unescaped_len);
      if (status != GTEXT_CSV_OK) {
        if (err) {
          csv_error_copy(err, &stream->error);
        }
        return status;
      }
      status = csv_stream_emit_event(
          stream, GTEXT_CSV_EVENT_FIELD, unescaped_data, unescaped_len);
      if (status != GTEXT_CSV_OK) {
        if (err) {
          csv_error_copy(err, &stream->error);
        }
        return status;
      }
    }
    else if (stream->state == CSV_STREAM_STATE_START_OF_FIELD) {
      // A delimiter was the last thing in the input, so the record has one
      // more field and it is empty: "a," is two fields, per RFC 4180 section 2
      // rule 5, and only a newline ends a record.  Reaching this state cannot
      // mean "just after a newline" - that leaves the parser in
      // START_OF_RECORD with in_record clear - so there is no risk of
      // inventing a field for a trailing line break.  Without this, a file not
      // ending in a newline silently lost its last field: "a," parsed as the
      // single field "a".
      GTEXT_CSV_Status status =
          csv_stream_emit_event(stream, GTEXT_CSV_EVENT_FIELD, "", 0);
      if (status != GTEXT_CSV_OK) {
        if (err) {
          csv_error_copy(err, &stream->error);
        }
        return status;
      }
    }

    GTEXT_CSV_Status status =
        csv_stream_emit_event(stream, GTEXT_CSV_EVENT_RECORD_END, NULL, 0);
    if (status != GTEXT_CSV_OK) {
      if (err) {
        csv_error_copy(err, &stream->error);
      }
      return status;
    }
  }

  // Emit END event
  GTEXT_CSV_Status status =
      csv_stream_emit_event(stream, GTEXT_CSV_EVENT_END, NULL, 0);
  if (status != GTEXT_CSV_OK && err) {
    csv_error_copy(err, &stream->error);
  }

  stream->state = CSV_STREAM_STATE_END;
  return status;
}

GTEXT_API void gtext_csv_stream_free(GTEXT_CSV_Stream * stream) {
  if (!stream) {
    return;
  }

  free(stream->input_buffer);
  free(stream->field.buffer);
  gtext_csv_error_free(&stream->error);
  free(stream);
}

GTEXT_INTERNAL_API void csv_stream_mark_bom_resolved(
    GTEXT_CSV_Stream * stream) {
  if (stream) {
    stream->bom_resolved = true;
  }
}

GTEXT_INTERNAL_API void csv_stream_set_original_input_buffer(
    GTEXT_CSV_Stream * stream, const char * input_buffer,
    size_t input_buffer_len) {
  if (!stream) {
    return;
  }
  stream->original_input_buffer = input_buffer;
  stream->original_input_buffer_len = input_buffer_len;
  csv_field_buffer_set_original_input(
      &stream->field, input_buffer, input_buffer_len);
}
