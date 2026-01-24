# CSV Module Documentation (ghoti.io)

This document describes the **full‑featured CSV parsing/writing library in C** implemented in the `text` library in the `ghoti.io` family. The implementation is **cross‑platform**, **dependency‑free** (libc only), and prioritizes **correctness** and **predictability** over convenience shortcuts.

CSV in the wild is not a single format. This module supports a **strict RFC 4180 mode** plus an explicit, configurable **dialect system** to handle common variants (TSV, custom delimiters, different quoting rules, Excel‑style behaviors, etc.).

---

## 1. Overview

The CSV module provides comprehensive CSV processing capabilities with support for strict RFC 4180 compliance, configurable dialects, multiple parsing and writing models, and advanced features like header processing and zero-copy parsing.

### Core Capabilities

- **Strict CSV parsing** per RFC 4180 with well-defined behavior
- **Dialect configuration**: delimiter, quote char, escaping rules, whitespace rules, newline rules
- **Two parsing models**: DOM/table and streaming/SAX
- **Two writing models**: DOM serialization and streaming writer
- **High-quality error diagnostics** with position information and context snippets
- **Round-trip correctness** under a chosen dialect (parse → write → parse stable)
- **Optional header processing** and column name mapping
- **In-situ / zero-copy parsing** for performance optimization

---

## 2. Parsing Modes

### 2.1 DOM Parsing

DOM parsing builds a complete in-memory table structure of the CSV data. This mode is ideal when you need to:

- Navigate and query the CSV structure
- Access values multiple times
- Work with the entire document at once

The DOM is allocated from an arena, making cleanup simple with a single `text_csv_free_table()` call.

### 2.2 Streaming Parsing

Streaming parsing processes CSV incrementally, emitting events as records and fields are encountered. This mode is ideal when you need to:

- Process large CSV files with minimal memory usage
- Handle CSV from network streams or files
- Transform CSV on-the-fly without building a full DOM

The streaming parser accepts input in chunks and maintains state between calls, making it suitable for network or file I/O scenarios.

---

## 3. Writing Modes

### 3.1 DOM Serialization

DOM serialization writes a complete CSV table to output. This is the simplest mode for converting a table back to CSV text.

### 3.2 Streaming Writer

The streaming writer allows you to construct CSV incrementally with structural enforcement. The writer maintains internal state to ensure valid CSV output (e.g., preventing fields without records).

---

## 4. Parse Options

The library provides extensive configuration options for parsing behavior:

### 4.1 Dialect Configuration

The dialect defines the exact format rules for parsing and writing CSV:

- **`delimiter`**: Field delimiter — **Default: `','`**
- **`quote`**: Quote character — **Default: `'"'`**
- **`escape`**: Escape mode:
  - `TEXT_CSV_ESCAPE_DOUBLED_QUOTE`: Escape quotes by doubling (`""`) — **Default**
  - `TEXT_CSV_ESCAPE_BACKSLASH`: Escape quotes with backslash (`\"`)
  - `TEXT_CSV_ESCAPE_NONE`: No escaping
- **`newline_in_quotes`**: Allow newlines inside quoted fields — **Default: `true`**
- **`accept_lf`**: Accept LF (`\n`) as newline — **Default: `true`**
- **`accept_crlf`**: Accept CRLF (`\r\n`) as newline — **Default: `true`**
- **`accept_cr`**: Accept CR (`\r`) as newline — **Default: `false`**
- **`trim_unquoted_fields`**: Trim whitespace from unquoted fields — **Default: `false`**
- **`allow_space_after_delimiter`**: Allow spaces after delimiter — **Default: `false`**
- **`allow_unquoted_quotes`**: Allow quotes in unquoted fields — **Default: `false`**
- **`allow_unquoted_newlines`**: Allow newlines in unquoted fields — **Default: `false`**
- **`allow_comments`**: Allow comment lines — **Default: `false`**
- **`comment_prefix`**: Comment prefix string — **Default: `"#"`**
- **`treat_first_row_as_header`**: Treat first row as header — **Default: `false`**
- **`header_dup_mode`**: Duplicate column name handling:
  - `TEXT_CSV_DUPCOL_ERROR`: Fail parse on duplicate column name — **Default**
  - `TEXT_CSV_DUPCOL_FIRST_WINS`: Use first occurrence
  - `TEXT_CSV_DUPCOL_LAST_WINS`: Use last occurrence
  - `TEXT_CSV_DUPCOL_COLLECT`: Store all indices for duplicate columns

