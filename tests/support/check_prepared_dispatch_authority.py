#!/usr/bin/env python3
"""Check dialect dispatch, including PeTTa's static-definition refusal."""

import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
args = parser.parse_args()
binary = str(args.binary.resolve())
tests = Path(__file__).resolve().parents[1]
source = (tests / "prepared_pure_dispatch_authority.metta").read_text()

for dialect in ("he", "prime"):
    result = subprocess.run([binary, "--lang", dialect, "-e", source],
                            capture_output=True, text=True, timeout=30)
    expected = (tests / f"prepared_pure_dispatch_authority.{dialect}.expected").read_text()
    assert (result.returncode, result.stdout, result.stderr) == (0, expected, ""), (dialect, result)

# These definitions are invalid in plain PeTTa; refusing them is part of its
# dispatch authority. Check every declaration separately, then retain all of
# the positive builtin and ordinary-relational calls from the same fixture.
declarations = {"length": "(= (length $items) 77)",
                "last": "(= (last $items) 88)",
                "id": "(= (id $value) 99)"}
for head, declaration in declarations.items():
    assert source.count(declaration) == 1
    result = subprocess.run([binary, "--lang", "petta", "-e", declaration + "\n!(unreachable)"],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and not result.stdout, (head, result)
    assert f"(permission_error modify static_procedure (/ {head} 2))" in result.stderr, (head, result)
    source = source.replace(declaration, "")

result = subprocess.run([binary, "--lang", "petta", "-e", source],
                        capture_output=True, text=True, timeout=30)
expected = (tests / "prepared_pure_dispatch_authority.petta.expected").read_text()
assert (result.returncode, result.stdout, result.stderr) == (0, expected, ""), result
print("PASS: HE/Prime dispatch, PeTTa static refusals, builtin calls and ordinary occurrences")
