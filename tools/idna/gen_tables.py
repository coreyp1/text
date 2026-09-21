#!/usr/bin/env python3
"""Generate the IDNA tables from the UCD.

Reads ``third_party/ucd/<version>/`` and writes ``src/idna/tables/``. The
output is committed, because a build must need neither the network nor
Python; this script is run by a person when the pinned UCD version changes,
and by ``make check-idna-tables`` to prove that what is committed is what this
script produces.

What is derived, and why from here rather than from a published table: RFC
5892 section 2 defines the IDNA2008 derived property as an *algorithm* over
UCD properties, and the algorithm is short. Deriving it is therefore closer to
the specification than copying somebody's rendering of it, and it is checked
against an independent implementation by tools/idna/oracle.py.

Four narrow tables come along for the contextual and bidi rules (RFC 5892
appendix A, RFC 5893). They are narrow on purpose - Script restricted to the
six scripts the CONTEXTO rules name, Joining_Type to the four values the ZWNJ
rule reads, Canonical_Combining_Class to Virama alone, Bidi_Class to the
classes the bidi rule distinguishes. This library validates host names; it is
not a Unicode library, and a table that carried more than the rules read would
be inviting it to become one.

Determinism is a requirement, not a nicety: the check target diffs the
regenerated output against the committed files, so every table is emitted from
sorted input in a fixed format with no timestamps and no dict-ordering
dependence.

Usage:
    tools/idna/gen_tables.py [--ucd DIR] [--out DIR] [--version V]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import os
import sys

MAX_CODEPOINT = 0x10FFFF

# The derived property of RFC 5892 section 1. UNASSIGNED is not stored: a
# codepoint absent from every range is unassigned, and unassigned and
# disallowed are the same answer to the only question asked here.
PVALID = 1
CONTEXTJ = 2
CONTEXTO = 3
DISALLOWED = 4

PROPERTY_NAMES = {
    PVALID: "GTEXT_IDNA_PVALID",
    CONTEXTJ: "GTEXT_IDNA_CONTEXTJ",
    CONTEXTO: "GTEXT_IDNA_CONTEXTO",
    DISALLOWED: "GTEXT_IDNA_DISALLOWED",
}

# RFC 5892 section 2.6. The exceptions are written into the RFC rather than
# derived from anything, so they are written here the same way.
EXCEPTIONS = {
    0x00DF: PVALID,   # LATIN SMALL LETTER SHARP S
    0x03C2: PVALID,   # GREEK SMALL LETTER FINAL SIGMA
    0x06FD: PVALID,   # ARABIC SIGN SINDHI AMPERSAND
    0x06FE: PVALID,   # ARABIC SIGN SINDHI POSTPOSITION MEN
    0x0F0B: PVALID,   # TIBETAN MARK INTERSYLLABIC TSHEG
    0x3007: PVALID,   # IDEOGRAPHIC NUMBER ZERO
    0x00B7: CONTEXTO, # MIDDLE DOT
    0x0375: CONTEXTO, # GREEK LOWER NUMERAL SIGN (KERAIA)
    0x05F3: CONTEXTO, # HEBREW PUNCTUATION GERESH
    0x05F4: CONTEXTO, # HEBREW PUNCTUATION GERSHAYIM
    0x30FB: CONTEXTO, # KATAKANA MIDDLE DOT
    0x0660: CONTEXTO, # ARABIC-INDIC DIGIT ZERO
    0x0661: CONTEXTO,
    0x0662: CONTEXTO,
    0x0663: CONTEXTO,
    0x0664: CONTEXTO,
    0x0665: CONTEXTO,
    0x0666: CONTEXTO,
    0x0667: CONTEXTO,
    0x0668: CONTEXTO,
    0x0669: CONTEXTO, # ARABIC-INDIC DIGIT NINE
    0x06F0: CONTEXTO, # EXTENDED ARABIC-INDIC DIGIT ZERO
    0x06F1: CONTEXTO,
    0x06F2: CONTEXTO,
    0x06F3: CONTEXTO,
    0x06F4: CONTEXTO,
    0x06F5: CONTEXTO,
    0x06F6: CONTEXTO,
    0x06F7: CONTEXTO,
    0x06F8: CONTEXTO,
    0x06F9: CONTEXTO, # EXTENDED ARABIC-INDIC DIGIT NINE
    0x0640: DISALLOWED, # ARABIC TATWEEL
    0x07FA: DISALLOWED, # NKO LAJANYALAN
    0x302E: DISALLOWED, # HANGUL SINGLE DOT TONE MARK
    0x302F: DISALLOWED, # HANGUL DOUBLE DOT TONE MARK
    0x3031: DISALLOWED, # VERTICAL KANA REPEAT MARK
    0x3032: DISALLOWED,
    0x3033: DISALLOWED,
    0x3034: DISALLOWED,
    0x3035: DISALLOWED, # VERTICAL KANA REPEAT MARK LOWER HALF
    0x303B: DISALLOWED, # VERTICAL IDEOGRAPHIC ITERATION MARK
}

# RFC 5892 section 2.7. Empty, and it has been empty since the RFC was
# published; it is here so the derivation reads like the specification and so
# that a future entry has an obvious place to go.
BACKWARD_COMPATIBLE = {}

# RFC 5892 section 2.11. Named by block rather than by codepoint, because
# that is how the rule is written.
IGNORABLE_BLOCK_NAMES = {
    "Combining Diacritical Marks for Symbols",
    "Musical Symbols",
    "Ancient Greek Musical Notation",
}

# The scripts the CONTEXTO rules of RFC 5892 appendix A name, and nothing
# else.
SCRIPTS_WANTED = ["Greek", "Hebrew", "Hiragana", "Katakana", "Han"]

# The Joining_Type values the CONTEXTJ zero-width-non-joiner rule reads.
JOINING_WANTED = ["T", "L", "R", "D"]

# The Bidi_Class values RFC 5893's rule distinguishes.
BIDI_WANTED = ["L", "R", "AL", "AN", "EN", "ES", "CS", "ET", "ON", "BN", "NSM"]


def parse_ranges(path, wanted=None):
    """Read a UCD file into {value: [(lo, hi), ...]}.

    `wanted` restricts which values are kept, which is what keeps the emitted
    tables to the size of the rules that read them.
    """
    out = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [f.strip() for f in line.split(";")]
            if len(fields) < 2:
                continue
            value = fields[1]
            if wanted is not None and value not in wanted:
                continue
            span = fields[0]
            if ".." in span:
                lo, hi = span.split("..")
            else:
                lo = hi = span
            out.setdefault(value, []).append((int(lo, 16), int(hi, 16)))
    return out


def to_set(ranges):
    members = set()
    for lo, hi in ranges:
        members.update(range(lo, hi + 1))
    return members


def derive(ucd):
    """RFC 5892 section 2, as the RFC writes it.

    The order of the tests is the order in the specification and is load
    bearing: Exceptions before everything, LDH before the general letter rule,
    Unstable before LetterDigits.
    """
    gc = parse_ranges(os.path.join(ucd, "DerivedGeneralCategory.txt"))
    core = parse_ranges(os.path.join(ucd, "DerivedCoreProperties.txt"))
    # Changes_When_NFKC_Casefolded lives here and not in
    # DerivedCoreProperties.txt, which is where it was first looked for - and
    # a missing property reads as an empty set, so every upper-case letter
    # came out PVALID instead of unstable. Named explicitly below rather than
    # merged into `core`, so the next reader can see which file it came from.
    norm = parse_ranges(os.path.join(ucd, "DerivedNormalizationProps.txt"))
    props = parse_ranges(os.path.join(ucd, "PropList.txt"))
    hangul = parse_ranges(os.path.join(ucd, "HangulSyllableType.txt"))
    blocks = parse_ranges(os.path.join(ucd, "Blocks.txt"))

    assigned = set()
    for value, ranges in gc.items():
        if value != "Cn":
            assigned |= to_set(ranges)

    letter_digits = set()
    for value in ("Ll", "Lu", "Lo", "Nd", "Lm", "Mn", "Mc"):
        letter_digits |= to_set(gc.get(value, []))

    unstable = to_set(norm.get("Changes_When_NFKC_Casefolded", []))
    if not unstable:
        sys.exit("Changes_When_NFKC_Casefolded is empty: the UCD layout moved")
    default_ignorable = to_set(core.get("Default_Ignorable_Code_Point", []))
    white_space = to_set(props.get("White_Space", []))
    noncharacter = to_set(props.get("Noncharacter_Code_Point", []))
    join_control = to_set(props.get("Join_Control", []))
    ignorable = default_ignorable | white_space | noncharacter

    old_hangul_jamo = set()
    for value in ("L", "V", "T"):
        old_hangul_jamo |= to_set(hangul.get(value, []))

    ignorable_blocks = set()
    for name, ranges in blocks.items():
        if name in IGNORABLE_BLOCK_NAMES:
            ignorable_blocks |= to_set(ranges)

    ldh = set(range(ord("a"), ord("z") + 1))
    ldh |= set(range(ord("0"), ord("9") + 1))
    ldh.add(ord("-"))

    result = []
    for cp in range(MAX_CODEPOINT + 1):
        if cp in EXCEPTIONS:
            result.append(EXCEPTIONS[cp])
        elif cp in BACKWARD_COMPATIBLE:
            result.append(BACKWARD_COMPATIBLE[cp])
        elif cp not in assigned:
            result.append(DISALLOWED)  # UNASSIGNED, and the same answer
        elif cp in ldh:
            result.append(PVALID)
        elif cp in join_control:
            result.append(CONTEXTJ)
        elif cp in unstable:
            result.append(DISALLOWED)
        elif cp in ignorable:
            result.append(DISALLOWED)
        elif cp in ignorable_blocks:
            result.append(DISALLOWED)
        elif cp in old_hangul_jamo:
            result.append(DISALLOWED)
        elif cp in letter_digits:
            result.append(PVALID)
        else:
            result.append(DISALLOWED)
    return result


def runs(values, skip):
    """Contiguous runs of equal value, leaving out `skip`."""
    out = []
    start = 0
    for cp in range(1, len(values) + 1):
        if cp == len(values) or values[cp] != values[start]:
            if values[start] != skip:
                out.append((start, cp - 1, values[start]))
            start = cp
    return out


def ranges_of(ucd_ranges, wanted):
    """One flat, sorted, merged list per wanted value."""
    out = {}
    for value in wanted:
        merged = []
        for lo, hi in sorted(ucd_ranges.get(value, [])):
            if merged and lo <= merged[-1][1] + 1:
                merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
            else:
                merged.append((lo, hi))
        out[value] = merged
    return out


BANNER = """/**
 * @file
 *
 * GENERATED by tools/idna/gen_tables.py from UCD {version}. Do not edit.
 *
 * Regenerate with:  tools/idna/gen_tables.py
 * Verify with:      make check-idna-tables
 *
 * Copyright 2026 by Corey Pennycuff
 */
