#!/usr/bin/env python3
"""Qualify authored Prime HOL reflexivity and dependent native proof use."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary, work = args.binary.resolve(), args.workdir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = root / "examples/prime/hol_light_native_evidence.metta"
    oracle = source.with_suffix(".expected")
    expected = oracle.read_text().splitlines()
    if len(expected) != 16 or expected[:7] != ["[()]"] * 7:
        raise AssertionError("native evidence observation inventory changed")
    routes = []
    for name, flags in (("default", []), ("hashcons", ["--eval-hashcons"])):
        started = time.monotonic()
        result = subprocess.run(
            [str(binary), "--lang", "prime", *flags, str(source)],
            cwd=root, text=True, capture_output=True, timeout=args.timeout)
        (work / f"{name}.stdout").write_text(result.stdout)
        (work / f"{name}.stderr").write_text(result.stderr)
        if result.returncode or result.stderr or result.stdout.splitlines() != expected:
            raise AssertionError(f"{name}: native evidence controls differ")
        routes.append({"route": name, "observations": 9,
                       "seconds": round(time.monotonic() - started, 3),
                       "stdout_sha256": digest(work / f"{name}.stdout")})
        print(f"{name}: 9 native evidence controls pass", flush=True)
    inputs = [source, oracle, Path(__file__), *[
        root / "lib/prime/hol_light" / f"{name}.metta"
        for name in ("data", "syntax", "kernel", "definitions", "derived", "profile", "native")]]
    receipt = {
        "schema": "prime-hol-native-evidence-v1",
        "authored_dialect": "prime",
        "binary_sha256": digest(binary),
        "input_sha256": {str(p.relative_to(root)): digest(p) for p in inputs},
        "routes": routes,
        "equality_interpretation": "predicate equality",
        "dependent_consumer": "identity of the native proof",
        "whole_authored_adapter_soundness_proved": False,
        "lambda_head_application_native_elaboration_qualified": True,
    }
    (work / "qualification.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
