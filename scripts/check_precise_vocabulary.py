#!/usr/bin/env python3
"""Reject the word ``surface`` anywhere in this tree.

The word is banned outright: every prior use was a stand-in for a more
precise noun.  The public builtin set is named ``builtin``; the two text
notations are ``rho``/``mrho`` syntax; and cost-rho's purse coordinate is
its ``location`` (Meredith: a purse is *located at* a rho name; the word
``surface`` appears in his cost papers only in the attack-surface idiom,
never as the purse coordinate).  If a genuinely spatial or geometric
surface ever enters this tree, add a narrow exemption below with the
mathematical justification.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = (
    ROOT / "Makefile",
    ROOT / "README.md",
    ROOT / "SHOWCASE.md",
    ROOT / "MORK_TUTORIAL.md",
    ROOT / "benchmarks",
    ROOT / "docs",
    ROOT / "examples",
    ROOT / "experiments",
    ROOT / "langdef",
    ROOT / "lib",
    ROOT / "rust",
    ROOT / "scripts",
    ROOT / "specs",
    ROOT / "src",
    ROOT / "tests",
    ROOT / "tools",
)

# No current exemptions.  A future exemption must name a genuinely
# spatial/geometric surface and cite its mathematical meaning.
DOMAIN_SURFACE_PATHS: set[Path] = set()

SKIP_DIRS = {".git", "__pycache__", "runtime", "target"}
SKIP_SUFFIXES = {
    ".a",
    ".d",
    ".gcda",
    ".gcno",
    ".o",
    ".pdf",
    ".pyc",
    ".so",
}
WORD = re.compile(r"surface", re.IGNORECASE)


def files_under(path: Path):
    if path.is_file():
        yield path
        return
    if not path.exists():
        return
    for candidate in path.rglob("*"):
        if not candidate.is_file():
            continue
        rel = candidate.relative_to(ROOT)
        if any(part in SKIP_DIRS for part in rel.parts):
            continue
        if candidate.suffix in SKIP_SUFFIXES:
            continue
        yield candidate


def main() -> int:
    this_file = Path(__file__).resolve()
    hits: list[str] = []
    for scan_root in SCAN_ROOTS:
        for path in files_under(scan_root):
            if path.resolve() == this_file:
                continue
            rel = path.relative_to(ROOT)
            if WORD.search(path.name) and rel not in DOMAIN_SURFACE_PATHS:
                hits.append(f"{rel}: banned filename")
            if rel in DOMAIN_SURFACE_PATHS:
                continue
            try:
                source = path.read_text(encoding="utf-8", errors="ignore")
            except OSError as exc:
                hits.append(f"{rel}: read failed: {exc}")
                continue
            for line_number, line in enumerate(source.splitlines(), start=1):
                if WORD.search(line):
                    hits.append(f"{rel}:{line_number}:{line}")

    if hits:
        print(
            "Banned syntax/interface metaphor found; use the concrete noun:",
            file=sys.stderr,
        )
        for hit in hits:
            print(hit, file=sys.stderr)
        return 1

    print("PASS: no use of 'surface' as a syntax/interface metaphor")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
