#!/usr/bin/env python3
"""Check the inputs and syntax judgments of the upstream MM0 corpus."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/langdef/mm0/upstream_textual_syntax_v1.json"


def checked_cases(root, manifest=MANIFEST):
    records = json.loads(manifest.read_text())
    if records.get("schema") != "mm0-textual-corpus-v1":
        raise ValueError("unknown MM0 textual corpus schema")
    cases = records["cases"]
    names = [record["path"] for record in cases]
    if len(names) != len(set(names)):
        raise ValueError("duplicate MM0 specification in the corpus")
    actual = {path.relative_to(root).as_posix()
              for path in (root / "tests/mm0_mmu").rglob("*.mm0") if path.is_file()}
    if actual != set(names):
        raise ValueError(f"MM0 syntax corpus inventory changed: unclassified={sorted(actual - set(names))}, "
                         f"missing={sorted(set(names) - actual)}")
    for record in cases:
        path = Path(record["path"])
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("MM0 fixture paths must be relative to the upstream root")
        if record["syntax"] not in ("accepted", "rejected"):
            raise ValueError(f"missing syntax judgment: {path}")
        if record["failure_stage"] == "proof" and record["syntax"] != "accepted":
            raise ValueError(f"a proof refusal is not a syntax refusal: {path}")
        if hashlib.sha256((root / path).read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"MM0 specification changed: {path}")
        if "proof" in record:
            proof = record["proof"]
            proof_path = Path(proof["path"])
            if proof_path.is_absolute() or ".." in proof_path.parts or hashlib.sha256(
                    (root / proof_path).read_bytes()).hexdigest() != proof["sha256"]:
                raise ValueError(f"MM0 proof fixture changed: {path}")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mm0-root", required=True, type=Path)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    root = args.mm0_root.resolve()
    cases = checked_cases(root, args.manifest)
    if args.check_only:
        print(f"MM0 syntax inventory: {len(cases)} specifications, all inputs pinned")
    else:
        for record in cases:
            for field in (os.fsencode(root / record["path"]), record["syntax"].encode()):
                sys.stdout.buffer.write(field + b"\0")


if __name__ == "__main__":
    main()