### 4.2 Unicode / Input Handling

- **`validate_utf8`**: Validate UTF-8 sequences in input — **Default: `true`**
- **`in_situ_mode`**: Zero-copy mode that references input buffer directly — **Default: `false`**
- **`keep_bom`**: Keep UTF-8 BOM at the start of input — **Default: `false`** (strips BOM if false)

### 4.3 Resource Limits

All limits use `0` to indicate library defaults:

- **`max_rows`**: Maximum number of rows (default: 10M)
- **`max_cols`**: Maximum number of columns per row (default: 100k)
- **`max_field_bytes`**: Maximum field size in bytes (default: 16MB)
- **`max_record_bytes`**: Maximum record size in bytes (default: 64MB)
- **`max_total_bytes`**: Maximum total input size (default: 1GB)

### 4.4 Error Context

- **`enable_context_snippet`**: Generate context snippet for errors — **Default: `true`**
- **`context_radius_bytes`**: Bytes before/after error in snippet — **Default: `40`**

---

## 5. Write Options

The library provides extensive configuration options for output formatting:

### 5.1 Quoting and Escaping

- **`quote_all_fields`**: Quote all fields — **Default: `false`**
- **`quote_empty_fields`**: Quote empty fields — **Default: `true`**
- **`quote_if_needed`**: Quote fields containing delimiter/quote/newline — **Default: `true`**
- **`always_escape_quotes`**: Always escape quotes (default behavior depends on escape mode) — **Default: `true`**

### 5.2 Formatting

- **`newline`**: Newline string for output — **Default: `"\n"`** (can use `"\r\n"`)
- **`trailing_newline`**: Add trailing newline at end of output — **Default: `false`**

---

## 6. Output Sinks

The writer supports multiple output destinations through a sink abstraction:

- **Growable buffer**: Dynamically-growing buffer for complete output
- **Fixed buffer**: Fixed-size buffer with truncation detection
- **Callback sink**: Custom write function for any destination (files, network, etc.)

---

## 7. DOM Operations

### 7.1 Value Access

The DOM provides accessors for CSV data:

- **Row count**: `text_csv_row_count()` — returns number of data rows (excluding header if present)
- **Column count**: `text_csv_col_count()` — returns number of columns in a row
- **Field access**: `text_csv_field()` — returns field data and length

### 7.2 Header Operations

When header processing is enabled:

- **Header lookup**: `text_csv_header_index()` — get column index by header name
- Header row is excluded from row count but accessible via adjusted indices

### 7.3 Table Mutation Operations

The CSV module provides a comprehensive set of mutation operations for modifying CSV tables in memory. All mutation operations are **atomic** — they either complete successfully or leave the table unchanged.

#### 7.3.1 Table Creation

**Create Empty Table:**
```c
text_csv_table* table = text_csv_new_table();
```

Creates a new empty table with initialized context and arena. The table starts with a default row capacity of 16 rows. No columns are defined until the first row is added.

**Create Table With Headers:**
```c
const char* headers[] = {"Name", "Age", "City"};
text_csv_table* table = text_csv_new_table_with_headers(headers, NULL, 3);
```

Creates a new table with specified column headers. Headers are treated as the first row and are excluded from the row count. A header map is built for fast column name lookup. Duplicate header names are not allowed.

#### 7.3.2 Row Operations

**Append Row:**
```c
const char* fields[] = {"Alice", "30", "New York"};
text_csv_row_append(table, fields, NULL, 3);
```

Adds a new row with the specified field values to the end of the table. The first row added sets the column count for the table. Subsequent rows must have the same number of fields (strict validation). All field data is copied to the arena.

**Insert Row:**
```c
const char* fields[] = {"Bob", "25", "San Francisco"};
text_csv_row_insert(table, 1, fields, NULL, 3);  // Insert at index 1
```

Inserts a new row at the specified index, shifting existing rows right. The index can equal `row_count`, which is equivalent to appending.

**Remove Row:**
```c
text_csv_row_remove(table, 0);  // Remove first data row
```

