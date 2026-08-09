#!/usr/bin/env python3
"""Translate a fail-closed basic MM2 subset into executable MeTTa.

The generated program runs in CeTTa's HE, Prime, and PeTTa lanes.  It keeps
MM2 data in a separate space and drives the companion tools/mm2_exec.metta
engine with a compile-time priority queue sorted by MORK's encoded expression
order.

Supported MM2 forms:

* ground exec locations and comma queries;
* comma output (compatibility add), and O output containing + and - sinks;
* a single variable-guarded count sink.

Dynamic exec creation, I sources, reflective exec mutation, mixed reductions,
and all other sinks are rejected.  Rejecting unsupported semantics is part of
the contract: this tool never silently emits an approximation.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys
from typing import Iterator, Sequence, TypeAlias


SExpr: TypeAlias = str | list["SExpr"]


class TranslationError(ValueError):
    pass


def tokenize(text: str) -> Iterator[str]:
    index = 0
    while index < len(text):
        char = text[index]
        if char.isspace():
            index += 1
            continue
        if char == ";":
            newline = text.find("\n", index)
            index = len(text) if newline < 0 else newline + 1
            continue
        if char in "()":
            yield char
            index += 1
            continue
        if char == '"':
            start = index
            index += 1
            escaped = False
            while index < len(text):
                current = text[index]
                index += 1
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == '"':
                    break
            else:
                raise TranslationError("unterminated string literal")
            yield text[start:index]
            continue
        start = index
        while (index < len(text) and
               not text[index].isspace() and
               text[index] not in "();"):
            index += 1
        if start == index:
            raise TranslationError(f"unexpected character {text[index]!r}")
        yield text[start:index]


def parse(text: str) -> list[SExpr]:
    tokens = list(tokenize(text))
    position = 0

    def read() -> SExpr:
        nonlocal position
        if position >= len(tokens):
            raise TranslationError("unexpected end of input")
        token = tokens[position]
        position += 1
        if token == ")":
            raise TranslationError("unexpected closing parenthesis")
        if token != "(":
            return token
        result: list[SExpr] = []
        while position < len(tokens) and tokens[position] != ")":
            result.append(read())
        if position >= len(tokens):
            raise TranslationError("unclosed parenthesis")
        position += 1
        return result

    forms: list[SExpr] = []
    while position < len(tokens):
        forms.append(read())
    return forms


def unparse(term: SExpr) -> str:
    if isinstance(term, list):
        return "(" + " ".join(unparse(item) for item in term) + ")"
    return term


def contains(term: SExpr, symbol: str) -> bool:
    if term == symbol:
        return True
    return isinstance(term, list) and any(contains(item, symbol) for item in term)


def has_variable(term: SExpr) -> bool:
    if isinstance(term, str):
        return term.startswith("$")
    return any(has_variable(item) for item in term)


def variables(term: SExpr) -> set[str]:
    if isinstance(term, str):
        return {term} if term.startswith("$") else set()
    result: set[str] = set()
    for item in term:
        result.update(variables(item))
    return result


def substitute(term: SExpr, old: str, new: str) -> SExpr:
    if term == old:
        return new
    if isinstance(term, list):
        return [substitute(item, old, new) for item in term]
    return term


def mork_encoded_key(term: SExpr) -> bytes:
    """Encode the ordering bytes used by MORK's PathMap expression keys."""
    variables: dict[str, int] = {}
    next_variable = 0
    output = bytearray()

    def emit(item: SExpr) -> None:
        nonlocal next_variable
        if isinstance(item, list):
            if len(item) >= 64:
                raise TranslationError("MORK expressions support fewer than 64 items")
            output.append(len(item))
            for child in item:
                emit(child)
            return
        if item.startswith('"'):
            raise TranslationError("quoted strings are outside the basic translator subset")
        if item.startswith("$"):
            anonymous = item == "$"
            if not anonymous and item in variables:
                output.append(0x80 | variables[item])
                return
            if next_variable >= 64:
                raise TranslationError("MORK expressions support fewer than 64 variables")
            if not anonymous:
                variables[item] = next_variable
            next_variable += 1
            output.append(0xC0)
            return
        encoded = item.encode("utf-8")
        if not encoded or len(encoded) >= 64:
            raise TranslationError(
                "MORK symbols must contain between 1 and 63 UTF-8 bytes")
        output.append(0xC0 | len(encoded))
        output.extend(encoded)

    emit(term)
    return bytes(output)


def is_call(term: SExpr, head: str, arity: int | None = None) -> bool:
    return (isinstance(term, list) and bool(term) and term[0] == head and
            (arity is None or len(term) == arity + 1))


@dataclass(frozen=True)
class CompiledRule:
    key: bytes
    raw: SExpr
    queue_entry: SExpr


def reject_exec_payload(term: SExpr) -> None:
    if contains(term, "exec"):
        raise TranslationError(
            "dynamic or reflective exec mutation is outside the basic subset")


