"""Preserve complete ML source witnesses and assertion-bearing guest imports."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from unittest.mock import patch

import build_curriculum_ledger as ledger

WRAPPERS = (
    "DeduktiLambdapi/02_cic_guest_stage1.metta",
    "DeduktiLambdapi/03_cic_guest_stage2.metta",
    "DeduktiLambdapi/04_cic_guest_stage3.metta",
)


def check(binary: Path, evidence: Path) -> None:
    evidence.mkdir(parents=True, exist_ok=True)
    source = ledger.CURRICULUM / "HOL/HOLLightProofClosure.ml"
    items = {name: statement for name, statement, _ in ledger.extract_items(source)}
    expected = "let self_imp = DISCH p (ASSUME p);;"
    assert items["self_imp"] == expected and expected in source.read_text()
    dependency = ledger.construct_feature(expected)
    assert ledger.quote_claims(expected, dependency, "HOL/HOLLightProofClosure.ml")
    assert not ledger.quote_claims("let self_imp =", "let self_imp = is outside the kernel")
    print("PASS curriculum complete ML source extraction")
    receipts = {}
    with patch.object(ledger, "BIN", binary), patch.dict(os.environ, {"TC_SCRATCH": str(evidence)}):
        for rel in WRAPPERS:
            path = ledger.CURRICULUM / rel
            rec = ledger.run_guest_file(path)
            assert rec["ok"] and rec["lines"] == ["[()]"], (rel, rec)
            assert ledger.guest_printed(rec, path.stem, "exercise") == "[()]"
            generated = ledger.port_guest_asserts(path)
            imports = ledger.recover_forms(generated.read_text())
            target = Path(imports[0][len("!(import! &self "):-1])
            original = path.parent / ledger.recover_forms(path.read_text())[0][len("!(import! &self "):-1]
            authored = ledger._command_spans(original.read_text(), ("!(assertEqual", "!(test"))
            retained = ledger._command_spans(target.read_text(), ("!(assertEqual", "!(test"))
            assert authored and len(authored) == len(retained), (rel, len(authored), len(retained))
            receipts[rel] = {"source": str(path), "source_sha256": ledger.sha256_file(path),
                             "outputs": rec["lines"], "imported_assertions": len(retained)}
            # A bad imported assertion must propagate through the wrapper.
            text = target.read_text()
            start, end = retained[0]
            target.write_text(text[:start] + "!(assertEqual True False)" + text[end:])
            code, lines, err = ledger.cetta_lines(generated)
            assert code == 0 and not err and len(lines) == 1 and "Error" in lines[0], (rel, code, lines, err)
            assert ledger.trace_failure(code, lines, err, targets=0)
            receipts[rel]["mutant_outputs"] = lines
            print("PASS curriculum import wrapper and assertion mutant " + rel)
    (evidence / "receipt.json").write_text(json.dumps(receipts, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    check(args.binary.resolve(), args.evidence.resolve())
