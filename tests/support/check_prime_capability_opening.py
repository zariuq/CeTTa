#!/usr/bin/env python3
"""Suspended filter templates retain their named variable at every depth."""

import argparse
import os
from pathlib import Path
import subprocess


def nested(leaf, depth):
    return "(Box " * depth + leaf + ")" * depth


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True)
    args = parser.parse_args()
    binary = os.environ.get("CETTA_BIN", args.cetta)
    directory = Path("runtime/prime_capability_opening")
    directory.mkdir(parents=True, exist_ok=True)
    for depth in (0, 31, 32, 33, 63, 64, 65, 128, 512):
        source = "!(import! &self list)\n"
        for target in (3, 4):
            source += (
                "!(list:filter (1 2 3) $x (== "
                + nested("$x", depth) + " "
                + nested(str(target), depth) + "))\n"
            )
        fixture = directory / f"depth-{depth}.metta"
        fixture.write_text(source)
        result = subprocess.run(
            [binary, "--lang", "prime", str(fixture)],
            capture_output=True, text=True, timeout=60, check=False,
        )
        if result.returncode != 0 or result.stdout.splitlines() != ["[()]", "[(3)]", "[()]"]:
            raise SystemExit(
                f"FAIL: capability opening at depth {depth}: "
                f"exit {result.returncode}\n{result.stdout}{result.stderr}"
            )
    print("PASS: Prime filter templates preserve matches and misses at nine depths")


if __name__ == "__main__":
    main()
