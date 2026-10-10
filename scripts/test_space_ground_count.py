#!/usr/bin/env python3
"""Check count observers and their declines against exact expected results."""
import argparse
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = [
    ("duplicate", "2"), ("missing", "0"), ("resolved", "2"),
    ("failed-template", "0"), ("multiple-template", "4"),
    ("effects", "2"), ("effect-rows", "2"), ("removed-one", "1"),
    ("removed-all", "0"), ("open-stored", "3"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--engines", nargs="+", default=("native", "pathmap"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    modes = [("he", profile) for profile in ("he", "he-compat", "extended")]
    modes += [("petta", profile) for profile in
              ("extended", "typecheck-v2", "typecheck-v3")]
    runs = 0
    for language, profile in modes:
        for engine in args.engines:
            for index in ("", "0", "1"):
                env = dict(os.environ, CETTA_PATHMAP_QUERY_INDEX=index)
                expected = list(EXPECTED)
                if language == "petta":
                    expected[7] = ("removed-one", "0")
                fixtures = [("observers", expected)]
                if language == "petta" or profile == "extended":
                    fixtures.append(("strict", [
                        ("duplicate", "3"), ("missing", "0"), ("resolved", "3"),
                        ("failed-template", "0"), ("multiple-template", "6"),
                        ("size-atom", "0" if language == "petta" else "2"),
                    ]))
                for fixture, expected in fixtures:
                    command = [str(binary), "--quiet", "--lang", language,
                               "--profile", profile, "--space-engine", engine,
                               f"tests/support/space_ground_count_{fixture}.metta"]
                    result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                            capture_output=True, timeout=60)
                    actual = re.findall(r"^\(countprobe (\S+) (\d+)\)$",
                                        result.stdout, re.M)
                    if result.returncode or result.stderr or actual != expected:
                        raise AssertionError((language, profile, engine, index, fixture,
                                              result.returncode, result.stdout,
                                              result.stderr, actual))
                    runs += 1
    print(f"PASS: ground count observer controls ({runs} checked runs)")


if __name__ == "__main__":
    main()