Removes the row at the specified index, shifting remaining rows left. If the table has headers, the header row (index 0) cannot be removed.

**Replace Row:**
```c
const char* fields[] = {"Charlie", "35", "Chicago"};
text_csv_row_set(table, 0, fields, NULL, 3);  // Replace first data row
```

Replaces the row at the specified index with new field values. The field count must match the table's column count.

**Clear Table:**
```c
text_csv_table_clear(table);
```

Removes all data rows from the table while preserving the table structure (headers if present, column count). This function automatically compacts the table to free memory from cleared rows.

#### 7.3.3 Column Operations

**Append Column:**
```c
text_csv_column_append(table, "Country", 0);  // Add "Country" column (null-terminated)
```

Adds a new column to the end of all rows. If the table has headers, the `header_name` parameter is required. All existing rows get an empty field added at the end.

**Insert Column:**
```c
text_csv_column_insert(table, 1, "MiddleName", 0);  // Insert at index 1
```

Inserts a new column at the specified index, shifting existing columns right. The index can equal the column count, which is equivalent to appending. When headers are present, all header map entries after the insertion point are automatically reindexed.

**Remove Column:**
```c
text_csv_column_remove(table, 0);  // Remove first column
```

Removes the column at the specified index from all rows, shifting remaining columns left. When headers are present, the header map entry is removed and remaining entries are reindexed.

**Rename Column:**
```c
text_csv_column_rename(table, 0, "FullName", 0);  // Rename first column
```

Renames a column header. This function only works if the table has headers. The new header name must not duplicate an existing header name.

#### 7.3.4 Field Operations

**Set Field Value:**
```c
text_csv_field_set(table, 0, 1, "31", 0);  // Set row 0, column 1 to "31"
```

Sets the value of a field at specified row and column indices. The field data is copied to the arena. If the field was previously in-situ (referencing the input buffer), it will be copied to the arena. If `field_length` is 0 and `field_data` is not NULL, it is assumed to be a null-terminated string.

#### 7.3.5 Utility Operations

**Clone Table:**
```c
text_csv_table* clone = text_csv_clone(table);
```

Creates a deep copy of the table, allocating all memory from a new arena. The cloned table is completely independent of the original.

**Compact Table:**
```c
text_csv_table_compact(table);
```

Moves all current table data to a new arena and frees the old arena. This releases memory from old allocations that may have been left behind due to repeated modifications. This function is automatically called by `text_csv_table_clear()`, but can also be called independently.

#### 7.3.6 Performance Characteristics

**Row Operations:**
- **Append**: O(1) amortized (O(n) when capacity grows)
- **Insert**: O(n) where n is the number of rows after insertion point
- **Remove**: O(n) where n is the number of rows after removal point
- **Set**: O(1) per field
- **Clear**: O(1) (compaction is O(n) but amortized)

**Column Operations:**
- **Append**: O(n) where n is the number of rows
- **Insert**: O(n×m) where n is the number of rows and m is the number of columns after insertion point
- **Remove**: O(n×m) where n is the number of rows and m is the number of columns after removal point
- **Rename**: O(1) (header map lookup is O(1) average case)

**Field Operations:**
- **Set**: O(1) per field

**Utility Operations:**
- **Clone**: O(n×m) where n is the number of rows and m is the average number of columns
- **Compact**: O(n×m) where n is the number of rows and m is the average number of columns

All mutation operations are atomic — they either complete successfully or leave the table unchanged. Memory allocations are performed before any state changes, ensuring no partial modifications.

---

## 8. Streaming Parser Events

The streaming parser emits events for:

- **Structure events**: `RECORD_BEGIN`, `RECORD_END`, `END`
- **Data events**: `FIELD` (with field data and length)

Each event includes position information (row index, column index) for error reporting and context.

---

## 9. In-Situ / Zero-Copy Parsing

An optional in-situ parsing mode allows the DOM to reference slices of the input buffer directly, avoiding copies for fields that don't require transformation. This mode requires the input buffer to remain valid for the lifetime of the DOM.

**When In-Situ Mode Works:**

In-situ mode is used for a field when **all** of the following conditions are met:

