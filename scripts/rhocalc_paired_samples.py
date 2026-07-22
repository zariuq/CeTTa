#!/usr/bin/env python3
"""Collect candidate and baseline benchmark samples in alternating order."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import statistics
import subprocess
from typing import Generic, Protocol, TypeVar


class TimedResult(Protocol):
    elapsed: float


ResultT = TypeVar("ResultT", bound=TimedResult)


MATERIAL_MAX_MEDIAN_RATIO = 1.25
MATERIAL_MIN_ABSOLUTE_SLACK_S = 0.010
ADAPTIVE_CLEAR_PASS_RATIO = 1.10
ADAPTIVE_CLEAR_PASS_SLACK_S = 0.005


@dataclass(frozen=True)
class AdaptivePairedPolicy:
    """Stop early only when a paired timing result is clearly non-regressing."""

    minimum_pairs: int = 1
    clear_pass_ratio: float = ADAPTIVE_CLEAR_PASS_RATIO
    clear_pass_slack_s: float = ADAPTIVE_CLEAR_PASS_SLACK_S

    def validate(self, maximum_pairs: int) -> None:
        if maximum_pairs < 1:
            raise ValueError("maximum_pairs must be positive")
        if not 1 <= self.minimum_pairs <= maximum_pairs:
            raise ValueError(
                "minimum_pairs must be between one and maximum_pairs"
            )
        if not 0 < self.clear_pass_ratio < MATERIAL_MAX_MEDIAN_RATIO:
            raise ValueError(
                "clear_pass_ratio must be positive and below the material "
                "regression ratio"
            )
        if not 0 <= self.clear_pass_slack_s < MATERIAL_MIN_ABSOLUTE_SLACK_S:
            raise ValueError(
                "clear_pass_slack_s must be nonnegative and below the "
                "material absolute slack"
            )

    def clearly_passes(
        self,
        candidate: list[TimedResult],
        baseline: list[TimedResult],
    ) -> bool:
        if len(candidate) < self.minimum_pairs or len(candidate) != len(baseline):
            return False
        candidate_median = statistics.median(result.elapsed for result in candidate)
        baseline_median = statistics.median(result.elapsed for result in baseline)
        clear_limit = max(
            baseline_median * self.clear_pass_ratio,
            baseline_median + self.clear_pass_slack_s,
        )
        return candidate_median <= clear_limit


@dataclass(frozen=True)
class InterleavedSamples(Generic[ResultT]):
    candidate: list[ResultT]
    baseline: list[ResultT]
    error: str | None
    stop_reason: str


def collect_interleaved(
    runs: int,
    candidate_run: Callable[[], ResultT],
    baseline_run: Callable[[], ResultT] | None,
    validate: Callable[[str, ResultT], str | None],
    *,
    adaptive_policy: AdaptivePairedPolicy | None = None,
) -> InterleavedSamples[ResultT]:
    if runs < 1:
        raise ValueError("runs must be positive")
    if adaptive_policy is not None:
        if baseline_run is None:
            raise ValueError("adaptive paired sampling requires a baseline")
        adaptive_policy.validate(runs)
    candidate: list[ResultT] = []
    baseline: list[ResultT] = []
    for sample_index in range(runs):
        if baseline_run is None:
            order = (("candidate", candidate_run),)
        elif sample_index % 2 == 0:
            order = (
                ("baseline", baseline_run),
                ("candidate", candidate_run),
            )
        else:
            order = (
                ("candidate", candidate_run),
                ("baseline", baseline_run),
            )
        for role, run in order:
            try:
                result = run()
            except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
                return InterleavedSamples(
                    candidate, baseline, f"{role}: {exc}", "error"
                )
            detail = validate(role, result)
            if detail is not None:
                return InterleavedSamples(candidate, baseline, detail, "error")
            (candidate if role == "candidate" else baseline).append(result)
        if (
            adaptive_policy is not None
            and adaptive_policy.clearly_passes(candidate, baseline)
        ):
            return InterleavedSamples(
                candidate, baseline, None, "adaptive-clear-pass"
            )
    stop_reason = "adaptive-maximum-pairs" if adaptive_policy else "fixed-pairs"
    return InterleavedSamples(candidate, baseline, None, stop_reason)
