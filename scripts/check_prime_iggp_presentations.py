#!/usr/bin/env python3
"""Audit the authored GDL and type-profile presentations for all IGGP games."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
import sys

import check_prime_iggp_manifest as corpus
from prime_iggp_presentation import (
    GdlDerivedDomainKind,
    GdlDerivedSignatureSupportKind,
    PresentationError,
    analyze_gdl_existing_type_domains,
    check_gdl_type_of_extension,
    extract_gdl_negative_premise_demands,
    extract_gdl_typing_constraints,
    gdl_empty_domain_receipts,
    gdl_derived_signature_supports,
    gdl_typing_demand,
    group_gdl_derived_signature_supports,
    inventory_gdl_derived_supports,
    inventory_gdl_existing_type_arc_analysis,
    inventory_gdl_empty_domain_receipts,
    inventory_gdl_checked_type_of_extension,
    inventory_gdl_negative_premises,
    inventory_gdl_source,
    inventory_gdl_typing_constraints,
    inventory_gdl_types,
    parse_gdl_source_presentation,
    parse_gdl_type_profile,
    project_gdl_derived_finite_completions,
    project_gdl_finite_typed_negative_premises,
    project_gdl_finite_typed_occurrences,
)


SOURCE_DIGEST = (
    "a7e21987b3a6c75fbceca4501730915dec370631fc93369bf07ac3dd4c225f0a"
)
EXPECTED_TOTALS = {
    "forms": 4898,
    "rules": 1811,
    "facts": 3087,
    "foreign_code_lines": 124,
    "negations": 402,
    "disjunctions": 6,
    "distinctions": 133,
    "unsafe_heads": 0,
    "unsafe_negatives": 0,
    "negative_relation_absence_demands": 397,
    "negative_distinct_refutation_demands": 5,
    "negative_unsupported_demands": 0,
    "negative_ground_relation_absences": 204,
    "negative_variable_relation_absences": 193,
    "negative_variable_demands": 314,
    "negative_unbound_variable_demands": 0,
    "negative_positive_binding_witnesses": 356,
    "negative_positive_binding_branches": 356,
    "signature_statements": 853,
    "signature_occurrences": 2861,
    "subtype_statements": 30,
    "duplicate_signatures": 8,
    "missing_applications": 210,
    "unmatched_authored_names": 3,
    "constraint_occurrence_types": 32178,
    "constraint_rule_variables": 4864,
    "constraint_applications": 21193,
    "constraint_authored_applications": 19321,
    "constraint_structural_applications": 825,
    "constraint_missing_profile_applications": 1030,
    "constraint_arity_mismatch_applications": 17,
    "constraint_ambiguous_authored_applications": 0,
    "constraint_derived_signatures": 213,
    "constraint_logical_forms": 541,
    "constraint_distinct_forms": 133,
    "constraint_equalities": 32178,
    "constraint_acceptances": 31371,
    "constraint_unsupported_shapes": 0,
    "support_signatures": 213,
    "support_single_anchor_signatures": 199,
    "support_subtype_ordered_signatures": 8,
    "support_unanchored_signatures": 2,
    "support_incomparable_signatures": 4,
    "support_slots": 574,
    "support_unanchored_slots": 2,
    "support_single_anchor_slots": 547,
    "support_comparable_multi_anchor_slots": 9,
    "support_incomparable_anchor_slots": 16,
    "arc_type_names": 332,
    "arc_subtype_edges": 30,
    "arc_acceptance_paths": 367,
    "arc_components": 26125,
    "arc_empty_components": 105,
    "arc_exact_conflict_components": 0,
    "arc_candidate_eliminations": 33003,
    "arc_derived_slots": 574,
    "arc_singleton_derived_slots": 390,
    "arc_multiple_derived_slots": 168,
    "arc_empty_derived_slots": 16,
    "arc_derived_signatures": 213,
    "arc_singleton_derived_signatures": 129,
    "arc_multiple_derived_signatures": 80,
    "arc_empty_derived_signatures": 4,
    "arc_known_equality_conflicts": 0,
    "arc_known_acceptance_conflicts": 0,
    "receipt_count": 105,
    "receipt_source_occurrences": 157,
    "receipt_derived_slots": 16,
    "receipt_with_derived_slots": 16,
    "receipt_exact_conflicts": 0,
    "receipt_candidate_eliminations": 245,
    "receipt_invalid_eliminations": 0,
    "finite_assignment_found": 44,
    "finite_assignment_choices": 21737,
    "finite_assignment_equalities": 26741,
    "finite_assignment_acceptances": 26043,
    "finite_assignment_nontrivial_paths": 1379,
    "finite_projected_derived_slots": 413,
    "finite_local_derived_candidates": 550,
    "finite_supported_derived_candidates": 550,
    "finite_unsupported_derived_candidates": 0,
    "finite_typed_projection_found": 44,
    "finite_typed_resolved_expressions": 31160,
    "finite_typed_applications": 17679,
    "finite_typed_derived_signatures": 164,
    "checked_type_of_extensions": 44,
    "checked_type_of_occurrence_judgments": 26741,
    "checked_type_of_application_occurrences": 17679,
    "checked_type_of_application_derivations": 17729,
    "checked_type_of_authored_rule_uses": 16241,
    "checked_type_of_structural_rule_uses": 659,
    "checked_type_of_extended_rule_uses": 829,
    "checked_type_of_variable_derivations": 8594,
    "checked_type_of_logical_derivations": 468,
    "checked_type_of_literal_boundaries": 7525,
    "checked_type_of_nontrivial_subtype_uses": 1379,
    "checked_type_of_derived_signatures": 164,
    "finite_typed_negative_relation_absences": 349,
    "finite_typed_negative_distinct_refutations": 0,
    "finite_typed_negative_unsupported": 0,
}
EXPECTED_GDL_ONLY = 33
EXPECTED_MIXED = 16
EXPECTED_NO_LIVE_GDL = ("horseshoe",)
EXPECTED_OVERLOADS = (("pilgrimage", ("place_pilgrim",)),)
EXPECTED_UNMATCHED = (
    ("asylum", (("f", 2),)),
    ("tiger_vs_dogs", (("cell", 2),)),
    ("ttcc4", (("cell", 2),)),
)
EXPECTED_UNANCHORED_SIGNATURES = (
    ("asylum", (("f", 2),)),
    ("lightboard", (("cell", 2),)),
)
EXPECTED_INCOMPARABLE_SIGNATURES = (
    (
        "tiger_vs_dogs",
        (("down", 4), ("left", 4), ("right", 4), ("up", 4)),
    ),
)
EXPECTED_EMPTY_COMPONENTS = (
    ("asylum", 29),
    ("battle_of_numbers", 7),
    ("coins", 30),
    ("dont_touch", 1),
    ("farming", 2),
    ("tiger_vs_dogs", 36),
)
EXPECTED_EMPTY_DERIVED_SIGNATURES = (
    (
        "tiger_vs_dogs",
        (("down", 4), ("left", 4), ("right", 4), ("up", 4)),
    ),
)
EXPECTED_NO_FINITE_ASSIGNMENT = (
    "asylum",
    "battle_of_numbers",
    "coins",
    "dont_touch",
    "farming",
    "tiger_vs_dogs",
)


class PresentationAuditError(RuntimeError):
    """The pinned presentation inventory no longer satisfies its ledger."""


@dataclass(frozen=True)
class TaskPresentationFamily:
    """One shared task semantics plus its authored instance presentations."""

    source_path: Path
    instance_paths: tuple[Path, ...]


@dataclass(frozen=True)
class GameAudit:
    game: str
    source_path: str
    forms: int
    rules: int
    facts: int
    foreign_code_lines: int
    negations: int
    disjunctions: int
    distinctions: int
    unsafe_heads: int
    unsafe_negatives: int
    negative_relation_absence_demands: int
    negative_distinct_refutation_demands: int
    negative_unsupported_demands: int
    negative_ground_relation_absences: int
    negative_variable_relation_absences: int
    negative_variable_demands: int
    negative_unbound_variable_demands: int
    negative_positive_binding_witnesses: int
    negative_positive_binding_branches: int
    signature_statements: int
    signature_occurrences: int
    subtype_statements: int
    duplicate_signatures: int
    overloads: tuple[str, ...]
    missing_applications: tuple[tuple[str, int], ...]
    unmatched_authored_names: tuple[tuple[str, int], ...]
    constraint_occurrence_types: int
    constraint_rule_variables: int
    constraint_applications: int
    constraint_authored_applications: int
    constraint_structural_applications: int
    constraint_missing_profile_applications: int
    constraint_arity_mismatch_applications: int
    constraint_ambiguous_authored_applications: int
    constraint_derived_signatures: int
    constraint_logical_forms: int
    constraint_distinct_forms: int
    constraint_equalities: int
    constraint_acceptances: int
    constraint_unsupported_shapes: int
    support_signatures: int
    support_single_anchor_signatures: int
    support_subtype_ordered_signatures: int
    support_unanchored_signatures: int
    support_incomparable_signatures: int
    support_slots: int
    support_unanchored_slots: int
    support_single_anchor_slots: int
    support_comparable_multi_anchor_slots: int
    support_incomparable_anchor_slots: int
    arc_type_names: int
    arc_subtype_edges: int
    arc_acceptance_paths: int
    arc_components: int
    arc_empty_components: int
    arc_exact_conflict_components: int
    arc_candidate_eliminations: int
    arc_derived_slots: int
    arc_singleton_derived_slots: int
    arc_multiple_derived_slots: int
    arc_empty_derived_slots: int
    arc_derived_signatures: int
    arc_singleton_derived_signatures: int
    arc_multiple_derived_signatures: int
    arc_empty_derived_signatures: int
    arc_known_equality_conflicts: int
    arc_known_acceptance_conflicts: int
    receipt_count: int
    receipt_source_occurrences: int
    receipt_derived_slots: int
    receipt_with_derived_slots: int
    receipt_exact_conflicts: int
    receipt_candidate_eliminations: int
    receipt_invalid_eliminations: int
    finite_assignment_found: int
    finite_assignment_choices: int
    finite_assignment_equalities: int
    finite_assignment_acceptances: int
    finite_assignment_nontrivial_paths: int
    finite_projected_derived_slots: int
    finite_local_derived_candidates: int
    finite_supported_derived_candidates: int
    finite_unsupported_derived_candidates: int
    finite_typed_projection_found: int
    finite_typed_resolved_expressions: int
    finite_typed_applications: int
    finite_typed_derived_signatures: int
    checked_type_of_extensions: int
    checked_type_of_occurrence_judgments: int
    checked_type_of_application_occurrences: int
    checked_type_of_application_derivations: int
    checked_type_of_authored_rule_uses: int
    checked_type_of_structural_rule_uses: int
    checked_type_of_extended_rule_uses: int
    checked_type_of_variable_derivations: int
    checked_type_of_logical_derivations: int
    checked_type_of_literal_boundaries: int
    checked_type_of_nontrivial_subtype_uses: int
    checked_type_of_derived_signatures: int
    finite_typed_negative_relation_absences: int
    finite_typed_negative_distinct_refutations: int
    finite_typed_negative_unsupported: int
    unanchored_signatures: tuple[tuple[str, int], ...]
    incomparable_signatures: tuple[tuple[str, int], ...]
    empty_derived_signatures: tuple[tuple[str, int], ...]


def add_digest_blob(digest, label: str, content: bytes) -> None:
    encoded_label = label.encode("utf-8")
    digest.update(len(encoded_label).to_bytes(8, "big"))
    digest.update(encoded_label)
    digest.update(len(content).to_bytes(8, "big"))
    digest.update(content)


def source_path(snapshot_root: Path, game: str) -> Path:
    gdl = snapshot_root / "games" / f"{game}.txt"
    if gdl.is_file():
        return gdl
    prolog_suffixed = snapshot_root / "games" / f"{game}.pl"
    if prolog_suffixed.is_file():
        return prolog_suffixed
    raise PresentationAuditError(f"{game}: no repository game source")


def _presentation_imports(path: Path) -> tuple[str, ...]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise PresentationAuditError(
            f"{path.name}: presentation import is not UTF-8"
        ) from exc
    imports = []
    for line in lines:
        words = line.strip().split()
        if not words or words[0] != "import":
            continue
        if len(words) != 2:
            raise PresentationAuditError(
                f"{path.name}: malformed presentation import"
            )
        imports.append(words[1])
    return tuple(imports)


def task_presentation_family(
    snapshot_root: Path, game: str
) -> TaskPresentationFamily:
    """Resolve a task's explicit common-core composition when it has one."""

    canonical = source_path(snapshot_root, game)
    games = snapshot_root / "games"
    instances = tuple(sorted(games.glob(f"{game}_map*.txt")))
    if not instances:
        return TaskPresentationFamily(canonical, ())

    imports = tuple(_presentation_imports(path) for path in instances)
    if any(len(names) != 1 for names in imports):
        raise PresentationAuditError(
            f"{game}: every task instance must import exactly one common core"
        )
    imported_names = {names[0] for names in imports}
    if len(imported_names) != 1:
        raise PresentationAuditError(
            f"{game}: task instances do not share one semantic core"
        )
    imported_name = next(iter(imported_names))
    core = games / f"{imported_name}.txt"
    if not core.is_file():
        raise PresentationAuditError(
            f"{game}: imported task core {imported_name} is missing"
        )
    if _presentation_imports(core):
        raise PresentationAuditError(
            f"{game}: nested task-core imports are not yet represented"
        )
    return TaskPresentationFamily(core, instances)


