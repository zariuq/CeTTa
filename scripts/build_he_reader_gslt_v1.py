#!/usr/bin/env python3
"""Emit the pinned HE scalar classes and scannerless reader presentation."""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from pathlib import Path


HE_WHITESPACE = {
    0x0009,
    0x000A,
    0x000B,
    0x000C,
    0x000D,
    0x0020,
    0x0085,
    0x00A0,
    0x1680,
    0x2000,
    0x2001,
    0x2002,
    0x2003,
    0x2004,
    0x2005,
    0x2006,
    0x2007,
    0x2008,
    0x2009,
    0x200A,
    0x2028,
    0x2029,
    0x202F,
    0x205F,
    0x3000,
}

HEX = {ord(char) for char in "0123456789abcdefABCDEF"}
HEX_NONZERO = HEX - {ord("0")}
HEX_FOUR_FIRST = {ord(char) for char in "123456789abcefABCEF"}
HEX_D = {ord("d"), ord("D")}
HEX_LOW_SEVEN = {ord(char) for char in "01234567"}
SIMPLE_ESCAPES = {ord(char) for char in "'\"\\nrt"}

UNICODE_HEX_CLASS_POINTS = {
    "he-zero-scalar": {ord("0")},
    "he-one-scalar": {ord("1")},
    "he-hex-scalar": HEX,
    "he-hex-nonzero-scalar": HEX_NONZERO,
    "he-hex-four-first-scalar": HEX_FOUR_FIRST,
    "he-hex-d-scalar": HEX_D,
    "he-hex-low-seven-scalar": HEX_LOW_SEVEN,
    "he-hex-high-eight-scalar": HEX - HEX_LOW_SEVEN,
    "he-hex-nonzero-nond-scalar": HEX_NONZERO - HEX_D,
    "he-hex-above-one-nond-scalar": HEX_FOUR_FIRST - {ord("1")},
}

UNICODE_HEX_CLASS_NAMES = {
    frozenset(points): name
    for name, points in UNICODE_HEX_CLASS_POINTS.items()
}


@dataclass(frozen=True)
class UnicodeResidualTransition:
    class_name: str
    target: int


@dataclass(frozen=True)
class UnicodeResidualState:
    residuals: frozenset[tuple[str, ...]]
    accepting: bool
    transitions: tuple[UnicodeResidualTransition, ...]


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
    for codepoint in reversed([ord(char) for char in text]):
        result = app("cons", app("cp", str(codepoint)), result)
    return result


def literal(text: str) -> str:
    return app("lit", codepoints(text))


def fact(rule_id: str, head: str) -> str:
    return f"    (rule {rule_id} (head {head}) (body))\n"


def rule(rule_id: str, head: str, body: list[str]) -> str:
    body_text = " ".join(body)
    return f"    (rule {rule_id} (head {head}) (body {body_text}))\n"


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
            [app("different", "?codepoint", app("cp", str(point))) for point in sorted(exclusions)],
        )
    )


