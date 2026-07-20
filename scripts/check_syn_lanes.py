#!/usr/bin/env python3
"""Acceptance gates for Prime's syntax algebra and HE profile isolation."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "prime" / "conformance" / "syntax_algebra.metta"
EXPECTED = FIXTURE.with_suffix(".expected")
RUNTIME = ROOT / "runtime" / "syn-lane-gate"


def lane_arguments(lane: str) -> list[str]:
    if lane == "prime":
        return ["--lang", "prime"]
    if lane in ("he", "he-compat", "he-extended", "he-prime"):
        return ["--profile", lane, "--lang", "he"]
    raise ValueError(f"unknown lane: {lane}")


def run(cetta: Path, lane: str, *arguments: str) -> str:
    command = [str(cetta), *lane_arguments(lane), *arguments]
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=60,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{' '.join(command)} exited {completed.returncode}:\n"
            f"{completed.stdout}"
        )
    return completed.stdout.rstrip("\n")


def require_equal(label: str, actual: str, expected: str) -> None:
    if actual != expected:
        raise AssertionError(
            f"{label} mismatch\n--- expected ---\n{expected}\n"
            f"--- actual ---\n{actual}"
        )


def check_fixture(cetta: Path) -> int:
    expected = EXPECTED.read_text().rstrip("\n")
    require_equal(
        "syntax fixture:prime", run(cetta, "prime", str(FIXTURE)), expected
    )
    return 1


def check_compact_failures(cetta: Path) -> int:
    malformed = ("@(", "*@", "$@", "&@", "!(")
    checks = 0
    for source in malformed:
        escaped = source.replace("\\", "\\\\").replace('"', '\\"')
        expected = f'[(Error (parse "{escaped}") ParseFailed)]'
        require_equal(
            f"malformed compact {source!r}:prime",
            run(cetta, "prime", "-e", f'!(parse "{escaped}")'),
            expected,
        )
        checks += 1
    return checks


def check_expanded_quote_forms(cetta: Path) -> int:
    cases = (
        ("(quote)", "[(quote)]"),
        ("(unquote)", "[(unquote)]"),
    )
    checks = 0
    for source, expected in cases:
        require_equal(
            f"expanded form remains parseable data {source}:prime",
            run(cetta, "prime", "-e", f'!(parse "{source}")'),
            expected,
        )
        checks += 1
    return checks


def check_exec_boundary(cetta: Path) -> int:
    RUNTIME.mkdir(parents=True, exist_ok=True)
    sources = {
        "compact": "!(+ 20 22)\n",
        "expanded": "(syn:exec (+ 20 22))\n",
        "eval": "(eval (+ 20 22))\n",
    }
    paths: dict[str, Path] = {}
    for name, source in sources.items():
        path = RUNTIME / f"{name}.metta"
        path.write_text(source)
        paths[name] = path

    checks = 0
    compact = run(cetta, "prime", str(paths["compact"]))
    expanded = run(cetta, "prime", str(paths["expanded"]))
    ordinary_eval = run(cetta, "prime", str(paths["eval"]))
    require_equal("compact exec:prime", compact, "[42]")
    require_equal("expanded exec:prime", expanded, compact)
    require_equal("top-level eval remains data:prime", ordinary_eval, "")
    checks += 3
    return checks


def check_strict_profile_isolation(cetta: Path) -> int:
    arguments = (
        "-e", "!(syn:var x)",
        "-e", "!(syn:ref self)",
        "-e", "! @doc",
    )
    expected = "\n".join(
        (
            "[(syn:var x)]",
            "[(syn:ref self)]",
            "[@doc]",
        )
    )
    checks = 0
    for lane in ("he", "he-compat", "he-extended", "he-prime"):
        require_equal(
            f"strict profile isolation:{lane}",
            run(cetta, lane, *arguments),
            expected,
        )
        checks += 1
    return checks


def check_he_has_no_syn_library(cetta: Path) -> int:
    expected = "[(Error (import! &self syn) Failed to resolve module syn)]"
    checks = 0
    for lane in ("he", "he-compat", "he-extended", "he-prime"):
        require_equal(
            f"no HE syn library:{lane}",
            run(cetta, lane, "-e", "!(import! &self syn)"),
            expected,
        )
        checks += 1
    return checks


def check_he_reflection_unchanged(cetta: Path) -> int:
    common = "\n".join(("[A]", '["(+ 1 2)"]'))
    trailing = {
        "he": '[(Error (parse "&@(agent zar)") ParseFailed)]',
        "he-compat": "[&@]",
        "he-extended": '[(Error (parse "&@(agent zar)") ParseFailed)]',
        "he-prime": '[(Error (parse "&@(agent zar)") ParseFailed)]',
    }
    arguments = (
        "-e", '!(parse "A")',
        "-e", "!(repr (+ 1 2))",
        "-e", '!(parse "&@(agent zar)")',
    )
    checks = 0
    for lane, final_line in trailing.items():
        require_equal(
            f"ordinary HE parse/repr unchanged:{lane}",
            run(cetta, lane, *arguments),
            f"{common}\n{final_line}",
        )
        checks += 1
    return checks


def check_mutant_is_killed(cetta: Path, mutation: int) -> int:
    expected = EXPECTED.read_text().rstrip("\n")
    output = run(cetta, "prime", str(FIXTURE))
    if output == expected:
        raise AssertionError(f"syntax mutation {mutation} survived in Prime")
    killed = 1
    print(f"(SyntaxMutationSummary {mutation} {killed} {killed} 0)")
    return killed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--expect-mutant-killed", type=int, metavar="ID")
    args = parser.parse_args()
    cetta = args.cetta.resolve()
    if not cetta.is_file():
        print(f"missing CeTTa executable: {cetta}", file=sys.stderr)
        return 2

    try:
        if args.expect_mutant_killed is not None:
            check_mutant_is_killed(cetta, args.expect_mutant_killed)
            return 0
        checks = 0
        checks += check_fixture(cetta)
        checks += check_compact_failures(cetta)
        checks += check_expanded_quote_forms(cetta)
        checks += check_exec_boundary(cetta)
        checks += check_strict_profile_isolation(cetta)
        checks += check_he_has_no_syn_library(cetta)
        checks += check_he_reflection_unchanged(cetta)
    except (AssertionError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"(SyntaxLaneGateSummary {checks} {checks} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
