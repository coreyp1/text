# JSON.md — Full‑Featured JSON Module Planning Document (ghoti.io)

This document defines the plan for a **full‑featured JSON parsing/writing library in C** to be added under the existing `text` library in the `ghoti.io` family. The implementation is **cross‑platform**, **dependency‑free** (libc only), and prioritizes **correctness** and **spec compliance** over simplicity.

---

## 1. Scope and Goals

### 1.1 Core Goals (v1)

Implement a JSON module that supports:

- **Strict JSON** per **RFC 8259 / ECMA‑404**:
  - Full JSON grammar correctness
  - Proper UTF‑8 handling
  - Exact rules for number syntax, escapes, structure, whitespace

- **Extended JSON modes (configurable)**:
  - **JSON with Comments (JSONC)** (`//` and `/* ... */`)
  - **Trailing commas** in arrays/objects
  - **Non‑finite numbers** (`NaN`, `Infinity`, `-Infinity`) (opt‑in)
  - **Duplicate key policies** (configurable: error, first‑wins, last‑wins, collect)
  - **Relaxed string rules** (optional: single quotes, unescaped control characters) (opt‑in, off by default)

- **Two parsing models**:
  1) **DOM/tree** (owning, navigable)
  2) **Streaming/SAX** (incremental, event‑based, low‑memory)

- **Two writing models**:
  1) **DOM serialization**
  2) **Streaming writer** that enforces structural correctness (object/array nesting)

- **High‑quality diagnostics**:
  - stable error codes
  - human‑readable message
  - byte offset + line/column
  - context snippet (optional)

- **Correctness first**:
  - **Round‑trip correctness** (including numbers, when enabled)
  - unicode surrogate pair correctness
  - strict validation for grammar and escapes
  - well‑defined behavior under all options

### 1.2 Advanced “Full‑Featured” Goals (v1–v2)

These are still part of the end product, but can land as milestones:

- **Exact number fidelity** options:
  - Preserve original lexeme exactly for round‑trip
  - Parse to multiple numeric representations:
    - signed 64‑bit int when exactly representable
    - unsigned 64‑bit int when applicable
    - decimal arbitrary precision (string-backed) representation
    - IEEE double (optional derived form)
- **JSON Pointer (RFC 6901)** evaluation on DOM
- **JSON Patch (RFC 6902)** apply to DOM
- **JSON Merge Patch (RFC 7386)** apply/merge
- **Canonical JSON / stable output** options:
  - deterministic key ordering
  - canonical escaping rules
  - newline normalization
- **Schema validation**:
  - JSON Schema support is large; plan is:
    - **v2**: a pragmatic subset (type checks, required, properties, items, enum, min/max)
    - optionally expand later
  - Keep schema engine modular and optional at compile time

---

## 2. Placement in Repository

Add under `text` library:

```
text/
  include/text/
    json.h
    json_dom.h
    json_stream.h
    json_writer.h
    json_pointer.h
    json_patch.h
    json_schema.h
  src/
    json_lexer.c
    json_parser.c
    json_dom.c
    json_stream.c
    json_writer.c
    json_pointer.c
    json_patch.c
    json_schema.c
    json_internal.h
  tests/
    test_json_parse.cpp
    test_json_write.cpp
    test_json_stream.cpp
    test_json_pointer.cpp
    test_json_patch.cpp
    test_json_schema.cpp
    data/
      ...
```

> Note: even though schema may be optional, it still lives here so the module remains cohesive.

---

## 3. Public API — Concepts

The API is designed to be:

- **Stable**: small set of core types, clear ownership rules
- **Correct by default**: strict JSON defaults; extensions are explicit opt‑ins
- **Configurable**: parse/write options, limits, duplicate key policy, number fidelity
- **Composable**: streaming parse ↔ streaming write; DOM parse ↔ DOM write
- **No hidden allocations**: clear allocation policy; ability to supply custom allocators (still libc-only by default)

---

## 4. Core Types (Draft)

### 4.1 Status / Error

