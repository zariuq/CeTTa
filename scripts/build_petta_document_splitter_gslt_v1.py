#!/usr/bin/env python3
"""Emit PeTTa's pinned whole-document splitter as scannerless GSLT data."""

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
        "; Scalar classes pinned to PeTTa filereader.pl under SWI-Prolog 10.1.9.\n",
        "; Comment termination is LF-only; quote toggling is escape-oblivious.\n",
        "(gslt-presentation-v1 PeTTaDocumentSplitterScalarClassesV1\n",
        "  (signature)\n",
        "  (equations)\n",
        "  (rewrites\n",
    ]
    emit_point_class(out, "petta-splitter-blank-scalar", PETTA_BLANKS)
    emit_except_class(out, "petta-splitter-nonquote-scalar", {0x0022})
    emit_except_class(out, "petta-splitter-comment-body-scalar", {0x000A})
    emit_except_class(
        out,
        "petta-splitter-ordinary-scalar",
        {0x0022, 0x0028, 0x0029, 0x003B},
    )
    out.append("  ))\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(out), encoding="utf-8")


def emit_splitter(path: Path) -> None:
    comment_line = app(
        "right",
        char(0x003B),
        app(
            "left",
            app("star", cls("petta-splitter-comment-body-scalar")),
            char(0x000A),
        ),
    )
    comment_eof = app(
        "right",
        char(0x003B),
        app(
            "left",
            app("star", cls("petta-splitter-comment-body-scalar")),
            "eof",
        ),
    )
    discarded_comment = app(
        "right", ref("petta-splitter-comment"), app("eps", "nil")
    )
    discarded_line_comment = app(
        "right", ref("petta-splitter-comment-line"), app("eps", "nil")
    )
    out = [
        "; PeTTa filereader.pl strip/3 plus top_forms//2 as scannerless GSLT data.\n",
        "; This compatibility presentation intentionally toggles quotes without escape handling.\n",
        "(gslt-presentation-v1 PeTTaDocumentSplitterSyntaxV1\n",
        "  (signature)\n",
        "  (equations)\n",
        "  (rewrites\n",
        definition(
            "def-petta-splitter-comment-line",
            "petta-splitter-comment-line",
            comment_line,
        ),
        definition(
            "def-petta-splitter-comment-eof",
            "petta-splitter-comment-eof",
            comment_eof,
        ),
        definition(
            "def-petta-splitter-comment",
            "petta-splitter-comment",
            alt(
                [
                    ref("petta-splitter-comment-line"),
                    ref("petta-splitter-comment-eof"),
                ]
            ),
        ),
        definition(
            "def-petta-splitter-quoted",
            "petta-splitter-quoted",
            app(
                "node",
                "naive-quoted",
                seq(
                    [
                        char(0x0022),
                        app("star", cls("petta-splitter-nonquote-scalar")),
                        char(0x0022),
                    ]
                ),
            ),
        ),
        definition(
            "def-petta-splitter-balanced-item",
            "petta-splitter-balanced-item",
            alt(
                [
                    ref("petta-splitter-balanced"),
                    ref("petta-splitter-quoted"),
                    discarded_line_comment,
                    cls("petta-splitter-ordinary-scalar"),
                ]
            ),
        ),
        definition(
            "def-petta-splitter-balanced",
            "petta-splitter-balanced",
            app(
                "node",
                "balanced",
                seq(
                    [
                        char(0x0028),
                        app("star", ref("petta-splitter-balanced-item")),
                        char(0x0029),
                    ]
                ),
            ),
        ),
        definition(
            "def-petta-splitter-top-skip-unit",
            "petta-splitter-top-skip-unit",
            alt([cls("petta-splitter-blank-scalar"), discarded_comment]),
        ),
        definition(
            "def-petta-splitter-top-skip",
            "petta-splitter-top-skip",
            app("star", ref("petta-splitter-top-skip-unit")),
        ),
        definition(
            "def-petta-splitter-top-form",
            "petta-splitter-top-form",
            alt(
                [
                    app(
                        "node",
                        "runnable",
                        app("right", char(0x0021), ref("petta-splitter-balanced")),
                    ),
                    app("node", "form", ref("petta-splitter-balanced")),
                ]
            ),
        ),
        definition(
            "def-petta-document",
            "petta-document",
            app(
                "node",
                "document",
                app(
                    "right",
                    ref("petta-splitter-top-skip"),
                    app(
                        "left",
                        app(
                            "star",
                            app(
                                "left",
                                ref("petta-splitter-top-form"),
                                ref("petta-splitter-top-skip"),
                            ),
                        ),
                        "eof",
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
    parser.add_argument("--splitter", type=Path, required=True)
    arguments = parser.parse_args()
    emit_classes(arguments.classes)
    emit_splitter(arguments.splitter)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
