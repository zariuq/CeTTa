#!/usr/bin/env python3
"""str:python-lower against CPython 3.12's str.lower, run with a Python 3.12.

- Every code point but U+03A3 (lone surrogates included) in one text, read
  raw from a file and written back raw, compared byte for byte.
- Generated texts around U+03A3, whose lowercase depends on its neighbors
  (Final_Sigma): cased and uncased letters, case-ignorable marks and
  punctuation, both small sigmas, U+0130 and surrogates. They are joined by
  newlines, which neither are cased nor are skipped, so each lowers as a
  text of its own.
- A sample of those texts in HE and Prime, and malformed bytes refused.
"""
import argparse
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unicodedata

VERSION = "15.0.0"
IMPORTS = {"petta": "!(import! &self (library str))\n!(import! &self (library fs))\n",
           "he": "!(import! &self str)\n!(import! &self fs)\n",
           "prime": "!(import! &self str)\n!(import! &self fs)\n"}
POOL = ([0x03A3, 0x03C3, 0x03C2, 0x41, 0x61, 0x20, 0x27, 0x2E, 0x3A, 0x31, 0x0130, 0x0049, 0x1F88, 0x1E9E, 0x02B0,
         0x00AD, 0x0345, 0x0300, 0x0301, 0x0307, 0x00B7, 0x2019, 0x05F4, 0x1D2C, 0x2160, 0x24B6, 0x10400, 0xD800,
         0xDC00, 0xE0001, 0x00B5, 0x01C5, 0x0391, 0x03A9])


def quoted(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def encoded(text):
    return text.encode("utf-8", "surrogatepass")


def lower_file(binary, dialect, source, target):
    program = Path(target).with_suffix(".metta")
    program.write_text(IMPORTS[dialect] + (
        "(= (lowered $text) (let $owner (fs:open-write %s (\"create\" \"truncate\") 384)"
        " (let $written (fs:write-all $owner (str:python-lower $text)) (let $closed (fs:close $owner) $written))))\n"
        "!(println! (RESULT (case (fs:read-text-result %s 0 \"raw\")"
        " (((ok $text) (lowered $text)) ((\"ok\" $text) (lowered $text)) ($other (read-failed $other))))))\n"
        % (quoted(str(target)), quoted(str(source)))), encoding="utf-8")
    result = subprocess.run([str(binary), "--lang", dialect, "--quiet", str(program)], capture_output=True,
                            encoding="utf-8", errors="replace", timeout=600)
    if result.returncode or "(RESULT (fs:written" not in result.stdout:
        raise SystemExit("%s: %s %s" % (dialect, result.stdout[-1500:], result.stderr[-1500:]))
    return Path(target).read_bytes()


def first_difference(got, want):
    got_text, want_text = got.decode("utf-8", "surrogatepass"), want.decode("utf-8", "surrogatepass")
    for index, (a, b) in enumerate(zip(got_text, want_text)):
        if a != b:
            return "at code point %d: got U+%04X, want U+%04X" % (index, ord(a), ord(b))
    return "lengths %d and %d" % (len(got_text), len(want_text))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    if unicodedata.unidata_version != VERSION:
        print("python-lower: SKIP, the oracle must be a Python 3.12 (Unicode %s); this one has Unicode %s"
              % (VERSION, unicodedata.unidata_version))
        return 0
    binary = arguments.binary.resolve()
    failures, checks = [], 0
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        every = "".join(chr(code) for code in range(0x110000) if code != 0x03A3)
        (work / "every.txt").write_bytes(encoded(every))
        got = lower_file(binary, "petta", work / "every.txt", work / "every-lower.txt")
        want = encoded(every.lower())
        checks += 1
        if got != want:
            failures.append("every code point: " + first_difference(got, want))

        generator = random.Random(2026)
        texts = ["".join(chr(generator.choice(POOL)) for _ in range(generator.randrange(1, 11))) for _ in range(30000)]
        texts += ["Σ", "ΟΣ", "ΟΣ.", "A'Σ", "AΣA", "AΣ'B", "İ", "ΣΣ",
                  "AͅΣ", "\ud800Σ", "A­Σ­", "ᾈΣ"]
        (work / "sigma.txt").write_bytes(encoded("\n".join(texts)))
        for dialect in ("petta", "he", "prime"):
            if dialect != "petta":
                texts_here = texts[:2000] + texts[-12:]
                (work / "sigma.txt").write_bytes(encoded("\n".join(texts_here)))
            else:
                texts_here = texts
            got = lower_file(binary, dialect, work / "sigma.txt", work / ("sigma-%s.txt" % dialect)).decode(
                "utf-8", "surrogatepass").split("\n")
            for text, lowered in zip(texts_here, got):
                checks += 1
                if lowered != text.lower():
                    failures.append("%s %r: got %r, want %r" % (dialect, text, lowered, text.lower()))
            if len(got) != len(texts_here):
                failures.append("%s: %d results for %d texts" % (dialect, len(got), len(texts_here)))

        program = work / "malformed.metta"
        program.write_text(IMPORTS["petta"] + '!(println! (RESULT (str:python-lower (str:byte-slice "é" 0 1))))\n',
                           encoding="utf-8")
        result = subprocess.run([str(binary), "--lang", "petta", "--quiet", str(program)], capture_output=True,
                                encoding="utf-8", errors="replace", timeout=60)
        checks += 1
        if "(StrMalformedUtf8V1 0)" not in result.stdout:
            failures.append("malformed bytes: " + result.stdout[-300:])
    for failure in failures[:20]:
        print("FAIL " + failure)
    print("python-lower: %d checks, %d failures" % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
