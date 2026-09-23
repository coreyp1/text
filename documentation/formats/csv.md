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
  This held only for an unquoted final field until recently; see
  [Deviations](#csv-deviations).

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
`context_radius_bytes` of 40 either side. Clear `enable_context_snippet` to
suppress them and the allocation they need; `0` for the radius means the
default, as it does for the limits above. Every limit in the table is checked
against a document that should exceed it, at the boundary - `max_rows` was
computed and never compared until that check was written.

## Save

The writer emits RFC 4180 by default: comma delimiter, double-quote quoting, doubled
quotes for escaping, and quoting applied only where the field requires it -
`quote_if_needed`. A field is quoted when it contains the delimiter, a quote,
or a line break. `quote_all_fields` and `quote_empty_fields` force the issue
where a downstream consumer is fussier than the format.

`quoting` names the policy as a whole, with the four values Python's `csv`
module uses: `GTEXT_CSV_QUOTE_MINIMAL` (the default, and RFC 4180's own rule),
`_ALL`, `_NONNUMERIC` and `_NONE`. `MINIMAL` is zero, so a zero-initialized
options structure means what it always meant, and `quote_all_fields` still wins
when set, so code written before the enum existed behaves as it did.

`NONNUMERIC` asks a question about the bytes rather than about a type. Nothing
in this module infers types - fields are bytes, deliberately - so where Python
asks whether the value is an `int` or a `float`, this asks whether the text
would be read as a number:

    [+-]? ( digits ( '.' digits? )? | '.' digits ) ( [eE] [+-]? digits )?

matched against the whole field. Narrower than C's `strtod`: no hex floats, no
`inf`, no `nan`, no surrounding space, and an empty field is not a number.
`+1`, `007`, `1.` and `.5` are, because spreadsheets write them. The policy can
only ever add quotes, never remove them, which matters when the dialect's
delimiter is one of the characters a number may contain - a `.` delimiter makes
`1.5` both numeric and unwritable bare, so it is quoted.

The newline string is configurable and `trim_trailing_empty_fields` exists
for consumers that treat a trailing delimiter as an error.

**The writer refuses a field it cannot represent.** Quoting is not a
presentation choice in CSV; it is the only thing that carries the delimiter, a
CR or an LF through a field. Neither escape mode is an alternative - both
`GTEXT_CSV_ESCAPE_DOUBLED_QUOTE` and `GTEXT_CSV_ESCAPE_BACKSLASH` concern the
quote character alone, so this parser reads `a\,b` as the two fields `a\` and
`b` under either. So a caller who clears `quote_if_needed`, `quote_all_fields`
and `quote_empty_fields` and then writes such a field has asked for a document
that cannot exist, and gets `GTEXT_CSV_E_UNQUOTABLE_FIELD`.

It used to get `GTEXT_CSV_OK` and bytes that said something else. A field
holding a comma read back as two fields; one holding a newline as two records;
one holding a quote produced `a""b`, which is four characters to RFC 4180 and
is refused outright by this library's own parser at its default
`allow_unquoted_quotes`. None of that was reachable under the default options,
where those characters force quoting.

`gtext_csv_write_table()` checks the whole table before the first byte reaches
the sink, so a refusal leaves the sink untouched rather than half a file. The
streaming writer is handed one field at a time and has nothing to look ahead
at, so there the refusal is per-field and whatever was already written stays
written.

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

**Fixed: a quoted final field no longer needs a trailing newline.**

`"a"`, `a,"b"` and `"a","b"` were all rejected with "Unterminated quoted
field". The same documents with a trailing newline parsed, and an unquoted
final field parsed without one, so the page's claim that the last record need
not end in a line break was true only for part of the grammar. RFC 4180 §2
rule 2 permits the omission and rule 5 permits any field to be quoted, so
these are ordinary documents - and a file exported without a trailing newline
whose last column contains a comma is a common shape.

The parser holds a quote seen inside a quoted field in an undecided state,
because the next character says whether it closed the field or began a doubled
`""` escape. End of input was treated as leaving that state unresolved, when in
fact it resolves it: there is no next character, so the quote was the closing
one. A field with no closing quote at all is still an error, and so is a
backslash escape with nothing after it.

The differential fuzzer could not have found this. It compares the table
parser against the streaming parser, and both reach the same
`gtext_csv_stream_finish()`, so they agreed with each other on the wrong
answer. Every case here was cross-checked against Python's `csv` module
instead, and is pinned in `tests/test-rfc-conformance.cpp`.

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

**Fixed: `allow_unquoted_newlines` now has one meaning.** It was recorded here
as incoherent - the table parser kept a trailing CRLF as field content while
treating a CRLF mid-document as a record separator, and the streaming parser
disagreed with both depending on where a chunk began.

The meaning was recovered from the commit that first implemented it, where the
check sits *after* the branch that ends a record on a complete newline
sequence:

> A CR or LF that this dialect does **not** accept as a line terminator is
> field content rather than an error.

Which bytes are terminators is what `accept_lf`, `accept_crlf` and `accept_cr`
decide. The option never overrides them; it only says what to do with a CR or
LF that is left over.

The per-character path had always implemented that. The bulk scanner
`csv_stream_scan_unquoted_field_ahead()` had taken the option to mean that a
recognized terminator becomes content too, which cannot be right - if a
terminator never ends a record, no record can end - and that is what produced
the self-contradiction. It now returns on a complete newline sequence
regardless of the option.

With `accept_cr` false, which is the default:

| Input | Option off | Option on |
|---|---|---|
| `a,b\rc` | rejected, bare CR | one record, second field `b\rc` |
| `aB\r\n` | one record, field `aB` | unchanged - CRLF is a terminator |
| `a\r\nB\r\n` | two records | unchanged |
| `a\r\r\nb` | rejected | `a\r` then `b` - the bare CR is content, the CRLF ends the record |

Setting `accept_cr` makes the first row split into two records instead, which
is the point: the dialect decides what a terminator is, and the option decides
what happens to everything else.

The fuzzer compared every dialect option except this one and
`treat_first_row_as_header`. It now compares this one too: 2,682,853
executions with no disagreement between the table parser and the chunked
stream.

**Fixed: an escaped quote closed the field.** Inside a quoted field `""` is
a literal quote, and the field runs on until a single quote closes it
(RFC 4180 §2). Two branches in the stream state machine read a doubled quote
followed by a delimiter as the end of the field, and said so in a comment, so
the remainder was read as an unquoted field holding a stray quote and refused
with "Unexpected quote in unquoted field". `"a"",b"`, `"x"", ""y"` and
`"{""a"": [1, 2]}"` - JSON in a CSV column, which is how it turned up - were
all rejected. The state machine underneath was already right: `QUOTED_FIELD`
sees a quote, moves to `QUOTE_IN_QUOTED`, and decides between an escape and a
close by what follows. Those two branches were fighting it.

This is a behavior change, in the stricter direction.
`field1,"text"",field2` used to parse as three fields and is now reported as
an unterminated quoted field, because nothing ever closes it. Python's `csv`
is lenient and returns `field1` and `text",field2`; refusing is the reading
this parser takes elsewhere. Every other value now matches Python exactly.

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

**Reach of the oracles, and where it ends.** `make conformance-csv` scores
this parser against [csv-spectrum](https://github.com/maxogden/csv-spectrum),
cloned on first use and pinned to the commit named in
`tools/conformance/CSV_SUITE_COMMIT`. Of the twelve cases it ships, **eleven
are checkable and all eleven pass** under
`gtext_csv_parse_options_default()`, so the figure to quote is "11 of the 11
checkable, out of 12 shipped" and never a bare 100%. The twelfth,
`location_coordinates`, is excluded because the suite's own fixture is a bare
object where every other one is an array of rows, and the phone number in it
is not the number in its own csv - a defect in the suite rather than an
answer about this parser, so the scorer reports it as unusable instead of
counting it against the parser. The scorer reports separately what
`dialect.allow_unquoted_quotes` would change, so a disagreement that is a
dialect rather than the grammar reads as one.

That corpus is what found the escaped-quote defect above: before it, the
score was 9 of 12. This page used to say there was no external CSV corpus and
that the answer was "unknown except where this page names it". That was true
when it was written and stopped being true in the same commit that fixed the
defect, which updated the README and not this page.

**The pass rate is scored, not enforced.** `CSS_MIN` and `CSS_MIN_CORPUS`
make the scorer exit non-zero below a given pass rate or corpus fraction, and
the `conformance-csv` target sets neither, so the gate prints its number and
then succeeds whatever the number is. `conformance-roundtrip` and
`conformance-fastpath` pass floors of 100; `conformance`, `conformance-json`
and `conformance-csv` pass none. A score nobody is held to is a report rather
than a gate.

There is still **no differential test against another parser**. That matters
less for CSV than it would for JSON, because there is no authority to be
differentially correct against - but Python's `csv` module encodes a
widely-held reading of the ambiguous cases, and the comparison against it
recorded above was made by hand, once, rather than by anything that runs
again.

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
- ~~**Dialect sniffing.**~~ **Added**: `gtext_csv_sniff()`. It guesses by
  *parsing* with each candidate rather than by counting characters, which is the
  difference that matters - a frequency count reads the `;` in
  `name,"Smith; John",42` as structure, and cannot see that `a,b;c / d,e,f;g`
  is regular under `;` and ragged under `,` even though `,` is twice as common.
  Parsing also means the sniffer cannot disagree with the parser that reads the
  document next, because it is the same one. The design was checked by replacing
  it with a frequency count: three tests fail, and they are the three written for
  those shapes.

  It tries `,` `;` tab `|` `:` and the two quote characters, reads the newline
  style directly rather than scoring it, and drops a truncated final record so
  that sniffing the first few kilobytes of a large file works. **It refuses
  rather than guessing** when no candidate splits the sample or when two explain
  it equally well, because a confident wrong answer reads a whole file into the
  wrong shape with nothing for the caller to check.

  `treat_first_row_as_header` is deliberately not guessed: whether the first row
  names the columns is a question about meaning rather than grammar, and no
  arrangement of bytes settles it. Python's `has_header()` guesses and is
  unreliable for that reason.

  Named dialects are exported too: `gtext_csv_dialect_tsv()`, `_semicolon()`,
  `_backslash_escape()`, `_excel()` and `_permissive()`, each differing from
  `gtext_csv_dialect_default()` only in the field it names.
- **Type inference.** Fields are bytes. Nothing converts them to numbers or
  dates, by design. `GTEXT_CSV_QUOTE_NONNUMERIC` asks whether a field's text
  *spells* a number when deciding whether to quote it, which is a question about
  bytes and not a type.

A pull reader used to be listed here and is now implemented:
`gtext_csv_reader_new()`, `gtext_csv_reader_feed()`, `gtext_csv_reader_next()`
and `gtext_csv_reader_free()`. It wraps the streaming parser, so it adds no
grammar, and it copies each event's bytes into its queue - the push callback's
`data` points into the caller's chunk or into a field buffer about to be reused,
so a reader that kept the pointer would hand back overwritten memory. That is
pinned by a test which overwrites the chunk in place after feeding it, and the
copy was confirmed load-bearing by removing it: three tests fail, one reporting
every field as the same reused buffer.

---

Back to \ref format_references "Format and specification references".
