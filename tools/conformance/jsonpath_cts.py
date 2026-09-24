#!/usr/bin/env python3
"""Score this library against the JSONPath Compliance Test Suite.

    tools/conformance/jsonpath_cts.py <suite dir> <runner>

The suite is one file, cts.json, holding 706 cases. Each is either a query that
must be refused (`invalid_selector`) or a query with a document and the node
list it must select - `result`, or `results` where more than one order is
acceptable because the specification leaves object member order to the
implementation.

Three outcomes are counted, not two. A query this library refuses as
*unsupported* - the filter selector, which needs an expression evaluator and, for
match() and search(), a regular expression engine - is not attempted, and is
reported separately rather than as a pass or a failure. Scoring it as a pass
where the case happens to be an invalid_selector one would count an
unimplemented feature as conformance; scoring it as a failure would bury the
cases that do work.

Environment:
    JPC_MIN         floor on the pass rate over attempted cases (default 100)
    JPC_VERBOSE     print every failing case
"""

import json
import os
import subprocess
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    suite, runner = sys.argv[1], sys.argv[2]
    with open(os.path.join(suite, "cts.json"), encoding="utf-8") as f:
        cases = json.load(f)["tests"]

    attempted = passed = 0
    unsupported = 0
    failures = []

    for case in cases:
        selector = case["selector"].encode("utf-8")
        document = (json.dumps(case["document"]).encode("utf-8")
                    if "document" in case else b"")
        # The selector goes through stdin length-prefixed rather than as an
        # argument: the suite has selectors containing a NUL, which argv cannot
        # carry.
        request = b"%d\n%s%s" % (len(selector), selector, document)
        try:
            out = subprocess.run([runner], input=request,
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                 timeout=30).stdout.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            attempted += 1
            failures.append((case["name"], "timed out"))
            continue

        line = out.split("\n", 1)[0]
        if line.startswith("UNSUPPORTED"):
            unsupported += 1
            continue

        attempted += 1
        if case.get("invalid_selector"):
            if line.startswith("INVALID"):
                passed += 1
            else:
                failures.append((case["name"], "accepted an invalid selector: " + line[:80]))
            continue

        if not line.startswith("OK "):
            failures.append((case["name"], "refused a valid selector: " + line[:80]))
            continue

        try:
            got = json.loads(line[3:])
        except json.JSONDecodeError as exc:
            failures.append((case["name"], "unreadable output (%s): %s" % (exc, line[:80])))
            continue

        if "results" in case:
            if any(got == alternative for alternative in case["results"]):
                passed += 1
            else:
                failures.append((case["name"], "got %s, none of the %d accepted orders"
                                 % (json.dumps(got)[:60], len(case["results"]))))
        elif got == case["result"]:
            passed += 1
        else:
            failures.append((case["name"], "got %s want %s"
                             % (json.dumps(got)[:60], json.dumps(case["result"])[:60])))

    total = len(cases)
    rate = 100.0 * passed / attempted if attempted else 0.0
    if failures and os.environ.get("JPC_VERBOSE"):
        for name, why in failures:
            print("  FAIL %s: %s" % (name, why))
    elif failures:
        for name, why in failures[:10]:
            print("  FAIL %s: %s" % (name, why))
        if len(failures) > 10:
            print("  ... and %d more (JPC_VERBOSE=1 for all)" % (len(failures) - 10))

    print("JSONPath Compliance Test Suite: %d cases" % total)
    print("  attempted             %5d" % attempted)
    print("  passed                %5d  (%.1f%% of attempted, %.1f%% of the suite)"
          % (passed, rate, 100.0 * passed / total))
    print("  not attempted         %5d  (the filter selector, refused as unsupported)"
          % unsupported)
    print("A percentage over attempted cases means nothing without that last count.")

    floor = float(os.environ.get("JPC_MIN", "100"))
    if rate + 1e-9 < floor:
        print("pass rate %.1f%% is below the floor of %.1f%%" % (rate, floor))
        return 1
    print("pass rate %.1f%% meets the floor of %.1f%%" % (rate, floor))
    return 0


if __name__ == "__main__":
    sys.exit(main())
