/**
 * @file csv_write.c
 * @brief Writing CSV programmatically
 *
 * This example demonstrates:
 * - Creating CSV data programmatically using the streaming writer
 * - Building records and fields incrementally
 * - Writing to a buffer sink
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Create a buffer sink for output
    text_csv_sink sink;
    if (text_csv_sink_buffer(&sink) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to create buffer sink\n");
        return 1;
    }

    // Create write options
    text_csv_write_options write_opt = text_csv_write_options_default();

    // Create a CSV writer
    text_csv_writer* writer = text_csv_writer_new(&sink, &write_opt);
    if (!writer) {
        fprintf(stderr, "Failed to create writer\n");
        text_csv_sink_buffer_free(&sink);
        return 1;
    }

    // Write header row
    if (text_csv_writer_record_begin(writer) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to begin record\n");
        text_csv_writer_free(writer);
        text_csv_sink_buffer_free(&sink);
        return 1;
    }

    if (text_csv_writer_field(writer, "Name", 4) != TEXT_CSV_OK ||
        text_csv_writer_field(writer, "Age", 3) != TEXT_CSV_OK ||
        text_csv_writer_field(writer, "City", 4) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to write header fields\n");
        text_csv_writer_free(writer);
        text_csv_sink_buffer_free(&sink);
        return 1;
    }

    if (text_csv_writer_record_end(writer) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to end header record\n");
        text_csv_writer_free(writer);
        text_csv_sink_buffer_free(&sink);
        return 1;
    }

    // Write data rows
    const char* names[] = {"Alice", "Bob", "Charlie"};
    const char* ages[] = {"30", "25", "35"};
    const char* cities[] = {"New York", "San Francisco", "Chicago"};

    for (int i = 0; i < 3; i++) {
        if (text_csv_writer_record_begin(writer) != TEXT_CSV_OK) {
            fprintf(stderr, "Failed to begin record %d\n", i);
            text_csv_writer_free(writer);
            text_csv_sink_buffer_free(&sink);
            return 1;
        }

        if (text_csv_writer_field(writer, names[i], strlen(names[i])) != TEXT_CSV_OK ||
            text_csv_writer_field(writer, ages[i], strlen(ages[i])) != TEXT_CSV_OK ||
            text_csv_writer_field(writer, cities[i], strlen(cities[i])) != TEXT_CSV_OK) {
            fprintf(stderr, "Failed to write fields for record %d\n", i);
            text_csv_writer_free(writer);
            text_csv_sink_buffer_free(&sink);
            return 1;
        }

        if (text_csv_writer_record_end(writer) != TEXT_CSV_OK) {
            fprintf(stderr, "Failed to end record %d\n", i);
            text_csv_writer_free(writer);
            text_csv_sink_buffer_free(&sink);
            return 1;
        }
    }

    // Finish writing
    if (text_csv_writer_finish(writer) != TEXT_CSV_OK) {
        fprintf(stderr, "Failed to finish writing\n");
        text_csv_writer_free(writer);
        text_csv_sink_buffer_free(&sink);
        return 1;
    }

    // Print output
    printf("Generated CSV:\n%.*s\n", (int)text_csv_sink_buffer_size(&sink),
           text_csv_sink_buffer_data(&sink));

    // Cleanup
    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);

    return 0;
}
