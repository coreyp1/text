@page format_csv CSV

# CSV

CSV has a specification that almost nobody writes and almost nobody reads.
RFC 4180 describes one dialect; real files are a family of dialects that
disagree about quoting, line endings, whitespace and whether rows must be
rectangular. This parser implements RFC 4180 as its default and exposes the
disagreements as explicit options rather than guessing. Back to
\ref format_references "Format and specification references".

## Normative references

- **Specification:** [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180),
  *Common Format and MIME Type for Comma-Separated Values (CSV) Files*,
  October 2005. It is Informational, not a Standards Track document, and it
  says so: it documents the format "as it is used" rather than defining it.
- **Erratum:** RFC 4180 was updated by
  [RFC 7111](https://www.rfc-editor.org/rfc/rfc7111) only to add fragment
  identifiers for selecting rows and columns from a CSV resource. That is a
  URI concern, not a grammar one, and is not implemented here.
- Everything outside RFC 4180's grammar - the semicolon dialect, tab
  separation, backslash escaping, comment lines, ragged rows - has **no
  specification at all**. Where this page describes those, the description is
  the specification as far as this library is concerned.

Parser allocations are owned by the returned `GTEXT_CSV_Table` and released
by `gtext_csv_free_table()`. Error context snippets are separately owned and
released by `gtext_csv_error_free()`.

## Parts implemented

**The RFC 4180 grammar (§2).** Records separated by CRLF; fields separated by
commas; fields optionally enclosed in double quotes; a quoted field may
contain commas, CRLF and doubled quotes. All of it, and it is the default
dialect.

RFC 4180 §2 rules 1-7 are enforced with two deliberate relaxations, both on
by default because refusing them would reject most CSV in existence:

- **Bare LF is accepted as a record separator.** §2 specifies CRLF. `accept_lf`
  defaults to `true`, `accept_crlf` to `true`. Turning `accept_lf` off gives
  the letter of the RFC.
- **The last record need not end in a line break**, which §2 already permits.

**Bare CR is *not* accepted by default.** `accept_cr` defaults to `false`, so
a classic Mac-style file fails with `GTEXT_CSV_E_INVALID` and the message
"Newline in unquoted field" rather than being silently read as one enormous
row. This is the one place the parser is stricter than most, and it is
deliberate: a lone CR is far more often a corrupted CRLF than an intended
separator.

**Dialect options**, all on `GTEXT_CSV_Dialect`:

| Option | Default | Effect |
|---|---|---|
| `delimiter` | `,` | any single byte; `;` and tab are the common alternatives |
| `quote` | double quote | any single byte |
| `escape` | `DOUBLED_QUOTE` | or `BACKSLASH`, or `NONE` |
| `newline_in_quotes` | `true` | RFC 4180 §2 rule 6 |
| `accept_lf` | `true` | relaxation, see above |
| `accept_crlf` | `true` | RFC 4180 §2 rule 1 |
| `accept_cr` | `false` | see above |
| `trim_unquoted_fields` | `false` | RFC 4180 §2 rule 4 forbids trimming |
| `allow_space_after_delimiter` | `false` | when off, the space is kept as field data |
| `allow_unquoted_quotes` | `false` | a quote inside a bare field |
| `allow_unquoted_newlines` | `false` | |
| `allow_comments` | `false` | with `comment_prefix`, default `"#"` |
| `treat_first_row_as_header` | `false` | RFC 4180 §2 rule 3, and the `Content-Type` `header` parameter |
| `header_dup_mode` | `FIRST_WINS` | or `ERROR`, `LAST_WINS`, `COLLECT` |

Note that `allow_space_after_delimiter` off does **not** make ` b` an error in
`a, b`; it makes the space part of the field, which is what RFC 4180 §2 rule 4
requires ("Spaces are considered part of a field and should not be ignored").
The option controls whether the space may be *discarded*, not whether it may
appear.

**Irregular rows.** RFC 4180 §2 rule 4 requires every record to contain the
same number of fields. Real exports routinely violate it, so the table model
accepts ragged rows, reports them through `gtext_csv_has_irregular_rows()`,
and offers `gtext_csv_normalize_rows()` and `gtext_csv_normalize_to_max()` to
square them up. Nothing is padded or truncated behind the caller's back.

**BOM.** A leading UTF-8 BOM is stripped by default; `keep_bom` retains it as
field data.

**Streaming.** An event-driven parser covering the same grammar, for inputs
that should not be materialized as a table. The result does not depend on how
the caller divides the input into chunks.

**Files.** `gtext_csv_parse_file()` and `gtext_csv_write_file()`. Reading is
incremental, so a pipe or `/dev/stdin` works and `max_total_bytes` is enforced
before the whole file is in memory. Writing is atomic - a temporary file
beside the destination, renamed over it once complete - so an interrupted
write leaves the previous file intact.

## Limits

| Option | Default |
|---|---|
| `max_rows` | 10,000,000 |
| `max_cols` | 100,000 |
| `max_field_bytes` | 16 MiB |
| `max_record_bytes` | 64 MiB |
| `max_total_bytes` | 1 GiB |

Error context snippets are generated by default, with
`context_radius_bytes` of 40 either side.

## Save

The writer emits RFC 4180 by default: comma delimiter, double-quote quoting, doubled
quotes for escaping, and quoting applied only where the field requires it -
`quote_if_needed`. A field is quoted when it contains the delimiter, a quote,
or a line break. `quote_all_fields` and `quote_empty_fields` force the issue
where a downstream consumer is fussier than the format.

The newline string is configurable and `trim_trailing_empty_fields` exists
for consumers that treat a trailing delimiter as an error.

## Compliance checklist

| Area | Supported | Rejected / limitation |
|---|---|---|
| Comma-delimited records | yes, default | |
| CRLF separator | yes, default | |
| Bare LF | accepted by default | `accept_lf = false` for strict §2 |
| Bare CR | **off by default** | `GTEXT_CSV_E_INVALID`, "Newline in unquoted field" |
| Quoted fields | yes | |
| Delimiter inside quotes | yes | |
| Line break inside quotes | yes, `newline_in_quotes` | |
| Doubled quote escape | yes, default | |
| Backslash escape | opt-in dialect | not RFC 4180 |
| Unterminated quote | | `GTEXT_CSV_E_UNTERMINATED_QUOTE` |
| Quote in an unquoted field | opt-in | `GTEXT_CSV_E_UNEXPECTED_QUOTE` |
| Stray quote inside a quoted field | | `GTEXT_CSV_E_INVALID`, "must be followed by quote, delimiter, or newline" |
| Ragged rows | accepted and reported | never silently padded |
| Empty input | 0 rows, not an error | |
| Input of a single newline | 0 rows, not an error | |
| Trailing delimiter | yields a final empty field | |
| Leading BOM | stripped by default | |
| Invalid UTF-8 | rejected by default | `GTEXT_CSV_E_INVALID_UTF8`; streaming does not check |
| Size limits | five, configurable | `GTEXT_CSV_E_LIMIT`, `E_TOO_MANY_ROWS`, `E_TOO_MANY_COLS` |

@anchor csv-deviations
## Deviations

**Fixed: `validate_utf8` now validates UTF-8.**

`GTEXT_CSV_Parse_Options::validate_utf8` defaults to `true` and is documented
as "Validate UTF-8 sequences in input". For a long time it did nothing of the
kind. A validator existed and was correct - `csv_validate_utf8()` in
`src/csv/csv_utils.c`, exercised directly by seven cases in
`tests/test-csv.cpp` - but no production path called it. The only place the
parser read the option was to decide whether in-situ mode could alias the
input buffer, so the option's sole observable effect was to *disable*
zero-copy. `GTEXT_CSV_E_INVALID_UTF8` was declared in the public enum and
raised nowhere.

`gtext_csv_parse_table()` now validates the whole buffer in one pass, after
BOM stripping and before tokenizing, and returns
`GTEXT_CSV_E_INVALID_UTF8` with the byte offset of the first bad sequence.
Lone `FF` bytes, truncated sequences, stray continuation bytes and overlong
encodings are all rejected; `validate_utf8 = false` still accepts them.

**Fixed: the streaming parser validates too.** `gtext_csv_stream_new()` takes
the same options struct and used to ignore `validate_utf8` entirely, so a
caller who asked for validation and fed the document in pieces got none. A
sequence can straddle a chunk boundary, so the bytes seen so far are now
carried in the stream and the verdict is deferred until the rest arrives; a
sequence still open when the input ends is reported as truncated. The rules
are the same ones `csv_validate_utf8()` applies, because the two parsers
disagreeing about which documents are well-formed is exactly what the
differential fuzzer is there to catch.

**Fixed: surrogate halves were accepted.** `ED A0 80` through `ED BF BF`
encode U+D800 to U+DFFF, which RFC 3629 §3 excludes from UTF-8 - they exist
only so UTF-16 can address the supplementary planes, and decoding one hands
the caller something that is not a character. Both parsers now reject them.
Overlong encodings, lone `FF` bytes, stray continuation bytes, sequences
beyond U+10FFFF and truncated sequences were already rejected.

**Fixed: the streaming parser no longer depends on where chunks are split.**
`gtext_csv_stream_feed()` accepts a document in pieces of any size, so the
result has to be the same whatever those sizes are. Four things made it
otherwise, all found by parsing the same document three ways - table parser,
one whole feed, one byte at a time - and comparing:

| Case | Was | Now |
|---|---|---|
| A field spanning two feeds | kept a pointer into the freed chunk: `"a, "` then `"b"` gave `" \0"` | copied out at the boundary |
| CRLF split between the CR and the LF | `GTEXT_CSV_E_INVALID`, "Newline in unquoted field" | one newline |
| A BOM arriving in pieces | not stripped | stripped |
| `""` fed a byte at a time | a literal `"` | an empty field |

The first is the one to know about: feeding consecutive slices of a single
long-lived array hides it completely, because the stale pointer stays valid by
accident. Every real caller reads into one buffer and refills it.

**Fixed: a trailing delimiter no longer loses its field.** RFC 4180 §2 says
only a line break ends a record, so `a,` is two fields and the second is
empty. A file with no trailing newline used to drop it.

**Fixed: three dialect options did nothing at all.** `trim_unquoted_fields`,
`allow_space_after_delimiter` and `newline_in_quotes` were declared in
`GTEXT_CSV_Dialect`, documented, given defaults - and read by no parser code
anywhere. Setting any of them changed no behavior whatsoever. They were found
by checking every field of the dialect struct for a consumer rather than by a
failing test, which is why they had survived so long.

| Option | Now |
|---|---|
| `trim_unquoted_fields` | strips leading and trailing spaces and tabs from unquoted fields; quoted fields are untouched, since the quotes are how the document says the spaces are data |
| `allow_space_after_delimiter` | skips spaces and tabs between a delimiter and the field, before deciding whether the field is quoted, so `a, "x,y"` reads as two fields |
| `newline_in_quotes` | when false, a newline inside a quoted field is rejected with `GTEXT_CSV_E_INVALID` |

Trimming happens at the single point where every field is emitted, so the
table and streaming parsers cannot disagree about it, and it only narrows the
view - in-situ fields stay in-situ.

**Fixed: an empty quoted field mid-record depended on the chunk boundary.**
The streaming parser had a special case treating `""` as a doubled quote - a
literal `"` - when it entered the quote state at a chunk boundary with an
empty field. RFC 4180 §2 makes `a,"",b` three fields whose middle one is
empty; a field holding one literal quote is written `""""`. The table parser
and larger chunk sizes both read it correctly, so the result depended on where
the reader's buffer happened to end. The differential fuzzer found it.

**Fixed: `GTEXT_CSV_ESCAPE_BACKSLASH` did not survive a round trip.** The
writer has escaped quotes as `\"` since it was written and had tests for it;
the reader decoded those escapes only when the field happened to be buffered,
so on the zero-copy path the backslashes stayed in the field. Only the writer
side had ever been tested. Two further faults were behind it: a chunk ending
on a backslash discarded everything accumulated so far, because the state
holding that position was missing from the list of states whose field survives
a chunk, and an attempt to decode in the second of two places double-decoded,
turning `p\\q` into `pq`. Decoding now happens in exactly one place, and the
quoted-field handler buffers the field as soon as it sees a backslash so that
place is always reached.

**Open: `allow_unquoted_newlines` is not coherent.** With it set, the table
parser keeps a trailing CRLF as field content but treats a CRLF in the middle
of the document as a record separator, and the streaming parser disagrees with
both depending on where a chunk starts - the bulk scanner honors the option
and the per-character path does not. The option is off by default. Deciding
what it should mean is a prerequisite to fixing it, so it is recorded here
rather than guessed at, and the fuzzer's differential check excludes it.

**Bare CR rejected by default**, as described above - stricter than Python's
`csv` module, which accepts it.

## Tested scope

**Fixtures** live in `tests/data/csv/`, hand-written and grouped by intent:
`strict/` for the RFC 4180 grammar, `dialects/` for semicolon, TSV and
backslash-escape variants, `edge-cases/` for BOM, the three line endings,
empty tables, empty and consecutive-empty fields and ragged rows, and
`invalid/` for each rejection.

**Direct behavioral check.** The compliance checklist was produced by parsing
each literal input with `gtext_csv_parse_options_default()` and recording the
status and the resulting field bytes. The UTF-8 defect above was found that
way, by checking a claim rather than reading a code path, and the fix is
pinned by four cases in `tests/test-csv.cpp`.

**Fuzzing.** `tests/fuzz/fuzz_csv.cpp` under libFuzzer with ASan and UBSan,
seeded from `tests/fuzz/corpus/csv/`. CSV's harness consumes the first *two*
input bytes as an options selector, since the delimiter and quote characters
have to come from somewhere for the dialect paths to be reachable.

It parses each document **both** ways and compares, so a wrong answer fails as
loudly as a crash. Chunk sizes come from the document's own bytes, letting the
fuzzer steer a boundary onto whichever byte breaks the parser, and each chunk
is copied through a scratch buffer that is overwritten immediately afterwards,
so a retained pointer is caught rather than tolerated.

Until September 2026 the harness called only `gtext_csv_parse_table()`. The
streaming parser - the most stateful code in the module - was never fuzzed at
all, which is why the chunk-boundary defects above survived. Reaching it took
coverage from 1744 edges to 2149 and immediately produced: a load of 2 from a
`_Bool`, a `malloc` of 12 GiB from a few hundred bytes, and two places where
the parser made no progress and spun until an unrelated limit tripped - 850ms
for fifteen bytes. All fixed.

**Reach of the oracles, and where it ends.** There is **no external CSV
corpus and no differential test against another parser**. This matters less
for CSV than it would for JSON, because there is no authority to be
differentially correct against - but it matters more than nothing, because
Python's `csv` module and `csv-spectrum` encode a widely-held reading of the
ambiguous cases, and comparing against them would say which of this parser's
choices are unusual. Today the answer is "unknown except where this page
names it".

**Round-trip is now pinned.** `CsvRoundTrip.WriteThenReparsePreservesFields`
in `tests/test-csv.cpp` parses, writes and reparses twelve documents chosen
for difficulty - embedded delimiters, doubled quotes, embedded newlines,
empty and consecutive-empty fields, a trailing delimiter, preserved
surrounding spaces, CRLF, ragged rows, multi-byte UTF-8, a field whose only
content is a quote character, and a record of nothing but delimiters - and
requires the fields to come back identical. The property held before the test
was written, so this pins existing behavior rather than recording a fix.

## Not implemented

- **RFC 7111 fragment identifiers** (`#row=`, `#col=`, `#cell=`). Out of
  scope: it is a URI feature, not a parsing one.
- **Dialect sniffing.** There is no equivalent of Python's `csv.Sniffer`;
  nothing inspects a document to guess its delimiter. Named dialects are now
  exported, though: `gtext_csv_dialect_tsv()`, `_semicolon()`,
  `_backslash_escape()`, `_excel()` and `_permissive()`, each differing from
  `gtext_csv_dialect_default()` only in the field it names.
- **Type inference.** Fields are bytes. Nothing converts them to numbers or
  dates, by design.
- **A coherent `allow_unquoted_newlines`**, as above.

---

Back to \ref format_references "Format and specification references".
