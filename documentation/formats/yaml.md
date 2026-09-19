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
| Plain scalars containing `-`, `,`, `?`, `#` | yes, since the §7.3.3 fix | `: ` and ` #` still end the scalar |
| Multi-line plain scalars | yes, in block context | folded to spaces; blank lines give breaks |
| Multi-line plain scalars in flow | no | `[a` then an indented `b]` does not fold |
| Flow plain scalars with spaces | yes | `[a - b, c]` is two entries |
| Block scalar chomping and folding | yes | clip, strip and keep; indentation indicator honoured |
| Single-pair mappings in flow (`[a: 1]`) | no | write `[{a: 1}]` |

@anchor yaml-deviations
## Deviations

**Fixed: a tag on a block-style collection is no longer dropped.**

`!!omap [{a: 1}]` kept its tag; `!!omap` followed by a block sequence did not.
A tag applies to the node that follows it, and the scanner can only attach a
pending tag to the next *scalar*, because in block context nothing yet says
whether the node starting there is that scalar or a collection whose first key
or item it is. So the tag stayed on the key or the first item and the
collection came out untagged - standard and application tags alike, at the
document root and nested, for block sequences and block mappings.

It mattered beyond the tag itself. `gtext_yaml_to_json()` refuses `!!set`,
`!!omap` and `!!pairs` so that a YAML-specific collection cannot silently
become a JSON array; with the tag gone that refusal did not happen, and
`!!omap\n- a: 1` converted to `[{"a":1}]` without complaint. Block style is
the common style in real YAML, so this was the usual case rather than a
corner.

Two shapes decide it, and they differ in nothing but a line break:

```yaml
!custom a: 1     # tags the key
```

```yaml
!custom
a: 1             # tags the mapping
```

Both produce the same events, so no rule over the event stream alone can tell
them apart. `GTEXT_YAML_Event` therefore carries `tag_line`, the line the tag
was written on, and the parser moves an own-line tag onto the collection it
turns out to begin. Same-line tags never move, so a tagged key or a tagged
scalar behaves exactly as before.

Every case is pinned in `YamlTagPlacement` in
`tests/yaml/test-yaml-standard-tags.cpp`, with the expectations taken from
PyYAML's composer on the same input.

**Fixed: a mapping key with no value no longer shifts the rest of the mapping.**

A mapping's children are collected as a flat alternating key, value, key,
value list, and the pairs are formed by halving it. A key whose value was
absent left that list one short, so the halving dropped the last key and
paired every later key with the wrong value:

| input | gave | should give |
| --- | --- | --- |
| `a:` | `{}` | `{a: null}` |
| `a:`<br>`b: 1` | `{a: b}`, discarding the `1` | `{a: null, b: 1}` |
| `{a}` | `{}` | `{a: null}` |
| `? a` | `{}` | `{a: null}` |
| `? a`<br>`? b` | *Explicit key already pending* | `{a: null, b: null}` |

The second row is the one that mattered: a missing value did not fail, it
silently shifted the rest of the mapping by one. A key with an empty value is
ordinary in configuration files. The last row is why the block spelling of a
`!!set` could not be parsed at all while the flow spelling could.

YAML says an absent value is null, so one is now supplied - when a scalar
appears at the key's own column while a value is still expected, when a second
`?` shows the previous key never got a `:`, and for a trailing key at the end
of a mapping.

Indentation decides, not absence alone. Content indented past the key is the
key's value, and a block sequence may sit at its key's own column and still be
that value, so the rule applies only to a scalar at the key's column and never
to `-`:

```yaml
x:          # {x: {y: 1}}      a:          # {a: [1]}
  y: 1                         - 1
```

The supplied value is `~` rather than an empty scalar. The resolver reads a
scalar's text without knowing whether it was quoted, so an empty one resolves
to the empty *string*, which would make `a:` indistinguishable from `a: ''` -
and those are different values. `a: ''` still gives a string.

Checked against PyYAML over 38 documents; 36 agreed, and the two that did not
are fixed below.

Those 38 have since grown into a 121-document comparison covering block
scalars, block structure and plain scalars, of which 114 now agree. The seven
that do not are each listed as still open on this page: one is a flow plain
scalar that does not fold across a line break, one a single-pair mapping in
flow, and the other five are malformed documents accepted rather than
refused.

**Fixed: block scalars keep the line breaks 8.1 gives them.**

The reported fault was that clip chomping, the default, dropped the final
line break: `a: |` over an indented `block` gave `"block"` rather than
`"block\n"`. Comparing the whole of §8.1 against PyYAML rather than that one
case found five more, and only 11 of 29 inputs agreed:

