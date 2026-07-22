#!/usr/bin/env python3
"""Differential semantic gate for positive GSLT lookahead."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
EXPECTED_MATRIX_DIGEST = (
    "d8761c733309d69ba998b74183acc57e1b0f71025c9f15e04e2e9876b188fbb1"
)


CASES = {
    "distinct_values": (
        "(parse (peek (alt (eps look-left) (eps look-right))) "
        "(cons (cp 97) nil) ?value ?rest)",
        (
            "(parse (peek (alt (eps look-left) (eps look-right))) "
            "(cons (cp 97) nil) look-left (cons (cp 97) nil))",
            "(parse (peek (alt (eps look-left) (eps look-right))) "
            "(cons (cp 97) nil) look-right (cons (cp 97) nil))",
        ),
    ),
    "distinct_extents": (
        "(parse (peek (alt (char (cp 97)) "
        "(seq (char (cp 97)) (char (cp 98))))) "
        "(cons (cp 97) (cons (cp 98) nil)) ?value ?rest)",
        (
            "(parse (peek (alt (char (cp 97)) "
            "(seq (char (cp 97)) (char (cp 98))))) "
            "(cons (cp 97) (cons (cp 98) nil)) (cp 97) "
            "(cons (cp 97) (cons (cp 98) nil)))",
            "(parse (peek (alt (char (cp 97)) "
            "(seq (char (cp 97)) (char (cp 98))))) "
            "(cons (cp 97) (cons (cp 98) nil)) "
            "(pair (cp 97) (cp 98)) "
            "(cons (cp 97) (cons (cp 98) nil)))",
        ),
    ),
    "duplicate_value": (
        "(parse (peek (alt (eps same) (eps same))) "
        "(cons (cp 97) nil) ?value ?rest)",
        (
            "(parse (peek (alt (eps same) (eps same))) "
            "(cons (cp 97) nil) same (cons (cp 97) nil))",
        ),
    ),
    "nullable_nonempty": (
        "(parse (peek (eps nullable)) (cons (cp 97) nil) ?value ?rest)",
        (
            "(parse (peek (eps nullable)) (cons (cp 97) nil) nullable "
            "(cons (cp 97) nil))",
        ),
    ),
    "eof_empty": (
        "(parse (peek eof) nil ?value ?rest)",
        ("(parse (peek eof) nil eof nil)",),
    ),
    "eof_nonempty": (
        "(parse (peek eof) (cons (cp 955) nil) ?value ?rest)",
        (),
    ),
    "mismatch": (
        "(parse (peek (char (cp 98))) "
        "(cons (cp 97) nil) ?value ?rest)",
        (),
    ),
    "unicode": (
        "(parse (peek (char (cp 955))) "
        "(cons (cp 955) nil) ?value ?rest)",
        (
            "(parse (peek (char (cp 955))) (cons (cp 955) nil) "
            "(cp 955) (cons (cp 955) nil))",
        ),
    ),
}


class GateFailure(RuntimeError):
    pass


def run_json(arguments: list[str], cwd: Path | None = None) -> dict:
    completed = subprocess.run(
        arguments,
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        timeout=60,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"command exited {completed.returncode}: {completed.stderr.strip()}"
        )
    try:
        value = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            f"non-JSON lookahead output: {completed.stdout!r}"
        ) from error
    if not isinstance(value, dict):
        raise GateFailure("lookahead output is not a JSON object")
    return value


def native_rows(binary: Path) -> dict[str, tuple[str, ...]]:
    core = PRESENTATIONS / "core" / "syntax_core_v1.metta"
    lookahead = PRESENTATIONS / "shared" / "lookahead_core_v1.metta"
    rows: dict[str, tuple[str, ...]] = {}
    for label, (query, expected) in CASES.items():
        result = run_json(
            [
                str(binary),
                str(core),
                str(lookahead),
                "--query-text",
                query,
                "--certs",
                "--timeout",
                "30",
            ]
        )
        terms = tuple(result.get("terms", ()))
        if terms != expected:
            raise GateFailure(f"native {label} answer set changed: {terms!r}")
        if result.get("answers") != len(expected):
            raise GateFailure(f"native {label} answer count is inconsistent")
        expected_outcome = (
            "NoAnswer" if not expected else "Unique" if len(expected) == 1 else "Ambiguous"
        )
        if result.get("outcome") != expected_outcome:
            raise GateFailure(f"native {label} outcome changed")
        certs = result.get("certs")
        if not isinstance(certs, list) or len(certs) != len(expected):
            raise GateFailure(f"native {label} certificate coverage changed")
        rows[label] = terms
    return rows


def petta_rows(petta_root: Path) -> tuple[dict[str, tuple[str, ...]], int]:
    foundation = petta_root / "experiments" / "gslt2parse_foundation"
    script = foundation / "test_lookahead_semantics_v1.pl"
    result = run_json(
        [
            "swipl",
            "-q",
            "-f",
            str(script),
            "--",
            str(PRESENTATIONS),
        ],
        cwd=foundation,
    )
    if result.get("protocol") != "lookahead-semantics-v1":
        raise GateFailure("PeTTa lookahead protocol changed")
    raw_rows = result.get("rows")
    if not isinstance(raw_rows, list) or len(raw_rows) != len(CASES):
        raise GateFailure("PeTTa lookahead row inventory changed")
    rows: dict[str, tuple[str, ...]] = {}
    duplicate_derivations = 0
    for row in raw_rows:
        if not isinstance(row, dict) or not isinstance(row.get("label"), str):
            raise GateFailure("malformed PeTTa lookahead row")
        label = row["label"]
        if label not in CASES or label in rows:
            raise GateFailure(f"unexpected PeTTa lookahead label: {label!r}")
        terms = tuple(row.get("terms", ()))
        expected = CASES[label][1]
        if terms != expected or row.get("semantic_count") != len(expected):
            raise GateFailure(f"PeTTa {label} answer set changed: {terms!r}")
        derivation_count = row.get("derivation_count")
        if not isinstance(derivation_count, int) or derivation_count < len(terms):
            raise GateFailure(f"PeTTa {label} derivation receipt is invalid")
        if label == "duplicate_value":
            duplicate_derivations = derivation_count
        rows[label] = terms
    if duplicate_derivations != 2:
        raise GateFailure("duplicate lookahead proofs no longer collapse to one answer")
    return rows, duplicate_derivations


def matrix_digest(rows: dict[str, tuple[str, ...]]) -> str:
    payload_rows = []
    for label in sorted(rows):
        term_digest = hashlib.sha256("\n".join(rows[label]).encode()).hexdigest()
        payload_rows.append(f"{label}/{len(rows[label])}/{term_digest}")
    return hashlib.sha256("\n".join(payload_rows).encode()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    c_rows = native_rows(arguments.binary.resolve())
    prolog_rows, duplicate_derivations = petta_rows(arguments.petta_root.resolve())
    if c_rows != prolog_rows:
        raise GateFailure("C and PeTTa lookahead answer sets differ")
    digest = matrix_digest(c_rows)
    if digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"lookahead matrix digest changed: {digest}")
    print(
        f"(LookaheadSemanticsV1MatrixSummary {len(CASES)} "
        f"{len(c_rows)} {len(prolog_rows)} {duplicate_derivations} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
