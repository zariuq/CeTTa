#!/usr/bin/env python3
"""Check actual cache production versus repeated delivery on the shared Need path."""

from collections import Counter
from pathlib import Path
import os
import subprocess

from run_need_equation_call_tournament import (
    Answer, event_field, parse_one, require, validate_native_receipts,
)


ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("CETTA_BIN", ROOT / "cetta"))
SOURCE = ROOT / "tests/prime/need_production_history.metta"


def main():
    command = [str(BINARY), "--lang", "prime"]
    plain = subprocess.run(command + [str(SOURCE)], capture_output=True, text=True)
    traced = subprocess.run(command + ["--emit-prime-need-trace", str(SOURCE)],
                            capture_output=True, text=True)
    require(plain.returncode == 0, f"plain execution failed: {plain.stderr}")
    require(traced.returncode == 0, f"recorded execution failed: {traced.stderr}")
    require(plain.stdout == traced.stdout, "recording changed values or duplicate occurrences")
    answers = []
    for line in traced.stderr.splitlines():
        if line.startswith("(prime-need:answer"):
            node = parse_one(line)
            answers.append(Answer(int(node[1]), int(node[2]), node[3][1], node[4]))
    validate_native_receipts({1: answers})
    require(Counter(answer.value for answer in answers) ==
            Counter({"(Pair 3 3)": 2, "(Pair 6 6)": 1}),
            "shared producer alternatives or equal physical rows were lost")
    producer_keys = []
    cell_keys = []
    for answer in answers:
        deliveries = answer.events_named("observe-cell")
        require(len(deliveries) == 2, "each Pair needs two distinct cell deliveries")
        require(len({event[1] for event in deliveries}) == 2,
                "two uses were identified as one occurrence")
        require([event_field(event, "production") for event in deliveries] == ["True", "False"],
                "the actual return must produce once and its cached use must not reproduce")
        keys = {(event_field(event, "need-session"), event_field(event, "producer"))
                for event in deliveries}
        require(len(keys) == 1 and all(key[1] is not None for key in keys),
                "the two deliveries do not name the same actual completed update")
        producer_keys.append(next(iter(keys)))
        cell_keys.append((event_field(deliveries[0], "need-session"),
                          event_field(deliveries[0], "cell")))
    require(len(set(cell_keys)) == 1, "the test ceased to fork alternatives of one shared cell")
    require(len(set(producer_keys)) == 3,
            "distinct alternative productions were collapsed by cell identity or equal payload")
    print("(PrimeNeedProductionHistorySummary answers 3 productions 3 deliveries 6)")


if __name__ == "__main__":
    main()
