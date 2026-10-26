#!/usr/bin/env python3
"""Drains of a space grow with the space, not with its square.

Usage: check_space_read_scaling.py CETTA_RUNTIME_STATS_BINARY

A drain takes one job at a time and records it done, as an MM2 rule set or a
work queue does: by transform (native and authored), and by a loop over
`once` of a match.  Each firing reads the first job, removes it and adds a
record.  At two sizes, each drain answers every job, and its work per firing
stays within a bound that does not depend on the size: the candidate rows
its reads offer, the rows contractions of removed rows move, and the rows
copied for readers pinned to the space when it changed.  A read that
prepared every candidate, a contraction per removal, or a reader copying the
space per removal, makes one of them grow with the space.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (300, 1200)
BOUNDS = {
    "match-native-candidates": 8,
    "space-mark-settle-row": 8,
    "space-pinned-occurrences-detached-row": 0,
}

TRANSFORM = """!(import! &self (library lib_transform))
!(bind! &q (new-space))
{adds}
(= (drain $q $k) (let $r (collapse (transform $q (take (job $j)) (done $j) $q))
   (if (== $r ()) $k (drain $q (+ $k 1)))))
!(drain &q 0)
!(size-atom (collapse (match &q (done $j) x)))
"""

LOOP = """!(bind! &q (new-space))
{adds}
(= (drain $q $k) (let $js (collapse (once (match $q (job $j) $j)))
   (if (== $js ()) $k
     (let $j (car-atom $js)
       (let $_ (remove-atom $q (job $j))
         (let $_2 (add-atom $q (done $j)) (drain $q (+ $k 1))))))))
!(drain &q 0)
!(size-atom (collapse (match &q (done $j) x)))
"""

DRAINS = (
    ("transform", TRANSFORM, {}),
    ("authored transform", TRANSFORM, {"CETTA_TRANSFORM_REFERENCE": "1"}),
    ("once-match loop", LOOP, {}),
)


def counters(report):
    found = {}
    for line in report.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            found[fields[1]] = int(fields[2])
    return found


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-space-reads-") as work:
        for name, source, env_extra in DRAINS:
            for size in SIZES:
                adds = "\n".join(f"!(add-atom &q (job j{i}))" for i in range(size))
                path = os.path.join(work, f"drain_{size}.metta")
                with open(path, "w") as f:
                    f.write(source.format(adds=adds))
                env = dict(os.environ)
                env.pop("CETTA_TRANSFORM_REFERENCE", None)
                env.update(CETTA_PETTA_SEARCH_MACHINE="1", **env_extra)
                p = subprocess.run([binary, "--emit-runtime-stats", "--lang", "petta", path],
                                   cwd=ROOT, env=env, capture_output=True, text=True,
                                   timeout=600)
                answers = [l for l in p.stdout.splitlines() if l != "true"]
                if p.returncode != 0 or answers[-2:] != [str(size), str(size)]:
                    print(f"FAIL: {name} drain of {size}: answered {answers[-2:]}")
                    return 1
                found = counters(p.stdout + p.stderr)
                for counter, bound in BOUNDS.items():
                    value = found.get(counter, 0)
                    if value > bound * size:
                        print(f"FAIL: {name} drain of {size}: {counter} {value} "
                              f"exceeds {bound} per firing")
                        return 1
    print(f"PASS: drains by transform, as authored, and by a once-match loop take "
          f"bounded work per firing at {SIZES[0]} and {SIZES[1]} jobs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
