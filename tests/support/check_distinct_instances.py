#!/usr/bin/env python3
"""Two spellings of a relation's distinct instances make each once.

Usage: check_distinct_instances.py CETTA_STATS_BINARY

Graphs of 60 and 120 nodes, each with 12 random successors; each program
counts or spreads the ends of the two-step paths into a bag:
- a guarded insert: an addition behind an absence guard that probes the
  row it adds, into the guard's own bag, under a count.  It adds each
  distinct instance once, as an addition into a set does, so the join by
  rows of bits sums out the variable the instance does not read and reads
  the guard once for each instance;
- a distinct spread: unique-atom over the collection of the paths' ends,
  each element added into a bag.  It is answered once for each distinct
  instance, not collected path by path and deduplicated after.
Every count must equal the ordinary path's, the join must run on rows of
bits, the effect instances made must equal the distinct pairs, and the
spread must answer fewer times than there are paths.  The binary is a
runtime-stats build.
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SUCCESSORS = 12
SPELLINGS = {
    "guarded insert": [
        "(= (put $a) (if (== (collapse (match &t $a t)) ()) (add-atom &t $a) done))",
        "!(size-atom (collapse (match &self (edge $x $y) (match &self (edge $y $z) (put (trans $x $z))))))",
    ],
    "distinct spread": [
        "!(let $ps (unique-atom (collapse (match &self (, (edge $x $y) (edge $y $z)) (trans $x $z)))) "
        "(size-atom (collapse (let $a (superpose $ps) (add-atom &t $a)))))",
    ],
}


def program(nodes, rng, spelling):
    lines = [f"(edge {x} {y})"
             for x in range(nodes)
             for y in sorted(rng.sample(range(nodes), SUCCESSORS))]
    lines += ["!(bind! &t (new-space))"] + SPELLINGS[spelling]
    lines.append("!(size-atom (collapse (match &t (trans $x $z) t)))")
    return "\n".join(lines) + "\n"


def run(binary, path, reference):
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    if reference:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                        "--emit-runtime-stats", path],
                       cwd=ROOT, env=env, capture_output=True, text=True,
                       timeout=300)
    counts = [int(line) for line in p.stdout.split() if line.isdigit()]
    counters = {}
    for line in p.stderr.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == "runtime-counter":
            counters[fields[1]] = int(fields[2])
    return p.returncode, counts, counters


def main():
    binary = os.path.abspath(sys.argv[1])
    seen = []
    with tempfile.TemporaryDirectory(prefix="cetta-distinct-") as work:
        for spelling in SPELLINGS:
            rng = random.Random(20261030)
            for nodes in (60, 120):
                path = os.path.join(work, f"paths_{nodes}.metta")
                with open(path, "w") as f:
                    f.write(program(nodes, rng, spelling))
                rc, counts, counters = run(binary, path, False)
                ref_rc, reference, _ = run(binary, path, True)
                if rc != 0 or ref_rc != 0 or counts != reference or \
                        len(counts) != 2:
                    print(f"FAIL: the {spelling} over {nodes} nodes counted "
                          f"{counts}, the ordinary path {reference}")
                    return 1
                answered, pairs = counts
                if counters.get("dense-relation-admission", 0) < 1:
                    print(f"FAIL: the {spelling} over {nodes} nodes did not "
                          f"join on rows of bits")
                    return 1
                made = counters.get("native-query-effect-instance", 0)
                if made != pairs:
                    print(f"FAIL: the {spelling} over {nodes} nodes made "
                          f"{made} instances for {pairs} distinct pairs")
                    return 1
                paths = nodes * SUCCESSORS * SUCCESSORS
                answers = counters.get("native-query-answer", 0)
                if spelling == "distinct spread" and answers >= paths:
                    print(f"FAIL: the {spelling} over {nodes} nodes answered "
                          f"{answers} times for {paths} paths and {pairs} "
                          f"distinct pairs")
                    return 1
                seen.append(f"{spelling} {pairs} instances over {nodes} nodes")
    print("PASS: a relation's distinct instances are made once each, by a "
          "guarded insert and by a distinct spread: " + ", ".join(seen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
