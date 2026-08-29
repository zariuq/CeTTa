#!/usr/bin/env python3
"""Qualify small search-control discriminators over one benchmark catalog."""

from __future__ import annotations

import argparse
from collections import Counter
import csv
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "benchmarks" / "controller_diversity" / "manifest.tsv"

sys.path.insert(0, str(ROOT / "scripts"))
from petta_machine_stats import (  # noqa: E402
    aggregate_controller_stats,
    aggregate_invocations,
    extract_machine_and_controller_stats,
)


REQUIRED_COLUMNS = (
    "id",
    "availability",
    "language",
    "source",
    "observation",
    "niche",
    "contrast",
    "transition_limit",
)
AVAILABILITY = frozenset(("runnable", "asset", "planned"))
OBSERVATIONS = frozenset(
    ("ordered-stream", "bounded-prefix", "complete-bag", "first-answer",
     "first-proof", "first-program", "graded-bag")
)
RUNNABLE_IDS = frozenset(
    ("fair-starvation", "order-sensitivity", "prime-shared-frontier",
     "absorbing-once", "once-recursive-first", "finite-prefix-recursive",
     "reverse-prefix-completion", "flat-complete-bag",
     "compression-induction")
)
CONTINUATION_COMPONENTS = (
    "authority", "terms", "bindings", "control_state",
    "obligations", "readout",
)


def load_manifest(path: Path = DEFAULT_MANIFEST) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if tuple(reader.fieldnames or ()) != REQUIRED_COLUMNS:
            raise ValueError(
                f"unexpected columns: {reader.fieldnames!r}; "
                f"expected {REQUIRED_COLUMNS!r}"
            )
        rows = list(reader)
    if not rows:
        raise ValueError("controller-diversity manifest is empty")
    return rows


def validate_manifest(
    rows: list[dict[str, str]], root: Path = ROOT
) -> None:
    identifiers: set[str] = set()
    runnable: set[str] = set()
    for row in rows:
        identifier = row["id"]
        if not identifier or identifier in identifiers:
            raise ValueError(f"duplicate or empty id: {identifier!r}")
        identifiers.add(identifier)
        if row["availability"] not in AVAILABILITY:
            raise ValueError(
                f"{identifier}: invalid availability {row['availability']!r}"
            )
        if row["observation"] not in OBSERVATIONS:
            raise ValueError(
                f"{identifier}: invalid observation {row['observation']!r}"
            )
        try:
            limit = int(row["transition_limit"])
        except ValueError as error:
            raise ValueError(f"{identifier}: non-integral limit") from error
        if limit < 0:
            raise ValueError(f"{identifier}: negative transition limit")
        if row["availability"] != "planned":
            source = root / row["source"]
            if not source.is_file():
                raise ValueError(
                    f"{identifier}: missing repository asset {row['source']!r}"
                )
        if row["availability"] == "runnable":
            runnable.add(identifier)
            if limit == 0:
                raise ValueError(f"{identifier}: runnable row has no limit")
    if runnable != RUNNABLE_IDS:
        raise ValueError(
            f"runnable ids {sorted(runnable)!r} differ from qualified set "
            f"{sorted(RUNNABLE_IDS)!r}"
        )