```c
typedef enum {
  TEXT_JSON_OK = 0,

  // General
  TEXT_JSON_E_INVALID,
  TEXT_JSON_E_OOM,
  TEXT_JSON_E_LIMIT,
  TEXT_JSON_E_DEPTH,
  TEXT_JSON_E_INCOMPLETE,

  // Lexing / parsing specifics
  TEXT_JSON_E_BAD_TOKEN,
  TEXT_JSON_E_BAD_NUMBER,
  TEXT_JSON_E_BAD_ESCAPE,
  TEXT_JSON_E_BAD_UNICODE,
  TEXT_JSON_E_TRAILING_GARBAGE,

  // Semantics / policy
  TEXT_JSON_E_DUPKEY,
  TEXT_JSON_E_NONFINITE,
  TEXT_JSON_E_SCHEMA,

  // Writer
  TEXT_JSON_E_WRITE,
  TEXT_JSON_E_STATE
} text_json_status;

typedef struct {
  text_json_status code;
  const char* message;     // stable static string or owned; define in policy
  size_t offset;           // byte offset from start
  int line;                // 1-based
  int col;                 // 1-based (byte-based for v1; unicode col optional later)
} text_json_error;
```

### 4.2 Value Types

```c
typedef enum {
  TEXT_JSON_NULL,
  TEXT_JSON_BOOL,
  TEXT_JSON_NUMBER,
  TEXT_JSON_STRING,
  TEXT_JSON_ARRAY,
  TEXT_JSON_OBJECT
} text_json_type;

typedef struct text_json_value text_json_value;
```

---

## 5. Parse Options (Full‑Featured)

```c
typedef enum {
  TEXT_JSON_DUPKEY_ERROR,
  TEXT_JSON_DUPKEY_FIRST_WINS,
  TEXT_JSON_DUPKEY_LAST_WINS,
  TEXT_JSON_DUPKEY_COLLECT   // store duplicates in an array (key -> array of values)
} text_json_dupkey_mode;

typedef struct {
  // Strictness / extensions
  int allow_comments;           // JSONC
  int allow_trailing_commas;
  int allow_nonfinite_numbers;  // NaN/Infinity/-Infinity
  int allow_single_quotes;      // optional relaxed mode (default off)
  int allow_unescaped_controls; // optional relaxed mode (default off)

  // Unicode / input handling
  int allow_leading_bom;        // default on
  int validate_utf8;            // default on
  int normalize_unicode;        // v2: NFC normalization (off by default)

  // Duplicate keys
  text_json_dupkey_mode dupkeys;

  // Limits (0 => library default)
  size_t max_depth;             // e.g. default 256
  size_t max_string_bytes;      // default e.g. 16MB
  size_t max_container_elems;   // default e.g. 1M
  size_t max_total_bytes;       // default e.g. 64MB

  // Number fidelity / representations
  int preserve_number_lexeme;   // keep original token text for exact round-trip
  int parse_int64;              // detect exact int64
  int parse_uint64;             // detect exact uint64
  int parse_double;             // derive double when representable
  int allow_big_decimal;        // store decimal as string-backed big-decimal
} text_json_parse_options;
```

Defaults (recommended):

- strict JSON: all relaxed options off
- validate UTF‑8 on
- preserve number lexeme on (correctness) **unless memory constraints are prioritized**
- detect int64/uint64/double as derived representations where exact

---

## 6. DOM Model (Correctness‑Oriented)

### 6.1 Ownership & Allocation

- Parsing builds an owning DOM: `text_json_value* root`.
- All node memory and owned strings are allocated from a **context arena** associated with the root.
- `text_json_free(root)` frees the entire arena in one call.
- Optional: allow passing an allocator vtable (still libc by default).

### 6.2 Numbers (Multi‑Representation)

A JSON number node stores:

- **canonical lexeme** as parsed (exact substring from input; normalized optional)
- optional **int64** if exactly representable
- optional **uint64** if exactly representable
- optional **double** if representable (or always computed, but stored with flag)
- optional **big decimal** representation (string-backed)

This supports:
- exact round-trip
- correctness in comparisons
- user can choose preferred numeric extraction

### 6.3 Object Storage / Duplicate Keys

- Internal object entries stored as an array of `{key, value}` (and possibly key hash).
- Duplicate behavior:
  - error: fail parse
  - first/last: select winner
  - collect: represent as `key -> array` (implementation detail: convert on collision)

### 6.4 DOM API Sketch

