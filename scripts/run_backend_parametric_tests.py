#!/usr/bin/env python3
"""Run expected-file tests under one or more CeTTa space engines."""

from __future__ import annotations

import argparse
import difflib
import glob
import shlex
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)


def split_words(value: str) -> list[str]:
    return [word for word in shlex.split(value or "") if word]


def discover(patterns: list[str]) -> list[Path]:
    found: set[Path] = set()
    for pattern in patterns:
        matches = glob.glob(pattern)
        for match in matches:
            path = Path(match)
            if path.is_file():
                found.add(path)
    return sorted(found, key=lambda p: str(p))


def short_diff(expected: str, actual: str, expected_label: str,
               actual_label: str, limit: int) -> str:
    lines = difflib.unified_diff(
        expected.splitlines(),
        actual.splitlines(),
        fromfile=expected_label,
        tofile=actual_label,
        lineterm="",
    )
    return "\n".join(list(lines)[:limit])


def run_one(cetta: str, backend: str, lang: str, profile: str | None,
            test_file: Path, timeout: float) -> tuple[bool, str]:
    cmd = [cetta]
    if profile:
        cmd.extend(["--profile", profile])
    cmd.extend(["--space-engine", backend, "--lang", lang, str(test_file)])
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        return False, f"TIMEOUT after {timeout:g}s\n{output.rstrip()}"
    output = proc.stdout.rstrip("\n")
    if proc.returncode != 0:
        return False, f"EXIT {proc.returncode}\n{output}"
    return True, output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", default="./cetta")
    parser.add_argument("--lang", default="he")
    parser.add_argument("--profile", default=None)
    parser.add_argument("--backends", required=True,
                        help="Space-separated backend names.")
    parser.add_argument("--skip-tests", default="",
                        help="Space-separated test paths to skip.")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--diff-lines", type=int, default=24)
    parser.add_argument("patterns", nargs="+")
    args = parser.parse_args()

    backends = split_words(args.backends)
    if not backends:
        print("FAIL: no backends requested", file=sys.stderr)
        return 2

    skip_tests = {str(Path(path)) for path in split_words(args.skip_tests)}
    tests = discover(args.patterns)
    if not tests:
        print("FAIL: no tests matched requested patterns", file=sys.stderr)
        return 2

    totals = {
        backend: defaultdict(int)
        for backend in backends
    }
    missing_expected = 0
    skipped = 0
    total_failures = 0

    for test_file in tests:
        test_key = str(test_file)
        if test_key in skip_tests:
            print(f"SKIP: {test_key} (configured skip)")
            skipped += 1
            continue
        expected_path = test_file.with_suffix(".expected")
        if not expected_path.is_file():
            print(f"SKIP: {test_key} (no .expected file)")
            missing_expected += 1
            continue
        expected = expected_path.read_text().rstrip("\n")
        for backend in backends:
            ok, actual = run_one(args.cetta, backend, args.lang, args.profile,
                                 test_file, args.timeout)
            if ok and actual == expected:
                print(f"PASS[{backend}]: {test_key}")
                totals[backend]["pass"] += 1
                continue

            print(f"FAIL[{backend}]: {test_key}")
            if ok:
                print(short_diff(expected, actual, str(expected_path),
                                 f"{test_key} ({backend})", args.diff_lines))
            else:
                print(actual)
            totals[backend]["fail"] += 1
            total_failures += 1

    print("---")
    for backend in backends:
        passed = totals[backend]["pass"]
        failed = totals[backend]["fail"]
        print(f"{backend}: {passed} passed, {failed} failed")
    print(f"skipped: {skipped}; missing expected: {missing_expected}")
    return 1 if total_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
