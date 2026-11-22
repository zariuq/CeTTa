#!/usr/bin/env python3
"""Check PeTTa's typed acceptance suite.

The suite is a corpus of typed PeTTa programs with the verdicts of Roman's
SWI-PeTTa checker at a pinned revision: exit 0 accepts, exit 2 rejects, under
the checker policy each case names.  The verdicts are the acceptance target
for a checker of PeTTa's typed line.  This check keeps the corpus whole:

- the manifest is well formed and its counts are its cases';
- every source is the one the verdict was recorded for, by SHA-256: the
  suite's own repros in this tree, and the reference cases in a checkout of
  the pinned revision;
- every program the checker accepts runs under PeTTa to completion, with no
  crash and within the time limit, by each route of the machine.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = "cetta-petta-typed-acceptance-v1"
POLICIES = ([], ["--strict"], ["--strict-det"])
VERDICTS = {0: "accept", 2: "reject"}


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source(case: dict, reference: pathlib.Path) -> pathlib.Path:
    relative = pathlib.PurePosixPath(case["file"])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"{case['id']}: unsafe path {case['file']}")
    if case["source_root"] == "candidate":
        return ROOT / pathlib.Path(relative)
    if case["source_root"] == "reference":
        return reference / pathlib.Path(relative)
    raise ValueError(f"{case['id']}: unknown source root")


def check_manifest(manifest: dict) -> list[dict]:
    if manifest.get("schema") != SCHEMA:
        raise ValueError(f"schema is not {SCHEMA}")
    cases = manifest["cases"]
    ids = [case["id"] for case in cases]
    if len(set(ids)) != len(ids):
        raise ValueError("duplicate case ids")
    verdicts = collections.Counter()
    cohorts = collections.Counter()
    for case in cases:
        if case["arguments"] not in POLICIES:
            raise ValueError(f"{case['id']}: unknown checker policy")
        expected = case["expected"]
        if VERDICTS.get(expected["exit"]) != expected["verdict"]:
            raise ValueError(f"{case['id']}: exit and verdict disagree")
        verdicts[expected["verdict"]] += 1
        cohorts[case["cohort"]] += 1
    counts = manifest["counts"]
    observed = {
        "accept": verdicts["accept"],
        "reject": verdicts["reject"],
        "roman_244": cohorts["roman-244"],
        "semantic_regressions": cohorts["semantic-regression"],
        "total": len(cases),
    }
    if observed != counts:
        raise ValueError(f"counts {counts} differ from the cases' {observed}")
    return cases


def check_reference_revision(reference: pathlib.Path, revision: str) -> None:
    head = subprocess.check_output(
        ["git", "-C", str(reference), "rev-parse", "HEAD"], text=True).strip()
    if head != revision:
        raise ValueError(f"reference checkout is at {head}, not {revision}")


def run(binary: pathlib.Path, path: pathlib.Path, cwd: pathlib.Path,
        machine: str, timeout: float) -> tuple[str, str]:
    environment = {"CETTA_PETTA_SEARCH_MACHINE": machine}
    try:
        completed = subprocess.run(
            [str(binary), "--lang", "petta", str(path)], cwd=cwd,
            capture_output=True, text=True, timeout=timeout,
            env={**os.environ, **environment})
    except subprocess.TimeoutExpired:
        return "timeout", ""
    if completed.returncode < 0:
        return f"signal {-completed.returncode}", completed.stderr[-400:]
    return f"exit {completed.returncode}", ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=pathlib.Path, required=True)
    parser.add_argument("--reference", type=pathlib.Path, required=True,
                        help="checkout of the oracle's pinned revision")
    parser.add_argument("--manifest", type=pathlib.Path,
                        default=ROOT / "tests/petta/typed_acceptance/manifest.json")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=60.0)
    arguments = parser.parse_args()
    binary = arguments.cetta.resolve()
    reference = arguments.reference.resolve()
    manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    try:
        cases = check_manifest(manifest)
        check_reference_revision(reference, manifest["oracle"]["revision"])
        for case in cases:
            path = source(case, reference)
            if not path.is_file():
                raise ValueError(f"{case['id']}: missing {case['file']}")
            if sha256(path) != case["source_sha256"]:
                raise ValueError(f"{case['id']}: {case['file']} changed")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"FAIL: typed acceptance suite: {error}")
        return 1
    accepted = sorted({source(case, reference) for case in cases
                       if case["expected"]["verdict"] == "accept"})
    jobs = [(path, machine) for path in accepted for machine in ("0", "1")]
    outcomes = collections.Counter()
    failures = []
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        futures = {pool.submit(run, binary, path, reference, machine,
                               arguments.timeout): (path, machine)
                   for path, machine in jobs}
        for future in concurrent.futures.as_completed(futures):
            path, machine = futures[future]
            outcome, detail = future.result()
            outcomes[outcome] += 1
            if not outcome.startswith("exit "):
                failures.append((path, machine, outcome, detail))
    for path, machine, outcome, detail in sorted(failures):
        print(f"FAIL: {path.name} (machine {machine}): {outcome} {detail}")
    if failures:
        return 1
    summary = ", ".join(f"{name} {count}" for name, count in sorted(outcomes.items()))
    print(f"PASS: typed acceptance suite: {len(cases)} cases "
          f"({manifest['counts']['accept']} accept, "
          f"{manifest['counts']['reject']} reject) match their sources; "
          f"{len(accepted)} accepted programs run on both routes ({summary})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
