#!/usr/bin/env python3
"""One captured variable does not keep a large frame alive.  Each call here
binds a chain of let variables and answers a term that keeps one of its
fresh variables open; fifty answers are collected.  The binding frames the
answers retain hold what the answers can reach, so their bytes do not grow
with the number of slots the activation had (4, 16 or 64).  As the contrast
shows, they do grow with the number of variables an answer keeps open
(1, 8 or 32).  Both evaluation routes are checked: the shared machine and
the canonical frames."""

import os
import subprocess
import sys
import tempfile

ANSWERS = 50
COUNTER = "bindings-frame-storage-bytes-peak"
ROUTES = (("default", {}), ("canonical", {"CETTA_HE_SHARED_EXECUTION": "0"}))


def body(slots, fresh):
    held = " ".join(f"$f{index}" for index in range(1, fresh + 1))
    term = f"(Held $k $v{slots} {held})"
    for index in range(slots, 0, -1):
        previous = "$k" if index == 1 else f"$v{index - 1}"
        term = f"(let $v{index} (+ {previous} 1) {term})"
    return term


def program(slots, fresh):
    return (f"(= (pick $k) {body(slots, fresh)})\n"
            "(= (picks $k) (if (== $k 0) () (let $h (pick $k)"
            " (let $t (picks (- $k 1)) (cons-atom $h $t)))))\n"
            f"!(let $l (picks {ANSWERS}) (size-atom $l))\n")


def frame_bytes(binary, directory, route, env, slots, fresh):
    path = os.path.join(directory, f"captured_{slots}_{fresh}.metta")
    with open(path, "w") as handle:
        handle.write(program(slots, fresh))
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "he", "--profile",
         "extended", path],
        capture_output=True, text=True, timeout=600,
        env={**os.environ, **env})
    if proc.returncode != 0 or proc.stdout.strip() != f"[{ANSWERS}]":
        sys.exit(f"FAIL: {route}, {slots} slots, {fresh} open: answer "
                 f"{proc.stdout.strip()!r}")
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter" and \
                parts[1] == COUNTER:
            return int(parts[2])
    sys.exit(f"FAIL: {COUNTER} missing from the runtime statistics")


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(dir="runtime") as directory:
        for route, env in ROUTES:
            by_slots = [frame_bytes(binary, directory, route, env, slots, 1)
                        for slots in (4, 16, 64)]
            if min(by_slots) <= 0 or max(by_slots) != min(by_slots):
                sys.exit(f"FAIL: {route}: the frames of answers keeping one "
                         f"variable grow with the activation's slots: "
                         f"{by_slots}")
            by_fresh = [frame_bytes(binary, directory, route, env, 64, fresh)
                        for fresh in (1, 8, 32)]
            if not by_fresh[0] < by_fresh[1] < by_fresh[2] or \
                    by_fresh[2] < 16 * by_fresh[0]:
                sys.exit(f"FAIL: {route}: the frames do not grow with the "
                         f"variables the answers keep: {by_fresh}")
            print(f"PASS: {route}: one kept variable holds {by_slots[0]} "
                  f"frame bytes for any activation size; 1/8/32 kept "
                  f"variables hold {by_fresh}")


if __name__ == "__main__":
    main()
