#!/usr/bin/env python3
"""Compare every clause alignment's images with the checked LP enumerator."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
args = parser.parse_args()
binary = str(args.binary.resolve())
corpus = json.loads((Path(__file__).resolve().parents[1] /
                    'fixtures/pattern_clause_model.json').read_text())
assert corpus['schema'] == 1 and len(corpus['cases']) == 240
assert {row['policy'] for row in corpus['cases']} == {'set', 'multiset'}


def alpha(text):
    variables = {}
    return re.sub(r'\$[\w-]+(?:#\d+)?',
        lambda m: variables.setdefault(m[0], f'$v{len(variables)}'), text)


def canonical_bag(text):
    # PeTTa's collapse freshens otherwise-unbound variables per answer.
    # Compare every complete image as one scope, retaining all its aliases,
    # the exact surrounding frontier, order and multiplicity.
    result = ''
    while (start := text.find('(image ')) >= 0:
        depth = 0
        for end in range(start, len(text)):
            depth += (text[end] == '(') - (text[end] == ')')
            if not depth:
                result += text[:start] + alpha(text[start:end + 1])
                text = text[end + 1:]
                break
        else:
            raise AssertionError(('unterminated image', text))
    return result + text


checks = hits = 0
for language in ('he', 'petta'):
    for reference in (False, True):
        environment = dict(os.environ,
            CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
            CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
        for index, row in enumerate(corpus['cases']):
            source = ('!(import! &self pat:clause)\n'
                f"!(collapse (let $w (pat:subsume {row['policy']} "
                f"{row['source']} {row['target']}) (pat:apply $w {row['template']})))")
            result = subprocess.run([binary, '--lang', language, '--profile',
                'extended', '-e', source], env=environment, capture_output=True,
                text=True, timeout=40)
            bag = '(' + ' '.join(row['answers']) + ')'
            expected = f'[()]\n[{bag}]\n' if language == 'he' else f'true\n{bag}\n'
            assert (result.returncode, canonical_bag(result.stdout), result.stderr) == (0, canonical_bag(expected), ''), (
                language, reference, index, source, result.returncode,
                result.stdout, result.stderr, expected)
            checks += 1
            hits += len(row['answers'])

print(f'PASS: {checks} clause/model comparisons and {hits} ordered complete witness observations')
