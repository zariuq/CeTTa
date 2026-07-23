#!/usr/bin/env python3
"""Compile PeTTa's composed document/form LanguageDefs to native C data."""

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
    node_body,
    node_definitions,
    normalize_syntax_ir,
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


PETTA_DIRECT_FRAGMENT_V1 = "petta-two-stage-direct-v1"


@dataclass(frozen=True)
class PeTTaProjectionPolicy:
    profile: str
    form_label: str
    expression_label: str
    token_label: str
    string_label: str
    variable_label: str
    quoted_token_label: str
    dollar_label: str
    escape_label: str
    escape_map: tuple[tuple[int, int], ...]
    identity_escape: bool
    runnable_label: str


def projection_policy(
    presentation: Presentation, profile_name: str
) -> PeTTaProjectionPolicy:
    form_label: str | None = None
    expression_label: str | None = None
    atom_rules: dict[str, tuple[str, str]] = {}
    variable_rules: dict[str, tuple[str, str]] = {}
    maps: dict[str, tuple[str, tuple[tuple[int, int], ...]]] = {}
    document_rules: dict[str, str] = {}
    for rule in presentation.rules:
        if rule.body or not isinstance(rule.head, tuple) or not rule.head:
            continue
        tag = symbol(rule.head[0], f"{presentation.source}:{rule.name}: tag")
        if tag == "sexpr-atom-profile":
            value = form(
                rule.head, tag, 8, f"{presentation.source}:{rule.name}"
            )
            if symbol(value[1], "projection profile") != profile_name:
                continue
            if form_label is not None:
                raise CompileError(f"duplicate projection profile {profile_name}")
            form_label = symbol(value[7], "form label")
            expression_label = symbol(value[8], "expression label")
        elif tag == "sexpr-atom-rule":
            value = form(
                rule.head, tag, 4, f"{presentation.source}:{rule.name}"
            )
            if symbol(value[1], "projection profile") == profile_name:
                atom_rules[symbol(value[2], "atom wrapper")] = (
                    symbol(value[3], "atom kind"),
                    symbol(value[4], "atom mode"),
                )
        elif tag == "sexpr-variable-rule":
            value = form(
                rule.head, tag, 4, f"{presentation.source}:{rule.name}"
            )
            if symbol(value[1], "projection profile") == profile_name:
                variable_rules[symbol(value[2], "variable wrapper")] = (
                    symbol(value[3], "variable mode"),
                    symbol(value[4], "anonymous mode"),
                )
        elif tag == "sexpr-wrapper-map":
            value = form(
                rule.head, tag, 4, f"{presentation.source}:{rule.name}"
            )
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
                    raise CompileError("projection map must contain integers")
                entries.append((mapping[1], mapping[2]))
                current = cons[2]
            if not isinstance(current, Symbol) or current.text != "sexpr-projection-nil":
                raise CompileError("malformed projection map")
            maps[symbol(value[2], "map wrapper")] = (
                symbol(value[3], "map failure mode"), tuple(entries)
            )
        elif tag == "sexpr-document-rule":
            value = form(
                rule.head, tag, 3, f"{presentation.source}:{rule.name}"
            )
            if symbol(value[1], "projection profile") == profile_name:
                document_rules[symbol(value[2], "document wrapper")] = symbol(
                    value[3], "document mode"
                )

    if form_label is None or expression_label is None:
        raise CompileError(f"missing projection profile {profile_name}")

    def unique_atom(kind: str, mode: str, description: str) -> str:
        values = [
            name for name, value in atom_rules.items()
            if value == (kind, mode)
        ]
        if len(values) != 1:
            raise CompileError(
                f"projection needs exactly one {description}; found {values}"
            )
        return values[0]

    variables = [
        name for name, value in variable_rules.items()
        if value == ("raw", "underscore-fresh")
    ]
    escapes = [
        name for name, value in maps.items()
        if value[0] == "identity"
    ]
    runnables = [
        name for name, mode in document_rules.items()
        if mode == "bang-prefix"
    ]
    if len(variables) != 1 or len(escapes) != 1 or len(runnables) != 1:
        raise CompileError(
            "projection needs one underscore-fresh variable, identity escape, "
            "and bang-prefix runnable"
        )
    if document_rules.get(form_label) != "value":
        raise CompileError("ordinary PeTTa forms must project as values")
    return PeTTaProjectionPolicy(
        profile_name,
        form_label,
        expression_label,
        unique_atom("symbol", "swi-number-bool", "PeTTa token"),
        unique_atom("string", "decoded", "PeTTa string"),
        variables[0],
        unique_atom("string", "retain-backslashes", "quoted token"),
        unique_atom("symbol", "raw", "dollar symbol"),
        escapes[0],
        maps[escapes[0]][1],
        True,
        runnables[0],
    )


