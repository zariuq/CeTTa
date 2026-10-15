#!/usr/bin/env python3
"""Public tensor fixtures: immutable alternatives, dialects and exact retrieval."""
import difflib
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    binary = Path(os.environ.get("CETTA_BIN", ROOT / "cetta")).resolve()
    outputs = 0
    for dialect, name in (("petta", "tensor_native"), ("petta", "tensor_retrieval"), ("petta", "row_index"),
                          ("he", "tensor_native"), ("prime", "tensor_native")):
        source = ROOT / "tests" / dialect / f"{name}.metta"
        expected = source.with_suffix(".expected").read_text()
        result = subprocess.run([str(binary), "--lang", dialect, str(source)],
                                cwd=ROOT, text=True, capture_output=True, timeout=60)
        if result.returncode or result.stdout != expected:
            sys.stderr.write(result.stderr)
            sys.stderr.writelines(difflib.unified_diff(
                expected.splitlines(True), result.stdout.splitlines(True),
                fromfile=f"{dialect}/{name}.expected", tofile="actual"))
            return 1
        count = len(expected.splitlines())
        outputs += count
        print(f"PASS: {dialect}/{name} ({count} results)")
        if dialect == "petta" and name == "tensor_native":
            for threads in (1, 4):
                parallel = subprocess.run([str(binary), "--num-threads", str(threads),
                                           "--lang", dialect, str(source)],
                                          cwd=ROOT, text=True,
                                          capture_output=True, timeout=60)
                if parallel.returncode or parallel.stdout != expected:
                    sys.stderr.write(parallel.stderr + parallel.stdout)
                    return 1
                print(f"PASS: tensor alternatives and hyperpose ({threads} threads)")
    for dialect in ("petta", "he", "prime"):
        source = ROOT / "tests" / dialect / "tensor_screening.metta"
        expected = source.with_suffix(".expected").read_text()
        result = subprocess.run([str(binary), "--lang", dialect, str(source)],
                                cwd=ROOT, text=True, capture_output=True, timeout=60)
        if result.returncode or result.stdout != expected:
            sys.stderr.write(result.stderr)
            sys.stderr.writelines(difflib.unified_diff(expected.splitlines(True), result.stdout.splitlines(True),
                fromfile=f"{dialect}/tensor_screening.expected", tofile="actual"))
            return 1
        outputs += len(expected.splitlines())
        print(f"PASS: {dialect}/tensor_screening")
    print(f"PASS: native tensor and retrieval fixtures ({outputs} results)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
