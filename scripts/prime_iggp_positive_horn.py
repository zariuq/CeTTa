"""Proof-relevant execution of the exact positive-Horn GDL fragment.

Structural compilation is deliberately authority-free.  It preserves source
occurrences, constructor terms, premise order, and proof multiplicity while
producing ordinary RuleMachine blocks.  The IGGP dataset encoding is a
separate, explicitly lossy boundary representation.  A later admission step
may license a native realization for one source/profile revision; neither
parsing nor representation creates that license.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import product
from typing import Any, Iterable

from prime_iggp_finite_herbrand import (
    FiniteHerbrandWitness,
    finite_herbrand_term_inhabits,
)
from prime_iggp_generation import GroundAtom, parse_ground_atom
from prime_iggp_presentation import (
    GdlFiniteTypedOccurrenceProjection,
    GdlSourcePresentation,
    GdlTypeProfile,
)


class PositiveHornBoundary(RuntimeError):
    """The authored source is outside this exact execution fragment."""


@dataclass(frozen=True)
class PositiveHornPremise:
    proof_variable: str
    goal: Any


@dataclass(frozen=True)
class PositiveHornBlock:
    identity: str
    source: Any
    proof: Any
    premises: tuple[PositiveHornPremise, ...]
    conclusion: Any


@dataclass(frozen=True)
class PositiveHornProgram:
    blocks: tuple[PositiveHornBlock, ...]
    source_form_count: int
    source_rule_count: int
    source_fact_count: int
    distinct_premise_count: int


@dataclass(frozen=True)
class GdlDatasetRepresentationTemplate:
    """One source-derived partial inverse for the dataset representation."""

    authored: Any
    represented: Any
    constructor_views: tuple["GdlDatasetConstructorView", ...] = ()


@dataclass(frozen=True)
class GdlDatasetConstructorView:
    """One checked partial constructor view at the dataset boundary.

    The dataset constructor embeds into the source constructor argument by
    argument.  Projection back to the dataset is defined only on source terms
    whose arguments inhabit the narrower represented types.
    """

    source_name: str
    represented_name: str
    source_argument_types: tuple[str, ...]
    represented_argument_types: tuple[str, ...]
    result_type: str


@dataclass(frozen=True)
class PositiveHornReferenceAnswer:
    """One proof occurrence produced from the authored Horn semantics."""

    conclusion: Any
    proof: Any


@dataclass(frozen=True)
class PositiveHornReferenceRun:
    """Ordered proof bag and work accounting for the independent oracle."""

    answers: tuple[PositiveHornReferenceAnswer, ...]
    states: int
    block_attempts: int
    block_matches: int


def _is_variable(term: Any) -> bool:
    return isinstance(term, str) and term.startswith("?")


def _variables(term: Any) -> set[str]:
    if _is_variable(term):
        return {term}
    if not isinstance(term, tuple):
        return set()
    return set().union(*(_variables(item) for item in term)) if term else set()


def _variable_map(terms: Iterable[Any]) -> dict[str, str]:
    result: dict[str, str] = {}

    def visit(term: Any) -> None:
        if _is_variable(term):
            if term not in result:
                result[term] = f"$gdl-v{len(result)}"
            return
        if isinstance(term, tuple):
            for item in term:
                visit(item)

    for term in terms:
        visit(term)
    return result


def _rename_variables(term: Any, variables: dict[str, str]) -> Any:
    if _is_variable(term):
        return variables[term]
    if isinstance(term, tuple):
        return tuple(_rename_variables(item, variables) for item in term)
    return term


def encode_gdl_dataset_application(
    application: Any,
    variables: dict[str, str] | None = None,
    *,
    constructor_views: Iterable[GdlDatasetConstructorView] = (),
    carrier: FiniteHerbrandWitness | None = None,
) -> Any:
    """Encode one nested GDL application using the dataset convention.

    For example, ``(legal p (move x y))`` becomes
    ``(legal_move p x y)``.  This preserves authored leaf order but may
    identify distinct constructor trees; it is not the native semantics.
    """

    variable_names = variables or {}
    views = tuple(constructor_views)

    def view_for(constructor: Any) -> GdlDatasetConstructorView | None:
        if not isinstance(constructor, tuple) or not constructor:
            return None
        candidates = tuple(
            view
            for view in views
            if view.source_name == constructor[0]
            and len(view.source_argument_types) == len(constructor) - 1
        )
        if len(candidates) > 1:
            raise PositiveHornBoundary(
                "source constructor has multiple dataset views"
            )
        return candidates[0] if candidates else None

    def validate_view(
        constructor: tuple[Any, ...], view: GdlDatasetConstructorView
    ) -> None:
        if carrier is None:
            return
        if not all(
            finite_herbrand_term_inhabits(carrier, argument, expected_type)
            for argument, expected_type in zip(
                constructor[1:], view.represented_argument_types
            )
        ):
            raise PositiveHornBoundary(
                "source constructor lies outside its typed dataset view"
            )

    if isinstance(application, str):
        return variable_names.get(application, application)
    if not isinstance(application, tuple) or not application:
        raise PositiveHornBoundary("GDL application is empty or malformed")
    if not isinstance(application[0], str) or _is_variable(application[0]):
        raise PositiveHornBoundary("dynamic GDL relation heads are outside the fragment")

    heads: list[str] = [application[0]]
    arguments: list[Any] = []

    def encode_argument(argument: Any) -> None:
        if isinstance(argument, tuple):
            if not argument or not isinstance(argument[0], str) or _is_variable(argument[0]):
                raise PositiveHornBoundary(
                    "dynamic GDL constructor heads are outside the fragment"
                )
            view = view_for(argument)
            if view is not None:
                validate_view(argument, view)
            heads.append(
                view.represented_name if view is not None else argument[0]
            )
            for nested in argument[1:]:
                encode_argument(nested)
            return
        arguments.append(variable_names.get(argument, argument))

    for argument in application[1:]:
        encode_argument(argument)
    head = "_".join(heads)
    return head if not arguments else (head, *arguments)


def gdl_dataset_constructor_views(
    profile: GdlTypeProfile,
    typed_source: GdlFiniteTypedOccurrenceProjection,
    carrier: FiniteHerbrandWitness,
) -> tuple[GdlDatasetConstructorView, ...]:
    """Derive unambiguous typed views from the IGGP action-name convention.

    Some authored games distinguish a source constructor with an ``_action``
    suffix while their dataset profile publishes the suffix-free constructor.
    The spelling only proposes a view.  A view exists only when one distinct
    profile signature has the same arity and result type, and each represented
    argument type embeds into the source argument type.  Incomparable or
    overloaded proposals yield no view.
    """

    result: list[GdlDatasetConstructorView] = []
    for source in typed_source.derived_signatures:
        if not source.name.endswith("_action"):
            continue
        represented_name = source.name.removesuffix("_action")
        if not represented_name:
            continue
        candidates = {
            (statement.argument_types, statement.result_type)
            for statement in profile.signatures
            if represented_name in statement.names
            and len(statement.argument_types) == source.arity
            and statement.result_type == source.result_type
            and all(
                (represented_type, source_type) in carrier.accepted_types
                for represented_type, source_type in zip(
                    statement.argument_types, source.argument_types
                )
            )
        }
        if len(candidates) != 1:
            continue
        represented_arguments, result_type = next(iter(candidates))
        result.append(
            GdlDatasetConstructorView(
                source_name=source.name,
                represented_name=represented_name,
                source_argument_types=source.argument_types,
                represented_argument_types=represented_arguments,
                result_type=result_type,
            )
        )
    return tuple(result)


def gdl_dataset_representation_templates(
    presentation: GdlSourcePresentation,
    constructor_views: Iterable[GdlDatasetConstructorView] = (),
    *,
    form_ordinals: frozenset[int] | None = None,
    profile: GdlTypeProfile | None = None,
) -> tuple[GdlDatasetRepresentationTemplate, ...]:
    """Derive the exact authored image of positive-Horn applications.

    A target-level ground relation is lifted only when the authored source
    itself supplies an unambiguous structural template for it.  This is a
    partial inverse of :func:`encode_gdl_dataset_application`, not a naming
    heuristic.  GDL's authored ``base`` declarations induce the episode
    interface ``true``.  Authored ``input`` declarations and state-indexed
    ``legal`` rules both supply action shapes for the episode interface
    ``does``.  A supplied type profile likewise composes declared Boolean
    operations with typed constructor arguments.  Thus ``true``/``next``
    compose with proposition constructors and ``legal``/``does`` compose with
    action constructors by the same rule.  Deriving those templates here keeps
    the external-state boundary compositional even when a declared action or
    proposition is not inspected by any transition rule; it does not assert
    that an action is legal in every state or that a proposition is derivable.
    """

    templates: list[GdlDatasetRepresentationTemplate] = []
    views = tuple(constructor_views)

    def add(authored: Any) -> None:
        exact = GdlDatasetRepresentationTemplate(
            authored,
            encode_gdl_dataset_application(authored),
        )
        if exact not in templates:
            templates.append(exact)
        if views:
            represented = encode_gdl_dataset_application(
                authored, constructor_views=views
            )
            if represented != exact.represented:
                viewed = GdlDatasetRepresentationTemplate(
                    authored, represented, views
                )
                if viewed not in templates:
                    templates.append(viewed)

    if form_ordinals is not None and any(
        ordinal < 0 or ordinal >= len(presentation.forms)
        for ordinal in form_ordinals
    ):
        raise PositiveHornBoundary(
            "dataset representation selects a missing source form"
        )
    for ordinal, occurrence in enumerate(presentation.forms):
        if form_ordinals is not None and ordinal not in form_ordinals:
            continue
        conclusion, premises = _positive_rule_parts(occurrence.form)
        for application in (conclusion, *premises):
            if (
                isinstance(application, tuple)
                and application
                and application[0] in {"distinct", "not", "or"}
            ):
                continue
            add(application)
            if isinstance(application, tuple) and application:
                if application[0] == "base" and len(application) == 2:
                    add(("true", application[1]))
                elif (
                    application[0] in {"input", "legal"}
                    and len(application) == 3
                ):
                    add(("does", application[1], application[2]))
    if profile is not None:
        subtype_edges: dict[str, set[str]] = {}
        for statement in profile.subtypes:
            subtype_edges.setdefault(statement.subtype, set()).add(
                statement.supertype
            )

        def embeds(source_type: str, target_type: str) -> bool:
            frontier = [source_type]
            visited: set[str] = set()
            while frontier:
                current = frontier.pop()
                if current == target_type:
                    return True
                if current in visited:
                    continue
                visited.add(current)
                frontier.extend(subtype_edges.get(current, ()))
            return False

        constructors: list[tuple[str, tuple[str, ...], str]] = []
        for statement in profile.signatures:
            if not statement.argument_types:
                continue
            for name in statement.names:
                source_names = tuple(
                    view.source_name
                    for view in views
                    if view.represented_name == name
                    and len(view.source_argument_types)
                    == len(statement.argument_types)
                    and view.result_type == statement.result_type
                )
                for source_name in source_names or (name,):
                    constructor = (
                        source_name,
                        statement.argument_types,
                        statement.result_type,
                    )
                    if constructor not in constructors:
                        constructors.append(constructor)

        for statement_ordinal, statement in enumerate(profile.signatures):
            if not embeds(statement.result_type, "bool"):
                continue
            argument_options = tuple(
                (
                    None,
                    *(
                        constructor
                        for constructor in constructors
                        if embeds(constructor[2], expected_type)
                    ),
                )
                for expected_type in statement.argument_types
            )
            for name_ordinal, name in enumerate(statement.names):
                for shape_ordinal, shape in enumerate(
                    product(*argument_options)
                ):
                    if not any(option is not None for option in shape):
                        continue
                    arguments: list[Any] = []
                    for argument_ordinal, option in enumerate(shape):
                        prefix = (
                            f"?gdl-profile-s{statement_ordinal}-"
                            f"n{name_ordinal}-h{shape_ordinal}-"
                            f"a{argument_ordinal}"
                        )
                        if option is None:
                            arguments.append(prefix)
                            continue
                        constructor_name, constructor_types, _ = option
                        constructor_arguments = tuple(
                            f"{prefix}-c{constructor_ordinal}"
                            for constructor_ordinal in range(
                                len(constructor_types)
                            )
                        )
                        arguments.append(
                            (constructor_name, *constructor_arguments)
                        )
                    add(name if not arguments else (name, *arguments))
    return tuple(templates)


def decode_gdl_dataset_ground_application(
    templates: Iterable[GdlDatasetRepresentationTemplate],
    dataset_ground: Any,
    *,
    carrier: FiniteHerbrandWitness | None = None,
) -> Any:
    """Decode one dataset atom through a unique authored source template."""

    def match(pattern: Any, value: Any, bindings: dict[str, Any]) -> bool:
        if _is_variable(pattern):
            previous = bindings.get(pattern)
            if previous is None:
                bindings[pattern] = value
                return True
            return previous == value
        if isinstance(pattern, tuple):
            return (
                isinstance(value, tuple)
                and len(pattern) == len(value)
                and all(
                    match(pattern_item, value_item, bindings)
                    for pattern_item, value_item in zip(pattern, value)
                )
            )
        if (
            isinstance(pattern, str)
            and isinstance(value, int)
            and pattern.lstrip("-").isdigit()
        ):
            return int(pattern) == value
        if (
            isinstance(pattern, int)
            and isinstance(value, str)
            and value.lstrip("-").isdigit()
        ):
            return pattern == int(value)
        return pattern == value

    def instantiate(term: Any, bindings: dict[str, Any]) -> Any:
        if _is_variable(term):
            return bindings[term]
        if isinstance(term, tuple):
            return tuple(instantiate(item, bindings) for item in term)
        return term

    def in_typed_view(
        term: Any, views: tuple[GdlDatasetConstructorView, ...]
    ) -> bool:
        if not isinstance(term, tuple) or not term:
            return True
        candidates = tuple(
            view
            for view in views
            if view.source_name == term[0]
            and len(view.source_argument_types) == len(term) - 1
        )
        if len(candidates) > 1:
            return False
        if candidates:
            view = candidates[0]
            if carrier is None:
                raise PositiveHornBoundary(
                    "typed dataset view lacks its finite carrier"
                )
            if not all(
                finite_herbrand_term_inhabits(
                    carrier, argument, expected_type
                )
                for argument, expected_type in zip(
                    term[1:], view.represented_argument_types
                )
            ):
                return False
        return all(in_typed_view(item, views) for item in term[1:])

    lifted: list[Any] = []
    for template in templates:
        bindings: dict[str, Any] = {}
        if match(template.represented, dataset_ground, bindings):
            candidate = instantiate(template.authored, bindings)
            if not in_typed_view(candidate, template.constructor_views):
                continue
            if candidate not in lifted:
                lifted.append(candidate)
    if not lifted:
        raise PositiveHornBoundary(
            "dataset fact lies outside the authored representation image"
        )
    if len(lifted) != 1:
        raise PositiveHornBoundary(
            "ground target fact has multiple authored lifting images"
        )
    return lifted[0]


def encode_gdl_dataset_ground_applications(
    source_ground: Any,
    *,
    constructor_views: Iterable[GdlDatasetConstructorView] = (),
    carrier: FiniteHerbrandWitness | None = None,
) -> tuple[Any, ...]:
    """Return the structural quotient and every defined typed-view quotient."""

    represented = [encode_gdl_dataset_application(source_ground)]
    views = tuple(constructor_views)
    if views:
        try:
            viewed = encode_gdl_dataset_application(
                source_ground,
                constructor_views=views,
                carrier=carrier,
            )
        except PositiveHornBoundary:
            viewed = None
        if viewed is not None and viewed not in represented:
            represented.append(viewed)
    return tuple(represented)


def decode_gdl_dataset_query_fibres(
    templates: Iterable[GdlDatasetRepresentationTemplate],
    dataset_query: Any,
) -> tuple[Any, ...]:
    """Lift a represented query to its exact authored query fibres.

    Dataset relations flatten nested GDL constructors into their predicate
    names.  A represented signature can therefore denote several disjoint
    authored term shapes.  Source variables remain query variables, while
    authored constants and constructor heads remain constraints.  Returning
    every distinct fibre avoids widening a represented slice into an unrelated
    structural query.
    """

    def signature(term: Any) -> tuple[str, int] | None:
        if isinstance(term, str):
            return (term, 0)
        if (
            isinstance(term, tuple)
            and term
            and isinstance(term[0], str)
        ):
            return (term[0], len(term) - 1)
        return None

    query_signature = signature(dataset_query)
    if query_signature is None:
        raise PositiveHornBoundary("target query has no relation signature")
    lifted: list[Any] = []
    for template in templates:
        if signature(template.represented) != query_signature:
            continue
        variables = _variable_map((template.authored,))
        represented_pattern = _rename_variables(
            template.represented, variables
        )
        authored_pattern = _rename_variables(template.authored, variables)
        substitution = _reference_unify(
            represented_pattern, dataset_query, {}
        )
        if substitution is None:
            continue
        candidate = _reference_apply(authored_pattern, substitution)
        if candidate not in lifted:
            lifted.append(candidate)
    if not lifted:
        return (dataset_query,)
    return tuple(lifted)


def decode_gdl_dataset_target_query_fibres(
    templates: Iterable[GdlDatasetRepresentationTemplate],
    dataset_query: Any,
    source_relation: str,
) -> tuple[Any, ...]:
    """Lift a dataset query inside one explicitly named source relation.

    Flattening can identify ``next (at ...)`` with an authored auxiliary
    relation named ``next_at``.  A task target retains the outer source
    judgment and therefore selects the corresponding fibre without deleting
    either authored relation.
    """

    fibres = tuple(
        fibre
        for fibre in decode_gdl_dataset_query_fibres(
            templates, dataset_query
        )
        if (
            fibre == source_relation
            or (
                isinstance(fibre, tuple)
                and fibre
                and fibre[0] == source_relation
            )
        )
    )
    if not fibres:
        raise PositiveHornBoundary(
            "dataset target has no authored source-relation fibre"
        )
    return fibres


def _positive_rule_parts(form: tuple[Any, ...]) -> tuple[Any, tuple[Any, ...]]:
    if form and form[0] == "<=":
        if len(form) < 2:
            raise PositiveHornBoundary("GDL rule has no conclusion")
        return form[1], tuple(form[2:])
    return form, ()


def _positive_horn_source(
    presentation: GdlSourcePresentation, *, structural: bool
) -> PositiveHornProgram:
    """Compile an authored positive-Horn source in one representation.

    Foreign source, negation, disjunction, unsafe heads, and generative
    ``distinct`` uses stay outside this exact image.  Structural ``distinct``
    is accepted only after earlier premises bind all of its operands; its
    finite evidence facts are supplied separately by
    :func:`distinct_evidence_blocks`.
    """

    if presentation.foreign_code:
        raise PositiveHornBoundary(
            "mixed GDL/foreign source requires a separately admitted route"
        )

    blocks: list[PositiveHornBlock] = []
    rule_count = 0
    fact_count = 0
    distinct_count = 0
    for ordinal, occurrence in enumerate(presentation.forms):
        form = occurrence.form
        conclusion, premises = _positive_rule_parts(form)
        variables = _variable_map((conclusion, *premises))
        bound: set[str] = set()
        for premise in premises:
            if isinstance(premise, tuple) and premise:
                if premise[0] in {"not", "or"}:
                    raise PositiveHornBoundary(
                        f"source form {ordinal + 1} uses {premise[0]}"
                    )
                if premise[0] == "distinct":
                    distinct_count += 1
                    unbound = _variables(premise) - bound
                    if unbound:
                        raise PositiveHornBoundary(
                            "structural distinct may test only variables bound "
                            "by earlier positive premises"
                        )
                    continue
            bound.update(_variables(premise))

        head_variables = _variables(conclusion)
        if premises and not head_variables.issubset(bound):
            raise PositiveHornBoundary(
                f"source form {ordinal + 1} has an unsafe rule head"
            )
        if not premises and head_variables:
            raise PositiveHornBoundary(
                f"source form {ordinal + 1} is a nonground fact"
            )

        authored = _rename_variables(conclusion, variables)
        represented = (
            authored
            if structural
            else encode_gdl_dataset_application(conclusion, variables)
        )
        source = (
            "gdl:source-occurrence",
            str(ordinal + 1),
            str(occurrence.start_line),
            str(occurrence.end_line),
        )
        premise_values = tuple(
            PositiveHornPremise(
                f"$gdl-p{index}",
                _rename_variables(premise, variables)
                if structural
                else encode_gdl_dataset_application(premise, variables),
            )
            for index, premise in enumerate(premises)
        )
        proof_head = "gdl:rule" if premises else "gdl:fact"
        proof = (
            (
                proof_head,
                source,
                ("gdl:structural-judgment", represented),
                (
                    "gdl:premises",
                    *(item.proof_variable for item in premise_values),
                ),
            )
            if structural
            else (
                proof_head,
                source,
                ("gdl:authored", authored),
                ("gdl:dataset-representation", represented),
                (
                    "gdl:premises",
                    *(item.proof_variable for item in premise_values),
                ),
            )
        )
        blocks.append(
            PositiveHornBlock(
                identity=f"gdl-source-{ordinal + 1:04d}",
                source=source,
                proof=proof,
                premises=premise_values,
                conclusion=represented,
            )
        )
        if premises:
            rule_count += 1
        else:
            fact_count += 1

    return PositiveHornProgram(
        blocks=tuple(blocks),
        source_form_count=len(presentation.forms),
        source_rule_count=rule_count,
        source_fact_count=fact_count,
        distinct_premise_count=distinct_count,
    )


def encode_positive_horn_dataset_source(
    presentation: GdlSourcePresentation,
) -> PositiveHornProgram:
    """Compile to the flattened representation used by the IGGP dataset."""

    return _positive_horn_source(presentation, structural=False)


def structural_positive_horn_source(
    presentation: GdlSourcePresentation,
) -> PositiveHornProgram:
    """Compile authored terms without quotienting nested constructors."""

    return _positive_horn_source(presentation, structural=True)


def ground_atom_term(atom: GroundAtom) -> Any:
    return atom.head if not atom.args else (
        atom.head,
        *(ground_atom_term(argument) for argument in atom.args),
    )


def episode_fact_blocks(
    episode_identity: Any, facts: Iterable[str]
) -> tuple[PositiveHornBlock, ...]:
    """Construct source-occurrence proofs for one finite episode delta."""

    result: list[PositiveHornBlock] = []
    for ordinal, text in enumerate(facts, 1):
        conclusion = ground_atom_term(parse_ground_atom(text))
        source = ("gdl:episode-occurrence", episode_identity, str(ordinal))
        result.append(
            PositiveHornBlock(
                identity=f"gdl-episode-{ordinal:04d}",
                source=source,
                proof=("gdl:episode-fact", source, conclusion),
                premises=(),
                conclusion=conclusion,
            )
        )
    return tuple(result)


def episode_term_blocks(
    episode_identity: Any, terms: Iterable[Any]
) -> tuple[PositiveHornBlock, ...]:
    """Construct occurrence proofs for an ordered bag of structural terms."""

    result: list[PositiveHornBlock] = []
    for ordinal, conclusion in enumerate(terms, 1):
        if _variables(conclusion):
            raise PositiveHornBoundary("episode term is not ground")
        source = ("gdl:episode-occurrence", episode_identity, str(ordinal))
        result.append(
            PositiveHornBlock(
                identity=f"gdl-episode-{ordinal:04d}",
                source=source,
                proof=("gdl:episode-fact", source, conclusion),
                premises=(),
                conclusion=conclusion,
            )
        )
    return tuple(result)


def distinct_evidence_blocks(
    domain_receipt: str, terms: Iterable[Any]
) -> tuple[PositiveHornBlock, ...]:
    """Construct the ordered finite proof bag for structural disequality.

    ``terms`` must be the complete finite ground domain established by the
    caller's typing/profile evidence.  Equal terms have no constructor;
    ordered unequal pairs each retain one proof occurrence.
    """

    domain = tuple(terms)
    if any(_variables(term) for term in domain):
        raise PositiveHornBoundary("distinct finite domain is not ground")
    if len(set(domain)) != len(domain):
        raise PositiveHornBoundary("distinct finite domain contains duplicates")
    result: list[PositiveHornBlock] = []
    for left in domain:
        for right in domain:
            if left == right:
                continue
            ordinal = len(result) + 1
            source = (
                "gdl:distinct-domain",
                domain_receipt,
                str(ordinal),
            )
            conclusion = ("distinct", left, right)
            result.append(
                PositiveHornBlock(
                    identity=f"gdl-distinct-{ordinal:04d}",
                    source=source,
                    proof=("gdl:distinct-proof", source, left, right),
                    premises=(),
                    conclusion=conclusion,
                )
            )
    return tuple(result)


def render_term(term: Any) -> str:
    if isinstance(term, str):
        return term
    if not isinstance(term, tuple) or not term:
        raise PositiveHornBoundary("cannot render malformed represented term")
    return "(" + " ".join(render_term(item) for item in term) + ")"


def render_block(block: PositiveHornBlock) -> str:
    premises = "rm-nil"
    for premise in reversed(block.premises):
        premises = (
            f"(rm-cons (rm-premise {premise.proof_variable} "
            f"(quote {render_term(premise.goal)})) {premises})"
        )
    return "\n".join(
        (
            f"(rm-block {block.identity} {render_term(block.source)}",
            f"  (quote {render_term(block.proof)})",
            f"  {premises}",
            f"  (quote {render_term(block.conclusion)}))",
        )
    )


def render_package(blocks: Iterable[PositiveHornBlock]) -> str:
    rendered = tuple(render_block(block) for block in blocks)
    return "(rm-package" + ("\n" + "\n".join(rendered) if rendered else "") + ")"


def target_query_patterns(atoms: Iterable[str]) -> tuple[Any, ...]:
    """Return one whole-answer query per first-seen target head/arity."""

    signatures: list[tuple[str, int]] = []
    for text in atoms:
        atom = parse_ground_atom(text)
        signature = (atom.head, len(atom.args))
        if signature not in signatures:
            signatures.append(signature)
    return tuple(
        head if arity == 0 else (
            head,
            *(f"$gdl-answer-{index}" for index in range(arity)),
        )
        for head, arity in signatures
    )


def _is_runtime_variable(term: Any) -> bool:
    return isinstance(term, str) and term.startswith("$")


def _runtime_variables(term: Any) -> set[str]:
    if _is_runtime_variable(term):
        return {term}
    if not isinstance(term, tuple):
        return set()
    return (
        set().union(*(_runtime_variables(item) for item in term))
        if term
        else set()
    )


def _reference_deref(term: Any, substitution: dict[str, Any]) -> Any:
    seen: set[str] = set()
    while (
        _is_runtime_variable(term)
        and term in substitution
        and term not in seen
    ):
        seen.add(term)
        term = substitution[term]
    return term


def _reference_apply(term: Any, substitution: dict[str, Any]) -> Any:
    term = _reference_deref(term, substitution)
    if _is_runtime_variable(term):
        return term
    if isinstance(term, tuple):
        return tuple(_reference_apply(item, substitution) for item in term)
    return term


def _reference_occurs(
    variable: str, term: Any, substitution: dict[str, Any]
) -> bool:
    term = _reference_deref(term, substitution)
    if term == variable:
        return True
    if not isinstance(term, tuple):
        return False
    return any(
        _reference_occurs(variable, item, substitution) for item in term
    )


def _reference_unify(
    left: Any, right: Any, substitution: dict[str, Any]
) -> dict[str, Any] | None:
    """Symmetric first-order unification used only by the corpus oracle."""

    pending = [(left, right)]
    result = dict(substitution)
    while pending:
        current_left, current_right = pending.pop()
        current_left = _reference_deref(current_left, result)
        current_right = _reference_deref(current_right, result)
        if current_left == current_right:
            continue
        if _is_runtime_variable(current_left):
            if _reference_occurs(current_left, current_right, result):
                return None
            result[current_left] = current_right
            continue
        if _is_runtime_variable(current_right):
            if _reference_occurs(current_right, current_left, result):
                return None
            result[current_right] = current_left
            continue
        if (
            not isinstance(current_left, tuple)
            or not isinstance(current_right, tuple)
            or len(current_left) != len(current_right)
        ):
            return None
        pending.extend(zip(current_left, current_right))
    return result


def _reference_rename(term: Any, variables: dict[str, str]) -> Any:
    if _is_runtime_variable(term):
        return variables[term]
    if isinstance(term, tuple):
        return tuple(_reference_rename(item, variables) for item in term)
    return term


def _reference_freshen(
    block: PositiveHornBlock, epoch: int
) -> PositiveHornBlock:
    variables: dict[str, str] = {}
    for term in (
        block.source,
        block.proof,
        block.conclusion,
        *(premise.proof_variable for premise in block.premises),
        *(premise.goal for premise in block.premises),
    ):
        for variable in sorted(_runtime_variables(term)):
            variables.setdefault(variable, f"{variable}#{epoch}")
    return PositiveHornBlock(
        identity=block.identity,
        source=_reference_rename(block.source, variables),
        proof=_reference_rename(block.proof, variables),
        premises=tuple(
            PositiveHornPremise(
                _reference_rename(premise.proof_variable, variables),
                _reference_rename(premise.goal, variables),
            )
            for premise in block.premises
        ),
        conclusion=_reference_rename(block.conclusion, variables),
    )


def solve_positive_horn_reference(
    blocks: Iterable[PositiveHornBlock],
    query: Any,
    *,
    depth: int = 32,
    max_states: int = 50_000_000,
    max_occurrences: int = 1_000_000,
) -> PositiveHornReferenceRun:
    """Execute the authored positive-Horn semantics as an independent oracle.

    This evaluator consumes proof-relevant blocks rather than RuleMachine
    bytecode.  It deliberately mirrors only the mathematical search order:
    authored block order, left-to-right premises, and occurrence-preserving
    backtracking.  It is a qualification oracle, never a runtime authority.
    """

    if depth < 0 or max_states <= 0 or max_occurrences <= 0:
        raise PositiveHornBoundary("positive-Horn reference limits are invalid")
    program = tuple(blocks)
    by_signature: dict[tuple[str, int], list[PositiveHornBlock]] = {}
    for block in program:
        conclusion = block.conclusion
        if isinstance(conclusion, str):
            if not _is_runtime_variable(conclusion):
                by_signature.setdefault((conclusion, 0), []).append(block)
        elif (
            isinstance(conclusion, tuple)
            and conclusion
            and isinstance(conclusion[0], str)
            and not _is_runtime_variable(conclusion[0])
        ):
            by_signature.setdefault(
                (conclusion[0], len(conclusion) - 1), []
            ).append(block)
    answers: list[PositiveHornReferenceAnswer] = []
    states = 0
    attempts = 0
    matches = 0
    fresh_epoch = 0
    root_proof = "$gdl-reference-proof"
    ground_cache: dict[tuple[Any, int], tuple[Any, ...]] = {}
    ground_cache_active: set[tuple[Any, int]] = set()

    def solve_premises(
        premises: tuple[PositiveHornPremise, ...],
        premise_index: int,
        remaining_depth: int,
        substitution: dict[str, Any],
    ):
        if premise_index == len(premises):
            yield substitution
            return
        premise = premises[premise_index]
        for solved in solve_goal(
            premise.goal,
            premise.proof_variable,
            remaining_depth,
            substitution,
        ):
            yield from solve_premises(
                premises,
                premise_index + 1,
                remaining_depth,
                solved,
            )

    def solve_goal(
        goal: Any,
        desired_proof: Any,
        remaining_depth: int,
        substitution: dict[str, Any],
    ):
        nonlocal states, attempts, matches, fresh_epoch
        if states == max_states:
            raise PositiveHornBoundary(
                "positive-Horn reference exhausted its state bound"
            )
        states += 1
        applied_goal = _reference_apply(goal, substitution)
        cache_key = (
            (applied_goal, remaining_depth)
            if not _runtime_variables(applied_goal)
            else None
        )
        if cache_key is not None and cache_key in ground_cache:
            for proof_carrier in ground_cache[cache_key]:
                matched = _reference_unify(
                    proof_carrier, desired_proof, substitution
                )
                if matched is not None:
                    yield matched
            return
        if isinstance(applied_goal, str):
            signature = (
                None
                if _is_runtime_variable(applied_goal)
                else (applied_goal, 0)
            )
        elif (
            isinstance(applied_goal, tuple)
            and applied_goal
            and isinstance(applied_goal[0], str)
            and not _is_runtime_variable(applied_goal[0])
        ):
            signature = (applied_goal[0], len(applied_goal) - 1)
        else:
            signature = None
        candidates = (
            tuple(by_signature.get(signature, ()))
            if signature is not None
            else program
        )
        collected: list[Any] | None = (
            []
            if cache_key is not None and cache_key not in ground_cache_active
            else None
        )
        if collected is not None:
            ground_cache_active.add(cache_key)
        for source_block in candidates:
            attempts += 1
            fresh_epoch += 1
            block = _reference_freshen(source_block, fresh_epoch)
            matched = _reference_unify(
                block.conclusion, goal, substitution
            )
            if matched is None:
                continue
            # RuleMachine receives proof builders through an explicit quote
            # boundary.  Retain that carrier around premise proofs, then
            # remove only the outer boundary when publishing an answer.
            matched = _reference_unify(
                ("quote", block.proof), desired_proof, matched
            )
            if matched is None:
                continue
            matches += 1
            if not block.premises:
                if collected is not None:
                    proof_carrier = _reference_apply(
                        desired_proof, matched
                    )
                    if not _runtime_variables(proof_carrier):
                        collected.append(proof_carrier)
                yield matched
            elif remaining_depth > 0:
                for solved in solve_premises(
                    block.premises, 0, remaining_depth - 1, matched
                ):
                    if collected is not None:
                        proof_carrier = _reference_apply(
                            desired_proof, solved
                        )
                        if not _runtime_variables(proof_carrier):
                            collected.append(proof_carrier)
                    yield solved
        if collected is not None:
            ground_cache_active.remove(cache_key)
            ground_cache[cache_key] = tuple(collected)

    for solution in solve_goal(query, root_proof, depth, {}):
        if len(answers) == max_occurrences:
            raise PositiveHornBoundary(
                "positive-Horn reference exhausted its occurrence bound"
            )
        conclusion = _reference_apply(query, solution)
        proof_carrier = _reference_apply(root_proof, solution)
        if (
            not isinstance(proof_carrier, tuple)
            or len(proof_carrier) != 2
            or proof_carrier[0] != "quote"
        ):
            raise PositiveHornBoundary(
                "positive-Horn proof crossed a malformed data boundary"
            )
        proof = proof_carrier[1]
        if _runtime_variables(conclusion) or _runtime_variables(proof):
            raise PositiveHornBoundary(
                "positive-Horn answer retains an unbound runtime variable"
            )
        answers.append(PositiveHornReferenceAnswer(conclusion, proof))
    return PositiveHornReferenceRun(
        answers=tuple(answers),
        states=states,
        block_attempts=attempts,
        block_matches=matches,
    )
