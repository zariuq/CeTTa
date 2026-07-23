#!/usr/bin/env python3
"""Finite executable reference model for Prime cell-causal applications.

This model is deliberately independent of the native rewrite frontiers.  It
defines the semantic distinctions that an eventual native contender must
refine: allocated cells own outcomes, rule occurrences subscribe to cells,
and concrete publications retain their actual causal receipts.
"""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, replace
from fractions import Fraction
from typing import Callable, Iterable, Mapping, Sequence


@dataclass(frozen=True, order=True)
class ProducerId:
    lineage: str
    cell: int
    generation: int = 0


@dataclass(frozen=True, order=True)
class SourceOccurrence:
    application: int
    position: int
    path: tuple[str, ...] = ()


@dataclass(frozen=True, order=True)
class RuleOccurrence:
    application: int
    ordinal: int


@dataclass(frozen=True, order=True)
class EffectSpec:
    occurrence: str
    state: str
    before: str
    after: str


@dataclass(frozen=True, order=True)
class OutcomeSpec:
    occurrence: str
    value: str
    probability: Fraction = Fraction(1, 1)
    value_type: str = "atom"
    stable_fault: bool = False
    effects: tuple[EffectSpec, ...] = ()


@dataclass(frozen=True)
class CellSpec:
    producer: ProducerId
    origin: str
    outcomes: tuple[OutcomeSpec, ...]
    dependencies: tuple[ProducerId, ...] = ()
    cyclic: bool = False


@dataclass(frozen=True)
class Check:
    source: SourceOccurrence
    accepted: frozenset[str]
    after: frozenset[int] = frozenset()
    expected_type: str = "atom"


@dataclass(frozen=True)
class Candidate:
    rule: RuleOccurrence
    checks: tuple[Check, ...]
    answer: str
    rhs_demands: tuple[SourceOccurrence, ...] = ()


@dataclass(frozen=True, order=True)
class Event:
    occurrence: str
    kind: str
    payload: tuple[str, ...]
    parents: frozenset[str] = frozenset()


@dataclass(frozen=True, order=True)
class Subscription:
    rule: RuleOccurrence
    source: SourceOccurrence
    producer: ProducerId
    role: str
    expected_type: str


@dataclass(frozen=True)
class CandidateState:
    passed: frozenset[int] = frozenset()
    rhs_observed: frozenset[SourceOccurrence] = frozenset()
    answer_roots: frozenset[str] = frozenset()
    refute_roots: frozenset[str] = frozenset()
    phase: str = "active"
    incomplete_reason: str | None = None


@dataclass(frozen=True, order=True)
class PublicationOccurrence:
    occurrence: str
    rule: RuleOccurrence
    answer: str
    receipt: frozenset[str]


@dataclass(frozen=True)
class MachineState:
    selected: tuple[tuple[ProducerId, str], ...]
    events: tuple[Event, ...]
    candidates: tuple[CandidateState, ...]
    subscriptions: frozenset[Subscription]
    publications: frozenset[PublicationOccurrence]
    producer_starts: frozenset[ProducerId]
    store: tuple[tuple[str, str], ...]
    steps: int = 0