@dataclass(frozen=True)
class PeTTaDirectPlan:
    presentation_name: str
    splitter_blank: str
    splitter_nonquote: str
    splitter_comment_body: str
    splitter_ordinary: str
    form_blank: str
    token_boundary: str
    token: str
    token_first: str
    string_plain: str
    quoted_token_plain: str
    comment_marker: int
    comment_line_end: int
    expression_open: int
    expression_close: int
    string_quote: int
    escape_marker: int
    variable_marker: int
    runnable_marker: int
    escape_map: tuple[tuple[int, int], ...]
    identity_escape: bool
    used_classes: tuple[str, ...]


def only(items: Sequence[SExpr], where: str) -> SExpr:
    if len(items) != 1:
        raise CompileError(f"{where}: expected one item, got {len(items)}")
    return items[0]


def ref_set(term: SExpr, where: str) -> set[str]:
    return {
        sir_reference(item, where)
        for item in flatten_sir_binary(term, "alt")
        if sir_tagged(item, "ref")
    }


def derive_splitter(
    ir: NormalizedSyntaxIR, policy: PeTTaProjectionPolicy
) -> dict[str, str | int]:
    defs = ir.definitions
    nodes = node_definitions(defs)
    _, document_body = node_body(nodes, "document")
    _, balanced_body = node_body(nodes, "balanced")
    _, quoted_body = node_body(nodes, "naive-quoted")

    document = sir_form(document_body, "right", 2, "PeTTa document")
    skip_name = sir_reference(document[1], "PeTTa document skip")
    document_left = sir_form(document[2], "left", 2, "PeTTa document")
    repetitions = sir_form(document_left[1], "star", 1, "PeTTa document")
    repeated = sir_form(repetitions[1], "left", 2, "PeTTa document item")
    top_form_name = sir_reference(repeated[1], "PeTTa top form")
    if sir_reference(repeated[2], "PeTTa trailing skip") != skip_name:
        raise CompileError("PeTTa document uses inconsistent skip definitions")
    if not isinstance(document_left[2], Symbol) or document_left[2].text != "sir-eof":
        raise CompileError("PeTTa document must consume eof")

    top_nodes: dict[str, SExpr] = {}
    for branch in flatten_sir_binary(defs[top_form_name], "alt"):
        node = sir_form(branch, "node", 2, "PeTTa top-form branch")
        top_nodes[symbol(node[1], "PeTTa top-form label")] = node[2]
    if set(top_nodes) != {policy.form_label, policy.runnable_label}:
        raise CompileError("PeTTa document form/runnable labels disagree with projection")
    form_ref = sir_reference(top_nodes[policy.form_label], "ordinary PeTTa form")
    runnable = sir_form(
        top_nodes[policy.runnable_label], "right", 2, "runnable PeTTa form"
    )
    runnable_marker = sir_character(runnable[1], "PeTTa runnable marker")
    runnable_ref = sir_reference(runnable[2], "runnable PeTTa form")
    if form_ref != runnable_ref:
        raise CompileError("ordinary and runnable PeTTa forms use different balancing")
    balanced_name = form_ref

    skip = sir_form(defs[skip_name], "star", 1, "PeTTa top skip")
    skip_unit_name = sir_reference(skip[1], "PeTTa top skip unit")
    skip_branches = flatten_sir_binary(defs[skip_unit_name], "alt")
    blank_branches = [x for x in skip_branches if sir_tagged(x, "class")]
    comment_branches = [x for x in skip_branches if sir_tagged(x, "right")]
    blank = sir_class_name(only(blank_branches, "PeTTa splitter blank"), "blank")
    comment_right = sir_form(
        only(comment_branches, "PeTTa splitter comment"),
        "right", 2, "PeTTa splitter comment"
    )
    comment_name = sir_reference(comment_right[1], "PeTTa splitter comment")
    sir_form(comment_right[2], "eps", 1, "PeTTa stripped comment")

    comment_refs = ref_set(defs[comment_name], "PeTTa comment branch")
    if len(comment_refs) != 2:
        raise CompileError("PeTTa comment needs line and eof branches")
    line_data: tuple[int, str, int] | None = None
    eof_data: tuple[int, str] | None = None
    line_name: str | None = None
    for name in comment_refs:
        grammar = sir_form(defs[name], "right", 2, f"PeTTa comment {name}")
        marker = sir_character(grammar[1], f"PeTTa comment {name}")
        tail = sir_form(grammar[2], "left", 2, f"PeTTa comment {name}")
        body_star = sir_form(tail[1], "star", 1, f"PeTTa comment {name}")
        body = sir_class_name(body_star[1], f"PeTTa comment {name}")
        if isinstance(tail[2], Symbol) and tail[2].text == "sir-eof":
            eof_data = (marker, body)
        else:
            line_data = (marker, body, sir_character(tail[2], "comment LF"))
            line_name = name
    if line_data is None or eof_data is None or line_data[:2] != eof_data:
        raise CompileError("PeTTa line/eof comments disagree")

    balanced = sir_form(balanced_body, "seq", 2, "PeTTa balanced form")
    expression_open = sir_character(balanced[1], "PeTTa expression open")
    balanced_tail = sir_form(balanced[2], "seq", 2, "PeTTa balanced form")
    balanced_star = sir_form(balanced_tail[1], "star", 1, "balanced items")
    item_name = sir_reference(balanced_star[1], "balanced item")
    expression_close = sir_character(balanced_tail[2], "PeTTa expression close")
    if balanced_name not in defs:
        raise CompileError("PeTTa balanced definition is absent")

    item_branches = flatten_sir_binary(defs[item_name], "alt")
    item_refs = {
        sir_reference(x, "balanced item") for x in item_branches
        if sir_tagged(x, "ref")
    }
    item_classes = [
        sir_class_name(x, "balanced ordinary") for x in item_branches
        if sir_tagged(x, "class")
    ]
    stripped_comments = [x for x in item_branches if sir_tagged(x, "right")]
    if len(item_classes) != 1 or len(stripped_comments) != 1:
        raise CompileError("PeTTa balanced-item alternatives are malformed")
    stripped = sir_form(stripped_comments[0], "right", 2, "in-form comment")
    if sir_reference(stripped[1], "in-form comment") != line_name:
        raise CompileError("PeTTa balanced forms must use LF-terminated comments")
    sir_form(stripped[2], "eps", 1, "stripped in-form comment")
    quoted_names = [name for name in item_refs if name != balanced_name]
    if len(quoted_names) != 1 or balanced_name not in item_refs:
        raise CompileError("PeTTa balanced recursion/quoted branches are malformed")

    quoted = sir_form(quoted_body, "seq", 2, "PeTTa naive quote")
    string_quote = sir_character(quoted[1], "PeTTa naive quote")
    quoted_tail = sir_form(quoted[2], "seq", 2, "PeTTa naive quote")
    quoted_star = sir_form(quoted_tail[1], "star", 1, "PeTTa naive quote")
    nonquote = sir_class_name(quoted_star[1], "PeTTa nonquote")
    if sir_character(quoted_tail[2], "PeTTa naive quote") != string_quote:
        raise CompileError("PeTTa naive quote delimiters differ")

    return {
        "blank": blank,
        "nonquote": nonquote,
        "comment_body": line_data[1],
        "ordinary": item_classes[0],
        "comment_marker": line_data[0],
        "comment_line_end": line_data[2],
        "expression_open": expression_open,
        "expression_close": expression_close,
        "string_quote": string_quote,
        "runnable_marker": runnable_marker,
    }


