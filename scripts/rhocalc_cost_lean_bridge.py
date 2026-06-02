#!/usr/bin/env python3

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 6:
        print(
            "usage: rhocalc_cost_lean_bridge.py "
            "<bin> <fixture.metta> <expected-file> <lean-file> <theorem>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    fixture = Path(sys.argv[2])
    expected_file = Path(sys.argv[3])
    lean_file = Path(sys.argv[4])
    theorem = sys.argv[5]

    proc = subprocess.run(
        [bin_path, "--profile", "he-extended", "--lang", "he", str(fixture)],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return proc.returncode

    try:
        expected_text = expected_file.read_text(encoding="utf-8").strip()
    except OSError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    actual_text = proc.stdout.strip()
    if actual_text != expected_text:
        print("cost-frontier output mismatch", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        print(f"expected: {expected_text}", file=sys.stderr)
        print(f"actual: {actual_text}", file=sys.stderr)
        return 1

    try:
        lean_text = lean_file.read_text(encoding="utf-8")
    except OSError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if theorem not in lean_text:
        print("lean theorem name not found", file=sys.stderr)
        print(f"file: {lean_file}", file=sys.stderr)
        print(f"theorem: {theorem}", file=sys.stderr)
        return 1

    mettapedia_root = Path(
        os.environ.get(
            "METTAPEDIA_ROOT",
            str(Path.cwd().resolve().parent.parent / "lean-projects" / "mettapedia"),
        )
    )

    helper = Path(__file__).resolve().with_name("rhocalc_lean_microcheck.py")
    proc = subprocess.run(
        ["python3", str(helper), str(mettapedia_root), str(lean_file)],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return proc.returncode

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
