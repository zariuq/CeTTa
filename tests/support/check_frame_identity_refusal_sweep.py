#!/usr/bin/env python3
"""A refused frame identity never becomes an answer.  The refusal point is
swept over PeTTa programs (CETTA_DIAG_FRAME_IDENTITY_REFUSE_FROM refuses every
mint from the N-th on): each run ends as the unrefused run does, or refused
as capacity with status 2, its standard output a prefix of the unrefused
run's and its error output the single capacity line.  Points: every mint up
to 60, then 40 spread geometrically to past the last.

usage: check_frame_identity_refusal_sweep.py BIN PROGRAM..."""

import math
import os
import subprocess
import sys

REPORTED = "error: observation incomplete: capacity-exhausted"
EXITED = "error: resource exhausted: contextual frame identities"
ROUTES = ("", "CETTA_OPEN_EQUATIONS_REFERENCE=1",
          "CETTA_OPEN_EQUATIONS_REFERENCE=1 CETTA_PREPARED_PURE_REFERENCE=1")


def run(binary, program, route, refuse_from=0, stats=False):
    env = dict(os.environ)
    env.update(item.split("=", 1) for item in route.split())
    if refuse_from:
        env["CETTA_DIAG_FRAME_IDENTITY_REFUSE_FROM"] = str(refuse_from)
    command = [binary] + (["--emit-runtime-stats"] if stats else []) + \
        ["--lang", "petta", program]
    proc = subprocess.run(command, capture_output=True, text=True, env=env,
                          timeout=120)
    return proc.returncode, proc.stdout, proc.stderr


def mints(binary, program, route):
    _, _, err = run(binary, program, route, stats=True)
    for line in err.splitlines():
        parts = line.split()
        if parts[:2] == ["runtime-counter", "frame-identity-mint"]:
            return int(parts[2])
    sys.exit(f"FAIL: {program}: no mint count (a runtime-stats build is needed)")


def main():
    binary, programs = sys.argv[1], sys.argv[2:]
    totals = {"complete": 0, "reported": 0, "exited": 0}
    for route in ROUTES:
        for program in programs:
            rc0, out0, err0 = run(binary, program, route)
            if rc0 != 0:
                sys.exit(f"FAIL: {program} [{route}]: unrefused status {rc0}")
            count = mints(binary, program, route)
            points = sorted(set(range(1, min(count, 60) + 2)) | {
                max(1, round(math.exp(math.log(count + 1) * k / 40)))
                for k in range(41)} | {count + 1})
            lines0 = out0.splitlines()
            for point in points:
                rc, out, err = run(binary, program, route, point)
                lines = out.splitlines()
                if rc == rc0 and out == out0 and err == err0:
                    totals["complete"] += 1
                elif rc == 2 and lines == lines0[:len(lines)] and \
                        err.strip() in (REPORTED, EXITED):
                    totals["reported" if err.strip() == REPORTED else "exited"] += 1
                else:
                    sys.exit(f"FAIL: {program} [{route}] refused from mint {point}: "
                             f"status {rc}, output {out[-200:]!r}, errors {err[-300:]!r}")
    print(f"PASS: a refused frame identity never became an answer: "
          f"{totals['reported']} runs reported capacity, {totals['exited']} exited "
          f"for it, {totals['complete']} completed")


if __name__ == "__main__":
    main()