def task_source_path(snapshot_root: Path, game: str) -> Path:
    """Return the semantic source actually shared by the benchmark tasks."""

    return task_presentation_family(snapshot_root, game).source_path


def audit(snapshot_root: Path) -> tuple[tuple[GameAudit, ...], str]:
    digest = hashlib.sha256(b"cetta-prime-iggp-presentation-source-v2\0")
    games: list[GameAudit] = []
    for game in corpus.GAMES:
        game_path = source_path(snapshot_root, game)
        task_family = task_presentation_family(snapshot_root, game)
        type_path = snapshot_root / "types" / f"{game}.typ"
        if not type_path.is_file():
            raise PresentationAuditError(f"{game}: type profile is missing")
        game_bytes = game_path.read_bytes()
        type_bytes = type_path.read_bytes()
        for path, content in ((game_path, game_bytes), (type_path, type_bytes)):
            add_digest_blob(
                digest, str(path.relative_to(snapshot_root)), content
            )
        if task_family.source_path != game_path:
            for path in (task_family.source_path, *task_family.instance_paths):
                add_digest_blob(
                    digest,
                    str(path.relative_to(snapshot_root)),
                    path.read_bytes(),
                )
        try:
            source = parse_gdl_source_presentation(
                game_bytes.decode("utf-8")
            )
            profile = parse_gdl_type_profile(type_bytes.decode("utf-8"))
        except (UnicodeDecodeError, PresentationError) as exc:
            raise PresentationAuditError(f"{game}: {exc}") from exc
        source_inventory = inventory_gdl_source(source)
        negative_demands = extract_gdl_negative_premise_demands(source)
        negative_inventory = inventory_gdl_negative_premises(
            negative_demands
        )
        retained_negative_count = (
            negative_inventory.relation_absences
            + negative_inventory.distinct_refutations
            + negative_inventory.unsupported
        )
        if retained_negative_count != source_inventory.negation_count:
            raise PresentationAuditError(
                f"{game}: negative-premise demands lost a source occurrence"
            )
        type_inventory = inventory_gdl_types(profile)
        demand = gdl_typing_demand(source, profile)
        constraint_evidence = extract_gdl_typing_constraints(source, profile)
        constraint_inventory = inventory_gdl_typing_constraints(
            constraint_evidence
        )
        signature_supports = group_gdl_derived_signature_supports(
            gdl_derived_signature_supports(
                constraint_evidence, profile
            )
        )
        support_inventory = inventory_gdl_derived_supports(
            tuple(
                slot
                for signature in signature_supports
                for slot in signature.slots
            )
        )
        arc_analysis = analyze_gdl_existing_type_domains(
            constraint_evidence, profile
        )
        arc_inventory = inventory_gdl_existing_type_arc_analysis(
            arc_analysis
        )
        discrepancy_receipts = gdl_empty_domain_receipts(arc_analysis)
        receipt_inventory = inventory_gdl_empty_domain_receipts(
            discrepancy_receipts, arc_analysis.universe
        )
        completion_projection = project_gdl_derived_finite_completions(
            constraint_evidence, arc_analysis
        )
        finite_assignment = (
            None
            if completion_projection is None
            else completion_projection.existence_witness
        )
        projected_domains = (
            ()
            if completion_projection is None
            else completion_projection.derived_domains
        )
        typed_projection = (
            None
            if finite_assignment is None
            else project_gdl_finite_typed_occurrences(
                constraint_evidence,
                arc_analysis,
                finite_assignment.assignment,
            )
        )
        checked_type_of_extension = (
            None
            if typed_projection is None
            else check_gdl_type_of_extension(
                constraint_evidence, profile, typed_projection
            )
        )
        checked_type_of_inventory = (
            None
            if checked_type_of_extension is None
            else inventory_gdl_checked_type_of_extension(
                checked_type_of_extension
            )
        )
        typed_negative_projection = (
            None
            if typed_projection is None
            else project_gdl_finite_typed_negative_premises(
                negative_demands, typed_projection
            )
        )
        local_finite_obstruction = bool(
            discrepancy_receipts
            or arc_analysis.known_equality_conflicts
            or arc_analysis.known_acceptance_conflicts
        )
        if finite_assignment is None and not local_finite_obstruction:
            raise PresentationAuditError(
                f"{game}: finite search found no assignment without a "
                "replayable local obstruction"
            )
        if finite_assignment is not None and local_finite_obstruction:
            raise PresentationAuditError(
                f"{game}: finite assignment bypassed a local obstruction"
            )
        games.append(
            GameAudit(
                game=game,
                source_path=str(game_path.relative_to(snapshot_root)),
                forms=source_inventory.form_count,
                rules=source_inventory.rule_count,
                facts=source_inventory.fact_count,
                foreign_code_lines=source_inventory.foreign_code_lines,
                negations=source_inventory.negation_count,
                disjunctions=source_inventory.disjunction_count,
                distinctions=source_inventory.distinct_count,
                unsafe_heads=source_inventory.unsafe_head_rules,
                unsafe_negatives=source_inventory.unsafe_negative_rules,
                negative_relation_absence_demands=(
                    negative_inventory.relation_absences
                ),
                negative_distinct_refutation_demands=(
                    negative_inventory.distinct_refutations
                ),
                negative_unsupported_demands=(
                    negative_inventory.unsupported
                ),
                negative_ground_relation_absences=(
                    negative_inventory.ground_relation_absences
                ),
                negative_variable_relation_absences=(
                    negative_inventory.variable_relation_absences
                ),
                negative_variable_demands=(
                    negative_inventory.variable_demands
                ),
                negative_unbound_variable_demands=(
                    negative_inventory.unbound_variable_demands
                ),
                negative_positive_binding_witnesses=(
                    negative_inventory.positive_binding_witnesses
                ),
                negative_positive_binding_branches=(
                    negative_inventory.positive_binding_branches
                ),
                signature_statements=type_inventory.signature_statements,
                signature_occurrences=type_inventory.signature_occurrences,
                subtype_statements=type_inventory.subtype_statements,
                duplicate_signatures=(
                    type_inventory.duplicate_signature_occurrences
                ),
                overloads=type_inventory.overloaded_symbols,
                missing_applications=demand.missing_applications,
                unmatched_authored_names=(
                    demand.unmatched_authored_name_applications
                ),
                constraint_occurrence_types=(
                    constraint_inventory.occurrence_types
                ),
                constraint_rule_variables=(
                    constraint_inventory.rule_variable_types
                ),
                constraint_applications=(
                    constraint_inventory.application_occurrences
                ),
                constraint_authored_applications=(
                    constraint_inventory.authored_applications
                ),
                constraint_structural_applications=(
                    constraint_inventory.structural_applications
                ),
                constraint_missing_profile_applications=(
                    constraint_inventory.missing_profile_applications
                ),
                constraint_arity_mismatch_applications=(
                    constraint_inventory.arity_mismatch_applications
                ),
                constraint_ambiguous_authored_applications=(
                    constraint_inventory.ambiguous_authored_applications
                ),
                constraint_derived_signatures=(
                    constraint_inventory.derived_signatures
                ),
                constraint_logical_forms=(
                    constraint_inventory.logical_forms
                ),
                constraint_distinct_forms=(
                    constraint_inventory.distinct_forms
                ),
                constraint_equalities=(
                    constraint_inventory.equality_constraints
                ),
                constraint_acceptances=(
                    constraint_inventory.acceptance_constraints
                ),
                constraint_unsupported_shapes=(
                    constraint_inventory.unsupported_shapes
                ),
                support_signatures=support_inventory.signatures,
                support_single_anchor_signatures=(
                    support_inventory.single_anchor_signatures
                ),
                support_subtype_ordered_signatures=(
                    support_inventory.subtype_ordered_signatures
                ),
                support_unanchored_signatures=(
                    support_inventory.unanchored_signatures
                ),
                support_incomparable_signatures=(
                    support_inventory.incomparable_signatures
                ),
                support_slots=support_inventory.slots,
                support_unanchored_slots=(
                    support_inventory.unanchored_slots
                ),
                support_single_anchor_slots=(
                    support_inventory.single_anchor_slots
                ),
                support_comparable_multi_anchor_slots=(
                    support_inventory.comparable_multi_anchor_slots
                ),
                support_incomparable_anchor_slots=(
                    support_inventory.incomparable_anchor_slots
                ),
                arc_type_names=arc_inventory.type_names,
                arc_subtype_edges=arc_inventory.subtype_edges,
                arc_acceptance_paths=arc_inventory.acceptance_paths,
                arc_components=arc_inventory.components,
                arc_empty_components=arc_inventory.empty_components,
                arc_exact_conflict_components=(
                    arc_inventory.exact_conflict_components
                ),
                arc_candidate_eliminations=(
                    arc_inventory.candidate_eliminations
                ),
                arc_derived_slots=arc_inventory.derived_slots,
                arc_singleton_derived_slots=(
                    arc_inventory.singleton_derived_slots
                ),
                arc_multiple_derived_slots=(
                    arc_inventory.multiple_derived_slots
                ),
                arc_empty_derived_slots=(
                    arc_inventory.empty_derived_slots
                ),
                arc_derived_signatures=(
                    arc_inventory.derived_signatures
                ),
                arc_singleton_derived_signatures=(
                    arc_inventory.singleton_derived_signatures
                ),
                arc_multiple_derived_signatures=(
                    arc_inventory.multiple_derived_signatures
                ),
                arc_empty_derived_signatures=(
                    arc_inventory.empty_derived_signatures
                ),
                arc_known_equality_conflicts=(
                    arc_inventory.known_equality_conflicts
                ),
                arc_known_acceptance_conflicts=(
                    arc_inventory.known_acceptance_conflicts
                ),
                receipt_count=receipt_inventory.receipts,
                receipt_source_occurrences=(
                    receipt_inventory.source_occurrences
                ),
                receipt_derived_slots=receipt_inventory.derived_slots,
                receipt_with_derived_slots=(
                    receipt_inventory.receipts_with_derived_slots
                ),
                receipt_exact_conflicts=(
                    receipt_inventory.exact_conflict_receipts
                ),
                receipt_candidate_eliminations=(
                    receipt_inventory.candidate_eliminations
                ),
                receipt_invalid_eliminations=(
                    receipt_inventory.invalid_candidate_eliminations
                ),
                finite_assignment_found=int(finite_assignment is not None),
                finite_assignment_choices=(
                    0
                    if finite_assignment is None
                    else len(finite_assignment.assignment.choices)
                ),
                finite_assignment_equalities=(
                    0
                    if finite_assignment is None
                    else len(finite_assignment.equalities)
                ),
                finite_assignment_acceptances=(
                    0
                    if finite_assignment is None
                    else len(finite_assignment.acceptances)
                ),
                finite_assignment_nontrivial_paths=(
                    0
                    if finite_assignment is None
                    else sum(
                        bool(discharge.path.steps)
                        for discharge in finite_assignment.acceptances
                    )
                ),
                finite_projected_derived_slots=len(projected_domains),
                finite_local_derived_candidates=sum(
                    len(domain.local_candidate_types)
                    for domain in projected_domains
                ),
                finite_supported_derived_candidates=sum(
                    len(domain.completions)
                    for domain in projected_domains
                ),
                finite_unsupported_derived_candidates=sum(
                    len(domain.globally_unsupported_types)
                    for domain in projected_domains
                ),
                finite_typed_projection_found=int(
                    typed_projection is not None
                ),
                finite_typed_resolved_expressions=(
                    0
                    if typed_projection is None
                    else len(typed_projection.resolved_expressions)
                ),
                finite_typed_applications=(
                    0
                    if typed_projection is None
                    else len(typed_projection.applications)
                ),
                finite_typed_derived_signatures=(
                    0
                    if typed_projection is None
                    else len(typed_projection.derived_signatures)
                ),
                checked_type_of_extensions=int(
                    checked_type_of_extension is not None
                ),
                checked_type_of_occurrence_judgments=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.occurrence_judgments
                ),
                checked_type_of_application_occurrences=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.application_occurrences
                ),
                checked_type_of_application_derivations=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.application_derivations
                ),
                checked_type_of_authored_rule_uses=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.authored_rule_uses
                ),
                checked_type_of_structural_rule_uses=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.structural_rule_uses
                ),
                checked_type_of_extended_rule_uses=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.extended_rule_uses
                ),
                checked_type_of_variable_derivations=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.variable_derivations
                ),
                checked_type_of_logical_derivations=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.logical_derivations
                ),
                checked_type_of_literal_boundaries=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.literal_boundaries
                ),
                checked_type_of_nontrivial_subtype_uses=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.nontrivial_subtype_uses
                ),
                checked_type_of_derived_signatures=(
                    0
                    if checked_type_of_inventory is None
                    else checked_type_of_inventory.derived_signatures
                ),
                finite_typed_negative_relation_absences=(
                    0
                    if typed_negative_projection is None
                    else len(typed_negative_projection.relation_absences)
                ),
                finite_typed_negative_distinct_refutations=(
                    0
                    if typed_negative_projection is None
                    else len(typed_negative_projection.distinct_refutations)
                ),
                finite_typed_negative_unsupported=(
                    0
                    if typed_negative_projection is None
                    else len(typed_negative_projection.unsupported)
                ),
                unanchored_signatures=tuple(
                    (signature.name, signature.arity)
                    for signature in signature_supports
                    if signature.kind
                    == GdlDerivedSignatureSupportKind.UNANCHORED
                ),
                incomparable_signatures=tuple(
                    (signature.name, signature.arity)
                    for signature in signature_supports
                    if signature.kind
                    == GdlDerivedSignatureSupportKind.INCOMPARABLE
                ),
                empty_derived_signatures=tuple(
                    sorted(
                        {
                            (domain.slot.name, domain.slot.arity)
                            for domain in arc_analysis.derived_domains
                            if domain.kind == GdlDerivedDomainKind.EMPTY
                        }
                    )
                ),
            )
        )
    return tuple(games), digest.hexdigest()


