#!/usr/bin/env python3
"""Semantic coefficients on the native continuation and Need evaluator."""
from __future__ import annotations

import argparse
from fractions import Fraction
from hashlib import sha256
import json
from pathlib import Path
import random

from test_prime_native_control import Gate, ROOT, bag, parse


def authored_fixture(gate, name):
    """HE and PeTTa use shared source; Prime uses its generated lexical spelling."""
    path = ROOT / 'tests/prime/weights' / name
    lexical = path.with_suffix('.lexical.metta')
    return lexical if getattr(gate, 'language', 'prime') == 'prime' and lexical.is_file() else path


def contribution(value):
    assert len(value) == 4 and value[0] == 'Weighted', value
    assert value[3][0] == 'provenance', value
    factors = value[3][1]
    ids = [factor[1] for factor in factors]
    assert len(ids) == len(set(ids)) and all(i > 0 for i in ids), value
    assert all(len(f) == 4 and f[0] == 'Factor' and f[2] > 0 for f in factors), value
    return [value[1], value[2], [f[3] for f in factors]]


def provenance_packet(values):
    """Compare independent runs modulo one factor/dependency identity bijection."""
    identities = {}
    def identity(value):
        return identities.setdefault(value, len(identities))
    def walk(value):
        if not isinstance(value, list):
            return value
        if value and value[0] == 'Weighted':
            contribution(value)
            subject = walk(value[1])
            factors = [[f[0], identity(f[1]), identity(f[2]), f[3]]
                       for f in value[3][1]]
            return [value[0], subject, value[2], ['provenance', factors]]
        return [walk(item) for item in value]
    return walk(values)


def provenance_packet_controls(gate):
    rows = [
        ['Weighted', ['Factor', 10, 20, 'data'], 10,
         ['provenance', [['Factor', 10, 20, 2], ['Factor', 30, 10, 3]]]],
        ['Weighted', 'V', 10,
         ['provenance', [['Factor', 10, 20, 2], ['Factor', 40, 10, 3]]]],
    ]
    renamed = json.loads(json.dumps(rows))
    names = {10: 91, 20: 7, 30: 8, 40: 6}
    for row in renamed:
        for factor in row[3][1]:
            factor[1:3] = [names[value] for value in factor[1:3]]
    expected = provenance_packet(rows)
    assert provenance_packet(renamed) == expected
    assert expected[0][1:3] == rows[0][1:3]
    gate.passed_case('cross-run factor renaming retains payloads, coefficients and one shared namespace')
    # A single map spans every occurrence and both identity positions.
    for row, factor, slot, changed in ((1, 1, 1, 30), (1, 0, 1, 50),
                                       (1, 0, 2, 30), (1, 1, 2, 50)):
        damaged = json.loads(json.dumps(rows))
        damaged[row][3][1][factor][slot] = changed
        assert provenance_packet(damaged) != expected, damaged
    assert provenance_packet(rows[::-1]) != expected
    assert provenance_packet(rows + rows[:1]) != expected
    changed = json.loads(json.dumps(rows))
    changed[0][3][1][0][3] = 9
    assert provenance_packet(changed) != expected
    gate.passed_case('packet comparison rejects alias merge/split, changed dependencies, order and factors')


def read(body, algebra='(algebra 0 1 + *)'):
    return f'(weight:read {algebra} {body})'


def control(budget=10000, mode='FIFO', extra=''):
    return f'(search-policy (control {mode} (budget {budget}) {extra}))'


def matched_coefficients(gate):
    source = '(edge a b) (edge a b) (edge a c) (edge b d) (edge c d) '
    source += '(= (grade b) 2) (= (grade c) 5) '
    crossing = '{$q}' if getattr(gate, 'language', 'prime') == 'prime' else ''
    first = '(match &self (edge a $q) (weight:where $q (grade $q)))'
    bodies = [
        ('first match', f'(let $q {first} (match &self (edge $q $z) $z){crossing})',
         [['d', 2, [2]], ['d', 2, [2]], ['d', 5, [5]]]),
        ('both matches', f'(let $q {first} (match &self (edge $q $z) (weight:where $z 3)){crossing})',
         [['d', 6, [2, 3]], ['d', 6, [2, 3]], ['d', 15, [5, 3]]]),
        ('returned constructor', f'(match &self (edge a $q) (weight:where (Seen $q) (grade $q)))',
         [[['Seen', 'b'], 2, [2]], [['Seen', 'b'], 2, [2]], [['Seen', 'c'], 5, [5]]]),
    ]
    for name, body, expected in bodies:
        trees, _ = gate.run('ordinary coefficient composition ' + name, source + f'!{read(body)}')
        actual = [contribution(x) for tree in trees for x in tree]
        assert bag(actual) == bag(expected), (name, actual, expected)
        _, controlled = gate.run('controlled coefficient composition ' + name,
            source + f'!(select 7 {control()} {read(body)})')
        assert controlled[-1]['closed'] == 'True'
        assert bag([contribution(x) for x in controlled[-1]['values']]) == bag(expected), controlled
        ids = [x[3][1][0][1] for x in controlled[-1]['values']]
        assert len(set(ids)) == len(ids), controlled
        gate.passed_case('matched physical rows retain coefficient world through ' + name)

    body = bodies[1][1]
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for budget in (0, 1, 7, 23, 71, 193):
        _, whole = gate.run(f'match coefficient whole {budget}', source +
            f'!(select 7 {control(500)} {read(body)})')
        trees, parts = gate.run(f'match coefficient split {budget}', source +
            f'!(let {pattern} (select 7 {control(budget)} {read(body)}) '
            f'(Split $p (select 7 {control(500-budget)} $r)))')
        values = trees[-1][0][1] + parts[-1]['values']
        assert [contribution(x) for x in values] == [
            contribution(x) for x in whole[-1]['values']], (budget, values, whole)
        for field in ('work', 'closed', 'satisfied', 'status'):
            assert parts[-1][field] == whole[-1][field], (budget, field, parts, whole)
        gate.passed_case(f'matched coefficient grade/body/return split {budget}')

    identities = '(= (gid $x) (weight:where $x 2)) (= (unitGradeId $x) (weight:where $x 1)) '
    for body, expected in [('(unitGradeId V)', ['V', 1, [1]]),
                           ('(gid (gid V))', ['V', 4, [2, 2]])]:
        trees, _ = gate.run('graded identity ' + body, identities + f'!{read(body)}')
        assert contribution(trees[-1][0]) == expected, trees
        gate.passed_case('graded identity returns through ' + body)


def nested_coefficients(gate):
    cases = [
        ('binding-local nested identity',
         '(= (gid $x) (weight:where $x $x)) ', '(gid (gid 7))',
         [[7, 49, [7, 7]]]),
        ('score-forced graded argument',
         '(= (coin) (weight:where 1 3)) (= (gid $x) (weight:where $x $x)) ',
         '(gid (coin))', [[1, 3, [3, 1]]]),
        ('shared graded alternatives',
         '(= (coin) (weight:where 2 3)) (= (coin) (weight:where 2 3)) '
         '(= (coin) (weight:where 5 7)) '
         '(= (gid $x) (weight:where (Pair $x $x) $x)) ',
         '(gid (coin))', [[['Pair', 2, 2], 6, [3, 2]],
                          [['Pair', 2, 2], 6, [3, 2]],
                          [['Pair', 5, 5], 35, [7, 5]]]),
        ('grade-only argument',
         '(= (coin) (weight:where 9 2)) (= (gid $x) (weight:where V $x)) ',
         '(gid (coin))', [['V', 18, [2, 9]]]),
    ]
    for name, source, body, expected in cases:
        trees, _ = gate.run(name + ' ordinary', source + f'!{read(body)}')
        actual = [contribution(x) for tree in trees for x in tree]
        assert bag(actual) == bag(expected), (name, actual, expected)
        for workers in (1, 2):
            extra = f'(workers {workers})'
            _, whole = gate.run(name + f' workers {workers}', source +
                f'!(select 7 {control(1000, extra=extra)} {read(body)})')
            result = whole[-1]
            assert result['closed'] == 'True'
            assert bag([contribution(x) for x in result['values']]) == bag(expected), whole
            for budget in (0, 9, 37, 93, 94, 95, 96, 97, 100, 101, 102, 110):
                pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
                trees, parts = gate.run(name + f' split {workers}/{budget}', source +
                    f'!(let {pattern} (select 7 {control(budget, extra=extra)} {read(body)}) '
                    f'(Split $p (select 7 {control(1000-budget, extra=extra)} $r)))')
                values = trees[-1][0][1] + parts[-1]['values']
                assert [contribution(x) for x in values] == [
                    contribution(x) for x in result['values']], (name, workers, budget, values, result)
                for field in ('work', 'closed', 'satisfied', 'status'):
                    assert parts[-1][field] == result[field], (name, workers, budget, field,
                        parts[-1][field],result[field])
            gate.passed_case(f'{name} retains nested grade scope and complete splits, workers={workers}')


def ordered_coefficients(gate):
    language=getattr(gate,'language','prime')
    algebra='(algebra Zero One Sum product)'
    source='(= (inner) (weight:where 3 Inner)) (= (tag 3) Outer) '
    source+='(= (product $a $b) (Then $a $b)) '
    for name, grade in [('forced','(tag $x)'),('literal','Outer')]:
        definitions=source+f'(= (gid $x) (weight:where $x {grade})) '
        factors=['Outer','Inner'] if name=='literal' and language=='prime' else ['Inner','Outer']
        expected=[3,['Then',['Then','One',factors[0]],factors[1]],factors]
        for workers in (1,2):
            _, results=gate.run('ordered graded argument '+name,
                definitions+f'!(select 7 {control(3000,extra=f"(workers {workers})")} '
                f'{read("(gid (inner))",algebra)})')
            assert results[-1]['closed']=='True'
            assert [contribution(x) for x in results[-1]['values']]==[expected],results
        gate.passed_case('ordered coefficient execution follows host demand: '+name)
    if language in ('prime','petta'):
        body='(App (gid (Lam T (+ (idx 0) 1))) 7)' if language=='prime' else \
            '(let $f (gid (|-> ($x) (+ $x 1))) ($f 7))'
        definitions='(= (gid $x) (weight:where $x 2)) '
        pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
        for cut in (0,7,19,47):
            trees,results=gate.run('returned graded closure '+str(cut),definitions+
                f'!(let {pattern} (select 7 {control(cut,extra="(workers 2)")} {read(body)}) '
                f'(Split $p (select 7 {control(3000-cut,extra="(workers 2)")} $r)))')
            values=trees[-1][0][1]+results[-1]['values']
            assert [contribution(x) for x in values]==[[8,2,[2]]],values
            assert results[-1]['closed']=='True',results
        gate.passed_case('returned graded host closure applies after split capture')


def matched_source_scopes(gate):
    source = '(= (loop) (loop)) !(bind! &kb (new-space)) !(add-atom &kb (Fact A)) '
    body = '(match &kb (Fact $x) (weight:where $x (loop)))'
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for name, mutation, expected in [
        ('matching insertion', '(add-atom &kb (Fact B))', 'invalidated'),
        ('duplicate insertion', '(add-atom &kb (Fact A))', 'invalidated'),
        ('selected row removal', '(remove-atom &kb (Fact A))', 'invalidated'),
        ('unchanged source', 'True', 'suspended'),
    ]:
        _, observations = gate.run('pending matched coefficient ' + name, source +
            f'!(let {pattern} (select 7 {control(30, extra="(workers 1)")} {read(body)}) '
            f'(let $_ {mutation} (select 7 {control(30, extra="(workers 1)")} $r)))')
        result = observations[-1]
        assert result['values'] == [] and result['closed'] == 'False'
        assert result['status'] == expected, (name, result)
        gate.passed_case('captured external match source survives pending grade: ' + name)

    other='!(bind! &other (new-space)) '
    _, observations=gate.run('unrelated Space mutation leaves captured query current',source+other+
        f'!(let {pattern} (select 7 {control(30)} {read(body)}) '
        f'(let $_ (add-atom &other (Fact B)) (select 7 {control(30)} $r)))')
    assert observations[-1]['status']=='suspended' and observations[-1]['closed']=='False',observations
    gate.passed_case('unrelated Space mutation preserves the actual captured query authority')

    definitions='(= (gid $x) (weight:where $x 2)) '
    expression='(let $s (gid (new-space)) (let $_ (add-atom $s A) (match $s A A)))'
    for workers in (1,2):
        for cut in (0,7,31,59):
            trees,observations=gate.run(f'graded returned Space workers={workers} cut={cut}',definitions+
                f'!(let {pattern} (select 7 {control(cut,extra=f"(workers {workers})")} {read(expression)}) '
                f'(Split $p (select 7 {control(3000-cut,extra=f"(workers {workers})")} $r)))')
            values=trees[-1][0][1]+observations[-1]['values']
            assert [contribution(x) for x in values]==[['A',2,[2]]],values
            assert observations[-1]['closed']=='True',observations
        gate.passed_case(f'captured graded Space retains identity through mutation, query and return workers={workers}')


def retained_queries(gate):
    pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    def nested(value):
        for _ in range(80): value=f'(N {value})'
        return value
    source=f'(F {nested("7")}) (F {nested("7")}) (F {nested("9")}) '
    source+='(= (grade 7) 2) (= (grade 9) 3) '
    expression=read(f'(match &self (F {nested("$x")}) $x (where (grade $x)))')
    expected=[[7,2,[2]],[7,2,[2]],[9,3,[3]]]
    for workers in (1,2):
        extra=f'(workers {workers})'
        _,whole=gate.run(f'retained deep query whole/{workers}',source+
            f'!(select 7 {control(1500,extra=extra)} {expression})')
        final=whole[-1]
        actual=[contribution(x) for x in final['values']]
        assert bag(actual)==bag(expected) and final['closed']=='True',final
        for cut in (0,1,7,13,23,37,61,101,final['work']-1):
            trees,parts=gate.run(f'retained deep query split/{workers}/{cut}',source+
                f'!(let {pattern} (select 7 {control(cut,extra=extra)} {expression}) '
                f'(Split $p (select 7 {control(1500-cut,extra=extra)} $r)))')
            combined=trees[-1][0][1]+parts[-1]['values']
            assert [contribution(x) for x in combined]==actual,(workers,cut,combined,actual)
            for field in ('work','closed','satisfied','status'):
                assert parts[-1][field]==final[field],(workers,cut,field,parts,whole)
        gate.passed_case(f'query nodes and graded duplicate rows survive exact cuts with {workers} workers')
    source='(F slow) (F fast) (= (body slow) (body slow)) (= (body fast) 42) '
    expression=read('(match &self (F $x) (body $x) (where 2))')
    for workers in (1,2,4):
        default=None
        for quantum in (None,1,8,40):
            extra=f'(workers {workers})'+('' if quantum is None else f' (quantum {quantum})')
            _,obs=gate.run(f'query body independence/{workers}/{quantum}',source+
                f'!(select 1 {control(1000,extra=extra)} {expression})')
            result=obs[-1]
            assert [contribution(x) for x in result['values']]==[[42,2,[2]]],obs
            assert result['satisfied']=='True' and result['closed']=='False',obs
            if quantum is None: default=result
            if quantum==1:
                for field in ('work','closed','satisfied','status'):
                    assert result[field]==default[field],(workers,field,default,result)
            if workers>1:
                for cut in (1,13,37,97,result['work']-1):
                    trees,parts=gate.run(f'query body split/{workers}/{quantum}/{cut}',source+
                        f'!(let {pattern} (select 1 {control(cut,extra=extra)} {expression}) '
                        f'(Split $p (select 1 {control(1000-cut,extra=extra)} $r)))')
                    values=trees[-1][0][1]+parts[-1]['values']
                    assert [contribution(x) for x in values]==[[42,2,[2]]],parts
                    for field in ('work','closed','satisfied','status'):
                        assert parts[-1][field]==result[field],(workers,quantum,cut,field,parts,result)
        gate.passed_case(f'a recursive first match body leaves later match work runnable with {workers} workers')

    source='(F [7]) (F [7 9]) (F [7]) (F (7 9)) '
    cases=[
        ('rigid list lengths','(match &self (F [7]) Hit (where 2))',
         [['Hit',2,[2]],['Hit',2,[2]]]),
        ('closed list absence','(match &self (F [7 9 11]) Hit (where 2))',[]),
        ('open list remainder','(match &self (F [7 | $t]) (Tail $t) (where (size-atom $t)))',
         [[['Tail',[]],0,[0]],[['Tail',[9]],1,[1]],[['Tail',[]],0,[0]]]),
    ]
    for name,body,expected in cases:
        for workers in (1,2):
            _,obs=gate.run(f'query {name}/{workers}',source+
                f'!(select 7 {control(1000,extra=f"(workers {workers})")} {read(body)})')
            assert bag([contribution(x) for x in obs[-1]['values']])==bag(expected),obs
            assert obs[-1]['closed']=='True' and obs[-1]['satisfied']=='False',obs
        gate.passed_case(f'{name} keeps list mismatch distinct from pending work')


def retained_query_owners(gate):
    from test_prime_native_cost import receipt

    source='!(bind! &kb (new-space)) '
    source+='!(add-atom &kb A) !(add-atom &kb B) !(add-atom &kb C) '
    extra='(workers 1) (costs native-events-v1)'
    # A closed wrapper computes its empty source once. Separate authored
    # alternatives still activate independently, including equal answers.
    cases=[
        ('direct complete-empty', '', '(match &kb Missing Hit)', [], 3, 0),
        ('matched but empty body', '', '(match &kb A (empty))', [], 3, 0),
        ('empty nested equation', '(= (wrapped) (match &kb Missing Hit)) ',
         '(wrapped)', [], None, 1),
        ('empty and successful alternatives',
         '(= (wrapped) (match &kb Missing Hit)) (= (wrapped) Good) (= (wrapped) Good) ',
         '(wrapped)', [['Good',1,[]],['Good',1,[]]], None, 3),
    ]
    for name,definitions,body,expected,visits,activations in cases:
        _,observed=gate.run('empty-query owner: '+name,source+definitions+
            f'!(select 7 {control(1000,extra=extra)} {read(body)})')
        final=observed[-1]
        assert final['closed']=='True' and final['satisfied']=='False',final
        assert [contribution(x) for x in final['values']]==expected,final
        counts=receipt(final['cost'])['counts']
        if visits is not None:
            # One traversal of the three-row source, even when a matched
            # body produces no answer. Projection must not replay the read.
            assert counts['index-visit']==visits,(name,counts)
        assert counts['equation-activation']==activations,(name,counts)
        gate.passed_case('complete-empty certificate belongs to its exact return edge: '+name)

    body=read('(match &kb Missing Hit)')
    pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    _,whole=gate.run('empty-query owner whole',source+f'!(select 7 {control(1000)} {body})')
    total=whole[-1]['work']
    assert total>13,whole
    for cut in sorted({0,1,7,13,total-2,total-1,total,total+7}):
        trees,parts=gate.run('empty-query owner split '+str(cut),source+
            f'!(let {pattern} (select 7 {control(cut)} {body}) '
            f'(State $p $w $c $t (select 7 {control(1000-cut)} $r)))')
        first=trees[-1][0]
        assert first[:5]==['State',[],min(cut,total),
                           'False' if cut<total else 'True',
                           'suspended' if cut<total else 'closed'],first
        for field in ('values','work','closed','satisfied','status'):
            assert parts[-1][field]==whole[-1][field],(cut,field,parts,whole)
    gate.passed_case('empty-query completion and exact work survive preparation/end/return cuts')

    for cut in sorted({13,total-2,total-1}):
        trees,parts=gate.run('captured absence invalidated '+str(cut),source+
            f'!(let {pattern} (select 7 {control(cut)} {body}) '
            f'(let $_ (add-atom &kb Missing) '
            f'(State $r $w $c $t (select 7 {control(1000-cut)} $r))))')
        first=trees[-1][0]
        final=parts[-1]
        assert first[2:5]==[cut,'False','suspended'],first
        assert final['residual']==first[1] and final['work']==cut,(first,final)
        assert final['values']==[] and final['status']=='invalidated',final
        assert final['closed']=='False' and final['satisfied']=='False',final
    gate.passed_case('invalidated absence retains the exact query handle and spent work')

    _,pending=gate.run('nonempty query retains open body',source+
        '(= (loop) (loop)) '+f'!(select 7 {control(1000)} '
        f'{read("(match &kb A (loop))")})')
    assert pending[-1]['values']==[] and pending[-1]['work']==1000,pending
    assert pending[-1]['closed']=='False' and pending[-1]['status']=='suspended',pending
    gate.passed_case('nonempty completed query does not close its unfinished body')


