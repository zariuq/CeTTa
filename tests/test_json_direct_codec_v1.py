#!/usr/bin/env python3
"""Compare the native encoder with the explicit canonical-conversion oracle.

Also exercise lists built by evaluation, whose PeTTa representation differs
from a parsed tuple. These are the object/array builders used by the LLM client.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from gslt2parse_schema_v1 import StringLiteral
from test_lbnf_values_v1 import output_values


def literal(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


TEXTS = [
    {"a": 1, "b": [True, False, None], "c": {"d": "x"}},
    {"quote": 'q"b\\s', "controls": "\b\f\n\r\t", "low": "\u0000\u0001\u001f\u007f"},
    {"unicode": "é€😀\u2028\u2029", "slash": "/", "ключ": "значение"},
    [0, -0.0, 1.5e10, -1.25e-3, 10 ** 30, "1e400"],
    {"deep": [[[[[[[[[[{"x": [[[[["y"]]]]]}]]]]]]]]]]},
    {"empty_object": {}, "empty_array": [], "nested": {"e": {}}},
    {"long": ("λ mixed ascii and \"quotes\" \n " * 1000)},
]


def run(binary, source, language, expression):
    source.write_text("!(import! &self json)\n!(import! &self str)\n!(str:sha256 (" + expression + "))\n")
    result = subprocess.run([binary, "--lang", language, str(source)],
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise AssertionError((language, result.returncode, result.stderr[-1500:]))
    answers = output_values(result.stdout)
    if len(answers) != 1 or not isinstance(answers[0], StringLiteral):
        raise AssertionError((language, expression, answers))
    return answers[0].text


def main():
    binary = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="json-direct-", dir=ROOT / "runtime") as directory:
        source = Path(directory) / "codec.metta"
        for language in ("he", "petta", "prime"):
            for wire in [json.dumps(value) for value in TEXTS] + [
                    '{"a":1,"a":2}', '{"a":1,"\\u0061":2}',
                    '{"x":{"y":1,"y":2}}',
                    '{"n":123456789012345678901234567890,"f":-0.0,"e":1E+2}']:
                value = f"(json-parse {literal(wire)})"
                direct = run(binary, source, language, f"json-stringify {value}")
                reference = run(binary, source, language, f"json:stringify-reference {value}")
                assert direct == reference, (language, direct[:200], reference[:200])
                checks += 1

            # The PeTTa foreign bridge returns strings as symbols. The
            # explicit JsonString tag carries their text meaning in all lanes.
            for encoder in ("json-stringify", "json:stringify-reference"):
                expected = '{"focus":"input:fixture"}'
                answer = run(binary, source, language,
                             f'{encoder} (JsonObject ((JsonPair "focus" (JsonString input:fixture))))')
                assert answer == hashlib.sha256(expected.encode()).hexdigest(), (language, answer)
                checks += 1

            # Compatibility is carried by JsonString, never inferred for a
            # bare symbol or a numeric payload masquerading as text.
            for invalid in ('bare-symbol', '(JsonString 12)',
                            '(JsonObject ((JsonPair bare-key JsonNull)))'):
                answer = run(binary, source, language,
                    f'case (json:stringify {invalid}) '
                    '(((JsonFailureV1 $phase JsonMalformedValueV1 $call $why) "rejected") '
                    '($other "accepted"))')
                assert answer == hashlib.sha256(b"rejected").hexdigest(), (language, invalid)
                checks += 1

            # Evaluated cons lists, including nested arrays and duplicate keys.
            setup = '''(($tail (cons-atom JsonNull ()))
              ($items (cons-atom (JsonString "x") $tail))
              ($last (cons-atom (JsonPair "k" (JsonString "second")) ()))
              ($pairs (cons-atom (JsonPair "k" (JsonArray $items)) $last))
              ($object (JsonObject $pairs)))'''
            expected = '{"k":["x",null],"k":"second"}'
            for encoder in ("json-stringify", "json:stringify-reference"):
                answer = run(binary, source, language,
                             f"let* {setup} ({encoder} $object)")
                assert answer == hashlib.sha256(expected.encode()).hexdigest(), (language, encoder, answer)
                checks += 1
            for operation, expected_value in (
                    ('json-object-get $object "k"', '["x",null]'),
                    ('let (JsonFoundV1 $index $value) (json:get-last $object "k") $value', '"second"'),
                    ('let (JsonMatchesV1 $matches) (json:get-all $object "k") (JsonArray $matches)', '[["x",null],"second"]')):
                answer = run(binary, source, language,
                             f"let* {setup} (json-stringify ({operation}))")
                assert answer == hashlib.sha256(expected_value.encode()).hexdigest(), (language, operation, answer)
                checks += 1
    print(f"JsonDirectCodecSummary {checks} 0")


if __name__ == "__main__":
    main()
