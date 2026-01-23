# JSON Module Documentation (ghoti.io)

This document describes the **full‑featured JSON parsing/writing library in C** implemented in the `text` library in the `ghoti.io` family. The implementation is **cross‑platform**, **dependency‑free** (libc only), and prioritizes **correctness** and **spec compliance** over simplicity.

---

## 1. Overview

The JSON module provides comprehensive JSON processing capabilities with support for strict RFC 8259 / ECMA‑404 compliance, extended JSON modes, multiple parsing and writing models, and advanced features like JSON Pointer, Patch, and Schema validation.

### Core Capabilities

- **Strict JSON parsing** per RFC 8259 / ECMA‑404 with full grammar correctness
- **Extended JSON modes** (JSONC, trailing commas, non-finite numbers, relaxed strings)
- **Two parsing models**: DOM/tree and streaming/SAX
- **Two writing models**: DOM serialization and streaming writer
- **High-quality error diagnostics** with position information and context snippets
- **Round-trip correctness** including exact number preservation
- **JSON Pointer (RFC 6901)**, **JSON Patch (RFC 6902)**, and **JSON Merge Patch (RFC 7386)** support
- **JSON Schema validation** with a pragmatic core subset

---

## 2. Parsing Modes

### 2.1 DOM Parsing

DOM parsing builds a complete in-memory tree structure of the JSON document. This mode is ideal when you need to:

- Navigate and query the JSON structure
- Modify the JSON structure
- Access values multiple times
- Work with the entire document at once

The DOM is allocated from an arena, making cleanup simple with a single `text_json_free()` call.

### 2.2 Streaming Parsing

Streaming parsing processes JSON incrementally, emitting events as values are encountered. This mode is ideal when you need to:

- Process large JSON documents with minimal memory usage
- Handle JSON from network streams or files
- Transform JSON on-the-fly without building a full DOM
- Process NDJSON (newline-delimited JSON) streams

The streaming parser accepts input in chunks and maintains state between calls, making it suitable for network or file I/O scenarios.

**Multi-Chunk Value Handling:**
The parser correctly handles values (strings, numbers) that span multiple chunks. When a value is incomplete at the end of a chunk, the parser preserves state and waits for more input. This ensures correct parsing regardless of how the input is split across chunks.

- **No chunk count limit**: Values can span 2, 3, 100, or more chunks
- **Total bytes limit**: Limited by `max_total_bytes` option (default: 64MB, configurable)
- **State preservation**: Incomplete values are buffered until completion
- **Examples**:
  - String: `"hello` (chunk 1) + `world"` (chunk 2) → correctly parses as `"helloworld"`
  - Number: `12345` (chunk 1) + `.678` (chunk 2) → correctly parses as `12345.678`
  - Escape sequences and Unicode escapes also work correctly across chunks

**Important:** Always call `text_json_stream_finish()` after feeding all input chunks. The last value may not be emitted until `finish()` is called, especially if it was incomplete at the end of the final chunk. This ensures all values are processed and the JSON structure is validated as complete.

---

## 3. Writing Modes

### 3.1 DOM Serialization

DOM serialization writes a complete JSON value tree to output. This is the simplest mode for converting a DOM back to JSON text.

### 3.2 Streaming Writer

The streaming writer allows you to construct JSON incrementally with structural enforcement. The writer maintains an internal stack to ensure valid JSON output (e.g., preventing values without keys inside objects).

---

## 4. Parse Options

The library provides extensive configuration options for parsing behavior:

### 4.1 Strictness / Extensions

- **`allow_comments`**: Enable JSONC mode (single-line `//` and multi-line `/* */` comments) — **Default: `false`**
- **`allow_trailing_commas`**: Allow trailing commas in arrays and objects — **Default: `false`**
- **`allow_nonfinite_numbers`**: Allow `NaN`, `Infinity`, and `-Infinity` as number values — **Default: `false`**
- **`allow_single_quotes`**: Allow single-quoted strings (relaxed mode) — **Default: `false`**
- **`allow_unescaped_controls`**: Allow unescaped control characters in strings (relaxed mode) — **Default: `false`**

### 4.2 Unicode / Input Handling

- **`allow_leading_bom`**: Allow UTF-8 BOM at the start of input — **Default: `true`**
- **`validate_utf8`**: Validate UTF-8 sequences in input — **Default: `true`**
- **`normalize_unicode`**: Apply NFC normalization to strings — **Default: `false`**
- **`in_situ_mode`**: Zero-copy mode that references input buffer directly — **Default: `false`**

