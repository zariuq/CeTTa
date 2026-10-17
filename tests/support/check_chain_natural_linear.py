#!/usr/bin/env python3
"""The natural ancestor query over a chain runs on the heap machines in both
dialects with work and retained memory linear in the chain's length.
Doubling the chain doubles the index probes the query makes and the peak
bytes of the arenas its evaluation holds.  The negative control counts
every node's ancestors, which is quadratic: doubling its chain quadruples
its probes, and the check must see that."""

import os
import subprocess
import sys
import tempfile

SIZES = (1024, 2048, 4096)
RULES = ("(= (ancestors $c) (match &self (parent $p $c) $p))\n"
         "(= (ancestors $c) (match &self (parent $p $c) (ancestors $p)))\n")
NATURAL = "!(let $a (unique (ancestors p0x0)) (ancestor p0x0 $a))\n"
QUADRATIC_SIZES = (256, 512)
QUADRATIC = ("(= (depths $n) (if (== $n 0) 0"
             " (let $s (collapse (ancestors $n))"
             " (+ (size-atom $s) (depths (- $n 1))))))\n"
             "!(depths {n})\n")
WORK = "match-position-index-key-probe"
MEMORY = ("eval-arena-live-bytes-peak", "scratch-arena-live-bytes-peak")


def natural(n):
    facts = "".join(f"(parent p{i}x0 p{i - 1}x0)\n" for i in range(1, n + 1))
    return facts + RULES + NATURAL


def quadratic(n):
    facts = "".join(f"(parent {i} {i - 1})\n" for i in range(1, n + 1))
    return facts + RULES + QUADRATIC.format(n=n)


def run(binary, directory, lang, name, n, text):
    path = os.path.join(directory, f"{name}_{lang}_{n}.metta")
    with open(path, "w") as handle:
        handle.write(text)
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", lang, "--profile",
         "extended", path],
        capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        sys.exit(f"FAIL: {name} ({lang}) at {n} exited {proc.returncode}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    if WORK not in counters or any(m not in counters for m in MEMORY):
        sys.exit("FAIL: work or memory counters missing from the runtime "
                 "statistics")
    return proc.stdout, counters[WORK], sum(counters[m] for m in MEMORY)


def doublings(values):
    return [larger / smaller for smaller, larger in zip(values, values[1:])]


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(dir="runtime") as directory:
        for lang in ("he", "petta"):
            work, memory = [], []
            for n in SIZES:
                out, probes, held = run(binary, directory, lang, "natural",
                                        n, natural(n))
                if out.count("(ancestor p0x0 ") != n:
                    sys.exit(f"FAIL: natural ({lang}) at {n} answered "
                             f"{out.count('(ancestor p0x0 ')} ancestors")
                work.append(probes)
                memory.append(held)
            if max(doublings(work)) > 2.25 or max(doublings(memory)) > 2.25:
                sys.exit(f"FAIL: natural chain ({lang}) grows faster than "
                         f"its length: probes {work}, held bytes {memory}")
            quad = []
            for n in QUADRATIC_SIZES:
                out, probes, _ = run(binary, directory, lang, "quadratic", n,
                                     quadratic(n))
                if str(n * (n - 1) // 2) not in out:
                    sys.exit(f"FAIL: the quadratic control ({lang}) at {n} "
                             f"answered {out.strip()}")
                quad.append(probes)
            if quad[0] == 0 or quad[1] < 3.5 * quad[0]:
                sys.exit(f"FAIL: the quadratic control ({lang}) does not "
                         f"show quadratic probes: {quad}")
            print(f"PASS: {lang} chain natural is linear (probes {work}, "
                  f"held bytes {memory}); quadratic control {quad}")


if __name__ == "__main__":
    main()