def derive_form(
    ir: NormalizedSyntaxIR, policy: PeTTaProjectionPolicy
) -> dict[str, str | int]:
    defs = ir.definitions
    nodes = node_definitions(defs)
    _, form_body = node_body(nodes, policy.form_label)
    expression_name, expression_body = node_body(nodes, policy.expression_label)
    string_name, string_body = node_body(nodes, policy.string_label)
    escape_name, escape_body = node_body(nodes, policy.escape_label)

    form_right = sir_form(form_body, "right", 2, "PeTTa form")
    skip_name = sir_reference(form_right[1], "PeTTa form skip")
    form_left = sir_form(form_right[2], "left", 2, "PeTTa form")
    atom_name = sir_reference(form_left[1], "PeTTa form atom")
    trailing = sir_form(form_left[2], "right", 2, "PeTTa form trailing skip")
    if sir_reference(trailing[1], "PeTTa trailing skip") != skip_name or not (
        isinstance(trailing[2], Symbol) and trailing[2].text == "sir-eof"
    ):
        raise CompileError("PeTTa form must use one skip and consume eof")
    skip = sir_form(defs[skip_name], "star", 1, "PeTTa form skip")
    form_blank = sir_class_name(skip[1], "PeTTa form blank")

    atom_refs = ref_set(defs[atom_name], "PeTTa atom")
    token_like_names = atom_refs - {string_name, expression_name}
    if len(token_like_names) != 1 or len(atom_refs) != 3:
        raise CompileError("PeTTa atom alternatives are malformed")
    token_like_name = next(iter(token_like_names))

    expression = sir_form(expression_body, "right", 2, "PeTTa expression")
    expression_open = sir_character(expression[1], "PeTTa expression open")
    expression_left = sir_form(expression[2], "left", 2, "PeTTa expression")
    expression_start = sir_form(expression_left[1], "right", 2, "PeTTa expression")
    if sir_reference(expression_start[1], "expression skip") != skip_name:
        raise CompileError("PeTTa expression uses a different skip")
    expression_star = sir_form(expression_start[2], "star", 1, "PeTTa expression")
    expression_item = sir_form(expression_star[1], "left", 2, "expression item")
    if sir_reference(expression_item[1], "expression atom") != atom_name or \
            sir_reference(expression_item[2], "expression skip") != skip_name:
        raise CompileError("PeTTa expression repetition is malformed")
    expression_close = sir_character(expression_left[2], "expression close")

    string = sir_form(string_body, "right", 2, "PeTTa string")
    string_quote = sir_character(string[1], "PeTTa string quote")
    string_left = sir_form(string[2], "left", 2, "PeTTa string")
    string_star = sir_form(string_left[1], "star", 1, "PeTTa string")
    string_unit_name = sir_reference(string_star[1], "PeTTa string unit")
    if sir_character(string_left[2], "PeTTa string quote") != string_quote:
        raise CompileError("PeTTa string delimiters differ")
    unit_branches = flatten_sir_binary(defs[string_unit_name], "alt")
    unit_refs = [sir_reference(x, "string unit") for x in unit_branches if sir_tagged(x, "ref")]
    unit_classes = [sir_class_name(x, "string unit") for x in unit_branches if sir_tagged(x, "class")]
    if unit_refs != [escape_name] or len(unit_classes) != 1 or len(unit_branches) != 2:
        raise CompileError("PeTTa string units are malformed")
    string_plain = unit_classes[0]
    escape = sir_form(escape_body, "right", 2, "PeTTa string escape")
    escape_marker = sir_character(escape[1], "PeTTa escape marker")
    if not isinstance(escape[2], Symbol) or escape[2].text != "sir-any":
        raise CompileError("PeTTa escape must accept exactly one arbitrary scalar")

    token_like = sir_form(defs[token_like_name], "left", 2, "PeTTa token-like")
    token_nodes: dict[str, SExpr] = {}
    for branch in flatten_sir_binary(token_like[1], "alt"):
        node = sir_form(branch, "node", 2, "PeTTa token-like branch")
        token_nodes[symbol(node[1], "PeTTa token label")] = node[2]
    expected_labels = {
        policy.token_label, policy.variable_label,
        policy.quoted_token_label, policy.dollar_label,
    }
    if set(token_nodes) != expected_labels:
        raise CompileError("PeTTa token-like labels disagree with projection")
    peek = sir_form(token_like[2], "peek", 1, "PeTTa token boundary")
    boundary_name = sir_reference(peek[1], "PeTTa token boundary")
    boundary_branches = flatten_sir_binary(defs[boundary_name], "alt")
    boundary_classes = [
        sir_class_name(x, "PeTTa token boundary") for x in boundary_branches
        if sir_tagged(x, "class")
    ]
    if len(boundary_classes) != 1 or not any(
        isinstance(x, Symbol) and x.text == "sir-eof"
        for x in boundary_branches
    ):
        raise CompileError("PeTTa token boundary must be one class or eof")

    variable = sir_form(
        token_nodes[policy.variable_label], "right", 2, "PeTTa variable"
    )
    variable_marker = sir_character(variable[1], "PeTTa variable marker")
    variable_plus = sir_form(variable[2], "plus", 1, "PeTTa variable")
    token_class = sir_class_name(variable_plus[1], "PeTTa variable token")
    dollar_marker = sir_character(
        token_nodes[policy.dollar_label], "PeTTa dollar symbol"
    )
    if dollar_marker != variable_marker:
        raise CompileError("PeTTa dollar and variable markers differ")

    token = sir_form(token_nodes[policy.token_label], "seq", 2, "PeTTa token")
    token_first = sir_class_name(token[1], "PeTTa token first")
    token_star = sir_form(token[2], "star", 1, "PeTTa token")
    if sir_class_name(token_star[1], "PeTTa token tail") != token_class:
        raise CompileError("PeTTa token and variable tails differ")

    quoted = sir_form(
        token_nodes[policy.quoted_token_label], "right", 2,
        "PeTTa quoted token"
    )
    if sir_character(quoted[1], "PeTTa quoted token") != string_quote:
        raise CompileError("PeTTa quoted token uses a different quote")
    quoted_seq = sir_form(quoted[2], "seq", 2, "PeTTa quoted token")
    quoted_star = sir_form(quoted_seq[1], "star", 1, "PeTTa quoted token")
    quoted_units = flatten_sir_binary(quoted_star[1], "alt")
    quoted_classes = [
        sir_class_name(x, "quoted-token plain") for x in quoted_units
        if sir_tagged(x, "class")
    ]
    quoted_pairs = [x for x in quoted_units if sir_tagged(x, "seq")]
    if len(quoted_classes) != 1 or len(quoted_pairs) != 1:
        raise CompileError("PeTTa quoted-token units are malformed")
    quoted_pair = sir_form(quoted_pairs[0], "seq", 2, "quoted escape pair")
    if sir_character(quoted_pair[1], "quoted escape marker") != escape_marker or \
            sir_class_name(quoted_pair[2], "quoted token scalar") != token_class:
        raise CompileError("PeTTa quoted-token escape pair is malformed")
    if sir_literal(quoted_seq[2], "quoted-token suffix") != (
        escape_marker, string_quote
    ):
        raise CompileError("PeTTa quoted-token suffix is not escape+quote")

    return {
        "blank": form_blank,
        "token_boundary": boundary_classes[0],
        "token": token_class,
        "token_first": token_first,
        "string_plain": string_plain,
        "quoted_token_plain": quoted_classes[0],
        "expression_open": expression_open,
        "expression_close": expression_close,
        "string_quote": string_quote,
        "escape_marker": escape_marker,
        "variable_marker": variable_marker,
    }


