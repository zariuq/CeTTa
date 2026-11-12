"""Import Prime's native higher-order logic and its set theories in Prime, HE
and PeTTa.

Each dialect runs its own program against the same checker: Prime with the
importing space as the theory space, HE and PeTTa (extended profiles) with
explicit theory spaces.  Every program's output must equal its expected
output exactly. Mixed theory judgments also run with one and two workers;
both executions preserve the same theory-specific verdicts and multiplicities.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

RUNS = (
    ("prime", ["--lang", "prime"], "hol_import.prime"),
    ("he", ["--lang", "he", "--profile", "extended"], "hol_import.he"),
    ("petta", ["--lang", "petta", "--profile", "extended"], "hol_import.petta"),
    ("petta serial judgments",
     ["--lang", "petta", "--profile", "extended", "--num-threads", "1"],
     "hol_set_judgments.petta"),
    ("petta parallel judgments",
     ["--lang", "petta", "--profile", "extended", "--num-threads", "2"],
     "hol_set_judgments.petta"),
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    repository = Path(__file__).resolve().parents[1]
    parser.add_argument("--binary", type=Path, default=repository / "cetta")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    directory = repository / "tests" / "hol_import"
    failures = 0
    for dialect, options, stem in RUNS:
        program = directory / f"{stem}.metta"
        expected = directory.joinpath(f"{stem}.expected").read_text()
        result = subprocess.run([str(args.binary), *options, str(program)], cwd=repository,
                                capture_output=True, text=True, timeout=args.timeout)
        output = result.stdout + result.stderr
        if result.returncode == 0 and output.rstrip("\n") == expected.rstrip("\n"):
            print(f"PASS: import hol and set in {dialect}")
        else:
            failures += 1
            print(f"FAIL: import hol and set in {dialect} (exit {result.returncode})")
            print(output[-2000:])
    print(f"hol and set import: {len(RUNS) - failures} passed, {failures} failed")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
