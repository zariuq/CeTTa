#!/usr/bin/env python3
"""Paired native decoder costs on an original source and an exported helper."""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_search import check_controls
from check_source_codec import text_arg
from gen_semiring_fixtures import sexpr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--export', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pairs', type=int, default=7)
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error('pairs must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    original, exported = args.source.read_text(), args.export.read_text()
    definition = parse_forms(exported)[0]
    assert definition[0] == '=' and isinstance(definition[1], list)
    name = definition[1][0]
    forms = parse_forms((ROOT / 'lib/petta/lib_ic_packed.metta').read_text())
    reference = deepcopy(next(form for form in forms
                              if isinstance(form, list) and len(form) == 3 and form[0] == '='
                              and isinstance(form[1], list) and form[1][0] == 'ic-packed:check'))
    reference[1][0] = 'packed-bench:reference'

    def replace(term):
        if isinstance(term, list):
            return [replace(child) for child in term]
        return 'packed-bench:separate' if term == 'ic-packed:observations' else term

    reference[2] = replace(reference[2])
    code = ['!(import! &self (library lib_ic_context_library))',
            '!(import! &self system)', sexpr(reference), r'''
(= (packed-bench:separate $model $original $current $candidate)
   (IC:Observations (ic-packed:observe $model $original) (ic-packed:observe $model $current)
     (ic-packed:observe $model $candidate)))
(= (packed-bench:run $sample $variant $checker $original $candidate)
   (let* (($start (system:monotonic-ns))
          ($result ($checker IC:LibraryArchive $original $original $candidate))
          ($end (system:monotonic-ns)))
     (println! (PackedCheckCost $sample $variant (- $end $start)
       (case $result
         (((IC:PackedAccepted $bytes $before $after) (Accepted (== $bytes $candidate) $before $after))
          ($rejected $rejected)))))))
''']
    calls = []
    for pair in range(args.pairs):
        order = ('reference', 'shared') if pair % 2 == 0 else ('shared', 'reference')
        for variant in order:
            checker = 'packed-bench:reference' if variant == 'reference' else 'ic-packed:check'
            calls.append(f'($ignored-{pair}-{variant} (packed-bench:run {pair} {variant} '
                         f'{checker} $original $candidate))')
    code.append(f'''!(let* (($document (syntax:read {text_arg(original)}))
      ($learned (syntax:read {text_arg(exported)}))
      ($original (ic:source-pack $document))
      ($candidate (source-library:pack-codec "LC2" {text_arg(name)} $learned))
      {' '.join(calls)}) ())''')
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'paired-checks.metta'
    path.write_text(source)
    run = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                         cwd=ROOT, capture_output=True, text=True)
    (args.output / 'stdout.log').write_text(run.stdout)
    (args.output / 'stderr.log').write_text(run.stderr)
    rows = [row for row in parse_forms(run.stdout)
            if isinstance(row, list) and row and row[0] == 'PackedCheckCost']
    assert run.returncode == 0 and len(rows) == args.pairs * 2, (run.returncode, rows, run.stderr[-1000:])
    assert len({sexpr(row[4]) for row in rows}) == 1, 'complete decisions differ'
    assert rows[0][4][0] == 'Accepted' and rows[0][4][1] == 'true', rows[0][4]
    ratios = []
    for pair in range(args.pairs):
        times = {row[2]: int(row[3]) for row in rows if row[1] == str(pair)}
        ratios.append(times['shared'] / times['reference'])
    result = {'passed': True, 'pairs': args.pairs, 'decision': rows[0][4],
              'median_nanoseconds': {variant: statistics.median(int(row[3]) for row in rows if row[2] == variant)
                                     for variant in ('reference', 'shared')},
              'paired_shared_over_reference': ratios, 'samples': rows,
              'binary_sha256': hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
              'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
              'export_sha256': hashlib.sha256(exported.encode()).hexdigest(),
              'scope': 'same-process complete acceptance check, unchanged original winner; not whole-learning or held-out utility'}
    (args.output / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items() if key not in ('samples',)}))


if __name__ == '__main__':
    main()
