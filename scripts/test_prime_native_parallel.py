#!/usr/bin/env python3
"""Native resumable-worker controls, including source occurrences and owned worlds."""
from __future__ import annotations
import argparse
from hashlib import sha256
import json
from pathlib import Path
import random
from test_prime_native_control import Gate, ROOT, bag

SUM = '(= (sum $n) (if (== $n 0) 0 (+ $n (sum (- $n 1))))) '
PATTERN = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'

def policy(workers=2, budget=100000, quantum=40, extra='', evidence=True):
    receipt = '(receipts True) (costs native-events-v1)' if evidence else ''
    return f'(search-policy (control FIFO (workers {workers}) (quantum {quantum}) (budget {budget}) (retain True) {receipt} {extra}))'

def observation(values):
    """Fresh physical IDs may differ, but values and ordered factors may not."""
    if isinstance(values, list):
        if values and values[0] == 'Weighted':
            factors = [f[3] for f in values[3][1]]
            return ['Weighted', observation(values[1]), observation(values[2]), factors]
        return [observation(x) for x in values]
    return values

def check_cost(result):
    fields = dict(result['cost'][2:])
    assert result['cost'][:2] == ['NativeCost', 'native-events-v1']
    counters = {x[1]: x[2] for x in fields['operations']}
    assert fields['work'] == ['Exact', sum(counters.values())]
    assert fields['span'][0] in ('Sequential', 'ForkJoin')
    assert fields['span'][1][0] == 'Exact'
    assert fields['span'][1][1] <= fields['work'][1]
    assert fields['trace-status'] == 'Complete'
    for wave in result.get('parallel-waves', []):
        assert wave[0] == 'NativeParallelWave' and wave[1] > 0
        for branch in wave[2]:
            tag, occurrence, scope, work, span, worker, start, end, phase = branch
            assert tag == 'NativeBranchSlice' and occurrence > 0 and scope > 0
            assert 0 <= span <= work and worker >= 0 and 0 < start <= end and phase in (0, 1)
    return fields

def fixed(gate):
    # Keep this ordinary entry isolated: other declarations change preparation.
    source = '(= (co:id $x) $x)\n!(co:id (Pair (superpose (1 2)) (superpose (3 4))))\n'
    expected = [[['Pair', 1, 3], ['Pair', 1, 4], ['Pair', 2, 3], ['Pair', 2, 4]]]
    for workers in (1, 2, 4):
        trees, _ = gate.run(f'ordinary retained read service workers={workers}', source,
                            arguments=('--num-threads', str(workers)))
        assert trees == expected, (workers, trees)
        assert not gate.receipts[-1]['stderr'], gate.receipts[-1]
        gate.passed_case(f'ordinary retained read service workers={workers}')

    cases = [
        ('independent', SUM + '(= (p) (sum 20)) (= (p) (sum 21))', '(p)', [210, 231]),
        ('duplicate occurrences', SUM + '(= (p) (sum 15)) (= (p) (sum 15))', '(p)', [120, 120]),
        ('shared choice and thread cache lifetime', SUM + '(= (choice) 2) (= (choice) 3) (= (share $x) (Pair (sum $x) (sum $x)))', '(share (choice))', [['Pair', 3, 3], ['Pair', 6, 6]]),
        ('weighted contributions', SUM + '(= (p) (weight:where (sum 15) 2)) (= (p) (weight:where (sum 16) 3))', '(weight:read (algebra 0 1 + *) (p))', None),
        ('zero coefficients remain occurrences', SUM + '(= (p) (weight:where (sum 9) 0)) (= (p) (weight:where (sum 8) 2))', '(weight:read (algebra 0 1 + *) (p))', None),
        ('open return and private variant lifetime', SUM + '(= (p) (Pair (sum 15) $open)) (= (p) (Pair (sum 16) $open))', '(p)', None),
        ('captured callable', SUM + '(= (apply $fn $n) ($fn $n)) (= (p) (apply sum 8)) (= (p) (apply sum 9))', '(p)', [36,45]),
        ('semantic coefficient world', '(= (coin) 2) (= (coin) 3) (= (f $x) (weight:where (Pair $x $x) $x))', '(weight:read (algebra 0 1 + *) (f (coin)))', None),
        ('shared coefficient once', '(= (weighted) (weight:where V 3)) (= (f $x) (Pair $x $x)) (= (p) (f (weighted))) (= (p) (f (weighted)))', '(weight:read (algebra 0 1 + *) (p))', None),
    ]
    for name, source, expression, expected in cases:
        _, serial = gate.run(name + ' serial', source + f' !(select 10 {policy(1)} {expression})')
        _, concurrent = gate.run(name + ' workers', source + f' !(select 10 {policy(2)} {expression})')
        a, b = serial[-1], concurrent[-1]
        assert a['closed'] == b['closed'] == 'True', (name,a,b)
        assert bag(observation(a['values'])) == bag(observation(b['values'])), (name,a,b)
        if expected is not None: assert bag(b['values']) == bag(expected)
        check_cost(a); check_cost(b)
        if name in ('independent', 'duplicate occurrences', 'weighted contributions'):
            assert b.get('parallel-waves'), (name,b)
        gate.passed_case(name)

    # A divergent producer remains live, while a finite peer can satisfy demand.
    _, result = gate.run('divergent peer', '(= (spin) (spin)) (= (p) (spin)) (= (p) Ready) ' +
        f'!(select 1 {policy(2, 3000, 8)} (p))')
    assert result[-1]['values'] == ['Ready'] and result[-1]['satisfied'] == 'True' and result[-1]['closed'] == 'False'
    gate.passed_case('divergent private producer cannot block a ready witness')


