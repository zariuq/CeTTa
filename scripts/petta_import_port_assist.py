#!/usr/bin/env python3
"""Report deterministic import rewrites for PeTTa -> CeTTa --lang petta ports.

This helper is intentionally narrow: it only normalizes import specs whose
meaning is already clear from the repo layout, and it flags the rest for
manual review instead of guessing at semantics.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


IMPORT_RE = re.compile(
    r'^(?P<prefix>\s*!\(import!\s+)(?P<dest>&[^\s()]+)\s+'
    r'(?P<spec>"[^"]*"|[^\s()]+)(?P<suffix>\s*\).*)$'
)


MANUAL_MODULES = {
    "lib_he",
    "lib_spaces",
    "lib_tabling",
}


@dataclass(frozen=True)
class ImportDecision:
    action: str
    normalized: str | None
    reason: str


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def cetta_lib_stems(root: pathlib.Path) -> set[str]:
    stems: set[str] = set()
    for path in (root / "lib").rglob("*.metta"):
        stems.add(path.stem)
    return stems


def unquote(token: str) -> str:
    if len(token) >= 2 and token[0] == '"' and token[-1] == '"':
        return token[1:-1]
    return token


def legacy_import_basename(spec: str) -> str:
    candidate = spec.replace("\\", "/").rstrip("/")
    return pathlib.PurePosixPath(candidate).name


def classify_import(spec: str, known_libs: set[str]) -> ImportDecision:
    raw = unquote(spec)
    if raw.startswith("("):
        return ImportDecision("manual", None, "compound import spec needs manual review")

    basename = legacy_import_basename(raw)
    if basename in MANUAL_MODULES:
        return ImportDecision("manual", None, f"{basename} is not in the first native Petta tranche")

    if basename in known_libs:
        if raw == basename:
            return ImportDecision("keep", basename, "already matches a CeTTa-local module stem")
        return ImportDecision("rewrite", basename, f"normalize legacy PeTTa path to CeTTa-local {basename}")

    if raw in known_libs:
        return ImportDecision("keep", raw, "already matches a CeTTa-local module stem")

    return ImportDecision("manual", None, "no deterministic CeTTa-local mapping found")


def scan_file(path: pathlib.Path, known_libs: set[str]) -> tuple[list[str], list[str]]:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    report: list[str] = []
    rewritten: list[str] = []
    for lineno, line in enumerate(lines, start=1):
        match = IMPORT_RE.match(line)
        if not match:
            rewritten.append(line)
            continue
        spec_token = match.group("spec")
        decision = classify_import(spec_token, known_libs)
        report.append(
            f"{path}:{lineno}: {decision.action}: {unquote(spec_token)}"
            + (f" -> {decision.normalized}" if decision.normalized else "")
            + f" [{decision.reason}]"
        )
        if decision.action == "rewrite" and decision.normalized:
            normalized_token = (
                f"\"{decision.normalized}\""
                if spec_token.startswith('"')
                else decision.normalized
            )
            rewritten.append(
                f"{match.group('prefix')}{match.group('dest')} "
                f"{normalized_token}{match.group('suffix')}\n"
            )
        else:
            rewritten.append(line)
    return report, rewritten


def iter_targets(targets: list[str]) -> list[pathlib.Path]:
    out: list[pathlib.Path] = []
    for target in targets:
        path = pathlib.Path(target)
        if path.is_dir():
            out.extend(sorted(path.rglob("*.metta")))
        else:
            out.append(path)
    return out


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Suggest deterministic import normalization for PeTTa files."
    )
    parser.add_argument("targets", nargs="+", help="PeTTa .metta files or directories to inspect")
    parser.add_argument(
        "--rewrite-safe",
        action="store_true",
        help="Print the safe rewritten contents for a single file instead of the report",
    )
    args = parser.parse_args(argv)

    targets = iter_targets(args.targets)
    if args.rewrite_safe and len(targets) != 1:
        parser.error("--rewrite-safe requires exactly one file target")

    known_libs = cetta_lib_stems(repo_root())
    all_reports: list[str] = []
    rewritten_output: list[str] | None = None
    for path in targets:
        report, rewritten = scan_file(path, known_libs)
        all_reports.extend(report)
        if args.rewrite_safe:
            rewritten_output = rewritten

    if args.rewrite_safe:
        sys.stdout.write("".join(rewritten_output or []))
        return 0

    if not all_reports:
        print("No import! lines found.")
        return 0

    for line in all_reports:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
