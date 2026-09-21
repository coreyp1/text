"""Round-trip every yaml-test-suite document this parser accepts.

    parse(input) -> write() -> parse(written)

and ask whether the two agree, twice over:

    by value   the same JSON.  A failure here is data lost or corrupted.
    by event   the same composed event stream.  Strictly harder: it also
               asks that anchors, tags and the text of every scalar survive.

The gap between the two is the writer choosing its own spelling, which is
documented and deliberate - a parse-write cycle is semantically faithful, not
textually faithful.  The first number is the one that has to be 100%.

Nothing in yaml-test-suite tests a writer; the suite is a corpus of inputs.
Running it backwards through this library costs nothing and measures the half
of the module the suite cannot see.

Usage:  yaml_roundtrip.py <suite-dir> <yts-runner> <yts-roundtrip> [-v N]
"""
import collections
import glob
import os
import subprocess
import sys

import yaml

SUITE, RUNNER, ROUNDTRIP = sys.argv[1], sys.argv[2], sys.argv[3]
VERBOSE = 0
if '-v' in sys.argv:
    i = sys.argv.index('-v')
    VERBOSE = int(sys.argv[i + 1]) if len(sys.argv) > i + 1 else 5


def unescape(s):
    """The suite writes invisible characters as glyphs."""
    return (s.replace('␣', ' ')
             .replace('———»', '\t').replace('——»', '\t')
             .replace('—»', '\t').replace('»', '\t')
             .replace('∎', '').replace('←', '\r').replace('⇔', '﻿'))


def run(cmd, src):
    out = subprocess.run(cmd, input=src, capture_output=True)
    return out.stdout.decode('utf-8', 'replace'), out.returncode


by_value = collections.Counter()
by_event = collections.Counter()
failures = []

for path in sorted(glob.glob(SUITE + '/src/*.yaml')):
    tid = os.path.basename(path)[:-5]
    for idx, case in enumerate(yaml.safe_load(open(path))):
        raw = case.get('yaml')
        if raw is None:
            continue
        label = tid if idx == 0 else '%s/%d' % (tid, idx)
        name = case.get('name', '')
        src = unescape(raw).encode('utf-8')

        before, _ = run([RUNNER], src)
        if before.startswith('FAIL'):
            # Refused by design; there is nothing to write back out.
            by_value['skip-refused'] += 1
            by_event['skip-refused'] += 1
            continue

        written, rc = run([ROUNDTRIP, '-w'], src)
        if rc != 0 or written.startswith('FAIL'):
            by_value['write-failed'] += 1
            failures.append((label, name, 'the writer refused it',
                             written.strip()[:70], ''))
        else:
            after, _ = run([RUNNER], written.encode('utf-8'))
            if after.startswith('FAIL'):
                by_value['reparse-failed'] += 1
                failures.append((label, name, 'the output does not parse',
                                 written.strip()[:70], after.strip()[:70]))
            elif after.strip() != before.strip():
                by_value['value-changed'] += 1
                failures.append((label, name, 'the value changed',
                                 before.strip()[:70], after.strip()[:70]))
            else:
                by_value['ok'] += 1

        events, _ = run([ROUNDTRIP], src)
        head = events.split('\n', 1)[0]
        by_event['ok' if head == 'OK' else head.split(':')[0]] += 1

print('=== round trip: parse -> write -> parse, over yaml-test-suite ===')
for title, counter in (('by value (was any data lost?)', by_value),
                       ('by event (did the spelling survive too?)', by_event)):
    attempted = sum(counter.values()) - counter['skip-refused']
    print('\n  %s' % title)
    for k in sorted(counter):
        if k != 'skip-refused':
            print('    %-16s %d' % (k, counter[k]))
    pct = 100.0 * counter['ok'] / attempted if attempted else 0.0
    print('    -> %d of %d documents (%.1f%%)' % (counter['ok'], attempted, pct))

print('\n%d of the suite\'s documents are refused by design and not attempted.'
      % by_value['skip-refused'])

if failures:
    print('\n=== documents whose value did not survive ===')
    for lab, nm, why, a, b in failures:
        print('  %-10s %-36s %s' % (lab, nm[:36], why))
        if VERBOSE:
            if a:
                print('               %s' % a)
            if b:
                print('               %s' % b)
            VERBOSE -= 1

floor = os.environ.get('YTS_RT_MIN')
if floor:
    attempted = sum(by_value.values()) - by_value['skip-refused']
    pct = 100.0 * by_value['ok'] / attempted if attempted else 0.0
    if pct + 0.05 < float(floor):
        print('round trip: %.1f%% by value is below the floor of %s%%'
              % (pct, floor))
        sys.exit(1)
    print('round trip: %.1f%% by value meets the floor of %s%%' % (pct, floor))
