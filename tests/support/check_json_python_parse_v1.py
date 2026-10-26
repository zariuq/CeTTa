#!/usr/bin/env python3
"""json:parse-python reads what Python's json.loads reads, and fails as it does.

Documents come from json.dumps of generated values (NaN and the infinities,
huge integers, repeated keys, escapes, lone and paired surrogates) with random
separators and whitespace, from character mutations and truncations of those,
and from fixed edge cases. Each is written as UTF-8 with surrogatepass, read
back raw by the engine and parsed. A value must equal json.loads's, compared
through json.dumps of both; an error must have the same exception class and
str(). Python texts also round-trip through str:python-codepoints and
str:from-python-codepoints.

The oracle is CPython 3.12, whose json the engine follows. Under another
Python the error messages are not compared (3.13 words trailing-comma errors
differently), only values and exception classes, and the summary says so.
"""
import argparse
import json
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FULL = sys.version_info[:2] == (3, 12)

POOL = list(' \t\n\r{}[],:"\\/-+.0123456789eEnulltruefalseNaNInfinityabcuU') + \
    ["\\u", "\\ud800", "\\udbff", "\\udc00", "\\udfff", "\\ud83d\\ude00", "\\u0000", "\\uZZZZ",
     "\x00", "\x01", "\x1f", "\x7f", "é", "日", "\U0001f642", "\ud800", "\udc00", "﻿",
     "NaN", "Infinity", "-Infinity", "null", "true", "false", '\\"', "\\\\", "\\x"]

EDGES = ["", " ", "﻿[]", "﻿", "-", "-0", "01", "1.", "1e", "1e+", "1E-", ".5", "+1",
         "1.5e-400", "1e999", "-1e999", "0e0", "-0.0", "1" * 4300, "1" * 4301, "-" + "9" * 4300,
         "-" + "9" * 4301, "1" * 4301 + ".0", "nul", "tru", "fals", "NaN", "NaNx", "Infinit",
         "-Infinit", "-I", "-Infinity", "Infinity", "[NaN, Infinity, -Infinity]", "[", "{", "[1",
         '{"a"', '{"a":', '{"a":1', '{"a" 1}', '{1: 2}', '[1 2]', '[,]', '[1,,2]', '{"a":1,}',
         '"abc', '"\\', '"\\u12', '"\\u1234', '"\\u12345"', '"\\ud800', '"\\ud800\\', '"\\ud800\\u',
         '"\\ud800\\udc0"', '"\\ud800\\uzzzz"', '"\\ud800\\u0041"', '"\\udc00\\ud800"',
         '"a\x00b"', '"\\/"', "[1]  ", "  [1]", "[1] [2]", '{"a":1,"a":2,"b":3,"a":4}',
         '{"__proto__": 1}', "[" * 50 + "]" * 50, '"\\ud800"', '"\ud800"', "\"\\t\\b\\f\\n\\r\"",
         "1\n2", "[\n1,\n2,\n]", '{"a":\n}']


def random_text(rng):
    pool = ["a", "Z", " ", '"', "\\", "\n", "\t", "\x00", "\x1f", "\x7f", "é", "日", "\U0001f642",
            "\ud800", "\udbff", "\udc00", "\udfff", " "]
    return "".join(rng.choice(pool) for _ in range(rng.randint(0, 10)))


def random_value(rng, depth=0):
    kind = rng.random()
    if depth > 3 or kind < 0.55:
        scalar = rng.random()
        if scalar < 0.25:
            return rng.choice([0.0, -0.0, 1.5, 1e-7, 2.5e300, 5e-324, float("nan"), float("inf"),
                               float("-inf"), rng.random() * 10 ** rng.randint(-20, 20)])
        if scalar < 0.5:
            return rng.randint(-10 ** rng.randint(1, 40), 10 ** rng.randint(1, 40))
        if scalar < 0.85:
            return random_text(rng)
        return rng.choice([None, True, False])
    if kind < 0.75:
        return [random_value(rng, depth + 1) for _ in range(rng.randint(0, 4))]
    keys = [random_text(rng) for _ in range(rng.randint(0, 4))]
    return {key: random_value(rng, depth + 1) for key in keys}


def document(rng, value):
    item, key = rng.choice([(", ", ": "), (",", ":"), (" ,\n", " :\t")])
    text = json.dumps(value, ensure_ascii=rng.random() < 0.5, separators=(item, key))
    if rng.random() < 0.2:
        # Repeated keys, which json.dumps never writes.
        text = re.sub(r'\{"', lambda m: '{"k": 1, "k": [2], "', text, count=1)
    if rng.random() < 0.3:
        text = rng.choice(["", " ", "\n", "\t \r\n"]) + text + rng.choice(["", " ", "\n"])
    return text


