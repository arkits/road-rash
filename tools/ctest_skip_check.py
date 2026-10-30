#!/usr/bin/env python3
"""Plan 33b Task 7: fail when CTest skipped a test for an unexpected reason.

CI has no disc and no local oracle captures, so tests labelled `disc` or
`fixture` exit 77 there; `posix` tests skip on Windows. Any other skip means
a test silently stopped running, which this check turns into a failure.
  tools/ctest_skip_check.py --build-dir build-windows-msvc -C Debug --junit ctest.xml
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('-C', '--config', help='multi-config build configuration')
    p.add_argument('--junit', type=Path, required=True, help='CTest --output-junit file')
    p.add_argument('--allow', default='disc,fixture,posix', help='comma-separated labels that may skip')
    args = p.parse_args()

    command = ['ctest', '--test-dir', str(args.build_dir), '--show-only=json-v1']
    if args.config:
        command += ['-C', args.config]
    listing = json.loads(subprocess.check_output(command, text=True))
    labels = {}
    for test in listing['tests']:
        values = next((x['value'] for x in test.get('properties', []) if x['name'] == 'LABELS'), [])
        labels[test['name']] = set(values)

    cases = list(ET.parse(args.junit).getroot().iter('testcase'))
    if not cases:
        raise SystemExit(f'FAIL: {args.junit} lists no tests')
    allowed = set(args.allow.split(','))
    skipped = sorted(c.get('name') for c in cases if c.find('skipped') is not None)
    unexpected = [name for name in skipped if not labels.get(name, set()) & allowed]
    by_label = {label: sum(1 for name in skipped if label in labels.get(name, set())) for label in sorted(allowed)}
    print(f'{len(cases)} tests, {len(skipped)} skipped ({", ".join(f"{k} {v}" for k, v in by_label.items())})')
    if unexpected:
        for name in unexpected:
            print(f'FAIL: {name} skipped but is labelled {sorted(labels.get(name, set())) or "nothing"}')
        sys.exit(1)
    print('PASS: every skip is an expected disc, fixture or host-specific test')


if __name__ == '__main__':
    main()
