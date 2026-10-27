#!/usr/bin/env python3
"""lib/toml against the TOML 1.1 cases of the official toml-test suite, and against Python's tomllib.

The corpus is the TOML 1.1 list of toml-test v2.2.0 (github.com/toml-lang/
toml-test, MIT), in tests/support/fixtures/toml-test; TOML 1.1 is a superset
of 1.0.  Each document is read raw from its file inside the engine.

- A valid document must give the suite's expected value, with dates and times
  in Python's isoformat. Where Python's tomllib also reads it (tomllib reads
  TOML 1.0), the result must equal tomllib's, keys in document order.
- A document holding an infinity or NaN must be refused as unrepresentable.
- An invalid document must be refused.
- A few documents also run through toml:to-json and toml:parse in PeTTa, HE
  and Prime.
"""
import argparse
import datetime
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests/support/fixtures/toml-test"
IMPORTS = {
    "petta": "!(import! &self (library toml))\n!(import! &self (library fs))\n",
    "he": "!(import! &self toml)\n!(import! &self fs)\n",
    "prime": "!(import! &self toml)\n!(import! &self fs)\n",
}
UNREPRESENTABLE = object()


def isoformat(kind, text):
    """A toml-test date or time as Python's isoformat writes it."""
    text = re.sub(r"(\.\d{6})\d+", r"\1", text)
    if kind == "time-local":
        return datetime.time.fromisoformat(text).isoformat()
    if kind == "date-local":
        return datetime.date.fromisoformat(text).isoformat()
    return datetime.datetime.fromisoformat(text.replace("z", "Z").replace("t", "T").replace(" ", "T")).isoformat()


def expected(value):
    """A toml-test tagged JSON value as lib/toml writes it."""
    if isinstance(value, list):
        items = [expected(item) for item in value]
        return UNREPRESENTABLE if UNREPRESENTABLE in items else items
    if set(value) == {"type", "value"} and isinstance(value["type"], str):
        kind, text = value["type"], value["value"]
        if kind == "string":
            return text
        if kind == "integer":
            return int(text)
        if kind == "bool":
            return text == "true"
        if kind == "float":
            number = float(text)
            return number if math.isfinite(number) else UNREPRESENTABLE
        return isoformat(kind, text)
    fields = {key: expected(item) for key, item in value.items()}
    return UNREPRESENTABLE if UNREPRESENTABLE in fields.values() else fields


def plain(value):
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items()}
    if isinstance(value, list):
        return [plain(item) for item in value]
    if isinstance(value, (datetime.datetime, datetime.date, datetime.time)):
        return value.isoformat()
    return value


def literal(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def run(binary, dialect, expressions):
    with tempfile.TemporaryDirectory() as directory:
        program = Path(directory) / "toml.metta"
        program.write_text(IMPORTS[dialect] + "".join("!(println! (RESULT %s))\n" % e for e in expressions),
                           encoding="utf-8")
        result = subprocess.run([str(binary.resolve()), "--lang", dialect, str(program)], capture_output=True,
                                encoding="utf-8", errors="replace", timeout=1200)
    if result.returncode:
        raise SystemExit("%s: engine exited %d: %s" % (dialect, result.returncode, result.stderr[-2000:]))
    lines = [line[len("(RESULT "):-1] for line in result.stdout.split("\n") if line.startswith("(RESULT ")]
    if len(lines) != len(expressions):
        raise SystemExit("%s: expected %d results, read %d" % (dialect, len(expressions), len(lines)))
    return lines


def read_document(path):
    return ('(case (fs:read-text-result %s 0 "raw") (((ok $text) (toml:to-json $text)) '
            '(("ok" $text) (toml:to-json $text)) ($other (read-failed $other))))' % literal(str(path)))


def same(left, right, ordered):
    """Equal values of equal types; with `ordered`, object keys in the same
    order too.  toml-test's expected files do not keep document order;
    tomllib does."""
    if isinstance(left, dict) and isinstance(right, dict):
        return (sorted(left) == sorted(right) and (not ordered or list(left) == list(right))
                and all(same(left[k], right[k], ordered) for k in left))
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(same(a, b, ordered) for a, b in zip(left, right))
    return type(left) is type(right) and left == right


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    names = [line.strip() for line in (CORPUS / "files-toml-1.1.0").read_text().splitlines()
             if line.strip().endswith(".toml")]
    results = run(arguments.binary, "petta", [read_document(CORPUS / name) for name in names])
    failures, counts = [], {"valid": 0, "tomllib": 0, "invalid": 0, "unrepresentable": 0}
    for name, text in zip(names, results):
        refused = text.startswith("(toml:failed")
        if name.startswith("invalid/"):
            counts["invalid"] += 1
            if not refused:
                failures.append((name, text[:200], "a refusal"))
            continue
        want = expected(json.loads((CORPUS / name).with_suffix(".json").read_text()))
        if want is UNREPRESENTABLE:
            counts["unrepresentable"] += 1
            if not text.startswith("(toml:failed toml:unrepresentable"):
                failures.append((name, text[:200], "refused as unrepresentable"))
            continue
        counts["valid"] += 1
        try:
            got = json.loads(json.loads(text))
        except (ValueError, TypeError):
            failures.append((name, text[:200], "JSON text"))
            continue
        if not same(got, want, False):
            failures.append((name, json.dumps(got)[:200], json.dumps(want)[:200]))
            continue
        try:
            python = plain(tomllib.loads((CORPUS / name).read_text(encoding="utf-8")))
        except tomllib.TOMLDecodeError:
            continue
        counts["tomllib"] += 1
        if not same(got, python, True):
            failures.append((name + " (tomllib)", json.dumps(got)[:200], json.dumps(python)[:200]))

    sample = ['title = "TOML"\n[owner]\nname = "Tom"\ndob = 1979-05-27T07:32:00-08:00\n',
              'a = [1, 2.5, true]\n"quoted key" = { x = "é" }\n', "x = = 1\n"]
    for dialect in ("petta", "he", "prime"):
        got = run(arguments.binary, dialect, ["(toml:to-json %s)" % literal(document) for document in sample])
        for document, text in zip(sample, got):
            try:
                want = plain(tomllib.loads(document))
            except tomllib.TOMLDecodeError:
                if not text.startswith("(toml:failed toml:invalid"):
                    failures.append((dialect + " sample", text, "a refusal"))
                continue
            if not text.startswith('"') or not same(json.loads(json.loads(text)), want, True):
                failures.append((dialect + " sample", text, json.dumps(want)))
        parsed = run(arguments.binary, dialect, ['(toml:parse "a = 1")'])[0]
        if parsed != '(JsonObject ((JsonPair "a" 1)))':
            failures.append((dialect + " toml:parse", parsed, '(JsonObject ((JsonPair "a" 1)))'))

    for name, got, want in failures[:30]:
        print("FAIL %s:\n  got  %s\n  want %s" % (name, got, want))
    print("toml: %d valid (%d also against tomllib), %d invalid, %d unrepresentable documents, %d failures"
          % (counts["valid"], counts["tomllib"], counts["invalid"], counts["unrepresentable"], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
