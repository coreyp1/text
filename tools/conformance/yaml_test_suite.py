import yaml, glob, json, subprocess, os, sys, collections, re

"""Score a YAML implementation against yaml-test-suite.

Usage: yaml_test_suite.py <suite-dir> <runner-command...>

The runner reads a YAML stream on stdin and writes one JSON document per line,
or a line beginning with "FAIL" if it refuses the input. tools/conformance/
carries one for this library; the reference implementations are driven through
the same interface so the scores are comparable.

A case is checked against every expectation it carries, and passes only if it
answers all of them:

- "fail" says the input must be refused.
- "json" gives the value, which the runner above is compared against.
- "tree" gives the event stream.  Set YTS_EVENTS to a command that reads a
  YAML stream on stdin and writes the suite's event notation, and those are
  checked too; without it they are skipped and counted.  The reference
  implementations are driven through the JSON interface only, so a comparison
  run leaves YTS_EVENTS unset and scores fewer cases - which the corpus line
  says out loud.

Nine cases carry no expectation of any kind.  They are counted and skipped;
there is nothing to be right about.
"""
SUITE = sys.argv[1]
RUNNER = sys.argv[2:]
if not RUNNER:
    sys.exit("usage: yaml_test_suite.py <suite-dir> <runner-command...>")
which = os.path.basename(RUNNER[-1])
env = dict(os.environ)
EVENTS = os.environ.get('YTS_EVENTS')

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

def run(cmd, src):
    try:
        r = subprocess.run(cmd, input=src.encode(), capture_output=True,
                           env=env, timeout=10)
    except subprocess.TimeoutExpired:
        return None, 'TIMEOUT'
    if r.returncode != 0:
        return None, 'CRASH rc=%d %s' % (r.returncode, r.stderr.decode()[:100])
    return r.stdout.decode('utf-8', 'replace'), None


def event_lines(text):
    """One event per line, with the indentation the suite adds for reading.

    Every event begins with "+", "-" or "=", and a scalar's own leading spaces
    come after the style character, so stripping the indent cannot eat any of
    the value."""
    return [l.lstrip(' ') for l in text.splitlines() if l.strip()]

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
        out, errkind = run(RUNNER, src)
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

        # A case is judged on every expectation it carries.  `verdict` holds
        # the first thing that went wrong, and stays None while nothing has;
        # `asked` says whether anything could be judged at all.
        verdict = None
        asked = False

        if 'json' in case:
            # Decode the expectation before judging the answer. Three cases
            # carry an explicit null here, and checking `rejected` first
            # scored those as defects when an implementation refused them and
            # skipped them when it did not - a case with no expectation cannot
            # be a failure either way.
            try: want = expected_docs(case['json'])
            except Exception:
                # The expectation itself does not parse, so the case cannot be
                # judged on it.  Terminal, so that every case lands in exactly
                # one bucket and the buckets still sum to the corpus.
                results['skip-bad-expect'] += 1; continue
            asked = True
            if rejected:
                verdict = 'json-rejected'
                failures.append((label, name, 'rejected a valid document',
                                 '', out.strip()[:90]))
            else:
                got = []; bad = False
                for line in out.splitlines():
                    if not line.strip(): continue
                    try: got.append(json.loads(line))
                    except Exception: bad = True
                if bad:
                    verdict = 'json-unparsable'
                    failures.append((label, name, 'emitted invalid JSON',
                                     '', out.strip()[:90]))
                elif got != want:
                    verdict = 'json-mismatch'
                    failures.append((label, name, 'value mismatch',
                                     json.dumps(want)[:85],
                                     json.dumps(got)[:85]))

        if 'tree' in case and EVENTS:
            asked = True
            ev_out, ev_err = run([EVENTS], src)
            if ev_err:
                if verdict is None:
                    verdict = 'crash'
                    failures.append((label, name, ev_err, '', ''))
            elif ev_out.startswith('FAIL'):
                if verdict is None:
                    verdict = 'tree-rejected'
                    failures.append((label, name, 'rejected a valid document',
                                     '', ev_out.strip()[:90]))
            else:
                want_ev = event_lines(unescape(case['tree']))
                got_ev = event_lines(ev_out)
                if got_ev != want_ev and verdict is None:
                    k = 0
                    while (k < min(len(got_ev), len(want_ev))
                           and got_ev[k] == want_ev[k]):
                        k += 1
                    verdict = 'tree-mismatch'
                    failures.append((label, name, 'event %d differs' % k,
                                     (want_ev[k:k + 1] or ['(nothing)'])[0][:85],
                                     (got_ev[k:k + 1] or ['(nothing)'])[0][:85]))
        elif 'tree' in case and 'json' not in case:
            results['skip-tree-only'] += 1; continue

        if not asked:
            # Neither a value nor an event stream, and not marked "fail".
            # The suite asserts nothing here; nothing can be got wrong.
            results['skip-no-expect'] += 1
        else:
            results[verdict or 'ok'] += 1