def split(gate, count):
    source = SUM + '(= (p) (Pair (sum 20) $open)) (= (p) (Pair (sum 21) $open)) '
    randomizer = random.Random(813)
    for budget in [1,5,10,20,47,200,501] + [randomizer.randrange(1,900) for _ in range(count)]:
        for workers in (1,2):
            trees, results = gate.run(f'split {budget} -> workers {workers}', source +
                f'!(let {PATTERN} (select 10 {policy(2,budget,17,evidence=False)} (p)) '
                f'(Split $v (select 10 {policy(workers,30000,17,evidence=False)} $r)))')
            emitted = trees[-1][0][1] + results[-1]['values']
            assert results[-1]['closed'] == 'True' and len(emitted) == 2
            assert sorted(x[1] for x in emitted) == [210,231]
            assert all(x[0] == 'Pair' and isinstance(x[2],str) and x[2].startswith('$open') for x in emitted)
            gate.passed_case(f'owned open returns survive {budget}-quantum pause and workers={workers}')


def storage(gate):
    source = SUM + '(= (p) (sum 30)) (= (p) (sum 31)) '
    trees, results = gate.run('worker storage', source +
        f'!(let {PATTERN} (select 10 {policy(2,200,9,evidence=False)} (p)) '
        f'(Stored (cost:storage $r) (select 10 {policy(1,30000,9,evidence=False)} $r)))')
    storage = trees[-1][0][1]
    assert storage[:2] == ['NativeStorage','native-owned-storage-v1'], storage
    fields = dict(storage[2:])
    assert fields['ownership'] == 'Complete', storage
    assert fields['accounted-live'][0] == fields['requested-retained'][0] == 'Exact', storage
    assert results[-1]['closed'] == 'True' and bag(results[-1]['values']) == bag([465,496])
    gate.passed_case('private worlds, source indices, leases and variant banks have a complete quiescent census')


def return_dependencies(gate):
    source='(= (first) (superpose (10 20))) '
    source+='(= (coin) (let $_ (println! "RETURN-PRODUCER") (superpose (2 3)))) '
    expected=[['Triple',x,y,y] for x in (10,20) for y in (2,3)]
    for workers in (1,2,4):
        _,results=gate.run(f'owned normalization return workers={workers}',source+
            f'(= (entry $x $y) (select 10 {policy(workers,50000,3)} (Triple $x $y $y))) !(entry (first) (coin))')
        result=results[-1]
        assert result['closed']=='True' and bag(result['values'])==bag(expected),result
        assert gate.receipts[-1]['stdout'].splitlines().count('RETURN-PRODUCER')==2
        if workers>1:
            assert any(x[4]=='Suspended' and x[5]=='Return'
                       for x in result.get('need-barriers',[])),result
        gate.passed_case(f'branch-local normalization callers preserve correlated uses workers={workers}')
    for budget in (17,59,113):
        program=source+f'(= (entry $x $y) (select 10 {policy(2,budget,3,evidence=False)} (Triple $x $y $y))) '
        program+='!(bind! &checkpoint (entry (first) (coin))) '
        program+=f'!(let {PATTERN} &checkpoint (Split $v (select 10 {policy(2,50000,3,evidence=False)} $r)))'
        trees,results=gate.run(f'normalization returns across episodes budget={budget}',program)
        assert results[-1]['closed']=='True' and bag(trees[-1][0][1]+results[-1]['values'])==bag(expected),(trees,results)
        assert gate.receipts[-1]['stdout'].splitlines().count('RETURN-PRODUCER')==2
        gate.passed_case(f'owned normalization task survives episode retirement budget={budget}')

    weighted=source.replace('(first) (superpose (10 20))',
                            '(first) (weight:where (superpose (10 20)) 7)')
    weighted=weighted.replace('(println! "RETURN-PRODUCER") (superpose (2 3))',
                              '(println! "RETURN-PRODUCER") (weight:where (superpose (2 3)) 5)')
    for workers in (1,2):
        _,results=gate.run(f'weighted normalization return workers={workers}',weighted+
            f'(= (entry $x $y) (select 10 {policy(workers,50000,3)} '
            '(weight:read (algebra 0 1 + *) (Triple $x $y $y)))) !(entry (first) (coin))')
        result=results[-1]
        assert result['closed']=='True' and bag(x[1] for x in result['values'])==bag(expected),result
        assert all(x[2]==35 and [f[3] for f in x[3][1]]==[7,5] for x in result['values']),result
        assert gate.receipts[-1]['stdout'].splitlines().count('RETURN-PRODUCER')==2
        gate.passed_case(f'branch-local return retains ordered shared coefficients once workers={workers}')


