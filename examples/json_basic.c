/**
 * @file json_basic.c
 * @brief Basic JSON parsing and writing example
 *
 * This example demonstrates:
 * - Parsing JSON from a string
 * - Accessing values in the DOM
 * - Writing JSON to a buffer
 * - Error handling
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // JSON input string
    const char* json_input = "{\"name\":\"Alice\",\"age\":30,\"active\":true}";
    size_t json_len = strlen(json_input);

    // Parse options (use defaults)
    text_json_parse_options opt = text_json_parse_options_default();
    text_json_error err = {0};

    // Parse JSON
    text_json_value* root = text_json_parse(json_input, json_len, &opt, &err);
    if (!root) {
        fprintf(stderr, "Parse error: %s (at line %d, col %d)\n",
                err.message, err.line, err.col);
        if (err.context_snippet) {
            fprintf(stderr, "Context: %s\n", err.context_snippet);
        }
        text_json_error_free(&err);
        return 1;
    }

    // Access object values
    const text_json_value* name_val = text_json_object_get(root, "name", 4);
    if (name_val) {
        const char* name_str;
        size_t name_len;
        if (text_json_get_string(name_val, &name_str, &name_len) == TEXT_JSON_OK) {
            printf("Name: %.*s\n", (int)name_len, name_str);
        }
    }

    const text_json_value* age_val = text_json_object_get(root, "age", 3);
    if (age_val) {
        int64_t age;
        if (text_json_get_i64(age_val, &age) == TEXT_JSON_OK) {
            printf("Age: %lld\n", (long long)age);
        }
    }

    const text_json_value* active_val = text_json_object_get(root, "active", 6);
    if (active_val) {
        int active;
        if (text_json_get_bool(active_val, &active) == TEXT_JSON_OK) {
            printf("Active: %s\n", active ? "true" : "false");
        }
    }

    // Write JSON to buffer
    text_json_sink sink;
    if (text_json_sink_buffer(&sink) != TEXT_JSON_OK) {
        fprintf(stderr, "Failed to create buffer sink\n");
        text_json_free(root);
        return 1;
    }

    text_json_write_options write_opt = text_json_write_options_default();
    write_opt.pretty = 1;  // Pretty-print
    write_opt.indent_spaces = 2;

    if (text_json_write_value(&sink, &write_opt, root, &err) != TEXT_JSON_OK) {
        fprintf(stderr, "Write error: %s\n", err.message);
        text_json_error_free(&err);
        text_json_sink_buffer_free(&sink);
        text_json_free(root);
        return 1;
    }

    // Print output
    printf("\nPretty-printed JSON:\n%s\n", text_json_sink_buffer_data(&sink));

    // Cleanup
    text_json_sink_buffer_free(&sink);
    text_json_free(root);

    return 0;
}
