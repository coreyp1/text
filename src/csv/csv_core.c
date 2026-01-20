/**
 * @file csv_core.c
 * @brief Core CSV types and default initialization
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <string.h>

TEXT_API text_csv_dialect text_csv_dialect_default(void) {
    text_csv_dialect d = {0};
    d.delimiter = ',';
    d.quote = '"';
    d.escape = TEXT_CSV_ESCAPE_DOUBLED_QUOTE;
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
    d.header_dup_mode = TEXT_CSV_DUPCOL_ERROR;
    return d;
}

TEXT_API text_csv_parse_options text_csv_parse_options_default(void) {
    text_csv_parse_options opts = {0};
    opts.dialect = text_csv_dialect_default();
    opts.validate_utf8 = true;
    opts.in_situ_mode = false;
    opts.keep_bom = false;
    opts.max_rows = 0;  // Library default
    opts.max_cols = 0;  // Library default
    opts.max_field_bytes = 0;  // Library default
    opts.max_record_bytes = 0;  // Library default
    opts.max_total_bytes = 0;  // Library default
    opts.enable_context_snippet = true;
    opts.context_radius_bytes = CSV_DEFAULT_CONTEXT_RADIUS_BYTES;
    return opts;
}

TEXT_API text_csv_write_options text_csv_write_options_default(void) {
    text_csv_write_options opts = {0};
    opts.dialect = text_csv_dialect_default();
    opts.newline = "\n";
    opts.quote_all_fields = false;
    opts.quote_empty_fields = true;
    opts.quote_if_needed = true;
    opts.always_escape_quotes = true;  // Default behavior depends on escape mode
    opts.trailing_newline = false;
    return opts;
}
