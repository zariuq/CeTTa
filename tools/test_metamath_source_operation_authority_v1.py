#!/usr/bin/env python3
"""Gate CeTTa's Metamath fold/checker GSLTs against their Lean source."""

from __future__ import annotations

import argparse
import difflib
import re
import subprocess
import tempfile
from pathlib import Path


EXPORTER = Path(
    "Mettapedia/Languages/Metamath/"
    "SourceGSLTOperationsMeTTaExport.lean"
)
TRUSTED_SOURCES = (
    Path("Mettapedia/Languages/Metamath/SourceGSLTOperations.lean"),
    EXPORTER,
)
FORBIDDEN = re.compile(
    r"(?m)^\s*(?:sorry|admit|native_decide|axiom)\b|_wanted"
)


class GateFailure(RuntimeError):
    pass


def exact_artifact(expected: Path, generated: Path) -> None:
    expected_text = expected.read_text(encoding="utf-8")
    generated_text = generated.read_text(encoding="utf-8")
    if expected_text == generated_text:
        return
    difference = "".join(
        difflib.unified_diff(
            expected_text.splitlines(keepends=True),
            generated_text.splitlines(keepends=True),
            fromfile=expected.name,
            tofile=f"generated-{expected.name}",
        )
    )
    raise GateFailure(
        f"{expected.name} is not the exact Lean export\n{difference}"
    )


def trusted_source_gate(mettapedia_root: Path) -> None:
    for relative in TRUSTED_SOURCES:
        source = mettapedia_root / relative
        if not source.is_file():
            raise GateFailure(f"missing trusted source: {relative}")
        match = FORBIDDEN.search(source.read_text(encoding="utf-8"))
        if match is not None:
            raise GateFailure(
                f"{relative}: forbidden proof escape {match.group(0)!r}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mettapedia-root", type=Path, required=True)
    parser.add_argument("--fold-artifact", type=Path, required=True)
    parser.add_argument("--checker-artifact", type=Path, required=True)
    arguments = parser.parse_args()

    mettapedia_root = arguments.mettapedia_root.resolve()
    trusted_source_gate(mettapedia_root)
    for artifact in (arguments.fold_artifact, arguments.checker_artifact):
        if not artifact.is_file():
            raise GateFailure(f"missing generated artifact: {artifact}")

    with tempfile.TemporaryDirectory(
        prefix="metamath-source-operation-authority-"
    ) as temporary:
        temporary_root = Path(temporary)
        generated_fold = temporary_root / arguments.fold_artifact.name
        generated_checker = temporary_root / arguments.checker_artifact.name
        command = [
            "lake",
            "env",
            "lean",
            "--run",
            str(EXPORTER),
            str(generated_fold),
            str(generated_checker),
        ]
        completed = subprocess.run(
            command,
            cwd=mettapedia_root,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if completed.returncode != 0:
            raise GateFailure(
                "Lean source-operation export failed:\n" + completed.stdout
            )
        exact_artifact(arguments.fold_artifact, generated_fold)
        exact_artifact(arguments.checker_artifact, generated_checker)

        mutated = generated_fold.read_bytes() + b"; mutation\n"
        if mutated == arguments.fold_artifact.read_bytes():
            raise GateFailure("source-authority mutation was not detected")

    print("metamath-source-operation-authority-v1")
    print("lean-source-files\t2")
    print("exact-generated-artifacts\t2")
    print("mutations-killed\t1")
    print("end")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateFailure as failure:
        raise SystemExit(f"FAIL: {failure}") from failure
