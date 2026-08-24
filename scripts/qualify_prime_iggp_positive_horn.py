#!/usr/bin/env python3
"""Qualify the reusable positive-Horn GDL proof kernel on pinned IGGP data."""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_iggp_manifest as corpus  # noqa: E402
from check_prime_iggp_presentations import (  # noqa: E402
    audit as audit_presentations,
    source_path,
    validate as validate_presentations,
)
from generate_prime_iggp_type_source import render_source_package  # noqa: E402
from prime_iggp_generation import (  # noqa: E402
    GenerationError,
    State,
    load_game_states,
    parse_ground_atom,
)
from prime_iggp_presentation import (  # noqa: E402
    GdlFiniteTypedOccurrenceProjection,
    GdlSignatureStatement,
    GdlTypeProfile,
    PresentationError,
    analyze_gdl_existing_type_domains,
    check_gdl_type_of_extension,
    extract_gdl_typing_constraints,
    find_gdl_finite_type_assignment,
    parse_gdl_source_presentation,
    parse_gdl_type_profile,
    project_gdl_finite_typed_occurrences,
)
from prime_iggp_positive_horn import (  # noqa: E402
    PositiveHornBlock,
    PositiveHornBoundary,
    distinct_evidence_blocks,
    episode_fact_blocks,
    episode_term_blocks,
    ground_atom_term,
    decode_gdl_dataset_ground_application,
    decode_gdl_dataset_query_fibres,
    encode_gdl_dataset_application,
    encode_positive_horn_dataset_source,
    gdl_dataset_representation_templates,
    render_package,
    render_term,
    solve_positive_horn_reference,
    structural_positive_horn_source,
    target_query_patterns,
)
from prime_iggp_finite_view import (  # noqa: E402
    FiniteViewProgram,
    construct_finite_state_view,
    construct_structural_finite_state_view,
    encode_finite_view_dataset_source,
    structural_finite_view_source,
)


DEFAULT_DEPTH = 32
DEFAULT_MAX_STATES = 50_000_000
DEFAULT_MAX_OCCURRENCES = 1_000_000


def _resolved_type_map(
    projection: GdlFiniteTypedOccurrenceProjection,
) -> dict[Any, str]:
    return {
        resolved.expression: resolved.type_name
        for resolved in projection.resolved_expressions
    }


def _finite_distinct_terms(
    profile: GdlTypeProfile,
    analysis: Any,
    constraints: Any,
    projection: GdlFiniteTypedOccurrenceProjection,
) -> tuple[str, ...]:
    """Return a checked nullary-complete domain for structural distinct."""

    resolved = _resolved_type_map(projection)
    operand_types: list[str] = []
    for logical in constraints.logical_forms:
        if logical.operator != "distinct":
            continue
        if len(logical.operands) != 2:
            raise PositiveHornBoundary(
                "structural distinct does not have exactly two operands"
            )
        for operand in logical.operands:
            type_name = resolved.get(operand)
            if type_name is None:
                raise PositiveHornBoundary(
                    "structural distinct operand lacks checked typing"
                )
            if type_name not in operand_types:
                operand_types.append(type_name)
    if not operand_types:
        return ()

    terms: list[str] = []
    open_constructors: list[str] = []
    for statement in profile.signatures:
        accepted = any(
            analysis.universe.accepts(
                statement.result_type, operand_type
            )
            for operand_type in operand_types
        )
        if not accepted:
            continue
        if statement.argument_types:
            open_constructors.extend(statement.names)
            continue
        for name in statement.names:
            if name not in terms:
                terms.append(name)
    if open_constructors:
        raise PositiveHornBoundary(
            "structural distinct type has non-nullary constructors: "
            + ", ".join(open_constructors)
        )
    if not terms:
        raise PositiveHornBoundary(
            "structural distinct has no finite typed ground domain"
        )
    return tuple(terms)


def _term_signature(term: Any) -> tuple[str, int]:
    if isinstance(term, str):
        return term, 0
    if (
        not isinstance(term, tuple)
        or not term
        or not isinstance(term[0], str)
    ):
        raise PositiveHornBoundary(
            "dataset answer has no relation signature"
        )
    return term[0], len(term) - 1


