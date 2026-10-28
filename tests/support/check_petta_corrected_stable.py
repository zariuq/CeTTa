#!/usr/bin/env python3
"""Qualify selected corrected-stable rules; open semantics are not this gate."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    root = Path(__file__).resolve().parents[2]
    fixtures = sorted((root / 'tests/petta').glob('corrected_stable_*.metta'))
    count = 0
    for machine in ('0', '1'):
        for source in fixtures:
            result = subprocess.run([str(binary), '--lang', 'petta', str(source)],
                                    cwd=root, capture_output=True, text=True, timeout=30,
                                    env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine))
            if result.returncode or result.stderr or '❌' in result.stdout or '✅' not in result.stdout:
                raise AssertionError((machine, source.name, result.returncode,
                                      result.stdout, result.stderr))
            count += result.stdout.count('✅')
    print(f'PASS: {count} settled corrected-stable assertions on both equation routes')


if __name__ == '__main__':
    main()