def fixed(gate):
    trees, _ = gate.run('automatic weighted evaluation',
        '!(weight:read (algebra 0 1 + *) (weight:where (Value 7) 3))')
    assert contribution(trees[-1][0]) == [['Value', 7], 3, [3]]
    gate.passed_case('ordinary evaluation owns coefficient and fold continuations')

    trees, _ = gate.run('unannotated and zero contributions',
        '!(weight:read (algebra 0 1 + *) (Value 7))\n'
        '!(weight:read (algebra 0 1 + *) (weight:where (Value 7) 0))')
    assert contribution(trees[0][0]) == [['Value', 7], 1, []]
    assert contribution(trees[1][0]) == [['Value', 7], 0, [0]]
    gate.passed_case('neutral unannotated derivation and retained semantic zero')

    prefix = '(= (choose) 2) (= (choose) 3) '
    expression = read('(let $x (choose) (weight:where (Pair $x $x) $x))')
    for mode in ('FIFO', 'PO', 'WO'):
        _, obs = gate.run('captured world ' + mode,
            prefix + f'!(select 7 {control(mode=mode)} {expression})')
        assert obs[-1]['closed'] == 'True'
        assert bag([contribution(x) for x in obs[-1]['values']]) == bag([
            [['Pair', 2, 2], 2, [2]], [['Pair', 3, 3], 3, [3]]])
    gate.passed_case('coefficient-result world reaches RHS across all controllers')

    source = '(= (weighted) (weight:where 2 3)) '
    expression = read('(let $x (weighted) (Pair $x $x))')
    trees, _ = gate.run('one shared weighted producer', source + f'!{expression}')
    assert contribution(trees[-1][0]) == [['Pair', 2, 2], 3, [3]]
    gate.passed_case('reusing one Need cell does not multiply its factor twice')

    source = '(= (rows) (weight:where V 2)) (= (rows) (weight:where V 2)) '
    _, obs = gate.run('physical equal rows', source +
                     f'!(select 7 {control()} {read("(rows)")})')
    values = obs[-1]['values']
    assert [contribution(x) for x in values] == [['V', 2, [2]], ['V', 2, [2]]]
    assert values[0][3][1][0][1] != values[1][3][1][0][1]
    gate.passed_case('equal row values retain two physical contributions')

    source = '(= (product $a $b) (Then $a $b)) '
    expression = read('(weight:where (weight:where V right) left)',
                      '(algebra zero one sum product)')
    trees, _ = gate.run('noncommutative factor order', source + f'!{expression}')
    assert contribution(trees[-1][0]) == [
        'V', ['Then', ['Then', 'one', 'left'], 'right'], ['left', 'right']]
    gate.passed_case('noncommutative program fold follows logical factor order')

    source = '(= (loop) (loop)) (= (rows) (weight:where A (loop))) '
    source += '(= (rows) (weight:where B 5)) '
    _, obs = gate.run('unfinished semantic coefficient', source +
        f'!(select 2 {control(350)} {read("(rows)")})')
    assert [contribution(x) for x in obs[-1]['values']] == [['B', 5, [5]]]
    assert obs[-1]['closed'] == 'False' and obs[-1]['satisfied'] == 'False'
    gate.passed_case('unfinished semantic coefficient stays pending while a peer finishes')

    source = '(= (loop $a $b) (loop $a $b)) '
    expression = read('(weight:where V 5)', '(algebra 0 1 + loop)')
    _, obs = gate.run('unfinished algebra multiplication', source +
                     f'!(select 1 {control(250)} {expression})')
    assert obs[-1]['values'] == [] and obs[-1]['closed'] == 'False'
    gate.passed_case('unfinished program-defined multiplication is an owned obligation')

    expression = read('(weight:where V 0)')
    _, obs = gate.run('zero advice is distinct from zero coefficient',
        f'!(select 1 {control(extra="(key $candidate 0)", mode="WO")} {expression})')
    assert [contribution(x) for x in obs[-1]['values']] == [['V', 0, [0]]]
    gate.passed_case('zero scheduling priority does not discard zero semantic contribution')

    source = '(= (pauseN $n $v) (if (== $n 0) $v (pauseN (- $n 1) $v))) '
    source += '(= (rows) (weight:where A (pauseN 6 2))) '
    source += '(= (rows) (weight:where B 3)) '
    expression = read('(rows)')
    source += '\n!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t)) '
    source += f'(select 1 {control(75)} {expression}) '
    source += f'(select 0 {control(20000, extra="(demand all)")} $r))'
    _, obs = gate.run('selective to complete weighted continuation', source)
    assert bag([contribution(x) for x in obs[-1]['values']]) == bag([
        ['A', 2, [2]], ['B', 3, [3]]])
    assert obs[-1]['closed'] == 'True' and obs[-1]['satisfied'] == 'True'
    gate.passed_case('selective demand resumes into a complete weighted observation')


def splits(gate, count):
    rng = random.Random(0xC0EFF1C1)
    source = '(= (slow $x $y) (if (== $y 0) 0 (+ $x (slow $x (- $y 1))))) '
    source += '(= (rows) (weight:where (weight:where A 5) 3)) '
    source += '(= (rows) (weight:where B 2)) '
    expression = read('(rows)', '(algebra 0 1 + slow)')
    for case in range(count):
        first, second = rng.randrange(40, 400), rng.randrange(30, 500)
        _, combined = gate.run(f'weighted combined {case}', source +
                              f'!(select 7 {control(first+second)} {expression})')
        split = source + '\n!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t)) '
        split += f'(select 7 {control(first)} {expression}) '
        split += f'(Split $p $w (select 7 {control(second)} $r)))'
        trees, resumed = gate.run(f'weighted split {case}', split)
        actual = trees[-1][0][1] + resumed[-1]['values']
        assert [contribution(x) for x in actual] == [
            contribution(x) for x in combined[-1]['values']], (case, actual, combined)
        for field in ('work', 'closed', 'satisfied', 'status'):
            assert resumed[-1][field] == combined[-1][field], (case, field, resumed, combined)
        gate.passed_case(f'pending coefficient/fold whole-state budget split {case}')


def coefficient_choice_worlds(gate):
    source = '(= (coin) 2) (= (coin) 3) (= (coefficient $x) $x) '
    source += '(= (f $x) (weight:where (Pair $x $x) (coefficient $x))) '
    expression = read('(f (coin))')
    expected = [[['Pair', 2, 2], 2, [2]], [['Pair', 3, 3], 3, [3]]]
    for workers in (1, 2):
        for mode in ('FIFO', 'PO', 'WO'):
            _, observations = gate.run(f'lazy coefficient choice {mode} workers={workers}',
                source + f'!(select 7 {control(mode=mode, extra=f"(workers {workers})")} {expression})')
            result = observations[-1]
            assert result['closed'] == 'True' and result['satisfied'] == 'False', result
            assert bag([contribution(x) for x in result['values']]) == bag(expected), result
            gate.passed_case(f'coefficient-selected lazy cell reaches both RHS uses {mode} workers={workers}')
        for first in (0, 9, 31, 97):
            split = source + '\n!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t)) '
            split += f'(select 7 {control(first, extra=f"(workers {workers})")} {expression}) '
            split += f'(Split $p (select 7 {control(extra=f"(workers {workers})")} $r)))'
            trees, observations = gate.run(f'lazy coefficient choice resume {first} workers={workers}', split)
            actual = trees[-1][0][1] + observations[-1]['values']
            assert observations[-1]['closed'] == 'True', observations
            assert bag([contribution(x) for x in actual]) == bag(expected), actual
            gate.passed_case(f'coefficient and body retain one selected world through resume {first} workers={workers}')


def algebras(gate):
    prefix = '!(import! &self lib/native_weight_algebras.metta) '
    prefix += '!(pragma! prefer-rationals True) '
    def evaluated(name, expression):
        _, obs = gate.run(name, prefix + f'!(select 1 {control(50000)} {expression})')
        assert obs[-1]['satisfied'] == 'True', obs
        return obs[-1]['values'][0]

    value = evaluated('Boolean AND coefficient',
        read('(weight:where (weight:where V True) False)', '(weights:boolean)'))
    assert contribution(value) == ['V', 'False', ['False', 'True']]
    assert evaluated('Boolean OR additive law', '(or True True)') == 'True'
    gate.passed_case('Boolean OR/AND keeps false contributions before support')

    value = evaluated('exact rational coefficients',
        read('(weight:where (weight:where V 3/7) 1/3)', '(weights:rational)'))
    assert contribution(value) == ['V', '1/7', ['1/3', '3/7']]
    gate.passed_case('exact rational multiplication is separate from minimum-plus costs')

    value = evaluated('vector coefficient',
        read('(weight:where (weight:where V (Vector (2 5))) (Vector (2 3)))',
             '(weights:vector 2)'))
    assert contribution(value) == ['V', ['Vector', [4, 15]], [
        ['Vector', [2, 3]], ['Vector', [2, 5]]]]
    pareto = evaluated('finite incomparable coefficient observation',
        '(weights:pareto ((Weighted a (Vector (1 4)) p) '
        '(Weighted b (Vector (4 1)) q) (Weighted c (Vector (5 5)) r) '
        '(Weighted d (Vector (1 4)) s)))')
    assert [x[1] for x in pareto] == ['a', 'b', 'd']
    gate.passed_case('componentwise vectors expose incomparable and tied Pareto occurrences')

    a, b = '(Matrix 2 2 (1 1 0 1))', '(Matrix 2 2 (1 0 1 1))'
    value = evaluated('source ordered matrix coefficients',
        read(f'(weight:where (weight:where V {b}) {a})', '(weights:matrix 2)'))
    assert contribution(value)[1] == ['Matrix', 2, 2, [2, 1, 1, 1]]
    opposite = evaluated('reverse matrix product', f'(matrix:mul {b} {a})')
    assert opposite == ['Matrix', 2, 2, [1, 1, 1, 2]]
    assert evaluated('rectangular indexed contraction',
        '(matrix:mul (Matrix 1 2 (1 2)) (Matrix 2 3 (3 4 5 6 7 8)))') == [
            'Matrix', 1, 3, [15, 18, 21]]
    assert evaluated('fractional matrix shape rejected', '(matrix:valid? (Matrix 1/2 2 (7)))') == 'False'
    _, obs = gate.run('incompatible contraction dimensions', prefix +
        f'!(select 1 {control()} (matrix:mul (Matrix 1 2 (1 2)) (Matrix 1 1 (3))))')
    assert obs[-1]['values'] == [] and obs[-1]['closed'] == 'True'
    gate.passed_case('matrix order and checked rectangular contraction retain shape distinctions')

    source = '(= (paths) (weight:where A (Complex 1 0))) '
    source += '(= (paths) (weight:where A (Complex -1 0))) '
    source += '(= (paths) (weight:where B (Complex 0 1))) '
    client = '(let (Observation $values (satisfied $s) (closed $c) (residual $r) (work $w) (status $t)) '
    client += f'(select 0 {control(20000, extra="(demand all)")} {read("(paths)", "(weights:complex)")}) '
    client += '(let $sums (weights:aggregate (weights:complex) $values) '
    client += '(Readouts $values $sums (weights:support (Complex 0 0) $sums) (weights:born $sums))))'
    _, obs = gate.run('native complex contribution and cancellation client', prefix + source +
                     f'!(select 1 {control(50000)} {client})')
    result = obs[-1]['values'][0]
    assert result[0] == 'Readouts' and len(result[1]) == 3
    assert [[x[1], x[2]] for x in result[2]] == [
        ['A', ['Complex', 0, 0]], ['B', ['Complex', 0, 1]]]
    assert [x[1] for x in result[3]] == ['B']
    assert [[x[1], x[2]] for x in result[4]] == [['A', 0], ['B', 1]]
    assert len(result[2][0][3][1]) == 2
    assert evaluated('zero Born norm is not a distribution',
        '(weights:born ((WeightedSum A (Complex 0 0) p)))') == ['BornUndefined', 'zero-norm']
    gate.passed_case('interference keeps physical provenance; support and Born are explicit readouts')

    _, obs = gate.run('existing evidence tensor coefficient', prefix +
        '!(import! &self lib/lib_wmpln_evidence.metta) ' +
        f'!(select 1 {control(30000)} ' + read(
            '(weight:where (weight:where Claim (evidence 3 2)) (evidence 4 1))',
            '(weights:pln)') + ')')
    assert contribution(obs[-1]['values'][0])[1] == ['evidence', 12, 2]
    gate.passed_case('PLN coefficient reuses evidence tensor without asserting revision independence')

    source = '(= (apply $operation $x $y) ($operation $x $y)) '
    source += '(= (product $x $y) (apply * $x $y)) '
    _, obs = gate.run('captured algebra callable', source +
        f'!(select 1 {control()} ' + read('(weight:where V 6)', '(algebra 0 1 + product)') + ')')
    assert contribution(obs[-1]['values'][0]) == ['V', 6, [6]]
    source = '(= (effect-product $x $y) (let $_ (println! "INTERPRET-FOLD-EFFECT") (* $x $y))) '
    _, obs = gate.run('ordinary computed interpretation retains its effect', source +
        f'!(select 1 {control()} ' + read('(weight:where V 6)', '(algebra 0 1 + effect-product)') + ')')
    assert contribution(obs[-1]['values'][0]) == ['V', 6, [6]]
    assert gate.receipts[-1]['stdout'].splitlines().count('INTERPRET-FOLD-EFFECT') == 1
    gate.passed_case('ordinary interpretation effect executes once; no algebra-law qualification is inferred')
    source = '(= (coefficient-effect) (let $_ (println! "FORBIDDEN-COEFFICIENT-EFFECT") 6)) '
    _, obs = gate.run('semantic coefficient cannot grant effect authority', source +
        f'!(select 1 {control()} (weight:where V (coefficient-effect)))')
    assert obs[-1]['status'] == 'fault' and obs[-1]['values'] == [], obs
    assert 'FORBIDDEN-COEFFICIENT-EFFECT' not in gate.receipts[-1]['stdout']
    gate.passed_case('pure coefficient producer refuses an effect before authorizing its RHS')


def registry_scopes(gate):
    for budget in (0, 1, 12):
        for case, initial, change, invalidated in (
            ('replacement', '!(bind! &coefficient 2)', '!(bind! &coefficient 9)', True),
            ('initial absence', '', '!(bind! &coefficient 9)', True),
            ('same value', '!(bind! &coefficient 2)', '!(bind! &coefficient 2)', False),
            ('unrelated name', '!(bind! &coefficient 2)', '!(bind! &unrelated 9)', False),
        ):
            source = initial + '\n(= (scope-route) (weight:where V (+ &coefficient 0)))\n'
            source += ('!(bind! &saved (let (Observation $p (satisfied $s) (closed $c) '
                       '(residual $r) (work $w) (status $t)) '
                       f'(select 1 {control(budget)} {read("(scope-route)")}) $r))\n')
            source += change + '\n'
            source += f'!(select 1 {control(1000, extra="(receipts True)")} &saved)\n'
            _, obs = gate.run(f'registry {case} after {budget} quanta', source)
            result = obs[-1]
            if invalidated:
                assert result['status'] == 'invalidated' and result['values'] == [], result
                assert result['satisfied'] == result['closed'] == 'False', result
                receipt = result['scope-revisions'][0]
                expected = 'None' if not initial else ['Some', 2]
                assert receipt == ['StaleRegistryBinding', '&coefficient', expected, ['Some', 9]], result
            else:
                assert result['satisfied'] == 'True', result
                assert contribution(result['values'][0]) == ['V', 2, [2]], result
                assert 'scope-revisions' not in result, result
            gate.passed_case(f'{case} retains lookup authority across {budget}+1000 split')


def ordinal_priorities(gate):
    policy = control(10000, mode='WO',
        extra='(order ordinal-cnf) (key (Answer $id $key) $key) (age 62)')
    source = ('(= (routes) (Answer high (Ordinal ((2 1))))) '
              '(= (routes) (Answer low (Ordinal ((0 3))))) '
              '(= (routes) (Answer middle (Ordinal ((1 1))))) ')
    _, obs = gate.run('finite ordinal priority', source + f'!(select 7 {policy} (routes))')
    assert [x[1] for x in obs[-1]['values']] == ['low', 'middle', 'high']
    assert obs[-1]['closed'] == 'True'
    gate.passed_case('normalized CNF priority orders 3 below omega below omega squared')
    source = ('(= (routes) (spin)) (= (spin) (spin)) '
              '(= (routes) (Answer finite (Ordinal ((1 1))))) ')
    policy = control(10000, mode='WO',
        extra='(order ordinal-cnf) (key (Answer $id $key) $key) (age 3)')
    _, obs = gate.run('ordinal protected service', source + f'!(select 1 {policy} (routes))')
    assert obs[-1]['satisfied'] == 'True' and obs[-1]['closed'] == 'False'
    assert obs[-1]['values'][0][1] == 'finite'
    gate.passed_case('protected service survives regenerating zero keys below omega')
    source = ('(= (routes) (Answer malformed (Ordinal ((0 0))))) '
              '(= (routes) (Answer valid (Ordinal ((1 1))))) ')
    _, obs = gate.run('malformed ordinal advice', source + f'!(select 7 {policy} (routes))')
    assert {x[1] for x in obs[-1]['values']} == {'malformed', 'valid'}
    assert obs[-1]['closed'] == 'True'
    gate.passed_case('malformed ordinal advice cannot prune an authorized result')


def real_circuits(gate):
    prefix = ('!(import! &self lib/native_weight_algebras.metta)\n'
              '!(import! &self lib/probability/finite_bayes.metta)\n'
              '!(import! &self lib/native_weight_reals.metta)\n'
              '!(pragma! prefer-rationals True)\n')
    trees, _ = gate.run('exact rational carrier guard', prefix +
        '!(weight:exact-rational? 2) !(weight:exact-rational? 2.5) '
        '!(weight:exact-rational? (/ 1 2))')
    assert trees[-3:] == [['True'], ['False'], ['True']], trees
    gate.passed_case('native carrier check distinguishes exact rationals from floating inputs')
    trees, _ = gate.run('real circuit coarse enclosure', prefix +
        '!(real:compare (RealLog 2) (RealQ 7/10) 0)')
    assert trees[-1][0][0] == 'RealUnresolved', trees
    trees, _ = gate.run('real circuit refined enclosure', prefix +
        '!(real:refine (real:compare (RealLog 2) (RealQ 7/10) 0) 4)')
    assert trees[-1] == ['Less'], trees
    gate.passed_case('overlapping real enclosures remain unresolved until refinement separates them')
    trees, _ = gate.run('exact real equality and float refusal', prefix +
        '!(real:compare (RealPlus (RealQ 1/3) (RealQ 2/3)) (RealQ 1) 0)\n'
        '!(real:compare (RealQ 0.7) (RealQ 7/10) 4)')
    assert trees[-2] == ['Equal'] and trees[-1][0][0] == 'RealInvalidInput', trees
    gate.passed_case('real equality is certified on singleton rational intervals; floats gain no certificate')


