#!/usr/bin/env python3
"""Loading deep facts costs what their relation holds, not a record a level.

Usage: check_deep_facts_load.py CETTA_BINARY

Documents of 131,072 facts (d (w (w ... (k i v)))) ten and forty deep,
whose head takes a relation by its holes, load at most 256 bytes a fact
above an empty program's peak (a record per level is about 70 bytes, so
ten levels read whole cost about 950), and count their facts.
"""
import os
import resource
import subprocess
import sys
import tempfile

FACTS = 131072
DEPTHS = (10, 40)
BOUND = 256


def peak_kib(binary, path):
    child = subprocess.run(
        [sys.executable, __file__, '--one', binary, path],
        capture_output=True, text=True, timeout=600)
    rc, _, rest = child.stdout.strip().partition(' ')
    peak, _, answer = rest.partition(' ')
    return int(rc), int(peak), answer


def one(binary, path):
    p = subprocess.run([binary, '--lang', 'petta', path], capture_output=True,
                       text=True, timeout=600)
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    print(p.returncode, peak, p.stdout.strip().replace('\n', ' '))
    return 0


def main():
    if sys.argv[1] == '--one':
        return one(sys.argv[2], sys.argv[3])
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix='cetta-deep-facts-') as work:
        empty = os.path.join(work, 'empty.metta')
        with open(empty, 'w') as f:
            f.write('!(+ 1 2)\n')
        _, base, _ = peak_kib(binary, empty)
        for depth in DEPTHS:
            deep = os.path.join(work, f'deep_{depth}.metta')
            with open(deep, 'w') as f:
                for i in range(FACTS):
                    term = f'(k {i} v{i % 7})'
                    for _ in range(depth):
                        term = f'(w {term})'
                    f.write(f'(d {term})\n')
                f.write('!(size-atom (collapse (match &self (d $x) t)))\n')
            rc, peak, answer = peak_kib(binary, deep)
            if rc != 0 or answer != str(FACTS):
                print(f'FAIL: the facts {depth} deep answered {answer!r} (rc {rc})')
                return 1
            per_fact = (peak - base) * 1024 // FACTS
            if per_fact > BOUND:
                print(f'FAIL: {FACTS} facts {depth} deep cost {per_fact} bytes a '
                      f'fact above start-up')
                return 1
            print(f'PASS: {FACTS} facts {depth} deep load at {per_fact} bytes a fact')
    return 0


if __name__ == '__main__':
    sys.exit(main())
