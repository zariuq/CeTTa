#!/usr/bin/env python3
"""Check the authored parser assertions through the native query report."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/test_gparse_gslt_core.metta"


def check(binary: Path, fixture: Path, report: Path) -> None:
    result = subprocess.run(
        [str(binary), "--lang", "he", "--profile", "extended",
         "--run-report", str(report), str(fixture)],
        cwd=ROOT, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"parser property exited {result.returncode}: {result.stderr[-2000:]}")
    data = json.loads(report.read_text())
    queries = data["queries"]
    if (data["dialect_exit"] or data["contract_exit"] or
            data["queries_unsettled"] or data["queries_declared"] != len(queries)):
        raise RuntimeError("parser property did not settle its declared query contract")
    if sum(query["tests"] for query in queries) != 20:
        raise RuntimeError("parser property must report all 20 authored assertions")
    for query in queries:
        if (not query["observation_complete"] or not query["tests_passed"] or
                not query["finalization_ready"] or not query["framing_ok"] or query["faults"]):
            raise RuntimeError(f"parser property query {query['id']} did not pass completely")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--fixture", type=Path, default=FIXTURE)
    args = parser.parse_args()
    output = ROOT / "runtime/test-gparse-core-property"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="check-", dir=output) as directory:
        check(args.binary.resolve(), args.fixture.resolve(), Path(directory) / "report.json")
    print("PASS: all 20 authored parser properties settle with complete native verdicts")


if __name__ == "__main__":
    main()
