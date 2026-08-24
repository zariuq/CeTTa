#!/usr/bin/env python3
"""Generate Prime qualification for Hopper's seven first-order task group."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_hopper_table1_manifest as corpus
from prime_iggp_generation import GenerationError, fact_block, materialize_outputs


TASKS = (
    "dropK",
    "allEven",
    "findDup",
    "length",
    "member",
    "sorted",
    "reverse",
)
SOURCE_VARIANT = "ho"
PROGRAMS = {
    "dropK": (
        "f(A,B,C):-ite_a(B,A,C).",
        "ite_p_a(A,B):-tail(A,B).",
    ),
    "allEven": (
        "f(A):-all_a(A).",
        "all_p_a(A):-even(A).",
    ),
    "findDup": (
        "caselist_q_a(A,B,C):-caselist_a(B,C).",
        "caselist_q_a(A,B,C):-member(C,B),eq(C,A).",
        "caselist_p_a(A):-e(B),member(A,B).",
        "f(A,B):-caselist_a(A,B).",
    ),
    "length": (
        "fold_p_a(A,B,C):-suc(A,C).",
        "f(A,B):-zero(C),fold_a(C,A,B).",
    ),
    "member": (
        "any_p_a(A,B):-head(A,B).",
        "f(A,B):-any_a(A,B).",
    ),
    "sorted": (
        "fold_p_a(A,B,C):-suc(B,C),geq(C,A).",
        "f(A):-zero(C),fold_a(C,A,B).",
    ),
    "reverse": (
        "fold_p_a(A,B,C):-head(C,B),tail(C,A).",
        "f(A,B):-empty(C),fold_a(C,A,B).",
    ),
}
EXPECTED_TARGETS = {
    "dropK": ("integer", "list", "list"),
    "allEven": ("list",),
    "findDup": ("list", "element"),
    "length": ("list", "integer"),
    "member": ("list", "element"),
    "sorted": ("list",),
    "reverse": ("list", "list"),
}
TOKEN = re.compile(
    r"\s*(?:(?P<name>[A-Za-z_][A-Za-z0-9_]*|-?[0-9]+)"
    r"|(?P<quoted>'[^']*')|(?P<punct>[\[\](),.]))"
)


@dataclass(frozen=True)
class Atom:
    head: str
    args: tuple["Term", ...] = ()


@dataclass(frozen=True)
class ListTerm:
    items: tuple["Term", ...]


Term = Atom | ListTerm


class GroundParser:
    def __init__(self, source: str, path: str):
        self.path = path
        self.tokens: list[str] = []
        position = 0
        while position < len(source):
            match = TOKEN.match(source, position)
            if match is None:
                if source[position:].strip():
                    raise GenerationError(
                        f"{path}: unsupported syntax near {source[position:position + 24]!r}"
                    )
                break
            quoted = match.group("quoted")
            self.tokens.append(
                match.group("name")
                or (quoted[1:-1] if quoted is not None else None)
                or match.group("punct")
            )
            position = match.end()
        self.index = 0

    def peek(self) -> str | None:
        return self.tokens[self.index] if self.index < len(self.tokens) else None

    def take(self, expected: str | None = None) -> str:
        token = self.peek()
        if token is None:
            raise GenerationError(f"{self.path}: unexpected end of source")
        if expected is not None and token != expected:
            raise GenerationError(
                f"{self.path}: expected {expected!r}, found {token!r}"
            )
        self.index += 1
        return token

    def term(self) -> Term:
        if self.peek() == "[":
            self.take("[")
            items: list[Term] = []
            if self.peek() != "]":
                while True:
                    items.append(self.term())
                    if self.peek() != ",":
                        break
                    self.take(",")
            self.take("]")
            return ListTerm(tuple(items))
        head = self.take()
        if head in "[](),.":
            raise GenerationError(f"{self.path}: expected atom, found {head!r}")
        if self.peek() != "(":
            return Atom(head)
        self.take("(")
        arguments: list[Term] = []
        if self.peek() != ")":
            while True:
                arguments.append(self.term())
                if self.peek() != ",":
                    break
                self.take(",")
        self.take(")")
        return Atom(head, tuple(arguments))


def parse_examples(path: Path) -> tuple[tuple[str, Atom], ...]:
    source = "\n".join(
        line.split("%", 1)[0]
        for line in path.read_text(encoding="utf-8").splitlines()
    )
    parser = GroundParser(source, path.as_posix())
    examples: list[tuple[str, Atom]] = []
    while parser.peek() is not None:
        polarity = parser.take()
        if polarity not in {"pos", "neg"}:
            raise GenerationError(
                f"{path}: expected pos or neg, found {polarity!r}"
            )
        parser.take("(")
        term = parser.term()
        parser.take(")")
        parser.take(".")
        if not isinstance(term, Atom) or term.head != "f":
            raise GenerationError(f"{path}: example target is not f")
        examples.append((polarity, term))
    return tuple(examples)


def leaf(term: Term) -> str:
    if not isinstance(term, Atom) or term.args:
        raise GenerationError(f"expected a ground leaf, found {term}")
    return term.head


def list_items(term: Term) -> tuple[str, ...]:
    if not isinstance(term, ListTerm):
        raise GenerationError(f"expected a proper ground list, found {term}")
    return tuple(leaf(item) for item in term.items)


def natural(term: Term) -> int:
    value = leaf(term)
    if not value.isdigit():
        raise GenerationError(f"expected a natural number, found {value!r}")
    return int(value)


def atom_name(value: str) -> str:
    return f"hopper:atom:{'n' + value if value.isdigit() else value}"


def nat_name(value: int | str) -> str:
    number = int(value)
    if number < 0:
        raise GenerationError(f"negative Hopper natural {number}")
    return f"hopper:nat:n{number}"


def render_list(values: Iterable[str], element: str) -> str:
    result = f"(list:nil {element})"
    naming = nat_name if element == "hopper:nat" else atom_name
    for value in reversed(tuple(values)):
        result = f"(list:cons {element} {naming(value)} {result})"
    return result


def target_term(task: str, target: Atom) -> str:
    arguments = target.args
    if task == "dropK" and len(arguments) == 3:
        return (
            f"(hopper:dropk:f {nat_name(natural(arguments[0]))} "
            f"{render_list(list_items(arguments[1]), 'hopper:atom')} "
            f"{render_list(list_items(arguments[2]), 'hopper:atom')})"
        )
    if task == "allEven" and len(arguments) == 1:
        return (
            f"(hopper:all-even:f "
            f"{render_list(list_items(arguments[0]), 'hopper:nat')})"
        )
    if task == "findDup" and len(arguments) == 2:
        return (
            f"(hopper:find-dup:f "
            f"{render_list(list_items(arguments[0]), 'hopper:atom')} "
            f"{atom_name(leaf(arguments[1]))})"
        )
    if task == "length" and len(arguments) == 2:
        return (
            f"(hopper:length:f "
            f"{render_list(list_items(arguments[0]), 'hopper:atom')} "
            f"{nat_name(natural(arguments[1]))})"
        )
    if task == "member" and len(arguments) == 2:
        return (
            f"(hopper:member:f "
            f"{render_list(list_items(arguments[0]), 'hopper:atom')} "
            f"{atom_name(leaf(arguments[1]))})"
        )
    if task == "sorted" and len(arguments) == 1:
        return (
            f"(hopper:sorted:f "
            f"{render_list(list_items(arguments[0]), 'hopper:nat')})"
        )
    if task == "reverse" and len(arguments) == 2:
        return (
            f"(hopper:reverse:f "
            f"{render_list(list_items(arguments[0]), 'hopper:atom')} "
            f"{render_list(list_items(arguments[1]), 'hopper:atom')})"
        )
    raise GenerationError(f"{task}: unsupported target shape {target}")


def proof_count(task: str, target: Atom) -> int:
    args = target.args
    if task == "dropK":
        count = natural(args[0])
        source = list_items(args[1])
        expected = list_items(args[2])
        return int(count <= len(source) and source[count:] == expected)
    if task == "allEven":
        return int(all(int(value) % 2 == 0 for value in list_items(args[0])))
    if task == "findDup":
        values = list_items(args[0])
        answer = leaf(args[1])
        positions = [index for index, value in enumerate(values) if value == answer]
        return len(positions) * (len(positions) - 1) // 2
    if task == "length":
        return int(len(list_items(args[0])) == natural(args[1]))
    if task == "member":
        return list_items(args[0]).count(leaf(args[1]))
    if task == "sorted":
        values = tuple(int(value) for value in list_items(args[0]))
        return int(all(left <= right for left, right in zip(values, values[1:])))
    if task == "reverse":
        return int(tuple(reversed(list_items(args[0]))) == list_items(args[1]))
    raise GenerationError(f"unsupported Hopper task {task}")


def load_sources(
    snapshot_root: Path, repo: Path
) -> tuple[
    dict[str, tuple[tuple[str, Atom], ...]],
    dict,
    tuple[tuple[str, str, Atom, int], ...],
]:
    manifest = corpus.load_manifest(
        repo / "benchmarks/prime/ilp/hopper_table1_manifest.json"
    )
    corpus.validate_manifest(manifest, repo)
    corpus.verify_snapshot(manifest, snapshot_root)
    by_name = {entry["name"]: entry for entry in manifest["tasks"]}
    examples: dict[str, tuple[tuple[str, Atom], ...]] = {}
    disagreements: list[tuple[str, str, Atom, int]] = []
    for task in TASKS:
        variant = by_name[task]["variants"][SOURCE_VARIANT]
        if tuple(variant["target"]["types"]) != EXPECTED_TARGETS[task]:
            raise GenerationError(f"{task}: target type profile changed")
        best = variant["best_program"]
        if best is None or tuple(best["clauses"]) != PROGRAMS[task]:
            raise GenerationError(f"{task}: authored HO best program changed")
        path = snapshot_root / "examples" / task / variant["path"] / "exs.pl"
        parsed = parse_examples(path)
        observed = {
            "positive": sum(polarity == "pos" for polarity, _ in parsed),
            "negative": sum(polarity == "neg" for polarity, _ in parsed),
        }
        if observed != variant["examples"]:
            raise GenerationError(f"{task}: source example count changed")
        for polarity, target in parsed:
            count = proof_count(task, target)
            if (count > 0) != (polarity == "pos"):
                disagreements.append((task, polarity, target, count))
        examples[task] = parsed
    expected_disagreement = (
        "sorted",
        "neg",
        Atom(
            "f",
            (
                ListTerm(
                    (Atom("0"), Atom("0"), Atom("0"), Atom("0"))
                ),
            ),
        ),
        1,
    )
    if tuple(disagreements) != (expected_disagreement,):
        raise GenerationError(
            f"Hopper program/example disagreement inventory changed: {disagreements}"
        )
    return examples, manifest, tuple(disagreements)


def source_atoms(examples: dict[str, tuple[tuple[str, Atom], ...]]) -> set[str]:
    values: set[str] = set()

    def visit(term: Term) -> None:
        if isinstance(term, ListTerm):
            for item in term.items:
                visit(item)
        elif term.args:
            for argument in term.args:
                visit(argument)
        else:
            values.add(term.head)

    for task_examples in examples.values():
        for _polarity, target in task_examples:
            visit(target)
    return values


def render_types(examples: dict[str, tuple[tuple[str, Atom], ...]]) -> str:
    atom_values = sorted(
        source_atoms(examples), key=lambda value: (not value.isdigit(), int(value) if value.isdigit() else value)
    )
    declarations = [
        "; Generated typed vocabulary for Hopper's seven first-order tasks.",
        "; The selected source program is each task's authored HO best program.",
        "; `hopper:atom` and `hopper:nat` keep list elements and counters distinct.",
        "",
        "(: hopper:atom (u 0))",
        "(: hopper:nat (u 0))",
    ]
    declarations.extend(f"(: {atom_name(value)} hopper:atom)" for value in atom_values)
    declarations.extend(f"(: {nat_name(value)} hopper:nat)" for value in range(103))
    declarations.extend(
        [
            "",
            "(: hopper:nat:zero (-> (value : hopper:nat) (u 0)))",
            "(: hopper:nat:succ",
            "  (-> (earlier : hopper:nat) (later : hopper:nat) (u 0)))",
            "(: hopper:nat:pred",
            "  (-> (later : hopper:nat) (earlier : hopper:nat) (u 0)))",
            "(: hopper:nat:even (-> (value : hopper:nat) (u 0)))",
            "(: hopper:nat:geq",
            "  (-> (left : hopper:nat) (right : hopper:nat) (u 0)))",
            "(: hopper:nat:proof:zero (hopper:nat:zero hopper:nat:n0))",
        ]
    )
    declarations.extend(
        f"(: hopper:nat:proof:succ-{value}-{value + 1} "
        f"(hopper:nat:succ {nat_name(value)} {nat_name(value + 1)}))"
        for value in range(102)
    )
    declarations.extend(
        [
            "(: hopper:nat:proof:pred-from-succ",
            "  (-> (later : hopper:nat) (earlier : hopper:nat)",
            "      (successor-evidence : (hopper:nat:succ earlier later))",
            "      (hopper:nat:pred later earlier)))",
            "(: hopper:nat:proof:even-zero (hopper:nat:even hopper:nat:n0))",
            "(: hopper:nat:proof:even-step",
            "  (-> (earlier : hopper:nat) (middle : hopper:nat)",
            "      (value : hopper:nat)",
            "      (earlier-evidence : (hopper:nat:even earlier))",
            "      (first-successor : (hopper:nat:succ earlier middle))",
            "      (second-successor : (hopper:nat:succ middle value))",
            "      (hopper:nat:even value)))",
            "(: hopper:nat:proof:geq-zero",
            "  (-> (value : hopper:nat) (hopper:nat:geq value hopper:nat:n0)))",
            "(: hopper:nat:proof:geq-step",
            "  (-> (left-earlier : hopper:nat) (left-later : hopper:nat)",
            "      (right-earlier : hopper:nat) (right-later : hopper:nat)",
            "      (left-successor : (hopper:nat:succ left-earlier left-later))",
            "      (right-successor : (hopper:nat:succ right-earlier right-later))",
            "      (earlier-evidence : (hopper:nat:geq left-earlier right-earlier))",
            "      (hopper:nat:geq left-later right-later)))",
            "",
            "(: hopper:atom-list:tail",
            "  (-> (source : (list hopper:atom))",
            "      (target : (list hopper:atom)) (u 0)))",
            "(: hopper:atom-list:tail:proof",
            "  (-> (head : hopper:atom) (tail : (list hopper:atom))",
            "      (evidence :",
            "        (rel:list:tail hopper:atom",
            "          (list:cons hopper:atom head tail) tail))",
            "      (hopper:atom-list:tail",
            "        (list:cons hopper:atom head tail) tail)))",
            "(: hopper:atom-list:head",
            "  (-> (source : (list hopper:atom))",
            "      (target : hopper:atom) (u 0)))",
            "(: hopper:atom-list:head:proof",
            "  (-> (head : hopper:atom) (tail : (list hopper:atom))",
            "      (evidence :",
            "        (rel:list:head hopper:atom",
            "          (list:cons hopper:atom head tail) head))",
            "      (hopper:atom-list:head",
            "        (list:cons hopper:atom head tail) head)))",
            "",
            "(: hopper:dropk:f",
            "  (-> (count : hopper:nat) (source : (list hopper:atom))",
            "      (target : (list hopper:atom)) (u 0)))",
            "(: hopper:dropk:proof",
            "  (-> (count : hopper:nat) (source : (list hopper:atom))",
            "      (target : (list hopper:atom))",
            "      (evidence :",
            "        (rel:iterate (list hopper:atom) hopper:nat",
            "          hopper:atom-list:tail hopper:nat:pred hopper:nat:n0",
            "          source count target))",
            "      (hopper:dropk:f count source target)))",
            "",
            "(: hopper:all-even:f (-> (values : (list hopper:nat)) (u 0)))",
            "(: hopper:all-even:proof",
            "  (-> (values : (list hopper:nat))",
            "      (evidence : (rel:all hopper:nat hopper:nat:even values))",
            "      (hopper:all-even:f values)))",
            "",
            "(: hopper:find-dup:nil-case (-> (answer : hopper:atom) (u 0)))",
            "(: hopper:find-dup:cons-case",
            "  (-> (head : hopper:atom) (tail : (list hopper:atom))",
            "      (answer : hopper:atom) (u 0)))",
            "(: hopper:find-dup:proof:recursive",
            "  (-> (head : hopper:atom) (tail : (list hopper:atom))",
            "      (answer : hopper:atom)",
            "      (evidence :",
            "        (rel:case-list hopper:atom hopper:atom",
            "          hopper:find-dup:nil-case hopper:find-dup:cons-case",
            "          tail answer))",
            "      (hopper:find-dup:cons-case head tail answer)))",
            "(: hopper:find-dup:proof:duplicate",
            "  (-> (head : hopper:atom) (tail : (list hopper:atom))",
            "      (member-evidence : (rel:list:member hopper:atom tail head))",
            "      (hopper:find-dup:cons-case head tail head)))",
            "(: hopper:find-dup:f",
            "  (-> (values : (list hopper:atom)) (answer : hopper:atom) (u 0)))",
            "(: hopper:find-dup:proof",
            "  (-> (values : (list hopper:atom)) (answer : hopper:atom)",
            "      (evidence :",
            "        (rel:case-list hopper:atom hopper:atom",
            "          hopper:find-dup:nil-case hopper:find-dup:cons-case",
            "          values answer))",
            "      (hopper:find-dup:f values answer)))",
            "",
            "(: hopper:length:step",
            "  (-> (before : hopper:nat) (head : hopper:atom)",
            "      (after : hopper:nat) (u 0)))",
            "(: hopper:length:step-proof",
            "  (-> (before : hopper:nat) (head : hopper:atom)",
            "      (after : hopper:nat)",
            "      (successor-evidence : (hopper:nat:succ before after))",
            "      (hopper:length:step before head after)))",
            "(: hopper:length:f",
            "  (-> (values : (list hopper:atom)) (length : hopper:nat) (u 0)))",
            "(: hopper:length:proof",
            "  (-> (values : (list hopper:atom)) (length : hopper:nat)",
            "      (evidence :",
            "        (rel:fold hopper:atom hopper:nat hopper:length:step",
            "          hopper:nat:n0 values length))",
            "      (hopper:length:f values length)))",
            "",
            "(: hopper:member:f",
            "  (-> (values : (list hopper:atom)) (answer : hopper:atom) (u 0)))",
            "(: hopper:member:proof",
            "  (-> (values : (list hopper:atom)) (answer : hopper:atom)",
            "      (evidence :",
            "        (rel:any hopper:atom hopper:atom",
            "          hopper:atom-list:head values answer))",
            "      (hopper:member:f values answer)))",
            "",
            "(: hopper:sorted:step",
            "  (-> (before : hopper:nat) (head : hopper:nat)",
            "      (after : hopper:nat) (u 0)))",
            "(: hopper:sorted:step-proof",
            "  (-> (before : hopper:nat) (head : hopper:nat)",
            "      (after : hopper:nat)",
            "      (successor-evidence : (hopper:nat:succ head after))",
            "      (order-evidence : (hopper:nat:geq after before))",
            "      (hopper:sorted:step before head after)))",
            "(: hopper:sorted:f (-> (values : (list hopper:nat)) (u 0)))",
            "(: hopper:sorted:proof",
            "  (-> (values : (list hopper:nat)) (after : hopper:nat)",
            "      (evidence :",
            "        (rel:fold hopper:nat hopper:nat hopper:sorted:step",
            "          hopper:nat:n0 values after))",
            "      (hopper:sorted:f values)))",
            "",
            "(: hopper:reverse:step",
            "  (-> (before : (list hopper:atom)) (head : hopper:atom)",
            "      (after : (list hopper:atom)) (u 0)))",
            "(: hopper:reverse:step-proof",
            "  (-> (before : (list hopper:atom)) (head : hopper:atom)",
            "      (head-evidence :",
            "        (rel:list:head hopper:atom",
            "          (list:cons hopper:atom head before) head))",
            "      (tail-evidence :",
            "        (rel:list:tail hopper:atom",
            "          (list:cons hopper:atom head before) before))",
            "      (hopper:reverse:step before head",
            "        (list:cons hopper:atom head before))))",
            "(: hopper:reverse:f",
            "  (-> (source : (list hopper:atom))",
            "      (target : (list hopper:atom)) (u 0)))",
            "(: hopper:reverse:proof",
            "  (-> (source : (list hopper:atom))",
            "      (target : (list hopper:atom))",
            "      (evidence :",
            "        (rel:fold hopper:atom (list hopper:atom) hopper:reverse:step",
            "          (list:nil hopper:atom) source target))",
            "      (hopper:reverse:f source target)))",
            "",
        ]
    )
    return "\n".join(declarations)


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules() -> str:
    blocks = [
        block(
            [
                "(rm-block list-empty hopper:list:empty",
                "  (quote (rel:list:empty-proof $element))",
                "  rm-nil",
                "  (quote (rel:list:empty $element (list:nil $element))))",
            ]
        ),
        block(
            [
                "(rm-block list-head hopper:list:head",
                "  (quote (rel:list:head-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:head $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-tail hopper:list:tail",
                "  (quote (rel:list:tail-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block atom-list-tail hopper:atom-list:tail",
                "  (quote (hopper:atom-list:tail:proof $head $tail",
                "    (unquote $evidence)))",
                "  (rm-cons",
                "    (rm-premise $evidence",
                "      (quote (rel:list:tail hopper:atom",
                "        (list:cons hopper:atom $head $tail) $tail)))",
                "    rm-nil)",
                "  (quote (hopper:atom-list:tail",
                "    (list:cons hopper:atom $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block atom-list-head hopper:atom-list:head",
                "  (quote (hopper:atom-list:head:proof $head $tail",
                "    (unquote $evidence)))",
                "  (rm-cons",
                "    (rm-premise $evidence",
                "      (quote (rel:list:head hopper:atom",
                "        (list:cons hopper:atom $head $tail) $head)))",
                "    rm-nil)",
                "  (quote (hopper:atom-list:head",
                "    (list:cons hopper:atom $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-member-here hopper:list:member-here",
                "  (quote (rel:list:member-here $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:member $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-member-there hopper:list:member-there",
                "  (quote (rel:list:member-there $element $head $tail",
                "    $member (unquote $tail-evidence)))",
                "  (rm-cons",
                "    (rm-premise $tail-evidence",
                "      (quote (rel:list:member $element $tail $member)))",
                "    rm-nil)",
                "  (quote (rel:list:member $element",
                "    (list:cons $element $head $tail) $member)))",
            ]
        ),
        fact_block(
            "nat-zero",
            "hopper:nat:zero",
            "hopper:nat:proof:zero",
            "(hopper:nat:zero hopper:nat:n0)",
        ),
    ]
    blocks.extend(
        fact_block(
            f"nat-succ-{value}-{value + 1}",
            f"hopper:nat:succ-{value}-{value + 1}",
            f"hopper:nat:proof:succ-{value}-{value + 1}",
            f"(hopper:nat:succ {nat_name(value)} {nat_name(value + 1)})",
        )
        for value in range(102)
    )
    blocks.extend(
        [
            block(
                [
                    "(rm-block nat-pred hopper:nat:pred",
                    "  (quote (hopper:nat:proof:pred-from-succ",
                    "    $later $earlier (unquote $successor-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $successor-evidence",
                    "      (quote (hopper:nat:succ $earlier $later)))",
                    "    rm-nil)",
                    "  (quote (hopper:nat:pred $later $earlier)))",
                ]
            ),
            fact_block(
                "nat-even-zero",
                "hopper:nat:even-zero",
                "hopper:nat:proof:even-zero",
                "(hopper:nat:even hopper:nat:n0)",
            ),
            block(
                [
                    "(rm-block nat-even-step hopper:nat:even-step",
                    "  (quote (hopper:nat:proof:even-step",
                    "    $earlier $middle $value",
                    "    (unquote $earlier-evidence)",
                    "    (unquote $first-successor)",
                    "    (unquote $second-successor)))",
                    "  (rm-cons",
                    "    (rm-premise $earlier-evidence",
                    "      (quote (hopper:nat:even $earlier)))",
                    "    (rm-cons",
                    "      (rm-premise $first-successor",
                    "        (quote (hopper:nat:succ $earlier $middle)))",
                    "      (rm-cons",
                    "        (rm-premise $second-successor",
                    "          (quote (hopper:nat:succ $middle $value)))",
                    "        rm-nil)))",
                    "  (quote (hopper:nat:even $value)))",
                ]
            ),
            block(
                [
                    "(rm-block nat-geq-zero hopper:nat:geq-zero",
                    "  (quote (hopper:nat:proof:geq-zero $value))",
                    "  rm-nil",
                    "  (quote (hopper:nat:geq $value hopper:nat:n0)))",
                ]
            ),
            block(
                [
                    "(rm-block nat-geq-step hopper:nat:geq-step",
                    "  (quote (hopper:nat:proof:geq-step",
                    "    $left-earlier $left-later $right-earlier $right-later",
                    "    (unquote $left-successor) (unquote $right-successor)",
                    "    (unquote $earlier-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $left-successor",
                    "      (quote (hopper:nat:succ $left-earlier $left-later)))",
                    "    (rm-cons",
                    "      (rm-premise $right-successor",
                    "        (quote (hopper:nat:succ $right-earlier $right-later)))",
                    "      (rm-cons",
                    "        (rm-premise $earlier-evidence",
                    "          (quote (hopper:nat:geq $left-earlier $right-earlier)))",
                    "        rm-nil)))",
                    "  (quote (hopper:nat:geq $left-later $right-later)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-zero rel:iterate-zero",
                    "  (quote (rel:iterate:zero $value $counter $step",
                    "    $predecessor $zero $source))",
                    "  rm-nil",
                    "  (quote (rel:iterate $value $counter $step $predecessor",
                    "    $zero $source $zero $source)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-step rel:iterate-step",
                    "  (quote (rel:iterate:step $value $counter $step",
                    "    $predecessor $zero $source $next $later $earlier $target",
                    "    (unquote $predecessor-evidence)",
                    "    (unquote $step-evidence)",
                    "    (unquote $recursive-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $predecessor-evidence",
                    "      (quote ($predecessor $later $earlier)))",
                    "    (rm-cons",
                    "      (rm-premise $step-evidence",
                    "        (quote ($step $source $next)))",
                    "      (rm-cons",
                    "        (rm-premise $recursive-evidence",
                    "          (quote (rel:iterate $value $counter $step",
                    "            $predecessor $zero $next $earlier $target)))",
                    "        rm-nil)))",
                    "  (quote (rel:iterate $value $counter $step $predecessor",
                    "    $zero $source $later $target)))",
                ]
            ),
            block(
                [
                    "(rm-block all-nil rel:all-nil",
                    "  (quote (rel:all:nil $element $predicate))",
                    "  rm-nil",
                    "  (quote (rel:all $element $predicate (list:nil $element))))",
                ]
            ),
            block(
                [
                    "(rm-block all-cons rel:all-cons",
                    "  (quote (rel:all:cons $element $predicate $head $tail",
                    "    (unquote $head-evidence) (unquote $tail-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $head-evidence (quote ($predicate $head)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:all $element $predicate $tail)))",
                    "      rm-nil))",
                    "  (quote (rel:all $element $predicate",
                    "    (list:cons $element $head $tail))))",
                ]
            ),
            block(
                [
                    "(rm-block any-here rel:any-here",
                    "  (quote (rel:any:here $element $target $predicate",
                    "    $values $answer (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence (quote ($predicate $values $answer)))",
                    "    rm-nil)",
                    "  (quote (rel:any $element $target $predicate $values $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block any-there rel:any-there",
                    "  (quote (rel:any:there $element $target $predicate",
                    "    $head $tail $answer (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:any $element $target $predicate $tail $answer)))",
                    "    rm-nil)",
                    "  (quote (rel:any $element $target $predicate",
                    "    (list:cons $element $head $tail) $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block case-list-nil rel:case-list-nil",
                    "  (quote (rel:case-list:nil $element $target $nil-case",
                    "    $cons-case $answer (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence (quote ($nil-case $answer)))",
                    "    rm-nil)",
                    "  (quote (rel:case-list $element $target $nil-case $cons-case",
                    "    (list:nil $element) $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block case-list-cons rel:case-list-cons",
                    "  (quote (rel:case-list:cons $element $target $nil-case",
                    "    $cons-case $head $tail $answer (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote ($cons-case $head $tail $answer)))",
                    "    rm-nil)",
                    "  (quote (rel:case-list $element $target $nil-case $cons-case",
                    "    (list:cons $element $head $tail) $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block fold-nil rel:fold-nil",
                    "  (quote (rel:fold:nil $element $accumulator $step $before))",
                    "  rm-nil",
                    "  (quote (rel:fold $element $accumulator $step $before",
                    "    (list:nil $element) $before)))",
                ]
            ),
            block(
                [
                    "(rm-block fold-cons rel:fold-cons",
                    "  (quote (rel:fold:cons $element $accumulator $step",
                    "    $before $head $tail $next $after",
                    "    (unquote $step-evidence) (unquote $tail-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $step-evidence",
                    "      (quote ($step $before $head $next)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:fold $element $accumulator $step",
                    "          $next $tail $after)))",
                    "      rm-nil))",
                    "  (quote (rel:fold $element $accumulator $step $before",
                    "    (list:cons $element $head $tail) $after)))",
                ]
            ),
            block(
                [
                    "(rm-block find-dup-recursive hopper:find-dup:recursive",
                    "  (quote (hopper:find-dup:proof:recursive $head $tail $answer",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:case-list hopper:atom hopper:atom",
                    "        hopper:find-dup:nil-case hopper:find-dup:cons-case",
                    "        $tail $answer)))",
                    "    rm-nil)",
                    "  (quote (hopper:find-dup:cons-case $head $tail $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block find-dup-here hopper:find-dup:duplicate",
                    "  (quote (hopper:find-dup:proof:duplicate $head $tail",
                    "    (unquote $member-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $member-evidence",
                    "      (quote (rel:list:member hopper:atom $tail $head)))",
                    "    rm-nil)",
                    "  (quote (hopper:find-dup:cons-case $head $tail $head)))",
                ]
            ),
            block(
                [
                    "(rm-block length-step hopper:length:step",
                    "  (quote (hopper:length:step-proof $before $head $after",
                    "    (unquote $successor-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $successor-evidence",
                    "      (quote (hopper:nat:succ $before $after)))",
                    "    rm-nil)",
                    "  (quote (hopper:length:step $before $head $after)))",
                ]
            ),
            block(
                [
                    "(rm-block sorted-step hopper:sorted:step",
                    "  (quote (hopper:sorted:step-proof $before $head $after",
                    "    (unquote $successor-evidence) (unquote $order-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $successor-evidence",
                    "      (quote (hopper:nat:succ $head $after)))",
                    "    (rm-cons",
                    "      (rm-premise $order-evidence",
                    "        (quote (hopper:nat:geq $after $before)))",
                    "      rm-nil))",
                    "  (quote (hopper:sorted:step $before $head $after)))",
                ]
            ),
            block(
                [
                    "(rm-block reverse-step hopper:reverse:step",
                    "  (quote (hopper:reverse:step-proof $before $head",
                    "    (rel:list:head-proof hopper:atom $head $before)",
                    "    (rel:list:tail-proof hopper:atom $head $before)))",
                    "  rm-nil",
                    "  (quote (hopper:reverse:step $before $head",
                    "    (list:cons hopper:atom $head $before))))",
                ]
            ),
            block(
                [
                    "(rm-block target-dropk hopper:target:dropk",
                    "  (quote (hopper:dropk:proof $count $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
            "    (rm-premise $evidence",
            "      (quote (rel:iterate (list hopper:atom) hopper:nat",
            "        hopper:atom-list:tail hopper:nat:pred hopper:nat:n0",
                    "        $source $count $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:dropk:f $count $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block target-all-even hopper:target:all-even",
                    "  (quote (hopper:all-even:proof $values (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:all hopper:nat hopper:nat:even $values)))",
                    "    rm-nil)",
                    "  (quote (hopper:all-even:f $values)))",
                ]
            ),
            block(
                [
                    "(rm-block target-find-dup hopper:target:find-dup",
                    "  (quote (hopper:find-dup:proof $values $answer",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:case-list hopper:atom hopper:atom",
                    "        hopper:find-dup:nil-case hopper:find-dup:cons-case",
                    "        $values $answer)))",
                    "    rm-nil)",
                    "  (quote (hopper:find-dup:f $values $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block target-length hopper:target:length",
                    "  (quote (hopper:length:proof $values $length",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:fold hopper:atom hopper:nat hopper:length:step",
                    "        hopper:nat:n0 $values $length)))",
                    "    rm-nil)",
                    "  (quote (hopper:length:f $values $length)))",
                ]
            ),
            block(
                [
                    "(rm-block target-member hopper:target:member",
                    "  (quote (hopper:member:proof $values $answer",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
            "    (rm-premise $evidence",
            "      (quote (rel:any hopper:atom hopper:atom",
            "        hopper:atom-list:head $values $answer)))",
                    "    rm-nil)",
                    "  (quote (hopper:member:f $values $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block target-sorted hopper:target:sorted",
                    "  (quote (hopper:sorted:proof $values $after",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:fold hopper:nat hopper:nat hopper:sorted:step",
                    "        hopper:nat:n0 $values $after)))",
                    "    rm-nil)",
                    "  (quote (hopper:sorted:f $values)))",
                ]
            ),
            block(
                [
                    "(rm-block target-reverse hopper:target:reverse",
                    "  (quote (hopper:reverse:proof $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:fold hopper:atom (list hopper:atom)",
                    "        hopper:reverse:step (list:nil hopper:atom)",
                    "        $source $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:reverse:f $source $target)))",
                ]
            ),
        ]
    )
    return "\n".join(
        [
            "; Generated proof-producing realization of seven Hopper HO programs.",
            "; The generic combinator blocks keep predicate arguments first-class.",
            "; Specialization preserves their proof constructors and source identity.",
            "",
            "(= (hopper:table1:first-order:package)",
            "  (compile:rule-package hopper-table1-first-order-ho-v1",
            "    (rm-package",
            *blocks,
            "    )))",
            "",
        ]
    )


def render_fixture(
    examples: dict[str, tuple[tuple[str, Atom], ...]], manifest: dict
) -> tuple[str, str, dict[str, dict[str, int]]]:
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    fixture = [
        "; Exact proof-relevant qualification for Hopper's first-order task group.",
        "; Each task uses its authored HO best program from the pinned snapshot.",
        "; Source header-count anomalies remain ledgered; examples are authoritative.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_rules.metta)",
        "",
        "(= (hopper:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (hopper:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (hopper:table1:first-order:package)",
        "      512 10000000 4096 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks (hopper:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (hopper:case $name not-derived $count $checks)",
        "            (hopper:case $name derived $count $checks))))))",
        "",
    ]
    expected = ["[()]", "[()]", "[()]", "[()]"]
    counts: dict[str, dict[str, int]] = {}
    for task in TASKS:
        source_positive = 0
        source_negative = 0
        derived = 0
        not_derived = 0
        proof_occurrences = 0
        label_disagreements = 0
        for ordinal, (polarity, target) in enumerate(examples[task], 1):
            count = proof_count(task, target)
            name = f"hopper:{task.lower()}:ho:{polarity}-{ordinal}"
            goal = target_term(task, target)
            fixture.extend(
                [
                    f"!(hopper:classify {name}",
                    f"  (quote {goal}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(hopper:case {name} derived {count} ({checks}))]"
                )
                derived += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(hopper:case {name} not-derived 0 ())]"
                )
                not_derived += 1
            source_positive += int(polarity == "pos")
            source_negative += int(polarity == "neg")
            label_disagreements += int((count > 0) != (polarity == "pos"))
        source_counts = entries[task]["variants"][SOURCE_VARIANT]["examples"]
        if (
            source_positive != source_counts["positive"]
            or source_negative != source_counts["negative"]
        ):
            raise GenerationError(f"{task}: source example counts changed")
        counts[task] = {
            "source_positive": source_positive,
            "source_negative": source_negative,
            "derived": derived,
            "not_derived": not_derived,
            "proof_occurrences": proof_occurrences,
            "label_disagreements": label_disagreements,
        }

    duplicate_list = render_list(("7", "7", "7"), "hopper:atom")
    fixture.extend(
        [
            "",
            "; A non-source canary makes `any` occurrence multiplicity observable.",
            "!(hopper:classify hopper:member:multiplicity-canary",
            f"  (quote (hopper:member:f {duplicate_list} {atom_name('7')})))",
        ]
    )
    expected.append(
        "[(hopper:case hopper:member:multiplicity-canary derived 3 "
        "(True True True))]"
    )
    return "\n".join(fixture) + "\n", "\n".join(expected) + "\n", counts


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_first_order_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_first_order_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_first_order_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_first_order_ground_truth.expected",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        examples, manifest, disagreements = load_sources(
            args.snapshot_root, repo
        )
        fixture, expected, counts = render_fixture(examples, manifest)
        materialize_outputs(
            (
                (args.types_output, render_types(examples)),
                (args.rules_output, render_rules()),
                (args.fixture_output, fixture),
                (args.expected_output, expected),
            ),
            args.check,
        )
    except (GenerationError, corpus.ManifestError, KeyError, OSError) as exc:
        print(f"FAIL: Hopper first-order generation: {exc}", file=sys.stderr)
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Hopper first-order qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences, "
        f"{len(disagreements)} pinned source disagreement"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
