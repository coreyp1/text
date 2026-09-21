"""Score this library's schema engine against JSON-Schema-Test-Suite.

Usage: json_schema_suite.py <suite-dir> <runner> [--draft draft2020-12]

The runner prints one TSV record per assertion, and one for each group whose
schema it refused, carrying the number of assertions that group holds. The
score is over every assertion in the corpus:

    total = passed + wrong + unreachable

`unreachable` is the assertions inside a group the engine would not compile.
Keeping them in the denominator is the whole point. This engine refuses what
it cannot enforce, which is the right policy and also one that makes a
pass-rate computed over attempted assertions climb as support narrows; the
suite's denominator is fixed, so ours is too.

JSS_MIN sets a floor the pass rate must meet, so the target can be wired into
a build without the score silently regressing. JSS_REPORT names a file to
write every wrong answer to.

Copyright 2026 by Corey Pennycuff
"""
import collections, glob, os, subprocess, sys

args = sys.argv[1:]
draft = 'draft2020-12'
if '--draft' in args:
    i = args.index('--draft')
    draft = args[i + 1]
    del args[i:i + 2]
if len(args) < 2:
    sys.exit(__doc__.strip().splitlines()[2])
suite, runner = args[0], args[1]

root = os.path.join(suite, 'tests', draft)
required = sorted(glob.glob(os.path.join(root, '*.json')))
if not required:
    sys.exit("no %s files under %s" % (draft, root))
optional = sorted(glob.glob(os.path.join(root, 'optional', '*.json')))
formats = sorted(glob.glob(os.path.join(root, 'optional', 'format', '*.json')))


def run(files):
    """Every assertion in `files`, as (passed, wrong, unreachable, records)."""
    if not files:
        return 0, 0, 0, []
    out = subprocess.run([runner] + files, capture_output=True, timeout=600)
    if out.returncode != 0:
        sys.exit("runner failed: %s" % out.stderr.decode('utf-8', 'replace'))
    records = [line.split('\t')
               for line in out.stdout.decode('utf-8', 'replace').splitlines()]
    passed = sum(1 for r in records if r[0] == 'PASS')
    wrong = sum(1 for r in records if r[0] == 'FAIL')
    unreachable = sum(int(r[3]) for r in records if r[0] in ('SKIP', 'BAD'))
    return passed, wrong, unreachable, records


def report(title, files):
    passed, wrong, unreachable, records = run(files)
    total = passed + wrong + unreachable
    if total == 0:
        return 0, 0, []
    print("\n=== %s: %d files, %d assertions ===" % (title, len(files), total))
    print("  answered correctly  %5d  (%.1f%%)" % (passed, 100.0 * passed / total))
    print("  answered wrongly    %5d" % wrong)
    print("  never run           %5d  (the engine refused the schema)" % unreachable)

    blocked = collections.Counter()
    for r in records:
        if r[0] == 'SKIP':
            blocked[r[4]] += int(r[3])
        elif r[0] == 'BAD':
            blocked['schema did not compile: ' + r[4]] += int(r[3])
    if blocked:
        print("\n  what is blocking them:")
        for keyword, n in blocked.most_common():
            print("    %5d  %s" % (n, keyword))

    wrongs = [r for r in records if r[0] == 'FAIL']
    if wrongs:
        print("\n  wrong answers:")
        for r in wrongs[:40]:
            print("    %s: %s / %s" % (r[1], r[2], r[3]))
            print("      expected %s, got %s%s"
                  % (r[4], r[5], (': ' + r[6]) if len(r) > 6 and r[6] else ''))
        if len(wrongs) > 40:
            print("    ... and %d more" % (len(wrongs) - 40))
    return total, passed, wrongs


total, passed, wrongs = report("required", required)
report("optional", optional)
report("optional/format", formats)

print("\nSuite commit: %s" % os.environ.get('JSS_COMMIT', '(unpinned)'))
if os.environ.get('JSS_REPORT'):
    with open(os.environ['JSS_REPORT'], 'w') as fh:
        for r in wrongs:
            fh.write('\t'.join(r) + '\n')
    print("wrong answers written to %s" % os.environ['JSS_REPORT'])

rate = 100.0 * passed / total if total else 0.0
floor = os.environ.get('JSS_MIN')
if floor:
    if rate + 1e-9 < float(floor):
        print("\nFAIL: required suite %.1f%% is below the JSS_MIN floor of %s%%"
              % (rate, floor))
        sys.exit(1)
    print("\nrequired suite %.1f%% meets the JSS_MIN floor of %s%%" % (rate, floor))
