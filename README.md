# Ghoti.io Text Library

A C library for parsing and serializing text-based file formats, including JSON, CSV, and configuration formats.

## Overview

The `text` library provides:
- JSON parsing and serialization
- CSV reader/writer
- Config format support (INI/TOML-like)
- Text encoding helpers

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

## Documentation

- [Modules](@ref modules) - Detailed documentation for JSON and CSV modules
- [Function Index](@ref functions_index) - Complete API reference

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
