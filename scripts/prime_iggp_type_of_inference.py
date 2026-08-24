"""Lower checked IGGP typing evidence to one generic inference program.

The lowering has two consumers generated from the same rule inventory:

* ``GPresentationV1`` is replayed by CeTTa's generic inference checker; and
* ``rm-package`` is searched relationally by RuleMachine to propose proofs.

The source presentation remains ordinary data.  It contributes exact node,
child, and scope facts, but no authority mode or verdict.  A later admission
boundary may bind the checked presentation to an identity and revision.
"""

from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
import json
import re
from typing import Any, Iterable

from prime_iggp_presentation import (
    GdlAuthoredTypeOfRule,
    GdlCheckedTypeOfExtension,
    GdlExtendedTypeOfRule,
    GdlSourceOccurrence,
    GdlSourcePresentation,
    GdlStructuralTypeOfRule,
    GdlSubtypeStatement,
    GdlTypeOfApplicationDerivation,
    GdlTypeOfJudgment,
    GdlTypeOfLiteralBoundary,
    GdlTypeOfLogicalDerivation,
    GdlTypeOfVariableDerivation,
    PresentationError,
)


_DIGEST_RE = re.compile(r"[0-9a-f]{64}\Z")


@dataclass(frozen=True)
class PatternVariable:
    """One depth-zero formal in the inference presentation."""

    name: str


@dataclass(frozen=True)
class PatternApplication:
    """One constructor application in the shared Pattern wire."""

    head: str
    arguments: tuple["InferencePattern", ...] = ()


InferencePattern = PatternVariable | PatternApplication


@dataclass(frozen=True)
class InferenceRule:
    """One rule used identically by proof search and proof replay."""

    identifier: str
    formals: tuple[str, ...]
    premises: tuple[InferencePattern, ...]
    conclusion: InferencePattern


@dataclass(frozen=True)
class GdlTypeOfInferenceCase:
    """One closed goal and its exact free-proof-tree multiplicity."""

    source: GdlSourceOccurrence
    kind: str
    goal: InferencePattern
    expected_proofs: int


@dataclass(frozen=True)
class GdlTypeOfInferenceProgram:
    """Authority-free checked-search/checker program for one GDL extension."""

    source_digest: str
    profile_digest: str
    revision: str
    constructors: tuple[tuple[str, int], ...]
    judgments: tuple[tuple[str, int], ...]
    rules: tuple[InferenceRule, ...]
    cases: tuple[GdlTypeOfInferenceCase, ...]


def _variable(name: str) -> PatternVariable:
    return PatternVariable(name)


def _application(
    head: str, *arguments: InferencePattern
) -> PatternApplication:
    return PatternApplication(head, tuple(arguments))


def _object_list(items: Iterable[InferencePattern]) -> InferencePattern:
    result: InferencePattern = _application("gdl:nil")
    for item in reversed(tuple(items)):
        result = _application("gdl:cons", item, result)
    return result


def _quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"))


def _wire_list(items: Iterable[str], nil: str, cons: str) -> str:
    result = nil
    for item in reversed(tuple(items)):
        result = f"({cons} {item} {result})"
    return result


def _render_pattern(pattern: InferencePattern, runtime: bool) -> str:
    if isinstance(pattern, PatternVariable):
        return f"${pattern.name}" if runtime else f'(FVar {_quoted(pattern.name)})'
    return (
        f'(PApp {_quoted(pattern.head)} '
        f'{_wire_list((_render_pattern(item, runtime) for item in pattern.arguments), "LNil", "LCons")})'
    )


def _render_rule(rule: InferenceRule) -> str:
    formals = _wire_list(
        (f'(Formal {_quoted(name)} 0)' for name in rule.formals),
        "LNil",
        "LCons",
    )
    premises = _wire_list(
        (_render_pattern(item, False) for item in rule.premises),
        "LNil",
        "LCons",
    )
    return (
        f'(GRuleV1 {_quoted(rule.identifier)} {formals} {premises} '
        f'{_render_pattern(rule.conclusion, False)} LNil)'
    )


