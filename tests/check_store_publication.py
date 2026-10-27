#!/usr/bin/env python3
"""Compare the native publication fault traces with the checked Lean model.

This finite conformance bridge does not verify C, the compiler or filesystem.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def traces(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return sorted(line for line in result.stdout.splitlines() if line.startswith("STORE_PUBLICATION "))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--lean", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    actual = traces([str(args.native.resolve())])
    expected = traces([str(args.lean.resolve()), str(args.model.resolve())])
    if len(actual) != 6 or actual != expected:
        raise AssertionError(dict(native=actual, model=expected))
    root = Path(__file__).resolve().parents[1]
    files = [root / name for name in ("src/native_disk_store.c", "src/native_journal.c",
                                     "tests/test_native_disk_store.c")]
    receipt = {"traces": actual, "model_sha256": digest(args.model),
               "native_sha256": digest(args.native),
               "sources": {str(path.relative_to(root)): digest(path) for path in files},
               "scope": "finite publication-fault conformance; filesystem assumptions remain explicit"}
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
