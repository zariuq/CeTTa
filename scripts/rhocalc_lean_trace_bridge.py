#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from rhocalc_bounded_reachability import normalize_expr, split_successor_set


def normalized_fixture_expr(bin_path: str, fixture: Path) -> tuple[str, str]:
    syntax = fixture.suffix.lstrip(".") or "mrho"
    proc = subprocess.run(
        [
            bin_path,
            "--translate",
            "--lang",
            "rhocalc",
            "--syntax",
            syntax,
            "--lang",
            "rhocalc",
            "--syntax",
            "mrho",
            str(fixture),
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return syntax, proc.stdout.strip()


def public_one_step_successors(bin_path: str, fixture: Path) -> list[str]:
    syntax, input_expr = normalized_fixture_expr(bin_path, fixture)
    proc = subprocess.run(
        [
            bin_path,
            "--rho-reduction-limit",
            "1",
            "--lang",
            "rhocalc",
            "--syntax",
            syntax,
            str(fixture),
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode not in (0, 3):
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        if proc.returncode == 0:
            return []
        raise RuntimeError("one-step run produced no residual output")
    residual = normalize_expr(bin_path, syntax, lines[0])
    if residual == input_expr:
        return []
    return [residual]


def main() -> int:
    if len(sys.argv) != 8:
        print(
            "usage: rhocalc_lean_trace_bridge.py "
            "<bin> <fixture.mrho> <expected-count> <mode> <expected-file-or-dash> "
            "<lean-file> <theorem>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    fixture = Path(sys.argv[2])
    expected_count = int(sys.argv[3])
    mode = sys.argv[4]
    expected_file_arg = sys.argv[5]
    lean_file = Path(sys.argv[6])
    theorem = sys.argv[7]

    if mode not in (
        "exact-one-step-nonempty",
        "exact-one-step-empty",
        "exact-one-step-match-file",
        "cetta-empty-lean-can-step",
    ):
        print(
            "mode must be exact-one-step-nonempty, exact-one-step-empty, exact-one-step-match-file, or cetta-empty-lean-can-step",
            file=sys.stderr,
        )
        return 2

    try:
        successors = public_one_step_successors(bin_path, fixture)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if len(successors) != expected_count:
        print("successor count mismatch", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        print(f"expected: {expected_count}", file=sys.stderr)
        print(f"actual: {len(successors)}", file=sys.stderr)
        return 1

    if mode == "exact-one-step-nonempty" and len(successors) == 0:
        print("expected a one-step witness but successor set is empty", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        return 1

    if mode in ("exact-one-step-empty", "cetta-empty-lean-can-step") and len(successors) != 0:
        print("expected empty one-step successor set but found successors", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        print(f"successor-count: {len(successors)}", file=sys.stderr)
        return 1

    if mode == "exact-one-step-match-file":
        if expected_file_arg == "-":
            print("expected file must be provided for exact-one-step-match-file", file=sys.stderr)
            return 2
        expected_file = Path(expected_file_arg)
        try:
            expected_text = expected_file.read_text(encoding="utf-8").strip()
        except OSError as exc:
            print(str(exc), file=sys.stderr)
            return 2
        expected_items = split_successor_set(expected_text)
        if expected_items:
            if len(expected_items) != 1:
                print("expected file must contain exactly one residual for exact-one-step-match-file", file=sys.stderr)
                print(f"file: {expected_file}", file=sys.stderr)
                print(f"items: {len(expected_items)}", file=sys.stderr)
                return 1
            expected_residual = expected_items[0]
        else:
            expected_residual = expected_text
        if len(successors) != 1:
            print("expected exactly one one-step successor", file=sys.stderr)
            print(f"fixture: {fixture}", file=sys.stderr)
            print(f"actual-count: {len(successors)}", file=sys.stderr)
            return 1
        if successors[0] != expected_residual:
            print("one-step residual mismatch", file=sys.stderr)
            print(f"fixture: {fixture}", file=sys.stderr)
            print(f"expected: {expected_residual}", file=sys.stderr)
            print(f"actual: {successors[0]}", file=sys.stderr)
            return 1

    try:
        lean_text = lean_file.read_text(encoding="utf-8")
    except OSError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if theorem not in lean_text:
        print("lean theorem name not found", file=sys.stderr)
        print(f"file: {lean_file}", file=sys.stderr)
        print(f"theorem: {theorem}", file=sys.stderr)
        return 1

    if mode == "cetta-empty-lean-can-step" and "CanStep" not in lean_text:
        print("Lean CanStep witness not found in theorem file", file=sys.stderr)
        print(f"file: {lean_file}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