def search_clients(gate):
    _, obs = gate.run_file('weighted seven-route clients',
        authored_fixture(gate, 'seven_routes.metta'))
    assert len(obs) == 3 and all(x['satisfied'] == 'True' for x in obs), obs
    unpacked = [[contribution(x) for x in result['values']] for result in obs]
    assert bag(x[0][1] for x in unpacked[0]) == bag(list('abcdefg')), unpacked
    assert [x[0][1] for x in unpacked[1]] == list('bcdefgh'), unpacked
    assert [x[0][1] for x in unpacked[2]] == list('bcdefgh'), unpacked
    assert unpacked[2][1][1:] == [0, [0]], unpacked
    assert obs[1]['closed'] == 'True'
    gate.passed_case('any-k, best-k and joint coverage retain weighted occurrences and required zero contribution')
    source = '(= (product $a $b) (+ $a $b)) (= (product $a $b) (* $a $b)) '
    trees, _ = gate.run('nondeterministic algebra callback', source +
        '!(weight:read (algebra 0 1 + product) (weight:where V 2))')
    assert bag(contribution(x) for tree in trees for x in tree) == bag([
        ['V', 3, [2]], ['V', 2, [2]]]), trees
    gate.passed_case('nondeterministic interpretation retains separate callback worlds')
    trees, _ = gate.run('captured algebra multiplication callback',
        '!(let $product * (weight:read (algebra 0 1 + $product) (weight:where V 2)))')
    assert [contribution(x) for tree in trees for x in tree] == [['V', 2, [2]]], trees
    gate.passed_case('a captured multiplication callable is invoked before its returned value is observed')
    source = '(= (product $a $b) (weight:where (* $a $b) 7)) '
    trees, _ = gate.run('readout callback contribution snapshot', source +
        '!(weight:read (algebra 0 1 + *) '
        '(weight:read (algebra 0 1 + product) (weight:where V 3)))')
    outer = contribution(trees[-1][0])
    assert contribution(outer[0]) == ['V', 3, [3]] and outer[1:] == [7, [7]], trees
    gate.passed_case('readout handles its source snapshot and leaves callback contributions to the enclosing read')


def scoped_reads(gate):
    """A handler owns new productions, preserving the shared operational world."""
    language = getattr(gate, 'language', 'prime')
    algebra = '(algebra 0 1 + *)'
    coins = '(=w (coin) H 2) (=w (coin) H 2) (=w (coin) T 5) (=w (bias) up 3) '
    choices = [('H', 2), ('H', 2), ('T', 5)]
    def weighted(value, coefficient, factors):
        return ['Weighted', value, coefficient, factors]
    def meaning(value):
        if not isinstance(value, list):
            return value
        if value and value[0] == 'Weighted':
            subject, coefficient, factors = contribution(value)
            return weighted(meaning(subject), coefficient, factors)
        return [meaning(item) for item in value]
    def nested(body):
        return f'(weight:read {algebra} {body})'
    cases = [
        ('independent nested read', coins,
         nested(f'(Pair {nested("(coin)")} (bias))'),
         [weighted(['Pair', weighted(v, c, [c]), 'up'], 3, [3]) for v, c in choices]),
        ('shared argument first demanded by inner read',
         coins + f'(= (inside $x) (let $seen {nested("$x")} (Pair $seen $x))) ',
         nested('(inside (coin))'),
         [weighted(['Pair', weighted(v, c if language == 'prime' else 1,
             [c] if language == 'prime' else []), v],
             1 if language == 'prime' else c, [] if language == 'prime' else [c]) for v, c in choices]),
        ('shared argument first demanded outside inner read',
         coins + f'(= (outside $x) (let $seen $x (Pair $seen {nested("$x")}))) ',
         nested('(outside (coin))'),
         [weighted(['Pair', v, weighted(v, 1, [])], c, [c]) for v, c in choices]),
        ('two reads of one shared argument',
         coins + f'(= (twice $x) (Pair {nested("$x")} {nested("$x")})) ',
         nested('(twice (coin))'),
         [weighted(['Pair', weighted(v, c if language == 'prime' else 1,
             [c] if language == 'prime' else []), weighted(v, 1, [])],
             1 if language == 'prime' else c, [] if language == 'prime' else [c]) for v, c in choices]),
        ('incoming prefix stays with outer read',
         '(=w (inner) V 2) (=w (outer) (weight:read (algebra 100 0 min +) (inner)) 7) ',
         nested('(outer)'), [weighted(weighted('V', 2, [2]), 7, [7])]),
        ('distinct algebra handlers',
         '(=w (inner) (leaf) 2) (=w (leaf) V 3) (=w (bias) up 7) ',
         nested('(Pair (weight:read (algebra 100 0 min +) (inner)) (bias))'),
         [weighted(['Pair', weighted('V', 5, [2, 3]), 'up'], 7, [7])]),
        ('interpretation callback factors stay outside source snapshot',
         '(= (product $a $b) (weight:where (* $a $b) 7)) ',
         nested('(weight:read (algebra 0 1 + product) (weight:where V 3))'),
         [weighted(weighted('V', 3, [3]), 7, [7])]),
        ('explicit coefficient reattachment', coins,
         nested(f'(let (Weighted $v $w $p) {nested("(coin)")} (weight:where $v $w))'),
         [weighted(v, c, [c]) for v, c in choices]),
        ('inner zero preserves outer effects',
         '(=w (inner) (let $_ (println! INNER-ZERO) V) 0) (=w (bias) up 3) ',
         nested(f'(Pair {nested("(inner)")} (bias))'),
         [weighted(['Pair', weighted('V', 0, [0]), 'up'], 3, [3])]),
        ('suspended inner coefficient',
         '(= (later $n $v) (if (== $n 0) $v (later (- $n 1) $v))) '
         '(=w (inner) V (later 4 2)) (=w (bias) up 3) ',
         nested(f'(Pair {nested("(inner)")} (bias))'),
         [weighted(['Pair', weighted('V', 2, [2]), 'up'], 3, [3])]),
    ]
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for name, source, expression, expected in cases:
        trees, _ = gate.run(name + ' ordinary', source + '!' + expression)
        ordinary = [value for tree in trees for value in tree]
        assert bag(meaning(x) for x in ordinary) == bag(expected), (name, ordinary, expected)
        for width in (1, 2):
            extra = f'(workers {width})'
            _, whole = gate.run(name + f' whole/{width}', source +
                f'!(select 7 {control(2000, extra=extra)} {expression})')
            final = whole[-1]
            assert final['closed'] == 'True', (name, width, final)
            assert bag(meaning(x) for x in final['values']) == bag(expected), (name, final, expected)
            # Distinct schedules can spend different physical work. A fixed
            # controller and width must retain the exact paid split state.
            for cut in (0, 1, 19, 71, 193, 389):
                trees, parts = gate.run(name + f' cut{cut}/{width}', source +
                    f'!(let {pattern} (select 7 {control(cut, extra=extra)} {expression}) '
                    f'(Split $p $r (select 7 {control(2000-cut, extra=extra)} $r)))')
                split = trees[-1][0]
                values = split[1] + parts[-1]['values']
                assert provenance_packet(values) == provenance_packet(final['values']), (name, width, cut, values, final)
                assert parts[-1]['residual'] == split[2], (name, width, cut, split, parts)
                for field in ('work', 'closed', 'satisfied', 'status'):
                    assert parts[-1][field] == final[field], (name, width, cut, field, parts, final)
                if name == 'inner zero preserves outer effects':
                    assert gate.receipts[-1]['stdout'].splitlines().count('INNER-ZERO') == 1
        gate.passed_case(name + ': scoped factors, actual host demand and complete split packet')
    source = '(= (verdict $x) Unknown) (=w (blocked) V 2 (admit verdict)) '
    expression = nested(nested('(blocked)'))
    for width in (1, 2):
        extra = f'(workers {width})'
        trees, parts = gate.run('parked inner handler retains outer read', source +
            f'!(let {pattern} (select 7 {control(2000, extra=extra)} {expression}) '
            f'(Parked $p $r $w (select 7 {control(0, extra=extra)} $r) '
            f'(select 7 {control(2000, extra=extra)} $r)))')
        parked = trees[-1][0]
        assert parked[1] == [] and len(parts) == 2, (parked, parts)
        for part in parts:
            assert part['values'] == [] and part['work'] == parked[3], (parked, part)
            assert part['residual'] == parked[2] and part['status'] == 'suspended', part
            assert part['closed'] == 'False', part
    gate.passed_case('parked inner verdict keeps both handlers without polling or false closure')
    trees, _ = gate.run_file('nested handlers ordinary file',
        authored_fixture(gate, 'nested_reads.metta'))
    actual = [meaning(value) for tree in trees for value in tree]
    expected = cases[0][3] + [weighted(['Pair', weighted('End', 5, [2, 3]), 'up'], 3, [3])]
    expected += [weighted(v, c, [c]) for v, c in choices]
    assert bag(actual) == bag(expected), (actual, expected)
    gate.passed_case('ordinary nested-read file composes declared algebras and explicit reattachment')


def weight_order_record(observation):
    """The weight-order certificate: direction, pruning verdict and stop."""
    record = next(item for item in observation['_fields'] if item[0] == 'weight-order')
    fields = {item[0]: item[1] for item in record[1:]}
    return fields['direction'], fields['pruning'], fields['stop']


def weight_observations(trees):
    for tree in trees:
        for value in tree:
            if isinstance(value, list) and value and value[0] == 'Observation':
                yield {'values': value[1], '_fields': value[2:],
                       **{item[0]: item[1] for item in value[2:]}}


def weight_licence_progress(gate):
    """Analysis retains its syntax and canonical inner matcher across grants."""
    def analysis(result):
        order = next(x for x in result['_fields'] if x[0] == 'weight-order')
        record = next(x for x in order[1:] if x[0] == 'analysis')
        return {x[0]: x[1] for x in record[1:]}
    def nested(value):
        for _ in range(48):
            value = f'(N {value})'
        return value
    source = f'(Road {nested("7")} 1) (Road {nested("7")} 1) '
    body = read(f'(match &self (Road {nested("$x")} $w) $x (where $w))')
    def policy(budget, width):
        return control(budget, 'WO', f'(workers {width}) (retain True) (demand all) '
            '(order by-weight descending) (law unit-interval-product)')
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) $order)'
    for width in (1, 2):
        trees, _ = gate.run(f'licence retained inner matcher width{width}', source +
            f'!(select 0 {policy(1500, width)} {body})')
        whole = list(weight_observations(trees))[-1]
        assert [contribution(v)[:2] for v in whole['values']] == [[7, 1], [7, 1]], whole
        assert whole['closed'] == 'True' and whole['status'] == 'closed', whole
        complete = analysis(whole)
        assert complete['generation'] == 1 and complete['steps'] > 0, complete
        assert complete['poll-cost'] == 'PhaseNotCpuBound', complete
        assert complete['observation'] == 'ExhaustiveNeedsNoLicence', complete
        for cut in (0, 1, 7, 31, 127, 257):
            trees, parts = gate.run(f'licence retained inner matcher width{width} cut{cut}', source +
                f'!(let {pattern} (select 0 {policy(cut, width)} {body}) '
                f'(Split $p (select 0 {policy(1500-cut, width)} $r)))')
            joined = trees[-1][0][1] + parts[-1]['values']
            assert [contribution(v)[:2] for v in joined] == [[7, 1], [7, 1]], joined
            resumed = list(weight_observations([[trees[-1][0][2]]]))[-1]
            assert analysis(resumed) == complete, (cut, analysis(resumed), complete)
            for field in ('work', 'closed', 'status', 'satisfied'):
                assert parts[-1][field] == whole[field], (width, cut, field, parts[-1], whole)
        gate.passed_case(f'licence phases, inner matcher and duplicate rows retain exact splits width{width}')


def weight_order(gate):
    """Search by the accumulated coefficient of a derivation."""
    trees, _ = gate.run_file('weighted chainer by accumulated weight', authored_fixture(gate, 'chainer_by_weight.metta'))
    fifo, greedy, licensed, unlicensed, keyed, best_all, fifo_all = list(weight_observations(trees))
    def first(observation):
        value, coefficient, _ = contribution(observation['values'][0])
        return value[1], Fraction(str(coefficient))
    assert first(fifo) == ('dew', Fraction(1, 40)), fifo
    assert first(greedy) == ('hose', Fraction(99, 200)), greedy
    gate.passed_case('arrival order and a key on one step factor both miss the best derivation')
    assert first(licensed) == ('rain', Fraction(27, 50)), licensed
    direction, pruning, stop = weight_order_record(licensed)
    assert direction == 'descending' and pruning == ['Licensed', 'unit-interval-product'], licensed
    assert stop[0] == 'Early' and licensed['closed'] == 'False', stop
    frontier, selected = Fraction(str(stop[1][1])), Fraction(str(stop[2][1]))
    assert selected == Fraction(27, 50) and frontier <= selected, stop
    gate.passed_case('best-first by accumulated weight returns the best derivation first, with its stopping bound')
    assert first(unlicensed) == ('rain', Fraction(27, 50)), unlicensed
    _, pruning, stop = weight_order_record(unlicensed)
    assert pruning == ['Refused', 'NoDeclaredLaw'] and stop == 'Exhausted', unlicensed
    assert unlicensed['closed'] == 'True'
    exhaustive = fifo_all['work']
    assert licensed['work'] * 2 < exhaustive and unlicensed['work'] >= exhaustive - 100, (
        licensed['work'], unlicensed['work'], exhaustive)
    gate.passed_case('without a declared law the same order enumerates every derivation and says why')
    assert first(keyed) == ('rain', Fraction(27, 50)) and keyed['work'] < exhaustive, keyed
    gate.passed_case('a key over the authored body reads the accumulated coefficient')
    ordered = [Fraction(str(contribution(x)[1])) for x in best_all['values']]
    assert ordered == sorted(ordered, reverse=True), best_all
    assert bag([contribution(x)[:2] for x in best_all['values']]) == \
        bag([contribution(x)[:2] for x in fifo_all['values']]), (best_all, fifo_all)
    assert weight_order_record(best_all)[2] == 'Exhausted'
    gate.passed_case('run to completion, weight order returns the same answer bag, best first')

    trees, _ = gate.run_file('cheapest routes on a cyclic map by weight', authored_fixture(gate, 'routes_by_weight.metta'))
    cheapest, three, unlicensed, broken = list(weight_observations(trees))
    assert [contribution(x)[1] for x in cheapest['values']] == [3], cheapest
    _, pruning, stop = weight_order_record(cheapest)
    assert pruning == ['Licensed', 'nonnegative-sum'] and stop[0] == 'Early', cheapest
    assert [contribution(x)[1] for x in three['values']] == [3, 5, 5], three
    gate.passed_case('min-plus routes stop early on a cyclic map once no pending route can be cheaper')
    assert unlicensed['values'] == [] and unlicensed['status'] == 'suspended', unlicensed
    assert weight_order_record(unlicensed)[1:] == (['Refused', 'NoDeclaredLaw'], 'Pending')
    assert weight_order_record(broken)[1] == ['Refused', ['CoefficientBreaksLaw', -1]], broken
    assert broken['status'] == 'suspended', broken
    gate.passed_case('without the law a cyclic map stays open; a negative road is refused, not trusted')

    trees, _ = gate.run_file('a coefficient above one breaks the law', authored_fixture(gate, 'law_breaking.metta'))
    breaking, misfit = list(weight_observations(trees))
    assert [contribution(x)[:2] for x in breaking['values']] == [
        [['Via', 'sprinkler', ['Seen', 'timer']], 2]], breaking
    assert weight_order_record(breaking)[1:] == (
        ['Refused', ['CoefficientBreaksLaw', 20]], 'Exhausted'), breaking
    assert weight_order_record(misfit)[1][0] == 'Refused' and \
        weight_order_record(misfit)[1][1][0] == 'LawDoesNotFitAlgebra', misfit
    gate.passed_case('a law the source breaks or the algebra does not fit is refused; the true best is still found')

    for policy in ('(order by-weight sideways)', '(key A 1) (order by-weight descending)',
                   '(law superior)', '(order by-weight descending) (best A 1)'):
        trees, _ = gate.run(f'malformed weight order {policy}',
            f'!(select 1 (search-policy (control WO {policy})) A)')
        assert trees[-1][0][0] == 'Error', trees
    trees, _ = gate.run('descending order needs a numeric key order',
        '!(select 1 (search-policy (control PO (order descending))) A)')
    assert trees[-1][0][0] == 'Error', trees
    gate.passed_case('a law needs weight order, and weight order owns its key and selection')


def weight_licence_dependency_cases(language):
    """Programs whose delayed dependency makes Hidden the actual best."""
    tenth, half = ('0.1', '0.5') if language == 'petta' else ('1/10', '1/2')
    slow = '(= (licence-slow $n $v) (if (== $n 0) $v (licence-slow (- $n 1) $v))) '
    predicate = '(= (licence-predicate $w) (licence-slow 120 (licence-verdict))) '
    predicate += '(=w (licence-verdict) True 20) '
    definitions = [
        ('authored predicate', predicate +
         f'(=w (licence-answer) Hidden {tenth} (admit licence-predicate)) ', 'UncheckedAdmission'),
        ('trailing predicate', predicate +
         f'(= (licence-answer) Hidden (where {tenth} (admit licence-predicate))) ', 'UncheckedAdmission'),
        ('explicit predicate', predicate +
         f'(= (licence-answer) (weight:where Hidden {tenth} (admit licence-predicate))) ', 'UncheckedAdmission'),
        ('matched predicate', predicate + '(licence-row A) ' +
         f'(= (licence-answer) (match &self (licence-row $x) Hidden '
         f'(where {tenth} (admit licence-predicate)))) ', 'UncheckedAdmission'),
        ('matched executable body', '(stored-computation (weight:where Hidden 20)) ' +
         f'(=w (licence-answer) (licence-slow 120 '
         f'(match &self (stored-computation $code) $code)) {tenth}) ', 'UncheckedMatchedValue'),
        ('graded matched executable body',
         f'(stored-computation (weight:where Hidden 40) {half}) ' +
         f'(=w (licence-answer) (licence-slow 120 '
         f'(match &self (stored-computation $code $w) $code (where $w))) {tenth}) ',
         'UncheckedMatchedValue'),
    ]
    return slow, tenth, definitions


