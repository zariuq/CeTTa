#!/usr/bin/env python3
"""Require every authored ZeroUV control rule to affect its witnesses."""

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
    chart: Path,
    quote_match: Path,
    query_kernel: Path,
    control: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "python3",
            str(harness),
            "--chart",
            str(chart),
            "--quote-match",
            str(quote_match),
            "--query-kernel",
            str(query_kernel),
            "--control",
            str(control),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--query-kernel", type=Path, required=True)
    parser.add_argument("--control", type=Path, required=True)
    arguments = parser.parse_args()

    harness = arguments.harness.resolve()
    chart = arguments.chart.resolve()
    quote_match = arguments.quote_match.resolve()
    query_kernel = arguments.query_kernel.resolve()
    control = arguments.control.resolve()
    baseline = run_harness(
        harness, chart, quote_match, query_kernel, control
    )
    if baseline.returncode != 0:
        raise GateFailure(
            "baseline conformance failed before mutation:\n"
            f"{baseline.stdout}{baseline.stderr}"
        )

    killed = 0
    survivors: list[str] = []
    with tempfile.TemporaryDirectory(
        prefix="zerouv-rule-mutations-"
    ) as raw:
        temporary = Path(raw)
        for rule_name in rule_names(control):
            mutation = temporary / f"{rule_name}.metta"
            mutation.write_text(
                without_rule(control, rule_name), encoding="utf-8"
            )
            result = run_harness(
                harness, chart, quote_match, query_kernel, mutation
            )
            if result.returncode == 0:
                survivors.append(rule_name)
            else:
                killed += 1
    if survivors:
        raise GateFailure(
            "semantic rules survived deletion: " + ", ".join(survivors)
        )
    print(f"(ZeroUVRuleMutationsV1Summary rules={killed} killed={killed})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
