#!/usr/bin/env python3
"""A query, or a collection's body, whose root is a control form runs on the
compiled open tier as the body of a nullary equation, as PeTTa translates a
query.  Every PeTTa test program answers the same, in the plain and the extended
profile, with those bodies compiled (CETTA_OPEN_QUERY_BODIES=1) as with every
body on the machine (=0).
Programs that end abnormally or answer differently from run to run on the
machine alone are skipped and listed.

Usage: check_open_query_bodies.py CETTA [PROGRAM ...]
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

TIMEOUT = 60
# Some fixtures are infinite searches meant for a bounded controller: the
# machine alone, under a transition bound, finds them, and they are skipped.
TRANSITIONS = "2000000"


# Each program runs in PeTTa's plain and extended profiles.
PROFILES = ([], ["--profile", "extended"])


def run(cetta, program, bodies, timeout, bounded, profile=()):
    env = dict(os.environ, CETTA_OPEN_QUERY_BODIES=bodies)
    if bounded:
        env.setdefault("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", TRANSITIONS)
    else:
        env.pop("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", None)
    try:
        result = subprocess.run([cetta, *profile, "--lang", "petta", program],
                                capture_output=True, text=True, env=env,
                                timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    return result.returncode, result.stdout, result.stderr


def compare(cetta, program):
    for profile in PROFILES:
        name, problem = compare_in(cetta, program, profile)
        if problem is not None:
            label = " ".join(profile) or "plain"
            return name, problem if problem.startswith("skipped") \
                else f"[{label}] {problem}"
    return program, None


def compare_in(cetta, program, profile):
    """None when the program answers alike; a reason to skip; or the
    failure.  The machine alone, under a transition bound, decides which
    programs are finite: a metered run keeps every body on the machine, so
    the compiled run is unbounded, under the timeout."""
    probe = run(cetta, program, "0", TIMEOUT, True, profile)
    if probe is None or probe[0] < 0:
        return program, "skipped: ends abnormally on the machine"
    if "fuel-exhausted" in probe[2]:
        return program, "skipped: reaches the transition bound on the machine"
    # A bound selects other routes, so both compared runs are unbounded.
    off = run(cetta, program, "0", TIMEOUT, False, profile)
    if off is None or off[0] < 0:
        return program, "skipped: ends abnormally on the machine"
    on = run(cetta, program, "1", TIMEOUT, False, profile)
    if on is not None and on[:2] == off[:2]:
        return program, None
    again = run(cetta, program, "0", TIMEOUT, False, profile)
    if again is None or again[:2] != off[:2]:
        return program, "skipped: answers differently from run to run"
    if on is None:
        return program, "times out with query bodies compiled"
    before, after = off[1].splitlines(), on[1].splitlines()
    at = next((i for i, (a, b) in enumerate(zip(before, after)) if a != b),
              min(len(before), len(after)))
    shown = (before[at] if at < len(before) else "<end>",
             after[at] if at < len(after) else "<end>")
    return program, (f"answers differ: exit {off[0]} -> {on[0]}, "
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
          f"with their query bodies compiled as on the machine"
          + (f" ({len(skipped)} skipped: they end abnormally or vary on the machine)"
             if skipped else ""))
    for program, reason in skipped:
        print(f"  {program}: {reason[len('skipped: '):]}")


if __name__ == "__main__":
    main()
