#!/usr/bin/env python3
"""Differential mutation of MMB files: native verifier vs an mm0-c build.

Each mutant is a copy of a real MMB file with a few random byte changes
(bit flips, byte replacements, small inserts/deletes in the proof stream).
Both verifiers check it without a specification (mm0-c built with
-DNO_PARSER, native with --no-spec); the verdicts are compared.

    python3 tests/mm0/fuzz_mmb.py NATIVE-mm0_check MM0C-NO-PARSER FILE.mmb COUNT SEED OUTDIR

Disagreements are kept in OUTDIR; tests/mm0/test_mmb.py lists the explained
differences (reserved type bit 55 is the one random mutation reaches).
"""
import multiprocessing
import os
from pathlib import Path
import random
import subprocess
import sys
import struct


def mutate(data, rng):
    b = bytearray(data)
    p_proof = struct.unpack_from("<I", b, 24)[0]
    for _ in range(rng.choice([1, 1, 1, 2, 3])):
        kind = rng.random()
        lo = 0 if rng.random() < 0.2 else p_proof
        at = rng.randrange(lo, len(b))
        if kind < 0.6:
            b[at] ^= 1 << rng.randrange(8)
        elif kind < 0.85:
            b[at] = rng.randrange(256)
        elif kind < 0.93:
            del b[at]
        else:
            b.insert(at, rng.randrange(256))
    return bytes(b)


def verdict(cmd, path):
    try:
        p = subprocess.run(cmd + [str(path)], stdin=subprocess.DEVNULL, capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        return "timeout", ""
    if p.returncode < 0 or p.returncode >= 128 and p.returncode != 255:
        return f"crash{p.returncode}", p.stderr.decode(errors="replace")[-300:]
    return ("accept" if p.returncode == 0 else "reject"), p.stderr.decode(errors="replace")[-300:]


def work(args):
    native, mm0c, base, start, count, seed, outdir = args
    data = Path(base).read_bytes()
    rng = random.Random(seed * 1000003 + start)
    results = []
    for i in range(start, start + count):
        m = mutate(data, rng)
        path = Path(outdir) / f"m{i}.mmb"
        path.write_bytes(m)
        n, nerr = verdict([native, "--no-spec"], path)
        c, cerr = verdict([mm0c], path)
        if n == c:
            path.unlink()
        results.append((i, n, c, nerr.strip().splitlines()[-1:] if nerr else [],
                        cerr.strip().splitlines()[:1] if cerr else []))
    return results


def main():
    native, mm0c, base, count, seed, outdir = sys.argv[1:7]
    count, seed = int(count), int(seed)
    Path(outdir).mkdir(parents=True, exist_ok=True)
    jobs = os.cpu_count() or 2
    chunk = (count + jobs - 1) // jobs
    tasks = [(native, mm0c, base, s, min(chunk, count - s), seed, outdir) for s in range(0, count, chunk)]
    with multiprocessing.Pool(jobs) as pool:
        results = [r for part in pool.map(work, tasks) for r in part]
    table = {}
    for _, n, c, _, _ in results:
        table[(n, c)] = table.get((n, c), 0) + 1
    print(f"base {base}: {count} mutants, seed {seed}")
    for (n, c), k in sorted(table.items()):
        print(f"  native {n:8} mm0-c {c:10} {k}")
    disagreements = [r for r in results if r[1] != r[2]]
    for i, n, c, nerr, cerr in disagreements[:60]:
        print(f"  m{i}: native {n} {nerr} | mm0-c {c} {cerr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
