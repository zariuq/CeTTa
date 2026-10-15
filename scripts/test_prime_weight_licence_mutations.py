#!/usr/bin/env python3
"""Compile separate licence-coverage omissions and check their actual answers."""
from __future__ import annotations

import argparse
from fractions import Fraction
from hashlib import sha256
import json
from pathlib import Path
import shlex
import subprocess
from types import SimpleNamespace

from test_prime_native_control import Gate, ROOT
from test_native_dialects import DialectGate
from test_prime_weights import (contribution, control, read, weight_licence_dependency_cases,
                               weight_observations, weight_order_record)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--compile-flags', required=True)
    parser.add_argument('--link-objects', required=True)
    parser.add_argument('--link-flags', required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=30)
    args = parser.parse_args()
    args.out_dir = args.out_dir.resolve()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    original = (ROOT / 'src/eval.c').read_text()
    admission = ('if (body && body->kind == ATOM_EXPR && body->expr.len == 4u &&\n'
                 '        atom_is_symbol_id(body->expr.elems[0], prime_need_symbols()->weight_where))')
    values = ('if (licence->body_position == licence->body_vars.len)')
    assert original.count(admission) == original.count(values) == 1, 'licence source extraction changed'
    variants = {
        'original': original,
        'omit-admission': original.replace(admission, admission.replace('if (body', 'if (false && body')),
        'omit-matched-values': original.replace(values, values.replace('if (licence->body_position', 'if (true || licence->body_position')),
    }
    objects = shlex.split(args.link_objects)
    receipt = {'source_sha256': sha256(original.encode()).hexdigest(),
               'script_sha256': sha256(Path(__file__).read_bytes()).hexdigest(),
               'case_source_sha256': sha256((ROOT/'scripts/test_prime_weights.py').read_bytes()).hexdigest(),
               'objects': {path: sha256((ROOT/path).read_bytes()).hexdigest() for path in objects},
               'variants': []}
    try:
        for name, text in variants.items():
            source = args.out_dir / (name + '.c')
            obj = args.out_dir / (name + '.o')
            binary = args.out_dir / ('cetta-' + name)
            source.write_text(text)
            commands = [shlex.split(args.cc) + shlex.split(args.compile_flags) +
                        ['-c', str(source), '-o', str(obj)],
                        shlex.split(args.cc) + objects + [str(obj), '-o', str(binary)] +
                        shlex.split(args.link_flags)]
            entry = {'name': name, 'source_sha256': sha256(source.read_bytes()).hexdigest(),
                     'builds': [], 'runs': [], 'checks': []}
            receipt['variants'].append(entry)
            for phase, command in zip(('compile', 'link'), commands):
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=300)
                entry['builds'].append({'phase': phase, 'command': command, 'exit': result.returncode,
                                        'stdout': result.stdout, 'stderr': result.stderr})
                assert result.returncode == 0, entry['builds'][-1]
            entry['binary'] = str(binary)
            entry['binary_sha256'] = sha256(binary.read_bytes()).hexdigest()
            for language in ('prime', 'he', 'petta'):
                gate_args = SimpleNamespace(binary=binary, timeout=args.timeout)
                gate = Gate(gate_args) if language == 'prime' else DialectGate(gate_args, language)
                entry['runs'].append({'language': language, 'receipts': gate.receipts})
                slow, _, cases = weight_licence_dependency_cases(language)
                for case, definition, reason in cases:
                    if name == 'omit-admission' and reason != 'UncheckedAdmission': continue
                    if name == 'omit-matched-values' and reason != 'UncheckedMatchedValue': continue
                    source_text = slow + definition + '(=w (licence-answer) Visible 1) '
                    for width in (1, 2):
                        for declared in (False, True):
                            options = f'(workers {width}) (order by-weight descending)'
                            if declared: options += ' (law unit-interval-product)'
                            trees, _ = gate.run(case, source_text +
                                f'!(select 1 {control(10000, "WO", options)} {read("(licence-answer)")})')
                            result = list(weight_observations(trees))[-1]
                            values_found = [[contribution(x)[0], Fraction(str(contribution(x)[1]))]
                                            for x in result['values']]
                            expected = [['Hidden', 2]] if not declared or name == 'original' else [['Visible', 1]]
                            assert values_found == expected, (name, case, language, width, result)
                            _, pruning, stop = weight_order_record(result)
                            if not declared or name == 'original':
                                assert result['closed'] == 'True' and stop == 'Exhausted', result
                            else:
                                assert pruning[0] == 'Licensed' and stop[0] == 'Early', result
                        entry['checks'].append({'language': language, 'case': case, 'width': width,
                                                'agrees_with_exhaustive': name == 'original'})
            print('PASS: weight licence ' + name +
                  (' preserves exhaustive best' if name == 'original' else ' rejected by exhaustive best'), flush=True)
    finally:
        (args.out_dir/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == '__main__':
    main()
