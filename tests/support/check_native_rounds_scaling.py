#!/usr/bin/env python3
"""A layered search's rounds run as native joins with a guard.

Usage: check_native_rounds_scaling.py CETTA_RUNTIME_STATS_BINARY

Each round expands the states of its layer along the edges of a grid graph,
and a function of one equation keeps a successor that the layer before does
not hold: `(if (== (collapse (match &L (layer $km $t) t)) ()) (add-atom $N $t)
done)`.  On the native path the call is its body and the absence a guard
read on the rows the round began with, so no successor costs an equation
call.  For grids of N and 2N on a side, the search reaches every state, and
the equation calls the machine answers stay below one in a hundred
successors.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIDES = (24, 48)
CALLS_PER_SUCCESSOR = 0.01

PROGRAM = """!(bind! &L (new-space set))
!(add-atom &L (layer 0 (c 0 0)))
(= (fresh $km $t $N)
   (if (== (collapse (match &L (layer $km $t) t)) ()) (add-atom $N $t) done))
(= (round $k $k1)
   (let* (($km (- $k 1)) ($N (new-space set))
          ($a (size-atom (collapse (match &L (layer $k $s)
                (match &self (edge $s $t) (fresh $km $t $N)))))))
     (size-atom (collapse (match $N $t (add-atom &L (layer $k1 $t)))))))
(= (bfs $k $D)
   (if (== $k $D) $k
       (let* (($k1 (+ $k 1)) ($n (round $k $k1)))
         (if (== $n 0) $k1 (bfs $k1 $D)))))
!(bfs 0 1000)
!(size-atom (collapse (match &L (layer $k $s) t)))
"""


def grid(side):
    edges = []
    for i in range(side):
        for j in range(side):
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                a, b = i + di, j + dj
                if 0 <= a < side and 0 <= b < side:
                    edges.append(f"(edge (c {i} {j}) (c {a} {b}))")
    return edges


def run(binary, work, side):
    edges = grid(side)
    path = os.path.join(work, f"grid_{side}.metta")
    with open(path, "w") as f:
        f.write("\n".join(edges) + "\n" + PROGRAM)
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    printed = [l for l in p.stdout.splitlines()
               if l.strip() and not l.startswith("runtime-")]
    if p.returncode != 0 or not printed or printed[-1] != str(side * side):
        print(f"FAIL: the search of the {side}x{side} grid reached "
              f"{printed[-1:]} states, expected {side * side}")
        return None
    calls = None
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[:2] == ["runtime-counter",
                                               "open-equation-answer"]:
            calls = int(fields[2])
    if calls is None:
        # a counter the binary always reports; its absence is no count
        stats = any(l.startswith("runtime-counter ") for l in
                    (p.stdout + p.stderr).splitlines())
        if not stats:
            print("FAIL: the binary reports no runtime counters")
            return None
        calls = 0
    return calls, len(edges)


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-native-rounds-") as work:
        for side in SIDES:
            measured = run(binary, work, side)
            if measured is None:
                return 1
            calls, successors = measured
            if calls > CALLS_PER_SUCCESSOR * successors:
                print(f"FAIL: the {side}x{side} search answered {calls} equation "
                      f"calls for {successors} successors")
                return 1
    print(f"PASS: layered searches of {SIDES[0]}x{SIDES[0]} and "
          f"{SIDES[1]}x{SIDES[1]} grids reach every state with their rounds "
          f"run natively")
    return 0


if __name__ == "__main__":
    sys.exit(main())
