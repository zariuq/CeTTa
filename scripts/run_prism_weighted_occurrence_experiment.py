#!/usr/bin/env python3
"""Run the finite weighted-occurrence and analytic CTMC experiment."""

from __future__ import annotations

import math
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "cetta"
FIXTURE = ROOT / "tests" / "prime" / "prism_weighted_occurrence.metta"

COUNTS = re.compile(
    r"\[\(prism:draw-counts\s+(\d+)\s+(\d+)\s+(\d+)\)\]"
)
TIMES = re.compile(
    r"\[\(prism:ctmc-times\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+(\d+)\)\]"
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
            timeout=30,
            check=False,
        )
    except subprocess.TimeoutExpired as timeout:
        fail("experiment exceeded its 30-second focused budget", timeout.stdout or "")

    output = completed.stdout
    if completed.returncode != 0:
        fail(f"CeTTa exited with status {completed.returncode}", output)
    if "(Error " in output:
        fail("a library law assertion failed", output)

    count_match = COUNTS.search(output)
    time_match = TIMES.search(output)
    if count_match is None or time_match is None:
        fail("machine-readable experiment observations are missing", output)

    left, right, draw_seed = map(int, count_match.groups())
    time_a = float(time_match.group(1))
    time_b = float(time_match.group(2))
    wait_seed = int(time_match.group(3))

    draws = left + right
    if draws != 128:
        fail(f"expected 128 occurrence draws, observed {draws}", output)
    if draw_seed != wait_seed:
        fail("equal-length pure runs did not consume the same explicit seed count", output)

    expected_left_probability = 1.0 / 4.0
    standard_deviation = math.sqrt(
        draws * expected_left_probability * (1.0 - expected_left_probability)
    )
    selection_z = abs(left - draws * expected_left_probability) / standard_deviation
    if selection_z > 4.0:
        fail(f"competing-hazard count is {selection_z:.3f} standard deviations away", output)

    total_time = time_a + time_b
    if not math.isfinite(total_time) or total_time <= 0.0:
        fail("CTMC dwell time is not finite and positive", output)
    occupancy_a = time_a / total_time
    expected_occupancy_a = 3.0 / 4.0
    occupancy_error = abs(occupancy_a - expected_occupancy_a)
    if occupancy_error > 0.08:
        fail(f"two-state CTMC occupancy error is {occupancy_error:.6f}", output)

    acknowledgement_count = sum(line == "[()]" for line in output.splitlines())
    if acknowledgement_count != 12:
        fail(
            "expected one import and eleven law acknowledgements, "
            f"observed {acknowledgement_count}",
            output,
        )
    law_assertions = acknowledgement_count - 1

    print(
        "PrismWeightedOccurrenceSummary "
        f"law_assertions={law_assertions} import_acknowledgements=1 "
        f"draws={draws} left={left} right={right} "
        f"selection_z={selection_z:.6f} occupancy_a={occupancy_a:.6f} "
        f"expected_occupancy_a={expected_occupancy_a:.6f} "
        f"occupancy_error={occupancy_error:.6f} seed={draw_seed}"
    )


if __name__ == "__main__":
    main()
