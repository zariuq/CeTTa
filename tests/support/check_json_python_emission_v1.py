#!/usr/bin/env python3
"""json:stringify-python writes exactly what Python's json.dumps writes.

Generated values cover finite floats from random bit patterns, every power of
two, decimal fractions, integers of both signs, strings with quotes,
backslashes, control characters, DEL, Basic Multilingual Plane and astral
scalars, and nested arrays and objects. Each value is emitted by the engine and
compared byte for byte with json.dumps under default arguments.
"""
import argparse
import json
import math
import random
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CONTROL_ESCAPES = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\t": "\\t", "\r": "\\r"}


def string_literal(text):
    return '"' + "".join(CONTROL_ESCAPES.get(c, c) for c in text) + '"'


def float_literal(value):
    return "%.16e" % value


def term(value):
    if value is None:
        return "JsonNull"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return float_literal(value)
    if isinstance(value, str):
        return string_literal(value)
    if isinstance(value, list):
        return "(JsonArray (" + " ".join(term(item) for item in value) + "))"
    pairs = " ".join("(JsonPair %s %s)" % (string_literal(key), term(item))
                     for key, item in value.items())
    return "(JsonObject (" + pairs + "))"


def random_float(rng):
    while True:
        choice = rng.random()
        if choice < 0.4:
            value = struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64)))[0]
        elif choice < 0.7:
            value = rng.random() * 10.0 ** rng.randint(-12, 22)
        elif choice < 0.85:
            value = round(rng.uniform(-1000, 1000), rng.randint(0, 6))
        else:
            value = rng.choice([0.1, 1e-05, 2.5e-07, 1e16, 1e+22, 1e23, 5e-324, 0.0001,
                                1.7976931348623157e308, 123456789012345680.0, 2e16 + 8,
                                1234567890123456.0, 100.0, 0.5, 9.5, 0.0026])
            value = -value if rng.random() < 0.5 else value
        if math.isfinite(value) and value != 0.0:
            return value


TEXT_POOL = (list("abcXYZ019 ~!{}[],:") + ['"', "\\", "\n", "\t", "\r", "\x01", "\x1f", "\x7f",
             "é", " ", " ", "日", "﻿", "\U0001f642", "\U0001d11e",
             "\U0010fffd"])


def random_text(rng):
    return "".join(rng.choice(TEXT_POOL) for _ in range(rng.randint(0, 12)))


def random_value(rng, depth=0):
    kind = rng.random()
    if depth > 3 or kind < 0.55:
        scalar = rng.random()
        if scalar < 0.45:
            return random_float(rng)
        if scalar < 0.65:
            return rng.randint(-10 ** rng.randint(1, 25), 10 ** rng.randint(1, 25))
        if scalar < 0.9:
            return random_text(rng)
        return rng.choice([None, True, False])
    if kind < 0.8:
        return [random_value(rng, depth + 1) for _ in range(rng.randint(0, 5))]
    return {random_text(rng): random_value(rng, depth + 1) for _ in range(rng.randint(0, 5))}


def emitted(cetta, terms):
    lines = ["!(import! &self (library json))"]
    lines += ["!(println! (json:stringify-python %s))" % expression for expression in terms]
    with tempfile.TemporaryDirectory() as directory:
        program = Path(directory) / "emit.metta"
        program.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run([cetta, "--lang", "petta", str(program)],
                                capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise SystemExit("engine exited %d: %s" % (result.returncode, result.stderr[-2000:]))
    printed = [line for line in result.stdout.splitlines() if line.startswith('"')]
    return [re.sub(r"\\(.)", r"\1", line[1:-1]) for line in printed]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True)
    parser.add_argument("--count", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=20261025)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    values = [2.0 ** k for k in range(-1074, 1024)]
    values += [-value for value in values]
    values += [random_value(rng) for _ in range(args.count)]
    values += [{"streaks": {"日\U0001f642": {"blocked": "待", "streak": 2}},
                "log_offset": 0, "mood": {"Cn": 1e-05}, "last_human_wake": "abc"},
               [0.1, 1e-05, 2.5e-07, 1.0, 1e+16, 0.0001, 1.2345678901234567], [], {}, ""]
    # PeTTa arithmetic refuses to overflow, but a parsed exponent overflow
    # projects to infinity; json.dumps spells it Infinity.
    overflow = "[1e999, -1e999, 1.5]"
    terms = [term(value) for value in values] + ['(json:parse "%s")' % overflow]
    expected = [json.dumps(value) for value in values] + [json.dumps(json.loads(overflow))]
    values.append(overflow)
    actual = emitted(args.cetta, terms)
    if len(actual) != len(values):
        raise SystemExit("expected %d results, read %d" % (len(values), len(actual)))
    failures = [(value, got, want) for value, got, want in zip(values, actual, expected)
                if got != want]
    for value, got, expected in failures[:10]:
        print("MISMATCH %r\n  engine %s\n  python %s" % (value, got, expected))
    print("json-python-emission: %d values, %d mismatches" % (len(values), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
