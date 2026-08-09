#!/usr/bin/env python3
"""Require every authored MeTTa Zero rule to affect its public witnesses."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from test_gslt_language_rule_mutations_v1 import rule_names, without_rule


class GateFailure(RuntimeError):
    pass


def run_harness(
    harness: Path,
    cetta: Path,
    chart: Path,
    sources: tuple[Path, Path, Path, Path],
    ground_capability: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "python3",
            str(harness),
            "--cetta",
            str(cetta),
            "--chart",
            str(chart),
            "--quote-match",
            str(sources[0]),
            "--query-kernel",
            str(sources[1]),
            "--observation",
            str(sources[2]),
            "--runner",
            str(sources[3]),
            "--ground-capability",
            str(ground_capability),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--query-kernel", type=Path, required=True)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--ground-capability", type=Path, required=True)
    arguments = parser.parse_args()

    harness = arguments.harness.resolve()
    cetta = arguments.cetta.resolve()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.query_kernel.resolve(),
        arguments.observation.resolve(),
        arguments.runner.resolve(),
    )
    ground_capability = arguments.ground_capability.resolve()
    baseline = run_harness(
        harness, cetta, chart, sources, ground_capability
    )
    if baseline.returncode != 0:
        raise GateFailure(
            "baseline conformance failed before mutation:\n"
            f"{baseline.stdout}{baseline.stderr}"
        )

    killed = 0
    survivors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="mettazero-rule-mutations-") as raw:
        temporary = Path(raw)
        all_sources = (*sources, ground_capability)
        for source_index, source in enumerate(all_sources):
            for rule_name in rule_names(source):
                mutation = temporary / f"{source_index}-{rule_name}.metta"
                mutation.write_text(
                    without_rule(source, rule_name), encoding="utf-8"
                )
                selected = list(sources)
                selected_ground = ground_capability
                if source_index < len(selected):
                    selected[source_index] = mutation
                else:
                    selected_ground = mutation
                result = run_harness(
                    harness,
                    cetta,
                    chart,
                    tuple(selected),
                    selected_ground,
                )
                if result.returncode == 0:
                    survivors.append(rule_name)
                else:
                    killed += 1
    if survivors:
        raise GateFailure(
            "semantic rules survived deletion: " + ", ".join(survivors)
        )
    print(
        f"(MettaZeroRuleMutationsV1Summary rules={killed} killed={killed})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
