#!/usr/bin/env python3
"""A value that crosses the open tier's boundary is copied only in its new
parts.  A tier loop threads a queue through a host goal at every step; at
twice the length the crossings copy about twice as much, not four times."""

import subprocess
import sys

LENGTHS = (1000, 2000)
COUNTERS = ("open-equation-host-export-bytes", "open-equation-host-import-bytes")


def run(binary, length):
    fixture = f"tests/petta/open_crossing_queue_{length}.metta"
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "petta", fixture],
        capture_output=True, text=True, timeout=600)
    if proc.returncode != 0 or proc.stdout.strip() != str(2 * length):
        sys.exit(f"FAIL: {fixture}: answer {proc.stdout.strip()!r}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    return counters


def main():
    binary = sys.argv[1]
    runs = {length: run(binary, length) for length in LENGTHS}
    small, large = LENGTHS
    for name in COUNTERS:
        before = runs[small].get(name, 0)
        after = runs[large].get(name, 0)
        if before <= 0:
            sys.exit(f"FAIL: {name} is zero: the crossing was not exercised")
        if after > 3 * before:
            sys.exit(f"FAIL: {name} grows superlinearly: {before} at "
                     f"{small}, {after} at {large}")
    print("PASS: crossing the open tier copies only what is new")


if __name__ == "__main__":
    main()
