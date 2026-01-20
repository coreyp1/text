# CSV Module Documentation (ghoti.io)

This document describes a **full‑featured CSV parsing/writing module in C** to be added to the `text` library in the `ghoti.io` family. The implementation is **cross‑platform**, **dependency‑free** (libc only), and prioritizes **correctness** and **predictability** over convenience shortcuts.

CSV in the wild is not a single format. This module supports a **strict RFC 4180 mode** plus an explicit, configurable **dialect system** to handle common variants (TSV, custom delimiters, different quoting rules, Excel‑style behaviors, etc.).

---

## 1. Overview

### Core Capabilities

- **Strict CSV parsing** (RFC 4180‑style) with well-defined behavior
- **Dialect configuration**: delimiter, quote char, escaping rules, whitespace rules, newline rules
- **Two parsing models**:
  - **Table/DOM** (materialize rows/fields)
  - **Streaming/SAX** (emit row/field events incrementally)
- **Two writing models**:
  - **Table serialization**
  - **Streaming writer** with structural enforcement
- **High-quality error diagnostics** with byte offset, line/column, row/field indices, and context snippets
- **Robust newline handling**: `\n`, `\r\n`, (optionally `\r`)
- **UTF‑8 validation option** (strict by default, configurable)
- **Configurable resource limits** (rows, columns, field size, total bytes, nesting not applicable)
- **Round‑trip correctness** under a chosen dialect (parse → write → parse stable)
- **Optional header processing** and column name mapping
- **Optional type inference / schema validation hooks** (lightweight; not a replacement for a database)

---

## 2. Format Model

### 2.1 Strict (RFC 4180‑Style) Model

In strict mode, the module enforces the following canonical rules (matching common RFC 4180 interpretations):

- Records separated by CRLF (`\r\n`) (optionally accept `\n` via dialect)
- Fields separated by delimiter (default `,`)
- Fields may be quoted (default `"`)
- Inside a quoted field:
  - Delimiters and newlines are permitted
  - A quote is escaped by doubling (`""`) (default behavior)
- Outside quotes, quotes are not allowed unless dialect permits
- Trailing delimiter means an empty final field

### 2.2 Dialects (Explicit Variants)

A dialect defines *exactly* how to parse and write CSV:

- **Delimiter**: `,` (CSV), `\t` (TSV), `;`, `|`, etc.
- **Quote character**: `"` (default), `'` (optional)
- **Escape mechanism**:
  - doubled quote (`""`) inside quoted fields (default)
  - backslash escape (`\"`, `\\`) (optional dialect)
- **Whitespace policy**:
  - preserve all spaces
  - trim unquoted fields
  - allow spaces after delimiter (common in human-authored files)
- **Newline policy**:
  - accept `\r\n`, `\n` (and optionally `\r`)
  - writer controls output newline style
- **Empty lines**:
  - preserve as empty records
  - ignore
- **Comment lines** (optional): allow lines starting with `#` or `//` when not in quotes
- **Header behavior**:
  - treat first record as header
  - allow duplicate column names (policy configurable)

---

## 3. Parsing Modes

### 3.1 Table (DOM) Parsing

DOM mode builds an in-memory representation:

- `table` → ordered list of `row`
- `row` → ordered list of `field` (byte strings)
- Optional header mapping: `column_name -> index`

**Memory model:**
- Arena allocation for stable ownership and O(1) teardown via `text_csv_free()`.
- Optional **in-situ** mode to reference the input buffer directly (caller must keep it alive).

### 3.2 Streaming Parsing

Streaming parsing processes CSV incrementally, emitting events:

- `RECORD_BEGIN`
- `FIELD` (with data slice or buffered copy depending on mode)
- `RECORD_END`
- `END`

Streaming is designed for large files, network streams, or ETL-style processing.

**Chunking rules:**
- The parser accepts arbitrary chunks; fields may span chunks.
- For quoted fields, newlines may appear inside quotes, so record boundaries are determined by state.

---

## 4. Writing Modes

### 4.1 Table Serialization

