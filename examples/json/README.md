# JSON Module Examples

This directory contains example programs demonstrating how to use the JSON module API.

## Examples

### json_basic.c
Basic JSON parsing and writing example. Demonstrates:
- Parsing JSON from a string
- Accessing values in the DOM
- Writing JSON to a buffer
- Error handling

### json_create.c
Creating JSON values programmatically. Demonstrates:
- Creating JSON values from scratch
- Building arrays and objects
- Mutating the DOM

### json_stream.c
Streaming parser example. Demonstrates:
- Using the streaming parser for incremental parsing
- Handling events from the streaming parser
- Processing large JSON documents without building a full DOM

### json_pointer.c
JSON Pointer (RFC 6901) example. Demonstrates:
- Using JSON Pointers to access nested values
- Reading and modifying values via pointers

### json_patch.c
JSON Patch (RFC 6902) and Merge Patch (RFC 7386) example. Demonstrates:
- Applying JSON Patch operations
- Applying JSON Merge Patch

### json_schema.c
JSON Schema validation example. Demonstrates:
- Compiling a JSON Schema
- Validating JSON values against a schema
- Handling validation errors

## Building Examples

The easiest way to build all examples is using the Makefile:

```bash
# Build the library and all examples
make examples
```

This will compile all examples to `build/linux/release/apps/examples/json/` (or the appropriate build directory for your platform).

To run an example (on Linux):

```bash
LD_LIBRARY_PATH=build/linux/release/apps build/linux/release/apps/examples/json/json_basic
```

### Manual Compilation

If you prefer to compile examples manually:

```bash
# Build the library first
make

# Compile a specific example
gcc -std=c17 -I include/ -L build/linux/release/apps/ \
    examples/json/json_basic.c -lghoti.io-text-dev -o json_basic

# Run (set LD_LIBRARY_PATH on Linux)
LD_LIBRARY_PATH=build/linux/release/apps ./json_basic
```

Or use pkg-config if installed:

```bash
gcc -std=c17 $(pkg-config --cflags ghoti.io-text-dev) \
    examples/json/json_basic.c \
    $(pkg-config --libs ghoti.io-text-dev) \
    -o json_basic
```

## Requirements

- C17-compatible compiler (gcc, clang)
- The ghoti.io-text library built and installed (or available in build directory)
- On Linux: may need to set `LD_LIBRARY_PATH` to point to the library location
