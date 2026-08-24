"""Independent typed least-model semantics for stratified authored GDL.

The model is finite because substitutions range over the exact typed
Herbrand carrier.  Positive premises bind substitutions from already
established supports; a negative premise is used only after its strictly
lower stratum has completed.  Support identity is kept separate from the
proof-hypergraph multiplicity observed by the native implementation.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any, Iterable

from prime_iggp_finite_herbrand import (
    FiniteHerbrandWitness,
    construct_finite_herbrand,
)
from prime_iggp_presentation import (
    GdlFiniteTypedOccurrenceProjection,
    GdlRuleVariableType,
    GdlSourcePresentation,
    GdlTypeProfile,
    PresentationError,
    analyze_gdl_existing_type_domains,
    check_gdl_type_of_extension,
    extract_gdl_typing_constraints,
    find_gdl_rule_variable_greatest_assignment,
    project_gdl_finite_typed_occurrences,
)
from prime_iggp_stratification import (
    RelationSignature,
    StratificationWitness,
    TargetDependencySlice,
    construct_target_dependency_slice,
    construct_stratification,
    relation_signature,
)


INTEGER = re.compile(r"-?[0-9]+\Z")


class StratifiedModelBoundary(RuntimeError):
    """The authored package is outside this exact finite model fragment."""


@dataclass(frozen=True)
class StratifiedModelSupport:
    relation_index: int
    stratum: int
    literal: Any


@dataclass(frozen=True)
class StratifiedModelReferenceStats:
    source_forms: int
    source_rules: int
    source_facts: int
    branch_expansions: int
    ground_instances: int
    distinct_checks: int
    support_nodes: int
    completed_strata: int
    episode_fact_occurrences: int
    episode_typing_proof_occurrences: int
    episode_support_nodes: int


@dataclass(frozen=True)
class StratifiedModelWitness:
    stratification: StratificationWitness
    carrier: FiniteHerbrandWitness
    supports: tuple[StratifiedModelSupport, ...]
    stats: StratifiedModelReferenceStats


@dataclass(frozen=True)
class _Branch:
    positive: tuple[Any, ...] = ()
    negative: tuple[Any, ...] = ()
    distinct: tuple[tuple[Any, Any], ...] = ()


@dataclass(frozen=True)
class _Template:
    head: Any
    head_relation: int
    head_stratum: int
    variables: tuple[str, ...]
    variable_types: tuple[str, ...]
    branch: _Branch


@dataclass(frozen=True)
class StratifiedModelBasis:
    """One immutable typed source basis reusable across finite episodes."""

    stratification: StratificationWitness
    carrier: FiniteHerbrandWitness
    typed_source: GdlFiniteTypedOccurrenceProjection
    templates: tuple[_Template, ...]
    source_forms: int
    source_rules: int
    source_facts: int
    branch_expansions: int
    target_slice: TargetDependencySlice | None = None


def _is_variable(term: Any) -> bool:
    return isinstance(term, str) and term.startswith("?")


def _constant(token: str) -> str | int:
    return int(token) if INTEGER.fullmatch(token) else token


def _variables_in_order(terms: Iterable[Any]) -> tuple[str, ...]:
    variables: list[str] = []

    def visit(term: Any) -> None:
        if _is_variable(term):
            if term not in variables:
                variables.append(term)
            return
        if isinstance(term, tuple):
            for child in term[1:]:
                visit(child)

    for term in terms:
        visit(term)
    return tuple(variables)


def _variables(term: Any) -> frozenset[str]:
    if _is_variable(term):
        return frozenset((term,))
    if not isinstance(term, tuple):
        return frozenset()
    return frozenset().union(*(_variables(item) for item in term[1:]))


def _expand_expression(
    expression: Any, branches: tuple[_Branch, ...]
) -> tuple[tuple[_Branch, ...], int]:
    if isinstance(expression, tuple) and expression:
        operator = expression[0]
        if operator == "and":
            if len(expression) < 2:
                raise StratifiedModelBoundary("and has no operand")
            expansions = 0
            current = branches
            for child in expression[1:]:
                current, added = _expand_expression(child, current)
                expansions += added
            return current, expansions
        if operator == "or":
            if len(expression) < 3:
                raise StratifiedModelBoundary(
                    "or does not have at least two alternatives"
                )
            output: list[_Branch] = []
            expansions = 0
            for branch in branches:
                for alternative in expression[1:]:
                    selected, nested = _expand_expression(
                        alternative, (branch,)
                    )
                    output.extend(selected)
                    expansions += 1 + nested
            return tuple(output), expansions
        if operator == "not":
            if len(expression) != 2:
                raise StratifiedModelBoundary("not has the wrong arity")
            operand = expression[1]
            if (
                isinstance(operand, tuple)
                and operand
                and operand[0] in {"and", "or", "not", "distinct"}
            ):
                raise StratifiedModelBoundary(
                    "negation operand is not one relational atom"
                )
            return (
                tuple(
                    _Branch(
                        branch.positive,
                        branch.negative + (operand,),
                        branch.distinct,
                    )
                    for branch in branches
                ),
                0,
            )
        if operator == "distinct":
            if len(expression) != 3:
                raise StratifiedModelBoundary(
                    "distinct does not have exactly two operands"
                )
            pair = (expression[1], expression[2])
            return (
                tuple(
                    _Branch(
                        branch.positive,
                        branch.negative,
                        branch.distinct + (pair,),
                    )
                    for branch in branches
                ),
                0,
            )
    return (
        tuple(
            _Branch(
                branch.positive + (expression,),
                branch.negative,
                branch.distinct,
            )
            for branch in branches
        ),
        0,
    )


def _compile_templates(
    presentation: GdlSourcePresentation,
    stratification: StratificationWitness,
    resolved_types: dict[Any, str],
    *,
    form_ordinals: frozenset[int] | None = None,
) -> tuple[tuple[_Template, ...], int, int, int]:
    relation_positions = {
        relation.signature: index
        for index, relation in enumerate(stratification.relations)
    }
    templates: list[_Template] = []
    rules = 0
    facts = 0
    branch_expansions = 0

    for form_index, occurrence in enumerate(presentation.forms):
        if form_ordinals is not None and form_index not in form_ordinals:
            continue
        form = occurrence.form
        if not form:
            raise StratifiedModelBoundary("source contains an empty form")
        if form[0] == "distinct":
            if (
                len(form) != 3
                or _variables(form)
                or form[1] == form[2]
            ):
                raise StratifiedModelBoundary(
                    "top-level distinct evidence is not ground and unequal"
                )
            facts += 1
            continue
        is_rule = form[0] == "<="
        if is_rule:
            if len(form) < 2:
                raise StratifiedModelBoundary("rule has no head")
            head = form[1]
            premises = form[2:]
            rules += 1
        else:
            head = form
            premises = ()
            facts += 1
        if (
            isinstance(head, tuple)
            and head
            and head[0] in {"and", "or", "not", "distinct"}
        ):
            raise StratifiedModelBoundary("source has a logical rule head")
        try:
            head_signature = relation_signature(head)
            head_relation = relation_positions[head_signature]
        except (KeyError, PresentationError) as exc:
            raise StratifiedModelBoundary(
                "rule head has no stratified relation"
            ) from exc
        head_stratum = stratification.relations[head_relation].stratum

        variables = _variables_in_order((head, *premises))
        variable_types: list[str] = []
        for variable in variables:
            type_name = resolved_types.get(
                GdlRuleVariableType(form_index, variable)
            )
            if type_name is None:
                raise StratifiedModelBoundary(
                    "rule variable lacks a checked exact type"
                )
            variable_types.append(type_name)

        branches: tuple[_Branch, ...] = (_Branch(),)
        for premise in premises:
            branches, added = _expand_expression(premise, branches)
            branch_expansions += added
        for branch in branches:
            bound = frozenset().union(
                *(_variables(premise) for premise in branch.positive)
            )
            if any(variable not in bound for variable in variables):
                raise StratifiedModelBoundary(
                    "every branch must bind every rule variable positively"
                )
            for premise in branch.positive:
                relation = relation_positions.get(
                    relation_signature(premise)
                )
                if (
                    relation is None
                    or stratification.relations[relation].stratum
                    > head_stratum
                ):
                    raise StratifiedModelBoundary(
                        "positive dependency exceeds its head stratum"
                    )
            for premise in branch.negative:
                relation = relation_positions.get(
                    relation_signature(premise)
                )
                if (
                    relation is None
                    or stratification.relations[relation].stratum
                    >= head_stratum
                ):
                    raise StratifiedModelBoundary(
                        "negative dependency is not strictly lower"
                    )
            templates.append(
                _Template(
                    head,
                    head_relation,
                    head_stratum,
                    variables,
                    tuple(variable_types),
                    branch,
                )
            )
    return tuple(templates), rules, facts, branch_expansions


class _CarrierIndex:
    def __init__(
        self,
        carrier: FiniteHerbrandWitness,
        typed_source: GdlFiniteTypedOccurrenceProjection,
    ):
        self.carrier = carrier
        self.typing_signatures = (
            *carrier.signatures,
            *typed_source.derived_signatures,
        )
        by_value: dict[Any, list[int]] = {}
        for index, term in enumerate(carrier.terms):
            by_value.setdefault(term.term, []).append(index)
        self.by_value = {
            value: tuple(indices) for value, indices in by_value.items()
        }
        self._acceptance_proof_counts: dict[tuple[str, str], int] = {}
        self._typing_proof_counts: dict[Any, dict[str, int]] = {}

    def matching(self, value: Any, expected_type: str) -> tuple[int, ...]:
        return tuple(
            index
            for index in self.by_value.get(value, ())
            if (
                self.carrier.terms[index].exact_type,
                expected_type,
            ) in self.carrier.accepted_types
        )

    def acceptance_proof_count(
        self, actual_type: str, expected_type: str
    ) -> int:
        """Count authored simple subtype paths, including reflexivity."""

        key = actual_type, expected_type
        cached = self._acceptance_proof_counts.get(key)
        if cached is not None:
            return cached

        def visit(actual: str, path: tuple[str, ...]) -> int:
            if actual in path:
                return 0
            extended = (*path, actual)
            proofs = 1 if actual == expected_type else 0
            for edge in self.carrier.subtypes:
                if edge.subtype == actual:
                    proofs += visit(edge.supertype, extended)
            return proofs

        proofs = visit(actual_type, ())
        self._acceptance_proof_counts[key] = proofs
        return proofs

    def typing_proof_counts(self, term: Any) -> dict[str, int]:
        """Count every profile-occurrence derivation of a ground term."""

        cached = self._typing_proof_counts.get(term)
        if cached is not None:
            return cached
        if isinstance(term, tuple):
            if not term or not isinstance(term[0], str):
                return {}
            name = term[0]
            arguments = term[1:]
        else:
            name = str(term)
            arguments = ()

        result: dict[str, int] = {}
        for signature in self.typing_signatures:
            if (
                signature.name != name
                or len(signature.argument_types) != len(arguments)
            ):
                continue
            alternatives = 1
            for argument, expected_type in zip(
                arguments, signature.argument_types
            ):
                accepted = sum(
                    proof_count
                    * self.acceptance_proof_count(
                        actual_type, expected_type
                    )
                    for actual_type, proof_count
                    in self.typing_proof_counts(argument).items()
                )
                alternatives *= accepted
                if alternatives == 0:
                    break
            if alternatives:
                result[signature.result_type] = (
                    result.get(signature.result_type, 0) + alternatives
                )
        self._typing_proof_counts[term] = result
        return result

    def literal_typing_proof_count(self, literal: Any) -> int:
        typings = self.typing_proof_counts(literal)
        accepted = {
            actual_type: proof_count
            * self.acceptance_proof_count(actual_type, "bool")
            for actual_type, proof_count in typings.items()
            if self.acceptance_proof_count(actual_type, "bool") > 0
        }
        if not accepted:
            raise StratifiedModelBoundary(
                "episode fact has no checked ground Boolean typing derivation"
            )
        if len(accepted) != 1:
            raise StratifiedModelBoundary(
                "episode fact has more than one accepted result type"
            )
        return next(iter(accepted.values()))


def _match(
    pattern: Any,
    value: Any,
    assignment: tuple[int | None, ...],
    template: _Template,
    carrier: _CarrierIndex,
) -> tuple[tuple[int | None, ...], ...]:
    if _is_variable(pattern):
        variable = template.variables.index(pattern)
        existing = assignment[variable]
        if existing is not None:
            return (
                (assignment,)
                if carrier.carrier.terms[existing].term == value
                else ()
            )
        output = []
        for term_index in carrier.matching(
            value, template.variable_types[variable]
        ):
            row = list(assignment)
            row[variable] = term_index
            output.append(tuple(row))
        return tuple(output)
    if isinstance(pattern, str):
        return (assignment,) if _constant(pattern) == value else ()
    if not isinstance(pattern, tuple) or not pattern:
        return ()
    if (
        not isinstance(value, tuple)
        or len(value) != len(pattern)
        or value[0] != pattern[0]
    ):
        return ()
    rows = (assignment,)
    for child_pattern, child_value in zip(pattern[1:], value[1:]):
        rows = tuple(
            next_row
            for row in rows
            for next_row in _match(
                child_pattern, child_value, row, template, carrier
            )
        )
        if not rows:
            break
    return rows


def _ground(
    expression: Any,
    assignment: tuple[int | None, ...],
    template: _Template,
    carrier: _CarrierIndex,
) -> Any:
    if _is_variable(expression):
        variable = template.variables.index(expression)
        term_index = assignment[variable]
        if term_index is None:
            raise StratifiedModelBoundary(
                "grounding contains an unbound variable"
            )
        return carrier.carrier.terms[term_index].term
    if isinstance(expression, str):
        return _constant(expression)
    if not isinstance(expression, tuple) or not expression:
        raise StratifiedModelBoundary("grounding contains malformed syntax")
    if not isinstance(expression[0], str) or _is_variable(expression[0]):
        raise StratifiedModelBoundary("grounding has a dynamic head")
    return (
        expression[0],
        *(
            _ground(argument, assignment, template, carrier)
            for argument in expression[1:]
        ),
    )


def _ground_literal_typing_proof_count(
    literal: Any,
    carrier_index: _CarrierIndex,
) -> int:
    """Count exact native-style typing alternatives for one ground relation.

    This is deliberately independent of the source evaluator.  A fact is in
    the episode image only when an authored Boolean signature accepts every
    structural argument through the finite profile's subtype relation.
    """

    if _variables(literal):
        raise StratifiedModelBoundary("episode fact is not ground")
    return carrier_index.literal_typing_proof_count(literal)


def canonical_ground_literal(literal: Any) -> Any:
    """Put external ground syntax in the source evaluator's value form."""

    def argument(term: Any) -> Any:
        if _is_variable(term):
            raise StratifiedModelBoundary("episode fact is not ground")
        if isinstance(term, str):
            return _constant(term)
        if isinstance(term, tuple):
            if (
                not term
                or not isinstance(term[0], str)
                or _is_variable(term[0])
            ):
                raise StratifiedModelBoundary(
                    "episode fact contains a dynamic constructor"
                )
            return (term[0], *(argument(child) for child in term[1:]))
        return term

    if _is_variable(literal):
        raise StratifiedModelBoundary("episode fact is not ground")
    if isinstance(literal, str):
        return literal
    if (
        not isinstance(literal, tuple)
        or not literal
        or not isinstance(literal[0], str)
        or _is_variable(literal[0])
    ):
        raise StratifiedModelBoundary(
            "episode fact has no static relation head"
        )
    return (literal[0], *(argument(child) for child in literal[1:]))