def _render_rule_machine_block(rule: InferenceRule) -> str:
    arguments = _wire_list((f"${name}" for name in rule.formals), "LNil", "LCons")
    children = _wire_list(
        (f"(unquote $premise_{index})" for index in range(len(rule.premises))),
        "PrNil",
        "PrCons",
    )
    proof = (
        f'(quote (GProof (GRuleInst {_quoted(rule.identifier)} {arguments}) '
        f'{children}))'
    )
    premises = "rm-nil"
    for index, premise in reversed(tuple(enumerate(rule.premises))):
        premises = (
            f'(rm-cons (rm-premise $premise_{index} '
            f'(quote {_render_pattern(premise, True)})) {premises})'
        )
    return (
        f'(rm-block {_symbol_token(rule.identifier)} '
        f'{_symbol_token("rule:" + rule.identifier)} {proof} {premises} '
        f'(quote {_render_pattern(rule.conclusion, True)}))'
    )


def _symbol_token(text: str) -> str:
    """Encode arbitrary authored text as a lowercase symbol token."""

    return "gdl-" + text.encode("utf-8").hex()


def _name_pattern(name: str) -> InferencePattern:
    return _application("gdl:name:" + name.encode("utf-8").hex())


def _type_pattern(name: str) -> InferencePattern:
    return _application("gdl:type:" + name.encode("utf-8").hex())


def _form_pattern(form_ordinal: int) -> InferencePattern:
    return _application(f"gdl:form:{form_ordinal}")


def _occurrence_pattern(source: GdlSourceOccurrence) -> InferencePattern:
    path = "root" if not source.path else ".".join(map(str, source.path))
    return _application(f"gdl:occurrence:{source.form_ordinal}:{path}")


def _term_pattern(term: Any) -> InferencePattern:
    if isinstance(term, str):
        if term.startswith("?"):
            return _application("gdl:variable", _name_pattern(term))
        return _application(
            "gdl:application", _name_pattern(term), _object_list(())
        )
    if not isinstance(term, tuple) or not term or not isinstance(term[0], str):
        raise PresentationError("inference lowering encountered a non-GDL term")
    return _application(
        "gdl:application",
        _name_pattern(term[0]),
        _object_list(_term_pattern(argument) for argument in term[1:]),
    )


@dataclass(frozen=True)
class _SourceNode:
    source: GdlSourceOccurrence
    form_ordinal: int
    term: Any
    children: tuple[GdlSourceOccurrence, ...]


def _source_nodes(source: GdlSourcePresentation) -> tuple[_SourceNode, ...]:
    nodes: list[_SourceNode] = []

    def visit(
        form_ordinal: int,
        start_line: int,
        end_line: int,
        path: tuple[int, ...],
        term: Any,
        literal: bool,
    ) -> None:
        occurrence = GdlSourceOccurrence(
            form_ordinal, start_line, end_line, path
        )
        child_terms: tuple[Any, ...] = ()
        if isinstance(term, tuple) and term:
            child_terms = tuple(term[1:])
        children = tuple(
            GdlSourceOccurrence(
                form_ordinal, start_line, end_line, path + (index,)
            )
            for index in range(1, len(child_terms) + 1)
        )
        nodes.append(_SourceNode(occurrence, form_ordinal, term, children))
        for index, child in enumerate(child_terms, 1):
            child_is_literal = bool(
                literal
                and isinstance(term, tuple)
                and term
                and term[0] in {"not", "or"}
            )
            visit(
                form_ordinal,
                start_line,
                end_line,
                path + (index,),
                child,
                child_is_literal,
            )

    for form_ordinal, occurrence in enumerate(source.forms):
        form = occurrence.form
        if form[0] == "<=":
            for index, literal in enumerate(form[1:], 1):
                visit(
                    form_ordinal,
                    occurrence.start_line,
                    occurrence.end_line,
                    (index,),
                    literal,
                    True,
                )
        else:
            visit(
                form_ordinal,
                occurrence.start_line,
                occurrence.end_line,
                (),
                form,
                True,
            )
    return tuple(nodes)


def _fact(identifier: str, conclusion: InferencePattern) -> InferenceRule:
    return InferenceRule(identifier, (), (), conclusion)