### 4.3 Duplicate Key Handling

- **`TEXT_JSON_DUPKEY_ERROR`**: Fail parsing when duplicate keys are encountered — **Default**
- **`TEXT_JSON_DUPKEY_FIRST_WINS`**: Use the first occurrence of a duplicate key
- **`TEXT_JSON_DUPKEY_LAST_WINS`**: Use the last occurrence of a duplicate key
- **`TEXT_JSON_DUPKEY_COLLECT`**: Store all values for duplicate keys in an array

### 4.4 Resource Limits

All limits use `0` to indicate library defaults:

- **`max_depth`**: Maximum nesting depth (default: 256)
- **`max_string_bytes`**: Maximum string size in bytes (default: 16MB)
- **`max_container_elems`**: Maximum array/object elements (default: 1M)
- **`max_total_bytes`**: Maximum total input size (default: 64MB)

### 4.5 Number Fidelity

- **`preserve_number_lexeme`**: Preserve original number text for exact round-trip — **Default: `true`**
- **`parse_int64`**: Detect and parse exact int64 representation — **Default: `true`**
- **`parse_uint64`**: Detect and parse exact uint64 representation — **Default: `true`**
- **`parse_double`**: Derive double representation when representable — **Default: `true`**
- **`allow_big_decimal`**: Store decimal as string-backed arbitrary precision — **Default: `false`**

Numbers can be accessed in multiple representations simultaneously, allowing you to choose the most appropriate form for your use case.

---

## 5. Write Options

The library provides extensive configuration options for output formatting:

### 5.1 Formatting

- **`pretty`**: Pretty-print output with indentation — **Default: `false`**
- **`indent_spaces`**: Number of spaces per indent level — **Default: `2`**
- **`newline`**: Newline string — **Default: `"\n"`** (can use `"\r\n"`)
- **`trailing_newline`**: Add trailing newline at end of output — **Default: `false`**
- **`space_after_colon`**: Add space after `:` in objects — **Default: `false`**
- **`space_after_comma`**: Add space after `,` in arrays/objects — **Default: `false`**
- **`inline_array_threshold`**: Maximum elements for inline array formatting — **Default: `-1`** (always inline when not pretty)
- **`inline_object_threshold`**: Maximum pairs for inline object formatting — **Default: `-1`** (always inline when not pretty)

### 5.2 Escaping

- **`escape_solidus`**: Escape forward slash `/` — **Default: `false`**
- **`escape_unicode`**: Output `\uXXXX` for non-ASCII characters (canonical mode) — **Default: `false`**
- **`escape_all_non_ascii`**: Escape all non-ASCII characters (stricter) — **Default: `false`**

### 5.3 Canonical / Deterministic Output

- **`sort_object_keys`**: Sort object keys for stable, deterministic output — **Default: `false`**
- **`canonical_numbers`**: Normalize numeric lexemes (use with care) — **Default: `false`**
- **`canonical_strings`**: Normalize string escapes — **Default: `false`**

### 5.4 Floating-Point Formatting

- **`float_format`**: Formatting strategy:
  - `TEXT_JSON_FLOAT_SHORTEST`: Shortest representation (default)
  - `TEXT_JSON_FLOAT_FIXED`: Fixed-point notation
  - `TEXT_JSON_FLOAT_SCIENTIFIC`: Scientific notation
- **`float_precision`**: Precision for fixed/scientific format (default: 6)

### 5.5 Extensions

- **`allow_nonfinite_numbers`**: Emit `NaN`/`Infinity` if node contains non-finite values — **Default: `false`**

---

## 6. Output Sinks

The writer supports multiple output destinations through a sink abstraction:

- **Growable buffer**: Dynamically-growing buffer for complete output
- **Fixed buffer**: Fixed-size buffer with truncation detection
- **Callback sink**: Custom write function for any destination (files, network, etc.)

---

## 7. DOM Operations

### 7.1 Value Access

The DOM provides type-safe accessors for all JSON value types:

- **Scalars**: `text_json_get_bool()`, `text_json_get_string()`
- **Numbers**: Multiple representations available (`get_i64()`, `get_u64()`, `get_double()`, `get_number_lexeme()`)
- **Arrays**: `text_json_array_size()`, `text_json_array_get()`
- **Objects**: `text_json_object_size()`, `text_json_object_get()`, `text_json_object_key()`, `text_json_object_value()`

