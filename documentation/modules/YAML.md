@page yaml_module YAML Module Documentation

# YAML Module Documentation (ghoti.io)

This document describes the **YAML parsing library in C** implemented in the `text` library in the `ghoti.io` family. The implementation is **cross-platform**, **dependency-free** (libc only), and prioritizes **correctness** and **memory safety** over simplicity.

---

## 1. Overview

The YAML module provides YAML 1.2.2 processing capabilities with support for streaming parsing, anchors/aliases, comprehensive error handling, and writer/serialization.

### Current Status (February 2026)

**Implemented:**
- ✅ Streaming parser with event callbacks
- ✅ UTF-8 validation and handling
- ✅ Scalar styles (plain, single-quoted, double-quoted, literal `|`, folded `>`)
- ✅ Flow and block collections (sequences `[]` and mappings `{}`)
- ✅ Anchors (`&anchor`) and aliases (`*anchor`)
- ✅ Multi-document streams (`---` and `...`)
- ✅ Comprehensive limit enforcement
- ✅ Memory-safe operation (zero leaks, valgrind-clean)
- ✅ DOM parser with accessors, mutation, and cloning
- ✅ Writer/serialization for DOM and streaming events
- ✅ 951 comprehensive tests with real-world YAML examples
- ✅ `%YAML` and `%TAG` directives with tag handle resolution
- ✅ Schema-based implicit typing (Failsafe, JSON, Core)
- ✅ Standard type tags (`!!timestamp`, `!!set`, `!!omap`, `!!pairs`) with validation

**Planned:**
- ⏳ YAML 1.1 compatibility mode
- ⏳ Merge keys (`<<`)
- ⏳ Binary scalar support (`!!binary`)
- ⏳ Custom tag resolution for application-defined types

### Core Capabilities

- **Streaming parsing** with event callbacks for memory-efficient processing
- **Full UTF-8 support** with validation and proper encoding handling
- **Anchor/alias resolution** with cycle detection and expansion limits
- **Comprehensive error diagnostics** with position information
- **Configurable limits** for depth, bytes, and alias expansion
- **Chunked input support** for network or file I/O scenarios
- **Memory safety** with zero memory leaks (valgrind-verified)
---

## 2. Parsing Models
### 2.1 Streaming Parsing (Implemented)

Streaming parsing processes YAML incrementally, emitting events as nodes are encountered. This mode is ideal when you need to:

- Process large YAML documents with minimal memory usage
- Handle YAML from network streams or files
- Transform YAML on-the-fly without building a full DOM
- Process multi-document streams

The streaming parser accepts input in chunks and maintains state between calls, making it suitable for network or file I/O scenarios.

**Basic Usage:**

```c
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>

// Define event callback
GTEXT_YAML_Status my_callback(GTEXT_YAML_Stream *s, const void *event, void *user) {
    // Process event
    // event contains scalar values, sequence/mapping starts/ends, etc.
    return GTEXT_YAML_OK;
}

// Create stream parser
GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(&opts, my_callback, user_data);

// Feed input (can be called multiple times with chunks)
const char *yaml = "key: value\nlist: [1, 2, 3]\n";
GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
if (status != GTEXT_YAML_OK) {
    // Handle error
}

// Finish parsing (validates completeness)
status = gtext_yaml_stream_finish(stream);

// Cleanup
gtext_yaml_stream_free(stream);
```

opts.dupkeys = GTEXT_YAML_DUPKEY_LAST_WINS;  // Allow duplicate keys, last wins


### 3.3 Schema and Tag Resolution

The resolver supports schema-based implicit typing and explicit tag handling.

- **`schema`**: Selects the implicit typing rules.
  - `GTEXT_YAML_SCHEMA_FAILSAFE`: All scalars remain strings.
  - `GTEXT_YAML_SCHEMA_JSON`: JSON-compatible null/bool/int/float/string.
  - `GTEXT_YAML_SCHEMA_CORE`: YAML core schema (includes `.nan`, `.inf`, etc.).
- **`resolve_tags`**: When disabled, preserves explicit tags and keeps scalars as strings.
- **`yaml_1_1`**: When enabled, apply YAML 1.1 implicit typing (yes/no/on/off, 0755 octal, sexagesimal). This is also enabled automatically when a `%YAML 1.1` directive is present.

```c
GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
opts.schema = GTEXT_YAML_SCHEMA_JSON;
opts.resolve_tags = true;
```
The parser correctly handles values (strings, numbers, collections) that span multiple chunks. When a value is incomplete at the end of a chunk, the parser preserves state and waits for more input.

