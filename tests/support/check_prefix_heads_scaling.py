#!/usr/bin/env python3
"""A head's prefix lookups cost the head's rows, not the space's.

Usage: check_prefix_heads_scaling.py CETTA_RUNTIME_STATS_BINARY

Many heads of one row each, every one asked once by a bound prefix: at N and
4 N heads the rows read building indexes grow at most 4.5 times (each head
is read from the head index, no index built for it), and every lookup
answers its row.  One head of many rows among ten times as many rows of
other heads, asked by its prefix: its index is built from its own rows,
fewer than twice them.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (2000, 8000)
GROWTH = 4.5


def counters_of(text):
    counters = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            counters[fields[1]] = int(fields[2])
    # Every fixture loads fresh terms. Static inventory counters can be
    # nonzero even in an engine whose event counters are disabled.
    if "match-position-index-build-row" not in counters or \
            counters.get("term-universe-insert", 0) <= 0:
        raise SystemExit("FAIL: prefix-head scaling requires enabled runtime counters")
    return counters


def run(binary, work, name, text):
    path = os.path.join(work, name)
    with open(path, "w") as f:
        f.write(text)
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines() if not l.startswith("runtime-")]
    return p.returncode, printed, counters_of(p.stdout + p.stderr)


def heads(binary, work, count):
    lines = [f"(h{i} a b{i})" for i in range(count)]
    lines += [f"!(match &self (h{i} a $x) $x)" for i in range(count)]
    rc, printed, counters = run(binary, work, f"heads_{count}.metta",
                                "\n".join(lines) + "\n")
    if rc != 0 or printed != [f"b{i}" for i in range(count)]:
        print(f"FAIL: {count} heads answered {printed[:3]}...")
        return None
    return counters.get("match-position-index-build-row", 0)


def heavy(binary, work):
    rows = 600
    lines = [f"(big {j % 3} r{j})" for j in range(rows)]
    lines += [f"(other{i} x y{i})" for i in range(10 * rows)]
    asked = [f"!(size-atom (collapse (match &self (big {k} $r) $r)))"
             for k in (0, 1, 2) for _ in range(4)]
    rc, printed, counters = run(binary, work, "heavy.metta",
                                "\n".join(lines + asked) + "\n")
    if rc != 0 or printed != [str(rows // 3)] * len(asked):
        print(f"FAIL: the heavy head answered {printed[:3]}..., expected "
              f"{rows // 3} each")
        return None
    return rows, counters.get("match-position-index-build-row", 0)


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-prefix-heads-") as work:
        built = [heads(binary, work, n) for n in SIZES]
        measured = heavy(binary, work)
    if None in built or measured is None:
        return 1
    small, large = built
    if large > GROWTH * small + SIZES[1]:
        print(f"FAIL: building indexes read {small} rows for {SIZES[0]} heads and "
              f"{large} for {SIZES[1]}")
        return 1
    rows, heavy_built = measured
    if heavy_built > 2 * rows:
        print(f"FAIL: a head of {rows} rows among {10 * rows} others was indexed "
              f"reading {heavy_built} rows")
        return 1
    print(f"PASS: prefix lookups cost their head's rows: {small} and {large} rows "
          f"read building indexes for {SIZES[0]} and {SIZES[1]} heads; a head of "
          f"{rows} rows indexed reading {heavy_built}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
