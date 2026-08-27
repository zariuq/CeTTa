#!/usr/bin/env python3
"""Qualify controller order, fairness, multiplicity, and refusal."""

from __future__ import annotations

from collections import Counter
import os
from pathlib import Path
import subprocess
import sys

from petta_machine_stats import parse_controller_stats_line


ROOT = Path(__file__).resolve().parents[1]
PETTA = ROOT / "tests" / "petta"
PRIME = ROOT / "tests" / "prime"


def run(binary: Path, fixture: str, *, controller: str | None,
        limit: int | None = None,
        stats: bool = False, language: str = "petta",
        fixture_root: Path = PETTA,
        forced_gc: bool = False) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if controller is None:
        env.pop("CETTA_SEARCH_CONTROLLER", None)
    else:
        env["CETTA_SEARCH_CONTROLLER"] = controller
    if limit is None:
        env.pop("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", None)
    else:
        env["CETTA_PETTA_MACHINE_TRANSITION_LIMIT"] = str(limit)
    if stats:
        env["CETTA_PETTA_MACHINE_STATS"] = "1"
    else:
        env.pop("CETTA_PETTA_MACHINE_STATS", None)
    if forced_gc:
        env["CETTA_GC_BUDGET_MB"] = "1"
    else:
        env.pop("CETTA_GC_BUDGET_MB", None)
    return subprocess.run(
        [str(binary), "--lang", language, str(fixture_root / fixture)],
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


def require_measured_controller_work(
    receipt: dict[str, int | str], label: str
) -> None:
    pairs = (
        ("captures", "capture_elapsed_ns"),
        ("restores", "restore_elapsed_ns"),
        ("expansions", "expansion_elapsed_ns"),
    )
    measured = False
    for count_name, elapsed_name in pairs:
        count = int(receipt.get(count_name, 0))
        elapsed = int(receipt.get(elapsed_name, 0))
        if count > 0:
            measured = True
            if elapsed <= 0:
                raise AssertionError(
                    f"{label}: {count_name}={count} but "
                    f"{elapsed_name}={elapsed}"
                )
        elif elapsed != 0:
            raise AssertionError(
                f"{label}: {count_name}=0 but {elapsed_name}={elapsed}"
            )
    if not measured:
        raise AssertionError(f"{label}: no controller representation work")


def main() -> int:
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "cetta").resolve()

    reference = run(
        binary, "search_controller_fifo_order.metta",
        controller=None, stats=True,
    )
    require_run(reference, "unselected reference execution")
    reference_expected = expected("search_controller_fifo_order.expected")
    if reference.stdout != reference_expected:
        raise AssertionError(
            "unselected reference execution changed\n"
            f"expected:\n{reference_expected}actual:\n{reference.stdout}"
        )
    if any(
        line.startswith("CETTA_CONTROLLER_STATS ")
        for line in reference.stderr.splitlines()
    ):
        raise AssertionError(
            "unselected reference execution activated a controller"
        )

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
    require_measured_controller_work(order_receipt, "asymmetric FIFO query")

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
        "refusals": 0,
        "answers": 13,
    }
    for field, expected_value in expected_receipt.items():
        if receipt.get(field) != expected_value:
            raise AssertionError(
                f"FIFO controller receipt has {field}="
                f"{receipt.get(field)!r}, expected {expected_value!r}"
            )
    require_measured_controller_work(receipt, "starvation FIFO query")

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
        binary, "search_controller_effect_refusal.metta",
        controller="inline-depth-first",
    )
    require_run(effect_expected, "effect baseline")
    effect_golden = expected(
        "search_controller_effect_refusal.expected")
    if effect_expected.stdout != effect_golden:
        raise AssertionError(
            "effect baseline changed\n"
            f"expected:\n{effect_golden}actual:\n{effect_expected.stdout}"
        )
    effect_fifo = run(
        binary, "search_controller_effect_refusal.metta",
        controller="fifo", stats=True,
    )
    require_run(effect_fifo, "effect refusal")
    effect_fifo_golden = expected(
        "search_controller_effect_refusal.fifo.expected")
    if effect_fifo.stdout != effect_fifo_golden:
        raise AssertionError(
            "effectful root was not refused under FIFO request\n"
            f"expected:\n{effect_fifo_golden}fifo:\n{effect_fifo.stdout}"
        )
    effect_receipts = [
        line for line in effect_fifo.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(effect_receipts) != 1:
        raise AssertionError(
            "effectful root emitted no FIFO refusal receipt"
        )
    effect_receipt = parse_controller_stats_line(effect_receipts[0])
    if effect_receipt.get("admitted") != 0 or (
        effect_receipt.get("active") != "refused"
    ) or effect_receipt.get("storage") != "none" or (
        effect_receipt.get("refusals") != 1
    ):
        raise AssertionError(
            "effectful root was not explicitly refused by FIFO admission"
        )
    for field in (
        "capture_elapsed_ns", "restore_elapsed_ns",
        "expansion_elapsed_ns",
    ):
        if int(effect_receipt.get(field, 0)) != 0:
            raise AssertionError(
                f"effectful refusal unexpectedly measured {field}"
            )

    prime_expected = (PRIME / "search_controller_frontier.expected").read_text(
        encoding="utf-8"
    )
    prime_inline = run(
        binary, "search_controller_frontier.metta",
        controller="inline-depth-first", language="prime",
        fixture_root=PRIME,
    )
    require_run(prime_inline, "Prime inline owned-frontier baseline")
    if prime_inline.stdout != prime_expected:
        raise AssertionError(
            "Prime inline owned-frontier baseline changed\n"
            f"expected:\n{prime_expected}actual:\n{prime_inline.stdout}"
        )
    prime_fifo = run(
        binary, "search_controller_frontier.metta",
        controller="fifo", stats=True, language="prime",
        fixture_root=PRIME, forced_gc=True,
    )
    require_run(prime_fifo, "Prime FIFO owned frontier under forced GC")
    if prime_fifo.stdout != prime_expected:
        raise AssertionError(
            "Prime FIFO changed the exact occurrence stream\n"
            f"expected:\n{prime_expected}actual:\n{prime_fifo.stdout}"
        )
    prime_receipts = [
        line for line in prime_fifo.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(prime_receipts) != 1:
        raise AssertionError("Prime FIFO emitted no unique controller receipt")
    prime_receipt = parse_controller_stats_line(prime_receipts[0])
    for field, expected_value in {
        "requested": "fifo",
        "admitted": 1,
        "active": "fifo",
        "storage": "full-image",
        "refusals": 0,
        "answers": 2,
    }.items():
        if prime_receipt.get(field) != expected_value:
            raise AssertionError(
                f"Prime FIFO receipt has {field}="
                f"{prime_receipt.get(field)!r}, expected {expected_value!r}"
            )
    if int(prime_receipt.get("expansions", 0)) == 0 or (
        int(prime_receipt.get("successors", 0)) != 2
    ):
        raise AssertionError("Prime relation was not frontier-expanded")
    require_measured_controller_work(prime_receipt, "Prime FIFO query")

    print("PASS: controller streams differ and occurrence bags agree exactly")
    print("PASS: FIFO reaches the bounded DFS-starvation witness")
    print("PASS: effectful roots are explicitly refused without substitution")
    print("PASS: Prime relation uses the shared FIFO frontier under forced GC")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