def mutate(rng, text):
    chars = list(text)
    for _ in range(rng.randint(1, 3)):
        action = rng.random()
        at = rng.randint(0, len(chars))
        if action < 0.35 and chars:
            del chars[min(at, len(chars) - 1)]
        elif action < 0.7:
            chars.insert(at, rng.choice(POOL))
        elif chars:
            chars[min(at, len(chars) - 1)] = rng.choice(POOL)
    text = "".join(chars)
    if rng.random() < 0.2:
        text = text[:rng.randint(0, len(text))]
    return text


def python_result(text):
    try:
        return ("OK", json.dumps(json.loads(text)))
    except json.JSONDecodeError as error:
        return ("ERR", "JSONDecodeError", str(error))[:3 if FULL else 2]
    except ValueError as error:
        return ("ERR", "ValueError", str(error))[:3 if FULL else 2]


def unescape(literal):
    return re.sub(r"\\(.)", r"\1", literal)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True)
    parser.add_argument("--count", type=int, default=12000)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    texts = list(EDGES)
    for _ in range(args.count):
        valid = document(rng, random_value(rng))
        texts.append(valid)
        texts.append(mutate(rng, valid))
    texts += ["".join(rng.choice(POOL) for _ in range(rng.randint(1, 12))) for _ in range(args.count // 2)]
    lists = [[rng.choice([0, 10, 65, 0x7f, 0xe9, 0x65e5, 0xd800, 0xdbff, 0xdc00, 0xdfff, 0xfffd,
                          0x1f642, 0x10ffff]) for _ in range(rng.randint(0, 8))] for _ in range(500)]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        lines = ["!(import! &self (library fs))", "!(import! &self (library json))",
                 "!(import! &self (library str))"]
        for index, text in enumerate(texts):
            path = root / ("%d.json" % index)
            path.write_bytes(text.encode("utf-8", "surrogatepass"))
            lines.append(
                '!(case (fs:read-text-result "%s" 0 "raw") ((("ok" $text) '
                "(case (json:parse-python $text) "
                "(((JsonPythonErrorV1 $class $message $msg $pos $line $col) (println! (ERR $class $message))) "
                "((JsonPythonErrorV1 $class $message) (println! (ERR $class $message))) "
                "((JsonFailureV1 $phase $kind $call $detail) (println! (FAIL $kind $detail))) "
                "($value (println! (OK (json:stringify-python $value))))))) "
                "($other (println! (UNREADABLE)))))" % path)
        for codes in lists:
            lines.append("!(let $s (str:from-python-codepoints (%s)) (println! (TEXT (str:python-codepoints $s) "
                         "(str:byte-length $s))))" % " ".join(map(str, codes)))
        program = root / "check.metta"
        program.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run([args.cetta, "--lang", "petta", str(program)],
                                capture_output=True, timeout=1200)
    stdout = result.stdout.decode("utf-8", errors="replace")
    if result.returncode:
        raise SystemExit("engine exited %d: %s" % (result.returncode,
                                                  result.stderr.decode("utf-8", "replace")[-2000:]))
    outcomes = [line for line in stdout.split("\n") if re.match(r"\((OK|ERR|FAIL|UNREADABLE)\b", line)]
    texts_out = [line for line in stdout.split("\n") if line.startswith("(TEXT ")]
    if len(outcomes) != len(texts) or len(texts_out) != len(lists):
        raise SystemExit("expected %d and %d results, read %d and %d"
                         % (len(texts), len(lists), len(outcomes), len(texts_out)))
    failures = []
    for text, line in zip(texts, outcomes):
        want = python_result(text)
        ok = re.fullmatch(r'\(OK "(.*)"\)', line)
        err = re.fullmatch(r'\(ERR (\w+) "(.*)"\)', line)
        got = ("OK", unescape(ok.group(1))) if ok else \
              ("ERR", err.group(1), unescape(err.group(2)))[:len(want)] if err else ("OTHER", line)
        if got != want:
            failures.append((text, got, want))
    for codes, line in zip(lists, texts_out):
        want = "(TEXT (%s) %d)" % (" ".join(map(str, codes)),
                                   len("".join(map(chr, codes)).encode("utf-8", "surrogatepass")))
        if line != want:
            failures.append((codes, line, want))
    for text, got, want in failures[:12]:
        print("MISMATCH %r\n  engine %r\n  python %r" % (text, got, want))
    oracle = "" if FULL else " (Python %d.%d: error classes compared, not messages)" % sys.version_info[:2]
    print("json-python-parse: %d documents and %d code-point texts, %d mismatches%s"
          % (len(texts), len(lists), len(failures), oracle))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
