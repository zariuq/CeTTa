#!/usr/bin/env python3
"""Replay native common-material observations against the checked compiler."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replay(binary, root, directory, label, program, expected, route=(), environment=None):
    source = directory / (label + ".metta")
    source.write_text(program)
    source_hash = hashlib.sha256(program.encode()).hexdigest()
    if digest(source) != source_hash:
        raise RuntimeError(label + ": probe changed before replay")
    process_environment = dict(os.environ)
    process_environment.pop("CETTA_PRIME_RELATIONAL_PLAN_REFERENCE", None)
    process_environment.update(environment or {})
    result = subprocess.run([str(binary), "--lang", "prime", *route, str(source)],
                            cwd=root, capture_output=True, text=True, timeout=180,
                            env=process_environment)
    if digest(source) != source_hash:
        raise RuntimeError(label + ": probe changed during replay")
    (directory / (label + ".stdout")).write_text(result.stdout)
    (directory / (label + ".stderr")).write_text(result.stderr)
    actual = result.stdout.splitlines()
    if result.returncode or actual != expected:
        mismatch = next((i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1]),
                        min(len(actual), len(expected)))
        raise RuntimeError(f"{label}: exit={result.returncode}, first mismatch={mismatch}, "
                           f"actual lines={len(actual)}, expected lines={len(expected)}")
    return {"name": label, "observations": len(expected), "source_sha256": source_hash,
            "stdout_sha256": digest(directory / (label + ".stdout")), "route": list(route),
            "environment": environment or {}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True,
                        help="Checked mathematical sources and compiled artifacts")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    generator = root / "scripts/generate_prime_common_set_profiles.py"
    checker = Path(__file__).resolve()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = root / "tests/prime/common_set/generated_compiler_cases.json"
    checked_inputs = {binary: digest(binary), fixture: digest(fixture),
                      generator: digest(generator), checker: digest(checker)}
    metadata = json.loads(fixture.read_text())
    lean_root = args.lean_root.resolve()
    for name, expected_hash in metadata["inputs"].items():
        source = lean_root / name
        if digest(source) != expected_hash:
            raise RuntimeError(f"Stale compiler source: {name}")
        checked_inputs[source] = expected_hash
    for name, expected_hash in metadata["artifacts"].items():
        artifact = lean_root / ".lake/build/lib/lean" / name
        if digest(artifact) != expected_hash:
            raise RuntimeError(f"Stale compiler artifact: {name}")
        checked_inputs[artifact] = expected_hash
    # Source/artifact hashes alone do not authenticate the retained case list.
    # Reproduce it, the signatures and the audit from current checked sources.
    generation = subprocess.run(["python3", str(generator), "--lean-root", str(lean_root), "--check"],
                                cwd=root, capture_output=True, text=True)
    if generation.returncode:
        raise RuntimeError("Fresh compiler qualification failed:\n" +
                           generation.stdout + generation.stderr)
    controls = metadata["compiler_controls"]
    statements = []
    expected = []
    for row in controls:
        if row["kind"] == "extra-law":
            continue
        name = row["name"] if row["kind"] == "primitive-law" else (
            f'({row["schema"]} {row["parameter_count"]} {row["source"]})')
        statements.append(f"!(set:known-proposition {name})")
        expected.append("[" + row["native"] + "]")
    program = "\n".join(statements) + "\n"
    results = []
    for label, route, environment in [
            ("compiler-default", (), {}),
            ("compiler-reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
            ("compiler-fuel", ("--fuel", "10000000"), {})]:
        results.append(replay(binary, root, output, label, program, expected, route, environment))

    # The logical extensions have independently compiled exact statements.
    extras = {row["name"]: row for row in controls if row["kind"] == "extra-law"}
    extension_program = []
    extension_expected = []
    for profile in ["prime-common", "foundation-common", "classical-common", "classical-foundation-common"]:
        extension_program += [f"!(bind! &{profile} (new-space))",
                              f"!(add-atom &{profile} (set:profile {profile}))"]
        extension_expected += ["[()]", "[()]"]
        extension_program.append(f"!(set:signature-digest &{profile})")
        extension_expected.append('["' + metadata["profiles"][profile]["signature_sha256"] + '"]')
        for law in ["foundationLaw", "excludedMiddle"]:
            present = ((law == "foundationLaw" and "foundation" in profile) or
                       (law == "excludedMiddle" and "classical" in profile))
            if present:
                extension_program.append(f"!(set:known-proposition &{profile} {law})")
                extension_expected.append("[" + extras[law]["native"] + "]")
            else:
                extension_program.append(f"!(car-atom (try (set:known-proposition &{profile} {law})))")
                extension_expected.append("[Undetermined]")
    results.append(replay(binary, root, output, "profile-extensions",
                          "\n".join(extension_program) + "\n", extension_expected))

    # Proof currentness, source boundaries and resource status are checked on
    # every route, using independently authored expected observations.
    routes = [("default", (), {}),
              ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
              ("fuel", ("--fuel", "10000000"), {})]
    for relative in ["tests/prime/common_set/default_core.metta",
                     "tests/prime/common_set/assumption_origins.metta",
                     "tests/prime/common_set/construction_origins.metta",
                     "tests/prime/common_set/schema_resources.metta",
                     "tests/prime/scoped/rule_dependency_currentness.metta",
                     "tests/prime/scoped/typing_dependency_currentness.metta",
                     "tests/prime/scoped/imported_proof_replay.metta"]:
        source = root / relative
        expected_file = source.with_suffix(".expected")
        checked_inputs[source] = digest(source)
        checked_inputs[expected_file] = digest(expected_file)
        expected_lines = expected_file.read_text().splitlines()
        for route_name, route, environment in routes:
            results.append(replay(binary, root, output, source.stem + "-" + route_name,
                                  source.read_text(), expected_lines, route, environment))

    manifest_read = """!(let (SetFoundationManifest (profile $profile) (signature-digest $digest)
      (checker $checker) (identity-policy $identity) (identity-region-assumptions $regions)
      (typed-declarations $declarations)
      (admitted-type-rules $type_rules)
      (profile-source $source) (adopted-rules $rules) (interpretations $interpretations)
      (admitted-assumptions $assumptions) (admitted-definitions $definitions)
      (admitted-constructions $constructions))
      (set:signature manifest) (IdentityEnvironment $identity (size-atom $regions)))
