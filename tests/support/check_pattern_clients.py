#!/usr/bin/env python3
"""Run the directional-matching clients without erasing answer multiplicity.
A collapsed answer list is a bag: its elements agree in any order."""
import argparse
import itertools
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
binary = str(parser.parse_args().binary.resolve())
root = Path(__file__).resolve().parents[2]
examples = root / 'examples' / 'pattern'
expected = json.loads((examples / 'expected.json').read_text())


def observations(output, language):
    spaces, revisions, occurrences = {}, {}, {}

    def occurrence(match):
        space, revision, row = match.groups()
        sid = spaces.setdefault(space, f's{len(spaces)}')
        rid = revisions.setdefault((space, revision), f'r{len(revisions)}')
        oid = occurrences.setdefault((space, revision, row), f'o{len(occurrences)}')
        return f'(pat:occurrence {sid} {rid} {oid})'

    result = []
    for line in output.splitlines():
        if language == 'he':
            assert line.startswith('[') and line.endswith(']'), line
            line = line[1:-1]
        variables = {}
        line = re.sub(r'\$[\w-]+(?:#\d+)?',
            lambda m: variables.setdefault(m[0], f'$v{len(variables)}'), line)
        result.append(re.sub(r'\(pat:occurrence (\d+) (\d+) (\d+)\)', occurrence, line))
    return result


def elements(text):
    """The top-level elements of a parenthesized list, or None."""
    text = text.strip()
    if not (text.startswith('(') and text.endswith(')')):
        return None
    inner, parts, depth, start, i = text[1:-1], [], 0, None, 0
    while i < len(inner):
        c = inner[i]
        if c == '"':
            if start is None:
                start = i
            i += 1
            while i < len(inner) and inner[i] != '"':
                i += 2 if inner[i] == '\\' else 1
        elif c == '(':
            if depth == 0:
                start = i
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                parts.append(inner[start:i + 1])
                start = None
        elif c.isspace():
            if depth == 0 and start is not None:
                parts.append(inner[start:i])
                start = None
        elif start is None:
            start = i
        i += 1
    if start is not None:
        parts.append(inner[start:])
    return parts


def agrees(output, language, wanted):
    """Whether the output observes what is wanted, each collapsed list as a
    bag.  Names are numbered in order of appearance, so the lines are fixed
    one at a time, each list in an order that agrees after the lines above."""
    lines = output.splitlines()
    if len(lines) != len(wanted):
        return False
    for index, line in enumerate(lines):
        if observations('\n'.join(lines[:index + 1]), language)[index] == wanted[index]:
            continue
        bracketed = language == 'he' and line.startswith('[') and line.endswith(']')
        parts = elements(line[1:-1] if bracketed else line)
        if not parts or len(parts) > 7:
            return False
        for order in itertools.permutations(parts):
            candidate = '(' + ' '.join(order) + ')'
            trial = lines[:index] + ['[' + candidate + ']' if bracketed else candidate]
            if observations('\n'.join(trial), language)[index] == wanted[index]:
                lines[index] = trial[index]
                break
        else:
            return False
    return True


checked = 0
for language in ('he', 'petta'):
    for reference in (False, True):
        env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
                   CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
        for client in ('ski', 'retrieval', 'nil_coverage', 'clause_retrieval', 'pln_selection', 'policy_forms'):
            name = f'{client}.{language}' if client in ('ski', 'pln_selection') else client
            wanted = expected[client][language]
            result = subprocess.run([binary, '--lang', language, '--profile', 'extended',
                str(examples / f'{name}.metta')], cwd=root, env=env,
                capture_output=True, text=True, timeout=60)
            actual = (result.returncode, observations(result.stdout, language), result.stderr)
            assert result.returncode == 0 and result.stderr == '' and \
                agrees(result.stdout, language, wanted), \
                (language, reference, client, actual, wanted)
            checked += 1

# These normalization controls are part of the oracle: fresh spelling may
# change, but aliasing, distinct occurrences, revisions and bags must not.
assert observations('(pair $u $u)\n', 'petta') != observations('(pair $u $v)\n', 'petta')
assert observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 2 3)\n', 'petta') != \
       observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 2 4)\n', 'petta')
assert observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 2 3)\n', 'petta') != \
       observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 4 3)\n', 'petta')
assert observations('(a a)\n', 'petta') != observations('(a)\n', 'petta')
assert agrees('((x $a) (y $b))\n', 'petta', ['((y $v0) (x $v1))'])
assert not agrees('((x a) (x a))\n', 'petta', ['((x a) (y a))'])
assert not agrees('((pair $a $a) (z))\n', 'petta', ['((z) (pair $v0 $v1))'])
protocol = root / 'tests/petta/pattern_declared_protocol.metta'
protocol_expected = protocol.with_suffix('.expected').read_text().splitlines()
for machine in ('0', '1'):
    result = subprocess.run(
        [binary, '--lang', 'petta', '--profile', 'extended', str(protocol)],
        cwd=root, env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine),
        capture_output=True, text=True, timeout=30)
    if result.returncode or result.stderr or not agrees(result.stdout, 'petta', protocol_expected):
        raise AssertionError(('declared pattern protocol', machine, result.returncode,
                              result.stdout, result.stderr))
    checked += 1
print(f'PASS: {checked} SKI, retrieval, Nil coverage, clause, PLN-selection and policy-form clients')
