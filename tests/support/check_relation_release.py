#!/usr/bin/env python3
"""A relation holds exactly the ids its facts hold: once its facts are gone,
the universe reclaims the ids only they held.  A program fills relations of
each form (two columns as bits, three as a table, deep facts' holes as a
table) with 20,000 facts, each over one fresh integer as the base's facts
are, so every relation stays a relation, and removes them; collecting at
every safe point, its last collection finds at most 64 live entries more
than a program that computes the same integers and stores nothing (a
relation holding every id it had seen kept 20,039 more).  With relations
off, rows hold the same facts, and the two programs must agree as well, or
the comparison measures nothing.

Usage: check_relation_release.py CETTA
"""
import os
import re
import subprocess
import sys
import tempfile

BASE = """(= (base $k $end) (if (>= $k $end) done (let* (($a (add-atom &self (pair $k 0))) ($b (add-atom &self (triple $k 0 0))) ($c (add-atom &self (deep $k (wrap 0))))) (base (+ $k 1) $end))))
!(base 0 6000)
"""
STORED = BASE + """(= (fill $k $end) (if (>= $k $end) done (let* (($v (+ 1000000 $k)) ($a (add-atom &self (pair $v 7))) ($b (add-atom &self (triple $v 7 7))) ($c (add-atom &self (deep $v (wrap 7))))) (fill (+ $k 1) $end))))
(= (drop $k $end) (if (>= $k $end) done (let* (($v (+ 1000000 $k)) ($a (remove-atom &self (pair $v 7))) ($b (remove-atom &self (triple $v 7 7))) ($c (remove-atom &self (deep $v (wrap 7))))) (drop (+ $k 1) $end))))
!(fill 0 20000)
!(drop 0 20000)
!(size-atom (collapse (match &self (pair $x $y) $x)))
"""
WALKED = BASE + """(= (fill $k $end) (if (>= $k $end) done (let* (($v (+ 1000000 $k))) (fill (+ $k 1) $end))))
!(fill 0 20000)
!(fill 0 20000)
!(size-atom (collapse (match &self (pair $x $y) $x)))
"""
EXPECTED = ["done", "done", "done", "6000"]
SLACK = 64


def live(cetta, text, relations):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(text)
        path = f.name
    env = dict(os.environ, CETTA_TERM_UNIVERSE_COLLECTION="reclaim-every",
               CETTA_TERM_UNIVERSE_COLLECTION_TRACE="1",
               CETTA_SPACE_DENSE=relations)
    try:
        done = subprocess.run([cetta, "--profile", "extended", "--lang",
                               "petta", path], env=env, capture_output=True,
                              text=True, timeout=900)
    finally:
        os.unlink(path)
    reports = re.findall(r"entries \d+ roots \d+ live (\d+)", done.stderr)
    if done.returncode != 0 or done.stdout.split() != EXPECTED or not reports:
        print(f"FAIL: exit {done.returncode}, answers {done.stdout.split()} "
              f"against {EXPECTED}, {len(reports)} collections")
        sys.exit(1)
    return int(reports[-1])


def main():
    cetta = sys.argv[1]
    failed = False
    for relations in ("off", "on"):
        stored = live(cetta, STORED, relations)
        walked = live(cetta, WALKED, relations)
        verdict = "PASS" if stored <= walked + SLACK else "FAIL"
        failed |= verdict == "FAIL"
        print(f"{verdict}: relations {relations}: {stored} entries live after "
              f"the facts went, against {walked} storing nothing")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
