#!/usr/bin/env python3
"""Require every new Zero interaction rule to affect a direct witness."""

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
    observation: Path,
    open_substitution: Path,
    support_indexed_abt: Path,
    interact: Path,
    provider: Path,
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
            "--observation",
            str(observation),
            "--open-substitution",
            str(open_substitution),
            "--support-indexed-abt",
            str(support_indexed_abt),
            "--interact",
            str(interact),
            "--provider",
            str(provider),
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
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--open-substitution", type=Path, required=True)
    parser.add_argument("--support-indexed-abt", type=Path, required=True)
    parser.add_argument("--interact", type=Path, required=True)
    parser.add_argument("--provider", type=Path, required=True)
    arguments = parser.parse_args()

    fixed = {
        "harness": arguments.harness.resolve(),
        "chart": arguments.chart.resolve(),
        "quote_match": arguments.quote_match.resolve(),
        "query_kernel": arguments.query_kernel.resolve(),
        "observation": arguments.observation.resolve(),
        "provider": arguments.provider.resolve(),
    }
    original_open = arguments.open_substitution.resolve()
    original_support = arguments.support_indexed_abt.resolve()
    original_interact = arguments.interact.resolve()
    baseline = run_harness(
        **fixed,
        open_substitution=original_open,
        support_indexed_abt=original_support,
        interact=original_interact,
    )
    if baseline.returncode != 0:
        raise GateFailure(
            "baseline conformance failed before mutation:\n"
            f"{baseline.stdout}{baseline.stderr}"
        )

    killed = 0
    survivors: list[str] = []
    with tempfile.TemporaryDirectory(
        prefix="mettazero-interact-rule-mutations-"
    ) as raw:
        temporary = Path(raw)
        for source_name, source in (
            ("open-substitution", original_open),
            ("support-indexed-abt", original_support),
            ("interact", original_interact),
        ):
            for rule_name in rule_names(source):
                mutation = temporary / f"{source_name}-{rule_name}.metta"
                mutation.write_text(
                    without_rule(source, rule_name), encoding="utf-8"
                )
                result = run_harness(
                    **fixed,
                    open_substitution=(
                        mutation
                        if source_name == "open-substitution"
                        else original_open
                    ),
                    support_indexed_abt=(
                        mutation
                        if source_name == "support-indexed-abt"
                        else original_support
                    ),
                    interact=(
                        mutation
                        if source_name == "interact"
                        else original_interact
                    ),
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
        f"(MettaZeroInteractRuleMutationsV1Summary "
        f"rules={killed} killed={killed})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
