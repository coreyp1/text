#!/usr/bin/env python3
"""Run one oracle gate, having first proved its reference is reachable.

    oracle_run.py <name>[,<name>...] -- <command> [args...]

Two jobs.

**Prove it, then print it.** The reference is resolved and asked its version
*before* the gate runs, and the version goes on the line above the gate's
numbers. For this library that line carries the whole claim: a clean NFC
differential against UCD 16.0.0 and one against 17.0.0 are different
statements, and a run that does not say which it made cannot be read a week
later. The same is true of the IDNA gate with more force, because there the
reference's version decides how many codepoints are compared at all.

**Fail closed.** With GHOTI_ORACLE_REQUIRED=1 an unreachable reference is an
error naming what is missing. Without it the gate still declines to run - but
loudly, with the word SKIPPED and a reason, and having actually tried rather
than having read `command -v`.

That distinction is this library's own lesson. `command -v python3` answers "is
something called python3 on PATH", which is not the question; the question is
"can this gate reach the reference it names". Four gates here exited 0 for
every fresh clone because they answered the first question - and python3 has
always been on PATH on this machine, while the reference it reached was two
Unicode releases from the tables it was checking.

Copyright 2026 by Corey Pennycuff
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env


def main(argv):
    if "--" not in argv or len(argv) < 2:
        sys.stderr.write("usage: oracle_run.py <name>[,<name>] -- <command>\n")
        return 2
    cut = argv.index("--")
    names = [n for n in argv[1:cut][0].split(",") if n]
    command = argv[cut + 1:]
    if not names or not command:
        sys.stderr.write("usage: oracle_run.py <name>[,<name>] -- <command>\n")
        return 2

    try:
        line = oracle_env.provenance(names)
    except oracle_env.OracleUnavailable as why:
        return oracle_env.decline(" ".join(command[:3]), why)
    print(line, flush=True)
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
