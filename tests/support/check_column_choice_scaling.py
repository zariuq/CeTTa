#!/usr/bin/env python3
"""A lookup reads its most selective bound column.

Usage: check_column_choice_scaling.py CETTA_RUNTIME_STATS_BINARY

A relation (SPO s r o) over four relations, beside a relation (KV k v tag)
of the same size, is probed by (SPO $s r1 $o) inside (probe $o): the
pattern binds the constant r1 and the object, which selects a row or two.
At N and 4 N facts, with N / 10 probes, the rows the position indexes offer
per probe do not grow: the first column after the variable offers a quarter
of the relation, and the object column is built once the probes have read
as many rows as its build.  The control (KV $k $v tag) inside (probe-k $v)
binds a selective first column and a constant tag: it builds one index and
reads the same rows per probe at both sizes.
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (20000, 80000)
GROWTH = 1.25

OBJECT_QUERY = ("!(size-atom (collapse (match &self (probe $o) "
                "(match &self (SPO $s r1 $o) $s))))\n")
CONTROL_QUERY = ("!(size-atom (collapse (match &self (probe-k $v) "
                 "(match &self (KV $k $v tag) $k))))\n")


def program(n, shape):
    rng = random.Random(7)
    objects = n // 2
    spo = [(rng.randrange(n // 8), i % 4, rng.randrange(objects)) for i in range(n)]
    kv = [(i, rng.randrange(objects)) for i in range(n)]
    probes = [rng.randrange(objects) for _ in range(n // 10)]
    lines = [f"(SPO s{s} r{r} o{o})" for s, r, o in spo]
    lines += [f"(KV k{k} v{v} tag)" for k, v in kv]
    if shape == "object":
        lines += [f"(probe o{o})" for o in probes]
        object_rows = {}
        for s, r, o in spo:
            if r == 1:
                object_rows[o] = object_rows.get(o, 0) + 1
        answers = sum(object_rows.get(o, 0) for o in probes)
        return "\n".join(lines) + "\n" + OBJECT_QUERY, answers, len(probes)
    lines += [f"(probe-k v{v})" for v in probes]
    value_rows = {}
    for _, v in kv:
        value_rows[v] = value_rows.get(v, 0) + 1
    answers = sum(value_rows.get(v, 0) for v in probes)
    return "\n".join(lines) + "\n" + CONTROL_QUERY, answers, len(probes)


def run(binary, work, n, shape):
    text, answers, probes = program(n, shape)
    path = os.path.join(work, f"{shape}_{n}.metta")
    with open(path, "w") as f:
        f.write(text)
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines() if not l.startswith("runtime-")]
    if p.returncode != 0 or printed[-1:] != [str(answers)]:
        print(f"FAIL: the {shape} probes over {n} facts answered {printed[-1:]}, "
              f"expected {answers}")
        return None
    counters = {}
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            counters[fields[1]] = int(fields[2])
    return (counters.get("match-position-index-candidate", 0) / probes,
            counters.get("match-position-index-build", 0))


def main():
    binary = os.path.abspath(sys.argv[1])
    results = {}
    with tempfile.TemporaryDirectory(prefix="cetta-column-choice-") as work:
        for shape in ("object", "control"):
            for n in SIZES:
                measured = run(binary, work, n, shape)
                if measured is None:
                    return 1
                results[shape, n] = measured
    for shape in ("object", "control"):
        small, large = results[shape, SIZES[0]][0], results[shape, SIZES[1]][0]
        if large > GROWTH * small + 1:
            print(f"FAIL: {shape} probes read {small:.1f} rows each over {SIZES[0]} "
                  f"facts and {large:.1f} over {SIZES[1]}")
            return 1
    builds = [results["control", n][1] for n in SIZES]
    if builds != [1, 1]:
        print(f"FAIL: the control built {builds} position indexes, expected one")
        return 1
    print("PASS: a lookup reads its most selective bound column: "
          f"{results['object', SIZES[0]][0]:.1f} and {results['object', SIZES[1]][0]:.1f} "
          f"rows a probe over {SIZES[0]} and {SIZES[1]} facts; the control "
          f"{results['control', SIZES[0]][0]:.1f} and {results['control', SIZES[1]][0]:.1f} "
          "with one index")
    return 0


if __name__ == "__main__":
    sys.exit(main())
