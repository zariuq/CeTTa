#!/usr/bin/env python3
"""Values crafted to share one home in a path index cost about what random
values cost: 80,000 facts (p I V) whose values V the index's former, public
placement hash (the splitmix64 finalizer, invertible) sends to one slot,
read back through the index by (p $x V), load and answer within three times
the random values' time plus 0.2 s (under that hash they took about
thirty times as long).

An integer field's key is the value itself, so the values are chosen by
inverting the finalizer at hashes whose low 24 bits are zero.

Usage: check_path_flood.py CETTA
"""
import os
import random
import resource
import subprocess
import sys
import tempfile

M = (1 << 64) - 1
C1 = 0xbf58476d1ce4e5b9
C2 = 0x94d049bb133111eb
K = 24
N = 80000
QUERIES = 64


def inv_xorshift(y, s):
    x = y
    for _ in range(64 // s + 1):
        x = y ^ (x >> s)
    return x & M


def path_hash(k):
    k ^= k >> 30
    k = (k * C1) & M
    k ^= k >> 27
    k = (k * C2) & M
    return k ^ (k >> 31)


def path_hash_inv(h):
    x = inv_xorshift(h, 31)
    x = (x * pow(C2, -1, 1 << 64)) & M
    x = inv_xorshift(x, 27)
    x = (x * pow(C1, -1, 1 << 64)) & M
    return inv_xorshift(x, 30)


def signed(u):
    return u - (1 << 64) if u >= (1 << 63) else u


def crafted():
    values = []
    for j in range(1, N + 1):
        k = path_hash_inv(j << K)
        assert path_hash(k) & ((1 << K) - 1) == 0
        values.append(signed(k))
    return values


def scattered():
    rnd = random.Random(7)
    seen = set()
    values = []
    while len(values) < N:
        v = signed(rnd.getrandbits(64))
        if v not in seen:
            seen.add(v)
            values.append(v)
    return values


def cpu(cetta, values):
    probes = values[::max(1, len(values) // QUERIES)][:QUERIES]
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        for i, v in enumerate(values):
            f.write(f"(p {i} {v})\n")
        for v in probes:
            f.write(f"!(match &self (p $x {v}) $x)\n")
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=600)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    expected = [str(values.index(v)) for v in probes]
    if result.returncode != 0 or result.stdout.split() != expected:
        print(f"FAIL: exit {result.returncode}, answers "
              f"{result.stdout.split()[:4]} where {expected[:4]}; "
              f"{result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    cetta = sys.argv[1]
    flood = crafted()
    plain = scattered()
    plain_cpu = min(cpu(cetta, plain) for _ in range(2))
    flood_cpu = min(cpu(cetta, flood) for _ in range(2))
    if flood_cpu > 3.0 * plain_cpu + 0.2:
        print(f"FAIL: crafted path keys take {flood_cpu:.2f} s against "
              f"{plain_cpu:.2f} s for random ones")
        sys.exit(1)
    print(f"PASS: crafted path keys take {flood_cpu:.2f} s against "
          f"{plain_cpu:.2f} s for random ones ({N} facts, {QUERIES} reads)")


if __name__ == "__main__":
    main()
