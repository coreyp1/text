/**
 * @file
 *
 * Copyright 2026 by Corey Pennycuff
 */
/**
 * @file csv_headers.c
 * @brief CSV header processing example
 *
 * This example demonstrates:
 * - Parsing CSV with header row
 * - Looking up columns by header name
 * - Accessing data using header names
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  // CSV input with header row
  const char * csv_input = "Name,Age,City\nAlice,30,New York\nBob,25,San "
                           "Francisco\nCharlie,35,Chicago";
  size_t csv_len = strlen(csv_input);

  // Parse options with header processing enabled
  GTEXT_CSV_Parse_Options opt = gtext_csv_parse_options_default();
  opt.dialect.treat_first_row_as_header = true;

  GTEXT_CSV_Error err = {0};
  GTEXT_CSV_Table * table =
      gtext_csv_parse_table(csv_input, csv_len, &opt, &err);
  if (!table) {
    fprintf(stderr, "Parse error: %s (at line %d, column %d)\n", err.message,
        err.line, err.column);
    if (err.context_snippet) {
      fprintf(stderr, "Context: %s\n", err.context_snippet);
    }
    gtext_csv_error_free(&err);
    return 1;
  }

  // Look up column indices by header name
  size_t name_col, age_col, city_col;

  if (gtext_csv_header_index(table, "Name", &name_col) != GTEXT_CSV_OK) {
    fprintf(stderr, "Column 'Name' not found\n");
    gtext_csv_free_table(table);
    return 1;
  }

  if (gtext_csv_header_index(table, "Age", &age_col) != GTEXT_CSV_OK) {
    fprintf(stderr, "Column 'Age' not found\n");
    gtext_csv_free_table(table);
    return 1;
  }

  if (gtext_csv_header_index(table, "City", &city_col) != GTEXT_CSV_OK) {
    fprintf(stderr, "Column 'City' not found\n");
    gtext_csv_free_table(table);
    return 1;
  }

  printf("Column indices: Name=%zu, Age=%zu, City=%zu\n\n", name_col, age_col,
      city_col);

  // Access data using header-based column indices
  size_t row_count = gtext_csv_row_count(table);
  printf("Data rows (using header-based access):\n");

  for (size_t row = 0; row < row_count; row++) {
    size_t name_len, age_len, city_len;
    const char * name = gtext_csv_field(table, row, name_col, &name_len);
    const char * age = gtext_csv_field(table, row, age_col, &age_len);
    const char * city = gtext_csv_field(table, row, city_col, &city_len);

    printf("Row %zu:\n", row);
    printf("  Name: %.*s\n", (int)name_len, name);
    printf("  Age: %.*s\n", (int)age_len, age);
    printf("  City: %.*s\n", (int)city_len, city);
    printf("\n");
  }

  // Write CSV back (header will be included)
  GTEXT_CSV_Sink sink;
  if (gtext_csv_sink_buffer(&sink) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to create buffer sink\n");
    gtext_csv_free_table(table);
    return 1;
  }

  GTEXT_CSV_Write_Options write_opt = gtext_csv_write_options_default();
  // Note: The write options don't need special header handling - the table
  // already knows about the header from parsing

  if (gtext_csv_write_table(&sink, &write_opt, table) != GTEXT_CSV_OK) {
    fprintf(stderr, "Write error\n");
    gtext_csv_sink_buffer_free(&sink);
    gtext_csv_free_table(table);
    return 1;
  }

  printf("Serialized CSV (with header):\n%.*s\n",
      (int)gtext_csv_sink_buffer_size(&sink),
      gtext_csv_sink_buffer_data(&sink));

  // Cleanup
  gtext_csv_sink_buffer_free(&sink);
  gtext_csv_free_table(table);

  return 0;
}