def equation_dependencies(gate):
    source='(= (coin) (let $_ (println! "ROW-PRODUCER") (superpose (2 3)))) '
    source+='(= (jobs $x) (+ $x $x)) (= (jobs $x) (+ $x $x)) '
    expected=[4,6]*2
    for workers in (1,2,4):
        _,results=gate.run(f'owned equation world workers={workers}',source+
            f'(= (entry $x) (select 10 {policy(workers,50000,3)} (jobs $x))) !(entry (coin))')
        result=results[-1]
        assert result['closed']=='True' and bag(result['values'])==bag(expected),result
        assert gate.receipts[-1]['stdout'].splitlines().count('ROW-PRODUCER')==2
        if workers>1:
            assert any(x[4]=='Suspended' and x[5]=='Equation'
                       for x in result.get('need-barriers',[])),result
        gate.passed_case(f'physical rows own independent worlds with internal shared choice workers={workers}')
    for workers in (1,2,4):
        for budget in (1,29,97,211):
            program=source+f'(= (entry $x) (select 10 {policy(workers,budget,3,evidence=False)} (jobs $x))) '
            program+='!(bind! &checkpoint (entry (coin))) '
            program+=f'!(let {PATTERN} &checkpoint (Split $v (select 10 {policy(workers,50000,3,evidence=False)} $r)))'
            trees,results=gate.run(f'equation worlds across episodes workers={workers} budget={budget}',program)
            assert results[-1]['closed']=='True' and bag(trees[-1][0][1]+results[-1]['values'])==bag(expected),(trees,results)
            assert gate.receipts[-1]['stdout'].splitlines().count('ROW-PRODUCER')==2
            gate.passed_case(f'equation alternative choices survive episode retirement workers={workers} budget={budget}')


