#!/usr/bin/env python3
"""Check CLI errors while preserving source effects and uninterpreted data."""
import errno
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[2]
    environment = dict(os.environ, LC_ALL="C")
    checks = 0
    with tempfile.TemporaryDirectory(prefix="cli-diagnostics-", dir=root / "runtime") as work:
        directory = Path(work)
        malformed = "; source origin\n(Alpha λ\n"
        (directory / "malformed.metta").write_text(malformed, encoding="utf-8")
        (directory / "-literal.metta").write_text("!(+ 20 22)\n", encoding="utf-8")

        def run(*arguments):
            return subprocess.run([str(binary), *arguments], cwd=directory,
                                  env=environment, text=True, capture_output=True,
                                  timeout=30)

        for option in ("--not-a-cetta-option", "--lang=he", "-Z"):
            result = run(option)
            assert result.returncode == 2 and "unknown option" in result.stderr, result
            assert "could not read" not in result.stderr, result
            checks += 1
        for language in ("petta", "he"):
            for arguments in (("-e", malformed), ("malformed.metta",)):
                result = run("--lang", language, *arguments)
                assert result.returncode != 0, result
                assert re.search(r"line 3, column 1", result.stderr), result
                if len(arguments) == 1:
                    assert "malformed.metta" in result.stderr, result
                checks += 1
            result = run("--lang", language, "missing-input.metta")
            assert result.returncode != 0 and "missing-input.metta" in result.stderr, result
            assert os.strerror(errno.ENOENT) in result.stderr, result
            checks += 1
            result = run("--lang", language, "--", "-literal.metta")
            assert result.returncode == 0 and "42" in result.stdout, result
            checks += 1
            result = run("--lang", language, "-e", "!(unregistered-diagnostic-control 42)")
            assert result.returncode == 0 and "unregistered-diagnostic-control" in result.stdout, result
            checks += 1
        result = run("--lang", "he", "-e", "!(println! before-error)\n(\n")
        assert result.returncode != 0 and "before-error" in result.stdout, result
        assert "line 3, column 1" in result.stderr, result
        checks += 1
    print(f"CliDiagnostics {checks} controls pass; file origins, reader positions, option errors and inert data")


if __name__ == "__main__":
    main()
