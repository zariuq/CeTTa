#!/usr/bin/env python3
"""Check the public token-profile parser lifecycle in the three data profiles."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "tests/langdef/bnf/token_parser_values_v1.metta"
EXPECTED = ["documented", "c-flex", "positioned-refusal", "close",
            "distinct-occurrences", "repeated-join", "policy-mismatch"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    args = parser.parse_args()
    for language, profile in [("prime", None), ("he", "extended"), ("petta", "extended")]:
        command = [str(args.cetta.resolve()), "--lang", language]
        if profile:
            command += ["--profile", profile]
        result = subprocess.run(command + [str(CLIENT)], cwd=ROOT, capture_output=True,
                                text=True, timeout=120)
        rows = re.findall(r"\(TokenCheckV1 ([\w-]+) yes\)", result.stdout)
        if result.returncode or result.stderr or rows != EXPECTED or " no " in result.stdout:
            raise AssertionError((language, result.returncode, result.stdout, result.stderr))
        print(f"(LbnfTokenValuesV1Summary {language} {len(rows)} 0)")


if __name__ == "__main__":
    main()