| input | gave | should give |
| --- | --- | --- |
| `a: \|` over `block` | `"block"` | `"block\n"` |
| the same with two blank lines after | `"block\n\n"` | `"block\n"` |
| `a: >` over `one`, blank, `two` | `"one\n\ntwo"` | `"one\ntwo\n"` |
| `a: >` over `one`, then a deeper `deep` | `"one   deep"` | `"one\n  deep\n"` |
| `a: \|2` over `   text` | `"text"` | `" text\n"` |
| `a: \|` over a blank line then `text` | `"text"` | `"\ntext\n"` |

Clip removed one break where it should strip every trailing one and restore
a single break. Folding counted a blank line twice, once for the line before
it and once for the blank itself. Lines indented past the block keep their
breaks - that is what lets a listing sit inside a `>` scalar without
collapsing onto one line - and they were being folded like any other. The
indentation indicator was parsed and then ignored. And the header consumed a
newline after the one that ends it, which swallowed the block's first line
whenever that line was blank.

Indentation now comes from the first non-empty line, as §8.1.1.1 says, rather
than from the smallest indentation anywhere in the block. That also decides
where the block ends, so `a: |` no longer reaches past its own content to
swallow a sibling key. A tab beyond the block's indentation is content and is
kept; only a tab standing in for the indentation itself is refused.

The indentation indicator counts from the parent node, which the scanner has
no node stack to consult. It reads the parent's indentation as the column of
the first non-space character on the header's own line - the key, the `-` or
the `?` that owns the scalar, or the indicator itself at the root. That
agrees with PyYAML on every nesting tested, including sequence entries and
explicit keys.

Three existing tests asserted the missing break, one of them in the suite
named for chomping; they agreed with the implementation rather than with
YAML, which is why none of this was caught. The table in
`tests/yaml/test-yaml-block-scalars.cpp` is generated from PyYAML's output
for each input rather than transcribed from the spec, and every row in it
fails if any one of these fixes is undone.

**Fixed: a block sequence closes when the line after it has left it.**

YAML lets a block sequence sit at the same column as the key that owns it,
and that spelling is the common one:

```yaml
a:
- 1
b: 2
```

This gave `{a: [1, {b: 2}]}` rather than `{a: [1], b: 2}` - the key swallowed
and its value buried a level down. The dedent rule closed a block collection
only when the next line was indented strictly less, so a sequence written
this way never closed at all. Indenting the sequence, the form that is easier
to read and rarer in practice, worked the whole time, which is why this
survived.

A block sequence at exactly the next line's indentation has ended too, unless
that line begins another entry. A block mapping at the same indentation is
never closed: that is the mapping the new key belongs to.

**Fixed: a key indented deeper than its mapping is refused.**

```yaml
a: 1
  b: 2
```

gave `{a: 1, {b: 2}: null}` - a mapping standing where a key should be - for
input PyYAML refuses outright. A key indented past its mapping starts a
nested mapping only when a key above is still waiting for a value to put it
under; with none, there was nothing for it to belong to and it was attached
anyway.

**Fixed: plain scalars are no longer truncated at an embedded indicator.**

A block-context plain scalar used to end at a space followed by `-`, `,` or
`:`, as though the scanner were in flow context, discarding the rest of the
value with no error and no warning. `key: a - b c` yielded `a`. The scanner
now follows §7.3.3: `c-indicator` (§5.3) restricts those characters to the
position where a node may *begin*, and `ns-plain-char` makes them ordinary
content thereafter. Flow context got the same correction, which is why
`key: [a-b, c]` now parses at all.

Each row below was cross-checked against PyYAML and is pinned by a case in
`tests/yaml/test-yaml-plain-scalars.cpp`:

| Input | PyYAML | Before | Now |
|---|---|---|---|
| `key: a - b c` | `a - b c` | `a` | `a - b c` |
| `key: a -b c` | `a -b c` | `a` | `a -b c` |
| `key: a b - c` | `a b - c` | `a b` | `a b - c` |
| `key: a, b` | `a, b` | `a` | `a, b` |
| `key: 3 - 4` | `3 - 4` | `3` | `3 - 4` |
| `key: a ? b` | `a ? b` | `a` | `a ? b` |
| `key: end-` | `end-` | `end` | `end-` |
| `key: a#b` | `a#b` | `a` | `a#b` |
| `key: a :b c` | `a :b c` | `a` | `a :b c` |
| `key: [a-b, c]` | `['a-b', 'c']` | fails to parse | `['a-b', 'c']` |
| `key: a # b` | `a` | `a` | `a` — unchanged, correct |
| `key: a-b c` | `a-b c` | `a-b c` | `a-b c` — unchanged, correct |

