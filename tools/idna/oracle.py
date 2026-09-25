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

The same package also carries UTS #46's mapping table, so the second half of
this compares that against ours - two readings of one published file, by
different people.

Usage:
    tools/idna/oracle.py [--ucd DIR] [--version V]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import os
import sys
import unicodedata
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tables

try:
    import idna.idnadata
    import idna.uts46data
except ImportError:
    sys.exit("oracle.py needs the `idna` package: pip install idna")

import gen_uts46


def check_mapping(root):
    """The UTS #46 mapping table against python-idna's copy of it.

    Two readings of one published file, by different people, so a
    disagreement is a defect in one of them - a range expanded wrongly, a
    mapping taken from the wrong field.

    The comparison is made against the *oracle's* version of the file rather
    than the one this library pins, and that is the whole trick. The two
    tables version independently and their contents really do change: 15.1.0
    calls the Georgian capitals disallowed and 16.0.0 maps them to their
    small-letter forms. Comparing across versions would report three hundred
    of those as findings and bury a real parsing error among them. Comparing
    this repository's parser against python-idna on the file python-idna was
    built from asks only the question the oracle can answer.

    Only mapped and ignored are compared, because they are all this library
    records: it takes validity from RFC 5892, so `valid` and `disallowed` are
    not questions it answers.
    """
    version = getattr(idna.uts46data, "__version__", None)
    if not version:
        print("\nUTS #46 mapping: skipped (python-idna does not say which "
              "version of the table it carries)")
        return 0

    path = os.path.join(
        root, "third_party", "idna", version, "IdnaMappingTable.txt")
    if not os.path.exists(path):
        url = ("https://www.unicode.org/Public/idna/%s/IdnaMappingTable.txt"
               % version)
        print("\nfetching %s for the oracle" % url)
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
            if not data.startswith(b"#"):
                raise ValueError("that did not look like a mapping table")
            with open(path, "wb") as handle:
                handle.write(data)
        except Exception as exc:  # network, 404, anything
            print("UTS #46 mapping: skipped (%s)" % exc)
            return 0

    ours = gen_uts46.parse_mapping(path)

    # python-idna stores (start, status[, mapping]), each entry running to the
    # next start. Expanded rather than bisected, because the comparison is
    # per-codepoint anyway and an off-by-one in a bisect would be a bug in the
    # oracle rather than a finding.
    theirs = {}
    rows = idna.uts46data.uts46data
    for index, row in enumerate(rows):
        start = row[0]
        stop = rows[index + 1][0] if index + 1 < len(rows) else start + 1
        status = row[1]
        if status not in ("M", "I"):
            continue
        target = row[2] if len(row) > 2 else ""
        value = ("mapped", [ord(c) for c in target]) if status == "M" \
            else ("ignored", [])
        for cp in range(start, stop):
            theirs[cp] = value

    disagree = []
    for cp in sorted(set(ours) | set(theirs)):
        if ours.get(cp) != theirs.get(cp):
            disagree.append((cp, ours.get(cp), theirs.get(cp)))

    print("\noracle: UTS #46 mapping %s against python-idna %s"
          % (version, idna.__version__))
    print("compared %d characters" % len(set(ours) | set(theirs)))
    if not disagree:
        print("no disagreements")
        return 0
    print("%d disagreements:" % len(disagree))
    for cp, mine, yours in disagree[:40]:
        print("  U+%04X  ours=%-28s theirs=%s" % (cp, mine, yours))
    if len(disagree) > 40:
        print("  ... and %d more" % (len(disagree) - 40))
    return 1


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

    # Which codepoints this library's UCD assigns, so that the three reasons a
    # comparison did not happen can be told apart. UnicodeData.txt lists the
    # large blocks as First/Last pairs rather than row by row, and without
    # expanding them the CJK and Hangul ranges read as unassigned - 254,501
    # codepoints in the wrong bucket.
    listed, blocks, first = set(), [], None
    for line in open(os.path.join(ucd, "UnicodeData.txt"), encoding="utf-8"):
        fields = line.split(";")
        if len(fields) < 2:
            continue
        cp, name = int(fields[0], 16), fields[1]
        if name.endswith(", First>"):
            first = cp
        elif name.endswith(", Last>"):
            blocks.append((first, cp))
            first = None
        else:
            listed.add(cp)

    def assigned_here(cp):
        return cp in listed or any(lo <= cp <= hi for lo, hi in blocks)
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
    # Four counts, not two and not three.  A codepoint the oracle's Unicode does
    # not assign gets DISALLOWED from it by default, and this library usually
    # says DISALLOWED too, so the pair agrees without either side having an
    # opinion; those agreements used to sit in the same total as the real
    # comparisons.  Splitting them out was the first fix and it was not enough,
    # because 925 of them are assigned in *our* UCD and only unassigned in the
    # reference's - so a line reading "neither version assigns them" was hiding
    # the one bucket where a wrong answer of ours passes silently.  That is the
    # finding's own shape one level up, which is why it gets its own line and
    # the wording says what it is rather than what it mostly is.
    #
    #   compared      both versions assign it: the figure to quote
    #   novel_agreed  ours assigns it, the reference does not, and we both say
    #                 DISALLOWED - so a DISALLOWED of ours that should have been
    #                 PVALID agrees with the reference's ignorance and passes
    #   skipped       ours assigns it, the reference does not, answers differ
    #   unassigned    neither assigns it
    shared = 0
    novel_agreed = 0
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
        if not assigned_there:
            shared += 1
            if assigned_here(cp):
                novel_agreed += 1
        if mine != yours:
            disagree.append((cp, mine, yours))

    print("oracle: python-idna %s (Unicode %s) against UCD %s"
          % (idna.__version__, unicodedata.unidata_version, args.version))
    print("compared %d codepoints where both versions have an opinion"
          % (compared - shared))
    print("  %d are assigned by neither version and agree by shared default"
          % (shared - novel_agreed))
    print("  %d were skipped: we assign them, the oracle does not, and the"
          % skipped)
    print("    answers differ, so the difference is a UCD version and not a bug")
    print("  %d are the blind spot: we assign them, the oracle does not, and we"
          % novel_agreed)
    print("    both say DISALLOWED - so a DISALLOWED of ours that should have")
    print("    been PVALID agrees with the oracle's ignorance and passes here")
    print("  %d total; the first number is the one to quote"
          % (compared + skipped))
    status = 0
    if not disagree:
        print("no disagreements")
    else:
        status = 1
        print("%d disagreements:" % len(disagree))
        for cp, mine, yours in disagree[:40]:
            try:
                name = unicodedata.name(chr(cp))
            except (ValueError, KeyError):
                name = "?"
            print("  U+%04X  ours=%-10s theirs=%-10s  %s"
                  % (cp, mine, yours, name))
        if len(disagree) > 40:
            print("  ... and %d more" % (len(disagree) - 40))

    status |= check_mapping(root)
    return status


if __name__ == "__main__":
    sys.exit(main())
