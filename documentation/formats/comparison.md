@page format_comparison Comparison with other libraries

# Comparison with other libraries

The goal this page measures against is a specific one: that a project could
choose `ghoti.io/text` *instead of* libyaml, RapidJSON, libcsv or PyYAML, and
not give anything up that it depended on. That is a higher bar than "parses
the format correctly", which the \ref format_references "format pages" already
cover. This page is about what a migrating caller would find missing.

Everything below was checked by compiling and running against the library, not
by reading headers. Where a number appears, the program that produced it is
named. Claims about *other* libraries are from their published feature sets
and are marked as such; only this library and the Python standard library were
measured here, on one machine, in one sitting.

---

## Summary: what would block an adoption today

Ordered by how many callers it stops, not by how hard it is to fix.

| # | Finding | Scope | Severity |
|---|---|---|---|
| 1 | ~~No `LICENSE` file~~ **fixed suite-wide**: all nine are LGPL-3.0-only | suite-wide | was: blocks all adoption |
| 2 | ~~JSON Schema silently ignores 14 standard keywords~~ **fixed** | JSON | was: silently wrong results |
| 3 | ~~The `release` build is compiled `-O0`~~ **fixed**: release is `-O2`, debug `-O0` | suite-wide | was: 1.5x to 2.1x slower |
| 4 | No custom allocator hook in any format | JSON parse done; CSV, YAML open | blocks embedded and arena callers |
| 5 | JSON parses at roughly a third of Python's stdlib speed | JSON | loses on throughput |
| 6 | No pull/iterator reader for JSON or CSV | JSON, CSV | forces an inverted control flow |
| 7 | ~~Thread-safety is documented for CSV only~~ **fixed** | JSON, YAML | was: unanswerable question |
| 8 | ~~No dialect presets~~ **fixed**; no sniffing | CSV | was: small friction, common need |

Findings 1 and 3 are properties of the shared template rather than of `text`,
so they belong in the suite's `SUITE-TODO.md` rather than being fixed in this
repository alone. See section 12 of `CONVENTIONS.md` for why.

---

## 1. The license was the first blocker - fixed suite-wide

**Every library is now LGPL-3.0-only.** Each carries `COPYING` (GPL-3.0) and
`COPYING.LESSER` (LGPL-3.0), because LGPLv3 is drafted as additional
permissions on top of GPLv3, and every file under `src/` and `include/`
carries an SPDX identifier and the notice. The README names it.

As found: there was no `LICENSE` file in this repository. Source files carried
`Copyright 2026 by Corey Pennycuff` and no grant of any kind, which under
default copyright means no one may use the library at all. That was true of
six of the nine libraries, and it was item 7 in the suite TODO.

It was listed first because the stated reason for building this library is
that the alternatives have licenses that do not suit. A library with no
license is strictly worse on that axis than the libraries it means to
replace.

**The resolution moves along that axis rather than to the end of it, and
deliberately so.** libyaml is MIT, RapidJSON is MIT and PyYAML is MIT; LGPL
is more restrictive than all three, so on the narrow question this section
asked - "is the license a reason not to adopt?" - LGPL is an improvement on
*no* license and a step back from the competition. That is the intended
trade. The suite is meant to support a commercial license alongside the open
one, and a permissive license gives a dual-license position nothing to sell:
there is no reason to pay for permission MIT already grants. LGPL's relink
obligation is what makes the paid license worth buying, which is the same
reason Qt moved from LGPLv2.1 to LGPLv3.

So an adopter who needs MIT terms is now a *customer* rather than a lost
cause, which is a different answer from the one this page originally
anticipated, not a better score on the same one. Dynamic linkers are
unaffected.

---

## 2. JSON Schema accepted schemas it did not enforce - fixed

**This has since been fixed**; the finding is kept because the reasoning is
what justifies the fix. `gtext_json_schema_compile()` now refuses a schema
that uses a standard keyword the engine does not enforce, failing with
`GTEXT_JSON_E_SCHEMA_UNSUPPORTED` and naming the keyword. See the
\ref format_json "JSON page" for the refused set and the opt-out.

As found: `gtext_json_schema_compile()` accepted a schema containing keywords
the engine does not implement, and `gtext_json_schema_validate()` then
returned `GTEXT_JSON_OK` for instances those keywords should reject. A caller who ports
a working draft-07 schema gets a validator that approves everything the
unimplemented half of the schema was meant to catch, with no error at compile
time and no warning at validation time.

