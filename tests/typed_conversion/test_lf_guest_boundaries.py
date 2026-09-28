"""Check legacy LF normalization and malformed-proof boundaries in both dialects."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
from unittest.mock import patch

import build_curriculum_ledger as ledger


def check(binary: Path, evidence: Path) -> None:
    evidence.mkdir(parents=True, exist_ok=True)
    kernel = ledger.CURRICULUM.parent / "kernel/kernel_signature_lf_v0.metta"
    source = ledger.CURRICULUM / "Notation/08_hol_core_concrete_syntax.metta"
    controls = """
!(nf SNil (App (Con f) (App (Lam (Con A) (Var 0)) (Con z))))
!(nf SNil (Pi (App (Lam (Con A) (Var 0)) (Con B)) (App (Lam (Con A) (Var 0)) (Var 0))))
!(nf SNil (Lam (App (Lam (Con A) (Var 0)) (Con B)) (App (Lam (Con A) (Var 0)) (Var 0))))
!(assertEqual (nf SNil (App (Lam (Srt type) (Var 0)) (Con x))) (Con x))
!(assertEqual (infer SNil CtxNil (Bad malformed-proof)) (Bad malformed-proof))
!(assertEqual (check-result SNil CtxNil (Bad malformed-proof) (Srt type)) False)
!(assertEqual (kernel-check (good-sig) (Lam (Con nat) (Bad malformed-proof)) (Pi (Con nat) (Con nat))) (Err check-failed))
!(assertEqual (kernel-check (good-sig) (Lam (Con nat) (Var 0)) (Pi (Con nat) (Con nat))) (Ok (CheckedPrf ANil (Lam (Con nat) (Var 0)) (Pi (Con nat) (Con nat)))))
"""
    wanted = ["[()]", "[(App (Con f) (Con z))]", "[(Pi (Con B) (Var 0))]",
              "[(Lam (Con B) (Var 0))]"] + ["[()]"] * 5

    def run(path: Path, dialect: str, label: str) -> list[str]:
        result = subprocess.run([str(binary), "--lang", dialect, "--eval-hashcons", str(path)],
                                cwd=ledger.ROOT, text=True, capture_output=True)
        (evidence / (label + ".stdout")).write_text(result.stdout)
        (evidence / (label + ".stderr")).write_text(result.stderr)
        assert result.returncode == 0 and not result.stderr, (label, result.returncode, result.stderr)
        return result.stdout.splitlines()

    path = evidence / "boundaries.metta"
    path.write_text(f"!(import! &self {kernel})\n" + controls)
    for dialect in ("he", "prime"):
        assert run(path, dialect, "boundaries-" + dialect) == wanted
    with patch.dict(os.environ, {"TC_SCRATCH": str(evidence)}):
        prime_source = ledger.port_guest_asserts(source)
    source_wanted = ["[()]"] * len(ledger.top_level_bangs(source.read_text()))
    assert len(source_wanted) == 39, "the authored positive/negative assertion contract changed"
    for dialect, path in (("he", source), ("prime", prime_source)):
        assert run(path, dialect, "authored-source-" + dialect) == source_wanted

    text = kernel.read_text().replace("!(import! &self kernel_binding_waist_v1)",
                                    f"!(import! &self {kernel.parent / 'kernel_binding_waist_v1.metta'})")
    app_start = text.index("(= (nf $sig (App ")
    app_end = text.index("(= (nf-app ", app_start)
    mutants = {
        "suspended-normalization": text[:app_start] +
            "(= (nf $sig (App $f $a)) (nf-app $sig (nf $sig $f) (nf $sig $a)))\n" + text[app_end:],
        "malformed-acceptance": text.replace("(= (infer $sig $ctx (Bad $e)) (Bad $e))",
                                           "(= (infer $sig $ctx (Bad $e)) (Srt type))", 1),
    }
    for label, mutation in mutants.items():
        assert mutation != text
        mutant_kernel = evidence / (label + "-kernel.metta")
        mutant_kernel.write_text(mutation)
        path = evidence / (label + ".metta")
        path.write_text(f"!(import! &self {mutant_kernel})\n" + controls)
        actual = run(path, "prime", label)
        assert actual != wanted, (label, "mutation was not detected")
        if label == "suspended-normalization":
            assert actual[1] != wanted[1] and actual[-1] == "[()]", actual
        else:
            assert any(line.startswith("[(Error (assertEqual ") for line in actual), actual

    lower = prime_source.read_text()
    old = "(= (hol-lowerProof-in $tbl (HBad $e)) (Bad $e))"
    assert old in lower
    path = evidence / "malformed-lowering.metta"
    path.write_text(lower.replace(old, old.replace("(Bad $e)", "(Err $e)"), 1))
    actual = run(path, "prime", "malformed-lowering")
    assert actual != source_wanted and any(line.startswith("[(Error (assertEqual ") for line in actual), actual
    print("PASS LF guest boundaries: 96 complete outputs in both dialects; 3 precise mutants detected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    check(args.binary.resolve(), args.evidence.resolve())
