#!/usr/bin/env python3
"""Qualify level-instantiation proofs and separately replay native schema controls."""

import argparse
import json
from pathlib import Path
import subprocess

from check_prime_common_set_profiles import digest, replay
from generate_prime_profile_catalogue import local_closure
from prime_material_qualification import json_inputs, native_inputs, unchanged


ENTRY = "Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased.NativeDependentSchemaControls"
BASE = "Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId."
ADAPTER = "Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased."
MODULES = [BASE + suffix for suffix in [
    "AnnotatedHeadMapping", "TypedEquality.WrittenHeadMorphism",
    "TypedEquality.Normalization.ExecutableSourceTransport",
    "TypedEquality.Normalization.InductiveHeadMapping", "Instances.TowerNumbersLevelTransport",
]] + ["Mettapedia.TypeTheory.UniverseLevel.AssignmentResolution"] + [
    ADAPTER + suffix for suffix in ["NativeDependentLevelSubstitution",
                                  "NativeDependentSchemaInstantiation", "NativeDependentSchemaControls"]
]


def checked(command, root, directory, label):
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    (directory / (label + ".stdout")).write_text(result.stdout)
    (directory / (label + ".stderr")).write_text(result.stderr)
    if result.returncode:
        errors = [line for line in result.stdout.splitlines() if "error:" in line]
        raise RuntimeError(label + " failed:\n" + ("\n".join(errors[-8:]) or result.stderr[-2000:]))
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lean-runner", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lean, binary, directory = args.lean_root.resolve(), args.binary.resolve(), args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    sources = local_closure(lean, ENTRY)
    audit = lean / "scripts/audit_profile_level_instantiation_contracts.lean"
    inputs = native_inputs(root, binary)
    providers = subprocess.run(
        ["rg", "--files", "--hidden", "--no-ignore", "native",
         "experiments/gslt2parse_foundation/native", "tools", "vendor"],
        cwd=root, text=True, capture_output=True, check=True,
    )
    for relative in providers.stdout.splitlines():
        path = root / relative
        if path.suffix in {".c", ".h", ".inc", ".def"}:
            inputs[path] = digest(path)
    for path in [*sources.values(), *local_closure(lean, "Mettapedia.Logic.KernelFoundationManifest").values(),
                 audit, lean / "lean-toolchain", lean / "lake-manifest.json", lean / "lakefile.lean",
                 Path(__file__).resolve(), root / "scripts/check_prime_common_set_profiles.py",
                 root / "scripts/check_prime_level_schema_substitution.py",
                 root / "scripts/generate_prime_profile_catalogue.py",
                 root / "scripts/prime_material_qualification.py", root / "make/build_object_tag.mk",
                 root / "src/generated/cetta_execution_contracts.generated.mk"]:
        inputs[path] = digest(path)
    fixture = root / "tests/prime/scoped/level_schema_substitution.metta"
    expected = fixture.with_suffix(".expected")
    for path in [fixture, expected]:
        inputs[path] = digest(path)
    if args.lean_runner:
        inputs[args.lean_runner.resolve()] = digest(args.lean_runner.resolve())

    def command(mode, *arguments):
        if args.lean_runner:
            return [str(args.lean_runner.resolve()), mode, *arguments]
        return ["lake", "build", *arguments] if mode == "build" else ["lake", "env", "lean", *arguments]

    checked(command("build", ENTRY), lean, directory, "joined-build")
    for module in MODULES:
        checked(command("file", "-DwarningAsError=true", str(sources[module])),
                lean, directory, "strict-" + module.rsplit(".", 1)[-1])
    artifacts = {
        lean / ".lake/build/lib/lean" / (name.replace(".", "/") + ".olean"):
        digest(lean / ".lake/build/lib/lean" / (name.replace(".", "/") + ".olean"))
        for name in sources
    }
    raw_audit = checked(command("file", "-DwarningAsError=true", "--run", str(audit)),
                        lean, directory, "exhaustive-audit")
    rows = [json.loads(line) for line in raw_audit.splitlines()]
    summaries = [row for row in rows if row.get("kind") == "summary"]
    modules = [row["manifest"]["ownerModule"] for row in rows if row.get("kind") == "module"]
    if len(summaries) != 1 or len(modules) != len(MODULES) or set(modules) != set(MODULES):
        raise RuntimeError("Missing exhaustive owned-module audit")
    summary = summaries[0]
    if (summary.get("containsAdmission") is not False or summary.get("hostChoiceDeclarations") != 0 or
            summary.get("safeKernelDeclarations", 0) <= 0 or summary.get("theorems", 0) <= 0):
        raise RuntimeError("Unsafe or incomplete level-instantiation contracts")
    results = []
    for name, route, environment in [
        ("default", (), {}), ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
        ("fuel", ("--fuel", "10000000"), {}),
    ]:
        results.append(replay(binary, root, directory, "schema-" + name, fixture.read_text(),
                              expected.read_text().splitlines(), route, environment))
    unchanged({**inputs, **artifacts})
    qualification = {
        "format": "prime-level-instantiation-contracts-v1", "binary_sha256": inputs[binary],
        "audit": summary, "results": results, "inputs": json_inputs(inputs),
        "artifacts": json_inputs(artifacts),
        "boundary": "General source transport, canonical schema-entry lowering and solver semantics are kernel-checked. Native replay is finite evidence; compiled C, arbitrary signature search, parser, allocation and shared-budget correspondence remain open.",
    }
    (directory / "qualified.json").write_text(json.dumps(qualification, indent=2) + "\n")
    print(json.dumps({"audit": summary, "replays": len(results),
                      "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
