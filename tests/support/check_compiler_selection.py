#!/usr/bin/env python3
"""The caller's compiler must receive a real compile through the native driver."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = shutil.which("cc")
    if not compiler:
        raise SystemExit("a C compiler is required")
    with tempfile.TemporaryDirectory(prefix="compiler-selection-") as temporary:
        directory = Path(temporary)
        calls = directory / "calls.jsonl"
        drivers = []
        for name in ("caller-cc", "explicit-native-cc"):
            driver = directory / name
            driver.write_text(
                "#!" + sys.executable + "\n"
                "import json, subprocess, sys\n"
                f"with open({str(calls)!r}, 'a') as out:\n"
                f"    out.write(json.dumps([{name!r}, sys.argv[1:]]) + '\\n')\n"
                f"raise SystemExit(subprocess.call([{compiler!r}, *sys.argv[1:]]))\n")
            driver.chmod(0o755)
            drivers.append(str(driver))
        source, obj = directory / "probe.c", directory / "probe.o"
        source.write_text("int compiler_selection_probe(void) { return 17; }\n")
        target = directory / "probe.mk"
        target.write_text(".PHONY: compiler-selection-probe\n"
                          "compiler-selection-probe:\n\t$(CC) -c " +
                          shlex.quote(str(source)) + " -o " + shlex.quote(str(obj)) + "\n")
        cases = [
            ("environment CC", {"CC": drivers[0]}, [], "caller-cc"),
            ("explicit native overrides environment CC",
             {"CC": drivers[0], "CESTA_NATIVE_CC": drivers[1]}, [], "explicit-native-cc"),
            ("explicit native compiler", {"CESTA_NATIVE_CC": drivers[1]}, [], "explicit-native-cc"),
            ("command-line CC", {}, ["CC=" + drivers[0]], "caller-cc"),
        ]
        for name, settings, arguments, wanted in cases:
            env = dict(os.environ)
            for key in ("CC", "CESTA_NATIVE_CC", "MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"):
                env.pop(key, None)
            env.update(settings)
            calls.unlink(missing_ok=True)
            obj.unlink(missing_ok=True)
            # Native-driver mode isolates compiler selection from bootstrapping
            # the Cesta tool. The selected driver must still compile C.
            result = subprocess.run(
                ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", str(target),
                 "compiler-selection-probe", "BUILD=core", "CESTA_BUILD=0", "CESTA=/bin/true",
                 "ENABLE_LIB_PROLOG=0", *arguments], cwd=ROOT, env=env,
                text=True, capture_output=True, timeout=30)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            assert obj.is_file() and obj.stat().st_size, name
            recorded = [json.loads(line) for line in calls.read_text().splitlines()]
            compiles = [row for row in recorded if "-c" in row[1] and str(source) in row[1]]
            assert len(compiles) == 1 and compiles[0][0] == wanted, (name, compiles)
            print("PASS: " + name)


if __name__ == "__main__":
    main()
