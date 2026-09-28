#!/usr/bin/env python3
"""Check independently versioned HE-prime and Prime lane contracts."""

from __future__ import annotations

import csv
import difflib
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tests/prime/crossdialect/cases.tsv"


def run(binary: Path, *args: str) -> str:
    completed = subprocess.run(
        [str(binary), *args],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"exit {completed.returncode}: {completed.stdout[-2000:].strip()}"
        )
    return completed.stdout.rstrip("\n")


def expected(path: str) -> str:
    return (ROOT / path).read_text().rstrip("\n")


def show_difference(lane: str, golden: str, actual: str) -> None:
    print(f"  {lane} output differs from its golden")
    lines = list(difflib.unified_diff(
        golden.splitlines(), actual.splitlines(),
        fromfile="expected", tofile="actual", n=1,
    ))
    for line in lines[:16]:
        print("  " + line[:500])
    if len(lines) > 16:
        print("  ...")


def main() -> int:
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "cetta"
    if not binary.is_file():
        print(f"FAIL: missing CeTTa binary {binary}", file=sys.stderr)
        return 2

    failures = 0
    passed = 0
    identity_boundary_seen = False
    with CASES.open(newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))

    for row in rows:
        source = row["source"]
        # Native declarations retain their binders and formed universes. An HE
        # annotation/scheme is not implicitly a declaration in that calculus.
        prime_source = row.get("prime_source") or source
        he_expected_path = row["he_expected"]
        prime_expected_path = row["prime_expected"]
        if he_expected_path == prime_expected_path:
            print(f"FAIL: {row['id']}: lane contracts share one golden path")
            failures += 1
            continue

        try:
            he = run(binary, "--profile", "he-prime", "--lang", "he", source)
            prime = run(binary, "--lang", "prime", prime_source)
            he_golden = expected(he_expected_path)
            prime_golden = expected(prime_expected_path)
        except (OSError, RuntimeError) as error:
            print(f"FAIL: {row['id']}: {error}")
            failures += 1
            continue

        ok = he == he_golden and prime == prime_golden
        if row["id"] == "language-identity":
            identity_boundary_seen = True
            ok = ok and he != prime

        if ok:
            print(f"PASS: {row['id']} (independent lane contracts)")
            passed += 1
        else:
            print(f"FAIL: {row['id']} (independent lane contracts)")
            if he != he_golden:
                show_difference("HE-prime", he_golden, he)
            if prime != prime_golden:
                show_difference("Prime", prime_golden, prime)
            if row["id"] == "language-identity" and he == prime:
                print("  Prime-only judgments collapsed into the HE-prime lane")
            failures += 1

    if not identity_boundary_seen:
        print("FAIL: missing executable Prime language-identity boundary")
        failures += 1

    # GNU make executes recursive-marked and '+' recipes even under -n.
    # A target-specific no-op shell also covers prerequisite recipes, while
    # leaving parse-time build configuration discovery unchanged. This checks
    # the expanded prerequisite recipes; nested submakes are not executed.
    make_result = subprocess.run(
        ["make", "-n", "--eval=test-prime: SHELL := /bin/true", "test-prime"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if make_result.returncode != 0:
        print("FAIL: could not inspect normative test-prime target")
        failures += 1
    elif "--profile he-prime" in make_result.stdout:
        print("FAIL: normative test-prime target invokes the HE-prime profile")
        failures += 1
    else:
        print("PASS: expanded test-prime prerequisite recipes do not select HE-prime")
        passed += 1

    print(f"Prime/HE-prime ownership gate: {passed} passed, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