def force_dependencies(gate):
    # Matching the outer argument owns one FORCE. Its source refers to an
    # earlier, still lazy cell; transporting that producer must retain the
    # common decision used by the later return, including duplicate rows.
    source = SUM + '(= (coin) (let $_ (println! "SHARED-PRODUCER") (superpose (2 3)))) '
    source += '(= (step $x) (+ $x 1)) (= (consume 3) Three) (= (consume 4) Four) '
    source += '(= (relay $x) (Pair (consume (step $x)) (step $x))) '
    source += '(= (jobs $x) (relay $x)) (= (jobs $x) (sum 30)) '
    expected = [['Pair','Three',3],['Pair','Four',4],465]
    for workers in (1,2,4):
        _,results=gate.run(f'owned FORCE captured dependency workers={workers}',source+
            f'(= (entry $x) (select 10 {policy(workers,50000,3)} (jobs $x))) !(entry (coin))')
        result=results[-1]
        assert result['closed']=='True' and bag(result['values'])==bag(expected),result
        assert gate.receipts[-1]['stdout'].splitlines().count('SHARED-PRODUCER')==1
        check_cost(result)
        if workers>1:
            assert result.get('parallel-waves'),result
            barriers=result.get('need-barriers',[])
            assert any(x[4]=='Suspended' for x in barriers),result
            assert any(x[4]=='Coordinator' for x in barriers),result
        gate.passed_case(f'one captured producer decision feeds matching and later return workers={workers}')
    for budget in (1,17,47,103,233):
        trees,results=gate.run(f'owned FORCE dependency split={budget}',source+
            f'(= (entry $x) (let {PATTERN} (select 10 {policy(2,budget,3,evidence=False)} (jobs $x)) '
            f'(Split $v (select 10 {policy(2,50000,3,evidence=False)} $r)))) !(entry (coin))')
        values=trees[-1][0][1]+results[-1]['values']
        assert results[-1]['closed']=='True' and bag(values)==bag(expected),(trees,results)
        assert gate.receipts[-1]['stdout'].splitlines().count('SHARED-PRODUCER')==1
        gate.passed_case(f'captured FORCE ownership and return survive split={budget}')

    for workers in (1,2,4):
        for budget in (1,47,103,233):
            program=source+f'(= (entry $x) (select 10 {policy(workers,budget,3,evidence=False)} (jobs $x))) '
            program+='!(bind! &checkpoint (entry (coin))) '
            program+=f'!(let {PATTERN} &checkpoint (Split $v (select 10 {policy(workers,50000,3,evidence=False)} $r)))'
            trees,results=gate.run(f'owned dependency across episodes workers={workers} budget={budget}',program)
            assert results[-1]['closed']=='True' and bag(trees[-1][0][1]+results[-1]['values'])==bag(expected),(trees,results)
            assert gate.receipts[-1]['stdout'].splitlines().count('SHARED-PRODUCER')==1
            gate.passed_case(f'captured FORCE and caller survive episode retirement workers={workers} budget={budget}')

    duplicate=source+'(= (consume 3) Three) '
    for workers in (1,2):
        _,results=gate.run(f'captured producer duplicate consumers workers={workers}',duplicate+
            f'(= (entry $x) (select 10 {policy(workers,50000,3)} (jobs $x))) !(entry (coin))')
        assert results[-1]['closed']=='True'
        assert bag(results[-1]['values'])==bag(expected+[['Pair','Three',3]]),results
        assert gate.receipts[-1]['stdout'].splitlines().count('SHARED-PRODUCER')==1
        gate.passed_case(f'physical duplicate rows retain returns while sharing one producer workers={workers}')

    lazy=source+'(= (spin) (spin)) '
    for workers in (1,2):
        _,results=gate.run(f'captured unneeded producer workers={workers}',lazy+
            f'(= (entry $x $unused) (select 10 {policy(workers,50000,3)} (jobs $x))) '
            '!(entry (coin) (spin))')
        assert results[-1]['closed']=='True' and bag(results[-1]['values'])==bag(expected),results
        gate.passed_case(f'detachment never forces a captured but unneeded divergent cell workers={workers}')

    # Same source syntax does not identify a cell, and two independent cells
    # may make different choices. Their actual branch counts are retained.
    independent=SUM+'(= (coin) (superpose (2 3))) (= (step $x) (+ $x 1)) '
    independent+='(= (consume 3) Three) (= (consume 4) Four) '
    independent+='(= (jobs $x $y) (Pair (consume (step $x)) (consume (step $y)))) '
    independent+='(= (jobs $x $y) (sum 30)) '
    independent_expected=[['Pair',a,b] for a in ('Three','Four') for b in ('Three','Four')]+[465]
    for workers in (1,2):
        _,results=gate.run(f'independent captured cells workers={workers}',independent+
            f'(= (entry $x $y) (select 10 {policy(workers,50000,3)} (jobs $x $y))) !(entry (coin) (coin))')
        assert results[-1]['closed']=='True' and bag(results[-1]['values'])==bag(independent_expected),results
        gate.passed_case(f'equal producer syntax preserves independent cell choices workers={workers}')

    weighted=source.replace('(println! "SHARED-PRODUCER") (superpose (2 3))',
                            '(println! "SHARED-PRODUCER") (weight:where (superpose (2 3)) 5)')
    for workers in (1,2):
        _,results=gate.run(f'captured weighted producer workers={workers}',weighted+
            f'(= (entry $x) (select 10 {policy(workers,50000,3)} '
            '(weight:read (algebra 0 1 + *) (jobs $x)))) !(entry (coin))')
        result=results[-1]
        assert result['closed']=='True' and len(result['values'])==3,result
        contributions={json.dumps(x[1]):x for x in result['values']}
        for value in expected[:2]:
            contribution=contributions[json.dumps(value)]
            assert contribution[2]==5 and [f[3] for f in contribution[3][1]]==[5],result
        assert contributions['465'][2]==1,result
        assert gate.receipts[-1]['stdout'].splitlines().count('SHARED-PRODUCER')==1
        gate.passed_case(f'captured coefficient world returns once through all shared uses workers={workers}')


