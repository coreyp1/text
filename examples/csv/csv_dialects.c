/**
 * @file csv_dialects.c
 * @brief CSV dialect examples
 *
 * This example demonstrates:
 * - Using different CSV dialects (TSV, semicolon-delimited, etc.)
 * - Configuring dialect options
 * - Parsing and writing with custom dialects
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Example 1: TSV (Tab-Separated Values)
    printf("=== Example 1: TSV (Tab-Separated Values) ===\n");
    const char* tsv_input = "Name\tAge\tCity\nAlice\t30\tNew York\nBob\t25\tSan Francisco";
    size_t tsv_len = strlen(tsv_input);

    text_csv_parse_options tsv_parse_opt = text_csv_parse_options_default();
    tsv_parse_opt.dialect.delimiter = '\t';  // Tab delimiter

    text_csv_error err = {0};
    text_csv_table* tsv_table = text_csv_parse_table(tsv_input, tsv_len, &tsv_parse_opt, &err);
    if (!tsv_table) {
        fprintf(stderr, "TSV parse error: %s\n", err.message);
        text_csv_error_free(&err);
        return 1;
    }

    printf("Parsed TSV:\n");
    for (size_t row = 0; row < text_csv_row_count(tsv_table); row++) {
        for (size_t col = 0; col < text_csv_col_count(tsv_table, row); col++) {
            size_t len;
            const char* field = text_csv_field(tsv_table, row, col, &len);
            printf("  [%.*s]", (int)len, field);
        }
        printf("\n");
    }

    // Write TSV back
    text_csv_sink tsv_sink;
    if (text_csv_sink_buffer(&tsv_sink) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to create TSV sink\n");
        text_csv_free_table(tsv_table);
        return 1;
    }
    text_csv_write_options tsv_write_opt = text_csv_write_options_default();
    tsv_write_opt.dialect.delimiter = '\t';
    if (text_csv_write_table(&tsv_sink, &tsv_write_opt, tsv_table) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to write TSV\n");
        text_csv_sink_buffer_free(&tsv_sink);
        text_csv_free_table(tsv_table);
        return 1;
    }
    printf("Serialized TSV: %.*s\n\n", (int)text_csv_sink_buffer_size(&tsv_sink),
           text_csv_sink_buffer_data(&tsv_sink));
    text_csv_sink_buffer_free(&tsv_sink);
    text_csv_free_table(tsv_table);

    // Example 2: Semicolon-delimited CSV
    printf("=== Example 2: Semicolon-delimited CSV ===\n");
    const char* semicolon_input = "Name;Age;City\nAlice;30;New York\nBob;25;San Francisco";
    size_t semicolon_len = strlen(semicolon_input);

    text_csv_parse_options semicolon_parse_opt = text_csv_parse_options_default();
    semicolon_parse_opt.dialect.delimiter = ';';

    text_csv_table* semicolon_table = text_csv_parse_table(semicolon_input, semicolon_len,
                                                            &semicolon_parse_opt, &err);
    if (!semicolon_table) {
        fprintf(stderr, "Semicolon parse error: %s\n", err.message);
        text_csv_error_free(&err);
        return 1;
    }

    printf("Parsed semicolon-delimited CSV:\n");
    for (size_t row = 0; row < text_csv_row_count(semicolon_table); row++) {
        for (size_t col = 0; col < text_csv_col_count(semicolon_table, row); col++) {
            size_t len;
            const char* field = text_csv_field(semicolon_table, row, col, &len);
            printf("  [%.*s]", (int)len, field);
        }
        printf("\n");
    }
    text_csv_free_table(semicolon_table);

    // Example 3: CSV with backslash escaping
    printf("\n=== Example 3: CSV with backslash escaping ===\n");
    // Note: With backslash escaping mode, \" is parsed as a literal quote character
    // In C string literals: \\\" produces \" (backslash-quote) in the actual string
    // This matches the format in tests/data/csv/dialects/backslash-escape/basic.csv
    const char* backslash_input = "name,description\nAlice,\"She said \\\"Hello\\\"\"\nBob,\"He said \\\"Goodbye\\\"\"\n";
    size_t backslash_len = strlen(backslash_input);

    text_csv_parse_options backslash_parse_opt = text_csv_parse_options_default();
    backslash_parse_opt.dialect.escape = TEXT_CSV_ESCAPE_BACKSLASH;

    // Reinitialize error structure
    text_csv_error backslash_err = {0};
    text_csv_table* backslash_table = text_csv_parse_table(backslash_input, backslash_len,
                                                            &backslash_parse_opt, &backslash_err);
    if (!backslash_table) {
        fprintf(stderr, "Backslash parse error: %s (at line %d, column %d)\n",
                backslash_err.message, backslash_err.line, backslash_err.column);
        if (backslash_err.context_snippet) {
            fprintf(stderr, "Context: %s\n", backslash_err.context_snippet);
        }
        text_csv_error_free(&backslash_err);
        return 1;
    }

    printf("Parsed CSV with backslash escaping:\n");
    for (size_t row = 0; row < text_csv_row_count(backslash_table); row++) {
        for (size_t col = 0; col < text_csv_col_count(backslash_table, row); col++) {
            size_t len;
            const char* field = text_csv_field(backslash_table, row, col, &len);
            printf("  [%.*s]", (int)len, field);
        }
        printf("\n");
    }
    text_csv_free_table(backslash_table);

    return 0;
}
