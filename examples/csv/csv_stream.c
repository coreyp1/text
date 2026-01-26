/**
 * @file csv_stream.c
 * @brief Streaming parser example
 *
 * This example demonstrates:
 * - Using the streaming parser for incremental parsing
 * - Handling events from the streaming parser
 * - Processing large CSV files without building a full DOM
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

// Event callback function
static GTEXT_CSV_Status event_callback(
    const GTEXT_CSV_Event * event, void * user_data) {
  (void)user_data; // Unused in this example

  switch (event->type) {
  case GTEXT_CSV_EVENT_RECORD_BEGIN:
    printf("Record %zu: [", event->row_index);
    break;

  case GTEXT_CSV_EVENT_FIELD:
    if (event->col_index > 0) {
      printf(", ");
    }
    printf("%.*s", (int)event->data_len, event->data);
    break;

  case GTEXT_CSV_EVENT_RECORD_END:
    printf("]\n");
    break;

  case GTEXT_CSV_EVENT_END:
    printf("End of CSV data\n");
    break;
  }

  return GTEXT_CSV_OK;
}

int main(void) {
  // CSV input (can be processed in chunks)
  const char * csv_input =
      "Name,Age,City\nAlice,30,New York\nBob,25,San Francisco";
  size_t csv_len = strlen(csv_input);

  // Create streaming parser
  GTEXT_CSV_Parse_Options opt = gtext_csv_parse_options_default();
  GTEXT_CSV_Stream * stream = gtext_csv_stream_new(&opt, event_callback, NULL);
  if (!stream) {
    fprintf(stderr, "Failed to create stream\n");
    return 1;
  }

  printf("Streaming parser events:\n");

  // Feed input (can be done in chunks)
  GTEXT_CSV_Error err = {0};
  if (gtext_csv_stream_feed(stream, csv_input, csv_len, &err) != GTEXT_CSV_OK) {
    fprintf(stderr, "Feed error: %s (at line %d, column %d)\n", err.message,
        err.line, err.column);
    if (err.context_snippet) {
      fprintf(stderr, "Context: %s\n", err.context_snippet);
    }
    gtext_csv_error_free(&err);
    gtext_csv_stream_free(stream);
    return 1;
  }

  // Finish parsing
  if (gtext_csv_stream_finish(stream, &err) != GTEXT_CSV_OK) {
    fprintf(stderr, "Finish error: %s\n", err.message);
    gtext_csv_error_free(&err);
    gtext_csv_stream_free(stream);
    return 1;
  }

  // Cleanup
  gtext_csv_stream_free(stream);

  return 0;
}
