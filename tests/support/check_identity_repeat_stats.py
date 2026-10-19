#!/usr/bin/env python3
"""Repeating a completed query holds no more frame identities than running
it once: on every route the identities live at exit and the peak are those
of one run, and only the mints grow, in proportion."""

import os
import subprocess
import sys

ROUTES = ("", "CETTA_OPEN_EQUATIONS_REFERENCE=1", "CETTA_PREPARED_PURE_REFERENCE=1",
          "CETTA_OPEN_EQUATIONS_REFERENCE=1 CETTA_PREPARED_PURE_REFERENCE=1")


def counters(binary, program, route):
    env = dict(os.environ)
    env.update(item.split("=", 1) for item in route.split())
    proc = subprocess.run([binary, "--emit-runtime-stats", "--lang", "petta", program],
                          capture_output=True, text=True, env=env, timeout=600)
    expected = open(program.replace(".metta", ".expected")).read()
    if proc.returncode != 0 or proc.stdout != expected:
        sys.exit(f"FAIL: {program} [{route}]: answer {proc.stdout!r}")
    found = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            found[parts[1]] = int(parts[2])
    return found


def main():
    binary = sys.argv[1]
    for route in ROUTES:
        once = counters(binary, "tests/petta/identity_repeat_1.metta", route)
        five = counters(binary, "tests/petta/identity_repeat_5.metta", route)
        for name in ("frame-identity-live", "frame-identity-live-peak"):
            if once.get(name) != five.get(name):
                sys.exit(f"FAIL: [{route}] {name}: {once.get(name)} once, "
                         f"{five.get(name)} after five runs")
        if five.get("frame-identity-mint", 0) < once.get("frame-identity-mint", 0):
            sys.exit(f"FAIL: [{route}] fewer mints for five runs than for one")
    print("PASS: repeated queries leave the identities of one run live, on every route")


if __name__ == "__main__":
    main()
