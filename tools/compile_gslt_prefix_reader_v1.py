#!/usr/bin/env python3
"""Compile the deterministic prefix S-expression GSLT fragment to C data."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import re
import sys
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compile_gslt_direct_reader_v1 import (  # noqa: E402
    Boundary,
    CompileError,
    NormalizedSyntaxIR,
    SExpr,
    ScalarClass,
    Symbol,
    c_identifier,
    c_u32_array,
    c_u8_array,
    file_digest,
    flatten_sir_binary,
    form,
    normalize_syntax_ir,
    parse_boundary,
    render,
    scalar_classes,
    sir_character,
    sir_class_name,
    sir_form,
    sir_literal,
    sir_reference,
    sir_tagged,
    symbol,
    tagged,
)
from gslt2parse_schema_v1 import Presentation, parse_presentation  # noqa: E402


PREFIX_FRAGMENT_V1 = "deterministic-prefix-sexpr-direct-v1"


@dataclass(frozen=True)
class PrefixProjectionPolicy:
    profile: str
    document_label: str
    expression_label: str
    word_label: str
    string_label: str
    anonymous_variable_label: str
    variable_label: str
    escape_label: str
    escape_map: tuple[tuple[int, int], ...]
    identity_escape: bool
    prefix_roles: dict[str, tuple[str, str]]


@dataclass(frozen=True)
class TokenRule:
    literal: tuple[int, ...]
    first: str | None
    tail: str | None
    boundary: Boundary
    projection: str
    strip_literal: bool


@dataclass(frozen=True)
class PrefixRule:
    literal: tuple[int, ...]
    payload_start: str | None
    role: str
    skip_before_payload: bool


@dataclass(frozen=True)
class PrefixPlan:
    presentation_name: str
    whitespace: str
    comment_marker: int
    comment_body: str
    comment_boundary: Boundary
    expression_open: int
    expression_close: int
    string_open: int
    string_close: int
    string_plain: str
    string_escape_prefix: tuple[int, ...]
    string_escape_source: str
    string_escape_map: tuple[tuple[int, int], ...]
    string_escape_identity: bool
    prefix_rules: tuple[PrefixRule, ...]
    token_rules: tuple[TokenRule, ...]
    used_classes: tuple[str, ...]


def scalar_classes_disjoint(left: ScalarClass, right: ScalarClass) -> bool:
    """Decide disjointness over Unicode scalar values, not raw integers."""
    if not left.complement:
        return all(not right.contains(value) for value in left.points)
    if not right.complement:
        return all(not left.contains(value) for value in right.points)
    excluded = {
        value
        for value in (*left.points, *right.points)
        if 0 <= value <= 0x10FFFF and not 0xD800 <= value <= 0xDFFF
    }
    return len(excluded) == 0x110000 - 0x800


def boundary_contains(
    boundary: Boundary, value: int, classes: dict[str, ScalarClass]
) -> bool:
    return value in boundary.literals or any(
        classes[name].contains(value) for name in boundary.classes
    )


def boundary_disjoint_class(
    boundary: Boundary,
    scalar_class: ScalarClass,
    classes: dict[str, ScalarClass],
) -> bool:
    return all(not scalar_class.contains(value) for value in boundary.literals) and all(
        scalar_classes_disjoint(classes[name], scalar_class)
        for name in boundary.classes
    )


def common_literal_prefix(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    length = 0
    while length < min(len(left), len(right)) and left[length] == right[length]:
        length += 1
    return length


def token_rules_disjoint(
    left: TokenRule,
    right: TokenRule,
    classes: dict[str, ScalarClass],
) -> bool:
    if not left.literal:
        assert left.first is not None
        if not right.literal:
            assert right.first is not None
            return scalar_classes_disjoint(classes[left.first], classes[right.first])
        return not classes[left.first].contains(right.literal[0])
    if not right.literal:
        return token_rules_disjoint(right, left, classes)

    common = common_literal_prefix(left.literal, right.literal)
    if common < min(len(left.literal), len(right.literal)):
        return True
    if len(left.literal) == len(right.literal):
        if left.first is None and right.first is None:
            return False
        if left.first is not None and right.first is not None:
            return scalar_classes_disjoint(classes[left.first], classes[right.first])
        bare, extended = (left, right) if left.first is None else (right, left)
        assert extended.first is not None
        return boundary_disjoint_class(
            bare.boundary, classes[extended.first], classes
        )

    shorter, longer = (
        (left, right)
        if len(left.literal) < len(right.literal)
        else (right, left)
    )
    next_value = longer.literal[len(shorter.literal)]
    if shorter.first is not None:
        return not classes[shorter.first].contains(next_value)
    return not boundary_contains(shorter.boundary, next_value, classes)


def token_prefix_disjoint(
    token: TokenRule,
    prefix: PrefixRule,
    classes: dict[str, ScalarClass],
) -> bool:
    if not token.literal:
        assert token.first is not None
        return not classes[token.first].contains(prefix.literal[0])

    common = common_literal_prefix(token.literal, prefix.literal)
    if common < min(len(token.literal), len(prefix.literal)):
        return True
    if len(token.literal) < len(prefix.literal):
        next_value = prefix.literal[len(token.literal)]
        if token.first is not None:
            return not classes[token.first].contains(next_value)
        return not boundary_contains(token.boundary, next_value, classes)
    if len(prefix.literal) < len(token.literal):
        next_value = token.literal[len(prefix.literal)]
        if prefix.skip_before_payload:
            return False
        assert prefix.payload_start is not None
        return not classes[prefix.payload_start].contains(next_value)

    if prefix.skip_before_payload:
        if token.first is not None:
            return False
        return not token.boundary.literals and not token.boundary.classes
    assert prefix.payload_start is not None
    if token.first is not None:
        return scalar_classes_disjoint(
            classes[token.first], classes[prefix.payload_start]
        )
    return boundary_disjoint_class(
        token.boundary, classes[prefix.payload_start], classes
    )


def validate_dispatch_partition(
    plan: PrefixPlan, classes: dict[str, ScalarClass]
) -> None:
    structural = {
        plan.comment_marker,
        plan.expression_open,
        plan.expression_close,
        plan.string_open,
        plan.string_close,
    }
    for rule in plan.prefix_rules:
        if not rule.literal or rule.literal[0] in structural:
            raise CompileError("prefix dispatch overlaps a structural scalar")
        if (rule.payload_start is None) == (not rule.skip_before_payload):
            raise CompileError(
                "prefix rule must choose exactly one payload-boundary policy"
            )
    for index, rule in enumerate(plan.token_rules):
        if rule.literal:
            if rule.literal[0] in structural:
                raise CompileError("token dispatch overlaps a structural scalar")
        else:
            if rule.first is None:
                raise CompileError("generic token rule has no first-scalar class")
            if any(classes[rule.first].contains(value) for value in structural):
                raise CompileError("generic token dispatch overlaps a structural scalar")
        for previous in plan.token_rules[:index]:
            if not token_rules_disjoint(previous, rule, classes):
                raise CompileError(
                    "token alternatives have overlapping dispatch: "
                    f"{previous!r} and {rule!r}"
                )
        for prefix in plan.prefix_rules:
            if not token_prefix_disjoint(rule, prefix, classes):
                raise CompileError(
                    "token dispatch overlaps a prefix alternative: "
                    f"{rule!r} and {prefix!r}"
                )


def projection_policy(
    presentation: Presentation, profile_name: str
) -> PrefixProjectionPolicy:
    document_label: str | None = None
    expression_label: str | None = None
    atom_rules: dict[str, tuple[str, str]] = {}
    variable_rules: dict[str, tuple[str, str]] = {}
    maps: dict[str, tuple[str, tuple[tuple[int, int], ...]]] = {}
    prefix_rules: dict[str, tuple[str, str]] = {}
    for rule in presentation.rules:
        if rule.body or not isinstance(rule.head, tuple) or not rule.head:
            continue
        tag = symbol(rule.head[0], f"{presentation.source}:{rule.name}: tag")
        if tag == "sexpr-atom-profile":
            value = form(rule.head, tag, 8, f"{presentation.source}:{rule.name}")
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
            entries: list[tuple[int, int]] = []
            current = value[4]
            while tagged(current, "sexpr-projection-cons"):
                cons = form(current, "sexpr-projection-cons", 2, "projection map")
                mapping = form(
                    cons[1], "sexpr-codepoint-map", 2, "projection map"
                )
                if not isinstance(mapping[1], int) or not isinstance(mapping[2], int):
                    raise CompileError("escape projection map must contain integers")
                entries.append((mapping[1], mapping[2]))
                current = cons[2]
            if not isinstance(current, Symbol) or current.text != "sexpr-projection-nil":
                raise CompileError("malformed escape projection map")
            maps[symbol(value[2], "map wrapper")] = (
                symbol(value[3], "map failure mode"), tuple(entries)
            )
        elif tag == "sexpr-prefix-rule":
            value = form(rule.head, tag, 4, f"{presentation.source}:{rule.name}")
            if symbol(value[1], "projection profile") == profile_name:
                prefix_rules[symbol(value[2], "prefix wrapper")] = (
                    symbol(value[3], "prefix role"),
                    symbol(value[4], "prefix mode"),
                )
    if document_label is None or expression_label is None:
        raise CompileError(f"missing projection profile {profile_name}")

    def unique(mapping: dict[str, tuple[str, str]], expected: tuple[str, str], label: str) -> str:
        values = [name for name, value in mapping.items() if value == expected]
        if len(values) != 1:
            raise CompileError(f"projection needs exactly one {label}; found {values}")
        return values[0]

    word = unique(atom_rules, ("symbol", "host-token"), "host token")
    string = unique(atom_rules, ("string", "decoded"), "decoded string")
    anonymous_variable = unique(
        variable_rules,
        ("empty", "fresh-anonymous"),
        "fresh anonymous variable",
    )
    variable = unique(variable_rules, ("raw", "no-anonymous"), "variable")
    escapes = [name for name, value in maps.items() if value[0] == "identity"]
    if len(escapes) != 1:
        raise CompileError(f"projection needs one identity escape; found {escapes}")
    required_prefixes = {
        ("quote", "open"),
        ("unquote", "open"),
        ("named-variable", "closed"),
        ("resolve-name", "closed"),
    }
    if set(prefix_rules.values()) != required_prefixes or len(prefix_rules) != 4:
        raise CompileError("projection needs the four distinct prefix roles")
    return PrefixProjectionPolicy(
        profile_name,
        document_label,
        expression_label,
        word,
        string,
        anonymous_variable,
        variable,
        escapes[0],
        maps[escapes[0]][1],
        True,
        prefix_rules,
    )


def node_definition(
    ir: NormalizedSyntaxIR, label: str
) -> tuple[str, SExpr]:
    matches: list[tuple[str, SExpr]] = []
    for name, grammar in ir.definitions.items():
        if not sir_tagged(grammar, "node"):
            continue
        node = sir_form(grammar, "node", 2, f"definition {name}")
        if symbol(node[1], f"definition {name}: label") == label:
            matches.append((name, node[2]))
    if len(matches) != 1:
        raise CompileError(f"expected one syntax node {label}; found {len(matches)}")
    return matches[0]


def peek_boundary(term: SExpr, defs: dict[str, SExpr], where: str) -> Boundary:
    peek = sir_form(term, "peek", 1, where)
    return parse_boundary(defs[sir_reference(peek[1], where)], where)


def parse_token_branch(
    term: SExpr, defs: dict[str, SExpr], where: str
) -> TokenRule:
    left = sir_form(term, "left", 2, where)
    boundary = peek_boundary(left[2], defs, f"{where}: boundary")
    body = left[1]
    if sir_tagged(body, "char") or sir_tagged(body, "lit"):
        return TokenRule(
            sir_literal(body, f"{where}: literal"),
            None,
            None,
            boundary,
            "word",
            False,
        )
    sequence = sir_form(body, "seq", 2, where)
    if sir_tagged(sequence[1], "char") or sir_tagged(sequence[1], "lit"):
        literal = sir_literal(sequence[1], f"{where}: literal")
        remainder = sir_form(sequence[2], "seq", 2, f"{where}: tail")
        first = sir_class_name(remainder[1], f"{where}: first")
        star = sir_form(remainder[2], "star", 1, f"{where}: tail")
    else:
        literal = ()
        first = sir_class_name(sequence[1], f"{where}: first")
        star = sir_form(sequence[2], "star", 1, f"{where}: tail")
    tail = sir_class_name(star[1], f"{where}: tail")
    return TokenRule(literal, first, tail, boundary, "word", False)


def derive_plan(
    syntax: Presentation,
    classes: dict[str, ScalarClass],
    policy: PrefixProjectionPolicy,
) -> PrefixPlan:
    ir = normalize_syntax_ir(syntax)
    defs = ir.definitions
    document_name, document_body = node_definition(ir, policy.document_label)
    expression_name, expression_body = node_definition(ir, policy.expression_label)
    word_name, word_body = node_definition(ir, policy.word_label)
    string_name, string_body = node_definition(ir, policy.string_label)
    anonymous_variable_name, anonymous_variable_body = node_definition(
        ir, policy.anonymous_variable_label
    )
    variable_name, variable_body = node_definition(ir, policy.variable_label)
    escape_name, escape_body = node_definition(ir, policy.escape_label)
    prefix_nodes = {
        wrapper: node_definition(ir, wrapper)
        for wrapper in policy.prefix_roles
    }

    document = sir_form(document_body, "left", 2, "document")
    document_start = sir_form(document[1], "right", 2, "document")
    skip_name = sir_reference(document_start[1], "document skip")
    repetition = sir_form(document_start[2], "star", 1, "document")
    item = sir_form(repetition[1], "left", 2, "document item")
    top_name = sir_reference(item[1], "top atom")
    if sir_reference(item[2], "document trailing skip") != skip_name:
        raise CompileError("document uses inconsistent skip definitions")
    if not isinstance(document[2], Symbol) or document[2].text != "sir-eof":
        raise CompileError("document must end at eof")
    atom_name = sir_reference(defs[top_name], "top atom")

    skip = sir_form(defs[skip_name], "star", 1, "skip")
    layout_name = sir_reference(skip[1], "skip layout")
    layout = flatten_sir_binary(defs[layout_name], "alt")
    whitespace_branches = [x for x in layout if sir_tagged(x, "class")]
    comment_branches = [x for x in layout if sir_tagged(x, "ref")]
    if len(layout) != 2 or len(whitespace_branches) != 1 or len(comment_branches) != 1:
        raise CompileError("skip must contain one whitespace class and one comment")
    whitespace = sir_class_name(whitespace_branches[0], "whitespace")
    comment_name = sir_reference(comment_branches[0], "comment")
    comment_defs = [
        sir_reference(x, "comment alternative")
        for x in flatten_sir_binary(defs[comment_name], "alt")
    ]
    if len(comment_defs) != 2:
        raise CompileError("comment must have newline and eof alternatives")
    comment_marker: int | None = None
    comment_body: str | None = None
    comment_boundary_eof = False
    comment_boundary_classes: set[str] = set()
    for name in comment_defs:
        node = sir_form(defs[name], "node", 2, f"comment {name}")
        if symbol(node[1], f"comment {name}: label") != "comment":
            raise CompileError("comment alternative has the wrong node label")
        right = sir_form(node[2], "right", 2, f"comment {name}")
        marker = sir_character(right[1], f"comment {name}: marker")
        until = sir_form(right[2], "until", 2, f"comment {name}")
        body = sir_class_name(until[2], f"comment {name}: body")
        if comment_marker is None:
            comment_marker, comment_body = marker, body
        elif comment_marker != marker or comment_body != body:
            raise CompileError("comment alternatives disagree")
        if isinstance(until[1], Symbol) and until[1].text == "sir-eof":
            comment_boundary_eof = True
        elif sir_tagged(until[1], "class"):
            comment_boundary_classes.add(
                sir_class_name(until[1], f"comment {name}: boundary")
            )
        else:
            raise CompileError("unsupported comment boundary")
    if comment_marker is None or comment_body is None or not comment_boundary_eof:
        raise CompileError("comment alternatives do not cover eof")
    comment_boundary = Boundary(
        True, (), tuple(sorted(comment_boundary_classes))
    )

    expression = sir_form(expression_body, "right", 2, "expression")
    expression_open = sir_character(expression[1], "expression open")
    expression_tail = sir_form(expression[2], "left", 2, "expression")
    expression_items = sir_form(expression_tail[1], "right", 2, "expression")
    if sir_reference(expression_items[1], "expression skip") != skip_name:
        raise CompileError("expression uses a different skip definition")
    expression_star = sir_form(expression_items[2], "star", 1, "expression")
    expression_item = sir_form(expression_star[1], "left", 2, "expression item")
    if sir_reference(expression_item[1], "expression atom") != atom_name or \
       sir_reference(expression_item[2], "expression skip") != skip_name:
        raise CompileError("expression recursion disagrees with document syntax")
    expression_close = sir_character(expression_tail[2], "expression close")

    atom_refs = {
        sir_reference(branch, "atom alternative")
        for branch in flatten_sir_binary(defs[atom_name], "alt")
    }
    expected_refs = {
        word_name,
        string_name,
        anonymous_variable_name,
        variable_name,
        expression_name,
        *(name for name, _ in prefix_nodes.values()),
    }
    if atom_refs != expected_refs:
        raise CompileError("atom alternatives differ from projected syntax nodes")

    word_rules: list[TokenRule] = []
    for branch in flatten_sir_binary(word_body, "alt"):
        name = sir_reference(branch, "word alternative")
        rule = parse_token_branch(defs[name], defs, f"word alternative {name}")
        word_rules.append(rule)
    if len(word_rules) != 4:
        raise CompileError("prefix fragment requires four word alternatives")
    ordinary_boundaries = {
        rule.boundary for rule in word_rules if rule.first is not None
    }
    if len(ordinary_boundaries) != 1:
        raise CompileError("extended word alternatives disagree on their boundary")
    token_boundary = next(iter(ordinary_boundaries))

    anonymous_variable = parse_token_branch(
        anonymous_variable_body, defs, "anonymous variable"
    )
    if anonymous_variable.first is not None or not anonymous_variable.literal:
        raise CompileError("anonymous variable must be one literal token")
    anonymous_variable = TokenRule(
        anonymous_variable.literal,
        None,
        None,
        anonymous_variable.boundary,
        "anonymous-variable",
        False,
    )
    if anonymous_variable.boundary != token_boundary:
        raise CompileError("anonymous variable and word boundaries differ")

    variable = sir_form(variable_body, "left", 2, "variable")
    variable_right = sir_form(variable[1], "right", 2, "variable")
    variable_literal = sir_literal(variable_right[1], "variable marker")
    variable_sequence = sir_form(variable_right[2], "seq", 2, "variable")
    variable_first = sir_class_name(variable_sequence[1], "variable first")
    variable_star = sir_form(variable_sequence[2], "star", 1, "variable tail")
    variable_tail = sir_class_name(variable_star[1], "variable tail")
    variable_boundary = peek_boundary(variable[2], defs, "variable boundary")
    if variable_boundary != token_boundary:
        raise CompileError("variable and word boundaries differ")
    token_rules = tuple(word_rules) + (
        anonymous_variable,
        TokenRule(
            variable_literal,
            variable_first,
            variable_tail,
            variable_boundary,
            "variable",
            True,
        ),
    )

    string = sir_form(string_body, "right", 2, "string")
    string_open = sir_character(string[1], "string open")
    string_tail = sir_form(string[2], "left", 2, "string")
    string_star = sir_form(string_tail[1], "star", 1, "string body")
    string_unit_name = sir_reference(string_star[1], "string unit")
    string_close = sir_character(string_tail[2], "string close")
    units = flatten_sir_binary(defs[string_unit_name], "alt")
    unit_refs = [sir_reference(x, "string escape") for x in units if sir_tagged(x, "ref")]
    unit_classes = [sir_class_name(x, "string plain") for x in units if sir_tagged(x, "class")]
    if unit_refs != [escape_name] or len(unit_classes) != 1 or len(units) != 2:
        raise CompileError("string body differs from projected escape/plain nodes")
    string_plain = unit_classes[0]
    escape = sir_form(escape_body, "seq", 2, "string escape")
    escape_prefix = sir_literal(escape[1], "string escape prefix")
    escape_source = sir_class_name(escape[2], "string escape source")

    prefix_rules: list[PrefixRule] = []
    for wrapper, (role, mode) in policy.prefix_roles.items():
        _, body = prefix_nodes[wrapper]
        outer = sir_form(body, "right", 2, f"prefix {wrapper}")
        literal = sir_literal(outer[1], f"prefix {wrapper}: literal")
        continuation = sir_form(outer[2], "right", 2, f"prefix {wrapper}")
        if mode == "open" and role == "unquote":
            peek = sir_form(continuation[1], "peek", 1, f"prefix {wrapper}")
            payload_start = sir_class_name(peek[1], f"prefix {wrapper}: payload")
            skip_before_payload = False
        else:
            if sir_reference(continuation[1], f"prefix {wrapper}: skip") != skip_name:
                raise CompileError(f"prefix {wrapper} uses an unexpected separator")
            payload_start = None
            skip_before_payload = True
        if sir_reference(continuation[2], f"prefix {wrapper}: atom") != atom_name:
            raise CompileError(f"prefix {wrapper} targets a different atom relation")
        prefix_rules.append(
            PrefixRule(literal, payload_start, role, skip_before_payload)
        )

    used = {
        whitespace,
        comment_body,
        string_plain,
        escape_source,
        *comment_boundary.classes,
    }
    for rule in token_rules:
        if rule.first:
            used.add(rule.first)
        if rule.tail:
            used.add(rule.tail)
        used.update(rule.boundary.classes)
    for rule in prefix_rules:
        if rule.payload_start:
            used.add(rule.payload_start)
    missing = sorted(used - set(classes))
    if missing:
        raise CompileError("undefined scalar classes: " + ", ".join(missing))
    if len({rule.literal[0] for rule in prefix_rules}) != len(prefix_rules):
        raise CompileError("prefix rules do not have disjoint leading scalars")
    if classes[string_plain].contains(string_close) or \
       classes[string_plain].contains(escape_prefix[0]):
        raise CompileError("string plain class overlaps close/escape dispatch")
    map_sources = {source for source, _ in policy.escape_map}
    if not all(classes[escape_source].contains(source) for source in map_sources):
        raise CompileError("escape projection maps a source excluded by syntax")
    plan = PrefixPlan(
        ir.presentation_name,
        whitespace,
        comment_marker,
        comment_body,
        comment_boundary,
        expression_open,
        expression_close,
        string_open,
        string_close,
        string_plain,
        escape_prefix,
        escape_source,
        policy.escape_map,
        policy.identity_escape,
        tuple(prefix_rules),
        token_rules,
        tuple(sorted(used)),
    )
    validate_dispatch_partition(plan, classes)
    return plan


def composition_digest(paths: Iterable[Path], profile: str) -> str:
    digest = sha256()
    digest.update(b"GSLTDirectPrefixReaderV1\0")
    digest.update(profile.encode("utf-8"))
    digest.update(b"\0")
    digest.update(PREFIX_FRAGMENT_V1.encode("utf-8"))
    digest.update(b"\0")
    for path in paths:
        payload = path.read_bytes()
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
    return digest.hexdigest()


def emit_boundary(
    lines: list[str], prefix: str, boundary: Boundary,
    class_ids: dict[str, str]
) -> str:
    literal_name = f"{prefix}_literals"
    classes_name = f"{prefix}_classes"
    if boundary.literals:
        lines.append(c_u32_array(literal_name, boundary.literals))
    if boundary.classes:
        lines.append(
            f"static const GSLTDirectScalarClassV1 *const {classes_name}[] = {{\n"
        )
        for name in boundary.classes:
            lines.append(f"    &{class_ids[name]},\n")
        lines.append("};\n")
    return (
        "{"
        f".allow_eof = {'true' if boundary.allow_eof else 'false'}, "
        f".literals = {literal_name if boundary.literals else 'NULL'}, "
        f".literal_len = UINT32_C({len(boundary.literals)}), "
        f".classes = {classes_name if boundary.classes else 'NULL'}, "
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
    try:
        plan = derive_plan(syntax, classes, policy)
    except CompileError as error:
        raise CompileError(
            f"LanguageDef is outside {PREFIX_FRAGMENT_V1}: {error}"
        ) from error
    digest = composition_digest(
        (syntax_path, classes_path, projection_path), profile
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
        lines.append(
            c_u8_array(
                ascii_name,
                [1 if scalar_class.contains(value) else 0 for value in range(256)],
            )
        )
        lines.append(
            f"static const GSLTDirectScalarClassV1 {identifier} = {{\n"
            f"    .ascii = {ascii_name},\n"
            f"    .points = {points_name if scalar_class.points else 'NULL'},\n"
            f"    .point_len = UINT32_C({len(scalar_class.points)}),\n"
            f"    .complement = {'true' if scalar_class.complement else 'false'},\n"
            "};\n"
        )

    escape_prefix_name = f"{c_prefix}_string_escape_prefix"
    lines.append(c_u32_array(escape_prefix_name, plan.string_escape_prefix))
    escape_map_name = f"{c_prefix}_string_escape_map"
    lines.append(
        f"static const GSLTDirectCodepointMapV1 {escape_map_name}[] = {{\n"
    )
    for source, target in plan.string_escape_map:
        lines.append(f"    {{UINT32_C({source}), UINT32_C({target})}},\n")
    lines.append("};\n")
    comment_boundary = emit_boundary(
        lines, f"{c_prefix}_comment_boundary", plan.comment_boundary, class_ids
    )

    prefix_literal_names: list[str] = []
    for index, rule in enumerate(plan.prefix_rules):
        name = f"{c_prefix}_prefix_{index}_literal"
        lines.append(c_u32_array(name, rule.literal))
        prefix_literal_names.append(name)
    role_names = {
        "quote": "GSLT_DIRECT_PREFIX_ROLE_QUOTE",
        "unquote": "GSLT_DIRECT_PREFIX_ROLE_UNQUOTE",
        "named-variable": "GSLT_DIRECT_PREFIX_ROLE_NAMED_VARIABLE",
        "resolve-name": "GSLT_DIRECT_PREFIX_ROLE_RESOLVE_NAME",
    }
    prefix_rules_name = f"{c_prefix}_prefix_rules"
    lines.append(
        f"static const GSLTDirectPrefixRuleV1 {prefix_rules_name}[] = {{\n"
    )
    for index, rule in enumerate(plan.prefix_rules):
        lines.append(
            "    {"
            f".literal = {prefix_literal_names[index]}, "
            f".literal_len = UINT32_C({len(rule.literal)}), "
            f".payload_start = {'&' + class_ids[rule.payload_start] if rule.payload_start else 'NULL'}, "
            f".role = {role_names[rule.role]}, "
            f".skip_before_payload = {'true' if rule.skip_before_payload else 'false'}"
            "},\n"
        )
    lines.append("};\n")

    token_literal_names: list[str | None] = []
    token_boundaries: list[str] = []
    for index, rule in enumerate(plan.token_rules):
        literal_name: str | None = None
        if rule.literal:
            literal_name = f"{c_prefix}_token_{index}_literal"
            lines.append(c_u32_array(literal_name, rule.literal))
        token_literal_names.append(literal_name)
        token_boundaries.append(
            emit_boundary(
                lines,
                f"{c_prefix}_token_{index}_boundary",
                rule.boundary,
                class_ids,
            )
        )
    token_rules_name = f"{c_prefix}_token_rules"
    lines.append(
        f"static const GSLTDirectTokenRuleV1 {token_rules_name}[] = {{\n"
    )
    for index, rule in enumerate(plan.token_rules):
        projection_name = {
            "word": "GSLT_DIRECT_TOKEN_PROJECTION_WORD",
            "variable": "GSLT_DIRECT_TOKEN_PROJECTION_VARIABLE",
            "anonymous-variable":
                "GSLT_DIRECT_TOKEN_PROJECTION_ANONYMOUS_VARIABLE",
        }[rule.projection]
        lines.append(
            "    {"
            f".literal = {token_literal_names[index] or 'NULL'}, "
            f".literal_len = UINT32_C({len(rule.literal)}), "
            f".first = {'&' + class_ids[rule.first] if rule.first else 'NULL'}, "
            f".tail = {'&' + class_ids[rule.tail] if rule.tail else 'NULL'}, "
            f".boundary = {token_boundaries[index]}, "
            f".projection = {projection_name}, "
            f".strip_literal = {'true' if rule.strip_literal else 'false'}"
            "},\n"
        )
    lines.append("};\n")

    lines.append(
        f"\nconst GSLTDirectPrefixReaderV1Plan {c_prefix}_plan = {{\n"
        f'    .presentation_name = "{plan.presentation_name}",\n'
        f'    .fragment = "{PREFIX_FRAGMENT_V1}",\n'
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
        f"    .string_plain = &{class_ids[plan.string_plain]},\n"
        f"    .string_escape_prefix = {escape_prefix_name},\n"
        f"    .string_escape_prefix_len = UINT32_C({len(plan.string_escape_prefix)}),\n"
        f"    .string_escape_source = &{class_ids[plan.string_escape_source]},\n"
        f"    .string_escape_map = {escape_map_name},\n"
        f"    .string_escape_map_len = UINT32_C({len(plan.string_escape_map)}),\n"
        f"    .string_escape_identity_fallback = {'true' if plan.string_escape_identity else 'false'},\n"
        f"    .prefix_rules = {prefix_rules_name},\n"
        f"    .prefix_rule_len = UINT32_C({len(plan.prefix_rules)}),\n"
        f"    .token_rules = {token_rules_name},\n"
        f"    .token_rule_len = UINT32_C({len(plan.token_rules)}),\n"
        "    .depth_limit = UINT32_C(4096),\n"
        "};\n\n"
        f"const char *{c_prefix}_program_digest(void) {{\n"
        f"    return {c_prefix}_plan.composition_digest;\n"
        "}\n\n"
        f"int {c_prefix}_parse_bytes_ids(\n"
        "    const uint8_t *input, size_t input_len,\n"
        "    const GSLTDirectPrefixProjectionV1 *projection,\n"
        "    AtomId **out_ids, GSLTDirectPrefixReaderV1Receipt *receipt,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "    return gslt_direct_prefix_reader_v1_parse_bytes_ids(\n"
        f"        &{c_prefix}_plan, input, input_len, projection, out_ids,\n"
        "        receipt, error_buf, error_buf_size);\n"
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
        f"extern const GSLTDirectPrefixReaderV1Plan {c_prefix}_plan;\n"
        f"const char *{c_prefix}_program_digest(void);\n\n"
        f"int {c_prefix}_parse_bytes_ids(\n"
        "    const uint8_t *input, size_t input_len,\n"
        "    const GSLTDirectPrefixProjectionV1 *projection,\n"
        "    AtomId **out_ids, GSLTDirectPrefixReaderV1Receipt *receipt,\n"
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
        print(f"GSLTDirectPrefixReaderCompileError: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
