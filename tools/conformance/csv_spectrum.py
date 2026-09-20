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


def score(permissive):
  passed = 0
  failures = []
  del unusable[:]
  env = dict(os.environ)
  if permissive:
      env['CSS_ALLOW_UNQUOTED_QUOTES'] = '1'
  else:
      env.pop('CSS_ALLOW_UNQUOTED_QUOTES', None)
  for path in cases:
    name = os.path.basename(path)[:-4]
    expected_path = os.path.join(SUITE, 'json', name + '.json')
    if not os.path.exists(expected_path):
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
print("=== csv-spectrum ===")
print("checked %d, passed %d  (%.1f%%)"
      % (checked, passed, 100.0 * passed / checked if checked else 0.0))
if relaxed_passed != passed:
    print("with dialect.allow_unquoted_quotes: %d  (%.1f%%)  - the difference "
          "is a dialect, not the grammar"
          % (relaxed_passed, 100.0 * relaxed_passed / checked))
for name, want, got in failures:
    print("  %-28s %s" % (name, want))
    if got:
        print("  %-28s got: %s" % ("", got))
for name in unusable:
    print("  %-28s skipped: the suite's fixture is not an array of rows and "
          "does not match its own csv" % name)

floor = os.environ.get('CSS_MIN')
if floor and checked:
    pct = 100.0 * passed / checked
    if pct + 0.05 < float(floor):
        print("conformance: %.1f%% is below the floor of %s%%" % (pct, floor))
        sys.exit(1)
    print("conformance: %.1f%% meets the floor of %s%%" % (pct, floor))
