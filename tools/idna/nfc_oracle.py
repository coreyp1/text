#!/usr/bin/env python3
"""Compare this library's NFC against Python's, over everything.

The normalisation is ghoti.io-unicode's, reached through the thin adapter in
src/idna/nfc.c. It can be wrong in ways nothing else here notices: a
composition exclusion missed, the canonical ordering made unstable, the "not
blocked from the starter" test written as a plain comparison - each of those
produces a normaliser that is right about almost every string and wrong about
a few. unicode runs its own, larger oracle against CPython; this one asks the
question through the call path that text actually uses, which is the part that
a migration can break.

Python's `unicodedata.normalize` is an independent implementation of the same
annex, written by other people from the same data. It is compiled into
CPython, so it is a different Unicode version from the one this library pins;
sequences containing a codepoint that version does not assign are skipped and
counted, and a disagreement about anything both versions know is a defect in
one of them.

What is compared:

- every codepoint on its own, which covers the singleton decompositions and
  every Hangul syllable;
- a starter followed by one combining mark, over every mark with a non-zero
  combining class, which is the composition table;
- a starter followed by two marks, which is where canonical ordering and the
  blocking rule live - an unstable sort or a wrong blocking test shows up
  here and nowhere else;
- the L, V and T jamo in every combination, which is Hangul composition.

Usage:
    tools/idna/nfc_oracle.py [--ucd DIR] [--version V]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import os
import subprocess
import sys
import sysconfig
import tempfile
import unicodedata

MAX_CODEPOINT = 0x10FFFF

DRIVER = r"""
/* Written by tools/idna/nfc_oracle.py. Reads sequences, writes their NFC. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "idna/nfc_internal.h"

int main(void) {
  char line[4096];
  uint32_t in[64];
  uint32_t out[512];
  while (fgets(line, sizeof line, stdin)) {
    size_t n = 0;
    char * at = line;
    while (n < sizeof(in) / sizeof(in[0])) {
      char * end = NULL;
      unsigned long value = strtoul(at, &end, 16);
      if (end == at) {
        break;
      }
      in[n++] = (uint32_t)value;
      at = end;
    }
    size_t got = 0;
    if (!gtext_nfc(in, n, out, sizeof(out) / sizeof(out[0]), &got)) {
      printf("ERROR\n");
      continue;
    }
    for (size_t i = 0; i < got; i++) {
      printf("%s%04X", i ? " " : "", out[i]);
    }
    printf("\n");
  }
  return 0;
}
"""


def build_driver(root, workdir):
    archive = None
    for candidate in ("linux", "darwin", "windows"):
        path = os.path.join(
            root, "build", candidate, "release", "apps",
            "libghoti.io-text-0.a")
        if os.path.exists(path):
            archive = path
            break
    if not archive:
        sys.exit("build the library first (make)")

    source = os.path.join(workdir, "driver.c")
    with open(source, "w", encoding="ascii") as handle:
        handle.write(DRIVER)
    binary = os.path.join(workdir, "driver")
    generated = os.path.join(
        os.path.dirname(os.path.dirname(archive)), "generated")
    # The archive's NFC is a call into ghoti.io-unicode now, so the driver
    # needs that library on its link line. pkg-config is asked the same way
    # the Makefile asks, along the same PKG_CONFIG_PATH: a driver linked
    # against a different build of unicode than the archive was would answer
    # for whichever one the loader picked.
    branch = "-0"
    unicode_flags = []
    for kind in ("--cflags", "--libs"):
        probe = subprocess.run(
            ["pkg-config", kind, "ghoti.io-unicode" + branch],
            capture_output=True, text=True)
        if probe.returncode != 0:
            sys.exit("pkg-config could not find ghoti.io-unicode" + branch
                     + "; point PKG_CONFIG_PATH at its .pc file")
        unicode_flags += probe.stdout.split()

    # An rpath for every -L the linker was given, so the driver finds the
    # shared library without the caller having exported LD_LIBRARY_PATH. A
    # gate that only passes when the environment happens to be right is a gate
    # that passes for the wrong reason.
    for flag in list(unicode_flags):
        if flag.startswith("-L") and len(flag) > 2:
            unicode_flags.append("-Wl,-rpath," + flag[2:])

    command = [
        os.environ.get("CC", "cc"), "-O1", "-o", binary, source,
        "-I", os.path.join(root, "include"),
        "-I", generated,
        "-I", os.path.join(root, "src"),
        archive,
    ] + unicode_flags + ["-lm"]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("could not build the NFC driver:\n" + result.stderr)
    return binary


def sequences(marks, starters, ccc):
    """Everything the two implementations are asked about."""
    for cp in range(0, MAX_CODEPOINT + 1):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        yield (cp,)
    for starter in starters:
        for mark in marks:
            yield (starter, mark)
    # Two marks, so that ordering and blocking are exercised. Every ordered
    # pair of marks would be forty million sequences; every pair drawn from
    # one mark per combining class, against every mark, is the part of that
    # space where the classes actually differ.
    # The class comes from the pinned UCD, like the marks themselves. It used
    # to come from unicodedata.combining(), which is the reference - and for a
    # mark the reference does not assign that returns 0, so the 46 marks newer
    # than the host's UCD all collapsed into a spurious class-0 bucket that
    # merged four real classes (9, 220, 230 and 234). It cost no coverage,
    # because all four are also held by long-assigned marks, and it added a
    # 56th representative whose sequences were skipped in their entirety. But
    # the key was wrong, and a class held *only* by marks the reference does
    # not know would have lost its representative to that bucket.
    #
    # This is the same substitution one line below the one the population
    # already avoided, which is the useful part: checking that `marks` comes
    # from the pin says nothing about the line that groups them.
    per_class = {}
    for mark in marks:
        per_class.setdefault(ccc[mark], mark)
    for starter in starters[:24]:
        for first in per_class.values():
            for second in marks:
                yield (starter, first, second)
    # Hangul, in both directions.
    for lead in range(0x1100, 0x1113):
        for vowel in range(0x1161, 0x1176):
            yield (lead, vowel)
            for trail in range(0x11A8, 0x11C3):
                yield (lead, vowel, trail)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    version = open(
        os.path.join(here, "UCD_VERSION"), encoding="utf-8").read().strip()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=version)
    parser.add_argument("--ucd", default=None)
    args = parser.parse_args()
    ucd = args.ucd or os.path.join(root, "third_party", "ucd", args.version)

    if not os.path.isdir(ucd):
        sys.exit("no %s; run tools/idna/fetch.sh" % ucd)

    # The marks and starters the comparison is built from come from the UCD
    # this library pins, not from Python's, so a mark too new for Python is
    # still asked about and then skipped by name rather than silently missed.
    marks = []
    starters = []
    ccc = {}
    for line in open(os.path.join(ucd, "UnicodeData.txt"), encoding="utf-8"):
        fields = line.split(";")
        if len(fields) < 6:
            continue
        cp = int(fields[0], 16)
        if int(fields[3]) != 0:
            marks.append(cp)
            ccc[cp] = int(fields[3])
        elif fields[5] and not fields[5].startswith("<"):
            starters.append(cp)
    if not marks or not starters:
        sys.exit("UnicodeData.txt yielded no marks or no starters")

    with tempfile.TemporaryDirectory() as workdir:
        binary = build_driver(root, workdir)

        checked = 0
        skipped = 0
        disagreements = []
        batch = []

        def flush():
            nonlocal checked, skipped
            if not batch:
                return
            text = "".join(
                " ".join("%04X" % cp for cp in seq) + "\n" for seq in batch)
            result = subprocess.run(
                [binary], input=text, capture_output=True, text=True)
            if result.returncode != 0:
                sys.exit("the NFC driver failed:\n" + result.stderr)
            lines = result.stdout.splitlines()
            if len(lines) != len(batch):
                sys.exit("the driver answered %d of %d sequences"
                         % (len(lines), len(batch)))
            for seq, answer in zip(batch, lines):
                source = "".join(chr(cp) for cp in seq)
                # A codepoint Python's Unicode has not assigned normalises to
                # itself there whatever this library says, so the comparison
                # would be against nothing. Category rather than name, because
                # a control character is assigned and has no name, and
                # skipping those would quietly drop them from the comparison.
                if any(unicodedata.category(chr(cp)) == "Cn" for cp in seq):
                    skipped += 1
                    continue
                expected = " ".join(
                    "%04X" % ord(c) for c in unicodedata.normalize(
                        "NFC", source))
                checked += 1
                if answer != expected:
                    if len(disagreements) < 20:
                        disagreements.append(
                            (" ".join("%04X" % cp for cp in seq),
                             answer, expected))
            batch.clear()

        for seq in sequences(marks, starters, ccc):
            batch.append(seq)
            if len(batch) >= 50000:
                flush()
        flush()

        # The reference's Unicode version against this library's pin, on the
        # line with the numbers rather than somewhere a reader has to know to
        # look. The skew is what the skip count *is*: a reference two releases
        # behind cannot answer for a codepoint it has never heard of, so a
        # clean run here is a claim about 3.4 million sequences and silent
        # about a million more. It is also why a disagreement is a finding
        # rather than a defect until someone checks whether the Consortium
        # changed that codepoint.
        oracle_ucd = unicodedata.unidata_version
        print("reference: CPython %s carrying UCD %s; these tables are UCD %s%s"
              % (".".join(str(n) for n in sys.version_info[:3]), oracle_ucd,
                 args.version,
                 "" if oracle_ucd == args.version else "  <- behind"))
        print("compared %d sequences, skipped %d the reference's UCD does not "
              "assign" % (checked, skipped))
        if disagreements:
            print("\ndisagreements:")
            for seq, ours, theirs in disagreements:
                print("  %-24s ours %-24s python %s" % (seq, ours, theirs))
            sys.exit(1)
        print("no disagreements")


if __name__ == "__main__":
    main()
