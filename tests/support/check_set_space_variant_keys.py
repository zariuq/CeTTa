#!/usr/bin/env python3
"""A set of open subgoals stores each variant once, and its subgoals share parts.

Usage: check_set_space_variant_keys.py CETTA_RUNTIME_STATS_BINARY

Forward chaining over Hilbert's axioms keeps its open subgoals in a set space,
round by round, as a trie keyed by first occurrences would: each new subgoal
is built from parts of others, its variables named as the rules that made it
name them.  A set keeps each subgoal as its alpha key, its variables numbered
by first occurrence, so subgoals that differ only in those names share their
records.  From the search to depth 11 to the search to depth 13 the subgoals
and proofs are the expected ones, and the records the term universe adds a
new subgoal stay within a bound that names would exceed.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# Subgoals and proofs of the search to each depth.
DEPTHS = ((11, 1542, 0), (13, 9124, 2))
PER_SUBGOAL = 5.0

PROGRAM = """(target {depth} (C (> (> (> p s) x) (> s x)) $x))
(axiom 1 (> $p (> $s $p)))
(axiom 2 (> (> $p (> $s $x)) (> (> $p $s) (> $p $x))))
(axiom 3 (> (> (! $p) (! $s)) (> $s $p)))
!(bind! &t (new-space set))
!(match &self (target $mps (C $ta $tx)) (add-atom &t (sol $mps 1 (C (/ $ta $ta) I))))
(= (upto $n) (if (< $n 0) (empty) $n))
(= (upto $n) (if (< $n 1) (empty) (let $m (- $n 1) (upto $m))))
(= (axiom-step $sk $k)
   (let* (($hi (upto $k)) ($shi (+ $hi 1)))
     (match &self (axiom $r $c)
       (match &t (sol $sk $shi (C (/ $c $b) $f))
         (add-atom &t (sol $k $hi (C $b ($r $f))))))))
(= (mp-step $sk $k)
   (let* (($hi (upto $k)) ($shi (+ $hi 1)))
     (match &t (sol $sk $hi (C (/ $b $c) $f))
       (add-atom &t (sol $k $shi (C (/ (> $a $b) (/ $a $c)) (M $f)))))))
(= (rounds $sk)
   (if (< $sk 1) done
       (let* (($k (- $sk 1))
              ($x (size-atom (collapse (axiom-step $sk $k))))
              ($y (size-atom (collapse (mp-step $sk $k)))))
         (rounds $k))))
!(match &self (target $mps $_) (rounds $mps))
!(match &self (target $mps (C $ta $tx))
   (match &t (sol 0 0 (C $ta $tx)) (let $_ (add-atom &t (final 0 0 (C $ta $tx))) (final 0 0 (C $ta $tx)))))
!(size-atom (collapse (match &t (sol $a $b $c) t)))
!(size-atom (collapse (match &t (final $a $b $c) t)))
"""


def run(binary, work, depth, subgoals, proofs):
    path = os.path.join(work, f"chain_{depth}.metta")
    with open(path, "w") as f:
        f.write(PROGRAM.format(depth=depth))
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines()
               if l.strip() and not l.startswith("runtime-")]
    if p.returncode != 0 or printed[-2:] != [str(subgoals), str(proofs)]:
        print(f"FAIL: the search to depth {depth} answered {printed[-2:]}, "
              f"expected {[str(subgoals), str(proofs)]}")
        return None
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[:2] == ["runtime-counter",
                                               "term-universe-insert"]:
            return int(fields[2])
    return 0


def main():
    binary = os.path.abspath(sys.argv[1])
    inserts = []
    with tempfile.TemporaryDirectory(prefix="cetta-variant-keys-") as work:
        for depth, subgoals, proofs in DEPTHS:
            measured = run(binary, work, depth, subgoals, proofs)
            if measured is None:
                return 1
            inserts.append(measured)
    added = DEPTHS[1][1] - DEPTHS[0][1]
    per_subgoal = (inserts[1] - inserts[0]) / added
    if per_subgoal > PER_SUBGOAL:
        print(f"FAIL: a new subgoal costs {per_subgoal:.2f} records "
              f"({inserts[0]} to depth {DEPTHS[0][0]}, {inserts[1]} to "
              f"depth {DEPTHS[1][0]})")
        return 1
    print(f"PASS: open subgoals stored as alpha keys share their parts: "
          f"{per_subgoal:.2f} records a new subgoal from depth "
          f"{DEPTHS[0][0]} to {DEPTHS[1][0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
