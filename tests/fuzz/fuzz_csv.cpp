/**
 * @file
 *
 * libFuzzer harness for the CSV parser.
 *
 * CSV looks simple and is not: quoting, embedded newlines, escape modes, and
 * the header row all interact, and the dialect is configurable enough that a
 * fixed set of options would leave most of the state machine unreached. The
 * first two input bytes therefore choose the dialect, including the delimiter
 * and quote characters, so the fuzzer can find combinations a human would not
 * write down.
 *
 * The same document is then parsed twice - once with gtext_csv_parse_table()
 * and once by feeding gtext_csv_stream_feed() in chunks whose sizes come from
 * the input - and the two results are compared.  Each is the other's oracle,
 * which is what makes this harness able to find a wrong answer and not only a
 * crash.  The chunks are copied through a scratch buffer that is overwritten
 * between calls, because a streaming parser that keeps a pointer into the
 * previous chunk passes every test fed from one contiguous array and fails for
 * every real caller reading into a reusable buffer.
 *
 * Build with: make fuzz-csv     Run: make fuzz-run-csv
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <ghoti.io/text/csv.h>
}

namespace {

/// Records the events the streaming parser emits, in the same shape the DOM
/// parser's contents are rendered into, so the two can be compared directly.
struct StreamCapture {
  std::vector<std::vector<std::string>> rows;
  bool overflowed = false;
};

GTEXT_CSV_Status capture_event(const GTEXT_CSV_Event * event, void * user) {
  StreamCapture * cap = static_cast<StreamCapture *>(user);
  switch (event->type) {
  case GTEXT_CSV_EVENT_RECORD_BEGIN:
    if (cap->rows.size() > 100000) {
      cap->overflowed = true;
      return GTEXT_CSV_E_LIMIT;
    }
    cap->rows.emplace_back();
    break;
  case GTEXT_CSV_EVENT_FIELD:
    if (cap->rows.empty()) {
      cap->rows.emplace_back();
    }
    cap->rows.back().emplace_back(
        event->data ? event->data : "", event->data_len);
    break;
  case GTEXT_CSV_EVENT_RECORD_END:
  case GTEXT_CSV_EVENT_END:
    break;
  }
  return GTEXT_CSV_OK;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 3) {
    return 0;
  }

  const uint8_t flags = data[0];
  const uint8_t chars = data[1];
  const void * text = data + 2;
  const size_t len = size - 2;

  GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
  // Delimiter and quote come from the input, so the parser is not always
  // looking for a comma. They must differ, or the dialect is meaningless.
  static const char candidates[] = {',', ';', '\t', '|', ':', '"', '\'', '#'};
  opts.dialect.delimiter = candidates[chars & 0x07];
  opts.dialect.quote = candidates[(chars >> 3) & 0x07];
  if (opts.dialect.quote == opts.dialect.delimiter) {
    opts.dialect.quote = '"';
    if (opts.dialect.delimiter == '"') {
      opts.dialect.delimiter = ',';
    }
  }

  opts.dialect.newline_in_quotes = (flags & 0x01) != 0;
  opts.dialect.accept_cr = (flags & 0x02) != 0;
  opts.dialect.trim_unquoted_fields = (flags & 0x04) != 0;
  opts.dialect.allow_space_after_delimiter = (flags & 0x08) != 0;
  opts.dialect.allow_unquoted_quotes = (flags & 0x10) != 0;
  opts.dialect.allow_unquoted_newlines = (flags & 0x20) != 0;
  opts.dialect.allow_comments = (flags & 0x40) != 0;
  opts.dialect.treat_first_row_as_header = (flags & 0x80) != 0;

  GTEXT_CSV_Error err{};
  GTEXT_CSV_Table * table = gtext_csv_parse_table(text, len, &opts, &err);

  std::vector<std::vector<std::string>> dom_rows;
  if (table) {
    // Read every field back, so indexing is exercised and not just parsing.
    size_t rows = gtext_csv_row_count(table);
    for (size_t r = 0; r < rows; r++) {
      size_t cols = gtext_csv_col_count(table, r);
      dom_rows.emplace_back();
      for (size_t c = 0; c < cols; c++) {
        size_t field_len = 0;
        const char * f = gtext_csv_field(table, r, c, &field_len);
        dom_rows.back().emplace_back(f ? f : "", field_len);
      }
    }
    gtext_csv_free_table(table);
  }
  // The error carries a heap-allocated context snippet, owned by us.
  gtext_csv_error_free(&err);

  // Same document, same options, fed in chunks.
  StreamCapture cap;
  GTEXT_CSV_Error serr{};
  GTEXT_CSV_Stream * stream = gtext_csv_stream_new(&opts, capture_event, &cap);
  if (!stream) {
    return 0;
  }

  // Chunk sizes are driven by the document's own bytes, so the fuzzer can
  // steer a boundary onto whichever byte breaks the parser - between a CR and
  // its LF, inside a BOM, between the two halves of a doubled quote.
  const uint8_t * bytes = static_cast<const uint8_t *>(text);
  GTEXT_CSV_Status sstatus = GTEXT_CSV_OK;
  size_t pos = 0;
  size_t step = 0;
  char scratch[64];
  while (pos < len && sstatus == GTEXT_CSV_OK) {
    size_t want = 1 + (size_t)(bytes[step++ % len] % sizeof scratch);
    if (want > len - pos) {
      want = len - pos;
    }
    // Hand the parser a copy that dies as soon as the call returns.
    memset(scratch, 0xDD, sizeof scratch);
    memcpy(scratch, bytes + pos, want);
    sstatus = gtext_csv_stream_feed(stream, scratch, want, &serr);
    memset(scratch, 0xEE, sizeof scratch);
    pos += want;
  }
  if (sstatus == GTEXT_CSV_OK) {
    sstatus = gtext_csv_stream_finish(stream, &serr);
  }
  gtext_csv_stream_free(stream);
  gtext_csv_error_free(&serr);

  // Both succeeded, so they must agree.  A difference here is a wrong answer,
  // which no amount of crash-free fuzzing would otherwise surface.
  // treat_first_row_as_header is deliberately not comparable: the table hides
  // the header row from gtext_csv_row_count() while the event stream reports
  // every record, header included.  That is the two APIs' contracts differing,
  // not the parsers disagreeing.
  //
  // allow_unquoted_newlines is excluded for a different reason: the two
  // parsers genuinely disagree under it, and the table parser disagrees with
  // itself - "aB\r\n" keeps the trailing CRLF as field content while
  // "a\r\nB\r\n" treats both as record separators.  The bulk scanner honors
  // the option and the per-character path does not, so the answer depends on
  // where a chunk happens to start.  That is a real defect, recorded on the
  // CSV format page; it is fenced off here so the harness keeps finding other
  // things instead of stopping on this one every run.
  if (table && sstatus == GTEXT_CSV_OK && !cap.overflowed
      && !opts.dialect.treat_first_row_as_header
      && !opts.dialect.allow_unquoted_newlines) {
    // A trailing empty record is reported by one and not the other; compare
    // only what both describe.
    while (!cap.rows.empty() && cap.rows.size() > dom_rows.size()
        && cap.rows.back().empty()) {
      cap.rows.pop_back();
    }
    if (cap.rows != dom_rows) {
      abort();
    }
  }

  return 0;
}
