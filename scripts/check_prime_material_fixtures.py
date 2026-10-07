#!/usr/bin/env python3
"""Replay portable material-profile controls on each evaluation route."""

import argparse
import json
from pathlib import Path

from check_prime_common_set_profiles import digest, replay
from prime_material_qualification import json_inputs, native_inputs, unchanged


FIXTURES = [
    "tests/prime/set_profiles/finite_graph_readouts.metta",
    "tests/prime/set_profiles/observed_graph_readouts.metta",
    "tests/prime/set_profiles/logical_equality_readouts.metta",
    "tests/prime/set_profiles/scott_canonical_readouts.metta",
    "tests/prime/set_profiles/stored_graph_readouts.metta",
    "tests/prime/set_profiles/logical_conversion_readouts.metta",
    "tests/prime/set_profiles/alias_carrier_refusals.metta",
    "tests/prime/set_profiles/open_readout_boundaries.metta",
    "tests/prime/set_profiles/definition_history_contracts.metta",
    "tests/prime/set_profiles/common_definition_history.metta",
    "examples/prime/sets/qualified_graph_readouts.metta",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve()
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    inputs = native_inputs(root, binary)
    for helper in [Path(__file__).resolve(), root / "scripts/check_prime_common_set_profiles.py"]:
        inputs[helper] = digest(helper)
    results = []
    for relative in FIXTURES:
        source = root / relative
        expectation = source.with_suffix(".expected")
        inputs[source] = digest(source)
        inputs[expectation] = digest(expectation)
        for name, route, environment in [
            ("default", (), {}),
            ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
            ("fuel", ("--fuel", "10000000"), {}),
        ]:
            results.append(replay(binary, root, directory, source.stem + "-" + name,
                                  source.read_text(), expectation.read_text().splitlines(),
                                  route, environment))
    unchanged(inputs)
    payload = {"format": "prime-material-fixtures-qualified-v1",
               "binary_sha256": inputs[binary], "fixtures": FIXTURES,
               "results": results, "inputs": json_inputs(inputs),
               "boundary": "Native replay against independent authored expectations; no Lean build or full-model existence claim."}
    (directory / "qualified.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"passed": len(results), "fixtures": len(FIXTURES),
                      "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
