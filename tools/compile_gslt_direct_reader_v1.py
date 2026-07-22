#!/usr/bin/env python3
"""Compile a structurally admitted S-expression LanguageDef to native C data.

The compiler recognizes the direct-reader schema from the actual syntax tree.
It extracts every delimiter, scalar class, boundary, escape, and Unicode
residual transition.  A grammar outside that schema is rejected; it is never
silently interpreted as the expected reader.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import re
import sys
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gslt2parse_schema_v1 import (  # noqa: E402
    Presentation,
    SExpr,
    Symbol,
    Variable,
    parse_presentation,
    render,
)


class CompileError(ValueError):
    pass


DIRECT_SEXPR_FRAGMENT_V1 = "deterministic-sexpr-direct-v1"


def symbol(term: SExpr, where: str) -> str:
    if not isinstance(term, Symbol) or not term.text:
        raise CompileError(f"{where}: expected a symbol, got {render(term)}")
    return term.text


def form(
    term: SExpr, head: str, arity: int | None = None, where: str = "form"
) -> tuple[SExpr, ...]:
    if not isinstance(term, tuple) or not term or symbol(term[0], where) != head:
        raise CompileError(f"{where}: expected ({head} ...), got {render(term)}")
    if arity is not None and len(term) != arity + 1:
        raise CompileError(
            f"{where}: expected {head}/{arity}, got {render(term)}"
        )
    return term


def tagged(term: SExpr, head: str) -> bool:
    return (
        isinstance(term, tuple)
        and bool(term)
        and isinstance(term[0], Symbol)
        and term[0].text == head
    )


def codepoint(term: SExpr, where: str) -> int:
    value = form(term, "cp", 1, where)[1]
    if not isinstance(value, int) or value < 0 or value > 0x10FFFF:
        raise CompileError(f"{where}: invalid Unicode scalar {render(value)}")
    if 0xD800 <= value <= 0xDFFF:
        raise CompileError(f"{where}: surrogate is not a Unicode scalar")
    return value


def list_codepoints(term: SExpr, where: str) -> tuple[int, ...]:
    values: list[int] = []
    current = term
    while tagged(current, "cons"):
        item = form(current, "cons", 2, where)
        values.append(codepoint(item[1], f"{where}: item"))
        current = item[2]
    if not isinstance(current, Symbol) or current.text != "nil":
        raise CompileError(f"{where}: malformed codepoint list")
    return tuple(values)


def definitions(presentation: Presentation) -> dict[str, SExpr]:
    result: dict[str, SExpr] = {}
    for rule in presentation.rules:
        head = form(
            rule.head,
            "definition",
            2,
            f"{presentation.source}:{rule.name}",
        )
        if rule.body:
            raise CompileError(
                f"{presentation.source}:{rule.name}: definition has premises"
            )
        name = symbol(head[1], f"{presentation.source}:{rule.name}: name")
        if name in result:
            raise CompileError(f"duplicate syntax definition {name}")
        result[name] = head[2]
    return result


@dataclass(frozen=True)
class NormalizedSyntaxIR:
    presentation_name: str
    definitions: dict[str, SExpr]


def compile_grammar_ir(term: SExpr, where: str) -> SExpr:
    """Execute the structural part of SyntaxCompilerV1 on one grammar term."""

    if isinstance(term, Symbol):
        if term.text == "any":
            return Symbol("sir-any")
        if term.text == "eof":
            return Symbol("sir-eof")
        raise CompileError(f"{where}: unsupported grammar atom {render(term)}")
    if not isinstance(term, tuple) or not term:
        raise CompileError(f"{where}: unsupported grammar term {render(term)}")
    head = symbol(term[0], where)
    unary = {
        "char": "sir-char",
        "class": "sir-class",
        "eps": "sir-eps",
        "lit": "sir-lit",
        "opt": "sir-opt",
        "peek": "sir-peek",
        "plus": "sir-plus",
        "ref": "sir-ref",
        "star": "sir-star",
    }
    binary = {
        "alt": "sir-alt",
        "left": "sir-left",
        "node": "sir-node",
        "right": "sir-right",
        "seq": "sir-seq",
        "until": "sir-until",
    }
    if head in {"char", "class", "eps", "lit", "ref"}:
        value = form(term, head, 1, where)
        return (Symbol(unary[head]), value[1])
    if head in {"opt", "peek", "plus", "star"}:
        value = form(term, head, 1, where)
        return (
            Symbol(unary[head]),
            compile_grammar_ir(value[1], f"{where}: {head}"),
        )
    if head == "node":
        value = form(term, head, 2, where)
        symbol(value[1], f"{where}: node label")
        return (
            Symbol(binary[head]),
            value[1],
            compile_grammar_ir(value[2], f"{where}: node body"),
        )
    if head in {"alt", "left", "right", "seq", "until"}:
        value = form(term, head, 2, where)
        return (
            Symbol(binary[head]),
            compile_grammar_ir(value[1], f"{where}: {head} left"),
            compile_grammar_ir(value[2], f"{where}: {head} right"),
        )
    raise CompileError(f"{where}: unsupported grammar constructor {head}")


def normalize_syntax_ir(presentation: Presentation) -> NormalizedSyntaxIR:
    source_definitions = definitions(presentation)
    return NormalizedSyntaxIR(
        presentation.name,
        {
            name: compile_grammar_ir(
                grammar, f"{presentation.source}: definition {name}"
            )
            for name, grammar in source_definitions.items()
        },
    )


def sir_form(
    term: SExpr, head: str, arity: int | None = None, where: str = "SIR form"
) -> tuple[SExpr, ...]:
    return form(term, f"sir-{head}", arity, where)


def sir_tagged(term: SExpr, head: str) -> bool:
    return tagged(term, f"sir-{head}")


def sir_reference(term: SExpr, where: str) -> str:
    return symbol(sir_form(term, "ref", 1, where)[1], f"{where}: reference")


def sir_class_name(term: SExpr, where: str) -> str:
    return symbol(sir_form(term, "class", 1, where)[1], f"{where}: class")


def sir_character(term: SExpr, where: str) -> int:
    return codepoint(sir_form(term, "char", 1, where)[1], f"{where}: char")


def sir_literal(term: SExpr, where: str) -> tuple[int, ...]:
    if sir_tagged(term, "char"):
        return (sir_character(term, where),)
    value = sir_form(term, "lit", 1, where)
    return list_codepoints(value[1], f"{where}: literal")


def flatten_sir_binary(term: SExpr, head: str) -> list[SExpr]:
    if sir_tagged(term, head):
        value = sir_form(term, head, 2, head)
        return flatten_sir_binary(value[1], head) + flatten_sir_binary(
            value[2], head
        )
    return [term]


@dataclass(frozen=True)
class ScalarClass:
    name: str
    points: tuple[int, ...]
    complement: bool

    def contains(self, value: int) -> bool:
        found = value in self._point_set
        return not found if self.complement else found

    @property
    def _point_set(self) -> frozenset[int]:
        return frozenset(self.points)


def scalar_classes(presentation: Presentation) -> dict[str, ScalarClass]:
    positive: dict[str, set[int]] = {}
    excluded: dict[str, set[int]] = {}
    for rule in presentation.rules:
        head = form(
            rule.head, "member", 2, f"{presentation.source}:{rule.name}"
        )
        name = symbol(head[1], f"{presentation.source}:{rule.name}: class")
        member = head[2]
        if isinstance(member, tuple):
            if rule.body:
                raise CompileError(
                    f"{presentation.source}:{rule.name}: finite member has premises"
                )
            positive.setdefault(name, set()).add(
                codepoint(member, f"{presentation.source}:{rule.name}: member")
            )
            continue
        if not isinstance(member, Variable):
            raise CompileError(
                f"{presentation.source}:{rule.name}: unsupported class rule"
            )
        if name in excluded:
            raise CompileError(f"duplicate complement definition for {name}")
        denied: set[int] = set()
        for premise in rule.body:
            different = form(
                premise,
                "different",
                2,
                f"{presentation.source}:{rule.name}: premise",
            )
            if (
                not isinstance(different[1], Variable)
                or different[1].name != member.name
            ):
                raise CompileError(
                    f"{presentation.source}:{rule.name}: class premise constrains "
                    "the wrong variable"
                )
            denied.add(
                codepoint(
                    different[2],
                    f"{presentation.source}:{rule.name}: exclusion",
                )
            )
        excluded[name] = denied
    overlap = set(positive) & set(excluded)
    if overlap:
        raise CompileError(
            "class has finite and complement definitions: "
            + ", ".join(sorted(overlap))
        )
    result = {
        name: ScalarClass(name, tuple(sorted(points)), False)
        for name, points in positive.items()
    }
    result.update(
        {
            name: ScalarClass(name, tuple(sorted(points)), True)
            for name, points in excluded.items()
        }
    )
    return result


@dataclass(frozen=True)
class ProjectionPolicy:
    profile: str
    document_label: str
    expression_label: str
    word_label: str
    string_label: str
    variable_label: str
    simple_label: str
    simple_map: tuple[tuple[int, int], ...]
    byte_label: str
    byte_hex: tuple[int, int, int, int]
    unicode_label: str
    unicode_hex: tuple[int, int, int, int]


def projection_policy(
    presentation: Presentation, profile_name: str
) -> ProjectionPolicy:
    document_label: str | None = None
    expression_label: str | None = None
    atom_rules: dict[str, tuple[str, str]] = {}
    variable_rules: dict[str, tuple[str, str]] = {}
    maps: dict[str, tuple[str, tuple[tuple[int, int], ...]]] = {}
    hexes: dict[str, tuple[int, int, int, int]] = {}
    for rule in presentation.rules:
        if rule.body or not isinstance(rule.head, tuple) or not rule.head:
            continue
        tag = symbol(rule.head[0], f"{presentation.source}:{rule.name}: tag")
        if tag == "sexpr-atom-profile":
            value = form(
                rule.head,
                "sexpr-atom-profile",
                8,
                f"{presentation.source}:{rule.name}",
            )
            if symbol(value[1], "projection profile") != profile_name:
                continue
            if document_label is not None:
                raise CompileError(f"duplicate projection profile {profile_name}")
            document_label = symbol(value[7], "document label")
            expression_label = symbol(value[8], "expression label")
        elif tag == "sexpr-atom-rule":
            value = form(rule.head, tag, 4, f"{presentation.source}:{rule.name}")
            if symbol(value[1], "projection profile") == profile_name:
                atom_rules[symbol(value[2], "atom wrapper")] = (
                    symbol(value[3], "atom kind"),
                    symbol(value[4], "atom mode"),
                )
        elif tag == "sexpr-variable-rule":
            value = form(rule.head, tag, 4, f"{presentation.source}:{rule.name}")
            if symbol(value[1], "projection profile") == profile_name:
                variable_rules[symbol(value[2], "variable wrapper")] = (
                    symbol(value[3], "variable mode"),
                    symbol(value[4], "anonymous policy"),
                )
        elif tag == "sexpr-wrapper-map":
            value = form(rule.head, tag, 4, f"{presentation.source}:{rule.name}")
            if symbol(value[1], "projection profile") != profile_name:
                continue
            wrapper = symbol(value[2], "map wrapper")
            mode = symbol(value[3], "map failure mode")
            entries: list[tuple[int, int]] = []
            current = value[4]
            while tagged(current, "sexpr-projection-cons"):
                cons = form(current, "sexpr-projection-cons", 2, "projection map")
                mapping = form(cons[1], "sexpr-codepoint-map", 2, "projection map")
                if not isinstance(mapping[1], int) or not isinstance(mapping[2], int):
                    raise CompileError("escape projection map must contain integers")
                entries.append((mapping[1], mapping[2]))
                current = cons[2]
            if not isinstance(current, Symbol) or current.text != "sexpr-projection-nil":
                raise CompileError("malformed escape projection map")
            maps[wrapper] = (mode, tuple(entries))
        elif tag == "sexpr-wrapper-hex":
            value = form(rule.head, tag, 6, f"{presentation.source}:{rule.name}")
            if symbol(value[1], "projection profile") != profile_name:
                continue
            if not all(isinstance(item, int) for item in value[3:]):
                raise CompileError("hex projection parameters must be integers")
            hexes[symbol(value[2], "hex wrapper")] = tuple(value[3:])  # type: ignore[arg-type]
    if document_label is None or expression_label is None:
        raise CompileError(f"missing projection profile {profile_name}")
    words = [name for name, value in atom_rules.items() if value == ("symbol", "raw")]
    strings = [name for name, value in atom_rules.items() if value == ("string", "decoded")]
    variables = [
        name for name, value in variable_rules.items() if value == ("raw", "no-anonymous")
    ]
    simple = [name for name, value in maps.items() if value[0] == "reject"]
    byte = [name for name, value in hexes.items() if value[0] == value[1] == 2]
    unicode = [name for name, value in hexes.items() if value[0] == 1 and value[1] >= 4]
    for label, candidates in (
        ("word", words),
        ("string", strings),
        ("variable", variables),
        ("simple escape", simple),
        ("byte escape", byte),
        ("Unicode escape", unicode),
    ):
        if len(candidates) != 1:
            raise CompileError(
                f"projection profile needs exactly one {label}; found {candidates}"
            )
    return ProjectionPolicy(
        profile_name,
        document_label,
        expression_label,
        words[0],
        strings[0],
        variables[0],
        simple[0],
        maps[simple[0]][1],
        byte[0],
        hexes[byte[0]],
        unicode[0],
        hexes[unicode[0]],
    )


@dataclass(frozen=True)
class Boundary:
    allow_eof: bool
    literals: tuple[int, ...]
    classes: tuple[str, ...]


def parse_boundary(term: SExpr, where: str) -> Boundary:
    allow_eof = False
    literals: set[int] = set()
    classes: set[str] = set()
    for branch in flatten_sir_binary(term, "alt"):
        if isinstance(branch, Symbol) and branch.text == "sir-eof":
            allow_eof = True
        elif sir_tagged(branch, "char"):
            literals.add(sir_character(branch, where))
        elif sir_tagged(branch, "class"):
            classes.add(sir_class_name(branch, where))
        else:
            raise CompileError(f"{where}: unsupported boundary branch {render(branch)}")
    return Boundary(allow_eof, tuple(sorted(literals)), tuple(sorted(classes)))


def node_definitions(defs: dict[str, SExpr]) -> dict[str, tuple[str, SExpr]]:
    result: dict[str, tuple[str, SExpr]] = {}
    for name, grammar in defs.items():
        if not sir_tagged(grammar, "node"):
            continue
        node = sir_form(grammar, "node", 2, f"definition {name}")
        label = symbol(node[1], f"definition {name}: node label")
        if label in result:
            raise CompileError(f"duplicate node label {label}")
        result[label] = (name, node[2])
    return result


def node_body(
    nodes: dict[str, tuple[str, SExpr]], label: str
) -> tuple[str, SExpr]:
    if label not in nodes:
        raise CompileError(f"missing syntax node {label}")
    return nodes[label]


@dataclass(frozen=True)
class UnicodeDFA:
    state_names: tuple[str, ...]
    accepting: tuple[bool, ...]
    transitions: tuple[tuple[int, ...], ...]
    transition_classes: tuple[str, ...]


def compile_unicode_dfa(
    defs: dict[str, SExpr], classes: dict[str, ScalarClass], start: str,
    minimum: int, maximum: int, radix: int
) -> UnicodeDFA:
    def resolve_alias(name: str) -> str:
        visited: set[str] = set()
        while name in defs and sir_tagged(defs[name], "ref"):
            if name in visited:
                raise CompileError("cyclic Unicode residual-state alias")
            visited.add(name)
            name = sir_reference(defs[name], f"Unicode state alias {name}")
        return name

    start = resolve_alias(start)
    queue: deque[str] = deque([start])
    names: list[str] = []
    seen: set[str] = set()
    raw: dict[str, tuple[bool, list[tuple[str, str]]]] = {}
    used_classes: set[str] = set()
    while queue:
        name = queue.popleft()
        if name in seen:
            continue
        seen.add(name)
        names.append(name)
        if name not in defs:
            raise CompileError(f"Unicode residual state {name} is undefined")
        accepting = False
        transitions: list[tuple[str, str]] = []
        for branch in flatten_sir_binary(defs[name], "alt"):
            if sir_tagged(branch, "eps"):
                sir_form(branch, "eps", 1, f"Unicode state {name}")
                accepting = True
                continue
            sequence = sir_form(branch, "seq", 2, f"Unicode state {name}")
            cls = sir_class_name(sequence[1], f"Unicode state {name}")
            target = resolve_alias(
                sir_reference(sequence[2], f"Unicode state {name}")
            )
            if cls not in classes or classes[cls].complement:
                raise CompileError(
                    f"Unicode state {name} requires a finite class {cls}"
                )
            for point in classes[cls].points:
                if point >= 256 or digit_value(point, radix) is None:
                    raise CompileError(
                        f"Unicode transition class {cls} is not a radix-{radix} digit class"
                    )
            transitions.append((cls, target))
            used_classes.add(cls)
            queue.append(target)
        raw[name] = (accepting, transitions)
    indices = {name: index for index, name in enumerate(names)}
    rows: list[tuple[int, ...]] = []
    accepting_rows: list[bool] = []
    for name in names:
        accepting, transitions = raw[name]
        row = [-1] * 256
        for cls, target in transitions:
            for point in classes[cls].points:
                old = row[point]
                next_state = indices[target]
                if old >= 0 and old != next_state:
                    raise CompileError(
                        f"Unicode state {name} has overlapping transitions at U+{point:04X}"
                    )
                row[point] = next_state
        accepting_rows.append(accepting)
        rows.append(tuple(row))
    reachable: set[tuple[int, int]] = {(0, 0)}
    frontier = {(0, 0)}
    accepted_depths: set[int] = set()
    while frontier:
        next_frontier: set[tuple[int, int]] = set()
        for state_id, depth in frontier:
            if accepting_rows[state_id]:
                accepted_depths.add(depth)
            if depth > maximum:
                continue
            for target in rows[state_id]:
                if target >= 0:
                    item = (target, depth + 1)
                    if item not in reachable:
                        reachable.add(item)
                        next_frontier.add(item)
        frontier = next_frontier
    if not accepted_depths or min(accepted_depths) < minimum or max(accepted_depths) > maximum:
        raise CompileError(
            "Unicode residual DFA acceptance depths disagree with projection bounds"
        )
    if any(depth > maximum for _, depth in reachable):
        raise CompileError("Unicode residual DFA admits an overlong path")
    return UnicodeDFA(
        tuple(names), tuple(accepting_rows), tuple(rows), tuple(sorted(used_classes))
    )


def digit_value(codepoint_value: int, radix: int) -> int | None:
    if ord("0") <= codepoint_value <= ord("9"):
        value = codepoint_value - ord("0")
    elif ord("a") <= codepoint_value <= ord("z"):
        value = codepoint_value - ord("a") + 10
    elif ord("A") <= codepoint_value <= ord("Z"):
        value = codepoint_value - ord("A") + 10
    else:
        return None
    return value if value < radix else None


@dataclass(frozen=True)
class DirectPlan:
    presentation_name: str
    whitespace: str
    comment_marker: int
    comment_body: str
    comment_boundary: Boundary
    expression_open: int
    expression_close: int
    string_open: int
    string_close: int
    variable_marker: int
    word_start: str
    word_tail: str
    word_boundary: Boundary
    variable_tail: str
    variable_boundary: Boundary
    string_plain: str
    simple_prefix: tuple[int, ...]
    simple_map: tuple[tuple[int, int], ...]
    byte_prefix: tuple[int, ...]
    byte_digit_classes: tuple[str, ...]
    byte_hex: tuple[int, int, int, int]
    unicode_prefix: tuple[int, ...]
    unicode_suffix: int
    unicode_dfa: UnicodeDFA
    unicode_hex: tuple[int, int, int, int]
    used_classes: tuple[str, ...]


@dataclass(frozen=True)
class ParserFragmentSelection:
    fragment: str
    plan: DirectPlan


def derive_plan(
    syntax: Presentation,
    classes: dict[str, ScalarClass],
    policy: ProjectionPolicy,
) -> DirectPlan:
    syntax_ir = normalize_syntax_ir(syntax)
    defs = syntax_ir.definitions
    nodes = node_definitions(defs)
    document_name, document_body = node_body(nodes, policy.document_label)
    expression_name, expression_body = node_body(nodes, policy.expression_label)
    word_name, word_body = node_body(nodes, policy.word_label)
    string_name, string_body = node_body(nodes, policy.string_label)
    variable_name, variable_body = node_body(nodes, policy.variable_label)
    simple_name, simple_body = node_body(nodes, policy.simple_label)
    byte_name, byte_body = node_body(nodes, policy.byte_label)
    unicode_name, unicode_body = node_body(nodes, policy.unicode_label)

    document_left = sir_form(document_body, "left", 2, "document")
    document_start = sir_form(document_left[1], "right", 2, "document start")
    skip_name = sir_reference(document_start[1], "document skip")
    document_star = sir_form(
        document_start[2], "star", 1, "document repetition"
    )
    document_item = sir_form(document_star[1], "left", 2, "document item")
    top_name = sir_reference(document_item[1], "document top atom")
    if sir_reference(document_item[2], "document trailing skip") != skip_name:
        raise CompileError("document uses two different skip definitions")
    if (
        not isinstance(document_left[2], Symbol)
        or document_left[2].text != "sir-eof"
    ):
        raise CompileError("document must end at eof")
    atom_name = sir_reference(defs[top_name], "top atom")

    skip = sir_form(defs[skip_name], "star", 1, "skip")
    layout_name = sir_reference(skip[1], "skip layout")
    layout_branches = flatten_sir_binary(defs[layout_name], "alt")
    whitespace_names = [
        sir_class_name(branch, "layout whitespace")
        for branch in layout_branches
        if sir_tagged(branch, "class")
    ]
    comment_names = [
        sir_reference(branch, "layout comment")
        for branch in layout_branches
        if sir_tagged(branch, "ref")
    ]
    if len(whitespace_names) != 1 or len(comment_names) != 1 or len(layout_branches) != 2:
        raise CompileError("layout must be one whitespace class or one comment")
    whitespace = whitespace_names[0]
    comment_grammar = defs[comment_names[0]]
    comment_node = sir_form(comment_grammar, "node", 2, "comment")
    comment_right = sir_form(comment_node[2], "right", 2, "comment")
    comment_marker = sir_character(comment_right[1], "comment marker")
    comment_left = sir_form(comment_right[2], "left", 2, "comment body")
    comment_star = sir_form(comment_left[1], "star", 1, "comment body")
    comment_body_class = sir_class_name(comment_star[1], "comment body")
    comment_peek = sir_form(comment_left[2], "peek", 1, "comment boundary")
    comment_boundary_name = sir_reference(comment_peek[1], "comment boundary")
    comment_boundary = parse_boundary(
        defs[comment_boundary_name], "comment boundary"
    )

    expression = sir_form(expression_body, "right", 2, "expression")
    expression_open = sir_character(expression[1], "expression open")
    expression_left = sir_form(expression[2], "left", 2, "expression")
    expression_start = sir_form(expression_left[1], "right", 2, "expression")
    if sir_reference(expression_start[1], "expression leading skip") != skip_name:
        raise CompileError("expression and document use different skip definitions")
    expression_star = sir_form(expression_start[2], "star", 1, "expression")
    expression_item = sir_form(
        expression_star[1], "left", 2, "expression item"
    )
    if sir_reference(expression_item[1], "expression atom") != atom_name:
        raise CompileError("expression and document use different atom definitions")
    if sir_reference(expression_item[2], "expression skip") != skip_name:
        raise CompileError("expression uses an unexpected trailing skip")
    expression_close = sir_character(expression_left[2], "expression close")

    atom_refs = {
        sir_reference(branch, "atom alternative")
        for branch in flatten_sir_binary(defs[atom_name], "alt")
    }
    expected_atom_refs = {word_name, string_name, variable_name, expression_name}
    if atom_refs != expected_atom_refs:
        raise CompileError(
            "atom alternatives differ from projected word/string/variable/expression nodes"
        )

    word = sir_form(word_body, "left", 2, "word")
    word_sequence = sir_form(word[1], "seq", 2, "word")
    word_start = sir_class_name(word_sequence[1], "word start")
    word_star = sir_form(word_sequence[2], "star", 1, "word tail")
    word_tail = sir_class_name(word_star[1], "word tail")
    word_peek = sir_form(word[2], "peek", 1, "word boundary")
    word_boundary = parse_boundary(
        defs[sir_reference(word_peek[1], "word boundary")],
        "word boundary",
    )

    variable = sir_form(variable_body, "left", 2, "variable")
    variable_right = sir_form(variable[1], "right", 2, "variable")
    variable_marker = sir_character(variable_right[1], "variable marker")
    variable_sequence = sir_form(
        variable_right[2], "seq", 2, "variable body"
    )
    variable_tail = sir_class_name(
        variable_sequence[1], "variable first scalar"
    )
    variable_star = sir_form(variable_sequence[2], "star", 1, "variable tail")
    if sir_class_name(variable_star[1], "variable tail") != variable_tail:
        raise CompileError("variable first and subsequent scalar classes differ")
    variable_peek = sir_form(variable[2], "peek", 1, "variable boundary")
    variable_boundary = parse_boundary(
        defs[sir_reference(variable_peek[1], "variable boundary")],
        "variable boundary",
    )

    string = sir_form(string_body, "right", 2, "string")
    string_open = sir_character(string[1], "string open")
    string_left = sir_form(string[2], "left", 2, "string")
    string_star = sir_form(string_left[1], "star", 1, "string body")
    string_unit_name = sir_reference(string_star[1], "string unit")
    string_close = sir_character(string_left[2], "string close")
    string_units = flatten_sir_binary(defs[string_unit_name], "alt")
    unit_refs = {
        sir_reference(item, "string unit")
        for item in string_units
        if sir_tagged(item, "ref")
    }
    unit_classes = [
        sir_class_name(item, "string unit")
        for item in string_units
        if sir_tagged(item, "class")
    ]
    if unit_refs != {simple_name, byte_name, unicode_name} or len(unit_classes) != 1 or len(string_units) != 4:
        raise CompileError("string units differ from the projected escape wrappers")
    string_plain = unit_classes[0]

    simple = sir_form(simple_body, "right", 2, "simple escape")
    simple_prefix = sir_literal(simple[1], "simple escape prefix")
    simple_class = sir_class_name(simple[2], "simple escape class")
    if simple_class not in classes:
        raise CompileError(f"undefined simple escape class {simple_class}")
    map_sources = {source for source, _ in policy.simple_map}
    admitted_sources = {
        point
        for point in range(0x110000)
        if classes[simple_class].contains(point)
    }
    if map_sources != admitted_sources:
        raise CompileError(
            "simple escape projection map and syntax class disagree"
        )

    byte = sir_form(byte_body, "right", 2, "byte escape")
    byte_prefix = sir_literal(byte[1], "byte escape prefix")
    byte_digit_classes = tuple(
        sir_class_name(item, "byte escape digit")
        for item in flatten_sir_binary(byte[2], "seq")
    )
    if len(byte_digit_classes) != policy.byte_hex[1]:
        raise CompileError("byte escape syntax and projection digit counts disagree")
    for name in byte_digit_classes:
        if name not in classes:
            raise CompileError(f"undefined byte digit class {name}")
        for point in range(0x110000):
            if classes[name].contains(point) and digit_value(point, policy.byte_hex[3]) is None:
                raise CompileError(f"byte digit class {name} admits a non-digit")

    unicode = sir_form(unicode_body, "right", 2, "Unicode escape")
    unicode_prefix = sir_literal(unicode[1], "Unicode escape prefix")
    unicode_left = sir_form(unicode[2], "left", 2, "Unicode escape")
    unicode_start_name = sir_reference(
        unicode_left[1], "Unicode residual start"
    )
    unicode_suffix = sir_character(unicode_left[2], "Unicode escape suffix")
    unicode_dfa = compile_unicode_dfa(
        defs,
        classes,
        unicode_start_name,
        policy.unicode_hex[0],
        policy.unicode_hex[1],
        policy.unicode_hex[3],
    )

    used = {
        whitespace,
        comment_body_class,
        word_start,
        word_tail,
        variable_tail,
        string_plain,
        *comment_boundary.classes,
        *word_boundary.classes,
        *variable_boundary.classes,
        *byte_digit_classes,
    }
    required = used | {simple_class, *unicode_dfa.transition_classes}
    missing = sorted(required - set(classes))
    if missing:
        raise CompileError("undefined scalar classes: " + ", ".join(missing))

    for label, value in (
        ("expression open", expression_open),
        ("expression close", expression_close),
        ("string open", string_open),
        ("string close", string_close),
        ("variable marker", variable_marker),
    ):
        if classes[word_start].contains(value):
            raise CompileError(f"word-start class overlaps {label}")
    if len({expression_open, string_open, variable_marker}) != 3:
        raise CompileError("atom dispatch literals are not disjoint")
    escape_starts = {simple_prefix[0], byte_prefix[0], unicode_prefix[0]}
    if classes[string_plain].contains(string_close) or any(
        classes[string_plain].contains(value) for value in escape_starts
    ):
        raise CompileError("string-plain class overlaps close/escape dispatch")
    return DirectPlan(
        syntax_ir.presentation_name,
        whitespace,
        comment_marker,
        comment_body_class,
        comment_boundary,
        expression_open,
        expression_close,
        string_open,
        string_close,
        variable_marker,
        word_start,
        word_tail,
        word_boundary,
        variable_tail,
        variable_boundary,
        string_plain,
        simple_prefix,
        policy.simple_map,
        byte_prefix,
        byte_digit_classes,
        policy.byte_hex,
        unicode_prefix,
        unicode_suffix,
        unicode_dfa,
        policy.unicode_hex,
        tuple(sorted(used)),
    )


def classify_parser_fragment(
    syntax: Presentation,
    classes: dict[str, ScalarClass],
    policy: ProjectionPolicy,
) -> ParserFragmentSelection:
    try:
        plan = derive_plan(syntax, classes, policy)
    except CompileError as error:
        raise CompileError(
            f"LanguageDef is outside {DIRECT_SEXPR_FRAGMENT_V1}: {error}"
        ) from error
    return ParserFragmentSelection(DIRECT_SEXPR_FRAGMENT_V1, plan)


def file_digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def composition_digest(
    paths: Iterable[Path], profile: str, fragment: str
) -> str:
    digest = sha256()
    digest.update(b"GSLTDirectReaderV1\0")
    digest.update(profile.encode("utf-8"))
    digest.update(b"\0")
    digest.update(fragment.encode("utf-8"))
    digest.update(b"\0")
    for path in paths:
        payload = path.read_bytes()
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
    return digest.hexdigest()


def c_identifier(text: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", text)
    if not result or result[0].isdigit():
        result = "v_" + result
    return result


def c_u32_array(name: str, values: Sequence[int]) -> str:
    if not values:
        return ""
    body = ", ".join(f"UINT32_C({value})" for value in values)
    return f"static const uint32_t {name}[] = {{{body}}};\n"


def c_u8_array(name: str, values: Sequence[int], columns: int = 16) -> str:
    rows = [
        "    " + ", ".join(str(value) for value in values[start : start + columns])
        for start in range(0, len(values), columns)
    ]
    return f"static const uint8_t {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def c_i16_array(name: str, values: Sequence[int], columns: int = 16) -> str:
    rows = [
        "    " + ", ".join(str(value) for value in values[start : start + columns])
        for start in range(0, len(values), columns)
    ]
    return f"static const int16_t {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def emit_boundary(
    lines: list[str], prefix: str, boundary: Boundary,
    class_ids: dict[str, str]
) -> str:
    literal_name = f"{prefix}_literals"
    class_name_value = f"{prefix}_classes"
    if boundary.literals:
        lines.append(c_u32_array(literal_name, boundary.literals))
    if boundary.classes:
        lines.append(
            f"static const GSLTDirectScalarClassV1 *const {class_name_value}[] = {{\n"
        )
        for value in boundary.classes:
            lines.append(f"    &{class_ids[value]},\n")
        lines.append("};\n")
    return (
        "{"
        f".allow_eof = {'true' if boundary.allow_eof else 'false'}, "
        f".literals = {literal_name if boundary.literals else 'NULL'}, "
        f".literal_len = UINT32_C({len(boundary.literals)}), "
        f".classes = {class_name_value if boundary.classes else 'NULL'}, "
        f".class_len = UINT32_C({len(boundary.classes)})"
        "}"
    )


def generate(
    syntax_path: Path,
    classes_path: Path,
    projection_path: Path,
    profile: str,
    c_prefix: str,
    output_c: Path,
    output_h: Path,
) -> None:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", c_prefix):
        raise CompileError(f"invalid C prefix {c_prefix!r}")
    syntax = parse_presentation(syntax_path)
    class_presentation = parse_presentation(classes_path)
    projection = parse_presentation(projection_path)
    classes = scalar_classes(class_presentation)
    policy = projection_policy(projection, profile)
    selection = classify_parser_fragment(syntax, classes, policy)
    plan = selection.plan
    digest = composition_digest(
        (syntax_path, classes_path, projection_path),
        profile,
        selection.fragment,
    )
    class_ids = {
        name: f"{c_prefix}_class_{index}_{c_identifier(name)}"
        for index, name in enumerate(plan.used_classes)
    }
    lines = [
        "/* Generated from the composed syntax, scalar-class, and projection GSLTs. */\n",
        f'#include "generated/{output_h.name}"\n\n',
        "#include <stddef.h>\n",
        "#include <stdint.h>\n\n",
    ]
    for name in plan.used_classes:
        scalar_class = classes[name]
        identifier = class_ids[name]
        points_name = f"{identifier}_points"
        ascii_name = f"{identifier}_ascii"
        if scalar_class.points:
            lines.append(c_u32_array(points_name, scalar_class.points))
        ascii_values = [
            1 if scalar_class.contains(value) else 0 for value in range(256)
        ]
        lines.append(c_u8_array(ascii_name, ascii_values))
        lines.append(
            f"static const GSLTDirectScalarClassV1 {identifier} = {{\n"
            f"    .ascii = {ascii_name},\n"
            f"    .points = {points_name if scalar_class.points else 'NULL'},\n"
            f"    .point_len = UINT32_C({len(scalar_class.points)}),\n"
            f"    .complement = {'true' if scalar_class.complement else 'false'},\n"
            "};\n"
        )
    simple_prefix_name = f"{c_prefix}_simple_prefix"
    byte_prefix_name = f"{c_prefix}_byte_prefix"
    unicode_prefix_name = f"{c_prefix}_unicode_prefix"
    lines.append(c_u32_array(simple_prefix_name, plan.simple_prefix))
    lines.append(c_u32_array(byte_prefix_name, plan.byte_prefix))
    lines.append(c_u32_array(unicode_prefix_name, plan.unicode_prefix))
    simple_map_name = f"{c_prefix}_simple_map"
    lines.append(
        f"static const GSLTDirectCodepointMapV1 {simple_map_name}[] = {{\n"
    )
    for source, target in plan.simple_map:
        lines.append(
            f"    {{UINT32_C({source}), UINT32_C({target})}},\n"
        )
    lines.append("};\n")
    byte_classes_name = f"{c_prefix}_byte_digit_classes"
    lines.append(
        f"static const GSLTDirectScalarClassV1 *const {byte_classes_name}[] = {{\n"
    )
    for name in plan.byte_digit_classes:
        lines.append(f"    &{class_ids[name]},\n")
    lines.append("};\n")
    unicode_transitions_name = f"{c_prefix}_unicode_transitions"
    unicode_accepting_name = f"{c_prefix}_unicode_accepting"
    lines.append(
        c_i16_array(
            unicode_transitions_name,
            [value for row in plan.unicode_dfa.transitions for value in row],
        )
    )
    lines.append(
        c_u8_array(
            unicode_accepting_name,
            [1 if value else 0 for value in plan.unicode_dfa.accepting],
        )
    )
    comment_boundary = emit_boundary(
        lines, f"{c_prefix}_comment_boundary", plan.comment_boundary, class_ids
    )
    word_boundary = emit_boundary(
        lines, f"{c_prefix}_word_boundary", plan.word_boundary, class_ids
    )
    variable_boundary = emit_boundary(
        lines,
        f"{c_prefix}_variable_boundary",
        plan.variable_boundary,
        class_ids,
    )
    lines.append(
        f"\nconst GSLTDirectReaderV1Plan {c_prefix}_plan = {{\n"
        f'    .presentation_name = "{plan.presentation_name}",\n'
        f'    .fragment = "{selection.fragment}",\n'
        f'    .syntax_digest = "{file_digest(syntax_path)}",\n'
        f'    .class_digest = "{file_digest(classes_path)}",\n'
        f'    .projection_digest = "{file_digest(projection_path)}",\n'
        f'    .compiler_digest = "{file_digest(Path(__file__).resolve())}",\n'
        f'    .composition_digest = "{digest}",\n'
        f'    .profile = "{profile}",\n'
        f"    .whitespace = &{class_ids[plan.whitespace]},\n"
        f"    .comment_marker = UINT32_C({plan.comment_marker}),\n"
        f"    .comment_body = &{class_ids[plan.comment_body]},\n"
        f"    .comment_boundary = {comment_boundary},\n"
        f"    .expression_open = UINT32_C({plan.expression_open}),\n"
        f"    .expression_close = UINT32_C({plan.expression_close}),\n"
        f"    .string_open = UINT32_C({plan.string_open}),\n"
        f"    .string_close = UINT32_C({plan.string_close}),\n"
        f"    .variable_marker = UINT32_C({plan.variable_marker}),\n"
        f"    .word_start = &{class_ids[plan.word_start]},\n"
        f"    .word_tail = &{class_ids[plan.word_tail]},\n"
        f"    .word_boundary = {word_boundary},\n"
        f"    .variable_tail = &{class_ids[plan.variable_tail]},\n"
        f"    .variable_boundary = {variable_boundary},\n"
        f"    .string_plain = &{class_ids[plan.string_plain]},\n"
        f"    .simple_prefix = {simple_prefix_name},\n"
        f"    .simple_prefix_len = UINT32_C({len(plan.simple_prefix)}),\n"
        f"    .simple_map = {simple_map_name},\n"
        f"    .simple_map_len = UINT32_C({len(plan.simple_map)}),\n"
        f"    .byte_prefix = {byte_prefix_name},\n"
        f"    .byte_prefix_len = UINT32_C({len(plan.byte_prefix)}),\n"
        f"    .byte_digit_classes = {byte_classes_name},\n"
        f"    .byte_digit_class_len = UINT32_C({len(plan.byte_digit_classes)}),\n"
        f"    .byte_hex_min = UINT32_C({plan.byte_hex[0]}),\n"
        f"    .byte_hex_max = UINT32_C({plan.byte_hex[1]}),\n"
        f"    .byte_value_max = UINT32_C({plan.byte_hex[2]}),\n"
        f"    .byte_radix = UINT32_C({plan.byte_hex[3]}),\n"
        f"    .unicode_prefix = {unicode_prefix_name},\n"
        f"    .unicode_prefix_len = UINT32_C({len(plan.unicode_prefix)}),\n"
        f"    .unicode_suffix = UINT32_C({plan.unicode_suffix}),\n"
        f"    .unicode_transitions = {unicode_transitions_name},\n"
        f"    .unicode_accepting = {unicode_accepting_name},\n"
        f"    .unicode_state_len = UINT32_C({len(plan.unicode_dfa.state_names)}),\n"
        f"    .unicode_hex_min = UINT32_C({plan.unicode_hex[0]}),\n"
        f"    .unicode_hex_max = UINT32_C({plan.unicode_hex[1]}),\n"
        f"    .unicode_value_max = UINT32_C({plan.unicode_hex[2]}),\n"
        f"    .unicode_radix = UINT32_C({plan.unicode_hex[3]}),\n"
        "    .depth_limit = UINT32_C(4096),\n"
        "};\n\n"
        f"const char *{c_prefix}_program_digest(void) {{\n"
        f"    return {c_prefix}_plan.composition_digest;\n"
        "}\n\n"
        f"int {c_prefix}_parse_bytes_ids(\n"
        "    const uint8_t *input, size_t input_len,\n"
        "    const GSLTDirectAtomProjectionV1 *projection,\n"
        "    AtomId **out_ids, GSLTDirectReaderV1Receipt *receipt,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "#ifndef GSLT_DIRECT_READER_V1_INSTANTIATE\n"
        "#define GSLT_DIRECT_READER_V1_INSTANTIATE "
        "gslt_direct_reader_v1_parse_bytes_ids\n"
        "#define GSLT_DIRECT_READER_V1_INSTANTIATE_LOCAL UINT32_C(1)\n"
        "#endif\n"
        "    int result = GSLT_DIRECT_READER_V1_INSTANTIATE(\n"
        f"        &{c_prefix}_plan, input, input_len, projection, out_ids,\n"
        "        receipt, error_buf, error_buf_size);\n"
        "#ifdef GSLT_DIRECT_READER_V1_INSTANTIATE_LOCAL\n"
        "#undef GSLT_DIRECT_READER_V1_INSTANTIATE_LOCAL\n"
        "#undef GSLT_DIRECT_READER_V1_INSTANTIATE\n"
        "#endif\n"
        "    return result;\n"
        "}\n"
    )
    output_c.parent.mkdir(parents=True, exist_ok=True)
    output_c.write_text("".join(lines), encoding="utf-8")

    guard = "CETTA_GENERATED_" + c_prefix.upper() + "_H"
    output_h.parent.mkdir(parents=True, exist_ok=True)
    output_h.write_text(
        "/* Generated from the composed syntax, scalar-class, and projection GSLTs. */\n"
        f"#ifndef {guard}\n#define {guard}\n\n"
        '#include "gslt_direct_reader_v1.h"\n\n'
        f"extern const GSLTDirectReaderV1Plan {c_prefix}_plan;\n"
        f"const char *{c_prefix}_program_digest(void);\n\n"
        f"int {c_prefix}_parse_bytes_ids(\n"
        "    const uint8_t *input, size_t input_len,\n"
        "    const GSLTDirectAtomProjectionV1 *projection,\n"
        "    AtomId **out_ids, GSLTDirectReaderV1Receipt *receipt,\n"
        "    char *error_buf, size_t error_buf_size);\n\n"
        f"#endif /* {guard} */\n",
        encoding="utf-8",
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--syntax", required=True, type=Path)
    parser.add_argument("--classes", required=True, type=Path)
    parser.add_argument("--projection", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--c-prefix", required=True)
    parser.add_argument("--output-c", required=True, type=Path)
    parser.add_argument("--output-h", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        generate(
            arguments.syntax,
            arguments.classes,
            arguments.projection,
            arguments.profile,
            arguments.c_prefix,
            arguments.output_c,
            arguments.output_h,
        )
    except (CompileError, OSError, ValueError) as error:
        print(f"GSLTDirectReaderCompileError: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
