#!/usr/bin/env python3
"""Run the directional-matching clients without erasing answer multiplicity."""
import argparse
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
            assert actual == (0, wanted, ''), (language, reference, client, actual, wanted)
            checked += 1

# These normalization controls are part of the oracle: fresh spelling may
# change, but aliasing, distinct occurrences, revisions and bags must not.
assert observations('(pair $u $u)\n', 'petta') != observations('(pair $u $v)\n', 'petta')
assert observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 2 3)\n', 'petta') != \
       observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 2 4)\n', 'petta')
assert observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 2 3)\n', 'petta') != \
       observations('(pat:occurrence 1 2 3)\n(pat:occurrence 1 4 3)\n', 'petta')
assert observations('(a a)\n', 'petta') != observations('(a)\n', 'petta')
print(f'PASS: {checked} SKI, retrieval, Nil coverage, clause, PLN-selection and policy-form clients')
