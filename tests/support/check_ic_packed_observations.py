#!/usr/bin/env python3
"""Independent complete-cost decisions with operation-local decoder sharing."""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor
from check_binary_value import encoded
from check_ic_search import check_controls


HELPERS = r'''
!(import! &self (library lib_ic_packed))
!(import! &self (library lib_binary_value))
(= (ic-packed:observe PackedObservation:Exact $bytes)
   (case (binary-value:unpack $bytes)
     (((BinaryValue:Value (PackedObservation:Sample $value $padding))
       (if (and (binary:unsigned? $value) (binary-tree:bytes? $padding)) (IC:Some $value) IC:None))
      ($invalid-observation IC:None))))
(= (ic-packed:observe PackedObservation:Near $bytes)
   (ic-packed:observe PackedObservation:Exact $bytes))
(= (ic-packed:accepts? PackedObservation:Exact $source $candidate) (== $source $candidate))
(= (ic-packed:accepts? PackedObservation:Near $source $candidate)
   (and (<= $candidate (+ $source 1)) (<= $source (+ $candidate 1))))
(= (packed-observation:view $result $candidate)
   (case $result
     (((IC:PackedAccepted $bytes $before $after)
       (Accepted (== $bytes $candidate) $before $after))
      ($rejection $rejection))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    entries = [(encoded(['PackedObservation:Sample', value, bytes(padding)]), value)
               for value, padding in ((0, 32), (0, 0), (1, 16), (2, 0))]
    entries += [(b'not-an-artifact', None), (b'', None)]
    program = [HELPERS]
    expected = []

    def add(expression, wanted):
        index = len(expected)
        program.append(f'!(println! (PackedObservationCase {index} {expression}))')
        expected.append(['PackedObservationCase', str(index), wanted])

    for profile in ('Exact', 'Near'):
        for indices in itertools.product(range(len(entries)), repeat=3):
            (original, source), (current, before), (candidate, after) = (entries[i] for i in indices)
            if any(value is None for value in (source, before, after)):
                wanted = ['IC:PackedRejected', 'InvalidArtifact']
            elif not all((value == source if profile == 'Exact' else abs(value-source) <= 1)
                         for value in (before, after)):
                wanted = ['IC:PackedRejected', 'Reconstruction']
            elif len(candidate) >= len(current):
                wanted = ['IC:PackedRejected', 'NotSmaller']
            else:
                wanted = ['Accepted', 'true', str(len(current)), str(len(candidate))]
            literals = ' '.join(tensor(value) for value in (original, current, candidate))
            add(f'(packed-observation:view (ic-packed:check PackedObservation:{profile} {literals}) '
                f'{tensor(candidate)})', wanted)

    # Invalid byte carriers are refused before domain decoding. Equal
    # observations in different bytes must still pass the original policy.
    good = tensor(entries[0][0])
    for invalid in ('not-bytes', '(tensor:from-values i64 (1) (0))', '(tensor:from-values u8 (1 1) (0))'):
        for position in range(3):
            fields = [good, good, good]
            fields[position] = invalid
            add(f'(ic-packed:check PackedObservation:Exact {" ".join(fields)})',
                ['IC:PackedRejected', 'InvalidArtifact'])
    text = '\n'.join(program) + '\n'
    check_controls(text)
    path = args.output / 'packed-observations.metta'
    path.write_text(text)
    run = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                         cwd=ROOT, capture_output=True, text=True)
    (args.output / 'stdout.log').write_text(run.stdout)
    (args.output / 'stderr.log').write_text(run.stderr)
    actual = [form for form in parse_forms(run.stdout)
              if isinstance(form, list) and form and form[0] == 'PackedObservationCase']
    errors = [{'case': i, 'wanted': wanted, 'actual': got}
              for i, (wanted, got) in enumerate(itertools.zip_longest(expected, actual)) if wanted != got]
    receipt = {'passed': run.returncode == 0 and not errors, 'checks': len(expected),
               'observations': len(actual), 'errors': errors,
               'scope': 'all triples of six byte artifacts, exact/nontransitive policies and invalid carriers'}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
