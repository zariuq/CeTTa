#!/usr/bin/env python3
"""A stored term's levels cost the same however deep they are: one term
800,000 levels deep, stored, read back by a match and counted, takes at most
twice as long as eight terms of 100,000 levels.

Decoding a stored term recursed once per level and overflowed the C stack
near 100,000 levels; interning a chain compared each level with its
neighbours down to their depth, which costs about eight times more for the
one deep term.  Both runs hold the same number of levels, so caches weigh
on them alike.

Usage: check_deep_terms_scaling.py CETTA
"""
import os
import resource
import subprocess
import sys
import tempfile

PROGRAM = """(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (s $acc))))
(= (peel $t $k) (case $t (((s $u) (peel $u (+ $k 1))) ($other $k))))
(= (fill $i $c $d) (if (>= $i $c) done
  (let $t (build $d (z $i)) (let $_ (add-atom &self (d $t)) (fill (+ $i 1) $c $d)))))
!(fill 0 {count} {depth})
!(match &self (d $x) (peel $x 0))
"""


def run(cetta, count, depth):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(PROGRAM.format(count=count, depth=depth))
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=300)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    lines = result.stdout.split()
    if (result.returncode != 0 or lines[:1] != ["done"] or
            sorted(lines[1:]) != [str(depth)] * count):
        print(f"FAIL: {count} terms of depth {depth}: exit {result.returncode}, "
              f"answers {lines[:4]}; {result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def main():
    cetta = sys.argv[1]
    shallow = min(run(cetta, 8, 100000) for _ in range(2))
    deep = min(run(cetta, 1, 800000) for _ in range(2))
    ratio = deep / max(shallow, 0.01)
    if ratio > 2.0:
        print(f"FAIL: one term of 800,000 levels took {deep:.2f} s against "
              f"{shallow:.2f} s for eight of 100,000 ({ratio:.1f}x)")
        sys.exit(1)
    print(f"PASS: a stored term's levels cost the same at any depth: one of "
          f"800,000 levels {deep:.2f} s, eight of 100,000 {shallow:.2f} s "
          f"({ratio:.1f}x)")


if __name__ == "__main__":
    main()
