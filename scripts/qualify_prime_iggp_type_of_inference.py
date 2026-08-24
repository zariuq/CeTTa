#!/usr/bin/env python3
"""Qualify CeTTa's native GDL ``type:of`` kernel on every covered game."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from check_prime_iggp_presentations import (  # noqa: E402
    audit as audit_presentations,
    source_path,
    validate as validate_presentations,
)
from generate_prime_iggp_type_of_inference import (  # noqa: E402
    build_program,
)
from generate_prime_iggp_type_source import (  # noqa: E402
    render_source_package,
    source_revision,
)
from prime_iggp_generation import (  # noqa: E402
    GenerationError,
    materialize_outputs,
)
from prime_iggp_presentation import PresentationError  # noqa: E402
from prime_iggp_type_of_inference import (  # noqa: E402
    render_gdl_type_of_inference_program,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-output", type=Path, required=True)
    args = parser.parse_args()

    if not args.runner.is_file():
        print("FAIL: native GDL qualification runner is missing", file=sys.stderr)
        return 2

    try:
        games, digest = audit_presentations(args.snapshot_root)
        validate_presentations(games, digest)
    except (OSError, UnicodeDecodeError, PresentationError) as exc:
        print(f"FAIL: IGGP presentation audit: {exc}", file=sys.stderr)
        return 1

    covered = tuple(
        game
        for game in games
        if game.checked_type_of_extensions == 1
        and game.checked_type_of_occurrence_judgments != 0
    )
    obstructed = tuple(
        game.game
        for game in games
        if game.checked_type_of_extensions == 0
    )
    no_live_gdl = tuple(
        game.game
        for game in games
        if game.checked_type_of_extensions == 1
        and game.checked_type_of_occurrence_judgments == 0
    )

    total_rules = 0
    total_cases = 0
    total_proofs = 0
    for ordinal, selected in enumerate(covered, 1):
        try:
            program = build_program(
                args.snapshot_root,
                selected.game,
                selected_audit=selected,
            )
            rendered = render_gdl_type_of_inference_program(program)
            source = source_path(args.snapshot_root, selected.game).read_bytes()
            profile = (
                args.snapshot_root / "types" / f"{selected.game}.typ"
            ).read_bytes()
            source_rendered = render_source_package(source, profile)
            materialize_outputs(
                (
                    (args.output, rendered),
                    (args.source_output, source_rendered),
                ),
                False,
            )
        except (
            GenerationError,
            OSError,
            PresentationError,
            UnicodeDecodeError,
        ) as exc:
            print(
                f"FAIL: IGGP {selected.game} native qualification input: {exc}",
                file=sys.stderr,
            )
            return 1

        rules = len(program.rules)
        cases = len(program.cases)
        proofs = sum(case.expected_proofs for case in program.cases)
        completed = subprocess.run(
            (
                str(args.runner),
                str(args.output),
                str(args.source_output),
                selected.game,
                program.source_digest,
                program.profile_digest,
                program.revision,
                source_revision(source, profile),
                str(rules),
                str(cases),
                str(proofs),
            ),
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.returncode != 0:
            if completed.stderr:
                print(completed.stderr, end="", file=sys.stderr)
            print(
                f"FAIL: IGGP {selected.game} native qualification "
                f"({ordinal}/{len(covered)})",
                file=sys.stderr,
            )
            return completed.returncode
        total_rules += rules
        total_cases += cases
        total_proofs += proofs

    for game in obstructed:
        print(
            f"OUTSIDE-FRAGMENT: IGGP {game}: "
            "no complete finite typing assignment"
        )
    for game in no_live_gdl:
        print(f"NO-LIVE-GDL: IGGP {game}: no source judgment to construct")
    print(
        "PrimeIggpNativeTypeOfCorpusSummary "
        f"covered_games={len(covered)} rules={total_rules} "
        f"cases={total_cases} proof_occurrences={total_proofs} "
        f"outside_fragment_games={len(obstructed)} "
        f"no_live_gdl_games={len(no_live_gdl)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
