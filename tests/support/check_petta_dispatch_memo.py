#!/usr/bin/env python3
"""The PeTTa machine remembers how each builtin head takes equations (per
head and arity, under one key of program, library, session, symbol table and
`&self`).  Every PeTTa test program answers the same with the memo checking
each remembered decision against a fresh one (CETTA_PETTA_DISPATCH_MEMO=check,
which stops the program on a difference) as without the memo (=off).
Programs that end abnormally or answer differently from run to run without
the memo are only checked for differences in the decisions themselves.

Usage: check_petta_dispatch_memo.py CETTA [PROGRAM ...]
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

TIMEOUT = 60
# Some fixtures are infinite searches meant for a bounded controller; a
# transition bound stops them where their own runners would.
TRANSITIONS = "2000000"


def run(cetta, program, memo, timeout):
    env = dict(os.environ, CETTA_PETTA_DISPATCH_MEMO=memo)
    env.setdefault("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", TRANSITIONS)
    try:
        result = subprocess.run([cetta, "--lang", "petta", program],
                                capture_output=True, text=True, env=env,
                                timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    return result.returncode, result.stdout, result.stderr


def compare(cetta, program):
    """None when the program answers alike; a reason to skip (the program
    ends abnormally or answers differently without the memo, run twice);
    or the failure."""
    off = run(cetta, program, "off", TIMEOUT)
    if off is None or off[0] < 0:
        # Still read what checking reports before the program ends.
        early = run(cetta, program, "check", 20)
        if early is not None and "petta dispatch memo:" in early[2]:
            return program, next(l for l in early[2].splitlines()
                                 if "petta dispatch memo:" in l)
        return program, "skipped: ends abnormally without the memo"
    # Checking recomputes every remembered decision, so it may run longer.
    checked = run(cetta, program, "check", 3 * TIMEOUT)
    if checked is not None and "petta dispatch memo:" in checked[2]:
        line = next(l for l in checked[2].splitlines()
                    if "petta dispatch memo:" in l)
        return program, line
    if checked is not None and checked[:2] == off[:2]:
        return program, None
    again = run(cetta, program, "off", TIMEOUT)
    if again is None or again[:2] != off[:2]:
        return program, "skipped: answers differently from run to run"
    if checked is None:
        return program, "times out with the memo checking"
    before, after = off[1].splitlines(), checked[1].splitlines()
    at = next((i for i, (a, b) in enumerate(zip(before, after)) if a != b),
              min(len(before), len(after)))
    shown = (before[at] if at < len(before) else "<end>",
             after[at] if at < len(after) else "<end>")
    return program, (f"answers differ: exit {off[0]} -> {checked[0]}, "
                     f"{len(before)} -> {len(after)} lines; "
                     f"line {at + 1}: {shown[0]!r} -> {shown[1]!r}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    cetta = os.path.abspath(sys.argv[1])
    programs = sys.argv[2:] or sorted(glob.glob("tests/petta/*.metta"))
    failures, skipped = [], []
    workers = 2
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for program, problem in pool.map(lambda p: compare(cetta, p), programs):
            if problem is None:
                continue
            if problem.startswith("skipped"):
                skipped.append((program, problem))
            else:
                failures.append((program, problem))
    for program, problem in failures:
        print(f"FAIL: {program}: {problem}")
    if failures:
        sys.exit(1)
    print(f"PASS: {len(programs) - len(skipped)} PeTTa programs answer the same "
          f"with every remembered dispatch decision checked as without the memo"
          + (f" ({len(skipped)} skipped: they end abnormally or vary without it)"
             if skipped else ""))
    for program, reason in skipped:
        print(f"  {program}: {reason[len('skipped: '):]}")


if __name__ == "__main__":
    main()
