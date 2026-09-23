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
(`validate_utf8`), through the lexer. Validation is by value and not only by
shape: an overlong encoding, a surrogate half, and anything past U+10FFFF are
all refused, as RFC 3629 §3 requires. They were not, until JSONTestSuite was
scored for the first time - `C0 AF`, an overlong `/` and the classic way past
a filter that matches on the character rather than the bytes, parsed as a
string. A leading UTF-8 BOM is accepted and skipped by default
(`allow_leading_bom`); §8.1 forbids emitting one, and the writer never does.

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
`tests/test-rfc-conformance.cpp`. Writing the three appendix
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

**Reach of the oracles.** `make conformance-json` clones
[JSONTestSuite](https://github.com/nst/JSONTestSuite) and scores this parser
against its `test_parsing` cases, which exist precisely to catch the
disagreements a hand-written fixture set will not think of. Of the 283 that
are decidable - 95 a parser must accept, 188 it must refuse - **281 pass,
99.3%**, and the two that do not are the duplicate-name policy rather than the
grammar: RFC 8259 says names SHOULD be unique and leaves the behaviour
unspecified when they are not, and this parser refuses them by default. With
`dupkeys = GTEXT_JSON_DUPKEY_LAST_WINS` the score is **283 of 283**.

Not one of the 188 must-refuse cases is accepted, which is the direction that
matters for a parser reading input it did not write.

The remaining 35 cases are marked `i_`, meaning the suite leaves the answer to
the implementation - very deep nesting, lone surrogates, huge exponents. This
parser accepts 14 of them. They are reported rather than scored.

**A number parsed with no options held nothing at all.** Every entry point's
documentation says the options argument may be NULL for defaults, and
`json_parse_internal()` passed that NULL straight through.
`json_parse_number()` reads its options as `if (opts && opts->parse_int64)` and
`if (opts && opts->preserve_number_lexeme)`, so with no options every number
came back with no preserved lexeme, no `int64` and no `double`. The value
reported type `NUMBER` and held nothing: `gtext_json_get_i64()`,
`_get_u64()`, `_get_double()` and `_get_number_lexeme()` all answered
`GTEXT_JSON_E_INVALID`, and `gtext_json_write_value()` then failed with
`GTEXT_JSON_E_WRITE` - so **any parsed document containing a number could not be
serialized**.

Passing `gtext_json_parse_options_default()` explicitly worked, which is why
nothing saw it: every test passes options. The one test that writes numbers
builds them with `gtext_json_new_number_i64()` rather than parsing them, and
does not check the status either. The defaults are substituted where the options
enter now, and the test asks the same questions with NULL and with an explicit
default and requires the same answers - the pair being the point, since either
alone would pass against a parser that ignored its options entirely.

**The streaming parser is now compared against the DOM parser.** It was not,
and they had drifted. `make conformance-json` scores the DOM parser, and the 47
streaming tests each fed input chosen to exercise the feature under test, so
nothing asked the two parsers the same question. Six disagreements had
accumulated, four of them the streaming parser *accepting* input the DOM parser
refuses:

| Input | Streaming parser | DOM parser |
|---|---|---|
| `{}`, `{ }`, `{"a":{}}`, `[{}]` | refused | accepted |
| `{"a":}` | accepted | refused |
| `[1,]` | accepted whatever `allow_trailing_commas` said | refused |
| `[1 2]` fed a byte at a time | accepted | refused |
| `[1,2,3],` in one feed | accepted | refused |

`JsonStreamDom.TheTwoParsersAgreeOnWhatJsonIs` asks both parsers about 46
inputs, each marked with what RFC 8259 says, and asks the streaming parser twice
- in one feed and a byte at a time, since a chunk boundary is its own way to
disagree. It is the instrument rather than six separate cases, so the next drift
shows up as a disagreement instead of waiting for someone to think of it.

**The schema engine has an oracle of its own**, which this page previously did
not mention at all. `make conformance-json-schema` clones
[JSON-Schema-Test-Suite](https://github.com/json-schema-org/JSON-Schema-Test-Suite)
at the commit in `tools/conformance/JSON_SCHEMA_COMMIT` and runs the
`draft2020-12` directory:

| Set | Files | Assertions | Answered correctly | Schemas refused |
|---|---|---|---|---|
| `required` | 46 | 1,301 | **1,301 (100.0%)** | 0 |
| `optional` | 13 | 162 | **162 (100.0%)** | 0 |
| `optional/format`, asserting | 21 | 866 | **866 (100.0%)** | 0 |

The "schemas refused" column is the one to read first, and is why the
percentage is worth anything: this engine refuses a schema it cannot fully
enforce, so a keyword it had not implemented would show up there as a case
never run rather than as a wrong answer. Zero refused and zero wrong is the
only combination that means what the percentage appears to mean.

The denominator was checked against the corpus rather than taken from the
runner - the 46 files hold 1,301 assertions between them, which is the number
answered - because a harness that silently skips a file reads exactly like one
that passes it. `pattern`, `patternProperties` and `format` need a
regular-expression provider, and the run supplies `ghoti.io-regex`; without
one those keywords are refused rather than ignored, and the suite is scored
with them present because that is the configuration in which the engine is
complete.

@anchor json-not-implemented
## Not implemented

- **The JSON Schema draft is named, and the engine is no longer a subset of
  it.** This entry used to say there was no named draft and that the subset
  "resembles draft-07". Both halves are out of date. The dialect is 2020-12 by
  default; 2019-09, draft-07 and draft-06 are each read with their own keyword
  set, scoped to the resource that declares `$schema`; draft-04 and earlier are
  refused rather than misread, because they spell `exclusiveMinimum` and `$id`
  differently and reading one as a later draft gives a wrong answer about the
  instance instead of an unknown keyword. The nine published 2020-12
  meta-schemas are embedded, so "this instance is a valid schema" resolves
  without a resolver and without a socket.
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
- **Schema keywords absent: three, and each is a missing dependency rather
  than a missing implementation.** This entry used to list
  `unevaluatedItems`, `unevaluatedProperties`, `$recursiveRef`,
  `$dynamicRef`, `format` and the `content*` family. All of those are
  implemented now, and between them they account for 395 of the 1,301
  assertions the `required` suite answers. What is left:

  - `pattern` and `patternProperties` with no regular-expression provider.
  - `regex` as a `format` value, with no provider. It is the only name in the
    format vocabulary this library declines; every other one is checked.
  - `$recursiveRef` with any value but `"#"`. 2019-09 defines exactly one, and
    the keyword is otherwise implemented - `$recursiveRef` and
    `$recursiveAnchor` are 2019-09's spelling of `$dynamicRef` and
    `$dynamicAnchor`, answered by the same dynamic-scope walk.

  A schema using one of these is refused rather than silently
  under-enforced - see [Deviations](#json-deviations). The reference model is
  the specification's and built on URIs: `$id` establishes a base and an
  embedded resource, `$anchor` names a location in one, and `$ref` resolves
  `#`, `#/...`, `#name`, a relative URI and an absolute one alike. Recursive
  references work and targets are compiled once and shared. A reference that
  leaves the document goes through
  `GTEXT_JSON_Schema_Options::resolver`, and is refused at compile time when
  there is none - a reference that does not resolve constrains nothing.
- **No JSONPath**, listed as future work on the
  \ref json_module "JSON module page".
- ~~**`normalize_unicode` is not implemented.**~~ **Implemented.** It used to
  be accepted and ignored, then refused; it normalizes now. The normalizer is
  `src/idna/nfc_utf8.c` over the NFC written for IDNA, which
  `make check-nfc-oracle` compares against Python's `unicodedata` across every
  assigned sequence.

  It applies in the lexer, at the single point where a JSON string becomes
  bytes, so object names are normalized as well as values - which is what makes
  it meaningful, because the parser then compares names that have already been
  normalized. `{"\u00e9":1,"e\u0301":2}` is one name written twice, and with
  the option on the default duplicate policy refuses it.

  Two interactions are deliberate and both are pinned by a test:

  - **It requires `validate_utf8`**, which is on by default. Normalizing bytes
    that have not been established as text is not a defined operation, so the
    pair is refused rather than half-answered.
  - **It disables `in_situ_mode` for strings.** In-situ points the DOM at the
    caller's buffer when the decoded string has the same length as the input,
    and that is not evidence the bytes are the same: canonical ordering sorts
    combining marks by combining class, so `U+4E00 U+0301 U+0327` normalizes to
    `U+4E00 U+0327 U+0301` - seven bytes either way, different bytes. With the
    length test alone in-situ wins and returns the un-normalized input, so the
    option would read as implemented and do nothing. Numbers are still
    referenced in place.
- **Error positions are not always filled in.** Every refusal now carries a
  status and a message, but some carry line 0 and column 0 rather than the
  place the fault was found. The message names the fault; it does not always
  say where.

---

Back to \ref format_references "Format and specification references".