Writes a fully materialized table using a selected dialect, guaranteeing that output re-parses to the same fields under that dialect.

### 4.2 Streaming Writer

Allows incremental construction of CSV output:

- Enforces that `field` calls occur within a record
- Enforces record boundaries
- Automatically inserts delimiters/newlines as needed
- Escapes/quotes fields according to dialect

---

## 5. Options

### 5.1 Dialect Options (`text_csv_dialect`)

Suggested fields:

- `delimiter` (default `','`)
- `quote` (default `'"'`)
- `escape` (enum):
  - `TEXT_CSV_ESCAPE_DOUBLED_QUOTE` (default)
  - `TEXT_CSV_ESCAPE_BACKSLASH`
  - `TEXT_CSV_ESCAPE_NONE`
- `newline_in_quotes` (bool; default `true`)
- `accept_lf` (bool; default `true`)
- `accept_crlf` (bool; default `true`)
- `accept_cr` (bool; default `false`)
- `trim_unquoted_fields` (bool; default `false`)
- `allow_space_after_delimiter` (bool; default `false`)
- `allow_unquoted_quotes` (bool; default `false`)
- `allow_unquoted_newlines` (bool; default `false`)
- `allow_comments` (bool; default `false`)
- `comment_prefix` (small string; default `"#"`)
- `treat_first_row_as_header` (bool; default `false`)
- `header_dup_mode` (enum):
  - `TEXT_CSV_DUPCOL_ERROR` (default)
  - `TEXT_CSV_DUPCOL_FIRST_WINS`
  - `TEXT_CSV_DUPCOL_LAST_WINS`
  - `TEXT_CSV_DUPCOL_COLLECT` (store list of indices)

### 5.2 Parse Options (`text_csv_parse_options`)

- `dialect` (embedded struct or pointer)
- `validate_utf8` (default `true`)
- `in_situ_mode` (default `false`)
- `keep_bom` (default `false`) — if false, strip UTF‑8 BOM on first field of first record
- **Limits** (0 means library defaults):
  - `max_rows` (default e.g. 10M)
  - `max_cols` (default e.g. 100k)
  - `max_field_bytes` (default e.g. 16MB)
  - `max_record_bytes` (default e.g. 64MB)
  - `max_total_bytes` (default e.g. 1GB)
- Error context:
  - `enable_context_snippet` (default `true`)
  - `context_radius_bytes` (default e.g. 40)

### 5.3 Write Options (`text_csv_write_options`)

- `dialect` (includes output newline choice)
- `newline` (default `"\n"` or `"\r\n"` per dialect)
- `quote_all_fields` (default `false`)
- `quote_empty_fields` (default `true`)
- `quote_if_needed` (default `true`)
- `always_escape_quotes` (default behavior depends on escape mode)
- `trailing_newline` (default `false`)

---

## 6. Error Reporting

The module uses stable error codes plus rich metadata:

- `text_csv_status` enum (OK + error kinds)
- `text_csv_error` struct:
  - `code`
  - `message`
  - `byte_offset`, `line`, `column`
  - `row_index`, `col_index`
  - `context_snippet`, `caret_offset` (optional)

Common error cases:

- Unterminated quoted field (EOF inside quotes)
- Invalid escape sequence (for backslash-escape dialect)
- Unexpected quote in unquoted field (when disallowed)
- Record/field size limit exceeded
- UTF‑8 validation failure (when enabled)
- Incomplete CRLF sequence (strict CRLF-only dialect)
- Too many columns/rows

---

## 7. API Sketch

> All public API functions use `TEXT_API` and include `<ghoti.io/text/macros.h>`.

### 7.1 Headers

```c
#include <ghoti.io/text/csv.h>
// or
#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_table.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <ghoti.io/text/csv/csv_writer.h>
```

### 7.2 Core Types

- `text_csv_status`
- `text_csv_error`
- `text_csv_dialect`
- `text_csv_parse_options`
- `text_csv_write_options`

### 7.3 Table (DOM) API