```c
text_json_value* text_json_parse(
  const char* bytes, size_t len,
  const text_json_parse_options* opt,
  text_json_error* err
);

void text_json_free(text_json_value* v);

text_json_type text_json_typeof(const text_json_value* v);

// Scalars
int text_json_get_bool(const text_json_value* v, int* out);

int text_json_get_string(const text_json_value* v, const char** out, size_t* out_len);

// Numbers (choose representations)
int text_json_get_number_lexeme(const text_json_value* v, const char** out, size_t* out_len);
int text_json_get_i64(const text_json_value* v, long long* out);
int text_json_get_u64(const text_json_value* v, unsigned long long* out);
int text_json_get_double(const text_json_value* v, double* out);

// Arrays
size_t text_json_array_size(const text_json_value* v);
const text_json_value* text_json_array_get(const text_json_value* v, size_t idx);

// Objects
size_t text_json_object_size(const text_json_value* v);
const char* text_json_object_key(const text_json_value* v, size_t idx, size_t* key_len);
const text_json_value* text_json_object_value(const text_json_value* v, size_t idx);
const text_json_value* text_json_object_get(const text_json_value* v, const char* key, size_t key_len);
```

### 6.5 DOM Mutation (Builder + Edit)

For full-featured use cases (patching, programmatic output), provide a mutable API:

```c
text_json_value* text_json_new_null(void);
text_json_value* text_json_new_bool(int b);
text_json_value* text_json_new_number_from_lexeme(const char* s, size_t len);
text_json_value* text_json_new_number_i64(long long x);
text_json_value* text_json_new_number_u64(unsigned long long x);
text_json_value* text_json_new_number_double(double x);

text_json_value* text_json_new_string(const char* s, size_t len);

text_json_value* text_json_new_array(void);
int text_json_array_push(text_json_value* arr, text_json_value* child);
int text_json_array_set(text_json_value* arr, size_t idx, text_json_value* child);
int text_json_array_insert(text_json_value* arr, size_t idx, text_json_value* child);
int text_json_array_remove(text_json_value* arr, size_t idx);

text_json_value* text_json_new_object(void);
int text_json_object_put(text_json_value* obj, const char* key, size_t key_len, text_json_value* val);
int text_json_object_remove(text_json_value* obj, const char* key, size_t key_len);
```

Mutation can either:
- allocate from a standalone arena owned by the root value created, or
- attach values to a `text_json_ctx` object.

---

## 7. Streaming (Incremental) Parser

### 7.1 Requirements

- Accept input in chunks (file/network/pipe)
- Emit events for:
  - null/bool/number/string
  - array/object begin/end
  - object key
- Enforce:
  - correct nesting
  - correct key/value placement
  - policy constraints (comments/trailing commas/etc.)

### 7.2 Event Types

```c
typedef enum {
  TEXT_JSON_EVT_NULL,
  TEXT_JSON_EVT_BOOL,
  TEXT_JSON_EVT_NUMBER,   // number token (lexeme always available)
  TEXT_JSON_EVT_STRING,
  TEXT_JSON_EVT_ARRAY_BEGIN,
  TEXT_JSON_EVT_ARRAY_END,
  TEXT_JSON_EVT_OBJECT_BEGIN,
  TEXT_JSON_EVT_OBJECT_END,
  TEXT_JSON_EVT_KEY
} text_json_event_type;

typedef struct {
  text_json_event_type type;
  union {
    int boolean;
    struct { const char* s; size_t len; } str;      // decoded utf-8 string
    struct { const char* s; size_t len; } number;   // exact lexeme slice (buffered)
  } as;
} text_json_event;
```

### 7.3 Streaming Parser API

```c
typedef int (*text_json_event_cb)(void* user, const text_json_event* evt, text_json_error* err);

typedef struct text_json_stream text_json_stream;

text_json_stream* text_json_stream_new(
  const text_json_parse_options* opt,
  text_json_event_cb cb,
  void* user
);

int text_json_stream_feed(text_json_stream* st, const char* bytes, size_t len, text_json_error* err);
int text_json_stream_finish(text_json_stream* st, text_json_error* err);
void text_json_stream_free(text_json_stream* st);
```

### 7.4 String & Number Chunking

Correctness-first approach:

- v1: buffer complete string/number token before emitting event.
- v2: optionally emit chunked string events (`STRING_CHUNK`) for very large strings.

---

## 8. Writer / Serializer (Full‑Featured)

### 8.1 Sink Abstraction

Support writing to:
- growable buffer
- fixed buffer
- callback sink

```c
typedef int (*text_json_write_fn)(void* user, const char* bytes, size_t len);

typedef struct {
  text_json_write_fn write;
  void* user;
} text_json_sink;
```

Provide helpers:
- `text_json_sink_buffer(...)`
- `text_json_sink_fixed_buffer(...)`

### 8.2 Writer Options

