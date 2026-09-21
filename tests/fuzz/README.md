# Fuzzing

Four libFuzzer harnesses:

| Target | Harness | Build | Run |
| --- | --- | --- | --- |
| JSON | `fuzz_json.cpp` | `make fuzz-json` | `make fuzz-run-json` |
| YAML | `fuzz_yaml.cpp` | `make fuzz-yaml` | `make fuzz-run-yaml` |
| YAML writers | `fuzz_yaml_writer.cpp` | `make fuzz-yaml-writer` | `make fuzz-run-yaml-writer` |
| CSV  | `fuzz_csv.cpp`  | `make fuzz-csv`  | `make fuzz-run-csv`  |

`make fuzz` builds and runs all four. Each runs for `FUZZ_TIME` seconds
(default 60); override it for a real campaign:

    make fuzz FUZZ_TIME=3600

Requires `clang`. The library sources are recompiled with clang's coverage
instrumentation and linked into the harness rather than linking the ordinary
shared library — libFuzzer steers its mutations by the coverage it observes,
and against an uninstrumented library it sees only the harness and degrades
into random input generation. AddressSanitizer and UndefinedBehaviorSanitizer
are enabled, since a parser reading one byte past a buffer is exactly the bug
being looked for and will not usually crash on its own.

## Fuzzing a writer

Three of these read. `fuzz_yaml_writer.cpp` writes, and it exists because for
a long time nothing did: the YAML harness parses and walks and never calls a
writer, so the most productive tool applied to that module had never been
pointed at half of it.

The property is the one every known writer defect broke:

> If the writer says OK, the bytes it wrote must parse, and must hold the
> same values.

A refusal is always a permitted answer — not every value has a YAML spelling,
and saying so is correct. What is never permitted is claiming success and
producing something this library cannot read back.

Two families of input reach the writers, and they are not the same:

- **documents that came from parsing**, which is what a round trip over
  yaml-test-suite measures; and
- **documents built through the DOM API**, which is where the interesting
  failures were. A corpus of YAML text can only carry values the parser
  accepts, so a DEL never reaches a writer from the first direction and is
  ordinary from the second. The harness therefore treats its bytes as a small
  program for building nodes — scalars, sequences, mappings, and hostile
  anchors and tags — as well as as a document to parse.

A fifth mode runs the streaming parser straight into the streaming writer
with no DOM in between. That is the path a caller of the event API uses, and
it is the only one that carries a `%YAML` or `%TAG` directive, an unresolved
tag spelling, or a comment as far as the writer.

Two of its finds were bugs in the harness rather than in the library, and both
are worth recording, because each marks a place where the property as first
written was stronger than the truth:

- the event API resolves nothing, so `*c%` — an alias to an anchor nobody
  declared — is an ordinary event stream, and the writer is right to hand back
  something equally unresolvable. The property only holds where the input was
  a document.
- a DOM may hold two equal keys; nothing in the API prevents it. Writing that
  document faithfully is the writer doing its job, and refusing to read it
  again is the *reader's* duplicate-key policy doing its own. `{: , : }` was
  the fuzzer's way of asking. The output is read back with
  `GTEXT_YAML_DUPKEY_KEEP_ALL`, because the question is whether this library
  can read what it wrote, not whether a policy likes it.

The rest were real, and every one is in `corpus/yaml-writer/`:

- an anchor named `!`, which the *parser* refused although `ns-anchor-char`
  admits it — nine printable ASCII characters could not begin a name;
- a tag of `!&!`, which the writer spells `!<!&!>` and the parser then read as
  a shorthand naming a handle nobody had declared, because a verbatim tag
  stopped being one as soon as its brackets came off;
- a scalar of bytes that are not UTF-8, which has no spelling at all — every
  escape of 5.7 names a code point, so `\xFF` would read back as U+00FF,
  two bytes and a different value;
- an empty tag string on a node inside a sequence, which is how it reached
  `[""]` — and `[""]` is JSON, so it took the JSON fast path, a second
  implementation of the parser that turned every quoted string back into
  whatever its text resolved to;
- `%` followed by a lone 0xC2, which the parser accepted as a document. The
  `c-printable` gate holds a UTF-8 sequence a feed cut in half rather than
  judging it from its first byte, and at the end of the stream it was still
  holding it — deferring to a scalar validator that a directive line never
  reaches. The writer emitted the directive back and the parser refused what
  it had just accepted;
