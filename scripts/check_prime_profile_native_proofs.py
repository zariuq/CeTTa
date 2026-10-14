#!/usr/bin/env python3
"""Replay rendered source derivations with their declared native authority."""

import argparse
import json
from pathlib import Path
import re
import subprocess

from check_prime_common_set_profiles import digest, replay
from generate_prime_profile_catalogue import local_closure
from prime_material_qualification import json_inputs, native_inputs, unchanged


def checked(command, root):
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("Source proof qualification failed:\n" + result.stdout + result.stderr)
    return result.stdout


def validated_module_audit(audit):
    owner = "Mettapedia.SetTheory.Profiles.ProfileNativeProofSerialization"
    if not isinstance(audit, dict) or audit.get("ownerModule") != owner:
        raise ValueError("The complete serialization module audit is absent")
    declarations = audit.get("declarations")
    implementations = audit.get("nonLogicalDeclarations")
    compiler_only = audit.get("compilerOnlyDeclarations")
    if (not isinstance(declarations, list) or not declarations or
            not isinstance(implementations, list) or not isinstance(compiler_only, list)):
        raise ValueError("The serialization audit omits owned declarations")
    names, checked_bodies = set(), 0
    for row in declarations:
        declaration = row.get("declaration", {})
        name = declaration.get("name")
        if (not name or name in names or declaration.get("originModule") != owner or
                declaration.get("unsafe") is not False or declaration.get("partial") is not False or
                declaration.get("kind") == "axiom" or not declaration.get("fullType") or
                row.get("containsAdmission") is not False):
            raise ValueError("An owned serialization declaration is not safe kernel evidence")
        dependencies = {item["name"] for item in row["transitiveAxioms"]}
        if not dependencies <= {"propext", "Quot.sound"}:
            raise ValueError("The serialization module uses Choice or an unpermitted axiom")
        names.add(name)
        checked_bodies += declaration.get("hasCheckedBody") is True
    runtime_names = set()
    for row in implementations:
        name, counterpart = row.get("name"), row.get("runtimeCounterpart")
        if (not name or name in names or name in runtime_names or counterpart not in names or
                row.get("checkingRole") != "nonLogicalImplementation" or
                row.get("hasCheckedBody") is not False or row.get("originModule") != owner):
            raise ValueError("A runtime implementation is not separated from its safe counterpart")
        runtime_names.add(name)
    if (len(names) != audit.get("kernelDeclarationCount") or
            checked_bodies != audit.get("checkedBodyDeclarationCount") or
            len(runtime_names) != audit.get("nonLogicalDeclarationCount") or
            any(not isinstance(name, str) for name in compiler_only) or
            len(compiler_only) != len(set(compiler_only)) or
            (set(compiler_only) & (names | runtime_names))):
        raise ValueError("The serialization module audit has inconsistent declaration accounting")


