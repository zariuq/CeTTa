"""Typed finite Herbrand carriers for authored IGGP profiles.

Support identity is a structural term paired with its exact result type.
Distinct signatures and typed argument choices remain construction witnesses;
subtyping only controls whether a term may occupy an argument position.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import product
import re
from typing import Any

from prime_iggp_presentation import (
    GdlSignatureStatement,
    GdlSubtypeStatement,
    GdlTypeProfile,
)


INTEGER = re.compile(r"-?[0-9]+\Z")


@dataclass(frozen=True)
class HerbrandSignature:
    statement_ordinal: int
    name_ordinal: int
    name: str
    argument_types: tuple[str, ...]
    result_type: str

    @property
    def relation(self) -> bool:
        return self.result_type == "bool"


@dataclass(frozen=True)
class HerbrandConstruction:
    signature_index: int
    argument_term_indices: tuple[int, ...]


@dataclass
class HerbrandTerm:
    term: Any
    exact_type: str
    depth: int
    constructions: list[HerbrandConstruction]


@dataclass(frozen=True)
class FiniteHerbrandWitness:
    signatures: tuple[HerbrandSignature, ...]
    subtypes: tuple[GdlSubtypeStatement, ...]
    accepted_types: frozenset[tuple[str, str]]
    terms: tuple[HerbrandTerm, ...]
    relation_indices: tuple[int, ...]
    type_count: int
    subtype_edge_count: int
    constructor_applications: int
    rounds: int
    maximum_depth: int


def _signatures(profile: GdlTypeProfile) -> tuple[HerbrandSignature, ...]:
    result: list[HerbrandSignature] = []
    for statement_ordinal, statement in enumerate(profile.statements):
        if not isinstance(statement, GdlSignatureStatement):
            continue
        for name_ordinal, name in enumerate(statement.names):
            result.append(
                HerbrandSignature(
                    statement_ordinal,
                    name_ordinal,
                    name,
                    statement.argument_types,
                    statement.result_type,
                )
            )
    return tuple(result)


def _accepted_types(
    profile: GdlTypeProfile,
    signatures: tuple[HerbrandSignature, ...],
) -> tuple[set[str], frozenset[tuple[str, str]]]:
    types: set[str] = set()
    for signature in signatures:
        types.add(signature.result_type)
        types.update(signature.argument_types)
    for subtype in profile.subtypes:
        types.add(subtype.subtype)
        types.add(subtype.supertype)
    accepted = {(type_name, type_name) for type_name in types}
    accepted.update(
        (subtype.subtype, subtype.supertype)
        for subtype in profile.subtypes
    )
    for middle in tuple(types):
        for actual in tuple(types):
            if (actual, middle) not in accepted:
                continue
            for expected in tuple(types):
                if (middle, expected) in accepted:
                    accepted.add((actual, expected))
    return types, frozenset(accepted)


def _constant(name: str) -> str | int:
    return int(name) if INTEGER.fullmatch(name) else name


def construct_finite_herbrand(
    profile: GdlTypeProfile,
) -> FiniteHerbrandWitness:
    """Construct the least typed ground carrier with all finite witnesses."""

    signatures = _signatures(profile)
    types, accepted = _accepted_types(profile, signatures)
    terms: list[HerbrandTerm] = []
    positions: dict[tuple[Any, str], int] = {}

    def retain(
        term: Any,
        exact_type: str,
        depth: int,
        construction: HerbrandConstruction,
    ) -> bool:
        key = term, exact_type
        existing = positions.get(key)
        if existing is None:
            positions[key] = len(terms)
            terms.append(
                HerbrandTerm(term, exact_type, depth, [construction])
            )
            return True
        if construction not in terms[existing].constructions:
            terms[existing].constructions.append(construction)
        return False

    for signature_index, signature in enumerate(signatures):
        if signature.relation or signature.argument_types:
            continue
        retain(
            _constant(signature.name),
            signature.result_type,
            0,
            HerbrandConstruction(signature_index, ()),
        )

    delta_start = 0
    applications = 0
    rounds = 0
    while delta_start < len(terms):
        snapshot = len(terms)
        rounds += 1
        for signature_index, signature in enumerate(signatures):
            if signature.relation or not signature.argument_types:
                continue
            candidates = tuple(
                tuple(
                    index
                    for index, term in enumerate(terms[:snapshot])
                    if (term.exact_type, expected_type) in accepted
                )
                for expected_type in signature.argument_types
            )
            if any(not choices for choices in candidates):
                continue
            for arguments in product(*candidates):
                if not any(index >= delta_start for index in arguments):
                    continue
                applications += 1
                value = (
                    signature.name,
                    *(terms[index].term for index in arguments),
                )
                retain(
                    value,
                    signature.result_type,
                    1 + max(terms[index].depth for index in arguments),
                    HerbrandConstruction(signature_index, arguments),
                )
        if len(terms) == snapshot:
            break
        delta_start = snapshot

    return FiniteHerbrandWitness(
        signatures=signatures,
        subtypes=profile.subtypes,
        accepted_types=accepted,
        terms=tuple(terms),
        relation_indices=tuple(
            index
            for index, signature in enumerate(signatures)
            if signature.relation
        ),
        type_count=len(types),
        subtype_edge_count=len(profile.subtypes),
        constructor_applications=applications,
        rounds=rounds,
        maximum_depth=max((term.depth for term in terms), default=0),
    )


def render_term(term: Any) -> str:
    """Render the profile's structural term subset as canonical MeTTa."""

    if isinstance(term, tuple):
        return "(" + " ".join(render_term(item) for item in term) + ")"
    return str(term)


def finite_herbrand_term_inhabits(
    witness: FiniteHerbrandWitness,
    term: Any,
    expected_type: str,
) -> bool:
    """Whether one ground term inhabits a type in the finite profile image.

    Dataset atoms retain integer tokens as strings, while the finite carrier
    stores them as integers.  Canonicalizing only that lexical distinction
    keeps this predicate about the typed carrier rather than the dataset's
    spelling.
    """

    def canonical(value: Any) -> Any:
        if isinstance(value, tuple):
            return tuple(canonical(item) for item in value)
        if isinstance(value, str) and INTEGER.fullmatch(value):
            return int(value)
        return value

    value = canonical(term)
    return any(
        item.term == value
        and (item.exact_type, expected_type) in witness.accepted_types
        for item in witness.terms
    )
