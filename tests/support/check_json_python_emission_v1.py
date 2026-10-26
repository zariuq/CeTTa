#!/usr/bin/env python3
"""json:stringify-python writes exactly what Python's json.dumps writes.

Generated values cover finite floats from random bit patterns, every power of
two, decimal fractions, integers of both signs, strings with quotes,
backslashes, control characters, DEL, Basic Multilingual Plane and astral
scalars, and nested arrays and objects. Each value is emitted by the engine and
compared byte for byte with json.dumps under default arguments, then again
under random ensure_ascii, sort_keys, separators and indent options.

Integers past Python's 4300-digit int-to-text limit must fail as json.dumps
fails, with its ValueError text and the prefix a streaming json.dump has
written by then.
"""
import argparse
import io
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


def string_expression(text):
    # The reader has no NUL escape, so text holding NUL is built from its
    # codepoints.
    if "\x00" not in text:
        return string_literal(text)
    return "(str:from-codepoints (%s))" % " ".join(str(ord(c)) for c in text)


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
        # Render oversized integers too; json.dumps keeps the default limit.
        limit = sys.get_int_max_str_digits()
        sys.set_int_max_str_digits(0)
        try:
            return str(value)
        finally:
            sys.set_int_max_str_digits(limit)
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


def with_options(value, options):
    """json:stringify-python-with under json.dumps keyword options. Text options
    are bound by let, since text holding NUL is an expression."""
    bindings, parts = [], []
    for name in ("ensure_ascii", "sort_keys"):
        if name in options:
            parts.append("(%s %s)" % (name.replace("_", "-"), options[name]))
    if options.get("separators") is not None:
        bindings += [("$item", options["separators"][0]), ("$key", options["separators"][1])]
        parts.append("(separators $item $key)")
    indent = options.get("indent")
    if isinstance(indent, int):
        parts.append("(indent %d)" % indent)
    elif indent is not None:
        bindings.append(("$indent", indent))
        parts.append("(indent $indent)")
    expression = "(json:stringify-python-with %s (%s))" % (term(value), " ".join(parts))
    for name, text in reversed(bindings):
        expression = "(let %s %s %s)" % (name, string_expression(text), expression)
    return expression


def unescaped(text):
    return re.sub(r"\\(.)", r"\1", text, flags=re.S)


def printed_items(output):
    """Printed strings, unescaped, and (LIMIT ...) and (UNEXPECTED ...) forms.

    println! escapes only backslashes and quotes, so indented text spans lines
    and the output is read as a sequence of forms, not of lines."""
    items, position = [], 0
    def string_end(start):
        end = start + 1
        while output[end] != '"':
            end += 2 if output[end] == "\\" else 1
        return end
    while position < len(output):
        if output[position] == '"':
            end = string_end(position)
            items.append(unescaped(output[position + 1:end]))
            position = end + 1
        elif output[position] == "(":
            depth, end = 0, position
            while True:
                if output[end] == '"':
                    end = string_end(end)
                elif output[end] == "(":
                    depth += 1
                elif output[end] == ")":
                    depth -= 1
                    if not depth:
                        break
                end += 1
            if output.startswith(("(LIMIT ", "(UNEXPECTED "), position):
                items.append(output[position:end + 1])
            position = end + 1
        else:
            position += 1
    return items


def emitted(cetta, terms):
    lines = ["!(import! &self (library json))", "!(import! &self (library str))"]
    lines += ["!(println! %s)" % expression for expression in terms]
    with tempfile.TemporaryDirectory() as directory:
        program = Path(directory) / "emit.metta"
        program.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run([cetta, "--lang", "petta", str(program)],
                                capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise SystemExit("engine exited %d: %s" % (result.returncode, result.stderr[-2000:]))
    items = printed_items(result.stdout)
    return [item for item in items if not item.startswith(("(LIMIT ", "(UNEXPECTED "))] + \
        [item for item in items if item.startswith(("(LIMIT ", "(UNEXPECTED "))]


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
    terms = ["(json:stringify-python %s)" % term(value) for value in values]
    terms.append('(json:stringify-python (json:parse "%s"))' % overflow)
    expected = [json.dumps(value) for value in values] + [json.dumps(json.loads(overflow))]
    values = values + [overflow]
    # Each value again under random json.dumps options; an indent without
    # separators also checks the "," item separator json.dumps then uses.
    separators = [(", ", ": "), (",", ":"), (" ; ", " = "), ("\x00", "\x00\x00"), (",\x00", ":"), None]
    indents = [None, None, None, 0, 2, 4, -1, "\t", "", "\x00 x"]
    for value in values[:-1]:
        options = {"ensure_ascii": rng.random() < 0.5, "sort_keys": rng.random() < 0.5,
                   "separators": rng.choice(separators), "indent": rng.choice(indents)}
        terms.append(with_options(value, options))
        expected.append(json.dumps(value, **options))
        values.append(value)
    # Past int()'s digit limit: json.dumps raises; json.dump has written a prefix.
    huge = 10 ** 4300
    limit_values = [huge, -huge, [huge], [1, huge], [[1], huge], [1, [huge]], [{"a": 1}, huge],
                    {"a": 1, "seq": huge}, {"x": [huge]}, {"x": [2.5, {"y": -huge}], "z": "w"},
                    {"b": 1, "a": huge}, [10 ** 4299, huge], {"a": {"b": huge}}, [[], [huge, 1]]]
    limit_options = [{"ensure_ascii": False, "sort_keys": False, "separators": (", ", ": ")},
                     {"ensure_ascii": True, "sort_keys": True, "separators": (",", ":")},
                     {"ensure_ascii": False, "sort_keys": True, "indent": 2},
                     {"ensure_ascii": True, "separators": (" ; ", " = "), "indent": "\t"}]
    limit_expected = []
    for value in limit_values:
        for options in limit_options:
            try:
                json.dumps(value, **options)
                raise SystemExit("json.dumps accepted an oversized integer")
            except ValueError as error:
                stream = io.StringIO()
                try:
                    json.dump(value, stream, **options)
                except ValueError:
                    pass
                limit_expected.append((str(error), stream.getvalue()))
            terms.append("(case %s (((JsonPythonErrorV1 ValueError $m $p) "
                         "(LIMIT $m $p)) ($other (UNEXPECTED $other))))" % with_options(value, options))
    actual = emitted(args.cetta, terms)
    limit_actual = actual[len(values):]
    actual = actual[:len(values)]
    if len(actual) != len(values) or len(limit_actual) != len(limit_expected):
        raise SystemExit("expected %d and %d results, read %d and %d"
                         % (len(values), len(limit_expected), len(actual), len(limit_actual)))
    failures = [(value, got, want) for value, got, want in zip(values, actual, expected)
                if got != want]
    for line, (message, prefix) in zip(limit_actual, limit_expected):
        texts = re.findall(r'"((?:[^"\\]|\\.)*)"', line, re.S)
        got = tuple(unescaped(text) for text in texts) \
            if line.startswith("(LIMIT ") and len(texts) == 2 else line
        if got != (message, prefix):
            failures.append(("oversized integer", got[:2] if isinstance(got, tuple) else got,
                             (message, prefix[:80])))
    for value, got, expected in failures[:10]:
        print("MISMATCH %r\n  engine %s\n  python %s" % (value, got, expected))
    print("json-python-emission: %d values and %d oversized integers, %d mismatches"
          % (len(values), len(limit_expected), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
