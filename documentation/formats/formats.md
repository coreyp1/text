@page format_references Format and Specification References

# Format and specification references

One page per format. Each says which external specification the parser
implements, which parts of it, where the implementation is deliberately
stricter or accidentally different from the reference parsers, and how every
claim on the page was checked. They are the authoritative reference for
grammar, option semantics and rejection behavior - ahead of the module pages,
which describe the API, and ahead of the source comments, which point back
here.

The distinction matters when reading: \ref json_module "JSON", \ref csv_module
"CSV" and \ref yaml_module "YAML" under \ref modules "Modules" tell you how to
call the library. The pages here tell you what it will do with a given byte
sequence, and how confident anyone should be about that.

## Formats

| Format | Page | Specification | Read | Write |
|---|---|---|---|---|
| JSON | \ref format_json "JSON" | RFC 8259 / ECMA-404, plus RFC 6901, 6902, 7386 | strict RFC 8259, with opt-in JSONC extensions | compact, pretty, and a canonical mode |
| CSV | \ref format_csv "CSV" | RFC 4180, plus configurable dialects | RFC 4180 and looser dialects; irregular rows | RFC 4180 with configurable quoting |
| YAML | \ref format_yaml "YAML" | YAML 1.2.2, with a 1.1 resolution mode | block and flow, anchors, tags, multi-document | DOM and streaming event serialization |

A cross-format audit against the libraries these are meant to replace is in
\ref format_comparison "Comparison with other libraries".

Formats named in the README's roadmap but with no parser - INI, TOML and the
rest - have no page here. A page is written with the parser, not after it.

## What each page contains

Every page answers the same questions in the same order, so that two formats
can be compared by reading the same heading twice. A page omits a section it
has nothing to put under.

- **Normative references** - the specification, its version, and the parts of
  it linked individually where the document is split up.
- **Parts implemented** - the grammar productions, option semantics and
  extensions, each citing the clause it comes from.
- **Save behavior** - what the writer chooses, and why that choice rather
  than another.
- **Compliance checklist** - a table of area, what is supported, and what is
  rejected with which status code.
- **Deviations** - where the parser is stricter than a reference parser, or
  where it differs because it is wrong. Named cases, each pinned by a
  reproduction.
- **Tested scope** - the fixtures, how they were generated, the external
  oracles and the reach of each, the fuzz harnesses, and the gaps.
- **Not implemented** - what is absent, listed so it is visible rather than
  discovered.

Format-independent concerns - the result codes, the allocation and ownership
contract, the limits - are described in the
\ref core_module "Core module page"; the pages here cover only what a given
format does with them.

## A note on confidence

These three parsers are not equally well established, and the pages say so
rather than presenting a uniform face.

JSON has a small, closed grammar and the page's claims are checked case by
case. CSV has no single grammar to conform to, so its page spends most of its
length on what the dialect options actually mean. YAML has the largest
specification of the three by an order of magnitude, no conformance corpus is
wired up, and its page carries the deviations found so far - including the
silent truncation of plain scalars, which a differential comparison against
PyYAML found and which is now fixed.

## Comparison with other libraries

\ref format_comparison "Comparison with other libraries" asks a different
question from the pages above. They ask whether a parser implements its
specification; that page asks what a caller migrating from libyaml, RapidJSON,
libcsv or PyYAML would find missing, and ranks the answers by how many
adoptions each one blocks. Its findings are measured rather than surveyed, and
two of them - the absent `LICENSE` file and the unoptimized default build - are
suite-wide rather than particular to this library.

## Work in progress

\ref format_allocator_todo "Extending the allocator to CSV and YAML" records
what remains of the allocator work, and why it was stopped rather than
half-finished: a parse option that covers an arena but not the structure
around it is heap corruption for anyone who uses it, not an incomplete
feature.

## Adding a format

\ref format_adding "Adding a format" is the checklist and the page template:
what a new parser's documentation has to answer before the parser is
considered done, and the skeleton to copy.
