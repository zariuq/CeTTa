#!/usr/bin/env python3
"""Integers crafted to share one home in the integer table cost about what
random integers cost: 40,000 values whose pages' prefixes the table's former,
public hash sent to one slot load within three times the random values' time
plus 0.2 s (under that hash they took about sixty times as long).

Usage: check_integer_flood.py CETTA
"""
import os
import random
import resource
import subprocess
import sys
import tempfile

M = (1 << 64) - 1
C1 = 0xbf58476d1ce4e5b9
C1_INVERSE = pow(C1, -1, 1 << 64)
K = 24
N = 40000


def inv_xorshift(y, s):
    x = y
    for _ in range(64 // s + 1):
        x = y ^ (x >> s)
    return x & M


def public_hash(x):
    x ^= x >> 30
    x = (x * C1) & M
    x ^= x >> 27
    return x


def public_hash_inv(h):
    x = inv_xorshift(h, 27)
    x = (x * C1_INVERSE) & M
    return inv_xorshift(x, 30)


def signed(u):
    return u - (1 << 64) if u >= (1 << 63) else u


def crafted(n):
    values, j = [], 1
    while len(values) < n:
        prefix = public_hash_inv(j << K)
        j += 1
        if prefix < (1 << 55):
            assert public_hash(prefix) & ((1 << K) - 1) == 0
            values.append(signed(prefix << 9))
    return values


def scattered(n):
    rnd, seen, values = random.Random(7), set(), []
    while len(values) < n:
        u = rnd.getrandbits(64)
        if (u >> 9) in seen:
            continue
        seen.add(u >> 9)
        values.append(signed(u))
    return values


def cpu(cetta, values):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        for v in values:
            f.write(f"(n {v})\n")
        f.write("!(+ 1 1)\n")
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=600)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    if result.returncode != 0 or result.stdout.split() != ["2"]:
        print(f"FAIL: exit {result.returncode}, answers "
              f"{result.stdout.split()[:4]}; {result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def main():
    cetta = sys.argv[1]
    flood, plain = crafted(N), scattered(N)
    flood_cpu = min(cpu(cetta, flood) for _ in range(2))
    plain_cpu = min(cpu(cetta, plain) for _ in range(2))
    if flood_cpu > 3.0 * plain_cpu + 0.2:
        print(f"FAIL: crafted integers take {flood_cpu:.2f} s against "
              f"{plain_cpu:.2f} s for random ones")
        sys.exit(1)
    print(f"PASS: crafted integers take {flood_cpu:.2f} s against "
          f"{plain_cpu:.2f} s for random ones")


if __name__ == "__main__":
    main()
