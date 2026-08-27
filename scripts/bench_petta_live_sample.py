#!/usr/bin/env python3
"""Differentially sample unrelated examples from the current PeTTa tree."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import re
import subprocess
import time

import petta_corpus_manifest as corpus
from petta_machine_stats import (
    aggregate_controller_stats,
    extract_machine_and_controller_stats,
)


SCHEMA = "cetta-petta-live-sample-v4"
OPTIONAL_JANUS_INIT_FAILURE_RE = re.compile(
    r"ERROR: .*?/swipy/janus\.pl:[0-9]+:\n"
    r"ERROR:    .*?/swipy/janus\.pl:[0-9]+: Initialization goal "
    r"raised exception:\n"
    r"ERROR:    open_shared_object/3: libpython[^ \n]*: cannot open "
    r"shared object file: No such file or directory\n"
)

# Keep the population hermetic and bounded.  Selection is random within each
# semantic stratum; the recorded seed makes every run exactly reproducible.
STRATA: dict[str, tuple[str, ...]] = {
    "scalar-control": (
        "case.metta",
        "if.metta",
        "letstar.metta",
        "math.metta",
        "types.metta",
    ),
    "recursion": (
        "factorial.metta",
        "fibsmart.metta",
        "patrick_iterate_fib.metta",
        "peanofast.metta",
        "tabling_fib.metta",
    ),
    "nondeterminism": (
        "collapse.metta",
        "ifcasenondet.metta",
        "permutations.metta",
        "supercollapse.metta",
        "superpose_nested.metta",
    ),
    "space-state": (
        "spaces.metta",
        "spaces3.metta",
        "spaces_find.metta",
        "state.metta",
        "mutex_and_transaction.metta",
    ),
    "higher-order": (
        "foldall.metta",
        "functionhead2.metta",
        "holfunctions.metta",
        "iter.metta",
        "lambda.metta",
    ),
    "reasoning": (
        "booleansolver.metta",
        "logicprog.metta",
        "nars_direct.metta",
        "nilbc.metta",
        "pln_roman.metta",
    ),
}


def select_examples(
    seed: int,
    per_stratum: int,
    available: set[str],
) -> list[tuple[str, str]]:
    if per_stratum <= 0:
        raise ValueError("per_stratum must be positive")
    generator = random.Random(seed)
    selected: list[tuple[str, str]] = []
    for stratum, population in STRATA.items():
        present = sorted(set(population).intersection(available))
        if len(present) < per_stratum:
            raise ValueError(
                f"{stratum}: requested {per_stratum} examples but only "
                f"{len(present)} are present"
            )
        selected.extend(
            (stratum, name)
            for name in generator.sample(present, per_stratum)
        )
    return selected


def select_tracked_examples(paths: list[str]) -> list[tuple[str, str]]:
    selected = []
    for text in sorted(paths):
        path = Path(text)
        if path.parent == Path("examples") and path.suffix == ".metta":
            selected.append(("tracked-corpus", path.name))
    if not selected:
        raise ValueError("no tracked top-level PeTTa examples found")
    return selected


def git_text(root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return result.stdout.strip()


def classify(
    petta: tuple[int | str, str, str],
    cetta: tuple[int | str, str, str],
    stdout_contract: str,
) -> tuple[str, bool, bool, bool]:
    petta_exit, petta_stdout, petta_stderr = petta
    cetta_exit, cetta_stdout, cetta_stderr = cetta
    stdout_observation_equal = corpus.stdout_observation_equal(
        petta_stdout, cetta_stdout, stdout_contract
    )
    stdout_stream_equal = (
        corpus.stdout_observation(
            petta_stdout, corpus.STDOUT_EXACT_STREAM
        )
        == corpus.stdout_observation(
            cetta_stdout, corpus.STDOUT_EXACT_STREAM
        )
    )
    stderr_equal = semantic_stderr(petta_stderr) == semantic_stderr(cetta_stderr)
    if petta_exit != cetta_exit:
        status = "EXIT_MISMATCH"
    elif not stdout_observation_equal:
        status = "STDOUT_MISMATCH"
    elif not stderr_equal:
        status = "STDERR_MISMATCH"
    elif stdout_stream_equal:
        status = "MATCH"
    else:
        status = "MATCH_REORDERED"
    return (
        status,
        stdout_observation_equal,
        stdout_stream_equal,
        stderr_equal,
    )


def semantic_stderr(text: str) -> str:
    """Remove only the optional SWI Janus initialization failure block."""
    return OPTIONAL_JANUS_INIT_FAILURE_RE.sub("", text)


def run_timed(function: object, *arguments: object) -> tuple[object, float]:
    started = time.monotonic()
    result = function(*arguments)  # type: ignore[operator]
    return result, time.monotonic() - started


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--petta-root", required=True, type=Path)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--per-stratum", type=int, default=1)
    parser.add_argument("--all-tracked", action="store_true")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--require-match", action="store_true")
    parser.add_argument(
        "--search-controller",
        choices=("inline-depth-first", "fifo"),
        default="inline-depth-first",
    )
    parser.add_argument(
        "--controller-stats",
        action="store_true",
        help="record controller admission and work without changing semantics",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    petta_root = arguments.petta_root.resolve()
    cetta = arguments.cetta.resolve()
    output = arguments.output.resolve()
    if not (petta_root / "run.sh").is_file():
        raise RuntimeError("PeTTa root does not contain run.sh")
    if not cetta.is_file():
        raise RuntimeError("CeTTa binary does not exist")
    if arguments.timeout <= 0:
        raise ValueError("timeout must be positive")
    if arguments.controller_stats and arguments.search_controller != "fifo":
        raise ValueError("controller statistics require the FIFO controller")
    if (
        arguments.require_match
        and arguments.search_controller == "fifo"
        and not arguments.controller_stats
    ):
        raise ValueError(
            "qualified FIFO comparison requires controller statistics"
        )

    available = {
        path.name for path in (petta_root / "examples").glob("*.metta")
    }
    seed = arguments.seed
    if arguments.all_tracked:
        tracked = git_text(
            petta_root, "ls-files", "--", "examples/*.metta"
        ).splitlines()
        selected = select_tracked_examples(tracked)
        selection_mode = "all-tracked"
    else:
        if seed is None:
            seed = time.time_ns()
        selected = select_examples(seed, arguments.per_stratum, available)
        selection_mode = "stratified"
    output.mkdir(parents=True, exist_ok=True)
    actual = output / "actual"
    actual.mkdir(exist_ok=True)

    rows = [
        "stratum\texample\tstatus\tpetta_exit\tcetta_exit\t"
        "stdout_contract\tstdout_observation_equal\t"
        "stdout_stream_equal\tstderr_equal\tpetta_seconds\t"
        "cetta_seconds\t"
        "source_sha256\torder\tcontroller_records\tcontroller_admitted\t"
        "controller_active_fifo\tcontroller_active_inline_depth_first\t"
        "controller_transitions\tcontroller_expansions\t"
        "controller_successors\tcontroller_captures\tcontroller_restores\t"
        "controller_inline_fallbacks\tcontroller_answers\t"
        "controller_max_frontier\tcontroller_max_frontier_atom_bytes\t"
        "controller_max_frontier_vector_bytes"
    ]
    counts: dict[str, int] = {}
    controller_totals: dict[str, int] = {}
    for index, (stratum, name) in enumerate(selected):
        source = petta_root / "examples" / name
        functions = (
            ("petta", corpus.run_oracle),
            ("cetta", corpus.run_cetta),
        )
        if index % 2:
            functions = tuple(reversed(functions))
        results: dict[str, tuple[int | str, str, str]] = {}
        elapsed: dict[str, float] = {}
        fixture = corpus.FIXTURE_CASES.get(name)
        for engine, function in functions:
            call_arguments = (
                (Path(__file__).resolve().parents[1], petta_root, source,
                 arguments.timeout, fixture)
                if engine == "petta"
                else (
                    cetta, petta_root, source, arguments.timeout, fixture,
                    arguments.search_controller,
                    arguments.controller_stats,
                )
            )
            result, seconds = run_timed(function, *call_arguments)
            results[engine] = result  # type: ignore[assignment]
            elapsed[engine] = seconds

        controller_aggregate = aggregate_controller_stats([])
        raw_cetta_stderr = results["cetta"][2]
        if arguments.controller_stats:
            _, controller_invocations, ordinary_stderr = (
                extract_machine_and_controller_stats(raw_cetta_stderr)
            )
            controller_aggregate = aggregate_controller_stats(
                controller_invocations
            )
            results["cetta"] = (
                results["cetta"][0],
                results["cetta"][1],
                ordinary_stderr,
            )
            for key, value in controller_aggregate.items():
                if key.startswith("max_"):
                    controller_totals[key] = max(
                        controller_totals.get(key, 0), value
                    )
                else:
                    controller_totals[key] = (
                        controller_totals.get(key, 0) + value
                    )

        stdout_contract = (
            corpus.STDOUT_OCCURRENCE_BAG
            if controller_aggregate["active_fifo"] > 0
            else corpus.STDOUT_EXACT_STREAM
        )
        (
            status,
            stdout_observation_equal,
            stdout_stream_equal,
            stderr_equal,
        ) = classify(
            results["petta"], results["cetta"], stdout_contract
        )
        counts[status] = counts.get(status, 0) + 1
        for engine in ("petta", "cetta"):
            _, stdout, stderr = results[engine]
            if engine == "cetta" and arguments.controller_stats:
                stderr = raw_cetta_stderr
            (actual / f"{name}.{engine}.stdout").write_text(
                stdout, encoding="utf-8"
            )
            (actual / f"{name}.{engine}.stderr").write_text(
                stderr, encoding="utf-8"
            )
        rows.append(
            f"{stratum}\t{name}\t{status}\t{results['petta'][0]}\t"
            f"{results['cetta'][0]}\t{stdout_contract}\t"
            f"{int(stdout_observation_equal)}\t"
            f"{int(stdout_stream_equal)}\t{int(stderr_equal)}\t"
            f"{elapsed['petta']:.6f}\t"
            f"{elapsed['cetta']:.6f}\t{corpus.sha256_file(source)}\t"
            f"{'-'.join(engine for engine, _ in functions)}\t"
            f"{controller_aggregate['records']}\t"
            f"{controller_aggregate.get('admitted', 0)}\t"
            f"{controller_aggregate['active_fifo']}\t"
            f"{controller_aggregate['active_inline_depth_first']}\t"
            f"{controller_aggregate.get('transitions', 0)}\t"
            f"{controller_aggregate.get('expansions', 0)}\t"
            f"{controller_aggregate.get('successors', 0)}\t"
            f"{controller_aggregate.get('captures', 0)}\t"
            f"{controller_aggregate.get('restores', 0)}\t"
            f"{controller_aggregate.get('inline_fallbacks', 0)}\t"
            f"{controller_aggregate.get('answers', 0)}\t"
            f"{controller_aggregate.get('max_frontier', 0)}\t"
            f"{controller_aggregate.get('max_frontier_atom_bytes', 0)}\t"
            f"{controller_aggregate.get('max_frontier_vector_bytes', 0)}"
        )
        print(
            f"[{index + 1:02d}/{len(selected):02d}] {stratum}: "
            f"{name} {status}",
            flush=True,
        )

    (output / "results.tsv").write_text(
        "\n".join(rows) + "\n", encoding="utf-8"
    )
    summary = {
        "schema": SCHEMA,
        "selection_mode": selection_mode,
        "seed": seed,
        "per_stratum": (
            None if arguments.all_tracked else arguments.per_stratum
        ),
        "search_controller": arguments.search_controller,
        "controller_stats": arguments.controller_stats,
        "stdout_contract_policy": (
            "occurrence-bag only with an active FIFO receipt; "
            "exact-stream otherwise"
        ),
        "controller_totals": controller_totals,
        "selected": [
            {"stratum": stratum, "example": name}
            for stratum, name in selected
        ],
        "counts": dict(sorted(counts.items())),
        "petta_revision": git_text(petta_root, "rev-parse", "HEAD"),
        "petta_tracked_dirty": bool(
            git_text(petta_root, "status", "--porcelain", "--untracked-files=no")
        ),
        "petta_run_sh_sha256": corpus.sha256_file(petta_root / "run.sh"),
        "cetta_sha256": corpus.sha256_file(cetta),
    }
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, sort_keys=True))
    qualified = counts.get("MATCH", 0) + counts.get(
        "MATCH_REORDERED", 0
    )
    if arguments.require_match and (
        qualified != len(selected)
        or set(counts) - {"MATCH", "MATCH_REORDERED"}
    ):
        raise RuntimeError(
            "live PeTTa sample violates its observation contract"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
