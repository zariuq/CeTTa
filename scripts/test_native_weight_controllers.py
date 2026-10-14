#!/usr/bin/env python3
"""Weighted controller agreement and complete retained-prefix controls.

Completed pure clients admit an explicit permutation and one fresh-name map.
Fixed-controller splits retain order and charged work. Neither comparison
identifies the costs or prefixes of different controllers.
"""
from __future__ import annotations

from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
import json

from test_prime_native_control import Gate, ROOT, bag
from test_native_weight_families import FamilyGate
from weighted_observation import controls, meaning, occurrence_witness, check_witness


def read(body, algebra='(algebra 0 1 + *)'):
    return f'(weight:read {algebra} {body})'


def policy(budget, width, mode='FIFO', all_results=True):
    demand = ' (demand all)' if all_results else ''
    return (f'(search-policy (control {mode} (budget {budget}) (workers {width})'
            f'{demand} (retain True) (age 2) (key (g3-lag $n $v) $n)))')


def pattern(prefix=''):
    return (f'(Observation ${prefix}p (satisfied ${prefix}s) (closed ${prefix}c) '
            f'(residual ${prefix}r) (work ${prefix}w) (status ${prefix}t))')


def tagged(tree, tag):
    if isinstance(tree, list):
        if tree and tree[0] == tag:
            yield tree
        else:
            for child in tree:
                yield from tagged(child, tag)


def compare(g, left, right, *, ordered):
    # These clients declare the returned outer Weighted record only. Subject
    # and coefficient data, even when Weighted-shaped, retain literal meaning.
    wrappers = ((),)
    witness = occurrence_witness(left, right, ordered=ordered, wrappers=wrappers)
    assert witness is not None and check_witness(
        left, right, witness, wrappers=wrappers, ordered=ordered), (left, right, witness)
    g.gate.receipts[-1].setdefault('weighted_comparisons', []).append({
        'left': left, 'right': right, 'ordered': ordered,
        'runtime_wrapper_paths': [[]], 'witness': witness})


def complete(observations, expected=None):
    assert len(observations) == 1, observations
    result = observations[0]
    assert result['closed'] == 'True' and result['status'] == 'closed', result
    if expected is not None:
        assert bag(map(meaning, result['values'])) == bag(expected), (result, expected)
    return result


def client_cases(language='prime'):
    crossing = '{$q}' if language == 'prime' else ''
    coins = '(=w (coin) 2 3) (=w (coin) 2 3) (=w (coin) 5 7)\n'
    yield dict(name='matched-two-stages',
        source='(edge a b) (edge a b) (edge a c) (edge b d) (edge c d) '
               '(= (grade b) 2) (= (grade c) 5)\n',
        expression=read('(let $q (match &self (edge a $q) (weight:where $q (grade $q))) '
                        '(match &self (edge $q $z) (weight:where $z 3))' + crossing + ')'),
        expected=[['d', 6, [2, 3]], ['d', 6, [2, 3]], ['d', 15, [5, 3]]])
    yield dict(name='shared-demand-duplicate-worlds',
        source=coins + '(= (graded $x) (weight:where (Pair $x $x) $x))\n',
        expression=read('(superpose ((graded (coin)) (graded (coin))))'),
        expected=[[['Pair', 2, 2], 6, [3, 2]]] * 4 + [[['Pair', 5, 5], 35, [7, 5]]] * 2)
    choices = [(2, 3), (2, 3), (5, 7)]
    yield dict(name='independent-producers', source=coins,
        expression=read('(Pair (coin) (coin))'),
        expected=[[['Pair', a, b], wa * wb, [wa, wb]] for a, wa in choices for b, wb in choices])
    yield dict(name='inherited-production-prefix', source='(=w (seed) (Box V) 2)\n',
        expression=read('(let (Box $s) (seed) (superpose ((Pair $s A) (Pair $s A) (Pair $s B))))'),
        expected=[[['Pair', 'V', 'A'], 2, [2]]] * 2 + [[['Pair', 'V', 'B'], 2, [2]]],
        common_prefix=True)
    yield dict(name='zero-and-equal-occurrences',
        source='(=w (row) A 0) (=w (row) A 0) (=w (row) B 5)\n',
        expression=read('(row)'), expected=[['A', 0, [0]], ['A', 0, [0]], ['B', 5, [5]]])
    yield dict(name='ordered-computed-coefficients',
        source='(= (inner) (weight:where 3 Inner)) (= (tag 3) Outer) '
               '(= (product $a $b) (Then $a $b)) '
               '(= (graded $x) (weight:where (Pair $x $x) (tag $x)))\n',
        expression=read('(superpose ((graded (inner)) (graded (inner))))', '(algebra Zero One Sum product)'),
        expected=[[['Pair', 3, 3], ['Then', ['Then', 'One', 'Inner'], 'Outer'], ['Inner', 'Outer']]] * 2)
    yield dict(name='unequal-work-priority',
        source='(= (g3-lag $n $v) (if (== $n 0) $v (g3-lag (- $n 1) $v))) '
               '(=w (row) (g3-lag 7 A) 2) (=w (row) (g3-lag 1 B) 3) '
               '(=w (row) (g3-lag 7 A) 2)\n',
        expression=read('(row)'), expected=[['A', 2, [2]], ['B', 3, [3]], ['A', 2, [2]]])


