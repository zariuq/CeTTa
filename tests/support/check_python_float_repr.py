#!/usr/bin/env python3
"""Python's float repr in CeTTa, against CPython's, over many doubles.

    check_python_float_repr.py CETTA [--random N] [--seed S] [--timeout SECONDS]

The doubles: every power of two, the doubles next to each, subnormals, and N
doubles of random bits (finite).  Each is written into a Python program as its
17-significant-digit literal, which reads back to the same double but is not
its shortest form, so the program's print must find the shortest digits
itself.  CeTTa (`CETTA --lang python`) and CPython run the program; their
outputs must be identical, line by line.
"""
import argparse
import math
import os
import random
import struct
import subprocess
import sys
import tempfile


def doubles(count, seed):
    out = []
    for k in range(-1074, 1024):
        x = math.ldexp(1.0, k)
        out.extend([x, math.nextafter(x, 0.0), math.nextafter(x, math.inf)])
    out.extend(math.ldexp(float(m), -1074) for m in (1, 2, 3, 5, 7, 255, 4503599627370495))
    rng = random.Random(seed)
    while count > 0:
        x = struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64)))[0]
        if math.isfinite(x):
            out.append(x)
            count -= 1
    return [x for x in out if math.isfinite(x) and x != 0.0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta")
    parser.add_argument("--random", type=int, default=3000)
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    xs = doubles(args.random, args.seed)
    program = "".join("print(%s)\n" % ("%.17g" % x) for x in xs)
    with tempfile.TemporaryDirectory() as work:
        path = os.path.join(work, "float_repr.py")
        with open(path, "w") as f:
            f.write(program)
        ref = subprocess.run([sys.executable, "-I", path], capture_output=True, text=True, timeout=args.timeout)
        got = subprocess.run([args.cetta, "--lang", "python", path], capture_output=True, text=True, timeout=args.timeout)
    want = ref.stdout.splitlines()
    have = got.stdout.splitlines()
    bad = [(i, w, h) for i, (w, h) in enumerate(zip(want, have)) if w != h]
    if len(want) != len(have) or bad or got.returncode != 0:
        print("FAIL: float repr: %d doubles, %d lines from CeTTa, %d differ, status %d"
              % (len(xs), len(have), len(bad), got.returncode))
        for i, w, h in bad[:10]:
            print("  %s: CPython %s | CeTTa %s" % ("%.17g" % xs[i], w, h))
        if got.stderr:
            print(got.stderr.strip().splitlines()[-1])
        return 1
    print("PASS: float repr equals CPython's on %d doubles" % len(xs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
