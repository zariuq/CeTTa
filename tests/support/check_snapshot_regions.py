#!/usr/bin/env python3
"""A collect-then-superpose region whose effects add rows to the space its
match reads answers on the snapshot the collection is, at a scale where the
native path makes its effects in several chunks.

Usage: check_snapshot_regions.py CETTA_BINARY

`(let $ps (collapse (match &q (q $x) (match &q (q $y) (p $x $y)))) ...)`
over 100 rows: the collection holds 10,000 pairs, every one read before any
row is added; the native run must count 10,000 additions, as the ordinary
path does, though they outnumber a chunk of effects.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROWS = 100


def program():
    lines = ["!(bind! &q (new-space))"]
    lines += [f"!(add-atom &q (q {i}))" for i in range(ROWS)]
    lines.append("!(let $ps (collapse (match &q (q $x) (match &q (q $y) (p $x $y)))) "
                 "(size-atom (collapse (let $a (superpose $ps) (add-atom &q (q $a))))))")
    lines.append("!(size-atom (collapse (match &q $z $z)))")
    return "\n".join(lines) + "\n"


def run(binary, path, reference):
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    if reference:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    p = subprocess.run([binary, "--profile", "extended", "--lang", "petta", path],
                       cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    return p.returncode, [l for l in p.stdout.splitlines() if l.strip()][-2:]


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-snapshot-") as work:
        path = os.path.join(work, "snapshot.metta")
        with open(path, "w") as f:
            f.write(program())
        native = run(binary, path, False)
        reference = run(binary, path, True)
    expected = (0, [str(ROWS * ROWS), str(ROWS + ROWS * ROWS)])
    if reference != expected or native != reference:
        print(f"FAIL: native {native}, reference {reference}, expected {expected}")
        return 1
    print(f"PASS: a snapshot region adds {ROWS * ROWS} rows to the space it reads "
          f"and counts them as the collection does")
    return 0


if __name__ == "__main__":
    sys.exit(main())
