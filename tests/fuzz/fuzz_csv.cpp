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
 * Build with: make fuzz-csv     Run: make fuzz-run-csv
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>

extern "C" {
#include <ghoti.io/text/csv.h>
}

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
  if (table) {
    // Read every field back, so indexing is exercised and not just parsing.
    size_t rows = gtext_csv_row_count(table);
    for (size_t r = 0; r < rows; r++) {
      size_t cols = gtext_csv_col_count(table, r);
      for (size_t c = 0; c < cols; c++) {
        size_t field_len = 0;
        (void)gtext_csv_field(table, r, c, &field_len);
      }
    }
    gtext_csv_free_table(table);
  }
  // The error carries a heap-allocated context snippet, owned by us.
  gtext_csv_error_free(&err);
  return 0;
}
