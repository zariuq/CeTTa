#!/usr/bin/env python3
"""Compare source-level relational work in CeTTa and SWI-PeTTa."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from petta_machine_stats import (  # noqa: E402
    aggregate_invocations,
    extract_observability,
    extract_publication_stats,
)
from semantic_work_counters import (  # noqa: E402
    FIELDS,
    from_cetta_machine,
    parse_swi,
)
from petta_corpus_manifest import alpha_canonicalize_output  # noqa: E402


SCHEMA = "cetta-swi-semantic-work-v1"
OCCURS_SETUP = re.compile(
    r"^(?:\(translatePredicate \(set_prolog_flag occurs_check "
    r"(?:true|True)\)\)|\$_[0-9]+)$"
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def normalize_stdout(text: str) -> str:
    lines = [line for line in text.replace("\r\n", "\n").split("\n") if line]
    if lines and OCCURS_SETUP.fullmatch(lines[0]):
        lines = lines[1:]
    normalized = ("\n".join(lines) + "\n") if lines else ""
    return alpha_canonicalize_output(normalized)


def run(
    command: list[str], cwd: Path, environment: dict[str, str], timeout: float
) -> tuple[str, str]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed with exit {completed.returncode}: {command!r}\n"
            f"stderr:\n{completed.stderr[-4000:]}"
        )
    return completed.stdout, completed.stderr


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--petta-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("sources", nargs="+", type=Path)
    arguments = parser.parse_args()
    if arguments.timeout <= 0:
        parser.error("--timeout must be positive")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    cetta = arguments.cetta.resolve()
    petta_root = arguments.petta_root.resolve()
    output = arguments.output.resolve()
    if not cetta.is_file() or not os.access(cetta, os.X_OK):
        raise RuntimeError("CeTTa binary is not executable")
    if not (petta_root / "run.sh").is_file():
        raise RuntimeError("PeTTa root does not contain run.sh")
    sources = [source.resolve() for source in arguments.sources]
    missing = [str(source) for source in sources if not source.is_file()]
    if missing:
        raise RuntimeError(f"source files do not exist: {missing}")

    output.mkdir(parents=True, exist_ok=True)
    actual = output / "actual"
    actual.mkdir(exist_ok=True)
    environment = os.environ.copy()
    environment["LC_ALL"] = "C.UTF-8"
    environment["CETTA_PETTA_SEARCH_MACHINE"] = "1"
    environment["CETTA_PETTA_MACHINE_STATS"] = "1"

    rows: list[dict[str, object]] = []
    all_equal = True
    for index, source in enumerate(sources, start=1):
        cetta_stdout, cetta_stderr = run(
            [str(cetta), "--lang", "petta", str(source)],
            cetta.parent,
            environment,
            arguments.timeout,
        )
        publications, cetta_without_publications = extract_publication_stats(
            cetta_stderr
        )
        if not publications:
            raise RuntimeError("CeTTa emitted no publication statistics")
        published_answers = sum(item["answers"] for item in publications)
        invocations, _, cetta_ordinary_stderr = extract_observability(
            cetta_without_publications
        )
        cetta_counters = from_cetta_machine(
            aggregate_invocations(invocations), published_answers
        )

        swi_stdout, swi_stderr = run(
            [
                "bash",
                str(petta_root / "run.sh"),
                "--silent",
                "--semantic-counters",
                str(source),
            ],
            petta_root,
            environment,
            arguments.timeout,
        )
        swi_counters, swi_ordinary_stderr = parse_swi(swi_stderr)

        counter_equal = cetta_counters == swi_counters
        normalized_cetta_stdout = normalize_stdout(cetta_stdout)
        normalized_swi_stdout = normalize_stdout(swi_stdout)
        stdout_equal = normalized_cetta_stdout == normalized_swi_stdout
        row_equal = counter_equal and stdout_equal
        all_equal = all_equal and row_equal
        stem = f"{index:02d}-{source.stem}"
        (actual / f"{stem}.cetta.stdout").write_text(
            cetta_stdout, encoding="utf-8"
        )
        (actual / f"{stem}.cetta.stderr").write_text(
            cetta_ordinary_stderr, encoding="utf-8"
        )
        (actual / f"{stem}.swi.stdout").write_text(
            swi_stdout, encoding="utf-8"
        )
        (actual / f"{stem}.swi.stderr").write_text(
            swi_ordinary_stderr, encoding="utf-8"
        )
        row = {
            "source": source.name,
            "source_sha256": sha256_file(source),
            "stdout_equal": stdout_equal,
            "stdout_sha256": sha256_bytes(
                normalized_cetta_stdout.encode("utf-8")
            ),
            "counters_equal": counter_equal,
            "cetta": cetta_counters,
            "swi": swi_counters,
        }
        rows.append(row)
        print(
            f"[{index:02d}/{len(sources):02d}] {source.name}: "
            f"stdout={'exact' if stdout_equal else 'DIFF'} "
            f"counters={'exact' if counter_equal else 'DIFF'}",
            flush=True,
        )

    identity = {
        "schema": SCHEMA,
        "cetta_sha256": sha256_file(cetta),
        "petta_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=petta_root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        ).stdout.strip(),
        "rows": rows,
    }
    (output / "results.json").write_text(
        json.dumps(identity, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    header = ["source", "stdout_equal", "counters_equal"]
    for field in FIELDS:
        header.extend((f"cetta_{field}", f"swi_{field}"))
    lines = ["\t".join(header)]
    for row in rows:
        values = [
            str(row["source"]),
            str(int(bool(row["stdout_equal"]))),
            str(int(bool(row["counters_equal"]))),
        ]
        cetta_values = row["cetta"]
        swi_values = row["swi"]
        assert isinstance(cetta_values, dict)
        assert isinstance(swi_values, dict)
        for field in FIELDS:
            values.extend((str(cetta_values[field]), str(swi_values[field])))
        lines.append("\t".join(values))
    (output / "results.tsv").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )
    return 0 if all_equal else 1


if __name__ == "__main__":
    raise SystemExit(main())