```c
typedef struct {
  // Formatting
  int pretty;               // 0 compact, 1 pretty
  int indent_spaces;        // e.g. 2, 4
  const char* newline;      // "\n" default; allow "\r\n"

  // Escaping
  int escape_solidus;       // optional
  int escape_unicode;       // output \uXXXX for non-ASCII (canonical mode)
  int escape_all_non_ascii; // stricter

  // Canonical / deterministic
  int sort_object_keys;     // stable output
  int canonical_numbers;    // normalize numeric lexemes (careful)
  int canonical_strings;    // normalize escapes

  // Extensions
  int allow_nonfinite_numbers; // emit NaN/Infinity if node contains it
} text_json_write_options;
```

### 8.3 DOM Write

```c
int text_json_write_value(
  text_json_sink sink,
  const text_json_write_options* opt,
  const text_json_value* v,
  text_json_error* err
);
```

### 8.4 Streaming Writer (Structural Enforcement)

```c
typedef struct text_json_writer text_json_writer;

text_json_writer* text_json_writer_new(text_json_sink sink, const text_json_write_options* opt);

int text_json_writer_object_begin(text_json_writer* w);
int text_json_writer_object_end(text_json_writer* w);
int text_json_writer_array_begin(text_json_writer* w);
int text_json_writer_array_end(text_json_writer* w);

int text_json_writer_key(text_json_writer* w, const char* key, size_t len);

int text_json_writer_null(text_json_writer* w);
int text_json_writer_bool(text_json_writer* w, int b);
int text_json_writer_number_lexeme(text_json_writer* w, const char* s, size_t len);
int text_json_writer_number_i64(text_json_writer* w, long long x);
int text_json_writer_number_u64(text_json_writer* w, unsigned long long x);
int text_json_writer_number_double(text_json_writer* w, double x);
int text_json_writer_string(text_json_writer* w, const char* s, size_t len);

int text_json_writer_finish(text_json_writer* w, text_json_error* err);
void text_json_writer_free(text_json_writer* w);
```

Writer maintains an internal stack to prevent invalid output (e.g., value without key inside object).

---

## 9. JSON Pointer, Patch, and Merge Patch

### 9.1 JSON Pointer (RFC 6901)

Support:

- Parse pointer string into tokens
- Evaluate against DOM:
  - `text_json_pointer_get(root, "/a/0/b")`
- Optional: set/insert/remove helpers (used by patch)

API sketch:

```c
const text_json_value* text_json_pointer_get(const text_json_value* root, const char* ptr, size_t len);
text_json_value* text_json_pointer_get_mut(text_json_value* root, const char* ptr, size_t len);
```

### 9.2 JSON Patch (RFC 6902)

Implement operations:

- add
- remove
- replace
- move
- copy
- test

Patch is represented as a JSON array of operations. Apply semantics must be correct.

API sketch:

```c
text_json_status text_json_patch_apply(
  text_json_value* root,
  const text_json_value* patch_array,
  text_json_error* err
);
```

### 9.3 JSON Merge Patch (RFC 7386)

```c
text_json_status text_json_merge_patch(
  text_json_value* target,
  const text_json_value* patch,
  text_json_error* err
);
```

---

## 10. Schema Validation (Pragmatic, Modular)

Full JSON Schema (2020-12 etc.) is large. Plan:

- **Core subset first**:
  - type
  - properties / required
  - items
  - enum / const
  - minimum/maximum, minLength/maxLength, minItems/maxItems
- Keep the schema engine isolated so it can evolve.

API sketch:

```c
typedef struct text_json_schema text_json_schema;

text_json_schema* text_json_schema_compile(const text_json_value* schema_doc, text_json_error* err);
void text_json_schema_free(text_json_schema* s);

text_json_status text_json_schema_validate(
  const text_json_schema* schema,
  const text_json_value* instance,
  text_json_error* err
);
```

Optionally allow reporting multiple errors (collect into array).

---

## 11. Implementation Plan (Correctness‑First)

### 11.1 Lexer

- Tokenizes:
  - punctuation: `{ } [ ] : ,`
  - keywords: `true false null`
  - string tokens
  - number tokens
  - extension tokens when enabled: comments, NaN/Infinity
- Tracks:
  - byte offset
  - line/col
- Ensures comment lexing is correct (no nesting for `/* */`, correct line updates)

### 11.2 Parser

- Recursive descent with explicit depth tracking
- Grammar correctness:
  - array/object structure
  - strict commas unless trailing comma enabled
  - key must be string (or single quote string only if enabled)
  - enforce no trailing garbage after root

### 11.3 Strings & Unicode

- Validate and decode escapes:
  - `\" \\ \/ \b \f \n \r \t`
  - `\uXXXX` with surrogate pairs
