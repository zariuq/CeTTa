"""Check the authored CIC maximum helper and its binder-capture regression."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
from unittest.mock import patch

import build_curriculum_ledger as ledger


def numeral(n: int) -> str:
    result = "(Con z)"
    for _ in range(n):
        result = f"(App (Con s) {result})"
    return result


def check(binary: Path, evidence: Path) -> int:
    evidence.mkdir(parents=True, exist_ok=True)
    sources = sorted((ledger.CURRICULUM / "DeduktiLambdapi").glob("cicdebug*.metta"))
    checked = 0
    for source in sources:
        forms = [form for form in ledger.recover_forms(source.read_text())
                 if form.startswith("(= (cic-nat-max ")]
        if not forms:
            continue
        assert len(forms) == 1, source
        definition = forms[0]
        controls = [f"!(assertEqual (cic-nat-max {numeral(i)} {numeral(j)}) {numeral(max(i, j))})"
                    for i in range(5) for j in range(5)]
        controls += ["!(assertEqual (cic-nat-max (Con x) (Con z)) (Con x))",
                     "!(assertEqual (cic-nat-max (Con z) (Con x)) (Con x))",
                     "!(assertEqual (cic-nat-max (Con x) (Con y)) (App (App (Con m) (Con x)) (Con y)))"]
        for dialect in ("he", "prime"):
            text = definition + "\n" + "\n".join(controls) + "\n"
            if dialect == "prime":
                text = ledger.prime_port(text)
            path = evidence / f"{source.stem}-{dialect}.metta"
            path.write_text(text)
            result = subprocess.run([str(binary), "--lang", dialect, str(path)],
                                    cwd=ledger.ROOT, text=True, capture_output=True)
            (path.with_suffix(".stdout")).write_text(result.stdout)
            (path.with_suffix(".stderr")).write_text(result.stderr)
            assert result.returncode == 0 and not result.stderr, (path, result.stderr)
            assert result.stdout.splitlines() == ["[()]"] * len(controls), (path, result.stdout)
            checked += len(controls)
        # Reintroduce the precise old capture: the whole nonzero argument and
        # its predecessor become the same variable. Its positive claim must fail.
        assert "($jNonZero (case $i" in definition, source
        mutation = definition.replace("($jNonZero (case $i", "($j0 (case $i", 1)
        path = evidence / f"{source.stem}-capture-mutant.metta"
        path.write_text(ledger.prime_port(mutation + "\n" + controls[6] + "\n"))
        result = subprocess.run([str(binary), "--lang", "prime", str(path)],
                                cwd=ledger.ROOT, text=True, capture_output=True)
        assert result.returncode == 0 and not result.stderr, (path, result.stderr)
        assert result.stdout.startswith("[(Error (assertEqual "), (path, result.stdout)
        assert "Got: [(App (App (Con m)" in result.stdout, (path, result.stdout)
    assert checked, "no CIC maximum definitions found"
    print(f"PASS CIC maximum: {checked} independent value checks; {checked // 56} capture mutants rejected")
    commands = 0
    for source in sources:
        wanted = ["[()]"] * len(ledger.top_level_bangs(source.read_text()))
        with patch.dict(os.environ, {"TC_SCRATCH": str(evidence / "full-source")}):
            prime_source = ledger.port_guest_asserts(source)
        for dialect, path in (("he", source), ("prime", prime_source)):
            result = subprocess.run([str(binary), "--lang", dialect, str(path)],
                                    cwd=ledger.ROOT, text=True, capture_output=True)
            stem = evidence / f"full-{source.stem}-{dialect}"
            Path(str(stem) + ".stdout").write_text(result.stdout)
            Path(str(stem) + ".stderr").write_text(result.stderr)
            assert result.returncode == 0 and not result.stderr, (source, dialect, result.stderr)
            assert result.stdout.splitlines() == wanted, (source, dialect, result.stdout)
            commands += len(wanted)
    print(f"PASS CIC complete sources: {commands} commands across {len(sources)} files in both dialects")
    return checked


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    check(args.binary.resolve(), args.evidence.resolve())
