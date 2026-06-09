#!/usr/bin/env python3
"""Fail if feature-availability rejection errors creep back in."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = [
    ROOT / "src",
    ROOT / "lib",
    ROOT / "tests",
    ROOT / "scripts",
    ROOT / "Makefile",
]
SKIP_FILES = {Path(__file__).resolve()}
SKIP_DIR_NAMES = {".git", "__pycache__", "runtime"}
SKIP_SUFFIXES = {
    ".a",
    ".d",
    ".o",
    ".pyc",
    ".so",
}

FORBIDDEN = [
    re.compile(r"profile_surface_error"),
    re.compile(r"SearchPolicyLaneUnavailable"),
    re.compile(r"search_policy_reason_unavailable"),
    re.compile(r"profile-surface-rust-gap"),
    re.compile(r"surface .*unavailable", re.IGNORECASE),
    re.compile(r"unavailable in profile", re.IGNORECASE),
    re.compile(r"unavailable in language", re.IGNORECASE),
]


def iter_files(path: Path):
    if path.is_file():
        yield path
        return
    for child in path.rglob("*"):
        if not child.is_file():
            continue
        if any(part in SKIP_DIR_NAMES for part in child.relative_to(ROOT).parts):
            continue
        if child.resolve() in SKIP_FILES:
            continue
        if child.suffix in SKIP_SUFFIXES:
            continue
        yield child


def main() -> int:
    hits: list[str] = []
    for root in SCAN_ROOTS:
        for path in iter_files(root):
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError as exc:
                hits.append(f"{path}: read failed: {exc}")
                continue
            for lineno, line in enumerate(text.splitlines(), start=1):
                if any(pattern.search(line) for pattern in FORBIDDEN):
                    rel = path.relative_to(ROOT)
                    hits.append(f"{rel}:{lineno}:{line}")
    if hits:
        print("Forbidden feature-availability rejection text found:", file=sys.stderr)
        for hit in hits:
            print(hit, file=sys.stderr)
        return 1
    print("PASS: no forbidden feature-availability rejection text")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