### 7.2 Value Creation and Mutation

The DOM supports programmatic creation and modification:

- **Value constructors**: Create null, bool, number, string, array, and object values
- **Array operations**: `push()`, `set()`, `insert()`, `remove()`
- **Object operations**: `put()`, `remove()`

### 7.3 Utility Operations

- **Deep equality**: Compare two JSON values with configurable semantics (lexeme-based or numeric equivalence)
- **Deep clone**: Clone a value tree into a new arena
- **Object merge**: Merge two objects with configurable conflict policy (first-wins, last-wins, or error)

---

## 8. Streaming Parser Events

The streaming parser emits events for:

- **Value events**: `NULL`, `BOOL`, `NUMBER`, `STRING`
- **Structure events**: `ARRAY_BEGIN`, `ARRAY_END`, `OBJECT_BEGIN`, `OBJECT_END`
- **Key events**: `KEY` (for object keys)

Each event includes the relevant data (boolean value, string/number text, etc.) and maintains position information for error reporting.

---

## 9. JSON Pointer (RFC 6901)

JSON Pointer provides path-based access to nested JSON values using a string syntax like `/a/0/b`:

- Evaluate pointers against DOM trees
- Support for escape sequences (`~0` for `~`, `~1` for `/`)
- Mutable access for patch operations

---

## 10. JSON Patch (RFC 6902)

JSON Patch allows modifying JSON documents using a sequence of operations:

- **add**: Add a value at a path
- **remove**: Remove a value at a path
- **replace**: Replace a value at a path
- **move**: Move a value from one path to another
- **copy**: Copy a value from one path to another
- **test**: Test that a value at a path equals an expected value

Patches are represented as JSON arrays of operation objects and are applied atomically (all operations succeed or the patch fails).

---

## 11. JSON Merge Patch (RFC 7386)

JSON Merge Patch provides a simpler merge operation that recursively merges a patch document into a target document. This is distinct from JSON Patch and is useful for configuration updates.

---

## 12. JSON Schema Validation

The library provides a pragmatic core subset of JSON Schema support. This subset covers the most commonly used validation features while omitting more advanced features for simplicity and maintainability.

### 12.1 Supported Keywords

- **Type validation**: `type` (supports `null`, `boolean`, `number`, `string`, `array`, `object`, or arrays of types)
- **Object validation**: `properties` (recursive validation), `required`
- **Array validation**: `items` (single schema for all items)
- **Value constraints**: `enum`, `const`
- **Numeric constraints**: `minimum`, `maximum` (inclusive)
- **String constraints**: `minLength`, `maxLength`
- **Array constraints**: `minItems`, `maxItems`

Schemas are compiled once and can be reused for validating multiple instances.

### 12.2 Omitted Features

