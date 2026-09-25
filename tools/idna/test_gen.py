#!/usr/bin/env python3
"""Tests for the IDNA table generator.

These run without the UCD, so `make check-idna-tables` can say something even
on a checkout that has not fetched it. They check the parts of the generator
that are logic rather than data: the run-length encoding the tables are built
from, and the shape of the RFC 5892 exception list.

Usage:  tools/idna/test_gen.py

Copyright 2026 by Corey Pennycuff
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tables as g

failures = []
checked = 0


def check(condition, what):
    global checked
    checked += 1
    if not condition:
        failures.append(what)


# runs() must coalesce equal neighbours, drop the skipped value, and report
# inclusive bounds - an off-by-one here would shift every range in the table.
check(g.runs([1, 1, 1], 4) == [(0, 2, 1)], "runs: one run")
check(g.runs([4, 1, 1, 4], 4) == [(1, 2, 1)], "runs: skip at both ends")
check(g.runs([1, 2, 1], 4) == [(0, 0, 1), (1, 1, 2), (2, 2, 1)],
      "runs: no coalescing across a different value")
check(g.runs([4, 4, 4], 4) == [], "runs: everything skipped")
check(g.runs([], 4) == [], "runs: empty")
check(g.runs([1], 4) == [(0, 0, 1)], "runs: single element")

# The exceptions are transcribed from RFC 5892 section 2.6 by hand, so their
# count and their partition are worth asserting. The RFC lists 6 PVALID; 25
# CONTEXTO, of which 5 are punctuation and 20 are the two blocks of ten
# Arabic-Indic digits; and 10 DISALLOWED.
counts = {}
for value in g.EXCEPTIONS.values():
    counts[value] = counts.get(value, 0) + 1
check(counts.get(g.PVALID) == 6, "exceptions: 6 PVALID, got %r" % counts.get(g.PVALID))
check(counts.get(g.CONTEXTO) == 25,
      "exceptions: 25 CONTEXTO, got %r" % counts.get(g.CONTEXTO))
check(counts.get(g.DISALLOWED) == 10,
      "exceptions: 10 DISALLOWED, got %r" % counts.get(g.DISALLOWED))
check(len(g.EXCEPTIONS) == 41, "exceptions: 41 entries in all")
check(g.EXCEPTIONS[0x00DF] == g.PVALID, "exceptions: sharp s is PVALID")
check(g.EXCEPTIONS[0x0640] == g.DISALLOWED, "exceptions: tatweel is DISALLOWED")
check(g.EXCEPTIONS[0x30FB] == g.CONTEXTO,
      "exceptions: katakana middle dot is CONTEXTO")

# The Script and Joining_Type lists these two checks guarded are gone: idna.c
# reads both properties from ghoti.io-unicode, in full, so there is no longer a
# narrow subset that could fail to name a value the C side switches on.

if failures:
    for line in failures:
        print("FAIL: %s" % line)
    sys.exit(1)
print("generator tests pass (%d checks)" % checked)