@dataclass(frozen=True)
class Problem:
    cells: tuple[CellSpec, ...]
    source_map: tuple[tuple[SourceOccurrence, ProducerId], ...]
    candidates: tuple[Candidate, ...]
    conversions: frozenset[tuple[str, str]] = frozenset()
    initial_store: tuple[tuple[str, str], ...] = ()

    def validate(self) -> None:
        cells = dict((cell.producer, cell) for cell in self.cells)
        if len(cells) != len(self.cells):
            raise ValueError("duplicate producer identity")
        sources = dict(self.source_map)
        if len(sources) != len(self.source_map):
            raise ValueError("one source occurrence maps to several producers")
        for producer in sources.values():
            if producer not in cells:
                raise ValueError("source occurrence names an absent producer")
        if len({candidate.rule for candidate in self.candidates}) != len(self.candidates):
            raise ValueError("duplicate rule occurrence identity")
        payloads: dict[str, tuple[str, ...]] = {}
        for cell in self.cells:
            if cell.cyclic and cell.outcomes:
                raise ValueError("a cyclic producer cannot claim completed outcomes")
            if not cell.cyclic and not cell.outcomes:
                raise ValueError("an acyclic producer needs at least one outcome")
            if not cell.cyclic and sum(
                outcome.probability for outcome in cell.outcomes
            ) != 1:
                raise ValueError("producer outcome probabilities must sum to one")
            for outcome in cell.outcomes:
                outcome_id = outcome_event_id(cell.producer, outcome)
                payload = (str(cell.producer), outcome.value, outcome.value_type)
                old = payloads.setdefault(outcome_id, payload)
                if old != payload:
                    raise ValueError("one occurrence ID names two event payloads")
                for effect in outcome.effects:
                    payload = (effect.state, effect.before, effect.after)
                    old = payloads.setdefault(effect.occurrence, payload)
                    if old != payload:
                        raise ValueError("one occurrence ID names two effect payloads")


@dataclass(frozen=True)
class RunResult:
    worlds: tuple[MachineState, ...]
    residuals: tuple[frozenset[str], ...]
    unsupported: bool = False

    @property
    def publications(self) -> frozenset[PublicationOccurrence]:
        return frozenset(
            publication
            for world in self.worlds
            for publication in world.publications
        )

    def rule_value_explanations(
        self,
    ) -> Mapping[tuple[RuleOccurrence, str], frozenset[frozenset[str]]]:
        grouped: dict[tuple[RuleOccurrence, str], set[frozenset[str]]] = defaultdict(set)
        for publication in self.publications:
            grouped[(publication.rule, publication.answer)].add(publication.receipt)
        return {key: frozenset(value) for key, value in grouped.items()}

    def value_explanations(
        self,
    ) -> Mapping[str, frozenset[tuple[RuleOccurrence, frozenset[str]]]]:
        grouped: dict[str, set[tuple[RuleOccurrence, frozenset[str]]]] = defaultdict(set)
        for publication in self.publications:
            grouped[publication.answer].add((publication.rule, publication.receipt))
        return {key: frozenset(value) for key, value in grouped.items()}


def producer_text(producer: ProducerId) -> str:
    return f"{producer.lineage}:{producer.cell}:{producer.generation}"


def outcome_event_id(producer: ProducerId, outcome: OutcomeSpec) -> str:
    return f"outcome:{producer_text(producer)}:{outcome.occurrence}"


def _selected(state: MachineState) -> dict[ProducerId, str]:
    return dict(state.selected)


def _events(state: MachineState) -> dict[str, Event]:
    return {event.occurrence: event for event in state.events}


def _store(state: MachineState) -> dict[str, str]:
    return dict(state.store)


def causal_closure(events: Mapping[str, Event], roots: Iterable[str]) -> frozenset[str]:
    closed: set[str] = set()
    pending = list(roots)
    while pending:
        occurrence = pending.pop()
        if occurrence in closed:
            continue
        event = events.get(occurrence)
        if event is None:
            raise ValueError(f"receipt names absent event {occurrence}")
        closed.add(occurrence)
        pending.extend(event.parents)
    return frozenset(closed)


def minimal_antichain(supports: Iterable[frozenset[str]]) -> tuple[frozenset[str], ...]:
    """Return every inclusion-minimal support, including mixed cardinalities."""
    ordered = sorted(
        set(supports), key=lambda support: (len(support), tuple(sorted(support)))
    )
    kept: list[frozenset[str]] = []
    for support in ordered:
        if any(old <= support for old in kept):
            continue
        kept.append(support)
    return tuple(kept)


def _cell(problem: Problem, producer: ProducerId) -> CellSpec:
    return next(cell for cell in problem.cells if cell.producer == producer)


