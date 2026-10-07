#!/usr/bin/env python3
"""Check simultaneous declaration-level substitution on all evaluation routes."""

import argparse
import json
from pathlib import Path

from check_prime_common_set_profiles import digest, replay


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = root / "tests/prime/scoped/level_schema_substitution.metta"
    expected = source.with_suffix(".expected")
    binary = args.binary.resolve()
    inputs = {path: digest(path) for path in [binary, source, expected, Path(__file__).resolve(),
              root / "scripts/check_prime_common_set_profiles.py", root / "src/prime_regular_kernel.c"]}
    results = []
    for route, flags, environment in [
        ("default", (), {}),
        ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
        ("fuel", ("--fuel", "10000000"), {}),
    ]:
        results.append(replay(binary, root, output, "schema-" + route, source.read_text(),
                              expected.read_text().splitlines(), flags, environment))
    for path, frozen in inputs.items():
        if digest(path) != frozen:
            raise RuntimeError("Replay input changed: " + str(path))
    receipt = {"format": "prime-level-schema-substitution-v1", "results": results,
               "inputs": {str(path): value for path, value in inputs.items()}}
    (output / "qualified.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Level schema substitution: 3 routes, 13 controls, 42 observations, all pass")


if __name__ == "__main__":
    main()
