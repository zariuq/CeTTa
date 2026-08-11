#!/usr/bin/env python3
"""Require every authored MeTTa-Interact rule to affect its witnesses."""

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
    core: Path,
    sequence: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "python3",
            str(harness),
            "--chart",
            str(chart),
            "--quote-match",
            str(quote_match),
            "--core",
            str(core),
            "--sequence",
            str(sequence),
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
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--sequence", type=Path, required=True)
    arguments = parser.parse_args()

    harness = arguments.harness.resolve()
    chart = arguments.chart.resolve()
    quote_match = arguments.quote_match.resolve()
    original_core = arguments.core.resolve()
    original_sequence = arguments.sequence.resolve()
    baseline = run_harness(
        harness, chart, quote_match, original_core, original_sequence
    )
    if baseline.returncode != 0:
        raise GateFailure(
            "baseline conformance failed before mutation:\n"
            f"{baseline.stdout}{baseline.stderr}"
        )

    killed = 0
    survivors: list[str] = []
    with tempfile.TemporaryDirectory(
        prefix="metta-interact-rule-mutations-"
    ) as raw:
        temporary = Path(raw)
        for source_name, source in (
            ("core", original_core),
            ("sequence", original_sequence),
        ):
            for rule_name in rule_names(source):
                mutation = temporary / f"{source_name}-{rule_name}.metta"
                mutation.write_text(
                    without_rule(source, rule_name), encoding="utf-8"
                )
                core = mutation if source_name == "core" else original_core
                sequence = (
                    mutation if source_name == "sequence" else original_sequence
                )
                result = run_harness(
                    harness, chart, quote_match, core, sequence
                )
                if result.returncode == 0:
                    survivors.append(f"{source_name}:{rule_name}")
                else:
                    killed += 1
    if survivors:
        raise GateFailure(
            "semantic rules survived deletion: " + ", ".join(survivors)
        )
    print(
        f"(MettaInteractionRuleMutationsV1Summary "
        f"rules={killed} killed={killed})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
