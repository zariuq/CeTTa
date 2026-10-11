#!/usr/bin/env python3
"""Check configuration ownership of internal helper executable names."""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--makefile', type=Path)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    makefile = (args.makefile or root / 'Makefile').resolve()
    source = makefile.read_text()
    names = sorted(set(re.findall(
        r'^([A-Z0-9_]+_BIN)\s*=\s*runtime/[^\n]*\$\(BUILD_(?:CANON|OBJ_TAG)\)',
        source, re.M)) - {'PUBLIC_BIN', 'PETTA_TYPECHECK_V2_OMISSION_BIN'})
    assert 'PARALLEL_EXECUTOR_LIFECYCLE_TEST_BIN' in names
    # The omission probe explicitly launches a normal, statistics-free child.
    # Statistics-only identity changes outside these two regression targets are
    # not part of this sanitizer/receipt/capture profile comparison.
    fragment = '.PHONY: helper-config-observe\nhelper-config-observe:\n\t@:\n'
    fragment += '$(info PublicArtifact PUBLIC_BIN=$(PUBLIC_BIN))\n'
    fragment += ''.join('$(info HelperArtifact ' + name + '=$(' + name + '))\n'
                        for name in names)
    base = dict(BUILD='python', CETTA_TEST_ISOLATED='1', GSLT_SOURCE_LEAN_ONLY_V1='1', ENABLE_SANITIZERS='0',
                SANITIZERS='address,undefined', ENABLE_RUNTIME_STATS='0',
                ENABLE_PRIME_CAUSAL_RECEIPTS='0', ENABLE_PRIME_NEED_CLOSURE_CAPTURE='0')
    profiles = [
        ('ordinary', {}),
        ('asan', dict(ENABLE_SANITIZERS='1')),
        ('tsan', dict(ENABLE_SANITIZERS='1', SANITIZERS='thread')),
        ('stats', dict(ENABLE_RUNTIME_STATS='1')),
        ('receipts', dict(ENABLE_PRIME_CAUSAL_RECEIPTS='1')),
        ('capture', dict(ENABLE_PRIME_CAUSAL_RECEIPTS='1', ENABLE_PRIME_NEED_CLOSURE_CAPTURE='1')),
        ('combined', dict(ENABLE_SANITIZERS='1', ENABLE_RUNTIME_STATS='1',
                         ENABLE_PRIME_CAUSAL_RECEIPTS='1', ENABLE_PRIME_NEED_CLOSURE_CAPTURE='1')),
        ('ordinary-return', {}),
    ]
    env = dict(os.environ)
    for key in ['MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'MAKELEVEL']:
        env.pop(key, None)
    records = []
    failures = []
    for profile, changes in profiles:
        values = base | changes
        command = ['make', '-s', '--no-print-directory', '-f', str(makefile), '-f', '-',
                   'helper-config-observe', *[key + '=' + value for key, value in sorted(values.items())]]
        result = subprocess.run(command, cwd=root, env=env, input=fragment,
                                capture_output=True, text=True, timeout=60)
        artifacts = dict(re.findall(r'^HelperArtifact ([A-Z0-9_]+)=(.*)$', result.stdout, re.M))
        public = dict(re.findall(r'^PublicArtifact ([A-Z0-9_]+)=(.*)$', result.stdout, re.M))
        expected_public = 'runtime/cetta-python-runtime-stats' if values['ENABLE_RUNTIME_STATS'] == '1' else 'cetta'
        if public.get('PUBLIC_BIN') != expected_public:
            failures.append(profile + ': public runtime alias changed')
        records.append(dict(profile=profile, command=command, exit=result.returncode, public=public,
                            artifacts=artifacts, stderr=result.stderr))
        if result.returncode or set(artifacts) != set(names):
            failures.append(profile + ': helper metadata query failed')
    if not failures:
        for name in names:
            seen = {}
            for record in (r for r in records[:-1] if r['profile'] != 'stats'):
                artifact = record['artifacts'][name]
                if artifact in seen:
                    failures.append(name + ': ' + seen[artifact] + ' aliases ' + record['profile'])
                else:
                    seen[artifact] = record['profile']
            if name in ('PETTA_ANALYSIS_CARDINALITY_TEST_BIN', 'PARALLEL_EXECUTOR_LIFECYCLE_TEST_BIN') and records[0]['artifacts'][name] == records[3]['artifacts'][name]:
                failures.append(name + ': ordinary aliases stats')
            if records[0]['artifacts'][name] != records[-1]['artifacts'][name]:
                failures.append(name + ': ordinary return changed its artifact identity')
    receipt = dict(makefile=str(makefile), makefile_sha256=hashlib.sha256(makefile.read_bytes()).hexdigest(),
                   helper_count=len(names), stats_scope='cardinality and executor; other stats-only identities unchanged', profiles=records, failures=failures, passed=not failures)
    if args.receipt:
        assert not args.receipt.exists()
        args.receipt.write_text(json.dumps(receipt, indent=2) + '\n')
    if failures:
        for failure in failures[:24]:
            print('FAIL:', failure)
        print(f'FAIL: {len(failures)} helper profile collisions or invalid metadata queries')
        return 1
    print(f'PASS: {len(names)} internal helpers retain distinct sanitizer/receipt/capture profiles, exact ordinary return, and stable public aliases')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
