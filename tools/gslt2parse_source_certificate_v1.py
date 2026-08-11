#!/usr/bin/env python3
"""Project one GSLT2Parse result to a recheckable source certificate.

The projector is deliberately untrusted.  It reads generated grammar data,
aligns the parser's semantic production tree with that data, and emits an
ordered token ledger plus a ``NodeC`` certificate.  The generic inference
checker must replay the emitted certificate before any downstream consumer
may use it.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gslt2parse_schema_v1 import (  # noqa: E402
    SExpr,
    SchemaError,
    StringLiteral,
    Symbol,
    parse_sexprs,
    render,
)


class ProjectionError(ValueError):
    pass


@dataclass(frozen=True)
class Production:
    label: str
    sort: str
    items: tuple[tuple[str, str], ...]


@dataclass(frozen=True)
class LexicalBinding:
    sort: str
    parser_node: str
    class_name: str


@dataclass(frozen=True)
class GrammarData:
    productions: dict[str, Production]
    lexical_by_sort: dict[str, LexicalBinding]


@dataclass(frozen=True)
class Lowered:
    certificate: SExpr
    tokens: tuple[SExpr, ...]
    next_index: int


def symbol(term: SExpr, where: str) -> str:
    if not isinstance(term, Symbol) or not term.text:
        raise ProjectionError(f"{where}: expected symbol, got {render(term)}")
    return term.text


def form(term: SExpr, head: str, arity: int | None, where: str) -> tuple[SExpr, ...]:
    if not isinstance(term, tuple) or not term:
        raise ProjectionError(f"{where}: expected ({head} ...), got {render(term)}")
    if symbol(term[0], f"{where} head") != head:
        raise ProjectionError(f"{where}: expected ({head} ...), got {render(term)}")
    if arity is not None and len(term) != arity + 1:
        raise ProjectionError(
            f"{where}: expected {head}/{arity}, got {len(term) - 1} arguments"
        )
    return term


def definition_body(forms: list[SExpr], name: str) -> SExpr:
    matches: list[SExpr] = []
    for raw in forms:
        if not isinstance(raw, tuple) or len(raw) != 3:
            continue
        if not isinstance(raw[0], Symbol) or raw[0].text != "=":
            continue
        lhs = raw[1]
        if (
            isinstance(lhs, tuple)
            and len(lhs) == 1
            and isinstance(lhs[0], Symbol)
            and lhs[0].text == name
        ):
            matches.append(raw[2])
    if len(matches) != 1:
        raise ProjectionError(
            f"expected exactly one nullary definition {name}, found {len(matches)}"
        )
    return matches[0]


def grammar_data(path: Path, grammar_name: str, bindings_name: str) -> GrammarData:
    try:
        forms = parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    except (OSError, UnicodeError, SchemaError) as error:
        raise ProjectionError(f"cannot read generated grammar: {error}") from error

    grammar = form(definition_body(forms, grammar_name), "mkGram", 1, grammar_name)
    raw_entries = grammar[1]
    if not isinstance(raw_entries, tuple):
        raise ProjectionError(f"{grammar_name}: expected a grammar entry list")

    productions: dict[str, Production] = {}
    lexical_classes: dict[str, str] = {}
    for index, entry in enumerate(raw_entries):
        where = f"{grammar_name} entry {index}"
        if not isinstance(entry, tuple) or not entry:
            raise ProjectionError(f"{where}: malformed entry")
        tag = symbol(entry[0], f"{where} tag")
        if tag == "LexD":
            lexical = form(entry, "LexD", 2, where)
            class_name = symbol(lexical[1], f"{where} class")
            sort = symbol(lexical[2], f"{where} sort")
            if sort in lexical_classes:
                raise ProjectionError(f"{where}: duplicate lexical sort {sort}")
            lexical_classes[sort] = class_name
        elif tag == "VarD":
            raise ProjectionError(
                f"{where}: literal lexical declarations are not supported by v1"
            )
        elif tag == "Prod":
            production = form(entry, "Prod", 3, where)
            label = symbol(production[1], f"{where} label")
            sort = symbol(production[2], f"{where} sort")
            raw_items = production[3]
            if not isinstance(raw_items, tuple):
                raise ProjectionError(f"{where}: expected a production item list")
            items: list[tuple[str, str]] = []
            for item_index, raw_item in enumerate(raw_items):
                item_where = f"{where} item {item_index}"
                if not isinstance(raw_item, tuple) or len(raw_item) != 2:
                    raise ProjectionError(f"{item_where}: malformed grammar item")
                kind = symbol(raw_item[0], f"{item_where} kind")
                if kind not in {"Tm", "Hl"}:
                    raise ProjectionError(
                        f"{item_where}: unsupported grammar item {kind}"
                    )
                items.append((kind, symbol(raw_item[1], f"{item_where} value")))
            if label in productions:
                raise ProjectionError(f"{where}: duplicate production label {label}")
            productions[label] = Production(label, sort, tuple(items))
        else:
            raise ProjectionError(f"{where}: unsupported grammar entry {tag}")

    raw_bindings = definition_body(forms, bindings_name)
    if not isinstance(raw_bindings, tuple):
        raise ProjectionError(f"{bindings_name}: expected a binding list")
    lexical_by_sort: dict[str, LexicalBinding] = {}
    for index, raw_binding in enumerate(raw_bindings):
        where = f"{bindings_name} entry {index}"
        binding = form(raw_binding, "ParserLexicalBinding", 3, where)
        sort = symbol(binding[1], f"{where} sort")
        parser_node = symbol(binding[2], f"{where} parser node")
        class_name = symbol(binding[3], f"{where} class")
        if sort in lexical_by_sort:
            raise ProjectionError(f"{where}: duplicate parser lexical sort {sort}")
        if lexical_classes.get(sort) != class_name:
            raise ProjectionError(
                f"{where}: parser binding disagrees with LexD for {sort}"
            )
        lexical_by_sort[sort] = LexicalBinding(sort, parser_node, class_name)

    if not productions:
        raise ProjectionError(f"{grammar_name}: grammar has no productions")
    if set(lexical_classes) != set(lexical_by_sort):
        missing = sorted(set(lexical_classes) ^ set(lexical_by_sort))
        raise ProjectionError(
            f"lexical declaration/binding domains differ: {', '.join(missing)}"
        )
    return GrammarData(productions, lexical_by_sort)


def stream_result(path: Path) -> tuple[SExpr, dict[str, str]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise ProjectionError(f"cannot read parser stream: {error}") from error
    metadata: dict[str, str] = {}
    results: list[SExpr] = []
    for line_number, line in enumerate(lines, 1):
        if "\t" not in line:
            continue
        key, value = line.split("\t", 1)
        if key == "result":
            try:
                terms = parse_sexprs(
                    value, source=f"{path}: result line {line_number}"
                )
            except SchemaError as error:
                raise ProjectionError(str(error)) from error
            if len(terms) != 1:
                raise ProjectionError(
                    f"{path}: result line {line_number} has {len(terms)} terms"
                )
            results.append(terms[0])
        elif key in metadata:
            raise ProjectionError(f"{path}: duplicate metadata field {key}")
        else:
            metadata[key] = value
    required = {
        "outcome": "completed",
        "decision": "accepted",
        "source-passes": "1",
    }
    for key, expected in required.items():
        if metadata.get(key) != expected:
            raise ProjectionError(
                f"{path}: expected {key}={expected}, got {metadata.get(key)!r}"
            )
    if len(results) != 1:
        raise ProjectionError(f"{path}: expected one semantic result, found {len(results)}")
    return results[0], metadata


def codepoint_list(term: SExpr, where: str) -> str:
    codepoints: list[int] = []
    current = term
    while isinstance(current, tuple) and current:
        item = form(current, "cons", 2, where)
        cp = form(item[1], "cp", 1, f"{where} codepoint")[1]
        if not isinstance(cp, int) or cp < 0 or cp > 0x10FFFF:
            raise ProjectionError(f"{where}: invalid Unicode scalar {render(cp)}")
        if 0xD800 <= cp <= 0xDFFF:
            raise ProjectionError(f"{where}: surrogate is not a Unicode scalar")
        codepoints.append(cp)
        current = item[2]
    if not isinstance(current, Symbol) or current.text != "nil":
        raise ProjectionError(f"{where}: malformed codepoint list")
    if not codepoints:
        raise ProjectionError(f"{where}: lexical token is empty")
    return "".join(chr(codepoint) for codepoint in codepoints)


def semantic_children(value: SExpr, count: int, where: str) -> list[SExpr]:
    if count == 0:
        if not isinstance(value, Symbol) or value.text != "nil":
            raise ProjectionError(f"{where}: zero-child production retained a value")
        return []
    if count == 1:
        return [value]
    pair = form(value, "pair", 2, where)
    return [pair[1], *semantic_children(pair[2], count - 1, where)]


def llist(items: tuple[SExpr, ...] | list[SExpr]) -> SExpr:
    result: SExpr = Symbol("Nil")
    for item in reversed(items):
        result = (Symbol("Cons"), item, result)
    return result


def lower_lexical(
    data: GrammarData, expected_sort: str, term: SExpr, index: int, where: str
) -> Lowered:
    binding = data.lexical_by_sort[expected_sort]
    node = form(term, "node", 2, where)
    observed_node = symbol(node[1], f"{where} node label")
    if observed_node != binding.parser_node:
        raise ProjectionError(
            f"{where}: expected lexical node {binding.parser_node}, got {observed_node}"
        )
    text = codepoint_list(node[2], f"{where} value")
    token: SExpr = (
        Symbol("Lex"),
        Symbol(binding.class_name),
        StringLiteral(text),
    )
    return Lowered(
        (Symbol("LeafC"), token, index, index + 1),
        (token,),
        index + 1,
    )


def lower_node(
    data: GrammarData, expected_sort: str, term: SExpr, index: int, where: str
) -> Lowered:
    if expected_sort in data.lexical_by_sort:
        return lower_lexical(data, expected_sort, term, index, where)

    node = form(term, "node", 2, where)
    label = symbol(node[1], f"{where} production label")
    production = data.productions.get(label)
    if production is None:
        raise ProjectionError(f"{where}: unknown production label {label}")
    if production.sort != expected_sort:
        raise ProjectionError(
            f"{where}: production {label} has sort {production.sort}, "
            f"expected {expected_sort}"
        )
    nonterminals = [item for item in production.items if item[0] == "Hl"]
    retained = semantic_children(node[2], len(nonterminals), where)
    retained_index = 0
    cursor = index
    tokens: list[SExpr] = []
    certificates: list[SExpr] = []
    for item_index, (kind, value) in enumerate(production.items):
        item_where = f"{where}/{label}[{item_index}]"
        if kind == "Tm":
            token = Symbol(value)
            tokens.append(token)
            certificates.append((Symbol("TokC"), token, cursor))
            cursor += 1
        else:
            child = lower_node(
                data,
                value,
                retained[retained_index],
                cursor,
                item_where,
            )
            retained_index += 1
            tokens.extend(child.tokens)
            certificates.append(child.certificate)
            cursor = child.next_index
    if not production.items:
        certificates.append((Symbol("EpsC"),))
    certificate: SExpr = (
        Symbol("NodeC"),
        Symbol(label),
        Symbol(expected_sort),
        index,
        cursor,
        llist(certificates),
    )
    return Lowered(certificate, tuple(tokens), cursor)


def unwrap_result(term: SExpr, root_node: str) -> SExpr:
    result = form(term, "result", 2, "parser result")
    if not isinstance(result[2], Symbol) or result[2].text != "nil":
        raise ProjectionError("parser result retained unconsumed input")
    root = form(result[1], "node", 2, "parser result root")
    if symbol(root[1], "parser result root label") != root_node:
        raise ProjectionError(f"parser result root is not {root_node}")
    return root[2]


def digest_file(path: Path, what: str) -> tuple[str, int]:
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ProjectionError(f"cannot read {what}: {error}") from error
    return sha256(payload).hexdigest(), len(payload)


def output_text(
    definition: str,
    source_digest: str,
    source_bytes: int,
    abi_digest: str,
    forest_digest: str,
    lowered: Lowered,
) -> str:
    certificate: SExpr = (
        Symbol("GSLT2ParseSourceCertificateV1"),
        StringLiteral(source_digest),
        source_bytes,
        StringLiteral(abi_digest),
        StringLiteral(forest_digest),
        llist(lowered.tokens),
        lowered.certificate,
    )
    return (
        "; Generated from a GSLT2Parse semantic result.\n"
        "; Recheck this certificate with the generic inference checker.\n\n"
        f"(= ({definition})\n  {render(certificate)})\n"
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--grammar", type=Path, required=True)
    parser.add_argument("--grammar-definition", required=True)
    parser.add_argument("--bindings-definition", required=True)
    parser.add_argument("--parser-output", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--abi", type=Path, required=True)
    parser.add_argument("--start-sort", required=True)
    parser.add_argument("--root-node", required=True)
    parser.add_argument("--definition", required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        data = grammar_data(
            arguments.grammar,
            arguments.grammar_definition,
            arguments.bindings_definition,
        )
        semantic_result, metadata = stream_result(arguments.parser_output)
        source_digest, source_bytes = digest_file(arguments.source, "source")
        abi_digest, _ = digest_file(arguments.abi, "ParserPack ABI")
        reported_bytes = metadata.get("input-bytes")
        if reported_bytes is None or int(reported_bytes) != source_bytes:
            raise ProjectionError(
                "parser input byte count disagrees with the source artifact"
            )
        forest_digest = metadata.get("forest-digest")
        if forest_digest is None or len(forest_digest) != 64:
            raise ProjectionError("parser output has no canonical forest digest")
        root = unwrap_result(semantic_result, arguments.root_node)
        lowered = lower_node(data, arguments.start_sort, root, 0, "root")
        if lowered.next_index != len(lowered.tokens):
            raise ProjectionError("certificate token boundaries are inconsistent")
        arguments.output.write_text(
            output_text(
                arguments.definition,
                source_digest,
                source_bytes,
                abi_digest,
                forest_digest,
                lowered,
            ),
            encoding="utf-8",
        )
    except (OSError, ValueError) as error:
        print(f"GSLT2Parse source certificate: {error}", file=sys.stderr)
        return 1
    print(
        "gslt2parse-source-certificate-v1\t"
        f"tokens={len(lowered.tokens)}\t"
        f"source={source_digest}\tforest={forest_digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
