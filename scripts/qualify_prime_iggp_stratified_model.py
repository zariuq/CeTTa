#!/usr/bin/env python3
"""Differentially qualify the native typed stratified GDL model."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from check_prime_iggp_presentations import (  # noqa: E402
    PresentationAuditError,
    audit as audit_presentations,
    source_path,
    validate as validate_presentations,
)
from generate_prime_iggp_type_source import render_source_package  # noqa: E402
from prime_iggp_finite_herbrand import render_term  # noqa: E402
from prime_iggp_presentation import (  # noqa: E402
    PresentationError,
    parse_gdl_source_presentation,
    parse_gdl_type_profile,
)
from prime_iggp_stratification import NegativeDependencyCycle  # noqa: E402
from prime_iggp_stratified_model import (  # noqa: E402
    StratifiedModelBoundary,
    construct_stratified_model,
)


@dataclass(frozen=True)
class NativeModel:
    outcome: str
    stats: dict[str, int]
    supports: dict[tuple[int, int, str], int]


STAT_NAMES = (
    "source_forms",
    "source_rules",
    "source_facts",
    "assignments",
    "branch_expansions",
    "ground_instances",
    "distinct_checks",
    "support_nodes",
    "proof_edges",
    "positive_premise_references",
    "absence_receipts",
    "rounds",
    "completed_strata",
)

EXPECTED_FULL_CORPUS_TOTALS = {
    "covered_games": 30,
    "supports": 53_395,
    "proof_edges": 59_127,
    "ground_instances": 61_316,
    "absence_receipts": 1_124,
    "distinct_checks": 15_120,
}


def parse_native_output(output: bytes) -> NativeModel:
    try:
        lines = output.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise ValueError("native model output is not UTF-8") from exc
    if not lines:
        raise ValueError("native model output is empty")
    header = lines[0].split("\t")
    if (
        len(header) != 2 + len(STAT_NAMES)
        or header[0] != "GdlStratifiedModelV1"
    ):
        raise ValueError("native model header is malformed")
    try:
        stats = {
            name: int(value)
            for name, value in zip(STAT_NAMES, header[2:])
        }
    except ValueError as exc:
        raise ValueError("native model statistics are malformed") from exc

    supports: dict[tuple[int, int, str], int] = {}
    observed_indices: set[int] = set()
    for line in lines[1:]:
        fields = line.split("\t")
        if not fields or fields[0] != "S" or len(fields) != 6:
            raise ValueError("native Established model has a malformed row")
        try:
            index = int(fields[1])
            relation = int(fields[2])
            stratum = int(fields[3])
            proof_edges = int(fields[4])
        except ValueError as exc:
            raise ValueError("native support row has a malformed number") from exc
        key = relation, stratum, fields[5]
        if index in observed_indices or key in supports:
            raise ValueError("native model repeats a support identity")
        observed_indices.add(index)
        supports[key] = proof_edges
    if header[1] in {"Established", "Incomplete"}:
        if len(supports) != stats["support_nodes"]:
            raise ValueError("native support rows disagree with statistics")
        if sum(supports.values()) != stats["proof_edges"]:
            raise ValueError("native support proof counts disagree with graph")
        if any(count == 0 for count in supports.values()):
            raise ValueError("native support has no proof edge")
    elif supports:
        raise ValueError("non-model outcome unexpectedly carries supports")
    return NativeModel(header[1], stats, supports)


def run_native(runner: Path, source_package: str) -> NativeModel:
    completed = subprocess.run(
        (str(runner), "-"),
        input=source_package.encode("utf-8"),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(detail or "native model runner failed")
    return parse_native_output(completed.stdout)


def expected_supports(witness: object) -> set[tuple[int, int, str]]:
    return {
        (support.relation_index, support.stratum, render_term(support.literal))
        for support in witness.supports
    }


def compare_game(game: str, native: NativeModel, witness: object) -> None:
    if native.outcome != "Established":
        raise ValueError(
            f"native result is {native.outcome}, expected Established"
        )
    expected = expected_supports(witness)
    observed = set(native.supports)
    if observed != expected:
        missing = sorted(expected - observed)[:3]
        extra = sorted(observed - expected)[:3]
        raise ValueError(
            f"support model differs: missing={missing} extra={extra}"
        )
    reference = witness.stats
    expected_stats = {
        "source_forms": reference.source_forms,
        "source_rules": reference.source_rules,
        "source_facts": reference.source_facts,
        "branch_expansions": reference.branch_expansions,
        "ground_instances": reference.ground_instances,
        "distinct_checks": reference.distinct_checks,
        "support_nodes": reference.support_nodes,
        "completed_strata": reference.completed_strata,
    }
    observed_stats = {
        name: native.stats[name] for name in expected_stats
    }
    if observed_stats != expected_stats:
        raise ValueError(
            f"model work witness differs: native={observed_stats} "
            f"reference={expected_stats}"
        )
    if native.stats["proof_edges"] < native.stats["support_nodes"]:
        raise ValueError("proof graph has fewer edges than supports")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument(
        "--games",
        nargs="*",
        help="optional exact game names; the default is every eligible game",
    )
    args = parser.parse_args()
    if not args.runner.is_file():
        print("FAIL: native stratified-model runner is missing", file=sys.stderr)
        return 2
    try:
        audits, digest = audit_presentations(args.snapshot_root)
        validate_presentations(audits, digest)
    except (OSError, UnicodeDecodeError, PresentationAuditError) as exc:
        print(f"FAIL: IGGP presentation audit: {exc}", file=sys.stderr)
        return 1

    requested = set(args.games or ())
    known = {selected.game for selected in audits}
    if requested - known:
        print(
            "FAIL: unknown IGGP games: "
            + ", ".join(sorted(requested - known)),
            file=sys.stderr,
        )
        return 2

    totals: Counter[str] = Counter()
    covered: list[str] = []
    eligible: set[str] = set()
    for selected in audits:
        if (
            selected.checked_type_of_extensions != 1
            or selected.checked_type_of_occurrence_judgments == 0
            or selected.foreign_code_lines != 0
        ):
            continue
        eligible.add(selected.game)
        if requested and selected.game not in requested:
            continue
        try:
            source_bytes = source_path(
                args.snapshot_root, selected.game
            ).read_bytes()
            profile_bytes = (
                args.snapshot_root / "types" / f"{selected.game}.typ"
            ).read_bytes()
            source = parse_gdl_source_presentation(
                source_bytes.decode("utf-8")
            )
            profile = parse_gdl_type_profile(
                profile_bytes.decode("utf-8")
            )
            package = render_source_package(source_bytes, profile_bytes)
            native = run_native(args.runner.resolve(), package)
            witness = construct_stratified_model(source, profile)
            compare_game(selected.game, native, witness)
        except (
            NegativeDependencyCycle,
            OSError,
            PresentationError,
            RuntimeError,
            StratifiedModelBoundary,
            UnicodeDecodeError,
            ValueError,
        ) as exc:
            print(
                f"FAIL: IGGP {selected.game} stratified model: {exc}",
                file=sys.stderr,
            )
            return 1
        covered.append(selected.game)
        totals["supports"] += native.stats["support_nodes"]
        totals["proof_edges"] += native.stats["proof_edges"]
        totals["ground_instances"] += native.stats["ground_instances"]
        totals["absence_receipts"] += native.stats["absence_receipts"]
        totals["distinct_checks"] += native.stats["distinct_checks"]

    missing_requested = requested - eligible
    if missing_requested:
        print(
            "FAIL: requested games are outside the finite typed GDL image: "
            + ", ".join(sorted(missing_requested)),
            file=sys.stderr,
        )
        return 2
    expected_count = len(requested) if requested else len(eligible)
    if len(covered) != expected_count:
        print(
            f"FAIL: qualified {len(covered)} of {expected_count} games",
            file=sys.stderr,
        )
        return 1
    observed_totals = {
        "covered_games": len(covered),
        "supports": totals["supports"],
        "proof_edges": totals["proof_edges"],
        "ground_instances": totals["ground_instances"],
        "absence_receipts": totals["absence_receipts"],
        "distinct_checks": totals["distinct_checks"],
    }
    if not requested and observed_totals != EXPECTED_FULL_CORPUS_TOTALS:
        print(
            "FAIL: full IGGP stratified-model totals drifted: "
            f"observed={observed_totals} "
            f"expected={EXPECTED_FULL_CORPUS_TOTALS}",
            file=sys.stderr,
        )
        return 1
    print(
        "PrimeIggpNativeStratifiedModelCorpusSummary "
        + " ".join(
            f"{name}={value}" for name, value in observed_totals.items()
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