def weight_licence_dependencies(gate):
    """An unanalysed executable dependency cannot license an early best."""
    slow, tenth, definitions = weight_licence_dependency_cases(getattr(gate, 'language', 'prime'))
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) $order)'
    def policy(budget, width, law=True):
        extra = f'(workers {width}) (order by-weight descending)'
        if law: extra += ' (law unit-interval-product)'
        return control(budget, 'WO', extra)
    def best(result):
        return [[contribution(v)[0], Fraction(str(contribution(v)[1]))] for v in result['values']]
    for name, definition, reason in definitions:
        source = slow + definition + '(=w (licence-answer) Visible 1) '
        for width in (1, 2):
            actual = []
            for law in (True, False):
                trees, _ = gate.run(name + f' width{width} law{law}', source +
                    f'!(select 1 {policy(10000, width, law)} {read("(licence-answer)")})')
                result = list(weight_observations(trees))[-1]
                assert best(result) == [['Hidden', 2]] and result['closed'] == 'True', result
                if law:
                    _, pruning, stop = weight_order_record(result)
                    assert pruning[0] == 'Refused' and pruning[1][0] == reason, result
                    assert stop == 'Exhausted', result
                actual.append(result)
            for cut in (0, 1, 31, 127):
                trees, parts = gate.run(name + f' width{width} cut{cut}', source +
                    f'!(let {pattern} (select 1 {policy(cut, width)} {read("(licence-answer)")}) '
                    f'(Split $p (select 1 {policy(10000-cut, width)} $r)))')
                combined = {**parts[-1], 'values': trees[-1][0][1] + parts[-1]['values']}
                assert best(combined) == best(actual[0]), (name, cut, combined, actual)
                for field in ('work', 'closed', 'status', 'satisfied'):
                    assert combined[field] == actual[0][field], (name, cut, field, combined, actual)
            gate.passed_case(name + f' width{width}: refusal retains the true best and exact accepted-work splits')

    # Duplicate matched code is still two derivations after licence refusal.
    source = slow + f'(stored-computation (weight:where Hidden 20)) ' * 2
    source += f'(=w (licence-answer) (match &self (stored-computation $code) $code) {tenth}) '
    source += '(=w (licence-answer) Visible 1) '
    for width in (1, 2):
        trees, _ = gate.run('duplicate matched code', source +
            f'!(select 7 {policy(10000, width)} {read("(licence-answer)")})')
        result = list(weight_observations(trees))[-1]
        assert sorted(best(result)) == [['Hidden', 2], ['Hidden', 2], ['Visible', 1]], result
        assert result['closed'] == 'True' and result['satisfied'] == 'False', result
        hidden = [v for v in result['values'] if contribution(v)[0] == 'Hidden']
        assert len({v[3][1][-1][1] for v in hidden}) == 2, hidden
    gate.passed_case('unchecked matched code retains duplicate physical derivations')

    for kind in ('open', 'empty'):
        prefix = '!(bind! &licence-space (new-space)) !(bind! &unrelated-space (new-space)) '
        if getattr(gate, 'language', 'prime') == 'petta':
            # The first add realizes a named location; removing its sole row
            # leaves an actual registered empty Space for the cursor contract.
            prefix = ('!(add-atom &licence-space LicenceSetup) '
                      '!(remove-atom &licence-space LicenceSetup) '
                      '!(add-atom &unrelated-space LicenceSetup) '
                      '!(remove-atom &unrelated-space LicenceSetup) ')
        if kind == 'open': prefix += '!(add-atom &licence-space (stored-computation $code)) '
        source = prefix + slow
        source += f'(=w (licence-answer) (licence-slow 120 '
        source += f'(match &licence-space (stored-computation $code) $code)) {tenth}) '
        source += '(=w (licence-answer) Visible 1) '
        captured = getattr(gate, 'language', 'prime') != 'prime'
        for mutation, status in [
            ('(add-atom &licence-space (stored-computation (weight:where Hidden 20)))',
             'invalidated' if captured else 'suspended'),
            ('(add-atom &unrelated-space Irrelevant)', 'suspended')]:
            trees, parts = gate.run(kind + ' unfinished query licence ' + mutation, source +
                f'!(let {pattern} (select 1 {policy(20, 1)} {read("(licence-answer)")}) '
                f'(let $_ {mutation} (select 1 {policy(0, 1)} $r)))')
            result = parts[-1]
            assert result['status'] == status and result['closed'] == 'False', result
            assert result['work'] == 20 and not result['values'], result
            # The hosts reach different phases at this same accepted cut:
            # Prime still has unread syntax; HE/PeTTa captured the query.
            # Neither prefix licenses pruning. A captured read invalidates
            # even before its first matcher poll or returned result.
            order = next(x for x in list(weight_observations(trees))[-1]['_fields']
                         if x[0] == 'weight-order')
            assert next(x[1] for x in order[1:] if x[0] == 'pruning') == ['Pending', 'Analysis'], order
            record = next(x for x in order[1:] if x[0] == 'analysis')
            fields = {x[0]: x[1] for x in record[1:]}
            assert fields['status'] == ('Invalidated' if status == 'invalidated' else 'Pending'), fields
            assert fields['query-service'] == ('NativeNodeCursor' if captured else 'NoQueryCaptured'), fields
        gate.passed_case(kind + ' query distinguishes captured authority from unread syntax at cut20')


    if getattr(gate, 'language', 'prime') == 'petta':
        # A missing logical location is empty, but has no physical query cursor.
        # bind! accepts new-state in this dialect and does not create a Space.
        source = '!(bind! &licence-absent (new-space)) ' + slow
        source += f'(=w (licence-answer) (licence-slow 120 '
        source += f'(match &licence-absent (stored-computation $code) $code)) {tenth}) '
        source += '(=w (licence-answer) Visible 1) '
        for mutation, status in [
            ('(add-atom &licence-absent (stored-computation (weight:where Hidden 20)))', 'invalidated'),
            ('(add-atom &unrelated-absent Irrelevant)', 'suspended')]:
            trees, parts = gate.run('absent-location licence ' + mutation, source +
                f'!(let {pattern} (select 1 {policy(20, 1)} {read("(licence-answer)")}) '
                f'(let $_ {mutation} (select 1 {policy(0, 1)} $r)))')
            result = parts[-1]
            assert result['status'] == status and result['closed'] == 'False', result
            assert result['work'] == 20 and not result['values'], result
            observed = list(weight_observations(trees))[-1]
            assert weight_order_record(observed)[1] == [
                'Refused', ['UnresolvedCoefficientSpace', '&licence-absent']], observed
            order = next(x for x in observed['_fields'] if x[0] == 'weight-order')
            record = next(x for x in order[1:] if x[0] == 'analysis')
            fields = {x[0]: x[1] for x in record[1:]}
            assert fields['query-service'] == 'NoQueryCaptured', fields
        gate.passed_case('absent location retains registry absence without inventing a query cursor')



def imported_coefficients(gate):
    """Own and imported physical rows preserve one shared graded producer."""
    module = 'tests/prime/weights/imported_grades.metta'
    source = f'(=w (import-choice) 2 3) !(import! &self {module}) '
    body = '(import-pair (weigh-import (import-choice)))'
    expected = [[['Pair', 2, 2], 6, [3, 2]],
                [['Pair', 2, 2], 6, [3, 2]],
                [['Pair', 5, 5], 35, [7, 5]]]
    trees, _ = gate.run('imported shared grading ordinary', source + f'!{read(body)}')
    actual = [contribution(x) for tree in trees for x in tree
              if isinstance(x, list) and x and x[0] == 'Weighted']
    assert bag(actual) == bag(expected), (actual, expected, trees)
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for workers in (1, 2):
        extra = f'(workers {workers})'
        _, whole = gate.run(f'imported shared grading workers{workers}', source +
            f'!(select 7 {control(3000, extra=extra)} {read(body)})')
        result = whole[-1]
        assert result['closed'] == 'True' and result['satisfied'] == 'False', result
        assert bag([contribution(x) for x in result['values']]) == bag(expected), result
        for cut in (0, 1, 19, 71, 193):
            trees, parts = gate.run(f'imported shared grading split{workers}/{cut}', source +
                f'!(let {pattern} (select 7 {control(cut, extra=extra)} {read(body)}) '
                f'(Split $p (select 7 {control(3000-cut, extra=extra)} $r)))')
            values = trees[-1][0][1] + parts[-1]['values']
            assert [contribution(x) for x in values] == [
                contribution(x) for x in result['values']], (workers, cut, values, result)
            for field in ('work', 'closed', 'satisfied', 'status'):
                assert parts[-1][field] == result[field], (workers, cut, field, parts, result)
        gate.passed_case(f'imported duplicate grades and shared body retain full splits workers{workers}')
    for workers in (1, 2):
        trees, _ = gate.run(f'imported coefficient law check workers{workers}', source +
            f'!(select 1 {control(3000, "WO", f"(workers {workers}) (order by-weight descending) (law unit-interval-product)")} '
            f'{read("(match &self (import-fact $w) Answer (where $w))")})')
        checked = list(weight_observations(trees))
        assert weight_order_record(checked[-1])[1] == [
            'Refused', ['CoefficientBreaksLaw', 2]], checked
        assert checked[-1]['closed'] == 'True', checked
        assert [contribution(x) for x in checked[-1]['values']] == [['Answer', 2, [2]]], checked
    gate.passed_case('imported matched coefficients participate in pruning-law admission')
    if getattr(gate, 'language', 'prime') == 'he':
        # HE aliases the same live imported module. The waiting grade keeps
        # the complete query read even when a later mutation has equal data.
        source += f'!(import! &weight-module {module}) !(bind! &unrelated (new-space)) '
        pending = read('(match &self (import-fact $x) $x (where (import-wait)))')
        for mutation, status in [
            ('(add-atom &weight-module (import-fact 2))', 'invalidated'),
            ('(remove-atom &weight-module (import-fact 2))', 'invalidated'),
            ('(add-atom &unrelated (import-fact 2))', 'suspended'),
        ]:
            _, parts = gate.run('imported pending grade read authority ' + mutation, source +
                f'!(let {pattern} (select 7 {control(100)} {pending}) '
                f'(let $_ {mutation} (select 7 {control(0)} $r)))')
            assert parts[-1]['values'] == [] and parts[-1]['closed'] == 'False', parts
            assert parts[-1]['status'] == status, parts
        gate.passed_case('live imported pending grade rejects source changes and retains unrelated writes')


def weight_handler_authority(gate):
    """A computed descriptor cannot lend an inner read's law to its caller."""
    source = '''
        (= (slow $n $v) (if (== $n 0) $v (slow (- $n 1) $v)))
        (=w (paths) (slow 10 (leaf)) 1)
        (=w (paths) B 1)
        (=w (leaf) A 1)
        (= (make-algebra)
           (let (Weighted $v $w $p)
                (weight:read (algebra 0 1 + *) (paths))
                (algebra 100 0 min +)))
    '''
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) $certificate)'
    for workers in (1, 2):
        policy = f'(order by-weight descending) (workers {workers})'
        body = '(weight:read (make-algebra) (paths))'
        complete = None
        for law in ('', '(law unit-interval-product)'):
            trees, _ = gate.run(f'computed descriptor scope {workers}/{law}', source +
                f'!(select 1 {control(30000, "WO", policy + law)} {body})')
            result = list(weight_observations(trees))[-1]
            assert [contribution(x)[:2] for x in result['values']] == [['A', 2]], result
            assert result['closed'] == 'True' and result['satisfied'] == 'True', result
            assert weight_order_record(result)[1][0] == 'Refused', result
            assert weight_order_record(result)[2] == 'Exhausted', result
            if law:
                complete = result
        gate.passed_case(f'inner product read cannot license outer min-plus read with {workers} workers')
        policy += '(law unit-interval-product)'
        for cut in (0, 1, 19, 71, 193):
            trees, observations = gate.run(f'computed descriptor split {workers}/{cut}', source +
                f'!(let {pattern} (select 1 {control(cut, "WO", policy)} {body}) '
                f'(Split $p (select 1 {control(30000-cut, "WO", policy)} $r)))')
            result = observations[-1]
            values = trees[-1][0][1] + result['values']
            assert [contribution(x) for x in values] == [contribution(x) for x in complete['values']], result
            for key in ('work', 'closed', 'satisfied', 'status'):
                assert result[key] == complete[key], (cut, key, result, complete)
        gate.passed_case(f'computed descriptor and read retain their full split observation with {workers} workers')

        pending = '''
            (= (descriptor) (algebra 0 1 + *))
            (= (descriptor) (descriptor-loop))
            (= (descriptor-loop) (descriptor-loop))
            (=w (candidate) A 1)
        '''
        _, observations = gate.run(f'unfinished descriptor authority {workers}', pending +
            f'!(select 1 {control(1000, "WO", policy)} (weight:read (descriptor) (candidate)))')
        result = observations[-1]
        assert result['values'] == [] and result['closed'] == 'False' and result['satisfied'] == 'False', result
        assert result['status'] == 'suspended', result
        gate.passed_case(f'unfinished outer descriptor remains part of best-answer authority with {workers} workers')


def authored_keys(gate):
    """Keys see the body as authored, the step factor, and matched rows."""
    prefix = ('!(pragma! prefer-rationals True)\n'
              '(= (slow $n $v) (if (== $n 0) $v (slow (- $n 1) $v)))\n')
    weighted = prefix + '(=w (pick) (slow 60 Low) 1/10)\n(=w (pick) (slow 60 High) 9/10)\n'
    def observe(name, source, key):
        _, obs = gate.run(name, source + f'!(select 1 (search-policy (control WO {key} (budget 100000))) '
                          '(weight:read (algebra 0 1 + *) (pick)))')
        return contribution(obs[-1]['values'][0])[0], obs[-1]['work']
    plain, plain_work = observe('weighted bodies, no key', weighted, '')
    body, body_work = observe('key on the authored body', weighted,
                              '(key (slow $n $v) (if (== $v High) 0 1))')
    factor, factor_work = observe('key on the step factor', weighted, '(key $b (- 0 $weight:factor))')
    internal, internal_work = observe('key on the attach wrapper', weighted,
                                      '(key (weight:where $b $c) (- 0 $c))')
    assert plain == 'Low' and body == 'High' and factor == 'High', (plain, body, factor)
    assert body_work < plain_work and factor_work < plain_work, (plain_work, body_work, factor_work)
    assert internal == 'Low' and internal_work == plain_work, (internal, internal_work)
    gate.passed_case('keys match the authored body and the step factor, not the attach wrapper')
    rows = (prefix + '!(bind! &kb (new-space))\n!(add-atom &kb (opt Low 1/10))\n'
            '!(add-atom &kb (opt High 9/10))\n'
            '(= (pick) (match &kb (opt $v $c) (slow 200 $v) (where $c)))\n')
    unkeyed, unkeyed_work = observe('weighted match rows, no key', rows, '')
    keyed, keyed_work = observe('key on a weighted match row', rows, '(key (slow $n Low) 1)')
    ranked, ranked_work = observe('match row coefficient key', rows, '(key $b (- 0 $weight:coefficient))')
    assert unkeyed == 'Low' and keyed == 'High' and ranked == 'High', (unkeyed, keyed, ranked)
    assert keyed_work < unkeyed_work and ranked_work < unkeyed_work
    gate.passed_case('weighted match rows are occurrences that keys can see')

    source = (prefix + '(= (explode) (println! "FORBIDDEN-KEY-BODY")) '
              '(= (pick) (slow 200 (explode))) (= (pick) (slow 1 High)) ')
    _, observations = gate.run('key wildcard leaves an unchosen body unforced', source +
        '!(select 1 (search-policy (control WO (key (slow $n $v) $n) (budget 100000))) '
        '(weight:read (algebra 0 1 + *) (pick)))')
    assert contribution(observations[-1]['values'][0]) == ['High', 1, []], observations
    assert 'FORBIDDEN-KEY-BODY' not in gate.receipts[-1]['stdout'], gate.receipts[-1]
    gate.passed_case('key inspection leaves wildcard source unexecuted')

    source = (prefix + '(= (tick) (let $_ (println! "KEY-SHARED-CELL") 7)) '
              '(= (twice $x) (slow 1 (Pair $x $x))) ')
    _, observations = gate.run('key wildcard preserves shared body cells', source +
        '!(select 1 (search-policy (control WO (key (slow $n $v) $n) (budget 100000))) '
        '(weight:read (algebra 0 1 + *) (twice (tick))))')
    assert contribution(observations[-1]['values'][0]) == [['Pair', 7, 7], 1, []], observations
    assert gate.receipts[-1]['stdout'].splitlines().count('KEY-SHARED-CELL') == 1, gate.receipts[-1]
    gate.passed_case('key inspection preserves one shared body invocation')


def native_handler_boundaries(gate):
    source = '(= (rows) (weighted A 2)) (= (rows) (weighted B 3)) '
    source += '(= (rows) (weighted C 5)) '
    source += '(= (weighted $x $w) (weight:read (algebra 0 1 + *) '
    source += '(weight:where $x $w))) '
    trees, _ = gate.run('ordinary multi-row native handlers', source + '!(rows)')
    ordinary = [contribution(x) for x in trees[-1]]
    _, obs = gate.run('agenda multi-row native handlers', source +
        f'!(select 4 {control()} (rows))')
    expected = [['A', 2, [2]], ['B', 3, [3]], ['C', 5, [5]]]
    assert bag(ordinary) == bag(expected)
    assert bag([contribution(x) for x in obs[-1]['values']]) == bag(expected)
    assert obs[-1]['closed'] == 'True'
    gate.passed_case('all physical RHSs retain native handlers under ordinary and agenda execution')

    source = '(= (loop) (loop)) '
    cases = [
        ('and', 'True', '(weight:where True 3)', ['True', 3, [3]]),
        ('or', 'False', '(weight:where True 5)', ['True', 5, [5]]),
        ('and', 'False', '(weight:where True (loop))', ['False', 1, []]),
        ('or', 'True', '(weight:where False (loop))', ['True', 1, []]),
    ]
    for operator, first, second, expected in cases:
        expression = read(f'({operator} {first} {second})')
        trees, _ = gate.run(f'owned short-circuit {operator} {first}',
                            source + f'!{expression}')
        assert contribution(trees[-1][0]) == expected, trees
    gate.passed_case('Boolean demand retains coefficient worlds and leaves skipped coefficients unforced')

    source = '(= (loop) (loop)) (= (rows) A) (= (rows) B) '
    source += '(= (rows) (select 1 (search-policy (control FIFO (budget 30))) '
    source += '(weight:read (algebra 0 1 + *) (weight:where V (loop))))) '
    trees, obs = gate.run('nested pending handler through physical row', source + '!(rows)')
    assert len(trees[-1]) == 3 and obs[-1]['values'] == []
    assert obs[-1]['closed'] == 'False' and obs[-1]['status'] == 'suspended'
    gate.passed_case('multi-row optimized admission falls back with the pending native consumer intact')



