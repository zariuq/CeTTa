#!/usr/bin/env python3
"""Require every authored GSLT-IL rule to affect its witnesses."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from test_gslt_language_rule_mutations_v1 import rule_names, without_rule


class GateFailure(RuntimeError):
    pass


def run_harness(
    harness: Path, chart: Path, semantics: Path
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "python3",
            str(harness),
            "--chart",
            str(chart),
            "--semantics",
            str(semantics),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--semantics", type=Path, required=True)
    arguments = parser.parse_args()

    harness = arguments.harness.resolve()
    chart = arguments.chart.resolve()
    semantics = arguments.semantics.resolve()
    baseline = run_harness(harness, chart, semantics)
    if baseline.returncode != 0:
        raise GateFailure(
            "baseline conformance failed before mutation:\n"
            f"{baseline.stdout}{baseline.stderr}"
        )

    killed = 0
    survivors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="gslt-il-rule-mutations-") as raw:
        temporary = Path(raw)
        for rule_name in rule_names(semantics):
            mutation = temporary / f"{rule_name}.metta"
            mutation.write_text(
                without_rule(semantics, rule_name), encoding="utf-8"
            )
            result = run_harness(harness, chart, mutation)
            if result.returncode == 0:
                survivors.append(rule_name)
            else:
                killed += 1
    if survivors:
        raise GateFailure(
            "semantic rules survived deletion: " + ", ".join(survivors)
        )
    print(f"(GsltIlRuleMutationsV1Summary rules={killed} killed={killed})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