1. **`in_situ_mode` is enabled** in parse options
2. **`validate_utf8` is disabled** (`false`) — UTF-8 validation requires copying
3. **Single-chunk parsing** — the entire CSV data is provided in one `text_csv_parse_table()` call
4. **No transformation needed** — the field doesn't require unescaping (no doubled quotes or backslash escapes)
5. **Field not buffered** — the field didn't span chunks or contain newlines in quotes

**When In-Situ Mode Falls Back to Copying:**

Fields are automatically copied to arena-allocated memory when:

- UTF-8 validation is enabled
- Field requires unescaping (doubled quotes or backslash escapes)
- Field was buffered (spans chunks or contains newlines in quotes)
- Multi-chunk streaming parsing (fields are buffered)

**Lifetime Requirements:**

⚠️ **Critical**: When in-situ mode is enabled, the input buffer (`data` parameter to `text_csv_parse_table()`) **must remain valid for the entire lifetime of the table**. The table may contain pointers directly into this buffer.

**Example Usage:**

```c
// Input buffer that will remain valid
const char* csv_data = "Name,Age\nJohn,30\nJane,25";
size_t csv_len = strlen(csv_data);

// Enable in-situ mode
text_csv_parse_options opts = text_csv_parse_options_default();
opts.in_situ_mode = true;
opts.validate_utf8 = false;  // Required for in-situ mode

text_csv_table* table = text_csv_parse_table(csv_data, csv_len, &opts, NULL);

// Fields may point directly into csv_data
const char* name = text_csv_field(table, 0, 0, NULL);

// CRITICAL: csv_data must remain valid until table is freed
text_csv_free_table(table);
```

---

## 10. Error Reporting

The library provides comprehensive error information:

- **Error codes**: Stable error codes for programmatic handling
- **Human-readable messages**: Descriptive error messages
- **Position information**: Byte offset, line number, and column number
- **Row/column indices**: Row and column indices (0-based)
- **Enhanced diagnostics** (optional):
  - Context snippet showing the error location
  - Caret positioning within the snippet

Error context snippets are dynamically allocated and must be freed via `text_csv_error_free()`.

Common error cases:

- Unterminated quoted field (EOF inside quotes)
- Invalid escape sequence (for backslash-escape dialect)
- Unexpected quote in unquoted field (when disallowed)
- Record/field size limit exceeded
- UTF-8 validation failure (when enabled)
- Too many columns/rows

---

## 11. Round-Trip Correctness

The library guarantees round-trip stability: when parsing and writing with the same dialect, parse → write → parse results in identical field values. This is critical for applications that need to preserve exact CSV data.

---

## 12. Design Philosophy

The library prioritizes **correctness over simplicity**:

1. **Strict by default**: Strict CSV compliance is the default; extensions are explicit opt-ins
2. **Dialect is part of correctness**: Parsing without a dialect is undefined; safe defaults are provided
3. **Streaming-first capability**: Handle huge inputs with bounded memory
4. **No hidden allocations**: Explicit arena ownership and clear lifetimes
5. **Round-trip stability**: Output must re-parse to the same fields under the same dialect
6. **Precise errors**: Make it obvious *where* and *why* parsing failed

---

## 13. Getting Started

Include the umbrella header:

```c
#include <ghoti.io/text/csv.h>
```

For fine-grained control, include specific headers:

```c
#include <ghoti.io/text/csv/csv_core.h>   // Core types and options
#include <ghoti.io/text/csv/csv_table.h>  // DOM parsing and manipulation
#include <ghoti.io/text/csv/csv_stream.h> // Streaming parser
#include <ghoti.io/text/csv/csv_writer.h> // Writer
```

Comprehensive usage examples are provided in the `examples/` directory.

---

## 14. Future Work

The following features are planned for future releases:

### 14.1 Advanced Header Features

- **Optional indexing**: Fast header lookup on huge column sets using optimized data structures
- **Lazy table mode**: Row offsets with on-demand field decode for memory-efficient access to large files

### 14.2 Schema and Type Inference

- **Schema inference helpers**: Type sniffing with configurable rules for automatic type detection
- **Type validation hooks**: Lightweight validation framework (not a replacement for a database)

### 14.3 Integration Helpers

- **Dialect presets**: Pre-configured dialects for common data pipelines (BigQuery-style CSV quirks, Excel exports, etc.)
- **Format detection**: Automatic dialect detection from input samples
