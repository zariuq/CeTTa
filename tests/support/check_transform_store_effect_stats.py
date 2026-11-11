#!/usr/bin/env python3
"""Open guided takes reuse their completed selection without public receipts."""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROGRAM = os.path.join(ROOT, "tests", "petta", "transform_store_effects.metta")
EXPECTED = os.path.join(ROOT, "tests", "petta", "transform_store_effects.expected")
ROUTES = ("", "CETTA_PETTA_SEARCH_MACHINE=1",
          "CETTA_PREPARED_PURE_REFERENCE=1",
          "CETTA_OPEN_EQUATIONS_REFERENCE=1",
          "CETTA_OPEN_EQUATIONS_REFERENCE=1 CETTA_PREPARED_PURE_REFERENCE=1")


def observe(binary, route):
    env = dict(os.environ)
    env.update(item.split("=", 1) for item in route.split())
    proc = subprocess.run(
        [binary, "--emit-runtime-stats", "--lang", "petta", PROGRAM],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
    with open(EXPECTED) as stream:
        expected = stream.read()
    if proc.returncode != 0 or proc.stdout != expected:
        raise AssertionError(
            f"answers differ [{route or 'default'}]: {proc.stdout!r}; {proc.stderr!r}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    prepared = counters.get("transform-take-prepared", 0)
    reused = counters.get("transform-take-reused", 0)
    declined = counters.get("transform-take-preparation-declined", 0)
    if reused <= 0:
        raise AssertionError(f"no open guided selection was reused [{route or 'default'}]")
    if prepared < reused:
        raise AssertionError(
            f"prepared={prepared} is below reused={reused} [{route or 'default'}]")
    if declined <= 0:
        raise AssertionError(f"ground controls did not decline reuse [{route or 'default'}]")
    if counters.get("space-store-prepared", 0) < reused:
        raise AssertionError(f"shared store preparation is missing [{route or 'default'}]")
    if counters.get("space-store-receipt", 0) != 0:
        raise AssertionError(f"ordinary transform allocated a receipt [{route or 'default'}]")
    if counters.get("space-store-publish-stale", 0) != 0:
        raise AssertionError(f"undisturbed publication became stale [{route or 'default'}]")
    return prepared, reused, declined


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: check_transform_store_effect_stats.py CETTA_BINARY")
    for route in ROUTES:
        prepared, reused, declined = observe(sys.argv[1], route)
        print(f"PASS: open take preparation/reuse [{route or 'default'}]: "
              f"prepared={prepared} reused={reused} declined={declined}")


if __name__ == "__main__":
    main()
