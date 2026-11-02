#!/usr/bin/env python3
"""A relation emptied down to 64 facts gives its storage back, in each form:
two columns kept as bits, three kept as a table, and deep facts whose holes
a table keeps.  A relation is filled with 200,000 facts and emptied down to
64, with relations from a head's 4,096th fact and from its first; its last
renumbering (CETTA_SPACE_DENSE_TRACE) must report at most 64 KiB, where a
relation kept at its high-water mark held its 200,000 facts' numbering and
rows (8 to 12 MiB of peak).  The relation's own bytes are read because the
process's peak also carries what the drain grows whatever form the facts
take (the evaluation's arenas, the universe's count table): rows emptied
the same way peak higher still.

Usage: check_relation_shrink.py CETTA
"""
import os
import re
import subprocess
import sys
import tempfile

FORMS = {
    'bits': ('(r $i $j)', '(r 5 $y)', '(r $x $y)'),
    'table': ('(r $i $j $i)', '(r 5 $y $z)', '(r $x $y $z)'),
    'deep': ('(r $i (wrap $j))', '(r 5 $y)', '(r $x $y)'),
}
BOUND = 64 * 1024


def program(form):
    fact, probe, every = FORMS[form]
    return (f"(= (fill $i $n) (if (>= $i $n) done (let* (($j (+ $i 1)) ($u (add-atom &self {fact}))) (fill (+ $i 1) $n))))\n"
            f"(= (drop $i $n) (if (>= $i $n) done (let* (($j (+ $i 1)) ($u (remove-atom &self {fact}))) (drop (+ $i 1) $n))))\n"
            f"!(fill 0 200000)\n!(size-atom (collapse (match &self {probe} $y)))\n"
            f"!(drop 64 200000)\n!(size-atom (collapse (match &self {every} $x)))\n")


def main():
    cetta = sys.argv[1]
    failed = False
    for form in FORMS:
        for mode in ("on", "eager"):
            with tempfile.NamedTemporaryFile("w", suffix=".metta",
                                             delete=False) as f:
                f.write(program(form))
                path = f.name
            env = dict(os.environ, CETTA_SPACE_DENSE=mode,
                       CETTA_SPACE_DENSE_TRACE="1")
            try:
                done = subprocess.run([cetta, "--profile", "extended",
                                       "--lang", "petta", path], env=env,
                                      capture_output=True, text=True,
                                      timeout=900)
            finally:
                os.unlink(path)
            answers = done.stdout.split()
            trims = re.findall(r"space relations: r renumbers its ids: (\d+) "
                               r"facts in (\d+) bytes", done.stderr)
            if done.returncode != 0 or answers != ["done", "1", "done", "64"]:
                print(f"FAIL: {form}, relations {mode}: exit "
                      f"{done.returncode}, answers {answers}")
                failed = True
                continue
            if not trims or int(trims[-1][1]) > BOUND:
                print(f"FAIL: an emptied relation kept as {form} (relations "
                      f"{mode}) keeps "
                      f"{trims[-1][1] + ' bytes' if trims else 'its numbering'}")
                failed = True
                continue
            print(f"PASS: an emptied relation kept as {form} (relations "
                  f"{mode}) keeps {trims[-1][0]} facts in {trims[-1][1]} bytes")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
