#!/usr/bin/env python3
"""Process probes, errno names, fixed-point text and the wall clock agree with Python's.

system:signal-probe is compared with os.kill(pid, 0) for the caller, process
groups, a process of another user, a reaped child and the ends of pid_t; IDs
outside pid_t must be refused. system:errno-value is compared with
getattr(errno, name) for every name the engine's table carries.
str:format-fixed is compared with '%.*f' over random bit patterns, integers,
special values and ties, at digit counts from 0 to 40 and a few large ones.
system:wall-time must read within a second of time.time().
"""
import argparse
import errno
import json
import os
import random
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(binary, work, lines, check_count=True):
    program = work / "program.metta"
    program.write_text("!(import! &self (library system))\n!(import! &self (library str))\n"
                       "!(import! &self (library json))\n"
                       + "".join("!(println! (RESULT %s))\n" % line for line in lines), encoding="utf-8")
    result = subprocess.run([str(binary), "--lang", "petta", str(program)], capture_output=True,
                            text=True, timeout=600)
    if result.returncode:
        raise SystemExit("engine exited %d: %s" % (result.returncode, result.stderr[-2000:]))
    results = [line[len("(RESULT "):-1] for line in result.stdout.split("\n") if line.startswith("(RESULT ")]
    if check_count and len(results) != len(lines):
        raise SystemExit("expected %d results, read %d" % (len(lines), len(results)))
    return results


def probe_cases(binary, work, failures):
    child = subprocess.Popen([sys.executable, "-c", "pass"])
    child.wait()
    pids = [os.getpid(), 0, -1, 1, child.pid, 2 ** 31 - 1, -2 ** 31, -os.getpgid(0)]
    outside = [2 ** 31, -2 ** 31 - 1]
    results = run(binary, work, ["(system:signal-probe %d)" % pid for pid in pids + outside])
    for pid, text in zip(pids, results):
        try:
            os.kill(pid, 0)
            want = "true"
        except OSError as error:
            want = "(system:os-error %d %s)" % (error.errno, json.dumps(os.strerror(error.errno)))
        if text != want:
            failures.append(("signal-probe %d" % pid, text, want))
    for pid, text in zip(outside, results[len(pids):]):
        if text == "true" or text.startswith("(system:os-error"):
            failures.append(("signal-probe refuses %d" % pid, text, "a refusal"))
    return len(pids) + len(outside)


def errno_cases(binary, work, failures):
    names = re.findall(r'\{"(E[A-Z0-9_]*)", ', (ROOT / "src/errno_names.h").read_text())
    names += ["ENOSUCHERRNO", "EACCESS", "eacces"]
    results = run(binary, work, ['(system:errno-value "%s")' % name for name in names])
    for name, text in zip(names, results):
        value = getattr(errno, name, None)
        want = "()" if value is None else str(value)
        if text != want:
            failures.append(("errno-value %s" % name, text, want))
    return len(names)


def number_literal(value):
    """An exact reader literal: integers as written, floats with seventeen
    significant digits in scientific form, the specials through Python's JSON."""
    if isinstance(value, int):
        return str(value)
    if value != value:
        return '(json:parse-python "NaN")'
    if abs(value) == float("inf"):
        return '(json:parse-python "%sInfinity")' % ("-" if value < 0 else "")
    return "%.16e" % value


def fixed_cases(binary, work, failures, count, seed):
    rng = random.Random(seed)
    values = [0.0, -0.0, 0.5, 1.5, 2.5, -2.5, 2.675, 1e-7, 5e-7, 1.0000005, 0.125, 1e22, 1e23,
              5e-324, -5e-324, 2.2250738585072014e-308, 1.7976931348623157e308,
              float("inf"), float("-inf"), float("nan"), 1761470400.123456789]
    # PeTTa arithmetic refuses to make NaN or an infinity, so they come from
    # Python's JSON reader, which a build without the JSON library lacks.
    special = run(binary, work, ['(json:parse-python "1.5")'], check_count=False)
    if special != ["1.5"]:
        values = [value for value in values if value == value and abs(value) != float("inf")]
        print("os-primitives: json:parse-python is unavailable; NaN and infinities skipped")
    while len(values) < count:
        bits = struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64)))[0]
        if bits == bits:
            values.append(bits if rng.random() < 0.5 else rng.uniform(-1e6, 1e6))
    integers = [0, 1, -1, 2 ** 53, 2 ** 53 + 1, 2 ** 63 - 1, -2 ** 63 + 1, 10 ** 15 + 3]
    cases = [(value, rng.randrange(41)) for value in values]
    cases += [(value, digits) for value in values[:40] for digits in (0, 6, 17)]
    cases += [(value, digits) for value in integers for digits in (0, 6, 30)]
    cases += [(0.1, 100), (1e-300, 340), (2.0 ** -1074, 1100)]
    results = run(binary, work, ["(str:format-fixed %s %d)" % (number_literal(value), digits)
                                 for value, digits in cases])
    for (value, digits), text in zip(cases, results):
        want = json.dumps("%.*f" % (digits, value))
        if text != want:
            failures.append(("format-fixed %r %d" % (value, digits), text[:120], want[:120]))
    # Digit counts whose shortest text cannot fit a string (sign, one integer
    # digit, the point and every fraction digit) are refused before any digit
    # is made; the largest positive count that may still fit is not run.
    limit = 0x7FFFFFFF
    impossible = [(1.5, limit - 1), (1.5, limit), (-1.5, limit - 2), (-0.0, limit - 2)]
    started = time.monotonic()
    refused = run(binary, work, ["(str:format-fixed %s %d)" % (number_literal(value), digits)
                                for value, digits in impossible])
    elapsed = time.monotonic() - started
    for (value, digits), text in zip(impossible, refused):
        if "fits a string" not in text:
            failures.append(("format-fixed refusal %r %d" % (value, digits), text[:120], "a refusal"))
    if elapsed > 30.0:
        failures.append(("format-fixed refusal time", "%.1fs" % elapsed, "an immediate refusal"))
    return len(cases) + len(impossible)


def wall_cases(binary, work, failures):
    before = time.time()
    text = run(binary, work, ["(system:wall-time)"])[0]
    after = time.time()
    try:
        value = float(text)
    except ValueError:
        failures.append(("wall-time", text, "a float"))
        return 1
    if not before - 1.0 <= value <= after + 1.0:
        failures.append(("wall-time", value, (before, after)))
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--count", type=int, default=3000)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        binary = args.binary.resolve()
        checked = probe_cases(binary, work, failures)
        checked += errno_cases(binary, work, failures)
        checked += fixed_cases(binary, work, failures, args.count, args.seed)
        checked += wall_cases(binary, work, failures)
    for what, got, want in failures[:12]:
        print("MISMATCH %s\n  native %r\n  python %r" % (what, got, want))
    print("os-primitives: %d cases, %d mismatches" % (checked, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
