#!/usr/bin/env python3
"""Regenerate the runtime word accessors; ordinary builds use the checked-in header."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "langdef/layouts/runtime_words.module"
HEADER = ROOT / "src/generated/cetta_layout_words.generated.h"
THEORY = "Frames"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cesta", default="cesta")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    work = ROOT / "runtime"
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="layout-regenerate-", dir=work) as directory:
        result = Path(directory) / "words.h"
        subprocess.run([args.cesta, "layout", "header", str(MODULE), THEORY, "-o", str(result)], check=True)
        generated = result.read_bytes()
    if args.check:
        if not HEADER.is_file() or HEADER.read_bytes() != generated:
            raise SystemExit("runtime word header differs; run regenerate-layout-words")
        print("runtime word header matches its layout declaration")
    else:
        HEADER.write_bytes(generated)


if __name__ == "__main__":
    main()
