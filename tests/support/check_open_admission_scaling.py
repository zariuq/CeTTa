#!/usr/bin/env python3
"""Rows holding variables that an equation adds into a set are added in
place.

Usage: check_open_admission_scaling.py CETTA_RUNTIME_STATS_BINARY

Each round of a backward search opens a premise for every subgoal of its
depth, as inverse modus ponens does: `(add-atom &t (sol $k1 (C (/ (> $a $b)
(/ $a $c)) (M $f))))`, a row whose `$a` nothing binds, matched against rows
that hold such variables themselves.  The set keeps a row holding variables
as its alpha key, which no naming of the variables changes, so the
equation's region adds it as it adds a ground row, without leaving the
addition to its host.  For N and 2N subgoals at the first depth, every round
makes a row for each, and the goals the region leaves to its host stay below
one in a hundred rows made.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SUBGOALS = (300, 600)
ROUNDS = 4
HOST_GOALS_PER_ROW = 0.01

PROGRAM = """(= (open $k $k1)
   (match &t (sol $k (C (/ $b $c) $f))
     (add-atom &t (sol $k1 (C (/ (> $a $b) (/ $a $c)) (M $f))))))
(= (rounds $k $n)
   (if (== $k $n) $k
       (let* (($k1 (+ $k 1)) ($m (size-atom (collapse (open $k $k1)))))
         (rounds $k1 $n))))
!(rounds 0 {rounds})
!(size-atom (collapse (match &t (sol $k $g) t)))
"""


def run(binary, work, subgoals):
    seeds = ["!(bind! &t (new-space set))"] + [
        f"!(add-atom &t (sol 0 (C (/ (x {i}) (y {i})) I)))"
        for i in range(subgoals)]
    path = os.path.join(work, f"open_{subgoals}.metta")
    with open(path, "w") as f:
        f.write("\n".join(seeds) + "\n" + PROGRAM.format(rounds=ROUNDS))
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines()
               if l.strip() and not l.startswith("runtime-")]
    rows = subgoals * (ROUNDS + 1)
    if p.returncode != 0 or not printed or printed[-1] != str(rows):
        print(f"FAIL: {ROUNDS} rounds over {subgoals} subgoals made "
              f"{printed[-1:]} rows, expected {rows}")
        return None
    host = None
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[:2] == ["runtime-counter",
                                               "open-equation-host"]:
            host = int(fields[2])
    if host is None:
        # a counter the binary always reports; its absence is no count
        stats = any(l.startswith("runtime-counter ") for l in
                    (p.stdout + p.stderr).splitlines())
        if not stats:
            print("FAIL: the binary reports no runtime counters")
            return None
        host = 0
    return host, subgoals * ROUNDS


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-open-admission-") as work:
        for subgoals in SUBGOALS:
            measured = run(binary, work, subgoals)
            if measured is None:
                return 1
            host, made = measured
            if host > HOST_GOALS_PER_ROW * made:
                print(f"FAIL: {ROUNDS} rounds over {subgoals} subgoals left "
                      f"{host} goals to the host for {made} rows made")
                return 1
    print(f"PASS: backward rounds over {SUBGOALS[0]} and {SUBGOALS[1]} "
          f"subgoals add their rows holding variables in place")
    return 0


if __name__ == "__main__":
    sys.exit(main())
