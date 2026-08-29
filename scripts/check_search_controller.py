#!/usr/bin/env python3
"""Qualify controller order, fairness, multiplicity, and refusal."""

from __future__ import annotations

from collections import Counter
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from petta_machine_stats import (
    parse_controller_stats_line,
    parse_stats_line,
)


ROOT = Path(__file__).resolve().parents[1]
PETTA = ROOT / "tests" / "petta"
PRIME = ROOT / "tests" / "prime"
HE = ROOT / "tests" / "he"
DIVERSITY = ROOT / "benchmarks" / "controller_diversity"


def run(binary: Path, fixture: str, *, controller: str | None,
        limit: int | None = None,
        stats: bool = False, language: str = "petta",
        fixture_root: Path = PETTA,
        forced_gc: bool = False,
        act_directory: Path | None = None,
        ) -> subprocess.CompletedProcess[str]:
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
    if act_directory is None:
        env.pop("CETTA_SEARCH_ACT_DIR", None)
    else:
        env["CETTA_SEARCH_ACT_DIR"] = str(act_directory)
    return subprocess.run(
        [str(binary), "--lang", language, str(fixture_root / fixture)],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
        timeout=30,
        check=False,
    )


def expected(name: str, fixture_root: Path = PETTA) -> str:
    return (fixture_root / name).read_text(encoding="utf-8")


