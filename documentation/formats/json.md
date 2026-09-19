@page format_json JSON

# JSON

The JSON parser implements RFC 8259 strictly by default, and relaxes toward
the JSONC dialect only when explicitly asked. Everything claimed below was
checked against the built library rather than read off the source, and the
cases that were checked are listed under
[Tested scope](#json-tested-scope). Back to the
\ref format_references "format index".

## Normative references

- **Specification:** [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259),
  *The JavaScript Object Notation (JSON) Data Interchange Format*, December
  2017. Identical in grammar to
  [ECMA-404, 2nd edition](https://ecma-international.org/publications-and-standards/standards/ecma-404/).
- **JSON Pointer:** [RFC 6901](https://www.rfc-editor.org/rfc/rfc6901), April
  2013.
- **JSON Patch:** [RFC 6902](https://www.rfc-editor.org/rfc/rfc6902), April
  2013.
- **JSON Merge Patch:** [RFC 7386](https://www.rfc-editor.org/rfc/rfc7386),
  October 2014.
- **JSON Schema:** still **names no draft**, but now covers most of
  draft-07 and 2020-12 including `$ref`, and refuses any schema it cannot
  fully enforce - see [Deviations](#json-deviations).
- **JSONC:** no specification exists. The extensions are opt-in and named
  individually below; where the dialect is ambiguous this page states what the
  parser does, and that decision is the specification as far as this library
  is concerned.

All parser allocations are owned by the returned `GTEXT_JSON_Value` and
released by `gtext_json_free()`. Error context snippets are separately owned
and released by `gtext_json_error_free()`.

## Parts implemented

**Values (RFC 8259 §3-§6).** All six: `null`, `true`/`false`, number, string,
array, object. A JSON text may be any value, not only an array or object -
RFC 8259 §2 widened this from the older RFC 4627, and the parser follows the
newer rule, so `42` and `"hi"` are complete documents.

**Numbers (§6).** The grammar is enforced exactly: a leading `-` but never a
leading `+`, no leading zeros, at least one digit either side of a `.`, and a
complete exponent. Beyond the grammar, the parser keeps more than a `double`:

- `preserve_number_lexeme` (default on) retains the original text, which is
  what makes exact round-tripping possible.
- `parse_int64` and `parse_uint64` (default on) detect exact integer
  representations.
- `parse_double` (default on) derives a `double` when the value is
  representable.

A number too large or too precise for any of those is still kept exactly, as
its lexeme, and read back with `gtext_json_get_number_lexeme()`. That is the
string-backed arbitrary-precision decimal, and it is on by default; there is
no separate option for it.

An integer too large for `int64_t` or `uint64_t` is **not** an error. It
parses, the lexeme is preserved, and the integer accessors simply do not
apply to it. This is a deliberate choice and is listed under
[Deviations](#json-deviations) because several popular parsers reject or
silently round instead.

**Strings (§7).** All escapes of the specification, `\u` included. Surrogate
pairs must be well formed: a high surrogate must be followed by a low
surrogate. Lone or reversed surrogates are rejected with
`GTEXT_JSON_E_BAD_UNICODE`. Unescaped control characters below U+0020 are
rejected unless `allow_unescaped_controls` is set.

**Encoding (§8.1).** Input must be UTF-8 and is validated by default
(`validate_utf8`), through the lexer. A leading UTF-8 BOM is accepted and
skipped by default (`allow_leading_bom`); §8.1 forbids emitting one, and the
writer never does.

**Duplicate names (§4).** The specification says names *should* be unique but
does not require it, so every parser has to choose. This one rejects by
default - `GTEXT_JSON_DUPKEY_ERROR` - and offers `FIRST_WINS`, `LAST_WINS`
and `COLLECT`, the last gathering duplicates into an array.

**JSONC extensions**, each off by default: `allow_comments` (`//` and
`/* */`), `allow_trailing_commas`, `allow_single_quotes`,
`allow_nonfinite_numbers` (`NaN`, `Infinity`, `-Infinity`) and
`allow_unescaped_controls`. None of these is RFC 8259; enabling any of them
means the input is no longer JSON.

**Pointer, Patch and Merge Patch.** RFC 6901 evaluation including the `~0`
and `~1` escapes; the six RFC 6902 operations `add`, `remove`, `replace`,
`move`, `copy` and `test`, applied atomically so that a failing operation
leaves the document unchanged; and RFC 7386 recursive merge.

The worked examples in RFC 6901 section 5, RFC 6902 Appendix A and the RFC
7386 Appendix A test table are all in the suite, in
`tests/test-rfc-conformance.cpp`. That is the only conformance corpus this
library has; the JSON syntax itself is still checked only against tests
written here rather than against JSONTestSuite. Writing the three appendix
tables down found three divergences that the existing tests, all written
against the implementation, agreed with: `"/"` resolved to the root rather
than to the member named `""`, `move` applied its `add` before its `remove`
so that moving within one array used unshifted indices, and a merge patch
adding a new object member stored its `null` members instead of dropping
them.

**Schema.** A core subset: `type` (including arrays of types), `properties`,
`required`, `items`, `enum`, `const`, `minimum`, `maximum`, `minLength`,
`maxLength`, `minItems`, `maxItems`, and the applicators listed on the
\ref json_module "JSON module page". `minLength` and `maxLength` count
characters, not bytes - see [Deviations](#json-deviations). Schemas compile
once and validate many instances.

## Limits

Applied unless overridden; `0` in the option means "use the default".

| Option | Default |
|---|---|
| `max_depth` | 256 |
| `max_string_bytes` | 16 MiB |
| `max_container_elems` | 1 Mi elements |
| `max_total_bytes` | 64 MiB |

## Save

The writer emits RFC 8259 by default and nothing else: compact, no trailing
newline, no BOM, keys in insertion order, and the original number lexeme
reproduced byte for byte when it was preserved. That last point is the reason
`canonical_numbers` is off by default - normalizing a lexeme is lossy, and
the safe default for a writer is to hand back what it was given.

Pretty-printing, indent width, newline string, spacing around `:` and `,`,
and inline thresholds for short arrays and objects are all configurable.
For a stable byte-for-byte output across runs, `sort_object_keys` orders
names, `escape_unicode` forces `\uXXXX` for non-ASCII, and `canonical_numbers`
normalizes numeric lexemes. String escapes are always normalized: the DOM
stores decoded strings, so the writer re-escapes canonically and the input's
original escape spellings are not retained.

`allow_nonfinite_numbers` must be set for the writer to emit `NaN` or
`Infinity`, and doing so produces output that is not JSON. Without it a
non-finite value is an error rather than a silent `null`, which is what
several other libraries substitute.

## Compliance checklist

| Area | Supported | Rejected / limitation |
|---|---|---|
| Top-level value | any of the six | empty input, `GTEXT_JSON_E_BAD_TOKEN` |
| Numbers | full §6 grammar | `.5`, `+1` `E_BAD_TOKEN`; `0123` `E_BAD_NUMBER`; `5.`, `1e` `E_INCOMPLETE` |
| Non-finite | opt-in | default `GTEXT_JSON_E_NONFINITE` |
| Integers beyond 64 bits | parsed, lexeme kept | no integer accessor applies |
| Strings | all §7 escapes | lone/reversed surrogate `E_BAD_UNICODE`; raw control `E_BAD_TOKEN` |
| Encoding | UTF-8, validated | invalid sequence `E_BAD_UNICODE` |
| Leading BOM | accepted, skipped | never written |
| Duplicate names | four policies | default `GTEXT_JSON_E_DUPKEY` |
| Trailing content | single value, or explicit consumed-length parse | `GTEXT_JSON_E_TRAILING_GARBAGE` |
| Comments, trailing commas, single quotes | opt-in | default `E_BAD_TOKEN` / `E_TRAILING_GARBAGE` |
| Depth | 256 default | `GTEXT_JSON_E_DEPTH` |
| Size limits | four, configurable | `GTEXT_JSON_E_LIMIT` |

@anchor json-deviations
## Where this parser differs from other parsers

| Case | This parser | Elsewhere |
|---|---|---|
| Duplicate names | error by default | most parsers take the last silently |
| Integer beyond `int64`/`uint64` | accepted, exact lexeme kept | often rounded to `double`, or rejected |
| Non-finite on write | error unless opted in | frequently written as `null` |
| Number round-trip | original lexeme reproduced | usually reformatted from `double` |
| `5.` and `1e` | `E_INCOMPLETE` rather than `E_BAD_NUMBER` | a single malformed-number error |

The last row is a wart rather than a design decision: a truncated number and a
grammatically invalid one are both simply invalid at top level, and reporting
one as "incomplete" invites a caller to wait for more input that will not
help. It is documented here because the status code is part of the API and
changing it would break callers.

**Fixed: the schema engine no longer accepts schemas it cannot enforce.**

`gtext_json_schema_compile()` used to accept a schema containing any keyword
it did not implement and ignore it, on the reasoning that unknown keywords are
ignorable - which JSON Schema does require, but only for keywords that are
genuinely unknown. Applied to standard assertion keywords the effect was that
a schema which looked like it constrained data did not, and validation
returned `GTEXT_JSON_OK` for instances the schema should have rejected. A
caller porting a working draft-07 schema got a validator that approved
everything the unimplemented half was meant to catch, with no error at compile
time and no warning at validation time.

This was the same failure shape as `validate_utf8` before it was wired up: an
interface naming a guarantee it does not provide, with no way for a caller to
notice.

Compiling now fails with `GTEXT_JSON_E_SCHEMA_UNSUPPORTED` when the schema
uses a standard keyword the engine does not enforce, and names the keyword in
`err.context_snippet`, which `gtext_json_error_free()` owns. The refused set
is the applicators - `$ref`, `$recursiveRef`, `$dynamicRef`, `allOf`, `anyOf`,
`oneOf`, `not`, `if`, `then`, `else`, `additionalItems`, `prefixItems`,
`contains`, `minContains`, `maxContains`, `additionalProperties`,
`patternProperties`, `propertyNames`, `dependentSchemas`, `dependentRequired`,
`dependencies`, `unevaluatedItems`, `unevaluatedProperties` - and the
assertions `pattern`, `format`, `multipleOf`, `exclusiveMinimum`,
`exclusiveMaximum`, `uniqueItems`, `minProperties`, `maxProperties`,
`contentEncoding`, `contentMediaType` and `contentSchema`.

The rule is that a keyword is refused when it changes which instances are
valid and the engine does not implement it. Everything else is still ignored,
because ignoring it is both correct and harmless:

| Ignored | Why it cannot mislead |
|---|---|
| `title`, `description`, `default`, `examples`, `$comment`, `readOnly`, `writeOnly`, `deprecated` | annotation only |
| `$schema` | selects a dialect where only one is implemented |
| `$defs`, `definitions` | containers nothing can reach while `$ref` is refused |
| `$id`, `$anchor`, `$vocabulary` | name a base URI nothing resolves against |
| vendor extensions, newer-draft keywords | JSON Schema requires ignoring them |

Most of that set has since been implemented and left the list.
`pattern` and `patternProperties` are the two whose membership is conditional:
they are refused only when the caller supplied no regular-expression provider,
because whether they can be enforced is a property of the caller's
configuration rather than of this library.

The check applies to subschemas as well as the root, since both go through the
same recursive compile.

This narrows what the engine accepts, so a caller who was relying on the old
behavior - knowing the ignored keywords were decorative - can set
`allow_unsupported_keywords` in `GTEXT_JSON_Schema_Options` and compile with
`gtext_json_schema_compile_with_options()`. The option exists so that the
strict default does not have to be argued about; it is not recommended.

`GTEXT_JSON_E_SCHEMA_UNSUPPORTED` was appended to `GTEXT_JSON_Status` rather
than grouped with `GTEXT_JSON_E_SCHEMA`, so no existing constant changed
value.

**Fixed: `minLength` and `maxLength` counted bytes.**

JSON Schema validation section 6.3 defines both over "the number of its
characters as defined by RFC 8259", and an RFC 8259 string is a sequence of
Unicode code points. This engine measured `instance->as.string.len`, which is
a byte count, so every non-ASCII instance was measured wrong - and wrong in
both directions at once. `{"maxLength": 1}` rejected `"é"`, which is one
character in two bytes; `{"minLength": 2}` accepted it.

Code points, not UTF-16 code units. An astral character such as U+1F4A9 is one
character here even though ECMAScript's own `.length` reports two, which is
the shape of the same mistake an implementation written in or ported from
JavaScript tends to make. The published test suite carries exactly that case
for this reason, and it is what caught this: `maxLength.json`'s "two graphemes
is long enough" expects `"💩💩"` - eight bytes, two characters - to satisfy
`maxLength: 2`.

The count is of bytes that are not UTF-8 continuation bytes, which is exact
for well-formed UTF-8 and cannot run past the end of the buffer for anything
else. That matters because the parser only validates UTF-8 when asked to, so
a caller who turned that off can reach the validator with bytes that decode to
nothing; an approximate count on input that is already invalid is the right
failure, and walking off the end is not.

`pattern` still receives *bytes*, because that is what the provider vtable
promises it. The two lengths now live in separate variables; sharing one is
how this arm came to measure both in bytes.

@anchor json-tested-scope
## Tested scope

**Fixtures** live in `tests/data/json/`, hand-written and grouped by what
they exercise: `rfc8259/` for the base grammar, `numbers/` for the integer
boundaries and precision, `unicode/` for surrogate handling, `invalid/` for
each rejection, `jsonc/` for the extensions, and `valid/` for structural
cases including large containers.

**Direct behavioral check.** Every row of the compliance checklist above was
produced by parsing the literal input with
`gtext_json_parse_options_default()` and recording the returned status, not
by reading the parser. Where this page names a status code, that code was
observed.

**Fuzzing.** `tests/fuzz/fuzz_json.cpp` under libFuzzer with ASan and UBSan,
seeded from `tests/fuzz/corpus/json/`. The harness spends its first input
byte selecting parse options, so the extension paths are reachable rather
than dead. It has found real bugs - a use-after-free in the object parser's
error path among them; `tests/fuzz/README.md` records what and how.

**Reach of the oracles, and where it ends.** There is **no external JSON
conformance corpus wired up**. The fixtures are this library's own reading of
RFC 8259, so they demonstrate the parser is self-consistent and matches that
reading - not that the reading is right. The obvious gap is
[JSONTestSuite](https://github.com/nst/JSONTestSuite), whose several hundred
`y_`/`n_`/`i_` cases exist precisely to catch the disagreements a hand-written
fixture set will not think of. Until it is run, "RFC 8259 compliant" on this
page means "compliant as far as the cases below reach".

@anchor json-not-implemented
## Not implemented

- **No named JSON Schema draft.** The subset resembles draft-07, but nothing
  in the code or the header says so, and a schema language without a version
  is not citeable. Naming the draft, and listing the keywords omitted from it,
  is a documentation fix; the alternative reading - that this is a
  JSON-Schema-shaped validator of its own - would need saying out loud.
- **`pattern` and `patternProperties` need an engine the caller supplies.**
  Both are implemented, against a regular-expression provider passed in
  `GTEXT_JSON_Schema_Options` - three function pointers and a context pointer.
  With a provider they are compiled at schema-compile time and enforced at
  validation time; without one they are refused the way any unenforceable
  keyword is.

  The vtable exists so that this library does not acquire a
  regular-expression dependency that every caller pays for, including the many
  who never write a `pattern`, and so that the one caller who does write one
  gets the dialect their schema means. That dialect is ECMA-262 with the `u`
  flag, the match is a *search* rather than an anchored match, and both
  strings are UTF-8 with lengths given - the three obligations the header
  states, and the three places a validator quietly gets this wrong.
  `search_fn` has a third answer besides yes and no: a search that could not
  finish becomes `GTEXT_JSON_E_LIMIT`, because a pattern that spent its budget
  has not said the instance is invalid, and recording that as "no match" turns
  a denial-of-service defence into a wrong validation result.
- **Schema keywords absent:** `unevaluatedItems` and `unevaluatedProperties`,
  which need annotation results collected across applicators;
  `$recursiveRef` and `$dynamicRef`; `format`; and the `content*` family. A
  schema using any of them is refused rather than silently under-enforced -
  see [Deviations](#json-deviations).

  Everything else is implemented, `$ref` included. It was the significant
  gap, because without it a schema can be neither factored nor recursive.
  Same-document JSON Pointer references resolve (`#` and `#/...`); an
  external URI, a named anchor and a pointer that resolves to nothing are all
  refused at compile time, since a reference that does not resolve constrains
  nothing.
- **No JSONPath**, listed as future work on the
  \ref json_module "JSON module page".
- **`normalize_unicode` is not implemented, and now says so.** NFC
  normalization is not performed. The option used to be accepted and ignored,
  so a caller who asked for normalization got unnormalized text with no way to
  tell; setting it now fails the parse with `GTEXT_JSON_E_INVALID`, and
  `gtext_json_stream_new()` returns NULL. The field is kept so that
  implementing NFC later is not an API change.
- **No JSONTestSuite integration**, as above.
- **Error messages are coarse.** Several distinct lexer failures report the
  string `"Lexer error"`. The status code distinguishes them; the message does
  not, and the message is what reaches a user.

---

Back to \ref format_references "Format and specification references".
