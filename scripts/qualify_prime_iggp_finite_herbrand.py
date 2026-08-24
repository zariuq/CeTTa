#!/usr/bin/env python3
"""Differentially qualify native typed Herbrand carriers on pinned IGGP."""

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
from prime_iggp_finite_herbrand import (  # noqa: E402
    FiniteHerbrandWitness,
    construct_finite_herbrand,
    render_term,
)
from prime_iggp_presentation import (  # noqa: E402
    PresentationError,
    parse_gdl_type_profile,
)


EXPECTED_COVERED_GAMES = 30
EXPECTED_TERMS = 197403
EXPECTED_CONSTRUCTOR_APPLICATIONS = 196328
EXPECTED_LARGEST_GAME = "ttcc4"
EXPECTED_LARGEST_TERMS = 125454


def render_witness(witness: FiniteHerbrandWitness) -> bytes:
    construction_count = sum(
        len(term.constructions) for term in witness.terms
    )
    constructor_count = sum(
        not signature.relation for signature in witness.signatures
    )
    lines = [
        "\t".join(
            (
                "GdlFiniteHerbrandV1",
                str(witness.type_count),
                str(len(witness.signatures)),
                str(constructor_count),
                str(len(witness.relation_indices)),
                str(witness.subtype_edge_count),
                str(len(witness.terms)),
                str(construction_count),
                str(witness.constructor_applications),
                str(witness.rounds),
                str(witness.maximum_depth),
            )
        )
    ]
    for term_index, term in enumerate(witness.terms):
        lines.append(
            "\t".join(
                (
                    "T",
                    str(term_index),
                    term.exact_type,
                    str(term.depth),
                    str(len(term.constructions)),
                    render_term(term.term),
                )
            )
        )
        for construction_index, construction in enumerate(
            term.constructions
        ):
            signature = witness.signatures[construction.signature_index]
            arguments = (
                ",".join(
                    str(index)
                    for index in construction.argument_term_indices
                )
                or "-"
            )
            lines.append(
                "\t".join(
                    (
                        "C",
                        str(term_index),
                        str(construction_index),
                        str(construction.signature_index),
                        str(signature.statement_ordinal),
                        str(signature.name_ordinal),
                        arguments,
                    )
                )
            )
    for relation_index, signature_index in enumerate(
        witness.relation_indices
    ):
        signature = witness.signatures[signature_index]
        lines.append(
            "\t".join(
                (
                    "Q",
                    str(relation_index),
                    str(signature_index),
                    str(signature.statement_ordinal),
                    str(signature.name_ordinal),
                    signature.name,
                    str(len(signature.argument_types)),
                    ",".join(signature.argument_types) or "-",
                )
            )
        )
    return ("\n".join(lines) + "\n").encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if not args.runner.is_file():
        print("FAIL: native finite-Herbrand runner is missing", file=sys.stderr)
        return 2
    try:
        audits, digest = audit_presentations(args.snapshot_root)
        validate_presentations(audits, digest)
    except (OSError, UnicodeDecodeError, PresentationAuditError) as exc:
        print(f"FAIL: IGGP presentation audit: {exc}", file=sys.stderr)
        return 1

    totals: Counter[str] = Counter()
    covered: list[str] = []
    largest = ("", 0)
    for selected in audits:
        if (
            selected.checked_type_of_extensions != 1
            or selected.checked_type_of_occurrence_judgments == 0
            or selected.foreign_code_lines != 0
        ):
            continue
        try:
            source_bytes = source_path(
                args.snapshot_root, selected.game
            ).read_bytes()
            profile_bytes = (
                args.snapshot_root / "types" / f"{selected.game}.typ"
            ).read_bytes()
            profile = parse_gdl_type_profile(
                profile_bytes.decode("utf-8")
            )
            witness = construct_finite_herbrand(profile)
            package = render_source_package(source_bytes, profile_bytes)
        except (OSError, PresentationError, UnicodeDecodeError) as exc:
            print(
                f"FAIL: IGGP {selected.game} finite Herbrand input: {exc}",
                file=sys.stderr,
            )
            return 1
        expected = render_witness(witness)
        completed = subprocess.run(
            (str(args.runner.resolve()), "-", "--herbrand"),
            input=package.encode("utf-8"),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if completed.returncode != 0:
            if completed.stderr:
                print(
                    completed.stderr.decode("utf-8", errors="replace"),
                    end="",
                    file=sys.stderr,
                )
            print(
                f"FAIL: IGGP {selected.game} native finite Herbrand",
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
                f"FAIL: IGGP {selected.game} finite Herbrand witness "
                f"differs at canonical line {mismatch}",
                file=sys.stderr,
            )
            return 1
        covered.append(selected.game)
        totals["terms"] += len(witness.terms)
        totals["applications"] += witness.constructor_applications
        if len(witness.terms) > largest[1]:
            largest = selected.game, len(witness.terms)

    if (
        len(covered) != EXPECTED_COVERED_GAMES
        or totals["terms"] != EXPECTED_TERMS
        or totals["applications"] != EXPECTED_CONSTRUCTOR_APPLICATIONS
        or largest != (EXPECTED_LARGEST_GAME, EXPECTED_LARGEST_TERMS)
    ):
        print(
            "FAIL: finite Herbrand corpus totals changed: "
            f"covered={len(covered)} terms={totals['terms']} "
            f"applications={totals['applications']} largest={largest}",
            file=sys.stderr,
        )
        return 1
    print(
        "PrimeIggpNativeFiniteHerbrandCorpusSummary "
        f"covered_games={len(covered)} terms={totals['terms']} "
        f"constructor_applications={totals['applications']} "
        f"largest_game={largest[0]} largest_terms={largest[1]}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
