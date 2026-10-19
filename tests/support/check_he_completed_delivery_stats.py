#!/usr/bin/env python3
"""A shared value is evaluated once per distinct node.  A DAG of depth n has
n + 1 distinct nodes and 2^(n+1) - 1 paths; evaluating it, typing it and
comparing it must cost the same at each further level, so every work counter
read here grows by the same amount from depth n to n + 1 as from n + 1 to
n + 2, with the machine on and off."""

import os
import subprocess
import sys
import tempfile

DEPTHS = (20, 21, 22)
ROUTES = (("machine on", {}),
          ("machine off", {"CETTA_PREPARED_PURE_REFERENCE": "1"}))
TYPED_WORK = ("query-equations", "he-completed-value-certified",
              "he-completed-value-delivered")
TYPED = """(: Tree Type)
(: leaf (-> Number Tree))
(: node (-> Tree Tree Tree))
(= (tdag $n) (if (== $n 0) (leaf 1) (let $t (tdag (- $n 1)) (node $t $t))))
!(let $d (tdag {n}) (get-type $d))
!(== (tdag {n}) (tdag {n}))
"""
UNTYPED = """(= (dag $n $leaf) (if (== $n 0) $leaf (let $t (dag (- $n 1) $leaf) (pair $t $t))))
!(== (dag {n} 1) (dag {n} 1.0))
"""
# What each route's work is counted by.  The untyped program runs whole in
# a machine on both routes, so its work is what that machine allocates and,
# with the prepared machine off, the unifications it tries.
PROGRAMS = (
    ("typed", TYPED, "[Tree]\n[True]\n",
     {"machine on": TYPED_WORK, "machine off": TYPED_WORK}),
    ("untyped", UNTYPED, "[True]\n",
     {"machine on": ("eval-arena-alloc-bytes",),
      "machine off": ("eval-arena-alloc-bytes", "unification-attempt")}),
)


def run(binary, path, route, expected):
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "he", path],
        capture_output=True, text=True, timeout=120,
        env=dict(os.environ, **route))
    if proc.returncode != 0 or proc.stdout != expected:
        sys.exit(f"FAIL: {path} {route}: answer {proc.stdout!r}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    return counters


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory() as scratch:
        for name, template, expected, work in PROGRAMS:
            for route_name, route in ROUTES:
                runs = []
                for n in DEPTHS:
                    path = os.path.join(scratch, f"{name}_{n}.metta")
                    with open(path, "w") as out:
                        out.write(template.format(n=n))
                    runs.append(run(binary, path, route, expected))
                for counter in work[route_name]:
                    a, b, c = (r.get(counter, 0) for r in runs)
                    if b <= a or b - a != c - b:
                        sys.exit(f"FAIL: {name}, {route_name}: {counter} is "
                                 f"{a}, {b}, {c} at depths {DEPTHS}")
    print("PASS: a shared value costs the same at each further level, "
          "typed or not, with the machine on and off")


if __name__ == "__main__":
    main()
