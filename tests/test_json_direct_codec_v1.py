#!/usr/bin/env python3
"""The direct legacy codec against the canonical-conversion reference.

Legacy stringify, lookup and unique-key parsing run directly by default; with
CETTA_JSON_CODEC=reference they convert through canonical values as before.
Every observable answer must agree, in all three languages, except a lookup
whose object has a malformed member it does not select: the direct codec no
longer converts that member, so the lookup answers instead of failing.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def literal(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


TEXTS = [
    {"a": 1, "b": [True, False, None], "c": {"d": "x"}},
    {"plain": "plain", "quote": 'q"b\\s', "controls": "\b\f\n\r\t", "low": "\u0000\u0001\u001f\u007f"},
    {"unicode": "é€😀  ", "slash": "/", "ключ": "значение"},
    [0, -0.0, 1.5e10, -1.25e-3, 10 ** 30, "1e400"],
    {"deep": [[[[[[[[[[{"x": [[[[["y"]]]]]}]]]]]]]]]]},
    {"empty_object": {}, "empty_array": [], "nested": {"e": {}}},
    {"long": ("λ mixed ascii and \"quotes\" \n " * 4000)},
]
RAW = [
    '{"a":1,"a":2}',
    '{"a":1,"\\u0061":2}',
    '{"x":{"y":1,"y":2}}',
    '[1,2,',
    '{"content":[{"type":"text","text":"hi"}],"usage":{"input_tokens":3}}',
    '{"n":123456789012345678901234567890,"f":-0.0,"e":1E+2}',
]
CONSTRUCTED = [
    '(JsonString hello)',
    '(JsonNumber "12.5")',
    '(JsonNumber "01")',
    '(JsonNumber 12)',
    '(JsonBool True)', '(JsonBool False)', '(JsonBool true)', '(JsonBool false)', '(JsonBool maybe)',
    '(JsonString 1)',
    '(JsonObject ((JsonPair 1 JsonNull)))',
    '(JsonObject notalist)',
    '(JsonPair "k" JsonNull)',
    '(JsonArray ((JsonNumber "01") (JsonString 5)))',
    '(JsonArray ((JsonNumber "01") (JsonNumber "02")))',
    '(JsonObject ((JsonPair "λ🙂" (JsonString "ok\\\\n"))))',
    '(JsonArray ())',
]


HEADER = "!(import! &self json)\n!(import! &self llm)\n"


def cases():
    lines = []
    for value in TEXTS:
        text = literal(json.dumps(value))
        lines.append(f"!(json-stringify (json-parse {text}))")
        if isinstance(value, dict):
            for key in list(value)[:3] + ["missing"]:
                lines.append(f"!(json-object-get (json-parse {text}) {literal(key)})")
    for text in RAW:
        lines.append(f"!(json-stringify (json-parse {literal(text)}))")
        lines.append(f"!(__cetta_lib_json_parse {literal(text)} JsonUniqueKeysV1)")
        lines.append(f"!(llm:v1:parse {literal(text)})")
        lines.append(f"!(json-object-get (json-parse {literal(text)}) \"a\")")
    for value in CONSTRUCTED:
        lines.append(f"!(json-stringify {value})")
        lines.append(f"!(json-object-get (JsonObject ((JsonPair \"k\" {value}))) \"k\")")
    lines.append('!(json-object-get (JsonArray ()) "k")')
    return lines


RELAXED = '!(json-object-get (JsonObject ((JsonPair "a" (JsonString "ok")) (JsonPair "b" (JsonString 5)))) "a")\n'


def run(binary, source, language, reference):
    env = dict(os.environ)
    env.pop("CETTA_JSON_CODEC", None)
    if reference:
        env["CETTA_JSON_CODEC"] = "reference"
    result = subprocess.run([binary, "--lang", language, str(source)],
                            capture_output=True, text=True, env=env, timeout=600)
    if result.returncode:
        raise AssertionError((language, reference, result.returncode, result.stderr[-1500:]))
    return result.stdout


def main():
    binary = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="json-direct-", dir=Path(binary).parent) as directory:
        source = Path(directory) / "codec.metta"
        relaxed = Path(directory) / "relaxed.metta"
        relaxed.write_text("!(import! &self json)\n" + RELAXED)
        for language in ("he", "petta", "prime"):
            # One case per run: an error result ends an HE program.
            for index, case in enumerate(cases()):
                source.write_text(HEADER + case + "\n")
                direct = run(binary, source, language, False)
                reference = run(binary, source, language, True)
                if direct != reference:
                    raise AssertionError((language, index, case[:200], direct[-400:], reference[-400:]))
                if not direct.strip():
                    raise AssertionError((language, index, case[:200], "no answer"))
                checks += 1
            answer = run(binary, relaxed, language, False)
            if 'JsonString "ok"' not in answer or "Error" in answer:
                raise AssertionError((language, "relaxed lookup", answer[-400:]))
            if "Error" not in run(binary, relaxed, language, True):
                raise AssertionError((language, "reference lookup should still fail"))
            checks += 2
    print(f"JsonDirectCodecSummary {checks} 0")


if __name__ == "__main__":
    main()