def metta_list_occurrences(output: str) -> list[str]:
    """Split one printed MeTTa result list without splitting nested terms."""
    text = output.strip()
    if len(text) < 2 or text[0] != "[" or text[-1] != "]":
        raise ValueError(f"not one printed MeTTa result list: {output!r}")
    body = text[1:-1]
    if not body:
        return []
    items: list[str] = []
    start = 0
    depth = 0
    quote: str | None = None
    escaped = False
    for index, character in enumerate(body):
        if quote is not None:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
            continue
        if character in {'"', "'"}:
            quote = character
        elif character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
            if depth < 0:
                raise ValueError(f"unbalanced result list: {output!r}")
        elif character == "," and depth == 0:
            item = body[start:index].strip()
            if not item:
                raise ValueError(f"empty result occurrence: {output!r}")
            items.append(item)
            start = index + 1
    if quote is not None or depth != 0:
        raise ValueError(f"unbalanced result list: {output!r}")
    item = body[start:].strip()
    if not item:
        raise ValueError(f"empty result occurrence: {output!r}")
    items.append(item)
    return items


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
        "storage": "shared-terms-owned-state",
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

    he_expected = expected(
        "search_controller_structural_equations.expected", HE)
    he_reference = run(
        binary, "search_controller_structural_equations.metta",
        controller=None, stats=True, language="he", fixture_root=HE,
    )
    require_run(he_reference, "HE structural-equation reference")
    if he_reference.stdout != he_expected:
        raise AssertionError(
            "HE structural-equation reference changed\n"
            f"expected:\n{he_expected}actual:\n{he_reference.stdout}"
        )
    if he_reference.stderr:
        raise AssertionError(
            "ordinary HE allocated or activated search-control machinery\n"
            f"stderr:\n{he_reference.stderr}"
        )
    he_inline = run(
        binary, "search_controller_structural_equations.metta",
        controller="inline-depth-first", stats=True,
        language="he", fixture_root=HE,
    )
    require_run(he_inline, "HE structural-equation inline controller")
    if he_inline.stdout != he_expected:
        raise AssertionError(
            "HE inline structural-equation stream changed\n"
            f"expected:\n{he_expected}actual:\n{he_inline.stdout}"
        )
    he_fifo = run(
        binary, "search_controller_structural_equations.metta",
        controller="fifo", stats=True, language="he", fixture_root=HE,
    )
    require_run(he_fifo, "HE structural-equation FIFO controller")
    he_fifo_expected = expected(
        "search_controller_structural_equations.fifo.expected", HE)
    if he_fifo.stdout != he_fifo_expected:
        raise AssertionError(
            "HE FIFO structural-equation stream changed\n"
            f"expected:\n{he_fifo_expected}actual:\n{he_fifo.stdout}"
        )
    if Counter(metta_list_occurrences(he_reference.stdout)) != Counter(
        metta_list_occurrences(he_fifo.stdout)
    ):
        raise AssertionError(
            "HE reference and FIFO disagree on the occurrence bag"
        )
    he_receipts = [
        line for line in he_fifo.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(he_receipts) != 1:
        raise AssertionError("HE FIFO emitted no unique controller receipt")
    he_receipt = parse_controller_stats_line(he_receipts[0])
    for field, expected_value in {
        "requested": "fifo",
        "admitted": 1,
        "active": "fifo",
        "storage": "shared-terms-owned-state",
        "refusals": 0,
        "answers": 10,
    }.items():
        if he_receipt.get(field) != expected_value:
            raise AssertionError(
                f"HE FIFO receipt has {field}="
                f"{he_receipt.get(field)!r}, expected {expected_value!r}"
            )
    if int(he_receipt.get("expansions", 0)) == 0:
        raise AssertionError("HE structural relation was not frontier-expanded")
    he_machine_lines = [
        line for line in he_fifo.stderr.splitlines()
        if line.startswith("PETTA_MACHINE_STATS ")
    ]
    if len(he_machine_lines) != 1:
        raise AssertionError("HE FIFO emitted no unique machine receipt")
    he_machine_receipt = parse_stats_line(he_machine_lines[0])
    if he_machine_receipt["owned_continuation_atom_bytes_captured"] > int(
        he_receipt.get("max_frontier_terms_shared_bytes", 0)
    ):
        raise AssertionError(
            "HE successor captures duplicated the immutable term pool"
        )
    require_measured_controller_work(he_receipt, "HE structural FIFO query")

    he_host_expected = expected(
        "search_controller_host_owned_refusal.expected", HE)
    he_host_reference = run(
        binary, "search_controller_host_owned_refusal.metta",
        controller=None, language="he", fixture_root=HE,
    )
    require_run(he_host_reference, "HE host-owned reference")
    he_host_fifo = run(
        binary, "search_controller_host_owned_refusal.metta",
        controller="fifo", stats=True, language="he", fixture_root=HE,
    )
    require_run(he_host_fifo, "HE host-owned controller decline")
    if he_host_reference.stdout != he_host_expected or (
        he_host_fifo.stdout != he_host_expected
    ):
        raise AssertionError(
            "HE host-owned relation did not remain canonical under FIFO"
        )
    if any(
        line.startswith("CETTA_CONTROLLER_STATS ")
        for line in he_host_fifo.stderr.splitlines()
    ):
        raise AssertionError(
            "HE host-owned relation incorrectly admitted a controller"
        )

    # The observation-derived default enters the continuation hub only for
    # unresolved bounded demand.  It runs deterministic stretches inside the
    # provider and returns to the hub at exactly externalizable clause choices,
    # rather than scheduling every machine transition.
    auto_once = run(
        binary, "once_recursive_first.metta", controller="auto",
        limit=100, stats=True, fixture_root=DIVERSITY,
    )
    require_run(auto_once, "observation-derived first-witness control")
    auto_once_expected = expected(
        "once_recursive_first.expected", DIVERSITY)
    if auto_once.stdout != auto_once_expected:
        raise AssertionError(
            "auto did not rescue the recursive-first witness\n"
            f"expected:\n{auto_once_expected}actual:\n{auto_once.stdout}"
        )
    auto_once_lines = [
        line for line in auto_once.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(auto_once_lines) != 1:
        raise AssertionError("auto first-witness run has no unique receipt")
    auto_once_receipt = parse_controller_stats_line(auto_once_lines[0])
    for field, expected_value in {
        "requested": "ratio",
        "admitted": 1,
        "active": "ratio",
        "answers": 1,
    }.items():
        if auto_once_receipt.get(field) != expected_value:
            raise AssertionError(
                f"auto first-witness receipt has {field}="
                f"{auto_once_receipt.get(field)!r}, "
                f"expected {expected_value!r}"
            )
    if int(auto_once_receipt.get("expansions", 0)) == 0:
        raise AssertionError("auto never externalized the genuine branch")
    if int(auto_once_receipt.get("scheduling_rounds", 0)) >= int(
        auto_once_receipt.get("transitions", 0)
    ):
        raise AssertionError(
            "auto still scheduled every provider transition"
        )

    auto_prefix = run(
        binary, "finite_prefix_recursive.metta", controller="auto",
        limit=100, stats=True, fixture_root=DIVERSITY,
    )
    require_run(auto_prefix, "observation-derived finite-prefix control")
    auto_prefix_expected = expected(
        "finite_prefix_recursive.expected", DIVERSITY)
    if auto_prefix.stdout != auto_prefix_expected:
        raise AssertionError(
            "auto changed the exact finite prefix\n"
            f"expected:\n{auto_prefix_expected}actual:\n{auto_prefix.stdout}"
        )
    auto_prefix_lines = [
        line for line in auto_prefix.stderr.splitlines()
        if line.startswith("CETTA_CONTROLLER_STATS ")
    ]
    if len(auto_prefix_lines) != 1:
        raise AssertionError("auto finite-prefix run has no unique receipt")
    auto_prefix_receipt = parse_controller_stats_line(auto_prefix_lines[0])
    if auto_prefix_receipt.get("active") != "ratio" or int(
        auto_prefix_receipt.get("answers", 0)
    ) != 2:
        raise AssertionError(
            "auto did not honor finite-prefix demand through fair descent"
        )
    if int(auto_prefix_receipt.get("scheduling_rounds", 0)) >= int(
        auto_prefix_receipt.get("transitions", 0)
    ):
        raise AssertionError(
            "auto finite-prefix control still polled per transition"
        )

    complete_bag = run(
        binary, "auto_complete_bag.metta", controller="auto",
        stats=True, fixture_root=DIVERSITY,
    )
    require_run(complete_bag, "complete-bag auto fast path")
    complete_bag_expected = expected(
        "auto_complete_bag.expected", DIVERSITY)
    if complete_bag.stdout != complete_bag_expected:
        raise AssertionError(
            "auto changed the complete occurrence bag\n"
            f"expected:\n{complete_bag_expected}"
            f"actual:\n{complete_bag.stdout}"
        )
    if any(
        line.startswith("CETTA_CONTROLLER_STATS ")
        for line in complete_bag.stderr.splitlines()
    ):
        raise AssertionError(
            "complete-bag auto unnecessarily activated a frontier"
        )

    # Compression advice is learned under an explicit controller, persisted
    # beside the policy receipt, then consumed only through explicit `auto`.
    # The age-protected ticks remain part of the recorded ratio discipline;
    # compression refines only its priority ticks.
    with tempfile.TemporaryDirectory(
        prefix="cetta-act-compression-"
    ) as directory_name:
        directory = Path(directory_name)
        training = run(
            binary, "search_controller_fifo_starvation.metta",
            controller="ratio:4", limit=100, stats=True,
            act_directory=directory,
        )
        require_run(training, "incremental-compression training run")
        training_lines = [
            line for line in training.stderr.splitlines()
            if line.startswith("CETTA_CONTROLLER_STATS ")
        ]
        if len(training_lines) != 1:
            raise AssertionError(
                "compression training emitted no unique controller receipt"
            )
        training_receipt = parse_controller_stats_line(training_lines[0])
        if int(training_receipt.get("compression_model_updates", 0)) == 0:
            raise AssertionError(
                "answer-producing continuations did not update the model"
            )
        if int(training_receipt.get(
            "compression_model_store_failures", 0
        )) != 0 or int(training_receipt.get(
            "act_profile_store_failures", 0
        )) != 0:
            raise AssertionError("compression model was not persisted")
        if len(list(directory.glob("*.compression.act"))) != 1:
            raise AssertionError(
                "training did not create one query-scoped compression model"
            )

        advised = run(
            binary, "search_controller_fifo_starvation.metta",
            controller="auto", limit=100, stats=True,
            act_directory=directory,
        )
        require_run(advised, "incremental-compression advised run")
        advised_lines = [
            line for line in advised.stderr.splitlines()
            if line.startswith("CETTA_CONTROLLER_STATS ")
        ]
        if len(advised_lines) != 1:
            raise AssertionError(
                "compression advice emitted no unique controller receipt"
            )
        advised_receipt = parse_controller_stats_line(advised_lines[0])
        if advised_receipt.get("advisor") != "incremental-compression":
            raise AssertionError("auto did not receipt compression advice")
        if int(advised_receipt.get(
            "compression_ranking_attempts", 0
        )) == 0 or int(advised_receipt.get(
            "compression_ranking_applied", 0
        )) == 0:
            raise AssertionError(
                "persisted successful structure did not rank a live frontier"
            )
        if not advised.stdout:
            raise AssertionError(
                "compression advice lost every productive answer"
            )

    # An unbounded ordered stream remains the reference path when no exact
    # learned key exists.  Learning never fabricates a policy for that scope.
    with tempfile.TemporaryDirectory(
        prefix="cetta-act-empty-"
    ) as directory_name:
        no_record = run(
            binary, "search_controller_fifo_order.metta",
            controller="auto", stats=True,
            act_directory=Path(directory_name),
        )
        require_run(no_record, "auto without an ACT record")
        if no_record.stdout != reference_expected:
            raise AssertionError("empty auto profile changed reference output")
        if any(
            line.startswith("CETTA_CONTROLLER_STATS ")
            for line in no_record.stderr.splitlines()
        ):
            raise AssertionError("empty auto profile fabricated a controller")

    print("PASS: controller streams differ and occurrence bags agree exactly")
    print("PASS: FIFO reaches the bounded DFS-starvation witness")
    print("PASS: effectful roots are explicitly refused without substitution")
    print("PASS: Prime relation uses the shared FIFO frontier under forced GC")
    print("PASS: HE structural equations use the shared qualified frontier")
    print("PASS: HE host-owned equations remain canonical under controller request")
    print("PASS: observation-derived auto controls only bounded competition")
    print("PASS: complete-bag auto leaves frontier machinery dormant")
    print("PASS: incremental compression learns and ranks only through auto")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