def _ground_atom_text(term: Any, *, argument: bool = False) -> str:
    if isinstance(term, str):
        return term if argument else f"{term}()"
    if not isinstance(term, tuple) or not term:
        raise PositiveHornBoundary("dataset answer is malformed")
    return (
        str(term[0])
        + "("
        + ", ".join(
            _ground_atom_text(item, argument=True) for item in term[1:]
        )
        + ")"
    )


def _episode_receipt(
    game: str, ordinal: int, states: Iterable[State]
) -> tuple[Any, ...]:
    members = tuple(state.episode for state in states)
    digest = hashlib.sha256("\0".join(members).encode("utf-8")).hexdigest()
    return (
        "gdl:episode-equivalence",
        game,
        f"state-{ordinal:05d}",
        digest,
        ("gdl:member-occurrences", *members),
    )


def _observe_helpers(
    depth: int, max_states: int, max_occurrences: int
) -> str:
    return "\n".join(
        (
            "(= (gdl:proof-goal",
            "      (quote (gdl:rule $source $authored",
            "        (gdl:dataset-representation $goal) $premises)))",
            "   $goal)",
            "(= (gdl:proof-goal",
            "      (quote (gdl:fact $source $authored",
            "        (gdl:dataset-representation $goal) $premises)))",
            "   $goal)",
            "(= (gdl:proof-goal",
            "      (quote (gdl:episode-fact $source $goal)))",
            "   $goal)",
            "(= (gdl:proof-goal",
            "      (quote (gdl:distinct-proof $source $left $right)))",
            "   (distinct $left $right))",
            "(= (gdl:observe $artifact $name (quote $query))",
            "  (let",
            "    (compile-result proof-occurrence-bag",
            "      $occurrences $metrics $revision)",
            "    (compile:run",
            f"      $artifact {depth} {max_states} {max_occurrences}",
            "      (quote $query))",
            "    (gdl:answers $name",
            "      (collapse",
            "        (let (occurrence $proof) (superpose $occurrences)",
            "          (gdl:proof-goal $proof))))))",
            "(= (gdl:observe-proofs $artifact $name (quote $query))",
            "  (let",
            "    (compile-result proof-occurrence-bag",
            "      $occurrences $metrics $revision)",
            "    (compile:run",
            f"      $artifact {depth} {max_states} {max_occurrences}",
            "      (quote $query))",
            "    (gdl:proofs $name",
            "      (collapse",
            "        (let (occurrence (quote $proof))",
            "          (superpose $occurrences) $proof)))))",
        )
    )


def _expected_line(
    observations: Iterable[tuple[str, str, tuple[Any, ...]]]
) -> str:
    rendered = []
    for constructor, label, values in observations:
        bag = "(" + " ".join(render_term(value) for value in values) + ")"
        rendered.append(f"({constructor} {label} {bag})")
    return "[" + ", ".join(rendered) + "]\n"


def _checked_positive_program(
    snapshot_root: Path, game: str, *, structural: bool = False
) -> tuple[tuple[PositiveHornBlock, ...], str, str]:
    source_bytes = source_path(snapshot_root, game).read_bytes()
    profile_bytes = (snapshot_root / "types" / f"{game}.typ").read_bytes()
    source = parse_gdl_source_presentation(source_bytes.decode("utf-8"))
    profile = parse_gdl_type_profile(profile_bytes.decode("utf-8"))
    constraints = extract_gdl_typing_constraints(source, profile)
    analysis = analyze_gdl_existing_type_domains(constraints, profile)
    assignment = find_gdl_finite_type_assignment(constraints, analysis)
    if assignment is None:
        raise PositiveHornBoundary(
            "authored source has no complete finite typing witness"
        )
    projection = project_gdl_finite_typed_occurrences(
        constraints, analysis, assignment.assignment
    )
    check_gdl_type_of_extension(constraints, profile, projection)
    program = (
        structural_positive_horn_source(source)
        if structural
        else encode_positive_horn_dataset_source(source)
    )
    distinct_terms = _finite_distinct_terms(
        profile, analysis, constraints, projection
    )
    source_digest = hashlib.sha256(source_bytes).hexdigest()
    profile_digest = hashlib.sha256(profile_bytes).hexdigest()
    receipt = (
        "gdl:typed-distinct-domain",
        game,
        source_digest,
        profile_digest,
    )
    distinct = distinct_evidence_blocks(receipt, distinct_terms)
    return program.blocks + distinct, source_digest, profile_digest


