#!/usr/bin/env python3
"""Compare matched builds on type-interface workloads, checking every answer."""
import argparse
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--lang", nargs="+", choices=("petta", "he"), default=["petta"])
    parser.add_argument("--interface-cases", action="store_true",
                        help="include cold signature families and a warm polymorphic call")
    args = parser.parse_args()
    if args.runs < 3:
        parser.error("at least three runs are required")
    binaries = (args.before.resolve(), args.after.resolve())
    workloads = []
    for name in ("untyped_numbers", "typed_numbers", "expression_guard"):
        source = Path(__file__).resolve().with_name(name + ".metta")
        workloads.append((name, [str(source)], ["100000"]))
    if args.interface_cases:
        for typed in (False, True):
            lines = []
            for index in range(250):
                head = f"pcg-cold-{index}"
                if typed:
                    lines.append(f"(: {head} (-> Number Number))")
                lines.extend((f"(= ({head} $x) $x)", f"!({head} 42)"))
            workloads.append(("cold_typed" if typed else "cold_untyped",
                              ["-e", "\n".join(lines)], ["42"] * 250))
        source = Path(__file__).resolve().with_name("polymorphic_guard.metta")
        workloads.append(("polymorphic_guard", [str(source)], ["10000"]))
    for language in args.lang:
        for workload, source_args, answers in workloads:
            expected = "\n".join(f"[{answer}]" if language == "he" else answer
                                 for answer in answers)
            samples = ([], [])
            for index in range(args.runs + 1):
                # Alternate order and exclude the initial warmup pair.
                for side in ((0, 1) if index % 2 == 0 else (1, 0)):
                    start = time.perf_counter()
                    result = subprocess.run(
                        [str(binaries[side]), "--lang", language, *source_args],
                        text=True, capture_output=True, timeout=60, check=True,
                    )
                    elapsed = time.perf_counter() - start
                    if result.stdout.strip() != expected or result.stderr:
                        raise RuntimeError(f"{workload}: unexpected output {result!r}")
                    if index:
                        samples[side].append(elapsed)
            before, after = (statistics.median(sample) for sample in samples)
            print(f"{language}/{workload}\tbefore={before:.6f}s\tafter={after:.6f}s"
                  f"\tbefore/after={before / after:.3f}x"
                  f"\tbefore_range={min(samples[0]):.6f}..{max(samples[0]):.6f}s"
                  f"\tafter_range={min(samples[1]):.6f}..{max(samples[1]):.6f}s", flush=True)


if __name__ == "__main__":
    main()
