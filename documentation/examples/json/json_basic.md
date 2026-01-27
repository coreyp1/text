@page example_json_basic json_basic.c - Basic JSON Parsing and Writing

# json_basic.c - Basic JSON Parsing and Writing

This example demonstrates the fundamental operations for working with JSON data: parsing JSON from a string, accessing values in the DOM, writing JSON to a buffer, and error handling.

## What This Example Demonstrates

- **Parsing JSON from a string** - Convert JSON text into a DOM structure
- **Accessing values in the DOM** - Extract string, number, and boolean values from parsed JSON
- **Writing JSON to a buffer** - Serialize JSON DOM back to text format
- **Error handling** - Proper error checking and reporting

## Use Case

This is the ideal starting point if you're new to the JSON module. It shows the basic workflow:
1. Parse JSON input
2. Access and use the data
3. Serialize back to JSON
4. Clean up resources

## Source Code

@include examples/json/json_basic.c

## Key API Functions Used

- `gtext_json_parse()` - Parse JSON text into a DOM
- `gtext_json_object_get()` - Get a value from a JSON object by key
- `gtext_json_get_string()` - Extract string value from JSON value
- `gtext_json_get_i64()` - Extract integer value from JSON value
- `gtext_json_get_bool()` - Extract boolean value from JSON value
- `gtext_json_sink_buffer()` - Create a buffer sink for writing
- `gtext_json_write_value()` - Serialize JSON value to text
- `gtext_json_free()` - Free JSON DOM memory

## Related Examples

- [json_create.c](@ref example_json_create) - Creating JSON programmatically
- [json_file_io.c](@ref example_json_file_io) - Reading and writing JSON files
- [Examples Overview](@ref examples) - Return to examples index
