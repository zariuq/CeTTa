#!/usr/bin/env python3
"""Effectful templates observe all matches selected at eq:match entry.

The first template retracts the alias. That must not remove the second
already selected occurrence, or keep the alias live for the next read.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


SOURCE = """\
(=e Cold Warm)
(Row Cold First)
(Row Cold Second)
!(collapse (eq:match &self (Row Warm $value)
    (if (== $value First)
        (let $_ (remove-atom &self (=e Cold Warm)) $value)
        $value)))
!(collapse (eq:match &self (Row Warm $value) $value))
!(size-atom (collapse (match &self (Row Cold $value) $value)))
!(eq:same &self Cold Warm)
"""

ORDERED = """\
!(bind! &q (new-space queue))
!(add-atom &q (=e Cold Warm))
!(add-atom &q (Row Cold First))
!(add-atom &q (Row Cold Second))
!(collapse
  (eq:match &q (Row Warm $value)
    (if (== $value First)
      (let $_ (remove-atom &q (Row Cold Second))
        (let $_ (add-atom &q (Row Cold Third)) $value))
      $value)))
!(collapse (eq:match &q (Row Warm $value) $value))
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cetta", type=Path)
    args = parser.parse_args()
    fixtures = {
        "alias-retract": (SOURCE, {
            "he": ["[(First Second)]", "[()]", "[2]", "[False]"],
            "petta": ["(First Second)", "()", "2", "false"],
        }),
        "queue-update": (ORDERED, {
            "he": ["[()]"] * 4 + ["[(First Second)]", "[(First Third)]"],
            "petta": ["true"] * 3 + ["(First Second)", "(First Third)"],
        }),
    }
    failures = []
    with tempfile.TemporaryDirectory(prefix="cetta-equational-cut-") as folder:
        for name, (source, expected) in fixtures.items():
            fixture = Path(folder) / (name + ".metta")
            fixture.write_text(source, encoding="utf-8")
            for dialect, want in expected.items():
                run = subprocess.run(
                    [str(args.cetta.resolve()), "--lang", dialect, "--profile",
                     "extended", str(fixture)],
                    text=True, capture_output=True, timeout=10, check=True,
                )
                got = [line.strip() for line in run.stdout.splitlines() if line.strip()]
                if got != want:
                    failures.append(
                        f"{name}/{dialect}: expected {want!r}, got {got!r}\n{run.stderr}"
                    )
    if failures:
        raise SystemExit("\n".join(failures))
    print("PASS: both dialects select once; effects run after selection")


if __name__ == "__main__":
    main()
