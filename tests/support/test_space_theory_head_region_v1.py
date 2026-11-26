#!/usr/bin/env python3
"""End-to-end verification of head-scoped counted-match regions, single
theory classification, and invalidation controls across HE and PeTTa."""

import subprocess
import sys


def run_cetta(cetta_bin, flags, program):
    proc = subprocess.run(
        [cetta_bin] + flags + ["-e", program],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if proc.returncode != 0:
        raise AssertionError(
            f"cetta {' '.join(flags)} exited {proc.returncode}\n"
            f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
        )
    return [line.strip() for line in proc.stdout.strip().splitlines() if line.strip()]


def expect_lines(name, got, expected):
    if got != expected:
        raise AssertionError(
            f"{name} mismatch:\n  got:      {got}\n  expected: {expected}"
        )


def main():
    cetta_bin = sys.argv[1] if len(sys.argv) > 1 else "./cetta"

    # 1. HE: repeated counted-match across unrelated data writes, same-head
    #    writes/removals, open atoms, template-rewriting definitions, and
    #    non-linear repeated-variable patterns.  (In HE `let $xs (collapse ...)
    #    (size-atom $xs)` routes directly through
    #    eval_try_private_collection_cardinality ->
    #    try_count_generic_match_collapse.)
    he_program = """
    (rel 77 1 2)
    (rel 77 3 4)
    (rel 77 5 6)
    (rel 99 1 2)
    (pair 5 5)
    (pair 5 6)

    ; 1st touch + unrelated writes + 2nd touch (promotes to head-scoped) + more unrelated writes
    !(let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs))
    !(let $_ (add-atom &self (noise 1))
      (let $_ (add-atom &self (noise 2))
        (let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs))))
    !(let $_ (add-atom &self (noise 3))
      (let $_ (add-atom &self (other a b))
        (let $_ (remove-atom &self (noise 1))
          (let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs)))))

    ; Same-head add and remove invalidate head-scoped count immediately
    !(let $_ (add-atom &self (rel 77 7 8))
      (let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs)))
    !(let $_ (remove-atom &self (rel 77 7 8))
      (let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs)))

    ; Open/wildcard atom invalidates watched head symbol count
    !(let $_ (add-atom &self ($open-head 77 9 10))
      (let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs)))
    !(let $_ (remove-atom &self ($open-head 77 9 10))
      (let $xs (collapse (match &self (rel 77 $b $c) 1)) (size-atom $xs)))

    ; Non-linear repeated-variable pattern must never alias linear cache keys
    !(let $xs (collapse (match &self (pair $x $y) 1)) (size-atom $xs))
    !(let $xs (collapse (match &self (pair $x $x) 1)) (size-atom $xs))
    !(let $_ (add-atom &self (pair 7 7))
      (let $xs (collapse (match &self (pair $x $x) 1)) (size-atom $xs)))

    ; Rule edit affecting template invalidates cached count via theory token
    !(let $xs (collapse (match &self (rel 77 $b $c) (tag))) (size-atom $xs))
    !(let $_ (add-atom &self (noise 4))
      (let $xs (collapse (match &self (rel 77 $b $c) (tag))) (size-atom $xs)))
    !(let $_ (add-atom &self (= (tag) (superpose (x y))))
      (let $xs (collapse (match &self (rel 77 $b $c) (tag))) (size-atom $xs)))
    """
    got = run_cetta(cetta_bin, ["--lang", "he"], he_program)
    expect_lines(
        "he-head-region-count",
        got,
        [
            "[3]",
            "[3]",
            "[3]",
            "[4]",
            "[3]",
            "[4]",
            "[3]",
            "[2]",
            "[1]",
            "[2]",
            "[3]",
            "[3]",
            "[6]",
        ],
    )

    # 2. PeTTa: repeated counted-match over &self and separate &db space,
    #    plus --profile extended =e interaction.
    petta_program = """
    (rel 77 1 2)
    (rel 77 3 4)
    (rel 77 5 6)
    (rel 99 1 2)
    (pair 5 5)
    (pair 5 6)

    !(size-atom (collapse (match &self (rel 77 $b $c) 1)))
    !(let $_ (add-atom &self (noise 1))
      (let $_ (add-atom &self (noise 2))
        (size-atom (collapse (match &self (rel 77 $b $c) 1)))))
    !(let $_ (add-atom &self (noise 3))
      (let $_ (remove-atom &self (noise 1))
        (size-atom (collapse (match &self (rel 77 $b $c) 1)))))
    !(let $_ (add-atom &self (rel 77 7 8))
      (size-atom (collapse (match &self (rel 77 $b $c) 1))))
    !(let $_ (remove-atom &self (rel 77 7 8))
      (size-atom (collapse (match &self (rel 77 $b $c) 1))))
    !(size-atom (collapse (match &self (pair $x $y) 1)))
    !(size-atom (collapse (match &self (pair $x $x) 1)))

    ; Separate data space (&db) isolated from &self data writes
    !(let $db (new-space)
      (let $_ (add-atom $db (item 10 a))
        (let $_ (add-atom $db (item 10 b))
          (let $c1 (size-atom (collapse (match $db (item 10 $v) 1)))
            (let $_ (add-atom $db (other-item 99 z))
              (let $c2 (size-atom (collapse (match $db (item 10 $v) 1)))
                (let $_ (add-atom $db (other-item 100 w))
                  (let $c3 (size-atom (collapse (match $db (item 10 $v) 1)))
                    (let $_ (add-atom $db (item 10 c))
                      (let $c4 (size-atom (collapse (match $db (item 10 $v) 1)))
                        ($c1 $c2 $c3 $c4)))))))))))

    ; Extended =e edit invalidates alias clock while raw match stays exact
    !(let $_ (add-atom &self (=e rel alias-rel))
      (let $_ (add-atom &self (alias-rel 77 99 100))
        ((size-atom (collapse (match &self (rel 77 $b $c) 1)))
         (size-atom (collapse (eq:match &self (rel 77 $b $c) 1))))))
    """
    got = run_cetta(
        cetta_bin, ["--lang", "petta", "--profile", "extended"], petta_program
    )
    expect_lines(
        "petta-head-region-count",
        got,
        ["3", "3", "3", "4", "3", "2", "1", "(2 2 2 3)", "(3 4)"],
    )

    print("PASS: head-scoped counted-match regions (HE + PeTTa + extended)")


if __name__ == "__main__":
    main()
