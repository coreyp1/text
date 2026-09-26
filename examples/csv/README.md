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

### csv_irregular_rows.c
Irregular rows support example. Demonstrates:
- Enabling irregular rows mode and appending rows with different field counts
- Parsing irregular CSV and normalizing it
- Column insertion with padding for short rows
- Using validation functions to check table structure
- Write trimming to remove trailing empty fields

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

Against an installed library:

```bash
cc -o csv_basic examples/csv/csv_basic.c $(pkg-config --cflags --libs ghoti.io-text-0)
```

A line that only adds `-I include` cannot see cutil, chron or unicode.

## Requirements

- C17-compatible compiler (gcc, clang)
- The ghoti.io-text library, and cutil, chron and unicode, on `PKG_CONFIG_PATH`
- On Linux, `make examples` needs `LD_LIBRARY_PATH=build/linux/release/apps`
