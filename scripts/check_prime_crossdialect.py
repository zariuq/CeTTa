#!/usr/bin/env python3
"""Check named HE-prime/Prime agreements and deliberate divergences."""

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
    with CASES.open(newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))

    for row in rows:
        source = row["source"]
        he = run(binary, "--profile", "he-prime", "--lang", "he", source)
        he_golden = expected(row["he_expected"])
        prime = run(binary, "--lang", "prime", source)
        relation = row["prime_relation"]

        ok = he == he_golden
        if relation == "same":
            ok = ok and prime == he_golden
        elif relation == "typed-equality-divergence":
            ok = (
                ok
                and prime != he_golden
                and "(BadArgType 2 Number String)" in prime
            )
        else:
            print(f"FAIL: {row['id']}: unknown relation {relation}")
            failures += 1
            continue

        if ok:
            print(f"PASS: {row['id']} ({relation})")
            passed += 1
        else:
            print(f"FAIL: {row['id']} ({relation})")
            if he != he_golden:
                print("  HE-prime output differs from its golden")
            if relation == "same" and prime != he_golden:
                print("  Prime output differs from the shared golden")
            if relation == "typed-equality-divergence":
                print("  Prime did not exhibit the typed equality boundary")
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

    print(f"Prime cross-dialect gate: {passed} passed, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
