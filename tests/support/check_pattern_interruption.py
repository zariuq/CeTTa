#!/usr/bin/env python3
"""Interrupted native queries cannot publish absence or execute a fallback."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
binary = str(parser.parse_args().binary.resolve())
checks = 0
rows = ''.join(f'(p {n})\n' for n in range(96))
open_rows = ''.join(f'(p $stored{n})\n' for n in range(96))
queries = (
    (rows, '(pat:query &self match% (p $x))'),
    (rows, '(pat:match% &self (p $x) $x)'),
    (open_rows, '(pat:%match &self (p a) $stored $stored)'),
)

for facts, query in queries:
    source = ('!(import! &self pat)\n' + facts +
              f'!(trace! ENTERED (case (collapse {query}) '
              '(($hits (trace! COMPLETED (size-atom $hits))))))')
    for language in ('he', 'petta'):
        for reference in (False, True):
            for limit in (16, 32, 64, 0):
                environment = dict(os.environ,
                    CETTA_PETTA_MACHINE_TRANSITION_LIMIT=str(limit),
                    CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
                    CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
                result = subprocess.run([binary, '--lang', language, '--profile',
                    'extended', '-e', source], env=environment, capture_output=True,
                    text=True, timeout=40)
                diagnostic = 'error: observation incomplete: fuel-exhausted\n'
                if limit:
                    expected = (1, '' if language == 'he' else 'ENTERED\n',
                                ('ENTERED\n' if language == 'he' else '') + diagnostic)
                elif language == 'he':
                    expected = (0, '[()]\n[96]\n', 'ENTERED\nCOMPLETED\n')
                else:
                    expected = (0, 'ENTERED\nCOMPLETED\ntrue\n96\n', '')
                actual = result.returncode, result.stdout, result.stderr
                assert actual == expected, (language, reference, limit, query, actual, expected)
                checks += 1

print(f'PASS: {checks} incomplete-frontier and completed-query controls')
