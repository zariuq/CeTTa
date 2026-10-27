#!/usr/bin/env python3
"""str:unicode-category reports Python 3.12's unicodedata.category.

Every run of code points sharing a category is probed at its first and last
code point as an integer, a random sample also as one-scalar texts, and the
argument errors are checked. The expected categories come from unicodedata
when it holds Unicode 15.0.0, the version the engine's table is generated
from. Under another Unicode version they come from the committed table, which
then checks the lookup but not the data, and the summary says so.
"""
import argparse
import random
import re
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = "15.0.0"


def table_categories():
    """The committed table as a lookup, for Pythons with other Unicode data."""
    text = (ROOT / "src/unicode_category15.h").read_text()
    names = re.findall(r'"(\w\w)"', re.search(r"names\[30\]\[3\] = \{(.*?)\};", text, re.S).group(1))
    starts = [int(value, 16) for value in
              re.findall(r"0x[0-9a-f]+", re.search(r"starts\[[^]]*\] = \{(.*?)\};", text, re.S).group(1))]
    values = [int(value) for value in
              re.findall(r"\d+", re.search(r"values\[[^]]*\] = \{(.*?)\};", text, re.S).group(1))]

    def category(code):
        low, high = 0, len(starts)
        while high - low > 1:
            middle = (low + high) // 2
            if starts[middle] <= code:
                low = middle
            else:
                high = middle
        return names[values[low]]
    return category


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    if unicodedata.unidata_version == VERSION:
        category, oracle = (lambda code: unicodedata.category(chr(code))), "unicodedata " + VERSION
    else:
        category = table_categories()
        oracle = "the committed table (this Python has Unicode %s)" % unicodedata.unidata_version
    codes = []
    previous = None
    for code in range(0x110000):
        current = category(code)
        if current != previous:
            if code:
                codes.append(code - 1)
            codes.append(code)
            previous = current
    codes.append(0x10ffff)
    codes += [rng.randrange(0x110000) for _ in range(3000)]
    scalars = [code for code in (rng.randrange(0x110000) for _ in range(3000))
               if not 0xd800 <= code <= 0xdfff][:2000] + [0, 10, 0x85, 0x378, 0x1fae8, 0x10ffff]
    lines = ["!(import! &self (library str))"]
    lines += ["!(println! (RESULT (str:unicode-category %d)))" % code for code in codes]
    lines += ["!(println! (RESULT (str:unicode-category (str:from-codepoints (%d)))))" % code
              for code in scalars]
    refused = ["-1", "1114112", '"ab"', '""']
    lines += ["!(println! (RESULT (str:unicode-category %s)))" % value for value in refused]
    lines.append("!(println! (RESULT (str:unicode-version)))")
    expected = ['"%s"' % category(code) for code in codes + scalars]
    with tempfile.TemporaryDirectory() as directory:
        program = Path(directory) / "categories.metta"
        program.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run([args.cetta, "--lang", "petta", str(program)],
                                capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise SystemExit("engine exited %d: %s" % (result.returncode, result.stderr[-2000:]))
    actual = [line[len("(RESULT "):-1] for line in result.stdout.split("\n")
              if line.startswith("(RESULT ")]
    if len(actual) != len(expected) + len(refused) + 1:
        raise SystemExit("expected %d results, read %d" % (len(expected) + len(refused) + 1, len(actual)))
    failures = [(code, got, want) for code, got, want in zip(codes + scalars, actual, expected)
                if got != want]
    failures += [(value, got, "an error") for value, got in zip(refused, actual[len(expected):])
                 if not got.startswith("(Error (str:unicode-category ")]
    if actual[-1] != '"%s"' % VERSION:
        failures.append(("version", actual[-1], VERSION))
    for code, got, want in failures[:10]:
        print("MISMATCH %r\n  engine %s\n  expected %s" % (code, got, want))
    print("unicode-category: %d lookups against %s, %d mismatches"
          % (len(actual), oracle, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
