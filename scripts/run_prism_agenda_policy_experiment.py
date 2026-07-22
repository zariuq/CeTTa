#!/usr/bin/env python3
"""Run the bounded equal-hazard versus demand-hazard agenda contrast."""

from __future__ import annotations

import re
import statistics
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "cetta"
FIXTURE = (
    ROOT / "tests" / "prime" / "practical" /
    "prism_agenda_policy_contrast.metta"
)

COMPARISON = re.compile(
    r"\[\(prism:agenda:comparison\s+(\d+)\s+"
    r"\(prism:agenda:trial uniform checked (\d+) (\d+) (\d+)\)\s+"
    r"\(prism:agenda:trial demand checked (\d+) (\d+) (\d+)\)\)\]"
)


def fail(message: str, output: str = "") -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    if output:
        print(output, file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    if not BINARY.is_file():
        fail("CeTTa binary is absent; run the normal build first")

    try:
        completed = subprocess.run(
            [str(BINARY), "--lang", "prime", str(FIXTURE)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
            check=False,
        )
    except subprocess.TimeoutExpired as timeout:
        fail("agenda contrast exceeded its 15-second focused budget", timeout.stdout or "")

    output = completed.stdout
    if completed.returncode != 0:
        fail(f"CeTTa exited with status {completed.returncode}", output)
    if "(Error " in output:
        fail("agenda contrast produced an error", output)

    rows = [tuple(map(int, match)) for match in COMPARISON.findall(output)]
    expected_seeds = [1, 7, 19, 42]
    if [row[0] for row in rows] != expected_seeds:
        fail("four checked comparison rows were not produced", output)

    uniform_expanded = [row[1] for row in rows]
    demand_expanded = [row[4] for row in rows]
    if any(expanded > 20 for expanded in uniform_expanded + demand_expanded):
        fail("a run exceeded the declared hard expansion budget", output)
    if not all(demand <= uniform for demand, uniform in zip(demand_expanded, uniform_expanded)):
        fail("demand weighting expanded more states for at least one fixed seed", output)

    uniform_mean = statistics.fmean(uniform_expanded)
    demand_mean = statistics.fmean(demand_expanded)
    if demand_mean >= uniform_mean:
        fail("demand weighting did not improve mean expansions", output)

    reduction = 1.0 - demand_mean / uniform_mean
    print(
        "PrismAgendaPolicySummary "
        f"seeds={len(rows)} checked_uniform={len(rows)} checked_demand={len(rows)} "
        f"uniform_expanded={uniform_expanded} demand_expanded={demand_expanded} "
        f"uniform_mean={uniform_mean:.3f} demand_mean={demand_mean:.3f} "
        f"relative_reduction={reduction:.3f} hard_budget=20"
    )


if __name__ == "__main__":
    main()
