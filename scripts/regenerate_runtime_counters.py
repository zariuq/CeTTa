#!/usr/bin/env python3
"""Regenerate counter declarations, or compare them without changing source."""

import argparse
from pathlib import Path
import subprocess


EXPORTS = (
    ("runtime_counters.export", "cetta_runtime_counters.generated.h"),
    ("runtime_counter_names.export", "cetta_runtime_counter_names.generated.inc"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cesta", default="cesta", help="Cesta executable")
    parser.add_argument("--check", action="store_true", help="compare generated files without replacing them")
    parser.add_argument("--output-dir", type=Path, help="directory to retain check outputs")
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    generated = project / "src/generated"
    output = (args.output_dir or project / "build/runtime-counters-check") if args.check else generated
    if args.check:
        protected = {(generated / name).resolve() for _, name in EXPORTS}
        if any((output / name).resolve() in protected for _, name in EXPORTS):
            parser.error("--check outputs must not replace the checked-in declarations")
    output.mkdir(parents=True, exist_ok=True)
    for spec, header in EXPORTS:
        target = output / header
        subprocess.run([args.cesta, "export-header", str(project / "lib" / spec), "-o", str(target)], check=True)
        # Keep the exported files terminated by exactly one newline.
        content = target.read_bytes().rstrip(b"\n") + b"\n"
        target.write_bytes(content)
        if args.check and content != (generated / header).read_bytes():
            raise SystemExit("generated counter declaration is stale: " + str(generated / header))
    print("counter declarations match" if args.check else "counter declarations regenerated")


if __name__ == "__main__":
    main()
