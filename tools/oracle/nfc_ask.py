#!/usr/bin/env python3
"""Answer NFC for a batch of sequences, as the reference sees them.

Runs *inside* the pinned image; `nfc_diff.py` on the host generates the
sequences, asks the library under test the same questions, and compares. The
split is forced by which half needs what: the library's answers come from a C
driver linked against the archive, which a `python:slim` image has no compiler
for, and the reference's answers come from a CPython that is deliberately not
this machine's.

The protocol is a batch, deliberately, and it is line-for-line: one answer per
input line, in order, and the caller checks the counts. A per-sequence
round-trip would be four million container invocations; a protocol that dropped
or reordered a line would be a comparison against the wrong sequence, which is
worse than no comparison because it prints as a disagreement.

Two kinds of answer:

  <hex> [<hex>...]   the NFC of that sequence
  skip               this reference does not assign one of those codepoints

`skip` is a real answer rather than an omission. A codepoint the reference's UCD
has never heard of normalises to itself there whatever the library says, so
comparing would be comparing against nothing - but the *count* of them is the
gate's whole skew, so it has to come back rather than vanish. Category rather
than name is the test, because a control character is assigned and has no name,
and testing the name would quietly drop every one of them from the comparison.

The first line is the version, so that a caller which somehow reached an
unexpected reference finds out from the answers and not only from the pin.

Copyright 2026 by Corey Pennycuff
"""

import sys
import unicodedata


def main():
    out = sys.stdout
    out.write("version unicodedata %s python %s\n"
              % (unicodedata.unidata_version, sys.version.split()[0]))
    # Bound locally: these are the two hot calls over several million lines.
    category = unicodedata.category
    normalize = unicodedata.normalize
    for line in sys.stdin:
        fields = line.split()
        if not fields:
            continue
        codepoints = [int(field, 16) for field in fields]
        if any(category(chr(cp)) == "Cn" for cp in codepoints):
            out.write("skip\n")
            continue
        out.write(" ".join(
            "%04X" % ord(ch)
            for ch in normalize("NFC", "".join(map(chr, codepoints)))) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
