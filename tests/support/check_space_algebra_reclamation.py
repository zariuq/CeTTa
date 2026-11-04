#!/usr/bin/env python3
"""Dropped algebra spaces release storage; retained snapshots preserve bags."""
import os
import sys

from check_escaped_space_reclamation import measured


def loop(operation, count):
    return ("(= (churn $n) (if (== $n 0) done "
            f"(let $s {operation} (churn (- $n 1)))))\n"
            f"!(churn {count})\n")


def main():
    cetta = os.path.abspath(sys.argv[1])
    os.environ["CETTA_PETTA_SEARCH_MACHINE"] = "1"
    sizes = (8000, 64000)
    operations = (
        ("union", "(space-union &self &self)"),
        ("intersection", "(space-intersection &self &self)"),
        ("folded union", "(space-union (| &self &self &self) &self)"),
    )
    for name, operation in operations:
        peaks = [measured(cetta, loop(operation, count), ["done"])
                 for count in sizes]
        if peaks[1] > 1.25 * peaks[0] + 8192:
            raise SystemExit(
                f"FAIL: dropped {name} results retain storage: {list(zip(sizes, peaks))}")
        print(f"PASS: dropped {name} results stay bounded {list(zip(sizes, peaks))} KiB")

    kept = """(= (churn $n) (if (== $n 0) done
  (let $s (space-union &self &self) (churn (- $n 1)))))
!(bind! &left (new-state (new-space)))
!(add-atom (get-state &left) (key 7))
!(add-atom (get-state &left) (key 7))
!(bind! &right (new-state (new-space)))
!(add-atom (get-state &right) (key 7))
!(bind! &saved_union (new-state
  (space-union (get-state &left) (get-state &right))))
!(bind! &saved_meet (new-state
  (space-intersection (get-state &left) (get-state &right))))
!(bind! &saved_fold (new-state
  (space-union (| (get-state &left) (get-state &right) (get-state &left))
               (get-state &right))))
!(let $l (get-state &left) (let $r (get-state &right)
  (bind! &saved_transaction (new-state (transaction (space-union $l $r))))))
!(let $s (space-union (get-state &left) (get-state &right))
  (add-atom &self (kept $s)))
!(add-atom (get-state &left) (key 8))
!(churn 8000)
!(collapse (match (get-state &saved_union) (key $x) $x))
!(collapse (match (get-state &saved_meet) (key $x) $x))
!(collapse (match (get-state &saved_fold) (key $x) $x))
!(collapse (match (get-state &saved_transaction) (key $x) $x))
!(collapse (match &self (kept $s) (match $s (key $x) $x)))
"""
    expected = ["true"] * 11 + ["done"] + (
        "(7 7 7) (7) (7 7 7 7 7 7) (7 7 7) (7 7 7)".split())
    measured(cetta, kept, expected)
    print("PASS: state- and fact-retained algebra snapshots preserve multiplicity and survive source mutation")

    branches = """!(bind! &left (new-state (new-space)))
!(add-atom (get-state &left) (key 7))
!(add-atom (get-state &left) (key 7))
!(bind! &right (new-state (new-space)))
!(add-atom (get-state &right) (key 7))
!(let $l (get-state &left) (let $r (get-state &right)
  (size-atom (collapse (let $s
    (hyperpose ((space-union $l $r) (space-intersection $l $r)))
    (match $s (key $x) $x))))))
"""
    measured(cetta, branches, ["true"] * 5 + ["4"])
    print("PASS: parallel-branch algebra results survive transfer and branch retirement")


if __name__ == "__main__":
    main()