def compile_exec(form: SExpr) -> CompiledRule:
    if not is_call(form, "exec", 3):
        raise TranslationError("exec must have shape (exec location query template)")
    assert isinstance(form, list)
    _, location, query, template = form
    if has_variable(location):
        raise TranslationError("exec location must be ground")
    if not (isinstance(query, list) and len(query) >= 2 and query[0] == ","):
        raise TranslationError("only comma queries are supported; I sources are rejected")
    if contains(query, "exec"):
        raise TranslationError("reflective exec queries are outside the basic subset")
    query_variables = variables(query)

    if not isinstance(template, list) or not template:
        raise TranslationError("exec template must be a non-empty comma or O expression")

    if template[0] == ",":
        operations: list[SExpr] = []
        for atom in template[1:]:
            reject_exec_payload(atom)
            if not variables(atom) <= query_variables:
                raise TranslationError(
                    "comma output contains a variable not bound by the query")
            operations.append(["mm2add", atom])
        translated: SExpr = ["mm2ops", operations]
        entry: SExpr = ["mm2write", query, translated]
    elif template[0] == "O":
        sinks = template[1:]
        if len(sinks) == 1 and is_call(sinks[0], "count", 3):
            reduction = sinks[0]
            assert isinstance(reduction, list)
            _, target, guard, counted = reduction
            if not isinstance(guard, str) or not guard.startswith("$") or guard == "$":
                raise TranslationError(
                    "basic count requires a named variable guard")
            if contains(query, guard):
                raise TranslationError(
                    "count guards bound by the query are outside the basic subset")
            if not contains(target, guard):
                raise TranslationError(
                    "count guard must occur in its output pattern")
            if contains(counted, guard):
                raise TranslationError(
                    "count guard may not occur in the counted value")
            if not (variables(target) - {guard}) <= query_variables:
                raise TranslationError(
                    "count output contains a variable not bound by the query")
            if not variables(counted) <= query_variables:
                raise TranslationError(
                    "counted value contains a variable not bound by the query")
            reject_exec_payload(target)
            reject_exec_payload(counted)
            target_with_slot = substitute(target, guard, "MM2_COUNT_SLOT")
            entry = ["mm2count", query, target_with_slot, counted]
        else:
            operations = []
            for sink in sinks:
                if not isinstance(sink, list) or len(sink) != 2:
                    raise TranslationError(
                        "basic O templates support only unary + and - sinks")
                operator, atom = sink
                if operator not in ("+", "-"):
                    raise TranslationError(
                        f"unsupported MM2 sink {operator!r}; supported: +, -, count")
                reject_exec_payload(atom)
                if not variables(atom) <= query_variables:
                    raise TranslationError(
                        "sink output contains a variable not bound by the query")
                operations.append(
                    ["mm2add" if operator == "+" else "mm2del", atom])
            translated = ["mm2ops", operations]
            entry = ["mm2write", query, translated]
    else:
        raise TranslationError("exec template functor must be comma or O")

    return CompiledRule(mork_encoded_key(form), form, entry)


@dataclass(frozen=True)
class CompiledProgram:
    atoms: tuple[SExpr, ...]
    rules: tuple[CompiledRule, ...]


def compile_program(text: str) -> CompiledProgram:
    forms = parse(text)
    if any(contains(form, "MM2_COUNT_SLOT") for form in forms):
        raise TranslationError("MM2_COUNT_SLOT is reserved by the translator")

    unique_atoms: dict[bytes, SExpr] = {}
    unique_rules: dict[bytes, CompiledRule] = {}
    for form in forms:
        key = mork_encoded_key(form)
        if is_call(form, "exec"):
            rule = compile_exec(form)
            unique_rules.setdefault(rule.key, rule)
        else:
            if has_variable(form):
                raise TranslationError(
                    "basic translator requires ground non-exec program atoms")
            unique_atoms.setdefault(key, form)

    rules = tuple(sorted(unique_rules.values(), key=lambda rule: rule.key))
    atoms = tuple(unique_atoms[key] for key in sorted(unique_atoms))
    return CompiledProgram(atoms, rules)


def render_program(program: CompiledProgram, include_engine: bool) -> str:
    parts: list[str] = []
    if include_engine:
        engine = Path(__file__).resolve().parent / "mm2_exec.metta"
        parts.append(engine.read_text(encoding="utf-8").rstrip())
    parts.append("!(let $ignored (bind! &mm2-program (new-space)) ())")
    parts.extend(
        f"!(let $ignored (add-atom &mm2-program {unparse(atom)}) ())"
        for atom in program.atoms)
    queue: SExpr = [rule.queue_entry for rule in program.rules]
    parts.append(
        f"!(let $ignored (mm2-run &mm2-program {unparse(queue)}) ())")
    parts.append(
        "!(collapse (match &mm2-program $atom (mm2-result $atom)))")
    return "\n\n".join(parts) + "\n"


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input", nargs="?", help="MM2 input file; omit to read standard input")
    parser.add_argument(
        "--program-only", action="store_true",
        help="omit the embedded companion MeTTa engine")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = argument_parser().parse_args(argv)
    try:
        if args.input:
            text = Path(args.input).read_text(encoding="utf-8")
        else:
            text = sys.stdin.read()
        program = compile_program(text)
        sys.stdout.write(render_program(program, not args.program_only))
        return 0
    except (OSError, TranslationError) as error:
        print(f"mm2_to_metta: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
