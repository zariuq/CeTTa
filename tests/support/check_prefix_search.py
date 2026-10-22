#!/usr/bin/env python3
"""Ordered numeric prefix search against independent occurrence counting."""

import argparse
import bisect
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True, type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    rng = random.Random(93117)
    cases = [([], 0), ([2, 2, 2], 2), ([2, 2, 2], 3),
             ([-8, -2, 0, 0, 3], 0), ([0.1, 0.2, 0.2, 0.7], 0.2),
             ([2**53, 2**53 + 1], 2**53 + 1),
             ([-2**63, -1, 2**63 - 1], 2**63 - 1)]
    for size in (0, 1, 2, 3, 15, 16, 17, 127, 128, 129, 1024):
        values = sorted(rng.randrange(-20, 21) for _ in range(size))
        cases.extend((values, cutoff) for cutoff in (-22, -8, 0, 8, 22))
    expected = [sum(value < bound for value in values) for values, bound in cases]
    assert expected == [bisect.bisect_left(values, bound) for values, bound in cases]
    checks = 0
    fixture = subprocess.run([str(binary), "--no-results", "--lang", "petta@1.0.6",
                              "--profile", "extended", "tests/list_value/list_lower_bound.metta"],
                             cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert fixture.returncode == 0, fixture.stderr
    assert fixture.stdout == (ROOT / "tests/list_value/list_lower_bound.expected").read_text()
    checks += len(fixture.stdout.splitlines())
    (ROOT / "runtime").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prefix-search-", dir=ROOT / "runtime") as directory:
        for language, profile in (("petta@1.0.6", None), ("petta@1.0.6", "extended"),
                                  ("he", "extended"), ("prime", None)):
            library = "lib/petta/list.metta" if language.startswith("petta") else "lib/list.metta"
            calls = []
            for values, bound in cases:
                payload = " ".join(map(str, values))
                calls.append(f"!(list:lower-bound-number ({payload}) {bound})")
                if profile == "extended" or language == "prime":
                    calls.append(f"!(list:lower-bound-number [{payload}] {bound})")
            observed_expected = (expected if profile != "extended" and language != "prime"
                                 else [value for value in expected for _ in range(2)])
            path = Path(directory) / "controls.metta"
            path.write_text(f'!(import! &self "{ROOT / library}")\n' + "\n".join(calls) + "\n")
            for reference in (False, True):
                env = dict(os.environ)
                env["CETTA_PREPARED_PURE_CODE_REFERENCE"] = "1" if reference else "0"
                command = [str(binary), "--lang", language]
                if profile:
                    command += ["--profile", profile]
                run = subprocess.run(command + [str(path)], cwd=ROOT, env=env,
                                     capture_output=True, text=True, timeout=90)
                assert run.returncode == 0, (language, reference, run.stderr, run.stdout[-2000:])
                lines = run.stdout.splitlines()
                observations = []
                for line in lines:
                    if line in ("true", "[()]", "()"):
                        continue
                    match = re.fullmatch(r"\[?(-?\d+)\]?", line)
                    assert match, (language, reference, line)
                    observations.append(int(match[1]))
                assert observations == observed_expected, (language, reference, observations)
                checks += len(observations)
    print(f"PrefixSearch {checks} 0")


if __name__ == "__main__":
    main()
