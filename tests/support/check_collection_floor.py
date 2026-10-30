#!/usr/bin/env python3
"""The term universe's collection pacing, end to end through the machine.

Usage: check_collection_floor.py CETTA_BINARY

A reclaiming universe's first collection waits for the floor's entries
made, whatever its rows released; record bytes make a collection due as
entries do; and an eager universe collects from the machine's own safe
points, on programs far below the floor.  Each program runs under
CETTA_TERM_UNIVERSE_COLLECTION_TRACE=1, which reports every collection, and
its answers must not depend on the pacing:

  a program that adds 6,000 rows and removes them, 10,000-odd entries,
  collects nothing by default, collects under reclaim-every and with the
  entry floor at 0, and answers alike under all three;
  a program whose statements hold 8 MB of strings in a few hundred entries
  collects by default, and not with a byte floor above its bytes;
  a program that makes over 100,000 entries collects by default.
"""
import os
import subprocess
import sys
import tempfile

DRAIN = """!(bind! &d (new-space))
(= (fill $n) (if (> $n 0) (let $u (add-atom &d (row $n)) (fill (- $n 1))) done))
!(fill 6000)
!(size-atom (collapse (match &d (row $x) (remove-atom &d (row $x)))))
!(size-atom (collapse (get-atoms &d)))
"""

GROW = """!(bind! &g (new-space))
(= (grow $n) (if (> $n 0) (let $u (add-atom &g (cell $n (pair $n $n))) (grow (- $n 1))) done))
!(grow 40000)
!(size-atom (collapse (get-atoms &g)))
"""


def strings():
    text = "x" * 200000
    return "".join(f'!(let $s "{text}{i}" ok)\n' for i in range(40))


def run(binary, path, **settings):
    env = dict(os.environ)
    for name in ("CETTA_TERM_UNIVERSE_COLLECTION",
                 "CETTA_TERM_UNIVERSE_COLLECTION_FLOOR",
                 "CETTA_TERM_UNIVERSE_COLLECTION_BYTE_FLOOR"):
        env.pop(name, None)
    env.update(settings)
    env["CETTA_TERM_UNIVERSE_COLLECTION_TRACE"] = "1"
    p = subprocess.run([binary, "--profile", "extended", "--lang", "petta", path],
                       env=env, capture_output=True, text=True, timeout=300)
    collections = sum(1 for line in p.stderr.splitlines()
                      if line.startswith("term-universe collection"))
    return p.returncode, p.stdout, collections


def main():
    binary = os.path.abspath(sys.argv[1])
    failures = []
    with tempfile.TemporaryDirectory(prefix="collection-floor-") as tmp:
        paths = {}
        for name, text in (("drain", DRAIN), ("grow", GROW), ("strings", strings())):
            paths[name] = os.path.join(tmp, name + ".metta")
            with open(paths[name], "w") as handle:
                handle.write(text)

        default = run(binary, paths["drain"])
        eager = run(binary, paths["drain"],
                    CETTA_TERM_UNIVERSE_COLLECTION="reclaim-every")
        floor0 = run(binary, paths["drain"],
                     CETTA_TERM_UNIVERSE_COLLECTION_FLOOR="0")
        for label, (code, _, _) in (("default", default), ("reclaim-every", eager),
                                    ("floor 0", floor0)):
            if code != 0:
                failures.append(f"the drain exits {code} ({label})")
        if default[2] != 0:
            failures.append(f"the drain collects {default[2]} times below the floor")
        if eager[2] == 0:
            failures.append("reclaim-every never collects the drain")
        if floor0[2] == 0:
            failures.append("a floor of 0 never collects the drain")
        if not (default[1] == eager[1] == floor0[1]):
            failures.append("the drain answers differently under different pacing")

        code, out, count = run(binary, paths["strings"])
        if code != 0 or count == 0:
            failures.append(f"8 MB of strings in few entries collect {count} times (exit {code})")
        code_high, out_high, count_high = run(
            binary, paths["strings"],
            CETTA_TERM_UNIVERSE_COLLECTION_BYTE_FLOOR=str(1 << 30))
        if code_high != 0 or count_high != 0:
            failures.append(f"a byte floor above the strings' bytes still collects "
                            f"{count_high} times (exit {code_high})")
        if out != out_high:
            failures.append("the strings answer differently under different pacing")

        code, _, count = run(binary, paths["grow"])
        if code != 0 or count == 0:
            failures.append(f"100,000-odd entries collect {count} times (exit {code})")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print(f"PASS: below the floor nothing collects ({default[2]}); reclaim-every "
          f"collects {eager[2]} times and a floor of 0 {floor0[2]}, answering alike; "
          "record bytes and entries above their floors collect")
    return 0


if __name__ == "__main__":
    sys.exit(main())
