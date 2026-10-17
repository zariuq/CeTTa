#!/usr/bin/env python3
"""Extending explicitly evaluated data revisits new structure, not its whole tail."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
WORK = "petta-translation-value-node"
REUSE = "petta-translation-value-reuse"
RULES = """
(= (push $i $xs) (eval (node $i $xs)))
(= (touch $n $xs)
   (if (== $n 0) $xs
       (let $ignored (eval (probe 1)) (touch (- $n 1) $xs))))
(= (grow $i $n $xs)
   (if (== $i $n) $xs (grow (+ $i 1) $n INSERT)))
(= (length-of () $n) $n)
(= (length-of (node $h $t) $n) (length-of $t (+ $n 1)))
"""


def run(binary, directory, n, quadratic):
    insertion = "(push $i $xs)"
    if quadratic:
        insertion = "(touch $i (push $i $xs))"
    source = RULES.replace("INSERT", insertion)
    source += f"!(length-of (grow 0 {n} ()) 0)\n"
    path = Path(directory) / f"{'quadratic' if quadratic else 'linear'}-{n}.metta"
    path.write_text(source)
    proc = subprocess.run([binary, "--lang", "petta", "--emit-runtime-stats", str(path)],
                          cwd=ROOT, env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1"),
                          capture_output=True, text=True, timeout=120)
    if proc.returncode or proc.stdout.strip() != str(n):
        raise AssertionError(f"Wrong answer at {n}: {proc.returncode}, {proc.stdout}, {proc.stderr}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    assert counters.get(WORK, 0) > 0, "Translation work counter missing or inactive"
    assert counters.get(REUSE, 0) > 0, "No surviving translation evidence was reused"
    return counters[WORK]


def ratios(work):
    return [b / a for a, b in zip(work, work[1:])]


binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="translation-work-", dir=ROOT / "runtime") as directory:
    positive = [run(binary, directory, n, False) for n in (1000, 2000, 4000)]
    negative = [run(binary, directory, n, True) for n in (256, 512)]
assert max(ratios(positive)) < 2.8, f"Data-tail translation is not linear: {positive}"
assert min(ratios(negative)) > 3.2, f"Deliberate repeated evaluation escaped the control: {negative}"
print(f"PASS: explicit data growth has linear translation work {positive}; "
      f"quadratic control {negative} is detected")
