#!/usr/bin/env python3
"""Run summary-only regular-span DFA probes on caller-supplied corpora."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
import time

from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_regular_span_dfa_v1 import (
    CASES,
    GateFailure,
    c_compiler_answers,
    parse_native_output,
)


CASE_BY_LANGUAGE = {str(case["language"]): case for case in CASES}


def corpus_argument(value: str) -> tuple[str, Path]:
    language, separator, raw_path = value.partition("=")
    if not separator or language not in CASE_BY_LANGUAGE or not raw_path:
        raise argparse.ArgumentTypeError(
            "expected one of prime|metamath|megalodon|tptp followed by =PATH"
        )
    path = Path(raw_path).resolve()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"corpus is not a file: {path}")
    return language, path


def run_probe(
    chart_binary: Path,
    dfa_binary: Path,
    petta_root: Path,
    language: str,
    corpus: Path,
    work_limit: int,
    token_limit: int,
) -> str:
    case = CASE_BY_LANGUAGE[language]
    compiler = c_compiler_answers(chart_binary, case)
    terms = tuple(compiler.get("terms", ()))
    if (
        compiler.get("term_digest") != case["answer_digest"]
        or compiler.get("answers") != case["answers"]
    ):
        raise GateFailure(f"{language}: compiler answers changed before scale probe")
    stream = export_stream(petta_root, ABI_CASES[language])
    with tempfile.TemporaryDirectory(
        prefix=f"gslt2parse-regular-span-{language}-"
    ) as raw:
        temp = Path(raw)
        abi_path = temp / "pack.abi"
        nfa_path = temp / "answers.nfa"
        abi_path.write_bytes(stream)
        nfa_path.write_text("\n".join(terms) + "\n", encoding="utf-8")
        start = time.monotonic()
        completed = subprocess.run(
            [
                str(dfa_binary), str(abi_path), str(nfa_path), str(corpus),
                "--summary",
                "--work-limit", str(work_limit),
                "--token-limit", str(token_limit),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=600,
        )
        elapsed_ms = round((time.monotonic() - start) * 1000)
    if completed.returncode != 0:
        raise GateFailure(
            f"{language}: scale probe failed: {completed.stderr.strip()}"
        )
    result = parse_native_output(completed.stdout)
    if result.get("build-outcome") != "completed":
        raise GateFailure(f"{language}: DFA construction was resource limited")
    fields = (
        language,
        str(case["tag"]),
        result.get("input-bytes"),
        result.get("input-scalars"),
        result.get("decoded-bytes"),
        result.get("source-passes"),
        result.get("work-items"),
        result.get("token-count"),
        result.get("dfa-states"),
        result.get("dfa-transitions"),
        result.get("scan-outcome"),
        str(elapsed_ms),
    )
    if any(value is None for value in fields):
        raise GateFailure(f"{language}: scale receipt omitted accounting")
    return "(RegularSpanDFAV1ScaleReceipt " + " ".join(fields) + ")"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--dfa-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument(
        "--corpus", action="append", type=corpus_argument, required=True
    )
    parser.add_argument("--work-limit", type=int, default=1_000_000_000)
    parser.add_argument("--token-limit", type=int, default=20_000_000)
    arguments = parser.parse_args()
    if arguments.work_limit <= 0 or arguments.token_limit <= 0:
        parser.error("work and token limits must be positive")
    for language, corpus in arguments.corpus:
        print(run_probe(
            arguments.chart_binary.resolve(),
            arguments.dfa_binary.resolve(),
            arguments.petta_root.resolve(),
            language,
            corpus,
            arguments.work_limit,
            arguments.token_limit,
        ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
