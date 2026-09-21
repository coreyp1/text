"""The JSON fast path against the general parser, over yaml-test-suite.

gtext_yaml_parse() has two implementations. Input that is also JSON goes to
the JSON parser and is converted; everything else goes through the YAML
scanner. Two implementations of one contract drift, and this one had.

What makes it worth a target of its own is that `make conformance` cannot see
it. That runner asks for GTEXT_YAML_DUPKEY_KEEP_ALL, because the suite has
duplicate-key cases it must not collapse - and that is the one setting which
turns the fast path off. All 395 cases have only ever gone the other way.

This asks no expectation of either side, only that they agree, so it needs no
corpus of answers and covers every document the suite ships.
"""

import collections
import glob
import os
import subprocess
import sys

import yaml

SUITE, TOOL = sys.argv[1], sys.argv[2]
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


counts = collections.Counter()
failures = []

for path in sorted(glob.glob(SUITE + '/src/*.yaml')):
    tid = os.path.basename(path)[:-5]
    for idx, case in enumerate(yaml.safe_load(open(path))):
        raw = case.get('yaml')
        if raw is None:
            continue
        label = tid if idx == 0 else '%s/%d' % (tid, idx)
        out = subprocess.run([TOOL], input=unescape(raw).encode('utf-8'),
                             capture_output=True)
        text = out.stdout.decode('utf-8', 'replace').strip()
        if text.startswith('OK not-json'):
            counts['not-json'] += 1
        elif text.startswith('OK'):
            counts['agree'] += 1
        else:
            counts['differ'] += 1
            failures.append((label, case.get('name', ''), text))

shipped = sum(counts.values())
total = counts['agree'] + counts['differ']
print('=== the JSON fast path against the general parser, '
      'over yaml-test-suite ===\n')
print('  agree  %d' % counts['agree'])
if counts['differ']:
    print('  differ %d' % counts['differ'])
print('  -> %d of %d documents (%.1f%%)'
      % (counts['agree'], total, 100.0 * counts['agree'] / total if total else 0.0))
print('\n  %d of the suite\'s %d documents are not JSON and never reach the'
      % (counts['not-json'], shipped))
print('  fast path at all.  Read the figure above as a statement about %d'
      % total)
print('  documents, and remember that a corpus of YAML is a thin corpus of')
print('  JSON: tests/yaml/test-yaml-json-fastpath.cpp is what actually holds')
print('  the two paths together, because the shapes they disagreed on -')
print('  ["0x10"], [""], {"": ""} - appear nowhere in this suite.')

if failures:
    print('\n=== documents the two paths read differently ===')
    for label, name, text in failures:
        print('  %-10s %-36s' % (label, name[:36]))
        if VERBOSE:
            for line in text.split('\n'):
                print('             %s' % line)
            VERBOSE -= 1

floor = os.environ.get('YTS_FP_MIN')
if floor:
    pct = 100.0 * counts['agree'] / total if total else 0.0
    if pct + 0.05 < float(floor):
        print('\nfast path: %.1f%% agreement is below the floor of %s%%'
              % (pct, floor))
        sys.exit(1)
    print('\nfast path: %.1f%% agreement meets the floor of %s%%' % (pct, floor))
