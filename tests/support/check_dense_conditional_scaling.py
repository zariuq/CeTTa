#!/usr/bin/env python3
"""Check native conditional-domain work, including sparse visits and probes.

Usage: check_dense_conditional_scaling.py RELATIONAL_PLAN_TEST_BINARY
"""
import subprocess
import sys


def main():
    run = subprocess.run([sys.argv[1]], capture_output=True, text=True, timeout=180)
    if run.returncode or run.stderr:
        print(run.stdout, end="")
        print(run.stderr, end="", file=sys.stderr)
        print("FAIL: native relational controls did not complete cleanly")
        return 1
    rows = []
    for line in run.stdout.splitlines():
        if line.startswith("dense-hybrid-work "):
            rows.append(dict((key, int(value)) for key, value in
                             (item.split("=", 1) for item in line.split()[1:])))
    if [row["n"] for row in rows] != [64, 128, 256]:
        print("FAIL: missing native conditional-domain ladder")
        return 1
    previous = None
    for row in rows:
        n = row["n"]
        total = row["words"] + row["lists"] + row["probes"]
        if (row["answers"] != n or not row["lists"] or
                row["probes"] < row["lists"] or total > 6 * n * n or
                row["retained"] > max(8 << 20, 64 * (n * n + 2 * n)) or
                (previous is not None and total > 5 * previous)):
            print("FAIL: conditional-domain answers, complete work or storage bound", row)
            return 1
        previous = total
        print("native conditional-domain work", row)
    print("PASS: conditional sparse/dense joins preserve native observations and measured quadratic work")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
