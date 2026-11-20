#!/usr/bin/env python3
"""Coordination local to the spaces a branch touches.

Three PeTTa workloads run as k hyperpose branches, k = 1, 2, 4, 8:

  disjoint  each branch adds to and matches in its own space;
  readers   every branch reads one space;
  writer    k-1 branches read one space while one branch adds to it.

Each run is checked on its answers.  The gate is counted coordination,
never time: with CETTA_GUARD_STATS each worker thread reports the guard
acquisitions it made and how many of them waited.  Branches over disjoint
spaces, and readers of one space, never wait for a space guard; a writer
among readers makes waits, on the writer's sections only.  Wall time and
the time branches spent waiting are reported beside the gate.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

EDGES = [("a", "b"), ("b", "c"), ("c", "d"), ("d", "e"), ("b", "f"), ("f", "g")]
STATS = re.compile(
    r"guard-stats reads=(\d+) writes=(\d+) contended=(\d+) wait_ns=(\d+) "
    r"reacquired=(\d+) leaf=(\d+) leaf_contended=(\d+) leaf_wait_ns=(\d+)")


def disjoint(k, n):
    calls = " ".join("(work &s%d %d 0)" % (i + 1, n) for i in range(k))
    spaces = "\n".join("!(add-atom &s%d (seed))" % (i + 1) for i in range(k))
    program = spaces + """
(= (work $s $n $acc)
   (if (> $n 0)
       (let $_ (add-atom $s (fact $n))
         (let $c (size-atom (collapse (match $s (fact $n) $n)))
           (work $s (- $n 1) (+ $acc $c))))
       $acc))
!(msort (collapse (hyperpose (%s))))
""" % calls
    return program, "(%s)" % " ".join([str(n)] * k)


def one_space(k, n, writer):
    facts = "\n".join("!(add-atom &kb (edge %s %s))" % edge for edge in EDGES)
    readers = k - 1 if writer else k
    calls = ["(reader %d 0)" % n for _ in range(readers)]
    if writer:
        calls.append("(writer %d)" % (n * 4))
    program = """%s
(= (path $x $y) (match &kb (edge $x $y) $y))
(= (path $x $z) (match &kb (edge $x $y) (path $y $z)))
(= (reader $n $acc)
   (if (> $n 0) (reader (- $n 1) (+ $acc (size-atom (collapse (path a $w))))) $acc))
(= (writer $n)
   (if (> $n 0) (let $_ (add-atom &kb (note $n)) (writer (- $n 1))) wrote))
!(msort (collapse (hyperpose (%s))))
""" % (facts, " ".join(calls))
    answers = sorted([str(6 * n)] * readers) + (["wrote"] if writer else [])
    return program, "(%s)" % " ".join(answers)


def run(binary, program, k):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as handle:
        handle.write(program)
        path = handle.name
    try:
        env = dict(os.environ, CETTA_GUARD_STATS="1")
        start = time.monotonic()
        done = subprocess.run(
            [binary, "--num-threads", str(k), "--lang", "petta", path],
            capture_output=True, text=True, env=env, timeout=1800)
        wall = time.monotonic() - start
    finally:
        os.unlink(path)
    workers = [tuple(int(x) for x in m.groups())
               for m in STATS.finditer(done.stderr)]
    lines = [line for line in done.stdout.splitlines()
             if line.strip() and not line.startswith("guard-stats")]
    return done.returncode, lines, wall, workers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--work", type=int, default=400)
    parser.add_argument("--ks", default="1,2,4,8")
    args = parser.parse_args()
    ks = [int(k) for k in args.ks.split(",")]
    failures = []
    print("%-9s %2s %8s %8s %9s %12s %9s %12s" % (
        "workload", "k", "wall_s", "acquire", "contended", "wait_ns/br",
        "leaf_wait", "leaf_ns/br"))
    for kind in ("disjoint", "readers", "writer"):
        for k in ks:
            if kind == "writer" and k < 2:
                continue
            if kind == "disjoint":
                program, expected = disjoint(k, args.work)
            else:
                program, expected = one_space(k, args.work // 8 or 1,
                                              kind == "writer")
            code, lines, wall, workers = run(args.binary, program, k)
            if code != 0 or not lines or lines[-1] != expected:
                failures.append("%s k=%d: answers %r, expected %r (rc=%d)" % (
                    kind, k, lines[-1:] if lines else lines, expected, code))
            branches = max(len(workers), 1)
            acquisitions = sum(w[0] + w[1] for w in workers)
            contended = sum(w[2] for w in workers)
            wait = sum(w[3] for w in workers)
            leaf_contended = sum(w[6] for w in workers)
            leaf_wait = sum(w[7] for w in workers)
            print("%-9s %2d %8.3f %8d %9d %12d %9d %12d" % (
                kind, k, wall, acquisitions, contended, wait // branches,
                leaf_contended, leaf_wait // branches))
            if kind != "writer" and contended != 0:
                failures.append("%s k=%d: %d space-guard acquisitions waited" % (
                    kind, k, contended))
    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print("PASS: disjoint spaces and readers of one space never wait for a "
          "space guard; answers agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
