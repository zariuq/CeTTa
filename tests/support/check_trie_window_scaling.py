#!/usr/bin/env python3
"""Lookups keyed past the match trie's window read their own rows.

Usage: check_trie_window_scaling.py CETTA_BINARY

The trie reads a row's first 511 elements and 8 levels.  Rows of 522
columns asked by their last bound column, and facts ten deep asked by a
value below the trie's levels (fewer than make a relation), each asked
once per row: from N to 4 N rows the lookups' time (best of three) grows at
most eightfold, where reading every row per lookup grows it sixteenfold,
and every lookup answers.
"""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def wide(rows, key_column=521):
    def columns(key):
        cols = [f"c{j}" for j in range(521)]
        cols[key_column - 1] = key
        return " ".join(cols)
    lines = [f"(wide {columns(str(i))} v{i})" for i in range(rows)]
    lines.append(f"(= (ask $i) (match &self (wide {columns('$i')} $v) $v))")
    return lines


def deep(rows):
    def nest(t):
        for _ in range(10):
            t = f"(w {t})"
        return t
    lines = [f"(d {nest(f'(k {i} v{i})')})" for i in range(rows)]
    lines.append(f"(= (ask $i) (match &self (d {nest('(k $i $v)')}) $v))")
    return lines


def run(binary, work, name, lines, rows):
    lines = lines + [
        "(= (all $n $acc) (if (== $n 0) $acc "
        "(let $v (ask (- $n 1)) (all (- $n 1) (+ $acc 1)))))",
        f"!(all {rows} 0)"]
    path = os.path.join(work, f"{name}_{rows}.metta")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    best = None
    for _ in range(3):
        began = time.perf_counter()
        p = subprocess.run([binary, "--lang", "petta", path], cwd=ROOT,
                           capture_output=True, text=True, timeout=900)
        took = time.perf_counter() - began
        if p.returncode != 0 or p.stdout.split() != [str(rows)]:
            print(f"FAIL: {name} over {rows} rows answered {p.stdout.split()[:3]}")
            return None
        best = took if best is None else min(best, took)
    return best


def main():
    binary = os.path.abspath(sys.argv[1])
    cases = (("wide", wide, (400, 1600)),
             ("wide-first-unindexed", lambda n: wide(n, 511), (400, 1600)),
             ("wide-middle-unindexed", lambda n: wide(n, 516), (400, 1600)),
             ("deep", deep, (800, 3200)))
    with tempfile.TemporaryDirectory(prefix="cetta-trie-window-") as work:
        for name, make, (small, large) in cases:
            per = [run(binary, work, name, make(n), n) for n in (small, large)]
            if None in per:
                return 1
            if per[1] > 8 * per[0] + 0.05:
                print(f"FAIL: {name} lookups took {per[0]:.3f} s over {small} rows "
                      f"and {per[1]:.3f} s over {large}")
                return 1
            print(f"PASS: {name} lookups past the trie's window: {per[0]:.3f} s and "
                  f"{per[1]:.3f} s over {small} and {large} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
