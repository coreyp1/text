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
core subset of JSON Schema. YAML implements anchors and aliases, merge keys,
multi-document streams, tag resolution and conversion to JSON.

## Documentation

- [Modules](@ref modules) — the API, per module
- [Format and specification references](@ref format_references) — which
  specification each parser implements, its deviations, and the evidence
- [Examples](@ref examples) — example programs
- [Function Index](@ref functions_index) — complete API reference

## Status

The test suite runs 1273 tests across 69 binaries with zero failures, clean
under valgrind and under ASan/UBSan, at 74.0% line coverage. Three libFuzzer
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

Comparison against PyYAML keeps finding defects here, so treat this module as
the least settled of the three. It has so far found and fixed the silent
truncation of plain scalars containing ` - `, ` , ` or ` # `; tags being
dropped from block-style collections; and a mapping key with no value being
dropped rather than made null, which shifted every later pair. Still open, and
listed with reproductions on [the YAML page](@ref format_yaml): block scalars
lose their trailing newline, a block sequence does not close on a dedent back
to its parent key, and multi-line and flow plain scalars containing spaces are
unsupported.

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
