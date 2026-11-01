#!/usr/bin/env python3
"""Spellings crafted to share one home in the symbol table intern about as
fast as random spellings of the same length: 32,768 spellings whose public
hash (FNV-1a) agrees in its low 20 bits, the bits a table of up to 2^20
slots placed them by before placement took a key of its own, intern within
three times the random spellings' time plus 0.2 s (placed by the hash
alone, they took about a hundred times as long).

The crafted set: FNV-1a's low k bits depend only on the low k bits before
them.  At each of 15 positions two 3-byte blocks are found that reach the
same low 20 bits from the state the position starts in; any choice of one
block per position reaches the same state, so the 2^15 spellings share one
home.

Usage: check_symbol_flood.py CETTA
"""
import itertools
import os
import random
import resource
import subprocess
import sys
import tempfile

MASK20 = (1 << 20) - 1
P = 1099511628211
ALPHABET = b"abcdefghijklmnopqrstuvwxyz0123456789"


def fnv_low(state, data):
    for byte in data:
        state = ((state ^ byte) * P) & MASK20
    return state


def crafted():
    prefix = b"qkm8"
    state = fnv_low(1469598103934665603 & MASK20, prefix)
    pairs = []
    for _ in range(15):
        seen = {}
        for block in itertools.product(ALPHABET, repeat=3):
            block = bytes(block)
            low = fnv_low(state, block)
            if low in seen:
                pairs.append((seen[low], block))
                state = low
                break
            seen[low] = block
        else:
            raise SystemExit("FAIL: no colliding blocks")
    return [prefix + b"".join(choice) for choice in itertools.product(*pairs)]


def scattered(n, length):
    rnd = random.Random(7)
    names = set()
    while len(names) < n:
        names.add(bytes(rnd.choice(ALPHABET) for _ in range(length)))
    return sorted(names)


def cpu(cetta, names):
    with tempfile.NamedTemporaryFile("wb", suffix=".metta", delete=False) as f:
        for name in names:
            f.write(b"(" + name + b")\n")
        f.write(b"!(+ 1 1)\n")
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
    flood = crafted()
    plain = scattered(len(flood), len(flood[0]))
    flood_cpu = min(cpu(cetta, flood) for _ in range(2))
    plain_cpu = min(cpu(cetta, plain) for _ in range(2))
    if flood_cpu > 3.0 * plain_cpu + 0.2:
        print(f"FAIL: crafted spellings take {flood_cpu:.2f} s against "
              f"{plain_cpu:.2f} s for random ones")
        sys.exit(1)
    print(f"PASS: crafted spellings take {flood_cpu:.2f} s against "
          f"{plain_cpu:.2f} s for random ones")


if __name__ == "__main__":
    main()