def _core_rules() -> tuple[InferenceRule, ...]:
    occurrence = _variable("occurrence")
    term = _variable("term")
    children = _variable("children")
    name = _variable("name")
    argument_types = _variable("argument_types")
    result_type = _variable("result_type")
    actual_type = _variable("actual_type")
    expected_type = _variable("expected_type")
    middle_type = _variable("middle_type")
    child = _variable("child")
    child_term = _variable("child_term")
    child_type = _variable("child_type")
    remaining_children = _variable("remaining_children")
    remaining_terms = _variable("remaining_terms")
    remaining_types = _variable("remaining_types")
    left = _variable("left")
    right = _variable("right")
    left_term = _variable("left_term")
    right_term = _variable("right_term")
    form = _variable("form")

    nil = _application("gdl:nil")
    cons = lambda head, tail: _application("gdl:cons", head, tail)
    application = _application("gdl:application", name, _variable("terms"))

    return (
        InferenceRule(
            "gdl:accepts-refl",
            ("actual_type",),
            (),
            _application("gdl:accepts", actual_type, actual_type),
        ),
        InferenceRule(
            "gdl:accepts-step",
            ("actual_type", "middle_type", "expected_type"),
            (
                _application("gdl:subtype", actual_type, middle_type),
                _application("gdl:accepts", middle_type, expected_type),
            ),
            _application("gdl:accepts", actual_type, expected_type),
        ),
        InferenceRule(
            "gdl:arguments-type-nil",
            (),
            (),
            _application("gdl:arguments-type", nil, nil, nil),
        ),
        InferenceRule(
            "gdl:arguments-type-cons",
            (
                "child",
                "remaining_children",
                "child_term",
                "remaining_terms",
                "child_type",
                "expected_type",
                "remaining_types",
            ),
            (
                _application("type:of", child, child_term, child_type),
                _application("gdl:accepts", child_type, expected_type),
                _application(
                    "gdl:arguments-type",
                    remaining_children,
                    remaining_terms,
                    remaining_types,
                ),
            ),
            _application(
                "gdl:arguments-type",
                cons(child, remaining_children),
                cons(child_term, remaining_terms),
                cons(expected_type, remaining_types),
            ),
        ),
        InferenceRule(
            "gdl:arguments-typed-nil",
            (),
            (),
            _application("gdl:arguments-typed", nil, nil),
        ),
        InferenceRule(
            "gdl:arguments-typed-cons",
            (
                "child",
                "remaining_children",
                "child_term",
                "remaining_terms",
                "child_type",
            ),
            (
                _application("type:of", child, child_term, child_type),
                _application(
                    "gdl:arguments-typed",
                    remaining_children,
                    remaining_terms,
                ),
            ),
            _application(
                "gdl:arguments-typed",
                cons(child, remaining_children),
                cons(child_term, remaining_terms),
            ),
        ),
        InferenceRule(
            "gdl:type-application",
            (
                "occurrence",
                "name",
                "terms",
                "children",
                "argument_types",
                "result_type",
            ),
            (
                _application("gdl:source-node", occurrence, application),
                _application("gdl:source-children", occurrence, children),
                _application(
                    "gdl:signature", name, argument_types, result_type
                ),
                _application(
                    "gdl:arguments-type",
                    children,
                    _variable("terms"),
                    argument_types,
                ),
            ),
            _application("type:of", occurrence, application, result_type),
        ),
        InferenceRule(
            "gdl:type-variable",
            ("occurrence", "form", "name", "result_type"),
            (
                _application(
                    "gdl:source-node",
                    occurrence,
                    _application("gdl:variable", name),
                ),
                _application("gdl:source-form", occurrence, form),
                _application("gdl:variable-type", form, name, result_type),
            ),
            _application(
                "type:of",
                occurrence,
                _application("gdl:variable", name),
                result_type,
            ),
        ),
        InferenceRule(
            "gdl:type-not",
            ("occurrence", "child", "child_term", "child_type"),
            (
                _application(
                    "gdl:source-node",
                    occurrence,
                    _application(
                        "gdl:application",
                        _name_pattern("not"),
                        cons(child_term, nil),
                    ),
                ),
                _application(
                    "gdl:source-children", occurrence, cons(child, nil)
                ),
                _application("type:of", child, child_term, child_type),
                _application(
                    "gdl:accepts", child_type, _type_pattern("bool")
                ),
            ),
            _application(
                "type:of",
                occurrence,
                _application(
                    "gdl:application",
                    _name_pattern("not"),
                    cons(child_term, nil),
                ),
                _type_pattern("bool"),
            ),
        ),
        InferenceRule(
            "gdl:type-or",
            (
                "occurrence",
                "child",
                "remaining_children",
                "child_term",
                "remaining_terms",
                "bool_types",
            ),
            (
                _application(
                    "gdl:source-node",
                    occurrence,
                    _application(
                        "gdl:application",
                        _name_pattern("or"),
                        cons(child_term, remaining_terms),
                    ),
                ),
                _application(
                    "gdl:source-children",
                    occurrence,
                    cons(child, remaining_children),
                ),
                _application(
                    "gdl:arguments-type",
                    cons(child, remaining_children),
                    cons(child_term, remaining_terms),
                    _variable("bool_types"),
                ),
                _application(
                    "gdl:all-type",
                    _variable("bool_types"),
                    _type_pattern("bool"),
                ),
            ),
            _application(
                "type:of",
                occurrence,
                _application(
                    "gdl:application",
                    _name_pattern("or"),
                    cons(child_term, remaining_terms),
                ),
                _type_pattern("bool"),
            ),
        ),
        InferenceRule(
            "gdl:all-type-nil",
            ("expected_type",),
            (),
            _application("gdl:all-type", nil, expected_type),
        ),
        InferenceRule(
            "gdl:all-type-cons",
            ("remaining_types", "expected_type"),
            (
                _application(
                    "gdl:all-type", remaining_types, expected_type
                ),
            ),
            _application(
                "gdl:all-type",
                cons(expected_type, remaining_types),
                expected_type,
            ),
        ),
        InferenceRule(
            "gdl:type-distinct",
            (
                "occurrence",
                "left",
                "right",
                "left_term",
                "right_term",
            ),
            (
                _application(
                    "gdl:source-node",
                    occurrence,
                    _application(
                        "gdl:application",
                        _name_pattern("distinct"),
                        cons(left_term, cons(right_term, nil)),
                    ),
                ),
                _application(
                    "gdl:source-children",
                    occurrence,
                    cons(left, cons(right, nil)),
                ),
                _application(
                    "gdl:arguments-typed",
                    cons(left, cons(right, nil)),
                    cons(left_term, cons(right_term, nil)),
                ),
            ),
            _application(
                "type:of",
                occurrence,
                _application(
                    "gdl:application",
                    _name_pattern("distinct"),
                    cons(left_term, cons(right_term, nil)),
                ),
                _type_pattern("bool"),
            ),
        ),
        InferenceRule(
            "gdl:literal",
            ("occurrence", "term", "actual_type"),
            (
                _application("type:of", occurrence, term, actual_type),
                _application(
                    "gdl:accepts", actual_type, _type_pattern("bool")
                ),
            ),
            _application("gdl:literal", occurrence, term),
        ),
    )


