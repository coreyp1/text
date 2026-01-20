/**
 * @file csv_basic.c
 * @brief Basic CSV parsing and writing example
 *
 * This example demonstrates:
 * - Parsing CSV from a string
 * - Accessing values in the DOM table
 * - Writing CSV to a buffer
 * - Error handling
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // CSV input string
    const char* csv_input = "Name,Age,City\nAlice,30,New York\nBob,25,San Francisco\nCharlie,35,Chicago";
    size_t csv_len = strlen(csv_input);

    // Parse options (use defaults)
    text_csv_parse_options opt = text_csv_parse_options_default();
    text_csv_error err = {0};

    // Parse CSV
    text_csv_table* table = text_csv_parse_table(csv_input, csv_len, &opt, &err);
    if (!table) {
        fprintf(stderr, "Parse error: %s (at line %d, column %d)\n",
                err.message, err.line, err.column);
        if (err.context_snippet) {
            fprintf(stderr, "Context: %s\n", err.context_snippet);
        }
        text_csv_error_free(&err);
        return 1;
    }

    // Access table data
    size_t row_count = text_csv_row_count(table);
    printf("Number of rows: %zu\n", row_count);

    // Print all rows
    for (size_t row = 0; row < row_count; row++) {
        size_t col_count = text_csv_col_count(table, row);
        printf("Row %zu (%zu columns): ", row, col_count);

        for (size_t col = 0; col < col_count; col++) {
            size_t field_len;
            const char* field = text_csv_field(table, row, col, &field_len);
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
    text_csv_sink sink;
    if (text_csv_sink_buffer(&sink) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to create buffer sink\n");
        text_csv_free_table(table);
        return 1;
    }

    text_csv_write_options write_opt = text_csv_write_options_default();

    if (text_csv_write_table(&sink, &write_opt, table) != TEXT_CSV_OK) {
        fprintf(stderr, "Write error\n");
        text_csv_sink_buffer_free(&sink);
        text_csv_free_table(table);
        return 1;
    }

    // Print output
    printf("\nSerialized CSV:\n%.*s\n", (int)text_csv_sink_buffer_size(&sink),
           text_csv_sink_buffer_data(&sink));

    // Cleanup
    text_csv_sink_buffer_free(&sink);
    text_csv_free_table(table);

    return 0;
}
