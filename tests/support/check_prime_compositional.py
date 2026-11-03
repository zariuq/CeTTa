#!/usr/bin/env python3
"""Exact source regressions for the compositional-foundation experiment."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
CASES = [
    'need_equation_choice_sharing', 'need_lambda_argument_shared',
    'need_stuck_application_argument', 'need_sharing_boundary',
    'need_lexical_scope', 'need_closure_capture', 'need_quote_preservation',
    'need_quoted_part_values', 'data_holding', 'let_source_answers',
    'computed_argument_choicepoints', 'binder_display_names',
    'routes/eval_returned_values',
    'scope_spectrum/scope_spectrum_lexical_fresh',
    'scope_spectrum/scope_spectrum_explicit_capture',
]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def instances(text):
    """The existing fixture observation: retain aliases, renumber per line."""
    lines = []
    for line in text.splitlines():
        names = {}
        def replace(match):
            identity = match.group(2)
            if identity not in names: names[identity] = len(names) + 1
            return match.group(1) + '#' + str(names[identity])
        lines.append(re.sub(r'(\$[^\s()#]*)#(\d+)', replace, line))
    return '\n'.join(lines)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--timeout', type=float)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    cases = [(ROOT/'tests/prime'/name).with_suffix('.metta') for name in CASES]
    cases += sorted((ROOT/'tests/prime/interaction').glob('*.metta'))
    rows = []
    for source in cases:
        expected = source.with_suffix('.expected')
        expected_text = expected.read_text()
        started = time.monotonic()
        try:
            result = subprocess.run([str(binary), '--lang', 'prime', str(source)],
                cwd=ROOT, capture_output=True, text=True, timeout=args.timeout)
            passed = result.returncode == 0 and not result.stderr and instances(result.stdout) == instances(expected_text)
            observation = {'exit_code':result.returncode, 'stdout':result.stdout, 'stderr':result.stderr}
        except subprocess.TimeoutExpired:
            passed, observation = False, {'incomplete':'timeout'}
        row = {'case':str(source.relative_to(ROOT)), 'passed':passed,
               'seconds':round(time.monotonic()-started,6),
               'source_sha256':sha(source), 'expected_sha256':sha(expected), **observation}
        rows.append(row)
        print(('PASS' if passed else 'FAIL')+': '+row['case'], flush=True)
    receipt = {'binary_sha256':sha(binary), 'cases':rows,
               'passed':all(row['passed'] for row in rows)}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2)+'\n')
    print(f"Compositional source cases: {sum(r['passed'] for r in rows)}/{len(rows)}")
    return 0 if receipt['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