def _checked_finite_program(
    snapshot_root: Path, game: str, *, structural: bool = False
) -> tuple[FiniteViewProgram, tuple[PositiveHornBlock, ...], str, str]:
    source_bytes = source_path(snapshot_root, game).read_bytes()
    profile_bytes = (snapshot_root / "types" / f"{game}.typ").read_bytes()
    source = parse_gdl_source_presentation(source_bytes.decode("utf-8"))
    profile = parse_gdl_type_profile(profile_bytes.decode("utf-8"))
    constraints = extract_gdl_typing_constraints(source, profile)
    analysis = analyze_gdl_existing_type_domains(constraints, profile)
    assignment = find_gdl_finite_type_assignment(constraints, analysis)
    if assignment is None:
        raise PositiveHornBoundary(
            "authored source has no complete finite typing witness"
        )
    projection = project_gdl_finite_typed_occurrences(
        constraints, analysis, assignment.assignment
    )
    check_gdl_type_of_extension(constraints, profile, projection)
    program = (
        structural_finite_view_source(source)
        if structural
        else encode_finite_view_dataset_source(source)
    )
    distinct_terms = _finite_distinct_terms(
        profile, analysis, constraints, projection
    )
    source_digest = hashlib.sha256(source_bytes).hexdigest()
    profile_digest = hashlib.sha256(profile_bytes).hexdigest()
    receipt = (
        "gdl:typed-distinct-domain",
        game,
        source_digest,
        profile_digest,
    )
    distinct = distinct_evidence_blocks(receipt, distinct_terms)
    return program, distinct, source_digest, profile_digest


def _state_groups(states: Iterable[State]) -> tuple[tuple[State, ...], ...]:
    groups: dict[tuple[str, ...], list[State]] = {}
    for state in states:
        groups.setdefault(state.background, []).append(state)
    return tuple(tuple(group) for group in groups.values())


def _signature_queries(
    group: tuple[State, ...],
    dataset_queries: dict[tuple[str, str], tuple[Any, ...]],
) -> tuple[Any, ...]:
    result: list[Any] = []
    for state in group:
        for query in dataset_queries[(state.target, state.split)]:
            signature = _term_signature(query)
            if all(_term_signature(existing) != signature for existing in result):
                result.append(query)
    return tuple(result)


def _validate_state_labels(
    group: tuple[State, ...],
    answers_by_signature: dict[tuple[str, int], tuple[Any, ...]],
    dataset_queries: dict[tuple[str, str], tuple[Any, ...]],
) -> None:
    for state in group:
        signatures = {
            _term_signature(query)
            for query in dataset_queries[(state.target, state.split)]
        }
        observed = {
            _ground_atom_text(answer)
            for signature in signatures
            for answer in answers_by_signature.get(signature, ())
        }
        expected_terms = {
            ground_atom_term(parse_ground_atom(value))
            for value in state.positives
        }
        expected = {_ground_atom_text(value) for value in expected_terms}
        if observed != expected:
            missing = sorted(expected - observed)
            extra = sorted(observed - expected)
            raise GenerationError(
                f"{state.episode}: GDL source and labels disagree; "
                f"missing={missing[:8]} extra={extra[:8]}"
            )


