#!/usr/bin/env python3
"""A generator nested to the right and a loop of removals grow linearly.

Two findings of the adversarial audit, fixed, kept fixed here:

* A right-nested `superpose` generator,
  `(= (range $a $b) (if (>= $a $b) (empty) (superpose ($a (range (+ $a 1) $b)))))`,
  handed each answer back through every pending alternative: 32,000
  answers took 1.81 s (exponent 2.11).  Collected for N and for 4 N, its
  time (best of three) may grow at most sixfold, where a cost per answer
  growing with the depth grows it sixteenfold; and its answers, sorted, are
  exactly 0 to N - 1, each once.
* A recursive loop of `remove-atom` calls slowed as it ran: 64k to 512k
  calls took 0.1 to 1.17 s.  Removing N atoms from a space of N rows, and
  then 4 N from 4 N, its time may grow at most sixfold as well, and the
  space ends empty.

Usage: check_generator_loop_scaling.py CETTA [N]
"""
import os
import subprocess
import sys
import tempfile
import time

GROWTH = 6.0

RANGE = ("(= (range $a $b) (if (>= $a $b) (empty)"
         " (superpose ($a (range (+ $a 1) $b)))))\n")


def nested(n):
    return RANGE + f"!(size-atom (collapse (range 0 {n})))\n"


def law(n):
    every = " ".join(str(i) for i in range(n))
    return RANGE + f"!(== (sort-atom (collapse (range 0 {n}))) ({every}))\n"


def removals(n):
    rows = "".join(f"(x {i})\n" for i in range(1, n + 1))
    return (rows +
            "(= (drop $n) (if (== $n 0) done"
            " (let $_ (remove-atom &self (x $n)) (drop (- $n 1)))))\n"
            f"!(drop {n})\n!(size-atom (collapse (match &self (x $i) $i)))\n")


def run(cetta, text):
    """Answers and the best of three wall times."""
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        best, answers = None, None
        for _ in range(3):
            start = time.monotonic()
            done = subprocess.run([cetta, "--lang", "petta", path],
                                  capture_output=True, text=True, timeout=600)
            spent = time.monotonic() - start
            if done.returncode != 0:
                return None, None
            answers = done.stdout.split()
            best = spent if best is None else min(best, spent)
        return answers, best
    finally:
        os.unlink(path)


def ladder(cetta, name, program, n, expected):
    small, t_small = run(cetta, program(n))
    large, t_large = run(cetta, program(4 * n))
    if small != expected(n) or large != expected(4 * n):
        print(f"FAIL: {name}: answers {small} and {large}")
        return False
    ratio = t_large / max(t_small, 1e-3)
    verdict = "PASS" if ratio <= GROWTH else "FAIL"
    print(f"{verdict}: {name} from {n} to {4 * n}: {t_small:.3f} s to "
          f"{t_large:.3f} s ({ratio:.1f}x, at most {GROWTH:.0f}x)")
    return verdict == "PASS"


def main():
    cetta = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 128000
    ok = ladder(cetta, "a right-nested superpose generator", nested, n,
                lambda m: [str(m)])
    ok = ladder(cetta, "a loop of removals", removals, n,
                lambda m: ["done", "0"]) and ok
    answers, _ = run(cetta, law(4096))
    if answers != ["true"]:
        print(f"FAIL: the right-nested generator's answers are not 0 to 4095 "
              f"once each: {answers}")
        ok = False
    else:
        print("PASS: the right-nested generator answers 0 to 4095, each once")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
