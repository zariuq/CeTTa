"""Replay indexed LF normalization and the complete CIC induction source."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
from unittest.mock import patch

import build_curriculum_ledger as ledger


def check(binary: Path, evidence: Path) -> None:
    evidence.mkdir(parents=True, exist_ok=True)
    source = ledger.CURRICULUM / "DeduktiLambdapi/03_cic_guest_indg_micro.metta"
    kernel = ledger.CURRICULUM.parent / "kernel/kernel_signature_lf_indexed_lib_v0.metta"
    wanted = ["[()]"] * len(ledger.top_level_bangs(source.read_text()))
    assert len(wanted) == 20, "the complete source and its rejection control changed"
    with patch.dict(os.environ, {"TC_SCRATCH": str(evidence)}):
        prime_source = ledger.port_guest_asserts(source)

    def run(path: Path, dialect: str, expected: list[str], label: str) -> None:
        result = subprocess.run([str(binary), "--lang", dialect, str(path)],
                                cwd=ledger.ROOT, text=True, capture_output=True)
        (evidence / (label + ".stdout")).write_text(result.stdout)
        (evidence / (label + ".stderr")).write_text(result.stderr)
        assert result.returncode == 0 and not result.stderr, (label, result.returncode, result.stderr)
        assert result.stdout.splitlines() == expected, (label, result.stdout)
        print(f"PASS indexed LF {label}: {len(expected)} commands")

    for dialect, path in (("he", source), ("prime", prime_source)):
        run(path, dialect, wanted, "CIC-complete-" + dialect)

    controls = evidence / "normalization-controls.metta"
    controls.write_text(f"!(import! &self {kernel})\n" + """
!(assertEqual (nf SNil (App (Lam (Con A) (Var 0)) (Con z))) (Con z))
!(assertEqual
  (nf SNil (App (Lam (Con A) (Var 0)) (App (Lam (Con A) (Var 0)) (Con z))))
  (Con z))
!(assertEqual
  (nf SNil (App (Con f) (App (Lam (Con A) (Var 0)) (Con z))))
  (App (Con f) (Con z)))
!(assertEqual (conv SNil (Con a) (Con b)) False)
!(nf SNil (App (Con f) (App (Lam (Con A) (Var 0)) (Con z))))
!(nf SNil (Pi (App (Lam (Con A) (Var 0)) (Con B)) (App (Lam (Con A) (Var 0)) (Var 0))))
!(nf SNil (Lam (App (Lam (Con A) (Var 0)) (Con B)) (App (Lam (Con A) (Var 0)) (Var 0))))
!(nf-args SNil (ArgsCons (App (Lam (Con A) (Var 0)) (Con B)) (ArgsCons (App (Lam (Con A) (Var 0)) (Var 0)) ArgsNil)))
!(nf-cases SNil (Cases (Case c (App (Lam (Con A) (Var 0)) (Con B))) CasesNil))
""")
    normal_forms = [
        "[(App (Con f) (Con z))]",
        "[(Pi (Con B) (Var 0))]",
        "[(Lam (Con B) (Var 0))]",
        "[(ArgsCons (Con B) (ArgsCons (Var 0) ArgsNil))]",
        "[(Cases (Case c (Con B)) CasesNil)]",
    ]
    for dialect in ("he", "prime"):
        run(controls, dialect, ["[()]"] * 5 + normal_forms, "beta-neutral-negative-" + dialect)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    check(args.binary.resolve(), args.evidence.resolve())
