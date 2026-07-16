#!/usr/bin/env python3
"""Check Prime verdict stability over increasing explicit producer budgets."""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import re
import subprocess
import sys


OBSERVATION = re.compile(
    r"^\[\(BudgetObservation ([^ ]+) ([0-9]+) "
    r"(Established|Refuted|Undetermined|Incomplete)\)\]$"
)
DETERMINATE = {"Established", "Refuted"}


def validate(groups: dict[str, list[tuple[int, str]]]) -> list[str]:
    errors: list[str] = []
    for label, samples in sorted(groups.items()):
        if len(samples) < 2:
            errors.append(f"{label}: needs at least two budget samples")
            continue
        previous_budget = -1
        settled: str | None = None
        for budget, status in samples:
            if budget <= previous_budget:
                errors.append(f"{label}: budgets are not strictly increasing")
            previous_budget = budget
            if settled is not None and status != settled:
                errors.append(
                    f"{label}: determinate {settled} changed to {status} "
                    f"at budget {budget}"
                )
            if status in DETERMINATE and settled is None:
                settled = status
    return errors


def self_test() -> None:
    positive = {
        "establish": [(1, "Incomplete"), (2, "Established"),
                       (4, "Established")],
        "refute": [(1, "Undetermined"), (2, "Refuted"), (4, "Refuted")],
        "open": [(1, "Incomplete"), (2, "Undetermined")],
    }
    if validate(positive):
        raise RuntimeError("monotonicity validator rejected its positive control")
    negative = {"flip": [(1, "Established"), (2, "Refuted")]}
    if not validate(negative):
        raise RuntimeError("monotonicity validator accepted a determinate flip")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_prime_budget_monotonicity.py CETTA", file=sys.stderr)
        return 2
    self_test()
    root = Path(__file__).resolve().parents[1]
    fixture = root / "tests" / "support" / "prime_02_budget_monotonicity.metta"
    completed = subprocess.run(
        [sys.argv[1], "--lang", "prime", str(fixture)],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        print(completed.stdout, end="", file=sys.stderr)
        return completed.returncode

    groups: dict[str, list[tuple[int, str]]] = defaultdict(list)
    unexpected: list[str] = []
    for line in completed.stdout.splitlines():
        match = OBSERVATION.fullmatch(line)
        if not match:
            unexpected.append(line)
            continue
        label, budget, status = match.groups()
        groups[label].append((int(budget), status))

    required = {
        "OpenExpected", "FormNat", "SynthZ", "CheckZ", "RejectTruth", "RefineNat",
        "MayOnly", "MustChoose", "ConvertArithmetic", "DelayedCounterexample",
    }
    errors = validate(groups)
    if set(groups) != required:
        errors.append(
            "observation labels differ: "
            f"expected={sorted(required)} actual={sorted(groups)}"
        )
    if unexpected:
        errors.append(f"unexpected output: {unexpected!r}")

    statuses = {status for samples in groups.values() for _, status in samples}
    if not {"Established", "Refuted", "Undetermined", "Incomplete"} <= statuses:
        errors.append(f"not all four statuses were exercised: {sorted(statuses)}")
    delayed = [status for _, status in groups.get("DelayedCounterexample", [])]
    if "Incomplete" not in delayed or "Refuted" not in delayed:
        errors.append(
            "delayed counterexample did not exercise incomplete-to-refuted"
        )

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "PASS: Prime budget monotonicity "
        f"({len(groups)} judgment families, "
        f"{sum(len(samples) for samples in groups.values())} observations)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
