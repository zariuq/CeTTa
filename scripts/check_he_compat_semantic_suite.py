#!/usr/bin/env python3
"""Run the CeTTa HE-compat semantic gate for the Tier 0/1 catalog slice."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

from mettapedia_paths import discover_mettapedia_root

ROOT = Path(__file__).resolve().parents[1]
METTAPEDIA = discover_mettapedia_root()
CONFORMANCE = METTAPEDIA / "scripts" / "conformance"
DEFAULT_CATALOG = (
    ROOT
    / "tests"
    / "generated"
    / "he_compat"
    / "he_compat_cases_2026-06-25.json"
)
DEFAULT_RESULTS = ROOT / "runtime" / "he_compat" / "he_io_results_latest.jsonl"
DEFAULT_SCRATCH = ROOT / "runtime" / "he_compat" / "he_io_cases"


def load_catalog(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise FileNotFoundError(f"catalog not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not isinstance(data.get("cases"), list):
        raise ValueError("catalog must be a JSON object with a cases list")
    return data


def run(cmd: list[str], cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, capture_output=True)


def print_proc(proc: subprocess.CompletedProcess[str]) -> None:
    if proc.stdout:
        print(proc.stdout, end="" if proc.stdout.endswith("\n") else "\n")
    if proc.stderr:
        print(proc.stderr, end="" if proc.stderr.endswith("\n") else "\n")


def run_mettapedia_oracle(results: Path, scratch: Path) -> None:
    results.parent.mkdir(parents=True, exist_ok=True)
    scratch.mkdir(parents=True, exist_ok=True)

    steps = [
        [
            "python3",
            str(CONFORMANCE / "check_he_io_lean_anchors.py"),
        ],
        [
            "python3",
            str(CONFORMANCE / "run_he_io_conformance.py"),
            "--results",
            str(results),
            "--scratch-dir",
            str(scratch),
        ],
        [
            "python3",
            str(CONFORMANCE / "check_he_io_baseline.py"),
            "--results",
            str(results),
            "--baseline",
            str(CONFORMANCE / "he_io_baseline_hyperon_0.2.10.json"),
        ],
    ]
    for cmd in steps:
        proc = run(cmd, cwd=METTAPEDIA)
        print_proc(proc)
        if proc.returncode != 0:
            raise RuntimeError(f"Mettapedia HE oracle step failed: {' '.join(cmd)}")


def normalize_term(term: str) -> str:
    return re.sub(r"\s+", " ", term.strip())


def split_top_level_commas(payload: str) -> list[str]:
    parts: list[str] = []
    buf: list[str] = []
    paren = 0
    bracket = 0
    in_str = False
    esc = False
    for ch in payload:
        if in_str:
            buf.append(ch)
            if esc:
                esc = False
                continue
            if ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            buf.append(ch)
            continue
        if ch == "(":
            paren += 1
            buf.append(ch)
            continue
        if ch == ")":
            paren -= 1
            buf.append(ch)
            continue
        if ch == "[":
            bracket += 1
            buf.append(ch)
            continue
        if ch == "]":
            bracket -= 1
            buf.append(ch)
            continue
        if ch == "," and paren == 0 and bracket == 0:
            token = "".join(buf).strip()
            if token:
                parts.append(token)
            buf = []
            continue
        buf.append(ch)
    tail = "".join(buf).strip()
    if tail:
        parts.append(tail)
    return parts


def parse_result_line(raw_line: str) -> list[str]:
    line = raw_line.strip()
    if not (line.startswith("[") and line.endswith("]")):
        raise ValueError(f"expected bracketed MeTTa result line, got: {line!r}")
    inner = line[1:-1].strip()
    if inner == "":
        return []
    return [normalize_term(tok) for tok in split_top_level_commas(inner)]


def first_result_terms(output: str) -> list[str]:
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            return parse_result_line(stripped)
    raise ValueError(f"no bracketed MeTTa result line found in output: {output!r}")


def bag_equal(left: list[str], right: list[str]) -> bool:
    return Counter(left) == Counter(right)


def run_cetta_case(
    cetta_bin: str,
    case_path: Path,
    expected_path: Path,
    oracle_expected: list[str],
) -> tuple[bool, str]:
    if not case_path.exists():
        return False, f"missing CeTTa case: {case_path}"
    if not expected_path.exists():
        return False, f"missing expected file: {expected_path}"
    cmd = [cetta_bin, "--profile", "he-compat", "--lang", "he", str(case_path)]
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    actual = proc.stdout
    if proc.stderr:
        actual = actual + proc.stderr
    expected_terms = [normalize_term(term) for term in oracle_expected]
    try:
        local_expected_terms = first_result_terms(expected_path.read_text(encoding="utf-8"))
        actual_terms = first_result_terms(actual)
    except ValueError as exc:
        return False, str(exc)
    if not bag_equal(local_expected_terms, expected_terms):
        return (
            False,
            f"local expected drift for {expected_path}\n"
            f"oracle expected: {expected_terms}\n"
            f"local expected: {local_expected_terms}",
        )
    if bag_equal(actual_terms, expected_terms):
        return True, ""
    return (
        False,
        f"CeTTa mismatch for {case_path}\n"
        f"oracle expected bag: {expected_terms}\n"
        f"actual bag: {actual_terms}\n"
        f"actual output:\n{actual}",
    )


def check_catalog_tier0_1(catalog: dict[str, Any], cetta_bin: str) -> None:
    failures: list[str] = []
    checked = 0
    for row in catalog["cases"]:
        tier = int(row.get("tier", 99))
        if tier > 1:
            continue
        if row.get("classification") == "needs-triage":
            failures.append(f"{row.get('id')}: Tier 0/1 case is needs-triage")
            continue
        if not row.get("upstream_oracle") and not row.get("lean_comparator"):
            failures.append(f"{row.get('id')}: Tier 0/1 case has no oracle/comparator")
            continue
        case_rel = str(row.get("cetta_path", ""))
        expected_rel = str(row.get("cetta_expected_path", ""))
        if not case_rel or not expected_rel:
            failures.append(f"{row.get('id')}: missing CeTTa path or expected path")
            continue
        oracle_expected = row.get("expected", [])
        if not isinstance(oracle_expected, list):
            failures.append(f"{row.get('id')}: missing oracle expected list")
            continue
        ok, reason = run_cetta_case(
            cetta_bin,
            ROOT / case_rel,
            ROOT / expected_rel,
            [str(term) for term in oracle_expected],
        )
        checked += 1
        if ok:
            print(f"[cetta-pass] {row.get('id')} -> {case_rel}")
        else:
            failures.append(reason)
    if failures:
        raise RuntimeError("\n\n".join(failures))
    print(f"Tier 0/1 CeTTa catalog cases passed: {checked}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--cetta", default=os.environ.get("CETTA_BIN", str(ROOT / "cetta")))
    parser.add_argument("--results", type=Path, default=DEFAULT_RESULTS)
    parser.add_argument("--scratch-dir", type=Path, default=DEFAULT_SCRATCH)
    args = parser.parse_args()

    catalog = load_catalog(args.catalog)
    if not catalog.get("no_runtime_trace_or_certificate_syntax", False):
        raise RuntimeError("catalog must explicitly forbid runtime trace/certificate syntax")

    run_mettapedia_oracle(args.results, args.scratch_dir)
    check_catalog_tier0_1(catalog, args.cetta)
    print("HE-compat semantic suite: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
