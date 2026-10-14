#!/usr/bin/env python3
"""A match over a conjunction whose templates have effects reads its joint
rows whole before any template runs.  The compiled machine completes those
rows itself when every row is ground and none would evaluate further; it
leaves the others to the host.  The program's answers are its golden file's,
and the runtime statistics show which conjunctions the machine completed and
which it declined."""

import subprocess
import sys

PROGRAM = "tests/test_he_joint_rows.metta"
EXPECTED = "tests/test_he_joint_rows.expected"
COMPLETED = "open-equation-joint-rows-completed"
DECLINED = "open-equation-joint-rows-declined"


def main():
    binary = sys.argv[1]
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "he", "--profile",
         "extended", PROGRAM],
        capture_output=True, text=True, timeout=600)
    with open(EXPECTED) as handle:
        expected = handle.read()
    if proc.returncode != 0 or proc.stdout != expected:
        sys.exit(f"FAIL: answers differ from {EXPECTED}:\n{proc.stdout}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    completed = counters.get(COMPLETED)
    declined = counters.get(DECLINED)
    if completed is None or declined is None:
        sys.exit("FAIL: joint-row counters missing from the runtime statistics")
    # Three conjunctions have ground rows that do not evaluate further; one
    # reads a row with a variable of its own, one a row holding a call.
    if completed != 3 or declined != 2:
        sys.exit(f"FAIL: completed {completed} and declined {declined} "
                 f"conjunctions; expected 3 and 2")
    print("PASS: the compiled machine completes three conjunctions' joint "
          "rows and leaves two to the host")


if __name__ == "__main__":
    main()
