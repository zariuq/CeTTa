#!/usr/bin/env python3
"""Compile the actual inline charge guard and discriminating control mutations."""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--out-dir', type=Path, required=True)
    args = parser.parse_args()
    source = ROOT / 'src/native_cost.h'
    control = ROOT / 'tests/test_native_cost_guard.c'
    header = source.read_text()
    body = ('return cetta_native_cost_active()\n'
            '        ? cetta_native_cost_charge_observed(kind, units) : 0u;')
    assert header.count(body) == 1, 'charge guard extraction changed'
    replacements = {
        'original': body,
        'unconditional': 'return cetta_native_cost_charge_observed(kind, units);',
        'never-observe': '(void)kind; (void)units; return 0u;',
        'twice-observed': ('if (!cetta_native_cost_active()) return 0u;\n'
                           '    cetta_native_cost_charge_observed(kind, units);\n'
                           '    return cetta_native_cost_charge_observed(kind, units);'),
    }
    args.out_dir.mkdir(parents=True, exist_ok=True)
    receipt = {'header_sha256': sha256(source.read_bytes()).hexdigest(),
               'control_sha256': sha256(control.read_bytes()).hexdigest(), 'cases': []}
    try:
        for name, replacement in replacements.items():
            directory = args.out_dir / name
            directory.mkdir(exist_ok=True)
            candidate = header.replace(body, replacement)
            (directory / 'native_cost.h').write_text(candidate)
            binary = (directory / 'control').resolve()
            command = shlex.split(args.cc) + ['-std=c11', '-O2', '-Wall', '-Wextra',
                '-Werror', '-I' + str(directory.resolve()), '-I' + str(ROOT / 'src'),
                str(control), '-o', str(binary)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            receipt['cases'].append({'name': name, 'compile': command, 'exit': run.returncode,
                'stdout': run.stdout, 'stderr': run.stderr,
                'candidate_sha256': sha256(candidate.encode()).hexdigest(),
                'binary_sha256': sha256(binary.read_bytes()).hexdigest()})
            assert run.returncode == (0 if name == 'original' else 1), receipt['cases'][-1]
            print('PASS: native cost guard ' + name, flush=True)
    finally:
        (args.out_dir / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
