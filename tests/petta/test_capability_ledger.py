#!/usr/bin/env python3

import argparse
import csv
import os
from pathlib import Path
import subprocess
import sys


EXPECTED_CAPABILITIES = {
    "dynamic-definition dispatch",
    "committed choice",
    "equation-head list patterns",
    "inverse relational calls",
    "type-directed demand",
    "higher-order specialization",
    "package-library descriptors",
    "translator predicates",
    "foreign-language modules",
    "tabling",
    "transactional effects",
    "world-refined space control",
    "recursive relational search",
    "intensional provenance",
}

EXPECTED_FIELDS = [
    "capability",
    "witness",
    "positive_expected",
    "negative_test",
    "negative_expected",
    "reference_cases",
    "obligation",
    "status",
]


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def repository_path(root: Path, value: str, field: str) -> Path:
    if not value:
        fail(f"empty {field}")
    candidate = (root / value).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        fail(f"{field} escapes the repository: {value}")
    if not candidate.is_file():
        fail(f"missing {field}: {value}")
    return candidate


def expected_output(path: Path) -> str:
    return path.read_text(encoding="utf-8").rstrip("\n")


def run_contract(
    binary: Path,
    root: Path,
    test_path: Path,
    expected_path: Path,
    capability: str,
    polarity: str,
) -> None:
    environment = os.environ.copy()
    environment["CETTA_PETTA_SEARCH_MACHINE"] = "1"
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    try:
        completed = subprocess.run(
            [str(binary), "--lang", "petta", str(test_path)],
            cwd=root,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
            check=False,
        )
    except subprocess.TimeoutExpired:
        fail(f"{capability} {polarity} contract timed out")
    if completed.returncode != 0:
        fail(
            f"{capability} {polarity} contract exited "
            f"{completed.returncode}:\n{completed.stdout}"
        )
    actual = completed.stdout.rstrip("\n")
    expected = expected_output(expected_path)
    if actual != expected:
        fail(
            f"{capability} {polarity} contract differs\n"
            f"expected:\n{expected}\nactual:\n{actual}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", required=True)
    parser.add_argument(
        "--ledger",
        default="tests/petta/unsupported/capabilities.tsv",
    )
    arguments = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    binary = Path(arguments.cetta)
    if not binary.is_absolute():
        binary = (root / binary).resolve()
    if not binary.is_file():
        fail(f"missing CeTTa binary: {binary}")

    ledger = repository_path(root, arguments.ledger, "ledger")
    with ledger.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != EXPECTED_FIELDS:
            fail(
                "capability ledger header differs: "
                f"{reader.fieldnames!r}"
            )
        rows = list(reader)

    capabilities = [row["capability"] for row in rows]
    if len(rows) != len(EXPECTED_CAPABILITIES):
        fail(
            f"capability ledger has {len(rows)} rows, "
            f"expected {len(EXPECTED_CAPABILITIES)}"
        )
    if len(set(capabilities)) != len(capabilities):
        fail("capability ledger contains duplicate capability rows")
    if set(capabilities) != EXPECTED_CAPABILITIES:
        missing = sorted(EXPECTED_CAPABILITIES - set(capabilities))
        extra = sorted(set(capabilities) - EXPECTED_CAPABILITIES)
        fail(f"capability ledger mismatch; missing={missing}, extra={extra}")

    for row in rows:
        capability = row["capability"]
        if row["status"] != "closed":
            fail(f"{capability} remains {row['status']!r}")
        if not row["reference_cases"].strip():
            fail(f"{capability} has no oracle reference cases")
        if not row["obligation"].strip():
            fail(f"{capability} has no semantic obligation")

        witness_name = row["witness"]
        if Path(witness_name).name != witness_name:
            fail(f"{capability} witness must name the unsupported fixture")
        witness = repository_path(
            root,
            f"tests/petta/unsupported/{witness_name}",
            f"{capability} witness",
        )
        positive_expected = repository_path(
            root,
            row["positive_expected"],
            f"{capability} positive expected output",
        )
        negative_test = repository_path(
            root,
            row["negative_test"],
            f"{capability} negative test",
        )
        negative_expected = repository_path(
            root,
            row["negative_expected"],
            f"{capability} negative expected output",
        )

        run_contract(
            binary,
            root,
            witness,
            positive_expected,
            capability,
            "positive",
        )
        run_contract(
            binary,
            root,
            negative_test,
            negative_expected,
            capability,
            "negative",
        )

    print(
        "PASS: all 14 PeTTa capability clusters have "
        "positive and negative executable contracts"
    )


if __name__ == "__main__":
    main()