def _source_producer(problem: Problem, source: SourceOccurrence) -> ProducerId:
    try:
        return dict(problem.source_map)[source]
    except KeyError as error:
        raise ValueError("candidate demands an unbound source occurrence") from error


def _outcome(problem: Problem, producer: ProducerId, occurrence: str) -> OutcomeSpec:
    cell = _cell(problem, producer)
    return next(outcome for outcome in cell.outcomes if outcome.occurrence == occurrence)


def _add_event(state: MachineState, event: Event) -> MachineState:
    events = _events(state)
    previous = events.get(event.occurrence)
    if previous is not None and previous != event:
        raise ValueError("one event occurrence has two payloads")
    events[event.occurrence] = event
    return replace(state, events=tuple(sorted(events.values())))


def _produce(
    problem: Problem,
    state: MachineState,
    producer: ProducerId,
    stack: frozenset[ProducerId] = frozenset(),
) -> tuple[MachineState, ...]:
    if producer in _selected(state):
        return (state,)
    cell = _cell(problem, producer)
    if cell.cyclic or producer in stack:
        return ()

    dependency_states = (state,)
    for dependency in cell.dependencies:
        expanded: list[MachineState] = []
        for current in dependency_states:
            expanded.extend(_produce(problem, current, dependency, stack | {producer}))
        dependency_states = tuple(expanded)
        if not dependency_states:
            return ()

    produced: list[MachineState] = []
    for current in dependency_states:
        selected = _selected(current)
        dependency_events = frozenset(
            outcome_event_id(dependency, _outcome(problem, dependency, selected[dependency]))
            for dependency in cell.dependencies
        )
        for outcome in cell.outcomes:
            branch = current
            store = _store(branch)
            parents = dependency_events
            compatible = True
            for effect in outcome.effects:
                actual = store.get(effect.state, effect.before)
                if actual != effect.before:
                    compatible = False
                    break
                event = Event(
                    effect.occurrence,
                    "effect",
                    (effect.state, effect.before, effect.after),
                    parents,
                )
                branch = _add_event(branch, event)
                store[effect.state] = effect.after
                parents = frozenset({effect.occurrence})
            if not compatible:
                continue
            outcome_id = outcome_event_id(producer, outcome)
            branch = _add_event(
                branch,
                Event(
                    outcome_id,
                    "stable-fault" if outcome.stable_fault else "outcome",
                    (producer_text(producer), outcome.value, outcome.value_type),
                    parents,
                ),
            )
            selected = _selected(branch)
            selected[producer] = outcome.occurrence
            branch = replace(
                branch,
                selected=tuple(sorted(selected.items())),
                producer_starts=branch.producer_starts | {producer},
                store=tuple(sorted(store.items())),
            )
            produced.append(branch)
    return tuple(produced)


def _convertible(problem: Problem, actual: str, expected: str) -> bool:
    return actual == expected or (actual, expected) in problem.conversions


def _candidate_actions(problem: Problem, state: MachineState, index: int):
    candidate = problem.candidates[index]
    current = state.candidates[index]
    if current.phase != "active":
        return []
    pending = [
        check_index
        for check_index, check in enumerate(candidate.checks)
        if check_index not in current.passed and check.after <= current.passed
    ]
    if pending:
        return [("check", check_index) for check_index in pending]
    if len(current.passed) != len(candidate.checks):
        return [("incomplete", "blocked-check-dependency")]
    rhs = [source for source in candidate.rhs_demands if source not in current.rhs_observed]
    if rhs:
        return [("rhs", source) for source in rhs]
    return [("publish", None)]


def _replace_candidate(
    state: MachineState, index: int, candidate: CandidateState
) -> MachineState:
    candidates = list(state.candidates)
    candidates[index] = candidate
    return replace(state, candidates=tuple(candidates), steps=state.steps + 1)


