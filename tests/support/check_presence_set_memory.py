#!/usr/bin/env python3
"""Small sets cost what they hold, however large the universe: 800 sets of
three atoms each, kept by rows of &self, in a universe of 400,000 ids, peak
at most 10 MB above the same program with plain spaces (a presence bitset
over the universe's ids costs 64 KB a set, 50 MB for the 800).

Usage: check_presence_set_memory.py CETTA
"""
import os
import subprocess
import sys
import tempfile


def program(kind):
    return (
        "(= (pad $n) (if (== $n 0) done (let $u (add-atom &self (n $n)) "
        "(pad (- $n 1)))))\n"
        "!(pad 400000)\n"
        f"(= (sets $k) (if (== $k 0) done (let $s (new-space{kind}) "
        "(let $a (add-atom $s (m $k a)) (let $b (add-atom $s (m $k b)) "
        "(let $c (add-atom $s (m $k c)) (let $d (add-atom $s (m $k a)) "
        "(let $e (add-atom &self (kept $s)) (sets (- $k 1))))))))))\n"
        "!(sets 800)\n"
        "!(size-atom (collapse (match &self (kept $s) $s)))\n")


def peak_kib(cetta, kind):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(program(kind))
        path = f.name
    try:
        proc = subprocess.Popen([cetta, "--profile", "extended", "--lang",
                                 "petta", path],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out = proc.stdout.read().decode()
        err = proc.stderr.read().decode()
        _, status, usage = os.wait4(proc.pid, 0)
    finally:
        os.unlink(path)
    answers = out.split()
    if os.waitstatus_to_exitcode(status) != 0 or answers != ["done", "done", "800"]:
        print(f"FAIL: exit {os.waitstatus_to_exitcode(status)}, answers "
              f"{answers[:6]} for{kind or ' plain'} spaces; {err[-200:]}")
        sys.exit(1)
    return usage.ru_maxrss


def main():
    cetta = sys.argv[1]
    plain = min(peak_kib(cetta, "") for _ in range(2))
    sets = min(peak_kib(cetta, " set") for _ in range(2))
    if sets > plain + 10 * 1024:
        print(f"FAIL: 800 small sets peak at {sets} KiB against {plain} KiB "
              f"as plain spaces")
        sys.exit(1)
    print(f"PASS: 800 small sets peak at {sets} KiB against {plain} KiB as "
          f"plain spaces")


if __name__ == "__main__":
    main()
