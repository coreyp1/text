# Fuzzing

Three libFuzzer harnesses, one per format:

| Target | Harness | Build | Run |
| --- | --- | --- | --- |
| JSON | `fuzz_json.cpp` | `make fuzz-json` | `make fuzz-run-json` |
| YAML | `fuzz_yaml.cpp` | `make fuzz-yaml` | `make fuzz-run-yaml` |
| CSV  | `fuzz_csv.cpp`  | `make fuzz-csv`  | `make fuzz-run-csv`  |

`make fuzz` builds and runs all three. Each runs for `FUZZ_TIME` seconds
(default 60); override it for a real campaign:

    make fuzz FUZZ_TIME=3600

Requires `clang`. The library sources are recompiled with clang's coverage
instrumentation and linked into the harness rather than linking the ordinary
shared library — libFuzzer steers its mutations by the coverage it observes,
and against an uninstrumented library it sees only the harness and degrades
into random input generation. AddressSanitizer and UndefinedBehaviorSanitizer
are enabled, since a parser reading one byte past a buffer is exactly the bug
being looked for and will not usually crash on its own.

## The options byte

Each harness consumes the first input byte (two, for CSV) as a selector for
the parse options, and treats the rest as the document. The dialects these
parsers accept are configurable enough that a fixed set of options would
leave most of the state machine unreachable: comments, trailing commas,
single quotes, alias resolution, and CSV's delimiter and quote characters are
all chosen from that byte.

A consequence worth remembering: a corpus file is not a valid document on its
own, it is one byte of options followed by a document. The seeds in
`corpus/*/` are built that way.

## Findings

The first run found four bugs, all fixed:

- **Use-after-free in the JSON parser.** A nested object lives in its
  parent's arena, and `gtext_json_free()` frees the whole arena, so the
  object parser's error path destroyed the parent; the parent's own error
  path then read freed memory. `{"":{` — five bytes — was enough.
- **Infinite loop in the YAML scanner.** A block scalar header running to
  end of input had no exit from the loop skipping the rest of the header
  line. `>[` — two bytes — hung the parser indefinitely.
- **Undefined behaviour on empty quoted scalars.** `a: ""` passed a NULL
  pointer to `memcpy`, and asked `malloc(0)` for the buffer, whose NULL
  return would have been reported as an allocation failure.
- **Leaked token buffers.** Several error paths in the stream layer abandoned
  a token that owned a heap buffer.

## Known outstanding

The YAML fuzzer still finds a small leak after roughly ten thousand
executions, in the same family as the last item above: a token carrying a
heap buffer abandoned on an error path in `stream.c`.

The individual sites are easy to patch and several have been, but the cause
is a disagreement about ownership rather than a missing `free`.
`yaml_internal.h` documents the scalar payload as "owned by scanner until
next token", while `scanner.c` allocates a fresh buffer per token and
`stream.c` frees it. Settling that — scanner-owned, freed on the next
`gtext_yaml_scanner_next()` — would remove the whole class at once, and is
worth doing before chasing the remaining sites one at a time.
