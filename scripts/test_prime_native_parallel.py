#!/usr/bin/env python3
"""Native resumable-worker controls, including source occurrences and owned worlds."""
from __future__ import annotations
import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import platform
import random
import statistics
from test_prime_native_control import Gate, ROOT, bag, parse, observations

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

def ordinary(gate):
    # Keep this ordinary entry isolated: other declarations change preparation.
    source = '(= (co:id $x) $x)\n!(co:id (Pair (superpose (1 2)) (superpose (3 4))))\n'
    expected = [[['Pair', 1, 3], ['Pair', 1, 4], ['Pair', 2, 3], ['Pair', 2, 4]]]
    for workers in (1, 2, 4):
        trees, _ = gate.run(f'ordinary retained read service workers={workers}', source,
                            arguments=('--num-threads', str(workers)))
        assert trees == expected, (workers, trees)
        assert not gate.receipts[-1]['stderr'], gate.receipts[-1]
        gate.passed_case(f'ordinary retained read service workers={workers}')

    ordinary = [
        ('recursive scalar', SUM + '!(sum 20)', [[210]]),
        ('unused divergent argument',
         '(= (silent) (silent)) (= (keep $ignored) (Box 7)) !(keep (silent))',
         [[['Box', 7]]]),
        ('shared argument choice',
         '(= (coin) 2) (= (coin) 3) (= (pair $x) (Pair $x $x)) !(pair (coin))',
         [[['Pair', 2, 2], ['Pair', 3, 3]]]),
        ('duplicate shared choice and independent occurrences',
         '(= (duplicate) False) (= (duplicate) False) '
         '(= (twice $x) (Pair $x $x)) '
         '!(twice (duplicate)) !(Pair (duplicate) (duplicate))',
         [[['Pair', 'False', 'False']] * 2,
          [['Pair', 'False', 'False']] * 4]),
        ('independent service occurrences',
         '(= (leaf) (superpose (4 6))) (= (pair-sum) (+ (leaf) (leaf))) !(pair-sum)',
         [[8, 10, 10, 12]]),
        ('constructor alternatives after recursive field',
         SUM + ''.join(f'(= (jobs) (Pair (sum {n}) (superpose (A B)))) '
                       for n in range(20, 24)) + '!(jobs)',
         [[['Pair', n * (n + 1) // 2, tag]
           for n in range(20, 24) for tag in ('A', 'B')]]),
        ('constructor alternatives before recursive field',
         SUM + '(= (jobs) (Pair (superpose (A B)) (sum 20))) !(jobs)',
         [[['Pair', 'A', 210], ['Pair', 'B', 210]]]),
        ('duplicate constructor alternatives',
         SUM + '(= (jobs) (Pair (sum 20) (superpose (A A B)))) '
         '(= (jobs) (Pair (sum 20) (superpose (A A B)))) !(jobs)',
         [[['Pair', 210, tag] for tag in ('A', 'A', 'B', 'A', 'A', 'B')]]),
        ('empty constructor field has no answer',
         SUM + '(= (jobs) (Pair (sum 20) (empty))) !(collapse (jobs))',
         [[[]]]),
        ('tuple return retains caller refinement outside value support',
         '(scope:profile mercury-implicit per-call reference) '
         '(= (refine $x) (let $x s2 (Got ((f x))))) '
         '!(let $r (refine $y) (Pair $y $r)) '
         '!(collapse (let $y other (refine $y)))',
         [[['Pair', 's2', ['Got', [['f', 'x']]]]], [[]]]),
    ]
    for name, source, expected in ordinary:
        for workers in (1, 2, 4):
            trees, _ = gate.run(f'ordinary {name} workers={workers}', source,
                                arguments=('--num-threads', str(workers)))
            assert trees == expected, (name, workers, trees)
            assert not gate.receipts[-1]['stderr'], gate.receipts[-1]
            gate.passed_case(f'ordinary {name} workers={workers}')

    source = ('(= (leaf) (Node $local $local)) '
              '(= (open-result) (Box (leaf))) !(open-result)')
    for workers in (1, 2, 4):
        trees, _ = gate.run(f'ordinary captured open result workers={workers}', source,
                            arguments=('--num-threads', str(workers)))
        assert len(trees) == 1 and len(trees[0]) == 1, trees
        value = trees[0][0]
        assert len(value) == 2 and value[0] == 'Box', value
        node = value[1]
        assert len(node) == 3 and node[0] == 'Node', node
        assert isinstance(node[1], str) and node[1].startswith('$local') and node[1] == node[2], node
        assert not gate.receipts[-1]['stderr'], gate.receipts[-1]
        gate.passed_case(f'ordinary captured open result workers={workers}')

    # Reuse the maintained clients: returned suspensions stay inert, and a
    # computed higher-order head still dispatches its proof-relevant call.
    for fixture in ('tests/prime/need_native_operands.metta',
                    'examples/prime/prime_native_mil_list_map_rel.metta'):
        path = ROOT / fixture
        expected = path.with_suffix('.expected').read_text()
        for workers in (1, 2, 4):
            gate.run_file(f'ordinary {fixture} workers={workers}', path,
                          arguments=('--num-threads', str(workers)))
            assert gate.receipts[-1]['stdout'] == expected, gate.receipts[-1]
            assert not gate.receipts[-1]['stderr'], gate.receipts[-1]
            gate.passed_case(f'ordinary {fixture} workers={workers}')


def measure_ordinary(gate):
    """Measure complete processes after correctness, retaining answer multiplicity."""
    args = gate.args
    directory = args.benchmark_directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    gate.measurement_conditions = {
        'system': platform.system(), 'machine': platform.machine(),
        'logical_cpus': os.cpu_count(),
    }
    if hasattr(os, 'getloadavg'):
        gate.measurement_conditions['load_average_start'] = os.getloadavg()
    if hasattr(os, 'sched_getaffinity'):
        gate.measurement_conditions['cpu_affinity'] = sorted(os.sched_getaffinity(0))
    if hasattr(os, 'getpriority'):
        gate.measurement_conditions['nice'] = os.getpriority(os.PRIO_PROCESS, 0)
    size = args.benchmark_size
    cases = [
        ('balanced', [size + i for i in range(4)], 0),
        ('small', [max(1, size // 1000) + i for i in range(4)], 0),
        ('unbalanced', [max(1, size // 20)] * 12 +
         [size * 3 // 2, max(1, size // 20), max(1, size // 3), max(1, size // 40)], 0),
        ('background-empty', [max(1, size // 4) + i for i in range(4)], 0),
        ('background-populated', [max(1, size // 4) + i for i in range(4)],
         args.benchmark_facts),
    ]
    measurements = []
    for name, inputs, background in cases:
        source = (SUM + '\n' +
                  ''.join(f'(background {i})\n' for i in range(background)) +
                  ''.join(f'(= (jobs) (Pair (sum {n}) (superpose (A B))))\n'
                          for n in inputs) + '!(jobs)\n')
        path = directory / f'{name}.metta'
        path.write_text(source)
        expected = [['Pair', n * (n + 1) // 2, tag]
                    for n in inputs for tag in ('A', 'B')]
        samples = {workers: [] for workers in (1, 2, 4)}
        for repetition in range(args.benchmark_repetitions):
            order = (1, 2, 4) if repetition % 2 == 0 else (4, 2, 1)
            for workers in order:
                trees, _ = gate.run_file(
                    f'measure {name} repetition={repetition} workers={workers}', path,
                    arguments=('--num-threads', str(workers)))
                assert len(trees) == 1 and bag(trees[0]) == bag(expected), (
                    name, workers, trees, expected)
                row = gate.receipts[-1]
                assert not row['stderr'], row
                # The workload is retained once in the measurement directory.
                row.pop('source')
                samples[workers].append({
                    'seconds': row['seconds'], 'cpu_seconds': row['cpu_seconds']})
                gate.passed_case(f'measured {name} repetition={repetition} workers={workers}')
        elapsed = {workers: statistics.median(s['seconds'] for s in values)
                   for workers, values in samples.items()}
        measurement = {
            'case': name, 'source': str(path), 'source_sha256': sha256(source.encode()).hexdigest(),
            'background_facts': background, 'inputs': inputs, 'samples': samples,
            'median_seconds': elapsed,
            'elapsed_ratio_vs_one_thread': {w: elapsed[1] / t for w, t in elapsed.items()},
            'elapsed_ratio_two_to_four_threads': elapsed[2] / elapsed[4],
        }
        measurements.append(measurement)
        print(f'MEASURE: {name} median process seconds {elapsed}', flush=True)
    gate.measurements = measurements
    if hasattr(os, 'getloadavg'):
        gate.measurement_conditions['load_average_end'] = os.getloadavg()


def fixed(gate):
    ordinary(gate)
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


def captured_query_publications(gate, history_controls=False):
    """Own writes keep a captured query; new reads see the reached world."""
    def source(facts, pattern, body):
        return ('!(bind! &facts (new-space))\n' +
                ''.join(f'!(add-atom &facts (Fact {x}))\n' for x in facts) +
                f'(= (selfwrite) (match &facts {pattern} {body}))\n')

    old = ['A', 'A', 'B']
    pairs = [['Pair', x, y] for x in old for y in old]
    cases = [
        ('append matching rows', source(old, '(Fact $x)',
            '(chain (add-atom &facts (Fact New)) $ignored $x)'), old, old + ['New'] * 3),
        ('remove captured rows', source(old, '(Fact $x)',
            '(chain (remove-atom &facts (Fact $x)) $ignored $x)'), old, []),
        ('later conjunctive factors', source(old, '(, (Fact $x) (Fact $y))',
            '(chain (add-atom &facts (Fact New)) $ignored (Pair $x $y))'), pairs, old + ['New'] * 9),
        ('later alternative factors', source(old, '(| (Fact $x) (Fact $x))',
            '(chain (add-atom &facts (Fact New)) $ignored $x)'), old * 2, old + ['New'] * 6),
        ('fresh read after own write', source(['A'], '(Fact $x)',
            '(chain (add-atom &facts (Fact B)) $ignored (match &facts (Fact $y) (Pair $x $y)))'),
            [['Pair', 'A', 'A'], ['Pair', 'A', 'B']], ['A', 'B']),
    ]
    state = '\n!(collapse (get-atoms &facts))\n'
    for name, setup, expected, final in cases:
        expected_world = [['Fact', x] for x in final]
        trees, _ = gate.run('ordinary ' + name, setup + '!(selfwrite)' + state)
        assert bag(trees[-2]) == bag(expected) and bag(trees[-1][0]) == bag(expected_world), trees
        gate.passed_case('ordinary ' + name + ' uses the finite captured bag')
        for workers in (1, 2, 4):
            trees, results = gate.run(f'{name} workers={workers}', setup +
                f'!(select 100 {policy(workers, quantum=3, evidence=False)} (selfwrite))' + state)
            assert results[-1]['closed'] == 'True' and results[-1]['status'] != 'invalidated', results
            assert bag(results[-1]['values']) == bag(expected), results
            assert bag(trees[-1][0]) == bag(expected_world), trees
            gate.passed_case(f'own {name} preserves answers and exact effects workers={workers}')
            for cut in ((0, 17, 41) if name == 'append matching rows' else (17,)):
                trees, results = gate.run(f'{name} cut={cut} workers={workers}', setup +
                    f'!(let {PATTERN} (select 100 {policy(workers,cut,3,evidence=False)} (selfwrite)) '
                    f'(Split $v (select 100 {policy(workers,100000,3,evidence=False)} $r)))' + state)
                values = trees[-2][0][1] + results[-1]['values']
                assert results[-1]['closed'] == 'True' and bag(values) == bag(expected), (trees, results)
                assert bag(trees[-1][0]) == bag(expected_world), trees
                gate.passed_case(f'own {name} survives cut={cut} workers={workers} without replay')

    setup = cases[0][1]
    for workers in (2, 4):
        trees, results = gate.run(f'own publication beside worker peer {workers}', setup + SUM +
            '(= (jobs) (selfwrite)) (= (jobs) (sum 20)) ' +
            f'!(select 100 {policy(workers,quantum=3)} (jobs))' + state)
        assert results[-1]['closed'] == 'True' and bag(results[-1]['values']) == bag(old + [210]), results
        assert bag(trees[-1][0]) == bag([['Fact', x] for x in old + ['New'] * 3]), trees
        assert results[-1].get('parallel-waves'), results
        gate.passed_case(f'own query publication composes with an actual worker peer {workers}')

    # Extended host profiles reuse the same captured-query service and their
    # own evaluation rules. Their default profiles remain separate controls.
    for language in ('he', 'petta'):
        for name, setup, expected, final in (cases[0], cases[1], cases[-1]):
            trees, results = gate.run(f'{language} own {name}', setup +
                f'!(select 100 {policy(2,quantum=3,evidence=False)} (selfwrite))' + state,
                language=language, profile='extended')
            if language == 'petta':
                trees = parse(gate.receipts[-1]['stdout'])
                results = list(observations(trees))
            assert results[-1]['closed'] == 'True' and bag(results[-1]['values']) == bag(expected), results
            final_world = trees[-1] if language == 'petta' else trees[-1][0]
            assert bag(final_world) == bag([['Fact', x] for x in final]), trees
            gate.passed_case(f'{language} extended own {name} retains its captured return')

    if history_controls:
        recording = '(history (selections 2048) (nodes 4096) (bytes 1048576) (owners 10000))'
        setup = cases[0][1]
        for cut in (17, 41):
            def program(observer, stop=''):
                captured_pattern = PATTERN[:-1] + ' (history $h))' if observer else PATTERN
                return (setup + f'!(bind! &before (select 100 {policy(2,cut,3,extra=observer,evidence=False)} (selfwrite)))\n' +
                    '!(let $report &before $report)\n' + stop + f'!(let {captured_pattern} &before (Split $v '
                    f'(select 100 {policy(2,100000,3,extra="(history False)",evidence=False)} $r)))' + state)
            reference_trees, reference = gate.run(f'own publication history reference {cut}', program(''))
            for forget in (False, True):
                stop = '!(history:forget &before 100000)\n' if forget else ''
                trees, results = gate.run(f'own publication history cut={cut} forget={forget}', program(recording, stop))
                for left, right in zip(reference, results):
                    assert all(left[key] == right[key] for key in
                               ('values', 'work', 'satisfied', 'closed', 'status')), (left, right)
                assert len(reference) == len(results) == 2 and trees[-1] == reference_trees[-1], (trees, results)
                assert bag(trees[-2][0][1] + results[-1]['values']) == bag(old), (trees, results)
                if forget:
                    assert 'history' not in results[-1], results[-1]
                else:
                    assert ['recording', 'False'] in results[-1]['history'][1:], results[-1]
                gate.passed_case(f'own publication record/stop/forget cut={cut} forget={forget} keeps exact work')


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


def shared_producer_boundaries(gate):
    source = ('(= (share:spin $k) (if (< $k 1) 1 (+ 1 (share:spin (- $k 1))))) '
              '(= (share:pair $p) (Pair (force $p) (force $p))) '
              '(= (share:one $p) (force $p)) '
              '(= (share:deny $p) '
              '(if-error (force (suspension:restrict $p (suspension:rights inspect))) Denied Leaked)) ')
    cases = [
        ('branching promise', '(let $_ (share:spin 40) (superpose (A B)))',
         '(superpose ((share:pair $p) (share:pair $p)))',
         [['Pair', 'A', 'A'], ['Pair', 'B', 'B']] * 2),
        ('equal distinct producer branches', '(let $_ (share:spin 40) (superpose (A A)))',
         '(superpose ((share:pair $p) (share:pair $p)))', [['Pair', 'A', 'A']] * 4),
        ('empty producer', '(let $_ (share:spin 40) (empty))',
         '(superpose ((share:one $p) (share:one $p)))', []),
        ('force rights allowed first', '(share:spin 40)',
         '(superpose ((share:one $p) (share:deny $p)))', [41, 'Denied']),
        ('force rights denied first', '(share:spin 40)',
         '(superpose ((share:deny $p) (share:one $p)))', [41, 'Denied']),
        ('closed deterministic producer', '(share:spin 40)',
         '(superpose ((share:pair $p) (share:pair $p)))', [['Pair', 41, 41]] * 2),
    ]
    for workers in (1, 2, 4):
        for name, producer, consumer, expected in cases:
            expression = f'(let $p (delay {producer}) {consumer})'
            _, results = gate.run(f'{name} workers={workers}', source +
                f'!(select 8 {policy(workers)} {expression})')
            result = results[-1]
            # This explicitly selects the complete-bag observer. Ordered
            # streams and prefix selection require their own placement law.
            assert result['closed'] == 'True' and bag(result['values']) == bag(expected), result
            gate.passed_case(f'{name} complete bag and closure workers={workers}')
        expression = ('(let $p (delay (+ $x 1)) '
                      '(superpose ((let $_ (unify $x 1 True False) (force $p)) '
                      '(let $_ (unify $x 2 True False) (force $p)))))')
        _, results = gate.run(f'captured producer refinements workers={workers}', source +
            f'!(select 8 {policy(workers)} {expression})')
        assert results[-1]['closed'] == 'True' and results[-1]['values'] == [2, 3], results[-1]
        gate.passed_case(f'captured lexical worlds stay distinct workers={workers}')


def stored_demand_boundaries(gate):
    for workers in (1, 2, 4):
        source = ('(= (code:choice) @A) (= (code:choice) @B) '
                  '(= (code:twice $c) (Pair *$c *$c)) '
                  '(= (code:run $c) *$c) '
                  '!(code:twice (code:choice)) !(code:run (+ 1 2)) !(collapse (code:run (empty)))')
        trees, _ = gate.run(f'activation operand demand workers={workers}', source,
                            arguments=('--num-threads', str(workers)))
        assert bag(trees[0]) == bag([['Pair', 'A', 'A'], ['Pair', 'B', 'B']]), trees
        assert trees[1:] == [[['unquote', 3]], [[]]], trees
        gate.passed_case(f'code value choice is shared; non-code and empty stay distinct workers={workers}')
        source = ('(= (zero:jobs) 1) (= (zero:jobs) 2) '
                  f'!(let {PATTERN} (select 0 {policy(workers, evidence=False)} (zero:jobs)) '
                  f'(Zero $v $w (select 0 {policy(workers, extra="(demand all)", evidence=False)} $r)))')
        trees, results = gate.run(f'zero initial demand workers={workers}', source)
        assert trees[-1][0][:3] == ['Zero', [], 0], trees
        assert results[-1]['values'] == [1, 2] and results[-1]['closed'] == 'True', results
        gate.passed_case(f'zero initial demand retains unexecuted source workers={workers}')
        source = ('(= (absolute:jobs) 1) (= (absolute:jobs) 2) (= (absolute:jobs) 3) '
                  f'!(let {PATTERN} (select 1 {policy(workers, evidence=False)} (absolute:jobs)) '
                  '(let (Observation $v2 (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2)) '
                  f'(select 1 {policy(workers, evidence=False)} $r) '
                  f'(Absolute $v $v2 $w $w2 (select 0 {policy(workers, extra="(demand all)", evidence=False)} $r2))))')
        trees, results = gate.run(f'satisfied absolute demand workers={workers}', source)
        value = trees[-1][0]
        assert value[:3] == ['Absolute', [1], []] and value[3] == value[4], trees
        assert results[-1]['values'] == [2, 3] and results[-1]['closed'] == 'True', results
        gate.passed_case(f'satisfied absolute demand preserves the suffix workers={workers}')
    source = ('(: stored:raw (-> (Data Atom) (Data Atom))) '
              '(= (stored:raw $x) $x) '
              '(= (stored:save $x) (add-atom &self (Saved $x))) '
              '!(stored:save (+ 1 2)) '
              '!(match &self (Saved $x) (stored:raw $x)) '
              '!(match &self (Saved $x) $x)')
    for workers in (1, 2, 4):
        trees, _ = gate.run(f'stored arithmetic source and value workers={workers}', source,
                            arguments=('--num-threads', str(workers)))
        assert trees[-2:] == [[['+', 1, 2]], [3]], trees
        gate.passed_case(f'storage reifies; explicit value demand evaluates workers={workers}')
    # Prefix mutation may invalidate this exact-snapshot controller. Its
    # committed physical effect must survive and cannot be reported undone.
    source = ('(Old) (Keep) (= (delete:jobs) (let $_ (remove-atom &self (Old)) Gone)) '
              '(= (delete:jobs) Ready) '
              '!(match &self (Old) Before) '
              f'!(select 10 {policy(2, evidence=False)} (delete:jobs)) '
              '!(match &self (Old) leaked) !(match &self (Keep) After)')
    trees, results = gate.run('deletion persists across scope invalidation', source)
    assert trees[0] == ['Before'] and trees[-1] == ['After'] and 'leaked' not in repr(trees)
    assert results[-1]['status'] == 'invalidated', results
    gate.passed_case('a committed deletion is retained when its source scope invalidates')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,default=ROOT/'cetta')
    parser.add_argument('--timeout',type=int,default=45)
    parser.add_argument('--cases',type=int,default=8)
    parser.add_argument('--history-controls',action='store_true')
    parser.add_argument('--ordinary-only', action='store_true',
                        help='Run ordinary-program controls on production builds without recording')
    parser.add_argument('--benchmark', action='store_true',
                        help='Measure ordinary programs after the selected correctness controls')
    parser.add_argument('--benchmark-size', type=int, default=20000)
    parser.add_argument('--benchmark-facts', type=int, default=3000)
    parser.add_argument('--benchmark-repetitions', type=int, default=5)
    parser.add_argument('--benchmark-directory', type=Path,
                        default=ROOT/'runtime/prime-native-parallel-benchmark')
    parser.add_argument('--receipt',type=Path)
    args=parser.parse_args();args.binary=args.binary.resolve();gate=Gate(args)
    if args.benchmark and (args.benchmark_size < 1 or args.benchmark_facts < 0 or
                           args.benchmark_repetitions < 1):
        parser.error('benchmark size and repetitions must be positive, and facts nonnegative')
    try:
        if args.ordinary_only:
            ordinary(gate)
        else:
            fixed(gate);split(gate,args.cases);storage(gate);force_dependencies(gate);equation_dependencies(gate);return_dependencies(gate);fuel_and_advice(gate);captured_query_publications(gate,args.history_controls);effect_barriers(gate);session_context(gate);receipt_capture(gate);nested_allowances(gate);generated(gate,args.cases);shared_producer_boundaries(gate);stored_demand_boundaries(gate)
        if args.benchmark:
            measure_ordinary(gate)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True,exist_ok=True)
            executed_binary = Path(os.environ.get('CETTA_WRAPPED_BIN', str(args.binary))).resolve()
            args.receipt.write_text(json.dumps({'binary_sha256':sha256(executed_binary.read_bytes()).hexdigest(),
                'executed_binary':str(executed_binary),
                'launcher':str(args.binary),'launcher_sha256':sha256(args.binary.read_bytes()).hexdigest(),
                'passed':gate.passed,'receipts':gate.receipts,
                'measurements':getattr(gate, 'measurements', []),
                'measurement_conditions':getattr(gate, 'measurement_conditions', {}),
                'measurement_scope':'Process startup, source loading, evaluation and output; '
                    'thread counts may select different execution paths. Ratios do not '
                    'isolate hardware parallelism.'},indent=2)+'\n')
    print(f'Prime native parallelism: {gate.passed} passed, 0 failed')

if __name__=='__main__':main()
