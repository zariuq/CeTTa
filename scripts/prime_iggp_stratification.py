"""Proof-relevant dependency stratification for authored GDL.

The source presentation remains authority-free.  This module constructs the
minimal stratum assignment required by its positive and negative dependency
edges, retaining exact source and branch occurrences.  A negative cycle is a
checked obstruction; foreign source and non-relational logical shapes remain
outside this exact image.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from prime_iggp_positive_horn import PositiveHornBoundary
from prime_iggp_presentation import GdlSourcePresentation


class StratificationBoundary(PositiveHornBoundary):
    """The presentation is outside the exact stratification construction."""


@dataclass(frozen=True, order=True)
class RelationSignature:
    """One authored top-level relation symbol and arity."""

    name: str
    arity: int


@dataclass(frozen=True)
class DependencyEdge:
    """One occurrence-retaining dependency of a rule head on a body atom."""

    source: tuple[Any, ...]
    path: tuple[int, ...]
    head: RelationSignature
    body: RelationSignature
    negative: bool


@dataclass(frozen=True)
class StratifiedRelation:
    """One relation in the minimal checked stratum assignment."""

    signature: RelationSignature
    stratum: int
    defined: bool


@dataclass(frozen=True)
class StratificationWitness:
    """The complete dependency graph and its minimal stratum assignment."""

    relations: tuple[StratifiedRelation, ...]
    edges: tuple[DependencyEdge, ...]

    @property
    def maximum_stratum(self) -> int:
        return max((relation.stratum for relation in self.relations), default=0)

    def stratum_of(self, signature: RelationSignature) -> int:
        for relation in self.relations:
            if relation.signature == signature:
                return relation.stratum
        raise KeyError(signature)


class NegativeDependencyCycle(RuntimeError):
    """A checked cycle containing at least one negative dependency edge."""

    def __init__(self, edges: Iterable[DependencyEdge]):
        self.edges = tuple(edges)
        super().__init__("GDL dependency graph contains a negative cycle")


@dataclass(frozen=True)
class TargetDependencySlice:
    """Exact dependency-closed source fibre for one requested relation.

    Form ordinals remain coordinates in the complete authored presentation.
    The slice therefore selects occurrences without copying or renumbering
    them.  Relations without authored clauses are retained as external inputs,
    never synthesized as missing definitions.
    """

    target: RelationSignature
    form_ordinals: tuple[int, ...]
    reachable_relations: tuple[RelationSignature, ...]
    external_relations: tuple[RelationSignature, ...]

    @property
    def selected_forms(self) -> frozenset[int]:
        return frozenset(self.form_ordinals)


def relation_signature(application: Any) -> RelationSignature:
    """Return the authored predicate identity without flattening terms.

    In particular, ``next(at(...))`` is ``next/1`` while an authored helper
    ``next_at(...)`` is ``next_at/3``.  A target naming convention may relate
    them later only through an explicit, injective representation witness.
    """

    if isinstance(application, str):
        if application.startswith("?"):
            raise StratificationBoundary("relation head is dynamic")
        return RelationSignature(application, 0)
    if (
        not isinstance(application, tuple)
        or not application
        or not isinstance(application[0], str)
        or application[0].startswith("?")
    ):
        raise StratificationBoundary("relation does not have a static signature")
    return RelationSignature(application[0], len(application) - 1)


def _source_identity(ordinal: int, start: int, end: int) -> tuple[Any, ...]:
    return (
        "gdl:source-occurrence",
        str(ordinal),
        str(start),
        str(end),
    )


def _body_dependencies(
    expression: Any,
    *,
    source: tuple[Any, ...],
    path: tuple[int, ...],
    negative: bool = False,
) -> tuple[tuple[RelationSignature, bool, tuple[int, ...]], ...]:
    if not isinstance(expression, tuple) or not expression:
        return ((relation_signature(expression), negative, path),)
    operator = expression[0]
    if operator == "distinct":
        if len(expression) != 3:
            raise StratificationBoundary(
                f"{source}: distinct does not have exactly two operands"
            )
        return ()
    if operator == "not":
        if len(expression) != 2:
            raise StratificationBoundary(
                f"{source}: not does not have exactly one operand"
            )
        operand = expression[1]
        if (
            isinstance(operand, tuple)
            and operand
            and operand[0] in {"and", "or", "not", "distinct"}
        ):
            raise StratificationBoundary(
                f"{source}: negation operand is not one relational atom"
            )
        return ((relation_signature(operand), not negative, path + (1,)),)
    if operator in {"and", "or"}:
        if operator == "or" and len(expression) < 3:
            raise StratificationBoundary(
                f"{source}: or does not have at least two branches"
            )
        if operator == "and" and len(expression) < 2:
            raise StratificationBoundary(f"{source}: and is empty")
        result: list[tuple[RelationSignature, bool, tuple[int, ...]]] = []
        for index, branch in enumerate(expression[1:], 1):
            result.extend(
                _body_dependencies(
                    branch,
                    source=source,
                    path=path + (index,),
                    negative=negative,
                )
            )
        return tuple(result)
    return ((relation_signature(expression), negative, path),)


def construct_target_dependency_slice(
    presentation: GdlSourcePresentation,
    target: RelationSignature,
) -> TargetDependencySlice:
    """Select exactly the clauses that can contribute to ``target``.

    This is a structural projection of one authored presentation, not a type
    judgment or an authority decision.  Every definition of a reachable
    relation is retained, and every relational premise of a retained rule is
    itself made reachable.  Active foreign source prevents an exact slice
    because it may define an otherwise missing dependency.
    """

    if presentation.foreign_code:
        raise StratificationBoundary(
            "mixed GDL/foreign source has no exact GDL-only target slice"
        )

    definitions: dict[RelationSignature, list[int]] = {}
    parts: dict[int, tuple[Any, tuple[Any, ...]]] = {}
    for ordinal, occurrence in enumerate(presentation.forms):
        form = occurrence.form
        if not form:
            raise StratificationBoundary("GDL source contains an empty form")
        if form[0] == "distinct":
            if len(form) != 3:
                raise StratificationBoundary(
                    f"source form {ordinal + 1} has malformed distinct evidence"
                )
            continue
        if form[0] == "<=":
            if len(form) < 2:
                raise StratificationBoundary("GDL rule has no conclusion")
            conclusion = form[1]
            premises = tuple(form[2:])
        else:
            conclusion = form
            premises = ()
        if (
            isinstance(conclusion, tuple)
            and conclusion
            and conclusion[0] in {"and", "or", "not", "distinct"}
        ):
            raise StratificationBoundary(
                f"source form {ordinal + 1} has a logical rule head"
            )
        signature = relation_signature(conclusion)
        definitions.setdefault(signature, []).append(ordinal)
        parts[ordinal] = (conclusion, premises)

    if target not in definitions:
        raise StratificationBoundary(
            "requested target has no authored defining occurrence"
        )

    reachable: list[RelationSignature] = [target]
    selected: set[int] = set()
    cursor = 0
    while cursor < len(reachable):
        signature = reachable[cursor]
        cursor += 1
        for ordinal in definitions.get(signature, ()):
            selected.add(ordinal)
            occurrence = presentation.forms[ordinal]
            _, premises = parts[ordinal]
            source = _source_identity(
                ordinal + 1, occurrence.start_line, occurrence.end_line
            )
            for premise_index, premise in enumerate(premises, 2):
                for dependency, _, _ in _body_dependencies(
                    premise,
                    source=source,
                    path=(premise_index,),
                ):
                    if dependency not in reachable:
                        reachable.append(dependency)

    external = tuple(
        signature for signature in reachable if signature not in definitions
    )
    return TargetDependencySlice(
        target=target,
        form_ordinals=tuple(sorted(selected)),
        reachable_relations=tuple(reachable),
        external_relations=external,
    )


def construct_stratification(
    presentation: GdlSourcePresentation,
    *,
    form_ordinals: frozenset[int] | None = None,
) -> StratificationWitness:
    """Construct minimal GDL strata or expose an exact negative cycle.

    The constraints are ``head >= body`` for a positive dependency and
    ``head > body`` for a negative dependency.  Repeated relaxation computes
    their least solution.  An update after ``|relations|`` passes is possible
    exactly when a dependency cycle contains a negative edge.
    """

    if presentation.foreign_code:
        raise StratificationBoundary(
            "mixed GDL/foreign source requires a separately admitted route"
        )
    if form_ordinals is not None and any(
        ordinal < 0 or ordinal >= len(presentation.forms)
        for ordinal in form_ordinals
    ):
        raise StratificationBoundary(
            "stratification selection names a source form outside the presentation"
        )

    signatures: list[RelationSignature] = []
    defined: set[RelationSignature] = set()
    edges: list[DependencyEdge] = []

    def retain(signature: RelationSignature) -> None:
        if signature not in signatures:
            signatures.append(signature)

    for ordinal, occurrence in enumerate(presentation.forms, 1):
        if form_ordinals is not None and ordinal - 1 not in form_ordinals:
            continue
        form = occurrence.form
        if not form:
            raise StratificationBoundary("GDL source contains an empty form")
        if form[0] == "distinct":
            if len(form) != 3:
                raise StratificationBoundary(
                    f"source form {ordinal} has malformed distinct evidence"
                )
            continue
        if form[0] == "<=":
            if len(form) < 2:
                raise StratificationBoundary("GDL rule has no conclusion")
            conclusion = form[1]
            premises = form[2:]
        else:
            conclusion = form
            premises = ()
        if (
            isinstance(conclusion, tuple)
            and conclusion
            and conclusion[0] in {"and", "or", "not", "distinct"}
        ):
            raise StratificationBoundary(
                f"source form {ordinal} has a logical rule head"
            )
        head = relation_signature(conclusion)
        retain(head)
        defined.add(head)
        source = _source_identity(
            ordinal, occurrence.start_line, occurrence.end_line
        )
        for premise_index, premise in enumerate(premises, 2):
            for body, negative, path in _body_dependencies(
                premise,
                source=source,
                path=(premise_index,),
            ):
                retain(body)
                edges.append(
                    DependencyEdge(source, path, head, body, negative)
                )

    positions = {signature: index for index, signature in enumerate(signatures)}
    strata = [0] * len(signatures)
    predecessor: list[DependencyEdge | None] = [None] * len(signatures)
    changed: int | None = None
    for _ in range(len(signatures)):
        changed = None
        for edge in edges:
            head_index = positions[edge.head]
            body_index = positions[edge.body]
            required = strata[body_index] + int(edge.negative)
            if strata[head_index] < required:
                strata[head_index] = required
                predecessor[head_index] = edge
                changed = head_index
        if changed is None:
            break
    if changed is not None:
        cursor = changed
        for _ in signatures:
            edge = predecessor[cursor]
            if edge is None:
                raise StratificationBoundary(
                    "dependency-cycle witness is internally incomplete"
                )
            cursor = positions[edge.body]
        cycle_start = cursor
        cycle: list[DependencyEdge] = []
        while True:
            edge = predecessor[cursor]
            if edge is None:
                raise StratificationBoundary(
                    "dependency-cycle witness is internally incomplete"
                )
            cycle.append(edge)
            cursor = positions[edge.body]
            if cursor == cycle_start:
                break
            if len(cycle) > len(signatures):
                raise StratificationBoundary(
                    "dependency-cycle witness does not close"
                )
        if not any(edge.negative for edge in cycle):
            raise StratificationBoundary(
                "dependency-cycle witness has no negative edge"
            )
        raise NegativeDependencyCycle(reversed(cycle))

    return StratificationWitness(
        relations=tuple(
            StratifiedRelation(
                signature=signature,
                stratum=strata[index],
                defined=signature in defined,
            )
            for index, signature in enumerate(signatures)
        ),
        edges=tuple(edges),
    )
