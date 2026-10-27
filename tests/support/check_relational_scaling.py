#!/usr/bin/env python3
"""Cyclic joins do work near their output, not the product of their parts.

Usage: check_relational_scaling.py CETTA_RUNTIME_STATS_BINARY [--reference]

The graph is a star, a hub with an edge to and from each of N spokes, beside
a few disjoint triangles and four-cliques.  Over the star a join of two
`edge` rows on their shared node meets the hub N times N times, and no
triangle or four-clique of the queries crosses it.  Over the graph at two
sizes, four times apart, a triangle query and a four-clique query are
counted, and the triangle query's answers are collected into a set space,
which is then counted.  The answers must equal the graph's, and the work
the run does for them (unification attempts, plus the keys the joins seek)
may grow at most eight times when the graph grows four times: a join that
pairs rows first grows sixteen times.  Rows of bits are left out, so that
the relational plan's intersections answer.  With --reference neither the
native query path nor the relational count runs, and the check is expected
to fail.
"""
import itertools
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (400, 1600)
GROWTH = 8

TRIANGLE = "(, (edge $x $y) (edge $y $z) (edge $z $x))"
CLIQUE = ("(, (edge $a $b) (edge $a $c) (edge $a $d) "
          "(edge $b $c) (edge $b $d) (edge $c $d))")

PROGRAM = """{facts}
!(bind! &t (new-space set))
!(size-atom (collapse (match &self {triangle} t)))
!(size-atom (collapse (match &self {clique} t)))
!(let $r (collapse (match &self {triangle} (add-atom &t (tri $x $y $z)))) done)
!(size-atom (collapse (match &t (tri $x $y $z) t)))
"""


def graph(spokes):
    edges = set()
    for spoke in range(1, spokes + 1):
        edges.add((0, spoke))
        edges.add((spoke, 0))
    first = spokes + 1
    for t in range(spokes // 40):
        a, b, c = first + 3 * t, first + 3 * t + 1, first + 3 * t + 2
        edges |= {(a, b), (b, c), (c, a)}
    first += 3 * (spokes // 40)
    for q in range(spokes // 80):
        nodes = range(first + 4 * q, first + 4 * q + 4)
        edges |= {(u, v) for u, v in itertools.combinations(nodes, 2)}
    return edges


def counts(edges):
    out = {}
    for u, v in edges:
        out.setdefault(u, set()).add(v)
    triangles = sum(1 for x, y in edges for z in out.get(y, ())
                    if x in out.get(z, ()))
    cliques = 0
    for a, b in edges:
        for c in out.get(a, set()) & out.get(b, set()):
            cliques += len(out.get(a, set()) & out.get(b, set()) & out.get(c, set()))
    return triangles, cliques


def counters(report):
    found = {}
    for line in report.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            found[fields[1]] = int(fields[2])
    return found


def main():
    binary = os.path.abspath(sys.argv[1])
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    env.pop("CETTA_RELATIONAL_COUNT_REFERENCE", None)
    env["CETTA_DENSE_RELATION_REFERENCE"] = "1"
    if "--reference" in sys.argv[2:]:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
        env["CETTA_RELATIONAL_COUNT_REFERENCE"] = "1"
    work = []
    with tempfile.TemporaryDirectory(prefix="cetta-relational-scaling-") as tmp:
        for spokes in SIZES:
            edges = graph(spokes)
            triangles, cliques = counts(edges)
            expected = [str(triangles), str(cliques), "done", str(triangles)]
            facts = "\n".join(f"(edge {u} {v})" for u, v in sorted(edges))
            path = os.path.join(tmp, f"star_{spokes}.metta")
            with open(path, "w") as f:
                f.write(PROGRAM.format(facts=facts, triangle=TRIANGLE,
                                       clique=CLIQUE))
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            answers = p.stdout.split()
            if p.returncode != 0 or answers[-4:] != expected:
                print(f"FAIL: over {spokes} spokes the queries answered "
                      f"{answers[-4:]}, expected {expected}")
                return 1
            found = counters(p.stderr)
            work.append(found.get("unification-attempt", 0) +
                        found.get("match-relational-key-seeks", 0))
    if work[1] > GROWTH * max(work[0], 1):
        print(f"FAIL: the work grew from {work[0]} to {work[1]} when the graph "
              f"grew {SIZES[1] // SIZES[0]} times, more than {GROWTH} times")
        return 1
    print(f"PASS: triangles and four-cliques over a star of {SIZES[0]} and "
          f"{SIZES[1]} spokes, counted and collected: work {work[0]} then "
          f"{work[1]}, within {GROWTH} times")
    return 0


if __name__ == "__main__":
    sys.exit(main())
