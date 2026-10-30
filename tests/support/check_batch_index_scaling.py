#!/usr/bin/env python3
"""Batches of derived rows reach the indexes as they are added.

Usage: check_batch_index_scaling.py CETTA_RUNTIME_STATS_BINARY

A set holds (e k) for 50 values of k.  Each round derives (f r k) for every
(e k) in one batch, then joins (f $y 7) with (e $y), a read its path index
serves.  At R rounds and at 4 R, the rows the indexes take per round, built
and appended, do not grow with the rounds: a batch tells every index the
space keeps.  An index the batch leaves behind is rebuilt whole by its next
reader, which takes every row so far in every round.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROUNDS = (20, 80)
GROWTH = 1.25
INDEX_ROWS = ("match-position-index-build-row", "match-position-index-append-row",
              "space-head-index-build-row", "native-pattern-index-build-row")

PROGRAM = """!(bind! &t (new-space set))
(= (seed $k $n) (if (>= $k $n) () (let $a (add-atom &t (e $k)) (seed (+ $k 1) $n))))
!(seed 0 50)
(= (round $r $n $acc)
   (if (>= $r $n) $acc
       (let* (($d (collapse (match &t (e $x) (add-atom &t (f $r $x)))))
              ($c (size-atom (collapse (match &t (, (f $y 7) (e $y)) $y)))))
         (round (+ $r 1) $n (+ $acc $c)))))
!(round 0 {rounds} 0)
"""


def answers_expected(rounds):
    return sum(min(r + 1, 50) for r in range(rounds))


def main():
    binary = os.path.abspath(sys.argv[1])
    per_round = []
    with tempfile.TemporaryDirectory(prefix="cetta-batch-index-") as work:
        for rounds in ROUNDS:
            path = os.path.join(work, f"batches_{rounds}.metta")
            with open(path, "w") as f:
                f.write(PROGRAM.format(rounds=rounds))
            p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                                "--lang", "petta", path],
                               cwd=ROOT, capture_output=True, text=True, timeout=600)
            answers = [l for l in p.stdout.splitlines() if l not in ("()", "true")
                       and not l.startswith("runtime-")]
            if p.returncode != 0 or answers[-1:] != [str(answers_expected(rounds))]:
                print(f"FAIL: {rounds} rounds answered {answers[-1:]}, expected "
                      f"{answers_expected(rounds)}")
                return 1
            taken = 0
            for line in (p.stdout + p.stderr).splitlines():
                fields = line.split()
                if len(fields) >= 3 and fields[0] == "runtime-counter" and \
                        fields[1] in INDEX_ROWS:
                    taken += int(fields[2])
            per_round.append(taken / rounds)
    if per_round[1] > GROWTH * per_round[0] + 1:
        print(f"FAIL: the indexes take {per_round[0]:.1f} rows a round over "
              f"{ROUNDS[0]} rounds and {per_round[1]:.1f} over {ROUNDS[1]}")
        return 1
    print(f"PASS: batches of derived rows reach the indexes as they are added: "
          f"{per_round[0]:.1f} and {per_round[1]:.1f} rows a round over "
          f"{ROUNDS[0]} and {ROUNDS[1]} rounds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
