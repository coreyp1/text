/* Read a CSV file on stdin; print the csv-spectrum JSON shape for it.
 *
 * The suite pairs each csvs/<name>.csv with json/<name>.json holding an array
 * of objects, one per data row, keyed by the header row. That is the shape
 * emitted here so the two can be compared by value. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ghoti.io/text/csv.h>
#include <ghoti.io/text/json.h>

int main(void) {
  static char buf[1 << 22];
  size_t n = fread(buf, 1, sizeof(buf), stdin);

  GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
  /* csv-spectrum's location_coordinates case holds a bare quote inside an
     unquoted field - 37"N - which RFC 4180 does not allow and this parser
     refuses by default. It is a dialect, not the grammar, so the scorer asks
     for both and says which is which rather than hiding the choice. */
  if (getenv("CSS_ALLOW_UNQUOTED_QUOTES")) {
    opts.dialect.allow_unquoted_quotes = true;
  }
  GTEXT_CSV_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_CSV_Table *table = gtext_csv_parse_table(buf, n, &opts, &err);
  if (!table) {
    printf("FAIL parse: %s\n", err.message ? err.message : "?");
    return 0;
  }

  GTEXT_JSON_Value *rows = gtext_json_new_array();
  if (!rows) { printf("FAIL oom\n"); gtext_csv_free_table(table); return 0; }

  const size_t row_count = gtext_csv_row_count(table);
  /* Row 0 is the header. A file with no rows at all is the empty array. */
  for (size_t r = 1; r < row_count; r++) {
    GTEXT_JSON_Value *obj = gtext_json_new_object();
    if (!obj) break;
    const size_t cols = gtext_csv_col_count(table, r);
    for (size_t c = 0; c < cols; c++) {
      size_t klen = 0, vlen = 0;
      const char *key = gtext_csv_field(table, 0, c, &klen);
      const char *val = gtext_csv_field(table, r, c, &vlen);
      if (!key) continue;
      GTEXT_JSON_Value *sv = gtext_json_new_string(val ? val : "", vlen);
      if (!sv) continue;
      if (gtext_json_object_put(obj, key, klen, sv) != GTEXT_JSON_OK) {
        gtext_json_free(sv);
      }
    }
    if (gtext_json_array_push(rows, obj) != GTEXT_JSON_OK) {
      gtext_json_free(obj);
    }
  }

  GTEXT_JSON_Sink sink;
  if (gtext_json_sink_buffer(&sink) != GTEXT_JSON_OK) {
    printf("FAIL sink\n");
  }
  else {
    GTEXT_JSON_Error jerr;
    memset(&jerr, 0, sizeof(jerr));
    if (gtext_json_write_value(&sink, NULL, rows, &jerr) != GTEXT_JSON_OK) {
      printf("FAIL write: %s\n", jerr.message ? jerr.message : "?");
    }
    else {
      fwrite(gtext_json_sink_buffer_data(&sink), 1,
          gtext_json_sink_buffer_size(&sink), stdout);
      putchar('\n');
    }
    gtext_json_sink_buffer_free(&sink);
  }

  gtext_json_free(rows);
  gtext_csv_free_table(table);
  return 0;
}
