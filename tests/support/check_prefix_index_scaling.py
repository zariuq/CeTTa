#!/usr/bin/env python3
"""Lookups that bind a prefix read that prefix's index, not the trie.

Usage: check_prefix_index_scaling.py CETTA_RUNTIME_STATS_BINARY

A relation (tri a b c) holds four rows for every pair (a, b), and a relation
(sig (bits ...) n) a signature of numbers per row.  Each (q a b) asks the
rows of its pair, nested in a match over q, and each signature is counted
by its rows.  At N and 4 N rows, the space builds no trie (it would hold
every row), the rows offered per probe stay flat, and the answers are the
expected ones.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (2000, 8000)
GROWTH = 1.25


def program(rows):
    pairs = rows // 4
    side = int(pairs ** 0.5) + 1
    lines = []
    for index in range(rows):
        pair = index % pairs
        lines.append(f"(tri {pair // side} {pair % side} {index})")
    signatures = rows // 8
    for index in range(signatures):
        bits = " ".join(str((index >> b) & 1) for b in range(12))
        lines.append(f"(sig (bits {bits}) {index})")
    probes = [(p // side, p % side) for p in range(0, pairs, 3)]
    lines += [f"(q {a} {b})" for a, b in probes]
    lines.append("!(size-atom (collapse (match &self (q $a $b) "
                 "(match &self (tri $a $b $c) $c))))")
    counted = [index for index in range(0, signatures, 5)]
    for index in counted:
        bits = " ".join(str((index >> b) & 1) for b in range(12))
        lines.append(f"!(size-atom (collapse (match &self (sig (bits {bits}) $n) t)))")
    answers = [str(4 * len(probes))] + ["1"] * len(counted)
    return "\n".join(lines) + "\n", answers, len(probes) + len(counted)


def run(binary, work, rows):
    text, answers, probes = program(rows)
    path = os.path.join(work, f"prefix_{rows}.metta")
    with open(path, "w") as f:
        f.write(text)
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines() if not l.startswith("runtime-")]
    if p.returncode != 0 or printed != answers:
        print(f"FAIL: over {rows} rows the lookups answered "
              f"{printed[:3]}..., expected {answers[:3]}...")
        return None
    counters = {}
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            counters[fields[1]] = int(fields[2])
    offered = counters.get("match-native-candidates", 0) / probes
    return offered, counters.get("native-pattern-index-build-row", 0)


def main():
    binary = os.path.abspath(sys.argv[1])
    results = []
    with tempfile.TemporaryDirectory(prefix="cetta-prefix-index-") as work:
        for rows in SIZES:
            measured = run(binary, work, rows)
            if measured is None:
                return 1
            results.append(measured)
    (small, small_trie), (large, large_trie) = results
    if small_trie or large_trie:
        print(f"FAIL: the trie was built over {small_trie} and {large_trie} rows")
        return 1
    if large > GROWTH * small + 1:
        print(f"FAIL: probes were offered {small:.1f} rows each over {SIZES[0]} "
              f"rows and {large:.1f} over {SIZES[1]}")
        return 1
    print(f"PASS: bound prefixes read their indexes, no trie: {small:.1f} and "
          f"{large:.1f} rows offered a probe over {SIZES[0]} and {SIZES[1]} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
