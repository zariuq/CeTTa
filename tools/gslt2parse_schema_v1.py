#!/usr/bin/env python3
"""Validate and canonically serialize the finite Horn GSLT v1 fragment.

The fragment reifies a one-carrier GSLT as:

* a term grammar: primitive atom/integer/string terms plus declared compound
  operators and their arities;
* identity equations (the v1 executable fragment admits no authored equations);
* named Horn rules, interpreted as rewrites of proof-search states.

The validator is an untrusted development gate.  It does not decide parsing or
serve as a language oracle.
"""

from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
from typing import Iterable, Iterator, Sequence, TypeAlias
import argparse
import json
import re


@dataclass(frozen=True, slots=True)
class Symbol:
    text: str


@dataclass(frozen=True, slots=True)
class Variable:
    name: str


@dataclass(frozen=True, slots=True)
class StringLiteral:
    text: str


SExpr: TypeAlias = Symbol | Variable | StringLiteral | int | tuple["SExpr", ...]


class SchemaError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class OperatorDecl:
    name: str
    arity: int


@dataclass(frozen=True, slots=True)
class RuleDecl:
    name: str
    head: SExpr
    body: tuple[SExpr, ...]


@dataclass(frozen=True, slots=True)
class Presentation:
    name: str
    operators: tuple[OperatorDecl, ...]
    rules: tuple[RuleDecl, ...]
    source: Path


_INTEGER = re.compile(r"-?[0-9]+\Z")
_SAFE_SYMBOL = re.compile(r'[^\s();"]+\Z')
_ROOT_FIELDS = ("signature", "equations", "rewrites")


def _require_unicode_scalars(value: str, *, source: str, offset: int) -> None:
    for index, char in enumerate(value):
        if 0xD800 <= ord(char) <= 0xDFFF:
            raise SchemaError(
                f"{source}: string at offset {offset} contains a non-scalar "
                f"Unicode surrogate at string index {index}"
            )


def _symbol(value: SExpr, context: str) -> str:
    if not isinstance(value, Symbol) or not value.text:
        raise SchemaError(f"{context}: expected a nonempty symbol")
    return value.text


def _list(value: SExpr, context: str) -> tuple[SExpr, ...]:
    if not isinstance(value, tuple):
        raise SchemaError(f"{context}: expected a list")
    return value


def parse_sexprs(text: str, *, source: str = "<text>") -> list[SExpr]:
    """Parse the canonical S-expression carrier without losing string identity."""

    tokens: list[SExpr | str] = []
    position = 0
    length = len(text)
    while position < length:
        char = text[position]
        if char.isspace():
            position += 1
            continue
        if char == ";":
            newline = text.find("\n", position)
            position = length if newline < 0 else newline + 1
            continue
        if char in "()":
            tokens.append(char)
            position += 1
            continue
        if char == '"':
            start = position
            position += 1
            escaped = False
            while position < length:
                current = text[position]
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == '"':
                    position += 1
                    break
                position += 1
            else:
                raise SchemaError(f"{source}: unterminated string at offset {start}")
            spelling = text[start:position]
            try:
                value = json.loads(spelling)
            except json.JSONDecodeError as error:
                raise SchemaError(
                    f"{source}: invalid string at offset {start}: {error.msg}"
                ) from error
            _require_unicode_scalars(value, source=source, offset=start)
            tokens.append(StringLiteral(value))
            continue

        start = position
        while (
            position < length
            and not text[position].isspace()
            and text[position] not in '();"'
        ):
            position += 1
        spelling = text[start:position]
        if not spelling:
            raise SchemaError(f"{source}: invalid token at offset {start}")
        if spelling.startswith("?") and len(spelling) > 1:
            tokens.append(Variable(spelling[1:]))
        elif _INTEGER.fullmatch(spelling):
            tokens.append(int(spelling))
        else:
            tokens.append(Symbol(spelling))

    index = 0

    def parse_one() -> SExpr:
        nonlocal index
        if index >= len(tokens):
            raise SchemaError(f"{source}: unexpected end of input")
        token = tokens[index]
        index += 1
        if token == "(":
            values: list[SExpr] = []
            while True:
                if index >= len(tokens):
                    raise SchemaError(f"{source}: unclosed '('")
                if tokens[index] == ")":
                    index += 1
                    return tuple(values)
                values.append(parse_one())
        if token == ")":
            raise SchemaError(f"{source}: unexpected ')'")
        assert not isinstance(token, str)
        return token

    forms: list[SExpr] = []
    while index < len(tokens):
        forms.append(parse_one())
    return forms


