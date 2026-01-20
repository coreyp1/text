# CSV Module Examples

This directory contains example programs demonstrating how to use the CSV module API.

## Examples

### csv_basic.c
Basic CSV parsing and writing example. Demonstrates:
- Parsing CSV from a string
- Accessing values in the DOM table
- Writing CSV to a buffer
- Error handling

### csv_stream.c
Streaming parser example. Demonstrates:
- Using the streaming parser for incremental parsing
- Handling events from the streaming parser
- Processing large CSV files without building a full DOM

### csv_write.c
Writing CSV programmatically. Demonstrates:
- Creating CSV data using the streaming writer
- Building records and fields incrementally
- Writing to a buffer sink

### csv_dialects.c
CSV dialect examples. Demonstrates:
- Using different CSV dialects (TSV, semicolon-delimited, etc.)
- Configuring dialect options
- Parsing and writing with custom dialects

### csv_headers.c
CSV header processing example. Demonstrates:
- Parsing CSV with header row
- Looking up columns by header name
- Accessing data using header names

## Building Examples

The easiest way to build all examples is using the Makefile:

```bash
# Build the library and all examples
make examples
```

This will compile all examples to `build/linux/release/apps/examples/csv/` (or the appropriate build directory for your platform).

To run an example (on Linux):

```bash
LD_LIBRARY_PATH=build/linux/release/apps build/linux/release/apps/examples/csv/csv_basic
```

### Manual Compilation

If you prefer to compile examples manually:

```bash
# Build the library first
make

# Compile a specific example
gcc -std=c17 -I include/ -L build/linux/release/apps/ \
    examples/csv/csv_basic.c -lghoti.io-text-dev -o csv_basic

# Run (set LD_LIBRARY_PATH on Linux)
LD_LIBRARY_PATH=build/linux/release/apps ./csv_basic
```

Or use pkg-config if installed:

```bash
gcc -std=c17 $(pkg-config --cflags ghoti.io-text-dev) \
    examples/csv/csv_basic.c \
    $(pkg-config --libs ghoti.io-text-dev) \
    -o csv_basic
```

## Requirements

- C17-compatible compiler (gcc, clang)
- The ghoti.io-text library built and installed (or available in build directory)
- On Linux: may need to set `LD_LIBRARY_PATH` to point to the library location
