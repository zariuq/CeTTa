#!/usr/bin/env python3
"""A rule over dense binary relations makes each instance it adds into a set
once, however many solutions give it.

Usage: check_dense_relations.py CETTA_RUNTIME_STATS_BINARY [--reference]

Over a random graph with N nodes, each edge present with probability 0.3, a
rule adds (two x z) into a set space for each path x -> y -> z, and the
triangles are counted.  Paths of two edges grow with N cubed, the pairs
they join with N squared.  Joined by rows of bits, the rule gathers the
z of every y for each x before it makes an instance, so the instances it
makes grow with the pairs: at most five times when N doubles, where one
instance per path grows eight times.  The answers must equal the graph's.
With --reference rows of bits are not used, and the check is expected to
fail.
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (40, 80)
GROWTH = 5

PROGRAM = """{facts}
!(bind! &t (new-space set))
!(let $r (collapse (match &self (, (edge $x $y) (edge $y $z)) (add-atom &t (two $x $z)))) done)
!(size-atom (collapse (match &t (two $x $z) t)))
!(size-atom (collapse (match &self (, (edge $x $y) (edge $y $z) (edge $z $x)) t)))
"""


def graph(rng, nodes):
    return sorted((u, v) for u in range(nodes) for v in range(nodes)
                  if rng.random() < 0.3)


def answers(edges):
    out = {}
    for u, v in edges:
        out.setdefault(u, set()).add(v)
    pairs = {(x, z) for x, y in edges for z in out.get(y, ())}
    triangles = sum(1 for x, y in edges for z in out.get(y, ())
                    if x in out.get(z, ()))
    return len(pairs), triangles


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
    env.pop("CETTA_DENSE_RELATION_REFERENCE", None)
    if "--reference" in sys.argv[2:]:
        env["CETTA_DENSE_RELATION_REFERENCE"] = "1"
    rng = random.Random(20261027)
    made = []
    with tempfile.TemporaryDirectory(prefix="cetta-dense-relations-") as tmp:
        for nodes in SIZES:
            edges = graph(rng, nodes)
            pairs, triangles = answers(edges)
            expected = ["done", str(pairs), str(triangles)]
            facts = "\n".join(f"(edge {u} {v})" for u, v in edges)
            path = os.path.join(tmp, f"graph_{nodes}.metta")
            with open(path, "w") as f:
                f.write(PROGRAM.format(facts=facts))
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            got = p.stdout.split()
            if p.returncode != 0 or got[-3:] != expected:
                print(f"FAIL: over {nodes} nodes the queries answered "
                      f"{got[-3:]}, expected {expected}")
                return 1
            made.append(counters(p.stderr).get("native-query-effect-instance", 0))
    if made[1] > GROWTH * max(made[0], 1):
        print(f"FAIL: the rule made {made[0]} then {made[1]} instances when "
              f"the graph grew from {SIZES[0]} to {SIZES[1]} nodes, more "
              f"than {GROWTH} times")
        return 1
    print(f"PASS: over {SIZES[0]} and {SIZES[1]} nodes the rule made "
          f"{made[0]} then {made[1]} instances, within {GROWTH} times")
    return 0


if __name__ == "__main__":
    sys.exit(main())
