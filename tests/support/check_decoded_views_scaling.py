#!/usr/bin/env python3
"""Rows a match reads are read where they are stored, not decoded.

Usage: check_decoded_views_scaling.py CETTA_RUNTIME_STATS_BINARY

A relation (val (pair a b) c) over a small alphabet holds N rows, and N
more open rows (sol (pair a b) c $x) sit in a set.  Matches whose template
is a call, so the native path leaves them to the general machines, read
every row: in a function's body (the open equation machine), at top level
(the search machine), over a stack, which reads its rows in turn, and over
the open rows; the open rows' alpha-variants are added again and leave the
set as it was.  At N and 4 N rows the answers are the expected ones, and the
entries the universe decodes are the atoms of the alphabet and the parts
many rows share, read often enough to be kept, not the rows: decoded entries
a row fall by a quarter or more from N to 4 N.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (2000, 8000)
FALL = 0.75


def program(rows):
    side = int(round(rows ** (1.0 / 3.0))) + 1
    names = [f"s{i}" for i in range(side)]
    lines, kept = [], 0
    for index in range(rows):
        a = names[index % side]
        b = names[(index // side) % side]
        c = names[(index // (side * side)) % side]
        lines.append(f"(val (pair {a} {b}) {c})")
        kept += c != "s0"
    lines += [
        "(= (keep $p $c) (if (== $c s0) (empty) (kept $p $c)))",
        "(= (scan) (match &self (val $p $c) (keep $p $c)))",
        "!(size-atom (collapse (scan)))",
        "!(size-atom (collapse (match &self (val $p $c) (keep $p $c))))",
        "!(bind! &st (new-space stack))",
        "!(size-atom (collapse (match &self (val $p $c) (add-atom &st (row $p $c)))))",
        "!(size-atom (collapse (match &st (row $p $c) (keep $p $c))))",
        "!(bind! &o (new-space set))",
        "!(size-atom (collapse (match &self (val $p $c) (add-atom &o (sol $p $c $x)))))",
        "!(size-atom (collapse (match &self (val $p $c) (add-atom &o (sol $p $c $y)))))",
        "!(size-atom (collapse (match &o (sol $p $c $z) (keep $p $c))))",
    ]
    answers = [str(kept), str(kept), str(rows), str(kept), str(rows),
               str(rows), str(kept)]
    return "\n".join(lines) + "\n", answers


def run(binary, work, rows):
    text, answers = program(rows)
    path = os.path.join(work, f"views_{rows}.metta")
    with open(path, "w") as f:
        f.write(text)
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines()
               if not l.startswith("runtime-") and l.strip().isdigit()]
    if p.returncode != 0 or printed != answers:
        print(f"FAIL: over {rows} rows the matches answered {printed}, "
              f"expected {answers}")
        return None
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[:2] == ["runtime-counter",
                                               "term-universe-lazy-decode"]:
            return int(fields[2])
    return 0


def main():
    binary = os.path.abspath(sys.argv[1])
    decoded = []
    with tempfile.TemporaryDirectory(prefix="cetta-decoded-views-") as work:
        for rows in SIZES:
            measured = run(binary, work, rows)
            if measured is None:
                return 1
            decoded.append(measured)
    small, large = decoded
    if large * SIZES[0] > FALL * small * SIZES[1]:
        print(f"FAIL: the universe decoded {small} entries over {SIZES[0]} "
              f"rows and {large} over {SIZES[1]}")
        return 1
    print(f"PASS: matches read stored rows in place: {small} and {large} "
          f"entries decoded over {SIZES[0]} and {SIZES[1]} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
