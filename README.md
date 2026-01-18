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

## Macros and Utilities

The library provides cross-compiler macros in `include/text/macros.h`:

- `TEXT_MAYBE_UNUSED(X)` - Mark unused function parameters
- `TEXT_DEPRECATED` - Mark deprecated functions
- `TEXT_API` - Mark functions for library export
- `TEXT_ARRAY_SIZE(a)` - Get compile-time array size
- `TEXT_BIT(x)` - Create bitmask with bit x set

Example:
```c
#include <text/macros.h>

void my_function(int TEXT_MAYBE_UNUSED(param)) {
    // param is intentionally unused
}
```