def fuel_and_advice(gate):
    source = SUM + '(= (p) (sum 10)) (= (p) (sum 11)) '
    for workers in (1,2):
        _, results = gate.run(f'finite semantic depth workers={workers}', source +
            f'!(select 10 {policy(workers)} (p))', arguments=('--fuel','256'))
        result = results[-1]
        assert result['closed'] == 'True' and bag(result['values']) == bag([55,66]), result
        if workers == 2: assert result.get('parallel-waves'), result
        check_cost(result)
        gate.passed_case(f'finite semantic path fuel survives owned worker transport workers={workers}')
        gate.run(f'exhausted semantic depth workers={workers}', source +
            f'!(select 10 {policy(workers)} (p))', arguments=('--fuel','8'), expected_exit=1)
        assert '(Incomplete fuel-exhausted)' in gate.receipts[-1]['stdout'], gate.receipts[-1]
        gate.passed_case(f'semantic fuel exhaustion remains incomplete workers={workers}')
        trees, results = gate.run(f'finite semantic fuel split workers={workers}', source +
            f'!(let {PATTERN} (select 10 {policy(2,30,7,evidence=False)} (p)) '
            f'(Split $v (select 10 {policy(workers,10000,7,evidence=False)} $r)))',
            arguments=('--fuel','256'))
        assert results[-1]['closed'] == 'True', results
        assert bag(trees[-1][0][1] + results[-1]['values']) == bag([55,66]), (trees,results)
        gate.passed_case(f'observation pause retains remaining semantic fuel workers={workers}')

    scored = SUM + '(= (spin) (spin)) (= (p) (sum 12)) (= (p) (sum 13)) '
    for score in ['(sum 30)', '(spin)']:
        _, results = gate.run(f'worker pure advice {score}', scored +
            f'!(select 2 {policy(2,10000,7,extra=f"(key $candidate {score})").replace("control FIFO", "control WO")} (p))')
        result = results[-1]
        assert result['satisfied'] == 'True' and bag(result['values']) == bag([78,91]), result
        assert result.get('parallel-waves'), result
        gate.passed_case(f'owned pure advice has no answer publication authority: {score}')
    for budget in [20,65,170]:
        old = policy(2,budget,7,extra='(key $candidate (sum 100))',evidence=False).replace('control FIFO','control WO')
        new = policy(2,10000,7,extra='(key $candidate 0)',evidence=False).replace('control FIFO','control WO')
        trees,results=gate.run(f'worker score generation {budget}',scored+
            f'!(let {PATTERN} (select 2 {old} (p)) (Split $v (select 2 {new} $r)))')
        assert results[-1]['satisfied']=='True', results
        assert bag(trees[-1][0][1]+results[-1]['values'])==bag([78,91]),(trees,results)
        gate.passed_case(f'worker score return stays in its captured generation across regrade at {budget}')