This core subset is sufficient for many validation use cases while keeping the implementation focused and maintainable. For a complete list of omitted JSON Schema features that are planned for future releases, see [Section 17.1: Additional JSON Schema Keywords](#171-additional-json-schema-keywords).

---

## 13. Error Reporting

The library provides comprehensive error information:

- **Error codes**: Stable error codes for programmatic handling
- **Human-readable messages**: Descriptive error messages
- **Position information**: Byte offset, line number, and column number
- **Enhanced diagnostics** (optional):
  - Context snippet showing the error location
  - Caret positioning within the snippet
  - Expected vs actual token descriptions

Error context snippets are dynamically allocated and must be freed via `text_json_error_free()`.

---

## 14. Additional Features

### 14.1 In-Situ / Zero-Copy Parsing

An optional in-situ parsing mode allows the DOM to reference slices of the input buffer directly, avoiding copies for strings and number lexemes. This mode requires the input buffer to remain valid for the lifetime of the DOM.

### 14.2 Multiple Top-Level Value Parsing

The parser can parse a single JSON value and return the number of bytes consumed, allowing you to parse multiple values from the same buffer sequentially. This is useful for embedding JSON in larger syntaxes or custom protocols.

### 14.3 Round-Trip Correctness

When number lexeme preservation is enabled, the library guarantees exact round-trip: parse → write → parse results in identical number representations. This is critical for applications that need to preserve exact numeric values.

---

## 15. Design Philosophy

The library prioritizes **correctness over simplicity**:

1. **Strict by default**: Strict JSON compliance is the default; extensions are explicit opt-ins
2. **Exact number preservation**: Number lexemes are preserved by default for round-trip correctness
3. **Unicode correctness**: Proper UTF-8 handling and surrogate pair validation
4. **Configurable duplicate keys**: Duplicate key handling is configurable (default: error)
5. **Arena allocation**: DOM uses arena allocation for predictable ownership and fast cleanup

---

## 16. Getting Started

Include the umbrella header:

```c
#include <ghoti.io/text/json.h>
```

For fine-grained control, include specific headers:

```c
#include <ghoti.io/text/json/json_core.h>  // Core types and options
#include <ghoti.io/text/json/json_dom.h>   // DOM parsing and manipulation
#include <ghoti.io/text/json/json_stream.h> // Streaming parser
#include <ghoti.io/text/json/json_writer.h> // Writer
#include <ghoti.io/text/json/json_pointer.h> // JSON Pointer
#include <ghoti.io/text/json/json_patch.h>   // JSON Patch
#include <ghoti.io/text/json/json_schema.h>  // Schema validation
```

Comprehensive usage examples are provided in the `examples/` directory.

---

## 17. Future Work

The following features are planned for future releases:

### 17.1 Additional JSON Schema Keywords

The following JSON Schema features are not currently implemented but are planned:

- **Composition keywords**: `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else` for conditional and combined schemas
- **Reference and definitions**: `$ref`, `$id`, `$anchor`, `$defs`, `$schema` for schema reuse and versioning
- **Advanced object validation**: `additionalProperties`, `patternProperties`, `propertyNames`, `dependencies`, `dependentRequired`, `dependentSchemas`
- **Advanced array validation**: `additionalItems`, `items` as array (tuple validation), `contains`, `minContains`, `maxContains`, `uniqueItems`
- **String pattern matching**: `pattern` (regular expressions), `format`, `contentEncoding`, `contentMediaType`
- **Additional numeric constraints**: `multipleOf`, `exclusiveMinimum`, `exclusiveMaximum`
- **Advanced features**: `unevaluatedProperties`, `unevaluatedItems`, `$vocabulary`, `$recursiveRef`, `$recursiveAnchor`

### 17.2 JSONPath Support

JSONPath is a query language for JSON that provides XPath-like expressions for selecting and extracting values from JSON documents. Unlike JSON Pointer (RFC 6901), which provides single-path access, JSONPath supports:

- **Wildcard matching**: `$.*` to select all properties
- **Array slicing**: `$[0:5]` to select array ranges
- **Filter expressions**: `$[?(@.price > 10)]` for conditional selection
- **Recursive descent**: `$..name` to find all `name` properties at any depth
- **Multiple path results**: Returns arrays of matching values

JSONPath support would complement the existing JSON Pointer functionality and provide more powerful query capabilities for JSON documents.

---

## 15. Security Considerations

The JSON library implements comprehensive defensive programming practices to ensure memory safety, prevent undefined behavior, and handle malicious or malformed input gracefully.

### 15.1 Integer Overflow Protection

All arithmetic operations throughout the library are protected against integer overflow and underflow:

- **Addition overflow checks**: All additions use overflow-safe checks (e.g., `if (a > SIZE_MAX - b)`) before performing operations
- **Multiplication overflow checks**: Multiplications are validated to prevent overflow (e.g., `if (a > SIZE_MAX / b)`)
- **Subtraction underflow checks**: Subtractions are validated to prevent underflow
- **Position tracking**: Line and column numbers use overflow-safe increment operations with INT_MAX limits

**Shared Utilities:**
The library uses shared utility functions from `json_utils.c` for consistent overflow protection:
- `json_check_add_overflow()` - checks if addition would overflow
- `json_check_mul_overflow()` - checks if multiplication would overflow
- `json_check_sub_underflow()` - checks if subtraction would underflow
- `json_check_int_overflow()` - checks integer overflow for position tracking

**Examples:**
- Buffer size calculations are validated before allocation
- String length calculations are checked for overflow
- Container element counts are validated against limits
- Total bytes consumed are tracked with overflow protection

### 15.2 Bounds Checking

All array and buffer accesses are protected with defensive bounds checking:

- **Array access**: All array element accesses validate indices against array size before access
- **Buffer access**: All buffer operations validate offsets against buffer size
- **Pointer arithmetic**: All pointer arithmetic is validated to ensure pointers remain within valid ranges
- **String operations**: String operations validate lengths and offsets before access

**Examples:**
- `text_json_array_get()` validates index against array size before access
- `text_json_object_key()` validates index against object size before access
- Lexer and parser validate buffer offsets before reading
- Stream operations validate stack indices before access

### 15.3 NULL Pointer Handling

All functions implement comprehensive NULL pointer checks:

- **Public API functions**: All public API functions validate required parameters for NULL
- **Internal functions**: Internal functions validate pointers before dereferencing
- **Error handling**: NULL pointer errors are handled gracefully with appropriate error codes
- **Resource cleanup**: Cleanup functions handle NULL gracefully (no-op for NULL pointers)

**Examples:**
- `text_json_parse()` returns NULL and sets error if input buffer is NULL
- `text_json_stream_new()` returns NULL if callback is NULL
- `text_json_stream_free()` safely handles NULL (no-op)
- All accessor functions validate value pointers before access

### 15.4 Input Validation

Comprehensive input validation is performed at all API boundaries:

- **Input size validation**: Input sizes are validated to prevent obvious overflow (SIZE_MAX/2 limit)
- **Resource limits**: All resource limits (string length, container size, total bytes) are enforced
- **State validation**: Stream state is validated before operations
- **Option validation**: Parse options are validated (limits checked via `json_get_limit()`)

**Examples:**
- `text_json_parse()` validates input size before parsing
- `text_json_stream_feed()` validates input size and stream state
- String length limits are enforced during parsing
- Container element limits are enforced during parsing

### 15.5 Error Handling

The library provides comprehensive error handling with detailed diagnostics:

- **Error codes**: Stable error codes for programmatic error handling
- **Error context**: Error structures include position information (offset, line, column)
- **Context snippets**: Error context snippets show the error location in input
- **Resource cleanup**: All error paths properly clean up resources (no memory leaks)
- **Error state**: Streams enter error state on failure (subsequent operations return error)

**Error Reporting:**
- Error structures include detailed position information
- Context snippets are generated for better diagnostics
- Error messages are descriptive and actionable
- Error cleanup is automatic (no resource leaks on error)

### 15.6 Resource Management

The library implements safe resource management patterns:

- **Arena allocation**: DOM values are allocated from an arena (single free operation)
- **Automatic cleanup**: Error paths automatically clean up allocated resources
- **Buffer management**: Buffers are properly managed with overflow-safe growth
- **Context snippet cleanup**: Error context snippets are properly freed

**Examples:**
- DOM values are freed via single `text_json_free()` call
- Stream resources are freed via `text_json_stream_free()`
- Error context snippets are freed via `text_json_error_free()`
- All cleanup functions handle NULL gracefully

### 15.7 Testing and Validation

The library includes comprehensive test coverage for security-critical scenarios:

- **Overflow/underflow tests**: 6 test cases covering overflow scenarios
- **NULL pointer tests**: 8 test cases covering NULL pointer handling
- **Bounds violation tests**: 4 test cases covering bounds checking
- **Invalid state tests**: 5 test cases covering state machine validation
- **Memory safety**: All tests pass Valgrind validation (no memory leaks)

**Test Coverage:**
- All overflow protection code paths are tested
- All NULL pointer checks are tested
- All bounds checking code paths are tested
- All error handling paths are tested
- Memory safety is validated with Valgrind

### 15.8 Best Practices for Users

When using the JSON library, follow these security best practices:

1. **Validate input sizes**: Check input sizes before parsing (library enforces SIZE_MAX/2 limit)
2. **Handle errors**: Always check return values and error structures
3. **Free resources**: Always free allocated values and error structures
4. **Set appropriate limits**: Configure resource limits based on your use case
5. **Validate UTF-8**: Enable UTF-8 validation for untrusted input
6. **Use streaming parser**: For large inputs, use streaming parser to limit memory usage

**Example:**
```c
// Validate input size before parsing
if (input_len > SIZE_MAX / 2) {
    // Handle error - input too large
    return;
}

// Parse with error handling
text_json_error err = {0};
text_json_value* value = text_json_parse(input, input_len, NULL, &err);
if (value == NULL) {
    // Handle error - check err.code and err.message
    text_json_error_free(&err);
    return;
}

// Use value...

// Free resources
text_json_free(value);
```

---
