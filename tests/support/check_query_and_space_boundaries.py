#!/usr/bin/env python3
"""Bound query roots and first named-space mutations agree on every route."""

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
binary = str(Path(sys.argv[1]).resolve())
checks = []
for profile in ("he", "he-compat", "extended"):
    for route in (
        {"CETTA_HE_SHARED_EXECUTION": "0"},
        {"CETTA_HE_SHARED_EXECUTION": "1"},
        {"CETTA_HE_SHARED_EXECUTION": "1", "CETTA_PREPARED_PURE_REFERENCE": "1",
         "CETTA_OPEN_EQUATIONS_REFERENCE": "0"},
        {"CETTA_HE_SHARED_EXECUTION": "1", "CETTA_OPEN_EQUATIONS_REFERENCE": "1"},
    ):
        checks.append(("tests/test_match_bound_query.metta",
                       ["--lang", "he", "--profile", profile], route))
for reference in ("0", "1"):
    for args in (["--lang", "petta"], ["--lang", "petta@1.1"],
                 ["--lang", "petta", "--profile", "extended"]):
        checks.append(("tests/petta/named_space_first_mutation.metta", args,
                       {"CETTA_OPEN_EQUATIONS_REFERENCE": reference}))
    checks.append(("tests/petta/named_space_first_nodup.metta",
                   ["--lang", "petta", "--profile", "extended"],
                   {"CETTA_OPEN_EQUATIONS_REFERENCE": reference}))

for fixture, arguments, overrides in checks:
    expected = (ROOT / fixture).with_suffix(".expected").read_text()
    run = subprocess.run([binary, *arguments, fixture], cwd=ROOT,
                         env=dict(os.environ, **overrides), capture_output=True,
                         text=True, timeout=60)
    if run.returncode or run.stdout != expected or "❌" in run.stderr:
        print(f"FAIL: {fixture} {arguments} {overrides}; exit={run.returncode}")
        print(run.stdout, run.stderr)
        raise SystemExit(1)
print(f"PASS: {len(checks)} bound-query and first-mutation observations")
