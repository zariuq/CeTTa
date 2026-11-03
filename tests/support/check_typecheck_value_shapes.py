#!/usr/bin/env python3
"""Check value-certified list selection without closing Expression's domain."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/petta/typecheck_value_shapes"
POSITIVES = {
    "guarded-nested": "true\n3\n0\n0\n0\n2\n0\n",
    "list-nested": "true\n3\n0\n",
    "guarded-total-selectors": "7\n0\n9\n0\n",
}
REJECTIONS = {
    "open-expression": "open-count/1",
    "guard-else": "wrong-branch/1",
    "guard-other-variable": "wrong-variable/2",
    "nondet-callback": "bad-count/1",
    "nonlinear-cons": "bad-nonlinear/1",
    "cross-argument-cons": "bad-cross/2",
    "shadowed-predicate": "shadowed-count/1",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--profile", choices=("typecheck-v2", "typecheck-v3"), action="append")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    binary = args.binary.resolve()
    binary_sha = digest(binary)
    profiles = args.profile or ["typecheck-v2", "typecheck-v3"]
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=False)
    rows = []
    failures = []
    environment = dict(os.environ, LC_ALL="C.UTF-8")

    def execute(name: str, source: Path, profile: str, expected: str | None,
                rejected_function: str | None = None) -> None:
        command = [str(binary), "--lang", "petta", "--profile", profile]
        if profile != "extended":
            command.append("--strict-det")
        command.append(str(source))
        try:
            completed = subprocess.run(command, cwd=ROOT, env=environment,
                                       text=True, capture_output=True,
                                       timeout=args.timeout, check=False)
            stdout, stderr, status = completed.stdout, completed.stderr, completed.returncode
            correct = (
                status == 2 and not stdout and "PeTTa type error:" in stderr
                and "body cardinality" in stderr and rejected_function in stderr
            ) if rejected_function else (
                status == 0 and stdout == expected and not stderr
            )
        except subprocess.TimeoutExpired:
            stdout, stderr, status, correct = "", "timeout", 124, False
        row = dict(case=name, profile=profile, source_sha256=digest(source),
                   argv=command, rc=status, correct=correct,
                   stdout=stdout, stderr=stderr)
        rows.append(row)
        if args.output_dir:
            stem = args.output_dir / (profile + "-" + name)
            stem.with_suffix(".stdout").write_text(stdout)
            stem.with_suffix(".stderr").write_text(stderr)
        if not correct:
            failures.append(f"{profile}:{name}: rc={status}, stdout={stdout!r}, stderr={stderr!r}")

    for profile in profiles:
        for name, expected in POSITIVES.items():
            execute(name, FIXTURES / (name + ".metta"), profile, expected)
        for name, function in REJECTIONS.items():
            execute(name, FIXTURES / (name + ".metta"), profile, None, function)
        execute("existing-open-domain-73",
                ROOT / "tests/petta/typecheck_v2_repros/73_expression_empty_cons_selection_is_not_total.metta",
                profile, None, "expression-shape-caller/1")
    # These explicit cardinalities establish the nonlinear and correlated
    # selector counterexamples. An empty process output would fail this check.
    execute("selector-counts", FIXTURES / "selector-counts.metta", "extended", "0\n0\n1\n")
    if digest(binary) != binary_sha:
        failures.append("binary changed during qualification")
    if args.output_dir:
        (args.output_dir / "results.json").write_text(json.dumps(
            dict(binary=str(binary), binary_sha256=binary_sha, rows=rows,
                 failures=failures, passed=not failures), indent=2) + "\n")
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"PASS: {len(rows)} proper-list value selection controls; exact positive answers and discriminating rejections")


if __name__ == "__main__":
    main()
