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
- **JSON5:** [json5.org](https://json5.org/), version 1.0.0, whose numeric
  and string grammars are ECMAScript's. Its extensions are opt-in and named
  individually below, one option per difference rather than one dialect flag.
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

**JSON5 numeric literals**, each off by default and each a separate question:

- `allow_hex_numbers` - `0x1F`, `0XdeadBEEF`, `-0x10`. A hex literal has no
  fraction and no exponent, because `e` is one of its digits: `0x1e2` is 482,
  not 100. The value goes into `int64`/`uint64` and the `double` is derived
  from it; the preserved lexeme keeps the spelling that was written.
- `allow_leading_plus` - `+1`, `+1.5e2`. The integer accessors see through the
  sign, so `+42` still has an `int64`.
- `allow_bare_decimal_point` - `.5` and `5.`, one option because they are one
  question: whether the point may sit at an edge. `.` alone is still not a
  number, and neither is `.e1`.

JSON5 puts the sign in front of the whole value, so `+Infinity` and `+NaN` are
valid JSON5 and need `allow_leading_plus` *and* `allow_nonfinite_numbers`.
`-NaN` needs only the latter, for the same reason `-Infinity` always has.
NaN's sign is not observable, so both signed spellings give NaN.

**JSON5 string escapes**, also off by default and also two separate questions:

- `allow_ecma_escapes` - `\xHH`, `\v`, `\0`, and ECMAScript's rule that any
  other character after a backslash is that character (`\a` is `a`, `\'` is an
  apostrophe). `\xHH` names a *codepoint*, so `\xe9` decodes to the two UTF-8
  bytes of U+00E9 rather than to the byte 0xE9, which would not be UTF-8 at
  all. `\0` is U+0000 and is an error where a digit follows it; `\1` through
  `\9` are always errors, because those were octal escapes in a language that
  no longer has them.
- `allow_line_continuations` - a backslash before a line terminator contributes
  nothing, which is how JSON5 writes a string over several lines. All five
  terminator sequences: LF, CR, CRLF, U+2028 and U+2029. CRLF counts as one, so
  the LF is not left behind to be read as an unescaped control character.

A backslash before a line terminator is a continuation and not an identity
escape, so with `allow_ecma_escapes` alone it is an error rather than quietly
meaning a newline. A *raw* newline inside a string is still a control character
under both options; the continuation is the backslash's doing.

**JSON5 unquoted object names.** `allow_unquoted_keys`, off by default, makes
`{a: 1}` legal. The name is an ECMAScript `IdentifierName`, which is wider than
`[A-Za-z_]`: any character with the Unicode property ID_Start may begin one, any
with ID_Continue may continue it, `$` and `_` may do either, and `\uXXXX`
escapes are allowed - `{\u0061: 1}` names `a`, and a surrogate pair reaches an
astral character as it does in ECMAScript 5.1. `\u{1F600}`, the ECMAScript 2015
spelling, is not accepted, because the JSON5 specification is written against
5.1. The properties come from the same generated table as the whitespace, so a
character that is merely non-ASCII is not a name: U+1F600 has neither property
and is refused, and U+0301 has ID_Continue only, so it may continue a name but
not start one.

`IdentifierName` includes the reserved words, so `{true: 1}` is an object whose
name is `true`, and so are `{null: 1}`, `{NaN: 1}` and `{Infinity: 1}` - the
last two without `allow_nonfinite_numbers`, which is about values. Those words
are still keywords wherever a value is expected, and `-Infinity` is not a name
at all, because a name cannot begin with a sign.

The name is decoded before it is compared, so `{a:1,"a":2}` and
`{a:1,\u0061:2}` are duplicate names, and with `normalize_unicode` an unquoted
name is normalized exactly as a quoted one is.

**JSON5 whitespace.** `allow_ecma_whitespace`, off by default, widens the space
*between* tokens from JSON's four characters - tab, LF, CR, space - to
ECMAScript's set: vertical tab, form feed, U+FEFF, every character in
General_Category Zs (U+00A0 and U+3000 among them), and the line terminators
U+2028 and U+2029. Zs comes from a generated table checked against the pinned
UCD by `make check-json5-tables`, because that category has moved before:
U+180E was Zs until Unicode 6.3 reclassified it as Cf. A character that only
looks space-like is not whitespace - U+200B ZERO WIDTH SPACE is Cf and stays a
syntax error.

**JSONPath (RFC 9535), without the filter selector.**
`gtext_json_path_compile()` and `gtext_json_path_select()`, with
`gtext_json_path_query()` for a one-shot, and `_select_paths()` / `_query_paths()`
where the *normalized path* of each result is wanted as well as the node. The root identifier, child and
descendant segments, and the name, wildcard, index and slice selectors,
including several selectors in one bracket. A result is a node list in the order
the specification gives, and it may hold the same node twice - `$[0,0]` selects
the first element twice, and §2.3.1.2 says so.

The **filter selector** is implemented: `$[?@.price < 10]`, with `&&`, `||`,
`!`, parentheses, the six comparison operators, and the functions `length()`,
`count()` and `value()`. A filter may hold another filter, and a comparison may
name the document root - `$.a[?@.b == $.x]` - as well as the current node.

`match()` and `search()` are the exception: they need an I-Regexp engine, which
this library does not have, so a query using either is refused with
`GTEXT_JSON_E_PATH_UNSUPPORTED`. That status is separate from
`GTEXT_JSON_E_PATH` for a query that is not well-formed, because the two ask the
caller for different things - and a query whose filter was quietly dropped would
select *every* element of the array rather than the ones asked for, which is why
refusing is the only safe answer to a construct that cannot be evaluated.

An **ill-typed** query is invalid rather than false, as §2.4.2 says: `length()`
takes a value so its argument cannot be a multi-node query, `count()` and
`value()` take a node list so their arguments cannot be literals, only a
singular query may be compared, and a value is not a test expression. Each of
those is `GTEXT_JSON_E_PATH`.

The examples in RFC 9535 §1.5 and the slice examples in §2.3.4 are in the suite,
in `tests/test-json-path.cpp`, written from the RFC rather than from this
implementation. `make conformance-jsonpath` scores it against the
[JSONPath Compliance Test Suite](https://github.com/jsonpath-standard/jsonpath-compliance-test-suite):
**650 of the 650 cases it attempts**, out of the 706 the suite ships. The other
56 use `match()` or `search()` and are refused as unsupported rather than
counted as passes or failures - a percentage over a subset means nothing without
that number beside it.

That score covers **both** halves of what the suite asserts: the node list -
which nodes a query selects and in what order - and the *normalized path* of each
result (§2.7), which `gtext_json_path_query_paths()` produces.
`$['store']['book'][0]['author']` is the only spelling §2.7 blesses: brackets
throughout, single-quoted names, no negative indices. Comparing only the values
would pass a query that selected the right nodes by the wrong route, and a
planted off-by-one in the index builder takes the score from 650 to 364.

That runner has found three defects so far, each of which the hand-written tests
agreed with: `$ ` is not a well-formed query, because `segments = *(S segment)`
puts the blank space *before* a segment; blank space *is* allowed before each
segment of a query inside a filter, so `length(@ .a .b)` is one query with two
segments; and `<=` is defined as "less than or equal" rather than as an ordering
of its own, which is why `null <= null` is true even though null is unordered.

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

**`max_depth` is a stack budget, not only a resource limit.**
`gtext_json_parse()` is recursive descent, so a document's nesting depth is the
parser's stack depth - measured at about **448 bytes per level**, calibrated by
bisecting the depth at which the parse dies at four stack sizes (445, 447, 448
and 447 bytes per level at 1, 2, 4 and 8 MiB). The default 256 needs some 115 KB
and is safe anywhere; the ceiling is about **2,300 levels on a 1 MiB thread
stack** and **18,000 on Linux's 8 MiB main stack**, and past it the parse does
not return an error - the process dies. So raising this limit for untrusted
input is choosing a number rather than removing one.

`gtext_json_stream_feed()` has no such bound: the streaming parser keeps its
nesting stack on the heap, and a 50,000-level document goes through it with
`max_depth` raised. For deep or untrusted input it is the parser to use. This is
the one place the two parsers accept different documents, and the difference is
in the safe direction.

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
changing it would break callers. (With `allow_bare_decimal_point`, `5.` is a
number and the row does not apply to it; `1e` is still incomplete.)

**Fixed: a comment split across two feeds was read as code.** The streaming
lexer skipped a `//` comment to the end of the buffer and reported that it had
skipped a comment, whether or not the newline had arrived - so the rest of the
comment, in the next chunk, was lexed as part of the document. With
`{ // "ghost": 99` and `"real": 1 }` in separate feeds that is not an error but
a wrong parse: the name `ghost` appears in the events. A `/* */` comment cut in
the same place was reported as unclosed instead of unfinished, and a lone `/` at
the end of a feed was an unknown token. All three now say "not yet" and keep
the bytes: `JsonStreamComments` feeds nine documents at every chunk size from
one byte up and compares the events against the same document in one feed. A
genuinely unclosed comment is still an error at `finish()`.

**Fixed: white space after the document could not arrive in its own feed.**
JSON allows white space after a top-level value and `gtext_json_parse()`
accepts it, but `gtext_json_stream_feed()` refused any feed once the document
was complete - so `"a"\n` was valid delivered in one feed and
`GTEXT_JSON_E_STATE` delivered in two, and a caller reading a file in
fixed-size blocks could not control which it got. Feeding in that state is now
allowed; content rather than white space is still
`GTEXT_JSON_E_TRAILING_GARBAGE`, whichever feed it arrives in, and feeding
after `gtext_json_stream_finish()` is still `GTEXT_JSON_E_STATE`. Found by the
JSON fuzzer's new DOM-against-stream differential, on a seed corpus entry, the
first time that property was asserted.

**Fixed: a signed `Infinity` or `NaN` could not be streamed.** `-Infinity`
reaches the lexer through the number path, because a sign starts a number, and
that path buffers a token that has not finished arriving. Two defects in the
buffering meant the value parsed in one feed and one byte at a time and almost
nowhere in between: the first feed to hold a sign *and* a letter appended those
bytes to the buffer twice, and the count of how much of the token had been seen
added this chunk's bytes to a buffer that already held them - which pushed it
past the nine characters of `-Infinity`, so the check for an unfinished word was
skipped and a prefix went to the number parser as though it were the whole
thing. Both predate the JSON5 work; the same failures reproduce on the commit
before it.

**Fixed: a tokenization error was reported as success.** The streaming
parser's error path passed the *lexer initialisation's* status to the error
reporter rather than the failing token's, and that status is
`GTEXT_JSON_OK` by then. So a feed that failed returned OK with an error struct
whose code said OK beside the message "Tokenization error", and the next feed
returned `GTEXT_JSON_E_STATE` - one call too late to say what was wrong.

**Fixed: a stream holding no value was accepted.** `gtext_json_parse()`
refuses an input that is only white space, or only a comment, because a JSON
text is a value. `gtext_json_stream_finish()` accepted both, emitting no events
and returning OK, because it treated "bytes arrived" as evidence that a value
had - so a caller could not tell an empty configuration file from a valid one.

**Fixed: a byte-order mark had to arrive whole, and was skipped in the middle
of a document.** `allow_leading_bom` skips a BOM at the start of the input.
In the streaming parser a truncated multi-byte sequence between tokens was read
as a bad token rather than an unfinished one, so `<BOM>1` fed a byte at a time
was refused; and because the parser re-initialises its lexer on each feed with a
compacted buffer, the mark was skipped wherever that buffer began - `[1,<BOM>2]`
was refused by `gtext_json_parse()` and accepted by the stream at a chunk size
of 3. The BOM is now skipped only where the input really begins. U+FEFF inside a
string was never affected: it is an ordinary character there.

**The streaming parser does not enforce the duplicate-name policy.** `dupkeys`
defaults to `GTEXT_JSON_DUPKEY_ERROR` and `gtext_json_parse()` honors it, but
`gtext_json_stream_feed()` emits both names and reports success. So the same
document is refused by one parser and accepted by the other, which is not a
JSON5 matter - it is equally true of two quoted names. It is pinned by a test
(`Json5UnquotedKeys.TheStreamingParserDoesNotSeeDuplicateNames`) so that it
shows up as a known gap rather than as a surprise. Closing it means holding
every name of every open object in memory, bounded by `max_container_elems`
and `max_string_bytes` but real, and that is a cost a streaming parser should
be asked for rather than assumed to want.

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
