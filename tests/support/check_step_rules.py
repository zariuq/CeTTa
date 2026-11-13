#!/usr/bin/env python3
"""One step of the transform calculus written in PeTTa (lib/petta/lib_step_rules.metta).

Usage: check_step_rules.py CETTA_BINARY [--swi PETTA_ROOT]

tests/petta/step_rules.metta states what a firing does as checks, each printing
(holds NAME) or (FAILS NAME got want): takes, reads and makes; told equations
that bind an unknown the store holds; guards that must hold at every solution;
stored schemas opened afresh; minted names; told disequations; runs; rules that
choose their occurrences; halting; and a rule fired as one transform over a
space.  Every check must hold with transform native, with transform written in
PeTTa, and with --swi on SWI-PeTTa as well.
"""
import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_transform_swi_differential as swi_lane  # noqa: E402

ROOT = swi_lane.ROOT
LIBRARY = "!(import! &self (library lib_step_rules))"


def verdict(lane, names, output):
    held = [line.strip()[len("(holds "):-1] for line in output.splitlines()
            if line.strip().startswith("(holds ")]
    missing = [name for name in names if name not in held]
    if missing:
        print(f"FAIL: on {lane}, {len(missing)} of {len(names)} checks do not hold: "
              + " ".join(missing[:8]))
        for line in output.splitlines():
            if line.startswith("(FAILS") or ("rror" in line and "janus" not in line
                                              and "libpython" not in line):
                print("    " + line[:260])
    return not missing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--swi")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    with open(os.path.join(ROOT, "tests", "petta", "step_rules.metta")) as stream:
        source = stream.read()
    names = [line.split()[1] for line in source.splitlines() if line.startswith("!(check ")]
    ok = True
    with tempfile.TemporaryDirectory(prefix="cetta-step-rules-") as work:
        _, out, err = swi_lane.run_cetta(binary, source, work, "native")
        ok = verdict("native", names, out + err) and ok
        _, out, err = swi_lane.run_cetta(
            binary, source.replace(swi_lane.LIBRARY_IMPORT, swi_lane.base.SPECS_IMPORT),
            work, "specs")
        ok = verdict("specs", names, out + err) and ok
        if args.swi:
            library = os.path.join(ROOT, "lib", "petta", "lib_step_rules.metta")
            program = swi_lane.for_swi(source.replace(LIBRARY, f"!(import! &self {library})"),
                                       "lib_transform_specs", work)
            _, out, err = swi_lane.run_swi(os.path.abspath(args.swi), program, work, "swi")
            ok = verdict("SWI-PeTTa", names, out + err) and ok
    if not ok:
        return 1
    lanes = "native, specs" + (", SWI-PeTTa" if args.swi else "")
    print(f"PASS: one step of the calculus written in PeTTa: {len(names)} checks hold ({lanes})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