def totals(games: tuple[GameAudit, ...]) -> dict[str, int]:
    observed = {
        field: sum(getattr(game, field) for game in games)
        for field in EXPECTED_TOTALS
        if field not in {"missing_applications", "unmatched_authored_names"}
    }
    observed["missing_applications"] = sum(
        len(game.missing_applications) for game in games
    )
    observed["unmatched_authored_names"] = sum(
        len(game.unmatched_authored_names) for game in games
    )
    return observed


def validate(games: tuple[GameAudit, ...], digest: str) -> None:
    if tuple(game.game for game in games) != corpus.GAMES:
        raise PresentationAuditError("game presentation order changed")
    if digest != SOURCE_DIGEST:
        raise PresentationAuditError("game/type presentation digest changed")
    observed_totals = totals(games)
    if observed_totals != EXPECTED_TOTALS:
        raise PresentationAuditError(
            f"presentation inventory changed: {observed_totals}"
        )
    gdl_only = sum(
        game.forms > 0 and game.foreign_code_lines == 0 for game in games
    )
    mixed = sum(
        game.forms > 0 and game.foreign_code_lines > 0 for game in games
    )
    no_live_gdl = tuple(game.game for game in games if game.forms == 0)
    if (
        gdl_only != EXPECTED_GDL_ONLY
        or mixed != EXPECTED_MIXED
        or no_live_gdl != EXPECTED_NO_LIVE_GDL
    ):
        raise PresentationAuditError("source-language coverage changed")
    overloads = tuple(
        (game.game, game.overloads) for game in games if game.overloads
    )
    if overloads != EXPECTED_OVERLOADS:
        raise PresentationAuditError("type-profile overload inventory changed")
    unmatched = tuple(
        (game.game, game.unmatched_authored_names)
        for game in games
        if game.unmatched_authored_names
    )
    if unmatched != EXPECTED_UNMATCHED:
        raise PresentationAuditError(
            "unmatched authored-name inventory changed"
        )
    unanchored = tuple(
        (game.game, game.unanchored_signatures)
        for game in games
        if game.unanchored_signatures
    )
    if unanchored != EXPECTED_UNANCHORED_SIGNATURES:
        raise PresentationAuditError(
            "unanchored derived-signature inventory changed"
        )
    incomparable = tuple(
        (game.game, game.incomparable_signatures)
        for game in games
        if game.incomparable_signatures
    )
    if incomparable != EXPECTED_INCOMPARABLE_SIGNATURES:
        raise PresentationAuditError(
            "incomparable derived-signature inventory changed"
        )
    empty_components = tuple(
        (game.game, game.arc_empty_components)
        for game in games
        if game.arc_empty_components
    )
    if empty_components != EXPECTED_EMPTY_COMPONENTS:
        raise PresentationAuditError(
            "finite existing-type contradiction inventory changed"
        )
    empty_derived = tuple(
        (game.game, game.empty_derived_signatures)
        for game in games
        if game.empty_derived_signatures
    )
    if empty_derived != EXPECTED_EMPTY_DERIVED_SIGNATURES:
        raise PresentationAuditError(
            "empty derived-signature domain inventory changed"
        )
    no_finite_assignment = tuple(
        game.game for game in games if not game.finite_assignment_found
    )
    if no_finite_assignment != EXPECTED_NO_FINITE_ASSIGNMENT:
        raise PresentationAuditError(
            "finite existing-type assignment inventory changed"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--snapshot-root",
        type=Path,
        required=True,
        help="pinned IGGP checkout containing games/ and types/",
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    try:
        games, digest = audit(args.snapshot_root)
        validate(games, digest)
    except PresentationAuditError as exc:
        print(f"FAIL: IGGP presentations: {exc}", file=sys.stderr)
        return 1

    if args.verbose:
        for game in games:
            source_kind = (
                "no-live-gdl"
                if game.forms == 0
                else "mixed"
                if game.foreign_code_lines
                else "gdl-only"
            )
            print(
                f"{game.game}: {source_kind}, {game.forms} forms, "
                f"{len(game.missing_applications)} missing signatures, "
                f"{len(game.unmatched_authored_names)} unmatched names, "
                f"{game.arc_empty_components} empty finite domains"
            )
    print(
        "PASS: 50 IGGP source presentations: 33 GDL-only, 16 mixed, "
        "1 no-live-GDL; 4898 ordered forms / 1811 rules; 124 foreign "
        "code lines retained separately; 32178 occurrence types / 63549 "
        "constraints; 402 negative occurrences remain 397 finite-relation "
        "absence demands plus 5 structural-distinct refutation demands; all "
        "314 negative variable demands retain positive binding evidence; "
        "207/213 derived-signature questions have only single "
        "or subtype-ordered anchors; existing-type arc analysis leaves "
        "390 singleton / 168 multiple / 16 empty derived slots; finite "
        "contradictions retain 105 occurrence-level receipts / 245 replayed "
        "candidate eliminations; 44 games carry total assignments replayed "
        "against 26741 equalities / 26043 directed acceptances, while six "
        "retain local finite obstructions; all 550 locally retained choices "
        "across the 413 satisfiable-game derived slots have complete replay "
        "witnesses; their 44 witness-indexed typed proposals retain 31160 "
        "resolved expressions / 17679 application occurrences / 164 complete "
        "derived signatures; 44 checked lowercase type:of extensions derive "
        "all 26741 source occurrences through 17729 proof-relevant application "
        "rule uses (including 50 retained duplicate-rule alternatives), 8594 "
        "variable uses, and 468 logical uses; 349 ordinary negative demands "
        "acquire argument types but no finite relation-completeness evidence, "
        "and none of these finite results becomes an open-world refutation"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