def render(term: SExpr) -> str:
    if isinstance(term, Symbol):
        if not _SAFE_SYMBOL.fullmatch(term.text):
            raise SchemaError(f"symbol cannot be serialized canonically: {term.text!r}")
        return term.text
    if isinstance(term, Variable):
        if not term.name or term.name == "_" or not _SAFE_SYMBOL.fullmatch(term.name):
            raise SchemaError(f"invalid variable name: {term.name!r}")
        return f"?{term.name}"
    if isinstance(term, StringLiteral):
        return json.dumps(term.text, ensure_ascii=False, separators=(",", ":"))
    if isinstance(term, int):
        return str(term)
    return "(" + " ".join(render(item) for item in term) + ")"


def parse_rule(form: SExpr, source: Path) -> RuleDecl:
    items = _list(form, f"{source}: rule")
    if len(items) != 4 or _symbol(items[0], f"{source}: rule tag") != "rule":
        raise SchemaError(f"{source}: malformed rule: {render(form)}")
    name = _symbol(items[1], f"{source}: rule name")
    head_wrapper = _list(items[2], f"{source}: rule {name} head")
    body_wrapper = _list(items[3], f"{source}: rule {name} body")
    if (
        len(head_wrapper) != 2
        or _symbol(head_wrapper[0], f"{source}: rule {name} head tag") != "head"
    ):
        raise SchemaError(f"{source}: malformed head in rule {name}")
    if (
        not body_wrapper
        or _symbol(body_wrapper[0], f"{source}: rule {name} body tag") != "body"
    ):
        raise SchemaError(f"{source}: malformed body in rule {name}")
    return RuleDecl(name, head_wrapper[1], tuple(body_wrapper[1:]))


def parse_presentation(path: Path) -> Presentation:
    forms = parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1:
        raise SchemaError(
            f"{path}: expected one gslt-presentation-v1 form, found {len(forms)}"
        )
    root = _list(forms[0], f"{path}: root")
    if len(root) < 2 or _symbol(root[0], f"{path}: root tag") != "gslt-presentation-v1":
        raise SchemaError(f"{path}: expected gslt-presentation-v1 root")
    name = _symbol(root[1], f"{path}: presentation name")

    fields: dict[str, tuple[SExpr, ...]] = {}
    for raw_field in root[2:]:
        field = _list(raw_field, f"{path}: presentation field")
        if not field:
            raise SchemaError(f"{path}: empty presentation field")
        tag = _symbol(field[0], f"{path}: presentation field tag")
        if tag not in _ROOT_FIELDS:
            raise SchemaError(f"{path}: unknown presentation field {tag}")
        if tag in fields:
            raise SchemaError(f"{path}: duplicate presentation field {tag}")
        fields[tag] = field
    missing = [field for field in _ROOT_FIELDS if field not in fields]
    if missing:
        raise SchemaError(f"{path}: missing presentation fields: {', '.join(missing)}")

    operators: list[OperatorDecl] = []
    operator_keys: set[tuple[str, int]] = set()
    for raw_declaration in fields["signature"][1:]:
        declaration = _list(raw_declaration, f"{path}: operator declaration")
        if (
            len(declaration) != 3
            or _symbol(declaration[0], f"{path}: operator tag") != "operator"
            or not isinstance(declaration[2], int)
            or declaration[2] <= 0
        ):
            raise SchemaError(
                f"{path}: operator declaration must be (operator NAME POSITIVE-ARITY)"
            )
        operator = OperatorDecl(
            _symbol(declaration[1], f"{path}: operator name"), declaration[2]
        )
        key = (operator.name, operator.arity)
        if key in operator_keys:
            raise SchemaError(f"{path}: duplicate operator {operator.name}/{operator.arity}")
        operator_keys.add(key)
        operators.append(operator)

    if len(fields["equations"]) != 1:
        raise SchemaError(
            f"{path}: v1 admits identity equations only; authored equations are unsupported"
        )

    rules = tuple(parse_rule(form, path) for form in fields["rewrites"][1:])
    local_rule_names: set[str] = set()
    for rule in rules:
        if rule.name in local_rule_names:
            raise SchemaError(f"{path}: duplicate rule name {rule.name}")
        local_rule_names.add(rule.name)
    return Presentation(name, tuple(operators), rules, path)


