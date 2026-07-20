#!/usr/bin/env python3
"""Focused positive and negative tests for paired benchmark sampling."""

from __future__ import annotations

from dataclasses import dataclass

from rhocalc_paired_samples import AdaptivePairedPolicy, collect_interleaved


@dataclass(frozen=True)
class Result:
    elapsed: float


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sequence(values: list[float], calls: list[str], role: str):
    iterator = iter(values)

    def run() -> Result:
        calls.append(role)
        return Result(next(iterator))

    return run


def accept(_role: str, _result: Result) -> str | None:
    return None


def main() -> int:
    calls: list[str] = []
    fixed = collect_interleaved(
        3,
        sequence([1.0, 1.0, 1.0], calls, "candidate"),
        sequence([1.0, 1.0, 1.0], calls, "baseline"),
        accept,
    )
    require(fixed.error is None, "fixed sampler failed")
    require(fixed.stop_reason == "fixed-pairs", "fixed stop reason changed")
    require(len(fixed.candidate) == 3 and len(fixed.baseline) == 3,
            "fixed sampler did not consume every pair")
    require(
        calls == [
            "baseline", "candidate",
            "candidate", "baseline",
            "baseline", "candidate",
        ],
        "fixed sampler no longer alternates pair order",
    )
    print("PASS fixed paired sampling remains exhaustive")

    calls = []
    clear = collect_interleaved(
        5,
        sequence([1.02], calls, "candidate"),
        sequence([1.0], calls, "baseline"),
        accept,
        adaptive_policy=AdaptivePairedPolicy(),
    )
    require(clear.stop_reason == "adaptive-clear-pass",
            "clear pass did not stop early")
    require(len(clear.candidate) == 1 and len(clear.baseline) == 1,
            "clear pass consumed unnecessary pairs")
    require(calls == ["baseline", "candidate"],
            "first adaptive pair was not alternating")
    print("PASS clear paired result stops after one pair")

    calls = []
    ambiguous = collect_interleaved(
        3,
        sequence([1.18, 1.17, 1.16], calls, "candidate"),
        sequence([1.0, 1.0, 1.0], calls, "baseline"),
        accept,
        adaptive_policy=AdaptivePairedPolicy(),
    )
    require(ambiguous.stop_reason == "adaptive-maximum-pairs",
            "ambiguous result stopped early")
    require(len(ambiguous.candidate) == 3 and len(ambiguous.baseline) == 3,
            "ambiguous result did not consume the full budget")
    print("PASS ambiguous paired result consumes the full budget")

    calls = []
    regression = collect_interleaved(
        3,
        sequence([1.7, 1.6, 1.5], calls, "candidate"),
        sequence([1.0, 1.0, 1.0], calls, "baseline"),
        accept,
        adaptive_policy=AdaptivePairedPolicy(),
    )
    require(regression.stop_reason == "adaptive-maximum-pairs",
            "apparent regression stopped before the full budget")
    require(len(regression.candidate) == 3,
            "apparent regression did not consume the full budget")
    print("PASS apparent regression consumes the full budget")

    rejected = collect_interleaved(
        3,
        lambda: Result(1.0),
        lambda: Result(1.0),
        lambda role, _result: "candidate rejected" if role == "candidate" else None,
        adaptive_policy=AdaptivePairedPolicy(),
    )
    require(rejected.error == "candidate rejected",
            "validation failure was not propagated")
    require(rejected.stop_reason == "error",
            "validation failure has the wrong stop reason")
    print("PASS validation failure remains fail-closed")

    try:
        collect_interleaved(
            3,
            lambda: Result(1.0),
            None,
            accept,
            adaptive_policy=AdaptivePairedPolicy(),
        )
    except ValueError:
        pass
    else:
        raise AssertionError("adaptive sampling accepted a missing baseline")
    print("PASS adaptive sampling rejects a missing baseline")

    print("SUMMARY rhocalc-paired-samples 6/6 PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
