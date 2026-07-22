#!/usr/bin/env python3
"""Emit the pinned PeTTa per-form reader as scannerless GSLT data."""

from __future__ import annotations

import argparse
from pathlib import Path


# SWI-Prolog 10.1.9 code_type(Code, space), excluding surrogate code points.
PETTA_BLANKS = {
    0x0009,
    0x000A,
    0x000B,
    0x000C,
    0x000D,
    0x0020,
    0x1680,
    0x2000,
    0x2001,
    0x2002,
    0x2003,
    0x2004,
    0x2005,
    0x2006,
    0x2008,
    0x2009,
    0x200A,
    0x2028,
    0x2029,
    0x205F,
    0x3000,
}

# library(dcg/basics):string_without(" \t\r\n()", ...)
TOKEN_DELIMITERS = {
    0x0009,
    0x000A,
    0x000D,
    0x0020,
    0x0028,
    0x0029,
}


def app(head: str, *arguments: str) -> str:
    return f"({head} {' '.join(arguments)})" if arguments else head


def char(codepoint: int) -> str:
    return app("char", app("cp", str(codepoint)))


def cls(name: str) -> str:
    return app("class", name)


def ref(name: str) -> str:
    return app("ref", name)


def alt(items: list[str]) -> str:
    if not items:
        raise ValueError("alt requires at least one item")
    result = items[-1]
    for item in reversed(items[:-1]):
        result = app("alt", item, result)
    return result


def seq(items: list[str]) -> str:
    if not items:
        return app("eps", "nil")
    result = items[-1]
    for item in reversed(items[:-1]):
        result = app("seq", item, result)
    return result


def codepoints(text: str) -> str:
    result = "nil"
    for codepoint in reversed([ord(value) for value in text]):
        result = app("cons", app("cp", str(codepoint)), result)
    return result


def literal(text: str) -> str:
    return app("lit", codepoints(text))


def fact(rule_id: str, head: str) -> str:
    return f"    (rule {rule_id} (head {head}) (body))\n"


def rule(rule_id: str, head: str, body: list[str]) -> str:
    return (
        f"    (rule {rule_id} (head {head}) "
        f"(body {' '.join(body)}))\n"
    )


def definition(rule_id: str, name: str, grammar: str) -> str:
    return fact(rule_id, app("definition", name, grammar))


def emit_point_class(out: list[str], name: str, points: set[int]) -> None:
    for point in sorted(points):
        out.append(
            fact(
                f"member-{name}-{point}",
                app("member", name, app("cp", str(point))),
            )
        )


def emit_except_class(out: list[str], name: str, exclusions: set[int]) -> None:
    out.append(
        rule(
            f"member-{name}",
            app("member", name, "?codepoint"),
            [
                app("different", "?codepoint", app("cp", str(point)))
                for point in sorted(exclusions)
            ],
        )
    )


def emit_classes(path: Path) -> None:
    out = [
        "; Scalar classes pinned to PeTTa parser.pl under SWI-Prolog 10.1.9.\n",
        "; Input is decoded to Unicode scalars before grammar execution.\n",
        "(gslt-presentation-v1 PeTTaFormReaderScalarClassesV1\n",
        "  (signature)\n",
        "  (equations)\n",
        "  (rewrites\n",
    ]
    emit_point_class(out, "petta-blank-scalar", PETTA_BLANKS)
    emit_point_class(out, "petta-token-boundary-scalar", TOKEN_DELIMITERS)
    emit_except_class(out, "petta-token-scalar", TOKEN_DELIMITERS)
    emit_except_class(
        out,
        "petta-token-first-scalar",
        PETTA_BLANKS | {0x0022, 0x0024, 0x0028, 0x0029},
    )
    emit_except_class(
        out,
        "petta-string-plain-scalar",
        {0x0022, 0x005C},
    )
    emit_except_class(
        out,
        "petta-quoted-token-plain-scalar",
        TOKEN_DELIMITERS | {0x0022, 0x005C},
    )
    out.append("  ))\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(out), encoding="utf-8")


def emit_reader(path: Path) -> None:
    quoted_token_unit = alt(
        [
            cls("petta-quoted-token-plain-scalar"),
            seq([char(0x005C), cls("petta-token-scalar")]),
        ]
    )
    token_body = alt(
        [
            app(
                "node",
                "variable",
                app("right", char(0x0024), app("plus", cls("petta-token-scalar"))),
            ),
            app("node", "dollar-symbol", char(0x0024)),
            app(
                "node",
                "quoted-token",
                app(
                    "right",
                    char(0x0022),
                    seq([app("star", quoted_token_unit), literal('\\"')]),
                ),
            ),
            app(
                "node",
                "token",
                seq(
                    [
                        cls("petta-token-first-scalar"),
                        app("star", cls("petta-token-scalar")),
                    ]
                ),
            ),
        ]
    )
    out = [
        "; PeTTa per-form DCG recognition as scannerless GSLT data.\n",
        "; Whole-file comment stripping and balanced-form extraction are a separate authority layer.\n",
        "(gslt-presentation-v1 PeTTaFormReaderSyntaxV1\n",
        "  (signature)\n",
        "  (equations)\n",
        "  (rewrites\n",
        definition(
            "def-petta-token-boundary",
            "petta-token-boundary",
            alt(["eof", cls("petta-token-boundary-scalar")]),
        ),
        definition(
            "def-petta-skip",
            "petta-skip",
            app("star", cls("petta-blank-scalar")),
        ),
        definition(
            "def-petta-string-escape",
            "petta-string-escape",
            app("node", "escape", app("right", char(0x005C), "any")),
        ),
        definition(
            "def-petta-string-unit",
            "petta-string-unit",
            alt(
                [
                    ref("petta-string-escape"),
                    cls("petta-string-plain-scalar"),
                ]
            ),
        ),
        definition(
            "def-petta-string",
            "petta-string",
            app(
                "node",
                "string",
                app(
                    "right",
                    char(0x0022),
                    app(
                        "left",
                        app("star", ref("petta-string-unit")),
                        char(0x0022),
                    ),
                ),
            ),
        ),
        definition(
            "def-petta-token-like",
            "petta-token-like",
            app(
                "left",
                token_body,
                app("peek", ref("petta-token-boundary")),
            ),
        ),
        definition(
            "def-petta-expression",
            "petta-expression",
            app(
                "node",
                "expression",
                app(
                    "right",
                    char(0x0028),
                    app(
                        "left",
                        app(
                            "right",
                            ref("petta-skip"),
                            app(
                                "star",
                                app(
                                    "left",
                                    ref("petta-atom"),
                                    ref("petta-skip"),
                                ),
                            ),
                        ),
                        char(0x0029),
                    ),
                ),
            ),
        ),
        definition(
            "def-petta-atom",
            "petta-atom",
            alt(
                [
                    ref("petta-string"),
                    ref("petta-expression"),
                    ref("petta-token-like"),
                ]
            ),
        ),
        definition(
            "def-petta-form",
            "petta-form",
            app(
                "node",
                "form",
                app(
                    "right",
                    ref("petta-skip"),
                    app(
                        "left",
                        ref("petta-atom"),
                        app("right", ref("petta-skip"), "eof"),
                    ),
                ),
            ),
        ),
        "  ))\n",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(out), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--classes", type=Path, required=True)
    parser.add_argument("--reader", type=Path, required=True)
    arguments = parser.parse_args()
    emit_classes(arguments.classes)
    emit_reader(arguments.reader)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