def _validate_term(
    term: SExpr,
    operators: set[tuple[str, int]],
    *,
    source: Path,
    rule: str,
    location: str,
) -> None:
    if isinstance(term, Variable):
        if not term.name or term.name == "_":
            raise SchemaError(
                f"{source}: anonymous variable at {location} in rule {rule}"
            )
        return
    if not isinstance(term, tuple):
        return
    if not term:
        raise SchemaError(f"{source}: empty term at {location} in rule {rule}")
    head = _symbol(term[0], f"{source}: {location} head in rule {rule}")
    key = (head, len(term) - 1)
    if key not in operators:
        raise SchemaError(
            f"{source}: undeclared operator {head}/{len(term) - 1} "
            f"at {location} in rule {rule}"
        )
    for index, child in enumerate(term[1:]):
        _validate_term(
            child,
            operators,
            source=source,
            rule=rule,
            location=f"{location}.{head}[{index}]",
        )


def admit(paths: Sequence[Path]) -> tuple[Presentation, ...]:
    if not paths:
        raise SchemaError("at least one presentation is required")
    presentations = tuple(parse_presentation(path) for path in paths)
    names: set[str] = set()
    operator_keys: set[tuple[str, int]] = set()
    rule_names: set[str] = set()
    for presentation in presentations:
        if presentation.name in names:
            raise SchemaError(f"duplicate presentation name {presentation.name}")
        names.add(presentation.name)
        for operator in presentation.operators:
            operator_keys.add((operator.name, operator.arity))
        for rule in presentation.rules:
            if rule.name in rule_names:
                raise SchemaError(f"duplicate composed rule name {rule.name}")
            rule_names.add(rule.name)

    for presentation in presentations:
        for rule in presentation.rules:
            if not isinstance(rule.head, tuple):
                raise SchemaError(
                    f"{presentation.source}: rule {rule.name} head must be an application"
                )
            _validate_term(
                rule.head,
                operator_keys,
                source=presentation.source,
                rule=rule.name,
                location="head",
            )
            for index, goal in enumerate(rule.body):
                if not isinstance(goal, tuple):
                    raise SchemaError(
                        f"{presentation.source}: body[{index}] of rule {rule.name} "
                        "must be an application"
                    )
                _validate_term(
                    goal,
                    operator_keys,
                    source=presentation.source,
                    rule=rule.name,
                    location=f"body[{index}]",
                )
    return presentations


def canonical_form(presentation: Presentation) -> SExpr:
    operators = tuple(
        (
            Symbol("operator"),
            Symbol(operator.name),
            operator.arity,
        )
        for operator in sorted(
            presentation.operators, key=lambda item: (item.name, item.arity)
        )
    )
    rules = tuple(
        (
            Symbol("rule"),
            Symbol(rule.name),
            (Symbol("head"), rule.head),
            (Symbol("body"), *rule.body),
        )
        for rule in sorted(presentation.rules, key=lambda item: item.name)
    )
    return (
        Symbol("gslt-presentation-v1"),
        Symbol(presentation.name),
        (Symbol("signature"), *operators),
        (Symbol("equations"),),
        (Symbol("rewrites"), *rules),
    )


def canonical_text(presentation: Presentation) -> str:
    return render(canonical_form(presentation)) + "\n"


def package_digest(presentations: Iterable[Presentation]) -> str:
    digest = sha256()
    digest.update(b"FiniteHornGSLTPackageV1\0")
    ordered = sorted(presentations, key=lambda item: item.name)
    for presentation in ordered:
        payload = canonical_text(presentation).encode("utf-8")
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
    return digest.hexdigest()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("presentations", nargs="+", type=Path)
    parser.add_argument("--emit-canonical", action="store_true")
    parser.add_argument("--digest-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        presentations = admit(args.presentations)
    except (OSError, SchemaError) as error:
        parser.exit(1, f"MalformedPresentation: {error}\n")
    digest = package_digest(presentations)
    if args.emit_canonical:
        for presentation in sorted(presentations, key=lambda item: item.name):
            print(canonical_text(presentation), end="")
    if args.digest_only:
        print(digest)
    else:
        operator_count = sum(len(item.operators) for item in presentations)
        rule_count = sum(len(item.rules) for item in presentations)
        print(
            f"(FiniteHornGSLTV1Summary {len(presentations)} "
            f"{operator_count} {rule_count} {digest})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
