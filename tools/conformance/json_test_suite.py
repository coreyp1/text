"""Score a JSON parser against JSONTestSuite.

Usage: json_test_suite.py <suite-dir> <runner-command...>

The runner reads one document on stdin and prints ACCEPT or REJECT. The
suite's test_parsing directory names each case by what a parser must do with
it: y_ must be accepted, n_ must be refused, i_ is left to the implementation
and is reported but not scored.

Duplicate object names are the one place where the suite's y_ classification
is a policy rather than a grammar rule. RFC 8259 says names SHOULD be unique
and leaves the behaviour unspecified when they are not, so refusing them is a
defensible default and is this library's. The score is reported both ways
rather than quietly relaxed: the headline is the default, and the second
number says what the grammar alone gives.
"""
import glob, os, subprocess, sys

SUITE = sys.argv[1]
RUNNER = sys.argv[2:]
if not RUNNER:
    sys.exit("usage: json_test_suite.py <suite-dir> <runner-command...>")

cases = sorted(glob.glob(os.path.join(SUITE, 'test_parsing', '*.json')))
if not cases:
    sys.exit("no test_parsing cases under %s" % SUITE)


def run(path, last_wins):
    env = dict(os.environ)
    if last_wins:
        env['JTS_DUPKEY_LAST_WINS'] = '1'
    else:
        env.pop('JTS_DUPKEY_LAST_WINS', None)
    with open(path, 'rb') as fh:
        data = fh.read()
    try:
        out = subprocess.run(RUNNER, input=data, capture_output=True,
                             env=env, timeout=20).stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        return None, 'timed out'
    return out.startswith('ACCEPT'), out.strip()[:70]


def score(last_wins):
    passed = 0
    checked = 0
    wrong = []
    for path in cases:
        name = os.path.basename(path)
        kind = name[0]
        if kind not in 'yn':
            continue
        checked += 1
        accepted, detail = run(path, last_wins)
        want = (kind == 'y')
        if accepted == want:
            passed += 1
        else:
            wrong.append((name, 'accepted' if accepted else detail))
    return checked, passed, wrong


checked, passed, wrong = score(False)
_, relaxed_passed, relaxed_wrong = score(True)

undecided = [os.path.basename(p) for p in cases
             if os.path.basename(p)[0] == 'i']
accepted_i = sum(1 for p in cases
                 if os.path.basename(p)[0] == 'i' and run(p, False)[0])

print("=== JSONTestSuite: test_parsing ===")
print("  must accept (y_)   %d" % sum(1 for p in cases if os.path.basename(p)[0] == 'y'))
print("  must refuse (n_)   %d" % sum(1 for p in cases if os.path.basename(p)[0] == 'n'))
print("  implementation-defined (i_)  %d, of which %d accepted"
      % (len(undecided), accepted_i))
print("\nchecked %d, passed %d  (%.1f%%)"
      % (checked, passed, 100.0 * passed / checked))
if relaxed_passed != passed:
    print("with dupkeys=LAST_WINS: %d  (%.1f%%)  - the difference is the "
          "duplicate-name policy, not the grammar"
          % (relaxed_passed, 100.0 * relaxed_passed / checked))

for name, why in wrong:
    print("  %-46s %s" % (name[:46], why))

report = os.environ.get('JTS_REPORT')
if report:
    with open(report, 'w') as fh:
        for name, why in wrong:
            fh.write("%-50s %s\n" % (name, why))
    print("failures written to %s" % report)

floor = os.environ.get('JTS_MIN')
if floor:
    pct = 100.0 * passed / checked
    if pct + 0.05 < float(floor):
        print("conformance: %.1f%% is below the floor of %s%%" % (pct, floor))
        sys.exit(1)
    print("conformance: %.1f%% meets the floor of %s%%" % (pct, floor))