# Two denominators, both printed, because they answer different questions and
# only one of them is the corpus.
#
# `checked` is the cases this harness can judge: it drives an implementation
# through a JSON-per-line interface, so a case that asserts an event stream
# has nothing to compare against.  `total` is every case the suite ships.
# Reporting only the first turns "100.0%" into a claim about a corpus that was
# never fully asked, which is the failure the JSON Schema harness already
# guards against by keeping refused schemas in its denominator.
#
# The skips here are not the engine narrowing - an implementation that started
# refusing a checkable case would score it `json-rejected`, a failure - so the
# rate over `checked` cannot be gamed by giving up on input.  What it can do
# is quietly shrink: a parse error in an expectation moves a case to
# `skip-bad-expect` and nobody sees the corpus get smaller.  YTS_MIN_CORPUS
# exists to make that a build failure rather than a rounding difference.
checked = sum(v for k, v in results.items() if not k.startswith('skip'))
total = sum(results.values())
skipped = total - checked
passed = results['fail-ok'] + results['ok']
print("=== yaml-test-suite: %s ===" % which)
for k, v in sorted(results.items()): print("  %-18s %d" % (k, v))
pct_checked = 100.0 * passed / checked if checked else 0.0
pct_corpus = 100.0 * passed / total if total else 0.0
covered = 100.0 * checked / total if total else 0.0
print("\nchecked %d of %d cases (%.1f%% of the corpus)" % (checked, total, covered))
print("passed  %d  (%.1f%% of checked, %.1f%% of the corpus)"
      % (passed, pct_checked, pct_corpus))
if skipped:
    print("%d case(s) were not asked; the %.1f%% figure is the one to quote "
          "only alongside that count" % (skipped, pct_checked))
report = os.environ.get('YTS_REPORT')
if report:
  with open(report, 'w') as fh:
    for lab, nm, why, want, got in failures:
        fh.write("%-10s %-50s %s\n" % (lab, nm[:50], why))
        if want: fh.write("           want: %s\n" % want)
        if got:  fh.write("           got : %s\n" % got)
  print("failures written to %s" % report)

corpus_floor = os.environ.get('YTS_MIN_CORPUS')
if corpus_floor:
    if covered + 0.05 < float(corpus_floor):
        print("conformance: only %.1f%% of the corpus was checked, below the "
              "floor of %s%%" % (covered, corpus_floor))
        sys.exit(1)
    print("conformance: %.1f%% of the corpus checked, meeting the floor of %s%%"
          % (covered, corpus_floor))

floor = os.environ.get('YTS_MIN')
if floor:
    pct = pct_checked
    if pct + 0.05 < float(floor):
        print("conformance: %.1f%% is below the floor of %s%%" % (pct, floor))
        sys.exit(1)
    print("conformance: %.1f%% meets the floor of %s%%" % (pct, floor))
