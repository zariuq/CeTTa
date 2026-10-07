#!/usr/bin/env python3
"""Qualify directional joins and clause coverage before recording their costs."""

import argparse
import hashlib
import json
from pathlib import Path

import bench_chaining as bench


def join_program(rows):
    facts = ''.join(f'(p {i} {i + 1})\n' for i in range(rows))
    facts += '(p 37 38)\n(q 38 found)\n'
    call = '(pat:query &self match% (, (p 37 $x) (q $x $y)))'
    query = f'!(let $hits (collapse {call}) (size-atom $hits))\n'
    miss = '!(let $hits (collapse (pat:query &self match% (absent $x))) (size-atom $hits))\n'
    return '!(import! &self pat)\n' + facts + query * 10 + miss, [2] * 10 + [0]


def clause_program(rows, premises):
    general = ' '.join(f'(pos (p{i} $x))' for i in range(premises))
    specific = ' '.join(f'(pos (p{i} a))' for i in range(premises))
    specific += ' ' + ' '.join(f'(pos (noise {i}))' for i in range(rows - premises))
    call = f'(pat:subsume set ({general}) ({specific}))'
    miss = f'(pat:subsume set ((neg (missing $x))) ({specific}))'
    return ('!(import! &self pat:clause)\n'
            f'!(let $hits (collapse {call}) (size-atom $hits))\n'
            f'!(let $hits (collapse {miss}) (size-atom $hits))\n'), [1, 0]


def prefix_program(premises):
    facts = ''.join(f'(p{i} a)\n' for i in range(premises))
    patterns = ' '.join(f'(p{i} $x)' for i in range(premises))
    call = f'(pat:query &self match% (, {patterns}))'
    return ('!(import! &self pat)\n' + facts +
            f'!(let $hits (collapse {call}) (size-atom $hits))\n'), [1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--stats-cetta', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--instructions', action='store_true')
    parser.add_argument('--reference-cetta', type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('samples must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    binaries = [args.cetta.resolve(), args.stats_cetta.resolve()]
    receipt = {'source_sha256': bench.source_tree_sha256(),
               'binary_sha256': [bench.sha256_file(p) for p in binaries],
               'cases': []}
    reference = args.reference_cetta.resolve() if args.reference_cetta else None
    if reference:
        receipt['reference_sha256'] = bench.sha256_file(reference)
    cases = [(f'join-{n}', join_program(n)) for n in (100, 1000, 10000)]
    cases += [(f'clause-{n}-{k}', clause_program(n, k))
              for k in (2, 3) for n in (8, 16, 32)]
    cases += [(f'prefix-{k}', prefix_program(k)) for k in (2, 8, 32, 64)]
    prefix_counts = {}
    for name, (program, values) in cases:
        source = args.output / f'{name}.metta'
        source.write_text(program)
        for language in ('he', 'petta'):
            prefix = '[()]\n' if language == 'he' else 'true\n'
            expected = (prefix + ''.join(f'[{v}]\n' if language == 'he' else f'{v}\n'
                                        for v in values)).encode()
            row = {'id': name, 'oracle': 'exact', 'normalizer': 'identity',
                   'expected_count': str(len(values) + 1),
                   'expected_ordered_sha256': hashlib.sha256(expected).hexdigest(),
                   'timeout_s': '180'}
            results = []
            case_output = args.output / f'{name}-{language}'
            case_output.mkdir(exist_ok=True)
            for sample in range(args.samples):
                runs = [(language, binaries[0])]
                if reference:
                    runs.append((language + '-reference', reference))
                if sample % 2:
                    runs.reverse()
                for label, binary in runs:
                    results.append(bench.run_engine(label, language, row, source.resolve(),
                        sample + 1, case_output, binary, bench.ROOT, bench.ROOT,
                        profile='extended', measure_instructions=args.instructions))
            measured = bench.run_engine(language + '-stats', language, row,
                source.resolve(), 1, case_output, binaries[1], bench.ROOT, bench.ROOT,
                profile='extended', mechanism_stats=True)
            bench.qualify(row, results + [measured])
            if name.startswith('prefix-') and int(name.removeprefix('prefix-')) >= 8:
                length = int(name.removeprefix('prefix-'))
                counters = measured['runtime_counters']
                # Differences cancel the dialect's fixed import/dispatch work.
                # Fixed-size premises must not rewalk the growing prefix.
                if language in prefix_counts:
                    previous_length, previous_counts = prefix_counts[language]
                    for counter in ('term-match-subject-view', 'term-match-pair-visit'):
                        delta = counters[counter] - previous_counts[counter]
                        assert 0 < delta <= 8 * (length - previous_length), (name, language, counter, delta)
                prefix_counts[language] = length, counters
            for result in results + [measured]:
                result.pop('normalized')
            record = {'case': name, 'language': language,
                      'program_sha256': bench.sha256_file(source),
                      'runs': results, 'mechanisms': measured}
            receipt['cases'].append(record)
            (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
            print(f'PASS: {name} {language}, {len(values)} checked observations', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