def completed_controllers(g):
    for case in client_cases(g.language):
        reference = None
        for width in (1, 2):
            for mode in ('FIFO', 'PO', 'WO'):
                _, observations = g.run(case['name'] + f' {mode}/{width}', case['source'] +
                    f'!(select 0 {policy(10000, width, mode)} {case["expression"]})')
                actual = complete(observations, case['expected'])
                if case.get('common_prefix'):
                    assert len({tuple(v[3][1][0][:3]) for v in actual['values']}) == 1, actual
                if reference is None:
                    reference = actual
                compare(g, reference['values'], actual['values'], ordered=False)
        g.check(case['name'] + ': completed occurrences, ordered factors and global sharing across six controllers')


def ordered_splits(g):
    selected = {'shared-demand-duplicate-worlds', 'inherited-production-prefix',
                'ordered-computed-coefficients'}
    for case in client_cases(g.language):
        if case['name'] not in selected:
            continue
        for width in (1, 2):
            for mode in ('FIFO', 'PO', 'WO'):
                name = case['name'] + f' {mode}/{width}'
                _, observations = g.run(name + ' whole', case['source'] +
                    f'!(select 0 {policy(10000, width, mode)} {case["expression"]})')
                whole = complete(observations, case['expected'])
                for cut in (0, 1, 2, 3, 7, 19, 31, 97):
                    trees, observations = g.run(name + f' cut{cut}', case['source'] +
                        f'!(let {pattern()} (select 0 {policy(cut, width, mode)} {case["expression"]}) '
                        f'(G3Split $p $w (select 0 {policy(10000-cut, width, mode)} $r)))')
                    roots = list(tagged(trees, 'G3Split'))
                    assert len(roots) == 1 and len(observations) == 1, trees
                    resumed = observations[0]
                    compare(g, whole['values'], roots[0][1] + resumed['values'], ordered=True)
                    for field in ('work', 'closed', 'satisfied', 'status'):
                        assert whole[field] == resumed[field], (name, cut, field, whole, resumed)
                g.check(name + ': exact ordered split, shared prefix and accepted work')


def choice_boundaries(g):
    cases = [
        ('empty', '', '(superpose ())', [], 0),
        ('single', '', '(superpose ((weight:where A 2)))', [['A', 2, [2]]], 0),
        ('duplicate', '', '(superpose ((weight:where A 2) (weight:where A 2) (weight:where B 3)))',
         [['A', 2, [2]], ['A', 2, [2]], ['B', 3, [3]]], 0),
        ('nested', '', '(superpose ((superpose ((weight:where A 2) (weight:where A 2))) (weight:where B 3)))',
         [['A', 2, [2]], ['A', 2, [2]], ['B', 3, [3]]], 0),
        ('effect', '(= (effect-g3) (let $unused (println! G3OnceEffect) (weight:where A 2)))\n',
         '(superpose ((effect-g3) (effect-g3)))', [['A', 2, [2]], ['A', 2, [2]]], 2)]
    for name, source, body, expected, effects in cases:
        trees, _ = g.run(name + ' ordinary weight read', source + '!' + read(body))
        values = [v for tree in trees for v in tree]
        assert bag(map(meaning, values)) == bag(expected), (name, values)
        assert g.effects('G3OnceEffect') == effects
        for width in (1, 2):
            _, observations = g.run(name + f' controlled/{width}', source +
                f'!(select 0 {policy(10000, width)} {read(body)})')
            actual = complete(observations, expected)
            compare(g, values, actual['values'], ordered=False)
            assert g.effects('G3OnceEffect') == effects
            if not expected:
                continue
            _, observations = g.run(name + f' first/{width}', source +
                f'!(select 1 {policy(10000, width, all_results=False)} {read(body)})')
            assert len(observations) == 1, observations
            first = observations[0]
            assert first['satisfied'] == 'True' and len(first['values']) == 1, first
            assert meaning(first['values'][0]) in expected
            # Siblings may execute effects before the first delivery. Demand
            # bounds accepted answers, not a prefix of all branch effects.
            if effects:
                assert 1 <= g.effects('G3OnceEffect') <= effects
            if len(expected) > 1:
                trees, observations = g.run(name + f' first/resume/{width}', source +
                    f'!(let {pattern("a")} (select 1 {policy(10000, width, all_results=False)} {read(body)}) '
                    f'(G3First $ap (select 0 {policy(10000, width)} $ar)))')
                starts = list(tagged(trees, 'G3First'))
                assert len(starts) == 1 and len(observations) == 1, trees
                resumed = complete(observations)
                compare(g, values, starts[0][1] + resumed['values'], ordered=False)
                assert g.effects('G3OnceEffect') == effects
        g.check(name + ': original, selected remainder and resumed effects preserve all occurrences')

    case = next(c for c in client_cases(g.language) if c['name'] == 'shared-demand-duplicate-worlds')
    for width in (1, 2):
        _, observations = g.run(f'repeated zero whole/{width}', case['source'] +
            f'!(select 0 {policy(10000, width)} {case["expression"]})')
        whole = complete(observations, case['expected'])
        for cut in (7, 31):
            text = case['source'] + (
                f'!(let {pattern("a")} (select 0 {policy(cut, width)} {case["expression"]}) '
                f'(let {pattern("b")} (select 0 {policy(0, width)} $ar) '
                f'(let {pattern("d")} (select 0 {policy(0, width)} $br) '
                '(G3Zeros $ap $aw $ar $ac $at $bp $bw $br $bc $bt $dp $dw $dr $dc $dt '
                f'(select 0 {policy(10000-cut, width)} $dr)))))')
            trees, observations = g.run(f'repeated zero/{width}/{cut}', text)
            packets = list(tagged(trees, 'G3Zeros'))
            assert len(packets) == 1 and len(observations) == 1, trees
            z = packets[0]
            assert z[4:6] == ['False', 'suspended'] and z[6] == [] and z[11] == [], z
            assert z[2:6] == z[7:11] == z[12:16], z
            resumed = observations[0]
            compare(g, whole['values'], z[1] + resumed['values'], ordered=True)
            for field in ('work', 'closed', 'satisfied', 'status'):
                assert whole[field] == resumed[field], (width, cut, field, whole, resumed)
        g.check(f'repeated zero/{width}: unchanged owner, work, pending state and eventual ordered results')


