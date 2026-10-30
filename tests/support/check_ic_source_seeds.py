#!/usr/bin/env python3
"""Independent controls for training-only structural seed ordering."""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import random
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_context_library import candidate, encoded, sites
from check_ic_family_transfer import payload
from check_ic_search import check_controls
from check_source_codec import reference, text_arg


def shared(first, second):
    if first[0] != second[0]:
        return 0
    return 1 + sum(shared(left, right) for left, right in zip(first[1], second[1]))


def node(term):
    head, children = term
    return f'(IC:Node {head} (' + ' '.join(map(node, children)) + '))'


def packet(term, sort='ByteTree'):
    return f'(IC:Packet ((IC:Slot Root {sort})) ({node(term)}))'


def expected(packets, limit):
    pairs = sorted((-shared(first, second), left, right)
                   for left, first in enumerate(packets)
                   for right, second in enumerate(packets)
                   if left < right and first[0] == second[0])
    return ['Library:SeedRegion', [[str(left), str(right)] for _score, left, right in pairs[:limit]],
            str(len(pairs))]


def size_abstraction(binary, output):
    source = '''
(= (sized:first $xs) (if (== (size-atom $xs) 0) Empty Nonempty))
(= (sized:second $ys) (if (== (size-atom $ys) 0) Zero Positive))
(= (sized:emit $xs) (let $seen (println! SizeInputEffect) $xs))
'''
    document = reference(source)
    inventory = sites(document)
    seeds = [next(index for index, (owner, path, _bound, _term) in enumerate(inventory)
                  if owner == wanted and path == (2,)) for wanted in (0, 1)]
    changed, _support, _pattern = candidate(document, 'ic-learned:size', *seeds, selected=inventory)
    program = f'''!(import! &self (library lib_ic_context_library))
!(let $original (syntax:read {text_arg(source)})
   (case (context-library:seed (ic:prepare-library Deferred $original) "ic-learned:size" {seeds[0]} {seeds[1]})
     (((Library:Candidate $bytes $support)
       (case (ic:export-library $bytes)
         (((Library:Export $name $document $text $restored)
           (println! (SizeExport (== $restored $original) (tensor:inspect $bytes)
             (tensor:inspect (binary:utf8 $text)))))))))))
'''
    check_controls(program)
    path = output / 'size-export.metta'
    path.write_text(program)
    result = subprocess.run([str(binary), '--lang', 'petta', str(path.resolve())], cwd=ROOT,
                            capture_output=True, text=True, timeout=60)
    (output / 'size-export.log').write_text(result.stdout + result.stderr)
    rows = [row for row in parse_forms(result.stdout) if isinstance(row, list) and row[:1] == ['SizeExport']]
    assert result.returncode == 0 and len(rows) == 1, result.stdout[-1000:]
    assert rows[0][1] == 'true' and payload(rows[0][2]) == encoded('ic-learned:size', changed)
    exported = payload(rows[0][3]).decode()
    assert reference(exported) == changed
    queries = [('(sized:first ())', ['Empty']), ('(sized:first (A A))', ['Nonempty']),
               ('(sized:second ())', ['Zero']),
               ('(sized:first (superpose (() (A A) (A A))))', ['Empty', 'Nonempty', 'Nonempty']),
               ('(sized:first (superpose ()))', []),
               ('(sized:first (sized:emit (quote (A A))))', ['Nonempty'])]
    observations = []
    for label, text in (('original', source), ('learned', exported)):
        path = output / f'size-{label}.metta'
        appendix = '\n'.join(f'!(println! (SizeAnswer {index} (collapse {call})))'
                             for index, (call, _wanted) in enumerate(queries))
        program = text + '\n' + appendix + '\n'
        check_controls(program)
        path.write_text(program)
        result = subprocess.run([str(binary), '--lang', 'petta', str(path.resolve())], cwd=ROOT,
                                capture_output=True, text=True, timeout=60)
        (output / f'size-{label}.log').write_text(result.stdout + result.stderr)
        observed = parse_forms(result.stdout)
        answers = [row[2] for row in observed if isinstance(row, list) and row[:1] == ['SizeAnswer']]
        assert result.returncode == 0 and answers == [wanted for _call, wanted in queries]
        assert observed.count('SizeInputEffect') == 1
        observations.append(observed)
    assert observations[0] == observations[1], 'ordered answers or input effect moved'
    return len(queries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    code = ['!(import! &self (library lib_ic_source_seeds))']
    wanted = []

    def add(expression, value):
        index = len(wanted)
        code.append(f'!(println! (SeedControl {index} {expression}))')
        wanted.append(['SeedControl', str(index), value])

    cases = [[], [(1, [])], [(1, []), (1, []), (1, [])],
             [(1, [(2, [])]), (1, [(3, [])]), (1, [(2, [])]), (4, [])]]
    rng = random.Random(291026)
    for count in (4, 9, 16):
        cases.append([(rng.randrange(3), [(rng.randrange(4), []) for _ in range(rng.randrange(3))])
                      for _ in range(count)])
    for trees, limit in itertools.product(cases, (0, 1, 3, 1000)):
        expression = '(' + ' '.join(map(packet, trees)) + ')'
        add(f'(source-seeds:region {expression} {limit})', expected(trees, limit))
    add('(source-seeds:region () -1)', ['Library:Refused', 'InvalidSeedLimit'])
    add('(source-seeds:region () Wrong)', ['Library:Refused', 'InvalidSeedLimit'])
    add('(ic:source-seed-region Wrong 4)', ['Library:Refused', 'InvalidTask'])
    mixed = '(' + packet((1, [])) + ' ' + packet((1, []), 'Other') + ')'
    add(f'(source-seeds:region {mixed} 4)', ['Library:SeedRegion', [], '0'])
    add('(source-seeds:region ((IC:Packet ((IC:Slot Root ByteTree)) ((IC:Hole Root 0)))) 4)',
        ['Library:SeedRegion', [], '0'])
    add('(source-context:split? (Syntax:Expr ((Syntax:Literal Symbol (binary:utf8 "size-atom")) (Syntax:Variable 0))))',
        ['Ctx:Call', '"size-atom"', [['Syntax:Variable', '0']]])
    add('(source-context:split? (Syntax:Expr ((Syntax:Literal Symbol (binary:utf8 "size-atom")))))', 'Ctx:None')
    add('(ic:prepare-library Deferred (syntax:read "(= (size-atom $x) 0) (= (use $x) (== (size-atom $x) 0))"))',
        ['Library:Refused', 'NonstandardOperators'])
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'seeds.metta'
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=60)
    (args.output / 'seeds.log').write_text(result.stdout + result.stderr)
    actual = [form for form in parse_forms(result.stdout)
              if isinstance(form, list) and form and form[0] == 'SeedControl']
    mismatches = [{'wanted': before, 'actual': after}
                  for before, after in itertools.zip_longest(wanted, actual) if before != after]
    size_queries = size_abstraction(args.cetta.resolve(), args.output)
    receipt = {'passed': result.returncode == 0 and not mismatches, 'returncode': result.returncode,
               'checks': len(wanted), 'mismatches': mismatches,
               'size_abstraction_execution_queries': size_queries,
               'scope': 'same-head structural ranking only; no encoded-cost bound or optimum claim'}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
