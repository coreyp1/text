/**
 * @file json_create.c
 * @brief Creating JSON values programmatically
 *
 * This example demonstrates:
 * - Creating JSON values from scratch
 * - Building arrays and objects
 * - Mutating the DOM
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Create a new object
    text_json_value* obj = text_json_new_object();
    if (!obj) {
        fprintf(stderr, "Failed to create object\n");
        return 1;
    }

    // Add key-value pairs
    text_json_value* name = text_json_new_string("Bob", 3);
    text_json_object_put(obj, "name", 4, name);

    text_json_value* age = text_json_new_number_i64(25);
    text_json_object_put(obj, "age", 3, age);

    text_json_value* active = text_json_new_bool(true);
    text_json_object_put(obj, "active", 6, active);

    // Create an array
    text_json_value* hobbies = text_json_new_array();
    text_json_array_push(hobbies, text_json_new_string("reading", 7));
    text_json_array_push(hobbies, text_json_new_string("coding", 6));
    text_json_array_push(hobbies, text_json_new_string("music", 5));
    text_json_object_put(obj, "hobbies", 7, hobbies);

    // Create nested object
    text_json_value* address = text_json_new_object();
    text_json_object_put(address, "street", 6, text_json_new_string("123 Main St", 11));
    text_json_object_put(address, "city", 4, text_json_new_string("Anytown", 7));
    text_json_object_put(address, "zip", 3, text_json_new_string("12345", 5));
    text_json_object_put(obj, "address", 7, address);

    // Write to buffer
    text_json_sink sink;
    if (text_json_sink_buffer(&sink) != TEXT_JSON_OK) {
        fprintf(stderr, "Failed to create buffer sink\n");
        text_json_free(obj);
        return 1;
    }

    text_json_write_options write_opt = text_json_write_options_default();
    write_opt.pretty = true;
    write_opt.indent_spaces = 2;

    text_json_error err = {0};
    if (text_json_write_value(&sink, &write_opt, obj, &err) != TEXT_JSON_OK) {
        fprintf(stderr, "Write error: %s\n", err.message);
        text_json_error_free(&err);
        text_json_sink_buffer_free(&sink);
        text_json_free(obj);
        return 1;
    }

    printf("Created JSON:\n%s\n", text_json_sink_buffer_data(&sink));

    // Cleanup
    text_json_sink_buffer_free(&sink);
    text_json_free(obj);

    return 0;
}
