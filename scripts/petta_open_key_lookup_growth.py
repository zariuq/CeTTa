#!/usr/bin/env python3
"""Growth gate for space lookups keyed by lists built inside a match.

A space holds n rules (ctv (P i) (r i) (P i $t) 0.5 0.1) and 2n ground facts
(obs (P i t1) 1), (obs (P i t2) 0).  For every rule, inside the match over the
rules, a key is built from the rule's head with cons-atom, and that key's
rules and fact are looked up, as SupplyNet's exact library does.  The program
runs at n and at 2n, and the space's reads are counted: the reads opened
(space-occurrence-cursor-open) and the rows they offer to unification
(space-occurrence-cursor-row).  The rows per read is the work the index leaves
to unification.  The gate fails only on its growth class (a ratio of at least
1.5 at 2n where it must be flat), never on a count, and the counts depend
neither on time nor on load.

  built     the key (P i t1), complete once its last tail is: each read
            offers the rows the same flat key selects, so the rows per read
            stay flat in n.
  open      degraded: the key (P i ...) with an unbound tail, which may spell
            a list of any length, so the fact's read has no bound position
            to select by and offers the facts in order until one unifies.
            Its rows per read must grow with n, or the count proves nothing.

Both answer every rule with its one rule and its fact's value 1.

Usage: petta_open_key_lookup_growth.py CETTA_BINARY (a build with runtime
stats).
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path


COUNTER_RE = re.compile(r"^runtime-counter\s+(\S+)\s+(\d+)\s*$")
FLAT = 1.5
SIZE = 500

KEYS = {
    "built": "(with-period $c t1)",
    "open": "(cons-atom (car-atom $c) (cons-atom (car-atom (cdr-atom $c)) $rest))",
}


def program(n: int, key: str) -> str:
    return f"""!(bind! &kb (new-space))
(= (with-period $s $t)
   (if (== (cdr-atom $s) ()) ($t) (cons-atom (car-atom $s) (with-period (cdr-atom $s) $t))))
(= (head-key $s) ((car-atom $s) (car-atom (cdr-atom $s))))
(= (rules-of $x) (let $k (head-key $x) (collapse (match &kb (ctv $k $n $x $p $q) $n))))
(= (obs-of $x) (once (match &kb (obs $x $v) $v)))
(= (twos $l) (if (== $l ()) 0 (+ (if (== (car-atom $l) 2) 1 0) (twos (cdr-atom $l)))))
(= (fill $i $n)
   (if (>= $i $n) done
       (let* (($j (+ $i 1))
              ($_ (add-atom &kb (ctv (P $i) (r $i) (P $i $t) 0.5 0.1)))
              ($_ (add-atom &kb (obs (P $i t1) 1)))
              ($_ (add-atom &kb (obs (P $i t2) 0))))
          (fill $j $n))))
!(fill 0 {n})
!(let $found (collapse (match &kb (ctv $k $n $c $p $q)
                 (let $x {key} (+ (length (rules-of $x)) (obs-of $x)))))
   (answers (length $found) (twos $found)))
"""


def counters(stderr: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in stderr.splitlines():
        match = COUNTER_RE.match(line.strip())
        if match:
            result[match.group(1)] = int(match.group(2))
    return result


def run(binary: str, work: Path, name: str, n: int) -> tuple[int, int]:
    path = work / f"{name}-{n}.metta"
    path.write_text(program(n, KEYS[name]))
    command = [binary, "--emit-runtime-stats", "--lang", "petta", str(path)]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"{name} at {n}: exit {completed.returncode}\n"
                           f"{completed.stdout}\n{completed.stderr[-2000:]}")
    expected = f"(answers {n} {n})"
    answers = [line for line in completed.stdout.splitlines()
               if line.startswith("(answers")]
    if answers != [expected]:
        raise RuntimeError(f"{name} at {n}: answers {answers}, "
                           f"expected {expected}")
    found = counters(completed.stderr)
    opened = found.get("space-occurrence-cursor-open")
    rows = found.get("space-occurrence-cursor-row")
    if not opened or rows is None:
        raise RuntimeError(f"{name} at {n}: the space's read counters are "
                           "absent (a build without runtime stats?)")
    return opened, rows


def gate(binary: str, work: Path, name: str, flat: bool) -> bool:
    small = run(binary, work, name, SIZE)
    large = run(binary, work, name, 2 * SIZE)
    per_small = small[1] / small[0]
    per_large = large[1] / large[0]
    ratio = per_large / per_small
    holds = ratio < FLAT if flat else ratio >= FLAT
    verdict = "PASS" if holds else "FAIL"
    claim = ("rows per read flat" if flat
             else "degraded variant shows the scan it rules out")
    print(f"{verdict}: open-key lookup growth {name} ({claim}): "
          f"n={SIZE} reads={small[0]} rows={small[1]} per-read={per_small:.2f}; "
          f"n={2 * SIZE} reads={large[0]} rows={large[1]} "
          f"per-read={per_large:.2f}; ratio={ratio:.2f}")
    return holds


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="open-key-growth-") as tmp:
        work = Path(tmp)
        ok = gate(binary, work, "built", True)
        ok = gate(binary, work, "open", False) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
