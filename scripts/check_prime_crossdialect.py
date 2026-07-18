#!/usr/bin/env python3
"""Check independently versioned HE-prime and Prime lane contracts."""

from __future__ import annotations

import csv
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
    return completed.stdout.rstrip("\n")


def expected(path: str) -> str:
    return (ROOT / path).read_text().rstrip("\n")


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
        he = run(binary, "--profile", "he-prime", "--lang", "he", source)
        he_expected_path = row["he_expected"]
        prime_expected_path = row["prime_expected"]
        if he_expected_path == prime_expected_path:
            print(f"FAIL: {row['id']}: lane contracts share one golden path")
            failures += 1
            continue

        he_golden = expected(he_expected_path)
        prime = run(binary, "--lang", "prime", source)
        prime_golden = expected(prime_expected_path)

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
                print("  HE-prime output differs from its golden")
            if prime != prime_golden:
                print("  Prime output differs from its golden")
            if row["id"] == "language-identity" and he == prime:
                print("  Prime-only judgments collapsed into the HE-prime lane")
            failures += 1

    if not identity_boundary_seen:
        print("FAIL: missing executable Prime language-identity boundary")
        failures += 1

    make_plan = subprocess.run(
        ["make", "-n", "test-prime"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    ).stdout
    if "--profile he-prime" in make_plan:
        print("FAIL: normative test-prime target invokes the HE-prime profile")
        failures += 1
    else:
        print("PASS: normative test-prime target is --lang prime only")
        passed += 1

    print(f"Prime/HE-prime ownership gate: {passed} passed, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
