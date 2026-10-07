#!/usr/bin/env python3
"""Replay native checking against independently checked source-wire controls."""

import argparse
import json
from pathlib import Path
import subprocess

from check_prime_common_set_profiles import digest, replay
from generate_prime_profile_catalogue import local_closure
from prime_material_qualification import json_inputs, native_inputs, unchanged


ENTRY = "Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased.NativeDependentSourceCheckingControls"
BASE = "Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId."
ADAPTER = "Mettapedia.Languages.MeTTa.PrimeCandidates.DeclarationBased."
MODULES = ["Mettapedia.GSLT.LanguageDef.CettaWireEquality"] + [BASE + suffix for suffix in [
    "NativeSyntax", "NativeSubstitution", "TypedEquality.Normalization.ExecutableReduction",
    "TypedEquality.Normalization.ExecutableConversion", "TypedEquality.Normalization.ExecutableChecking",
    "TypedEquality.Normalization.ExecutableWrittenChecking", "TypedEquality.Normalization.ExecutableRawChecking",
    "TypedEquality.Normalization.ExecutableWeakHead", "Instances.ExecutableTowerChoices",
    "Instances.ExecutableTowerNumbers", "Instances.ExecutableTowerNumbersWeakHead",
    "Examples.ExecutableCheckingControls", "Examples.ExecutableWrittenCheckingControls",
    "Examples.ExecutableWeakHeadControls",
]] + [ADAPTER + suffix for suffix in [
    "NativeDependentSourceWire", "NativeDependentSourceChecking", "NativeDependentSourceCheckingControls",
]]

# Ordinary formation distinguishes a non-type from a failed term checking
# request. The two explicit formation controls are outside the native request
# class. The reference procedure returns no certificate for both; it does not
# use absence of a bounded certificate as a proof of refutation.
OUTSIDE_FORMATION = {"non-type-context", "non-type-expected"}
FIXTURES = [
    "tests/prime/conformance/regular_kernel_constructors.metta",
    "tests/prime/scoped/refutation_needs_principal_types.metta",
    "tests/prime/scoped/cumulativity_curriculum.metta",
    "tests/prime/scoped/inductive_parameters.metta",
    "tests/prime/scoped/induction_curriculum.metta",
    "tests/prime/scoped/definition_revision_closure.metta",
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
    audit = lean / "scripts/audit_profile_executable_checking_contracts.lean"
    export = lean / "scripts/export_prime_dependent_checker_controls.lean"
    inputs = native_inputs(root, binary)
    audit_sources = local_closure(lean, "Mettapedia.Logic.KernelFoundationManifest")
    providers = subprocess.run(
        ["rg", "--files", "--hidden", "--no-ignore", "native",
         "experiments/gslt2parse_foundation/native", "tools", "vendor"],
        cwd=root, text=True, capture_output=True, check=True,
    )
    for relative in providers.stdout.splitlines():
        path = root / relative
        if path.suffix in {".c", ".h", ".inc", ".def"}:
            inputs[path] = digest(path)
    for path in [*sources.values(), *audit_sources.values(), audit, export,
                 lean / "lean-toolchain", lean / "lake-manifest.json", lean / "lakefile.lean",
                 Path(__file__).resolve(), root / "scripts/check_prime_common_set_profiles.py",
                 root / "scripts/generate_prime_profile_catalogue.py",
                 root / "scripts/prime_material_qualification.py",
                 root / "make/build_object_tag.mk",
                 root / "src/generated/cetta_execution_contracts.generated.mk"]:
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
        raise RuntimeError("Unsafe or incomplete checking contracts")
    exported = checked(command("file", "-DwarningAsError=true", "--run", str(export)),
                       lean, directory, "independent-observations")
    probes = [json.loads(line) for line in exported.splitlines()]
    labels = [row["label"] for row in probes]
    if len(labels) != len(set(labels)) or len(labels) != 22 or not OUTSIDE_FORMATION <= set(labels):
        raise RuntimeError("Incomplete independent observation panel")
    program = [
        "(set:profile hol)",
        "!(bind! &s (let $space (new-space) (let $_ (add-atom $space (set:profile hol)) $space)))",
        "!(set:inductive &s num (u 0) (: zero num) (: succ (-> num num)))",
    ]
    expected = ["[()]", "[num]"]
    for row in probes:
        if not isinstance(row["expected"], bool):
            raise RuntimeError("Expected observation is not Boolean")
        status = ("Undetermined" if row["label"] in OUTSIDE_FORMATION else
                  "Established" if row["expected"] else "Refuted")
        if status == "Undetermined" and row["expected"]:
            raise RuntimeError("Outside-formation control unexpectedly accepted")
        program += ["; " + row["label"],
                    "!(car-atom (try (type:check &s " + row["source"] + " " + row["type"] + ")))"]
        expected.append("[" + status + "]")
    results = []
    routes = [("default", (), {}),
              ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
              ("fuel", ("--fuel", "10000000"), {})]
    for name, route, environment in routes:
        results.append(replay(binary, root, directory, "checked-wires-" + name,
                              "\n".join(program) + "\n", expected, route, environment))
    for relative in FIXTURES:
        source = root / relative
        expected_file = source.with_suffix(".expected")
        inputs[source], inputs[expected_file] = digest(source), digest(expected_file)
        for name, route, environment in routes:
            results.append(replay(binary, root, directory, source.stem + "-" + name,
                                  source.read_text(), expected_file.read_text().splitlines(), route, environment))
    unchanged({**inputs, **artifacts})
    qualification = {
        "format": "prime-executable-checking-contracts-v1",
        "binary_sha256": inputs[binary], "audit": summary, "independent_controls": probes,
        "outside_formation_controls": sorted(OUTSIDE_FORMATION), "fixtures": FIXTURES,
        "results": results, "inputs": json_inputs(inputs), "artifacts": json_inputs(artifacts),
        "boundary": "General theorems qualify reconstructed reference checking and exact source-wire lowering. C observations are independently replayed finite controls; universal C checker, schema, inspection, parser and allocation correspondence remain open.",
    }
    (directory / "qualified.json").write_text(json.dumps(qualification, indent=2) + "\n")
    print(json.dumps({"audit": summary, "controls": len(probes), "replays": len(results),
                      "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
