/**
 * @file csv_stream.c
 * @brief Streaming parser example
 *
 * This example demonstrates:
 * - Using the streaming parser for incremental parsing
 * - Handling events from the streaming parser
 * - Processing large CSV files without building a full DOM
 */

#include <ghoti.io/text/csv.h>
#include <stdio.h>
#include <string.h>

// Event callback function
static text_csv_status event_callback(const text_csv_event* event, void* user_data) {
    (void)user_data;  // Unused in this example

    switch (event->type) {
        case TEXT_CSV_EVENT_RECORD_BEGIN:
            printf("Record %zu: [", event->row_index);
            break;

        case TEXT_CSV_EVENT_FIELD:
            if (event->col_index > 0) {
                printf(", ");
            }
            printf("%.*s", (int)event->data_len, event->data);
            break;

        case TEXT_CSV_EVENT_RECORD_END:
            printf("]\n");
            break;

        case TEXT_CSV_EVENT_END:
            printf("End of CSV data\n");
            break;
    }

    return TEXT_CSV_OK;
}

int main(void) {
    // CSV input (can be processed in chunks)
    const char* csv_input = "Name,Age,City\nAlice,30,New York\nBob,25,San Francisco";
    size_t csv_len = strlen(csv_input);

    // Create streaming parser
    text_csv_parse_options opt = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opt, event_callback, NULL);
    if (!stream) {
        fprintf(stderr, "Failed to create stream\n");
        return 1;
    }

    printf("Streaming parser events:\n");

    // Feed input (can be done in chunks)
    text_csv_error err = {0};
    if (text_csv_stream_feed(stream, csv_input, csv_len, &err) != TEXT_CSV_OK) {
        fprintf(stderr, "Feed error: %s (at line %d, column %d)\n",
                err.message, err.line, err.column);
        if (err.context_snippet) {
            fprintf(stderr, "Context: %s\n", err.context_snippet);
        }
        text_csv_error_free(&err);
        text_csv_stream_free(stream);
        return 1;
    }

    // Finish parsing
    if (text_csv_stream_finish(stream, &err) != TEXT_CSV_OK) {
        fprintf(stderr, "Finish error: %s\n", err.message);
        text_csv_error_free(&err);
        text_csv_stream_free(stream);
        return 1;
    }

    // Cleanup
    text_csv_stream_free(stream);

    return 0;
}
