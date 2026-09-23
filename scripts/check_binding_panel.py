#!/usr/bin/env python3
"""Qualify contextual bindings on the standing PeTTa and HE workloads."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--petta-examples", type=Path,
                        default=ROOT.parent / "PeTTa" / "examples")
    parser.add_argument("--check-reference", action="store_true",
                        help="also require recorded wall/RSS references to be met")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = digest(binary)
    manifest = ROOT / "benchmarks/inference_cost_axes/standing_panel.json"
    cases = json.loads(manifest.read_text())
    # Validate every input before starting the measured sequence.
    for case in cases:
        source = (args.petta_examples / case["petta_example"]
                  if "petta_example" in case else ROOT / case["source"])
        case["resolved_source"] = source.resolve(strict=True)
        case["source_sha256"] = digest(case["resolved_source"])
        if "expected" in case:
            case["stdout_sha256"] = digest(ROOT / case["expected"])
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"binary_sha256": identity, "manifest_sha256": digest(manifest),
              "results": [], "binary_unchanged": True}
    passed = True
    for case in cases:
        for repetition in range(case.get("repetitions", 1)):
            name = case["name"] + (f"-repeat-{repetition}" if repetition else "")
            stdout_path = args.output / f"{name}.out"
            stderr_path = args.output / f"{name}.err"
            command = [str(binary), "--lang", case["lang"],
                       *case.get("arguments", []), str(case["resolved_source"])]
            metrics_path = args.output / f"{name}.metrics"
            # Measure the executable after exec, rather than inheriting the Python
            # parent's high-water RSS from fork (os.wait4 on Popen would do so).
            measured = ["/usr/bin/time", "-f", "%e %M", "-o", str(metrics_path.resolve()),
                        *command]
            with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                process = subprocess.Popen(measured, cwd=ROOT, stdout=stdout,
                                           stderr=stderr, start_new_session=True)
                try:
                    process.wait()
                except BaseException:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                    raise
            metrics = metrics_path.read_text().splitlines()[-1].split()
            elapsed, rss_kib = float(metrics[0]), int(metrics[1])
            actual = digest(stdout_path)
            correct = process.returncode == 0 and actual == case["stdout_sha256"]
            reference = case.get("reference", {})
            reference_met = (
                elapsed <= reference.get("wall_seconds", float("inf")) and
                rss_kib <= reference.get("rss_kib", float("inf")))
            result = dict(name=name, lang=case["lang"], rc=process.returncode,
                          actual=actual, expected=case["stdout_sha256"],
                          correct=correct, wall_seconds=elapsed,
                          rss_kib=rss_kib, source_sha256=case["source_sha256"],
                          source_unchanged=digest(case["resolved_source"]) == case["source_sha256"],
                          reference=reference,
                          reference_met=reference_met)
            report["results"].append(result)
            report["binary_unchanged"] = digest(binary) == identity
            (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
            accepted = correct and result["source_unchanged"] and (
                reference_met or not args.check_reference)
            passed = passed and accepted and report["binary_unchanged"]
            print(f"{name}: {'PASS' if accepted else 'FAIL'} "
                  f"wall={elapsed:.3f}s rss={rss_kib}KiB", flush=True)
            if not correct or not result["source_unchanged"] or not report["binary_unchanged"]:
                return 1
    print(f"BindingPanel checks={len(report['results'])} "
          f"correct={sum(r['correct'] for r in report['results'])}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
