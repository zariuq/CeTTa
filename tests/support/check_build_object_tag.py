#!/usr/bin/env python3
"""Exercise recursive configuration isolation and reject its original failure."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def clean_environment() -> dict[str, str]:
    env = os.environ.copy()
    for key in list(env):
        if key in {"MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "MAKELEVEL"} or key.startswith(
            "CETTA_BUILD_OBJ_TAG_"
        ):
            del env[key]
    return env


def run_probe(root: Path, rules: Path, directory: Path, tag: str = "forced-lane") -> list[tuple[str, str, str, str]]:
    env = clean_environment()
    result = subprocess.run(
        [
            "make", "--no-print-directory", "-s", "-f",
            "tests/support/build_object_tag_probe.mk", "BUILD_OBJ_TAG=" + tag,
            "TAG_RULES=" + str(rules), "matrix",
        ],
        cwd=root, env=env, text=True, capture_output=True, check=False,
    )
    (directory / "stdout").write_text(result.stdout)
    (directory / "stderr").write_text(result.stderr)
    if result.returncode:
        raise AssertionError(result.stderr or result.stdout)
    return [tuple(line.split("\t")[1:]) for line in result.stdout.splitlines()
            if line.startswith("TagProbe\t")]


def check(rows: list[tuple[str, str, str, str]], tag: str = "forced-lane") -> int:
    # Each explicitly selected namespace runs the same matrix. Child choices
    # are independent expected distinctions, rather than expected hash values.
    roots = [i for i, row in enumerate(rows) if row[0] in {"root", "explicit"}]
    if len(roots) != 2:
        raise AssertionError("both namespaces must be observed")
    checks = 0
    for number, start in enumerate(roots):
        end = roots[number + 1] if number + 1 < len(roots) else len(rows)
        panel = dict((label, (tag, identity, binary)) for label, tag, identity, binary in rows[start:end])
        namespace = tag if number == 0 else "another-lane"
        root_label = "root" if number == 0 else "explicit"
        for label in [root_label, "same", "returned"]:
            if panel[label][2] != namespace or panel[label] != panel[root_label]:
                raise AssertionError(f"{label} changed the primary addresses")
            checks += 1
        changes = ["nogmp", "nojson", "causal", "stats", "timing", "compiler", "cflags",
                   "cppflags", "ldflags", "python", "mork", "pathmap"]
        tags = [panel[label][0] for label in changes]
        if panel[root_label][0] in tags or len(set(tags)) != len(tags):
            raise AssertionError("different configurations aliased an object address")
        if any(not (address.startswith(namespace + ".config-") or address.startswith("tagged.config-"))
               or len(address) > 160 for address in tags):
            raise AssertionError("a changed configuration escaped its namespace")
        checks += len(changes)
        if panel[root_label][0] == namespace or any(panel[label][2] != panel[label][0] for label in changes):
            raise AssertionError("objects or variant executables are not configuration addressed")
        checks += 1
        if panel["nested"] != panel["nogmp"] or panel["nested-same"] != panel["nogmp"]:
            raise AssertionError("nesting changed an unchanged configuration")
        if panel["nested-other"] != panel["nojson"]:
            raise AssertionError("nested selection differs from sibling selection")
        checks += 3
    return checks


def compilation_roundtrip(root: Path, directory: Path) -> int:
    env = clean_environment()
    namespace = "same-primary"
    hashes = []
    import hashlib
    for number, value in enumerate([1, 2, 1]):
        result = subprocess.run(
            ["make", "--no-print-directory", "-s", "-f", "tests/support/build_object_tag_probe.mk",
             "BUILD_OBJ_TAG=" + namespace, "PROBE_OUT=" + str(directory.resolve()),
             "PROBE_VALUE=" + str(value), "small-binary"],
            cwd=root, env=env, capture_output=True, text=True, check=False,
        )
        (directory / f"leg-{number}.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(result.stderr)
        binary = directory / namespace
        answer = subprocess.run([str(binary.resolve())], capture_output=True, text=True, check=True)
        if answer.stdout != str(value) + "\n":
            raise AssertionError("a top-level configuration return leg linked stale objects")
        hashes.append(hashlib.sha256(binary.read_bytes()).hexdigest())
    if hashes[0] != hashes[2] or hashes[0] == hashes[1]:
        raise AssertionError("the compiled return-leg binary disagrees")
    return 4


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    directory = args.output or Path(tempfile.mkdtemp(prefix="build-object-tag.", dir=root / "runtime"))
    directory.mkdir(parents=True, exist_ok=True)
    positive = directory / "positive"
    positive.mkdir(exist_ok=True)
    count = check(run_probe(root, root / "make/build_object_tag.mk", positive))
    long_tag = "forced-lane-" + "x" * 160
    long_panel = directory / "long-namespace"
    long_panel.mkdir(exist_ok=True)
    count += check(run_probe(root, root / "make/build_object_tag.mk", long_panel, long_tag), long_tag)
    compiled = directory / "compiled-return-leg"
    compiled.mkdir(exist_ok=True)
    count += compilation_roundtrip(root, compiled)
    # An adversarial control restores the old configuration-insensitive tag.
    mutant = directory / "unisolated.mk"
    mutant.write_text("BUILD_TAG_CONFIG_ID := fixed\nBUILD_BINARY_TAG = $(BUILD_OBJ_TAG)\n")
    negative = directory / "negative"
    negative.mkdir(exist_ok=True)
    try:
        check(run_probe(root, mutant.resolve(), negative))
    except AssertionError:
        count += 1
    else:
        raise AssertionError("the original forced-tag failure escaped the control")
    print(f"PASS: recursive build-object tag isolation ({count} checks)")


if __name__ == "__main__":
    main()