- Output stored in UTF‑8
- If `validate_utf8`:
  - verify incoming UTF‑8 outside escapes is valid
- Optional (v2): track column in codepoints rather than bytes

### 11.4 Numbers

- Validate number syntax before conversion
- Preserve lexeme when enabled
- Derive int64/uint64 exactly when possible:
  - parse as decimal integer; overflow detection
- Derive double:
  - use `strtod` on a bounded copy; validate full span consumed
- Big decimal (string-backed) for high precision:
  - store normalized form or raw lexeme (decide via option)
- Nonfinite numbers:
  - only allowed when option enabled
  - stored with special flags; writer must respect allow_nonfinite_numbers

### 11.5 Memory

- Arena allocator for parse DOM:
  - reduces fragmentation
  - fast cleanup
- Writer uses minimal temp allocations (stack buffers for escaping)

### 11.6 Deterministic Output

- Key sorting:
  - stable sort keys using UTF‑8 byte comparison
  - define exactly whether sort is by raw key bytes or decoded codepoints (v1: bytes)
- Canonical numbers:
  - only if lexeme not preserved or user explicitly requests normalization

---

## 12. Testing & Verification

### 12.1 Test Suites

- RFC 8259 examples
- A curated subset of JSONTestSuite (valid/invalid)
- Additional JSONC/trailing comma cases
- Unicode torture tests:
  - surrogate pairs
  - invalid surrogate sequences
  - invalid UTF‑8 sequences
- Number tests:
  - boundary int64/uint64
  - exponent extremes
  - invalid forms: `01`, `1.`, `.1`, `-`, `--1`, `1e`, `1e+`
- Round-trip tests:
  - parse → write → parse → deep equal (structural)
  - parse with preserve lexeme → write canonical off → exact number lexeme equality
- Streaming tests:
  - feed byte-by-byte
  - feed random chunk sizes
  - ensure state machine correctness

### 12.2 Fuzzing (Optional but recommended)

Even without external deps, you can integrate a small fuzz harness in `tests/`:

- random bytes → parse (should not crash)
- known valid corpus → must succeed
- ensure time/space limits enforced

---

## 13. Milestones

### Milestone A — Strict JSON DOM + Writer (Correct)
- strict parse (RFC/ECMA)
- DOM write (compact + pretty)
- full unicode correctness
- good errors

### Milestone B — Extensions (JSONC, trailing commas, nonfinite)
- options implemented & tested
- nonfinite numbers round trip (when enabled)

### Milestone C — Streaming Parser + Streaming Writer
- incremental feed/finish
- event correctness
- writer structural enforcement

### Milestone D — Pointer + Patch + Merge Patch
- RFC 6901, 6902, 7386
- full test suite

### Milestone E — Schema (subset) + Canonical Output
- schema compile/validate subset
- deterministic output knobs

---

## 14. Early Decisions (Locked‑In for Correctness)

These are chosen now to align with “correctness, not simplicity”:

1) **Preserve number lexeme by default** (exact round-trip)
2) **Decode strings to UTF‑8 and validate surrogate pairs**
3) **Strict mode default**; extensions are explicit opt‑ins
4) **Duplicate keys are configurable**; default is **error**
5) **DOM uses arena allocation** for predictable ownership and cleanup

---

## 15. Deliverables

- `include/text/json*.h` public headers
- `src/json_*.c` implementation
- `tests/` with corpus + unit tests
- `JSON.md` (this doc) kept at repo root or `text/docs/`

---

## 16. Appendix: Suggested Header Breakdown

- `json.h` — umbrella include + common types (`status`, `error`, options)
- `json_dom.h` — DOM parse + DOM API
- `json_stream.h` — streaming parser
- `json_writer.h` — writer + sink
- `json_pointer.h` — pointer evaluation
- `json_patch.h` — patch/merge patch
- `json_schema.h` — schema compile/validate (optional build flag)


---

## 17. Additional Features for a Truly Full-Featured JSON Library

The following capabilities are now explicitly included as part of the **full-featured** scope. Some may be implemented incrementally, but they are considered first-class design goals.

### 17.1 JSON5 Mode (Explicit)

In addition to JSONC and trailing commas, the library supports a **JSON5-compatible mode** (opt-in), including:

- Unquoted object keys
- Single-quoted strings
- Leading `+` on numbers
- Leading or trailing decimal points (`.5`, `1.`)
- Hexadecimal integers (`0xFF`)
- Extended whitespace