def _rule_identifier_for_signature(rule: object) -> str:
    if isinstance(rule, GdlAuthoredTypeOfRule):
        signature = rule.signature
        return (
            "gdl:signature-authored:"
            f"{signature.statement_ordinal}:{signature.name_ordinal}"
        )
    if isinstance(rule, GdlStructuralTypeOfRule):
        return (
            "gdl:signature-structural:"
            f"{rule.name.encode('utf-8').hex()}:{len(rule.argument_types)}"
        )
    if isinstance(rule, GdlExtendedTypeOfRule):
        signature = rule.signature
        return (
            "gdl:signature-extension:"
            f"{signature.name.encode('utf-8').hex()}:{signature.arity}"
        )
    raise PresentationError("unknown type:of signature rule")


def _signature_parts(
    rule: object,
) -> tuple[str, tuple[str, ...], str]:
    if isinstance(rule, GdlAuthoredTypeOfRule):
        signature = rule.signature
        return (
            signature.name,
            signature.argument_types,
            signature.result_type,
        )
    if isinstance(rule, GdlStructuralTypeOfRule):
        return rule.name, rule.argument_types, rule.result_type
    if isinstance(rule, GdlExtendedTypeOfRule):
        signature = rule.signature
        return signature.name, signature.argument_types, signature.result_type
    raise PresentationError("unknown type:of signature rule")


def _pattern_heads(pattern: InferencePattern) -> Iterable[tuple[str, int]]:
    if isinstance(pattern, PatternVariable):
        return
    yield pattern.head, len(pattern.arguments)
    for argument in pattern.arguments:
        yield from _pattern_heads(argument)


