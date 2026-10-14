#!/usr/bin/env python3
"""HE's frames on the shared heap machine hold what the live continuation can
still reach.  A tail walk keeps a constant support, so doubling its depth
keeps its frame bytes; a walk whose sums are pending keeps a frame per level,
so its frame bytes grow with the depth it holds.  A fresh variable in each
body keeps both walks on the heap machine's frames, since the compiled tier
declines a body that introduces one."""

import os
import subprocess
import sys
import tempfile

DEPTHS = (1000, 2000, 4000)
TAIL = ("(= (walk $n $acc) (if (== $n 0) $acc"
        " (let $v (Box $fresh) (walk (- $n 1) (+ $acc 1)))))\n"
        "!(walk {n} 0)\n")
PENDING = ("(= (sumf $n) (if (== $n 0) 0"
           " (let $v (Box $fresh) (+ $n (sumf (- $n 1))))))\n"
           "!(sumf {n})\n")
COUNTER = "bindings-frame-storage-bytes-peak"


def run(binary, directory, name, template, depth, expected):
    path = os.path.join(directory, f"{name}_{depth}.metta")
    with open(path, "w") as handle:
        handle.write(template.format(n=depth))
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "he", "--profile",
         "extended", path],
        capture_output=True, text=True, timeout=600)
    if proc.returncode != 0 or proc.stdout.strip() != f"[{expected}]":
        sys.exit(f"FAIL: {name} at depth {depth}: answer "
                 f"{proc.stdout.strip()!r}")
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter" and \
                parts[1] == COUNTER:
            return int(parts[2])
    sys.exit(f"FAIL: {COUNTER} missing from the runtime statistics")


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(dir="runtime") as directory:
        tail = [run(binary, directory, "tail", TAIL, depth, depth)
                for depth in DEPTHS]
        pending = [run(binary, directory, "pending", PENDING, depth,
                       depth * (depth + 1) // 2)
                   for depth in DEPTHS]
    if min(tail) <= 0 or min(pending) <= 0:
        sys.exit("FAIL: no frame storage: the walks did not run on the heap "
                 "machine's frames")
    if tail[-1] * 10 > tail[0] * 11:
        sys.exit(f"FAIL: a constant support's frame bytes grow with depth: "
                 f"{tail}")
    for smaller, larger in zip(pending, pending[1:]):
        if larger * 10 < smaller * 14:
            sys.exit(f"FAIL: pending frames do not grow with the depth they "
                     f"hold: {pending}")
    print(f"PASS: frame bytes follow the reachable support (tail {tail}, "
          f"pending {pending})")


if __name__ == "__main__":
    main()
