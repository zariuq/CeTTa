"""Constructive finite-state evidence for the exact GDL ``true`` view.

This layer does not interpret failed search as negation.  Authored ``base``
declarations define a finite state domain.  A complete episode occurrence bag
then partitions that domain into present facts and explicitly witnessed
absences.  The resulting absence proofs are ordinary positive premises for
the existing proof-relevant Horn machinery.

Parsing and representation remain authority-free.  A runtime may license this
construction only when its source and episode identities are current.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from prime_iggp_positive_horn import (
    PositiveHornBlock,
    PositiveHornBoundary,
    PositiveHornProgram,
    episode_fact_blocks,
    episode_term_blocks,
    encode_gdl_dataset_application,
    encode_positive_horn_dataset_source,
    structural_positive_horn_source,
)
from prime_iggp_presentation import (
    GdlFormOccurrence,
    GdlSourcePresentation,
)


ABSENCE_HEAD_PREFIX = "gdl:finite-relation-absent-v1:"


class FiniteViewBoundary(PositiveHornBoundary):
    """The source or episode is outside the finite-state exact image."""


@dataclass(frozen=True)
class FiniteStateDomainMember:
    """One occurrence of a source-declared member of the ``true`` domain."""

    source: Any
    authored_member: Any
    authored_literal: Any
    represented_literal: Any
    absence_goal: Any


@dataclass(frozen=True)
class FiniteViewProgram:
    """A positive program plus its constructive finite-state capability."""

    positive_program: PositiveHornProgram
    domain: tuple[FiniteStateDomainMember, ...]
    negative_premise_count: int
    structural: bool

    @property
    def blocks(self) -> tuple[PositiveHornBlock, ...]:
        return self.positive_program.blocks


@dataclass(frozen=True)
class FiniteViewEpisode:
    """The proof-relevant present/absent partition of one complete episode."""

    receipt: Any
    positive_blocks: tuple[PositiveHornBlock, ...]
    absence_blocks: tuple[PositiveHornBlock, ...]

    @property
    def blocks(self) -> tuple[PositiveHornBlock, ...]:
        return self.positive_blocks + self.absence_blocks


def _is_variable(term: Any) -> bool:
    return isinstance(term, str) and term.startswith("?")


def _variables(term: Any) -> set[str]:
    if _is_variable(term):
        return {term}
    if not isinstance(term, tuple):
        return set()
    return set().union(*(_variables(item) for item in term)) if term else set()


def _source_identity(ordinal: int, occurrence: GdlFormOccurrence) -> Any:
    return (
        "gdl:source-occurrence",
        str(ordinal),
        str(occurrence.start_line),
        str(occurrence.end_line),
    )


def finite_absence_goal(represented_literal: Any) -> Any:
    """Name the positive evidence family for absence of one represented term."""

    if isinstance(represented_literal, str):
        return ABSENCE_HEAD_PREFIX + represented_literal
    if (
        not isinstance(represented_literal, tuple)
        or not represented_literal
        or not isinstance(represented_literal[0], str)
    ):
        raise FiniteViewBoundary("finite-state literal is malformed")
    return (
        ABSENCE_HEAD_PREFIX + represented_literal[0],
        *represented_literal[1:],
    )


def _rewrite_finite_negative(
    premise: Any,
    *,
    bound: set[str],
    domain_signatures: set[tuple[str, int]],
    domain_literals: set[Any],
    structural: bool,
) -> tuple[Any, bool]:
    if not isinstance(premise, tuple) or not premise:
        return premise, False
    if premise[0] == "or":
        raise FiniteViewBoundary("disjunction is outside the finite-view slice")
    if premise[0] != "not":
        return premise, False
    if len(premise) != 2:
        raise FiniteViewBoundary("not does not have exactly one operand")
    operand = premise[1]
    if (
        not isinstance(operand, tuple)
        or len(operand) != 2
        or operand[0] != "true"
    ):
        raise FiniteViewBoundary(
            "only absence from the source-declared true domain is covered"
        )
    missing = _variables(operand) - bound
    if missing:
        raise FiniteViewBoundary(
            "finite-state absence may test only variables bound by earlier "
            "positive premises"
        )
    represented = operand if structural else encode_gdl_dataset_application(operand)
    signature = (
        (represented, 0)
        if isinstance(represented, str)
        else (represented[0], len(represented) - 1)
    )
    if signature not in domain_signatures:
        raise FiniteViewBoundary(
            "negative true premise has no source-declared finite domain"
        )
    if not _variables(operand) and represented not in domain_literals:
        raise FiniteViewBoundary(
            "ground negative true premise is outside the declared base domain"
        )
    return finite_absence_goal(represented), True


def _finite_view_source(
    presentation: GdlSourcePresentation, *, structural: bool
) -> FiniteViewProgram:
    """Compile range-restricted ``not (true ...)`` using explicit absence.

    ``base`` declarations are the complete finite domain for episode ``true``
    facts.  Every negative variable must already be bound by an earlier
    positive premise.  All other negative and disjunctive shapes abstain at
    this boundary rather than being approximated.
    """

    if presentation.foreign_code:
        raise FiniteViewBoundary(
            "mixed GDL/foreign source requires a separately admitted route"
        )

    domain: list[FiniteStateDomainMember] = []
    for ordinal, occurrence in enumerate(presentation.forms, 1):
        form = occurrence.form
        if not form or form[0] != "base":
            continue
        if len(form) != 2 or _variables(form[1]):
            raise FiniteViewBoundary(
                "base declaration is not a ground unary state member"
            )
        authored_literal = ("true", form[1])
        represented_literal = (
            authored_literal
            if structural
            else encode_gdl_dataset_application(authored_literal)
        )
        domain.append(
            FiniteStateDomainMember(
                source=_source_identity(ordinal, occurrence),
                authored_member=form[1],
                authored_literal=authored_literal,
                represented_literal=represented_literal,
                absence_goal=finite_absence_goal(represented_literal),
            )
        )
    if not domain:
        raise FiniteViewBoundary(
            "source has no ground base declarations for a finite state view"
        )

    def signature(term: Any) -> tuple[str, int]:
        if isinstance(term, str):
            return (term, 0)
        return (term[0], len(term) - 1)

    domain_signatures = {
        signature(member.represented_literal) for member in domain
    }
    domain_literals = {member.represented_literal for member in domain}
    rewritten: list[GdlFormOccurrence] = []
    negative_count = 0
    for occurrence in presentation.forms:
        form = occurrence.form
        if not form or form[0] != "<=":
            rewritten.append(occurrence)
            continue
        if len(form) < 2:
            raise FiniteViewBoundary("GDL rule has no conclusion")
        bound: set[str] = set()
        body: list[Any] = []
        for premise in form[2:]:
            replacement, was_negative = _rewrite_finite_negative(
                premise,
                bound=bound,
                domain_signatures=domain_signatures,
                domain_literals=domain_literals,
                structural=structural,
            )
            body.append(replacement)
            if was_negative:
                negative_count += 1
            elif not (
                isinstance(premise, tuple)
                and premise
                and premise[0] == "distinct"
            ):
                bound.update(_variables(premise))
        rewritten.append(
            GdlFormOccurrence(
                occurrence.start_line,
                occurrence.end_line,
                ("<=", form[1], *body),
            )
        )
    if negative_count == 0:
        raise FiniteViewBoundary(
            "source contains no covered finite-state negative premise"
        )

    rewritten_presentation = GdlSourcePresentation(tuple(rewritten), ())
    positive_program = (
        structural_positive_horn_source(rewritten_presentation)
        if structural
        else encode_positive_horn_dataset_source(rewritten_presentation)
    )
    return FiniteViewProgram(
        positive_program=positive_program,
        domain=tuple(domain),
        negative_premise_count=negative_count,
        structural=structural,
    )


def encode_finite_view_dataset_source(
    presentation: GdlSourcePresentation,
) -> FiniteViewProgram:
    """Compile the finite view in the flattened dataset representation."""

    return _finite_view_source(presentation, structural=False)


def structural_finite_view_source(
    presentation: GdlSourcePresentation,
) -> FiniteViewProgram:
    """Compile the finite view while retaining authored constructor terms."""

    return _finite_view_source(presentation, structural=True)


def _is_true_relation(term: Any, *, structural: bool) -> bool:
    head = term if isinstance(term, str) else term[0]
    return head == "true" if structural else (
        head == "true" or head.startswith("true_")
    )


def _construct_finite_state_view(
    program: FiniteViewProgram,
    episode_identity: Any,
    positive_blocks: tuple[PositiveHornBlock, ...],
) -> FiniteViewEpisode:
    """Construct explicit absence evidence beside positive occurrences.

    ``positive_blocks`` is the complete ordered episode bag supplied by the
    caller's current snapshot.  Duplicate positive occurrences are retained.
    A ``true`` fact outside the authored ``base`` domain is outside this exact
    image and cannot be turned into either presence or absence evidence.
    """

    domain_literals = {
        member.represented_literal for member in program.domain
    }
    for block in positive_blocks:
        if (
            _is_true_relation(
                block.conclusion, structural=program.structural
            )
            and block.conclusion not in domain_literals
        ):
            raise FiniteViewBoundary(
                "episode true fact is outside the source-declared base domain"
            )

    present = {block.conclusion for block in positive_blocks}
    receipt = (
        "gdl:complete-finite-relation-view-v1",
        episode_identity,
        (
            "gdl:domain-occurrences",
            *(member.source for member in program.domain),
        ),
        (
            "gdl:ordered-episode-facts",
            *(block.conclusion for block in positive_blocks),
        ),
    )
    absence_blocks: list[PositiveHornBlock] = []
    for member in program.domain:
        if member.represented_literal in present:
            continue
        ordinal = len(absence_blocks) + 1
        source = (
            "gdl:finite-view-absence",
            receipt,
            member.source,
            str(ordinal),
        )
        absence_blocks.append(
            PositiveHornBlock(
                identity=f"gdl-finite-absence-{ordinal:04d}",
                source=source,
                proof=(
                    "gdl:finite-absence-proof",
                    source,
                    ("gdl:declared-member", member.authored_member),
                    ("gdl:absent-literal", member.represented_literal),
                ),
                premises=(),
                conclusion=member.absence_goal,
            )
        )
    return FiniteViewEpisode(
        receipt=receipt,
        positive_blocks=positive_blocks,
        absence_blocks=tuple(absence_blocks),
    )


def construct_finite_state_view(
    program: FiniteViewProgram,
    episode_identity: Any,
    facts: Iterable[str],
) -> FiniteViewEpisode:
    """Construct a complete view in the flattened dataset representation."""

    if program.structural:
        raise FiniteViewBoundary(
            "structural finite view requires structural episode terms"
        )
    return _construct_finite_state_view(
        program,
        episode_identity,
        episode_fact_blocks(episode_identity, facts),
    )


def construct_structural_finite_state_view(
    program: FiniteViewProgram,
    episode_identity: Any,
    terms: Iterable[Any],
) -> FiniteViewEpisode:
    """Construct a complete view over authored structural episode terms."""

    if not program.structural:
        raise FiniteViewBoundary(
            "dataset finite view requires represented episode facts"
        )
    return _construct_finite_state_view(
        program,
        episode_identity,
        episode_term_blocks(episode_identity, terms),
    )
