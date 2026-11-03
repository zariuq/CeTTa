#!/usr/bin/env python3
"""Refuse every cold-start allocation, then retry in the surviving host."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--language", default="petta")
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    refused = 0
    for profile in ("-", "extended"):
        def run(site, mode):
            command = [binary, args.language, profile, str(site), mode]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode or result.stderr:
                raise RuntimeError(f"startup refusal failed: {command}\n{result.stdout}\n{result.stderr}")
            observed = json.loads(result.stdout)
            assert observed["recovered"] is True
            return observed
        baseline = run(2**64 - 1, "single")
        assert baseline["status"] == 0 and baseline["allocations"] > 0
        for mode in ("single", "persistent"):
            for site in range(baseline["allocations"]):
                observed = run(site, mode)
                assert observed["status"] != 0, (site, mode, observed)
                refused += 1
        print(f"startup refusal: {args.language}/{profile}, {baseline['allocations']} cold allocation sites, both denial modes and recovery pass", flush=True)
    print(f"Embedding cold-start refusals: {refused} rejected without terminating the host")


if __name__ == "__main__":
    main()