def _observe_source(
    problem: Problem,
    state: MachineState,
    index: int,
    source: SourceOccurrence,
    role: str,
    expected_type: str,
) -> tuple[tuple[MachineState, OutcomeSpec, frozenset[str]], ...]:
    candidate = problem.candidates[index]
    producer = _source_producer(problem, source)
    subscription = Subscription(candidate.rule, source, producer, role, expected_type)
    subscribed = replace(state, subscriptions=state.subscriptions | {subscription})
    branches = _produce(problem, subscribed, producer)
    observed = []
    for branch in branches:
        outcome = _outcome(problem, producer, _selected(branch)[producer])
        event_id = outcome_event_id(producer, outcome)
        roots = causal_closure(_events(branch), {event_id})
        observed.append((branch, outcome, roots))
    return tuple(observed)


def _step_action(problem: Problem, state: MachineState, index: int, action):
    kind, payload = action
    candidate = problem.candidates[index]
    current = state.candidates[index]
    if kind == "incomplete":
        return (_replace_candidate(
            state,
            index,
            replace(current, phase="incomplete", incomplete_reason=payload),
        ),)
    if kind == "publish":
        receipt = causal_closure(_events(state), current.answer_roots)
        occurrence = (
            f"publication:{candidate.rule.application}:{candidate.rule.ordinal}:"
            + ",".join(sorted(receipt))
        )
        publication = PublicationOccurrence(
            occurrence, candidate.rule, candidate.answer, receipt
        )
        return (_replace_candidate(
            replace(state, publications=state.publications | {publication}),
            index,
            replace(current, phase="published"),
        ),)
    if kind == "rhs":
        successors = []
        for branch, _outcome_spec, roots in _observe_source(
            problem, state, index, payload, "rhs", "atom"
        ):
            successors.append(_replace_candidate(
                branch,
                index,
                replace(
                    current,
                    rhs_observed=current.rhs_observed | {payload},
                    answer_roots=current.answer_roots | roots,
                ),
            ))
        if not successors:
            return (_replace_candidate(
                state,
                index,
                replace(current, phase="incomplete", incomplete_reason="blackhole"),
            ),)
        return tuple(successors)

    check_index = payload
    check = candidate.checks[check_index]
    successors = []
    for branch, outcome, roots in _observe_source(
        problem, state, index, check.source, "lhs", check.expected_type
    ):
        matches = (
            outcome.value in check.accepted
            and _convertible(problem, outcome.value_type, check.expected_type)
        )
        if matches:
            next_candidate = replace(
                current,
                passed=current.passed | {check_index},
                answer_roots=current.answer_roots | roots,
            )
        else:
            next_candidate = replace(
                current,
                phase="refuted",
                refute_roots=roots,
            )
        successors.append(_replace_candidate(branch, index, next_candidate))
    if not successors:
        return (_replace_candidate(
            state,
            index,
            replace(current, phase="incomplete", incomplete_reason="blackhole"),
        ),)
    return tuple(successors)


