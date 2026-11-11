#!/usr/bin/env python3
"""A relation of deep facts keeps its reads indexed.

Usage: check_deep_relation_lookups.py CETTA_BINARY

Backward chaining over a knowledge base of 2n+1 deep facts, (kb (: a A))
and (kb (: r (R A B))) for n targets and one open rule, modus ponens, proves
each target from a fact and a relation.  Its reads fix the conclusion, or
leave an implication to unification there, so a relation that answered
them by walking every fact would grow the run with n squared.  At n and 2n,
both past the facts at which a head takes a relation, the run answers every
target, and its processor time grows by at most three times.
"""
import os
import resource
import subprocess
import sys
import tempfile

SIZES = (2500, 5000)
GROWTH = 3.0

RULES = """(= (bc (: $proof $conclusion) $depth) (match &self (kb (: $proof $conclusion)) (: $proof $conclusion)))
(= (bc (: (@ $f $x) $conclusion) (S $depth))
   (let* (((: $f (-> $premise $conclusion)) (bc (: $f (-> $premise $conclusion)) $depth))
          ((: $x $premise) (bc (: $x $premise) $depth)))
     (: (@ $f $x) $conclusion)))
!(bind! &p (new-space set))
!(let $r (collapse (match &self (target $c) (let (: $proof $c) (bc (: $proof $c) (S (S (S Z)))) (add-atom &p (proof $proof $c))))) done)
!(size-atom (collapse (match &p (proof $p $c) t)))
"""


def program(n):
    kb = ''.join(f'(kb (: a{i} A{i}))\n(kb (: r{i} (R A{i} B{i})))\n'
                 for i in range(n))
    kb += '(kb (: MP (-> (R $p $q) (-> $p $q))))\n'
    targets = ''.join(f'(target B{i})\n' for i in range(n))
    return kb + targets + RULES


def cpu_seconds(binary, path):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    run = subprocess.run([binary, '--lang', 'petta', path],
                         capture_output=True, text=True, timeout=600)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    used = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    return run.returncode, run.stdout, used


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    binary = os.path.abspath(sys.argv[1])
    times = []
    with tempfile.TemporaryDirectory(prefix='deep-lookups-') as directory:
        for n in SIZES:
            path = os.path.join(directory, f'bc_{n}.metta')
            with open(path, 'w') as handle:
                handle.write(program(n))
            rc, out, used = cpu_seconds(binary, path)
            if rc != 0 or out != f'done\n{n}\n':
                print(f'FAIL: backward chaining over {2 * n + 1} deep facts '
                      f'answered {out!r} (rc {rc})')
                return 1
            times.append(used)
    growth = times[1] / max(times[0], 1e-3)
    if growth > GROWTH:
        print(f'FAIL: deep relation reads grow {growth:.1f}x from n={SIZES[0]} '
              f'to n={SIZES[1]} ({times[0]:.2f}s, {times[1]:.2f}s); '
              f'indexed reads grow about 2x')
        return 1
    print(f'PASS: deep relation reads stay indexed: {times[0]:.2f}s at '
          f'n={SIZES[0]}, {times[1]:.2f}s at n={SIZES[1]} ({growth:.1f}x)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