The eleven keywords the header lists all work. These fourteen are accepted and
ignored, each verified by compiling a schema using it and validating an
instance that violates it:

| Keyword | Instance that should fail | Result |
|---|---|---|
| `$ref` | `123` against a string definition | passes |
| `allOf` | `123` against a string branch | passes |
| `anyOf` | `123` against a string branch | passes |
| `oneOf` | `123` against a string branch | passes |
| `not` | `123` against a negated integer | passes |
| `pattern` | `"zzz"` against `^a+$` | passes |
| `additionalProperties` | an extra property under `false` | passes |
| `uniqueItems` | `[1,1]` | passes |
| `multipleOf` | `7` against a multiple of 10 | passes |
| `exclusiveMaximum` | `5` against an exclusive 5 | passes |
| `if` / `then` | `99` against a maximum of 3 | passes |
| `contains` | `[1,2]` with no string | passes |
| `propertyNames` | a non-matching key | passes |
| `dependentRequired` | a missing dependent key | passes |

Ignoring a genuinely unknown keyword is correct JSON Schema behavior, and the
engine does that too: a made-up keyword is ignored, as it should be. The defect
is that standard keywords are indistinguishable from made-up ones, so the
engine cannot tell a caller that it did not understand the schema it was given.

This is the same failure shape as the `validate_utf8` defect fixed earlier in
this repository: an option that names a guarantee, and does not provide it,
with no way for a caller to notice. The remedy did not require implementing
the keywords: rejecting a schema that uses one converts a silent wrong answer
into a loud, actionable one, and it was a much smaller change than the
fourteen implementations. That is what was done.

**How this compares.** Full draft-07 or 2020-12 validation is the normal
offering elsewhere: `ajv`, `jsonschema` and `valijson` all implement `$ref`
and the applicator keywords, because without `$ref` a schema cannot be
recursive or modular.

**That gap is now closed.** `$ref` resolves same-document JSON Pointers,
recursion included, alongside the boolean applicators, `if`/`then`/`else`,
`contains`, `additionalProperties`, `propertyNames`, `prefixItems`,
`dependentSchemas` and draft-07's `dependencies`. What remains unimplemented
is `pattern` and `patternProperties` (a regular-expression engine is a
dependency decision), `unevaluated*` (needs annotation collection), the
dynamic-scope references, `format`, and the `content*` family - and each is
still refused rather than ignored.

---

## 3. The optimized build is not the one anyone gets - fixed

**`release` is now `-O2` and `debug` is `-O0`.** The level is chosen by
`OPT_CFLAGS`, and it is the one thing that separates the two builds; `-g`
stays in both, because a release nobody can read in a debugger is a release
nobody can diagnose.

As found: `CFLAGS` held a literal `-O0` and the `ifeq ($(BUILD),debug)` block
appended `-debug` to `BRANCH` and `VERSION_STRING` without changing how
anything was compiled. So `release` and `debug` differed only in the name on
the artifact, and the library a caller linked against after a plain `make` was
unoptimized. It was never a decision: the production build doubled as the
debugging build early on, and nothing revisited it.

What the old default cost, measured at the time on the same inputs by
rebuilding with `EXTRA_CFLAGS="-O2"`:

| Parser | `release` as it shipped (`-O0`) | `-O2` | Gain |
|---|---|---|---|
| JSON DOM parse | 37.4 MB/s | 57.5 MB/s | 1.5x |
| CSV DOM parse | 47.4 MB/s | 101.8 MB/s | 2.1x |

Two things about the move are worth recording, because neither is visible in
the diff.

**The sanitizer gate is pinned at `-O1`, not inherited.** It had inherited
the release level, which meant this change moved it from `-O0` to `-O2` as a
side effect. It is now `-O1` in `ASAN_UBSAN_FLAGS`, matching `make fuzz`,
which has always carried `-O1` on a command line it builds itself.

The first argument for inheriting was that a UB gate should compile the code
that ships. Measured, it buys nothing. One defect per program, so that halting
at the first finding cannot hide a later one:

| Defect | `-O1` | `-O2` |
|---|---|---|
| heap-use-after-free | caught | caught |
| stack-buffer-overflow | caught | caught |
| signed integer overflow | caught | caught |
| float-to-int overflow | caught | caught |
| strict-aliasing violation | **not caught** | **not caught** |

