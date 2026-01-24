/**
 * @file csv_file_io.c
 * @brief CSV file I/O example demonstrating option changes and mutations
 *
 * This example demonstrates:
 * - Parsing CSV from a hard-coded string
 * - Modifying the CSV table (adding rows, modifying fields, adding columns)
 * - Writing CSV to a file with specific options (trailing newline)
 * - Reading CSV back from the file
 * - Printing CSV with different options (different formatting)
 *
 * This code is cross-platform and uses standard C file I/O.
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Custom write callback for FILE*
static text_csv_status file_write_fn(void* user, const char* bytes, size_t len) {
    FILE* file = (FILE*)user;
    size_t written = fwrite(bytes, 1, len, file);
    return (written == len) ? TEXT_CSV_OK : TEXT_CSV_E_WRITE;
}

int main(void) {
    const char* filename = "example.output.csv";

    // ========================================================================
    // Step 1: Parse CSV from a hard-coded string
    // ========================================================================
    printf("=== Step 1: Parsing CSV from string ===\n");
    const char* csv_input = "Name,Age,City\nAlice,30,New York\nBob,25,San Francisco\nCharlie,35,Chicago";
    size_t csv_len = strlen(csv_input);

    text_csv_parse_options parse_opt = text_csv_parse_options_default();
    parse_opt.dialect.treat_first_row_as_header = true;  // Enable header processing
    text_csv_error err = {0};

    text_csv_table* table = text_csv_parse_table(csv_input, csv_len, &parse_opt, &err);
    if (!table) {
        fprintf(stderr, "Parse error: %s (at line %d, column %d)\n",
                err.message, err.line, err.column);
        if (err.context_snippet) {
            fprintf(stderr, "Context: %s\n", err.context_snippet);
        }
        text_csv_error_free(&err);
        return 1;
    }

    size_t row_count = text_csv_row_count(table);
    printf("Parsed %zu rows successfully\n\n", row_count);

    // ========================================================================
    // Step 2: Modify the CSV table (add rows, modify fields, add columns)
    // ========================================================================
    printf("=== Step 2: Modifying CSV table ===\n");

    // Modify an existing field
    text_csv_field_set(table, 0, 1, "31", 0);  // Change Alice's age from 30 to 31
    printf("Modified field: row 0, column 1 (Alice's age) -> 31\n");

    // Add a new row
    const char* new_row_fields[] = {"David", "28", "Boston"};
    text_csv_row_append(table, new_row_fields, NULL, 3);
    printf("Added new row: David, 28, Boston\n");

    // Add a new column (Country)
    text_csv_column_append(table, "Country", 0);
    printf("Added new column: Country\n");

    // Set values for the new column in existing rows
    text_csv_field_set(table, 0, 3, "USA", 0);  // Alice
    text_csv_field_set(table, 1, 3, "USA", 0);  // Bob
    text_csv_field_set(table, 2, 3, "USA", 0);  // Charlie
    text_csv_field_set(table, 3, 3, "USA", 0);  // David
    printf("Set Country column values for all rows\n\n");

    // ========================================================================
    // Step 3: Write CSV to file with trailing newline enabled
    // ========================================================================
    printf("=== Step 3: Writing CSV to file (with trailing newline) ===\n");
    // Remove existing file to ensure overwrite (cross-platform)
    remove(filename);

    FILE* output_file = fopen(filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Failed to open file %s for writing\n", filename);
        text_csv_free_table(table);
        return 1;
    }

    text_csv_sink file_sink;
    file_sink.write = file_write_fn;
    file_sink.user = output_file;

    text_csv_write_options write_opt = text_csv_write_options_default();
    write_opt.trailing_newline = true;  // Enable trailing newline for file output

    text_csv_status status = text_csv_write_table(&file_sink, &write_opt, table);
    fclose(output_file);

    if (status != TEXT_CSV_OK) {
        fprintf(stderr, "Write error: %d\n", status);
        text_csv_free_table(table);
        return 1;
    }

    printf("Successfully wrote CSV to %s\n\n", filename);

    // Clean up the original table
    text_csv_free_table(table);
    table = NULL;

    // ========================================================================
    // Step 4: Read CSV back from file
    // ========================================================================
    printf("=== Step 4: Reading CSV from file ===\n");
    FILE* input_file = fopen(filename, "rb");
    if (!input_file) {
        fprintf(stderr, "Failed to open file %s for reading\n", filename);
        return 1;
    }

    // Read entire file into buffer
    fseek(input_file, 0, SEEK_END);
    long file_size = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);

    if (file_size < 0) {
        fprintf(stderr, "Failed to determine file size\n");
        fclose(input_file);
        return 1;
    }

    char* file_buffer = (char*)malloc(file_size + 1);
    if (!file_buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        fclose(input_file);
        return 1;
    }

    size_t bytes_read = fread(file_buffer, 1, file_size, input_file);
    fclose(input_file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Failed to read entire file\n");
        free(file_buffer);
        return 1;
    }

    file_buffer[file_size] = '\0';

    // Parse with same options as step 1 (with header processing)
    text_csv_parse_options read_parse_opt = text_csv_parse_options_default();
    read_parse_opt.dialect.treat_first_row_as_header = true;  // Enable header processing
    text_csv_error read_err = {0};

    table = text_csv_parse_table(file_buffer, file_size, &read_parse_opt, &read_err);
    free(file_buffer);

    if (!table) {
        fprintf(stderr, "Parse error reading file: %s (at line %d, column %d)\n",
                read_err.message, read_err.line, read_err.column);
        if (read_err.context_snippet) {
            fprintf(stderr, "Context: %s\n", read_err.context_snippet);
        }
        text_csv_error_free(&read_err);
        return 1;
    }

    printf("Successfully read %zu rows from file\n\n", text_csv_row_count(table));

    // ========================================================================
    // Step 5: Print CSV to stdout with different options (quote all fields)
    // ========================================================================
    printf("=== Step 5: Printing CSV to stdout (with quote_all_fields) ===\n");
    text_csv_sink stdout_sink;
    if (text_csv_sink_buffer(&stdout_sink) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to create buffer sink\n");
        text_csv_free_table(table);
        return 1;
    }

    text_csv_write_options print_opt = text_csv_write_options_default();
    print_opt.quote_all_fields = true;  // Quote all fields for display
    print_opt.trailing_newline = false; // No trailing newline for display

    status = text_csv_write_table(&stdout_sink, &print_opt, table);
    if (status != TEXT_CSV_OK) {
        fprintf(stderr, "Write error: %d\n", status);
        text_csv_sink_buffer_free(&stdout_sink);
        text_csv_free_table(table);
        return 1;
    }

    printf("CSV output (all fields quoted):\n");
    printf("%.*s\n", (int)text_csv_sink_buffer_size(&stdout_sink),
           text_csv_sink_buffer_data(&stdout_sink));

    // Cleanup
    text_csv_sink_buffer_free(&stdout_sink);
    text_csv_free_table(table);

    printf("\n=== Example complete ===\n");
    printf("File %s has been created. You can inspect it to see the trailing newline.\n", filename);

    return 0;
}