def _pattern_variables(pattern: InferencePattern) -> Iterable[str]:
    if isinstance(pattern, PatternVariable):
        yield pattern.name
        return
    for argument in pattern.arguments:
        yield from _pattern_variables(argument)


def _validate_rule(rule: InferenceRule) -> None:
    if not rule.identifier or not rule.formals:
        if not rule.identifier:
            raise PresentationError("inference rule has an empty identifier")
    if len(set(rule.formals)) != len(rule.formals):
        raise PresentationError(
            f"inference rule {rule.identifier!r} repeats a formal"
        )
    used = {
        name
        for pattern in (*rule.premises, rule.conclusion)
        for name in _pattern_variables(pattern)
    }
    if used != set(rule.formals):
        raise PresentationError(
            f"inference rule {rule.identifier!r} formal inventory differs "
            "from its patterns"
        )


def _validate_digest(digest: str, label: str) -> None:
    if not _DIGEST_RE.fullmatch(digest):
        raise PresentationError(f"{label} is not a lowercase SHA-256 digest")


def lower_gdl_type_of_inference_program(
    source: GdlSourcePresentation,
    extension: GdlCheckedTypeOfExtension,
    *,
    source_digest: str,
    profile_digest: str,
) -> GdlTypeOfInferenceProgram:
    """Build a relational proof producer and generic-checker presentation.

    The lowering re-reads every source occurrence before constructing facts.
    A checked extension for another source, or one whose application/logical
    shape has drifted, is rejected rather than rebound by position.
    """

    _validate_digest(source_digest, "source digest")
    _validate_digest(profile_digest, "profile digest")
    nodes = _source_nodes(source)
    node_by_source = {node.source: node for node in nodes}
    expected_sources = {
        judgment.source for judgment in extension.occurrence_judgments
    }
    if len(node_by_source) != len(nodes) or set(node_by_source) != expected_sources:
        raise PresentationError(
            "inference lowering source occurrences do not match the checked extension"
        )

    judgment_by_source = {
        judgment.source: judgment
        for judgment in extension.occurrence_judgments
    }
    if len(judgment_by_source) != len(extension.occurrence_judgments):
        raise PresentationError("checked extension repeats an occurrence judgment")

    application_by_source: dict[
        GdlSourceOccurrence, tuple[GdlTypeOfApplicationDerivation, ...]
    ] = {}
    for derivation in extension.application_derivations:
        application_by_source.setdefault(derivation.conclusion.source, ())
        application_by_source[derivation.conclusion.source] += (derivation,)
        node = node_by_source[derivation.conclusion.source]
        if (
            isinstance(node.term, str)
            and node.term != derivation.application.name
        ) or (
            isinstance(node.term, tuple)
            and node.term
            and node.term[0] != derivation.application.name
        ):
            raise PresentationError(
                "checked application no longer names the source term"
            )

    variable_by_source = {
        derivation.conclusion.source: derivation
        for derivation in extension.variable_derivations
    }
    logical_by_source = {
        derivation.conclusion.source: derivation
        for derivation in extension.logical_derivations
    }
    if (
        len(variable_by_source) != len(extension.variable_derivations)
        or len(logical_by_source) != len(extension.logical_derivations)
    ):
        raise PresentationError("checked extension repeats a local derivation")

    rules: list[InferenceRule] = list(_core_rules())
    for rule in rules:
        _validate_rule(rule)
    rule_ids = {rule.identifier for rule in rules}

    def add_rule(rule: InferenceRule) -> None:
        _validate_rule(rule)
        if rule.identifier in rule_ids:
            raise PresentationError(
                f"inference lowering repeats rule {rule.identifier!r}"
            )
        rule_ids.add(rule.identifier)
        rules.append(rule)

    source_node_rule: dict[GdlSourceOccurrence, str] = {}
    source_children_rule: dict[GdlSourceOccurrence, str] = {}
    source_form_rule: dict[GdlSourceOccurrence, str] = {}
    for node in nodes:
        token = _occurrence_pattern(node.source).head
        node_id = f"gdl:source-node:{token}"
        children_id = f"gdl:source-children:{token}"
        form_id = f"gdl:source-form:{token}"
        source_node_rule[node.source] = node_id
        source_children_rule[node.source] = children_id
        source_form_rule[node.source] = form_id
        add_rule(
            _fact(
                node_id,
                _application(
                    "gdl:source-node",
                    _occurrence_pattern(node.source),
                    _term_pattern(node.term),
                ),
            )
        )
        add_rule(
            _fact(
                children_id,
                _application(
                    "gdl:source-children",
                    _occurrence_pattern(node.source),
                    _object_list(
                        _occurrence_pattern(child) for child in node.children
                    ),
                ),
            )
        )
        add_rule(
            _fact(
                form_id,
                _application(
                    "gdl:source-form",
                    _occurrence_pattern(node.source),
                    _form_pattern(node.form_ordinal),
                ),
            )
        )

    signature_rule_ids: dict[object, str] = {}
    for derivation in extension.application_derivations:
        signature = derivation.rule
        identifier = _rule_identifier_for_signature(signature)
        signature_rule_ids[signature] = identifier
        if identifier in rule_ids:
            continue
        name, argument_types, result_type = _signature_parts(signature)
        add_rule(
            _fact(
                identifier,
                _application(
                    "gdl:signature",
                    _name_pattern(name),
                    _object_list(_type_pattern(item) for item in argument_types),
                    _type_pattern(result_type),
                ),
            )
        )

    resolved_types = {
        item.expression: item.type_name
        for item in extension.proposal.resolved_expressions
    }
    variable_type_rule: dict[object, str] = {}
    for derivation in extension.variable_derivations:
        variable = derivation.variable
        type_name = resolved_types.get(variable)
        if type_name is None:
            raise PresentationError("variable type is absent from replayed assignment")
        identifier = (
            "gdl:variable-type:"
            f"{variable.form_ordinal}:{variable.name.encode('utf-8').hex()}"
        )
        variable_type_rule[variable] = identifier
        if identifier in rule_ids:
            continue
        add_rule(
            _fact(
                identifier,
                _application(
                    "gdl:variable-type",
                    _form_pattern(variable.form_ordinal),
                    _name_pattern(variable.name),
                    _type_pattern(type_name),
                ),
            )
        )

    subtype_rule: dict[GdlSubtypeStatement, str] = {}
    for ordinal, statement in enumerate(extension.authored_profile.statements):
        if not isinstance(statement, GdlSubtypeStatement):
            continue
        identifier = f"gdl:subtype-authored:{ordinal}"
        subtype_rule[statement] = identifier
        add_rule(
            _fact(
                identifier,
                _application(
                    "gdl:subtype",
                    _type_pattern(statement.subtype),
                    _type_pattern(statement.supertype),
                ),
            )
        )

    def proof_count(source_occurrence: GdlSourceOccurrence) -> int:
        cached = proof_counts.get(source_occurrence)
        if cached is not None:
            return cached
        if source_occurrence in variable_by_source:
            result = 1
        elif source_occurrence in logical_by_source:
            logical = logical_by_source[source_occurrence]
            result = 1
            for operand in logical.operands:
                result *= proof_count(operand.source)
        else:
            derivations = application_by_source.get(source_occurrence)
            if not derivations:
                raise PresentationError("source occurrence has no typing derivation")
            child_product = 1
            for premise in derivations[0].premises:
                child_product *= proof_count(premise.judgment.source)
            result = len(derivations) * child_product
        proof_counts[source_occurrence] = result
        return result

    proof_counts: dict[GdlSourceOccurrence, int] = {}
    cases: list[GdlTypeOfInferenceCase] = []
    for judgment in extension.occurrence_judgments:
        node = node_by_source[judgment.source]
        cases.append(
            GdlTypeOfInferenceCase(
                source=judgment.source,
                kind="type:of",
                goal=_application(
                    "type:of",
                    _occurrence_pattern(judgment.source),
                    _term_pattern(node.term),
                    _type_pattern(judgment.type_name),
                ),
                expected_proofs=proof_count(judgment.source),
            )
        )
    for boundary in extension.literal_boundaries:
        node = node_by_source[boundary.judgment.source]
        cases.append(
            GdlTypeOfInferenceCase(
                source=boundary.judgment.source,
                kind="literal",
                goal=_application(
                    "gdl:literal",
                    _occurrence_pattern(boundary.judgment.source),
                    _term_pattern(node.term),
                ),
                expected_proofs=proof_count(boundary.judgment.source),
            )
        )

    used_signature_rules = set(signature_rule_ids.values())
    if not used_signature_rules.issubset(rule_ids):
        raise PresentationError("a checked signature has no inference rule")
    for derivation in extension.variable_derivations:
        if derivation.variable not in variable_type_rule:
            raise PresentationError("a checked variable has no scope fact")
    for discharge in extension.proposal.witness.acceptances:
        for step in discharge.path.steps:
            if step not in subtype_rule:
                raise PresentationError("a checked subtype step has no authored rule")

    judgments = (
        ("type:of", 3),
        ("gdl:source-node", 2),
        ("gdl:source-children", 2),
        ("gdl:source-form", 2),
        ("gdl:signature", 3),
        ("gdl:variable-type", 3),
        ("gdl:subtype", 2),
        ("gdl:accepts", 2),
        ("gdl:arguments-type", 3),
        ("gdl:arguments-typed", 2),
        ("gdl:all-type", 2),
        ("gdl:literal", 2),
    )
    judgment_names = {name for name, _arity in judgments}
    constructor_map: dict[str, int] = {}
    for rule in rules:
        for pattern in (*rule.premises, rule.conclusion):
            for head, arity in _pattern_heads(pattern):
                if head in judgment_names:
                    continue
                previous = constructor_map.setdefault(head, arity)
                if previous != arity:
                    raise PresentationError(
                        f"constructor {head!r} appears at two arities"
                    )
    for case in cases:
        for head, arity in _pattern_heads(case.goal):
            if head in judgment_names:
                continue
            previous = constructor_map.setdefault(head, arity)
            if previous != arity:
                raise PresentationError(
                    f"constructor {head!r} appears at two arities"
                )
    constructors = tuple(sorted(constructor_map.items()))

    seed = "\n".join(
        (
            "gdl-type-of-inference-v2",
            source_digest,
            profile_digest,
            *(_render_rule(rule) for rule in rules),
            *(
                f"{case.kind}:{case.expected_proofs}:"
                f"{_render_pattern(case.goal, False)}"
                for case in cases
            ),
        )
    )
    revision = "gdl-type-of-" + sha256(seed.encode("utf-8")).hexdigest()
    return GdlTypeOfInferenceProgram(
        source_digest=source_digest,
        profile_digest=profile_digest,
        revision=revision,
        constructors=constructors,
        judgments=judgments,
        rules=tuple(rules),
        cases=tuple(cases),
    )