def construct_stratified_model_basis(
    presentation: GdlSourcePresentation,
    profile: GdlTypeProfile,
    *,
    target: RelationSignature | None = None,
) -> StratifiedModelBasis:
    """Construct the checked source basis shared by all of its episodes."""
    if presentation.foreign_code:
        raise StratifiedModelBoundary(
            "mixed GDL/foreign source requires another admitted route"
        )
    target_slice = (
        None
        if target is None
        else construct_target_dependency_slice(presentation, target)
    )
    selected_forms = (
        None if target_slice is None else target_slice.selected_forms
    )
    stratification = construct_stratification(
        presentation, form_ordinals=selected_forms
    )
    carrier = construct_finite_herbrand(profile)
    constraints = extract_gdl_typing_constraints(
        presentation, profile, form_ordinals=selected_forms
    )
    analysis = analyze_gdl_existing_type_domains(constraints, profile)
    assignment = find_gdl_rule_variable_greatest_assignment(
        constraints, analysis
    )
    if assignment is None:
        raise StratifiedModelBoundary(
            "source has no coherent greatest rule-variable typing witness"
        )
    projection = project_gdl_finite_typed_occurrences(
        constraints, analysis, assignment.assignment
    )
    check_gdl_type_of_extension(constraints, profile, projection)
    resolved_types = {
        resolved.expression: resolved.type_name
        for resolved in projection.resolved_expressions
    }
    templates, rules, facts, branch_expansions = _compile_templates(
        presentation,
        stratification,
        resolved_types,
        form_ordinals=selected_forms,
    )
    return StratifiedModelBasis(
        stratification=stratification,
        carrier=carrier,
        typed_source=projection,
        templates=templates,
        source_forms=(
            len(presentation.forms)
            if target_slice is None
            else len(target_slice.form_ordinals)
        ),
        source_rules=rules,
        source_facts=facts,
        branch_expansions=branch_expansions,
        target_slice=target_slice,
    )


