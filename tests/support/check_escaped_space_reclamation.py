#!/usr/bin/env python3
"""Removed space-valued facts release their images during a long request.

A loop stores a new space in a fact and removes that fact each iteration.
The 80-fold ladder must finish with the same answer and within 8 MiB of
its smallest peak. A separate control retains selected spaces in live facts
and reads them after collection. Usage: check_escaped_space_reclamation.py CETTA
"""
import json
import os
import subprocess
import sys
import tempfile

BODY = """(= (churn $n) (if (== $n 0) done
  (let $s (new-space)
    (let $_ (add-atom $s (x $n))
      (let $_ (add-atom &self (held $s))
        {keep}
        (let $_ (remove-atom &self (held $s)) (churn (- $n 1)))
        {close})))))
!(churn {n})
"""


def one(cetta, path):
    child = subprocess.Popen([cetta, "--profile", "extended", "--lang",
                              "petta", path], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    out, err = child.communicate()
    # communicate waits, so use RUSAGE_CHILDREN only in this fresh worker,
    # which has measured exactly one engine process.
    import resource
    print(json.dumps([child.returncode, out.split(),
                      resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
                      err[-1000:]]))


def measured(cetta, text, expected):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        worker = subprocess.run([sys.executable, __file__, "--one", cetta, path],
            capture_output=True, text=True, check=True, timeout=180)
        code, answers, peak, err = json.loads(worker.stdout)
    finally:
        os.unlink(path)
    if code or answers != expected:
        raise SystemExit(f"FAIL: exit {code}, answers {answers}, expected {expected}; {err}")
    return peak


def main():
    cetta = os.path.abspath(sys.argv[1])
    sizes = (8000, 64000, 640000)
    peaks = [measured(cetta, BODY.format(n=n, keep="", close=""), ["done"])
             for n in sizes]
    if max(peaks) - min(peaks) > 8192:
        raise SystemExit(f"FAIL: removed space-valued facts retain memory: {list(zip(sizes, peaks))}")
    kept = BODY.format(n=8000,
        keep="(let $_ (if (== (% $n 1000) 0) (add-atom &self (kept $s)) ())",
        close=")") + """!(size-atom (collapse (match &self (kept $s) (match $s (x $v) $v))))
!(collapse (match &self (kept $s) (match $s (x 8000) found)))
"""
    measured(cetta, kept, ["done", "8", "(found)"])
    print(f"PASS: removed space-valued facts stay bounded {list(zip(sizes, peaks))} KiB; retained spaces remain readable")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--one":
        one(sys.argv[2], sys.argv[3])
    else:
        main()