def build_workload(
    snapshot_root: Path,
    repo: Path,
    game: str,
    *,
    depth: int,
    max_states: int,
    max_occurrences: int,
) -> tuple[str, bytes, dict[str, int]]:
    base, source_digest, profile_digest = _checked_positive_program(
        snapshot_root, game
    )
    states = load_game_states(snapshot_root, repo, game, f"gdl:{game}")
    groups = _state_groups(states)
    dataset_queries: dict[tuple[str, str], tuple[Any, ...]] = {}
    for state in states:
        dataset = (state.target, state.split)
        if dataset not in dataset_queries:
            dataset_queries[dataset] = target_query_patterns(state.atoms)

    base_revision = (
        f"gdl-positive-{game}-{source_digest[:12]}-{profile_digest[:12]}"
    )
    workload = [
        f"(= (gdl:positive-base-{game})",
        f"  (compile:rule-package {base_revision}",
        "    " + render_package(base).replace("\n", "\n    ") + "))",
        _observe_helpers(depth, max_states, max_occurrences),
    ]
    expected = bytearray()
    total_queries = 0
    total_proofs = 0
    total_episode_facts = 0
    maximum_reference_states = 0
    maximum_reference_attempts = 0
    proof_canaries = 0

    for ordinal, group in enumerate(groups, 1):
        receipt = _episode_receipt(game, ordinal, group)
        delta = episode_fact_blocks(receipt, group[0].background)
        total_episode_facts += len(delta)
        queries = _signature_queries(group, dataset_queries)
        blocks = base + delta
        observations: list[tuple[str, str, tuple[Any, ...]]] = []
        answers_by_signature: dict[tuple[str, int], tuple[Any, ...]] = {}
        calls: list[str] = []
        for query_ordinal, query in enumerate(queries, 1):
            run = solve_positive_horn_reference(
                blocks,
                query,
                depth=depth,
                max_states=max_states,
                max_occurrences=max_occurrences,
            )
            answers = tuple(answer.conclusion for answer in run.answers)
            signature = _term_signature(query)
            answers_by_signature[signature] = answers
            label = f"gdl:{game}:group-{ordinal:05d}:q-{query_ordinal:02d}"
            observations.append(("gdl:answers", label, answers))
            calls.append(
                f"(gdl:observe $artifact {label} "
                f"(quote {render_term(query)}))"
            )
            total_queries += 1
            total_proofs += len(answers)
            maximum_reference_states = max(
                maximum_reference_states, run.states
            )
            maximum_reference_attempts = max(
                maximum_reference_attempts, run.block_attempts
            )
            if (
                proof_canaries == 0
                and 0 < len(run.answers) <= 8
                and any(
                    isinstance(answer.proof, tuple)
                    and answer.proof
                    and answer.proof[0] == "gdl:rule"
                    for answer in run.answers
                )
            ):
                proof_label = f"gdl:{game}:proof-canary"
                calls.append(
                    f"(gdl:observe-proofs $artifact {proof_label} "
                    f"(quote {render_term(query)}))"
                )
                observations.append(
                    (
                        "gdl:proofs",
                        proof_label,
                        tuple(answer.proof for answer in run.answers),
                    )
                )
                proof_canaries += 1
        _validate_state_labels(group, answers_by_signature, dataset_queries)
        revision = f"gdl-{game}-state-{ordinal:05d}"
        workload.extend(
            (
                "!(let $artifact",
                "  (compile:link-package",
                f"    (gdl:positive-base-{game}) {revision}",
                "    " + render_package(delta).replace("\n", "\n    ") + ")",
                "  (superpose",
                "    (" + "\n     ".join(calls) + ")))",
            )
        )
        expected.extend(_expected_line(observations).encode("utf-8"))
        if ordinal % 500 == 0 or ordinal == len(groups):
            print(
                f"QUALIFY-BUILD: IGGP {game} state fibres "
                f"{ordinal}/{len(groups)}",
                file=sys.stderr,
                flush=True,
            )

    workload_text = "\n".join(workload) + "\n"
    return workload_text, bytes(expected), {
        "source_blocks": len(base),
        "state_occurrences": len(states),
        "state_fibres": len(groups),
        "candidate_cases": sum(len(state.atoms) for state in states),
        "positive_labels": sum(len(state.positives) for state in states),
        "episode_facts": total_episode_facts,
        "queries": total_queries,
        "proof_occurrences": total_proofs,
        "maximum_reference_states": maximum_reference_states,
        "maximum_reference_attempts": maximum_reference_attempts,
        "proof_canaries": proof_canaries,
    }


