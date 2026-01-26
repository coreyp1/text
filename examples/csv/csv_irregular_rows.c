/**
 * @file
 *
 * Irregular rows support example.
 *
 * This example demonstrates:
 * - Enabling irregular rows mode and appending rows with different field counts
 * - Parsing irregular CSV and normalizing it
 * - Column insertion with padding for short rows
 * - Using validation functions to check table structure
 * - Write trimming to remove trailing empty fields
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

static void print_table(GTEXT_CSV_Table * table, const char * title) {
  printf("\n=== %s ===\n", title);
  size_t row_count = gtext_csv_row_count(table);
  size_t col_count = gtext_csv_col_count(table, 0);

  printf("Row count: %zu, Column count (max): %zu\n", row_count, col_count);
  printf("Has irregular rows: %s\n",
      gtext_csv_has_irregular_rows(table) ? "yes" : "no");
  printf("Max column count: %zu, Min column count: %zu\n",
      gtext_csv_max_col_count(table), gtext_csv_min_col_count(table));

  for (size_t row = 0; row < row_count; row++) {
    size_t row_cols = gtext_csv_col_count(table, row);
    printf("Row %zu (%zu columns): ", row, row_cols);
    for (size_t col = 0; col < row_cols; col++) {
      size_t field_len;
      const char * field = gtext_csv_field(table, row, col, &field_len);
      if (field) {
        if (field_len == 0) {
          printf("[<empty>]");
        } else {
          printf("[%.*s]", (int)field_len, field);
        }
      } else {
        printf("[NULL]");
      }
      if (col < row_cols - 1) {
        printf(", ");
      }
    }
    printf("\n");
  }
}

int main(void) {
  printf("=== CSV Irregular Rows Example ===\n");

  // ============================================================
  // Example 1: Enabling irregular rows and appending rows
  // ============================================================
  printf("\n--- Example 1: Irregular Rows Mode ---\n");

  GTEXT_CSV_Table * table = gtext_csv_new_table();
  if (!table) {
    fprintf(stderr, "Failed to create table\n");
    return 1;
  }

  // Enable irregular rows mode
  gtext_csv_set_allow_irregular_rows(table, true);
  printf("Irregular rows mode enabled\n");

  // Append rows with different field counts
  const char * row1[] = {"Alice", "30"};
  const char * row2[] = {"Bob", "25", "LA", "extra"};
  const char * row3[] = {"Charlie"};

  GTEXT_CSV_Error err = {0};
  if (gtext_csv_row_append(table, row1, NULL, 2, &err) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to append row 1: %s\n", err.message);
    gtext_csv_error_free(&err);
    gtext_csv_free_table(table);
    return 1;
  }

  if (gtext_csv_row_append(table, row2, NULL, 4, NULL) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to append row 2\n");
    gtext_csv_free_table(table);
    return 1;
  }

  if (gtext_csv_row_append(table, row3, NULL, 1, NULL) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to append row 3\n");
    gtext_csv_free_table(table);
    return 1;
  }

  print_table(table, "After appending irregular rows");

  // ============================================================
  // Example 2: Parsing irregular CSV and normalizing
  // ============================================================
  printf("\n--- Example 2: Parsing and Normalizing Irregular CSV ---\n");

  const char * csv_input = "name,age,city\nAlice,30\nBob,25,LA,extra\nCharlie\n";
  GTEXT_CSV_Parse_Options parse_opts = gtext_csv_parse_options_default();
  parse_opts.dialect.treat_first_row_as_header = true;

  GTEXT_CSV_Table * parsed_table =
      gtext_csv_parse_table(csv_input, strlen(csv_input), &parse_opts, NULL);
  if (!parsed_table) {
    fprintf(stderr, "Failed to parse CSV\n");
    gtext_csv_free_table(table);
    return 1;
  }

  print_table(parsed_table, "Parsed irregular CSV (before normalization)");

  // Normalize to maximum column count
  if (gtext_csv_normalize_to_max(parsed_table) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to normalize table\n");
    gtext_csv_free_table(parsed_table);
    gtext_csv_free_table(table);
    return 1;
  }

  print_table(parsed_table, "After normalizing to maximum");

  gtext_csv_free_table(parsed_table);

  // ============================================================
  // Example 3: Column insertion with padding
  // ============================================================
  printf("\n--- Example 3: Column Insertion with Padding ---\n");

  // Create a table with irregular rows
  GTEXT_CSV_Table * padding_table = gtext_csv_new_table();
  gtext_csv_set_allow_irregular_rows(padding_table, true);

  const char * pad_row1[] = {"A", "B"};
  const char * pad_row2[] = {"X"};

  gtext_csv_row_append(padding_table, pad_row1, NULL, 2, NULL);
  gtext_csv_row_append(padding_table, pad_row2, NULL, 1, NULL);

  print_table(padding_table, "Before column insertion");

  // Insert column at index 2 (beyond the length of row 1)
  if (gtext_csv_column_insert(padding_table, 2, "NewCol", 0) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to insert column\n");
    gtext_csv_free_table(padding_table);
    gtext_csv_free_table(table);
    return 1;
  }

  print_table(padding_table, "After inserting column at index 2 (padding applied)");

  gtext_csv_free_table(padding_table);

  // ============================================================
  // Example 4: Using validation functions
  // ============================================================
  printf("\n--- Example 4: Validation Functions ---\n");

  // Validate the table we created earlier
  GTEXT_CSV_Status status = gtext_csv_validate_table(table);
  if (status == GTEXT_CSV_OK) {
    printf("Table validation: PASSED\n");
  } else {
    printf("Table validation: FAILED (status: %d)\n", status);
  }

  printf("Has irregular rows: %s\n",
      gtext_csv_has_irregular_rows(table) ? "yes" : "no");
  printf("Max column count: %zu\n", gtext_csv_max_col_count(table));
  printf("Min column count: %zu\n", gtext_csv_min_col_count(table));

  // ============================================================
  // Example 5: Write trimming
  // ============================================================
  printf("\n--- Example 5: Write Trimming ---\n");

  // Create a table with trailing empty fields
  GTEXT_CSV_Table * trim_table = gtext_csv_new_table();
  gtext_csv_set_allow_irregular_rows(trim_table, true);

  const char * trim_row1[] = {"Name", "Age", "City", "", ""};
  const char * trim_row2[] = {"Alice", "30", "NYC", "", ""};
  const char * trim_row3[] = {"Bob", "25", "", ""};

  gtext_csv_row_append(trim_table, trim_row1, NULL, 5, NULL);
  gtext_csv_row_append(trim_table, trim_row2, NULL, 5, NULL);
  gtext_csv_row_append(trim_table, trim_row3, NULL, 4, NULL);

  print_table(trim_table, "Before write trimming");

  // Write without trimming
  GTEXT_CSV_Sink sink1;
  gtext_csv_sink_buffer(&sink1);
  GTEXT_CSV_Write_Options write_opts1 = gtext_csv_write_options_default();
  write_opts1.trim_trailing_empty_fields = false;

  gtext_csv_write_table(&sink1, &write_opts1, trim_table);
  printf("\nOutput WITHOUT trimming:\n%.*s\n",
      (int)gtext_csv_sink_buffer_size(&sink1),
      gtext_csv_sink_buffer_data(&sink1));
  gtext_csv_sink_buffer_free(&sink1);

  // Write with trimming
  GTEXT_CSV_Sink sink2;
  gtext_csv_sink_buffer(&sink2);
  GTEXT_CSV_Write_Options write_opts2 = gtext_csv_write_options_default();
  write_opts2.trim_trailing_empty_fields = true;

  gtext_csv_write_table(&sink2, &write_opts2, trim_table);
  printf("\nOutput WITH trimming:\n%.*s\n",
      (int)gtext_csv_sink_buffer_size(&sink2),
      gtext_csv_sink_buffer_data(&sink2));
  gtext_csv_sink_buffer_free(&sink2);

  gtext_csv_free_table(trim_table);

  // ============================================================
  // Cleanup
  // ============================================================
  gtext_csv_free_table(table);

  printf("\n=== Example Complete ===\n");
  return 0;
}
