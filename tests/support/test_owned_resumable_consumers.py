#!/usr/bin/env python3
"""Owned consumer observations: demand, duplicate order, effects and faults."""
from pathlib import Path
import subprocess
import sys
import tempfile

# Expected observations were checked against the committed dialect runtime.
# Cases include refusals of fusion: they must retain ordinary evaluation.
CASES = [
    ('ordered-duplicates', 'he', '(= (choice) 3)\n(= (choice) 3)\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[3]\n[11]\n', 0),
    ('ordered-duplicates', 'petta', '(= (choice) 3)\n(= (choice) 3)\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '3\n11\n', 0),
    ('empty', 'he', '(= (choice) (empty))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[0]\n[0]\n', 0),
    ('empty', 'petta', '(= (choice) (empty))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '0\n0\n', 0),
    ('late-effect', 'he', '(= (choice) 3)\n(= (choice) (let $ignored (println! late-effect) 5))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', 'late-effect\nlate-effect\n[2]\n[8]\n', 0),
    ('late-effect', 'petta', '(= (choice) 3)\n(= (choice) (let $ignored (println! late-effect) 5))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', 'late-effect\nlate-effect\n2\n8\n', 0),
    ('late-fault', 'he', '(= (choice) 3)\n(= (choice) (/ 1 0))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[1]\n[3]\n', 0),
    ('late-fault', 'petta', '(= (choice) 3)\n(= (choice) (/ 1 0))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '', 2),
    ('unselected-effect', 'he', '(= (choice) (if True 3 (println! unselected-effect)))\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[8]\n', 0),
    ('unselected-effect', 'petta', '(= (choice) (if True 3 (println! unselected-effect)))\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '2\n8\n', 0),
    ('conditional', 'he', '(= (choice) (if (< 1 2) (+ 1 2) 5))\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[8]\n', 0),
    ('conditional', 'petta', '(= (choice) (if (< 1 2) (+ 1 2) 5))\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '2\n8\n', 0),
    ('non-number', 'he', '(= (choice) 3)\n(= (choice) held)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[(+ 3 held)]\n', 0),
    ('non-number', 'petta', '(= (choice) 3)\n(= (choice) held)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '', 2),
    ('nested', 'he', '(= (coin $n) $n)\n(= (coin $n) (+ $n 1))\n(= (choice) (+ (coin 2) (coin 3)))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[4]\n[24]\n', 0),
    ('nested', 'petta', '(= (coin $n) $n)\n(= (coin $n) (+ $n 1))\n(= (choice) (+ (coin 2) (coin 3)))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '4\n24\n', 0),
    ('numeric-overflow', 'he', '(= (choice) 9223372036854775807)\n(= (choice) 1)\n(= (choice) 2)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[3]\n[9223372036854775810]\n', 0),
    ('numeric-overflow', 'petta', '(= (choice) 9223372036854775807)\n(= (choice) 1)\n(= (choice) 2)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '3\n9223372036854775810\n', 0),
    ('nondeterministic-step', 'he', '(= (choice) 3)\n(= (choice) 3)\n(= (both $a $x) (+ $a $x))\n(= (both $a $x) (- $a $x))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (both $a $x)))\n', '[2]\n[6, 0, 0, -6]\n', 0),
    ('nondeterministic-step', 'petta', '(= (choice) 3)\n(= (choice) 3)\n(= (both $a $x) (+ $a $x))\n(= (both $a $x) (- $a $x))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (both $a $x))\n', '2\n6\n0\n0\n-6\n', 0),
    ('step-effect', 'he', '(= (choice) 3)\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (let $ignored (println! step-effect) (+ $a $x))))\n', 'step-effect\nstep-effect\n[2]\n[8]\n', 0),
    ('step-effect', 'petta', '(= (choice) 3)\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (let $ignored (println! step-effect) (+ $a $x)))\n', 'step-effect\nstep-effect\n2\n8\n', 0),
    ('held-syntax', 'he', '(= (choice) 3)\n!(let $held (collapse (println! held-effect)) (quote $held))\n', 'held-effect\n[(quote (()))]\n', 0),
    ('held-syntax', 'petta', '(= (choice) 3)\n!(let $held (collapse (println! held-effect)) (quote $held))\n', 'held-effect\n(true)\n', 0),
    ('proof-count', 'he', '(= (proof-leaves 0) left)\n(= (proof-leaves 0) right)\n(= (proof-leaves $n) (if (> $n 0) (Node (proof-leaves (- $n 1)) (proof-leaves (- $n 1))) (empty)))\n!(size (collapse (proof-leaves 3)))\n', '[256]\n', 0),
    ('numeric-fold', 'he', '(= (samples $n) (* $n $n))\n(= (samples $n) (if (< $n 1024) (samples (+ $n 1)) (empty)))\n!(let $xs (collapse (samples 0)) (foldl-atom $xs 0 $acc $item (+ $acc $item)))\n', '[358438400]\n', 0),
    ('proof-count', 'petta', '(= (proof-leaves 0) left)\n(= (proof-leaves 0) right)\n(= (proof-leaves $n) (if (> $n 0) (Node (proof-leaves (- $n 1)) (proof-leaves (- $n 1))) (empty)))\n!(length (collapse (proof-leaves 3)))\n', '256\n', 0),
    ('numeric-fold', 'petta', '(= (samples $n) (* $n $n))\n(= (samples $n) (if (< $n 1024) (samples (+ $n 1)) (empty)))\n!(foldl-atom (collapse (samples 0)) 0 $acc $item (+ $acc $item))\n', '358438400\n', 0),
]


def main():
    binary = Path(sys.argv[1]).resolve()
    failures = []
    with tempfile.TemporaryDirectory(prefix="owned-consumers-", dir="runtime") as directory:
        for name, language, text, expected, expected_exit in CASES:
            source = Path(directory) / (name + "-" + language + ".metta")
            source.write_text(text)
            result = subprocess.run([str(binary), "--lang", language, "--profile", "extended",
                str(source)], capture_output=True, text=True, timeout=30)
            if result.returncode != expected_exit or result.stdout != expected:
                failures.append((name, language, result.returncode, result.stdout, result.stderr))
            elif expected_exit and "uncaught PeTTa error:" not in result.stderr:
                failures.append((name, language, "missing fault", result.stderr))
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("owned resumable consumers: " + str(len(CASES)) + " dialect observations passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
