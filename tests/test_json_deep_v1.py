#!/usr/bin/env python3
"""Deep JSON through the public library in all three evaluation languages."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="json-deep-", dir=Path(binary).parent) as directory:
        source = Path(directory) / "deep.metta"
        for opening, closing in (("[", "]"), ('{"x":', "}")):
            for language in ("he", "petta", "prime"):
                depth = 100000
                text = opening * depth + "true" + closing * depth
                literal = json.dumps(text)
                source.write_text(
                    "!(import! &self json)\n"
                    f"!(== (json:stringify (json:parse {literal})) {literal})\n"
                    f"!(== (json-stringify (json-parse {literal})) {literal})\n"
                    f"!(== (json-stringify (json:to-legacy (json:parse {literal}))) {literal})\n"
                )
                run = subprocess.run([binary, "--lang", language, str(source)],
                                     capture_output=True, text=True)
                expected = ["true"] * 4 if language == "petta" else ["[()]", "[True]", "[True]", "[True]"]
                if run.returncode or run.stdout.splitlines() != expected:
                    raise AssertionError((language, opening, run.returncode,
                                          run.stdout[:500], run.stderr[-500:]))
                checks += 3
    print(f"JsonDeepSummary {checks} 0")


if __name__ == "__main__":
    main()
