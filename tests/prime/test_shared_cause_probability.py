#!/usr/bin/env python3
"""Prime-to-world-model shared-cause probability discriminator."""

from __future__ import annotations

from collections import Counter, defaultdict
from fractions import Fraction
from pathlib import Path
import os
import subprocess

from run_need_equation_call_tournament import (
    Answer,
    event_field,
    parse_one,
    require,
    validate_native_receipts,
)


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/prime/shared_cause_probability.metta"
BINARY = Path(os.environ.get("CETTA_BIN", ROOT / "cetta"))


def run_native():
    common = [str(BINARY), "--lang", "prime"]
    plain = subprocess.run(
        common + [str(SOURCE)], text=True, capture_output=True, check=False
    )
    traced = subprocess.run(
        common + ["--emit-prime-need-trace", str(SOURCE)],
        text=True,
        capture_output=True,
        check=False,
    )
    require(plain.returncode == 0, f"plain run failed: {plain.stderr}")
    require(traced.returncode == 0, f"traced run failed: {traced.stderr}")
    require(
        plain.stdout == traced.stdout,
        "enabling causal receipts changed the native answer stream",
    )

    forms: dict[int, list[Answer]] = defaultdict(list)
    for line in traced.stderr.splitlines():
        if not line.startswith("(prime-need:answer"):
            continue
        node = parse_one(line)
        form = int(node[1])
        forms[form].append(Answer(form, int(node[2]), node[3][1], node[4]))
    validate_native_receipts(forms)
    return forms


def main() -> int:
    forms = run_native()
    answers = [
        answer for answer in forms[1]
        if answer.events_named("use-equation")
    ]
    require(
        Counter(answer.value for answer in answers) == Counter({"goal": 2}),
        "the two admitted rule occurrences did not publish two goal explanations",
    )
    rules = {
        event_field(event, "rule")
        for answer in answers
        for event in answer.events_named("use-equation")
    }
    require(len(rules) == 2, "distinct rule occurrences were collapsed")

    observations = []
    for answer in answers:
        observed = answer.events_named("observe-cell")
        require(len(observed) == 1, "a goal receipt lacks one exact observation")
        event = observed[0]
        observations.append((
            event_field(event, "need-session"),
            event_field(event, "cell"),
            event_field(event, "after"),
        ))
    require(
        len(set(observations)) == 1,
        "the two explanations do not share one producer outcome",
    )

    fair_heads = Fraction(1, 2)
    dependence_aware = fair_heads
    naive_independent_or = 1 - (1 - fair_heads) ** len(answers)
    require(dependence_aware == Fraction(1, 2), "shared-world probability changed")
    require(
        naive_independent_or == Fraction(3, 4),
        "the independent-explanation discriminator changed",
    )
    require(
        dependence_aware != naive_independent_or,
        "shared-cause identity failed to discriminate probability aggregation",
    )
    print(
        "(PrimeSharedCauseProbabilitySummary "
        "native-explanations 2 shared-producer-outcomes 1 "
        "dependence-aware 1/2 naive-independent-or 3/4)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