This mode is enabled via a preset parse option and is strictly isolated from default RFC-compliant parsing.

---

### 17.2 NDJSON / JSON Lines

The streaming parser supports **NDJSON** (newline-delimited JSON):

- Multiple top-level JSON values separated by newlines
- Robust handling of chunked input
- Ability to resume parsing after each completed value

This is especially intended for logging, ingestion, and ETL-style workloads.

---

### 17.3 In-Situ / Zero-Copy Parsing Mode

An optional **in-situ parsing mode** allows the DOM to reference slices of the input buffer directly:

- Strings and number lexemes may point into the original input
- No copying into the arena for these values
- Caller must keep the input buffer alive for the lifetime of the DOM

This mode is disabled by default and must be explicitly requested via parse options.

---

### 17.4 DOM Utility Operations

To support advanced manipulation and tooling use cases, the DOM API includes:

- **Deep equality comparison**
  - Configurable semantics (lexeme-based vs numeric equivalence)
- **Deep clone**
  - Clone a value into a new arena/context
- **Object merge helpers**
  - Merge two objects with configurable conflict policy
  - Distinct from JSON Merge Patch semantics

---

### 17.5 Multiple Top-Level Value Parsing

The parser can optionally:

- Parse a single JSON value
- Return the number of bytes consumed
- Allow the caller to continue parsing subsequent values from the same buffer

This is useful for embedding JSON inside larger syntaxes or custom protocols.

---

### 17.6 Enhanced String Handling Options

Additional string-handling policies include:

- Preserve **raw string escape sequences** for exact re-emission
- UTF-8 handling modes:
  - reject invalid UTF-8
  - replace invalid sequences
  - allow invalid sequences (verbatim)

---

### 17.7 Enhanced Error Reporting

Error diagnostics may optionally include:

- A short **context snippet** with caret positioning
- Expected vs actual token descriptions
- Best-effort recovery mode for tooling (formatters, linters)

---

### 17.8 Writer Enhancements

The writer explicitly guarantees:

- Locale-independent numeric formatting
- Configurable floating-point formatting strategies
- Additional pretty-print controls:
  - trailing newline
  - spacing after `:` and `,`
  - inline formatting thresholds for small arrays/objects

---

### 17.9 Path and Query Helpers

In addition to RFC 6901 JSON Pointer:

- Optional **dot/bracket path syntax** (`a.b[0].c`) as a convenience API
- Stable object iteration helpers
- Optional object key indexing for faster lookups

---

## 18. Testing Framework Notes

All tests for this module are written in **C++** and use the **Google Test** framework.

Tests should be written as features are added/modified to verify behavior.

Test file conventions:

- Test sources use `.cpp` extensions
- Public C headers are included via `extern "C"` blocks
- Tests validate:
  - strict vs relaxed modes
  - round-trip correctness
  - streaming behavior
  - error reporting accuracy
  - patch, pointer, and schema semantics

---

## 19. Implementation Notes

This section documents key implementation details and decisions made during development.

### 19.1 Header Organization

The implementation follows the header breakdown specified in Section 16:
- `json_core.h` - Core types, enums, and option structures (separate from umbrella for dependency reduction)
- `json.h` - Umbrella header that includes all JSON module headers
- Individual module headers (`json_dom.h`, `json_stream.h`, etc.) for fine-grained includes

All headers are designed to compile independently and include necessary dependencies.

### 19.2 API Export

All public API functions use the `TEXT_API` macro (defined in `macros.h`) to ensure proper symbol export on Windows (DLL) and Unix (visibility attributes). Internal functions do not use this macro.

### 19.3 Documentation

All public API functions and types are documented with comprehensive Doxygen comments in the header files. Documentation follows Doxygen standards with:
- File-level documentation (`@file`, `@brief`)
- Function documentation (`@brief`, `@param`, `@return`)
- Type and enum documentation
- Cross-references to relevant RFCs and specifications

### 19.4 Memory Management

- DOM values are allocated from an arena allocator
- All DOM memory is freed via `text_json_free()` which frees the entire arena
- Temporary parsing structures (e.g., `json_number`) use malloc and must be explicitly destroyed
- Error context snippets are dynamically allocated and must be freed via `text_json_error_free()`

### 19.5 Examples

Comprehensive usage examples are provided in the `examples/` directory, demonstrating:
- Basic parsing and writing
- Programmatic value creation
- Streaming parser usage
- JSON Pointer (RFC 6901)
- JSON Patch and Merge Patch (RFC 6902, RFC 7386)
- JSON Schema validation

---

