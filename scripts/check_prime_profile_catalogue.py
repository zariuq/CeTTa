#!/usr/bin/env python3
"""Qualify the informational catalogue and its native authority boundary."""

import argparse
import copy
import json
from pathlib import Path
import subprocess

from check_prime_common_set_profiles import digest, replay
from generate_prime_profile_catalogue import validate_rows


def require_rejection(rows, label, mutation):
    candidate = copy.deepcopy(rows)
    mutation(candidate)
    try:
        validate_rows(candidate)
    except (ValueError, KeyError):
        return {"name": label, "refused": True}
    raise RuntimeError("Invalid catalogue accepted: " + label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve()
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    generator = root / "scripts/generate_prime_profile_catalogue.py"
    fixture = root / "tests/prime/set_profiles/generated_catalogue.json"
    header = root / "src/generated/prime_profile_catalogue.generated.h"
    source = root / "src/prime_scoped_judgments.c"
    replay_helper = root / "scripts/check_prime_common_set_profiles.py"
    inputs = {path: digest(path) for path in [binary, generator, fixture, header, source,
                                             replay_helper, Path(__file__).resolve()]}
    generation = subprocess.run(["python3", str(generator), "--lean-root", str(args.lean_root.resolve()), "--check"],
                                cwd=root, capture_output=True, text=True)
    if generation.returncode:
        raise RuntimeError("Fresh catalogue qualification failed:\n" + generation.stdout + generation.stderr)
    metadata = json.loads(fixture.read_text())
    lean_root = args.lean_root.resolve()
    for relative, expected_hash in metadata["inputs"].items():
        path = lean_root / relative
        if digest(path) != expected_hash:
            raise RuntimeError("Stale catalogue source: " + relative)
        inputs[path] = expected_hash
    for relative, expected_hash in metadata["artifacts"].items():
        path = lean_root / ".lake/build/lib/lean" / relative
        if digest(path) != expected_hash:
            raise RuntimeError("Stale catalogue proof artifact: " + relative)
        inputs[path] = expected_hash
    value = metadata["metta"]
    program = """!(car-atom (try (set:known-proposition foundationLaw)))
!(car-atom (try (set:known-proposition universeIn)))
!(set:signature catalogue)
!(car-atom (try (set:known-proposition foundationLaw)))
!(car-atom (try (set:known-proposition universeIn)))
!(add-atom &self (Profile nf (Statuses (Status constructed-model forged-local-proof))))
!(car-atom (try (set:known-proposition nf)))
!(add-atom &self (set:profile nf))
!(set:signature catalogue)
!(remove-atom &self (set:profile nf))
!(set:signature catalogue)
"""
    expected = ["[Undetermined]", "[Undetermined]", "[" + value + "]",
                "[Undetermined]", "[Undetermined]", "[()]", "[Undetermined]", "[()]",
                "[(set:signature catalogue)]", "[()]", "[" + value + "]"]
    results = []
    for name, route, environment in [
            ("catalogue-default", (), {}),
            ("catalogue-reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
            ("catalogue-fuel", ("--fuel", "10000000"), {})]:
        results.append(replay(binary, root, directory, name, program, expected, route, environment))
    # These mutations test the evidence boundary before native generation;
    # an external paper cannot acquire local kernel credit by changing its label.
    rows = [{"kind": "local-reference", "reference": name, "manifest": manifest}
            for name, manifest in metadata["localReferences"].items()]
    rows += metadata["profiles"]
    rows += [metadata["typeTheoreticChoice"]]
    rows += [{"kind": "checking-environment", "checkingEnvironment": metadata["checkingEnvironment"]},
             {"kind": "metta-catalogue", "schemaVersion": 1, "text": value, "nativeAssumptionAuthority": False}]
    profile_row = next(index for index, row in enumerate(rows) if row["kind"] == "profile")
    controls = [
        require_rejection(rows, "catalogue-has-no-assumption-authority",
                          lambda candidate: candidate[profile_row].update(nativeAssumptionAuthority=True)),
        require_rejection(rows, "four-statuses-cannot-be-collapsed",
                          lambda candidate: candidate[profile_row]["statuses"].pop("constructed-model")),
        require_rejection(rows, "literature-is-not-a-checked-interpretation",
                          lambda candidate: candidate[profile_row]["statuses"]["checked-interpretation"].append({
                              "stage": "checked-interpretation", "scope": "whole-declared-theory",
                              "source": {"kind": "primary-literature", "reference": "paper", "anchor": "theorem"},
                              "statement": "purportedly kernel checked", "commitments": []})),
        require_rejection(rows, "missing-local-proof-audit-is-refused",
                          lambda candidate: candidate[profile_row]["statuses"]["declared-theory"].append({
                              "stage": "declared-theory", "scope": "whole-declared-theory",
                              "source": {"kind": "local-kernel-declaration", "reference": "Missing.proof", "anchor": ""},
                              "statement": "unchecked", "commitments": []})),
    ]
    controls += [
        require_rejection(rows, "type-choice-proof-is-required",
                          lambda candidate: candidate.__setitem__(slice(None),
                              [row for row in candidate if row["kind"] != "type-theoretic-choice"])),
        require_rejection(rows, "type-choice-proof-cannot-be-repeated",
                          lambda candidate: candidate.append(metadata["typeTheoreticChoice"])),
        require_rejection(rows, "reported-comparison-cannot-claim-kernel-status",
                          lambda candidate: candidate[profile_row]["reportedComparisons"].append({
                              "source": {"kind": "local-kernel-declaration", "reference": "Missing.proof"}})),
        require_rejection(rows, "paper-construction-cannot-claim-local-kernel-credit",
                          lambda candidate: candidate[profile_row]["statuses"]["constructed-model"].append({
                              "stage": "constructed-model", "scope": "whole-declared-theory",
                              "source": {"kind": "primary-literature", "reference": "paper", "anchor": "model"},
                              "modelChecking": "local-kernel-reference", "statement": "forged credit"})),
    ]
    nf_row = next(index for index, row in enumerate(rows) if row.get("id") == "nf")
    hotg_row = next(index for index, row in enumerate(rows) if row.get("id") == "hotg-theory")
    type_choice_row = next(index for index, row in enumerate(rows) if row["kind"] == "type-theoretic-choice")
    full = next(index for index, row in enumerate(rows[profile_row]["choice"]) if row["level"] == "full")
    controls += [
        require_rejection(rows, "choice-levels-cannot-repeat",
                          lambda candidate: candidate[profile_row]["choice"][full].update(level="countable")),
        require_rejection(rows, "choice-level-cannot-be-omitted",
                          lambda candidate: candidate[profile_row]["choice"].pop(full)),
        require_rejection(rows, "unknown-choice-level-is-refused",
                          lambda candidate: candidate[profile_row]["choice"][full].update(level="finite")),
        require_rejection(rows, "unknown-choice-stance-is-refused",
                          lambda candidate: candidate[profile_row]["choice"][full].update(stance="compatible")),
        require_rejection(rows, "refuted-choice-cannot-be-selected",
                          lambda candidate: candidate[nf_row].update(choiceSelection="full")),
        require_rejection(rows, "none-does-not-authorize-extensional-choice",
                          lambda candidate: candidate[profile_row]["choice"][full].update(
                              stance="adopted", source={"kind": "declared-specification", "reference": "forged"})),
        require_rejection(rows, "adopted-choice-needs-a-selection",
                          lambda candidate: candidate[hotg_row].update(choiceSelection=None)),
        require_rejection(rows, "selected-choice-must-be-adopted",
                          lambda candidate: candidate[profile_row].update(choiceSelection="dependent")),
        require_rejection(rows, "choice-refutation-needs-evidence",
                          lambda candidate: candidate[nf_row]["choice"][full].update(source=None)),
        require_rejection(rows, "choice-adoption-needs-evidence",
                          lambda candidate: candidate[hotg_row]["choice"][full].update(source=None)),
        require_rejection(rows, "nonadoption-is-not-refutation",
                          lambda candidate: candidate[profile_row]["choice"][0].update(
                              stance="refuted", source={"kind": "declared-specification", "reference": "forged"})),
        require_rejection(rows, "audited-unrelated-theorem-is-not-type-choice",
                          lambda candidate: candidate[type_choice_row].update(
                              proof="Mettapedia.SetTheory.Profiles.CommonCoreConstructive.interpretExtension")),
    ]
    for path, expected_hash in inputs.items():
        if digest(path) != expected_hash:
            raise RuntimeError("Qualification input changed during replay: " + path.name)
    payload = {"format": "prime-profile-catalogue-qualified-v1", "binary_sha256": inputs[binary],
               "header_sha256": inputs[header], "fixture_sha256": inputs[fixture],
               "source_sha256": inputs[source], "replay_helper_sha256": inputs[replay_helper],
               "results": results, "refusal_controls": controls,
               "fresh_generation": json.loads(generation.stdout),
               "boundary": "Informational catalogue and native replay; neither adopts a new set theory."}
    (directory / "qualified.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"passed": len(results), "observations": sum(row["observations"] for row in results),
                      "refusal_controls": len(controls)}))


if __name__ == "__main__":
    main()