- **No chunk count limit**: Values can span 2, 3, 100, or more chunks
- **Total bytes limit**: Limited by `max_total_bytes` option (default: 64MB)
- **State preservation**: Incomplete values are buffered until completion
- **Examples**:
  - String: `"hello` (chunk 1) + ` world"` (chunk 2) → correctly parses as `"hello world"`
  - Number: `12345` (chunk 1) + `.678` (chunk 2) → correctly parses as `12345.678`
  - Collections spanning chunks work correctly with proper state tracking

**Important:** Always call `gtext_yaml_stream_finish()` after feeding all input chunks. The last value may not be emitted until `finish()` is called, especially if it was incomplete at the end of the final chunk.

### 2.2 DOM Parsing (Implemented)

DOM parsing builds a complete in-memory tree structure of the YAML document. This mode is ideal when you need to:

- Navigate and query the YAML structure
- Modify the YAML structure
- Access values multiple times
- Work with the entire document at once

**DOM Accessors (selected):**
- `gtext_yaml_node_type()`
- `gtext_yaml_node_as_string()`, `gtext_yaml_node_as_bool()`, `gtext_yaml_node_as_int()`, `gtext_yaml_node_as_float()`
- `gtext_yaml_node_is_null()`, `gtext_yaml_node_as_timestamp()`
- `gtext_yaml_sequence_length()`, `gtext_yaml_sequence_get()`
- `gtext_yaml_mapping_size()`, `gtext_yaml_mapping_get()`

**Example: Clone a subtree into a new document**

```c
#include <ghoti.io/text/yaml.h>

const char *yaml = "root: {items: [one, two]}\n";
GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, NULL);
if (!doc) {
  // Handle parse error
}

const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
const GTEXT_YAML_Node *items = gtext_yaml_mapping_get(root, "root");

GTEXT_YAML_Document *clone_doc = gtext_yaml_document_new(NULL, NULL);
GTEXT_YAML_Node *clone = gtext_yaml_node_clone(clone_doc, items);
gtext_yaml_document_set_root(clone_doc, clone);

gtext_yaml_free(doc);
gtext_yaml_free(clone_doc);
```

### 2.3 Writing and Formatting (Implemented)

The writer serializes a DOM to a sink and exposes formatting options such as
indentation, scalar styles, flow vs. block collections, and line-width aware
folding.

```c
GTEXT_YAML_Sink sink;
gtext_yaml_sink_buffer(&sink);

GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
opts.pretty = true;
opts.indent_spaces = 4;
opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
opts.line_width = 12;

gtext_yaml_write_document(doc, &sink, &opts);
printf("%s", gtext_yaml_sink_buffer_data(&sink));
gtext_yaml_sink_buffer_free(&sink);
```

---

## 3. Parse Options

The library provides extensive configuration options for parsing behavior:

### 3.1 Resource Limits

All limits use `0` to indicate library defaults:

- **`max_depth`**: Maximum nesting depth — **Default: 256**
- **`max_total_bytes`**: Maximum total input size — **Default: 64MB**
- **`max_alias_expansion`**: Maximum alias expansion count — **Default: 10,000**

**Example: Setting strict limits for untrusted input**

```c
GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();

// Max 10 MB input
opts.max_total_bytes = 10 * 1024 * 1024;

// Max depth of 50 levels
opts.max_depth = 50;

// Max 100 alias expansions (prevents exponential expansion attacks)
opts.max_alias_expansion = 100;

GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(&opts, callback, user_data);
```

### 3.2 Duplicate Key Handling

- **`GTEXT_YAML_DUPKEY_ERROR`**: Fail parsing when duplicate keys are encountered — **Default** (spec-compliant)
- **`GTEXT_YAML_DUPKEY_FIRST_WINS`**: Use the first occurrence of a duplicate key
- **`GTEXT_YAML_DUPKEY_LAST_WINS`**: Use the last occurrence of a duplicate key

```c
GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
opts.dupkey_mode = GTEXT_YAML_DUPKEY_LAST_WINS;  // Allow duplicate keys, last wins
```

---

## 4. Error Handling

The YAML parser provides detailed error information when parsing fails.

### 4.1 Error Codes

