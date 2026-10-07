#!/usr/bin/env python3
"""Run the full forced-tag gate and preserve its primary binary and behavior."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--tag")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    directory = args.output or Path(tempfile.mkdtemp(prefix="build-object-tag-full.", dir=root / "runtime"))
    directory.mkdir(parents=True, exist_ok=True)
    tag = args.tag or "core." + directory.name
    binary = root / "runtime" / ("cetta-" + tag)
    env = os.environ.copy()
    for key in list(env):
        if key in {"MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "MAKELEVEL"} or key.startswith(
            "CETTA_BUILD_OBJ_TAG_"
        ):
            del env[key]
    base = ["make", "--no-print-directory", "BUILD=core", "CETTA_TEST_ISOLATED=1",
            "BUILD_OBJ_TAG=" + tag, "-j" + str(args.jobs)]
    receipt = {"tag": tag, "started": time.time(), "commands": [], "probes": {}}

    def run_make(name: str, targets: list[str]) -> None:
        command = base + targets
        with (directory / (name + ".log")).open("w") as log:
            result = subprocess.run(command, cwd=root, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, check=False)
        receipt["commands"].append({"command": command, "exit": result.returncode})
        (directory / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        if result.returncode:
            raise AssertionError(f"{name} failed; see {directory / (name + '.log')}")

    def probe(phase: str) -> None:
        expected = (root / "tests/support/build_object_tag_arithmetic.expected").read_text()
        rows = []
        for route, route_env in [([], {}), (["--fuel", "1000000"], {}),
                                 ([], {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"})]:
            command = [str(binary), "--quiet", "--lang", "prime"] + route + [
                "tests/support/build_object_tag_arithmetic.metta"
            ]
            result = subprocess.run(command, cwd=root, env={**env, **route_env}, capture_output=True,
                                    text=True, timeout=60, check=False)
            rows.append({"command": command, "environment": route_env, "exit": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
            if result.returncode or result.stdout != expected:
                receipt["probes"][phase] = rows
                raise AssertionError(f"arithmetic disagrees at {phase}: {result.stdout}")
        receipt["probes"][phase] = rows

    run_make("primary-build", ["all"])
    before = sha256(binary)
    receipt["primaryBefore"] = before
    probe("before")
    (directory / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Primary built; running the full forced-tag gate.", flush=True)
    run_make("forced-tag-test", ["-k", "test"])
    after = sha256(binary)
    receipt["primaryAfter"] = after
    probe("after")
    receipt["finished"] = time.time()
    receipt["unchanged"] = before == after
    (directory / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if before != after:
        raise AssertionError("a recursive test changed the primary binary")
    print("PASS: full forced-tag test preserves the primary hash and all three arithmetic routes")


if __name__ == "__main__":
    main()
