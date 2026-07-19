#!/usr/bin/env python3
"""Collect candidate and baseline benchmark samples in alternating order."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import subprocess
from typing import Generic, Protocol, TypeVar


class TimedResult(Protocol):
    elapsed: float


ResultT = TypeVar("ResultT", bound=TimedResult)


@dataclass(frozen=True)
class InterleavedSamples(Generic[ResultT]):
    candidate: list[ResultT]
    baseline: list[ResultT]
    error: str | None


def collect_interleaved(
    runs: int,
    candidate_run: Callable[[], ResultT],
    baseline_run: Callable[[], ResultT] | None,
    validate: Callable[[str, ResultT], str | None],
) -> InterleavedSamples[ResultT]:
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
                    candidate, baseline, f"{role}: {exc}"
                )
            detail = validate(role, result)
            if detail is not None:
                return InterleavedSamples(candidate, baseline, detail)
            (candidate if role == "candidate" else baseline).append(result)
    return InterleavedSamples(candidate, baseline, None)
