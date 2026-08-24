#!/usr/bin/env python3
"""Differentially qualify native GDL stratification on the pinned corpus."""

from __future__ import annotations

import argparse
from collections import Counter
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
from prime_iggp_presentation import (  # noqa: E402
    PresentationError,
    parse_gdl_source_presentation,
)
from prime_iggp_stratification import (  # noqa: E402
    NegativeDependencyCycle,
    StratificationWitness,
    construct_stratification,
)


EXPECTED_COVERED_GAMES = 30
EXPECTED_RELATIONS = 610
EXPECTED_DEFINED_RELATIONS = 548
EXPECTED_EDGES = 2938
EXPECTED_NEGATIVE_EDGES = 293
EXPECTED_MAXIMUM_STRATA = {0: 4, 1: 12, 2: 11, 3: 2, 5: 1}


def render_witness(witness: StratificationWitness) -> bytes:
    """Render the complete ordered witness in the C qualifier's format."""

    relation_index = {
        relation.signature: index
        for index, relation in enumerate(witness.relations)
    }
    lines = [
        "\t".join(
            (
                "GdlStratificationV1",
                str(len(witness.relations)),
                str(len(witness.edges)),
                str(witness.maximum_stratum),
            )
        )
    ]
    for index, relation in enumerate(witness.relations):
        lines.append(
            "\t".join(
                (
                    "R",
                    str(index),
                    relation.signature.name,
                    str(relation.signature.arity),
                    str(relation.stratum),
                    "1" if relation.defined else "0",
                )
            )
        )
    for index, edge in enumerate(witness.edges):
        path = ".".join(str(step) for step in edge.path) or "-"
        lines.append(
            "\t".join(
                (
                    "E",
                    str(index),
                    str(edge.source[1]),
                    str(edge.source[2]),
                    str(edge.source[3]),
                    path,
                    str(relation_index[edge.head]),
                    str(relation_index[edge.body]),
                    "1" if edge.negative else "0",
                )
            )
        )
    return ("\n".join(lines) + "\n").encode("utf-8")


def run_native(runner: Path, source_package: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        (str(runner), "-"),
        input=source_package.encode("utf-8"),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()

    if not args.runner.is_file():
        print("FAIL: native stratification runner is missing", file=sys.stderr)
        return 2
    try:
        audits, digest = audit_presentations(args.snapshot_root)
        validate_presentations(audits, digest)
    except (OSError, UnicodeDecodeError, PresentationAuditError) as exc:
        print(f"FAIL: IGGP presentation audit: {exc}", file=sys.stderr)
        return 1

    totals: Counter[str] = Counter()
    maximum_strata: Counter[int] = Counter()
    covered: list[str] = []
    mixed: list[str] = []
    typing_outside: list[str] = []
    no_live_gdl: list[str] = []
    for selected in audits:
        if selected.checked_type_of_extensions != 1:
            typing_outside.append(selected.game)
            continue
        if selected.checked_type_of_occurrence_judgments == 0:
            no_live_gdl.append(selected.game)
            continue
        if selected.foreign_code_lines != 0:
            mixed.append(selected.game)
            continue
        try:
            source_bytes = source_path(
                args.snapshot_root, selected.game
            ).read_bytes()
            profile_bytes = (
                args.snapshot_root / "types" / f"{selected.game}.typ"
            ).read_bytes()
            presentation = parse_gdl_source_presentation(
                source_bytes.decode("utf-8")
            )
            witness = construct_stratification(presentation)
            source_package = render_source_package(source_bytes, profile_bytes)
        except (
            NegativeDependencyCycle,
            OSError,
            PresentationError,
            UnicodeDecodeError,
        ) as exc:
            print(
                f"FAIL: IGGP {selected.game} stratification input: {exc}",
                file=sys.stderr,
            )
            return 1

        expected = render_witness(witness)
        completed = run_native(args.runner.resolve(), source_package)
        if completed.returncode != 0:
            if completed.stderr:
                print(
                    completed.stderr.decode("utf-8", errors="replace"),
                    end="",
                    file=sys.stderr,
                )
            print(
                f"FAIL: IGGP {selected.game} native stratification",
                file=sys.stderr,
            )
            return completed.returncode
        if completed.stdout != expected:
            observed_lines = completed.stdout.decode(
                "utf-8", errors="replace"
            ).splitlines()
            expected_lines = expected.decode("utf-8").splitlines()
            mismatch = next(
                (
                    index
                    for index, (observed, wanted) in enumerate(
                        zip(observed_lines, expected_lines), 1
                    )
                    if observed != wanted
                ),
                min(len(observed_lines), len(expected_lines)) + 1,
            )
            print(
                f"FAIL: IGGP {selected.game} dependency witness differs "
                f"at canonical line {mismatch}",
                file=sys.stderr,
            )
            return 1

        covered.append(selected.game)
        totals["relations"] += len(witness.relations)
        totals["defined_relations"] += sum(
            relation.defined for relation in witness.relations
        )
        totals["edges"] += len(witness.edges)
        totals["negative_edges"] += sum(
            edge.negative for edge in witness.edges
        )
        maximum_strata[witness.maximum_stratum] += 1

    observed = {
        "covered_games": len(covered),
        "relations": totals["relations"],
        "defined_relations": totals["defined_relations"],
        "edges": totals["edges"],
        "negative_edges": totals["negative_edges"],
    }
    expected_totals = {
        "covered_games": EXPECTED_COVERED_GAMES,
        "relations": EXPECTED_RELATIONS,
        "defined_relations": EXPECTED_DEFINED_RELATIONS,
        "edges": EXPECTED_EDGES,
        "negative_edges": EXPECTED_NEGATIVE_EDGES,
    }
    if observed != expected_totals or dict(maximum_strata) != (
        EXPECTED_MAXIMUM_STRATA
    ):
        print(
            "FAIL: stratification corpus totals changed: "
            f"totals={observed} maxima={dict(maximum_strata)}",
            file=sys.stderr,
        )
        return 1

    for game in typing_outside:
        print(f"OUTSIDE-FRAGMENT: IGGP {game}: no complete typing witness")
    for game in mixed:
        print(f"OUTSIDE-FRAGMENT: IGGP {game}: mixed GDL/foreign source")
    for game in no_live_gdl:
        print(f"NO-LIVE-GDL: IGGP {game}: no source judgment")
    maxima = ",".join(
        f"{stratum}:{count}"
        for stratum, count in sorted(maximum_strata.items())
    )
    print(
        "PrimeIggpNativeStratificationCorpusSummary "
        f"covered_games={len(covered)} relations={totals['relations']} "
        f"defined_relations={totals['defined_relations']} "
        f"edges={totals['edges']} negative_edges={totals['negative_edges']} "
        f"maximum_strata={maxima} mixed_games={len(mixed)} "
        f"outside_typing_games={len(typing_outside)} "
        f"no_live_gdl_games={len(no_live_gdl)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
