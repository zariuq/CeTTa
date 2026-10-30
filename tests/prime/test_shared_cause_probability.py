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


def run_native(source=SOURCE):
    common = [str(BINARY), "--lang", "prime"]
    plain = subprocess.run(
        common + [str(source)], text=True, capture_output=True, check=False
    )
    traced = subprocess.run(
        common + ["--emit-prime-need-trace", str(source)],
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


def cause_outcome(answer):
    observed = answer.events_named("observe-cell")
    require(observed, "a goal receipt has no source observation")
    # A receipt can retain both production and later delivery of that same
    # immutable update. Event count is not a count of independent causes.
    require(all(event_field(event, "after") == "heads" for event in observed),
            "a goal was justified by a different outcome")
    identities = {
        (event_field(event, "need-session"), event_field(event, "producer"))
        for event in observed
    }
    require(len(identities) == 1, "one explanation combines distinct source outcomes")
    identity = next(iter(identities))
    require(all(part not in (None, "0") for part in identity),
            "an observation does not identify a completed producer outcome")
    return identity


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

    observations = [cause_outcome(answer) for answer in answers]
    cells = {(event_field(event, "need-session"), event_field(event, "cell"))
             for answer in answers for event in answer.events_named("observe-cell")}
    require(len(cells) == 1, "the shared fixture no longer uses one argument cell")
    require(
        len(set(observations)) == 1,
        "the two explanations do not share one producer outcome",
    )

    independent = run_native(ROOT / "tests/prime/shared_cause_probability_independent.metta")
    separate = [answer for answer in independent[1] if answer.value == "goal"]
    require(len(separate) == 2, "the independent control lost a goal occurrence")
    require(len({cause_outcome(answer) for answer in separate}) == 2,
            "equal outcomes of independent cells were merged into one cause")

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
