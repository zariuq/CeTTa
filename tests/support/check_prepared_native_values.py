#!/usr/bin/env python3
"""Prepared native wrappers and direct calls observe the same PeTTa values."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    for reference in ("0", "1"):
        env = dict(os.environ, CETTA_PREPARED_PURE_REFERENCE=reference)
        result = subprocess.run([str(args.binary.resolve()), "--lang", "petta",
            str(root / "tests/petta/prepared_native_values.metta")], cwd=root,
            env=env, text=True, capture_output=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout.count("✅") == 10 and "❌" not in result.stdout, result.stdout
    print("PASS: 20 native value-boundary checks, including held code and single evaluation")


if __name__ == "__main__":
    main()
