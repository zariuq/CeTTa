#!/usr/bin/env python3
"""Finding a stored value's frame identifiers asks an open-addressing set
whether each one is known.  Identifiers pack an activation's epoch above one
source variable, so the set must hash the whole identifier: a slot taken from
the low bits alone puts every activation of a variable in one slot, and each
lookup probes past all the earlier ones, about half as many probes per
lookup as the value has frames.  A set kept at most half full averages under
two probes beyond the first slot per lookup; four is the bound."""

import os
import subprocess
import sys

FIXTURE = "tests/petta/open_values_frame_walk.metta"


def main():
    binary = sys.argv[1]
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "petta", FIXTURE],
        capture_output=True, text=True, timeout=600,
        env=dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE="1"))
    expected = open(FIXTURE.replace(".metta", ".expected")).read()
    if proc.returncode != 0 or proc.stdout != expected:
        sys.exit(f"FAIL: {FIXTURE}: answer {proc.stdout!r}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    lookups = counters.get("bindings-frame-id-set-lookup", 0)
    probes = counters.get("bindings-frame-id-set-probe", 0)
    if lookups < 100000:
        sys.exit(f"FAIL: {lookups} frame identifier lookups: the set was not exercised")
    if probes > 4 * lookups:
        sys.exit(f"FAIL: {probes} extra probes for {lookups} frame identifier lookups")
    print(f"PASS: frame identifiers of stored values found with {probes} extra probes "
          f"in {lookups} lookups")


if __name__ == "__main__":
    main()