def run(
    problem: Problem,
    *,
    reverse_actions: bool = False,
    step_budget: int | None = None,
    frontier_policy: str = "breadth-first",
    priority: Callable[[MachineState], object] | None = None,
) -> RunResult:
    problem.validate()
    if frontier_policy not in {"breadth-first", "depth-first", "ranked"}:
        raise ValueError("unknown frontier policy")
    if frontier_policy == "ranked" and priority is None:
        raise ValueError("ranked frontier policy requires a priority function")
    if frontier_policy != "ranked" and priority is not None:
        raise ValueError("a priority function belongs only to ranked control")
    if not problem.candidates:
        return RunResult((), (), unsupported=True)
    initial = MachineState(
        selected=(),
        events=(),
        candidates=tuple(CandidateState() for _ in problem.candidates),
        subscriptions=frozenset(),
        publications=frozenset(),
        producer_starts=frozenset(),
        store=tuple(sorted(problem.initial_store)),
    )
    pending = deque([initial])
    seen = {initial}
    terminals: set[MachineState] = set()
    while pending:
        if frontier_policy == "breadth-first":
            state = pending.popleft()
        elif frontier_policy == "depth-first":
            state = pending.pop()
        else:
            selected_index = min(
                range(len(pending)),
                key=lambda index: (priority(pending[index]), repr(pending[index])),
            )
            state = pending[selected_index]
            del pending[selected_index]
        if step_budget is not None and state.steps >= step_budget:
            candidates = tuple(
                candidate if candidate.phase != "active" else replace(
                    candidate,
                    phase="incomplete",
                    incomplete_reason="budget",
                )
                for candidate in state.candidates
            )
            terminals.add(replace(state, candidates=candidates))
            continue
        actions = [
            (index, action)
            for index in range(len(problem.candidates))
            for action in _candidate_actions(problem, state, index)
        ]
        if reverse_actions:
            actions.reverse()
        if not actions:
            terminals.add(state)
            continue
        for index, action in actions:
            for successor in _step_action(problem, state, index, action):
                if successor not in seen:
                    seen.add(successor)
                    pending.append(successor)

    residual_candidates = []
    for world in terminals:
        if all(candidate.phase == "refuted" for candidate in world.candidates):
            residual_candidates.append(frozenset().union(
                *(candidate.refute_roots for candidate in world.candidates)
            ))
    return RunResult(
        tuple(sorted(terminals, key=normalized_world)),
        minimal_antichain(residual_candidates),
    )


def normalized_world(world: MachineState):
    return (
        world.selected,
        tuple(sorted((p.rule, p.answer, tuple(sorted(p.receipt)))
                     for p in world.publications)),
        tuple((candidate.phase, tuple(sorted(candidate.refute_roots)))
              for candidate in world.candidates),
        world.store,
    )


def normalized_result(result: RunResult):
    return (
        tuple(normalized_world(world) for world in result.worlds),
        tuple(tuple(sorted(receipt)) for receipt in result.residuals),
        result.unsupported,
    )


def exact_probability(result: RunResult, problem: Problem, answer: str) -> Fraction:
    total = Fraction(0, 1)
    counted_assignments: set[tuple[tuple[ProducerId, str], ...]] = set()
    for world in result.worlds:
        if not any(publication.answer == answer for publication in world.publications):
            continue
        # Scheduler interleavings are not probabilistic worlds.  Count each
        # selected producer assignment once even if several transition orders
        # reach it with different subscription histories.
        if world.selected in counted_assignments:
            continue
        counted_assignments.add(world.selected)
        probability = Fraction(1, 1)
        for producer, occurrence in world.selected:
            probability *= _outcome(problem, producer, occurrence).probability
        total += probability
    return total


def naive_independent_explanation_probability(
    result: RunResult, problem: Problem, answer: str
) -> Fraction:
    probabilities = []
    outcome_by_event = {
        outcome_event_id(cell.producer, outcome): outcome
        for cell in problem.cells
        for outcome in cell.outcomes
    }
    for _rule, receipt in result.value_explanations().get(answer, frozenset()):
        probability = Fraction(1, 1)
        for event in receipt:
            outcome = outcome_by_event.get(event)
            if outcome is not None:
                probability *= outcome.probability
        probabilities.append(probability)
    complement = Fraction(1, 1)
    for probability in probabilities:
        complement *= 1 - probability
    return 1 - complement


def import_problem(
    problem: Problem, renaming: Mapping[ProducerId, ProducerId]
) -> Problem:
    if len(set(renaming.values())) != len(renaming):
        raise ValueError("persistence import renaming must be injective")
    if set(renaming) != {cell.producer for cell in problem.cells}:
        raise ValueError("persistence import renaming must cover every producer")
    cells = tuple(
        replace(
            cell,
            producer=renaming[cell.producer],
            dependencies=tuple(renaming[dependency] for dependency in cell.dependencies),
        )
        for cell in problem.cells
    )
    source_map = tuple(
        (source, renaming[producer]) for source, producer in problem.source_map
    )
    return replace(problem, cells=cells, source_map=source_map)