| Code | Meaning |
|------|---------|
| `GTEXT_YAML_OK` | Success |
| `GTEXT_YAML_E_INVALID` | Generic parse/validation error |
| `GTEXT_YAML_E_OOM` | Out of memory |
| `GTEXT_YAML_E_LIMIT` | A configured limit was exceeded |
| `GTEXT_YAML_E_DEPTH` | Maximum nesting depth exceeded |
| `GTEXT_YAML_E_INCOMPLETE` | More input required to complete parsing |
| `GTEXT_YAML_E_BAD_TOKEN` | Unexpected token encountered |
| `GTEXT_YAML_E_BAD_ESCAPE` | Invalid escape sequence in quoted scalar |
| `GTEXT_YAML_E_DUPKEY` | Duplicate mapping key |
| `GTEXT_YAML_E_WRITE` | Sink/write error (writer only) |
| `GTEXT_YAML_E_STATE` | Operation not valid in current state |

### 4.2 Error Context

When parsing fails, the error includes position information:

```c
GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, input, len);
if (status != GTEXT_YAML_OK) {
    // Error occurred - get details
    printf("YAML parsing failed with code %d\n", status);
    
    // Future: retrieve detailed error with line/column information
    // GTEXT_YAML_Error *err = gtext_yaml_stream_get_error(stream);
    // printf("Error at line %d, column %d: %s\n", err->line, err->col, err->message);
}
```

---

## 5. Scalar Styles

YAML supports multiple scalar styles, all of which are fully supported:

### 5.1 Plain Scalars

```yaml
key: value
number: 123
boolean: true
```

Plain scalars are unquoted strings that follow YAML's flow syntax rules. They cannot contain certain special characters without quoting.

### 5.2 Single-Quoted Scalars

```yaml
key: 'value with spaces'
path: 'C:\Program Files\'
literal: 'It''s escaped with double quotes'
```

Single-quoted scalars preserve literal characters. Single quotes are escaped by doubling them (`''`).

### 5.3 Double-Quoted Scalars

```yaml
key: "value with\nnewline"
unicode: "Hello \u0041\u0042\u0043"
special: "Tab:\t Quote:\" Backslash:\\"
```

Double-quoted scalars support escape sequences:
- `\\` - Backslash
- `\"` - Quote
- `\n` - Newline
- `\t` - Tab
- `\r` - Carriage return
- `\uXXXX` - Unicode code point (4 hex digits)
- `\UXXXXXXXX` - Unicode code point (8 hex digits)

**Note:** Some escape sequences (`\0`, `\a`, `\b`, `\f`, `\v`, `\e`) are defined in YAML 1.2.2 but not yet implemented in this parser.

### 5.4 Literal Scalars (`|`)

```yaml
script: |
  #!/bin/bash
  echo "Line 1"
  echo "Line 2"
  echo "Line 3"
```

Literal scalars preserve newlines and indentation. Each line break is preserved as-is.

**Line break normalization:** Input line endings are normalized to `\n` during parsing.
Use `GTEXT_YAML_Write_Options.newline` to control output line endings (LF/CRLF/CR).

### 5.5 Folded Scalars (`>`)

```yaml
description: >
  This is a long paragraph
  that will be folded into
  a single line with spaces
  between the words.
  
  Empty lines create paragraph breaks.
```

Folded scalars join lines with spaces, making them ideal for long text passages. Empty lines create paragraph breaks.

---

## 6. Collections

### 6.1 Sequences (Arrays)

**Flow Style:**
```yaml
numbers: [1, 2, 3, 4, 5]
mixed: [string, 123, true, null]
```

**Block Style:**
```yaml
items:
  - first
  - second
  - third
nested:
  - [a, b, c]
  - [d, e, f]
```

### 6.2 Mappings (Objects)

**Flow Style:**
```yaml
config: {host: localhost, port: 8080, debug: true}
```

**Block Style:**
```yaml
server:
  host: localhost
  port: 8080
  ssl:
    enabled: true
    cert: /path/to/cert
```

### 6.3 Nested Collections

```yaml
users:
  - name: Alice
    roles: [admin, user]
    config:
      theme: dark
      notifications: true
  - name: Bob
    roles: [user]
    config:
      theme: light
      notifications: false
```

---

## 7. Anchors and Aliases

YAML supports anchors (`&name`) and aliases (`*name`) for reusing values:

### 7.1 Basic Anchors

```yaml
defaults: &defaults
  timeout: 30
  retries: 3

service1:
  <<: *defaults
  name: api

service2:
  <<: *defaults
  name: worker
  timeout: 60  # Override default
```

### 7.2 Merge Keys (`<<`)