def emit_classes(path: Path) -> None:
    out = [
        "; Scalar classes pinned to HE SExprParser at authority revision 3f76dc46.\n",
        "; The input carrier is decoded Unicode scalars; malformed UTF-8 is rejected before class lookup.\n",
        "(gslt-presentation-v1 HEReaderScalarClassesV1\n",
        "  (signature)\n",
        "  (equations)\n",
        "  (rewrites\n",
    ]
    emit_point_class(out, "he-whitespace-scalar", HE_WHITESPACE)
    emit_point_class(out, "he-newline-scalar", {0x0A})
    emit_point_class(out, "he-simple-escape-scalar", SIMPLE_ESCAPES)
    emit_point_class(out, "he-hex-scalar", HEX)
    emit_point_class(out, "he-hex-nonzero-scalar", HEX_NONZERO)
    emit_point_class(out, "he-hex-four-first-scalar", HEX_FOUR_FIRST)
    emit_point_class(out, "he-hex-d-scalar", HEX_D)
    emit_point_class(out, "he-hex-low-seven-scalar", HEX_LOW_SEVEN)
    emit_point_class(out, "he-zero-scalar", {ord("0")})
    emit_point_class(out, "he-one-scalar", {ord("1")})
    emit_point_class(
        out, "he-hex-high-eight-scalar", HEX - HEX_LOW_SEVEN
    )
    emit_point_class(
        out, "he-hex-nonzero-nond-scalar", HEX_NONZERO - HEX_D
    )
    emit_point_class(
        out,
        "he-hex-above-one-nond-scalar",
        HEX_FOUR_FIRST - {ord("1")},
    )

    emit_except_class(out, "he-non-newline-scalar", {0x0A})
    emit_except_class(out, "he-string-plain-scalar", {0x22, 0x5C})
    emit_except_class(
        out,
        "he-word-start-scalar",
        HE_WHITESPACE | {0x22, 0x24, 0x28, 0x29, 0x3B},
    )
    emit_except_class(
        out,
        "he-word-tail-scalar",
        HE_WHITESPACE | {0x28, 0x29, 0x3B},
    )
    emit_except_class(
        out,
        "he-variable-tail-scalar",
        HE_WHITESPACE | {0x23, 0x28, 0x29},
    )
    out.append("  ))\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(out), encoding="utf-8")


def unicode_valid_class_sequences() -> list[tuple[str, ...]]:
    sequences: set[tuple[str, ...]] = {()}
    for total_digits in range(1, 9):
        sequences.add(tuple(["he-zero-scalar"] * total_digits))
        for significant_digits in range(1, min(6, total_digits) + 1):
            leading_zeros = total_digits - significant_digits
            prefix = ["he-zero-scalar"] * leading_zeros
            if significant_digits <= 3:
                sequences.add(
                    tuple(
                        prefix
                        + ["he-hex-nonzero-scalar"]
                        + ["he-hex-scalar"] * (significant_digits - 1)
                    )
                )
            elif significant_digits == 4:
                sequences.add(
                    tuple(
                        prefix
                        + ["he-hex-four-first-scalar"]
                        + ["he-hex-scalar"] * 3
                    )
                )
                sequences.add(
                    tuple(
                        prefix
                        + ["he-hex-d-scalar", "he-hex-low-seven-scalar"]
                        + ["he-hex-scalar"] * 2
                    )
                )
            elif significant_digits == 5:
                sequences.add(
                    tuple(
                        prefix
                        + ["he-hex-nonzero-scalar"]
                        + ["he-hex-scalar"] * 4
                    )
                )
            else:
                sequences.add(
                    tuple(
                        prefix
                        + ["he-one-scalar", "he-zero-scalar"]
                        + ["he-hex-scalar"] * 4
                    )
                )
    return sorted(sequences, key=lambda item: (len(item), item))


def unicode_residual_derivative(
    residuals: frozenset[tuple[str, ...]], codepoint: int
) -> frozenset[tuple[str, ...]]:
    return frozenset(
        residual[1:]
        for residual in residuals
        if residual
        and codepoint in UNICODE_HEX_CLASS_POINTS[residual[0]]
    )


def validate_unicode_residual_dfa(
    sequences: list[tuple[str, ...]],
    states: tuple[UnicodeResidualState, ...],
) -> None:
    start = frozenset(sequences)
    if not states or states[0].residuals != start:
        raise ValueError("Unicode residual DFA start relation changed")
    if len(start) != len(sequences):
        raise ValueError("Unicode source relation contains duplicate sequences")

    seen_states: set[frozenset[tuple[str, ...]]] = set()
    for state in states:
        if state.residuals in seen_states:
            raise ValueError("Unicode residual DFA contains duplicate states")
        seen_states.add(state.residuals)
        if state.accepting != (() in state.residuals):
            raise ValueError("Unicode residual DFA acceptance bit changed")

        enabled = {
            codepoint
            for codepoint in HEX
            if unicode_residual_derivative(state.residuals, codepoint)
        }
        covered: set[int] = set()
        for transition in state.transitions:
            points = UNICODE_HEX_CLASS_POINTS.get(transition.class_name)
            if not points:
                raise ValueError("Unicode residual DFA uses an unknown class")
            if covered & points:
                raise ValueError("Unicode residual DFA transitions overlap")
            covered.update(points)
            if transition.target >= len(states):
                raise ValueError("Unicode residual DFA target is absent")
            target = states[transition.target].residuals
            for codepoint in points:
                if unicode_residual_derivative(
                    state.residuals, codepoint
                ) != target:
                    raise ValueError(
                        "Unicode residual DFA transition is not a derivative"
                    )
        if covered != enabled:
            raise ValueError("Unicode residual DFA transition coverage changed")

    reachable = {0}
    pending = [0]
    while pending:
        state_id = pending.pop()
        for transition in states[state_id].transitions:
            if transition.target not in reachable:
                reachable.add(transition.target)
                pending.append(transition.target)
    if len(reachable) != len(states):
        raise ValueError("Unicode residual DFA contains unreachable states")


def unicode_residual_dfa(
    sequences: list[tuple[str, ...]],
) -> tuple[UnicodeResidualState, ...]:
    alphabet = tuple(sorted(HEX))
    start = frozenset(sequences)
    identifiers = {start: 0}
    pending = deque([start])
    states: list[UnicodeResidualState] = []

    while pending:
        residuals = pending.popleft()
        targets: dict[frozenset[tuple[str, ...]], set[int]] = {}
        for codepoint in alphabet:
            target = unicode_residual_derivative(residuals, codepoint)
            if target:
                targets.setdefault(target, set()).add(codepoint)
        transitions: list[UnicodeResidualTransition] = []
        for target, points in sorted(
            targets.items(), key=lambda item: tuple(sorted(item[1]))
        ):
            if target not in identifiers:
                identifiers[target] = len(identifiers)
                pending.append(target)
            class_name = UNICODE_HEX_CLASS_NAMES.get(frozenset(points))
            if class_name is None:
                raise ValueError(
                    "Unicode residual DFA produced an undeclared class: "
                    + "".join(chr(point) for point in sorted(points))
                )
            transitions.append(
                UnicodeResidualTransition(class_name, identifiers[target])
            )
        states.append(
            UnicodeResidualState(
                residuals,
                () in residuals,
                tuple(transitions),
            )
        )

    result = tuple(states)
    if len(result) != len(identifiers):
        raise ValueError("Unicode residual DFA state accounting changed")
    validate_unicode_residual_dfa(sequences, result)
    return result


def emit_reader(path: Path) -> None:
    valid_sequences = unicode_valid_class_sequences()
    unicode_dfa = unicode_residual_dfa(valid_sequences)
    out = [
        "; HE empty-tokenizer concrete syntax as scannerless GSLT data.\n",
        "; Recognition matches the pinned Rust SExprParser, including its exact string-escape language.\n",
        "; Runtime regex token actions and syntax-tree recovery/span metadata are separate versioned layers.\n",
        "(gslt-presentation-v1 HEReaderSyntaxV1\n",
        "  (signature)\n",
        "  (equations)\n",
        "  (rewrites\n",
    ]

    out.extend(
        [
            definition(
                "def-he-unicode-extra-opens",
                "he-unicode-extra-opens",
                app("star", char(ord("{"))),
            ),
        ]
    )
    for state_id, state in enumerate(unicode_dfa):
        name = f"he-unicode-state-{state_id:03d}"
        branches = []
        if state.accepting:
            branches.append(ref("he-unicode-extra-opens"))
        for transition in state.transitions:
            branches.append(
                app(
                    "right",
                    ref("he-unicode-extra-opens"),
                    seq(
                        [
                            cls(transition.class_name),
                            ref(
                                "he-unicode-state-"
                                f"{transition.target:03d}"
                            ),
                        ]
                    ),
                )
            )
        out.append(definition(f"def-{name}", name, alt(branches)))
    out.extend(
        [
            definition(
                "def-he-unicode-valid-body",
                "he-unicode-valid-body",
                ref("he-unicode-state-000"),
            ),
            definition(
                "def-he-unicode-escape",
                "he-unicode-escape",
                app(
                    "node",
                    "unicode-escape",
                    app(
                        "right",
                        literal("\\u{"),
                        app(
                            "left",
                            ref("he-unicode-valid-body"),
                            char(ord("}")),
                        ),
                    ),
                ),
            ),
            definition(
                "def-he-byte-escape",
                "he-byte-escape",
                app(
                    "node",
                    "byte-escape",
                    app(
                        "right",
                        literal("\\x"),
                        seq([cls("he-hex-low-seven-scalar"), cls("he-hex-scalar")]),
                    ),
                ),
            ),
            definition(
                "def-he-simple-escape",
                "he-simple-escape",
                app(
                    "node",
                    "simple-escape",
                    app(
                        "right",
                        char(ord("\\")),
                        cls("he-simple-escape-scalar"),
                    ),
                ),
            ),
            definition(
                "def-he-string-unit",
                "he-string-unit",
                alt(
                    [
                        ref("he-unicode-escape"),
                        ref("he-byte-escape"),
                        ref("he-simple-escape"),
                        cls("he-string-plain-scalar"),
                    ]
                ),
            ),
            definition(
                "def-he-string",
                "he-string",
                app(
                    "node",
                    "string",
                    app(
                        "right",
                        char(ord('"')),
                        app(
                            "left",
                            app("star", ref("he-string-unit")),
                            char(ord('"')),
                        ),
                    ),
                ),
            ),
            definition(
                "def-he-comment-boundary",
                "he-comment-boundary",
                alt([cls("he-newline-scalar"), "eof"]),
            ),
            definition(
                "def-he-comment",
                "he-comment",
                app(
                    "node",
                    "comment",
                    app(
                        "right",
                        char(ord(";")),
                        app(
                            "left",
                            app("star", cls("he-non-newline-scalar")),
                            app("peek", ref("he-comment-boundary")),
                        ),
                    ),
                ),
            ),
            definition(
                "def-he-layout-unit",
                "he-layout-unit",
                alt([cls("he-whitespace-scalar"), ref("he-comment")]),
            ),
            definition("def-he-skip", "he-skip", app("star", ref("he-layout-unit"))),
            definition(
                "def-he-word-boundary",
                "he-word-boundary",
                alt(
                    [
                        "eof",
                        cls("he-whitespace-scalar"),
                        char(ord("(")),
                        char(ord(")")),
                        char(ord(";")),
                    ]
                ),
            ),
            definition(
                "def-he-variable-boundary",
                "he-variable-boundary",
                alt(
                    [
                        "eof",
                        cls("he-whitespace-scalar"),
                        char(ord("(")),
                        char(ord(")")),
                    ]
                ),
            ),
            definition(
                "def-he-word",
                "he-word",
                app(
                    "node",
                    "word",
                    app(
                        "left",
                        seq(
                            [
                                cls("he-word-start-scalar"),
                                app("star", cls("he-word-tail-scalar")),
                            ]
                        ),
                        app("peek", ref("he-word-boundary")),
                    ),
                ),
            ),
            definition(
                "def-he-variable",
                "he-variable",
                app(
                    "node",
                    "variable",
                    app(
                        "left",
                        app(
                            "right",
                            char(ord("$")),
                            app("star", cls("he-variable-tail-scalar")),
                        ),
                        app("peek", ref("he-variable-boundary")),
                    ),
                ),
            ),
            definition(
                "def-he-expression",
                "he-expression",
                app(
                    "node",
                    "expression",
                    app(
                        "right",
                        char(ord("(")),
                        app(
                            "left",
                            app(
                                "right",
                                ref("he-skip"),
                                app(
                                    "star",
                                    app("left", ref("he-atom"), ref("he-skip")),
                                ),
                            ),
                            char(ord(")")),
                        ),
                    ),
                ),
            ),
            definition(
                "def-he-atom",
                "he-atom",
                alt(
                    [
                        ref("he-string"),
                        ref("he-variable"),
                        ref("he-expression"),
                        ref("he-word"),
                    ]
                ),
            ),
            definition("def-he-top-atom", "he-top-atom", ref("he-atom")),
            definition(
                "def-he-document",
                "he-document",
                app(
                    "node",
                    "document",
                    app(
                        "left",
                        app(
                            "right",
                            ref("he-skip"),
                            app(
                                "star",
                                app("left", ref("he-top-atom"), ref("he-skip")),
                            ),
                        ),
                        "eof",
                    ),
                ),
            ),
        ]
    )
    out.append("  ))\n")
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