def effect_barriers(gate):
    # The equation's future contains an effect, while its current computation
    # is ordinary private work. The coordinator executes the effect exactly
    # once; the peer's pure work retains its own worker continuation.
    source = SUM + '(= (jobs) (let $n (sum 12) (let $_ (println! "OWNED-EFFECT") (Done $n)))) '
    source += '(= (jobs) (sum 13)) '
    for workers in (1,2):
        _, results = gate.run(f'effect after recursive work workers={workers}', source +
            f'!(select 10 {policy(workers,30000,3)} (jobs))')
        result=results[-1]
        assert result['closed']=='True' and bag(result['values'])==bag([['Done',78],91]), result
        assert gate.receipts[-1]['stdout'].splitlines().count('OWNED-EFFECT')==1
        if workers==2:
            assert result.get('parallel-waves'),result
            barriers=result.get('effect-barriers',[])
            assert any(x[3]=='Suspended' for x in barriers),result
            assert any(x[3]=='Coordinator' for x in barriers),result
        gate.passed_case(f'ordinary native effect rendezvous preserves recursive return and peer workers={workers}')
    for budget in [1,7,19,47,100,200]:
        trees,results=gate.run(f'owned effect split {budget}',source+
            f'!(let {PATTERN} (select 10 {policy(2,budget,3,evidence=False)} (jobs)) '
            f'(Split $v (select 10 {policy(2,30000,3,evidence=False)} $r)))')
        assert results[-1]['closed']=='True',results
        assert bag(trees[-1][0][1]+results[-1]['values'])==bag([['Done',78],91]),(trees,results)
        assert gate.receipts[-1]['stdout'].splitlines().count('OWNED-EFFECT')==1
        gate.passed_case(f'one effect with its retained return across {budget}-quantum split')

    # Force once before the barrier, then use the same cell after re-entry.
    # Reconstructing source syntax here would create crossed or extra pairs.
    source=SUM+'(= (coin) 2) (= (coin) 3) '
    source+='(= (shared $x) (let $left (sum $x) (let $_ (println! "SHARED-BARRIER") (Pair $left (sum $x))))) '
    source+='(= (jobs) (shared (coin))) (= (jobs) (sum 30)) '
    for budget in [0,17,87,233]:
        if budget:
            resumed=policy(2,30000,3,extra='(receipts True)',evidence=False)
            program=f'!(let {PATTERN} (select 10 {policy(2,budget,3,evidence=False)} (jobs)) (Split $v (select 10 {resumed} $r)))'
        else:
            program=f'!(select 10 {policy(2,30000,3)} (jobs))'
        trees,results=gate.run(f'cached choice through effect barrier split={budget}',source+program)
        values=(trees[-1][0][1] if budget else [])+results[-1]['values']
        assert results[-1]['closed']=='True' and bag(values)==bag([['Pair',3,3],['Pair',6,6],465]),(trees,results)
        assert gate.receipts[-1]['stdout'].splitlines().count('SHARED-BARRIER')==2
        assert any(x[3]=='Suspended' for r in results for x in r.get('effect-barriers',[])),results
        gate.passed_case(f'cached cell decision survives worker effect re-entry and split={budget}')

    source=SUM+'(= (spin) (spin)) '
    source+='(= (jobs) (let $n (sum 3) (let $_ (println! "PINNED-PENDING") (spin)))) '
    source+='(= (jobs) (sum 30)) '
    _,results=gate.run('session retires a pinned unfinished worker world',source+
        f'!(select 10 {policy(2,400,3)} (jobs))')
    assert results[-1]['closed']=='False' and results[-1]['status']=='suspended',results
    assert results[-1]['residual'][:2]==['NativeHandle','prime-search'],results
    assert gate.receipts[-1]['stdout'].splitlines().count('PINNED-PENDING')==1
    assert any(x[3]=='Coordinator' for x in results[-1].get('effect-barriers',[])),results
    gate.passed_case('pending worker retirement detaches caches before escaped world teardown')

    source='!(bind! &store (new-space)) !(bind! &alias &store) '+SUM
    source+='(= (jobs) (let $n (sum 10) (let $_ (add-atom &store (Saved $n)) Done))) '
    source+='(= (jobs) (sum 11)) '
    trees,results=gate.run('worker effect retains published space aliases',source+
        f'!(select 10 {policy(2,30000,3)} (jobs)) !(collapse (match &alias (Saved $n) $n))')
    assert results[-1]['closed']=='True' and bag(results[-1]['values'])==bag(['Done',66]), results
    assert trees[-1]==[[55]],trees
    assert results[-1].get('parallel-waves'),results
    gate.passed_case('worker rendezvous mutates the original published Space and preserves its alias')

    source='!(bind! &out (new-space)) '+SUM
    source+='(= (jobs) (let $n (sum 10) (let $cell (new-state $n) (let $_ (add-atom &out (Saved $cell)) Done)))) '
    source+='(= (jobs) (sum 11)) '
    trees,results=gate.run('worker resource escapes into persistent owner',source+
        f'!(select 10 {policy(2,30000,3)} (jobs)) !(match &out (Saved $cell) (get-state $cell))')
    assert results[-1]['closed']=='True' and bag(results[-1]['values'])==bag(['Done',66]),results
    assert trees[-1]==[55],trees
    gate.passed_case('escaped StateCell survives answer and private continuation retirement')

    source=SUM+'(= (coefficient) (let $_ (println! "COEFFICIENT-MUST-NOT-RUN") 2)) '
    source+='(= (jobs) (weight:where A (coefficient))) (= (jobs) (sum 11)) '
    _,results=gate.run('worker semantic coefficient authority',source+
        f'!(select 10 {policy(2,30000,3)} (jobs))')
    assert results[-1]['status']=='fault',results
    assert 'COEFFICIENT-MUST-NOT-RUN' not in gate.receipts[-1]['stdout']
    gate.passed_case('worker pure semantic grading cannot acquire ordinary effect authority')


