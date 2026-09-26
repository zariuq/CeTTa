#!/usr/bin/env python3
"""Building a list by repeated cons-atom, and filtering it, grows memory with
the list's length.  At twice the length the growth above an empty program's
footprint is about twice as large, not four times (a copied tail) or
exponential (a filter term that doubles at every kept item).  The sizes keep
even a regression small."""

import os
import signal
import sys
import tempfile
import time

BUILD = ("(= (build $n $acc) (if (== $n 0) $acc "
         "(build (- $n 1) (cons-atom $n $acc))))\n")
CASES = (
    ("cons-atom", "!(size-atom (build {n} ()))\n", (5000, 10000),
     lambda n: str(n)),
    # 100 items fail the test; the kept items are the ones above 100.
    ("filter-atom",
     "!(let $l (build {n} ()) "
     "(size-atom (filter-atom $l (|-> ($x) (> $x 100)))))\n",
     (108, 116), lambda n: str(n - 100)),
)
SLACK_KB = 8 * 1024
TIMEOUT_SECONDS = 60


def measure(binary, source):
    with tempfile.NamedTemporaryFile(
            "w", suffix=".metta", dir="runtime", delete=False) as handle:
        handle.write(source)
        path = handle.name
    try:
        pid = os.fork()
        if pid == 0:
            devnull = os.open(os.devnull, os.O_WRONLY)
            out = os.open(path + ".out", os.O_WRONLY | os.O_CREAT, 0o600)
            os.dup2(out, 1)
            os.dup2(devnull, 2)
            os.execv(binary, [binary, "--lang", "petta", path])
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while True:
            waited, status, usage = os.wait4(pid, os.WNOHANG)
            if waited == pid:
                break
            if time.monotonic() > deadline:
                os.kill(pid, signal.SIGKILL)
                os.wait4(pid, 0)
                os.unlink(path + ".out")
                sys.exit(f"FAIL: {source.strip()!r} ran past "
                         f"{TIMEOUT_SECONDS} s")
            time.sleep(0.02)
        with open(path + ".out") as result:
            answer = result.read().strip()
        os.unlink(path + ".out")
        if status != 0:
            sys.exit(f"FAIL: {source.strip()!r} exited with status {status}")
        return answer, usage.ru_maxrss
    finally:
        os.unlink(path)


def main():
    binary = os.path.abspath(sys.argv[1])
    _, base = measure(binary, "!(+ 1 2)\n")
    for name, query, (small, large), expected in CASES:
        growth = {}
        for n in (small, large):
            answer, rss = measure(binary, BUILD + query.format(n=n))
            if answer != expected(n):
                sys.exit(f"FAIL: {name} at {n}: answer {answer!r}, "
                         f"expected {expected(n)!r}")
            growth[n] = max(rss - base, 0)
        if growth[large] > 3 * growth[small] + SLACK_KB:
            sys.exit(f"FAIL: {name} grows superlinearly: "
                     f"{growth[small]} KB at {small}, "
                     f"{growth[large]} KB at {large}")
    print("PASS: building and filtering a list grow with its length")


if __name__ == "__main__":
    main()
