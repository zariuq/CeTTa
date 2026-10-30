#!/usr/bin/env python3
"""The value worker and CLI select the same nested-import policy."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, encode
from value_worker_client import Worker


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("worker", type=Path)
    args = parser.parse_args()
    checks = 0
    with tempfile.TemporaryDirectory(prefix="worker-imports-") as folder:
        directory = Path(folder)
        nested = directory / "nested"
        nested.mkdir()
        (nested / "leaf.metta").write_text("(= (worker-imports:leaf $value) (+ $value 1))\n")
        middle = nested / "middle.metta"
        middle.write_text('!(import! &self "./leaf.metta")\n')
        entry = directory / "entry.metta"
        entry.write_text('!(import! &self ' + json.dumps(str(middle)) + ')\n'
                         '(= (worker-imports:run $value) (worker-imports:leaf $value))\n')
        driver = directory / "cli.metta"
        driver.write_text('!(import! &self ' + json.dumps(str(entry)) + ')\n'
                          '!(println! (RESULT (worker-imports:run 41)))\n')
        for language in ("petta", "prime"):
            for mode in ("relative", "ancestor-walk"):
                worker = Worker(args.worker.resolve(), entry, "worker-imports:run",
                                language=language, import_mode=mode)
                try:
                    result = worker.call(41)
                    if result != 42:
                        raise AssertionError((language, mode, result))
                    checks += 1
                finally:
                    worker.close()
                cli = subprocess.run([str(args.binary.resolve()), "--lang", language,
                                      "--import-mode", mode, str(driver)],
                                     capture_output=True, text=True, timeout=60)
                lines = [line for line in cli.stdout.splitlines() if line.startswith("(RESULT ")]
                if cli.returncode or lines != ["(RESULT 42)"]:
                    raise AssertionError((language, mode, cli.returncode, cli.stdout, cli.stderr))
                checks += 1
        worker = Worker(args.worker.resolve(), entry, "worker-imports:run", language="petta")
        explicit = Worker(args.worker.resolve(), entry, "worker-imports:run",
                          language="petta", import_mode="upstream")
        try:
            # Corrected stable's default resolves at the source file. The
            # explicit working-directory compatibility override is separate.
            if worker.call(41) != 42:
                raise AssertionError("corrected stable default is not source-relative")
            if encode(explicit.call(41)) != encode(E("worker-imports:leaf", 41)):
                raise AssertionError("explicit working-directory override changed")
            checks += 2
        finally:
            worker.close(); explicit.close()
        default_cli = subprocess.run([str(args.binary.resolve()), "--lang", "petta", str(driver)],
                                     capture_output=True, text=True, timeout=60)
        if default_cli.returncode or [line for line in default_cli.stdout.splitlines()
                                     if line.startswith("(RESULT ")] != ["(RESULT 42)"]:
            raise AssertionError(("default CLI import", default_cli.returncode,
                                  default_cli.stdout, default_cli.stderr))
        checks += 1
        # Only ancestor-walk can find a leaf above the importing module.
        deeper = nested / "deeper"
        deeper.mkdir()
        above = deeper / "middle.metta"
        above.write_text('!(import! &self "./leaf.metta")\n')
        entry.write_text('!(import! &self ' + json.dumps(str(above)) + ')\n'
                         '(= (worker-imports:run $value) (worker-imports:leaf $value))\n')
        cases = (("petta", "relative", E("worker-imports:leaf", 41),
                  "(RESULT (worker-imports:leaf 41))"),
                 ("petta", "ancestor-walk", 42, "(RESULT 42)"),
                 ("prime", "ancestor-walk", 42, "(RESULT 42)"))
        for language, mode, expected, printed in cases:
            worker = Worker(args.worker.resolve(), entry, "worker-imports:run",
                            language=language, import_mode=mode)
            try:
                if encode(worker.call(41)) != encode(expected):
                    raise AssertionError((language, mode, "ancestor scope changed"))
                checks += 1
            finally:
                worker.close()
            cli = subprocess.run([str(args.binary.resolve()), "--lang", language,
                                  "--import-mode", mode, str(driver)],
                                 capture_output=True, text=True, timeout=60)
            lines = [line for line in cli.stdout.splitlines() if line.startswith("(RESULT ")]
            if cli.returncode or lines != [printed]:
                raise AssertionError((language, mode, cli.returncode, cli.stdout, cli.stderr))
            checks += 1
        try:
            Worker(args.worker.resolve(), entry, "worker-imports:run", import_mode="unknown")
        except ValueError:
            checks += 1
        else:
            raise AssertionError("invalid client import policy was accepted")
        invalid = subprocess.run([str(args.worker.resolve()), "--import-mode", "unknown", str(entry),
                                  "worker-imports:run"], capture_output=True, timeout=60)
        if invalid.returncode == 0:
            raise AssertionError("invalid worker import policy was accepted")
        checks += 1
    print("value worker imports: %d checks passed" % checks)


if __name__ == "__main__":
    main()
