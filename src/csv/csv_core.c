/**
 * @file
 *
 * Core CSV types and default initialization.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <string.h>

#include "csv_internal.h"

#include <ghoti.io/text/csv/csv_core.h>
GTEXT_API GTEXT_CSV_Dialect gtext_csv_dialect_default(void) {
  GTEXT_CSV_Dialect d = {0};
  d.delimiter = ',';
  d.quote = '"';
  d.escape = GTEXT_CSV_ESCAPE_DOUBLED_QUOTE;
  d.newline_in_quotes = true;
  d.accept_lf = true;
  d.accept_crlf = true;
  d.accept_cr = false;
  d.trim_unquoted_fields = false;
  d.allow_space_after_delimiter = false;
  d.allow_unquoted_quotes = false;
  d.allow_unquoted_newlines = false;
  d.allow_comments = false;
  d.comment_prefix = "#";
  d.treat_first_row_as_header = false;
  d.header_dup_mode = GTEXT_CSV_DUPCOL_FIRST_WINS;
  return d;
}

GTEXT_API GTEXT_CSV_Parse_Options gtext_csv_parse_options_default(void) {
  GTEXT_CSV_Parse_Options opts = {0};
  opts.dialect = gtext_csv_dialect_default();
  opts.validate_utf8 = true;
  opts.in_situ_mode = false;
  opts.keep_bom = false;
  opts.max_rows = 0;         // Library default
  opts.max_cols = 0;         // Library default
  opts.max_field_bytes = 0;  // Library default
  opts.max_record_bytes = 0; // Library default
  opts.max_total_bytes = 0;  // Library default
  opts.enable_context_snippet = true;
  opts.context_radius_bytes = CSV_DEFAULT_CONTEXT_RADIUS_BYTES;
  return opts;
}

GTEXT_API GTEXT_CSV_Write_Options gtext_csv_write_options_default(void) {
  GTEXT_CSV_Write_Options opts = {0};
  opts.dialect = gtext_csv_dialect_default();
  opts.newline = "\n";
  opts.quote_all_fields = false;
  opts.quote_empty_fields = true;
  opts.quote_if_needed = true;
  opts.always_escape_quotes = true; // Default behavior depends on escape mode
  opts.trailing_newline = false;
  return opts;
}
