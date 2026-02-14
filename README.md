# Ghoti.io Text Library

A C library for parsing and serializing text-based file formats, including JSON, CSV, YAML, and configuration formats.

## Overview

The `text` library provides:
- **JSON** - Fast, spec-compliant JSON parsing and serialization
- **CSV** - RFC 4180 compliant CSV reader/writer
- **YAML** - Streaming YAML 1.2 parser (DOM builder in progress)
- **Config** - INI/TOML-like configuration format support
- **Text encoding** - UTF-8 validation and encoding helpers

## Dependencies

- `cutil` - Core utility library (required)

## Building

```bash
make
```

## Testing

```bash
make test
```

## Installation

```bash
sudo make install
```

## Usage

See the examples directory for usage examples.

### YAML File I/O Example

```c
#include <ghoti.io/text/yaml.h>

GTEXT_YAML_Error err = {};
GTEXT_YAML_Document *doc = gtext_yaml_parse_file("config.yaml", NULL, &err);
if (!doc) {
    /* Handle parse error */
}

/* Modify doc or inspect values here */

GTEXT_YAML_Status status = gtext_yaml_write_file("out.yaml", doc, NULL, &err);
gtext_yaml_free(doc);
```

## Documentation

- [Modules](@ref modules) - Detailed documentation for JSON, CSV, and YAML modules
- [Examples](@ref examples) - Example programs demonstrating library usage
- [Function Index](@ref functions_index) - Complete API reference

## Module Status

### JSON (Production Ready)
- ✅ Complete DOM parser and serializer
- ✅ Streaming parser and writer
- ✅ Comprehensive test coverage (100+ tests)
- ✅ Zero memory leaks verified with valgrind
- ✅ Full JSON spec compliance

### CSV (Production Ready)
- ✅ RFC 4180 compliant reader and writer
- ✅ Configurable delimiters and quoting
- ✅ Comprehensive test coverage
- ✅ Memory safe and validated

### YAML (Alpha - In Active Development)
- ✅ Streaming parser with event callbacks
- ✅ UTF-8 validation
- ✅ Anchor/alias resolution with cycle detection
- ✅ Multi-document stream support
- ✅ Security limits (depth, bytes, alias expansion)
- ✅ Memory safe (781 tests pass valgrind with zero leaks)
- ✅ DOM builder with accessors and mutation
- ✅ Writer/serializer for DOM and streaming events
- ⏳ Full YAML 1.2 spec compliance (in progress)

See [YAML Module Documentation](@ref yaml_module) for detailed status and usage.

## Macros and Utilities

The library provides cross-compiler macros in `include/ghoti.io/text/macros.h`:

- `GTEXT_MAYBE_UNUSED(X)` - Mark unused function parameters
- `GTEXT_DEPRECATED` - Mark deprecated functions
- `GTEXT_API` - Mark functions for library export
- `GTEXT_ARRAY_SIZE(a)` - Get compile-time array size
- `GTEXT_BIT(x)` - Create bitmask with bit x set

Example:
```c
#include <ghoti.io/text/macros.h>

void my_function(int GTEXT_MAYBE_UNUSED(param)) {
    // param is intentionally unused
}
```
