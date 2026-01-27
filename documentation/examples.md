@page examples Examples

# Examples

This page provides an overview of the example programs included with the Ghoti.io Text library. These examples demonstrate practical usage of the library's APIs and serve as starting points for your own projects.

---

## Overview

The examples are organized by module:

- **JSON Examples** - Demonstrate JSON parsing, writing, streaming, and advanced features
- **CSV Examples** - Demonstrate CSV reading, writing, streaming, and dialect handling

All examples are located in the `examples/` directory and can be built using the Makefile.

---

## JSON Module Examples

The JSON examples are located in `examples/json/` and demonstrate various aspects of the JSON module API.

@subpage example_json_basic

@subpage example_json_create

@subpage example_json_stream

@subpage example_json_file_io

@subpage example_json_pointer

@subpage example_json_patch

@subpage example_json_schema

---

## CSV Module Examples

The CSV examples are located in `examples/csv/` and demonstrate various aspects of the CSV module API.

@subpage example_csv_basic

@subpage example_csv_stream

@subpage example_csv_write

@subpage example_csv_file_io

@subpage example_csv_dialects

@subpage example_csv_headers

@subpage example_csv_irregular_rows

---

## Building Examples

### Using the Makefile (Recommended)

The easiest way to build all examples is using the Makefile:

```bash
# Build the library and all examples
make examples
```

This will compile all examples to `build/linux/release/apps/examples/` (or the appropriate build directory for your platform).

### Running Examples

To run an example (on Linux):

```bash
# JSON example
LD_LIBRARY_PATH=build/linux/release/apps \
    build/linux/release/apps/examples/json/json_basic

# CSV example
LD_LIBRARY_PATH=build/linux/release/apps \
    build/linux/release/apps/examples/csv/csv_basic
```

On other platforms, adjust the library path as needed for your system.

### Manual Compilation

If you prefer to compile examples manually:

```bash
# Build the library first
make

# Compile a specific example (JSON)
gcc -std=c17 -I include/ -L build/linux/release/apps/ \
    examples/json/json_basic.c -lghoti.io-text-dev -o json_basic

# Compile a specific example (CSV)
gcc -std=c17 -I include/ -L build/linux/release/apps/ \
    examples/csv/csv_basic.c -lghoti.io-text-dev -o csv_basic

# Run (set LD_LIBRARY_PATH on Linux)
LD_LIBRARY_PATH=build/linux/release/apps ./json_basic
```

### Using pkg-config

If the library is installed system-wide, you can use pkg-config:

```bash
# JSON example
gcc -std=c17 $(pkg-config --cflags ghoti.io-text-dev) \
    examples/json/json_basic.c \
    $(pkg-config --libs ghoti.io-text-dev) \
    -o json_basic

# CSV example
gcc -std=c17 $(pkg-config --cflags ghoti.io-text-dev) \
    examples/csv/csv_basic.c \
    $(pkg-config --libs ghoti.io-text-dev) \
    -o csv_basic
```

---

## Requirements

To build and run the examples, you need:

- **C17-compatible compiler** (GCC, Clang, or MSVC)
- **The ghoti.io-text library** built and installed (or available in the build directory)
- **On Linux:** May need to set `LD_LIBRARY_PATH` to point to the library location
- **On macOS:** May need to set `DYLD_LIBRARY_PATH` to point to the library location
- **On Windows:** The library should be in the system PATH or the same directory as the executable

---

## Getting Started

1. **Start with the basics:** Begin with `json_basic.c` or `csv_basic.c` to understand the fundamental API usage.

2. **Explore your use case:** Choose examples that match what you're trying to accomplish:
   - File I/O? → `json_file_io.c` or `csv_file_io.c`
   - Large data? → `json_stream.c` or `csv_stream.c`
   - Building data? → `json_create.c` or `csv_write.c`
   - Advanced features? → `json_pointer.c`, `json_patch.c`, `json_schema.c`, or `csv_dialects.c`

3. **Read the source:** Each example is well-commented and demonstrates best practices for using the library.

4. **Adapt to your needs:** Use the examples as templates and modify them for your specific requirements.

---

## Related Documentation

- [JSON Module](@ref json_module) - Complete JSON module documentation
- [CSV Module](@ref csv_module) - Complete CSV module documentation
- [Core Module](@ref core_module) - Core utilities and macros
- [Function Index](@ref functions_index) - Complete API reference
- [Main Documentation](@ref mainpage) - Library overview