"""
    for policy in ["identity-j", "identity-scoped", "identity-uip", "identity-univalence"]:
        results.append(replay(binary, root, output, "manifest-" + policy,
                              manifest_read, [f"[(IdentityEnvironment {policy} 0)]"],
                              ("--profile", policy)))
    # The explicit declaration is removed directly so unrelated type
    # declarations do not contribute query answers to this control.
    region_program = manifest_read + "!(id:region prop)\n" + manifest_read
    region_program += "!(remove-atom &self (: id:assumed-unique@prop "
    region_program += "(-> (a : prop) (b : prop) (p : (id prop a b)) (q : (id prop a b)) "
    region_program += "(id (id prop a b) p q))))\n" + manifest_read
    results.append(replay(binary, root, output, "manifest-region-withdrawal", region_program,
                          ["[(IdentityEnvironment identity-scoped 0)]", "[id:assumed-unique@prop]",
                           "[(IdentityEnvironment identity-scoped 1)]", "[()]",
                           "[(IdentityEnvironment identity-scoped 0)]"],
                          ("--profile", "identity-scoped")))

    for path, expected_hash in checked_inputs.items():
        if digest(path) != expected_hash:
            raise RuntimeError(f"Qualification input changed during replay: {path.name}")
    payload = {"format": "prime-common-native-qualified-v1", "binary_sha256": checked_inputs[binary],
               "compiler_cases_sha256": checked_inputs[fixture], "results": results,
               "generator_sha256": checked_inputs[generator], "checker_sha256": checked_inputs[checker],
               "fresh_generation": json.loads(generation.stdout),
               "boundary": "Native observations; mathematical interpretation proofs are audited separately."}
    (output / "qualified.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"passed": len(results), "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
