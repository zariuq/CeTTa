#!/usr/bin/env python3
"""Compare MatchDecision backends against the linear lane oracles."""

from __future__ import annotations

import argparse
import csv
import difflib
import os
from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/test_manifest.tsv"
@dataclass(frozen=True)
class Case:
    lane: str
    name: str
    arguments: tuple[str, ...]
    expected: Path


def expected_path(source: Path) -> Path:
    return source.with_suffix(".expected")


def manifest_cases() -> list[Case]:
    cases: list[Case] = []
    with MANIFEST.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            source = ROOT / row["path"]
            expected = expected_path(source)
            common = (
                row["build"] in {"main", "any"}
                and row["space_engine"] == "native"
                and row["expect"] == "golden"
                and expected.is_file()
            )
            if not common:
                continue
            if (row["lang"] == "he"
                    and row["profile"] == "extended"
                    and row["lane"] == "test"):
                cases.append(Case(
                    "he", row["path"],
                    ("--lang", "he", "--profile", "extended",
                     row["path"]), expected,
                ))
            elif (row["lang"] == "prime"
                  and row["lane"] == "test-prime"):
                cases.append(Case(
                    "prime", row["path"],
                    ("--lang", "prime", row["path"]), expected,
                ))
    return cases


def nil_petta_cases() -> list[Case]:
    base = Path(
        "experiments/gslt2parse_foundation/migration_fixtures/nil_chaining"
    )
    names = (
        "nil_hilbert_obfc_jarr_petta_v1",
        "nil_sumo_john_carry_flower_petta_v1",
        "nil_typed_synthesis_petta_v1",
    )
    return [
        Case(
            "petta", f"{base}/{name}.metta",
            ("--lang", "petta", "--profile", "extended",
             f"{base}/{name}.metta"),
            ROOT / base / f"{name}.expected",
        )
        for name in names
    ]


def prime_frontier_cases() -> list[Case]:
    base = Path("examples/prime/rewrite_frontier_tutorial")
    cases = [
        Case(
            "prime", f"{base}/{name}.metta",
            ("--lang", "prime", f"{base}/{name}.metta"),
            ROOT / base / f"{name}.expected",
        )
        for name in ("01_directional_rules", "02_rule_occurrences")
    ]
    for frontier in ("monolithic", "candidate-local", "demand-cohort"):
        for name in ("03_disjoint_supports", "04_overlapping_supports"):
            cases.append(Case(
                "prime", f"{base}/{name}.metta:{frontier}",
                ("--lang", "prime", "--prime-rewrite-frontier", frontier,
                 f"{base}/{name}.metta"),
                ROOT / base / f"{name}.{frontier}.expected",
            ))
    return cases


def mode_environment(mode: str) -> dict[str, str]:
    environment = os.environ.copy()
    environment.update({
        "CETTA_HE_MATCH_DECISION": mode,
        "CETTA_PETTA_MATCH_DECISION": mode,
        "CETTA_PRIME_MATCH_DECISION": mode,
    })
    return environment


def normalized_output(data: str) -> str:
    return data.rstrip("\n")


def render_diff(expected: str, actual: str) -> str:
    return "\n".join(list(difflib.unified_diff(
        expected.splitlines(), actual.splitlines(),
        fromfile="oracle", tofile="candidate", lineterm="",
    ))[:30])


def run_case(binary: Path, case: Case, mode: str,
             timeout: float) -> tuple[int, str]:
    completed = subprocess.run(
        (str(binary), *case.arguments), cwd=ROOT,
        env=mode_environment(mode), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=timeout, check=False,
    )
    return completed.returncode, normalized_output(completed.stdout)


def run_receipt_oracle(binary: Path, mode: str,
                       timeout: float) -> tuple[int, str]:
    environment = mode_environment(mode)
    environment["CETTA_BIN"] = str(binary)
    completed = subprocess.run(
        (sys.executable,
         "tests/prime/run_need_equation_call_tournament.py"),
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=timeout, check=False,
    )
    return completed.returncode, normalized_output(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--lane", choices=("all", "he", "petta", "prime"),
                        default="all")
    arguments = parser.parse_args()
    binary = arguments.cetta.resolve()
    if not binary.is_file():
        parser.error(f"CeTTa binary not found: {binary}")

    cases = manifest_cases() + nil_petta_cases() + prime_frontier_cases()
    if arguments.lane != "all":
        cases = [case for case in cases if case.lane == arguments.lane]
    seen: set[tuple[str, ...]] = set()
    cases = [case for case in cases
             if not (case.arguments in seen or seen.add(case.arguments))]

    passed = 0
    failures: list[str] = []
    lane_counts: dict[str, int] = {"he": 0, "petta": 0, "prime": 0}
    for case in cases:
        lane_counts[case.lane] += 1
        expected = normalized_output(case.expected.read_text(encoding="utf-8"))
        try:
            oracle_code, oracle = run_case(
                binary, case, "linear", arguments.timeout)
        except subprocess.TimeoutExpired:
            failures.append(f"{case.name}: linear timed out")
            continue
        if oracle_code != 0 or oracle != expected:
            detail = render_diff(expected, oracle)
            failures.append(
                f"{case.name}: linear oracle disagrees with golden"
                f" (exit {oracle_code})\n{detail}"
            )
            continue
        case_ok = True
        for mode in ("deep",):
            try:
                code, output = run_case(binary, case, mode, arguments.timeout)
            except subprocess.TimeoutExpired:
                failures.append(f"{case.name}: {mode} timed out")
                case_ok = False
                break
            if code != oracle_code or output != oracle:
                detail = render_diff(oracle, output)
                failures.append(
                    f"{case.name}: {mode} diverged from linear"
                    f" (exit {code}, oracle exit {oracle_code})\n{detail}"
                )
                case_ok = False
                break
        if case_ok:
            passed += 1

    receipt_passed = 0
    if arguments.lane in {"all", "prime"}:
        try:
            receipt_code, receipt_oracle = run_receipt_oracle(
                binary, "linear", arguments.timeout)
            if receipt_code != 0:
                failures.append(
                    "Prime receipt oracle failed in linear mode\n"
                    + "\n".join(receipt_oracle.splitlines()[-30:])
                )
            else:
                for mode in ("deep",):
                    code, output = run_receipt_oracle(
                        binary, mode, arguments.timeout)
                    if code != receipt_code or output != receipt_oracle:
                        failures.append(
                            f"Prime receipt oracle: {mode} diverged from linear\n"
                            + render_diff(receipt_oracle, output)
                        )
                        break
                else:
                    receipt_passed = 1
        except subprocess.TimeoutExpired:
            failures.append("Prime receipt oracle timed out")

    print(
        "(MatchDecisionLaneOracleSummary "
        f"cases {len(cases)} passed {passed} failed {len(cases) - passed} "
        f"he {lane_counts['he']} petta {lane_counts['petta']} "
        f"prime {lane_counts['prime']} receipts {receipt_passed})"
    )
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