def build_native_workload(
    snapshot_root: Path,
    repo: Path,
    game: str,
    *,
    depth: int,
    max_states: int,
    max_occurrences: int,
    finite_view: bool = False,
) -> tuple[str, bytes, dict[str, int]]:
    """Build ordinary data for the direct-C Space-hosted proof kernel."""

    finite_program: FiniteViewProgram | None = None
    if finite_view:
        finite_program, distinct, _, _ = _checked_finite_program(
            snapshot_root, game, structural=True
        )
        base = finite_program.blocks + distinct
    else:
        base, _, _ = _checked_positive_program(
            snapshot_root, game, structural=True
        )
    source_bytes = source_path(snapshot_root, game).read_bytes()
    profile_bytes = (snapshot_root / "types" / f"{game}.typ").read_bytes()
    presentation = parse_gdl_source_presentation(
        source_bytes.decode("utf-8")
    )
    lifting_templates = gdl_dataset_representation_templates(presentation)
    states = load_game_states(snapshot_root, repo, game, f"gdl:{game}")
    groups = _state_groups(states)
    dataset_queries: dict[tuple[str, str], tuple[Any, ...]] = {}
    for state in states:
        dataset = (state.target, state.split)
        if dataset not in dataset_queries:
            dataset_queries[dataset] = target_query_patterns(state.atoms)

    workload = [render_source_package(source_bytes, profile_bytes).rstrip()]
    expected = bytearray()
    total_queries = 0
    total_proofs = 0
    total_episode_facts = 0
    maximum_reference_states = 0
    maximum_reference_attempts = 0
    proof_canaries = 0

    for ordinal, group in enumerate(groups, 1):
        receipt = _episode_receipt(game, ordinal, group)
        facts = tuple(
            decode_gdl_dataset_ground_application(
                lifting_templates,
                ground_atom_term(parse_ground_atom(value)),
            )
            for value in group[0].background
        )
        reference_delta = (
            construct_structural_finite_state_view(
                finite_program, receipt, facts
            ).blocks
            if finite_program is not None
            else episode_term_blocks(receipt, facts)
        )
        total_episode_facts += len(facts)
        queries = _signature_queries(group, dataset_queries)
        blocks = base + reference_delta
        observations: list[tuple[str, str, tuple[Any, ...]]] = []
        answers_by_signature_lists: defaultdict[
            tuple[str, int], list[Any]
        ] = defaultdict(list)
        rendered_queries: list[str] = []
        for query_ordinal, query in enumerate(queries, 1):
            signature = _term_signature(query)
            structural_queries = decode_gdl_dataset_query_fibres(
                lifting_templates, query
            )
            for shape_ordinal, structural_query in enumerate(
                structural_queries, 1
            ):
                run = solve_positive_horn_reference(
                    blocks,
                    structural_query,
                    depth=depth,
                    max_states=max_states,
                    max_occurrences=max_occurrences,
                )
                structural_answers = tuple(
                    answer.conclusion for answer in run.answers
                )
                represented_answers = tuple(
                    encode_gdl_dataset_application(answer)
                    for answer in structural_answers
                )
                answers_by_signature_lists[signature].extend(
                    represented_answers
                )
                label = (
                    f"gdl:{game}:group-{ordinal:05d}:"
                    f"q-{query_ordinal:02d}:shape-{shape_ordinal:02d}"
                )
                observations.append(
                    ("gdl:answers", label, structural_answers)
                )
                proof_canary = (
                    proof_canaries == 0
                    and 0 < len(run.answers) <= 8
                    and any(
                        isinstance(answer.proof, tuple)
                        and answer.proof
                        and answer.proof[0] == "gdl:rule"
                        for answer in run.answers
                    )
                )
                if proof_canary:
                    proof_canaries += 1
                mode = "proof-canary" if proof_canary else "answers-only"
                rendered_queries.append(
                    f"    (query {label} {render_term(structural_query)} "
                    f"{mode})"
                )
                total_queries += 1
                total_proofs += len(structural_answers)
                maximum_reference_states = max(
                    maximum_reference_states, run.states
                )
                maximum_reference_attempts = max(
                    maximum_reference_attempts, run.block_attempts
                )
        answers_by_signature = {
            signature: tuple(answers)
            for signature, answers in answers_by_signature_lists.items()
        }
        _validate_state_labels(group, answers_by_signature, dataset_queries)
        workload.extend(
            (
                "(gdl-finite-view-native-episode-v1"
                if finite_view
                else "(gdl-positive-horn-native-episode-v1",
                f"  {render_term(receipt)}",
                "  (facts",
                *(f"    {render_term(fact)}" for fact in facts),
                "  )",
                "  (queries",
                *rendered_queries,
                "  ))",
            )
        )
        expected.extend(_expected_line(observations).encode("utf-8"))
        if ordinal % 500 == 0 or ordinal == len(groups):
            print(
                f"QUALIFY-BUILD: IGGP {game} native state fibres "
                f"{ordinal}/{len(groups)}",
                file=sys.stderr,
                flush=True,
            )

    return "\n".join(workload) + "\n", bytes(expected), {
        "source_blocks": len(base),
        "state_occurrences": len(states),
        "state_fibres": len(groups),
        "candidate_cases": sum(len(state.atoms) for state in states),
        "positive_labels": sum(len(state.positives) for state in states),
        "episode_facts": total_episode_facts,
        "queries": total_queries,
        "proof_occurrences": total_proofs,
        "maximum_reference_states": maximum_reference_states,
        "maximum_reference_attempts": maximum_reference_attempts,
        "proof_canaries": proof_canaries,
    }