The last two are control cases: `#` after a space still opens a comment, and
a `-` with no space before it was always kept, which is what located the
fault in the space-then-indicator transition rather than in indicator
handling generally.

Of a 48-case comparison against PyYAML, agreement went from 11 to 38. The
ten that still differ are listed under
[Tested scope](#yaml-tested-scope); four of them are cases where this parser
is right and PyYAML is applying YAML 1.1 rules.

**Still open: `key: a : b` is truncated rather than rejected.** A `:`
followed by a space does end a plain scalar, and PyYAML raises a
`ScannerError` for the trailing `b`. This parser yields `a` and discards the
rest. It is the one remaining case in that family, and it is a malformed
document either way.

**Fixed: a block plain scalar continues onto the lines below it.**

§7.3.3's `ns-plain-multi-line` was not implemented, so `key: a` over an
indented `b` kept only `a` and made the continuation a key of its own. It did
not fail - it rearranged the document:

| input | gave | should give |
| --- | --- | --- |
| `key: a`<br>`  b` | `{key: a, b: null}` | `{key: "a b"}` |
| `key: a`<br>`  b`<br>`other: 1` | `{key: a, b: other, 1: null}` | `{key: "a b", other: 1}` |
| `a:`<br>`  b: x`<br>`    y`<br>`  c: 1` | `{a: {b: x, y: c, 1: null}}` | `{a: {b: "x y", c: 1}}` |

The second row is the shape of the damage: an integer ends up standing as a
key. A break folds to a space and a run of blank lines gives one break each,
as flow folding does elsewhere.

A continuation line has to be indented past the node the scalar belongs to,
which the scanner has no node stack to look up. It tracks the indentation
opened by the most recent `:`, `-` or `?`: for `:` the column its key began
at, since `- x: 1` puts the key at column 2 while the line starts at 0, and
for the others the indicator's own column. A document marker or a comment
line never continues a scalar, and a new document resets the tracking.

A key cannot appear on a continuation line, and the parser refuses one
through its existing rule that a key must share a line with its `:`.

**Fixed: a flow plain scalar may contain spaces.**

`ns-plain-char` does not exclude white space, and flow context adds only
`c-flow-indicator` to what ends a scalar. Ending one at its first space did
not merely refuse `[a - b]`, as this page previously recorded - it silently
split valid documents:

| input | gave | should give |
| --- | --- | --- |
| `key: [a b, c]` | three entries | `["a b", "c"]` |
| `key: {k: v w, j: x}` | `{k: v, w: j, x: null}` | `{k: "v w", j: "x"}` |
| `key: [1 2, 3]` | `[1, 2, 3]` | `["1 2", 3]` |
| `key: [a - b, c]` | failed to parse | `["a - b", "c"]` |

Trailing white space is not part of the scalar, and a `:` ends one only where
it separates a key from a value - followed by white space, a flow indicator
or the end - so `[a:b, c]` holds `a:b`.

Two things follow from this that are worth stating, because they read as
regressions and are not. `{a:1}` is a mapping whose single key is the string
`a:1`, not `{a: 1}`: a plain key in flow context needs the space after its
colon, and JSON's spelling does not carry over. And an entry that reaches a
comma with no `:` is a key whose value is null, so `{a, b}` is two entries
rather than one pair. Both match PyYAML. Three tests were written in JSON's
spelling and counted the nodes they expected from it; their inputs now say
what they meant.

**Still open: a flow plain scalar does not fold across a line break.**
`key: [a` over an indented `b]` is `["a b"]` to PyYAML and two entries here.
Block context gained this; flow context has not.

**Still open: a single-pair mapping in a flow sequence.** `[a: 1]` is
`[{a: 1}]` to PyYAML and does not parse here; write `[{a: 1}]`.

**`!!timestamp`, `!!set`, `!!omap` and `!!pairs` are honored at all**, which
1.2 does not require, since they are 1.1 repository types. Parsers that
implement 1.2 strictly will reject or ignore them.

@anchor yaml-tested-scope
## Tested scope

**Tests.** 64 test files under `tests/yaml/`, carrying 484 of the suite's
1296 test cases across 72 binaries, all passing. They cover the scalar styles,
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
untrusted input use `gtext_yaml_parse_options_safe()`, which bounds resource
consumption.

The silent-truncation defect that previously made every plain scalar suspect
is fixed. What remains open is narrower and listed under
[Deviations](#yaml-deviations): multi-line plain scalars, flow plain scalars
containing spaces, and one malformed input reported as truncation rather
than as an error.

---

Back to \ref format_references "Format and specification references".