The special key `<<` merges mapping values:

```yaml
base: &base
  x: 1
  y: 2

extended:
  <<: *base
  z: 3
# Results in: {x: 1, y: 2, z: 3}
```

### 7.3 Alias Expansion Limits

To prevent denial-of-service attacks via exponential alias expansion, the parser enforces `max_alias_expansion`:

```yaml
# This could cause exponential expansion:
a: &a [x, x]
b: &b [*a, *a]     # 4 elements
c: &c [*b, *b]     # 8 elements
d: &d [*c, *c]     # 16 elements
e: [*d, *d]        # 32 elements
```

The parser tracks total expansion count and fails with `GTEXT_YAML_E_LIMIT` when exceeded.

### 7.4 Cycle Detection

The parser detects and rejects cycles in anchor definitions:

```yaml
# This is invalid - creates a cycle:
a: &a
  b: *b
b: &b
  a: *a
```

Parser returns `GTEXT_YAML_E_INVALID` when cycles are detected.

---

## 8. Multi-Document Streams

YAML supports multiple documents in a single stream using `---` (document separator) and `...` (document terminator):

```yaml
---
doc: 1
name: First document
---
doc: 2
name: Second document
...
---
doc: 3
name: Third document
```

**Usage:**

```c
// The streaming parser handles multi-document streams automatically
// Each document's content is delivered via callbacks
// Document boundaries are implicit in the event stream

GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, multi_doc_yaml, len);
status = gtext_yaml_stream_finish(stream);
```

---

## 9. Encoding Support

The parser accepts UTF-8, UTF-16, and UTF-32 input when a BOM is present.
Input is normalized to UTF-8 internally.

### 9.1 BOM Detection and Transcoding

Supported BOMs:

- UTF-8: EF BB BF
- UTF-16LE: FF FE
- UTF-16BE: FE FF
- UTF-32LE: FF FE 00 00
- UTF-32BE: 00 00 FE FF

If no BOM is present, input is treated as UTF-8.

### 9.2 UTF-8 Validation

All input is validated as valid UTF-8. Invalid sequences cause `GTEXT_YAML_E_INVALID`:

```c
// Invalid UTF-8 will be rejected
const char *invalid_utf8 = "key: \xFF\xFF invalid";
GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, invalid_utf8, strlen(invalid_utf8));
// status == GTEXT_YAML_E_INVALID
```

### 9.3 Unicode Escapes

Double-quoted strings support Unicode escapes:

```yaml
unicode: "\u0048\u0065\u006C\u006C\u006F"  # Hello
emoji: "\U0001F600"  # 😀
chinese: "\u4E2D\u6587"  # 中文
```

---

## 10. Limits and Security

### 10.1 Decompression Bomb Protection

The parser implements multiple layers of protection against malicious inputs:

**Max Depth:** Prevents stack overflow via deeply nested structures.
```yaml
# With max_depth=5, this would fail:
- - - - - - - - - - - too deep
```

**Max Total Bytes:** Prevents memory exhaustion from large inputs.
```c
opts.max_total_bytes = 10 * 1024 * 1024;  // 10 MB limit
```

**Max Alias Expansion:** Prevents "YAML bombs" where tiny inputs expand exponentially.
```c
opts.max_alias_expansion = 100;  // Strict limit for untrusted input
```

### 10.2 Example: Strict Limits for Untrusted Input

```c
GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();

// Max 1 MB input
opts.max_total_bytes = 1024 * 1024;

// Max 32 levels deep
opts.max_depth = 32;

// Max 50 alias expansions
opts.max_alias_expansion = 50;

// Use first occurrence for duplicate keys (lenient)
opts.dupkey_mode = GTEXT_YAML_DUPKEY_FIRST_WINS;

GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(&opts, callback, NULL);
```

---

## 11. Common Patterns

### 11.1 Configuration Files

```yaml
# application.yaml
app:
  name: MyApp
  version: 1.0.0
  debug: false

server:
  host: 0.0.0.0
  port: 8080
  workers: 4

database:
  driver: postgresql
  host: db.example.com
  port: 5432
  name: production
  pool:
    min: 5
    max: 20
```

### 11.2 Docker Compose

```yaml
version: '3.8'
services:
  web:
    image: nginx:latest
    ports:
      - "80:80"
    volumes:
      - ./html:/usr/share/nginx/html
  db:
    image: postgres:13
    environment:
      POSTGRES_PASSWORD: secret
```

