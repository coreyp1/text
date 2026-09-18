@page format_yaml YAML

# YAML

YAML is by far the largest of the three specifications this library
implements, and the YAML parser is the least settled part of it. This page
says what is implemented, what is known to be wrong, and - the section that
matters most - how little of the claim to 1.2 conformance has actually been
measured. Back to the \ref format_references "format index".

Read \ref yaml_module "the YAML module page" for the API. This page is about
the format.

## Normative references

- **Specification:** [YAML 1.2.2](https://yaml.org/spec/1.2.2/), October
  2021. This is the current revision; it is editorial relative to 1.2.1 and
  clause numbers below refer to it.
- **YAML 1.1:** [yaml.org/spec/1.1](https://yaml.org/spec/1.1/), for the
  resolution rules the compatibility mode restores.
- **Core schema:** 1.2.2 §10.3. **JSON schema:** §10.2. **Failsafe schema:**
  §10.1.
- **Tags:** the `tag:yaml.org,2002:` namespace, §10.
- `!!timestamp`, `!!set`, `!!omap` and `!!pairs` are **not** part of the YAML
  1.2 core schema. They come from the
  [YAML 1.1 type repository](https://yaml.org/type/), which 1.2 does not
  carry forward. Their treatment here is described below and is this
  library's decision, not a specified behavior.

Parser allocations come from an arena owned by the `GTEXT_YAML_Document` and
released by `gtext_yaml_free()`. Error context snippets are separately owned
and released by `gtext_yaml_error_free()`.

## Parts implemented

**Structure.** Block and flow collections (§8, §7.4), nested to the depth
limit. Multi-document streams with `---` and `...` markers (§9). Explicit
keys (`?`), and complex keys behind `allow_complex_keys`.

**Scalar styles (§7, §8.1).** All five: plain, single-quoted, double-quoted,
literal (`|`) and folded (`>`), with the clip, strip (`-`) and keep (`+`)
chomping indicators. Folding and chomping were checked directly: `key: >`
over two indented lines yields `a b`, and `key: |` yields `a\nb`.

**Escapes (§5.7).** The full set, including `\xNN`, `\uXXXX` and
`\UXXXXXXXX`.

**Encoding (§5.2).** UTF-8, UTF-16 and UTF-32 with BOM detection and
transcoding to UTF-8. UTF-8 validation is on by default and is genuinely
wired, unlike CSV's.

**Directives (§6.8).** `%YAML` and `%TAG`, with tag handle resolution.
A `%YAML 1.1` directive switches resolution, as does the `yaml_1_1` parse
option.

**Schemas (§10).** `GTEXT_YAML_SCHEMA_CORE` by default, with `JSON` and
`FAILSAFE` available. Core resolution was checked directly: `true` resolves
to a bool node, `123` to int, `3.14` to float, and both `null` and `~` to
null.

Under 1.2 defaults, `yes` resolves to the **string** `"yes"` and `0755` to
the **string** `"0755"` - correct for 1.2, which removed 1.1's `y|yes|on`
booleans and leading-zero octals. Enabling 1.1 compatibility restores them,
along with sexagesimals (`190:20:30`), and each emits a warning
(`GTEXT_YAML_WARNING_YAML11_BOOL`, `_OCTAL`, `_SEXAGESIMAL`) so that a
document relying on the old rules is visible rather than silent.

**Anchors and aliases (§6.9, §7.1).** `&anchor` and `*alias`, with cycle
detection and a total-expansion limit that bounds the billion-laughs attack.

**Merge keys.** The `<<` key from the 1.1 type repository, on by default via
`allow_merge_keys`. Verified: `a: &A {x: 1}` merged into a mapping that also
sets `y` yields both keys. `gtext_yaml_document_has_merge_keys()` reports
whether a document used them.

**Tags.** `!!str`, `!!int`, `!!float`, `!!bool`, `!!null`, `!!seq`, `!!map`
from the core schema. From the 1.1 type repository, with the limits noted:

- `!!binary` is base64-decoded. The node remains a string node - its string
  accessor returns the base64 source text - and `gtext_yaml_node_as_binary()`
  returns the decoded bytes. Writing re-emits the base64 form.
- `!!timestamp` accepts a strict ISO 8601 subset and **validates without
  parsing**: the value stays a string, with no normalization, no timezone
  conversion and no epoch.
- `!!set` validates that mapping values are null. `!!omap` and `!!pairs`
  validate that entries are single-pair mappings, and `!!omap` enforces
  unique keys. Distinct `GTEXT_YAML_Node_Type` values exist for all three.

Custom application tags are supported behind `enable_custom_tags`, with
constructor, representer and JSON-converter callbacks.

**Parsing models.** A one-shot DOM parser, an event-driven streaming parser,
and a pull-model reader. The DOM supports accessors, mutation, sequence
insert/append/remove, and deep cloning.

**YAML to JSON.** `gtext_yaml_to_json()` converts a document, with options
governing how the YAML-only constructs that JSON cannot express are handled.

## Parse options and limits

| Option | Default | Safe mode |
|---|---|---|
| `schema` | `CORE` | `CORE` |
| `dupkeys` | `ERROR` | `ERROR` |
| `max_depth` | 256 | 64 |
| `max_total_bytes` | 64 MiB | 16 MiB |
| `max_alias_expansion` | 10,000 | 1,000 |
| `validate_utf8` | `true` | `true` |
| `resolve_tags` | `true` | `true` |
| `retain_comments` | `false` | `false` |
| `yaml_1_1` | `false` | `false` |
| `allow_aliases` | `true` | **`false`** |
| `allow_merge_keys` | `true` | **`false`** |
| `allow_complex_keys` | `true` | **`false`** |
| `allow_nonstandard_tags` | `true` | **`false`** |
| `require_string_keys` | `false` | **`true`** |
| `enable_custom_tags` | `false` | `false` |
| `enable_json_fast_path` | `true` | `true` |

`gtext_yaml_parse_options_safe()` returns the right-hand column. It is the
correct starting point for untrusted input: it removes aliases entirely,
which is the only complete defense against expansion attacks, and it refuses
non-string keys, which is what most consumers assume anyway.

## Save

The writer emits UTF-8 with no BOM, two-space indent, plain scalars where
they are safe, and `FLOW_STYLE_AUTO` - block for anything nested, flow for
short leaf collections. `line_width` of 0 means no folding.

Round-trip fidelity is **not** a goal of the current writer. Comments are
dropped unless `retain_comments` was set at parse time, and scalar style is
not preserved across a parse-write cycle; a document written back out is
semantically equal to the input, not textually equal.

## Compliance checklist

| Area | Supported | Rejected / limitation |
|---|---|---|
| Block collections | yes | |
| Flow collections | yes | |
| All five scalar styles | yes | |
| Chomping indicators | clip, strip, keep | |
| Multi-document streams | yes | |
| `%YAML` / `%TAG` | yes | |
| Core / JSON / Failsafe schema | yes | |
| YAML 1.1 resolution | opt-in, warns | |
| Anchors and aliases | yes, cycle-detected | `GTEXT_YAML_E_LIMIT` past `max_alias_expansion` |
| Merge keys | yes, default on | |
| `!!binary` | decoded | via a separate accessor |
| `!!timestamp` | validated | not parsed into a time type |
| `!!set` / `!!omap` / `!!pairs` | validated, own node types | |
| UTF-16 / UTF-32 input | yes, transcoded | |
| Duplicate keys | `ERROR` default | `GTEXT_YAML_E_DUPKEY` |
| Depth | 256 default | `GTEXT_YAML_E_DEPTH` |
| Comment round-trip | opt-in retention | not preserved on write by default |
| Scalar style round-trip | no | |
| **Plain scalars containing `-`, `,` or `:`** | **no - truncated** | see below |

@anchor yaml-deviations
## Deviations

**Plain scalars are truncated at an embedded indicator. Silently.**

This is a data-loss bug and the most serious thing on any of these pages.

A block-context plain scalar ends at a space followed by `-`, `,` or `:`, as
though the scanner were in flow context. The remainder of the value is
discarded, no error is raised and no warning is emitted. Checked against
PyYAML as an oracle:

| Input | PyYAML | This parser |
|---|---|---|
| `key: a - b c` | `a - b c` | `a` |
| `key: a -b c` | `a -b c` | `a` |
| `key: a b - c` | `a b - c` | `a b` |
| `key: a, b` | `a, b` | `a` |
| `key: 3 - 4` | `3 - 4` | `3` |
| `key: a : b` | `ScannerError` | `a` |
| `key: a # b` | `a` | `a` — correct |
| `key: a-b c` | `a-b c` | `a-b c` — correct |

Per §7.3.3, `,`, `[`, `]`, `{` and `}` are indicators only in flow context;
in block context they are ordinary plain-scalar characters. A `-` is an
indicator only at the start of a token followed by a space. A `:` followed by
a space genuinely does end a plain scalar - but it should then be a parse
error in this position, not a silent truncation.

The last two rows are the control cases: `#` after a space correctly starts a
comment, and a `-` with no preceding space is correctly kept, which locates
the fault in the space-then-indicator transition rather than in indicator
handling generally.

Until this is fixed, **quote any plain scalar that may contain a dash, comma
or colon**. Ranges (`1 - 10`), lists written inline (`a, b`), times
(`12:00`) and prose containing a dash are all affected.

`documentation/YAML-LIMITATIONS.md`, now folded into this page, described this
as "plain scalars are space-delimited tokens (breaks multi-word values)".
That description is out of date in a way that understates and mislocates the
problem: ordinary multi-word plain scalars work correctly, and
`key: hello world here` yields the whole of `hello world here`. What remains
broken is narrower, and harder to notice.

**`!!timestamp`, `!!set`, `!!omap` and `!!pairs` are honored at all**, which
1.2 does not require, since they are 1.1 repository types. Parsers that
implement 1.2 strictly will reject or ignore them.

## Tested scope

**Tests.** 61 test files under `tests/yaml/`, part of a suite that runs 1750
tests across 68 binaries with zero failures. They cover the scalar styles,
collections, anchors and aliases including the cycle and exponential-expansion
cases, merge keys, the tag types, directives, multi-document streams, UTF-8
and the other encodings, the DOM accessors and mutation, cloning, the writer,
the pull reader, chunked scanning, partial input, the limits, safe mode,
1.1 mode, config mode, and YAML-to-JSON conversion. `tests/yaml/test-yaml-real-world.cpp`
parses Docker Compose, Kubernetes and GitHub Actions shapes.

**Fixtures** are thin: `tests/data/yaml/` holds 14 formatting files plus one
binary regression case. Most tests carry their YAML inline as string
literals, which keeps them readable but means there is no corpus to run
another parser against.

**Fuzzing.** `tests/fuzz/fuzz_yaml.cpp` under libFuzzer with ASan and UBSan,
from 13 tracked seeds. It has been the most productive single tool applied to
this parser: two separate infinite loops in the block-scalar scanner, a
use-after-free, undefined behavior on empty quoted scalars and several leaked
token buffers. `tests/fuzz/README.md` records each.

**Memory.** The suite runs clean under valgrind and under ASan/UBSan.

**Reach of the oracles, and where it ends.** This is the section to read
before trusting the word "conformant" anywhere near this parser.

- **The [YAML test suite](https://github.com/yaml/yaml-test-suite) is not
  wired up.** It is the only broad measure of YAML conformance that exists,
  and without it the compliance percentage is not low or high - it is
  *unmeasured*. Every positive claim on this page reaches exactly as far as
  the cases listed above.
- **No differential testing against libyaml or PyYAML** is automated. PyYAML
  was used by hand to establish the truncation table above, which is how that
  bug was characterized - and it was found on the first handful of inputs
  tried, which is the strongest available argument that running a real corpus
  would find more.
- **Fuzzing proves absence of crashes, not correctness.** It found the
  hangs and the memory errors; it cannot find a parser that confidently
  returns the wrong string, which is precisely the defect above.
- **No benchmarks.** Parsing speed and memory use are unmeasured. Treat the
  parser as suitable for configuration-sized documents.

## Not implemented

- **Comment preservation on write.** Comments can be retained in the DOM but
  are not re-emitted.
- **Scalar style preservation.** A parse-write cycle normalizes style.
- **Timestamp parsing into a time type**, as above.
- **YAML test suite integration**, as above - the largest single gap.
- **Benchmarks**, as above.
- **Native Windows (MSVC)** is untested; MSYS2/MinGW is exercised.

## Status

Alpha. The API may change before 1.0. The parser is appropriate for
configuration files from sources you control, and for prototyping. For
untrusted input use `gtext_yaml_parse_options_safe()` - and note that safe
mode bounds resource consumption and does nothing about the truncation bug,
which affects trusted and untrusted documents equally.

---

Back to \ref format_references "Format and specification references".