def _expected(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _run(
    binary: Path,
    row: dict[str, str],
    controller: str,
    timeout_seconds: float,
    *,
    act_directory: Path | None = None,
    transition_limit: int | None = None,
) -> dict[str, Any]:
    environment = os.environ.copy()
    if controller == "ordinary":
        environment.pop("CETTA_SEARCH_CONTROLLER", None)
    else:
        environment["CETTA_SEARCH_CONTROLLER"] = controller
    environment["CETTA_PETTA_MACHINE_STATS"] = "1"
    environment["CETTA_PETTA_MACHINE_TRANSITION_LIMIT"] = str(
        transition_limit
        if transition_limit is not None else row["transition_limit"]
    )
    if act_directory is None:
        environment.pop("CETTA_SEARCH_ACT_DIR", None)
    else:
        environment["CETTA_SEARCH_ACT_DIR"] = str(act_directory)
    started = time.monotonic_ns()
    process = subprocess.run(
        [str(binary), "--lang", row["language"], str(ROOT / row["source"])],
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_seconds,
        check=False,
    )
    elapsed = time.monotonic_ns() - started
    if process.returncode != 0:
        raise RuntimeError(
            f"{row['id']} {controller}: exit {process.returncode}\n"
            f"stdout:\n{process.stdout}\nstderr:\n{process.stderr}"
        )
    machine, controller_receipts, ordinary_stderr = (
        extract_machine_and_controller_stats(process.stderr)
    )
    if ordinary_stderr:
        raise RuntimeError(
            f"{row['id']} {controller}: unexpected stderr\n{ordinary_stderr}"
        )
    aggregate = aggregate_invocations(machine)
    aggregate.update(
        {
            f"controller_{key}": value
            for key, value in aggregate_controller_stats(
                controller_receipts
            ).items()
        }
    )
    result = {
        "stdout": process.stdout,
        "elapsed_ns": elapsed,
        "aggregate": aggregate,
        "controller_receipts": controller_receipts,
    }
    return result


def _work_signature(aggregate: dict[str, Any]) -> dict[str, Any]:
    """Remove clocks while retaining every semantic and physical-work count."""
    return {
        key: value
        for key, value in aggregate.items()
        if "elapsed_ns" not in key and not key.startswith("ttfa_ns_")
    }


def _qualify_ordinary_erasure(
    row: dict[str, str],
    ordinary: dict[str, Any],
    inline: dict[str, Any],
) -> None:
    identifier = row["id"]
    if ordinary["stdout"] != inline["stdout"]:
        raise RuntimeError(
            f"{identifier}: explicit inline selection changed the stream"
        )
    ordinary_work = _work_signature(ordinary["aggregate"])
    inline_work = _work_signature(inline["aggregate"])
    if ordinary_work != inline_work:
        differing = sorted(
            key for key in ordinary_work.keys() | inline_work.keys()
            if ordinary_work.get(key) != inline_work.get(key)
        )
        raise RuntimeError(
            f"{identifier}: unused controller changed physical work: "
            + ", ".join(differing)
        )


def _qualify(
    row: dict[str, str], ordinary: dict[str, Any],
    inline: dict[str, Any], fifo: dict[str, Any]
) -> None:
    _qualify_ordinary_erasure(row, ordinary, inline)
    identifier = row["id"]
    if identifier == "fair-starvation":
        if inline["stdout"]:
            raise RuntimeError("fair-starvation: DFS unexpectedly emitted")
        expected = _expected(
            "tests/petta/search_controller_fifo_starvation.expected"
        )
        if fifo["stdout"] != expected:
            raise RuntimeError("fair-starvation: FIFO prefix changed")
        if fifo["aggregate"]["controller_active_fifo"] < 1:
            raise RuntimeError("fair-starvation: FIFO was not admitted")
    elif identifier == "order-sensitivity":
        inline_expected = _expected(
            "tests/petta/search_controller_fifo_order.expected"
        )
        fifo_expected = _expected(
            "tests/petta/search_controller_fifo_order.fifo.expected"
        )
        if inline["stdout"] != inline_expected or fifo["stdout"] != fifo_expected:
            raise RuntimeError("order-sensitivity: pinned stream changed")
        if inline["stdout"] == fifo["stdout"]:
            raise RuntimeError("order-sensitivity: streams no longer differ")
        if Counter(inline["stdout"].splitlines()) != Counter(
            fifo["stdout"].splitlines()
        ):
            raise RuntimeError("order-sensitivity: occurrence bags differ")
    elif identifier == "prime-shared-frontier":
        expected = _expected("tests/prime/search_controller_frontier.expected")
        if inline["stdout"] != expected or fifo["stdout"] != expected:
            raise RuntimeError("prime-shared-frontier: exact stream changed")
        if fifo["aggregate"]["controller_active_fifo"] < 1:
            raise RuntimeError("prime-shared-frontier: FIFO was not admitted")
    elif identifier == "absorbing-once":
        expected = _expected(
            "benchmarks/controller_diversity/deep_once.expected"
        )
        if inline["stdout"] != expected or fifo["stdout"] != expected:
            raise RuntimeError("absorbing-once: exact first answer changed")
        if fifo["aggregate"]["controller_expansions"] < 1:
            raise RuntimeError(
                "absorbing-once: first-witness frontier was not exercised"
            )
        if fifo["aggregate"]["controller_answers"] != 1:
            raise RuntimeError(
                "absorbing-once: first-witness scope leaked a sibling"
            )
    elif identifier == "once-recursive-first":
        expected = _expected(
            "benchmarks/controller_diversity/once_recursive_first.expected"
        )
        if inline["stdout"]:
            raise RuntimeError(
                "once-recursive-first: bounded inline descent unexpectedly "
                "found a witness"
            )
        if fifo["stdout"] != expected:
            raise RuntimeError(
                "once-recursive-first: controlled first witness changed"
            )
        if fifo["aggregate"]["controller_active_fifo"] < 1 or fifo[
            "aggregate"
        ]["controller_expansions"] < 1:
            raise RuntimeError(
                "once-recursive-first: controlled frontier was not active"
            )
        if fifo["aggregate"]["controller_answers"] != 1:
            raise RuntimeError(
                "once-recursive-first: first-witness contraction failed"
            )
    elif identifier == "finite-prefix-recursive":
        expected = _expected(
            "benchmarks/controller_diversity/"
            "finite_prefix_recursive.expected"
        )
        inline_expected = _expected(
            "benchmarks/controller_diversity/"
            "finite_prefix_recursive.inline.expected"
        )
        if inline["stdout"] != inline_expected:
            raise RuntimeError(
                "finite-prefix-recursive: bounded inline result changed"
            )
        if fifo["stdout"] != expected:
            raise RuntimeError(
                "finite-prefix-recursive: exact controlled prefix changed"
            )
        if fifo["aggregate"]["controller_active_fifo"] < 1 or fifo[
            "aggregate"
        ]["controller_expansions"] < 1:
            raise RuntimeError(
                "finite-prefix-recursive: controlled frontier was not active"
            )
        if fifo["aggregate"]["controller_answers"] != 2:
            raise RuntimeError(
                "finite-prefix-recursive: source did not stop at the "
                "declared prefix"
            )
    elif identifier == "reverse-prefix-completion":
        expected = _expected(
            "benchmarks/controller_diversity/"
            "reverse_prefix_requires_completion.expected"
        )
        if inline["stdout"] != expected or fifo["stdout"] != expected:
            raise RuntimeError(
                "reverse-prefix-completion: selected result changed"
            )
        if inline["aggregate"]["answers"] != 3 or fifo[
            "aggregate"
        ]["controller_answers"] != 3:
            raise RuntimeError(
                "reverse-prefix-completion: source was contracted before "
                "the order transformation completed"
            )
    elif identifier == "flat-complete-bag":
        expected = _expected(
            "benchmarks/controller_diversity/flat_complete_bag.expected"
        )
        expected_bag = Counter(expected.splitlines())
        if Counter(inline["stdout"].splitlines()) != expected_bag:
            raise RuntimeError("flat-complete-bag: inline bag changed")
        if Counter(fifo["stdout"].splitlines()) != expected_bag:
            raise RuntimeError("flat-complete-bag: FIFO bag changed")
        if fifo["aggregate"]["controller_active_fifo"] < 1:
            raise RuntimeError("flat-complete-bag: FIFO was not admitted")
        if fifo["aggregate"]["controller_expansions"] < 1:
            raise RuntimeError("flat-complete-bag: no frontier work measured")
    else:
        raise RuntimeError(f"no qualifier for runnable row {identifier!r}")


def _qualify_compression(
    baseline: dict[str, Any],
    training: dict[str, Any],
    advised: dict[str, Any],
) -> None:
    golden = _expected(
        "benchmarks/controller_diversity/compression_guidance.expected"
    )
    if baseline["stdout"]:
        raise RuntimeError(
            "compression-induction: fresh bounded ratio unexpectedly found "
            "the oldest successful branch"
        )
    if training["stdout"] != golden:
        raise RuntimeError(
            "compression-induction: generous training did not find the "
            "successful branch exactly once"
        )
    if advised["stdout"] != golden:
        raise RuntimeError(
            "compression-induction: learned bounded run missed the "
            "successful branch"
        )
    receipts = advised["controller_receipts"]
    if len(receipts) != 1:
        raise RuntimeError(
            "compression-induction: advised run has no unique receipt"
        )
    receipt = receipts[0]
    if receipt.get("advisor") != "incremental-compression" or int(
        receipt.get("compression_ranking_applied", 0)
    ) == 0:
        raise RuntimeError(
            "compression-induction: auto did not apply receipted guidance"
        )
    if int(receipt.get("compression_model_store_failures", 0)) != 0:
        raise RuntimeError(
            "compression-induction: learned model failed persistence"
        )
    if int(receipt.get("act_profile_store_failures", 0)) != 0:
        raise RuntimeError(
            "compression-induction: controller profile failed persistence"
        )


def _result(
    identifier: str,
    controller: str,
    samples: list[dict[str, Any]],
) -> dict[str, Any]:
    aggregates = [sample["aggregate"] for sample in samples]
    def median(key: str) -> int:
        return int(statistics.median(
            aggregate.get(key, 0) for aggregate in aggregates
        ))

    result = {
        "id": identifier,
        "controller": controller,
        "runs": len(samples),
        "elapsed_ns": int(statistics.median(
            sample["elapsed_ns"] for sample in samples
        )),
        "transitions": median("transitions"),
        "answers": median("answers"),
        "expansions": median("controller_expansions"),
        "expansion_attempts": median(
            "owned_continuation_expansion_attempts"
        ),
        "expansion_unsupported": median(
            "owned_continuation_expansion_unsupported"
        ),
        "successors": median("controller_successors"),
        "max_frontier": median("controller_max_frontier"),
        "atom_bytes_captured": median(
            "owned_continuation_atom_bytes_captured"
        ),
        "vector_bytes_captured": median(
            "owned_continuation_vector_bytes_captured"
        ),
        "heap_collections": median("heap_collections"),
        "heap_collection_elapsed_ns": median(
            "heap_collection_elapsed_ns"
        ),
        "heap_bytes_reclaimed": median("heap_bytes_reclaimed"),
        "binding_entries_discarded": median(
            "binding_entries_discarded"
        ),
        "max_heap_live_bytes": median("max_heap_live_bytes"),
        "max_binding_entries": median("max_binding_entries"),
        "capture_elapsed_ns": median(
            "controller_capture_elapsed_ns"
        ),
        "restore_elapsed_ns": median(
            "controller_restore_elapsed_ns"
        ),
        "expansion_elapsed_ns": median(
            "controller_expansion_elapsed_ns"
        ),
        "reclamation_attempts": median(
            "controller_reclamation_attempts"
        ),
        "reclamation_applied": median(
            "controller_reclamation_applied"
        ),
        "reclamation_deferred": median(
            "controller_reclamation_deferred"
        ),
        "reclamation_failures": median(
            "controller_reclamation_failures"
        ),
        "reclamation_elapsed_ns": median(
            "controller_reclamation_elapsed_ns"
        ),
        "reclamation_shared_bytes_reclaimed": median(
            "controller_reclamation_shared_bytes_reclaimed"
        ),
        "reclamation_shared_bytes_before": median(
            "controller_reclamation_shared_bytes_before"
        ),
        "reclamation_shared_bytes_after": median(
            "controller_reclamation_shared_bytes_after"
        ),
        "frontier_shared_bytes": median(
            "controller_max_frontier_shared_bytes"
        ),
        "frontier_exclusive_bytes": median(
            "controller_max_frontier_exclusive_bytes"
        ),
        "frontier_control_bytes": median(
            "controller_max_frontier_control_bytes"
        ),
        "compression_advisor": median(
            "controller_advisor_incremental_compression"
        ),
        "compression_ranking_applied": median(
            "controller_compression_ranking_applied"
        ),
        "compression_model_observations": median(
            "controller_compression_model_observations"
        ),
        "compression_model_updates": median(
            "controller_compression_model_updates"
        ),
        "act_profile_store_failures": median(
            "controller_act_profile_store_failures"
        ),
        "compression_model_store_failures": median(
            "controller_compression_model_store_failures"
        ),
        "compression_ranking_elapsed_ns": median(
            "controller_compression_ranking_elapsed_ns"
        ),
        "compression_model_bytes": median(
            "controller_compression_model_bytes"
        ),
        "compression_lineage_bytes": median(
            "controller_max_compression_lineage_bytes"
        ),
        "compression_working_bytes": median(
            "controller_max_compression_working_bytes"
        ),
    }
    for component in CONTINUATION_COMPONENTS:
        result[f"{component}_shared_bytes"] = median(
            f"controller_max_frontier_{component}_shared_bytes"
        )
        result[f"{component}_exclusive_bytes"] = median(
            f"controller_max_frontier_{component}_exclusive_bytes"
        )
    return result


def run_current(
    binary: Path,
    rows: list[dict[str, str]],
    runs: int,
    timeout_seconds: float,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for row in rows:
        if row["availability"] != "runnable":
            continue
        if row["id"] == "compression-induction":
            baseline_samples: list[dict[str, Any]] = []
            advised_samples: list[dict[str, Any]] = []
            runtime_directory = ROOT / "runtime"
            runtime_directory.mkdir(exist_ok=True)
            for _ in range(runs):
                with tempfile.TemporaryDirectory(
                    prefix="compression-fresh-", dir=runtime_directory
                ) as fresh_name, tempfile.TemporaryDirectory(
                    prefix="compression-learned-", dir=runtime_directory
                ) as learned_name:
                    fresh_directory = Path(fresh_name)
                    learned_directory = Path(learned_name)
                    baseline = _run(
                        binary, row, "ratio:16", timeout_seconds,
                        act_directory=fresh_directory,
                    )
                    training = _run(
                        binary, row, "ratio:16", timeout_seconds,
                        act_directory=learned_directory,
                        transition_limit=256,
                    )
                    advised = _run(
                        binary, row, "auto", timeout_seconds,
                        act_directory=learned_directory,
                    )
                    _qualify_compression(baseline, training, advised)
                    baseline_samples.append(baseline)
                    advised_samples.append(advised)
            results.append(_result(
                row["id"], "ratio:16-fresh", baseline_samples
            ))
            results.append(_result(
                row["id"], "auto:compression", advised_samples
            ))
            continue
        samples: dict[str, list[dict[str, Any]]] = {
            "ordinary": [],
            "inline-depth-first": [],
            "fifo": [],
        }
        for run_index in range(runs):
            controllers = ("ordinary", "inline-depth-first", "fifo")
            offset = run_index % len(controllers)
            order = controllers[offset:] + controllers[:offset]
            for controller in order:
                samples[controller].append(
                    _run(binary, row, controller, timeout_seconds)
                )
        _qualify(
            row,
            samples["ordinary"][0],
            samples["inline-depth-first"][0],
            samples["fifo"][0],
        )
        for controller, controller_samples in samples.items():
            results.append(_result(
                row["id"], controller, controller_samples
            ))
    return results


def print_results(results: list[dict[str, Any]]) -> None:
    component_columns = tuple(
        column
        for component in CONTINUATION_COMPONENTS
        for column in (
            f"{component}_shared_bytes",
            f"{component}_exclusive_bytes",
        )
    )
    columns = (
        "id", "controller", "runs", "elapsed_ns", "transitions", "answers",
        "expansions", "expansion_attempts", "expansion_unsupported",
        "successors", "max_frontier", "atom_bytes_captured",
        "vector_bytes_captured", "heap_collections",
        "heap_collection_elapsed_ns", "heap_bytes_reclaimed",
        "binding_entries_discarded", "max_heap_live_bytes",
        "max_binding_entries", "capture_elapsed_ns",
        "restore_elapsed_ns", "expansion_elapsed_ns",
        "reclamation_attempts", "reclamation_applied",
        "reclamation_deferred",
        "reclamation_failures", "reclamation_elapsed_ns",
        "reclamation_shared_bytes_reclaimed",
        "reclamation_shared_bytes_before",
        "reclamation_shared_bytes_after",
        "frontier_shared_bytes", "frontier_exclusive_bytes",
        "frontier_control_bytes",
    ) + component_columns + (
        "compression_advisor",
        "compression_ranking_applied", "compression_model_observations",
        "compression_model_updates", "act_profile_store_failures",
        "compression_model_store_failures",
        "compression_ranking_elapsed_ns", "compression_model_bytes",
        "compression_lineage_bytes", "compression_working_bytes",
    )
    print("\t".join(columns))
    for result in results:
        print("\t".join(str(result[column]) for column in columns))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--cetta", type=Path)
    parser.add_argument("--run-current", action="store_true")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    rows = load_manifest(args.manifest)
    validate_manifest(rows)
    if not args.run_current:
        print("\t".join(REQUIRED_COLUMNS))
        for row in rows:
            print("\t".join(row[column] for column in REQUIRED_COLUMNS))
        return 0
    if args.cetta is None:
        parser.error("--run-current requires --cetta")
    results = run_current(
        args.cetta.resolve(), rows, args.runs, args.timeout
    )
    print_results(results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
