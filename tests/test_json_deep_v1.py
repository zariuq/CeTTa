#!/usr/bin/env python3
"""Deep JSON through the public library in both evaluation languages."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    depth = 100000
    checks = 0
    with tempfile.TemporaryDirectory(prefix="json-deep-") as directory:
        source = Path(directory) / "deep.metta"
        for opening, closing in (("[", "]"), ('{"x":', "}")):
            text = opening * depth + "true" + closing * depth
            literal = json.dumps(text)
            source.write_text(
                "!(import! &self json)\n"
                f"!(== (json:stringify (json:parse {literal})) {literal})\n"
                f"!(== (json-stringify (json-parse {literal})) {literal})\n"
                f"!(== (json-stringify (json:to-legacy (json:parse {literal}))) {literal})\n"
            )
            for language in ("he", "petta"):
                run = subprocess.run([binary, "--lang", language, str(source)],
                                     capture_output=True, text=True)
                expected = ["[()]", "[True]", "[True]", "[True]"] if language == "he" else ["true"] * 4
                if run.returncode or run.stdout.splitlines() != expected:
                    raise AssertionError((language, opening, run.returncode,
                                          run.stdout[:500], run.stderr[-500:]))
                checks += 3
    print(f"JsonDeepSummary {checks} 0")


if __name__ == "__main__":
    main()
