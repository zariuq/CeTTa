#!/usr/bin/env python3
"""Qualify typed native stratified GDL episodes on the pinned IGGP tasks."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass, field
import hashlib
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from check_prime_iggp_presentations import (  # noqa: E402
    PresentationAuditError,
    audit as audit_presentations,
    task_source_path,
    validate as validate_presentations,
)
from generate_prime_iggp_type_source import render_source_package  # noqa: E402
from prime_iggp_finite_herbrand import render_term  # noqa: E402
from prime_iggp_episode_evidence import (  # noqa: E402
    EVIDENCE_PATH,
    EpisodeEvidenceError,
    expected_corpus_contradictions,
    expected_game_totals,
    load_episode_evidence,
    validate_episode_evidence,
)
from prime_iggp_generation import (  # noqa: E402
    GenerationError,
    State,
    load_game_states,
    parse_gdl,
    parse_ground_atom,
)
from prime_iggp_positive_horn import (  # noqa: E402
    PositiveHornBoundary,
    decode_gdl_dataset_ground_application,
    decode_gdl_dataset_target_query_fibres,
    encode_gdl_dataset_ground_applications,
    gdl_dataset_constructor_views,
    gdl_dataset_representation_templates,
    ground_atom_term,
    target_query_patterns,
)
from prime_iggp_presentation import (  # noqa: E402
    GdlSourcePresentation,
    PresentationError,
    STANDARD_GDL_ARITIES,
    parse_gdl_source_presentation,
    parse_gdl_type_profile,
)
from prime_iggp_stratification import (  # noqa: E402
    NegativeDependencyCycle,
    RelationSignature,
    TargetDependencySlice,
    relation_signature,
)
from prime_iggp_stratified_model import (  # noqa: E402
    StratifiedModelBoundary,
    canonical_ground_literal,
    construct_stratified_model_basis,
    construct_stratified_model_from_basis,
)


@dataclass
class NativeEpisode:
    outcome: str
    stats: dict[str, int]
    digest: str
    supports: dict[tuple[int, int, Any], int] = field(default_factory=dict)
    support_indices: set[int] = field(default_factory=set)
    episode_occurrences: list[str] = field(default_factory=list)
    authored_edges: int | None = None
    episode_edges: int | None = None


@dataclass(frozen=True)
class NativeEpisodeRun:
    source_outcome: str
    selection_kind: int
    selection_eligible: int
    selection_frontier: int
    target_name: str | None
    target_arity: int
    target_source_forms: int
    target_selected_forms: int
    target_reachable_relations: int
    target_external_relations: int
    episodes: tuple[NativeEpisode, ...]


@dataclass(frozen=True)
class CorpusContradictionReceipt:
    game: str
    evaluated_tasks: int
    established_tasks: int
    contradiction_tasks: int
    evaluated_states: int
    evaluated_episodes: int
    typing_proofs: int
    supports: int
    proof_edges: int
    contradiction_states: int
    missing_labels: int
    extra_labels: int
    target_states: tuple[tuple[str, int], ...]
    digest: str
    first_episode: str
    first_missing: tuple[str, ...]
    first_extra: tuple[str, ...]


class CorpusLabelContradiction(RuntimeError):
    """The pinned task labels conflict with independently checked semantics."""

    def __init__(self, receipt: CorpusContradictionReceipt):
        super().__init__(
            f"{receipt.game}: {receipt.contradiction_states} states carry "
            "labels inconsistent with the authored source"
        )
        self.receipt = receipt


EPISODE_STAT_NAMES = (
    "authored_facts",
    "typing_proof_occurrences",
    "seeded_support_nodes",
    "seeded_proof_edges",
    "support_nodes",
    "proof_edges",
    "ground_instances",
    "distinct_checks",
    "absence_receipts",
    "completed_strata",
)


def _parse_structural_term(text: str) -> Any:
    forms = parse_gdl(text)
    if len(forms) != 1:
        raise ValueError("native structural term does not parse uniquely")
    return canonical_ground_literal(forms[0])


def parse_native_output(output: bytes) -> NativeEpisodeRun:
    try:
        lines = output.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise ValueError("native episode output is not UTF-8") from exc
    if not lines:
        raise ValueError("native episode output is empty")
    header = lines[0].split("\t")
    if len(header) != 13 or header[0] != "GdlStratifiedEpisodesV2":
        raise ValueError("native episode header is malformed")
    try:
        expected_episodes = int(header[2])
        selection_kind = int(header[3])
        selection_eligible = int(header[4])
        selection_frontier = int(header[5])
        int(header[6])
        target_arity = int(header[8])
        target_source_forms = int(header[9])
        target_selected_forms = int(header[10])
        target_reachable_relations = int(header[11])
        target_external_relations = int(header[12])
    except ValueError as exc:
        raise ValueError("native episode header has malformed counts") from exc
    target_name = None if header[7] == "-" else header[7]
    if target_name is None and any(
        (
            target_arity,
            target_source_forms,
            target_selected_forms,
            target_reachable_relations,
            target_external_relations,
        )
    ):
        raise ValueError("untargeted native episode run carries a target receipt")
    if target_name is not None and header[1] == "Established" and (
        target_source_forms <= 0
        or target_selected_forms <= 0
        or target_selected_forms > target_source_forms
        or target_reachable_relations <= 0
    ):
        raise ValueError("targeted native episode run has an invalid slice receipt")

    episodes: dict[int, NativeEpisode] = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if not fields:
            continue
        try:
            ordinal = int(fields[1])
        except (IndexError, ValueError) as exc:
            raise ValueError("native episode row has no ordinal") from exc
        if fields[0] == "E":
            if len(fields) != 16 or ordinal in episodes:
                raise ValueError("native episode result row is malformed")
            try:
                stats = {
                    name: int(value)
                    for name, value in zip(EPISODE_STAT_NAMES, fields[3:13])
                }
                stats["query_count"] = int(fields[13])
                stats["emitted_supports"] = int(fields[14])
            except ValueError as exc:
                raise ValueError("native episode statistics are malformed") from exc
            episodes[ordinal] = NativeEpisode(fields[2], stats, fields[15])
            continue
        episode = episodes.get(ordinal)
        if episode is None:
            raise ValueError("native episode detail precedes its result")
        if fields[0] == "S":
            if len(fields) != 7:
                raise ValueError("native support row is malformed")
            try:
                index = int(fields[2])
                relation = int(fields[3])
                stratum = int(fields[4])
                proof_edges = int(fields[5])
            except ValueError as exc:
                raise ValueError("native support row has malformed numbers") from exc
            literal = _parse_structural_term(fields[6])
            key = relation, stratum, literal
            if index in episode.support_indices or key in episode.supports:
                raise ValueError("native support identity is repeated")
            episode.support_indices.add(index)
            episode.supports[key] = proof_edges
        elif fields[0] == "P":
            if len(fields) != 5:
                raise ValueError("native episode-proof row is malformed")
            int(fields[2])
            int(fields[3])
            episode.episode_occurrences.append(fields[4])
        elif fields[0] == "O":
            if len(fields) != 4 or episode.authored_edges is not None:
                raise ValueError("native proof-origin row is malformed")
            episode.authored_edges = int(fields[2])
            episode.episode_edges = int(fields[3])
        else:
            raise ValueError("native episode output has an unknown row")

    if sorted(episodes) != list(range(1, expected_episodes + 1)):
        raise ValueError("native episode ordinals are incomplete")
    for episode in episodes.values():
        stats = episode.stats
        if episode.outcome in {"Established", "Incomplete"}:
            if len(episode.supports) != stats["emitted_supports"]:
                raise ValueError("native emitted supports disagree with statistics")
            if stats["query_count"] == 0:
                if len(episode.supports) != stats["support_nodes"]:
                    raise ValueError("native support rows disagree with statistics")
                if sum(episode.supports.values()) != stats["proof_edges"]:
                    raise ValueError(
                        "native support proof counts disagree with graph"
                    )
            if episode.authored_edges is None or episode.episode_edges is None:
                raise ValueError("native proof origins were not reported")
            if episode.authored_edges + episode.episode_edges != stats["proof_edges"]:
                raise ValueError("native proof origins do not partition the graph")
            if episode.episode_edges != stats["seeded_proof_edges"]:
                raise ValueError("native episode proof receipt disagrees with graph")
            if len(episode.episode_occurrences) != episode.episode_edges:
                raise ValueError("native episode occurrence bag is incomplete")
        elif episode.supports or episode.episode_occurrences:
            raise ValueError("non-model outcome unexpectedly carries a model")
    return NativeEpisodeRun(
        header[1], selection_kind, selection_eligible,
        selection_frontier,
        target_name, target_arity, target_source_forms,
        target_selected_forms, target_reachable_relations,
        target_external_relations,
        tuple(episodes[index] for index in sorted(episodes)),
    )


def _state_groups(states: Iterable[State]) -> tuple[tuple[State, ...], ...]:
    groups: dict[tuple[str, ...], list[State]] = {}
    for state in states:
        groups.setdefault(state.background, []).append(state)
    return tuple(tuple(group) for group in groups.values())


def _lift_dataset_terms(
    values: Iterable[str],
    templates: object,
    carrier: object,
) -> tuple[Any, ...]:
    lifted = []
    for value in values:
        try:
            lifted.append(
                decode_gdl_dataset_ground_application(
                    templates,
                    ground_atom_term(parse_ground_atom(value)),
                    carrier=carrier,
                )
            )
        except PositiveHornBoundary as exc:
            raise PositiveHornBoundary(f"{value}: {exc}") from exc
    return tuple(lifted)


def _project_dataset_terms_to_target(
    values: Iterable[str],
    full_templates: object,
    carrier: object,
    target_slice: TargetDependencySlice,
    external_patterns: tuple[Any, ...],
) -> tuple[tuple[Any, ...], int]:
    """Project external facts through the proved target dependency fibre.

    The full authored presentation first determines each dataset atom's
    structural meaning.  Only relations outside the dependency closure are
    omitted.  The target model then checks each retained structural fact at
    its typed ingress; interface declarations such as ``base`` and ``input``
    need not masquerade as executable rules in the selected slice.
    """

    external = frozenset(target_slice.external_relations)
    retained: list[Any] = []
    omitted = 0
    for value in values:
        dataset_term = ground_atom_term(parse_ground_atom(value))
        if (
            isinstance(dataset_term, tuple)
            and dataset_term
            and dataset_term[0] == "distinct"
        ):
            omitted += 1
            continue
        try:
            full = canonical_ground_literal(
                decode_gdl_dataset_ground_application(
                    full_templates, dataset_term, carrier=carrier
                )
            )
        except PositiveHornBoundary as exc:
            raise PositiveHornBoundary(f"{value}: {exc}") from exc
        if relation_signature(full) not in external or not any(
            _authored_pattern_matches(pattern, full)
            for pattern in external_patterns
        ):
            omitted += 1
            continue
        retained.append(full)
    return tuple(retained), omitted


def _canonical_authored_pattern(term: Any) -> Any:
    if isinstance(term, str):
        if term.startswith("?"):
            return term
        return int(term) if term.lstrip("-").isdigit() else term
    if isinstance(term, tuple):
        return tuple(_canonical_authored_pattern(item) for item in term)
    return term


def _authored_pattern_matches(pattern: Any, ground: Any) -> bool:
    bindings: dict[str, Any] = {}

    def visit(left: Any, right: Any) -> bool:
        if isinstance(left, str) and left.startswith("?"):
            if left not in bindings:
                bindings[left] = right
                return True
            return bindings[left] == right
        if isinstance(left, tuple):
            return (
                isinstance(right, tuple)
                and len(left) == len(right)
                and all(visit(a, b) for a, b in zip(left, right))
            )
        return left == right

    return visit(pattern, ground)


def _logical_relation_atoms(expression: Any) -> tuple[Any, ...]:
    if not isinstance(expression, tuple) or not expression:
        return (expression,)
    if expression[0] == "distinct":
        return ()
    if expression[0] in {"and", "or", "not"}:
        return tuple(
            atom
            for child in expression[1:]
            for atom in _logical_relation_atoms(child)
        )
    return (expression,)


def _target_external_patterns(
    presentation: GdlSourcePresentation,
    target_slice: TargetDependencySlice,
) -> tuple[Any, ...]:
    external = frozenset(target_slice.external_relations)
    patterns: list[Any] = []
    for ordinal in target_slice.form_ordinals:
        form = presentation.forms[ordinal].form
        premises = form[2:] if form and form[0] == "<=" else ()
        for premise in premises:
            for atom in _logical_relation_atoms(premise):
                if relation_signature(atom) not in external:
                    continue
                pattern = _canonical_authored_pattern(atom)
                if pattern not in patterns:
                    patterns.append(pattern)
    return tuple(patterns)


def _validate_static_closure(
    values: Iterable[str],
    presentation: GdlSourcePresentation,
    templates: object,
    carrier: object,
    source_supports: set[Any],
) -> tuple[Any, ...]:
    authored_distinct = {
        canonical_ground_literal(occurrence.form)
        for occurrence in presentation.forms
        if occurrence.form and occurrence.form[0] == "distinct"
    }
    result = []
    for value in values:
        dataset_term = ground_atom_term(parse_ground_atom(value))
        if (
            isinstance(dataset_term, tuple)
            and dataset_term
            and dataset_term[0] == "distinct"
        ):
            static = canonical_ground_literal(dataset_term)
            if static not in authored_distinct:
                raise PositiveHornBoundary(
                    f"{value}: static disequality lacks authored evidence"
                )
        else:
            try:
                static = canonical_ground_literal(
                    decode_gdl_dataset_ground_application(
                        templates,
                        dataset_term,
                        carrier=carrier,
                    )
                )
            except PositiveHornBoundary as exc:
                raise PositiveHornBoundary(f"{value}: {exc}") from exc
            if static not in source_supports:
                raise PositiveHornBoundary(
                    f"{value}: static fact is not established by authored source"
                )
        result.append(static)
    return tuple(result)


def _query_matches(pattern: Any, value: Any) -> bool:
    bindings: dict[str, Any] = {}

    def visit(left: Any, right: Any) -> bool:
        if isinstance(left, str) and left.startswith("$"):
            if left not in bindings:
                bindings[left] = right
                return True
            return bindings[left] == right
        if isinstance(left, tuple):
            return (
                isinstance(right, tuple)
                and len(left) == len(right)
                and all(visit(a, b) for a, b in zip(left, right))
            )
        return left == right

    return visit(pattern, value)


def _digest_field(digest: Any, value: str) -> None:
    encoded = value.encode("utf-8")
    digest.update(len(encoded).to_bytes(8, "big"))
    digest.update(encoded)


def _corpus_contradiction_signature(
    receipt: CorpusContradictionReceipt,
) -> dict[str, object]:
    return {
        "evaluated_tasks": receipt.evaluated_tasks,
        "established_tasks": receipt.established_tasks,
        "contradiction_tasks": receipt.contradiction_tasks,
        "evaluated_states": receipt.evaluated_states,
        "evaluated_episodes": receipt.evaluated_episodes,
        "typing_proofs": receipt.typing_proofs,
        "supports": receipt.supports,
        "proof_edges": receipt.proof_edges,
        "contradiction_states": receipt.contradiction_states,
        "missing_labels": receipt.missing_labels,
        "extra_labels": receipt.extra_labels,
        "target_states": receipt.target_states,
        "digest": receipt.digest,
        "first_episode": receipt.first_episode,
        "first_missing": receipt.first_missing,
        "first_extra": receipt.first_extra,
    }


def _state_observed_answers(
    state: State,
    supports: Iterable[Any],
    templates: object,
    constructor_views: object,
    carrier: object,
    query_cache: dict[int, tuple[Any, ...]],
) -> set[Any]:
    queries = tuple(
        canonical_ground_literal(query)
        for query in _state_query_patterns(state, query_cache)
    )
    source_fibres = tuple(
        canonical_ground_literal(fibre)
        for query in queries
        for fibre in decode_gdl_dataset_target_query_fibres(
            templates, query, state.target
        )
    )
    represented: set[Any] = set()
    for support in supports:
        if not any(
            _query_matches(fibre, support) for fibre in source_fibres
        ):
            continue
        for encoded in encode_gdl_dataset_ground_applications(
            support,
            constructor_views=constructor_views,
            carrier=carrier,
        ):
            canonical = canonical_ground_literal(encoded)
            if any(_query_matches(query, canonical) for query in queries):
                represented.add(canonical)
    return represented


def _expected_answers(state: State) -> set[Any]:
    return {
        canonical_ground_literal(ground_atom_term(parse_ground_atom(value)))
        for value in state.positives
    }


def _state_query_patterns(
    state: State,
    cache: dict[int, tuple[Any, ...]],
) -> tuple[Any, ...]:
    """Parse each immutable task vocabulary once in its load epoch."""

    identity = id(state.atoms)
    patterns = cache.get(identity)
    if patterns is None:
        patterns = target_query_patterns(state.atoms)
        cache[identity] = patterns
    return patterns


def _group_query_fibres(
    group: tuple[State, ...],
    templates: object,
    query_cache: dict[int, tuple[Any, ...]],
) -> tuple[Any, ...]:
    fibres: list[Any] = []
    for state in group:
        for query in _state_query_patterns(state, query_cache):
            for fibre in decode_gdl_dataset_target_query_fibres(
                templates, query, state.target
            ):
                canonical = canonical_ground_literal(fibre)
                if canonical not in fibres:
                    fibres.append(canonical)
    return tuple(fibres)


def _reference_episode_ordinals(
    groups: tuple[tuple[State, ...], ...],
    full_reference_max_groups: int,
    reference_groups: int,
) -> frozenset[int]:
    count = len(groups)
    if count <= full_reference_max_groups:
        return frozenset(range(1, count + 1))
    selected = {
        1,
        count,
        1 + min(range(count), key=lambda index: len(groups[index][0].background)),
        1 + max(range(count), key=lambda index: len(groups[index][0].background)),
    }
    if reference_groups == 1:
        return frozenset((1,))
    for index in range(reference_groups):
        selected.add(1 + index * (count - 1) // (reference_groups - 1))
    return frozenset(sorted(selected)[:reference_groups])


def _render_episode_form(
    game: str,
    ordinal: int,
    facts: Iterable[Any],
    queries: Iterable[Any],
) -> str:
    rendered_facts = " ".join(render_term(fact) for fact in facts)
    fact_field = "(facts)" if not rendered_facts else f"(facts {rendered_facts})"
    rendered_queries = " ".join(render_term(query) for query in queries)
    query_field = (
        "(queries)" if not rendered_queries
        else f"(queries {rendered_queries})"
    )
    return (
        f"(gdl-stratified-episode-v1 "
        f"(gdl:iggp-episode {game} {ordinal}) "
        f"{fact_field} {query_field})"
    )


def run_native(runner: Path, workload: str) -> NativeEpisodeRun:
    completed = subprocess.run(
        (str(runner), "-"),
        input=workload.encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(detail or "native episode runner failed")
    return parse_native_output(completed.stdout)


def compare_game(
    snapshot_root: Path,
    repo: Path,
    runner: Path,
    game: str,
    *,
    batch_size: int,
    full_reference_max_groups: int,
    reference_groups: int,
    target: str | None = None,
) -> Counter[str]:
    source_bytes = task_source_path(snapshot_root, game).read_bytes()
    profile_bytes = (snapshot_root / "types" / f"{game}.typ").read_bytes()
    presentation = parse_gdl_source_presentation(source_bytes.decode("utf-8"))
    profile = parse_gdl_type_profile(profile_bytes.decode("utf-8"))
    if target is not None and target not in STANDARD_GDL_ARITIES:
        raise GenerationError(f"unknown standard GDL target {target!r}")
    target_signature = (
        None
        if target is None
        else RelationSignature(target, STANDARD_GDL_ARITIES[target])
    )
    basis = construct_stratified_model_basis(
        presentation, profile, target=target_signature
    )
    constructor_views = gdl_dataset_constructor_views(
        profile, basis.typed_source, basis.carrier
    )
    full_templates = gdl_dataset_representation_templates(
        presentation,
        constructor_views,
        profile=(profile if basis.target_slice is not None else None),
    )
    templates = gdl_dataset_representation_templates(
        presentation,
        constructor_views,
        form_ordinals=(
            None
            if basis.target_slice is None
            else basis.target_slice.selected_forms
        ),
        profile=(profile if basis.target_slice is not None else None),
    )
    source_witness = construct_stratified_model_from_basis(basis)
    external_patterns = (
        ()
        if basis.target_slice is None
        else _target_external_patterns(presentation, basis.target_slice)
    )
    states = load_game_states(snapshot_root, repo, game, f"gdl:{game}")
    if target is not None:
        states = tuple(state for state in states if state.target == target)
    if not states:
        raise GenerationError(
            "game has no IGGP states"
            if target is None
            else f"game has no IGGP states for {target}"
        )
    if any(state.statics != states[0].statics for state in states):
        raise GenerationError("task files disagree on their static closure")
    query_cache: dict[int, tuple[Any, ...]] = {}

    source_supports = {support.literal for support in source_witness.supports}
    if basis.target_slice is None:
        static_facts = _validate_static_closure(
            states[0].statics,
            presentation,
            templates,
            basis.carrier,
            source_supports,
        )
        omitted_static_facts = 0
    else:
        static_facts, omitted_static_facts = (
            _project_dataset_terms_to_target(
                states[0].statics,
                full_templates,
                basis.carrier,
                basis.target_slice,
                external_patterns,
            )
        )
        if any(fact not in source_supports for fact in static_facts):
            raise PositiveHornBoundary(
                "retained static fact is not established by target source"
            )

    groups = _state_groups(states)
    reference_ordinals = _reference_episode_ordinals(
        groups, full_reference_max_groups, reference_groups
    )
    source_package = render_source_package(source_bytes, profile_bytes)
    totals: Counter[str] = Counter()
    contradiction_counts: Counter[str] = Counter()
    contradiction_targets: Counter[str] = Counter()
    contradiction_digest = hashlib.sha256(
        b"cetta-prime-iggp-corpus-contradiction-v1\0"
    )
    first_contradiction: tuple[
        str, tuple[str, ...], tuple[str, ...]
    ] | None = None
    for batch_start in range(0, len(groups), batch_size):
        batch_groups = groups[batch_start:batch_start + batch_size]
        plans = []
        episode_forms = []
        for offset, group in enumerate(batch_groups):
            ordinal = batch_start + offset + 1
            if basis.target_slice is None:
                facts = _lift_dataset_terms(
                    group[0].background,
                    templates,
                    basis.carrier,
                )
                omitted_facts = 0
            else:
                facts, omitted_facts = _project_dataset_terms_to_target(
                    group[0].background,
                    full_templates,
                    basis.carrier,
                    basis.target_slice,
                    external_patterns,
                )
            reference = (
                construct_stratified_model_from_basis(
                    basis, initial_facts=facts
                )
                if ordinal in reference_ordinals
                else None
            )
            queries = () if reference is not None else _group_query_fibres(
                group, templates, query_cache
            )
            plans.append(
                (ordinal, group, facts, omitted_facts, queries, reference)
            )
            episode_forms.append(
                _render_episode_form(game, ordinal, facts, queries)
            )

        target_request = (
            ""
            if target_signature is None
            else (
                f"(gdl-stratified-target-v1 "
                f"{target_signature.name} {target_signature.arity})\n"
            )
        )
        workload = (
            source_package + target_request +
            "\n".join(episode_forms) + "\n"
        )
        native = run_native(runner, workload)
        if (
            native.source_outcome != "Established"
            or native.selection_kind != 1
            or native.selection_eligible != 1
            or native.selection_frontier != 1
        ):
            raise ValueError(
                "source did not select its request-local unique greatest realization"
            )
        if target_signature is None:
            if native.target_name is not None:
                raise ValueError("whole-source run unexpectedly carries a target")
        elif (
            native.target_name != target_signature.name
            or native.target_arity != target_signature.arity
            or native.target_source_forms != len(presentation.forms)
            or native.target_selected_forms != basis.source_forms
            or native.target_reachable_relations !=
                len(basis.target_slice.reachable_relations)
            or native.target_external_relations !=
                len(basis.target_slice.external_relations)
        ):
            raise ValueError("native target slice receipt differs from source oracle")
        if len(native.episodes) != len(plans):
            raise ValueError("native episode count differs from workload batch")

        for plan, observed in zip(plans, native.episodes):
            ordinal, group, facts, omitted_facts, queries, reference = plan
            if observed.outcome != "Established":
                raise ValueError(
                    f"episode {ordinal} returned {observed.outcome}"
                )
            if observed.stats["query_count"] != len(queries):
                raise ValueError(
                    f"episode {ordinal} query receipt differs from workload"
                )
            if reference is not None:
                expected_supports = {
                    (support.relation_index, support.stratum, support.literal)
                    for support in reference.supports
                }
                if set(observed.supports) != expected_supports:
                    missing = sorted(
                        (relation, stratum, render_term(literal))
                        for relation, stratum, literal
                        in expected_supports - set(observed.supports)
                    )[:3]
                    extra = sorted(
                        (relation, stratum, render_term(literal))
                        for relation, stratum, literal
                        in set(observed.supports) - expected_supports
                    )[:3]
                    raise ValueError(
                        f"episode {ordinal} support model differs; "
                        f"missing={missing} extra={extra}"
                    )
                expected_stats = {
                    "authored_facts": len(facts),
                    "typing_proof_occurrences": (
                        reference.stats.episode_typing_proof_occurrences
                    ),
                    "seeded_support_nodes": (
                        reference.stats.episode_support_nodes
                    ),
                    "seeded_proof_edges": (
                        reference.stats.episode_typing_proof_occurrences
                    ),
                    "support_nodes": reference.stats.support_nodes,
                    "ground_instances": reference.stats.ground_instances,
                    "distinct_checks": reference.stats.distinct_checks,
                    "completed_strata": reference.stats.completed_strata,
                }
                actual_stats = {
                    name: observed.stats[name] for name in expected_stats
                }
                if actual_stats != expected_stats:
                    raise ValueError(
                        f"episode {ordinal} work differs; "
                        f"native={actual_stats} reference={expected_stats}"
                    )
                totals["reference_episodes"] += 1
            else:
                if (
                    observed.stats["authored_facts"] != len(facts)
                    or observed.stats["typing_proof_occurrences"] < len(facts)
                    or observed.stats["seeded_proof_edges"] !=
                        observed.stats["typing_proof_occurrences"]
                    or observed.stats["completed_strata"] !=
                        basis.stratification.maximum_stratum + 1
                ):
                    raise ValueError(
                        f"episode {ordinal} native receipt is inconsistent"
                    )
                if any(
                    not any(_query_matches(query, literal) for query in queries)
                    for _, _, literal in observed.supports
                ):
                    raise ValueError(
                        f"episode {ordinal} emitted support outside its queries"
                    )
            if len(set(observed.episode_occurrences)) != len(facts):
                raise ValueError(
                    f"episode {ordinal} lost external fact occurrence identity"
                )

            structural_supports = {
                literal for _, _, literal in observed.supports
            }
            oracle_supports = (
                structural_supports if reference is not None else None
            )
            for state in group:
                actual_answers = _state_observed_answers(
                    state,
                    structural_supports,
                    templates,
                    constructor_views,
                    basis.carrier,
                    query_cache,
                )
                expected_answers = _expected_answers(state)
                if actual_answers != expected_answers:
                    if oracle_supports is None:
                        oracle = construct_stratified_model_from_basis(
                            basis, initial_facts=facts
                        )
                        oracle_supports = {
                            support.literal for support in oracle.supports
                        }
                    oracle_answers = _state_observed_answers(
                        state,
                        oracle_supports,
                        templates,
                        constructor_views,
                        basis.carrier,
                        query_cache,
                    )
                    if actual_answers != oracle_answers:
                        raise ValueError(
                            f"{state.episode}: native answers disagree with "
                            "the independent source oracle"
                        )
                    missing = tuple(sorted(
                        render_term(value)
                        for value in expected_answers - actual_answers
                    ))
                    extra = tuple(sorted(
                        render_term(value)
                        for value in actual_answers - expected_answers
                    ))
                    contradiction_counts["states"] += 1
                    contradiction_counts["missing"] += len(missing)
                    contradiction_counts["extra"] += len(extra)
                    contradiction_targets[state.target] += 1
                    _digest_field(contradiction_digest, state.episode)
                    _digest_field(contradiction_digest, state.target)
                    for value in missing:
                        _digest_field(contradiction_digest, f"missing:{value}")
                    for value in extra:
                        _digest_field(contradiction_digest, f"extra:{value}")
                    if first_contradiction is None:
                        first_contradiction = (
                            state.episode, missing, extra
                        )
            totals["episodes"] += 1
            totals["states"] += len(group)
            totals["fact_occurrences"] += len(facts)
            totals["omitted_fact_occurrences"] += omitted_facts
            totals["typing_proofs"] += observed.stats[
                "typing_proof_occurrences"
            ]
            totals["supports"] += observed.stats["support_nodes"]
            totals["emitted_supports"] += observed.stats["emitted_supports"]
            totals["proof_edges"] += observed.stats["proof_edges"]
        if len(groups) > batch_size:
            print(
                f"EpisodeBatch\t{game}\t"
                f"completed={min(batch_start + batch_size, len(groups))} "
                f"total={len(groups)}",
                flush=True,
            )
    totals["tasks"] = len({state.target for state in states})
    totals["static_facts"] = len(static_facts)
    totals["omitted_static_facts"] = omitted_static_facts
    if basis.target_slice is not None:
        totals["source_forms"] = len(presentation.forms)
        totals["selected_forms"] = basis.source_forms
        totals["reachable_relations"] = len(
            basis.target_slice.reachable_relations
        )
        totals["external_relations"] = len(
            basis.target_slice.external_relations
        )
    if first_contradiction is not None:
        first_episode, first_missing, first_extra = first_contradiction
        raise CorpusLabelContradiction(
            CorpusContradictionReceipt(
                game=game,
                evaluated_tasks=totals["tasks"],
                established_tasks=(
                    totals["tasks"] - len(contradiction_targets)
                ),
                contradiction_tasks=len(contradiction_targets),
                evaluated_states=totals["states"],
                evaluated_episodes=totals["episodes"],
                typing_proofs=totals["typing_proofs"],
                supports=totals["supports"],
                proof_edges=totals["proof_edges"],
                contradiction_states=contradiction_counts["states"],
                missing_labels=contradiction_counts["missing"],
                extra_labels=contradiction_counts["extra"],
                target_states=tuple(sorted(contradiction_targets.items())),
                digest=contradiction_digest.hexdigest(),
                first_episode=first_episode,
                first_missing=first_missing,
                first_extra=first_extra,
            )
        )
    return totals


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--full-reference-max-groups", type=int, default=512)
    parser.add_argument("--reference-groups", type=int, default=32)
    parser.add_argument(
        "--games",
        nargs="*",
        help="optional exact games; the default is every source-eligible game",
    )
    args = parser.parse_args()
    if (
        args.batch_size <= 0
        or args.full_reference_max_groups < 0
        or args.reference_groups <= 0
    ):
        print("FAIL: episode qualification limits are invalid", file=sys.stderr)
        return 2
    if not args.runner.is_file():
        print("FAIL: native stratified-episode runner is missing", file=sys.stderr)
        return 2
    repo = Path(__file__).resolve().parents[1]
    try:
        audits, digest = audit_presentations(args.snapshot_root)
        validate_presentations(audits, digest)
        evidence = load_episode_evidence(repo / EVIDENCE_PATH)
        validate_episode_evidence(
            evidence,
            repo,
            audits=audits,
            presentation_digest=digest,
            snapshot_root=args.snapshot_root,
        )
        pinned_game_totals = expected_game_totals(evidence)
        pinned_contradictions = expected_corpus_contradictions(evidence)
    except (
        EpisodeEvidenceError,
        OSError,
        UnicodeDecodeError,
        PresentationAuditError,
    ) as exc:
        print(f"FAIL: IGGP presentation audit: {exc}", file=sys.stderr)
        return 1

    requested = set(args.games or ())
    known = {item.game for item in audits}
    if requested - known:
        print(
            "FAIL: unknown IGGP games: "
            + ", ".join(sorted(requested - known)),
            file=sys.stderr,
        )
        return 2

    source_eligible = {
        item.game
        for item in audits
        if item.checked_type_of_extensions == 1
        and item.checked_type_of_occurrence_judgments > 0
        and item.foreign_code_lines == 0
    }
    if requested - source_eligible:
        print(
            "FAIL: requested games are outside the typed finite source image: "
            + ", ".join(sorted(requested - source_eligible)),
            file=sys.stderr,
        )
        return 2

    selected = sorted(requested or source_eligible)
    totals: Counter[str] = Counter()
    covered = []
    outside: list[tuple[str, str]] = []
    contradictions: list[CorpusContradictionReceipt] = []
    for game in selected:
        try:
            game_totals = compare_game(
                args.snapshot_root,
                repo,
                args.runner.resolve(),
                game,
                batch_size=args.batch_size,
                full_reference_max_groups=args.full_reference_max_groups,
                reference_groups=args.reference_groups,
            )
        except CorpusLabelContradiction as exc:
            receipt = exc.receipt
            expected = pinned_contradictions.get(game)
            observed = _corpus_contradiction_signature(receipt)
            if observed != expected:
                print(
                    f"FAIL: IGGP {game} corpus contradiction drifted: "
                    f"observed={observed} expected={expected}",
                    file=sys.stderr,
                )
                return 1
            contradictions.append(receipt)
            continue
        except (PositiveHornBoundary, StratifiedModelBoundary) as exc:
            outside.append((game, str(exc)))
            continue
        except (
            GenerationError,
            NegativeDependencyCycle,
            OSError,
            PresentationError,
            RuntimeError,
            UnicodeDecodeError,
            ValueError,
        ) as exc:
            print(
                f"FAIL: IGGP {game} stratified episodes: {exc}",
                file=sys.stderr,
            )
            return 1
        covered.append(game)
        pinned = pinned_game_totals.get(game)
        if pinned is not None:
            observed = {name: game_totals[name] for name in pinned}
            if observed != pinned:
                print(
                    f"FAIL: IGGP {game} episode totals drifted: "
                    f"observed={observed} expected={pinned}",
                    file=sys.stderr,
                )
                return 1
        totals.update(game_totals)
        print(
            f"Established\t{game}\t"
            + " ".join(
                f"{name}={game_totals[name]}"
                for name in (
                    "tasks",
                    "states",
                    "episodes",
                    "fact_occurrences",
                    "supports",
                    "emitted_supports",
                    "proof_edges",
                    "reference_episodes",
                )
            ),
            flush=True,
        )

    expected_contradictions = set(pinned_contradictions) & set(selected)
    observed_contradictions = {receipt.game for receipt in contradictions}
    if observed_contradictions != expected_contradictions:
        print(
            "FAIL: IGGP corpus contradiction inventory drifted: "
            f"observed={sorted(observed_contradictions)} "
            f"expected={sorted(expected_contradictions)}",
            file=sys.stderr,
        )
        return 1

    for game, reason in outside:
        print(f"OutsideFragment\t{game}\t{reason}")
    for receipt in contradictions:
        print(
            f"CorpusContradiction\t{receipt.game}\t"
            f"established_tasks={receipt.established_tasks} "
            f"contradiction_tasks={receipt.contradiction_tasks} "
            f"evaluated_states={receipt.evaluated_states} "
            f"evaluated_episodes={receipt.evaluated_episodes} "
            f"contradiction_states={receipt.contradiction_states} "
            f"missing_labels={receipt.missing_labels} "
            f"extra_labels={receipt.extra_labels} "
            f"digest={receipt.digest}"
        )
    established_tasks = totals["tasks"] + sum(
        receipt.established_tasks for receipt in contradictions
    )
    contradiction_tasks = sum(
        receipt.contradiction_tasks for receipt in contradictions
    )
    print(
        "PrimeIggpNativeStratifiedEpisodeSummary "
        f"source_eligible_games={len(source_eligible)} "
        f"source_eligible_tasks={4 * len(source_eligible)} "
        f"selected_games={len(selected)} covered_games={len(covered)} "
        f"outside_games={len(outside)} "
        f"contradiction_games={len(contradictions)} "
        f"established_tasks={established_tasks} "
        f"contradiction_tasks={contradiction_tasks} "
        + " ".join(
            f"{name}={totals[name]}"
            for name in (
                "states",
                "episodes",
                "static_facts",
                "fact_occurrences",
                "typing_proofs",
                "supports",
                "emitted_supports",
                "proof_edges",
                "reference_episodes",
            )
        )
    )
    if outside:
        return 1
    if requested and contradictions:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
