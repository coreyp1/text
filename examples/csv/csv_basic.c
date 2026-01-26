/**
 * @file
 *
 * Basic CSV parsing and writing example.
 *
 * This example demonstrates:
 * - Parsing CSV from a string
 * - Accessing values in the DOM table
 * - Writing CSV to a buffer
 * - Error handling
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  // CSV input string
  const char * csv_input = "Name,Age,City\nAlice,30,New York\nBob,25,San "
                           "Francisco\nCharlie,35,Chicago";
  size_t csv_len = strlen(csv_input);

  // Parse options (use defaults)
  GTEXT_CSV_Parse_Options opt = gtext_csv_parse_options_default();
  GTEXT_CSV_Error err = {0};

  // Parse CSV
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

  // Access table data
  size_t row_count = gtext_csv_row_count(table);
  printf("Number of rows: %zu\n", row_count);

  // Print all rows
  for (size_t row = 0; row < row_count; row++) {
    size_t col_count = gtext_csv_col_count(table, row);
    printf("Row %zu (%zu columns): ", row, col_count);

    for (size_t col = 0; col < col_count; col++) {
      size_t field_len;
      const char * field = gtext_csv_field(table, row, col, &field_len);
      if (field) {
        printf("[%.*s]", (int)field_len, field);
        if (col < col_count - 1) {
          printf(", ");
        }
      }
    }
    printf("\n");
  }

  // Write CSV to buffer
  GTEXT_CSV_Sink sink;
  if (gtext_csv_sink_buffer(&sink) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to create buffer sink\n");
    gtext_csv_free_table(table);
    return 1;
  }

  GTEXT_CSV_Write_Options write_opt = gtext_csv_write_options_default();

  if (gtext_csv_write_table(&sink, &write_opt, table) != GTEXT_CSV_OK) {
    fprintf(stderr, "Write error\n");
    gtext_csv_sink_buffer_free(&sink);
    gtext_csv_free_table(table);
    return 1;
  }

  // Print output
  printf("\nSerialized CSV:\n%.*s\n", (int)gtext_csv_sink_buffer_size(&sink),
      gtext_csv_sink_buffer_data(&sink));

  // Cleanup
  gtext_csv_sink_buffer_free(&sink);
  gtext_csv_free_table(table);

  return 0;
}
