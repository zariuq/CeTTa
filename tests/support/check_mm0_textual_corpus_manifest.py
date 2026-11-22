#!/usr/bin/env python3
"""Manifest controls on two tiny inputs; never loads the upstream corpus."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from mm0_textual_corpus_v1 import checked_cases


def main():
    with tempfile.TemporaryDirectory(prefix="mm0-corpus-", dir=ROOT / "runtime") as directory:
        root = Path(directory)
        good = root / "tests/mm0_mmu/pass/valid λ\nname.mm0"
        proof = root / "tests/mm0_mmu/fail/proof.mm0"
        good.parent.mkdir(parents=True)
        proof.parent.mkdir(parents=True)
        good.write_text("sort s;\n")
        proof.write_text("provable sort p;\n")
        paired = proof.with_suffix(".mmu")
        paired.write_text("a paired proof fixture\n")
        pin = lambda path: dict(path=path.relative_to(root).as_posix(),
                                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        records = dict(schema="mm0-textual-corpus-v1", cases=[
            dict(**pin(good), syntax="accepted", integration="accepted", failure_stage=None),
            dict(**pin(proof), syntax="accepted", integration="rejected", failure_stage="proof", proof=pin(paired))])
        manifest = root / "cases.json"
        manifest.write_text(json.dumps(records))
        assert checked_cases(root, manifest) == records["cases"]
        command = [sys.executable, str(ROOT / "tools/mm0_textual_corpus_v1.py"),
                   "--mm0-root", str(root), "--manifest", str(manifest)]
        result = subprocess.run(command, capture_output=True, check=True)
        assert result.stdout == b"".join(str(path.resolve()).encode() + b"\0accepted\0"
                                          for path in (good, proof))
        print("PASS: proof refusals keep accepted syntax; whitespace and Unicode paths survive the corpus stream")

        def rejected(marker):
            try:
                checked_cases(root, manifest)
            except ValueError as error:
                assert marker in str(error), str(error)
                return
            raise AssertionError(f"missed corpus mutation: {marker}")

        new = good.with_name("new.mm0")
        new.write_text("sort t;\n")
        rejected("unclassified")
        new.unlink()
        text = good.read_bytes()
        good.write_bytes(text + b"term t: s;\n")
        rejected("specification changed")
        good.write_bytes(text)
        text = paired.read_bytes()
        paired.write_bytes(text + b"changed\n")
        rejected("proof fixture changed")
        paired.write_bytes(text)
        bad = copy.deepcopy(records)
        bad["cases"][1]["syntax"] = "rejected"
        manifest.write_text(json.dumps(bad))
        rejected("not a syntax refusal")
        manifest.write_text(json.dumps(records))
        assert checked_cases(root, manifest) == records["cases"]
        print("PASS: unclassified input, changed specification, changed proof and wrong failure stage are refused")
    print("MM0 syntax corpus controls: 2 positive cases and 4 detected mutations")


if __name__ == "__main__":
    main()
