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
booleans and leading-zero octals. So do `1_000`, `0b101` and `0O14`: digit
separators, binary literals and the upper-case base prefixes are all 1.1's,
and 10.3.2 admits only `[-+]? [0-9]+`, `0o [0-7]+` and `0x [0-9a-fA-F]+`.
Enabling 1.1 compatibility restores all of them, along with sexagesimals
(`190:20:30`), and the older ones emit a warning
(`GTEXT_YAML_WARNING_YAML11_BOOL`, `_OCTAL`, `_SEXAGESIMAL`) so that a
document relying on the old rules is visible rather than silent.

Each row of the table is a list of spellings rather than a word matched
without regard to case, so `tRue`, `nULL` and `.Nan` are strings. `%YAML 1.7`
is parsed as 1.2 with a `GTEXT_YAML_WARNING_YAML_VERSION`; `%YAML 2.0` is
refused, since 6.8.1 has a processor decline a major version it does not
implement.

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
- `!!timestamp` is read by [`chron`](https://github.com/Ghoti-io/chron),
  which implements the YAML 1.1 type repository's own expression. The node
  stays a string node - its string accessor returns the **normalized**
  spelling, `YYYY-MM-DDTHH:MM:SS` with the shortest fraction that loses
  nothing - and the value is reached through
  `gtext_yaml_node_timestamp_value()`, which hands back a `GCHRON_YamlValue`.
  `gtext_yaml_node_as_timestamp()` remains as a flattened view of the same
  thing.

  Three things are worth knowing. **A timestamp with no zone is not UTC**: the
  document did not say which zone it meant, and `GCHRON_YAML_DATE_TIME` keeps
  the civil reading rather than deciding for it - `chron`'s `zoned.h` is where
  a caller who knows the zone resolves it, and where the conversion can report
  that the reading names no instant, or two. No timezone conversion happens
  here. **A `:60` second is kept exactly as written**, because the value holds
  it as `:59` of the same minute and re-emitting that would move the reading a
  second earlier; `gtext_yaml_node_timestamp_is_leap_second()` says so.
  And this library does **not** resolve timestamps implicitly - an untagged
  `2001-12-14` is a string - so `!!timestamp` is the only way in.
- `!!set` validates that mapping values are null. `!!omap` and `!!pairs`
  validate that entries are single-pair mappings, and `!!omap` enforces
  unique keys. Distinct `GTEXT_YAML_Node_Type` values exist for all three.
  `gtext_yaml_to_json()` renders them as the structures they already are - a
  set as an object with null values, an omap or pairs as an array of
  single-pair objects, which keeps an omap's order and a pairs' duplicate
  keys. Only the tag is lost, as it is for every tagged node.

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

### Tags

Two rules govern tags, and only one of them is an option.

A tag in the `tag:yaml.org,2002:` namespace has to name a type the spec
defines. `!!bogus` is a malformed document and is refused whatever the
options say, because that namespace is not the author's to extend. The
types this library resolves there are `str`, `bool`, `int`, `float`,
`null`, `seq`, `map`, `set`, `omap`, `pairs`, `binary`, `timestamp` and
`merge`; `!!value` and `!!yaml` are named by the 1.1 type repository but
are not among them, and are refused for the same reason.

Everything else - a local tag like `!point`, or a global one under your own
prefix - is what tags exist for, and is accepted by default. The spec's own
examples use them freely. `allow_nonstandard_tags = false` refuses them, and
that is the setting's whole job: it is a lockdown for input you do not
trust, not a correctness rule, and turning it on for ordinary documents will
refuse valid YAML.

A `%TAG` directive is expanded before either rule is applied, so a handle
redirected away from the YAML namespace escapes the first rule and a handle
pointed into it does not.

The event API (`gtext_yaml_stream_*`) reports tags as written and resolves
nothing, so neither rule applies there; a consumer of events decides what a
tag means for itself.

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
| `!!timestamp` | parsed, by `chron` | `GCHRON_YamlValue`; normalized on output; explicit tag only |
| `!!set` / `!!omap` / `!!pairs` | validated, own node types | convert to JSON structurally |
| UTF-16 / UTF-32 input | yes, transcoded | |
| Duplicate keys | `ERROR` default | `GTEXT_YAML_E_DUPKEY` |
| Depth | 256 default | `GTEXT_YAML_E_DEPTH` |
| Comment round-trip | opt-in retention | not preserved on write by default |
| Scalar style round-trip | no | |
| Plain scalars containing `-`, `,`, `?`, `#` | yes, since the §7.3.3 fix | `: ` and ` #` still end the scalar |
| Multi-line plain scalars | yes, in block context | folded to spaces; blank lines give breaks |
| Multi-line plain scalars in flow | yes | `[a` over an indented `b]` is one scalar |
| Flow plain scalars with spaces | yes | `[a - b, c]` is two entries |
| Block scalar chomping and folding | yes | clip, strip and keep; indentation indicator honoured |
| Single-pair mappings in flow (`[a: 1]`) | yes | equivalent to `[{a: 1}]` |
| Tabs inside a plain scalar | yes | 1.2 allows them; PyYAML, a 1.1 parser, does not |

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

It mattered beyond the tag itself. The node's *type* follows the tag, and so
does the validation attached to it: `!!set` checks that every value is null,
and with the tag sitting on the first key instead, `gtext_yaml_node_type()`
reported a plain mapping and that check never ran. Block style is the common
style in real YAML, so this was the usual case rather than a corner.

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

Those 38 have since grown into a 153-document comparison covering block
scalars, block structure, plain scalars and flow collections. It is checked
against two implementations rather than one: PyYAML, which implements YAML
1.1, and js-yaml, which implements 1.2. 152 of the 153 agree with js-yaml,
and the one that does not is a case where this parser is the more faithful of
the two - js-yaml renders `[[1]: 2]` as `{"1": 2}` only because a JavaScript
object cannot have an array key, while the DOM here keeps the sequence.

Five differ from PyYAML, and all five are places where the two oracles
disagree with each other and this parser follows 1.2: PyYAML rejects tabs
that 1.2 allows inside a plain scalar, and folds a flow scalar across a line
break with no indentation at all, which js-yaml refuses. Where a
YAML-version question arises, that is the rule this page records.

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

Of a 48-case comparison against PyYAML, agreement went from 11 to 38 when
that fix landed. Those cases are now part of the wider comparison described
above, and all of them agree.

**Fixed: `key: a : b` is rejected rather than rearranged.** A `:` followed by
a space ends a plain scalar, and both oracles refuse the trailing `b`. This
parser used to yield `{key: "a", b: null}` - the tail of a value quietly
turned into a pair of its own. The scalar before a `:` is its key, held
provisionally as the previous key's value until the `:` claims it; with none
outstanding the `:` has no key at all. A `:` with no space after it is
ordinary content, so `key: a :b` is still the one scalar `a :b`.

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

**Fixed: a flow plain scalar folds across a line break.**

Block context gained this and flow context had not, so `key: [a` over an
indented `b]` stayed two entries where it is one scalar. The two now share
one rule, with one difference: in flow context a line that begins with the
collection's own punctuation ends the scalar rather than continuing it, which
is what keeps `[a - b` over ` , c]` two entries.

**Fixed: `[a: 1]`, a single-pair mapping written straight into a flow
sequence.**

It did not parse at all. The `:` fell through to the block-mapping path and
pushed a block level inside the sequence, which then swallowed the `]` that
should have closed it - so the failure was reported as an unterminated
collection, some distance from its cause. The pair has no `}` of its own, so
the `,` or `]` that ends the entry closes it, and a pair whose value never
arrived gets the null it stands for: `[a:]` is `[{a: null}]`. The key may be
a collection, as in `[[1]: 2]`.

**Fixed: input that ends inside a flow collection is an error.**

`gtext_yaml_parse()` returned a document whose root was NULL and reported
success, so `key: [a, b` with no `]` looked like an empty document rather
than a broken one - and a caller checking only for a NULL document saw
nothing wrong. The dedent rule deliberately stops at a flow collection,
because indentation says nothing about where `[` and `{` end, and nothing
else closed them either. An empty document still has a NULL root, and that is
still not an error.

**Fixed: two flow entries with nothing between them are refused.**

`key: [a` over `b]` quietly became a two-entry sequence. Within a line two
words are one plain scalar, so this only arises across a line break, where
the second line is not indented enough to continue the scalar and becomes a
second entry instead. The same rule covers flow mappings, where the entry is
a whole pair, and nested collections and aliases as well as scalars. Laying a
flow collection out over several lines is unaffected: entries separated by
`,` need no indentation of their own.

**Fixed: a scalar with no key to hold it is refused.**

A scalar indented past its block mapping is that mapping's value, and only
while a key above is still waiting for one. With every key already paired
there was nothing for it to be, and it became a trailing key with a null
value:

```yaml
a: |
    deep
  shallow
```

gave `{a: "deep\n", shallow: null}`. The column that decides this is the
line's first non-space rather than the scalar's own, because a tag or anchor
sits before the scalar and belongs to the same node - `!!str true` as a key
starts where the tag does.

**Tabs inside a plain scalar are content, and this parser keeps them.**
PyYAML raises on a tab anywhere in a plain scalar, and this page previously
counted that as a defect here. It is a YAML 1.1 rule: 1.2's
`nb-ns-plain-in-line` allows `s-white`, which includes a tab, between plain
characters. PyYAML rejects `key:\ta` on the same grounds, which no reading of
1.2 supports, and js-yaml keeps the tab. Where the two oracles disagree this
parser follows 1.2.

**`!!timestamp`, `!!set`, `!!omap` and `!!pairs` are honored at all**, which
1.2 does not require, since they are 1.1 repository types. Parsers that
implement 1.2 strictly will reject or ignore them.

@anchor yaml-tested-scope
## Tested scope

**Tests.** 99 test files under `tests/yaml/`, all passing. They cover the
scalar styles, collections, anchors and aliases including the cycle and
exponential-expansion cases, merge keys, the tag types, directives,
multi-document streams, UTF-8 and the other encodings, the DOM accessors and
mutation, cloning, the writers, the pull reader, chunked scanning, partial
input, the limits, safe mode, 1.1 mode, config mode, and YAML-to-JSON
conversion. `tests/yaml/test-yaml-real-world.cpp` parses Docker Compose,
Kubernetes and GitHub Actions shapes.

**Fixtures.** `tests/data/yaml/` holds the formatting files, one binary
regression case, and `spec-1.2.2.corpus` - the cases found by reading the
specification rather than by running the suite, which
`tests/yaml/test-yaml-spec-corpus.cpp` scores without needing the network.
Most tests carry their YAML inline as string literals, which keeps them
readable.

**Fuzzing.** Two harnesses under libFuzzer with ASan and UBSan.

`tests/fuzz/fuzz_yaml.cpp` parses and walks, from 13 tracked seeds. It was
the most productive single tool applied to this parser: two separate infinite
loops in the block-scalar scanner, a use-after-free, undefined behavior on
empty quoted scalars and several leaked token buffers. `tests/fuzz/README.md`
records each.

`tests/fuzz/fuzz_yaml_writer.cpp` holds the writers to the property all four
of their known defects broke - *if the writer says OK, the bytes it wrote
must parse, and must hold the same values*. It reaches them along four paths,
and the distinction matters: documents that came from parsing, which is what
a round trip over the suite measures, and documents built through the DOM
API, which is where the interesting failures were. A corpus of YAML text can
only carry values the parser accepts, so a DEL never reaches a writer from
the first direction and is ordinary in the second. The fifth path is the
streaming parser feeding the streaming writer with no DOM in between.

**Memory.** The suite runs clean under valgrind and under ASan/UBSan, with
`-fno-sanitize-recover=undefined` so a finding fails the run it is found in.

**Reach of the oracles, and where it ends.** This is the section to read
before trusting the word "conformant" anywhere near this parser.

- **[yaml-test-suite](https://github.com/yaml/yaml-test-suite) measures the
  reader only.** `make conformance` scores it - see
  [Status](#yaml-status) - but the suite is a corpus of *inputs* and tests no
  writer at all. `make conformance-roundtrip` is what runs it backwards.
- **A corpus measures the corpus.** Passing all 395 checkable cases is a
  statement about 406 documents, not about the grammar; see
  [Where the suite ends](#yaml-where-the-suite-ends) for the divergences found
  by reading 1.2.2 instead, none of which appears anywhere in the suite.
- **Differential testing is scored, not automated into a gate.** The same
  harness scores js-yaml and PyYAML alongside this parser, which is the
  calibration that makes the figure readable; nothing fails a build on a
  disagreement with either.
- **Fuzzing proves absence of crashes, not correctness.** It found the
  hangs and the memory errors; on its own it cannot find a parser that
  confidently returns the wrong string. What it *can* find is a writer whose
  output its own parser refuses, which is why `fuzz_yaml_writer.cpp` asserts
  that and not merely that nothing crashed.
- **No benchmarks.** Parsing speed and memory use are unmeasured. Treat the
  parser as suitable for configuration-sized documents.

## Known defects

None is open.

What stands here instead is a limit rather than a defect. **A null cannot
survive a round trip through the failsafe schema.** That schema resolves
nothing, so `null` is written and the string `"null"` comes back. Nothing
else is available: there is no spelling the failsafe schema reads as a null,
because having none is what asking for it means. It is a test asserting the
limit.

The one that stood here until recently - that a block mapping with two entries
did not parse in UTF-16 - is fixed, and it was worse than this page said: the
same mismatch broke ordinary UTF-8 with a byte order mark in front of it, which
is what a good many editors write. It is described under *The offsets and the
text they index were two different streams* below.

What remains is a documented limit rather than a defect, and it is the
outward-facing half of that same mismatch. **Every `offset` this module
reports - on an error, a warning, an event, a node's source location - counts
bytes of the decoded character stream, not of the buffer you passed in.** They
are the same number for UTF-8 with no mark, and for nothing else: a mark moves
them by three, and UTF-16 and UTF-32 have no byte-for-byte relation to it at
all. `line` and `col` are counted in characters and are right in every
encoding, so they are what to locate a fault by when the input was not plain
UTF-8. Translating the offsets back would mean keeping a map of the whole
document, which is a cost every caller would pay for something few need; it is
written down instead, on each of those fields.

## Not implemented

- **JSON to YAML is implemented**, which this list used to omit in both
  directions. `gtext_json_to_yaml()` is the reverse of `gtext_yaml_to_json()`.
  YAML 1.2 section 10.2 makes JSON a subset of YAML, so it cannot fail on the
  grammar; it can fail on `max_depth`.

  Types are preserved rather than re-resolved, which is the whole of it. YAML
  resolves a plain scalar by its contents, so a JSON string reading `true`,
  `null`, `42`, `1.5`, `~` or `yes` would change type if it were written plain.
  Every string becomes a node explicitly typed `GTEXT_YAML_STRING`, object names
  included, and the writer quotes what needs quoting because it knows. Numbers
  keep the lexeme the JSON parser preserved, so `1.0`, `1e3` and an integer of
  thirty digits all survive as written rather than being reformatted through a
  double.

  One case is lossy and says so: a JSON integer too large for an `int64` has no
  YAML integer node here, and becomes a string. That is not a third answer
  invented for the conversion - it is what this library's own YAML parser does
  with the same digits, which the test establishes by parsing them rather than
  assuming it.

- **Comment preservation on write.** Comments can be retained in the DOM but
  the DOM writer does not re-emit them. The streaming writer does write a
  COMMENT event it is given.
- **Scalar style preservation.** A parse-write cycle normalizes style.
- **Timestamp parsing into a time type**, as above.
- **Benchmarks**, as above.
- **Native Windows (MSVC)** is untested; MSYS2/MinGW is exercised.

@anchor yaml-status
## Status

Alpha. The API may change before 1.0. The parser is appropriate for
configuration files from sources you control, and for prototyping. For
untrusted input use `gtext_yaml_parse_options_safe()`, which bounds resource
consumption.

The silent-truncation defect that previously made every plain scalar suspect
is fixed, as are the multi-line and flow cases that were still open beside
it. Nothing on the list this page used to carry under
[Deviations](#yaml-deviations) is outstanding; every entry there now records
a fixed defect, and the only remaining difference from either oracle is one
where this parser is the more faithful of the two.

That is not the same as conformance, and the difference turned out to be
large. The 153 documents were chosen by working outward from defects already
found, so the corpus measured what had already been fixed.

`make conformance` runs [yaml-test-suite](https://github.com/yaml/yaml-test-suite)
against this parser. Of the 395 cases it can check - those carrying a `json`
field, checked by value, those carrying a `tree`, checked by event stream,
and those marked `fail`, checked by refusal - **all 395 pass**. The eleven
left over carry no expectation, or one the harness cannot decode; they are
counted separately rather than folded into the rate, so the figure to quote
is "395 of the 395 checkable, out of 406 shipped" and never a bare 100%. The same harness
scores js-yaml at 82.0% and PyYAML at 77.3% on the value cases, which is the
calibration that makes the number readable: neither reference scores 100%
either.

The first run scored 191 of 368, 51.9%. A hundred and seventy-five cases
have been fixed since, in twelve batches: quoted-scalar line folding and directives; a
group of structural refusals - a second top-level node, a root block
scalar's indentation, a folded scalar's blank lines, and the block scalar
header; the rule that `:`, `-` and `?` are indicators only where nothing
plain-safe follows them; tabs, which were refused anywhere in leading white
space when only indentation is forbidden to them; and a group of positional
rules - a scalar no `:` ever claimed is not a key, a comment needs white
space in front of it, and a block entry cannot start beside a node already
on its line; and a group around documents and anchors - a lone `...` no
longer invents a document, a stream may hold none at all, an anchor may be
redefined, and an alias may stand where a key does; and the tag property in
its three spellings, with the rule that only a plain scalar is resolved by
its contents; and a group of closed lists - the escapes a double-quoted
scalar may carry, the entries a flow collection may leave empty, and where
a directive may stand; and a group about where a line may begin - only a
comment may follow a `...`, a comment needs white space in front of it, and
a flow collection's continuation lines need indenting past the node that
owns them; and the empty node, which was being dropped rather than made
null; and a group about properties - a scanner error that kept its message,
an explicit key in a flow sequence, an empty key in a flow collection, a
document marker ending a block scalar, and the chunk size no longer changing
what a document means; and a node's anchors and tags, where two of a kind
with nothing between them name two nodes or none.

The denominator moved from 368 to 366 because of a harness bug, not
progress: three cases carry an explicit null where the expected value goes,
and the harness judged whether the parser had refused the input before it
tried to decode that, so refusing one scored as a defect and accepting it
was skipped.

It then moved from 366 to 395 when the harness learned to check an event
stream, and that is where the interesting part is. The 38 cases it had been
skipping were not a random sample: the suite gives a case an event stream
instead of a JSON value exactly when the value cannot be written as JSON - a
null key, duplicate keys, a key that is not a scalar - which is the same
ground a parser is most likely to get wrong. Sixteen of them were refused
outright, valid documents this parser called invalid, while the score read
"all 366 checked cases pass".

All sixteen are fixed, along with two answered inexactly.

The last two to go, `26DV` and `6BFJ`, were one gap: a property written at
the end of a line, whose node turns out to be a block mapping whose first key
is not a scalar. A property reaches the parser on the first node it can
attach to, and the handover that gives it to the collection instead ran only
on a `SCALAR` event - the one shape where the key and the `:` that opens the
mapping are adjacent. An alias key or a flow collection key puts other events
between the two, and the property was dropped, or in `26DV` carried on to a
later scalar, which says the wrong thing rather than nothing:

```yaml
top3: &node3
  *alias1 : scalar3   # &node3 is the nested mapping's, not scalar3's
```

Three things were needed. Every event that can carry a node now takes custody
of a held property, not just `SCALAR`. The check that a held property was
claimed - one event later at most - now waits through the flow collection it
precedes, because a `[` cannot reach its `:` in one event. And the stream
hands a property past an alias rather than onto it: an alias node is `*` and
a name and nothing else (`c-ns-alias-node`, 7.1), so a property on an alias's
own line is an error, and one on the line above belongs to what that line
opens. A tag written that way used to be dropped without a word and an anchor
carried on to the next node; both are now refused, and the message says an
alias may carry no property rather than reporting a second one.

@anchor yaml-where-the-suite-ends
## The other parser

`gtext_yaml_parse()` has two implementations. Input that is also JSON is
handed to the JSON parser and converted - faster, and on by default;
everything else goes through the YAML scanner. Two implementations of one
contract drift, and this one had:

| input | the fast path gave | the general parser gives |
| --- | --- | --- |
| `[""]` | `[null]` | `[""]` |
| `{"a":""}` | `{"a":null}` | `{"a":""}` |
| `["0x10"]` | `[16]` | `["0x10"]` |
| `{"":""}` | `{"null":null}` | `{"":""}` |

One rule accounts for all four. A scalar is resolved by its contents only
when it was written plain (10.3.2) - that is the whole point of quoting - and
a node built by the fast path carried no style, so every JSON string was
resolved as though it had been written bare. The general parser was taught
this some time ago; the page above records it, under the block scalars. The
second implementation never heard it.

**Nothing measured it, and the reason is the useful part.** `make conformance`
asks for `GTEXT_YAML_DUPKEY_KEEP_ALL`, because the suite has duplicate-key
cases it must not collapse - and that is the one setting which turns the fast
path off. All 395 cases had only ever gone the other way. An entire parser,
on by default, was outside every figure this page quotes.

`make conformance-fastpath` now runs the two against each other over the
suite, asking no expectation of either side, only that they agree. It reports
**5**. Four hundred and one of the suite's 406 documents are not JSON and
never reach the fast path at all, so a hundred per cent there is a statement
about five documents - and none of the four shapes above is among them. A
corpus of YAML is a thin corpus of JSON. The target is worth having because it
will catch drift in what it does cover and costs nothing, but the gate that
holds the two paths together is
`tests/yaml/test-yaml-json-fastpath.cpp`, which asks the question directly:
the fast path and the general parser must give the same answer for every
document, and a quoted scalar stays a string.

The file already held five tests of the fast path. Every one of them checked
it *on its own*, which is how it came to disagree with the other parser
without anything noticing.

## Where the suite ends

Passing all 395 checkable cases is a statement about 406 documents, not about
the grammar. Eleven divergences from YAML 1.2.2 were found afterwards by
reading the specification rather than running the suite, and a scan of all
406 case inputs confirmed that **none of the eleven shapes appears anywhere in
the corpus** - which is why the score stayed where it was while they were
wrong.

| Divergence | Rule | Was |
| --- | --- | --- |
| `tRue`, `fAlse`, `nULL`, `.Nan` | 10.3.2 lists spellings, not words | resolved case-insensitively |
| `1_000`, `1_0.5` | digit separators are 1.1 | resolved under 1.2, no warning |
| `0b101` | binary is 1.1 | resolved under 1.2, no warning |
| `0O14`, `0X1f` | 1.2 writes `0o`/`0x` lower case | resolved |
| `%YAML<tab>1.2` | `s-separate-in-line` is `s-white+`, 5.5 | refused |
| `%TAG<tab>!e! p`, `%TAG !e!<tab>p` | the same | refused |
| two `%TAG` for one handle | 6.8.2 | second silently replaced the first |
| `%YAML 2.0` | 6.8.1 | parsed as though it said 1.2 |
| `%YAML 1.2` then a bare node | `l-directive-document`, 9.2 | accepted |
| `a: *x` before `b: &x 1` | an alias names a *preceding* anchor, 7.1 | resolved forward |
| `!<> 1` | `c-verbatim-tag` needs `ns-uri-char+` | accepted |
| a NUL, ESC, DEL or C1 control anywhere | `c-printable`, 5.1 | accepted as content |
| `x<BOM>y` in a plain or block scalar | `nb-char` excludes it, 5.4 | accepted |
| a BOM before a later document | `l-document-prefix`, 5.2 | refused |

All of them are fixed and pinned in `tests/data/yaml/spec-1.2.2.corpus`, which
`make test` scores - not `make conformance`, which needs the network on first
use and is not what anybody runs before committing. Prefer upstreaming a case
to yaml-test-suite over adding one there.

The last three of those arrived by a different route, and it is worth saying
how. NEL, LS and PS were written down as a fourth divergence - "folded as
line breaks, though 1.2's `b-char` is LF and CR alone" - and they were
nothing of the kind. A terminal renders U+0085 as nothing and U+2028 as a
space, and the output had been read off a screen rather than out of the
bytes. The parser had always kept all three as ordinary content, which is
what 1.2 asks for.

Checking that properly is what turned up `c-printable`, which was not
enforced at all. A NUL, an ESC, a DEL or a C1 control travelled through as
scalar content in every style, quoted or not, and the NUL was the worst of
them: `a: x\0y` came back as `{"a": "x"}` with the rest of the scalar gone.
Both PyYAML and js-yaml refuse every one. The gate now runs over the decoded
character stream as bytes arrive, before any token is cut, so no style can be
missed - and it holds a sequence a feed cut in half rather than judging it
from its first byte. Escapes are untouched: `"\u0001"` is six printable
characters in the stream whatever it builds.

**Read the bytes, not the terminal.** Two items on a list of thirteen were
there because a screen could not draw the difference.

## The writer, and running the suite backwards

Everything above measures the parser. yaml-test-suite is a corpus of inputs
and tests no writer at all, so `make conformance-roundtrip` runs it backwards:
every document the parser accepts is written out again and re-read. All three
writers are scored, because there are three.

|  | by value | by event |
| --- | ---: | ---: |
| the DOM writer, flow style | **282 of 282, 100%** | 280, 99.3% |
| the DOM writer, block style | **282 of 282, 100%** | **282, 100%** |
| the streaming writer | **282 of 282, 100%** | 280, 99.3% |

*By value* asks whether the JSON is the same - a failure there is data lost or
corrupted, and the target holds all three to 100%. *By event* also asks that
anchors, tags and the text of every scalar survive. Block style reaches that
too; the two flow-style figures cannot, and the two documents they miss are
the whole of the difference: `ns-flow-seq-entry` has no empty alternative, so
an entry that is the empty node with no properties has to be written `~` there,
and `~` is a scalar the document did not hold. In block style the same entry
is written `-` and nothing, which is what the document said.

`make test` runs the same property over the shapes that were wrong, without
needing the network: see `tests/yaml/test-yaml-roundtrip.cpp`.

The first measurement of this scored 90.4% and 83.0%. What was wrong:

- **A resolved tag was written as bare text.** `!<tag:example.com,2000:app/foo>`
  came out as `tag:example.com,2000:app/foo` with no `!<...>` around it, so it
  re-read as a mapping key, or failed on the comma. Every tag now goes out as
  one of 5.6's three spellings: a shorthand where the tag has one, `!!x` for
  the standard namespace, and the verbatim `!<uri>` otherwise. (The verbatim
  form was percent-encoded at first; see below for why it is not any more.)
- **Block scalars had no chomping indicator and no indentation indicator.** A
  value with no trailing line break gained one, a value with three kept one,
  and a first line beginning with a space lost the space. The writer now
  decides all three before it commits to the style, and falls back to quotes
  for a value that has no faithful block spelling at all.
- **Folding turned every line break into a space.** `>` was written with one
  break where the content held one, and 8.1.3 folds that away: `"ab cd\nef"`
  came back as `"ab cd ef"`. A run of *k* line breaks needs *k+1* on the page.
- **A single-quoted scalar was chosen for content it cannot hold.** Single
  quotes have no escapes, so a line break inside them folds to a space.
- **An empty node was written `~`.** `a:` and `- ` are how 7.2's empty node is
  spelled; writing `~` put a scalar into the document that the document never
  held. It is written as nothing now, everywhere a spelling for it exists.
- **A key that ended in a property took the colon with it.** 6.9.2 stops an
  anchor name at a flow indicator and nowhere else, so `*b: 1` names the
  anchor `b:` and `!!null: 1` the tag `!!null:`. Such a key is separated from
  its colon: `*b : 1`.
- **An empty collection started a line of its own** in column zero, which ended
  the mapping that introduced it. `key: []` stays on one line.
- **A stream with no documents could not be written**, so `# comment only`,
  `...` and a file of one blank line came back as writer failures.
- **A collection inside a flow collection reset itself to block style**, which
  wrote `finish: x: 89` into the middle of a `{...}`. One function decides
  flow or block now, and both the collection and whatever introduced it ask
  it, so a container never writes `-` and a line break for a child that is
  going to be flow after all.
- **Comments are dropped**, which is the one still on the planned list.

The streaming writer had all of the same faults and a few of its own, because
it was a second implementation of the same rules and had drifted from the
first. It is not one any more: the spelling of a scalar, a tag, a property and
a block scalar lives in one place, the *choice* of spelling lives in one more
(`plan_scalar_style`), and both writers call both.

### What running it backwards still could not see

Four more writer defects were found afterwards, and they share a shape worth
naming: **a corpus of YAML text can only carry values the parser accepts.**
Every measurement above reaches a writer through a document that was parsed,
and that is not the writers' domain - the DOM API takes any `char *` a caller
hands it. A DEL never arrives from the first direction and is ordinary in the
second.

- **A character 5.1 forbids was written raw.** The escape loop escaped
  `c < 0x20` and stopped there, so DEL, the C1 block apart from NEL, and the
  two non-characters at the end of the BMP went out as themselves - 34 code
  points for which the parser refused the writer's own output, correctly,
  since `c-printable` had been enforced two commits earlier.
- **An anchor name was never checked.** The writer emitted `&` followed by
  whatever string it held. An anchor of `a b` wrote `&a b`, which reads back
  as the anchor `a` with the rest of the line as the value; an anchor of
  `a[b` wrote a document that does not parse. Unlike a scalar style, an
  anchor has one spelling and no fallback, so a name outside `ns-anchor-name`
  is now refused rather than mangled. The same holds for a tag outside
  `ns-uri-char+`; for a tag in the `tag:yaml.org,2002:` namespace naming a
  type the spec does not define, since that namespace is not the author's to
  extend and the resolver refuses `!!bogus` on the way in whatever the options
  say; and for a scalar that is not UTF-8, because a YAML stream is a stream
  of *characters* and every escape of 5.7 names a code point - the byte 0xFF
  has no spelling, and `\xFF` would read back as U+00FF, two bytes and a
  different value.
- **A tag gained a layer of escaping on every round trip.** The writer
  percent-encoded `%` unconditionally, on the premise that the reader decodes
  it. The reader decodes only where a `%TAG` prefix was substituted - and
  deliberately so, since a decoded `%21` in a shorthand would become the very
  character that makes a handle. With no matching decode, `!a%21b` became
  `!a%2521b`, then `!a%252521b`, without bound. A verbatim tag is used
  exactly as written (5.3), so it is now written exactly as held.
- **The streaming writer answered a `%YAML`, a `%TAG` and an `:` with OK and
  wrote nothing.** Those were two cases of one habit - returning success for
  an event it had no code for - and the second is the worse of the two. See
  below.

### What the writer fuzzer has found, by kind

The sections that follow are in the order the harness found them, which is
also the order in which each fix uncovered the next. Read as a list they are
long; read as a set they fall into four groups, and the groups are the useful
part:

- **The writer inventing structure for events that describe none.** An
  `INDICATOR` answered with OK, a directive written with no code for it, a
  second root node glued to the first, a directive that did not close the
  document before it. The cure is always the same: refuse, at the first event
  that cannot be written.
- **A tag taken as a label rather than an assertion.** `!!binary (((`,
  `!!int ""`, a shorthand whose handle no `%TAG` declared. A tag says what a
  node *is*, the reader checks it, and the DOM constructor did not.
- **Something decided from a scalar's text without asking its style.** The
  quoting whitelist, the DOM constructor, `is_merge_key()`, explicit tags, and
  the JSON fast path before them. Only a plain scalar is resolved by its
  contents (10.3.2); this is the one to look for first.
- **A rule applied to one spelling of a document and not the others.** The
  nesting limit that reached flow collections only, the indicator run that did
  not ask what followed it, the property that was part of its key in one place
  and not the next. A writer is a cheap spelling-changer, which is why it
  keeps finding these.
- **A conversion with no defined answer.** A double turned into an `int64_t`
  without asking whether it fits. Undefined for a NaN, an infinity, or
  anything past the range, and what the hardware gives is `INT64_MIN` -
  a plausible-looking number, which is why the sexagesimal one sat there
  unnoticed while the other tripped UBSan.
- **Two coordinate systems for one document.** The parser measured positions
  in the text the caller handed in and was given offsets counted in the text
  the scanner had decoded. Nothing said which of the two a `size_t` meant, so
  the two were interchangeable right up to the first document where they
  differed.

Most of them are in the *reader*. That is not what the harness was built for,
and it is the strongest thing that can be said for building it.

### Three places that asked the wrong node where it stood

The writer fuzzer's next run found a *parser* defect, and then two more behind
it. All three are one mistake made three times, which is the reason they are
written up together: **a key that is a flow collection has no scalar of its
own, and three separate places measured the last scalar to find out where the
key stood.** `{}` has no scalar at all, so what they measured was whatever
came before - on whatever line that was.

```yaml
a: 1
{}: 2       # was refused; so was "[x]: 3" in its place
```

`{}: 1` on its own always parsed, because with nothing in front of it there
was no earlier scalar to be measured instead. It was the *combination* that
failed, which is what made it hard to see.

- **`last_scalar_offset`, in the guard that a block key must stand where a
  block entry may start.** That guard is what refuses `x: { y: z }in: valid`,
  and it was refusing this too - for a key standing beside a node it was
  nowhere near, namely the `1` on the line above.
- **`last_scalar_key_col`, in `key_indent`.** With the offset fixed, the
  nested spelling still failed: `outer:` over `  {}: 1` measured the key at
  the column of `outer`, which matched the enclosing mapping's indentation
  when it should not have, and the entry was refused for having no key at all.
- **`last_scalar_col`, in the scanner's `node_indent`.** That sets the
  boundary a plain scalar folds across. With no scalar on the `{}: 1` line it
  came from the line above, so in `outer:` over `  {}: 1` over `  b: 2` the
  `b` folded into the value `1` and its own `:` was then orphaned. The
  scanner now tracks `last_key_col`, which a closing `]` or `}` sets to the
  column its collection opened at.

The third changed one thing outside this shape, and correctly: `[x]: 1` now
measures from the `[` rather than from the `x`, so a continuation line one
column further in folds where it used to be refused. js-yaml agrees.

A fourth sat behind those, and is a different mistake in the same area. A node
standing at the key's own column while a value is still expected is the *next
entry's key*, not that value - the rule that makes `a:` over `b: 1` two
entries. The scalar case had it; the four places a completed collection is
added to its parent did not, so

```yaml
a:
{}: 1
```

put the flow mapping where `a`'s value goes. That one was never refused - it
was accepted with the wrong shape, `{}` nested inside `a`, which is the worse
failure of the two. The rule is now one function, and it asks which style the
collection is: a **block** sequence *may* stand at its own key's column and
still be the value, because that is what `key:` over `- a` means (8.2.1). Not
making that distinction cost eight documents of yaml-test-suite, all of them
zero-indented sequences, which is how the exception got written down.

Every row of `tests/yaml/test-yaml-flow-collection-key.cpp` was checked
against js-yaml, refusals included.

### Five more the writer found, and only one of them in the writer

The run after that went five deep, each fix uncovering the next. Only the
first is a writer defect; the rest are the parser, reached through it.

- **The string `---` was written plain.** A line of exactly those three
  characters is `c-directives-end` (9.1.2), and `c-forbidden` keeps it out of
  a document's content wherever it begins a line with a break, white space or
  end of input after it (9.1.1). The whitelist deciding this is about
  characters and every one of these is `-`, which it admits. So the writer
  produced a document marker, called it OK, and the reader agreed with the
  bytes and handed back an empty document. `...` was the same. `----` and
  `---x` stay plain, because `c-forbidden` wants a break or white space after
  the three.
- **A block mapping's first entry could not have an empty key.** `: 1` is an
  entry whose key is the empty node - that is the `e-node` arm of
  `ns-l-block-map-implicit-entry` (8.2.2) - and it worked at the top level and
  as a *later* entry of a nested mapping. It did not work as the first entry
  of one: `a:` over `  : 1` was refused. A later entry only has to join a
  mapping that is already open; the first has to open it, and the branch that
  opens one ran only where no block mapping was open at all. Neither reference
  implements this arm anywhere - js-yaml and PyYAML both refuse `: 1` at the
  top level - and no suite case nests one, so nothing but the writer was
  going to ask.
- **A shorthand tag was written with a handle no `%TAG` had declared.** A
  named handle means whatever a `%TAG` declared it to mean and means nothing
  where none did (6.8.2). The DOM writer emits no directives, so a named
  handle is undeclared there by construction - and `!a!3` went out as itself,
  leaving a document this parser refuses. There is nothing to fall back on:
  `!<!a!3>` is a *different* tag, the literal URI rather than the handle's
  prefix followed by `3`, and a prefix the writer invented would be worse than
  a refusal. It is refused now, the way an unwritable anchor and `!!bogus`
  already are. The streaming writer does declare handles, and the parser's
  own event stream reports a tag as it was written (5.3), so `!e!foo` arriving
  beside its `%TAG !e! ...` still writes as it arrived - the writer tracks
  what it has declared, and forgets it at each document end, because a `%TAG`
  applies only to the document it precedes.
- **The DOM writer read two uninitialised pointers off the stack.** Adding a
  field to `yaml_writer_state` did it: the two DOM entry points set every
  field by hand, which is correct exactly until the next field is added. Both
  `memset` first now.
- **A property in front of a key was not part of the key.** `&a {}` is a key
  that begins at the `&`, and a block mapping is indented where its key is.
  Both places that ask where a key stands were taking the node's own column,
  so with an anchor the mapping was indented to the `{` and the next entry
  fell outside it: `&a {}: 1` over `b: 2` was refused as a second top-level
  node while `&a {}: 1` alone parsed. In the scanner the same blind spot was
  not new to flow keys at all - `outer:` over `  &a x: 1` over `   c` was
  refused where the same three lines without the `&a` fold into `1 c`. The
  scanner tracks `line_node_col` now: the first character on a line that is
  neither indentation nor one of the `-`, `?` and `:` indicators a compact
  entry may put in front of a node. That one field replaced three, and is
  what both the flow-key and the property questions were really asking.

### White space is in none of 10.3.2's rows

The integer row is `[-+]? [0-9]+` and its two prefixed forms are `0o [0-7]+`
and `0x [0-9a-fA-F]+`. The float rows are the same shape, and the boolean and
null rows are enumerations of whole words. **Not one of them contains white
space anywhere** - and that had never been stated in one place, so it was
enforced in pieces and the pieces had a hole in the middle.

`strtoll()` skips leading white space, and this code consumes the sign and any
base prefix itself before handing `strtoll` what follows. So the space between
the two was never measured by anything:

```yaml
+

1
```

is a plain scalar whose folded content is `+\n1`, and it came back as the
**integer 1**. `0x` over `10` came back as 16, and `a: + 1` was 1 as well.
js-yaml reads all three as the strings they are.

Two things changed. `gtext_yaml_plain_text_classify()` now says that text
carrying white space anywhere is a string, which is the whole rule in one
line and replaces a test of the two *ends* that had been added for the DOM
API's sake. And `parse_int_value()` requires a digit of its base straight
after the sign, on its own account, because a helper whose contract is a
10.3.2 row should not depend on its callers to keep it honest.

The suite has no case with white space inside something that would otherwise
resolve, which is what kept this out of every score on this page. The five
cases are in `tests/data/yaml/spec-1.2.2.corpus`.

The fuzzer came back for the two white-space characters YAML does not have.
The vertical tab and the form feed are not `c-printable` (5.1), so no parsed
scalar holds one - but the DOM API takes any `char *`, and `strtod()` skips
them exactly as it skips a space. `\v6662.` was answering *the float 6662*,
and the writer then escaped it into a quoted scalar the reader read back as a
string. `parse_float_value()` wants a digit or a `.` after the optional sign
now, the way `parse_int_value()` wants a digit, and the white-space test is
C's whole set rather than YAML's `s-white`.

### A property in front of a key, for the third time

The rule that a flow collection standing at the key's own column is the next
entry's key - the one that makes `a:` over `{}: 1` two entries rather than one
- was measuring the collection and not the entry. With a property in front,
`&k [x]` begins at the `&` and the columns no longer matched, so the
collection landed where `a`'s value goes and the `:` after it found nothing to
claim.

The correction only applies where the collection **begins its own line**.
`a: [b, c]` is a sequence on the same line as its key, and asking for that
line's first node answers `a`; ten documents of yaml-test-suite say what
happens if you do not draw that line.

### Three more, and a harness that had been trapping in silence

- **An empty `!!binary` value was refused.** Base64 of no bytes is the empty
  string, so `!!binary ""` is an empty byte string - which is what PyYAML
  gives back, and its type repository is where `!!binary` is defined. Refusing
  it made the writer's own output unreadable, since that is exactly how an
  empty binary node is written.
- **`!!binary` was an assertion nobody checked.** A tag says what kind of
  value a node holds, and this one can be false. The DOM constructor took any
  text at all: a node built from good base64 answered *false* to
  `gtext_yaml_node_as_binary()` where the same document parsed answers with
  the bytes, and text that is not base64 was taken all the same and written
  after the tag, so `(((` went out as `!!binary (((`. It is decoded on the way
  in now, the way the parser decodes it, and refused when it will not decode.
- **A document was written with two root nodes.** `l-bare-document` is a
  single `s-l+block-node` (9.2). `*a: x` is not a mapping - 6.9.2 stops an
  alias name only at a flow indicator, so the name is `a:` and the `x` is a
  second node at document level, which is why the DOM parser refuses the
  document. The streaming parser reports what is written and leaves composing
  to its consumer, so the writer saw an `ALIAS` and then a `SCALAR`, wrote
  both, and produced `*a:x`: two nodes run together with no separator, and a
  document nothing can read. It is the same habit as answering an `INDICATOR`
  with OK - inventing structure for events that describe none - and a second
  root is refused now.

The last of those was found through a path of the fuzz harness that trapped
**in silence**: only `must_round_trip()` printed what the writer had produced,
and the event-pipe path did not, so every find there began with reverse
engineering a dozen bytes. It prints now. That is a harness defect, and the
count in `tests/fuzz/README.md` says so.

### A directive that did not close the document before it, and a quoted `<<`

- **A directive was written straight after a document's content.** A directive
  may only follow a document that has been ended *explicitly*: `l-yaml-stream`
  reaches a directive document through `l-document-suffix`, which is
  `c-document-end` (9.2). The writer wrote only the line break. A `%TAG` there
  produced a stream this parser refuses - correctly, *"Directive after
  content, with no `...` to close the document"* - and a `%YAML 1.2` after a
  **plain** scalar was worse: it folded into the scalar, so `a` over
  `%YAML 1.2` came back as the one string `a %YAML 1.2` with nothing reported
  at all.
- **A quoted `"<<"` was taken for a merge key.** A key is a merge key because
  its *contents* resolve to `tag:yaml.org,2002:merge`, and only a plain scalar
  is resolved by its contents (10.3.2). Both PyYAML and js-yaml read `"<<"` as
  the two-character string. Taking it for a merge key was the usual two faults
  at once: `{"<<": 1}` was refused for a merge value that is not a mapping,
  and `{"<<": {a: 1}}` was **merged** - the key vanished and its contents were
  spliced into the mapping around it, silently. An explicit `!!merge` tag
  still says so whatever the style, because then it is the tag and not the
  contents doing the resolving.

That last one is the fourth time this rule has been the answer here, after the
JSON fast path, the writer's quoting whitelist and the DOM constructor. It is
worth stating once more as a place to look: **anything this library decides
from a scalar's text has to ask what style it was written in first.**

### A regression of this page's own making

`-`, `?` and `:` are the indicators a compact entry may put in front of its
node, and `line_node_col` - added two sections above to answer where a key
begins - skipped every one of them. It should skip them only where white
space follows: `c-l-block-seq-entry` is `"-" s-l+block-indented` and
`s-l+block-indented` begins with separation, so `- x` is an entry holding `x`
while **`-: 1` is a mapping whose key is the plain scalar `-`**.

Measuring that mapping at the `1` rather than at column zero left one entry
parsing and a second refused for not being on its key's line:

```yaml
-: 1
x: 2        # refused: "Mapping key not on same line as ':'"
```

It survived three commits, because the suite has `-: 1` nowhere and neither
did any test here. Four cases are in `tests/data/yaml/spec-1.2.2.corpus` now,
and six rows of it fail without the fix.

The scanner holds such an indicator for one character rather than looking
ahead, because a scanner fed in chunks cannot reliably see the next byte.

The parser had the same gap one layer up, and the fuzzer found it next. The
test for "does this collection begin its own line?" - which is what keeps
`a: [b, c]` from reading as two entries - was `block_key_may_start_at()`,
which skips `-`, `?`, `:` and `*` without asking what follows them. That is
deliberate where it is used for its own guard, since an alias event's offset
points past its `*`. It is wrong here: `-: {a: 1}` looked like a flow mapping
standing at the start of its line, so the collection was taken for the next
entry's key and the value it really was went missing. The two tests are
separate functions now, because they are separate questions.

### Base64 is exempt from the whitelist, not from the plain style

The writer's quoting whitelist is bypassed for `!!binary`, because `/` and `=`
are ordinary in base64 and quoting every binary scalar would be noise. The
bypass was total, and it should not have been: a line break folds to a space
(6.5), and white space at either end is separation the scanner takes off
before the content begins.

So a binary scalar with a break in its text - which is how base64 is written
by hand, in short lines - went out plain across two lines and came back with
the break folded into a space. The *bytes* were the same, since base64 ignores
white space, which is why nothing measuring values noticed. The text was not,
and the text is what `gtext_yaml_node_as_string()` returns and what this
library keeps as written rather than re-encoding (suite case 565N).

Single-line base64 still goes out plain, which is what the bypass is for.

### A kept trailing break, doubled by the document separator

A block scalar ends its own last line, and with `+` chomping that break is
part of the value (8.1.1.2). The break `DOCUMENT_START` writes before the next
`---` was written unconditionally, so it landed on top of the one the block
had already written and the value gained a line feed:

```yaml
|+
  a
            # "a\n\n"
---
x
```

came back as `a\n\n\n`. Clip and strip chomping collapse a trailing break,
which is why this only ever showed on `+` - and only where a second document
follows, since with nothing after it there is no `---` to separate from. Two
conditions at once, neither of them rare on its own.

The writer has a `writer_write_separator()` that knows whether a block scalar
has already terminated the line; the document separator was the one place not
going through it.

### A tag is an assertion the constructor did not check, twice more

`!!binary` was the first of these; the rest of the tagged scalar types had the
same gap. The parser refuses `!!int ""` - an explicit tag names the type whose
syntax 10.3.2 defines, and the content has to be in it - but the DOM
constructor took any text at all, so the writer put such a node out as
`!!int ""` and this parser refused the writer's own output. Six shapes did
that: `!!int`, `!!bool` and `!!float` over an empty value or over `abc`.

Two of them turned out to be the *parser's* answer being wrong rather than the
constructor's being absent:

- **`!!float 12` was refused.** 10.3.2's float row is
  `[-+]? ( \. [0-9]+ | [0-9]+ ( \. [0-9]* )? ) ( [eE] [-+]? [0-9]+ )?` - the
  fraction is optional, so `12` is in it. Implicit resolution answers *int*
  for that text only because the int row is tried first; an explicit
  `!!float` is asking for the other reading. Both references give 12.0.
- **`!!null x` was accepted**, and the `x` went nowhere. The null row is
  `~ | null | Null | NULL | <empty>` and nothing else. js-yaml refuses it;
  PyYAML accepts it, being 1.1, which is the sort of disagreement that makes
  a single oracle dangerous.

### A nesting limit the block half of the grammar never reached

`max_depth` was counted in the stream layer, at the `[` and `{` of a flow
collection. Block structure never touched it, because block structure is
composed by the DOM parser rather than reported by the scanner. So

```yaml
- - - - - ... x        # five thousand of them
```

parsed to a DOM five thousand deep with the default limit of 256 in force.
That is exactly the input a nesting limit exists for, and it was the one input
the limit did not see. The same held for nested block mappings.

The limit belongs where the parse stack deepens, which is `stack_push()`, and
that is where it is now - one place for both halves of the grammar. A depth
refusal is also not an allocation failure, and all eleven of that function's
callers reported one; they share a `stack_push_failed()` that says which
happened.

It surfaced through the writer, and the mechanism is worth keeping in mind:
the flow-style writer turns block nesting into flow nesting, which *is*
counted, so the writer produced a document this parser then refused. **A limit
that only some spellings of the same document reach is not a limit**, and
running a document through a writer is a cheap way to change its spelling.

The test that was here asked "if it failed, did it fail with `E_DEPTH`?",
which passes when nothing fails at all.

### A second limit nobody set, in the scanner

The scanner tracked flow context in a fixed 32-entry array, and when it ran
out the push was **dropped** while the matching pop still counted down. Past
32 nested flow collections the scanner believed it was back in block context
with the brackets still open, and mis-scanned what followed rather than
refusing it.

It does not fail on every shape. `[[[ ... a: 1 ... ]]]` forty deep comes back
right, because one dropped push and one clamped pop cancel out - which is why
it took a fuzzer and a particular shape to reach:

```yaml
[{: [{[[[[[[[[[[[[[[{":": [[[[[[[[[[[[[[~]]]]]]]]]]]]]]}]]]]]]]]]]]]]]: }]}, ~]
```

At thirteen it parsed and at fourteen it did not, because thirty-three
brackets are open at the deepest point. The error was *"Unterminated flow
collection"* on a document whose brackets balance.

The array grows now. A nesting limit is `max_depth`'s job - it defaults to 256
and both the stream layer and the parser enforce it - and a fixed array
somewhere else is a second limit nobody set and nobody can see.

### A built scalar with white space at either end

Also from the writer fuzzer, and the same lesson as the DOM constructor's
other faults: `strtoll()` skips leading white space, so `" 3"`, `"\t3"` and
`"\n3"` all answered *the integer 3*. The writer then quoted them - correctly,
since no plain spelling of that text exists - and the reader read back the
string. A plain scalar's content has white space at neither end, since
ns-plain begins and ends with an ns-char (7.3.3), so text carrying any can
only be a quoted scalar, and a quoted scalar is a string (10.3.2). The
trailing end was already right, which is why only half of this ever showed.

No corpus can put this question: a parse of `" 3"` *is* the integer 3, because
the scanner takes the space off before any of it is asked. The text only
reaches the classifier through the DOM API.

### Two event APIs that were not a pipe

`gtext_yaml_writer_event()` takes a `GTEXT_YAML_Event`. So does the callback
of `gtext_yaml_stream_new()`. The types fit, the names match, and joining them
is the obvious thing to write:

```c
GTEXT_YAML_Status on_event(GTEXT_YAML_Stream *s, const void *ev, void *user) {
  return gtext_yaml_writer_event((GTEXT_YAML_Writer *)user, ev);
}
```

That returned `GTEXT_YAML_OK` throughout and wrote `a1b2` for

```yaml
a: 1
b: 2
```

**They are not two ends of a pipe.** The writer takes *composed* events - a
collection is `MAPPING_START`, its pairs, `MAPPING_END` - which is the shape
`gtext_yaml_stream_walk()` produces from a parsed document. The streaming
parser reports structure *as it was written* instead: the `:` of a block
mapping, the `-` of a block sequence, and even the `,` between two flow
entries arrive as `GTEXT_YAML_EVENT_INDICATOR`, and composing them is the
consumer's job - which is what the DOM parser is. A lone scalar is the whole
of what crosses unchanged.

Nothing said so, and the writer's silence was what made it look as though
something did. An indicator is refused now, at the first event that cannot be
written rather than quietly at every one of them, and the header says what the
function takes.

This was looked for as a gap in the measurement - the round trip drives the
streaming writer from a walk of the tree, so the event-API path was scored
nowhere - and it is not one. Scoring that pairing over the suite gives 36.9%,
which measures the mismatch and not the writer. `tools/conformance/yaml_roundtrip.c`
keeps a `-e` flag that runs it, as a diagnostic and not as a mode:
`make conformance-roundtrip` still scores the three writers there are.

`tests/fuzz/fuzz_yaml_writer.cpp` searches the same space without a list, and
holds the writers to the property all of those defects broke: *if the writer
says OK, the bytes it wrote must parse, and must hold the same values.* A
refusal is always a permitted answer - not every value has a YAML spelling,
and saying so is correct; what is never permitted is claiming success and
producing something this library cannot read back.

Its first find was a bug in itself rather than in the library, which is worth
recording as its own kind of result: the event API resolves nothing, so it
accepts an alias to an anchor nobody declared, and the writer is right to hand
back something equally unresolvable. The two that followed were real - the
non-UTF-8 scalar above, and the verbatim tag below, from a tag of `!&!`.

Three more parser defects came out of the same work:

- **`\x` wrote a byte where 5.7 names a character.** `ns-esc-8-bit` is
  handled with `\u` and `\U` now; it used to be handled apart from them and
  wrote the raw byte, which is right below U+0080 and wrong above it. `"\x92"`
  produced a lone 0x92, and a lone continuation byte is not UTF-8 - so the
  parser refused `"a\x92b"`, which is exactly what the writer produces for a
  C1 control. A `\x` or `\u` without its hex digits is an error too; it used
  to copy the digits through and substitute U+FFFD respectively.
- **`c-verbatim-tag` was not held to `ns-uri-char+`.** Only the `+` was
  enforced, from the earlier fix for `!<>`. A space, a tab, a brace, a
  quotation mark, a non-ASCII byte and a bare `%` all travelled through as
  part of the tag.
- **Nine characters could not begin an anchor name.** `ns-anchor-char` is
  `ns-char` minus the flow indicators, so `&!x`, `&&x`, `&*x`, `&#x`, `&%x`,
  `&|x`, `&>x` and the two quotes all name anchors. Each was taken for what it
  means somewhere else - a tag, a comment, a directive, a block scalar - and
  the name came out empty. The rule *inside* a name was already right;
  this was the same rule at the first position. The writer is what asked the
  question, by spelling an anchor a caller gave it.
- **A shorthand tag's name could not begin with `-`, `?`, `:`, `#` or several
  others.**
  `c-ns-shorthand-tag` is a handle and then `ns-tag-char+`, which is
  `ns-uri-char` less `!` and the flow indicators - so `!-` is the tag `!-`,
  and the `-` was being taken for a block entry indicator instead. `!-[]`
  parsed, because the `[` ends the name before anything can misread it; only
  putting a space after the tag showed it, which is exactly what the writer
  does. `#` went the same way, since it starts no comment where no white space
  precedes it. The rule is `ns-tag-char` now rather than whatever the branches
  below it happened not to catch, so `!|` is still a block scalar - `|` is not
  a URI character. The same rule at the first position of an *anchor* name is
  two entries below; this is it for tags.
- **A truncated UTF-8 sequence at the end of the stream was let through.**
  The gate that enforces `c-printable` runs over the decoded character stream
  as bytes arrive and holds a sequence a feed cut in half, which is right
  while more input may come; at the end of the stream it was still holding it,
  and skipping it, on the grounds that `gtext_utf8_validate()` would catch it
  once the scalar was assembled. Bytes on a directive or a comment line never
  become a scalar, so nothing ever did: `%` followed by a lone 0xC2 was a
  document, and the same bytes with a line break after them were not. It
  surfaced because the writer emitted the directive back and the parser then
  refused what it had just accepted.
- **Quoting a scalar changed its value.** Only a plain scalar is resolved by
  its contents (10.3.2), so the writer may quote for style anywhere except
  where the plain text would have resolved to something other than a string -
  and the whitelist deciding that had left out two characters that can appear
  in one. `~` is the null the 10.3.2 table gives first and `+` leads the core
  schema's integer and float rows, so null came back as the string `"~"` and
  the integer `+1` as `"+1"`. Neither character is a `c-indicator`; neither
  needed quoting at all.
- **And not quoting one changed it too.** The same whitelist admitted `-`
  unconditionally, but `ns-plain-first` admits it only when an
  `ns-plain-safe` character follows - a lone `-` on a line is a block
  sequence entry. The string `"-"` went out plain and came back as a sequence
  holding one empty node.
- **A scalar built through the DOM API had no type worth the name, and the
  writer had to guess.** `gtext_yaml_node_new_scalar()` made a *string* of
  whatever it was given, which is a default rather than an assertion, and it
  made both halves wrong: `gtext_yaml_node_type()` said "string" of a node
  holding `1`, and the writer, told it was a string, wrote it plain - so it
  came back as the integer `1`. There was no way through the API to write a
  string that looks like a number. The plain constructor takes the type from
  the text now, as a parse of the same characters would;
  `gtext_yaml_node_new_scalar_typed()` is where a caller says otherwise; and a
  string the text would not have produced goes out quoted.

  Three smaller faults sat behind that one, each found by the writer fuzzer
  once the one in front of it was gone:

  - **A tag did not stop the text from deciding.** A tag is what says what
    kind of scalar a node is; only a non-specific tag on a node written plain
    hands the question to the contents (10.3.2). So `!` over an empty scalar
    is the empty *string* - which is what a re-read gives - and the
    constructor was answering null.
  - **A type was set without the value that goes with it.** A node marked
    integer kept a zeroed union, so `gtext_yaml_node_as_int()` answered 0 for
    a scalar of `1`, and every conversion built on it - `gtext_yaml_to_json()`
    included - answered the same.
  - **An empty tag string was a tag everywhere but where it was written.** A
    node carrying `""` was given a tag's spacing and none of its text, and a
    scalar with nothing else to write wrote nothing at all - so an entry
    holding one disappeared from the sequence it was in.
- **A verbatim tag stopped being one as soon as its brackets came off.** They
  were stripped in the stream layer, and after that nothing distinguished
  `!<!a!>` from the shorthand `!a!` - so it was held to the rule that a handle
  must be declared, and refused for a `%TAG` nobody had written; and with
  `%TAG ! tag:e.com,2000:` in force, `!<!a>` was expanded by it into
  `tag:e.com,2000:a`. 5.3 says a verbatim tag is used exactly as written. The
  brackets travel as far as the resolver now, which is the only thing that
  ever knew to take them off; `gtext_yaml_node_tag()` still answers the bare
  URI. The writer fuzzer found this one, from a tag of `!&!`.

Three parser defects turned up the same way, none of them visible to a test
that only reads:

- A node begins where its *properties* begin, not where its content does. The
  properties are held by the stream and reported with the event they end up
  on, so `&anchor c: 3` under an empty `b:` arrived at the column of the `c` -
  eight past the mapping - and was taken for b's value rather than the next
  key.
- A bare `!` is a whole tag property (5.3) and an ordinary node follows it.
  The scanner read that node as the tag's *name*, which takes a `:` with it,
  so `a: !` over `b: 2` was a mapping whose second key was `b:`; and the
  stream never recorded where the `!` was written, so it was carried past the
  empty node it belonged to and hung on the next one.
- `gtext_yaml_stream_feed()` stopped at the first alias it read and returned.
  The rest of the document was read only because `gtext_yaml_stream_finish()`
  ran a *second copy* of the same four-hundred-line loop that did not stop -
  a copy that had also never gained `%YAML` and `%TAG` handling, nor any of
  the fixes the first had collected. There is one copy now.

### The offsets and the text they index were two different streams

The parser asks positional questions no token carries the answer to: *is this
`:` the first thing on its line? what column does this entry begin at? is
there already a node beside it? is this the `---` line?* Six helpers answer
them, and each works the same way - take the node's offset, scan backwards to
the line break, and read what stands between.

The text they read was `ctx->input_buffer`, the bytes the caller handed in.
The offset they were given counts bytes of the **decoded** character stream,
which is what the scanner reports everything in. Those are the same bytes for
UTF-8 with no byte order mark, and for nothing else.

So a block mapping with two entries did not parse at all:

```
FF FE  "a: 1\nb: 2\n" in UTF-16LE   ->  "Mapping key beside a node already
                                           on this line"
```

The page that recorded this called it a UTF-16 defect, which understated it.
The mark is stripped before decoding, so *ordinary UTF-8 with a byte order
mark in front of it* - what a good many editors write - is shifted by three
and fails exactly the same way. `EF BB BF` and then `a: 1` over `b: 2` was
refused with the same message. Every encoding was affected, and every shape
with more than one entry: a sequence of two, a nested mapping, an explicit
key, a key carrying a property. The single-entry mapping that the encoding
tests all used asks none of those questions, which is why five of them passed
while the feature did not work.

It was written up as a design decision rather than a patch because the
scanner *compacts* its decode buffer as it consumes - `memmove` in
`scanner_consume` - so it is a sliding window an absolute offset cannot index.
That framing had the layers the wrong way round. The buffer is a window
because it is allowed to be; the DOM parser is the one caller that needs it
whole, and it already holds the entire document. So the scanner now takes a
switch: with it on, the consumed prefix is kept, `cursor` and `offset` stay
equal, and the decoded stream can be indexed from its first byte. The DOM
parser turns it on and the event-only streaming API does not, which is what
keeps a streaming parse bounded by its tokens rather than its length.

The field the helpers read is called `decoded_input` now, and it is filled
where each event arrives rather than once at the start - the scanner owns that
buffer and moves it as it grows. `input_buffer`, with its comment about a
future in-situ mode, was a plausible-looking name for the wrong text, and the
wrong text is what every one of those six helpers read.

yaml-test-suite is UTF-8 throughout, so `make conformance` could not see any
of this and still cannot. The gate is
`tests/yaml/test-yaml-encoding.cpp`, which now parses fourteen shapes in six
encodings and requires all six to agree - on what the document means, and on
whether it is a document at all. Reverting the fix fails it sixty times, twelve
of the fourteen shapes in each of the five encodings that are not plain UTF-8.
Every one of those sixty is a disagreement about whether the bytes are a
document: eleven refused where UTF-8 accepts, and `--- a: b` accepted where
UTF-8 refuses it.

The memory-safety half was fixed first and separately: `colon_begins_its_line()`
scans backwards and was the one of the six with no bound check, so an offset
past the end of the caller's buffer made its first read land outside the
allocation. ASan reported a heap-buffer-overflow 22 bytes before whatever the
allocator had put next. The reproducing bytes are still a test and a tracked
fuzz seed.

### A conversion with no answer, in two places

`(int64_t)f` where `f` is an infinity is undefined behaviour - 6.3.1.4 - and
UBSan says so in as many words: *"inf is outside the range of representable
values of type 'long'"*. What the hardware does on x86-64 is hand back
`INT64_MIN`.

The writer fuzzer built the node that reaches it: a scalar whose text is
`.INF` and whose caller said it was an integer.
`gtext_yaml_node_new_scalar_typed()` took that claim, and at the time nothing
checked it - so the text and the type disagreed, and the conversion still had
to have a defined answer. It leaves the union at its zero, which is what text
resolving to neither an int nor a float already gets.

The constructor refuses that node outright now - see *A claim was checkable
whether or not a tag was there to check it* below - so nothing reaches the
conversion from this door any more. The bound stays where it is: the union is
filled before the claim is judged, so the cast still runs on the way to the
refusal, and the resolver reaches it by another road entirely.

Looking for the same shape elsewhere found it a second time, quietly: YAML
1.1's sexagesimal integers are accumulated in floating point and then cast,
with nothing between. So `1:99999999999999999999999999999999` resolved to the
integer **-9223372036854775808**, and `!!int` on it did too. A *decimal*
that large has always been handled - `strtoll()` reports `ERANGE`,
`parse_int_value()` gives up, and the scalar stays the string it was written
as - but the sexagesimal rows never asked. They answer the same way as the
decimal now: a string where the text alone decides, and a refusal where the
tag says `int` and there is no string to fall back to.

The bound is one function both call. Writing the test out twice was how the
two had come to differ in the first place, and `(double)INT64_MAX` rounds
*up* to 2^63, so `<= INT64_MAX` is not the comparison - `< 2^63` is.

### Four the widened writer fuzzer found in its first ninety seconds

The harness could only ever build three kinds of node with default options.
Given the parse options, the write options, four more node kinds and a stored
scalar style to choose from, it found four defects before the first run
finished - which says more about what it could not previously *construct*
than about how hard any of these were to hit.

**A preferred scalar style changed what a scalar was.**
`GTEXT_YAML_Write_Options::scalar_style` was applied to every scalar whatever
its type. Only a plain scalar is resolved by its contents (10.3.2), so any
other style makes a scalar a string: the null went out as `""` and came back
the empty string, 42 came back `"42"`, true came back `"true"`. Four of the
five styles did it, to four of the five types. The style is honoured for
strings, where it costs nothing, and ignored for everything else. Canonical
form is exempt and needs to be: it writes `!!int` in front of the value, and
an explicit tag carries the type whatever the quoting does.

**A comment was written without being checked.** Twice over, and the second
is the worse. A character 5.1 forbids came out raw, so the writer produced a
document this library refuses to read. And a line break in an *inline*
comment ended the comment and made content of the rest - a mapping of one
entry with the inline comment `one\nevil: yes` was written as `k: v # one`
over `evil: yes` and read back with **two** entries. The comment escaped into
the document and nothing reported it. A comment has one spelling and no
escapes, so one that cannot be written is refused, which is the position the
writer already took for an anchor name. A leading comment keeps its one
exception: `\n` renders as another `#` line, which is a real spelling of a
multi-line comment.

**A NUL stopped the C conversions where 10.3.2 does not.** The mirror image
of the white-space defect above it: white space makes `strtoll()` and
`strtod()` read *past* the end of a row, and a NUL makes them stop *before*
the text does. `"42\0x"` was handed to `strtoll()`, which saw `42` and
answered the integer 42 - so the DOM API built an integer out of text the
parser reads as a string, and a quoted `"42\0"` *parses* to a string. The
tell was that `"true\0"` was already a string: the bool and null rows compare
with lengths and were right, and only the two rows that hand their text to
the C library were wrong.

**A tag had to agree with the text but not with the declared type.**
`gtext_yaml_node_new_scalar_typed()` takes two claims, and only one was
checked against the tag. The check skipped every string-typed node, on the
reasoning that `!!str` takes any text and needs none - true of `!!str`, and
the exemption was written for the tag and applied to the *type*. So a node
tagged `!!int` and declared a string was built, the writer emitted
`!!int "abc"`, and this library refused to read its own output. What a tag
names is what `dom_scalar_type()` answers, and the declared type has to match
it; a tag this library does not resolve still answers "string", so a custom
tag leaves the type to the caller, which is the point of one.

### An omap is an ordered mapping, and only half of that was enforced

`!!omap` takes a sequence of single-pair mappings whose keys are **unique**.
The resolver holds a parsed one to both rules and refuses either violation -
*"omap entries must be single-pair mappings"*, *"omap keys must be unique"*.
The DOM appenders held it to neither:

```
  new_omap(), append {a: 1}, append {a: 2}   built
  written                    !!omap [{a: 1}, {a: 2}]
  read back                  omap keys must be unique
```

The same shape as the typed constructor above: a node the constructors accept
and the parser will not read back is a node nothing can write, so the
appenders ask the question now, using `nodes_equal()` - the comparison the
resolver uses - so that the two doors cannot drift apart on what *the same
key* means. `gtext_yaml_sequence_insert()` had it too and is fixed alongside.

`!!pairs` is the type that takes duplicate keys; that is the whole difference
between the two, so it is deliberately left alone.

It was found as a stored fuzz artifact from an earlier run, and it is worth
recording how nearly it was miscatalogued. The artifact's output was 434
bytes of UTF-16 holding six levels of nested flow mappings, and read by eye
the striking thing in it was `!!null ""` - a quoted empty scalar behind a
`!!null` tag, which looks exactly like the style-versus-tag contradiction this
page describes elsewhere. That went into *Known defects* as the diagnosis.
It was wrong: `!!null ""` parses perfectly well, because a tag decides the
type and the empty string is a legal null content. Decoding the artifact and
asking the parser for its *error message* - rather than reading its output and
inferring one - gave the answer in four words. **The failing input says which
fault it hit; look at that before looking at the bytes.**

### Quoting is a free choice only for a string

`scalar_needs_quotes()` admits a small whitelist of characters and quotes
everything else. The note above it says why that is safe: *"quoting is
value-preserving here: neither text resolves to anything but a string"*. True
of a string, and of nothing else - and the whitelist was applied to every
scalar.

`:` was not on it. `0:0` is nonetheless a legal plain scalar - 7.3.3's
`ns-plain-char` admits `:` where an `ns-plain-safe` character follows it - and
parsed with `yaml_1_1` it is the sexagesimal integer 0. It was written in
quotes, and quotes make a string:

```
  parsed with yaml_1_1   0:0     the integer 0
  written                "0:0"
  read back with 1_1     "0:0"   the string
```

The writer fuzzer found it through the event pipe, which is where it had to
be found: there a scalar arrives plain and has to leave plain, because
plainness is what carries the type, and the streaming writer is not told what
anything resolved to. That rules out the other fix available for the DOM
writer - *don't quote a non-string* - which is exact where the type is known
and unavailable where it is not.

So the whitelist learned the production it had been standing in for, for `:`
alone: a colon that is not first, has a character after it, and that character
is `ns-plain-safe` for the context - which excludes the flow indicators inside
a flow collection, where a `,` or a `]` would end the scalar rather than
belong to it. The first position is left as it was on purpose: `ns-plain-first`
admits `:` there under the same rule, and a leading `:` is how a block mapping
writes a value with no key, which is too close to the syntax to be worth the
one character it saves.

The exposure was narrow, which is why it lasted: in the 1.2 core schema `0:0`
is a string either way and quoting a string costs nothing. It is 1.1, where
sexagesimals resolve, that the quoting reached.

### The writer was never told which dialect it was writing for

Only a plain scalar is resolved by its contents (10.3.2), so *may this go out
plain* is a question about what the reader resolves - and the reader's answer
is what a schema and a version **are**. `GTEXT_YAML_Write_Options` carried
neither, so the writer answered with the 1.2 core schema whatever the document
had been read as, and every other dialect read back something that had not
been written:

```
  the string "yes"   written plain   read back under 1_1   the bool true
  the string "012"   written plain   read back under 1_1   the integer 10
  a null             written "~"     read back under JSON  the string "~"
```

Six of the eight 1.1 spellings probed came back as the wrong type. The page
had recorded only the `[~]` case, and called it a gap rather than a defect on
the grounds that the writer had never been given the question - which was
right about the cause and too generous about the cost, because the 1.1 half
loses a *value* and not a spelling.

The options carry `schema` and `yaml_1_1` now, defaulting to the 1.2 core
schema, which is what this writer emitted before it could be told and what
`gtext_yaml_parse_options_default()` reads - so no output changes until a
caller says otherwise. Set them to the parse options a document came from and
a string spelling one of that dialect's words is quoted, and a null is spelled
the way that dialect spells one.

Two things fell out of doing it rather than being aimed at. The rows that
decide all this were written out a second time inside
`gtext_yaml_plain_text_classify()` with the core-schema answers hard-coded,
under a comment reasoning that *"a document written here is not read back in
1.1 mode"* - a claim about the caller rather than about the library, and the
fuzzer falsified it. There is one copy now and the 1.2 spelling is a call to
it. And the failsafe schema turns out to be the one dialect a null cannot
survive at all: it resolves nothing, so `null` is written and a string comes
back. That is what asking for the failsafe schema means, and it is a test
asserting the limit rather than a defect.

### A comment is only a comment where nothing follows it

A comment is `#` and then everything to the end of the line (7.1). The writer
knew the rule for the comment's own text - a line break inside one ends it
early, so a comment carrying one is refused - and not for what it is written
*next to*. Three places put something after a comment on the same line, and
each lost a different thing:

```
  a flow collection      [x # note, y]      Unterminated flow collection
  a block mapping key    k # note: v        read back as a mapping of NONE
  a streamed comment     - x# mid           read back as the scalar "x# mid"
```

The first is the one that was written down as an open question here, on the
understanding that it needed a decision about whether this writer preserves
comments or merely tolerates them. It needed no such decision, because the
premise was wrong: it was described as reachable only by building a document
through the DOM API, and it is not. The parser attaches a comment written
inside brackets to the entry before it, so

```
  [ x, # note
    y ]
```

is input this library reads, writes, and then refuses to read back. Refusing
to write it would mean refusing a document we had just parsed, and dropping
the comment would lose one on every round trip of an ordinary flow
collection. 7.4 permits a line break inside a flow collection - which is how
that input spelled it in the first place - so the writer ends the line and
carries on below it. The flag that decides this is a property of the
*position* and not of the node: a nested collection's own trailing comment
sits inside its parent's brackets.

The second came out of testing the first. An implicit key shares its line
with the `:` that follows it, so a key carrying an inline comment cannot be
one - and a mapping of one entry went out as `k # note: v` and came back as a
mapping of **none**, with no error anywhere to say so. 7.4's explicit form
puts the key and its colon on separate lines and is what the equivalent input
spells, so that is what gets written:

```
  ? k # note
  : v
```

The third is the worst of them and was in the other writer. `#` begins a
comment only at the start of a line or after white space; anywhere else it is
an ordinary character of the plain scalar it lands in. The streaming writer
emitted the indent for a fresh line - nothing at all, at the root - straight
after the scalar it had just written, so a comment did not merely go to the
wrong place, it was **absorbed into the value**, and the result parsed
cleanly. It had no idea where on a line it was; every byte it emits goes
through one function, so that is where it now keeps track. The comment event
already carried a flag saying which kind of comment the caller meant, and it
had never been read.

Nothing in the test suite had ever written a comment event, which is how the
third survived. The first two were found by reading what the writer does
after it writes a comment, once the first had a fix to check.

**A claim was checkable whether or not a tag was there to check it.**
The check above ran only for a node that also carried a tag, on the reasoning
that a tag is an assertion that can be false. True, and beside the point: the
tag is not what makes the claim checkable, the *type* is.
`gtext_yaml_node_new_scalar_typed(doc, "NO", 2, GTEXT_YAML_NULL, NULL, NULL)`
is the same false claim as `!!null NO`, and only the tagged spelling was
refused - the same assertion went through one door and not the other.

What got through could not be written by anything. Canonical form emitted
`!!null "NO"`, which this library then refuses to read; plain form emitted
`NO`, which reads back as the string. The contradiction was in the node, so
the constructor is where it stops.

`gtext_yaml_node_new_scalar()` cannot build one - it takes the type *from* the
text rather than from a caller - so this was the only door a false claim could
enter by, which is what made it worth closing rather than tolerating. A string
is still never refused, and needs no check: any text is a string, and text
that would resolve to something else is given a quoted style so that it stays
one.

It is a change to published behaviour: the constructor returns NULL where it
used to return a node. It is a narrow one, because no caller can have been
relying on it for anything that worked - every such node either wrote output
this library refuses to read, or read back as a different type than the one it
claimed. The property the gate had been hiding is now a test in its own right:
**what the typed constructor accepts for a type is exactly what this parser
accepts behind the tag that names it.** That one is checked against the parser
rather than against a table, so it cannot be made to agree with a mistake.

### An anchor moved, and the registration it made did not

`adopt_own_line_anchor()` was written for an anchor with no token to arrive
on. A flow collection has a `[` or a `{`; a block one has nothing, so an
anchor written on the line above reaches the parser attached to the first
scalar the stream can hang it on - the mapping's first key, or the sequence's
first entry - and the function moves it to the collection where it belongs.
That much was right, and the cases tested for it all put their alias *after*
the collection, where the collection has closed and re-registered the name.

An alias *inside* the collection was still bound to the scalar:

```
  &O          *O gave the string "k", so a document both PyYAML and js-yaml
  k: v        read as recursive came back finite - and said so nowhere.
  j: *O
```

The anchor string moved and the alias-table entry did not. The scalar
registers the name before `adopt_own_line_anchor()` ever sees it, and that
entry is what an alias looks the name up in, so every alias written between
the anchor and the end of the collection resolved to a node that never held
it. The flow spelling `&O [1, *O]` was always right for the reason the block
one was wrong: there the anchor arrives with the `[`, no scalar claims it,
and the alias takes the path meant for this - `lookup_anchor()` misses,
`anchor_open_on_stack()` finds the name on the level being built, and the
binding is deferred until the collection exists. `unregister_anchor()` gives
the entry up with the string, so the block spelling takes that same path.

The entry keeps its name and gives up its node, which is exactly the state a
collection's anchor is in between its opening and its closing. Only an entry
still pointing at that scalar is cleared: a name anchored to something else
earlier in the document keeps that binding, because an alias written in
between has already resolved against it.

**The wrong answer was not always a wrong value.** Used as a key, the alias
resolved to the mapping's own first key - which is, necessarily, a key that
mapping already has - so a document with two distinct keys came back
*Duplicate mapping key*:

```
  &O
  k: v
  *O : x
```

And the anchor reaches the mapping on the way *out* whether or not the alias
does, so one document said two things. With `require_string_keys` the first
parse accepted that input and the second refused it: the alias was a string
going in and a mapping coming back. That is how the writer fuzzer surfaced
it, and it is the shape of finding this target exists for - not a crash, but
a document this library parses, writes, and then will not read.

Fixing it makes such documents genuinely recursive, which is why it was left
open once: it reaches `max_alias_expansion`, `nodes_equal()`, the writer, and
`to_json`, and those wanted measuring together rather than one at a time. All
four hold, and none of them needed changing. `max_alias_expansion` counts the
aliases a document *writes*, not the expansions a reader could take from
them, so one alias is one alias however it points. `nodes_equal()` carries a
depth bound already. The writer emits an alias by name and has nothing to
recurse into, and the anchor it needs is on the node the alias names, so
`&O {k: v, j: *O}` goes out and reads back as the same shape. `to_json`
refuses an alias outright, as it did before. The only thing that had to
change was a test helper: `Render()` prints an alias by printing its target,
which a recursive document turns into an infinite walk, so it now stops at a
node already on the path.

### A folded scalar broken where the fold would not come back

From the writer fuzzer again, and the first of these that changed a
document's *text* rather than its types: a space came back as a newline.

    before "-III)I\n?+-II{  *   s{ \t  a* {["
    after  "-III)I\n?+-II{  *   s{\n\t  a* {["

A folded block scalar reads a single line break as a space (8.1.3), and that
is what lets the writer break a long line at a space and get the space back
on the next parse. The exception is a **more-indented** line, whose preceding
break 6.5 keeps rather than folds — and more-indented means beginning with a
space *or a tab*. `write_folded_line()` refused to break where a **space**
stood on either side of the candidate and said nothing about tabs, so
`"three \tfour"` went out as a line ending `three` and a line beginning with
a tab. The reader kept that break, and the space was gone.

The rule was already written down, and already enforced once: the planner
that decides whether a value has a folded spelling at all refuses one whose
own lines begin or end with white space, and it names tabs correctly. What
was missing is that the same rule has to hold for the lines the writer
*invents*. A constraint checked against incoming data and not against data
the code generates itself is the shape worth remembering here.

It needs a particular shape to show, which is why the corpus had not found
it: the break-point loop keeps the last valid candidate within the line
width, so a tab-adjacent space is only chosen when nothing after it is also a
candidate. `"one two three \tfour five six"` folds correctly;
`"one two three \tfourfivesix"` does not.

A refused document says which fault it hit. The scanner describes
everything it rejects, and that message now travels back with the status
rather than being left behind in the token loop, so an unterminated quoted
scalar and a stray tab no longer arrive as the same three words.

---

Back to \ref format_references "Format and specification references".
