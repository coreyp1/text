#!/usr/bin/env python3
"""Dump python-idna's tables, as the reference holds them.

Runs *inside* the pinned image; `idna_diff.py` on the host derives this
library's answers from the pinned UCD and compares. Unlike the NFC gate, both
halves of this comparison are pure data - there is no driver to link - so the
split is not forced by a compiler. It is here for a different reason:

**python-idna's module surface is not stable and its output is.** 3.10 exposed
`idna.uts46data.uts46data` as a list of tuples; 3.19 replaced it with three
parallel arrays (`uts46_starts`, `uts46_statuses`, `uts46_replacements`) and
deleted the old name. A differential that reached into the package directly
broke on that upgrade - and the upgrade is the whole point of pinning this
reference, because 3.19 is the release whose tables match this library's UCD.
With the reach-in confined here, adapting to the next such change is one
function and the comparison never learns about it.

The protocol, one record per line, all codepoints hexadecimal and all ranges
half-open:

  version idna <v> idnadata <ucd> uts46data <ucd>
  C <NAME> <lo> <hi>              a codepoint_classes range: PVALID, CONTEXTJ,
                                  CONTEXTO
  U <lo> <hi> <status> [<hex>...] a UTS #46 run and its mapping

**What is deliberately not here: which codepoints are assigned.** The obvious
thing to emit is `unicodedata.category(cp) != "Cn"` from this interpreter, and
that is what the host-side script used to read from *its* interpreter. It is the
wrong instrument either way. The question the comparison needs is "does the UCD
release these tables were generated from assign this codepoint", and the
interpreter's `unicodedata` answers for a different release - `python:3.14-slim`
carries 16.0.0 while the idna installed over it carries 17.0.0, so asking here
would silently partition the codespace by a third version that neither side of
the comparison uses. The host answers it from the pinned UCD instead, having
first asserted that `idnadata`'s version is that same pin.

Copyright 2026 by Corey Pennycuff
"""

import sys

import idna
import idna.idnadata
import idna.uts46data


def classes():
    """codepoint_classes as half-open ranges.

    Each entry packs a range into one integer as `(lo << 32) | hi_exclusive`,
    which is the package's own storage format and not something either
    specification says. Emitted unexpanded: expanding here would turn a few
    thousand lines into a few hundred thousand for no gain, and the host expands
    per codepoint anyway.
    """
    for name, ranges in idna.idnadata.codepoint_classes.items():
        for entry in ranges:
            yield name, entry >> 32, entry & 0xFFFFFFFF


def uts46_runs():
    """The UTS #46 table as (lo, hi, status, target) with hi exclusive.

    Both shapes python-idna has used. Each run starts at its own codepoint and
    ends where the next one starts, in both layouts; the final run covers one
    codepoint, which is what the package's own lookup does with it.
    """
    starts = getattr(idna.uts46data, "uts46_starts", None)
    if starts is not None:
        statuses = idna.uts46data.uts46_statuses
        replacements = idna.uts46data.uts46_replacements
        for index, lo in enumerate(starts):
            hi = starts[index + 1] if index + 1 < len(starts) else lo + 1
            yield lo, hi, chr(statuses[index]), replacements[index] or ""
        return
    rows = idna.uts46data.uts46data
    for index, row in enumerate(rows):
        lo = row[0]
        hi = rows[index + 1][0] if index + 1 < len(rows) else lo + 1
        yield lo, hi, row[1], (row[2] if len(row) > 2 else "") or ""


def main():
    out = sys.stdout
    out.write("version idna %s idnadata %s uts46data %s\n"
              % (idna.__version__, idna.idnadata.__version__,
                 idna.uts46data.__version__))
    for name, lo, hi in classes():
        out.write("C %s %04X %04X\n" % (name, lo, hi))
    for lo, hi, status, target in uts46_runs():
        out.write("U %04X %04X %s%s\n"
                  % (lo, hi, status,
                     "".join(" %04X" % ord(ch) for ch in target)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