The last row is the one that decided it. Aliasing was the specific hazard the
inheriting argument named - inert at `-O0`, live at `-O2` - and no sanitizer
in this toolchain detects it at any level, so running the gate at the release
level never bought that coverage. The instrumented build is also not the
shipped binary in any case: it carries redzones and different inlining, so
"the level that ships" was less true than it sounded.

What pinning does buy is that the gate and the fuzzers share one codegen, so a
finding reproduces between them, and that neither changes silently the next
time the release level does.

**What `-O2` broke was a test, not a compile.** The 54 sources compile clean,
and that is a real zero rather than an unasked question - the same probe gives
no diagnostics at `-O0` and two hard errors at `-O2` under these flags, so the
warnings that need the optimizer's dataflow are armed and have nothing to say
here. Four assertions in `CsvDialectPresets` compared dialect structs with
`memcmp` over `sizeof`, which reads the eleven padding bytes the struct
carries, and padding holds indeterminate values. Every named field agreed;
one pad byte did not. A test that compares structs bytewise is the failure
mode to look for when a library moves off `-O0`, because it is invisible to
`-Werror` and presents as a library regression.

---

## 4. Throughput

Measured with a 3.7 MB JSON document of 20,000 records and a 12.4 MB CSV of
200,000 rows, best of three runs each, library built `-O2` - which is now
simply `make`, where it once took an override - compared against
the Python standard library on the same machine and the same files.

| Parser | This library | Python stdlib | Ratio |
|---|---|---|---|
| JSON to DOM | 57.5 MB/s | 151.5 MB/s (`json.loads`) | 0.38x |
| CSV to DOM | 101.8 MB/s | 110.7 MB/s (`csv.reader`) | 0.92x |

**CSV is competitive.** Parity with the Python C implementation is a
reasonable place for a general-purpose C parser to sit.

**JSON is not.** Being 2.6x slower than Python's standard library is a poor
showing for a C library, and the gap against the libraries people pick JSON
parsers for is far larger: RapidJSON and yyjson are positioned an order of
magnitude above this, and simdjson one beyond that. A caller choosing a JSON
parser on speed will not choose this one.

A `gprof` profile of both parsers shows no single dominant hotspot; the cost is
spread across per-byte state-machine work. Two structural observations worth
following up, neither yet acted on:

- The CSV table parser runs on top of the streaming parser, paying an event
  callback per field and a position update per byte, at 17.4 million calls to
  `csv_stream_advance_position()` for the 12.4 MB input.
- `json_get_limit()` is called 1.8 million times parsing 3.7 MB, which is a
  limits lookup on the hot path rather than a value hoisted before the loop.

Both are guesses about where the time goes, stated as guesses. Neither has
been confirmed by changing the code and re-measuring, which is the only thing
that would settle it.

---

## 5. No custom allocator hook - JSON parse done, CSV and YAML open

**Partly addressed.** `GTEXT_JSON_Parse_Options::allocator` now routes the
whole JSON parse path - the arena, every DOM node, key and string in it, the
preserved number lexemes and the parser's transient buffers - through a
caller-supplied `GTEXT_Allocator`, with `gtext_json_free()` releasing through
the same one. `make check-allocators` fails the build if a converted file
calls `malloc`, `calloc`, `realloc` or `free` directly, so the coverage claim
is enforced rather than promised. Still open: the JSON writer, streaming
parser, Pointer, Patch and Schema, and all of CSV and YAML.

`GTEXT_Allocator` **is** cutil's `GCU_Allocator`, under a local name, which is
what `image`, `model` and `compress` do. For a while `text` declared its own
copy instead, because CONVENTIONS.md's "standalone by design" was read as
forbidding a cutil dependency; that was a misreading - a dependency inside the
suite is fine while the graph stays a DAG, and cutil is its root. The copy is
gone, `gtext_allocator_default()` returns `gcu_allocator_default()` rather than
reimplementing it, and no cast is needed to pass one library's allocator to
another.

As found: none of the three formats let a caller supply an allocator. There is no
`malloc`/`free` pair, no opaque user pointer, and no arena handle in any public
options struct. Internally the parsers do use arenas, so the machinery is
there; it is simply not reachable.