- a tag of `!!-.#`, which the writer spelled straight out. The
  `tag:yaml.org,2002:` namespace is not the author's to extend, so the
  resolver refuses a tag in it naming no type the spec defines — and the
  writer was producing them. The DOM API validates neither tags nor anchors,
  which is what makes it the right instrument for this;
- a document of one `~`, which the streaming writer wrote as `"~"` — turning
  null into a one-character string. Only a plain scalar is resolved by its
  contents, so quoting is a change of value and not only of style wherever the
  plain text would have resolved to something else. `+1` had the same fault in
  both writers. Two characters missing from a whitelist - and one too many in
  it, since the same list admitted `-` unconditionally: the string `"-"` went
  out plain and came back as a sequence holding one empty node, because
  `ns-plain-first` admits `-` only when something plain-safe follows it;
- a built scalar of `1`, which is the same question from the other end. The
  DOM constructor made a *string* of whatever text it was given and the writer
  wrote that text plain, so the string `"1"` came back as the integer `1` and
  no route through the API could write it otherwise. The constructor takes the
  type from the text now, and `gtext_yaml_node_new_scalar_typed()` is where a
  caller says otherwise;
- a tag of `!-`, which the writer spells `!- ` with a space after it — and the
  parser read that as the non-specific tag `!` followed by a block entry. A
  shorthand tag's name is `ns-tag-char+`, and `-` is one. `!-[]` had always
  parsed, because the `[` ends the name before anything can misread it; only
  the space the writer adds exposed it. `!#` followed once it was fixed — `#`
  is `ns-uri-char` too, and starts no comment where no white space precedes
  it.

Three more came out from behind the DOM constructor, one at a time as
each was fixed: a tag did not stop the text from deciding the type, a type
was set without the value that goes with it, and an empty tag string counted
as a tag everywhere except where it was written — which made an entry
holding one vanish from its sequence.

The JSON fast path is the one worth the longest look. It was not a writer bug
at all; the writer was the instrument. Nothing else in the project could have
found it, because `make conformance` asks for `KEEP_ALL` duplicate keys and
that is the one setting which turns the fast path off.

The artifacts are two to six bytes each and say nothing on their own, so the
harness prints what the writer produced and both JSON renderings before it
traps. That is the difference between an artifact worth keeping and one worth
deleting.

The next run went after the parser instead. A flow collection could not be a
block mapping's key on any line but the first (`a: 1` over `{}: 2`), and three
separate places turned out to be measuring the last *scalar* to find out where
that key stood - which says nothing about a key that is a flow collection,
since `{}` has no scalar at all. Two were in the parser and one in the
scanner, and each was only visible once the one in front of it was gone. A
fourth sat behind those: `a:` over `{}: 1` was not refused at all, it was
accepted with `{}` nested inside `a`, because the rule that a node at the
key's own column is the *next key* had never been given to the four places a
completed collection is added to its parent. Giving it to them without asking
which style the collection is costs eight documents of yaml-test-suite, all
zero-indented sequences - `key:` over `- a` is a block sequence standing at
its own key's column and still being the value.

Then a built scalar of `"\n3"`, which `gtext_yaml_node_type()` called the
integer 3 because `strtoll()` skips leading white space. The writer quoted it,
correctly - no plain spelling of that text exists - and the reader read back
the string. A plain scalar has white space at neither end (7.3.3), so text
that carries any can only be quoted, and quoted is string.

The run after that went five deep, one fix uncovering the next, and only the
first was a writer defect:

- the string `---` written plain, which is `c-directives-end` (9.1.2) and is
  kept out of a document's content by `c-forbidden` (9.1.1). The writer
  produced a document marker, called it OK, and the reader agreed with the
  bytes and gave back an empty document. `...` was the same;
- a block mapping's *first* entry could not have an empty key. `a:` over
  `  : 1` was refused, while the same entry later in the same mapping worked:
  a later entry joins a mapping already open, the first has to open one, and
  the branch that opens one ran only where no block mapping was open at all;
