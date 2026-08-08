#!/usr/bin/env python3
"""Regeneration and semantic-mutation gates for MatchDecisionPolicyV1."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


class GateFailure(RuntimeError):
    pass


def run_generator(generator: Path, policy: Path,
                  output: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        (sys.executable, str(generator),
         "--policy", str(policy), "--out", str(output)),
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False,
    )


def probe_cell(cc: str, header: Path, directory: Path) -> int:
    source = directory / f"probe-{header.stem}.c"
    binary = directory / f"probe-{header.stem}"
    source.write_text(
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        f'#include "{header}"\n'
        "int main(void) {\n"
        "    printf(\"%u\\n\", (unsigned)cetta_md_policy_v1[3][3][1][1]);\n"
        "    return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    compiled = subprocess.run(
        (cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
         str(source), "-o", str(binary)),
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False,
    )
    if compiled.returncode != 0:
        raise GateFailure(f"policy probe did not compile:\n{compiled.stderr}")
    executed = subprocess.run(
        (str(binary),), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    if executed.returncode != 0:
        raise GateFailure(f"policy probe failed:\n{executed.stderr}")
    return int(executed.stdout.strip())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--cc", default="cc")
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    policy = root / (
        "experiments/gslt2parse_foundation/presentations/core/"
        "match_decision_policy_v1.metta"
    )
    generator = root / "tools/generate_match_decision_policy_v1.py"
    generated = root / "src/generated/match_decision_policy_v1.generated.h"

    try:
        with tempfile.TemporaryDirectory(
                prefix="match-decision-policy-v1-") as raw:
            directory = Path(raw)
            candidate = directory / generated.name
            completed = run_generator(generator, policy, candidate)
            if completed.returncode != 0:
                raise GateFailure(
                    "policy regeneration failed:\n" + completed.stderr
                )
            if candidate.read_bytes() != generated.read_bytes():
                raise GateFailure("runtime-consumed policy table is stale")
            if probe_cell(arguments.cc, candidate, directory) != 1:
                raise GateFailure(
                    "authored equal-head policy does not keep the candidate"
                )

            source = policy.read_text(encoding="utf-8")
            anchor = (
                "(head (candidate-policy expression expression-head "
                "equal equal keep))"
            )
            if source.count(anchor) != 1:
                raise GateFailure("semantic mutation anchor changed")
            mutated_policy = directory / "mutated-policy.metta"
            mutated_policy.write_text(
                source.replace(anchor, anchor.replace("keep", "refute")),
                encoding="utf-8",
            )
            mutated = directory / "mutated.generated.h"
            completed = run_generator(generator, mutated_policy, mutated)
            if completed.returncode != 0:
                raise GateFailure(
                    "semantic mutant generation failed:\n" + completed.stderr
                )
            if mutated.read_bytes() == generated.read_bytes():
                raise GateFailure("semantic mutation left the table unchanged")
            if probe_cell(arguments.cc, mutated, directory) != 2:
                raise GateFailure(
                    "semantic mutation did not change executable behavior"
                )

            missing_policy = directory / "missing-policy.metta"
            missing_policy.write_text(
                source.replace(
                    "policy-expression-head-different",
                    "policy-expression-head-different-removed", 1,
                ).replace(
                    "(head (candidate-policy expression expression-head "
                    "equal different refute))",
                    "(head (removed-candidate-policy expression "
                    "expression-head equal different refute))", 1,
                ),
                encoding="utf-8",
            )
            completed = run_generator(
                generator, missing_policy,
                directory / "missing.generated.h",
            )
            if completed.returncode == 0:
                raise GateFailure("incomplete semantic policy was admitted")

        print("(MatchDecisionPolicyV1Summary 4 4 0)")
        return 0
    except (OSError, ValueError, GateFailure) as error:
        print(f"test_match_decision_policy_v1: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
