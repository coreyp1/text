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

**Tests.** 92 test files under `tests/yaml/`, carrying 582 of the suite's
2172 test cases across 106 binaries, all passing. They cover the scalar styles,
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
  the standard namespace, and the verbatim `!<uri>` otherwise, with anything
  outside `ns-uri-char` percent-encoded.
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
a block scalar lives in one place and both writers call it.

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

A refused document says which fault it hit. The scanner describes
everything it rejects, and that message now travels back with the status
rather than being left behind in the token loop, so an unterminated quoted
scalar and a stray tab no longer arrive as the same three words.

---

Back to \ref format_references "Format and specification references".
