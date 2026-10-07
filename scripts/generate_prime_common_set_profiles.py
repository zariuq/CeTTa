#!/usr/bin/env python3
"""Generate the common material signatures from the checked Lean exporter."""

import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess


def checked_command(command, directory):
    result = subprocess.run(command, cwd=directory, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("Compiler qualification failed:\n" + result.stdout + result.stderr)
    return result


def hol_signature(source):
    match = re.search(r'static const char SJ_HOL_SIGNATURE_TEXT\[\] =\s*(.*?);', source, re.S)
    if not match:
        raise ValueError("HOL signature declaration missing")
    return "".join(ast.literal_eval(literal)
                   for literal in re.findall(r'"(?:\\.|[^"\\])*"', match.group(1)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--export", type=Path,
                        help="Retained output to compare with a fresh checked export")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lean_root = args.lean_root.resolve()
    exporter = "scripts/export_common_set_profiles.lean"
    audit = "scripts/audit_common_set_profiles.lean"
    modules = ["CommonCore", "CommonCoreConstructive", "CommonCoreSyntax", "CommonCoreSubstitution",
               "CommonCoreClassicalLogic", "CommonCoreClassical", "CommonCoreClassicalCollection",
               "CommonCoreClassicalControls", "CommonCoreLogicalCompilation", "CommonCoreNativeScope",
               "CommonCoreNativeSchemas", "CommonSetProfiles"]
    inputs = [f"Mettapedia/SetTheory/Profiles/{module}.lean" for module in modules] + [exporter, audit]
    input_hashes = {name: hashlib.sha256((lean_root / name).read_bytes()).hexdigest()
                    for name in inputs}
    # Imported proof artifacts must match their current sources. A retained
    # JSONL file is comparison evidence, never an alternative source of laws.
    checked_command(["lake", "build", "Mettapedia.SetTheory.Profiles.CommonSetProfiles"], lean_root)
    artifact_paths = {f"Mettapedia/SetTheory/Profiles/{module}.olean":
                      lean_root / ".lake/build/lib/lean/Mettapedia/SetTheory/Profiles" /
                      (module + ".olean") for module in modules}
    artifact_hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                       for name, path in artifact_paths.items()}
    checked_command(["lake", "env", "lean", "-DwarningAsError=true", audit], lean_root)
    result = checked_command(["lake", "env", "lean", "-DwarningAsError=true", "--run", exporter], lean_root)
    raw = result.stdout
    for name, expected_hash in input_hashes.items():
        if hashlib.sha256((lean_root / name).read_bytes()).hexdigest() != expected_hash:
            raise RuntimeError(f"Compiler source changed during checking: {name}")
    for name, expected_hash in artifact_hashes.items():
        if hashlib.sha256(artifact_paths[name].read_bytes()).hexdigest() != expected_hash:
            raise RuntimeError(f"Compiler artifact changed during checking: {name}")
    if args.export and args.export.read_text() != raw:
        raise SystemExit("Retained export differs from the freshly checked compiler")
    rows = [json.loads(line) for line in raw.splitlines() if line.strip()]
    primitives = [row for row in rows if row["kind"] == "primitive-law"]
    extras = {row["name"]: row for row in rows if row["kind"] == "extra-law"}
    schemas = [row for row in rows if row["kind"] == "schema"]
    expected = {"coreEmpty", "corePairing", "coreUnion", "coreInfinity", "coreExtensionality"}
    if {row["name"] for row in primitives} != expected or len(primitives) != len(expected):
        raise ValueError("Unexpected primitive-law inventory")
    if set(extras) != {"foundationLaw", "excludedMiddle"}:
        raise ValueError("Unexpected extension-law inventory")
    if {row["schema"] for row in schemas} != {"boundedSeparation", "strongCollection", "subsetCollection"}:
        raise ValueError("Missing schema compiler controls")

    prelude = hol_signature((root / "src/prime_scoped_judgments.c").read_text())
    common = prelude + "(: In (-> set (-> set prop)))\n"
    common += "".join(f'(set:seed {row["name"]} {row["native"]})\n' for row in primitives)
    common += "(set:schema boundedSeparation bounded-formula-v1 1)\n"
    common += "(set:schema strongCollection material-formula-v1 2)\n"
    common += "(set:schema subsetCollection material-formula-v1 3)\n"
    common += "(set:fragment-certificate common-material-fo-v1 constructive-graph well-founded-sets hypersets)\n"
    common += "(set:checking-boundary material-fragment-models native-provider-qualified-separately)\n"

    foundation = f'(set:seed foundationLaw {extras["foundationLaw"]["native"]})\n'
    classical = f'(set:seed excludedMiddle {extras["excludedMiddle"]["native"]})\n'
    profiles = {
        "prime-common": ("SJ_PRIME_COMMON_SIGNATURE_TEXT", common),
        "foundation-common": ("SJ_FOUNDATION_COMMON_SIGNATURE_TEXT", common + foundation),
        "classical-common": ("SJ_CLASSICAL_COMMON_SIGNATURE_TEXT", common + classical),
        "classical-foundation-common": ("SJ_CLASSICAL_FOUNDATION_COMMON_SIGNATURE_TEXT", common + foundation + classical),
    }
    header = "/* Generated by the checked common material exporter. */\n"
    for _, (constant, signature) in profiles.items():
        header += f"static const char {constant}[] =\n"
        header += "\n".join("    " + json.dumps(line, ensure_ascii=True)
                            for line in signature.splitlines(keepends=True)) + ";\n\n"
    metadata = {
        "format": "prime-common-set-profiles-v1",
        "inputs": input_hashes,
        "artifacts": artifact_hashes,
        "profiles": {name: {"signature": signature,
                            "signature_sha256": hashlib.sha256(signature.encode()).hexdigest()}
                     for name, (_, signature) in profiles.items()},
        "compiler_controls": [{key: value for key, value in row.items()
                               if key not in {"source_ast", "native_ast"}} for row in rows],
        "checked_export_sha256": hashlib.sha256(raw.encode()).hexdigest(),
        "interpretation_boundary": "The checked material fragment; native replay is separate evidence.",
    }
    outputs = {
        root / "src/generated/prime_common_set_profiles.generated.h": header.rstrip() + "\n",
        root / "tests/prime/common_set/generated_compiler_cases.json": json.dumps(metadata, indent=2) + "\n",
    }
    for name in profiles:
        outputs[root / "lib/profiles" / (name.replace("-", "_") + ".metta")] = (
            "; An explicit common-material interpretation; additional laws are profile-local.\n"
            f"(set:profile {name})\n")
    changed = []
    for path, text in outputs.items():
        if path.exists() and path.read_text() == text:
            continue
        changed.append(str(path.relative_to(root)))
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    if args.check and changed:
        raise SystemExit("Stale generated profiles: " + ", ".join(changed))
    print(json.dumps({"profiles": list(profiles), "schema_cases": len(schemas),
                      "generated_files": len(outputs), "changed": changed}))


if __name__ == "__main__":
    main()
