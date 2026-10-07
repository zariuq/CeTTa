#!/usr/bin/env python3
"""Generate informational set-profile data from its checked Lean catalogue."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


STAGES = {"declared-theory", "constructed-model", "checked-interpretation", "executable-fragment"}
KERNEL_SOURCES = {"local-kernel-declaration", "external-kernel-project"}
STANDARD_AXIOMS = {"propext", "Classical.choice", "Quot.sound"}
CHOICE_LEVELS = {"none", "countable", "dependent", "full"}
CHOICE_STANCES = {"unspecified", "not-adopted", "adopted", "refuted"}
SOURCE_KINDS = {"local-kernel-declaration", "external-kernel-project", "primary-literature",
                "native-replay", "declared-specification"}
TYPE_CHOICE_REFERENCES = {
    "proof": ("Mettapedia.SetTheory.Profiles.ProfileChoice.piSigmaChoice_exists", "theorem"),
    "computedWitnesses": ("Mettapedia.SetTheory.Profiles.ProfileChoice.piSigmaChoice", "definition"),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(command, directory):
    result = subprocess.run(command, cwd=directory, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("Catalogue qualification failed:\n" + result.stdout + result.stderr)
    return result.stdout


def local_closure(lean_root, entry):
    pending = [entry]
    sources = {}
    while pending:
        name = pending.pop()
        if name in sources:
            continue
        path = lean_root / (name.replace(".", "/") + ".lean")
        text = path.read_text()
        sources[name] = path
        pending.extend(imported for imported in re.findall(r"^import\s+(Mettapedia\.[\w.]+)\s*$", text, re.M)
                       if imported not in sources)
    return sources


def validate_rows(rows):
    references = {}
    profiles = []
    values = []
    environments = []
    type_choices = []
    for row in rows:
        kind = row["kind"]
        if kind == "local-reference":
            name = row["reference"]
            if name in references:
                raise ValueError("Duplicate local catalogue reference: " + name)
            manifest = row["manifest"]
            declaration = manifest["declaration"]
            if (manifest["containsAdmission"] or not declaration["hasCheckedBody"] or
                    declaration["unsafe"] or declaration["partial"] or
                    not declaration["fullType"] or declaration["name"] != name):
                raise ValueError("Unchecked catalogue reference: " + name)
            axioms = {axiom["name"] for axiom in manifest["transitiveAxioms"]}
            if not axioms <= STANDARD_AXIOMS:
                raise ValueError("Unexpected primitive dependencies: " + repr(sorted(axioms - STANDARD_AXIOMS)))
            references[name] = manifest
        elif kind == "profile":
            profiles.append(row)
        elif kind == "metta-catalogue":
            values.append(row)
        elif kind == "checking-environment":
            environments.append(row)
        elif kind == "type-theoretic-choice":
            type_choices.append(row)
            if (row["form"] != "PiSigma" or row["adoptsExtensionalChoice"] or
                    not row["requiresConstructedWitnesses"]):
                raise ValueError("Type-theoretic and extensional Choice were conflated")
        else:
            raise ValueError("Unknown catalogue export row: " + kind)
    if len(values) != 1 or len(environments) != 1:
        raise ValueError("Catalogue value or checking environment is missing or repeated")
    if len(type_choices) != 1:
        raise ValueError("Exactly one type-theoretic Choice theorem is required")
    for field, (expected, declaration_kind) in TYPE_CHOICE_REFERENCES.items():
        name = type_choices[0][field]
        if name != expected or name not in references:
            raise ValueError("Type-theoretic Choice reference lacks its audit")
        if references[name]["declaration"]["kind"] != declaration_kind:
            raise ValueError("Type-theoretic Choice reference has the wrong declaration kind")
    if not profiles or len({profile["id"] for profile in profiles}) != len(profiles):
        raise ValueError("Catalogue has no profiles or repeats a profile identifier")
    for profile in profiles:
        if profile["schemaVersion"] != 1 or profile["nativeAssumptionAuthority"]:
            raise ValueError("Catalogue data cannot confer native assumption authority")
        if set(profile["statuses"]) != STAGES:
            raise ValueError("The four distinct evidence stages are required")
        evidence = [item for items in profile["statuses"].values() for item in items]
        evidence += profile.get("reportedComparisons", [])
        for stage, items in profile["statuses"].items():
            for item in items:
                if item["stage"] != stage:
                    raise ValueError("Evidence is filed under a different stage")
                if stage == "constructed-model":
                    expected = ("local-kernel-reference" if item["source"]["kind"] == "local-kernel-declaration"
                                else "reported-external-construction")
                    if item.get("modelChecking") != expected:
                        raise ValueError("Constructed-model provenance was misclassified")
                if stage == "checked-interpretation" and item["source"]["kind"] not in KERNEL_SOURCES:
                    raise ValueError("A reported interpretation has no named kernel checker")
        for item in profile.get("reportedComparisons", []):
            if item["source"]["kind"] in KERNEL_SOURCES:
                raise ValueError("A reported comparison was relabelled as kernel evidence")
        for item in evidence + profile["choice"]:
            source = item["source"]
            if source is None:
                continue
            if source["kind"] not in SOURCE_KINDS or not source["reference"]:
                raise ValueError("Unknown or unnamed catalogue evidence source")
            if source["kind"] == "local-kernel-declaration" and source["reference"] not in references:
                raise ValueError("A local evidence reference lacks its complete dependency audit")
        choices = profile["choice"]
        if len(choices) != 4 or {item["level"] for item in choices} != CHOICE_LEVELS:
            raise ValueError("Four distinct extensional Choice levels are required")
        by_level = {item["level"]: item for item in choices}
        for item in choices:
            if item["stance"] not in CHOICE_STANCES:
                raise ValueError("Unknown extensional Choice stance")
            if item["level"] == "none" and item["stance"] in {"adopted", "refuted"}:
                raise ValueError("No adoption of Choice is not a Choice principle or its negation")
            if item["stance"] in {"adopted", "refuted"} and item["source"] is None:
                raise ValueError("Choice adoption or refutation needs named evidence")
        selection = profile["choiceSelection"]
        if selection not in CHOICE_LEVELS | {None}:
            raise ValueError("Unknown extensional Choice level")
        adopted = {item["level"] for item in choices if item["stance"] == "adopted"}
        if selection is None and adopted:
            raise ValueError("Adopted Choice has no selected level")
        if selection == "none" and (by_level["none"]["stance"] != "not-adopted" or
                any(by_level[level]["stance"] not in {"not-adopted", "refuted"}
                    for level in CHOICE_LEVELS - {"none"})):
            raise ValueError("The no-Choice selection conflicts with its recorded commitments")
        if selection in CHOICE_LEVELS - {"none"} and (
                by_level[selection]["stance"] != "adopted" or
                by_level["none"]["stance"] != "not-adopted"):
            raise ValueError("The selected Choice principle is not adopted")
    value = values[0]
    if (value["schemaVersion"] != 1 or value["nativeAssumptionAuthority"] or
            not value["text"].startswith("(SetTheoryCatalogue ")):
        raise ValueError("Invalid informational catalogue value")
    environment = environments[0]["checkingEnvironment"]
    return profiles, references, value["text"], environment, type_choices[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lean_root = args.lean_root.resolve()
    entry = "Mettapedia.SetTheory.Profiles.ProfileCatalogueExport"
    exporter = lean_root / "scripts/export_profile_catalogue.lean"
    closure = local_closure(lean_root, entry)
    input_paths = list(closure.values()) + [exporter, lean_root / "lean-toolchain", lean_root / "lake-manifest.json"]
    input_hashes = {str(path.relative_to(lean_root)): digest(path) for path in input_paths}
    checked(["lake", "build", entry], lean_root)
    artifacts = {name.replace(".", "/") + ".olean":
                 lean_root / ".lake/build/lib/lean" / (name.replace(".", "/") + ".olean")
                 for name in closure}
    artifact_hashes = {name: digest(path) for name, path in artifacts.items()}
    raw = checked(["lake", "env", "lean", "-DwarningAsError=true", "--run", str(exporter)], lean_root)
    rows = [json.loads(line) for line in raw.splitlines() if line.strip()]
    profiles, references, value, environment, type_choice = validate_rows(rows)
    local_imports = {name for name in environment["importClosure"] if name.startswith("Mettapedia.")}
    if local_imports - set(closure):
        raise ValueError("The source snapshot omits checked local imports: " + repr(sorted(local_imports - set(closure))))
    for relative, expected in input_hashes.items():
        if digest(lean_root / relative) != expected:
            raise RuntimeError("Catalogue source changed during qualification: " + relative)
    for relative, expected in artifact_hashes.items():
        if digest(artifacts[relative]) != expected:
            raise RuntimeError("Catalogue proof artifact changed during qualification: " + relative)
    header = "/* Informational data from the checked evidence-scoped catalogue. */\n"
    header += "static const char SJ_PROFILE_CATALOGUE_TEXT[] =\n"
    header += "\n".join("    " + json.dumps(value[offset:offset+240], ensure_ascii=True)
                        for offset in range(0, len(value), 240)) + ";\n"
    metadata = {
        "format": "prime-profile-catalogue-v1", "nativeAssumptionAuthority": False,
        "profiles": profiles, "localReferences": references, "checkingEnvironment": environment,
        "typeTheoreticChoice": type_choice,
        "inputs": input_hashes, "artifacts": artifact_hashes,
        "export_sha256": hashlib.sha256(raw.encode()).hexdigest(),
        "catalogue_sha256": hashlib.sha256(value.encode()).hexdigest(), "metta": value,
    }
    outputs = {
        root / "src/generated/prime_profile_catalogue.generated.h": header,
        root / "tests/prime/set_profiles/generated_catalogue.json": json.dumps(metadata, indent=2) + "\n",
    }
    changed = []
    for path, content in outputs.items():
        if path.exists() and path.read_text() == content:
            continue
        changed.append(str(path.relative_to(root)))
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    if args.check and changed:
        raise SystemExit("Stale generated catalogue: " + ", ".join(changed))
    print(json.dumps({"profiles": len(profiles), "local_references": len(references),
                      "local_source_modules": len(closure), "changed": changed}))


if __name__ == "__main__":
    main()
