#!/usr/bin/env python3
"""Require every authored semantic rule to affect the conformance witnesses."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


def symbol_text(value: sx.SExpr) -> str | None:
    return value.text if isinstance(value, sx.Symbol) else None


def rule_names(path: Path) -> list[str]:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise GateFailure(f"{path}: expected one presentation")
    for section in forms[0][1:]:
        if (
            isinstance(section, tuple)
            and section
            and symbol_text(section[0]) == "rewrites"
        ):
            names: list[str] = []
            for rule in section[1:]:
                if (
                    not isinstance(rule, tuple)
                    or len(rule) < 2
                    or symbol_text(rule[0]) != "rule"
                    or symbol_text(rule[1]) is None
                ):
                    raise GateFailure(f"{path}: malformed rule")
                names.append(symbol_text(rule[1]) or "")
            return names
    raise GateFailure(f"{path}: presentation has no rewrites")


def without_rule(path: Path, name: str) -> str:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    root = forms[0]
    assert isinstance(root, tuple)
    changed = False
    sections: list[sx.SExpr] = []
    for section in root[1:]:
        if (
            isinstance(section, tuple)
            and section
            and symbol_text(section[0]) == "rewrites"
        ):
            retained: list[sx.SExpr] = [section[0]]
            for rule in section[1:]:
                assert isinstance(rule, tuple)
                if symbol_text(rule[1]) == name:
                    changed = True
                else:
                    retained.append(rule)
            sections.append(tuple(retained))
        else:
            sections.append(section)
    if not changed:
        raise GateFailure(f"{path}: rule {name} was not found")
    return sx.render((root[0], *sections)) + "\n"


def run_harness(
    harness: Path,
    chart: Path,
    core: Path,
    observation: Path,
    public_observation: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "python3",
            str(harness),
            "--chart",
            str(chart),
            "--core",
            str(core),
            "--observation",
            str(observation),
            "--public-observation",
            str(public_observation),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--public-observation", type=Path, required=True)
    arguments = parser.parse_args()

    sources = [
        arguments.core.resolve(),
        arguments.observation.resolve(),
        arguments.public_observation.resolve(),
    ]
    baseline = run_harness(
        arguments.harness.resolve(),
        arguments.chart.resolve(),
        *sources,
    )
    if baseline.returncode != 0:
        raise GateFailure(
            "baseline conformance failed before mutation:\n"
            f"{baseline.stdout}{baseline.stderr}"
        )

    survivors: list[str] = []
    killed = 0
    with tempfile.TemporaryDirectory(prefix="gslt-rule-mutations-") as raw:
        temporary = Path(raw)
        for source_index, source in enumerate(sources):
            for rule_name in rule_names(source):
                mutation = temporary / f"{source_index}-{rule_name}.metta"
                mutation.write_text(
                    without_rule(source, rule_name), encoding="utf-8"
                )
                selected = list(sources)
                selected[source_index] = mutation
                result = run_harness(
                    arguments.harness.resolve(),
                    arguments.chart.resolve(),
                    *selected,
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
        f"(GsltLanguageRuleMutationsV1Summary rules={killed} killed={killed})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
