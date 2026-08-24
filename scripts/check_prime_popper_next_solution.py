#!/usr/bin/env python3
"""Reproduce and structurally check Popper's synthesis-next solution."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys


ATOM_RE = re.compile(r"(?P<name>[a-z_]+)\((?P<args>[^()]*)\)")
SCORE = "Precision:1.00 Recall:1.00 TP:7 FN:0 TN:4 FP:0 Size:8"


class SolutionError(RuntimeError):
    """The learned solution does not have the pinned logical structure."""


def parse_atom(text: str) -> tuple[str, tuple[str, ...]]:
    match = ATOM_RE.fullmatch(text.strip())
    if not match:
        raise SolutionError(f"cannot parse learned literal: {text!r}")
    return (
        match.group("name"),
        tuple(item.strip() for item in match.group("args").split(",")),
    )


def parse_clause(
    text: str,
) -> tuple[tuple[str, tuple[str, ...]], list[tuple[str, tuple[str, ...]]]]:
    if ":-" not in text or not text.endswith("."):
        raise SolutionError(f"cannot parse learned clause: {text!r}")
    head_text, body_text = text[:-1].split(":-", 1)
    body_parts = re.split(r"\s*,\s*(?=[a-z_]+\()", body_text)
    return parse_atom(head_text), [parse_atom(part) for part in body_parts]


def is_adjacent_clause(text: str) -> bool:
    head, body = parse_clause(text)
    if head[0] != "next_list" or len(head[1]) != 2 or len(body) != 4:
        return False
    source, result = head[1]
    heads = [args for name, args in body if name == "head"]
    tails = [args for name, args in body if name == "tail"]
    markers = [args for name, args in body if name == "x"]
    if len(heads) != 2 or len(tails) != 1 or len(markers) != 1:
        return False
    marker = markers[0][0]
    tail = tails[0][1] if tails[0][0] == source else None
    return tail is not None and (source, marker) in heads and (
        tail, result
    ) in heads


def is_scan_clause(text: str) -> bool:
    head, body = parse_clause(text)
    if head[0] != "next_list" or len(head[1]) != 2 or len(body) != 2:
        return False
    source, result = head[1]
    tails = [args for name, args in body if name == "tail"]
    recursive = [args for name, args in body if name == "next_list"]
    return (
        len(tails) == 1
        and len(recursive) == 1
        and tails[0][0] == source
        and recursive[0] == (tails[0][1], result)
    )


def solution_lines(stdout: str) -> tuple[str, list[str]]:
    marker = "********** SOLUTION **********"
    if marker not in stdout:
        raise SolutionError("Popper did not publish a complete solution")
    tail = stdout.rsplit(marker, 1)[1]
    lines = []
    for raw_line in tail.splitlines():
        line = re.sub(r"^\s*\d+(?:\.\d+)?s\s+", "", raw_line).strip()
        if line:
            lines.append(line)
    score = next((line for line in lines if line.startswith("Precision:")), "")
    clauses = [line for line in lines if line.startswith("next_list(")]
    return score, clauses


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--popper-root", type=Path, required=True,
        help="pinned Popper checkout root",
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()
    command = [
        "uv", "run", "popper.py", "examples/synthesis-next",
        "--timeout", str(args.timeout),
    ]
    try:
        completed = subprocess.run(
            command,
            cwd=args.popper_root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout + 60.0,
            check=False,
        )
        if completed.returncode != 0:
            raise SolutionError(
                f"Popper exited with status {completed.returncode}"
            )
        score, clauses = solution_lines(completed.stdout)
        if score != SCORE:
            raise SolutionError(f"unexpected score: {score!r}")
        if len(clauses) != 2:
            raise SolutionError(f"expected two clauses, found {clauses!r}")
        if sum(is_adjacent_clause(clause) for clause in clauses) != 1:
            raise SolutionError("missing unique adjacency clause")
        if sum(is_scan_clause(clause) for clause in clauses) != 1:
            raise SolutionError("missing unique recursive scan clause")
    except (OSError, subprocess.SubprocessError, SolutionError) as exc:
        print(f"FAIL: synthesis-next learning: {exc}", file=sys.stderr)
        return 1
    print(
        "PASS: pinned Popper reproduces the complete size-8 "
        "synthesis-next program"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