def pareto_complete_input(g):
    prefix = f'!(import! &self {ROOT}/lib/native_weight_algebras.metta)\n'
    # The exact same packet occurs twice. Filtering must keep both positions;
    # it must neither aggregate provenance nor manufacture new occurrence IDs.
    a = '(Weighted A (Vector (5 5)) (provenance ((Factor 11 13 EvidenceA))))'
    b = '(Weighted B (Vector (1 1)) (provenance ((Factor 17 19 EvidenceB))))'
    for width in (1, 2):
        expression = f'(G3Pareto (weights:pareto ({a} {a})) (weights:pareto ({a} {a} {b})))'
        _, observations = g.run(f'Pareto complete input/{width}', prefix +
            f'!(select 1 {policy(50000, width, all_results=False)} {expression})')
        result = observations[-1]['values']
        assert len(result) == 1 and result[0][0] == 'G3Pareto', observations
        earlier, final = result[0][1:]
        assert earlier == [['Weighted', 'A', ['Vector', [5, 5]], ['provenance', [['Factor', 11, 13, 'EvidenceA']]]]] * 2, earlier
        assert final == [['Weighted', 'B', ['Vector', [1, 1]], ['provenance', [['Factor', 17, 19, 'EvidenceB']]]]], final
        assert earlier[0] == earlier[1] and earlier[0] not in final
    g.check('Pareto preserves identical packets and refuses to treat a prefix as final before a future dominator')


def controllers(gate):
    g = FamilyGate(gate)
    gate._weighted_comparator_controls = controls()
    g.check('independent weighted witness rejects literal changes, lost occurrences, sharing and order faults')
    completed_controllers(g)
    ordered_splits(g)
    choice_boundaries(g)
    pareto_complete_input(g)


def main():
    p = ArgumentParser()
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--timeout', type=float, default=30)
    p.add_argument('--languages', nargs='+', default=['prime', 'he', 'petta'])
    p.add_argument('--program-dir', type=Path)
    p.add_argument('--receipt', type=Path)
    args = p.parse_args()
    args.binary = args.binary.resolve()
    if args.program_dir:
        args.program_dir.mkdir(parents=True, exist_ok=True)
        args.program_dir = args.program_dir.resolve()
    from test_native_dialects import DialectGate
    gates = []
    try:
        for language in args.languages:
            assert language in ('prime', 'he', 'petta'), language
            gate = Gate(args) if language == 'prime' else DialectGate(args, language)
            gates.append(gate)
            controllers(gate)
    finally:
        if args.receipt:
            args.receipt.write_text(json.dumps({
                'binary': str(args.binary), 'binary_sha256': sha256(args.binary.read_bytes()).hexdigest(),
                'passed': sum(g.passed for g in gates),
                'comparator_controls': [getattr(g, '_weighted_comparator_controls', []) for g in gates],
                'runs': [r for g in gates for r in g.receipts]}, indent=2) + '\n')
    print(f'Native weighted controllers: {sum(g.passed for g in gates)} passed, 0 failed')


if __name__ == '__main__':
    main()