"""


def emit_ranges(out, name, entries, value_expr):
    out.append("const GTEXT_IDNA_Range %s[] = {" % name)
    line = " "
    for lo, hi, value in entries:
        piece = " {0x%04X,0x%04X,%s}," % (lo, hi, value_expr(value))
        if len(line) + len(piece) > 78:
            out.append(line)
            line = " "
        line += piece
    if line.strip():
        out.append(line)
    out.append("};")
    out.append("")
    out.append("const size_t %s_count = sizeof(%s) / sizeof(%s[0]);"
               % (name, name, name))
    out.append("")


def main():
    parser = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    version = open(os.path.join(here, "UCD_VERSION"), encoding="utf-8").read().strip()
    parser.add_argument("--version", default=version)
    parser.add_argument("--ucd", default=None)
    parser.add_argument("--out", default=os.path.join(root, "src", "idna", "tables"))
    args = parser.parse_args()
    ucd = args.ucd or os.path.join(root, "third_party", "ucd", args.version)

    if not os.path.isdir(ucd):
        sys.exit("no UCD at %s - run tools/idna/fetch.sh" % ucd)

    derived = derive(ucd)
    # DISALLOWED is the default answer, so it is the one value not stored.
    derived_runs = runs(derived, DISALLOWED)

    scripts = ranges_of(parse_ranges(os.path.join(ucd, "Scripts.txt"),
                                     SCRIPTS_WANTED), SCRIPTS_WANTED)
    joining = ranges_of(parse_ranges(os.path.join(ucd, "DerivedJoiningType.txt"),
                                     JOINING_WANTED), JOINING_WANTED)
    bidi = ranges_of(parse_ranges(os.path.join(ucd, "DerivedBidiClass.txt"),
                                  BIDI_WANTED), BIDI_WANTED)
    # Virama is Canonical_Combining_Class 9, and nothing else here reads any
    # other class, so the table is that one value.
    combining = ranges_of(parse_ranges(os.path.join(ucd, "DerivedCombiningClass.txt"),
                                       ["9"]), ["9"])

    os.makedirs(args.out, exist_ok=True)
    body = [BANNER.format(version=args.version).rstrip(), "",
            '#include "tables_internal.h"', ""]

    emit_ranges(body, "gtext_idna_derived", derived_runs,
                lambda v: PROPERTY_NAMES[v])

    script_entries = []
    for index, name in enumerate(SCRIPTS_WANTED):
        for lo, hi in scripts[name]:
            script_entries.append((lo, hi, index))
    script_entries.sort()
    emit_ranges(body, "gtext_idna_script", script_entries,
                lambda v: "GTEXT_IDNA_SCRIPT_" + SCRIPTS_WANTED[v].upper())

    joining_entries = []
    for index, name in enumerate(JOINING_WANTED):
        for lo, hi in joining[name]:
            joining_entries.append((lo, hi, index))
    joining_entries.sort()
    emit_ranges(body, "gtext_idna_joining", joining_entries,
                lambda v: "GTEXT_IDNA_JOINING_" + JOINING_WANTED[v])

    bidi_entries = []
    for index, name in enumerate(BIDI_WANTED):
        for lo, hi in bidi[name]:
            bidi_entries.append((lo, hi, index))
    bidi_entries.sort()
    emit_ranges(body, "gtext_idna_bidi", bidi_entries,
                lambda v: "GTEXT_IDNA_BIDI_" + BIDI_WANTED[v])

    virama_entries = [(lo, hi, 0) for lo, hi in combining["9"]]
    emit_ranges(body, "gtext_idna_virama", virama_entries, lambda v: "0")

    with open(os.path.join(args.out, "idna_tables.c"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(body).rstrip() + "\n")

    counts = {
        "derived": len(derived_runs),
        "script": len(script_entries),
        "joining": len(joining_entries),
        "bidi": len(bidi_entries),
        "virama": len(virama_entries),
    }
    print("UCD %s: %s" % (args.version,
                          ", ".join("%s %d ranges" % (k, v)
                                    for k, v in sorted(counts.items()))))


if __name__ == "__main__":
    main()