This is a hard requirement in several of the markets this library would be
adopted into: embedded targets that forbid `malloc` after startup, game engines
with frame allocators, and any host that wants allocation accounted per
subsystem. RapidJSON makes the allocator a template parameter, yyjson takes an
allocator struct, jansson has `json_set_alloc_funcs()`, and libyaml lets the
caller control the emitter buffer. It is the most commonly cited reason to
reject a C parser outright.

---

## 6. Push streaming exists everywhere; pull only in YAML

| Format | DOM | Push (feed) | Pull (next) |
|---|---|---|---|
| JSON | yes | `gtext_json_stream_feed()` | none |
| CSV | yes | `gtext_csv_stream_feed()` | none |
| YAML | yes | `gtext_yaml_stream_feed()` | `gtext_yaml_reader_next()` |

YAML's pull reader is the interface a caller wants when the consuming code owns
the loop, which is the usual case when parsing into an application's own types.
With only a push interface, a JSON or CSV caller has to invert control, hold
their own state machine, and reassemble structure across callbacks.

`gtext_yaml_reader_next()` shows the intended shape already exists in this
codebase, which makes the asymmetry an omission rather than a design position.
For JSON in particular, the pull reader is the interface that the fastest
competing libraries lead with.

---

## 7. Thread safety is documented once - fixed

`documentation/modules/CSV.md` states plainly that the module is not
thread-safe, that a table belongs to one thread, and that distinct tables may
be used concurrently because they share no state. That is exactly what a caller
needs to know.

Neither the JSON nor the YAML documentation said anything on the subject.

**Now closed.** Section 7 of the \ref core_module "Core module page" states the
rule for all three, with per-module sections on the JSON and YAML pages. The
answer was indeed the same for all three, but establishing that meant checking
rather than assuming, and the check found two things:

- `gtext_version_string()` raced with itself, formatting into a function-local
  static guarded by a second static flag. It returns a compile-time constant
  now, so the buffer and the race are both gone.
- Number formatting used to force the C locale, thread-locally where
  `uselocale()` existed and process-globally where it did not. It no longer
  changes the locale at all - nothing in the library calls `setlocale`,
  `uselocale`, `newlocale` or `localeconv` - so the compromise that used to be
  documented here is gone rather than mitigated. See Core.md.

A claim that YAML accessors lazily cached alias resolution was also written
and then removed, because reading the code showed they do not: aliases and
merge keys resolve during the parse, so concurrent readers of a finished
document are safe.

---

## 8. Per-format feature comparison

### JSON

Compared against nlohmann/json, RapidJSON, jansson and cJSON.

**Present, and competitive.** DOM with typed accessors; push streaming; a
writer with buffer and fixed-buffer sinks; file read and write; JSON Pointer
(RFC 6901); JSON Patch (RFC 6902); JSON Merge Patch (RFC 7386); duplicate-key
policy with four modes including collect-into-array; number handling that keeps
the original lexeme and offers exact `int64`, `uint64`, `double` and
string-backed big decimal; in-situ zero-copy parsing; depth, string, element
and total-size limits; JSONC comments, trailing commas, single quotes,
non-finite numbers and unescaped controls as opt-in extensions; canonical
output with sorted keys; error reporting with offset, line, column, a context
snippet and a caret.

That patch and pointer set is better than most C JSON libraries ship. The
duplicate-key modes and the preserved lexeme are genuinely uncommon and are
real advantages over cJSON and jansson.

**Missing.**

- A pull reader, as above.
- ~~Schema beyond the core subset, and honest failure when a schema exceeds
  it.~~ **Both done**, and the first is no longer a subset: 2020-12 by default
  with 2019-09, draft-07 and draft-06 read as themselves, scored 1,301 of 1,301
  on the official suite's `required` set with no schema refused. Three
  dependencies remain rather than three gaps - see the
  \ref format_json "JSON page".
- A custom allocator.
- ~~NFC normalization.~~ **Done.** `normalize_unicode` normalizes every string
  the lexer decodes, object names included, so duplicate-name detection sees
  normalized names. It requires `validate_utf8` and turns off in-situ for
  strings, both deliberately - see the \ref format_json "JSON page".
- JSON5 proper, as distinct from the JSONC subset that is supported.
- Conversion to YAML. The reverse direction exists.
- SIMD-accelerated scanning, which is what the throughput gap is really about.

### CSV

Compared against libcsv, Python's `csv` module and rapidcsv.

