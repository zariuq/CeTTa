#!/usr/bin/env python3
"""Positive and negative witnesses for the ratified HE syntax contract."""

from __future__ import annotations

from typing import Any

from test_he_parser_authority_v1 import ATOM_CASES as RUST_BASELINE_CASES


AtomValue = tuple[Any, ...]
Case = tuple[bytes, tuple[AtomValue, ...]]


def rejected(reason: str) -> tuple[AtomValue, ...]:
    return (("error", reason),)


RATIFIED_ATOM_CASES: dict[str, Case] = dict(RUST_BASELINE_CASES)
RATIFIED_ATOM_CASES.update(
    {
        "bare-dollar-variable": (
            b"$",
            rejected("A variable name must contain at least one scalar"),
        ),
        "semicolon-and-quote-inside-variable": (
            b"$a;\"b",
            (("var", "a"),),
        ),
        "hash-leading-word": (
            b"#foo",
            (("sym", "#foo"),),
        ),
        "comment-carriage-return": (
            b"a;comment\rb",
            (("sym", "a"), ("sym", "b")),
        ),
        "unicode-empty-body": (
            b'"\\u{}"',
            rejected("A Unicode escape requires one through six hex digits"),
        ),
        "unicode-repeated-open-brace": (
            b'"\\u{{41}"',
            rejected("A Unicode escape has exactly one opening brace"),
        ),
        "unicode-seven-leading-zero": (
            b'"\\u{0000041}"',
            rejected("A Unicode escape has at most six hex digits"),
        ),
        "unicode-max-scalar": (
            b'"\\u{10ffff}"',
            (("sym", '"\U0010ffff"'),),
        ),
        "unicode-surrogate": (
            b'"\\u{d800}"',
            rejected("A Unicode escape must denote a scalar value"),
        ),
    }
)


RUST_ATOM_OBSERVATIONS: dict[str, Case] = dict(RUST_BASELINE_CASES)
RUST_ATOM_OBSERVATIONS.update(
    {
        "hash-leading-word": (
            b"#foo",
            (("sym", "#foo"),),
        ),
        "comment-carriage-return": (
            b"a;comment\rb",
            (("sym", "a"),),
        ),
        "unicode-empty-body": (
            b'"\\u{}"',
            (("sym", '"\x00"'),),
        ),
        "unicode-repeated-open-brace": (
            b'"\\u{{41}"',
            (("sym", '"A"'),),
        ),
        "unicode-seven-leading-zero": (
            b'"\\u{0000041}"',
            (("sym", '"A"'),),
        ),
        "unicode-max-scalar": (
            b'"\\u{10ffff}"',
            (("sym", '"\U0010ffff"'),),
        ),
        "unicode-surrogate": (
            b'"\\u{d800}"',
            (("error", "Invalid escape sequence"),),
        ),
    }
)


if set(RATIFIED_ATOM_CASES) != set(RUST_ATOM_OBSERVATIONS):
    raise RuntimeError("ratified and Rust observation inventories differ")
if any(
    RATIFIED_ATOM_CASES[label][0] != RUST_ATOM_OBSERVATIONS[label][0]
    for label in RATIFIED_ATOM_CASES
):
    raise RuntimeError("ratified and Rust observations use different inputs")


def check_rust_observation(
    label: str,
    observed: tuple[AtomValue, ...],
) -> str:
    expected = RUST_ATOM_OBSERVATIONS[label][1]
    if observed != expected:
        raise RuntimeError(
            f"Rust HE observation changed for {label}: "
            f"expected {expected!r}, observed {observed!r}"
        )
    ratified = RATIFIED_ATOM_CASES[label][1]
    return "agrees" if observed == ratified else "diverges"