def authored_coefficients(gate):
    def row(style, lhs, rhs, grade):
        if style == 'explicit': return f'(= {lhs} (weight:where {rhs} {grade})) '
        if style == 'equals-w': return f'(=w {lhs} {rhs} {grade}) '
        return f'(= {lhs} {rhs} (where {grade})) '

    cases = [
        ('unit identity', [('(gid $x)', '$x', '1')], '(gid V)', [['V', 1, [1]]]),
        ('literal nested identity', [('(gid $x)', '$x', '2')], '(gid (gid 7))', [[7, 4, [2, 2]]]),
        ('binding-local nested identity', [('(gid $x)', '$x', '$x')], '(gid (gid 7))', [[7, 49, [7, 7]]]),
        ('score forces graded argument', [('(coin)', '2', '3'), ('(gid $x)', '(Pair $x $x)', '$x')],
         '(gid (coin))', [[['Pair', 2, 2], 6, [3, 2]]]),
        ('physical duplicate graded rows', [('(coin)', '2', '3'), ('(coin)', '2', '3'),
                                          ('(coin)', '5', '7'), ('(gid $x)', '(Pair $x $x)', '$x')],
         '(gid (coin))', [[['Pair', 2, 2], 6, [3, 2]], [['Pair', 2, 2], 6, [3, 2]],
                          [['Pair', 5, 5], 35, [7, 5]]]),
        ('same row distinct invocations', [('(gid $x)', '$x', '2')],
         '(Pair (gid 7) (gid 7))', [[['Pair', 7, 7], 4, [2, 2]]]),
    ]
    pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for name, rows, expression, expected in cases:
        reference=None
        for style in ('explicit','equals-w','trailing-where'):
            source=''.join(row(style,*parts) for parts in rows)
            trees,_=gate.run(f'authored {style} {name} ordinary',source+f'!{read(expression)}')
            values=[contribution(x) for tree in trees for x in tree]
            assert bag(values)==bag(expected),(style,name,values,expected)
            for workers in (1,2):
                extra=f'(workers {workers})'
                _,obs=gate.run(f'authored {style} {name} workers={workers}',source+
                    f'!(select 7 {control(2000,extra=extra)} {read(expression)})')
                final=obs[-1]
                actual=[contribution(x) for x in final['values']]
                assert bag(actual)==bag(expected) and final['closed']=='True',(style,name,final)
                if workers==1 and style=='explicit':reference=actual
                if workers==1:assert actual==reference,(style,name,actual,reference)
                for cut in (0,17,73,101):
                    trees,parts=gate.run(f'authored {style} {name} split={workers}/{cut}',source+
                        f'!(let {pattern} (select 7 {control(cut,extra=extra)} {read(expression)}) '
                        f'(Split $p (select 7 {control(2000-cut,extra=extra)} $r)))')
                    combined=trees[-1][0][1]+parts[-1]['values']
                    assert [contribution(x) for x in combined]==actual,(style,name,workers,cut,combined,actual)
                    for field in ('work','closed','satisfied','status'):
                        assert parts[-1][field]==final[field],(style,name,workers,cut,field,parts[-1],final)
            gate.passed_case(f'{style}: {name} retains grade/body/return worlds and splits')

    facts='(edge a b) (edge a b) (edge a c) (edge b d) (edge c d) (= (grade b) 2) (= (grade c) 5) '
    crossing = '{$x}' if getattr(gate, 'language', 'prime') == 'prime' else ''
    first='(match &self (edge a $x) $x (where (grade $x)))'
    expression=f'(let $x {first} (match &self (edge $x $z) $z (where 3)){crossing})'
    explicit='(let $x (match &self (edge a $x) (weight:where $x (grade $x))) '
    explicit+=f'(match &self (edge $x $z) (weight:where $z 3)){crossing})'
    for body in (explicit,expression):
        _,obs=gate.run('authored two-step binding-local match',facts+f'!(select 7 {control(3000)} {read(body)})')
        values=[contribution(x) for x in obs[-1]['values']]
        assert bag(values)==bag([['d',6,[2,3]],['d',6,[2,3]],['d',15,[5,3]]]),obs
        assert obs[-1]['closed']=='True'
    gate.passed_case('trailing match where agrees with explicit attachment through two correlated matches')

    for style in ('equals-w','trailing-where'):
        declaration=row(style,'(gid $x)','$x','2').strip()
        source=f'!(add-atom &self {declaration}) !{read("(gid 7)")} '
        source+=f'!(match &self {declaration} RawRow) !(remove-atom &self {declaration}) '
        source+=f'!{read("(gid 7)")}'
        trees,_=gate.run('dynamic authored '+style,source)
        contributions=[contribution(x) for tree in trees for x in tree
                       if isinstance(x,list) and x and x[0]=='Weighted']
        assert contributions==[[7,2,[2]],[['gid',7],1,[]]],(style,trees)
        assert 'RawRow' in gate.receipts[-1]['stdout'],gate.receipts[-1]
        gate.passed_case(style+': dynamic add/remove preserves authored raw row and changes execution view')

    for style in ('equals-w','trailing-where'):
        source=row(style,'(spin)','(spin)','1')+row(style,'(gid $x)','$x','(spin)')
        _,obs=gate.run('pending authored grade '+style,source+
            f'!(let {pattern} (select 7 {control(37)} {read("(gid 7)")}) '
            f'(select 7 {control(63)} $r))')
        assert obs[-1]['closed']=='False' and obs[-1]['values']==[] and obs[-1]['status']=='suspended',obs
        gate.passed_case(style+': divergent coefficient retains its body and full residual')



def callable_admission(gate):
    language = getattr(gate, 'language', 'prime')
    pattern = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    if language == 'prime':
        recursive = '''
(= ((fix $f) $u) (($f (fix $f)) $u))
(= ((fix $f) $u $v) (($f (fix $f)) $u $v))
(= (and-lazy $p $q) (if $p $q False))
(= (or-lazy $p $q) (if $p True $q))
(= (any-at $pred $individuals)
   (if (== $individuals ()) False
       (let ($w $rest) (decons-atom $individuals)
         (if ($pred $w) True (any-at $pred $rest)))))
(= (parent $a $b)
   (if (== $a ann) (== $b bob)
       (if (== $a bob) (== $b dan) False)))
'''
        function = ('(fix (lam self (lam x (lam z (or-lazy (parent x z) '
                    '(any-at (lam y (and-lazy (parent x y) (self y z))) '
                    '(ann bob dan)))))))')
        for name, expression, expected in (
            ('recursive positive', f'({function} ann dan)', 'True'),
            ('recursive negative', f'({function} dan ann)', 'False'),
            ('typed condition', f'(if ({function} ann dan) yes no)', 'yes'),
            ('captured condition', f'(let $f {function} (if ($f ann dan) yes no))', 'yes'),
        ):
            trees, _ = gate.run('completed head scope: ' + name,
                                recursive + '!' + expression)
            assert trees == [[expected]], (name, trees)
        gate.passed_case('computed-head completion applies only to its original call across recursive tails')
        head_rows = '(= (rows) ((pick) 7)) (= (rows) ((pick) 9)) '
        for name, definitions in (
            ('computed tail', '(= (pick) relay) (= (pick2) done) '
             '(= (relay $x) ((pick2) $x)) (= (done $x) (V $x))'),
            ('lambda tail', '(= (pick) (lam x ((pick2) x))) '
             '(= (pick2) done) (= (done $x) (V $x))'),
            ('effect return', '(= (pick) relay) (= (pick2) done) '
             '(= (relay $x) ((pick2) $x)) '
             '(= (done $x) (let $_ (println! "HEAD-RETURN-EFFECT") (V $x)))'),
        ):
            source = head_rows + definitions
            for width in (1, 2):
                for quantum in (1, 8):
                    extra = f'(workers {width}) (quantum {quantum})'
                    _, parts = gate.run(name + f' whole/{width}/{quantum}', source +
                        f'!(select 7 {control(1000, extra=extra)} (rows))')
                    whole = parts[-1]
                    assert whole['values'] == [['V', 7], ['V', 9]], whole
                    assert whole['status'] == 'closed' and whole['closed'] == 'True', whole
                    if name == 'effect return':
                        assert gate.receipts[-1]['stdout'].splitlines().count('HEAD-RETURN-EFFECT') == 2
                    for cut in (0, 1, 11, 39, 73):
                        trees, parts = gate.run(name + f' split/{width}/{quantum}/{cut}', source +
                            f'!(let {pattern} (select 7 {control(cut, extra=extra)} (rows)) '
                            f'(Split $p (select 7 {control(1000-cut, extra=extra)} $r)))')
                        assert trees[-1][0][1] + parts[-1]['values'] == whole['values'], (name, cut, parts)
                        for field in ('work', 'closed', 'satisfied', 'status'):
                            assert parts[-1][field] == whole[field], (name, cut, field, parts, whole)
                        if name == 'effect return':
                            assert gate.receipts[-1]['stdout'].splitlines().count('HEAD-RETURN-EFFECT') == 2
            gate.passed_case(name + ' preserves current call readiness, ordered results and once-only effects across worker returns')
    literal = read('(weight:where Value 2)')
    source = f'(= (literal-read) {literal}) '
    for body in (literal, '(literal-read)'):
        trees, _ = gate.run('nested literal read preserves held host return ' + body,
            source + f'!(let (Weighted $v $c $p) {body} (Selected $v $c))')
        assert trees == [[['Selected', 'Value', 2]]], trees
    gate.passed_case('literal and defined read plans preserve the actual returned value shape')
    parked = read('(weight:where Claim 2 (admit check))')
    source = '(= (check $w) Maybe) ' + f'(= (parked-read) {parked}) '
    trees, parts = gate.run('defined ordinary parked read retains its caller', source +
        f'!(let {pattern} (parked-read) (Retained $w '
        f'(select 0 {control(0, extra="(demand all)")} $r)))')
    assert len(parts) == 1 and parts[0]['status'] == 'suspended', (trees, parts)
    assert parts[0]['values'] == [] and parts[0]['closed'] == 'False', parts
    assert parts[0]['work'] == trees[-1][0][1], (trees, parts)
    gate.passed_case('defined ordinary parked read exports the original complete session')
    cases = [
        ('named unary', '(= (check $w) True)', 'check', True, [['Claim', 2, [2]]]),
        ('partial callable', '(= (check $x $w) True)', '(check 2)',
         language == 'petta', [['Claim', 2, [2]]] if language == 'petta' else []),
        ('authored higher-order head', '(= ((check $x) $w) True)', '(check 2)',
         language != 'petta', [['Claim', 2, [2]]] if language != 'petta' else []),
        ('computed callable', '(= (check $w) True) (= (pick) check)', '(pick)',
         True, [['Claim', 2, [2]]]),
        ('graded computed callable', '(=w (pick) check 5) (=w (check $w) True 3)',
         '(pick)', True, [['Claim', 30, [5, 3, 2]]]),
        ('named unknown verdict', '(= (check $w) Maybe)', 'check', False, []),
        ('unknown returned callable', '(= (pick) Unknown)', '(pick)', False, []),
        ('parked and duplicate accepted verdicts',
         '(= (check $w) Maybe) (= (check $w) True) (= (check $w) True) (= (check $w) False)',
         'check', False, [['Claim', 2, [2]], ['Claim', 2, [2]]]),
        ('parked and duplicate computed callables',
         '(= (pick) Unknown) (= (pick) check) (= (pick) check) (= (check $w) True)',
         '(pick)', False, [['Claim', 2, [2]], ['Claim', 2, [2]]]),
    ]
    for name, definitions, predicate, closed, expected in cases:
        source = definitions + f' (=w (f) Claim 2 (admit {predicate})) '
        trees, ordinary = gate.run(name+' ordinary admission', source+f'!{read("(f)")}')
        actual = ordinary[-1]['values'] if ordinary else [x for tree in trees for x in tree]
        assert bag([contribution(x) for x in actual]) == bag(expected), (name, trees)
        if closed:
            assert not ordinary, (name, ordinary)
        else:
            assert ordinary[-1]['status'] == 'suspended' and ordinary[-1]['closed'] == 'False', ordinary
        for width in (1, 2):
            extra = f'(workers {width})'
            _, observations = gate.run(name+f' controlled/{width}', source+
                f'!(select 7 {control(1000, extra=extra)} {read("(f)")})')
            whole = observations[-1]
            assert bag([contribution(x) for x in whole['values']]) == bag(expected), whole
            assert whole['closed'] == ('True' if closed else 'False'), whole
            assert whole['status'] == ('closed' if closed else 'suspended'), whole
            for cut in (0, 13, 39, 103):
                trees, parts = gate.run(name+f' split/{width}/{cut}', source+
                    f'!(let {pattern} (select 7 {control(cut, extra=extra)} {read("(f)")}) '
                    f'(Split $p (select 7 {control(1000-cut, extra=extra)} $r)))')
                combined = trees[-1][0][1] + parts[-1]['values']
                assert [contribution(x) for x in combined] == [contribution(x) for x in whole['values']], (name, cut, parts)
                for field in ('work', 'closed', 'satisfied', 'status'):
                    assert parts[-1][field] == whole[field], (name, cut, field, parts, whole)
        gate.passed_case(name+' retains host application, coefficient order and exact split work')
        if not closed:
            zero = f'(select 0 {control(0, extra="(demand all)")} $r)'
            trees, parts = gate.run(name+' ordinary parked session repeated zero resume', source+
                f'!(let {pattern} {read("(f)")} (Zero $p $w {zero} {zero} {zero}))')
            prefix = trees[-1][0][1]
            assert bag([contribution(x) for x in prefix]) == bag(expected), trees
            for part in parts:
                assert part['values'] == [] and part['closed'] == 'False' and part['status'] == 'suspended', part
                assert part['work'] == trees[-1][0][2], (trees, parts)
                assert part['residual'] == ordinary[-1]['residual'], (parts, ordinary)
            assert len(parts) == 3, parts
            gate.passed_case(name+' ordinary residual is retained without polling or duplicate publication')

    definition = '(= (accept-bound $x $w) (== $x $w))' if language == 'petta' else \
        '(= ((accept-bound $x) $w) (== $x $w))'
    source = definition + ' (=w (coin) 2 3) (=w (coin) 2 3) (=w (coin) 5 7) '
    source += '(=w (gid $x) (Pair $x $x) $x (admit (accept-bound $x))) '
    trees, _ = gate.run('compound predicate captures the selected shared argument', source+f'!{read("(gid (coin))")}')
    assert bag([contribution(x) for tree in trees for x in tree]) == bag([
        [['Pair', 2, 2], 6, [3, 2]], [['Pair', 2, 2], 6, [3, 2]],
        [['Pair', 5, 5], 35, [7, 5]]]), trees
    gate.passed_case('compound predicate and body share the chosen argument and physical factors')

    for name, source, predicate in [
        ('computed primitive effect', '(= (pick) println!)', '(pick)'),
        ('head evaluation effect', '(= (check $w) True) (= (pick) (let $_ (println! "FORBIDDEN") check))', '(pick)'),
        ('computed source mutation', '(= (pick) check) (= (check $w) (let $_ (add-atom &self (Leak)) True))', '(pick)'),
    ]:
        source += f' (=w (f) Claim 2 (admit {predicate})) '
        for width in (1, 2):
            _, parts = gate.run(name+f'/{width}', source+
                f'!(select 7 {control(1000, extra=f"(workers {width})")} {read("(f)")}) '
                '!(match &self (Leak) Leaked)')
            assert parts[-1]['values'] == [] and parts[-1]['status'] == 'fault', parts
            output = gate.receipts[-1]['stdout']
            assert 'FORBIDDEN' not in output and 'Leaked' not in output and '2' not in output.splitlines(), output
        gate.passed_case(name+' is refused at the actual resolved operation')

    source = '(= (pick) (pick)) (=w (f) Claim 2 (admit (pick))) '
    for width in (1, 2):
        extra = f'(workers {width})'
        _, whole = gate.run(f'recursive computed predicate head/{width}', source+
            f'!(select 7 {control(1000, extra=extra)} {read("(f)")})')
        assert whole[-1]['values'] == [] and whole[-1]['status'] == 'suspended', whole
        assert whole[-1]['work'] == 1000 and whole[-1]['closed'] == 'False', whole
        for cut in (0, 1, 17, 61):
            _, parts = gate.run(f'recursive computed predicate head split/{width}/{cut}', source+
                f'!(let {pattern} (select 7 {control(cut, extra=extra)} {read("(f)")}) '
                f'(select 7 {control(1000-cut, extra=extra)} $r))')
            assert parts[-1] == whole[-1], (width, cut, parts, whole)
    gate.passed_case('recursive computed head is owned within the actual agenda allowance')

    source = '(= (check $w) Maybe) (=w (f) Claim 2 (admit check)) '
    for changed in (False, True):
        mutation = '(add-atom &self (NewFact))' if changed else '(add-atom (new-space) (NewFact))'
        trees, parts = gate.run(f'ordinary parked session read authority/{changed}', source+
            f'!(let {pattern} {read("(f)")} (let $_ {mutation} '
            f'(Retained $w (select 0 {control(0, extra="(demand all)")} $r))))')
        assert parts[-1]['status'] == ('invalidated' if changed else 'suspended'), parts
        assert parts[-1]['closed'] == 'False' and parts[-1]['values'] == [], parts
        assert parts[-1]['work'] == trees[-1][0][1], (trees, parts)
    gate.passed_case('ordinary pending authority survives unrelated writes and refuses changed source')
    trees, _ = gate.run('ordinary parked session retirement', source+
        f'!(let {pattern} {read("(f)")} (Done Retired)) !(let {pattern} {read("(f)")} (Done RetiredAgain))')
    assert [x for tree in trees for x in tree] == [['Done', 'Retired'], ['Done', 'RetiredAgain']], trees
    gate.passed_case('retiring ordinary parked observations preserves the enclosing evaluator')


