#!/usr/bin/env python3
"""Compare C, linked x86 and hybrid kernels on the same basic programs.

These are whole-process measurements, including startup, parsing and output.
Statistics are disabled in timed runs. Every result must equal the C result;
the separate kernel corpus and image checks establish broader conformance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
MODES = ("c", "x86", "hybrid")
CASES = (
    ("typed forward chaining", ("--lang", "tcalc"), "tests/tcalc/typed_rules_by_depth.tcalc"),
    ("unary GCD 9,6", ("--lang", "tcalc", "--profile", "step=one"), "tests/tcalc/chr_gcd.chr"),
    ("2x3 tile puzzle", ("--lang", "tcalc"), "tests/tcalc/tiles_2x3.tcalc"),
    ("transform reads and joins", ("--lang", "petta", "--profile", "extended", "--no-results"),
     "tests/petta/transform_read_batch.metta"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=31)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0 or args.timeout <= 0:
        parser.error("runs and timeout must be positive; warmups must be nonnegative")
    binary = args.binary.resolve()
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    rows = []
    for name, options, fixture in CASES:
        reference = None
        samples = {mode: [] for mode in MODES}
        for iteration in range(args.warmups + args.runs):
            # Rotate order so one mode is not always measured first.
            offset = iteration % len(MODES)
            for mode in MODES[offset:] + MODES[:offset]:
                started = time.perf_counter_ns()
                result = subprocess.run(
                    [str(binary), "--kernel", mode, *options, fixture],
                    cwd=ROOT, capture_output=True, timeout=args.timeout, check=True,
                )
                elapsed = (time.perf_counter_ns() - started) / 1e6
                observation = (result.stdout, result.stderr)
                if reference is None:
                    assert mode == "c"
                    reference = observation
                if observation != reference:
                    raise RuntimeError(f"{name}: {mode} differs from C")
                if iteration >= args.warmups:
                    samples[mode].append(elapsed)
        medians = {mode: statistics.median(values) for mode, values in samples.items()}
        row = {"case": name, "fixture": fixture, "median_ms": medians,
               "relative_to_c": {mode: value / medians["c"] for mode, value in medians.items()},
               "samples_ms": samples, "equal_results": True}
        rows.append(row)
        print(name + ": " + ", ".join(f"{mode}={medians[mode]:.3f} ms" for mode in MODES), flush=True)
    if hashlib.sha256(binary.read_bytes()).hexdigest() != digest:
        raise RuntimeError("benchmark executable changed during measurement")
    record = {"binary_sha256": digest, "runs": args.runs, "warmups": args.warmups,
              "timing": "whole process; startup, parsing and output included; statistics disabled",
              "cases": rows}
    with args.out.open("x") as output:
        json.dump(record, output, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
