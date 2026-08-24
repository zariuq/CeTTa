#!/usr/bin/env python3
"""Generate a checked GDL ``type:of`` inference program for one IGGP game."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from prime_iggp_generation import (  # noqa: E402
    GenerationError,
    materialize_outputs,
)
import check_prime_iggp_manifest as corpus  # noqa: E402
from check_prime_iggp_presentations import (  # noqa: E402
    GameAudit,
    audit as audit_presentations,
    source_path as presentation_source_path,
    validate as validate_presentations,
)
from prime_iggp_presentation import (  # noqa: E402
    PresentationError,
    analyze_gdl_existing_type_domains,
    check_gdl_type_of_extension,
    extract_gdl_typing_constraints,
    find_gdl_finite_type_assignment,
    inventory_gdl_checked_type_of_extension,
    parse_gdl_source_presentation,
    parse_gdl_type_profile,
    project_gdl_finite_typed_occurrences,
)
from prime_iggp_type_of_inference import (  # noqa: E402
    GdlTypeOfInferenceProgram,
    lower_gdl_type_of_inference_program,
    render_gdl_type_of_inference_program,
)


DEFAULT_GAME = "scissors_paper_stone"
SPS_GDL_SHA256 = (
    "fe401ac80704e5b138a48b80d6d2bd171427456245e72e555614efb96351710a"
)
SPS_TYPE_SHA256 = (
    "eabc85a114e3021c95e54bc6dd3e57a58921c8f37a06b232c1580cdee92cb9ba"
)

EXPECTED_OCCURRENCE_JUDGMENTS = 140
EXPECTED_APPLICATION_OCCURRENCES = 81
EXPECTED_APPLICATION_DERIVATIONS = 81
EXPECTED_AUTHORED_RULE_USES = 72
EXPECTED_STRUCTURAL_RULE_USES = 3
EXPECTED_EXTENDED_RULE_USES = 6
EXPECTED_VARIABLE_DERIVATIONS = 56
EXPECTED_LOGICAL_DERIVATIONS = 3
EXPECTED_LITERAL_BOUNDARIES = 45
EXPECTED_DERIVED_SIGNATURES = 3
EXPECTED_NONTRIVIAL_SUBTYPE_USES = 0
EXPECTED_RULES = 483
EXPECTED_CASES = 185
EXPECTED_CONSTRUCTORS = 208
EXPECTED_PROOF_OCCURRENCES = 185


def _selected_audit(
    snapshot_root: Path, game: str
) -> GameAudit:
    games, digest = audit_presentations(snapshot_root)
    validate_presentations(games, digest)
    selected = next((item for item in games if item.game == game), None)
    if selected is None:
        raise GenerationError(f"unknown IGGP game {game!r}")
    if (
        selected.checked_type_of_extensions != 1
        or selected.checked_type_of_occurrence_judgments == 0
    ):
        raise GenerationError(
            f"{game}: no complete checked type:of extension"
        )
    return selected


def build_program(
    snapshot_root: Path,
    game: str = DEFAULT_GAME,
    *,
    selected_audit: GameAudit | None = None,
) -> GdlTypeOfInferenceProgram:
    """Replay one pinned source/profile extension and lower its shared calculus."""

    selected = selected_audit or _selected_audit(snapshot_root, game)
    if selected.game != game:
        raise GenerationError(
            f"{game}: supplied presentation audit belongs to {selected.game}"
        )
    if (
        selected.checked_type_of_extensions != 1
        or selected.checked_type_of_occurrence_judgments == 0
    ):
        raise GenerationError(
            f"{game}: no complete checked type:of extension"
        )
    game_path = presentation_source_path(snapshot_root, game)
    type_path = snapshot_root / "types" / f"{game}.typ"
    source_bytes = game_path.read_bytes()
    profile_bytes = type_path.read_bytes()
    source_digest = hashlib.sha256(source_bytes).hexdigest()
    profile_digest = hashlib.sha256(profile_bytes).hexdigest()
    source = parse_gdl_source_presentation(source_bytes.decode("utf-8"))
    profile = parse_gdl_type_profile(profile_bytes.decode("utf-8"))
    constraints = extract_gdl_typing_constraints(source, profile)
    analysis = analyze_gdl_existing_type_domains(constraints, profile)
    assignment = find_gdl_finite_type_assignment(constraints, analysis)
    if assignment is None:
        raise GenerationError(
            f"{game}: no complete finite type assignment"
        )
    proposal = project_gdl_finite_typed_occurrences(
        constraints, analysis, assignment.assignment
    )
    extension = check_gdl_type_of_extension(
        constraints, profile, proposal
    )
    inventory = inventory_gdl_checked_type_of_extension(extension)
    observed_inventory = (
        inventory.occurrence_judgments,
        inventory.application_occurrences,
        inventory.application_derivations,
        inventory.authored_rule_uses,
        inventory.structural_rule_uses,
        inventory.extended_rule_uses,
        inventory.variable_derivations,
        inventory.logical_derivations,
        inventory.literal_boundaries,
        inventory.nontrivial_subtype_uses,
        inventory.derived_signatures,
    )
    expected_inventory = (
        selected.checked_type_of_occurrence_judgments,
        selected.checked_type_of_application_occurrences,
        selected.checked_type_of_application_derivations,
        selected.checked_type_of_authored_rule_uses,
        selected.checked_type_of_structural_rule_uses,
        selected.checked_type_of_extended_rule_uses,
        selected.checked_type_of_variable_derivations,
        selected.checked_type_of_logical_derivations,
        selected.checked_type_of_literal_boundaries,
        selected.checked_type_of_nontrivial_subtype_uses,
        selected.checked_type_of_derived_signatures,
    )
    if observed_inventory != expected_inventory:
        raise GenerationError(f"{game}: checked type:of inventory changed")
    if game == DEFAULT_GAME:
        pinned_sps_inventory = (
            EXPECTED_OCCURRENCE_JUDGMENTS,
            EXPECTED_APPLICATION_OCCURRENCES,
            EXPECTED_APPLICATION_DERIVATIONS,
            EXPECTED_AUTHORED_RULE_USES,
            EXPECTED_STRUCTURAL_RULE_USES,
            EXPECTED_EXTENDED_RULE_USES,
            EXPECTED_VARIABLE_DERIVATIONS,
            EXPECTED_LOGICAL_DERIVATIONS,
            EXPECTED_LITERAL_BOUNDARIES,
            EXPECTED_NONTRIVIAL_SUBTYPE_USES,
            EXPECTED_DERIVED_SIGNATURES,
        )
        if observed_inventory != pinned_sps_inventory:
            raise GenerationError("SPS checked type:of inventory changed")

    program = lower_gdl_type_of_inference_program(
        source,
        extension,
        source_digest=source_digest,
        profile_digest=profile_digest,
    )
    if len(program.cases) != (
        selected.checked_type_of_occurrence_judgments
        + selected.checked_type_of_literal_boundaries
    ):
        raise GenerationError(f"{game}: inference-case inventory changed")
    if game == DEFAULT_GAME:
        observed_program = (
            source_digest,
            profile_digest,
            len(program.rules),
            len(program.cases),
            len(program.constructors),
            sum(case.expected_proofs for case in program.cases),
        )
        expected_program = (
            SPS_GDL_SHA256,
            SPS_TYPE_SHA256,
            EXPECTED_RULES,
            EXPECTED_CASES,
            EXPECTED_CONSTRUCTORS,
            EXPECTED_PROOF_OCCURRENCES,
        )
        if observed_program != expected_program:
            raise GenerationError("SPS inference-program inventory changed")
    return program


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--game", choices=corpus.GAMES, default=DEFAULT_GAME)
    parser.add_argument(
        "--output",
        type=Path,
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        program = build_program(args.snapshot_root, args.game)
        output = args.output or (
            repo
            / "lib/ilp"
            / f"iggp_{args.game}_type_of_inference.generated.metta"
        )
        materialize_outputs(
            ((output, render_gdl_type_of_inference_program(program)),),
            args.check,
        )
    except (GenerationError, PresentationError, UnicodeDecodeError, OSError) as exc:
        print(
            f"FAIL: IGGP {args.game} type:of inference generation: {exc}",
            file=sys.stderr,
        )
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} IGGP {args.game} "
        "type:of "
        f"inference: {len(program.cases)} closed goals, "
        f"{sum(case.expected_proofs for case in program.cases)} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
