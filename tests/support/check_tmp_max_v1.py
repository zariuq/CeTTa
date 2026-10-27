#!/usr/bin/env python3
"""system:tmp-max against os.TMP_MAX, with an optional pinned tempfile check."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

IMPORTS = {"petta": "!(import! &self (library system))\n", "he": "!(import! &self system)\n",
           "prime": "!(import! &self system)\n"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--tempfile", action="store_true",
                        help="also compare the selected interpreter's tempfile retry policy")
    arguments = parser.parse_args()
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        for dialect, imports in IMPORTS.items():
            program = Path(directory) / ("tmp-max-%s.metta" % dialect)
            program.write_text(imports + "!(println! (RESULT (system:tmp-max)))\n", encoding="utf-8")
            result = subprocess.run([str(arguments.binary.resolve()), "--lang", dialect, str(program)],
                                    capture_output=True, text=True, timeout=60)
            got = [line for line in result.stdout.splitlines() if line.startswith("(RESULT ")]
            expected = getattr(os,"TMP_MAX",10000)
            want = "(RESULT %d)" % expected
            if result.returncode or got != [want]:
                failures.append("%s: exit %d, %r, want %r" % (dialect, result.returncode, got, want))
            if arguments.tempfile and tempfile.TMP_MAX!=expected:
                failures.append("%s: this Python tempfile policy is %d, distinct from os.TMP_MAX %d" %
                                (dialect,tempfile.TMP_MAX,expected))
    for failure in failures:
        print("FAIL " + failure)
    print("tmp-max: %d cases, %d failures (os.TMP_MAX %d; tempfile policy %d)" %
          (len(IMPORTS), len(failures),getattr(os,"TMP_MAX",10000),tempfile.TMP_MAX))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
