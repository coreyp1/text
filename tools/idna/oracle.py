#!/usr/bin/env python3
"""Compare the derived IDNA tables against an independent implementation.

The derivation in gen_tables.py is this repository's reading of RFC 5892
section 2. A reading is a thing that can be wrong, and the first one here was:
it looked for Changes_When_NFKC_Casefolded in the wrong UCD file, found an
empty set, and made every upper-case letter PVALID. Nothing about that is
visible in the output unless something else is asked the same question.

That something else is the `idna` package, which implements IDNA2008 from its
own generated tables. It is not a rewording of this code - different author,
different derivation, same RFC - which is what makes the comparison worth
anything.

The two are built against different Unicode versions, so codepoints the
oracle's Unicode does not know are skipped and counted. A disagreement about a
codepoint both versions assign is a defect in one of them.

Usage:
    tools/idna/oracle.py [--ucd DIR] [--version V]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import os
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tables

try:
    import idna.idnadata
except ImportError:
    sys.exit("oracle.py needs the `idna` package: pip install idna")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    version = open(os.path.join(here, "UCD_VERSION"), encoding="utf-8").read().strip()
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default=version)
    parser.add_argument("--ucd", default=None)
    args = parser.parse_args()
    ucd = args.ucd or os.path.join(root, "third_party", "ucd", args.version)
    if not os.path.isdir(ucd):
        sys.exit("no UCD at %s - run tools/idna/fetch.sh" % ucd)

    ours = gen_tables.derive(ucd)
    classes = idna.idnadata.codepoint_classes
    # Each entry packs a half-open range into one integer as
    # (lo << 32) | hi_exclusive, which is the oracle's own storage format and
    # not something either specification says.
    theirs = {}
    for name, ranges in classes.items():
        for entry in ranges:
            lo = entry >> 32
            hi = entry & 0xFFFFFFFF
            for cp in range(lo, hi):
                theirs[cp] = name

    names = {gen_tables.PVALID: "PVALID", gen_tables.CONTEXTJ: "CONTEXTJ",
             gen_tables.CONTEXTO: "CONTEXTO", gen_tables.DISALLOWED: "DISALLOWED"}

    disagree = []
    skipped = 0
    compared = 0
    for cp in range(gen_tables.MAX_CODEPOINT + 1):
        # Surrogates and codepoints the oracle's Unicode does not assign are
        # not a disagreement about anything.
        try:
            assigned_there = unicodedata.category(chr(cp)) != "Cn"
        except ValueError:
            assigned_there = False
        mine = names[ours[cp]]
        yours = theirs.get(cp, "DISALLOWED")
        if not assigned_there and mine != yours:
            skipped += 1
            continue
        compared += 1
        if mine != yours:
            disagree.append((cp, mine, yours))

    print("oracle: python-idna %s (Unicode %s) against UCD %s"
          % (idna.__version__, unicodedata.unidata_version, args.version))
    print("compared %d codepoints, skipped %d the oracle's Unicode "
          "does not assign" % (compared, skipped))
    if not disagree:
        print("no disagreements")
        return 0
    print("%d disagreements:" % len(disagree))
    for cp, mine, yours in disagree[:40]:
        try:
            name = unicodedata.name(chr(cp))
        except (ValueError, KeyError):
            name = "?"
        print("  U+%04X  ours=%-10s theirs=%-10s  %s" % (cp, mine, yours, name))
    if len(disagree) > 40:
        print("  ... and %d more" % (len(disagree) - 40))
    return 1


if __name__ == "__main__":
    sys.exit(main())
