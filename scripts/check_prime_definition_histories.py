#!/usr/bin/env python3
"""Qualify conservative definition theorems and independent native controls."""

import argparse
import json
from pathlib import Path
import subprocess

from check_prime_common_set_profiles import digest, replay
from generate_prime_profile_catalogue import local_closure
from prime_material_qualification import json_inputs, native_inputs, unchanged


ENTRY = "Mettapedia.SetTheory.Profiles.ProfileDefinitionHistoryContracts"
MODULES = [
    "Mettapedia.Logic.FinitaryRuleSystem.Tree",
    "Mettapedia.Logic.HOL.ProofSyntaxStructural",
    "Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.TypedEquality.DefinitionHistories",
    "Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Examples.DefinitionHistoryControls",
    "Mettapedia.Logic.HOL.Syntax.ConstantSubstitutionComposition",
    "Mettapedia.Logic.HOL.ProofSyntaxConstantSubstitution",
    "Mettapedia.Logic.HOL.ProofSyntaxHypothesisSubstitution",
    "Mettapedia.Logic.HOL.DefinitionHistorySemantics",
    "Mettapedia.Logic.HOL.DefinitionHistoryProofConservativity",
    "Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.HOL.HOLDefinitionHistoryErasure",
    "Mettapedia.SetTheory.Profiles.MaterialDefinitionHistoryControls",
    ENTRY,
]
FIXTURES = [
    "tests/prime/set_profiles/definition_history_contracts.metta",
    "tests/prime/set_profiles/common_definition_history.metta",
    "tests/prime/scoped/definition_revision_closure.metta",
    "tests/prime/scoped/typing_dependency_currentness.metta",
    "tests/prime/scoped/induction_append_nil.metta",
    "tests/prime/scoped/induction_curriculum.metta",
]


def checked(command, root, directory, label):
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    (directory / (label + ".stdout")).write_text(result.stdout)
    (directory / (label + ".stderr")).write_text(result.stderr)
    if result.returncode:
        errors = [line for line in result.stdout.splitlines() if "error:" in line]
        detail = "\n".join(errors[-8:]) or result.stderr[-2000:]
        raise RuntimeError(label + " failed (complete logs retained in " + str(directory) + "):\n" + detail)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lean-runner", type=Path,
                        help="Optional launcher accepting build and file subcommands")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lean, binary, directory = args.lean_root.resolve(), args.binary.resolve(), args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    sources = local_closure(lean, ENTRY)
    audit = lean / "scripts/audit_profile_definition_history_contracts.lean"
    audit_sources = local_closure(lean, "Mettapedia.Logic.KernelFoundationManifest")
    inputs = native_inputs(root, binary)
    # The executable also compiles local providers outside src/. Pin their
    # source data, build inputs and this gate's actual launcher separately
    # from the independently checked mathematical interpretation.
    provider_files = subprocess.run(
        ["rg", "--files", "--hidden", "--no-ignore", "native",
         "experiments/gslt2parse_foundation/native", "tools", "vendor"],
        cwd=root, text=True, capture_output=True, check=True,
    )
    for relative in provider_files.stdout.splitlines():
        path = root / relative
        if path.suffix in {".c", ".h", ".inc", ".def"}:
            inputs[path] = digest(path)
    inputs[root / "src/generated/cetta_execution_contracts.generated.mk"] = digest(
        root / "src/generated/cetta_execution_contracts.generated.mk")
    if args.lean_runner:
        inputs[args.lean_runner.resolve()] = digest(args.lean_runner.resolve())
    for path in [*sources.values(), *audit_sources.values(), audit,
                 lean / "lean-toolchain", lean / "lake-manifest.json",
                 lean / "lakefile.lean", root / "make/build_object_tag.mk",
                 Path(__file__).resolve(), root / "scripts/check_prime_common_set_profiles.py",
                 root / "scripts/generate_prime_profile_catalogue.py"]:
        inputs[path] = digest(path)

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
    exported = checked(command("file", "-DwarningAsError=true", "--run", str(audit)),
                       lean, directory, "exhaustive-audit")
    rows = [json.loads(line) for line in exported.splitlines()]
    summaries = [row for row in rows if row.get("kind") == "summary"]
    audited = [row["manifest"]["ownerModule"] for row in rows if row.get("kind") == "module"]
    if len(summaries) != 1 or len(audited) != len(MODULES) or set(audited) != set(MODULES):
        raise RuntimeError("The complete owned-module audit is absent")
    summary = summaries[0]
    if (summary.get("containsAdmission") is not False or
            summary.get("definitionChoiceDeclarations") != 0 or
            summary.get("hostChoiceDeclarations") != summary.get("legacyComparisonDeclarations") or
            summary.get("legacyComparisonDeclarations") != 1 or
            summary.get("safeKernelDeclarations", 0) <= 0 or
            summary.get("theorems", 0) <= 0):
        raise RuntimeError("Definition theorems are not choice-free safe kernel evidence")

    results = []
    for relative in FIXTURES:
        source = root / relative
        expected = source.with_suffix(".expected")
        inputs[source], inputs[expected] = digest(source), digest(expected)
        for name, route, environment in [
            ("default", (), {}),
            ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
            ("fuel", ("--fuel", "10000000"), {}),
        ]:
            results.append(replay(binary, root, directory, source.stem + "-" + name,
                                  source.read_text(), expected.read_text().splitlines(), route, environment))
    unchanged({**inputs, **artifacts})
    payload = {
        "format": "prime-definition-histories-qualified-v1",
        "binary_sha256": inputs[binary], "audit": summary, "fixtures": FIXTURES,
        "results": results, "inputs": json_inputs(inputs), "artifacts": json_inputs(artifacts),
        "boundary": "General conservative erasure and constructed-model theorems concern checked acyclic histories. Native alias admission and receipt currentness are separately replayed; full checker and recursive-admission correspondence remain open.",
    }
    (directory / "qualified.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"audit": summary, "passed": len(results), "fixtures": len(FIXTURES),
                      "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