def _run_memfd(runner: Path, workload: str) -> subprocess.CompletedProcess[bytes]:
    if hasattr(os, "memfd_create"):
        descriptor = os.memfd_create("cetta-prime-iggp-positive-horn")
    else:
        anonymous_root = runner.parent
        descriptor = os.open(
            anonymous_root,
            os.O_TMPFILE | os.O_RDWR,
            0o600,
        )
    try:
        payload = workload.encode("utf-8")
        with os.fdopen(os.dup(descriptor), "wb", closefd=True) as stream:
            stream.write(payload)
        os.lseek(descriptor, 0, os.SEEK_SET)
        return subprocess.run(
            (str(runner), "--lang", "prime", f"/proc/self/fd/{descriptor}"),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            pass_fds=(descriptor,),
        )
    finally:
        os.close(descriptor)


def _run_native_memfd(
    runner: Path,
    workload: str,
    *,
    depth: int,
    max_states: int,
    max_occurrences: int,
) -> subprocess.CompletedProcess[bytes]:
    if hasattr(os, "memfd_create"):
        descriptor = os.memfd_create(
            "cetta-prime-iggp-positive-horn-native"
        )
    else:
        anonymous_root = runner.parent
        descriptor = os.open(
            anonymous_root,
            os.O_TMPFILE | os.O_RDWR,
            0o600,
        )
    try:
        payload = workload.encode("utf-8")
        with os.fdopen(os.dup(descriptor), "wb", closefd=True) as stream:
            stream.write(payload)
        os.lseek(descriptor, 0, os.SEEK_SET)
        return subprocess.run(
            (
                str(runner),
                f"/proc/self/fd/{descriptor}",
                str(depth),
                str(max_states),
                str(max_occurrences),
            ),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            pass_fds=(descriptor,),
        )
    finally:
        os.close(descriptor)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    runner_group = parser.add_mutually_exclusive_group(required=True)
    runner_group.add_argument("--runner", type=Path)
    runner_group.add_argument("--native-runner", type=Path)
    parser.add_argument("--game", action="append", choices=corpus.GAMES)
    parser.add_argument("--depth", type=int, default=DEFAULT_DEPTH)
    parser.add_argument(
        "--max-states", type=int, default=DEFAULT_MAX_STATES
    )
    parser.add_argument(
        "--max-occurrences", type=int, default=DEFAULT_MAX_OCCURRENCES
    )
    args = parser.parse_args()

    runner = args.native_runner or args.runner
    if not runner or not runner.is_file():
        print("FAIL: positive-Horn runner is missing", file=sys.stderr)
        return 2
    try:
        audits, digest = audit_presentations(args.snapshot_root)
        validate_presentations(audits, digest)
        type_covered = {
            audit.game
            for audit in audits
            if audit.checked_type_of_extensions == 1
            and audit.checked_type_of_occurrence_judgments != 0
        }
        requested = tuple(args.game or corpus.GAMES)
        selected: list[tuple[str, str]] = []
        outside: list[tuple[str, str]] = []
        for game in requested:
            if game not in type_covered:
                outside.append((game, "no complete finite typing witness"))
                continue
            try:
                _checked_positive_program(args.snapshot_root, game)
            except PositiveHornBoundary as exc:
                if not args.native_runner:
                    outside.append((game, str(exc)))
                    continue
                positive_reason = str(exc)
                try:
                    _checked_finite_program(args.snapshot_root, game)
                except PositiveHornBoundary as finite_exc:
                    outside.append(
                        (
                            game,
                            f"positive-Horn: {positive_reason}; "
                            f"finite-view: {finite_exc}",
                        )
                    )
                    continue
                selected.append((game, "finite-view"))
                continue
            selected.append((game, "positive-horn"))

        totals: defaultdict[str, int] = defaultdict(int)
        mode_counts: defaultdict[str, int] = defaultdict(int)
        for ordinal, (game, mode) in enumerate(selected, 1):
            workload_builder = (
                build_native_workload
                if args.native_runner
                else build_workload
            )
            workload_arguments = {
                "depth": args.depth,
                "max_states": args.max_states,
                "max_occurrences": args.max_occurrences,
            }
            if args.native_runner:
                workload_arguments["finite_view"] = mode == "finite-view"
            workload, expected, stats = workload_builder(
                args.snapshot_root, repo, game, **workload_arguments
            )
            print(
                f"QUALIFY-RUN: IGGP {game} mode={mode} executing "
                f"{stats['queries']} whole-answer queries",
                file=sys.stderr,
                flush=True,
            )
            completed = (
                _run_native_memfd(
                    runner.resolve(),
                    workload,
                    depth=args.depth,
                    max_states=args.max_states,
                    max_occurrences=args.max_occurrences,
                )
                if args.native_runner
                else _run_memfd(runner.resolve(), workload)
            )
            if completed.returncode != 0:
                if completed.stderr:
                    print(
                        completed.stderr.decode("utf-8", errors="replace"),
                        end="",
                        file=sys.stderr,
                    )
                print(
                    f"FAIL: IGGP {game} {mode} execution "
                    f"({ordinal}/{len(selected)})",
                    file=sys.stderr,
                )
                return completed.returncode
            if args.native_runner and completed.stderr:
                print(
                    completed.stderr.decode("utf-8", errors="replace"),
                    end="",
                    file=sys.stderr,
                )
            if completed.stdout != expected:
                mismatch = next(
                    (
                        index
                        for index, (left, right) in enumerate(
                            zip(completed.stdout, expected)
                        )
                        if left != right
                    ),
                    min(len(completed.stdout), len(expected)),
                )
                print(
                    f"FAIL: IGGP {game} ordered proof bag differs at "
                    f"output byte {mismatch}; observed={len(completed.stdout)} "
                    f"expected={len(expected)}",
                    file=sys.stderr,
                )
                return 1
            print(
                f"PrimeIggpPositiveHorn"
                f"{'Native' if args.native_runner else ''}Game "
                f"game={game} mode={mode} "
                + " ".join(f"{key}={value}" for key, value in stats.items())
            )
            mode_counts[mode] += 1
            for key, value in stats.items():
                totals[key] += value

        for game, reason in outside:
            print(f"OUTSIDE-FRAGMENT: IGGP {game}: {reason}")
        print(
            f"PrimeIggpPositiveHorn"
            f"{'Native' if args.native_runner else ''}Summary "
            f"covered_games={len(selected)} "
            f"outside_fragment_games={len(outside)} "
            f"positive_horn_games={mode_counts['positive-horn']} "
            f"finite_view_games={mode_counts['finite-view']} "
            + " ".join(f"{key}={value}" for key, value in totals.items())
        )
        return 0
    except (
        GenerationError,
        OSError,
        PresentationError,
        PositiveHornBoundary,
        UnicodeDecodeError,
    ) as exc:
        print(f"FAIL: IGGP positive-Horn qualification: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
