#!/usr/bin/env python3
"""One index serves every head, found within a logarithmic comparison allowance.

Every head is asked by the same bound column, then all heads are revisited
in rounds: the space builds the column's index once, however many heads
ask, and each lookup finds it within the allowance.  Adds of every head
then update that one index once each.  The fixture retains native rows so
the relation shortcut cannot bypass the directory. Event counters must
establish that the intended route ran.
Usage: check_index_directory_scaling.py CETTA_WITH_RUNTIME_STATS
"""
import os
import subprocess
import sys
import tempfile


def run(binary, heads, updates=False):
    lines = ["(= (q $h) (size-atom (collapse (match &self ($h 1 $r) $r))))"]
    lines += [f"(h{i} {j % 3} r{j})" for i in range(heads) for j in range(33)]
    rounds = 1 if updates else 32
    lines += [f"!(q h{i})" for _ in range(rounds) for i in range(heads)]
    expected = ["11"] * (rounds * heads)
    if updates:
        lines += [f"!(let $_ (add-atom &self (h{i} 1 extra{i})) (q h{i}))"
                  for i in range(heads)]
        expected += ["12"] * heads
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as source:
        source.write("\n".join(lines) + "\n")
        path = source.name
    try:
        result = subprocess.run([binary, "--emit-runtime-stats", "--profile",
            "extended", "--lang", "petta", path], capture_output=True,
            text=True, timeout=180, env=dict(os.environ, CETTA_SPACE_DENSE="off"))
    finally:
        os.unlink(path)
    if result.returncode or result.stdout.split() != expected:
        raise SystemExit(f"FAIL: directory answers or exit status for {heads} heads")
    counters = {}
    for line in result.stderr.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == "runtime-counter":
            counters[fields[1]] = int(fields[2])
    required = ["term-universe-insert", "match-position-index-build",
                "match-position-index-directory-probe",
                "match-position-index-directory-probe-peak"]
    if any(counters.get(name, 0) <= 0 for name in required):
        raise SystemExit("FAIL: directory scaling requires enabled event counters and an exercised directory")
    builds = counters["match-position-index-build"]
    probes = counters["match-position-index-directory-probe"]
    peak = counters["match-position-index-directory-probe-peak"]
    # The lookups read one column's index; the prefix they bind may read
    # its own: at most two builds, whatever the heads.
    if builds > 2:
        raise SystemExit(f"FAIL: {heads} heads built {builds} indexes; one index "
                         f"of a kind serves every head")
    if updates:
        visits = counters.get("match-position-index-append-row", 0)
        if not 0 < visits <= 2 * heads:
            raise SystemExit(f"FAIL: {heads} adds of every head: {visits} index updates, "
                             f"{builds} builds; at most two updates per add without rebuilding")
        return visits, builds
    limit = 2 * (heads + 1).bit_length()
    if peak > limit or probes > 33 * heads * limit:
        raise SystemExit(f"FAIL: {heads} heads: {builds} builds, {probes} comparisons, "
                         f"peak {peak}, at most {limit} per lookup")
    return probes, peak, builds


def main():
    binary = os.path.abspath(sys.argv[1])
    observations = [(n, *run(binary, n)) for n in (160, 640)]
    print("PASS: directory comparisons (heads, total, peak, builds):", observations)
    updates = [(n, *run(binary, n, updates=True)) for n in (160, 640)]
    print("PASS: append work (heads, index updates, builds):", updates)


if __name__ == "__main__":
    main()