def derive_plan(
    splitter: Presentation, form_syntax: Presentation,
    classes: dict[str, ScalarClass], policy: PeTTaProjectionPolicy
) -> PeTTaDirectPlan:
    split = derive_splitter(normalize_syntax_ir(splitter), policy)
    parsed = derive_form(normalize_syntax_ir(form_syntax), policy)
    for key in ("expression_open", "expression_close", "string_quote"):
        if split[key] != parsed[key]:
            raise CompileError(f"PeTTa splitter/form {key} values disagree")
    used = {
        str(split["blank"]), str(split["nonquote"]),
        str(split["comment_body"]), str(split["ordinary"]),
        str(parsed["blank"]), str(parsed["token_boundary"]),
        str(parsed["token"]), str(parsed["token_first"]),
        str(parsed["string_plain"]), str(parsed["quoted_token_plain"]),
    }
    missing = sorted(used - set(classes))
    if missing:
        raise CompileError("undefined PeTTa scalar classes: " + ", ".join(missing))
    if not policy.escape_map:
        raise CompileError("PeTTa projection has no named escape map")
    return PeTTaDirectPlan(
        f"{splitter.name}+{form_syntax.name}",
        str(split["blank"]), str(split["nonquote"]),
        str(split["comment_body"]), str(split["ordinary"]),
        str(parsed["blank"]), str(parsed["token_boundary"]),
        str(parsed["token"]), str(parsed["token_first"]),
        str(parsed["string_plain"]), str(parsed["quoted_token_plain"]),
        int(split["comment_marker"]), int(split["comment_line_end"]),
        int(split["expression_open"]), int(split["expression_close"]),
        int(split["string_quote"]), int(parsed["escape_marker"]),
        int(parsed["variable_marker"]), int(split["runnable_marker"]),
        policy.escape_map, policy.identity_escape, tuple(sorted(used)),
    )


