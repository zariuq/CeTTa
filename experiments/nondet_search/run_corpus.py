#!/usr/bin/env python3
"""Run corpus programs on CeTTa and SWI-PeTTa, one run at a time.

  run_corpus.py --cetta BIN --swi DIR [--reps N] [--timeout S] [--stats] NAME...

Prints one tab-separated line per run: engine, program, repetition,
wall seconds, max RSS (KB), exit status and the last output line; with
--stats, CeTTa's machine counters are appended as key=value pairs.
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORPUS = HERE / "corpus"


def run(cmd, cwd=None, env=None, timeout=60):
    command = ["/usr/bin/time", "-f", "__TIME__ %e %M", "timeout", str(timeout), *cmd]
    start = time.monotonic()
    proc = subprocess.run(command, cwd=cwd, env=env,
                          capture_output=True, text=True)
    elapsed = time.monotonic() - start
    wall, rss = None, None
    for line in proc.stderr.splitlines():
        if line.startswith("__TIME__"):
            _, w, m = line.split()
            wall, rss = float(w), int(m)
    return proc, wall if wall is not None else elapsed, rss


def last_line(text):
    lines = [l for l in text.splitlines()
             if l.strip() and "janus" not in l.lower() and "libpython" not in l.lower()]
    return lines[-1].strip() if lines else ""


def stats_of(stderr):
    pairs = {}
    for line in stderr.splitlines():
        for key, value in re.findall(r"([a-z_]+)[=:]\s*([0-9]+)", line):
            pairs.setdefault(key, value)
    return pairs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cetta")
    ap.add_argument("--swi")
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("names", nargs="+")
    args = ap.parse_args()
    for name in args.names:
        prog = CORPUS / f"{name}.metta"
        for rep in range(args.reps):
            if args.cetta:
                env = dict(os.environ)
                if args.stats:
                    env["CETTA_PETTA_MACHINE_STATS"] = "1"
                proc, wall, rss = run([args.cetta, "--lang", "petta", str(prog)],
                                      env=env, timeout=args.timeout)
                extra = ""
                if args.stats:
                    st = stats_of(proc.stderr)
                    extra = "\t" + " ".join(f"{k}={v}" for k, v in st.items())
                print(f"cetta\t{name}\t{rep}\t{wall:.3f}\t{rss}\t{proc.returncode}\t"
                      f"{last_line(proc.stdout)}{extra}", flush=True)
            if args.swi:
                proc, wall, rss = run(["bash", "./run.sh", str(prog), "--silent"],
                                      cwd=args.swi, timeout=args.timeout)
                print(f"swi\t{name}\t{rep}\t{wall:.3f}\t{rss}\t{proc.returncode}\t"
                      f"{last_line(proc.stdout)}", flush=True)


if __name__ == "__main__":
    main()
