#!/usr/bin/env python3
"""Rows of a relation given one far number each grow by a leaf, not by bits
up to the number.

ROWS rows of a relation hold 64 small numbers each, a filler numbers FILLER
more, and then every row takes the filler's last number.  A row kept as bits
up to its largest number grew by FILLER/8 bytes for that one pair (4,000
rows past 300,000 others: 150 MB more than without the far pairs).  With the
far pairs the run's peak may exceed the same run without them by at most
1 KiB a row: the row's new leaf and list, and the fact's own records.  Both
runs must count every pair.

Usage: check_dense_far_rows.py CETTA [ROWS] [FILLER]
"""
import json
import os
import subprocess
import sys
import tempfile

PER_ROW = 1024


def program(rows, filler, far):
    lines = []
    for r in range(rows):
        lines.extend(f"(r x{r} {j})" for j in range(64))
    lines.extend(f"(r filler v{v})" for v in range(filler))
    if far:
        lines.extend(f"(r x{r} v{filler - 1})" for r in range(rows))
    lines.append("!(size-atom (collapse (match &self (r $a $b) t)))")
    return "\n".join(lines) + "\n"


def peak(cetta, text):
    """Exit code, answers and peak resident kilobytes of one run."""
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        # Fixture construction can leave more resident Python memory than
        # the engine needs. A small worker starts the measured child after
        # exec, so its inherited pre-exec RSS excludes that construction.
        worker = subprocess.run(
            [sys.executable, __file__, "--one", cetta, path],
            capture_output=True, text=True, check=True, timeout=600)
        measured = json.loads(worker.stdout)
    finally:
        os.unlink(path)
    return measured


def one(cetta, path):
    child = subprocess.Popen([cetta, "--profile", "extended", "--lang",
                              "petta", path], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True)
    out = child.stdout.read()
    child.stdout.close()
    _, status, usage = os.wait4(child.pid, 0)
    child.returncode = os.waitstatus_to_exitcode(status)
    print(json.dumps([child.returncode, out.split(), usage.ru_maxrss]))


def main():
    cetta = sys.argv[1]
    rows = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
    filler = int(sys.argv[3]) if len(sys.argv) > 3 else 300000
    base_pairs = rows * 64 + filler
    near_code, near_answers, near_kb = peak(cetta, program(rows, filler, False))
    far_code, far_answers, far_kb = peak(cetta, program(rows, filler, True))
    if near_code or far_code or near_answers != [str(base_pairs)] or \
            far_answers != [str(base_pairs + rows)]:
        print(f"FAIL: far rows: exits {near_code} and {far_code}, answers "
              f"{near_answers} and {far_answers}, expected {base_pairs} and "
              f"{base_pairs + rows}")
        sys.exit(1)
    grown = (far_kb - near_kb) * 1024
    bound = rows * PER_ROW
    verdict = "PASS" if grown <= bound else "FAIL"
    print(f"{verdict}: {rows} rows given one number past {filler} others "
          f"peak {far_kb} KiB against {near_kb} KiB without it: "
          f"{grown // rows if grown > 0 else 0} bytes a row, at most {PER_ROW}")
    sys.exit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--one":
        one(sys.argv[2], sys.argv[3])
    else:
        main()
