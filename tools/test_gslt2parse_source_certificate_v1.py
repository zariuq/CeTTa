#!/usr/bin/env python3
"""Positive and destructive gates for source-certificate projection."""

from __future__ import annotations

from collections.abc import Callable

from gslt2parse_schema_v1 import SExpr, StringLiteral, Symbol
from gslt2parse_source_certificate_v1 import (
    GrammarData,
    LexicalBinding,
    Production,
    ProjectionError,
    lower_node,
    unwrap_result,
)


class GateFailure(RuntimeError):
    pass


def cp_list(text: str) -> SExpr:
    result: SExpr = Symbol("nil")
    for character in reversed(text):
        result = (
            Symbol("cons"),
            (Symbol("cp"), ord(character)),
            result,
        )
    return result


def lexical(text: str, label: str = "lex-token") -> SExpr:
    return (Symbol("node"), Symbol(label), cp_list(text))


def source_node(label: str, value: SExpr) -> SExpr:
    return (Symbol("node"), Symbol(label), value)


def expect_projection_error(label: str, action: Callable[[], object]) -> None:
    try:
        action()
    except ProjectionError:
        return
    raise GateFailure(f"{label}: malformed producer evidence was accepted")


def main() -> int:
    data = GrammarData(
        productions={
            "root_pair": Production(
                "root_pair",
                "root",
                (("Tm", "OPEN"), ("Hl", "token"),
                 ("Hl", "token"), ("Tm", "CLOSE")),
            ),
            "root_empty": Production("root_empty", "root", ()),
            "other": Production("other", "other-sort", ()),
        },
        lexical_by_sort={
            "token": LexicalBinding("token", "lex-token", "token-class")
        },
    )

    semantic = source_node(
        "root_pair",
        (Symbol("pair"), lexical("a"), lexical("b")),
    )
    lowered = lower_node(data, "root", semantic, 0, "positive")
    expected_tokens: tuple[SExpr, ...] = (
        Symbol("OPEN"),
        (Symbol("Lex"), Symbol("token-class"), StringLiteral("a")),
        (Symbol("Lex"), Symbol("token-class"), StringLiteral("b")),
        Symbol("CLOSE"),
    )
    if lowered.tokens != expected_tokens or lowered.next_index != 4:
        raise GateFailure("positive projection changed token order or multiplicity")
    expected_certificate: SExpr = (
        Symbol("NodeC"), Symbol("root_pair"), Symbol("root"), 0, 4,
        (Symbol("Cons"), (Symbol("TokC"), Symbol("OPEN"), 0),
         (Symbol("Cons"),
          (Symbol("LeafC"), expected_tokens[1], 1, 2),
          (Symbol("Cons"),
           (Symbol("LeafC"), expected_tokens[2], 2, 3),
           (Symbol("Cons"), (Symbol("TokC"), Symbol("CLOSE"), 3),
            Symbol("Nil"))))),
    )
    if lowered.certificate != expected_certificate:
        raise GateFailure("positive projection changed the source certificate")

    mutations: tuple[tuple[str, Callable[[], object]], ...] = (
        (
            "unknown production",
            lambda: lower_node(
                data, "root", source_node("invented", Symbol("nil")), 0,
                "unknown",
            ),
        ),
        (
            "wrong production sort",
            lambda: lower_node(
                data, "root", source_node("other", Symbol("nil")), 0,
                "sort",
            ),
        ),
        (
            "wrong lexical node",
            lambda: lower_node(data, "token", lexical("a", "wrong"), 0, "lex"),
        ),
        (
            "empty lexical token",
            lambda: lower_node(data, "token", lexical(""), 0, "empty"),
        ),
        (
            "surrogate lexical scalar",
            lambda: lower_node(
                data,
                "token",
                source_node(
                    "lex-token",
                    (Symbol("cons"), (Symbol("cp"), 0xD800), Symbol("nil")),
                ),
                0,
                "surrogate",
            ),
        ),
        (
            "missing semantic child",
            lambda: lower_node(
                data, "root", source_node("root_pair", lexical("a")), 0,
                "missing-child",
            ),
        ),
        (
            "extra semantic child",
            lambda: lower_node(
                data,
                "root",
                source_node(
                    "root_pair",
                    (Symbol("pair"), lexical("a"),
                     (Symbol("pair"), lexical("b"), lexical("c"))),
                ),
                0,
                "extra-child",
            ),
        ),
        (
            "unconsumed parser suffix",
            lambda: unwrap_result(
                (Symbol("result"), source_node("database", semantic),
                 Symbol("trailing")),
                "database",
            ),
        ),
    )
    for label, action in mutations:
        expect_projection_error(label, action)

    print(f"(GSLT2ParseSourceCertificateV1Summary 1 {len(mutations)} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
