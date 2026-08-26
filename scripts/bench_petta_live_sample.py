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


SCHEMA = "cetta-petta-live-sample-v1"
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
) -> tuple[str, bool, bool]:
    petta_exit, petta_stdout, petta_stderr = petta
    cetta_exit, cetta_stdout, cetta_stderr = cetta
    stdout_equal = (
        corpus.semantic_stdout(petta_stdout)
        == corpus.semantic_stdout(cetta_stdout)
    )
    stderr_equal = semantic_stderr(petta_stderr) == semantic_stderr(cetta_stderr)
    if petta_exit != cetta_exit:
        status = "EXIT_MISMATCH"
    elif not stdout_equal:
        status = "STDOUT_MISMATCH"
    elif not stderr_equal:
        status = "STDERR_MISMATCH"
    else:
        status = "MATCH"
    return status, stdout_equal, stderr_equal


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
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--require-match", action="store_true")
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

    seed = arguments.seed
    if seed is None:
        seed = time.time_ns()
    available = {
        path.name for path in (petta_root / "examples").glob("*.metta")
    }
    selected = select_examples(seed, arguments.per_stratum, available)
    output.mkdir(parents=True, exist_ok=True)
    actual = output / "actual"
    actual.mkdir(exist_ok=True)

    rows = [
        "stratum\texample\tstatus\tpetta_exit\tcetta_exit\t"
        "stdout_equal\tstderr_equal\tpetta_seconds\tcetta_seconds\t"
        "source_sha256\torder"
    ]
    counts: dict[str, int] = {}
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
        for engine, function in functions:
            call_arguments = (
                (Path(__file__).resolve().parents[1], petta_root, source,
                 arguments.timeout, None)
                if engine == "petta"
                else (cetta, petta_root, source, arguments.timeout, None)
            )
            result, seconds = run_timed(function, *call_arguments)
            results[engine] = result  # type: ignore[assignment]
            elapsed[engine] = seconds

        status, stdout_equal, stderr_equal = classify(
            results["petta"], results["cetta"]
        )
        counts[status] = counts.get(status, 0) + 1
        for engine in ("petta", "cetta"):
            _, stdout, stderr = results[engine]
            (actual / f"{name}.{engine}.stdout").write_text(
                stdout, encoding="utf-8"
            )
            (actual / f"{name}.{engine}.stderr").write_text(
                stderr, encoding="utf-8"
            )
        rows.append(
            f"{stratum}\t{name}\t{status}\t{results['petta'][0]}\t"
            f"{results['cetta'][0]}\t{int(stdout_equal)}\t"
            f"{int(stderr_equal)}\t{elapsed['petta']:.6f}\t"
            f"{elapsed['cetta']:.6f}\t{corpus.sha256_file(source)}\t"
            f"{'-'.join(engine for engine, _ in functions)}"
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
        "seed": seed,
        "per_stratum": arguments.per_stratum,
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
    if arguments.require_match and counts != {"MATCH": len(selected)}:
        raise RuntimeError("live PeTTa sample is not exact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
