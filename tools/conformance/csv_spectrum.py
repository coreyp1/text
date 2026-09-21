"""Score a CSV parser against csv-spectrum.

Usage: csv_spectrum.py <suite-dir> <runner-command...>

The suite pairs each csvs/<name>.csv with json/<name>.json holding an array of
objects, one per data row, keyed by the header row. The runner reads a CSV on
stdin and prints that shape, or a line beginning with FAIL if it refuses the
input; the comparison is by value, so formatting and key order do not matter.
"""
import glob, json, os, subprocess, sys

SUITE = sys.argv[1]
RUNNER = sys.argv[2:]
if not RUNNER:
    sys.exit("usage: csv_spectrum.py <suite-dir> <runner-command...>")

cases = sorted(glob.glob(os.path.join(SUITE, 'csvs', '*.csv')))
if not cases:
    sys.exit("no csvs/ cases under %s" % SUITE)

unusable = []
no_fixture = []


def score(permissive):
  passed = 0
  failures = []
  del unusable[:]
  del no_fixture[:]
  env = dict(os.environ)
  if permissive:
      env['CSS_ALLOW_UNQUOTED_QUOTES'] = '1'
  else:
      env.pop('CSS_ALLOW_UNQUOTED_QUOTES', None)
  for path in cases:
    name = os.path.basename(path)[:-4]
    expected_path = os.path.join(SUITE, 'json', name + '.json')
    if not os.path.exists(expected_path):
        # Counted and named.  A case whose expectation is simply missing used
        # to vanish here without leaving a trace in the output, which is the
        # same hole as scoring a corpus you never finished reading.
        no_fixture.append(name)
        continue
    with open(expected_path, encoding='utf-8') as fh:
        want = json.load(fh)
    # The suite's own shape is an array of objects, one per data row.
    # location_coordinates.json is a bare object and its phone number is not
    # the one in its csv - 1234567890 against 2095257564 - so the fixture
    # does not describe its own input and no parser can match it. Report it
    # rather than counting it against the parser.
    if not isinstance(want, list):
        unusable.append(name)
        continue
    with open(path, 'rb') as fh:
        data = fh.read()
    try:
        out = subprocess.run(RUNNER, input=data, capture_output=True,
                             env=env, timeout=20).stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        failures.append((name, 'timed out', ''))
        continue
    if out.startswith('FAIL'):
        failures.append((name, out.strip()[:70], ''))
        continue
    try:
        got = json.loads(out)
    except Exception:
        failures.append((name, 'emitted invalid JSON', out.strip()[:70]))
        continue
    if got == want:
        passed += 1
    else:
        failures.append((name, json.dumps(want)[:70], json.dumps(got)[:70]))
  return passed, failures


passed, failures = score(False)
relaxed_passed, _ = score(True)
checked = passed + len(failures)
total = len(cases)
excluded = len(unusable) + len(no_fixture)
print("=== csv-spectrum ===")
# Two denominators.  The pass rate is over the cases that can be judged; the
# corpus line says how many that was out of what the suite ships, so a rate
# computed over a shrinking corpus cannot read as a rising score.
print("checked %d of %d cases (%.1f%% of the corpus)"
      % (checked, total, 100.0 * checked / total if total else 0.0))
print("passed  %d  (%.1f%% of checked, %.1f%% of the corpus)"
      % (passed, 100.0 * passed / checked if checked else 0.0,
         100.0 * passed / total if total else 0.0))
if relaxed_passed != passed:
    print("with dialect.allow_unquoted_quotes: %d  (%.1f%%)  - the difference "
          "is a dialect, not the grammar"
          % (relaxed_passed, 100.0 * relaxed_passed / checked))
for name, want, got in failures:
    print("  %-28s %s" % (name, want))
    if got:
        print("  %-28s got: %s" % ("", got))
for name in unusable:
    print("  %-28s excluded: the suite's fixture is not an array of rows and "
          "does not match its own csv" % name)
for name in no_fixture:
    print("  %-28s excluded: the suite ships no json/ expectation for it" % name)
if excluded:
    print("%d of %d cases were not asked; quote the %.1f%% only with that count"
          % (excluded, total,
             100.0 * passed / checked if checked else 0.0))

corpus_floor = os.environ.get('CSS_MIN_CORPUS')
if corpus_floor and total:
    covered = 100.0 * checked / total
    if covered + 0.05 < float(corpus_floor):
        print("conformance: only %.1f%% of the corpus was checked, below the "
              "floor of %s%%" % (covered, corpus_floor))
        sys.exit(1)
    print("conformance: %.1f%% of the corpus checked, meeting the floor of %s%%"
          % (covered, corpus_floor))

floor = os.environ.get('CSS_MIN')
if floor and checked:
    pct = 100.0 * passed / checked
    if pct + 0.05 < float(floor):
        print("conformance: %.1f%% is below the floor of %s%%" % (pct, floor))
        sys.exit(1)
    print("conformance: %.1f%% meets the floor of %s%%" % (pct, floor))
