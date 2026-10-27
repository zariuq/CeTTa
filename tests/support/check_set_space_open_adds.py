#!/usr/bin/env python3
"""A set space holding an open atom takes its alpha-variants without storing them.

Usage: check_set_space_open_adds.py CETTA_RUNTIME_STATS_BINARY

A function adds `(rule $x (f $x) $y)` into a set space again and again; each
activation gives the atom fresh variables, so each addition is an
alpha-variant of the one the set holds.  The set must hold one atom, and the
term universe must not grow with the number of additions: the insertions of
a run with many more additions exceed those of a smaller run by at most a
bound that does not depend on the sizes.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (500, 4000)
BOUND = 64

PROGRAM = """!(bind! &s (new-space set))
(= (fill $k) (if (== $k 0) done (let $_ (add-atom &s (rule $x (f $x) $y)) (fill (- $k 1)))))
!(fill {count})
!(size-atom (collapse (match &s (rule $a $b $c) t)))
"""


def counters(report):
    found = {}
    for line in report.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            found[fields[1]] = int(fields[2])
    return found


def main():
    binary = os.path.abspath(sys.argv[1])
    inserts = []
    with tempfile.TemporaryDirectory(prefix="cetta-set-open-adds-") as work:
        for count in SIZES:
            path = os.path.join(work, f"adds_{count}.metta")
            with open(path, "w") as f:
                f.write(PROGRAM.format(count=count))
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT,
                               capture_output=True, text=True, timeout=600)
            answers = p.stdout.split()
            if p.returncode != 0 or answers[-2:] != ["done", "1"]:
                print(f"FAIL: {count} additions answered {answers[-2:]}, "
                      f"expected ['done', '1']")
                return 1
            inserts.append(counters(p.stderr).get("term-universe-insert", 0))
    growth = inserts[1] - inserts[0]
    if growth > BOUND:
        print(f"FAIL: {SIZES[1]} additions inserted {growth} more terms than "
              f"{SIZES[0]} ({inserts}), more than {BOUND}")
        return 1
    print(f"PASS: a set holding an open atom takes {SIZES[0]} and {SIZES[1]} "
          f"alpha-variants with {growth} more term insertions, at most {BOUND}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