- a shorthand tag written with a handle no `%TAG` had declared. The DOM writer
  emits no directives, so a named handle is undeclared there by construction -
  and `!a!3` went out as itself. Refused now, since `!<!a!3>` is a different
  tag and an invented prefix would be worse; the streaming writer tracks the
  handles its own `%TAG` directives declare, and forgets them at each document
  end;
- two uninitialised pointers read off the stack by the DOM writer, because its
  two entry points set every field of their state by hand and a field had just
  been added. Both `memset` first now;
- a property in front of a key not being part of the key. `&a {}` begins at
  the `&`, and a block mapping is indented where its key is, so `&a {}: 1`
  over `b: 2` put the second entry outside the mapping the first had opened.
  In the scanner that blind spot was not new to flow keys at all: `outer:`
  over `  &a x: 1` over `   c` was refused where the same lines without the
  `&a` fold into `1 c`.

Two more after those. A built scalar of `+` and `1` with a break between them
was the integer 1, because `strtoll()` skips leading white space and this code
consumes the sign itself before handing `strtoll` what follows - and that half
was reachable from *text* as well, so `+` over a blank line over `1` parsed as
1 where both references read the string. Not one row of the 10.3.2 table
contains white space anywhere, which is the whole rule and is now written as
one. And the property-in-front-of-a-key defect had a third site: the rule that
a flow collection at the key's own column is the next entry's key measured the
collection rather than the entry, so `a:` over `&k [x]:` put the sequence
where a's value goes.

Three more after those: an empty `!!binary` value refused where base64 of no
bytes is the empty string and the writer writes exactly that; `!!binary` taken
as an assertion nobody checked, so a node built from good base64 answered
false to `gtext_yaml_node_as_binary()` and one built from `(((` was written
after the tag; and a document written with **two root nodes**, because
`*a: x` is an alias named `a:` followed by a second node at document level and
the writer put the second straight after the first, producing `*a:x`.

That last one came through the event-pipe path, which had been trapping **in
silence** - only `must_round_trip()` printed what the writer produced. Two of
this harness's own defects were already listed above; this is the third, and
it is the one that cost the most, because every find there began with reverse
engineering a dozen bytes. It prints now.

Two more again: a directive written straight after a document's content,
where 9.2 wants a `...` first - loud for `%TAG`, and silent for `%YAML` after
a plain scalar, which folded the directive into the scalar and turned `a` into
`a %YAML 1.2`. And a quoted `"<<"` taken for a merge key, where both
references read the two-character string: `{"<<": 1}` was refused and
`{"<<": {a: 1}}` was *merged*, the key vanishing into the mapping around it.
Only a plain scalar is resolved by its contents (10.3.2), for the fourth time
in this file.

Then three more, two of which were regressions of the fixes above. A "-",
"?" or ":" is an indicator only where white space follows it - "- x" is an
entry holding x, "-: 1" a mapping whose key is the plain scalar "-" - and both
the scanner's `line_node_col` and the parser's "does this begin its own line?"
test had been skipping them without asking. And base64, which is exempt from
the writer's quoting whitelist because "/" and "=" are ordinary in it, was
exempt from the plain style's own limits too: a binary scalar written over two
lines came back with its break folded into a space. The bytes were the same,
which is why nothing measuring values noticed; the text was not.

And a kept trailing break doubled by the document separator: a block scalar
ends its own last line, `+` chomping keeps that break as part of the value,
and the break written before the next `---` landed on top of it. Only on `+`,
and only with a second document after it - two conditions at once, neither of
them rare on its own.

Then the same white-space hole through the two characters YAML does not have:
the vertical tab and the form feed are not c-printable, so no parsed scalar
holds one, but the DOM API takes any `char *` and `strtod()` skips them like a
space. `"\v6662."` was the float 6662.

And `!!binary` turned out to be only the first tag the constructor was not
checking: `!!int ""` was built without complaint and written as `!!int ""`,
which this parser refuses. Chasing the six shapes that did that found two
where the *parser* was the one in the wrong - `!!float 12` refused, where
10.3.2's float row makes its fraction optional and both references give 12.0,
and `!!null x` accepted, where the null row is five spellings and nothing
else.