def validated_rows(rows):
    if not rows or rows[0].get("kind") != "manifest":
        raise ValueError("Missing checked source-proof manifest")
    manifest, cases = rows[0], rows[1:]
    validated_module_audit(manifest.get("serializationModuleAudit"))
    environment = manifest.get("checkingEnvironment")
    if (not isinstance(environment, dict) or not environment.get("leanVersion") or
            not environment.get("kernelRules") or not environment.get("importClosure")):
        raise ValueError("The source-proof host checking environment is absent")
    references = {}
    for reference in manifest.get("references", []):
        declaration = reference.get("declaration", {})
        name = declaration.get("name")
        if (not name or name in references or reference.get("containsAdmission") is not False or
                declaration.get("hasCheckedBody") is not True or declaration.get("unsafe") is not False or
                declaration.get("partial") is not False or not declaration.get("fullType")):
            raise ValueError("A source-proof reference lacks a safe checked body")
        axioms = {item["name"] for item in reference["transitiveAxioms"]}
        if not axioms <= {"propext", "Quot.sound"}:
            raise ValueError("A constructive source-proof reference has an unexpected dependency")
        references[name] = reference
    if not references:
        raise ValueError("The source-proof theorem audit is absent")
    expected_families = {"hypothesis", "weakening", "bottomElim", "bothIntro", "bothLeft", "bothRight",
                         "eitherLeft", "eitherRight", "eitherElim", "implyIntro", "implyElim",
                         "allIntro", "allElim", "existIntro", "existElim", "equalRefl", "equalElim"}
    if set(manifest.get("sourceConstructorFamilies", [])) != expected_families:
        raise ValueError("The source deduction constructor panel is incomplete")
    seen = set()
    for case in cases:
        if case.get("kind") != "source-proof-fixture" or not isinstance(case.get("id"), str):
            raise ValueError("Malformed source-proof fixture")
        if case["id"] in seen or not re.fullmatch(r"[A-Za-z0-9_-]+", case["id"]):
            raise ValueError("Repeated or invalid source-proof fixture identifier")
        seen.add(case["id"])
        for field in ["goal", "proof", "sourceTheorem", "sourceConclusion"]:
            if not isinstance(case.get(field), str) or not case[field]:
                raise ValueError("Missing typed source-proof field: " + field)
        if case["sourceTheorem"] not in references:
            raise ValueError("A replayed source proof has no audited kernel reference")
        declarations = case.get("requiredNativeDeclarations")
        if not isinstance(declarations, list) or any(not isinstance(item, str) for item in declarations):
            raise ValueError("Native declaration authority is not retained")
        for field in ["programs", "expected"]:
            if not isinstance(case.get(field), dict) or set(case[field]) != {"ordinary", "rule_constants"}:
                raise ValueError("Both native proof representations must be qualified")
        for representation in ["ordinary", "rule_constants"]:
            program, expected = case["programs"][representation], case["expected"][representation]
            if not isinstance(program, str) or not program.strip():
                raise ValueError("A proof replay program is absent")
            if not isinstance(expected, list) or not expected or any(not isinstance(line, str) for line in expected):
                raise ValueError("Independent expected proof observations are absent")
            if "set:native-proof/rules" in program:
                raise ValueError("The replay uses a nonexistent proof-package operation")
            theorem = "(set:theorem " + case["id"] + " " + case["goal"] + " " + case["proof"] + ")"
            if theorem not in program:
                raise ValueError("The replayed theorem does not use the rendered source derivation")
            spelling = ("set:native-proof " + case["id"] +
                        (" rule-constants" if representation == "rule_constants" else ""))
            if spelling not in program:
                raise ValueError("Replay does not check the rendered theorem's actual native package")
    if not cases:
        raise ValueError("The source-proof panel is empty")
    if "fixture_cases" in manifest and manifest["fixture_cases"] != len(cases):
        raise ValueError("The source-proof panel is incomplete")
    return manifest, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lean, binary, directory = args.lean_root.resolve(), args.binary.resolve(), args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    exporter = lean / "scripts/export_profile_native_proofs.lean"
    entries = re.findall(r"^import\s+(Mettapedia\.[\w.]+)\s*$", exporter.read_text(), re.M)
    if not entries:
        raise ValueError("Source-proof exporter has no checked local entry point")
    sources = {}
    for entry in entries:
        sources.update(local_closure(lean, entry))
    inputs = native_inputs(root, binary)
    for path in [*sources.values(), exporter, lean / "lean-toolchain", lean / "lake-manifest.json",
                 Path(__file__).resolve(), root / "scripts/check_prime_common_set_profiles.py",
                 root / "scripts/generate_prime_profile_catalogue.py"]:
        inputs[path] = digest(path)
    checked(["lake", "build", *entries], lean)
    artifacts = {lean / ".lake/build/lib/lean" / (name.replace(".", "/") + ".olean"):
                 digest(lean / ".lake/build/lib/lean" / (name.replace(".", "/") + ".olean"))
                 for name in sources}
    exported = checked(["lake", "env", "lean", "-DwarningAsError=true", "--run", str(exporter)], lean)
    (directory / "lean-source-proofs.jsonl").write_text(exported)
    manifest, cases = validated_rows([json.loads(line) for line in exported.splitlines()])
    routes = [("default", (), {}),
              ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
              ("fuel", ("--fuel", "10000000"), {})]
    results = []
    for case in cases:
        for representation in ["ordinary", "rule_constants"]:
            for name, route, environment in routes:
                results.append(replay(binary, root, directory,
                                      case["id"] + "-" + representation + "-" + name,
                                      case["programs"][representation], case["expected"][representation],
                                      route, environment))
    unchanged({**inputs, **artifacts})
    payload = {"format": "prime-source-native-proofs-qualified-v1",
               "binary_sha256": inputs[binary], "manifest": manifest,
               "source_proof_fixtures": len(cases), "results": results,
               "inputs": json_inputs(inputs), "artifacts": json_inputs(artifacts),
               "boundary": "Typed source compilation and recovery are kernel theorems. Rendering, native parsing and native proof-package agreement are separately replayed implementation evidence."}
    (directory / "qualified.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"passed": len(results), "source_proof_fixtures": len(cases),
                      "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