### 11.3 Kubernetes Manifests

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: nginx-deployment
spec:
  replicas: 3
  selector:
    matchLabels:
      app: nginx
  template:
    metadata:
      labels:
        app: nginx
    spec:
      containers:
      - name: nginx
        image: nginx:1.14.2
        ports:
        - containerPort: 80
```

### 11.4 CI/CD Configuration

```yaml
# .github/workflows/ci.yml
name: CI
on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Run tests
        run: make test
```

---

## 12. Best Practices

### 12.1 Memory Management

- Always call `gtext_yaml_stream_finish()` after feeding all input
- Always call `gtext_yaml_stream_free()` to release resources
- The parser is designed to be leak-free (verified with valgrind)

### 12.2 Error Handling

- Always check return status codes
- Don't continue processing after errors
- Set appropriate limits for untrusted input

### 12.3 Performance

- Use streaming parsing for large documents
- Feed input in reasonable chunks (4KB-64KB is typical)
- Don't set limits unnecessarily low (causes early failures)

### 12.4 Security

- Always set `max_total_bytes` for untrusted input
- Use strict `max_alias_expansion` (e.g., 100) for untrusted input
- Set reasonable `max_depth` based on expected data structure

---

## 13. Testing

The YAML module includes 781 comprehensive tests covering:

- ✅ All scalar styles (plain, quoted, literal, folded)
- ✅ Escape sequences and Unicode handling
- ✅ Flow and block collections
- ✅ Nested structures up to max depth
- ✅ Anchors, aliases, and merge keys
- ✅ Multi-document streams
- ✅ Error conditions and invalid input
- ✅ Limit enforcement
- ✅ UTF-8 validation
- ✅ Real-world YAML files (Docker, K8s, GitHub Actions, etc.)

All tests pass with zero memory leaks (valgrind-verified).

---

## 14. Future Enhancements

### Planned Features

- **Comment Preservation**: Maintain comments for round-trip editing
- **Source Location Tracking**: Attach line/column info to DOM nodes
- **Scalar Style Preservation**: Preserve scalar styles during round-trip
- **YAML to JSON Conversion**: Provide a lossless conversion utility

### Compatibility

- The parser aims for YAML 1.2.2 compliance
- Currently implements core features sufficient for common use cases
- Extensions and advanced features being added incrementally

---

## 15. API Reference

### Custom Tags

Custom tags are opt-in and configured through parse/write options. Set
`enable_custom_tags=true` and provide a `custom_tags` array with
`custom_tag_count`. The parser calls the `construct` callback for matching
explicit tags. The writer calls the `represent` callback to choose the tag
to emit.

```c
GTEXT_YAML_Custom_Tag handlers[] = {
  {
    .tag = "!example",
    .construct = my_construct,
    .represent = my_represent,
    .user = my_context,
  },
};

GTEXT_YAML_Parse_Options parse_opts = gtext_yaml_parse_options_default();
parse_opts.enable_custom_tags = true;
parse_opts.custom_tags = handlers;
parse_opts.custom_tag_count = 1;

GTEXT_YAML_Write_Options write_opts = gtext_yaml_write_options_default();
write_opts.enable_custom_tags = true;
write_opts.custom_tags = handlers;
write_opts.custom_tag_count = 1;
```

### Core Functions

```c
// Create streaming parser
GTEXT_YAML_Stream* gtext_yaml_stream_new(
    const GTEXT_YAML_Parse_Options *opts,
    GTEXT_YAML_Stream_Callback callback,
    void *user_data
);

// Feed input chunk
GTEXT_YAML_Status gtext_yaml_stream_feed(
    GTEXT_YAML_Stream *stream,
    const char *data,
    size_t len
);

// Finish parsing (validate completeness)
GTEXT_YAML_Status gtext_yaml_stream_finish(GTEXT_YAML_Stream *stream);

// Free resources
void gtext_yaml_stream_free(GTEXT_YAML_Stream *stream);

// Get default options
GTEXT_YAML_Parse_Options gtext_yaml_parse_options_default(void);

// Free error structure
void gtext_yaml_error_free(GTEXT_YAML_Error *err);
```

### Type Definitions

See `<ghoti.io/text/yaml/yaml_core.h>` for complete type definitions and enumerations.

---

## 16. License

Copyright 2026 by Corey Pennycuff

Part of the ghoti.io text library.

---

**Last Updated:** February 11, 2026  
**Module Version:** 0.1.0 (Alpha)  
**Test Count:** 781 tests, all passing