And a nesting limit that reached only half the grammar. `max_depth` was
counted at the `[` and `{` of a flow collection, in the stream layer; block
structure is composed by the DOM parser and never touched it, so `- ` five
thousand times parsed to a DOM five thousand deep with a limit of 256 in
force. The writer is what exposed it - the flow-style writer turns block
nesting into flow nesting, which *is* counted - and that is a trick worth
remembering: running a document through a writer changes its spelling cheaply,
and a limit that only some spellings reach is not a limit.

And then a second limit nobody set: the scanner tracked flow context in a
fixed 32-entry array, and when it ran out the push was *dropped* while the
matching pop still counted down - so past 32 nested flow collections it
believed it was back in block context with the brackets still open. Plain
nesting survives that, because one dropped push and one clamped pop cancel;
the shape the fuzzer built does not, and came back as "Unterminated flow
collection" on a document whose brackets balance. The array grows now, and
`max_depth` is the only limit.

And then the one that is still open, which is also the biggest. An event's
offset indexes the *decoded* character stream; the parser's positional helpers
index the raw input the caller handed in. For UTF-8 those are the same bytes.
For UTF-16 they are not, and a block mapping with two entries does not parse
at all - `a: 1` over `b: 2` is refused. One of those helpers scans backwards
and had no bound check, so the mismatch was a **heap-buffer-overflow**, which
is what ASan caught here. The clamp is in and its bytes are the first seed in
`corpus/yaml-writer/` worth tracking, because it no longer traps; the offsets
are on the YAML format page under *Known defects*, where the fix is a design
decision rather than a patch.

**This target is not yet quiet, and the notes above say so rather than
pretending otherwise.** Every run of it so far has found something, each fix
exposing the next - which is what a new harness does on a surface nothing had
fuzzed before, and is the strongest available argument that the surface needed
one. The best run so far went twenty-three thousand executions in twenty
minutes before it found the property-column defect above. The corpus under
`corpus/yaml-writer/` is the record; a run that goes the full `FUZZ_TIME`
without a find will be the first, and this paragraph should be updated when it
happens.

## The options byte

Each harness consumes the first input byte (two, for CSV) as a selector for
the parse options — for the writer harness, for which of its five paths to
run — and treats the rest as the document. The dialects these
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
- **Undefined behavior on empty quoted scalars.** `a: ""` passed a NULL
  pointer to `memcpy`, and asked `malloc(0)` for the buffer, whose NULL
  return would have been reported as an allocation failure.
- **Leaked token buffers.** Several error paths in the stream layer abandoned
  a token that owned a heap buffer.

Then, once token ownership was settled (below) and the fuzzer could reach
deeper, a second infinite loop in the YAML scanner: the block scalar *body*
committed its consumption with `while (cursor < pos2)`, and `scanner_consume()`
cannot advance past the end of the input, so a `pos2` beyond it spun forever.
Same shape as the header loop, one function further down.

## Token ownership

The leaked token buffers were not five independent mistakes. `yaml_internal.h`
documented the scalar payload as "owned by scanner until next token", while
`scanner.c` allocated a fresh buffer per token and `stream.c` freed it — so
every error path that abandoned a token leaked, and the fuzzer found them one
at a time.

The scanner owns the payload now, exactly as the header always said: it is
released when the next token is requested, and on scanner teardown. All
eighteen frees in `stream.c` are gone, consumers borrow rather than own, and
the two that keep the text for longer (the DOM builder and the pull reader)
already copied it.

After the change the YAML fuzzer ran 2.4 million executions clean, with
coverage up from 4,764 to 7,736 — the leak reports had been masking how much
of the parser it could not get to.

## Current state

| Target | Executions | Result |
| --- | ---: | --- |
| JSON | 4.6M | clean |
| YAML | 1.2M | clean |
| YAML writers | see below | still finding things; see below |
| CSV  | 6.5k | clean |

The writer harness is new, and its execution count is not yet comparable: it
builds a document and re-parses one on every run, so it is much slower per
execution than a parse-only harness. The four writer defects it was written
for had already been found by hand; it exists so the next four are not, and it
has already earned that — forty-three library defects and three of its own,
listed above. Most of the twenty are in the *reader*, which is not what this harness
was built to test: a writer is an instrument for asking a parser questions a
corpus of inputs cannot phrase, and it turns out to ask a lot of them.

CSV is much slower per execution because the harness reads back every field of
every parsed table; that is deliberate, since indexing is where a row/column
mismatch would show.
