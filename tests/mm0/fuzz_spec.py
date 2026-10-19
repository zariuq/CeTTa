#!/usr/bin/env python3
"""Differential mutation of a .mm0 specification against its MMB file.

Mutations act on the text: replace, delete or duplicate a character or a
whitespace-separated token, or swap two tokens.  Both verifiers read the
mutated specification against the unchanged MMB file.

    python3 tests/mm0/fuzz_spec.py NATIVE-mm0_check MM0C FILE.mmb FILE.mm0 COUNT SEED OUTDIR

Disagreements are kept in OUTDIR; tests/mm0/test_mmb.py lists the explained
differences (notations that leave out or repeat a variable, as in mm0-rs).
"""
import multiprocessing
import os
from pathlib import Path
import random
import re
import subprocess
import sys

ALPHABET = "()$:;.{}>=_-~@[]!? \nabcxyzPQRA0123456789"


def mutate(text, rng):
    tokens = re.findall(r"\S+|\s+", text)
    for _ in range(rng.choice([1, 1, 2])):
        k = rng.random()
        if k < 0.35:
            at = rng.randrange(len(text))
            text = text[:at] + rng.choice(ALPHABET) + text[at + 1:]
        elif k < 0.5:
            at = rng.randrange(len(text))
            text = text[:at] + text[at + 1:]
        else:
            tokens = re.findall(r"\S+|\s+", text)
            words = [i for i, t in enumerate(tokens) if not t.isspace()]
            i = rng.choice(words)
            if k < 0.7:
                del tokens[i]
            elif k < 0.85:
                j = rng.choice(words)
                tokens[i], tokens[j] = tokens[j], tokens[i]
            else:
                tokens.insert(i, tokens[i] + " ")
            text = "".join(tokens)
    return text


def verdict(cmd, mmb, spec_path):
    with open(spec_path, "rb") as f:
        try:
            p = subprocess.run(cmd + [mmb], stdin=f, capture_output=True, timeout=60)
        except subprocess.TimeoutExpired:
            return "timeout", ""
    out = p.stderr.decode(errors="replace").strip().splitlines()
    if p.returncode < 0 or (p.returncode >= 128 and p.returncode != 255):
        return f"crash{p.returncode}", out[:1]
    return ("accept" if p.returncode == 0 else "reject"), out[:1]


def work(args):
    native, mm0c, mmb, spec, start, count, seed, outdir = args
    text = Path(spec).read_text()
    rng = random.Random(seed * 1000003 + start)
    results = []
    for i in range(start, start + count):
        m = mutate(text, rng)
        path = Path(outdir) / f"s{i}.mm0"
        path.write_text(m)
        n, nerr = verdict([native], mmb, path)
        c, cerr = verdict([mm0c], mmb, path)
        if n == c:
            path.unlink()
        results.append((i, n, c, nerr, cerr))
    return results


def main():
    native, mm0c, mmb, spec, count, seed, outdir = sys.argv[1:8]
    count, seed = int(count), int(seed)
    Path(outdir).mkdir(parents=True, exist_ok=True)
    jobs = os.cpu_count() or 2
    chunk = (count + jobs - 1) // jobs
    tasks = [(native, mm0c, mmb, spec, s, min(chunk, count - s), seed, outdir) for s in range(0, count, chunk)]
    with multiprocessing.Pool(jobs) as pool:
        results = [r for part in pool.map(work, tasks) for r in part]
    table = {}
    for _, n, c, _, _ in results:
        table[(n, c)] = table.get((n, c), 0) + 1
    print(f"spec {spec} against {mmb}: {count} mutants, seed {seed}")
    for (n, c), k in sorted(table.items()):
        print(f"  native {n:8} mm0-c {c:10} {k}")
    for i, n, c, nerr, cerr in [r for r in results if r[1] != r[2]][:80]:
        print(f"  s{i}: native {n} {nerr} | mm0-c {c} {cerr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
