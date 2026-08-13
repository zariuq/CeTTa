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
class NikAuthorityFrameV1:
    mode: str
    certificate_policy: str
    fiber: str
    outcomes: tuple[str, ...]
    default_outcome: str
    native_projection: str
    status: str
    commitments: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class NikRuleFrameV1:
    native_projection: str
    role: str


@dataclass(frozen=True, slots=True)
class RuleDecl:
    name: str
    head: SExpr
    body: tuple[SExpr, ...]
    nik_frame: NikRuleFrameV1 | None = None


@dataclass(frozen=True, slots=True)
class Presentation:
    name: str
    operators: tuple[OperatorDecl, ...]
    rules: tuple[RuleDecl, ...]
    source: Path
    nik_frame: NikAuthorityFrameV1 | None = None


_INTEGER = re.compile(r"-?[0-9]+\Z")
_SAFE_SYMBOL = re.compile(r'[^\s();"]+\Z')
_REQUIRED_ROOT_FIELDS = ("signature", "equations", "rewrites")
_OPTIONAL_ROOT_FIELDS = ("nik-authority-frame-v1",)
_ROOT_FIELDS = _REQUIRED_ROOT_FIELDS + _OPTIONAL_ROOT_FIELDS
_NIK_AUTHORITY_MODES = {
    "direct-decision",
    "native-proof",
    "admitted-operation",
    "boundary-certificate",
}
_NIK_PROJECTION_STATUSES = {"pending", "qualified", "derived"}
_NIK_PRESENTATION_STATUSES = {
    "AUTHORED_FRAGMENT",
    "COMPLETE_PRESENTATION",
    "GENERATED_PROJECTION",
}
_NIK_RULE_ROLES = {"calculus", "calibration"}


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


def _parse_named_fields(
    forms: Sequence[SExpr],
    *,
    allowed: set[str],
    source: Path,
    context: str,
) -> dict[str, tuple[SExpr, ...]]:
    fields: dict[str, tuple[SExpr, ...]] = {}
    for raw_field in forms:
        field = _list(raw_field, f"{source}: {context} field")
        if not field:
            raise SchemaError(f"{source}: empty {context} field")
        tag = _symbol(field[0], f"{source}: {context} field tag")
        if tag not in allowed:
            raise SchemaError(f"{source}: unknown {context} field {tag}")
        if tag in fields:
            raise SchemaError(f"{source}: duplicate {context} field {tag}")
        fields[tag] = field
    missing = sorted(allowed.difference(fields))
    if missing:
        raise SchemaError(
            f"{source}: missing {context} fields: {', '.join(missing)}"
        )
    return fields


def _one_symbol_field(
    field: tuple[SExpr, ...], *, source: Path, context: str
) -> str:
    if len(field) != 2:
        raise SchemaError(f"{source}: {context} must contain exactly one symbol")
    return _symbol(field[1], f"{source}: {context}")


def parse_nik_authority_frame(
    form: SExpr, source: Path
) -> NikAuthorityFrameV1:
    items = _list(form, f"{source}: nik-authority-frame-v1")
    if not items or _symbol(items[0], f"{source}: authority frame tag") != (
        "nik-authority-frame-v1"
    ):
        raise SchemaError(f"{source}: malformed nik-authority-frame-v1")
    fields = _parse_named_fields(
        items[1:],
        allowed={
            "mode",
            "certificate-policy",
            "fiber",
            "outcome-algebra",
            "native-projection",
            "status",
            "commitments",
        },
        source=source,
        context="nik-authority-frame-v1",
    )
    mode = _one_symbol_field(fields["mode"], source=source, context="mode")
    if mode not in _NIK_AUTHORITY_MODES:
        raise SchemaError(f"{source}: unsupported NIK authority mode {mode}")
    certificate_policy = _one_symbol_field(
        fields["certificate-policy"],
        source=source,
        context="certificate-policy",
    )
    if mode == "direct-decision" and certificate_policy != "none":
        raise SchemaError(
            f"{source}: direct-decision authority requires certificate-policy none"
        )
    fiber = _one_symbol_field(
        fields["fiber"], source=source, context="fiber"
    )

    outcome = fields["outcome-algebra"]
    if len(outcome) != 4:
        raise SchemaError(
            f"{source}: outcome-algebra must contain outcomes, exclusive, and default"
        )
    raw_outcomes = _list(outcome[1], f"{source}: outcome list")
    if len(raw_outcomes) < 2:
        raise SchemaError(f"{source}: outcome algebra requires at least two outcomes")
    outcomes = tuple(
        _symbol(item, f"{source}: outcome algebra member") for item in raw_outcomes
    )
    if len(set(outcomes)) != len(outcomes):
        raise SchemaError(f"{source}: outcome algebra contains duplicate outcomes")
    exclusive = _list(outcome[2], f"{source}: outcome exclusivity")
    if len(exclusive) != 1 or _symbol(
        exclusive[0], f"{source}: outcome exclusivity tag"
    ) != "exclusive":
        raise SchemaError(f"{source}: outcome algebra must declare (exclusive)")
    default = _list(outcome[3], f"{source}: default outcome")
    if len(default) != 2 or _symbol(
        default[0], f"{source}: default outcome tag"
    ) != "default":
        raise SchemaError(f"{source}: outcome algebra must declare one default")
    default_outcome = _symbol(default[1], f"{source}: default outcome value")
    if default_outcome not in outcomes:
        raise SchemaError(f"{source}: default outcome is not in the outcome algebra")

    native_projection = _one_symbol_field(
        fields["native-projection"],
        source=source,
        context="native-projection",
    )
    if native_projection not in _NIK_PROJECTION_STATUSES:
        raise SchemaError(
            f"{source}: unsupported native-projection status {native_projection}"
        )
    status = _one_symbol_field(
        fields["status"], source=source, context="status"
    )
    if status not in _NIK_PRESENTATION_STATUSES:
        raise SchemaError(f"{source}: unsupported NIK presentation status {status}")
    commitments_field = fields["commitments"]
    commitments = tuple(
        _symbol(item, f"{source}: authority commitment")
        for item in commitments_field[1:]
    )
    if len(set(commitments)) != len(commitments):
        raise SchemaError(f"{source}: duplicate NIK authority commitment")
    return NikAuthorityFrameV1(
        mode,
        certificate_policy,
        fiber,
        outcomes,
        default_outcome,
        native_projection,
        status,
        commitments,
    )


