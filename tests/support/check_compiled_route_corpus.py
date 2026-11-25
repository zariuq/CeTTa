#!/usr/bin/env python3
"""Compare the compiled route with the interpreter over every example and
benchmark program, in both dialects.

Each program runs once untimed (so first-run work such as fetching inputs does
not count), then with the reference settings and with the candidate settings.
Outputs are compared after normalizing what legitimately varies between runs:
memory addresses, fresh variable numbers and printed decimal numbers such as
timings.  Apparent slowdowns are re-timed one at a time, best of three, before
they are reported.

Exit status is nonzero when an answer differs.  This is a qualification check,
not a readiness gate: it takes minutes.

Usage: check_compiled_route_corpus.py CETTA [--reference K=V ...] [--candidate K=V ...]
Defaults: reference CETTA_TCALC_EVAL=0, candidate CETTA_TCALC_EVAL=auto.
"""

import concurrent.futures as cf
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOPS = ("examples", "benchmarks")
TIMEOUT_S = 12
WORKERS = 4
SLOW_RATIO = 1.25
SLOW_FLOOR_S = 0.2

_ADDR = re.compile(r"0x[0-9a-f]+")
_FRESH = re.compile(r"#[0-9]+")
_DECIMAL = re.compile(r"[0-9]+\.[0-9]+")


def normalize(text):
    return _DECIMAL.sub("N.N", _FRESH.sub("#ID", _ADDR.sub("0xADDR", text)))


def parse_env(args, flag, default):
    env = {}
    i = 0
    while i < len(args):
        if args[i] == flag and i + 1 < len(args):
            key, _, value = args[i + 1].partition("=")
            env[key] = value
            i += 2
        else:
            i += 1
    return env or default


def run(cetta, path, lang, extra_env, timeout=TIMEOUT_S):
    env = dict(os.environ)
    env.update(extra_env)
    start = time.perf_counter()
    try:
        proc = subprocess.run([cetta, "--lang", lang, os.path.basename(path)],
                              cwd=os.path.dirname(path), env=env,
                              capture_output=True, timeout=timeout)
        return proc.returncode, time.perf_counter() - start, proc.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return "timeout", float(timeout), ""


def best_of(cetta, path, lang, extra_env, n=3):
    return min(run(cetta, path, lang, extra_env, timeout=60)[1] for _ in range(n))


def untracked_paths():
    proc = subprocess.run(["git", "status", "--porcelain", "--untracked-files=normal"],
                          cwd=ROOT, capture_output=True, text=True)
    return {line[3:] for line in proc.stdout.splitlines() if line.startswith("?? ")}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cetta = os.path.abspath(sys.argv[1])
    reference = parse_env(sys.argv[2:], "--reference", {"CETTA_TCALC_EVAL": "0"})
    candidate = parse_env(sys.argv[2:], "--candidate", {"CETTA_TCALC_EVAL": "auto"})
    files = sorted(os.path.join(dp, f) for top in TOPS
                   for dp, _, fns in os.walk(os.path.join(ROOT, top))
                   for f in fns if f.endswith(".metta"))
    jobs = [(p, lang) for p in files for lang in ("petta", "he")]
    before = untracked_paths()

    def job(item):
        path, lang = item
        run(cetta, path, lang, reference)
        ref = run(cetta, path, lang, reference)
        cand = run(cetta, path, lang, candidate)
        return path, lang, ref, cand

    differs, one_timeout, both_timeout, slow = [], [], 0, []
    with cf.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for path, lang, ref, cand in pool.map(job, jobs):
            rel = os.path.relpath(path, ROOT)
            if ref[0] == "timeout" and cand[0] == "timeout":
                both_timeout += 1
                continue
            if (ref[0] == "timeout") != (cand[0] == "timeout"):
                one_timeout.append((rel, lang, ref[1], cand[1], ref[0], cand[0]))
                continue
            if ref[0] != cand[0] or normalize(ref[2]) != normalize(cand[2]):
                differs.append((rel, lang))
            elif ref[1] >= SLOW_FLOOR_S and cand[1] > SLOW_RATIO * ref[1]:
                slow.append((path, lang))

    confirmed = []
    for path, lang in slow:
        r = best_of(cetta, path, lang, reference)
        c = best_of(cetta, path, lang, candidate)
        if c > SLOW_RATIO * r:
            confirmed.append((os.path.relpath(path, ROOT), lang, r, c))

    created = sorted(untracked_paths() - before)
    print(f"{len(jobs)} runs; {both_timeout} timed out in both modes")
    for rel, lang in differs:
        print(f"ANSWER DIFFERS: {lang} {rel}")
    for rel, lang, rt, ct, rr, cr in one_timeout:
        print(f"TIMEOUT IN ONE MODE: {lang} {rel} (reference {rr} {rt:.2f}s, candidate {cr} {ct:.2f}s)")
    for rel, lang, r, c in confirmed:
        print(f"SLOWER {c / r:.2f}x: {lang} {rel} ({r:.3f}s -> {c:.3f}s)")
    for path in created:
        print(f"CREATED IN THE TREE BY A PROGRAM: {path}")
    print("PASS: no answer differences" if not differs else f"FAIL: {len(differs)} answer differences")
    return 1 if differs else 0


if __name__ == "__main__":
    raise SystemExit(main())