def construct_stratified_model_from_basis(
    basis: StratifiedModelBasis,
    *,
    initial_facts: Iterable[Any] = (),
) -> StratifiedModelWitness:
    """Construct one least model from a previously checked source basis.

    ``initial_facts`` is an ordered external episode bag.  Every occurrence is
    checked against the same finite typed carrier before its support quotient
    seeds evaluation; repeated equal facts therefore remain visible in the
    occurrence statistics while contributing one support node.
    """

    stratification = basis.stratification
    carrier = basis.carrier
    templates = basis.templates

    relation_positions = {
        relation.signature: index
        for index, relation in enumerate(stratification.relations)
    }
    supports: list[StratifiedModelSupport] = []
    support_keys: set[tuple[int, Any]] = set()
    supports_by_relation: list[list[Any]] = [
        [] for _ in stratification.relations
    ]
    groundings: set[tuple[int, tuple[int | None, ...]]] = set()
    carrier_index = _CarrierIndex(carrier, basis.typed_source)
    distinct_checks = 0
    episode_fact_occurrences = 0
    episode_typing_proof_occurrences = 0
    episode_support_nodes = 0

    for external_literal in initial_facts:
        literal = canonical_ground_literal(external_literal)
        proof_count = _ground_literal_typing_proof_count(
            literal, carrier_index
        )
        try:
            relation = relation_positions[relation_signature(literal)]
        except KeyError as exc:
            raise StratifiedModelBoundary(
                "episode fact relation is absent from the authored calculus"
            ) from exc
        episode_fact_occurrences += 1
        episode_typing_proof_occurrences += proof_count
        key = relation, literal
        if key in support_keys:
            continue
        support_keys.add(key)
        supports_by_relation[relation].append(literal)
        supports.append(
            StratifiedModelSupport(
                relation,
                stratification.relations[relation].stratum,
                literal,
            )
        )
        episode_support_nodes += 1

    for stratum in range(stratification.maximum_stratum + 1):
        changed = True
        while changed:
            changed = False
            for template_index, template in enumerate(templates):
                if template.head_stratum != stratum:
                    continue
                rows: tuple[tuple[int | None, ...], ...] = (
                    (None,) * len(template.variables),
                )
                for premise in template.branch.positive:
                    relation = relation_positions[relation_signature(premise)]
                    rows = tuple(
                        matched
                        for row in rows
                        for literal in supports_by_relation[relation]
                        for matched in _match(
                            premise, literal, row, template, carrier_index
                        )
                    )
                    if not rows:
                        break
                for row in rows:
                    if any(term is None for term in row):
                        raise StratifiedModelBoundary(
                            "positive joins did not bind every variable"
                        )
                    grounding = template_index, row
                    if grounding in groundings:
                        continue
                    groundings.add(grounding)

                    allowed = True
                    for left, right in template.branch.distinct:
                        distinct_checks += 1
                        if _ground(
                            left, row, template, carrier_index
                        ) == _ground(right, row, template, carrier_index):
                            allowed = False
                            break
                    if not allowed:
                        continue
                    for premise in template.branch.negative:
                        relation = relation_positions[
                            relation_signature(premise)
                        ]
                        literal = _ground(
                            premise, row, template, carrier_index
                        )
                        if (relation, literal) in support_keys:
                            allowed = False
                            break
                    if not allowed:
                        continue

                    literal = _ground(
                        template.head, row, template, carrier_index
                    )
                    key = template.head_relation, literal
                    if key in support_keys:
                        continue
                    support_keys.add(key)
                    supports_by_relation[template.head_relation].append(
                        literal
                    )
                    supports.append(
                        StratifiedModelSupport(
                            template.head_relation,
                            template.head_stratum,
                            literal,
                        )
                    )
                    changed = True

    return StratifiedModelWitness(
        stratification=stratification,
        carrier=carrier,
        supports=tuple(supports),
        stats=StratifiedModelReferenceStats(
            source_forms=basis.source_forms,
            source_rules=basis.source_rules,
            source_facts=basis.source_facts,
            branch_expansions=basis.branch_expansions,
            ground_instances=len(groundings),
            distinct_checks=distinct_checks,
            support_nodes=len(supports),
            completed_strata=stratification.maximum_stratum + 1,
            episode_fact_occurrences=episode_fact_occurrences,
            episode_typing_proof_occurrences=(
                episode_typing_proof_occurrences
            ),
            episode_support_nodes=episode_support_nodes,
        ),
    )


def construct_stratified_model(
    presentation: GdlSourcePresentation,
    profile: GdlTypeProfile,
    *,
    initial_facts: Iterable[Any] = (),
) -> StratifiedModelWitness:
    """Construct a checked source basis and its exact least model."""

    basis = construct_stratified_model_basis(presentation, profile)
    return construct_stratified_model_from_basis(
        basis, initial_facts=initial_facts
    )