def session_context(gate):
    source=SUM+'(= (session-job) (Session (sum 5) (id:policy) (context:self))) '
    source+='(= (session-job) (Session (sum 6) (id:policy) (context:self))) '
    cases=[(name,name,'program') for name in
           ('identity-j','identity-scoped','identity-uip','identity-univalence')]
    cases += [('context-self-home','identity-j','home'),
              ('context-self-here','identity-j','here')]
    for profile,identity,context in cases:
        for budget in (0,43):
            if budget:
                expression=(f'!(let {PATTERN} (select 10 {policy(2,budget,3,evidence=False)} (session-job)) '
                    f'(Split $v (select 10 {policy(2,30000,3,evidence=False)} $r)))')
            else:
                expression=f'!(select 10 {policy(2,30000,3)} (session-job))'
            trees,results=gate.run(f'captured session {profile} split={budget}',
                source+expression+' !(Session (id:policy) (context:self))',profile=profile)
            values=(trees[-2][0][1] if budget else [])+results[-1]['values']
            assert results[-1]['closed']=='True',results
            assert bag(values)==bag([['Session',15,identity,context],
                                    ['Session',21,identity,context]]),(trees,results)
            assert trees[-1]==[['Session',identity,context]],trees
            if not budget: assert results[-1].get('parallel-waves'),results
            gate.passed_case(f'captured {profile} survives worker/coordinator entry and restores caller split={budget}')


def receipt_capture(gate):
    source = SUM + '(= (jobs) (sum 12)) (= (jobs) (sum 13)) '
    # A nested agenda without its own meter still runs inside the outer
    # observation. Both physical worker bodies must reach that observation.
    for workers in (1, 2):
        _, results = gate.run(f'inherited outer cost workers={workers}', source +
            f'!(cost:run (select 10 {policy(workers,30000,5,evidence=False)} (jobs)))')
        outer = results[-1]
        assert outer['closed'] == 'True', results
        inner = {'values': outer['values'][0][1], **dict(outer['values'][0][2:])}
        assert inner['closed'] == 'True' and bag(inner['values']) == bag([78,91]), inner
        assert 'cost' not in inner and 'parallel-waves' not in inner, inner
        fields = check_cost(outer)
        counts = {x[1]: x[2] for x in fields['operations']}
        assert counts['equation-activation'] == 29, fields
        if workers == 2:
            assert counts['worker-dispatch'] > 0 and counts['worker-join'] > 0, fields
            assert fields['span'][0] == 'ForkJoin', fields
        gate.passed_case(f'outer meter retains receipt-free inner worker execution workers={workers}')

    requested = policy(2,30000,5,extra='(receipts True)',evidence=False)
    for budget in (17,30000):
        trees, results = gate.run(f'late receipt observation {budget}', source +
            f'!(let {PATTERN} (select 10 {policy(2,budget,5,evidence=False)} (jobs)) '
            f'(History $v $w (select 10 {requested} $r)))')
        before = trees[-1][0]
        result = results[-1]
        assert result['closed'] == 'True' and bag(before[1]+result['values']) == bag([78,91]), (trees,results)
        assert result['receipt-capture'] == ['ReceiptCapture',
            ['work-intervals', [['WorkInterval',before[2],result['work']]]],
            ['outside','Unrecorded']], result
        assert 'cost' not in result, result
        if budget == 30000:
            assert result['work'] == before[2] and not result.get('parallel-waves'), result
        else:
            assert result.get('parallel-waves'), result
            assert all(branch[2:5] == ['Untracked']*3
                       for wave in result['parallel-waves'] for branch in wave[2]), result
        gate.passed_case(f'late receipt request records only its interval after {budget}-quantum allowance')

    capture_pattern = PATTERN[:-1] + ' (receipt-capture $capture) (consumer-revisions $revisions) (cost $firstcost))'
    second_pattern = ('(Observation $mv (satisfied $ms) (closed $mc) (residual $mr) '
                      '(work $mw) (status $mt) (cost $secondcost))')
    trees, results = gate.run('toggle history retains cumulative metering', source +
        f'!(let {capture_pattern} (select 10 {policy(1,17,5)} (jobs)) '
        f'(let {second_pattern} (select 10 {policy(2,40,5,evidence=False)} $r) '
        f'(Toggle $v $mv $w $mw $firstcost $secondcost (select 10 {requested} $mr))))')
    value = trees[-1][0]
    result = results[-1]
    assert bag(value[1]+value[2]+result['values']) == bag([78,91]), (trees,results)
    assert result['receipt-capture'] == ['ReceiptCapture',
        ['work-intervals', [['WorkInterval',0,value[3]], ['WorkInterval',value[4],result['work']]]],
        ['outside','Unrecorded']], result
    costs = [dict(x[2:]) for x in (value[5],value[6],result['cost'])]
    assert len({x['scope'] for x in costs}) == 1, costs
    assert costs[0]['work'][1] < costs[1]['work'][1] < costs[2]['work'][1], costs
    assert {x[1]:x[2] for x in costs[-1]['operations']}['equation-activation'] == 29, costs
    check_cost(result)
    gate.passed_case('disabling and resuming optional history keeps the original cumulative cost scope')


