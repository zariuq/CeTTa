#!/usr/bin/env python3
"""Check Prime's typed-to-erased runtime coherence and annotation boundary."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
RUNTIME_CASES = (
    (
        ROOT / "tests/prime/gradual/runtime_typed.metta",
        ROOT / "tests/prime/gradual/runtime.expected",
    ),
    (
        ROOT / "tests/prime/gradual/dependent_vector_typed.metta",
        ROOT / "tests/prime/gradual/dependent_vector.expected",
    ),
    (
        ROOT / "tests/prime/gradual/state_typed.metta",
        ROOT / "tests/prime/gradual/state.expected",
    ),
)
ANNOTATIONS = ROOT / "tests/prime/gradual/annotation_boundary.metta"
ANNOTATIONS_EXPECTED = ROOT / "tests/prime/gradual/annotation_boundary.expected"


def run(binary: Path, *args: str) -> str:
    completed = subprocess.run(
        [str(binary), "--lang", "prime", *args],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return completed.stdout.rstrip("\n")


def erase_fixture_annotations(source: str) -> tuple[str, int]:
    kept: list[str] = []
    erased = 0
    for line_number, line in enumerate(source.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("(: "):
            if stripped.count("(") != stripped.count(")"):
                raise ValueError(
                    f"line {line_number}: gradual fixture annotations must occupy one line"
                )
            erased += 1
            continue
        kept.append(line)
    return "\n".join(kept) + "\n", erased


def main() -> int:
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "cetta"
    if not binary.is_file():
        print(f"FAIL: missing CeTTa binary {binary}", file=sys.stderr)
        return 2

    failures = 0
    for typed_path, expected_path in RUNTIME_CASES:
        try:
            typed_source = typed_path.read_text()
            erased_source, annotation_count = erase_fixture_annotations(typed_source)
        except (OSError, ValueError) as error:
            print(f"FAIL: {typed_path.relative_to(ROOT)}: {error}")
            failures += 1
            continue
        if annotation_count == 0:
            print(
                f"FAIL: {typed_path.relative_to(ROOT)} contains no annotations "
                "to erase"
            )
            failures += 1
            continue
        if any(line.strip().startswith("(: ") for line in erased_source.splitlines()):
            print(
                f"FAIL: annotation erasure left a declaration behind in "
                f"{typed_path.relative_to(ROOT)}"
            )
            failures += 1
            continue

        try:
            runtime_golden = expected_path.read_text().rstrip("\n")
        except OSError as error:
            print(f"FAIL: {expected_path.relative_to(ROOT)}: {error}")
            failures += 1
            continue
        typed = run(binary, str(typed_path.relative_to(ROOT)))
        erased = run(binary, "-e", erased_source)
        runtime_ok = True
        if typed != runtime_golden:
            print(
                f"FAIL: typed Prime runtime for {typed_path.name} differs "
                "from its golden"
            )
            runtime_ok = False
        if erased != runtime_golden:
            print(
                f"FAIL: annotation-erased Prime runtime for {typed_path.name} "
                "differs from its golden"
            )
            runtime_ok = False
        if typed != erased:
            print(
                f"FAIL: typed and annotation-erased Prime runtimes disagree "
                f"for {typed_path.name}"
            )
            runtime_ok = False
        if runtime_ok:
            print(
                f"PASS: {typed_path.name} preserves runtime behavior after "
                "annotation erasure"
            )
        else:
            failures += 1

    annotation_golden = ANNOTATIONS_EXPECTED.read_text().rstrip("\n")
    annotation_output = run(binary, str(ANNOTATIONS.relative_to(ROOT)))
    if annotation_output != annotation_golden:
        print("FAIL: Prime annotation authority boundary differs from its golden")
        failures += 1
    else:
        print(
            "PASS: valid annotations check and a false result annotation is "
            "refuted and rejected"
        )

    total = len(RUNTIME_CASES) + 1
    print(f"Prime internal graduality gate: {total - failures} passed, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
