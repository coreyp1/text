/**
 * @file json_create.c
 * @brief Creating JSON values programmatically
 *
 * This example demonstrates:
 * - Creating JSON values from scratch
 * - Building arrays and objects
 * - Mutating the DOM
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  // Create a new object
  GTEXT_JSON_Value * obj = gtext_json_new_object();
  if (!obj) {
    fprintf(stderr, "Failed to create object\n");
    return 1;
  }

  // Add key-value pairs
  GTEXT_JSON_Value * name = gtext_json_new_string("Bob", 3);
  gtext_json_object_put(obj, "name", 4, name);

  GTEXT_JSON_Value * age = gtext_json_new_number_i64(25);
  gtext_json_object_put(obj, "age", 3, age);

  GTEXT_JSON_Value * active = gtext_json_new_bool(true);
  gtext_json_object_put(obj, "active", 6, active);

  // Create an array
  GTEXT_JSON_Value * hobbies = gtext_json_new_array();
  gtext_json_array_push(hobbies, gtext_json_new_string("reading", 7));
  gtext_json_array_push(hobbies, gtext_json_new_string("coding", 6));
  gtext_json_array_push(hobbies, gtext_json_new_string("music", 5));
  gtext_json_object_put(obj, "hobbies", 7, hobbies);

  // Create nested object
  GTEXT_JSON_Value * address = gtext_json_new_object();
  gtext_json_object_put(
      address, "street", 6, gtext_json_new_string("123 Main St", 11));
  gtext_json_object_put(
      address, "city", 4, gtext_json_new_string("Anytown", 7));
  gtext_json_object_put(address, "zip", 3, gtext_json_new_string("12345", 5));
  gtext_json_object_put(obj, "address", 7, address);

  // Write to buffer
  GTEXT_JSON_Sink sink;
  if (gtext_json_sink_buffer(&sink) != GTEXT_JSON_OK) {
    fprintf(stderr, "Failed to create buffer sink\n");
    gtext_json_free(obj);
    return 1;
  }

  GTEXT_JSON_Write_Options write_opt = gtext_json_write_options_default();
  write_opt.pretty = true;
  write_opt.indent_spaces = 2;

  GTEXT_JSON_Error err = {0};
  if (gtext_json_write_value(&sink, &write_opt, obj, &err) != GTEXT_JSON_OK) {
    fprintf(stderr, "Write error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(obj);
    return 1;
  }

  printf("Created JSON:\n%s\n", gtext_json_sink_buffer_data(&sink));

  // Cleanup
  gtext_json_sink_buffer_free(&sink);
  gtext_json_free(obj);

  return 0;
}
