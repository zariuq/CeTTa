#!/usr/bin/env python3
"""Native-scalar JSON: distinct encoder oracles and evaluated list carriers.

Derived from the constructed-sequence controls on the collection integration
branch, using the namespaced native-scalar API rather than scalar wrappers.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from gslt2parse_schema_v1 import StringLiteral
from test_lbnf_values_v1 import output_values


def literal(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def run(binary, source, language, expression):
    library = '(library json)' if language == 'petta' else 'json'
    source.write_text(f'!(import! &self {library})\n!(import! &self str)\n!(str:sha256 ({expression}))\n')
    result = subprocess.run([binary, '--lang', language, str(source)],
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
    fixtures = [
        {'a': 1, 'b': [True, False, None], 'c': {'d': 'x'}},
        {'quote': 'q"b\\s', 'controls': '\b\f\n\r\t', 'low': '\u0000\u0001\u001f\u007f'},
        {'unicode': 'é€😀\u2028\u2029', 'slash': '/', 'ключ': 'значение'},
        [0, -0.0, 1.5e10, -1.25e-3, 10**30, '1e400'],
        {'deep': [[{'x': [['y']]}]]},
        {'empty_object': {}, 'empty_array': [], 'nested': {'e': {}}},
        {'long': 'λ mixed ascii and "quotes" \n ' * 1000},
    ]
    with tempfile.TemporaryDirectory(prefix='json-direct-', dir=ROOT / 'runtime') as directory:
        source = Path(directory) / 'codec.metta'
        for language in ('he', 'petta', 'prime'):
            for wire in [json.dumps(value) for value in fixtures] + [
                '{"a":1,"a":2}', '{"a":1,"\\u0061":2}',
                '{"x":{"y":1,"y":2}}', '{"n":123456789012345678901234567890,"f":-0.0,"e":1E+2}',
            ]:
                value = f'(json:parse {literal(wire)})'
                direct = run(binary, source, language, f'json:stringify {value}')
                reference = run(binary, source, language, f'json:stringify-reference {value}')
                assert direct == reference, (language, direct, reference)
                checks += 1
            # Ordinary JSON has native scalar leaves. Legacy wrappers and
            # untagged symbol text must not acquire a second, implicit meaning.
            for invalid in ('bare-symbol', '(JsonString input:fixture)', '(JsonString 12)',
                            '(JsonString "native-but-wrapped")',
                            '(JsonObject ((JsonPair bare-key JsonNull)))'):
                for encoder in ('json:stringify', 'json:stringify-reference'):
                    answer = run(binary, source, language,
                        f'case ({encoder} {invalid}) '
                        '(((JsonFailureV1 $phase JsonMalformedValueV1 $call $why) "rejected") '
                        '($other "accepted"))')
                    assert answer == hashlib.sha256(b'rejected').hexdigest(), (language, encoder, invalid)
                    checks += 1
            setup = '''(($tail (cons-atom JsonNull ()))
              ($items (cons-atom "x" $tail))
              ($last (cons-atom (JsonPair "k" "second") ()))
              ($pairs (cons-atom (JsonPair "k" (JsonArray $items)) $last))
              ($value (JsonObject $pairs)))'''
            for encoder in ('json:stringify', 'json:stringify-reference'):
                answer = run(binary, source, language, f'let* {setup} ({encoder} $value)')
                assert answer == hashlib.sha256(b'{"k":["x",null],"k":"second"}').hexdigest(), (language, encoder, answer)
                checks += 1
            answer = run(binary, source, language,
                         f'let* {setup} (json:stringify (json:get $value "k"))')
            assert answer == hashlib.sha256(b'["x",null]').hexdigest(), (language, answer)
            checks += 1
            # Every public observer consumes the same evaluated member spine.
            for operation, expected in (
                ('let (JsonFoundV1 $index $found) (json:get-first $value "k") $found',
                 '["x",null]'),
                ('let (JsonFoundV1 $index $found) (json:get-last $value "k") $found',
                 '"second"'),
                ('let (JsonMatchesV1 $matches) (json:get-all $value "k") (JsonArray $matches)',
                 '[["x",null],"second"]'),
                ('json:get $value "missing"', 'null'),
            ):
                answer = run(binary, source, language,
                             f'let* {setup} (json:stringify ({operation}))')
                assert answer == hashlib.sha256(expected.encode()).hexdigest(), (language, operation, answer)
                checks += 1
            pretty = '{\n  "k": [\n    "x",\n    null\n  ],\n  "k": "second"\n}'
            answer = run(binary, source, language, f'let* {setup} (json:pretty $value)')
            assert answer == hashlib.sha256(pretty.encode()).hexdigest(), (language, answer)
            checks += 1
            for encoder in ('json:stringify', 'json:stringify-reference'):
                for invalid in (
                    'malformed-tail',
                    '(cons-atom (JsonPair bare-key 1) ())',
                ):
                    answer = run(binary, source, language,
                        f'let $members {invalid} (case ({encoder} (JsonObject $members)) '
                        '(((JsonFailureV1 $phase JsonMalformedValueV1 $call $why) "rejected") '
                        '($other "accepted")))')
                    assert answer == hashlib.sha256(b'rejected').hexdigest(), (language, encoder, invalid)
                    checks += 1
    print(f'JsonDirectCodec {checks} 0')


if __name__ == '__main__':
    main()
