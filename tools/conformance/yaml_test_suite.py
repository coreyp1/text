import yaml, glob, json, subprocess, os, sys, collections, re

"""Score a YAML implementation against yaml-test-suite.

Usage: yaml_test_suite.py <suite-dir> <runner-command...>

The runner reads a YAML stream on stdin and writes one JSON document per line,
or a line beginning with "FAIL" if it refuses the input. tools/conformance/
carries one for this library; the reference implementations are driven through
the same interface so the scores are comparable.

Of the suite's cases, those carrying a "json" field are checked by value and
those marked "fail" by refusal. The rest assert an event stream, which this
harness does not emit, and are skipped and counted.
"""
SUITE = sys.argv[1]
RUNNER = sys.argv[2:]
if not RUNNER:
    sys.exit("usage: yaml_test_suite.py <suite-dir> <runner-command...>")
which = os.path.basename(RUNNER[-1])
env = dict(os.environ)

def unescape(src):
    """The suite writes characters that are hard to read as marks (ReadMe.md,
    "Special Characters"). A hard tab is a guillemet padded with em-dashes to
    four columns."""
    # A tab is padded with em-dashes out to the next four-column stop, so
    # the count depends on the column and the ReadMe's list of four forms is
    # not exhaustive: 3RLN uses five.
    src = re.sub('\u2014*\u00bb', '\t', src)
    src = src.replace('␣', ' ')        # trailing space
    src = src.replace('↵', '')         # trailing newline, shown explicitly
    src = src.replace('←', '\r')       # carriage return
    src = src.replace('⇔', '﻿')   # byte order mark
    src = src.replace('∎\n', '')       # no final newline
    src = src.replace('∎', '')
    return src

def run(src):
    try:
        r = subprocess.run(RUNNER, input=src.encode(), capture_output=True,
                           env=env, timeout=10)
    except subprocess.TimeoutExpired:
        return None, 'TIMEOUT'
    if r.returncode != 0:
        return None, 'CRASH rc=%d %s' % (r.returncode, r.stderr.decode()[:100])
    return r.stdout.decode('utf-8', 'replace'), None

def expected_docs(js):
    dec = json.JSONDecoder(); out = []; i = 0
    while i < len(js):
        while i < len(js) and js[i] in ' \t\r\n': i += 1
        if i >= len(js): break
        v, i = dec.raw_decode(js, i)
        out.append(v)
    return out

results = collections.Counter(); failures = []
for f in sorted(glob.glob(SUITE + '/src/*.yaml')):
    tid = os.path.basename(f)[:-5]
    cases = yaml.safe_load(open(f))
    for idx, case in enumerate(cases):
        label = tid if len(cases) == 1 else '%s/%d' % (tid, idx)
        name = case.get('name', '')
        if case.get('yaml') is None:
            results['skip-no-yaml'] += 1; continue
        src = unescape(case['yaml'])
        out, errkind = run(src)
        if errkind:
            results['crash'] += 1
            failures.append((label, name, errkind, '', '')); continue
        rejected = out.startswith('FAIL')

        if case.get('fail'):
            if rejected: results['fail-ok'] += 1
            else:
                results['fail-missed'] += 1
                failures.append((label, name, 'should be rejected', '', out.strip()[:90]))
            continue
        if 'json' not in case:
            results['skip-tree-only'] += 1; continue
        # Decode the expectation before judging the answer. Three cases carry
        # an explicit null here, and checking `rejected` first scored those as
        # defects when an implementation refused them and skipped them when it
        # did not - a case with no expectation cannot be a failure either way.
        try: want = expected_docs(case['json'])
        except Exception:
            results['skip-bad-expect'] += 1; continue
        if rejected:
            results['json-rejected'] += 1
            failures.append((label, name, 'rejected a valid document', '', out.strip()[:90]))
            continue
        got = []; bad = False
        for line in out.splitlines():
            if not line.strip(): continue
            try: got.append(json.loads(line))
            except Exception: bad = True
        if bad:
            results['json-unparsable'] += 1
            failures.append((label, name, 'emitted invalid JSON', '', out.strip()[:90]))
        elif got == want:
            results['json-ok'] += 1
        else:
            results['json-mismatch'] += 1
            failures.append((label, name, 'value mismatch',
                             json.dumps(want)[:85], json.dumps(got)[:85]))

checked = sum(v for k, v in results.items() if not k.startswith('skip'))
passed = results['fail-ok'] + results['json-ok']
print("=== yaml-test-suite: %s ===" % which)
for k, v in sorted(results.items()): print("  %-18s %d" % (k, v))
print("\nchecked %d, passed %d  (%.1f%%)" % (checked, passed, 100.0 * passed / checked))
report = os.environ.get('YTS_REPORT')
if report:
  with open(report, 'w') as fh:
    for lab, nm, why, want, got in failures:
        fh.write("%-10s %-50s %s\n" % (lab, nm[:50], why))
        if want: fh.write("           want: %s\n" % want)
        if got:  fh.write("           got : %s\n" % got)
  print("failures written to %s" % report)

floor = os.environ.get('YTS_MIN')
if floor:
    pct = 100.0 * passed / checked
    if pct + 0.05 < float(floor):
        print("conformance: %.1f%% is below the floor of %s%%" % (pct, floor))
        sys.exit(1)
    print("conformance: %.1f%% meets the floor of %s%%" % (pct, floor))