**Present, and competitive.** A table DOM with row and column insert, append,
remove, rename and set; header handling with an index and duplicate-key
iteration; irregular-row support with explicit normalize-to-max and validate
operations; clone, clear and compact; configurable dialect covering delimiter,
quote character, escape mode, and which newlines to accept; push streaming with
record-begin, field and record-end events; a writer; file read and write;
in-situ mode; error reporting carrying byte offset, line, column, and both row
and column indices, plus a snippet and caret.

The row and column index in the error struct is better than libcsv, which
reports very little, and the column-level table operations have no equivalent in
Python's `csv`.

**Round-trip fidelity is sound.** Parsing, writing and re-parsing twelve
documents chosen for difficulty - embedded commas, embedded quotes, embedded
newlines, empty and consecutive-empty fields, a trailing delimiter, preserved
surrounding spaces, CRLF, ragged rows, multi-byte UTF-8, a field whose content
is a single quote character, and a record of nothing but delimiters - produced
byte-identical fields in every case. The gap was therefore in the *test
suite*, not in the parser, and it is now closed:
`CsvRoundTrip.WriteThenReparsePreservesFields` pins all twelve.

**Missing.**

- A pull reader, as above.
- ~~Dialect presets.~~ **Added**: `gtext_csv_dialect_tsv()`, `_semicolon()`,
  `_backslash_escape()`, `_excel()` and `_permissive()`. Exporting them turned
  into a correctness pass - writing a test per preset was the first time
  several dialect options had been exercised, and three of them turned out to
  do nothing at all. See the \ref format_csv "CSV page".
- Dialect sniffing, equivalent to Python's `csv.Sniffer`.
- Quoting policies beyond a `quote_all_fields` boolean. Python offers minimal,
  all, non-numeric and none; only the first two are reachable here.
- Type inference, which is deliberate and correctly documented as such.
- Conversion to JSON.
- RFC 7111 fragments, deliberately out of scope.
- ~~`validate_utf8` in the streaming parser~~ and ~~a coherent
  `allow_unquoted_newlines`~~ - both done; see the \ref format_csv "CSV page".

### YAML

Compared against libyaml, libfyaml, PyYAML and yaml-cpp.

**Present, and strongly competitive.** This is the most complete of the three.
DOM with mappings, sequences, and the `omap`, `pairs` and `set` types that most
libraries omit; anchors and aliases with a separate resolver and an expansion
limit; merge keys; complex keys; custom and non-standard tags; comment
retention, both leading and inline, with setters as well as getters; scalar
style preservation and control; source location per node; typed accessors
including binary and timestamp; multi-document parse and emit; a JSON fast
path; a safe-mode option set; partial parsing; both push streaming and a pull
reader; a YAML 1.1 resolution mode; conversion to JSON with and without tag
information; file read and write, including read-all-documents.

Comment retention with round-trip emission and per-node source locations are
the standout features. libyaml discards comments entirely and yaml-cpp's
support is partial, so a configuration-rewriting tool that must preserve a
file's comments is a case where this library is the better choice outright.

**Missing.**

- Schema validation. The `GTEXT_YAML_Schema` option selects an implicit typing
  schema - failsafe, JSON, core - and is not a validator. There is no
  equivalent of the JSON Schema engine, nor of Kwalify or Rx.
- A custom allocator.
- In-situ zero-copy parsing, which JSON and CSV both offer.
- Conversion from JSON, the reverse of the supported direction.
- ~~A documented thread-safety position.~~ **Fixed**, and it was already fixed
  when this line still said otherwise - section 7 above closed it, and
  `documentation/modules/YAML.md` section 16 is the per-module statement.
- ~~`key: a : b` truncates rather than rejecting.~~ **Fixed**: it is rejected
  rather than rearranged. Nothing in that family is open now.

---

## What this adds up to

On correctness and on breadth of feature, the library is in good shape, and in
two places - YAML comment and style preservation, JSON patch and pointer
together with the duplicate-key modes - it is ahead of the common alternatives.
The format pages are unusually honest about deviations, which is itself worth
something to an adopter.

What stands between it and the stated goal is a short list, and most of it is
not parser work: a license, an optimized default build, an allocator hook, two
pull readers, one honest failure in the schema engine, and a paragraph each on
thread safety for JSON and YAML. Throughput on the JSON side is the one item
that is genuinely a project rather than a task.

---

Back to \ref format_references "Format and specification references".