def admitted_coefficients(gate):
    def row(style, lhs, rhs, grade, predicate=None):
        admit=f' (admit {predicate})' if predicate else ''
        if style=='explicit':return f'(= {lhs} (weight:where {rhs} {grade}{admit})) '
        if style=='equals-w':return f'(=w {lhs} {rhs} {grade}{admit}) '
        return f'(= {lhs} {rhs} (where {grade}{admit})) '
    pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    def compare(name, source, body, expected, *, algebra='(algebra 0 1 + *)', closed=True):
        for workers in (1,2):
            extra=f'(workers {workers})'
            _,obs=gate.run(name+f' whole/{workers}',source+
                f'!(select 7 {control(1500,extra=extra)} {read(body,algebra)})')
            final=obs[-1]
            actual=[contribution(x) for x in final['values']]
            assert bag(actual)==bag(expected),(name,workers,actual,expected)
            assert final['closed']==('True' if closed else 'False'),(name,final)
            assert final['status']==('closed' if closed else 'suspended'),(name,final)
            for cut in (0,11,29,53,101):
                trees,parts=gate.run(name+f' split/{workers}/{cut}',source+
                    f'!(let {pattern} (select 7 {control(cut,extra=extra)} {read(body,algebra)}) '
                    f'(Split $p (select 7 {control(1500-cut,extra=extra)} $r)))')
                combined=trees[-1][0][1]+parts[-1]['values']
                assert [contribution(x) for x in combined]==actual,(name,workers,cut,combined,actual)
                for field in ('work','closed','satisfied','status'):
                    assert parts[-1][field]==final[field],(name,workers,cut,field,parts[-1],final)
        gate.passed_case(name+' preserves its predicate/body worlds and complete split work')

    for style in ('explicit','equals-w','trailing-where'):
        accept='(= (accept 2) True) (= (accept 1) False) '
        source=accept+'(= (plain $x) (+ $x 1)) '+row(style,'(gid $x)','$x','2','accept')
        compare(style+' admission only at authored coefficient',source,
            '(Pair (plain 2) (gid (plain 2)))',[[['Pair',3,3],2,[2]]])
        compare(style+' unrelated plain call retains neutral coefficient',source,'(plain 2)',[[3,1,[]]])
        source='(= (accept $w) False) '+row(style,'(gid)','(let $_ (println! "GUARD-BODY-EFFECT") V)','0','accept')
        compare(style+' false guard does not evaluate body',source,'(gid)',[])
        assert all('GUARD-BODY-EFFECT' not in r['stdout'] for r in gate.receipts[-12:])
        source=row(style,'(gid)','(let $_ (println! "ATTACH-BODY-EFFECT") V)','0')
        compare(style+' zero attachment still evaluates body',source,'(gid)',[['V',0,[0]]])
        assert all(sum(line.strip('"')=='ATTACH-BODY-EFFECT' for line in r['stdout'].splitlines())==1
                   for r in gate.receipts[-12:])
        source='(= (accept $w) Maybe) '+row(style,'(gid)','V','2','accept')
        compare(style+' unknown predicate retains returned verdict',source,'(gid)',[],closed=False)
        source='(= (accept $w) (accept $w)) '+row(style,'(gid)','V','2','accept')
        compare(style+' divergent predicate retains producer',source,'(gid)',[],closed=False)
        source='(= (accept $w) Maybe) (= (accept $w) True) (= (accept $w) True) (= (accept $w) False) '
        source+=row(style,'(gid)','V','2','accept')
        compare(style+' unresolved predicate does not hide later true occurrences',source,'(gid)',
                [['V',2,[2]],['V',2,[2]]],closed=False)
        source='(= (accept $w) (let $_ (println! "FORBIDDEN-GUARD-EFFECT") True)) '
        source+=row(style,'(gid)','V','2','accept')
        _,obs=gate.run(style+' predicate cannot acquire effect authority',source+
            f'!(select 7 {control(1500,extra="(workers 2)")} {read("(gid)")})')
        assert obs[-1]['status']=='fault' and obs[-1]['values']==[],obs
        assert 'FORBIDDEN-GUARD-EFFECT' not in gate.receipts[-1]['stdout']
        gate.passed_case(style+' predicate effects are refused before body publication')
        source='(= (grade A) 2) (= (grade B) 3) (= (accept $w) True) '
        source+=row(style,'(gid)','V','(grade $local)','accept')
        compare(style+' grade-only variables are fresh from caller bindings',source,'(let $local A (gid))',
                [['V',2,[2]],['V',3,[3]]])

    facts='(F 1) (F 2) (F 2) (= (accept 2) True) (= (accept 1) False) '
    compare('binding-local admitted match preserves duplicate physical rows',facts,
        '(match &self (F $x) $x (where $x (admit accept)))',[[2,2,[2]],[2,2,[2]]])
    source='(F a 2 yes) (F b 3 no) (= (yes $w) True) (= (no $w) False) '
    compare('match-bound body grade and callable share the captured row',source,
        '(match &self (F $x $g $predicate) (Pair $x $g) (where $g (admit $predicate)))',
        [[['Pair','a',2],2,[2]]])
    for name, verdict, expected in [('accepting', 'True', [['V', 2, [2]]]),
                                    ('rejecting', 'False', [])]:
        compare('captured ' + name + ' admission callable',
            f'(= (captured-guard $w) {verdict}) ',
            '(let $predicate captured-guard (weight:where V 2 (admit $predicate)))', expected)
    _, captured = gate.run('captured admission callable retains pure authority',
        '(F 2 captured-effect) (= (captured-effect $w) (let $_ (println! "FORBIDDEN-CAPTURED-GUARD") True)) '
        f'!(select 7 {control(1500)} '+read(
            '(match &self (F $g $predicate) V (where $g (admit $predicate)))')+')')
    assert captured[-1]['status'] == 'fault' and captured[-1]['values'] == [], captured
    assert 'FORBIDDEN-CAPTURED-GUARD' not in gate.receipts[-1]['stdout']
    gate.passed_case('a captured admission callable cannot execute effects')
    if getattr(gate, 'language', 'prime') == 'prime':
        compare('returned admission application stays data',
            '(= (yes $w) True) (= (quoted-guard $w) (quote (yes 2))) ',
            '(weight:where V 2 (admit quoted-guard))', [], closed=False)
    source='(= (accept True) True) (= (accept False) False) '
    source+='(=w (gid $x) $x False (admit accept)) '
    compare('Boolean false coefficient uses explicit Boolean admission',source,'(gid V)',[],
            algebra='(algebra False True or and)')
    source='(= (accept $w) True) (= (product $a $b) (Then $a $b)) (= (tag 3) Outer) '
    source+='(=w (inner) 3 Inner) (=w (gid $x) $x (tag $x) (admit accept)) '
    compare('admitted noncommutative score-forced argument order',source,'(gid (inner))',
            [[3,['Then',['Then','One','Inner'],'Outer'],['Inner','Outer']]],
            algebra='(algebra Zero One Sum product)')
    source='(=w (accept $w) True Guard) (= (product $a $b) (Then $a $b)) (= (tag 3) Outer) '
    source+='(=w (inner) 3 Inner) (=w (gid $x) $x (tag $x) (admit accept)) '
    compare('predicate derivation factors precede its admitted coefficient',source,'(gid (inner))',
            [[3,['Then',['Then',['Then','One','Inner'],'Guard'],'Outer'],['Inner','Guard','Outer']]],
            algebra='(algebra Zero One Sum product)')
    source='(=w (accept $w) True 3) (=w (gid $x) $x 2 (admit accept)) '
    compare('graded predicate retains [3,2] before the identity body',source,'(gid 7)',
            [[7,6,[3,2]]])
    source='(=w (accept $w) True 3) (=w (coin) 2 5) (=w (coin) 2 5) '
    source+='(=w (gid $x) (Pair $x $x) $x (admit accept)) '
    compare('graded shared argument and predicate retain occurrence order',source,'(gid (coin))',
            [[['Pair',2,2],30,[5,3,2]],[['Pair',2,2],30,[5,3,2]]])
    language=getattr(gate,'language','prime')
    for style in ('explicit','equals-w','trailing-where'):
        source='(= (accept 2) True) '+row(style,'(sum2 $x $y)','(+ $x $y)','2','accept')
        partial='(let $f (sum2 7) ($f 8))'
        expected=[[15,2,[2]]] if language=='petta' else [[[['sum2',7],8],1,[]]]
        trees,_=gate.run(style+' admitted partial application ordinary',source+f'!{read(partial)}')
        assert [contribution(x) for tree in trees for x in tree]==expected,(style,trees)
        compare(style+' admitted partial application preserves host arity',source,partial,expected)
        source='(= (accept 2) True) (= (sum2 $x $y) (+ $x $y)) '
        source+=row(style,'(make $x)','(sum2 $x)','2','accept')
        expected=[[15,2,[2]]] if language=='petta' else [[[['sum2',7],8],2,[2]]]
        compare(style+' admitted equation returns host partial value',source,
                '(let $f (make 7) ($f 8))',expected)
        source='(= (accept 2) True) '
        if language=='prime':
            rhs='(Lam T (Pair $x (idx 0)))'
            body='(App (make 7) 8)'
        elif language=='petta':
            rhs='(|-> ($y) (Pair $x $y))'
            body='(let $f (make 7) ($f 8))'
        else:
            source+='(= ((captured $x) $y) (Pair $x $y)) '
            rhs='(captured $x)'
            body='(let $f (make 7) ($f 8))'
        source+=row(style,'(make $x)',rhs,'2','accept')
        compare(style+' admitted equation returns captured host function',source,
                body,[[['Pair',7,8],2,[2]]])
        if language=='he':
            source='(= (accept $w) True) '
            source+=row(style,'((captured $x) $y)','(Pair $x $y)','$x','accept')
            source+=row(style,'(make $x)','(captured $x)','2','accept')
            compare(style+' admitted compound equation captures its head bindings',source,
                    '(let $f (make 7) ($f 8))',[[['Pair',7,8],14,[2,7]]])
        source='(= (accept 2) True) (= (add1 $x) (+ $x 1)) '
        source+=row(style,'(gid $x)','$x','2','accept')
        compare(style+' admitted identity returns callable',source,'(let $f (gid add1) ($f 7))',[[8,2,[2]]])
        compare(style+' admitted identity returns same retained Space',source,
            '(let $s (gid (new-space)) (let $_ (add-atom $s A) (match $s A A)))',[['A',2,[2]]])
        if language in ('prime','petta'):
            body='(App (gid (Lam T (+ (idx 0) 1))) 7)' if language=='prime' else \
                '(let $f (gid (|-> ($x) (+ $x 1))) ($f 7))'
            compare(style+' admitted identity returns host closure',source,body,[[8,2,[2]]])
        if style!='explicit':
            declaration=row(style,'(inserted $x)','$x','2','accept').strip()
            source='(= (accept 2) True) '+f'!(add-atom &self {declaration}) '
            source+=f'!{read("(inserted 7)")} !(match &self {declaration} RawGuardedRow) '
            source+=f'!(remove-atom &self {declaration}) !{read("(inserted 7)")}'
            trees,_=gate.run(style+' dynamic admitted raw row',source)
            values=[contribution(x) for tree in trees for x in tree
                    if isinstance(x,list) and x and x[0]=='Weighted']
            assert values==[[7,2,[2]],[['inserted',7],1,[]]],(style,trees)
            assert 'RawGuardedRow' in gate.receipts[-1]['stdout']
            gate.passed_case(style+' dynamic admitted row retains raw syntax and occurrence')

    source='(= (accept $w) Maybe) (= (items) 1) (= (items) 2) '
    _,obs=gate.run('revoking parked old consumer retains source witnesses',source+
        f'!(let {pattern} (select 1 {control(1500,extra="(workers 2) (goal $xs (weight:where True 2 (admit accept)))")} (items)) '
        f'(select 1 {control(1500,extra="(workers 2) (goal $xs False) (receipts True)")} $r))')
    assert obs[-1]['closed']=='True' and obs[-1]['values']==[],obs
    revision=obs[-1]['consumer-revisions'][0]
    assert ['retained-witnesses',2] in revision and revision[-1][1]>0,revision
    gate.passed_case('consumer revision revokes parked verdicts and retains source occurrences')
    source='(= (accept $w) Maybe) !(bind! &kb (new-space)) !(add-atom &kb (F 2)) '
    body='(match &kb (F $x) $x (where $x (admit accept)))'
    for name,action,status in [('current','True','suspended'),
                               ('changed','(add-atom &kb (F 3))','invalidated')]:
        trees,obs=gate.run('parked query scope '+name,source+
            f'!(let {pattern} (select 7 {control(1500,extra="(workers 2)")} {read(body)}) '
            f'(PriorWork $w (let $_ {action} (select 7 {control(1500,extra="(workers 2)")} $r))))')
        assert obs[-1]['status']==status and obs[-1]['closed']=='False' and obs[-1]['values']==[],obs
        assert obs[-1]['work']==trees[-1][0][1],(trees,obs)
        gate.passed_case('parked verdict spends no runnable quanta and rechecks query authority: '+name)
    if language=='prime':
        trees,_=gate.run('authored forms remain uninterpreted in he compatibility',
            '(=w (gid $x) $x 2) !(gid 7) !(weight:where V 2 (admit accept))',
            language='he',profile='he-compat')
        assert trees==[[['gid',7]],[['weight:where','V',2,['admit','accept']]]],trees
        gate.passed_case('he compatibility retains its ordinary unknown-expression contract')



def authored_algebra_clients(gate):
    language = getattr(gate, 'language', 'prime')
    prefix = '!(import! &self lib/native_weight_algebras.metta) !(pragma! prefer-rationals True) '
    pattern = '(Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    upper, lower = '(Matrix 2 2 (1 1 0 1))', '(Matrix 2 2 (1 0 1 1))'
    u, l = ['Matrix', 2, 2, [1, 1, 0, 1]], ['Matrix', 2, 2, [1, 0, 1, 1]]
    ul, lu = ['Matrix', 2, 2, [2, 1, 1, 1]], ['Matrix', 2, 2, [1, 1, 1, 2]]
    matrix_source = f'(=w (operand) {lower} {upper}) '
    matrix_source += '(=w (demanded-id $x) $x $x) '
    matrix_source += f'(= (literal-id $x) $x (where {lower})) '
    vector_source = '(= (vector:nonzero $v) (not (== $v (Vector (0 0))))) '
    vector_source += '(=w (left-id $x) $x (Vector (1 0)) (admit vector:nonzero)) '
    vector_source += '(= (right-value) V (where (Vector (0 1)) (admit vector:nonzero))) '
    route_source = f'(Edge a b {upper}) (Edge a b {upper}) (Edge b c {lower}) '
    crossing = '{$middle}' if language == 'prime' else ''
    route = ('(let $middle (match &self (Edge a $via $first) $via '
             '(where $first (admit matrix:valid?))) '
             '(match &self (Edge $middle $last $second) (Route a $middle $last) '
             f'(where $second (admit matrix:valid?))){crossing})')
    cases = [
        ('matrix score-forced argument', matrix_source, '(demanded-id (operand))',
         '(weights:matrix 2)', [[l, ul, [u, l]]]),
        ('matrix literal-grade host demand', matrix_source, '(literal-id (operand))',
         '(weights:matrix 2)', [[l, lu if language == 'prime' else ul,
                                [l, u] if language == 'prime' else [u, l]]]),
        ('matrix binding-local duplicate paths', route_source, route,
         '(weights:matrix 2)', [[['Route', 'a', 'b', 'c'], ul, [u, l]]] * 2),
        ('vector admitted nonzero factors with zero product', vector_source,
         '(left-id (right-value))', '(weights:vector 2)',
         [['V', ['Vector', [0, 0]], [['Vector', [1, 0]], ['Vector', [0, 1]]]
           if language == 'prime' else [['Vector', [0, 1]], ['Vector', [1, 0]]]]]),
        ('complex authored imaginary path', '(=w (imaginary-id $x) $x (Complex 0 1)) ',
         '(imaginary-id (imaginary-id A))', '(weights:complex)',
         [['A', ['Complex', -1, 0], [['Complex', 0, 1], ['Complex', 0, 1]]]]),
        ('PLN authored sequential evidence', '!(import! &self lib/lib_wmpln_evidence.metta) '
         '(=w (evidence-left $x) $x (evidence 2 3)) '
         '(= (evidence-right) Claim (where (evidence 5 7))) ',
         '(evidence-left (evidence-right))', '(weights:pln)',
         [['Claim', ['evidence', 10, 21], [['evidence', 2, 3], ['evidence', 5, 7]]
           if language == 'prime' else [['evidence', 5, 7], ['evidence', 2, 3]]]])]
    for name, source, body, algebra, expected in cases:
        expression = read(body, algebra)
        for width in (1, 2):
            extra = f'(workers {width})'
            _, whole = gate.run(name + f' whole/{width}', prefix + source +
                               f'!(select 7 {control(30000, extra=extra)} {expression})')
            answer = whole[-1]
            actual = [contribution(v) for v in answer['values']]
            assert bag(actual) == bag(expected), (name, width, actual, expected)
            assert answer['closed'] == 'True' and answer['satisfied'] == 'False', answer
            if 'duplicate paths' in name:
                ids = [v[3][1][0][1] for v in answer['values']]
                assert len(set(ids)) == 2, answer
            for cut in (0, 17, 97):
                trees, parts = gate.run(name + f' split/{width}/{cut}', prefix + source +
                    f'!(let {pattern} (select 7 {control(cut, extra=extra)} {expression}) '
                    f'(Split $prefix (select 7 {control(30000-cut, extra=extra)} $r)))')
                values = trees[-1][0][1] + parts[-1]['values']
                assert [contribution(v) for v in values] == actual, (name, width, cut, values, actual)
                for field in ('work', 'closed', 'satisfied', 'status'):
                    assert parts[-1][field] == answer[field], (name, width, cut, field, parts[-1], answer)
        gate.passed_case(name + ' retains declared algebra, factor order and residuals')


