#!/usr/bin/env python3

from __future__ import annotations

from rhocalc_m3_rholang_cli_compare import (
    expected_observation_sets,
    format_observation_set,
    parse_rholang_storage_outputs,
)


def expect_equal(actual: object, expected: object) -> None:
    if actual != expected:
        raise AssertionError(f"expected {expected!r}, got {actual!r}")


def expect_raises(fn) -> None:
    try:
        fn()
    except (RuntimeError, ValueError):
        return
    raise AssertionError("expected failure")


def main() -> int:
    expect_equal(expected_observation_sets("empty"), {()})
    expect_equal(
        expected_observation_sets("out=send(payload nil);out=nil"),
        {("out=send(payload nil)",), ("out=nil",)},
    )
    expect_raises(lambda: expected_observation_sets("out"))

    expect_equal(
        parse_rholang_storage_outputs("Storage Contents:\nThe space is empty."),
        (),
    )
    expect_equal(
        parse_rholang_storage_outputs('Storage Contents:\n"sink"!(Nil)'),
        ("sink=nil",),
    )
    expect_equal(
        parse_rholang_storage_outputs('Storage Contents:\n"sink"!("payload"!(Nil))'),
        ("sink=send(payload nil)",),
    )
    expect_equal(
        parse_rholang_storage_outputs(
            'Storage Contents:\n"b"!("p"!(Nil)) |\n"a"!(Nil)'
        ),
        ("a=nil", "b=send(p nil)"),
    )
    expect_equal(
        parse_rholang_storage_outputs(
            'Storage Contents:\n"sink"!(("a"!(Nil) | "b"!(Nil)))'
        ),
        ("sink=par(send(a nil)|send(b nil))",),
    )
    expect_equal(
        parse_rholang_storage_outputs(
            "Storage Contents:\n"
            '"out"!(Unforgeable(0x957a364a58d054107bd15365f45523ac80ce5e58955ebd016b9dbc06867d2541))'
        ),
        ("out=drop(fresh0)",),
    )
    expect_equal(
        parse_rholang_storage_outputs(
            "Storage Contents:\n"
            'Unforgeable(0x957a364a58d054107bd15365f45523ac80ce5e58955ebd016b9dbc06867d2541)!(Nil)'
        ),
        ("fresh0=nil",),
    )
    expect_equal(format_observation_set(("sink=nil",)), "sink=nil")

    expect_raises(lambda: parse_rholang_storage_outputs("no storage marker"))
    expect_raises(lambda: parse_rholang_storage_outputs("Storage Contents:\nfor (x <- @\"c\") { Nil }"))
    expect_raises(lambda: parse_rholang_storage_outputs('Storage Contents:\n"sink"!(Nil'))
    expect_raises(lambda: parse_rholang_storage_outputs("Storage Contents:\nUnforgeable(0xabc"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
