/**
 * @file
 *
 * Copyright 2026 by Corey Pennycuff
 */
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
  GTEXT_CSV_Sink sink;
  if (gtext_csv_sink_buffer(&sink) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to create buffer sink\n");
    return 1;
  }

  // Create write options
  GTEXT_CSV_Write_Options write_opt = gtext_csv_write_options_default();

  // Create a CSV writer
  GTEXT_CSV_Writer * writer = gtext_csv_writer_new(&sink, &write_opt);
  if (!writer) {
    fprintf(stderr, "Failed to create writer\n");
    gtext_csv_sink_buffer_free(&sink);
    return 1;
  }

  // Write header row
  if (gtext_csv_writer_record_begin(writer) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to begin record\n");
    gtext_csv_writer_free(writer);
    gtext_csv_sink_buffer_free(&sink);
    return 1;
  }

  if (gtext_csv_writer_field(writer, "Name", 4) != GTEXT_CSV_OK ||
      gtext_csv_writer_field(writer, "Age", 3) != GTEXT_CSV_OK ||
      gtext_csv_writer_field(writer, "City", 4) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to write header fields\n");
    gtext_csv_writer_free(writer);
    gtext_csv_sink_buffer_free(&sink);
    return 1;
  }

  if (gtext_csv_writer_record_end(writer) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to end header record\n");
    gtext_csv_writer_free(writer);
    gtext_csv_sink_buffer_free(&sink);
    return 1;
  }

  // Write data rows
  const char * names[] = {"Alice", "Bob", "Charlie"};
  const char * ages[] = {"30", "25", "35"};
  const char * cities[] = {"New York", "San Francisco", "Chicago"};

  for (int i = 0; i < 3; i++) {
    if (gtext_csv_writer_record_begin(writer) != GTEXT_CSV_OK) {
      fprintf(stderr, "Failed to begin record %d\n", i);
      gtext_csv_writer_free(writer);
      gtext_csv_sink_buffer_free(&sink);
      return 1;
    }

    if (gtext_csv_writer_field(writer, names[i], strlen(names[i])) !=
            GTEXT_CSV_OK ||
        gtext_csv_writer_field(writer, ages[i], strlen(ages[i])) !=
            GTEXT_CSV_OK ||
        gtext_csv_writer_field(writer, cities[i], strlen(cities[i])) !=
            GTEXT_CSV_OK) {
      fprintf(stderr, "Failed to write fields for record %d\n", i);
      gtext_csv_writer_free(writer);
      gtext_csv_sink_buffer_free(&sink);
      return 1;
    }

    if (gtext_csv_writer_record_end(writer) != GTEXT_CSV_OK) {
      fprintf(stderr, "Failed to end record %d\n", i);
      gtext_csv_writer_free(writer);
      gtext_csv_sink_buffer_free(&sink);
      return 1;
    }
  }

  // Finish writing
  if (gtext_csv_writer_finish(writer) != GTEXT_CSV_OK) {
    fprintf(stderr, "Failed to finish writing\n");
    gtext_csv_writer_free(writer);
    gtext_csv_sink_buffer_free(&sink);
    return 1;
  }

  // Print output
  printf("Generated CSV:\n%.*s\n", (int)gtext_csv_sink_buffer_size(&sink),
      gtext_csv_sink_buffer_data(&sink));

  // Cleanup
  gtext_csv_writer_free(writer);
  gtext_csv_sink_buffer_free(&sink);

  return 0;
}