def composition_digest(
    paths: Iterable[Path], profile: str, fragment: str
) -> str:
    digest = sha256()
    digest.update(b"GSLTDirectPeTTaReaderV1\0")
    digest.update(profile.encode("utf-8"))
    digest.update(b"\0")
    digest.update(fragment.encode("utf-8"))
    digest.update(b"\0")
    for path in paths:
        payload = path.read_bytes()
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
    return digest.hexdigest()


def generate(
    splitter_syntax_path: Path, splitter_classes_path: Path,
    form_syntax_path: Path, form_classes_path: Path,
    projection_path: Path, profile: str, c_prefix: str,
    output_c: Path, output_h: Path,
) -> None:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", c_prefix):
        raise CompileError(f"invalid C prefix {c_prefix!r}")
    splitter = parse_presentation(splitter_syntax_path)
    form_syntax = parse_presentation(form_syntax_path)
    split_classes = scalar_classes(parse_presentation(splitter_classes_path))
    form_classes = scalar_classes(parse_presentation(form_classes_path))
    overlap = set(split_classes) & set(form_classes)
    if overlap:
        raise CompileError(
            "splitter/form scalar class names overlap: " + ", ".join(sorted(overlap))
        )
    classes = {**split_classes, **form_classes}
    policy = projection_policy(parse_presentation(projection_path), profile)
    plan = derive_plan(splitter, form_syntax, classes, policy)
    paths = (
        splitter_syntax_path, splitter_classes_path,
        form_syntax_path, form_classes_path, projection_path,
    )
    digest = composition_digest(paths, profile, PETTA_DIRECT_FRAGMENT_V1)
    class_ids = {
        name: f"{c_prefix}_class_{index}_{c_identifier(name)}"
        for index, name in enumerate(plan.used_classes)
    }
    lines = [
        "/* Generated from the composed PeTTa document, form, and projection GSLTs. */\n",
        f'#include "generated/{output_h.name}"\n\n',
        "#include <stddef.h>\n#include <stdint.h>\n\n",
    ]
    for name in plan.used_classes:
        scalar_class = classes[name]
        identifier = class_ids[name]
        points_name = f"{identifier}_points"
        ascii_name = f"{identifier}_ascii"
        if scalar_class.points:
            lines.append(c_u32_array(points_name, scalar_class.points))
        lines.append(c_u8_array(
            ascii_name,
            [1 if scalar_class.contains(value) else 0 for value in range(256)],
        ))
        lines.append(
            f"static const GSLTDirectScalarClassV1 {identifier} = {{\n"
            f"    .ascii = {ascii_name},\n"
            f"    .points = {points_name if scalar_class.points else 'NULL'},\n"
            f"    .point_len = UINT32_C({len(scalar_class.points)}),\n"
            f"    .complement = {'true' if scalar_class.complement else 'false'},\n"
            "};\n"
        )
    escape_name = f"{c_prefix}_string_escape_map"
    lines.append(f"static const GSLTDirectCodepointMapV1 {escape_name}[] = {{\n")
    for source, target in plan.escape_map:
        lines.append(f"    {{UINT32_C({source}), UINT32_C({target})}},\n")
    lines.append("};\n")
    lines.append(
        f"\nconst GSLTDirectPeTTaReaderV1Plan {c_prefix}_plan = {{\n"
        f'    .presentation_name = "{plan.presentation_name}",\n'
        f'    .fragment = "{PETTA_DIRECT_FRAGMENT_V1}",\n'
        f'    .splitter_syntax_digest = "{file_digest(splitter_syntax_path)}",\n'
        f'    .splitter_class_digest = "{file_digest(splitter_classes_path)}",\n'
        f'    .form_syntax_digest = "{file_digest(form_syntax_path)}",\n'
        f'    .form_class_digest = "{file_digest(form_classes_path)}",\n'
        f'    .projection_digest = "{file_digest(projection_path)}",\n'
        f'    .compiler_digest = "{file_digest(Path(__file__).resolve())}",\n'
        f'    .composition_digest = "{digest}",\n'
        f'    .profile = "{profile}",\n'
        f"    .splitter_blank = &{class_ids[plan.splitter_blank]},\n"
        f"    .splitter_nonquote = &{class_ids[plan.splitter_nonquote]},\n"
        f"    .splitter_comment_body = &{class_ids[plan.splitter_comment_body]},\n"
        f"    .splitter_ordinary = &{class_ids[plan.splitter_ordinary]},\n"
        f"    .form_blank = &{class_ids[plan.form_blank]},\n"
        f"    .token_boundary = &{class_ids[plan.token_boundary]},\n"
        f"    .token = &{class_ids[plan.token]},\n"
        f"    .token_first = &{class_ids[plan.token_first]},\n"
        f"    .string_plain = &{class_ids[plan.string_plain]},\n"
        f"    .quoted_token_plain = &{class_ids[plan.quoted_token_plain]},\n"
        f"    .comment_marker = UINT32_C({plan.comment_marker}),\n"
        f"    .comment_line_end = UINT32_C({plan.comment_line_end}),\n"
        f"    .expression_open = UINT32_C({plan.expression_open}),\n"
        f"    .expression_close = UINT32_C({plan.expression_close}),\n"
        f"    .string_quote = UINT32_C({plan.string_quote}),\n"
        f"    .escape_marker = UINT32_C({plan.escape_marker}),\n"
        f"    .variable_marker = UINT32_C({plan.variable_marker}),\n"
        f"    .runnable_marker = UINT32_C({plan.runnable_marker}),\n"
        f"    .string_escape_map = {escape_name},\n"
        f"    .string_escape_map_len = UINT32_C({len(plan.escape_map)}),\n"
        f"    .string_escape_identity_fallback = {'true' if plan.identity_escape else 'false'},\n"
        "    .depth_limit = UINT32_C(4096),\n"
        "};\n\n"
        f"const char *{c_prefix}_program_digest(void) {{\n"
        f"    return {c_prefix}_plan.composition_digest;\n"
        "}\n\n"
        f"int {c_prefix}_parse_bytes_ids(\n"
        "    const uint8_t *input, size_t input_len,\n"
        "    const GSLTDirectPeTTaProjectionV1 *projection,\n"
        "    AtomId **out_ids, GSLTDirectPeTTaReaderV1Receipt *receipt,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "    return gslt_direct_petta_reader_v1_parse_bytes_ids(\n"
        f"        &{c_prefix}_plan, input, input_len, projection, out_ids,\n"
        "        receipt, error_buf, error_buf_size);\n"
        "}\n"
    )
    output_c.parent.mkdir(parents=True, exist_ok=True)
    output_c.write_text("".join(lines), encoding="utf-8")

    guard = "CETTA_GENERATED_" + c_prefix.upper() + "_H"
    output_h.parent.mkdir(parents=True, exist_ok=True)
    output_h.write_text(
        "/* Generated from the composed PeTTa document, form, and projection GSLTs. */\n"
        f"#ifndef {guard}\n#define {guard}\n\n"
        '#include "gslt_direct_reader_v1.h"\n\n'
        f"extern const GSLTDirectPeTTaReaderV1Plan {c_prefix}_plan;\n"
        f"const char *{c_prefix}_program_digest(void);\n\n"
        f"int {c_prefix}_parse_bytes_ids(\n"
        "    const uint8_t *input, size_t input_len,\n"
        "    const GSLTDirectPeTTaProjectionV1 *projection,\n"
        "    AtomId **out_ids, GSLTDirectPeTTaReaderV1Receipt *receipt,\n"
        "    char *error_buf, size_t error_buf_size);\n\n"
        f"#endif /* {guard} */\n",
        encoding="utf-8",
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--splitter-syntax", required=True, type=Path)
    parser.add_argument("--splitter-classes", required=True, type=Path)
    parser.add_argument("--form-syntax", required=True, type=Path)
    parser.add_argument("--form-classes", required=True, type=Path)
    parser.add_argument("--projection", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--c-prefix", required=True)
    parser.add_argument("--output-c", required=True, type=Path)
    parser.add_argument("--output-h", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        generate(
            arguments.splitter_syntax, arguments.splitter_classes,
            arguments.form_syntax, arguments.form_classes,
            arguments.projection, arguments.profile, arguments.c_prefix,
            arguments.output_c, arguments.output_h,
        )
    except (CompileError, OSError, ValueError) as error:
        print(f"GSLTDirectPeTTaReaderCompileError: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
