#!/usr/bin/env python3
"""Check calibrated instruction comparisons, child memory accounting and cancellation."""
import argparse
import os
from pathlib import Path
import resource
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "benchmarks/mam_families"))
from run import paired_measure, runtime_counter_receipt, runtime_counters
from cost_gate import instruction_gate


def check_instruction_gate():
    def series(counts):
        return {i: dict(instructions=n, correct=True, rc=0, timed_out=False,
                        stdout_sha256="a" * 64) for i, n in enumerate(counts)}

    reference = series([10000] * 5)
    control = series([10001, 9998, 10003, 9996, 10002])
    # An unchanged program can have a positive paired median within its noise.
    same = instruction_gate(reference, control, reference, control)
    assert same["accepted"] and same["paired_median_percent"] > 0, same
    assert same["status"] == "no_detected_regression", same
    # A consistent increase well beyond the measured variation cannot pass.
    worse = instruction_gate(reference, series([10100] * 5), reference, control)
    assert worse["status"] == "regression" and not worse["accepted"], worse
    better = instruction_gate(reference, series([9900] * 5), reference, control)
    assert better["status"] == "improvement" and better["accepted"], better
    boundary = instruction_gate(reference, series([10001, 10020, 10001, 10020, 10001]),
                                reference, control)
    assert boundary["status"] == "inconclusive" and not boundary["accepted"], boundary
    # Missing measurements, wrong answers and mismatched observations stay open.
    for key, value in (("correct", False), ("instructions", None),
                       ("stdout_sha256", "b" * 64), ("timed_out", True)):
        broken = series([10000] * 5)
        broken[0][key] = value
        result = instruction_gate(reference, broken, reference, control)
        assert result["status"] == "unavailable" and not result["accepted"], result
    incomplete = series([10000] * 4)
    assert not instruction_gate(reference, incomplete, reference, control)["accepted"]
    # With deterministic counts there is no invented allowance for an increase.
    exact = instruction_gate(reference, series([10001] * 5), reference, reference)
    assert exact["status"] == "regression" and exact["calibration_percent"] == 0, exact
    print("PASS: calibrated paired instruction gate rejects regressions, uncertainty and invalid observations")


def check_runtime_counters():
    observed = runtime_counters(
        "diagnostic before\n"
        "runtime-counter region-attempt 3\n"
        "runtime-counter region-commit 2\n"
        "diagnostic after\n")
    assert observed == {"region-attempt": 3, "region-commit": 2}
    rejected = (
        "",
        "runtime-counter missing-value\n",
        "runtime-counter counter nope\n",
        "runtime-counter counter -1\n",
        "runtime-counter counter 1\nruntime-counter counter 1\n",
    )
    for text in rejected:
        try:
            runtime_counters(text)
        except ValueError:
            continue
        raise AssertionError(("invalid runtime receipt accepted", text))
    receipt = runtime_counter_receipt(
        "runtime-counter region-attempt 3\n"
        "runtime-counter unused 0\n"
        "runtime-counter runtime-statistics-recorded 1\n")
    assert receipt["runtime_counter_count"] == 3
    assert receipt["runtime_counters"] == {
        "region-attempt": 3, "runtime-statistics-recorded": 1}
    assert len(receipt["runtime_counter_sha256"]) == 64
    try:
        runtime_counter_receipt(
            "runtime-counter region-attempt 0\n"
            "runtime-counter runtime-statistics-recorded 0\n")
    except ValueError:
        pass
    else:
        raise AssertionError("non-instrumented runtime vector accepted")
    print("PASS: runtime receipt parser accepts one total nonnegative vector and rejects absent, malformed and duplicate rows")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    check_instruction_gate()
    check_runtime_counters()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # A measurement of the child must not inherit this launcher's high-water mark.
    retained = bytearray(64 << 20)
    assert retained[0] == 0
    parent_peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    measurements = []
    for size in (1, 128):
        result = paired_measure([sys.executable, "-c", f"b=bytearray({size}<<20); print(len(b))"],
                                out, dict(os.environ), out / f"memory-{size}", 10, None, False)
        assert result["rc"] == 0 and not result["timed_out"], result
        assert Path(result["stdout_file"]).read_text() == str(size << 20) + "\n"
        measurements.append(result)
    small, large = measurements
    assert small["rss_kib"] < parent_peak // 2, (parent_peak, small)
    assert large["rss_kib"] > small["rss_kib"] + (96 << 10), measurements
    precise = paired_measure(
        [sys.executable, "-c", "print(sum(range(1000000)))"],
        out, dict(os.environ), out / "precise-wall", 10, None, True)
    assert precise["rc"] == 0 and precise["perf_wall"] > 0, precise
    assert precise["instructions"] > 0 and precise["cycles"] > 0, precise
    timed = paired_measure([sys.executable, "-c", "import time; time.sleep(5)"],
                           out, dict(os.environ), out / "cancel", 0.1, None, False)
    assert timed["timed_out"] and timed["rc"] != 0 and timed["rss_kib"] is None, timed
    print("PASS: native child peak memory excludes launcher history; cancelled jobs retain no invented measurement")


if __name__ == "__main__":
    main()
