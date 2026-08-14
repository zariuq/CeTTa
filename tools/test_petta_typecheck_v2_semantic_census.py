#!/usr/bin/env python3
"""Qualify non-calculus PeTTa mechanisms in the semantic census."""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from petta_typecheck_v2_corpus import split_census_records  # noqa: E402


def run_case(
    cetta: pathlib.Path,
    fixture: pathlib.Path,
    *,
    reference: bool,
    profile: str | None,
) -> tuple[str, str, dict[str, dict[str, str]], set[str]]:
    environment = os.environ.copy()
    environment["CETTA_PETTA_TYPECHECK_CENSUS"] = "1"
    environment["CETTA_PETTA_SEARCH_MACHINE"] = "1"
    if reference:
        environment["CETTA_PETTA_CLAUSE_SLOT_FRAME_REFERENCE"] = "1"
    command = [str(cetta), "--lang", "petta"]
    if profile:
        command.extend(["--profile", profile])
    command.append(str(fixture))
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        text=True,
        capture_output=True,
        check=False,
        timeout=30.0,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{profile or 'default'} "
            f"{'reference' if reference else 'direct'} matcher exited "
            f"{completed.returncode}: {completed.stderr}"
        )
    clean_stderr, catalog, hits = split_census_records(completed.stderr)
    return completed.stdout, clean_stderr, catalog, hits


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=pathlib.Path, required=True)
    parser.add_argument("--fixture", type=pathlib.Path, required=True)
    parser.add_argument("--expected", type=pathlib.Path, required=True)
    args = parser.parse_args()

    cetta = args.cetta.resolve()
    fixture = args.fixture.resolve()
    expected = args.expected.read_text(encoding="utf-8")
    event = "clause-slot-alias-preserved"
    expected_descriptor = {
        "scope": "mechanism",
        "kind": "mechanism",
        "mapping": "caller-visible-substitution-preservation",
        "axis": "source-evaluated-stage-evidence",
    }
    for label, profile in (("default", None), ("typecheck-v2", "typecheck-v2")):
        direct_out, direct_err, catalog, direct_hits = run_case(
            cetta, fixture, reference=False, profile=profile
        )
        reference_out, reference_err, reference_catalog, reference_hits = (
            run_case(cetta, fixture, reference=True, profile=profile)
        )
        if direct_out != expected or reference_out != expected:
            raise SystemExit(
                f"{label} clause-slot direct/reference matcher drifted "
                "from the exact fixture"
            )
        if direct_out != reference_out or direct_err != reference_err:
            raise SystemExit(
                f"{label} clause-slot direct/reference observations diverged"
            )
        if catalog.get(event) != expected_descriptor:
            raise SystemExit(
                f"{label} clause-slot alias event lacks its mechanism mapping"
            )
        if (
            reference_catalog
            and reference_catalog.get(event) != expected_descriptor
        ):
            raise SystemExit(
                f"{label} direct/reference binaries reported different catalogs"
            )
        if event not in direct_hits:
            raise SystemExit(
                f"{label} direct clause-slot fixture did not exercise "
                "alias preservation"
            )
        if event in reference_hits:
            raise SystemExit(
                f"{label} isolated reference matcher reported a "
                "direct-frame event"
            )

    print(
        "PASS: clause-slot alias preservation is exercised by the direct "
        "matcher and agrees exactly with the isolated authority under the "
        "default and typecheck-v2 profiles"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
