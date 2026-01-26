/**
 * @file json_file_io.c
 * @brief JSON file I/O example demonstrating option changes between steps
 *
 * This example demonstrates:
 * - Parsing JSON from a hard-coded string (strict mode, no trailing commas)
 * - Modifying the JSON object (adding entries)
 * - Writing JSON to a file with compact output and sorted keys
 * - Reading JSON back from the file (allowing trailing commas for lenient
 * parsing)
 * - Printing JSON with pretty printing and different formatting options
 *
 * Note: Trailing commas are a parse-time extension only. The JSON writer
 * outputs valid JSON without trailing commas, as trailing commas are not
 * valid in standard JSON.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Custom write callback for FILE*
static int file_write_fn(void * user, const char * bytes, size_t len) {
  FILE * file = (FILE *)user;
  size_t written = fwrite(bytes, 1, len, file);
  return (written == len) ? 0 : -1;
}

int main(void) {
  const char * filename = "example.output.json";

  // ========================================================================
  // Step 1: Parse JSON from a hard-coded string (strict mode, no trailing
  // commas)
  // ========================================================================
  printf("=== Step 1: Parsing JSON from string (strict mode) ===\n");
  // Note: This JSON does NOT have trailing commas (strict JSON)
  const char * json_input =
      "{\"name\":\"Alice\",\"age\":30,\"city\":\"New York\",\"active\":true}";
  size_t json_len = strlen(json_input);

  GTEXT_JSON_Parse_Options parse_opt = gtext_json_parse_options_default();
  parse_opt.allow_trailing_commas = false; // Strict mode - no trailing commas
  GTEXT_JSON_Error err = {0};

  GTEXT_JSON_Value * root =
      gtext_json_parse(json_input, json_len, &parse_opt, &err);
  if (!root) {
    fprintf(stderr, "Parse error: %s (at line %d, col %d)\n", err.message,
        err.line, err.col);
    if (err.context_snippet) {
      fprintf(stderr, "Context: %s\n", err.context_snippet);
    }
    gtext_json_error_free(&err);
    return 1;
  }

  printf(
      "Parsed JSON successfully (strict mode, no trailing commas allowed)\n");

  // Access a value to verify parsing
  const GTEXT_JSON_Value * name_val = gtext_json_object_get(root, "name", 4);
  if (name_val) {
    const char * name_str;
    size_t name_len;
    if (gtext_json_get_string(name_val, &name_str, &name_len) ==
        GTEXT_JSON_OK) {
      printf("Found name: %.*s\n\n", (int)name_len, name_str);
    }
  }

  // ========================================================================
  // Step 2: Modify the JSON object (add new entries)
  // ========================================================================
  printf("=== Step 2: Modifying JSON object (adding entries) ===\n");

  // Add a new string field
  GTEXT_JSON_Value * country = gtext_json_new_string("USA", 3);
  gtext_json_object_put(root, "country", 7, country);

  // Add a new number field
  GTEXT_JSON_Value * score = gtext_json_new_number_i64(95);
  gtext_json_object_put(root, "score", 5, score);

  // Add a new array field
  GTEXT_JSON_Value * tags = gtext_json_new_array();
  gtext_json_array_push(tags, gtext_json_new_string("developer", 9));
  gtext_json_array_push(tags, gtext_json_new_string("senior", 6));
  gtext_json_object_put(root, "tags", 4, tags);

  printf("Added fields: country, score, tags\n\n");

  // ========================================================================
  // Step 3: Write JSON to file with compact output and sorted keys
  // ========================================================================
  printf(
      "=== Step 3: Writing JSON to file (compact format, sorted keys) ===\n");
  // Remove existing file to ensure overwrite (cross-platform)
  remove(filename);

  FILE * output_file = fopen(filename, "wb");
  if (!output_file) {
    fprintf(stderr, "Failed to open file %s for writing\n", filename);
    gtext_json_free(root);
    return 1;
  }

  GTEXT_JSON_Sink file_sink;
  file_sink.write = file_write_fn;
  file_sink.user = output_file;

  GTEXT_JSON_Write_Options write_opt = gtext_json_write_options_default();
  write_opt.pretty = false;          // Compact output (no pretty printing)
  write_opt.sort_object_keys = true; // Sort keys for deterministic output
  write_opt.trailing_newline = true; // Add trailing newline for file

  GTEXT_JSON_Status status =
      gtext_json_write_value(&file_sink, &write_opt, root, &err);
  fclose(output_file);

  if (status != GTEXT_JSON_OK) {
    fprintf(stderr, "Write error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_free(root);
    return 1;
  }

  printf("Successfully wrote JSON to %s (compact format, sorted keys)\n\n",
      filename);

  // Clean up the original object
  gtext_json_free(root);
  root = NULL;

  // ========================================================================
  // Step 4: Read JSON back from file (lenient parsing with trailing commas
  // allowed)
  // ========================================================================
  printf("=== Step 4: Reading JSON from file (lenient mode, trailing commas "
         "allowed) ===\n");
  FILE * input_file = fopen(filename, "rb");
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

  char * file_buffer = (char *)malloc(file_size + 1);
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

  // Parse with different options - now allow trailing commas (more lenient)
  // Note: The file doesn't contain trailing commas (valid JSON), but we
  // demonstrate that the parser can handle them if present
  GTEXT_JSON_Parse_Options read_parse_opt = gtext_json_parse_options_default();
  read_parse_opt.allow_trailing_commas =
      true; // Allow trailing commas when reading
  GTEXT_JSON_Error read_err = {0};

  root = gtext_json_parse(file_buffer, file_size, &read_parse_opt, &read_err);
  free(file_buffer);

  if (!root) {
    fprintf(stderr, "Parse error reading file: %s (at line %d, col %d)\n",
        read_err.message, read_err.line, read_err.col);
    if (read_err.context_snippet) {
      fprintf(stderr, "Context: %s\n", read_err.context_snippet);
    }
    gtext_json_error_free(&read_err);
    return 1;
  }

  printf("Successfully read JSON from file (with trailing commas allowed)\n\n");

  // ========================================================================
  // Step 5: Print JSON to stdout with pretty printing enabled
  // ========================================================================
  printf("=== Step 5: Printing JSON to stdout (pretty format) ===\n");
  GTEXT_JSON_Sink stdout_sink;
  if (gtext_json_sink_buffer(&stdout_sink) != GTEXT_JSON_OK) {
    fprintf(stderr, "Failed to create buffer sink\n");
    gtext_json_free(root);
    return 1;
  }

  GTEXT_JSON_Write_Options print_opt = gtext_json_write_options_default();
  print_opt.pretty = true;            // Enable pretty printing
  print_opt.indent_spaces = 2;        // 2-space indentation
  print_opt.space_after_colon = true; // Add space after colon for readability
  print_opt.sort_object_keys =
      false; // Preserve insertion order (different from file)
  print_opt.trailing_newline = false; // No trailing newline for display

  status = gtext_json_write_value(&stdout_sink, &print_opt, root, &read_err);
  if (status != GTEXT_JSON_OK) {
    fprintf(stderr, "Write error: %s\n", read_err.message);
    gtext_json_error_free(&read_err);
    gtext_json_sink_buffer_free(&stdout_sink);
    gtext_json_free(root);
    return 1;
  }

  printf("JSON output (pretty printed):\n");
  printf("%s\n", gtext_json_sink_buffer_data(&stdout_sink));

  // Cleanup
  gtext_json_sink_buffer_free(&stdout_sink);
  gtext_json_free(root);

  printf("\n=== Example complete ===\n");
  printf("File %s has been created. Compare the compact format in the file\n",
      filename);
  printf("(with sorted keys) with the pretty-printed format shown above\n");
  printf("(with spaces after colons and preserved insertion order).\n");
  printf("\nNote: Trailing commas are only supported during parsing, not "
         "writing,\n");
  printf("as they are not valid in standard JSON.\n");

  return 0;
}
