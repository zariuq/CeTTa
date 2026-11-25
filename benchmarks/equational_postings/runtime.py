#!/usr/bin/env python3
"""Measure the present CeTTa selector on the Schema/Range fixture.

The prototype in bench.c is not connected to this binary. Literal matching
on the known raw spelling is a comparison route, not an =e implementation.
"""
import argparse
from pathlib import Path
import statistics
import subprocess
import tempfile
import time


def program(rows, reads, modulo):
    operator = "eq:match" if modulo else "match"
    selector = "(Range Float 0 1)" if modulo else "AI:UnitRange"
    query = f"({operator} &self (Schema {selector} $value) $value)"
    source = ["(=e AI:UnitRange (Range Float 0 1))",
              "(Schema AI:UnitRange hit)"]
    source.extend(f"(Schema Other {i})" for i in range(rows - 1))
    source.extend(f"!(let $_ (add-atom &self (Unrelated {i})) (collapse {query}))"
                  for i in range(reads))
    source.append(f"!(collapse {query})")
    return "\n".join(source)


def measure(binary, fixture, dialect, rows, reads, modulo, repeats):
    fixture.write_text(program(rows, reads, modulo), encoding="utf-8")
    samples = []
    for _ in range(repeats):
        begin = time.perf_counter_ns()
        result = subprocess.run(
            [str(binary), "--lang", dialect, "--profile", "extended",
             str(fixture)],
            text=True, capture_output=True, timeout=12, check=True,
        )
        samples.append(time.perf_counter_ns() - begin)
        want = (["[(hit)]"] if dialect == "he" else ["(hit)"]) * (reads + 1)
        got = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        if got != want:
            raise ValueError(f"{dialect} rows={rows} reads={reads}: {got!r} != {want!r}")
    return statistics.median(samples)


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("cetta", type=Path)
    args.add_argument("--repeats", type=int, choices=(1, 3, 5), default=3)
    args.add_argument("--reads", type=int, default=48)
    opts = args.parse_args()
    if opts.reads <= 0:
        args.error("--reads must be positive")
    binary = opts.cetta.resolve()
    print("dialect,rows,route,load_ns,whole_ns,extra_ns_per_sample", flush=True)
    with tempfile.TemporaryDirectory(prefix="cetta-reverse-query-") as folder:
        fixture = Path(folder) / "schema-range.metta"
        for dialect in ("he", "petta"):
            for rows in (512, 8192):
                for modulo in (False, True):
                    load = measure(binary, fixture, dialect, rows, 0, modulo, opts.repeats)
                    whole = measure(binary, fixture, dialect, rows, opts.reads, modulo, opts.repeats)
                    route = "modulo" if modulo else "literal"
                    print(f"{dialect},{rows},{route},{load:.0f},{whole:.0f},"
                          f"{max(0, whole-load)/opts.reads:.0f}", flush=True)


if __name__ == "__main__":
    main()
