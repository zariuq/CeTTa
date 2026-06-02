#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: rhocalc_lean_microcheck.py <mettapedia-root> <lean-file>",
            file=sys.stderr,
        )
        return 2

    mettapedia_root = Path(sys.argv[1]).resolve()
    lean_file = Path(sys.argv[2]).resolve()

    if not mettapedia_root.is_dir():
        print(f"missing Mettapedia root: {mettapedia_root}", file=sys.stderr)
        return 2
    if not lean_file.is_file():
        print(f"missing Lean microcheck file: {lean_file}", file=sys.stderr)
        return 2

    build = subprocess.run(
        ["lake", "build", "Mettapedia.Languages.ProcessCalculi.RhoCalculus.OperationalBridge"],
        cwd=str(mettapedia_root),
        check=False,
        text=True,
        capture_output=True,
    )
    if build.returncode != 0:
        sys.stderr.write(build.stdout)
        sys.stderr.write(build.stderr)
        return build.returncode

    proc = subprocess.run(
        ["lake", "env", "lean", str(lean_file)],
        cwd=str(mettapedia_root),
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode == 0:
        return 0

    sys.stderr.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