def authored_pln_client(gate):
    """Natural-count admission on actual shared, authored contributions."""
    language = getattr(gate, 'language', 'prime')
    fixture = authored_fixture(gate, 'pln_shared_identity.metta')
    program = fixture.read_text().replace('../../../lib/', str(ROOT / 'lib') + '/')
    source = program[:program.index('\n!(weight:read')] + '\n'
    ev = lambda p, n: ['evidence', p, n]
    shared = [[['Pair', 2, 2], ev(6, 1), [ev(3, 1), ev(2, 1)]],
              [['Pair', 2, 2], ev(6, 1), [ev(3, 1), ev(2, 1)]],
              [['Pair', 5, 5], ev(35, 1), [ev(7, 1), ev(5, 1)]]]

    def packet(values):
        # Independent runs allocate fresh identities. Preserve the complete
        # equality pattern of factor and dependency IDs, not their integers.
        identifiers = {}
        def identity(value):
            if value not in identifiers:
                identifiers[value] = len(identifiers)
            return identifiers[value]
        result = []
        for value in values:
            contribution(value)
            factors = value[3][1]
            result.append([value[1], value[2],
                [[identity(f[1]), identity(f[2]), f[3]] for f in factors]])
        return result

    def physical_rows(values):
        ids = [value[3][1][0][1] for value in values]
        assert len(ids) == len(set(ids)), values

    trees, observations = gate.run_file('PLN shared identity authored file', fixture)
    ordinary = [value for tree in trees for value in tree
                if isinstance(value, list) and value and value[0] == 'Weighted']
    assert [contribution(v) for v in ordinary[:2]] == [
        [7, ev(49, 1), [ev(7, 1), ev(7, 1)]],
        [7, ev(4, 1), [ev(2, 1), ev(2, 1)]]], ordinary
    assert bag(contribution(v) for v in ordinary[2:]) == bag(shared), ordinary
    assert len(observations) == 1 and observations[0]['closed'] == 'True', observations
    assert bag(contribution(v) for v in observations[0]['values']) == bag(shared), observations
    physical_rows(ordinary[2:])
    physical_rows(observations[0]['values'])
    gate.passed_case('PLN authored identity preserves three physical shared choices and nested evidence')

    pattern = '(Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    def compare(name, definitions, body, expected, *, closed=True, effect=None, duplicate=False):
        expression = read(body, '(weights:pln)')
        for width in (1, 2):
            extra = f'(workers {width})'
            _, observations = gate.run(name + f' whole/{width}', source + definitions +
                f'!(select 7 {control(10000, extra=extra)} {expression})')
            assert observations, gate.receipts[-1]
            final = observations[-1]
            assert bag(contribution(v) for v in final['values']) == bag(expected), (name, final)
            assert final['closed'] == ('True' if closed else 'False'), (name, final)
            assert final['satisfied'] == 'False', (name, final)
            assert final['status'] == ('closed' if closed else 'suspended'), (name, final)
            if duplicate:
                physical_rows(final['values'])
            if effect:
                marker, count = effect
                assert sum(line.strip('"') == marker for line in
                           gate.receipts[-1]['stdout'].splitlines()) == count
            cuts = sorted({0, 1, 31, 97, final['work'] // 2, max(0, final['work'] - 1)})
            for cut in cuts:
                trees, parts = gate.run(name + f' split/{width}/{cut}', source + definitions +
                    f'!(let {pattern} (select 7 {control(cut, extra=extra)} {expression}) '
                    f'(Split $prefix $r $w (select 7 {control(10000-cut, extra=extra)} $r)))')
                split = trees[-1][0]
                actual = parts[-1]
                values = split[1] + actual['values']
                assert packet(values) == packet(final['values']), (name, width, cut, values, final)
                assert actual['residual'] == split[2], (name, cut, split)
                assert actual['work'] >= split[3], (name, cut, split)
                for field in ('work', 'closed', 'satisfied', 'status'):
                    assert actual[field] == final[field], (name, width, cut, field, actual, final)
                if effect:
                    assert sum(line.strip('"') == marker for line in
                               gate.receipts[-1]['stdout'].splitlines()) == count
            if not closed:
                trees, parts = gate.run(name + f' parked/{width}', source + definitions +
                    f'!(let {pattern} (select 7 {control(10000, extra=extra)} {expression}) '
                    f'(Parked $prefix $r $w (select 7 {control(0, extra=extra)} $r) '
                    f'(select 7 {control(10000, extra=extra)} $r)))')
                parked = trees[-1][0]
                assert packet(parked[1]) == packet(final['values']), (name, parked, final)
                assert len(parts) == 2, (name, parts)
                for after in parts:
                    assert after['values'] == [] and after['work'] == parked[3], (name, after, parked)
                    assert after['residual'] == parked[2] and after['closed'] == 'False', (name, after)
                    assert after['status'] == 'suspended', (name, after)
        gate.passed_case(name + ': factors, identity sharing, fixed-controller splits and retained completion')

    compare('PLN shared graded identity', '', '(pln:paired (weigh-by-self (pln:coin)))',
            shared, duplicate=True)
    compare('PLN graded predicate order',
        '(=w (pln:graded-accept $packet) (pln:positive-count? $packet) (evidence 3 1)) '
        '(=w (pln:guarded-id $x) $x (evidence $x 1) (admit pln:graded-accept)) ',
        '(pln:paired (pln:guarded-id (pln:coin)))',
        [[['Pair', 2, 2], ev(18, 1), [ev(3, 1), ev(3, 1), ev(2, 1)]]] * 2 +
        [[['Pair', 5, 5], ev(105, 1), [ev(7, 1), ev(3, 1), ev(5, 1)]]], duplicate=True)
    compare('PLN binding-local positive match',
        '(EvidenceChoice 2 (evidence 3 1)) (EvidenceChoice 2 (evidence 3 1)) '
        '(EvidenceChoice 5 (evidence 7 1)) (EvidenceChoice 9 (evidence 0 1)) ',
        '(pln:paired (weigh-by-self (match &self (EvidenceChoice $x $grade) $x '
        '(where $grade (admit pln:positive-count?)))))', shared, duplicate=True)
    compare('PLN zero attachment',
        '(=w (pln:zero-row) (let $_ (println! PLN-ZERO-BODY) Claim) (evidence 0 0)) ',
        '(pln:zero-row)', [['Claim', ev(0, 0), [ev(0, 0)]]], effect=('PLN-ZERO-BODY', 1))
    compare('PLN positive counter-only refusal',
        '(=w (pln:counter) (let $_ (println! PLN-BLOCKED-BODY) Claim) '
        '(evidence 0 1) (admit pln:positive-count?)) ', '(pln:counter)', [],
        effect=('PLN-BLOCKED-BODY', 0))
    for predicate in ('pln:nonzero-count?', 'pln:positive-count?'):
        factors = [ev(1, 0), ev(0, 1)] if language == 'prime' else [ev(0, 1), ev(1, 0)]
        expected = [['Claim', ev(0, 0), factors]] if predicate == 'pln:nonzero-count?' else []
        compare('PLN opposite polarities with ' + predicate,
            f'(=w (pln:left $x) $x (evidence 1 0) (admit {predicate})) '
            f'(= (pln:right) Claim (where (evidence 0 1) (admit {predicate}))) ',
            '(pln:left (pln:right))', expected)
    for label, grade in (('unknown', 'UnknownEvidence'), ('wrong arity', '(evidence 3)'),
                         ('negative count', '(evidence -1 2)'), ('fractional count', '(evidence 1.5 2)'),
                         ('nonnumeric count', '(evidence Maybe 2)')):
        compare('PLN parked ' + label,
            '(= (pln:unsettled) (let $_ (println! PLN-BLOCKED-BODY) 2) '
            f'(where {grade} (admit pln:positive-count?))) ',
            '(pln:paired (weigh-by-self (pln:unsettled)))', [], closed=False,
            effect=('PLN-BLOCKED-BODY', 0))
    compare('PLN parked sibling keeps accepted duplicate rows',
        '(=w (pln:rows) Claim (evidence -1 2) (admit pln:positive-count?)) '
        '(=w (pln:rows) Claim (evidence 2 1) (admit pln:positive-count?)) '
        '(=w (pln:rows) Claim (evidence 2 1) (admit pln:positive-count?)) ',
        '(pln:rows)', [['Claim', ev(2, 1), [ev(2, 1)]]] * 2, closed=False, duplicate=True)
    compare('PLN delayed coefficient retains its shared world',
        '(= (pln:later $n $value) (if (== $n 0) $value (pln:later (- $n 1) $value))) '
        '(=w (pln:slow-id $x) $x (pln:later 6 (evidence $x 1)) (admit pln:positive-count?)) ',
        '(pln:paired (pln:slow-id (pln:coin)))', shared, duplicate=True)

    # A deliberately partial authored predicate has the host's ordinary
    # failed-match meaning; it is not the total count-profile policy above.
    _, observations = gate.run('PLN partial predicate host boundary', source +
        '(= (pln:partial (evidence $p $n)) (> $p 0)) '
        '(=w (pln:row) Claim UnknownEvidence (admit pln:partial)) '
        f'!(select 7 {control(1000)} {read("(pln:row)", "(weights:pln)")})')
    result = observations[-1]
    assert result['values'] == [], result
    assert result['closed'] == ('True' if language == 'petta' else 'False'), result
    assert result['status'] == ('closed' if language == 'petta' else 'suspended'), result
    gate.passed_case('PLN total profile remains distinct from the host partial-predicate contract')


def authored_readouts(gate):
    prefix='!(import! &self lib/native_weight_algebras.metta) !(pragma! prefer-rationals True) '
    pattern='(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    cases=[
        ('complex interference', '(=w (paths) A (Complex 1 0)) (= (paths) A (where (Complex -1 0))) (=w (paths) B (Complex 0 1)) ', '(weights:complex)',
            '(let $sums (weights:aggregate (weights:complex) $v) (Readouts $v $sums (weights:support (Complex 0 0) $sums) (weights:born $sums)))'),
        ('vector Pareto ties','(=w (paths) a (Vector (1 4))) (=w (paths) a (Vector (1 4))) (=w (paths) b (Vector (4 1))) (= (paths) c (where (Vector (5 5)))) (=w (paths) d (Vector (1 4))) ','(weights:vector 2)',
            '(Readouts $v (weights:pareto $v))'),
        ('PLN nonidempotent sum','!(import! &self lib/lib_wmpln_evidence.metta) (=w (paths) Claim (evidence 2 3)) (=w (paths) Claim (evidence 2 3)) (= (paths) Claim (where (evidence 5 7))) ','(weights:pln)',
            '(Readouts $v (weights:aggregate (weights:pln) $v))')]
    def verify(name,result):
        assert result[0]=='Readouts',result
        raw=result[1];cs=[contribution(x) for x in raw]
        assert len({x[3][1][0][1] for x in raw})==len(raw),raw
        if name=='complex interference':
            assert bag(cs)==bag([['A',['Complex',1,0],[['Complex',1,0]]],['A',['Complex',-1,0],[['Complex',-1,0]]],['B',['Complex',0,1],[['Complex',0,1]]]]),cs
            assert [[x[1],x[2]] for x in result[2]]==[['A',['Complex',0,0]],['B',['Complex',0,1]]],result
            assert [x[1] for x in result[3]]==['B'],result
            assert [[x[1],x[2]] for x in result[4]]==[['A',0],['B',1]],result
            assert bag(result[2][0][3][1]+result[2][1][3][1])==bag(raw),result
            # Born is applied after amplitude addition: the two unit A paths cancel.
            assert sum(x[2][1]**2+x[2][2]**2 for x in raw if x[1]=='A')==2
        elif name=='vector Pareto ties':
            assert [x[1] for x in result[2]]==[x[1] for x in raw if x[1]!='c'],result
            assert bag(x[1] for x in result[2])==bag(['a','a','b','d']),result
            assert bag(result[2])==bag(x for x in raw if x[1]!='c'),result
        else:
            assert len(raw)==3 and [[x[1],x[2]] for x in result[2]]==[['Claim',['evidence',9,13]]],result
            assert bag(result[2][0][3][1])==bag(raw),result
        return cs
    for name,source,algebra,consumer in cases:
        for width in (1,2):
            extra=f'(workers {width})'
            client=f'(let {pattern} (select 0 {control(20000,extra=extra+" (demand all)")} {read("(paths)",algebra)}) {consumer})'
            _,whole=gate.run(name+f' whole/{width}',prefix+source+f'!(select 1 {control(50000,extra=extra)} {client})')
            answer=whole[-1];assert answer['satisfied']=='True' and len(answer['values'])==1,answer
            expected=verify(name,answer['values'][0])
            for cut in (0,19,83):
                trees,parts=gate.run(name+f' split/{width}/{cut}',prefix+source+f'!(let {pattern} (select 1 {control(cut,extra=extra)} {client}) (Split $v (select 1 {control(50000-cut,extra=extra)} $r)))')
                values=trees[-1][0][1]+parts[-1]['values'];assert len(values)==1,values
                assert verify(name,values[0])==expected,(name,values,expected)
                for field in ('work','satisfied','closed','status'): assert parts[-1][field]==answer[field],(name,width,cut,field,answer,parts)
        gate.passed_case(name+' keeps actual authored contributions and explicit readout')

def captured_source_unchanged(gate):
    prefix='!(import! &self lib/native_weight_algebras.metta) '
    source='(=w (gid $x) $x (Vector (1 0))) '
    body=read('(gid V)','(weights:vector 2)')
    # HE lists local rows separately from its live imported module.  Check
    # both owned stores, including their complete ordered payloads, rather
    # than mistaking the importer's local count for the composed query view.
    # The other hosts retain the count control over their combined store;
    # its standard-library documentation includes multiline string data.
    he = getattr(gate, 'language', 'prime') == 'he'
    if he:
        prefix += '!(import! &weight-module lib/native_weight_algebras.metta) '
    snapshot = '!(let $rows (collapse (get-atoms &self)) (Count (size-atom $rows))) '
    if he:
        snapshot = '!(collapse (get-atoms &self)) !(collapse (get-atoms &weight-module)) '
    trees,results=gate.run('captured source retains authored rows during coefficient fold',prefix+source+snapshot+f'!(select 7 {control(30000)} {body}) '+snapshot)
    width = 2 if he else 1
    tail = trees[-(2 * width + 1):]
    snapshots = [tree[0] for tree in tail[:width] + tail[width+1:]]
    assert len(snapshots) == 2 * width and snapshots[:width] == snapshots[width:], snapshots
    if he:
        assert len(snapshots[1]) > 20, snapshots
        assert len(snapshots[0]) == 1 and snapshots[0][0][0] == '=w', snapshots
    else:
        assert snapshots[0][0] == 'Count' and snapshots[0][1] > 20, snapshots
    assert results[-1]['closed']=='True' and [contribution(x) for x in results[-1]['values']]==[['V',['Vector',[1,0]],[['Vector',[1,0]]]]],results
    pattern='(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    _,results=gate.run('captured fold source mutation is still refused',prefix+source+f'!(let {pattern} (select 7 {control(17)} {body}) (let $_ (add-atom &self (=w (gid $x) $x (Vector (0 1)))) (select 7 {control(30000)} $r)))')
    assert results[-1]['status']=='invalidated' and results[-1]['closed']=='False',results
    gate.passed_case('canonical coefficient fold preserves pinned source; real insertion invalidates')


def observation_values(value):
    if isinstance(value, list):
        if value and value[0] == 'NativeHandle':
            return value[:2]
        return [observation_values(x) for x in value]
    return value


def nested_selection_boundary(gate):
    language = getattr(gate, 'language', 'prime')
    source = '(= (rows) 7) (= (rows) 7) (= (rows) 9) '
    pattern = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    cases = [
        ('native three-argument', f'(select 2 {control(1000)} (rows))'),
        ('native two-argument', f'(select {control(1000)} (rows))'),
        ('zero inner allowance', f'(select 2 {control(0)} (rows))'),
        ('malformed native strategy', '(select 2 (search-policy (control UnknownStrategy)) (rows))'),
        ('malformed native option', '(select 2 (search-policy (control FIFO (budget UnknownBudget))) (rows))'),
    ]
    cases.append(('ordinary unary selection', '(select (superpose (7 7 9)))'))
    if language == 'he':
        source += '(: held (-> Atom Atom)) (= (held $x) $x) '
        cases.append(('HE held Atom operand', f'(held (select 2 {control(1000)} (rows)))'))
    for name, body in cases:
        ordinary, _ = gate.run(name+' ordinary', source+'!'+body)
        returned = parse(gate.receipts[-1]['stdout']) if language == 'petta' else ordinary[-1]
        expected = [observation_values(v) for v in returned]
        for width in (1, 2):
            extra = f'(workers {width})'
            _, observations = gate.run(name+f' whole/{width}', source+
                f'!(select 0 {control(4000,extra=extra+" (demand all)")} {body})')
            whole = observations[-1]
            assert [observation_values(v) for v in whole['values']] == expected, (name, width, expected, whole)
            assert whole['closed'] == 'True', whole
            for cut in (0, 1, 17, 83):
                retained_prefix = '(quote $v){$v}' if language == 'prime' else '(quote $v)' if language == 'he' else '$v'
                trees, observations = gate.run(name+f' split/{width}/{cut}', source+
                    f'!(let {pattern} (select 0 {control(cut,extra=extra+" (demand all)")} {body}) '
                    f'(Split {retained_prefix} (select 0 {control(4000-cut,extra=extra+" (demand all)")} $r)))')
                saved = trees[-1][0][1]
                if language in ('prime','he'):
                    assert saved[0] == 'quote', saved
                    saved = saved[1]
                values = saved+observations[-1]['values']
                assert [observation_values(v) for v in values] == [observation_values(v) for v in whole['values']], (name,width,cut,trees,whole)
                for field in ('work','satisfied','closed','status'):
                    assert observations[-1][field] == whole[field], (name,width,cut,field,observations[-1],whole)
        gate.passed_case(name+' keeps canonical host interpretation and complete split observation')


def prime_tuple_boundary(gate):
    source = '(=w (coin) 3 2) (=w (coin) 3 2) (=w (coin) 6 5) '
    source += '(= (list-pair $x) ((Box $x $x))) (= (box-pair $x) (Box $x $x)) '
    pattern = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for name,body,expected in [
        ('expression-headed list', '(list-pair (coin))', [[['Box',3,3]], [['Box',3,3]], [['Box',6,6]]]),
        ('inert symbol-headed tuple', '(box-pair (coin))', [['Box',3,3], ['Box',3,3], ['Box',6,6]]),
    ]:
        for width in (1,2):
            extra=f'(workers {width})'
            _,observations=gate.run(name+f' whole/{width}',source+f'!(select 7 {control(2000,extra=extra)} {read(body)})')
            whole=observations[-1]
            values=[contribution(v) for v in whole['values']]
            assert bag(v[0] for v in values)==bag(expected),values
            assert bag((v[1],v[2]) for v in values)==bag([(2,[2]),(2,[2]),(5,[5])]),values
            assert whole['closed']=='True' and whole['status']=='closed',whole
            for cut in (0,1,17,83):
                trees,observations=gate.run(name+f' split/{width}/{cut}',source+
                    f'!(let {pattern} (select 7 {control(cut,extra=extra)} {read(body)}) '
                    f'(Split $v (select 7 {control(2000-cut,extra=extra)} $r)))')
                actual=trees[-1][0][1]+observations[-1]['values']
                assert [contribution(v) for v in actual]==values,(name,width,cut,actual,values)
                for field in ('work','satisfied','closed','status'):
                    assert observations[-1][field]==whole[field],(name,width,cut,field,observations[-1],whole)
        gate.passed_case(name+' preserves shared choices, duplicate factors and split work')


def implicit_first(gate):
    divergent = '(= (first-tail-loop) (first-tail-loop)) '
    first_cases = [
        ('direct first', '', '(select (superpose (7 (first-tail-loop))))', [7]),
        ('captured first', '(= (pick $x) (select $x)) ',
         '(pick (superpose (7 (first-tail-loop))))', [7]),
        ('demanded limit', '(= (pick $n $x) (select $n $x)) ',
         '(pick (+ 0 1) (superpose (7 (first-tail-loop))))', [7]),
        ('shared first', '(= (pick $x) (let $v (select $x) (Pair $v $x))) ',
         '(pick (superpose (7 (first-tail-loop))))', [['Pair', 7, 7]]),
        ('second first observer', '(= (pick $x) (let $v (select $x) (Pair $v (select $x)))) ',
         '(pick (superpose (7 (first-tail-loop))))', [['Pair', 7, 7]]),
        ('equation first', '(= (first-rows) 7) (= (first-rows) (first-tail-loop)) '
         '(= (pick $x) (select $x)) ', '(pick (first-rows))', [7]),
    ]
    for name, prefix, body, expected in first_cases:
        trees, _ = gate.run(name+' retains an unvisited divergent suffix', divergent+prefix+'!'+body)
        assert [v for tree in trees for v in tree] == expected, (name, trees)
        gate.passed_case(name+' returns its chosen world before the undemanded suffix')
    plain = '(= (coin) 3) (= (coin) 6) '
    plain += '(= (pick-pair $x) (let $chosen (select $x) (Pair $chosen $x))) '
    trees, _ = gate.run('ordinary unannotated captured first choice', plain+'!(pick-pair (coin))')
    assert trees == [[['Pair', 3, 3]]], trees
    gate.passed_case('ordinary unary select retains its captured lazy choice without an observer')
    source='(=w (coin) 3 2) (=w (coin) 3 2) (=w (coin) 6 5) '
    source+='(= (pick-pair $x) (let $chosen (select $x) (Pair $chosen $x))) '
    pattern='(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    cases=[
        ('ordinary first value','(select (superpose (7 7 9)))',[7],False),
        ('captured chosen cell',read('(pick-pair (coin))'),[[['Pair',3,3],2,[2]]],True),
        ('duplicate outer worlds',read('(let $x (coin) (Pair (select $x) $x))'),
            [[['Pair',3,3],2,[2]],[['Pair',3,3],2,[2]],[['Pair',6,6],5,[5]]],True),
        ('empty first value','(select (empty))',[],False),
    ]
    for name,body,expected,weighted in cases:
        trees, _ = gate.run(name+' ordinary Outcome', source+'!'+body)
        ordinary = [v for tree in trees for v in tree]
        ordinary = [contribution(v) for v in ordinary] if weighted else ordinary
        assert bag(ordinary) == bag(expected), (name, ordinary, expected)
        for width in (1,2):
            extra=f'(workers {width}) (demand all)'
            _,observations=gate.run(name+f' whole/{width}',source+f'!(select 0 {control(1500,extra=extra)} {body})')
            whole=observations[-1]
            values=[contribution(v) for v in whole['values']] if weighted else whole['values']
            assert bag(values)==bag(expected),(name,width,whole)
            assert whole['closed']=='True' and whole['status']=='closed',whole
            if name=='duplicate outer worlds':
                ids=[v[3][1][0][1] for v in whole['values']]
                assert len(ids)==len(set(ids)),whole
            for cut in (0,1,7,17,37,83,149,251):
                trees,obs=gate.run(name+f' split/{width}/{cut}',source+
                    f'!(let {pattern} (select 0 {control(cut,extra=extra)} {body}) '
                    f'(Split $v (select 0 {control(1500-cut,extra=extra)} $r)))')
                actual=trees[-1][0][1]+obs[-1]['values']
                actual=[contribution(v) for v in actual] if weighted else actual
                assert actual==values,(name,width,cut,actual,values)
                for field in ('work','closed','satisfied','status'):
                    assert obs[-1][field]==whole[field],(name,width,cut,field,obs[-1],whole)
            gate.passed_case(name+f' preserves selected Outcome/world across splits, workers={width}')
    unknown='(= (accept $w) Maybe) (=w (rows) 7 2 (admit accept)) '
    recursive='(= (loop) (loop)) '
    for name,prefix,body,parked in [('parked first guard',unknown,'(select (rows))',True),
                                      ('unfinished first call',recursive,'(select (loop))',False)]:
        for width in (1,2):
            extra=f'(workers {width}) (demand all)'
            _,obs=gate.run(name+f' whole/{width}',prefix+f'!(select 0 {control(300,extra=extra)} {body})')
            whole=obs[-1]
            assert not whole['values'] and whole['closed']=='False' and whole['status']=='suspended',whole
            assert (whole['work']<300)==parked,whole
            for cut in (0,1,17,73,149):
                trees,obs=gate.run(name+f' split/{width}/{cut}',prefix+
                    f'!(let {pattern} (select 0 {control(cut,extra=extra)} {body}) '
                    f'(Split $v (select 0 {control(300-cut,extra=extra)} $r)))')
                assert not trees[-1][0][1] and not obs[-1]['values'],trees
                for field in ('work','closed','satisfied','status'):
                    assert obs[-1][field]==whole[field],(name,width,cut,field,obs[-1],whole)
            if parked:
                _,obs=gate.run(name+f' repeated allowance/{width}',prefix+
                    f'!(let {pattern} (select 0 {control(300,extra=extra)} {body}) '
                    f'(select 0 {control(300,extra=extra)} $r))')
                assert obs[-1]['work']==whole['work'] and obs[-1]['closed']=='False',obs
            gate.passed_case(name+f' retains full open child without false closure, workers={width}')


