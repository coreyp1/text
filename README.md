# Ghoti.io Text Library

A C library for parsing and serializing structured text formats. It provides
three parsers - JSON, CSV and YAML - each with a DOM model, a streaming model
and a writer, sharing one result-code, allocation and limits contract.

## Example

```c
#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  const char *src = "{\"name\":\"ghoti\",\"version\":[0,0,0]}";

  GTEXT_JSON_Error err = {0};
  GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
  GTEXT_JSON_Value *doc = gtext_json_parse(src, strlen(src), &opts, &err);
  if (!doc) {
    fprintf(stderr, "%s at line %d, column %d\n", err.message, err.line, err.col);
    gtext_json_error_free(&err);
    return 1;
  }

  const char *name = NULL;
  size_t name_len = 0;
  if (gtext_json_get_string(gtext_json_object_get(doc, "name", strlen("name")), &name, &name_len)
      == GTEXT_JSON_OK) {
    printf("%.*s\n", (int)name_len, name);
  }

  gtext_json_free(doc);
  return 0;
}
```

## Dependencies

[ghoti.io-cutil](https://github.com/Ghoti-io/cutil), for the `GCU_Allocator`
vtable the suite shares, resolved through pkg-config. Nothing else beyond
libc. Google Test is required only to build the test suite, and clang only to
build the fuzzers.

## Building

If cutil is installed where pkg-config can find it:

```bash
make
make test
sudo make install
```

Otherwise build the suite into a local prefix from the parent folder, which
installs cutil first, and point this build at the same prefix:

```bash
./bootstrap.sh
make -C text test PREFIX="$PWD/.local"
```

`make help` lists every target. `make docs` builds the Doxygen manual into
`docs/`.

## The API

Each format lives behind one header - `ghoti.io/text/json.h`,
`ghoti.io/text/csv.h`, `ghoti.io/text/yaml.h` - and follows the same shape.

**Parsing** has two models. The DOM parsers (`gtext_json_parse()`,
`gtext_csv_parse_table()`, `gtext_yaml_parse()`) take a buffer and return an
owned tree, freed by the format's free function. The streaming parsers take
input in chunks of any size and deliver events through callbacks, for inputs
too large to hold or arriving from a socket. YAML additionally offers a
pull-model reader.

**Options** are plain structs obtained from a `*_options_default()` function
and modified before use, never global state. They carry the dialect or
strictness settings, the resource limits, and the error-reporting detail
level. `gtext_yaml_parse_options_safe()` returns a hardened variant for
untrusted input.

**Limits** are enforced by every parser - nesting depth, total input size,
and per-format limits on string length, element counts, row and column
counts. Each has a documented default rather than being unbounded, and each
is tested at its boundary against a document that should exceed it.

**Errors** come back as a status code plus a struct carrying byte offset,
line and column, and optionally a context snippet with a caret. Snippets are
owned by the caller and released with the format's `*_error_free()`.

**Writing** mirrors parsing: serialize a DOM, or drive a streaming writer
with events. Write options control formatting, escaping and canonical output.

**Files.** Each format reads and writes a path directly -
`gtext_json_parse_file()` / `gtext_json_write_file()` and the CSV and YAML
equivalents. Reads are incremental, so a pipe or `/dev/stdin` works and the
size limit applies before the file is in memory; writes are atomic, going to a
temporary file beside the destination and replacing it only once complete.

JSON additionally implements JSON Pointer, JSON Patch, JSON Merge Patch and a
core subset of JSON Schema. The schema engine refuses a schema whose keywords
it cannot enforce rather than ignoring them, since a schema that looks like it
constrains its data and does not is the worse failure. Two of those keywords -
`pattern` and `patternProperties` - are regular expressions, and this library
has no engine; they are enforced when the caller supplies one through
`GTEXT_JSON_Schema_Options`, which is three function pointers, and refused when
they do not. YAML implements anchors and aliases, merge keys, multi-document
streams, tag resolution and conversion to JSON.

## Documentation

- [Modules](@ref modules) — the API, per module
- [Format and specification references](@ref format_references) — which
  specification each parser implements, its deviations, and the evidence
- [Examples](@ref examples) — example programs
- [Function Index](@ref functions_index) — complete API reference

## Status

The test suite runs 1,305 tests across 73 binaries with zero failures, clean
under valgrind and under ASan/UBSan, at 74.3% line coverage. (Counting these
from `make test` output needs care: three binaries are run twice, once under
their module target and once in the sweep, so summing every `[ PASSED ]` line
overstates the total by 773.) Three libFuzzer
harnesses cover the three parsers; `tests/fuzz/README.md` records what they
have found. All of it runs in CI on every push and pull request, along with a
coverage floor and the symbol, allocator and header gates - until recently
none of it did, and `make test` exited 0 even with a failing suite.

`tests/test-rfc-conformance.cpp` holds the worked examples from RFC 6901,
RFC 6902, RFC 7386 and RFC 4180 §2, transcribed from the specifications rather
than from this implementation. Writing them down found four divergences that
the existing tests agreed with. No external corpus - JSONTestSuite,
csv-spectrum, yaml-test-suite - is wired up yet, so conformance beyond those
tables is unmeasured.

**JSON — stable.** RFC 8259 by default, with opt-in JSONC extensions.
Exact number round-tripping through lexeme preservation. No external
conformance corpus is wired up, so read
[the JSON page](@ref format_json) before relying on the phrase "spec
compliant".

**CSV — stable.** RFC 4180 by default, with configurable dialects and
support for ragged rows. The streaming parser gives the same answer whatever
chunk sizes it is fed, and the fuzzer checks it against the table parser on
every input, every dialect option included. `validate_utf8` is honored by both
parsers, incrementally in the streaming one so that a sequence split across
feeds is still checked.
See [the CSV page](@ref format_csv).

**YAML — alpha.** Block and flow collections, all five scalar styles,
anchors and aliases, merge keys, tags, multi-document streams, UTF-16/32
input, a DOM with mutation and cloning, a writer, and YAML-to-JSON
conversion. The API may change before 1.0. The YAML test suite has never been
run against this parser, so conformance to 1.2.2 is unmeasured rather than
partial.

Comparison against other implementations keeps finding defects here, so treat
this module as the least settled of the three. Fifteen are fixed so far, and what they have in
common is worth stating plainly: most did not fail on valid input, they
quietly changed what it meant. Plain scalars containing ` - `, ` , ` or ` # `
were truncated. Tags were dropped from block-style collections. A mapping key
with no value was dropped rather than made null, shifting every later pair. A
block sequence at its parent key's own column never closed, so the next key
became one of its entries. A key indented deeper than its mapping was nested
as a mapping standing where a key belongs. Block scalars lost the line break
clip chomping keeps, folded blank lines and more-indented lines wrongly, and
ignored the indentation indicator. Plain scalars did not continue onto the
lines below them, so a continuation became a key of its own. And a plain
scalar inside `[` `]` or `{` `}` ended at its first space, so `[a b, c]` came
out as three entries rather than two.

Six more have been fixed since: a flow plain scalar now folds across a line
break, `[a: 1]` parses as the single-pair mapping it is, input ending inside
a flow collection is an error rather than an empty document, two flow entries
with nothing between them are refused, a scalar with no key to hold it is
refused, and `key: a : b` is refused rather than rearranged into
`{key: "a", b: null}`.

Ten more came out of running yaml-test-suite. Quoted scalars now fold
their line breaks, which plain and block scalars already did - a wrapped
`"a\n  b"` was coming back with the wrapping still in it. A `%` directive no
longer stands as a document of its own, and on its own with no document to
apply to it is now refused. And a quoted scalar whose escape or line break
straddled a feed boundary lost bytes, because the scanner took the byte it
was still deciding about for the closing quote; that one was found by a test
that feeds the input one byte at a time, not by the suite.

The rest are structural. A second top-level node used to overwrite the
first, so `- a` and `- b` followed by `invalid: x` returned only
`{"invalid": "x"}` with the sequence gone. A `-` at a block mapping's own
column with no key waiting became a sequence standing where a key belongs.
A block scalar that is the document's root was required to be indented past
column 0, so `--- >` over three lines at column 0 collected nothing. In a
folded scalar a blank line before a more-indented line lost its break. And
a malformed block header - `|0`, `|10`, `|+-`, `| junk` - was read as
something rather than refused. And a `:` that begins a node is now an
ordinary plain character where it does not end a key, so `- ::vector` and
`{x: :x}` parse instead of being refused for having no key in front of the
colon.

A 153-document comparison backs this, checked against two implementations
rather than one: PyYAML, which implements YAML 1.1, and js-yaml, which
implements 1.2. 152 of the 153 agree with js-yaml, and the one that does not
is a case where this parser is the more faithful of the two. Five differ from
PyYAML, and all five are places where the two oracles disagree with each
other and this parser follows 1.2 - it keeps tabs inside a plain scalar,
which 1.1 refuses. Having two oracles is what made those five legible as a
version question rather than as defects; against PyYAML alone they looked
like bugs, and one of them had been recorded here as such.

None of the defects this page used to list as open is outstanding, and that
turned out to matter much less than it sounds. The corpus was chosen by
working outward from defects already found, so it measured the things that
had already been fixed.

**yaml-test-suite has now been run.** `make conformance` clones it and scores
this parser against it: **71.3%** of the 366 cases that can be checked by
value or by refusal. For calibration, the same harness scores **js-yaml at
82.0%** and **PyYAML at 77.3%** - so a mature implementation does not score
100% here either. The remaining 38 cases assert an event stream the harness
does not emit.

The first run scored 51.9%, a long way from the 99% the hand-built corpus
had suggested. Seventy cases have been fixed since, in three batches:
quoted-scalar line folding and directives; a group of structural refusals -
a second top-level node no longer silently replaces the first, a root block
scalar is no longer required to be indented past column 0, a blank line
beside a more-indented line in a folded scalar keeps its break, and a
malformed block header is refused; and the rule that a `:` is a mapping
indicator only where it ends a key. The largest group still failing is the
48 documents that should be refused and are not.

The denominator moved from 368 to 366 along the way, and that was a harness
bug rather than progress: three suite cases carry an explicit null where the
expected value goes, and the harness was checking whether the parser had
refused the input before it tried to decode that. Refusing one of those
scored as a defect while accepting it was skipped. It is measured before the
answer is judged now, and the reference scores moved with it.

## Macros and Utilities

Cross-compiler macros live in `include/ghoti.io/text/macros.h`:

- `GTEXT_MAYBE_UNUSED(X)` — mark unused function parameters
- `GTEXT_DEPRECATED` — mark deprecated functions
- `GTEXT_API` — mark functions for library export
- `GTEXT_ARRAY_SIZE(a)` — compile-time array size
- `GTEXT_BIT(x)` — bitmask with bit x set

See [the Core module page](@ref core_module) for the version API and the
platform notes.

## License

MIT. See [LICENSE](LICENSE).