- `text_csv_table * text_csv_parse_table(...)`
- `void text_csv_free_table(text_csv_table *)`
- Accessors:
  - `size_t text_csv_row_count(const text_csv_table *)`
  - `size_t text_csv_col_count(const text_csv_table *, size_t row)`
  - `const char * text_csv_field(const text_csv_table *, size_t row, size_t col, size_t *len)`
- Header helpers (if enabled):
  - `text_csv_status text_csv_header_index(const text_csv_table *, const char *name, size_t *out_idx)`

### 7.4 Streaming Parser API

- `text_csv_stream * text_csv_stream_new(const text_csv_parse_options *, text_csv_event_cb, void *user)`
- `text_csv_status text_csv_stream_feed(text_csv_stream *, const void *data, size_t len)`
- `text_csv_status text_csv_stream_finish(text_csv_stream *)`
- `void text_csv_stream_free(text_csv_stream *)`

Events:

- `TEXT_CSV_EVENT_RECORD_BEGIN`
- `TEXT_CSV_EVENT_FIELD`
- `TEXT_CSV_EVENT_RECORD_END`
- `TEXT_CSV_EVENT_END`

### 7.5 Writer API

Sink abstraction mirrors the JSON sink style:

- `text_csv_sink` + `text_csv_write_fn`
- `text_csv_sink_buffer()` and `text_csv_sink_fixed_buffer()` helpers

Writer:

- `text_csv_writer * text_csv_writer_new(text_csv_sink, const text_csv_write_options *)`
- `text_csv_status text_csv_writer_record_begin(text_csv_writer *)`
- `text_csv_status text_csv_writer_field(text_csv_writer *, const void *bytes, size_t len)`
- `text_csv_status text_csv_writer_record_end(text_csv_writer *)`
- `text_csv_status text_csv_writer_finish(text_csv_writer *)`
- `void text_csv_writer_free(text_csv_writer *)`

Table write:

- `text_csv_status text_csv_write_table(text_csv_sink, const text_csv_write_options *, const text_csv_table *)`

---

## 8. Design Philosophy

- **Strict by default**: start from a predictable baseline and require opt-in for messy dialect behaviors.
- **Dialect is part of correctness**: parsing without a dialect is undefined; provide safe defaults.
- **Streaming-first capability**: handle huge inputs with bounded memory.
- **No hidden allocations**: explicit arena ownership and clear lifetimes.
- **Round-trip stability**: output must re-parse to the same fields under the same dialect.
- **Precise errors**: make it obvious *where* and *why* parsing failed.

---

## 9. Testing & Verification

### 9.1 Test Corpus

- RFC 4180 examples (and strict interpretations)
- Edge cases:
  - Empty file, single empty record, trailing newline vs no trailing newline
  - Trailing delimiter (empty last field)
  - Quoted fields with delimiters/newlines
  - Quote escaping (`""`) and backslash escaping (if enabled)
  - CRLF vs LF vs CR behavior per dialect
  - BOM handling
  - Very large fields (limit enforcement)
  - UTF‑8 valid/invalid sequences
- Dialect matrix tests (run same inputs across multiple dialect settings)

### 9.2 Property / Fuzz Testing (Optional but Recommended)

- Generate random records/fields under dialect rules
- Ensure parse → write → parse is stable
- Fuzz invalid inputs to validate error bounds and avoid UB

---

## 10. Deliverables

- Public headers in `include/ghoti.io/text/csv/`
- Implementation in `src/text/csv/`
- Tests in `tests/` (C++ with gtest)
- `examples/` programs:
  - `csv_basic.c` (parse table + print)
  - `csv_stream.c` (stream parse)
  - `csv_write.c` (stream write + sink)
  - `csv_dialects.c` (dialect comparisons)

---

## 11. Future Work

- Optional indexing for fast header lookup on huge column sets
- Optional "lazy table" mode (row offsets + on-demand field decode)
- Schema inference helpers (type sniffing with configurable rules)
- Integration helpers for common data pipelines (BigQuery-style CSV quirks as a dialect preset)