def owned_alternatives(gate):
    """Explicit agendas retain branch cursors and each admitted return."""
    pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    for head in ('superpose','hyperpose'):
        for width in (1,2):
            extra=f'(workers {width})'
            name=f'{head}/{width}'
            source='(= (tail-loop) (tail-loop)) '
            body=f'({head} (7 (tail-loop)))'
            _,obs=gate.run(name+' owned prefix',source+f'!(select 1 {control(500,extra=extra)} {body})')
            expected=obs[-1]
            assert expected['values']==[7] and expected['satisfied']=='True',expected
            assert expected['closed']=='False',expected
            _,later=gate.run(name+' recursive first alternative',source+
                f'!(select 1 {control(500,extra=extra)} ({head} ((tail-loop) 7)))')
            assert later[-1]['values']==[7] and later[-1]['satisfied']=='True',later
            assert later[-1]['closed']=='False',later
            _,zero=gate.run(name+' zero allowance',source+f'!(select 1 {control(0,extra=extra)} {body})')
            assert not zero[-1]['values'] and zero[-1]['work']==0,zero
            assert zero[-1]['closed']=='False' and zero[-1]['status']=='suspended',zero
            for cut in (0,1,2,3,4,7,13,expected['work']-1):
                trees,parts=gate.run(name+f' prefix cut{cut}',source+
                    f'!(let {pattern} (select 1 {control(cut,extra=extra)} {body}) '
                    f'(Split $p (select 1 {control(500-cut,extra=extra)} $r)))')
                actual={**parts[-1],'values':trees[-1][0][1]+parts[-1]['values']}
                for field in ('values','work','closed','satisfied','status'):
                    assert actual[field]==expected[field],(name,cut,field,actual,expected)
            trees,parts=gate.run(name+' continue retained loop',source+
                f'!(let {pattern} (select 1 {control(500,extra=extra)} {body}) '
                f'(Split $p (select 0 {control(100,extra=extra+" (demand all)")} $r)))')
            assert trees[-1][0][1]==[7] and not parts[-1]['values'],parts
            assert parts[-1]['closed']=='False' and parts[-1]['status']=='suspended',parts
            assert parts[-1]['work']==expected['work']+100,parts
            gate.passed_case(name+' owns unfinished alternatives through exact prefix splits')

            for label,source,body,values in (
                ('duplicate values','',f'({head} (7 7 9))',[7,7,9]),
                ('shared choice','(= (coin) 3) (= (coin) 6) '
                 f'(= (pairs $x) ({head} ((Pair $x $x) (Pair $x $x)))) ',
                 '(pairs (coin))',[['Pair',3,3],['Pair',3,3],['Pair',6,6],['Pair',6,6]]),
            ):
                _,obs=gate.run(name+' '+label,source+f'!(select 7 {control(1000,extra=extra)} {body})')
                assert bag(obs[-1]['values'])==bag(values) and obs[-1]['closed']=='True',obs
                gate.passed_case(name+' preserves '+label)
            _,obs=gate.run(name+' graded physical duplicates','(=w (graded) 7 2) '+
                f'!(select 7 {control(1000,extra=extra)} {read(f"({head} ((graded) (graded)))")})')
            assert [contribution(v) for v in obs[-1]['values']]==[[7,2,[2]],[7,2,[2]]],obs
            assert len({v[3][1][0][1] for v in obs[-1]['values']})==2,obs
            assert obs[-1]['closed']=='True',obs
            gate.passed_case(name+' preserves distinct graded branch occurrences')

            # FIFO interleaves admitted branches. A ready sibling effect may
            # precede the outer observation; resumption must not repeat it.
            body=f'({head} (7 (let $_ (println! "READY-ALTERNATIVE") 8)))'
            _,prefix=gate.run(name+' admitted sibling effect',
                f'!(select 1 {control(500,extra=extra)} {body})')
            assert prefix[-1]['values']==[7] and prefix[-1]['closed']=='False',prefix
            assert gate.receipts[-1]['stdout'].splitlines().count('READY-ALTERNATIVE')==1
            trees,parts=gate.run(name+' retain committed sibling effect',
                f'!(let {pattern} (select 1 {control(500,extra=extra)} {body}) '
                f'(Split $p (select 0 {control(1000,extra=extra+" (demand all)")} $r)))')
            assert trees[-1][0][1]==[7] and parts[-1]['values']==[8],parts
            assert parts[-1]['closed']=='True',parts
            assert gate.receipts[-1]['stdout'].splitlines().count('READY-ALTERNATIVE')==1
            gate.passed_case(name+' retains committed sibling effects exactly once')

            source='(= (later 0) (let $_ (println! "LATER-ALTERNATIVE") 8)) '
            source+='(= (later $n) (if (> $n 0) (later (- $n 1)) (empty))) '
            body=f'({head} (7 (later 10)))'
            _,prefix=gate.run(name+' untouched effect suffix',source+
                f'!(select 1 {control(500,extra=extra)} {body})')
            assert prefix[-1]['values']==[7] and prefix[-1]['closed']=='False',prefix
            assert 'LATER-ALTERNATIVE' not in gate.receipts[-1]['stdout']
            _,whole=gate.run(name+' full delayed effect',source+
                f'!(select 7 {control(1500,extra=extra)} {body})')
            expected=whole[-1]
            assert expected['values']==[7,8] and expected['closed']=='True',expected
            assert gate.receipts[-1]['stdout'].splitlines().count('LATER-ALTERNATIVE')==1
            for cut in (0,1,4,7,13,23,71):
                trees,parts=gate.run(name+f' delayed effect cut{cut}',source+
                    f'!(let {pattern} (select 7 {control(cut,extra=extra)} {body}) '
                    f'(Split $p (select 7 {control(1500-cut,extra=extra)} $r)))')
                actual={**parts[-1],'values':trees[-1][0][1]+parts[-1]['values']}
                for field in ('values','work','closed','satisfied','status'):
                    assert actual[field]==expected[field],(name,cut,field,actual,expected)
                assert gate.receipts[-1]['stdout'].splitlines().count('LATER-ALTERNATIVE')==1
            gate.passed_case(name+' retains untouched effects and complete split work')


def authored_programs(gate):
    """Complete authored files cross equation, match and observer boundaries."""
    def weighted_rows(trees):
        return [value for tree in trees for value in tree
                if isinstance(value,list) and value and value[0]=='Weighted']

    for file,expected in (
        ('weighted_routes.metta',[[['Route','a','b','d'],6,[2,3]],
            [['Route','a','b','d'],6,[2,3]],[['Route','a','c','d'],15,[5,3]]]),
        ('shared_graded_identity.metta',[[['Pair',2,2],6,[3,2]],
            [['Pair',2,2],6,[3,2]],[['Pair',5,5],35,[7,5]]]),
    ):
        trees,obs=gate.run_file(file,authored_fixture(gate, file))
        ordinary=weighted_rows(trees)
        if file=='shared_graded_identity.metta':
            assert contribution(ordinary[0])==[7,49,[7,7]],ordinary
            ordinary=ordinary[1:]
        assert bag([contribution(v) for v in ordinary])==bag(expected),(file,trees)
        assert len(obs)==1 and obs[-1]['closed']=='True',(file,obs)
        assert bag([contribution(v) for v in obs[-1]['values']])==bag(expected),(file,obs)
        for values in (ordinary,obs[-1]['values']):
            ids=[v[3][1][0][1] for v in values]
            assert len(set(ids))==3,(file,values)
        gate.passed_case(file+' preserves whole authored ordinary and controlled contributions')

    pattern='(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
    source=authored_fixture(gate,'shared_graded_identity.metta').read_text()
    body=read('(paired (graded-attempt))')
    expected=[[['Pair','V','V'],3,[3]],[['Pair','V','V'],5,[5]]]
    trees,_=gate.run('failed graded alternative before equal shared answers',source+'!'+body)
    actual=weighted_rows(trees)[-len(expected):]
    assert bag([contribution(value) for value in actual])==bag(expected),actual
    for name,factors in (('plain-attempt',[]),('unit-attempt',[1])):
        trees,_=gate.run(name+' source annotation identity',source+'!'+read(f'({name})'))
        actual=weighted_rows(trees)[-1:]
        assert [contribution(value) for value in actual]==[['V',1,factors]],actual
    gate.passed_case('an absent annotation differs from an explicit unit factor')
    for width in (1,2):
        extra=f'(workers {width})'
        _,whole=gate.run(f'failed graded alternative whole/{width}',source+
            f'!(select 7 {control(2000,extra=extra)} {body})')
        final=whole[-1]
        assert final['closed']=='True' and bag([contribution(value) for value in final['values']])==bag(expected),final
        assert len({value[3][1][0][1] for value in final['values']})==2,final
        for cut in (0,1,7,19,71):
            trees,parts=gate.run(f'failed graded alternative cut{cut}/{width}',source+
                f'!(let {pattern} (select 7 {control(cut,extra=extra)} {body}) '
                f'(Split $p (select 7 {control(2000-cut,extra=extra)} $r)))')
            values=trees[-1][0][1]+parts[-1]['values']
            assert provenance_packet(values)==provenance_packet(final['values']),(width,cut,values,final)
            for field in ('work','closed','satisfied','status'):
                assert parts[-1][field]==final[field],(width,cut,field,parts[-1],final)
        gate.passed_case(f'failed production stays out of equal shared answer grades through five cuts/{width}')

    for style in ('equals-w','trailing-where'):
        def row(lhs,rhs,grade):
            return (f'(=w {lhs} {rhs} {grade}) ' if style=='equals-w'
                    else f'(= {lhs} {rhs} (where {grade})) ')
        source=row('(coin)','2','3')*2+row('(coin)','5','7')
        source+=row('(gid $x)','$x','$x')+'(= (paired $x) (Pair $x $x)) '
        source+='(= (wrapped) (paired (gid (coin)))) '
        body=read('(wrapped)')
        expected=[[['Pair',2,2],6,[3,2]],[['Pair',2,2],6,[3,2]],[['Pair',5,5],35,[7,5]]]
        trees,_=gate.run(style+' shared argument through ordinary wrapper',source+'!'+body)
        values=[x for tree in trees for x in tree]
        assert bag([contribution(x) for x in values])==bag(expected),(style,trees)
        assert len({x[3][1][0][1] for x in values})==3,values
        for width in (1,2):
            extra=f'(workers {width})'
            _,whole=gate.run(style+f' shared wrapper whole/{width}',source+
                f'!(select 7 {control(2000,extra=extra)} {body})')
            final=whole[-1]
            assert bag([contribution(x) for x in final['values']])==bag(expected),final
            assert final['closed']=='True',final
            for cut in (0,1,7,19,37,151,389):
                trees,parts=gate.run(style+f' shared wrapper cut{cut}/{width}',source+
                    f'!(let {pattern} (select 7 {control(cut,extra=extra)} {body}) '
                    f'(Split $p (select 7 {control(2000-cut,extra=extra)} $r)))')
                values=trees[-1][0][1]+parts[-1]['values']
                assert [contribution(x) for x in values]==[contribution(x) for x in final['values']],(style,width,cut,values,final)
                assert len({x[3][1][0][1] for x in values})==3,values
                for field in ('work','closed','satisfied','status'):
                    assert parts[-1][field]==final[field],(style,width,cut,field,parts[-1],final)
            gate.passed_case(style+f' shares each graded argument and its factor once through seven cuts/{width}')

        # A name captured as PeTTa data stays data after later insertion.
        # Prime and HE interpret that same authored position dynamically.
        initial='(= (cached-pair $x) (Pair $x $x)) (= (cached-call) (cached-pair (item))) '
        grade_row=row('(item)','2','3').strip()
        replacement=row('(item)','5','7').strip()
        query='!'+read('(cached-call)')+' '
        program=initial+query+f'!(add-atom &self {grade_row}) '+query
        program+=f'!(remove-atom &self {grade_row}) !(add-atom &self {replacement}) '+query
        program+='!(add-atom &self (= (item) 8)) '+query
        program+=f'!(remove-atom &self {replacement}) '+query
        trees,_=gate.run(style+' prepared plan revision and mixed rows',program)
        values=[contribution(x) for tree in trees for x in tree
                if isinstance(x,list) and x and x[0]=='Weighted']
        expected_dynamic=([
            [['Pair',['item'],['item']],1,[]]]*5
            if getattr(gate,'language','prime')=='petta' else [
            [['Pair',['item'],['item']],1,[]], [['Pair',2,2],3,[3]],
            [['Pair',5,5],7,[7]], [['Pair',5,5],7,[7]],
            [['Pair',8,8],1,[]], [['Pair',8,8],1,[]]])
        assert bag(values)==bag(expected_dynamic),(style,values)
        gate.passed_case(style+' preserves authored data or dynamic callable roles through insertion')

        # Cache a previously callable, unannotated head, then replace its
        # physical rows. All hosts must re-admit the plan at each revision.
        program='(= (item) 1) '+initial+query
        program+='!(remove-atom &self (= (item) 1)) '
        program+=f'!(add-atom &self {grade_row}) '+query
        program+=f'!(remove-atom &self {grade_row}) !(add-atom &self {replacement}) '+query
        program+='!(add-atom &self (= (item) 8)) '+query
        program+=f'!(remove-atom &self {replacement}) '+query
        trees,_=gate.run(style+' callable plan revision and mixed rows',program)
        values=[contribution(x) for tree in trees for x in tree
                if isinstance(x,list) and x and x[0]=='Weighted']
        assert bag(values)==bag([
            [['Pair',1,1],1,[]], [['Pair',2,2],3,[3]],
            [['Pair',5,5],7,[7]], [['Pair',5,5],7,[7]],
            [['Pair',8,8],1,[]], [['Pair',8,8],1,[]]]),(style,values)
        gate.passed_case(style+' revalidates callable source after grade insertion, replacement and removal')

        _,parts=gate.run(style+' pending shared source mutation',source+
            f'!(let {pattern} (select 7 {control(37)} {body}) '
            f'(let $_ (add-atom &self {row("(coin)","8","11").strip()}) '
            f'(select 7 {control(2000)} $r)))')
        assert not parts[-1]['values'] and parts[-1]['closed']=='False',parts
        assert parts[-1]['status']=='invalidated' and parts[-1]['work']==37,parts
        gate.passed_case(style+' keeps the pending shared world when its captured source is invalidated')

    file='guard_and_zero.metta'
    _,obs=gate.run_file(file,authored_fixture(gate, file))
    assert len(obs)==3 and not obs[0]['values'] and obs[0]['closed']=='True',obs
    assert [contribution(v) for v in obs[1]['values']]==[['V',0,[0]]],obs
    assert obs[1]['closed']=='True',obs
    assert not obs[2]['values'] and obs[2]['closed']=='False' and obs[2]['status']=='suspended',obs
    lines=[line.strip('"') for line in gate.receipts[-1]['stdout'].splitlines()]
    assert lines.count('ZERO-BODY')==1 and 'BLOCKED-BODY' not in lines and 'PENDING-BODY' not in lines,lines
    gate.passed_case(file+' separates attachment, false admission and unresolved predicate')

    file='resume_pending_grade.metta'
    source=(authored_fixture(gate, file)).read_text()
    for cut in (0,1,13,36,37,38,71):
        trees,obs=gate.run(file+f' cut{cut}',source.replace('(budget 37)',f'(budget {cut})').replace(
            '(budget 1963)',f'(budget {2000-cut})'))
        first,last=obs
        assert bag([contribution(v) for v in first['values']])==bag([
            ['A',2,[2]],['A',2,[2]],['B',3,[3]]]),obs
        combined=trees[-1][0][1]+last['values']
        assert [contribution(v) for v in combined]==[contribution(v) for v in first['values']],trees
        for field in ('work','closed','satisfied','status'):
            assert first[field]==last[field],(cut,field,obs)
        assert first['closed']=='True',obs
    gate.passed_case(file+' preserves full pending coefficient state and exact work across cuts')


def family_boundaries(gate):
    from test_native_weight_families import families
    families(gate)


def ordering_refusal_recovery(gate):
    from test_native_weight_families import ordering_recovery
    ordering_recovery(gate)


def controller_agreement(gate):
    from test_native_weight_controllers import controllers
    controllers(gate)


def computed_head_authority(gate):
    source = '(= (head:source) +) '
    _, obs = gate.run('computed pure coefficient head', source +
        f'!(select 1 {control()} {read("(weight:where V ((head:source) 2 3))")})')
    assert contribution(obs[-1]['values'][0])[:2] == ['V', 5], obs
    gate.passed_case('computed heads resolve under the existing pure coefficient authority')
    for name, source, coefficient, marker in (
        ('resolved effect', '(= (head:source) println!) ',
         '((head:source) "FORBIDDEN-COMPUTED-HEAD")', 'FORBIDDEN-COMPUTED-HEAD'),
        ('effectful producer', '',
         '((let $_ (println! "FORBIDDEN-HEAD-PRODUCER") +) 2 3)', 'FORBIDDEN-HEAD-PRODUCER'),
    ):
        _, obs = gate.run('computed coefficient '+name, source +
            f'!(select 1 {control()} {read(f"(weight:where V {coefficient})")})')
        assert obs[-1]['status'] == 'fault' and not obs[-1]['values'], obs
        assert marker not in gate.receipts[-1]['stdout'], gate.receipts[-1]
        gate.passed_case('pure authority refuses '+name+' before publication')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=ROOT / 'cetta')
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--cases', type=int, default=12)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    gate = Gate(args)
    try:
        fixed(gate)
        matched_coefficients(gate)
        matched_source_scopes(gate)
        retained_queries(gate)
        retained_query_owners(gate)
        nested_coefficients(gate)
        scoped_reads(gate)
        authored_coefficients(gate)
        admitted_coefficients(gate)
        callable_admission(gate)
        ordered_coefficients(gate)
        implicit_first(gate)
        owned_alternatives(gate)
        authored_programs(gate)
        nested_selection_boundary(gate)
        prime_tuple_boundary(gate)
        authored_algebra_clients(gate)
        authored_pln_client(gate)
        authored_readouts(gate)
        captured_source_unchanged(gate)
        splits(gate, args.cases)
        coefficient_choice_worlds(gate)
        algebras(gate)
        registry_scopes(gate)
        ordinal_priorities(gate)
        real_circuits(gate)
        family_boundaries(gate)
        ordering_refusal_recovery(gate)
        controller_agreement(gate)
        search_clients(gate)
        weight_order(gate)
        weight_licence_progress(gate)
        weight_licence_dependencies(gate)
        weight_handler_authority(gate)
        imported_coefficients(gate)
        authored_keys(gate)
        native_handler_boundaries(gate)
        computed_head_authority(gate)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True, exist_ok=True)
            args.receipt.write_text(json.dumps({
                'binary': str(args.binary),
                'binary_sha256': sha256(args.binary.read_bytes()).hexdigest(),
                'eval_sha256': sha256((ROOT / 'src/eval.c').read_bytes()).hexdigest(),
                'passed': gate.passed, 'receipts': gate.receipts}, indent=2) + '\n')
    print(f'Prime native weights: {gate.passed} passed, 0 failed')


if __name__ == '__main__':
    main()
