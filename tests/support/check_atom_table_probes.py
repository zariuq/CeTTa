#!/usr/bin/env python3
"""Tables keyed by atoms find each key within a few slots.

Usage: check_atom_table_probes.py CETTA_RUNTIME_STATS_BINARY

unique-atom and the answer tables of memoized calls index their keys by
hash.  Keys that differ only in small leaves, such as (p x y) over small
integers, are common in rule programs, and a hash that let them collapse
onto a few values would send each insertion along a run of slots holding
other keys: the work per key would grow with the table.  At two sizes, each
program answers every key, and the slots its tables pass per key stay
within a bound that does not depend on the size.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (40, 160)
BOUND = 4

UNIQUE = """{facts}
!(size-atom (unique-atom (collapse (match &self (e $x $y) (p $x $y)))))
"""

MEMO = """!(import! &self (library lib_memo))
!(config-memoize (answer-limit 100000000))
!(memoize pairs)
{facts}
(= (pairs) (match &self (e $x $y) (p $x $y)))
!(size-atom (collapse (pairs)))
"""

PROGRAMS = (
    ("unique-atom", UNIQUE),
    ("memoized answers", MEMO),
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
    with tempfile.TemporaryDirectory(prefix="cetta-atom-table-probes-") as work:
        for name, source in PROGRAMS:
            for size in SIZES:
                facts = "\n".join(f"(e {i} {j})" for i in range(size) for j in range(size))
                path = os.path.join(work, f"keys_{size}.metta")
                with open(path, "w") as f:
                    f.write(source.format(facts=facts))
                p = subprocess.run([binary, "--emit-runtime-stats", "--lang", "petta", path],
                                   cwd=ROOT, capture_output=True, text=True, timeout=600)
                keys = size * size
                answers = [l for l in p.stdout.splitlines() if l != "true"]
                if p.returncode != 0 or not answers or answers[-1] != str(keys):
                    print(f"FAIL: {name} over {keys} keys: answered {answers[-1:]}")
                    return 1
                probes = counters(p.stdout + p.stderr).get("atom-table-key-probe", 0)
                if probes > BOUND * keys:
                    print(f"FAIL: {name} over {keys} keys: {probes} slots passed, "
                          f"more than {BOUND} per key")
                    return 1
    print(f"PASS: unique-atom and memoized answer tables pass at most {BOUND} slots "
          f"per key over {SIZES[0] ** 2} and {SIZES[1] ** 2} integer-pair keys")
    return 0


if __name__ == "__main__":
    sys.exit(main())