def parse_nik_rule_frame(form: SExpr, source: Path, name: str) -> NikRuleFrameV1:
    items = _list(form, f"{source}: rule {name} nik-rule-frame-v1")
    if not items or _symbol(items[0], f"{source}: rule {name} frame tag") != (
        "nik-rule-frame-v1"
    ):
        raise SchemaError(f"{source}: malformed nik-rule-frame-v1 in rule {name}")
    fields = _parse_named_fields(
        items[1:],
        allowed={"native-projection", "role"},
        source=source,
        context=f"rule {name} nik-rule-frame-v1",
    )
    native_projection = _one_symbol_field(
        fields["native-projection"],
        source=source,
        context=f"rule {name} native-projection",
    )
    role = _one_symbol_field(
        fields["role"], source=source, context=f"rule {name} role"
    )
    if role not in _NIK_RULE_ROLES:
        raise SchemaError(f"{source}: unsupported NIK rule role {role} in {name}")
    return NikRuleFrameV1(native_projection, role)


def parse_rule(form: SExpr, source: Path, *, framed: bool = False) -> RuleDecl:
    items = _list(form, f"{source}: rule")
    expected_length = 5 if framed else 4
    if (
        len(items) != expected_length
        or _symbol(items[0], f"{source}: rule tag") != "rule"
    ):
        raise SchemaError(f"{source}: malformed rule: {render(form)}")
    name = _symbol(items[1], f"{source}: rule name")
    nik_frame = parse_nik_rule_frame(items[2], source, name) if framed else None
    head_index = 3 if framed else 2
    body_index = 4 if framed else 3
    head_wrapper = _list(items[head_index], f"{source}: rule {name} head")
    body_wrapper = _list(items[body_index], f"{source}: rule {name} body")
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
    return RuleDecl(name, head_wrapper[1], tuple(body_wrapper[1:]), nik_frame)


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
    missing = [field for field in _REQUIRED_ROOT_FIELDS if field not in fields]
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

    nik_frame = (
        parse_nik_authority_frame(fields["nik-authority-frame-v1"], path)
        if "nik-authority-frame-v1" in fields
        else None
    )
    rules = tuple(
        parse_rule(form, path, framed=nik_frame is not None)
        for form in fields["rewrites"][1:]
    )
    local_rule_names: set[str] = set()
    for rule in rules:
        if rule.name in local_rule_names:
            raise SchemaError(f"{path}: duplicate rule name {rule.name}")
        local_rule_names.add(rule.name)
    return Presentation(name, tuple(operators), rules, path, nik_frame)


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


def canonical_nik_authority_frame(frame: NikAuthorityFrameV1) -> SExpr:
    return (
        Symbol("nik-authority-frame-v1"),
        (Symbol("mode"), Symbol(frame.mode)),
        (Symbol("certificate-policy"), Symbol(frame.certificate_policy)),
        (Symbol("fiber"), Symbol(frame.fiber)),
        (
            Symbol("outcome-algebra"),
            tuple(Symbol(outcome) for outcome in frame.outcomes),
            (Symbol("exclusive"),),
            (Symbol("default"), Symbol(frame.default_outcome)),
        ),
        (Symbol("native-projection"), Symbol(frame.native_projection)),
        (Symbol("status"), Symbol(frame.status)),
        (
            Symbol("commitments"),
            *(Symbol(commitment) for commitment in frame.commitments),
        ),
    )


def canonical_nik_rule_frame(frame: NikRuleFrameV1) -> SExpr:
    return (
        Symbol("nik-rule-frame-v1"),
        (Symbol("native-projection"), Symbol(frame.native_projection)),
        (Symbol("role"), Symbol(frame.role)),
    )


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
            *(
                (canonical_nik_rule_frame(rule.nik_frame),)
                if rule.nik_frame is not None
                else ()
            ),
            (Symbol("head"), rule.head),
            (Symbol("body"), *rule.body),
        )
        for rule in sorted(presentation.rules, key=lambda item: item.name)
    )
    authority_frame: tuple[SExpr, ...] = ()
    if presentation.nik_frame is not None:
        authority_frame = (canonical_nik_authority_frame(presentation.nik_frame),)
    return (
        Symbol("gslt-presentation-v1"),
        Symbol(presentation.name),
        *authority_frame,
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
    parser.add_argument("--require-nik-authority-frame", action="store_true")
    args = parser.parse_args(argv)
    try:
        presentations = admit(args.presentations)
        if args.require_nik_authority_frame:
            unframed = [
                str(item.source)
                for item in presentations
                if item.nik_frame is None
            ]
            if unframed:
                raise SchemaError(
                    "missing nik-authority-frame-v1: " + ", ".join(unframed)
                )
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
