/**
 * @file
 *
 * Copyright 2026 by Corey Pennycuff
 */
/**
 * @file
 *
 * Basic JSON parsing and writing example.
 *
 * This example demonstrates:
 * - Parsing JSON from a string
 * - Accessing values in the DOM
 * - Writing JSON to a buffer
 * - Error handling
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  // JSON input string
  const char * json_input = "{\"name\":\"Alice\",\"age\":30,\"active\":true}";
  size_t json_len = strlen(json_input);

  // Parse options (use defaults)
  GTEXT_JSON_Parse_Options opt = gtext_json_parse_options_default();
  GTEXT_JSON_Error err = {0};

  // Parse JSON
  GTEXT_JSON_Value * root = gtext_json_parse(json_input, json_len, &opt, &err);
  if (!root) {
    fprintf(stderr, "Parse error: %s (at line %d, col %d)\n", err.message,
        err.line, err.col);
    if (err.context_snippet) {
      fprintf(stderr, "Context: %s\n", err.context_snippet);
    }
    gtext_json_error_free(&err);
    return 1;
  }

  // Access object values
  const GTEXT_JSON_Value * name_val = gtext_json_object_get(root, "name", 4);
  if (name_val) {
    const char * name_str;
    size_t name_len;
    if (gtext_json_get_string(name_val, &name_str, &name_len) ==
        GTEXT_JSON_OK) {
      printf("Name: %.*s\n", (int)name_len, name_str);
    }
  }

  const GTEXT_JSON_Value * age_val = gtext_json_object_get(root, "age", 3);
  if (age_val) {
    int64_t age;
    if (gtext_json_get_i64(age_val, &age) == GTEXT_JSON_OK) {
      printf("Age: %lld\n", (long long)age);
    }
  }

  const GTEXT_JSON_Value * active_val =
      gtext_json_object_get(root, "active", 6);
  if (active_val) {
    bool active;
    if (gtext_json_get_bool(active_val, &active) == GTEXT_JSON_OK) {
      printf("Active: %s\n", active ? "true" : "false");
    }
  }

  // Write JSON to buffer
  GTEXT_JSON_Sink sink;
  if (gtext_json_sink_buffer(&sink) != GTEXT_JSON_OK) {
    fprintf(stderr, "Failed to create buffer sink\n");
    gtext_json_free(root);
    return 1;
  }

  GTEXT_JSON_Write_Options write_opt = gtext_json_write_options_default();
  write_opt.pretty = true; // Pretty-print
  write_opt.indent_spaces = 2;

  if (gtext_json_write_value(&sink, &write_opt, root, &err) != GTEXT_JSON_OK) {
    fprintf(stderr, "Write error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(root);
    return 1;
  }

  // Print output
  printf("\nPretty-printed JSON:\n%s\n", gtext_json_sink_buffer_data(&sink));

  // Cleanup
  gtext_json_sink_buffer_free(&sink);
  gtext_json_free(root);

  return 0;
}
