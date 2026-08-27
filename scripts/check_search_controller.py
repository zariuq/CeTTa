#!/usr/bin/env python3
"""Qualify controller order, fairness, multiplicity, and effect fallback."""

from __future__ import annotations

from collections import Counter
import os
from pathlib import Path
import subprocess
import sys

from petta_machine_stats import parse_controller_stats_line


ROOT = Path(__file__).resolve().parents[1]
PETTA = ROOT / "tests" / "petta"


def run(binary: Path, fixture: str, *, controller: str, limit: int | None = None,
        stats: bool = False) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["CETTA_SEARCH_CONTROLLER"] = controller
    if limit is None:
        env.pop("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", None)
    else:
        env["CETTA_PETTA_MACHINE_TRANSITION_LIMIT"] = str(limit)
    if stats:
        env["CETTA_PETTA_MACHINE_STATS"] = "1"
    else:
        env.pop("CETTA_PETTA_MACHINE_STATS", None)
    return subprocess.run(
        [str(binary), "--lang", "petta", str(PETTA / fixture)],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
        timeout=30,
        check=False,
    )


def expected(name: str) -> str:
    return (PETTA / name).read_text(encoding="utf-8")


def require_run(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"{label}: exit {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def main() -> int:
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "cetta").resolve()

    inline = run(
        binary, "search_controller_fifo_order.metta",
        controller="inline-depth-first",
    )
    require_run(inline, "finite inline-depth-first")
    inline_expected = expected("search_controller_fifo_order.expected")
    if inline.stdout != inline_expected:
        raise AssertionError(
            "finite inline-depth-first stream differs\n"
            f"expected:\n{inline_expected}actual:\n{inline.stdout}"
        )

    fifo_order = run(
        binary, "search_controller_fifo_order.metta",
        controller="fifo", stats=True,
    )
    require_run(fifo_order, "finite FIFO")
    fifo_expected = expected(
        "search_controller_fifo_order.fifo.expected")
    if fifo_order.stdout != fifo_expected:
        raise AssertionError(
            "finite FIFO stream differs\n"
            f"expected:\n{fifo_expected}actual:\n{fifo_order.stdout}"
        )
    if inline.stdout == fifo_order.stdout:
        raise AssertionError(
            "asymmetric controller canary did not distinguish streams"
        )
    if Counter(inline.stdout.splitlines()) != Counter(
        fifo_order.stdout.splitlines()
    ):
        raise AssertionError(
            "completed controllers disagree on the occurrence bag"
        )
    fifo_order_receipts = [
        line for line in fifo_order.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(fifo_order_receipts) != 2:
        raise AssertionError(
            "finite FIFO run did not emit one receipt per query"
        )
    order_receipt = parse_controller_stats_line(
        fifo_order_receipts[0])
    if order_receipt.get("admitted") != 1 or (
        order_receipt.get("active") != "fifo"
    ) or int(order_receipt.get("expansions", 0)) == 0:
        raise AssertionError(
            "asymmetric FIFO query was not actively frontier-scheduled"
        )

    fifo = run(
        binary, "search_controller_fifo_starvation.metta",
        controller="fifo", limit=100, stats=True,
    )
    require_run(fifo, "bounded FIFO starvation witness")
    starvation_expected = expected(
        "search_controller_fifo_starvation.expected")
    if fifo.stdout != starvation_expected:
        raise AssertionError(
            "bounded FIFO starvation witness changed\n"
            f"expected:\n{starvation_expected}actual:\n{fifo.stdout}"
        )
    stats_lines = [
        line for line in fifo.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(stats_lines) != 1:
        raise AssertionError("FIFO run did not emit one controller receipt")
    receipt = parse_controller_stats_line(stats_lines[0])
    expected_receipt = {
        "requested": "fifo",
        "admitted": 1,
        "active": "fifo",
        "inline_fallbacks": 0,
        "answers": 13,
    }
    for field, expected_value in expected_receipt.items():
        if receipt.get(field) != expected_value:
            raise AssertionError(
                f"FIFO controller receipt has {field}="
                f"{receipt.get(field)!r}, expected {expected_value!r}"
            )

    depth_first = run(
        binary, "search_controller_fifo_starvation.metta",
        controller="inline-depth-first", limit=100,
    )
    require_run(depth_first, "bounded depth-first starvation witness")
    if depth_first.stdout:
        raise AssertionError(
            "depth-first starvation witness unexpectedly emitted an answer:\n"
            + depth_first.stdout
        )

    effect_expected = run(
        binary, "search_controller_effect_fallback.metta",
        controller="inline-depth-first",
    )
    require_run(effect_expected, "effect baseline")
    effect_golden = expected(
        "search_controller_effect_fallback.expected")
    if effect_expected.stdout != effect_golden:
        raise AssertionError(
            "effect baseline changed\n"
            f"expected:\n{effect_golden}actual:\n{effect_expected.stdout}"
        )
    effect_fifo = run(
        binary, "search_controller_effect_fallback.metta",
        controller="fifo", stats=True,
    )
    require_run(effect_fifo, "effect fallback")
    if effect_fifo.stdout != effect_golden:
        raise AssertionError(
            "effectful root changed under controller request\n"
            f"expected:\n{effect_golden}fifo:\n{effect_fifo.stdout}"
        )
    effect_receipts = [
        line for line in effect_fifo.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(effect_receipts) != 1:
        raise AssertionError(
            "effectful root was not visibly declined by FIFO admission"
        )
    effect_receipt = parse_controller_stats_line(effect_receipts[0])
    if effect_receipt.get("admitted") != 0 or (
        effect_receipt.get("active") != "inline-depth-first"
    ):
        raise AssertionError(
            "effectful root was not visibly declined by FIFO admission"
        )

    print("PASS: controller streams differ and occurrence bags agree exactly")
    print("PASS: FIFO reaches the bounded DFS-starvation witness")
    print("PASS: effectful roots visibly retain inline semantics")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