def render_gdl_type_of_inference_program(
    program: GdlTypeOfInferenceProgram,
) -> str:
    """Render one canonical, authority-free inference document.

    Rules are flat siblings rather than a deeply nested wire list.  This is
    semantically neutral, permits incremental checking, and keeps document
    depth proportional to one rule instead of to the size of the corpus.
    """

    constructors = _wire_list(
        (
            f'(CDecl {_quoted(name)} {arity})'
            for name, arity in program.constructors
        ),
        "LNil",
        "LCons",
    )
    judgments = _wire_list(
        (
            f'(JDecl {_quoted(name)} {arity})'
            for name, arity in program.judgments
        ),
        "LNil",
        "LCons",
    )
    presentation = (
        f"(GPresentationV1 1 {constructors} {judgments} LNil "
        "GNoConversion)"
    )
    rules = "(rules\n    " + "\n    ".join(
        _render_rule(rule) for rule in program.rules
    ) + ")"
    package = "(rm-package\n    " + "\n    ".join(
        _render_rule_machine_block(rule) for rule in program.rules
    ) + ")"
    cases = "(cases\n    " + "\n    ".join(
        f"(case {case.expected_proofs} "
        f"{_render_pattern(case.goal, False)})"
        for case in program.cases
    ) + ")"
    return "\n".join(
        (
            "(gdl-type-of-inference-v2",
            f"  (source-digest {_quoted(program.source_digest)})",
            f"  (profile-digest {_quoted(program.profile_digest)})",
            f"  (revision {program.revision})",
            f"  (presentation {presentation})",
            f"  {rules}",
            f"  (rule-package {package})",
            f"  {cases})",
            "",
        )
    )
