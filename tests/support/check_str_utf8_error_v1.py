#!/usr/bin/env python3
"""str:utf8-error locates malformed UTF-8 exactly as Python's strict decoder.

Random byte strings, weighted toward lead and continuation bytes, are written
to files, read back raw by the engine and examined with str:utf8-error. Each
result must name the start, end, first byte and reason of Python's
UnicodeDecodeError, or report well-formed text where Python decodes it.
"""
import argparse
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REASONS = {"invalid start byte": "invalid-start-byte",
           "invalid continuation byte": "invalid-continuation-byte",
           "unexpected end of data": "unexpected-end-of-data"}


def expected(data):
    try:
        data.decode("utf-8")
        return "StrUtf8WellFormedV1"
    except UnicodeDecodeError as error:
        return "(StrUtf8ErrorV1 %d %d %d %s)" % (error.start, error.end, data[error.start],
                                                 REASONS[error.reason])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True)
    parser.add_argument("--count", type=int, default=3000)
    parser.add_argument("--seed", type=int, default=20261025)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    pool = list(range(0x20, 0x80, 5)) + list(range(0x80, 0x100))
    samples = [b"", b"plain", "é日🙂".encode(), b"\xed\xa0\x80", b"\xe0\x80\x80", b"\xf4\x90\x80\x80",
               b"\xc0\xaf", b"\xe1\x80", b"\xf0\x9f\x99", b";; generated\n\xff\n"]
    samples += [bytes(rng.choice(pool) for _ in range(rng.randint(1, 8))) for _ in range(args.count)]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        lines = ["!(import! &self (library fs))", "!(import! &self (library str))"]
        for index, data in enumerate(samples):
            path = root / ("%d.bin" % index)
            path.write_bytes(data)
            lines.append('!(case (fs:read-text-result "%s" 0 "raw") '
                         '((("ok" $text) (println! (RESULT (str:utf8-error $text)))) '
                         '($other (println! (RESULT unreadable)))))' % path)
        program = root / "check.metta"
        program.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run([args.cetta, "--lang", "petta", str(program)],
                                capture_output=True, timeout=600)
    stdout = result.stdout.decode("utf-8", errors="replace")
    stderr = result.stderr.decode("utf-8", errors="replace")
    if result.returncode:
        raise SystemExit("engine exited %d: %s" % (result.returncode, stderr[-2000:]))
    actual = [re.sub(r"^\(RESULT (.*)\)$", r"\1", line)
              for line in stdout.splitlines() if line.startswith("(RESULT ")]
    if len(actual) != len(samples):
        raise SystemExit("expected %d results, read %d" % (len(samples), len(actual)))
    failures = [(data, got, expected(data)) for data, got in zip(samples, actual)
                if got != expected(data)]
    for data, got, want in failures[:10]:
        print("MISMATCH %r\n  engine %s\n  python %s" % (data, got, want))
    print("str-utf8-error: %d byte strings, %d mismatches" % (len(samples), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
