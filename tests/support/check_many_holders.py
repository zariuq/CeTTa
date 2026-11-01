#!/usr/bin/env python3
"""A space per item registers its holders in its universe in constant
expected time: CPU for 64,000 spaces stays within sixteen times that for
8,000 (linear work gives about eight, a registry scanned on each
registration about sixty-four).

Usage: check_many_holders.py CETTA
"""
import os
import resource
import subprocess
import sys
import tempfile

PROGRAM = """(= (mk $n) (if (== $n 0) done
  (let $s (new-space) (let $_ (add-atom $s (item $n)) (mk (- $n 1))))))
!(mk {n})
"""


def cpu_seconds(cetta, n):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(PROGRAM.format(n=n))
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=300)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    if result.returncode != 0 or "done" not in result.stdout:
        print(f"FAIL: {n} spaces: exit {result.returncode}: {result.stdout[-200:]}"
              f"{result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def main():
    cetta = sys.argv[1]
    small = min(cpu_seconds(cetta, 8000) for _ in range(2))
    large = min(cpu_seconds(cetta, 64000) for _ in range(2))
    ratio = large / max(small, 0.01)
    if ratio > 16.0:
        print(f"FAIL: 64,000 spaces took {large:.2f} s against {small:.2f} s "
              f"for 8,000 ({ratio:.1f}x)")
        sys.exit(1)
    print(f"PASS: a space per item registers in constant time: 64,000 spaces "
          f"{large:.2f} s, 8,000 {small:.2f} s ({ratio:.1f}x)")


if __name__ == "__main__":
    main()
