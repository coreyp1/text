@page example_json_create json_create.c - Creating JSON Programmatically

# json_create.c - Creating JSON Programmatically

This example demonstrates how to build JSON data structures from scratch in your code, rather than parsing existing JSON. It shows creating objects, arrays, and nested structures.

## What This Example Demonstrates

- **Creating JSON values from scratch** - Build JSON data programmatically
- **Building arrays and objects** - Construct complex data structures
- **Mutating the DOM** - Add and modify values in JSON structures
- **Nested structures** - Create objects within objects

## Use Case

Use this example when you need to:
- Generate JSON configuration files
- Build JSON responses for APIs
- Create JSON data structures dynamically
- Transform data into JSON format

## Source Code

@include examples/json/json_create.c

## Key API Functions Used

- `gtext_json_new_object()` - Create a new empty JSON object
- `gtext_json_new_array()` - Create a new empty JSON array
- `gtext_json_new_string()` - Create a JSON string value
- `gtext_json_new_number_i64()` - Create a JSON number value
- `gtext_json_new_bool()` - Create a JSON boolean value
- `gtext_json_object_put()` - Add a key-value pair to an object
- `gtext_json_array_push()` - Add an element to an array
- `gtext_json_free()` - Free JSON DOM memory

## Related Examples

- [json_basic.c](@ref example_json_basic) - Basic parsing and writing
- [json_file_io.c](@ref example_json_file_io) - File I/O operations
- [Examples Overview](@ref examples) - Return to examples index