def nested_allowances(gate):
    source = SUM + '(= (jobs) (sum 12)) (= (jobs) (sum 13)) '
    for workers in (1,2,4):
        inner = f'(select 10 (search-policy (control FIFO (workers {workers}) (quantum 5))) (jobs))'
        def outer(budget):
            return policy(1,budget,evidence=False)
        _, completed = gate.run(f'nested complete width={workers}', source +
            f'!(select 1 {outer(30000)} {inner})')
        assert completed[-1]['values'] == [[78,91]] and completed[-1]['satisfied'] == 'True', completed
        _, shortage = gate.run(f'nested finite closure width={workers}', source +
            f'!(select 2 {outer(30000)} {inner})')
        assert shortage[-1]['values'] == [[78,91]] and shortage[-1]['satisfied'] == 'False', shortage
        assert shortage[-1]['closed'] == 'True' and shortage[-1]['status'] == 'closed', shortage
        gate.passed_case(f'nested width={workers} distinguishes finite shortage from suspension')
        for first,second in ((0,0),(0,17),(1,1),(17,23),(48,0),(100,233),(476,476)):
            _, combined = gate.run(f'nested combined {workers} {first}+{second}', source +
                f'!(select 1 {outer(first+second)} {inner})')
            program = source + f'!(let {PATTERN} (select 1 {outer(first)} {inner}) '
            program += f'(Split $v $w (select 1 {outer(second)} $r)))'
            trees,resumed = gate.run(f'nested split {workers} {first}+{second}', program)
            assert trees[-1][0][2] <= first, (trees,resumed)
            assert resumed[-1]['work'] <= first+second, (trees,resumed)
            assert trees[-1][0][1]+resumed[-1]['values'] == combined[-1]['values'], (combined,resumed)
            for field in ('work','closed','satisfied','status'):
                assert resumed[-1][field] == combined[-1][field], (workers,first,second,field,combined,resumed)
            # Inspecting a prefix never replaces the complete suspended return
            # graph: its original handle can still produce the ordered result.
            middle = '(Observation $mv (satisfied $ms) (closed $mc) (residual $mr) (work $mw) (status $mt))'
            program = source + f'!(let {PATTERN} (select 1 {outer(first)} {inner}) '
            program += f'(let {middle} (select 1 {outer(second)} $r) '
            program += f'(Finished $v $mv (select 1 {outer(30000)} $mr))))'
            trees, finished = gate.run(f'nested residual {workers} {first}+{second}', program)
            answer = trees[-1][0]
            assert answer[1]+answer[2]+finished[-1]['values'] == [[78,91]], (trees,finished)
            assert finished[-1]['satisfied']=='True', finished
            gate.passed_case(f'nested width={workers} retains ordered return and both allowances at {first}+{second}')


def generated(gate, count):
    randomizer = random.Random(644)
    for i in range(count):
        inputs = [randomizer.randrange(1,25) for _ in range(randomizer.randrange(2,6))]
        source = SUM + ' '.join(f'(= (p) (sum {n}))' for n in inputs)
        _, result = gate.run(f'generated occurrence bag {i}', source +
            f' !(select 100 {policy(2,100000,randomizer.randrange(3,31))} (p))')
        assert bag(result[-1]['values']) == bag([n*(n+1)//2 for n in inputs])
        assert result[-1]['closed'] == 'True'
        check_cost(result[-1])
        gate.passed_case(f'generated duplicate-sensitive source bag {i}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,default=ROOT/'cetta')
    parser.add_argument('--timeout',type=int,default=45)
    parser.add_argument('--cases',type=int,default=8)
    parser.add_argument('--receipt',type=Path)
    args=parser.parse_args();args.binary=args.binary.resolve();gate=Gate(args)
    try:
        fixed(gate);split(gate,args.cases);storage(gate);force_dependencies(gate);equation_dependencies(gate);return_dependencies(gate);fuel_and_advice(gate);effect_barriers(gate);session_context(gate);receipt_capture(gate);nested_allowances(gate);generated(gate,args.cases)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True,exist_ok=True)
            args.receipt.write_text(json.dumps({'binary_sha256':sha256(args.binary.read_bytes()).hexdigest(),
                'passed':gate.passed,'receipts':gate.receipts},indent=2)+'\n')
    print(f'Prime native parallelism: {gate.passed} passed, 0 failed')

if __name__=='__main__':main()
