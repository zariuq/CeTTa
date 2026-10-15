#!/usr/bin/env python3
"""Owned operation receipts and their reflected native observations."""
from __future__ import annotations

import argparse
from collections import Counter
from hashlib import sha256
import json
from pathlib import Path
import random

from test_prime_native_control import Gate, ROOT, bag, parse

PATTERN = ('(Observation $values (satisfied $satisfied) (closed $closed) '
           '(residual $residual) (work $quanta) (status $status) (cost $receipt))')
ANALYSIS_PATTERN = ('(CostAnalysis (metric $metric) (source-profile $profile) (result $result) (input $input) '
                    '(source $instance $revision) (demand $demand) (steps $steps) '
                    '(residual $residual) (analysis-cost $cost))')


def policy(budget=10000, extra='', profile='native-events-v1'):
    return f'(search-policy (control FIFO (budget {budget}) (costs {profile}) {extra}))'


def cost_fields(value, profile):
    assert value[:2] == ['NativeCost', profile], value
    fields = dict(value[2:])
    counts = {item[1]: item[2] for item in fields['operations'] if item[0] == 'Counter'}
    assert len(counts) == len(fields['operations']) == 29, value
    assert all(isinstance(n, int) and n >= 0 for n in counts.values()), value
    assert fields['work'] == ['Exact', sum(counts.values())], value
    assert fields['span'][0] in ('Sequential', 'ForkJoin'), value
    assert fields['span'][1][0] == 'Exact', value
    assert 0 <= fields['span'][1][1] <= fields['work'][1], value
    fields['counts'] = counts
    return fields


def receipt(value, trace=False):
    fields = cost_fields(value, 'native-events-v1')
    counts = fields['counts']
    assert fields['span'] == ['Sequential', fields['work']]
    assert fields['trace-status'] == 'Complete'
    charge = fields['read']
    assert charge[0] == 'CostEvent' and charge[4:] == ['cost-read', 1, 1], charge
    assert charge[2] == fields['scope'] and charge[1] > 0
    if trace:
        expanded = []
        totals = Counter()
        for tag, first, origin, occurrence, kind, units, repeat in fields['events']:
            assert tag == 'CostEvent' and first > 0 and origin > 0
            assert occurrence >= 0 and units > 0 and repeat > 0
            expanded.extend((i, origin, occurrence, kind, units)
                            for i in range(first, first + repeat))
            totals[kind] += units * repeat
        identities = [x[0] for x in expanded]
        assert identities == sorted(set(identities))
        assert all(totals[name] == count for name, count in counts.items())
        assert not identities or identities[-1] < charge[1]
        fields['expanded'] = expanded
    fields['counts'] = counts
    return fields


def compact_receipt(value, trace=False):
    fields = cost_fields(value, 'native-counts-v1')
    assert fields['trace-status'] == 'Unrecorded', value
    assert fields['read'] == ['UnrecordedCostRead', 'cost-read', 1], value
    if trace:
        assert fields['events'] == 'Unrecorded', value
    return fields


def compact_accounts(gate):
    def compact(budget=10000, extra=''):
        return policy(budget, extra, 'native-counts-v1')

    cases = [
        ('duplicate answers', '(= (rows) A) (= (rows) B) (= (rows) B) ', '(rows)'),
        ('shared choice', '(= (coin) 3) (= (coin) 7) (= (twice $x) (Pair $x $x)) ',
         '(twice (coin))'),
        ('completed effect', '(= (once-effect) (chain (println! tick) $x Done)) ',
         '(once-effect)'),
    ]
    for name, source, expression in cases:
        _, full = gate.run('full cost ' + name, source +
            f'!(select 0 {policy(extra="(demand all)")} {expression})')
        _, brief = gate.run('compact cost ' + name, source +
            f'!(select 0 {compact(extra="(demand all)")} {expression})')
        a, b = receipt(full[-1]['cost']), compact_receipt(brief[-1]['cost'])
        assert a['counts'] == b['counts'], (name, a, b)
        assert a['span'] == b['span'] and a['work'] == b['work']
        for field in ('values', 'work', 'closed', 'satisfied', 'status'):
            assert full[-1][field] == brief[-1][field], (name, field)
        if name == 'completed effect':
            assert gate.receipts[-1]['stdout'].splitlines().count('tick') == 1
        gate.passed_case(name + ': compact work equals full accounting without a chronology')

    source = '(= (rows) A) (= (rows) B) (= (rows) B) '
    trees, parts = gate.run('compact reads and inherited resumption', source +
        f'!(let {PATTERN} (select 0 {compact(5, "(demand all)")} (rows)) '
        '(CompactReads $values $receipt (cost:inspect $residual) '
        '(cost:trace $residual) '
        '(select 0 (search-policy (control FIFO (budget 10000) (demand all) (retain True))) $residual)))')
    row = trees[-1][0]
    before, inspected, traced = (compact_receipt(row[2]), compact_receipt(row[3]),
                                compact_receipt(row[4], trace=True))
    after = compact_receipt(parts[-1]['cost'])
    assert before['scope'] == inspected['scope'] == traced['scope'] == after['scope']
    assert [x['counts']['cost-read'] for x in (before, inspected, traced)] == [0, 1, 2]
    assert bag(row[1] + parts[-1]['values']) == bag(['A', 'B', 'B'])
    assert parts[-1]['closed'] == 'True' and after['counts']['cost-read'] == 3
    gate.passed_case('compact reads charge later snapshots and resumption inherits the account')

    trees, parts = gate.run('drop event history and resume through saved alias', source +
        f'!(let {PATTERN} (select 0 {policy(5, "(demand all)")} (rows)) '
        f'(let $alias $residual (Lowered $values $receipt (select 0 {compact(0)} $residual) '
        '(select 0 (search-policy (control FIFO (budget 10000) (demand all))) $alias))))')
    row = trees[-1][0]
    initial = receipt(row[2]); lowered = compact_receipt(parts[-2]['cost'])
    final = compact_receipt(parts[-1]['cost'])
    assert initial['scope'] == lowered['scope'] == final['scope']
    assert lowered['work'][1] >= initial['work'][1]
    assert all(lowered['counts'][k] >= n for k, n in initial['counts'].items())
    assert bag(row[1] + parts[-2]['values'] + parts[-1]['values']) == bag(['A', 'B', 'B'])
    assert parts[-1]['closed'] == 'True'
    gate.passed_case('forgetting chronology preserves paid work and the existing owning alias')

    trees, parts = gate.run('refuse retrospective cost history', source +
        f'!(let {PATTERN} (select 0 {compact(5, "(demand all)")} (rows)) '
        f'(Upgrade $values (select 0 {policy()} $residual) '
        '(select 0 (search-policy (control FIFO (budget 10000) (demand all))) $residual)))')
    row = trees[-1][0]
    assert row[2][0] == 'select', row
    assert parts[-1]['closed'] == 'True'
    assert bag(row[1] + parts[-1]['values']) == bag(['A', 'B', 'B'])
    compact_receipt(parts[-1]['cost'])
    gate.passed_case('a compact account cannot fabricate a complete earlier event history')

    trees, _ = gate.run('full scope containing a compact scope', source +
        f'!(let {PATTERN} (cost:run (select 0 {compact(extra="(demand all)")} (rows))) '
        '(let (Observation $iv (satisfied $is) (closed $ic) (residual $ir) '
        '(work $iw) (status $it) (cost $ip)) (car-atom $values) '
        '(Nested (cost:trace $residual) (cost:trace $ir))))')
    outer = receipt(trees[-1][0][1], trace=True)
    inner = compact_receipt(trees[-1][0][2], trace=True)
    assert outer['scope'] != inner['scope'] and outer['expanded']
    assert outer['counts']['equation-activation'] >= inner['counts']['equation-activation'] == 3
    gate.passed_case('a containing recorder owns its events while the compact inner account stays unrecorded')

    measurements = []
    for profile in ('native-events-v1', 'native-counts-v1'):
        trees, _ = gate.run('retained cost storage ' + profile, source +
            f'!(let {PATTERN} (select 0 {policy(extra="(demand all)", profile=profile)} (rows)) '
            '(Storage $values (cost:storage $residual)))')
        measurements.append(storage(trees[-1][0][2], recorded=profile == 'native-events-v1'))
    assert measurements[1]['requested-retained'][1] < measurements[0]['requested-retained'][1]
    gate.passed_case('requested totals retain less owned storage than the matching event chronology')

    for workers in (2, 4):
        trees, observed = gate.run(f'compact worker slices width{workers}',
            '(= (sum $n) (if (== $n 0) 0 (+ $n (sum (- $n 1))))) '
            '(= (rows) (sum 20)) (= (rows) (sum 21)) (= (rows) (sum 21)) '
            f'!(select 0 {compact(extra=f"(demand all) (workers {workers}) (quantum 8)")} (rows))')
        result = observed[-1]; fields = compact_receipt(result['cost'])
        assert result['closed'] == 'True' and bag(result['values']) == bag([210, 231, 231])
        assert fields['counts']['worker-dispatch'] > 0, fields
        assert fields['counts']['worker-join'] > 0, fields
        gate.passed_case(f'compact worker slices width{workers} preserve duplicates and joined work/span')


def compact_history(gate):
    def compact(budget=10000, extra=''):
        return policy(budget, extra, 'native-counts-v1')

    source = '(= (rows) A) (= (rows) B) (= (rows) B) '
    trees, observed = gate.run('compact account through recording erasure', source +
        f'!(bind! &saved (select 0 {compact(5, "(demand all)")} (rows))) '
        f'!(let {PATTERN} &saved (SavedPrefix $values $receipt)) '
        f'!(bind! &tail (let {PATTERN} &saved $residual)) '
        '!(history:forget &saved 131072) '
        '!(select 0 (search-policy (control FIFO (budget 10000) (demand all))) &tail)')
    change = dict(trees[-2][0][1:])
    assert trees[-2][0][0] == 'HistoryPolicy' and change['status'] == 'Unrecorded'
    assert observed[-1]['closed'] == 'True'
    assert bag(trees[1][0][1] + observed[-1]['values']) == bag(['A', 'B', 'B'])
    compact_receipt(observed[-1]['cost'])
    gate.passed_case('owned checkpoint replacement preserves the compact account and its residual')

    trees, _ = gate.run('compact capture cannot promise exact replay',
        f'!(history:capture (quote (cost:run {compact(0)} '
        '(chain (println! must-not-run) $x Done))) '
        '(ReplayLimits 8 8192 134217728))')
    report = trees[-1][0]
    assert report[0] == 'ReplayCheckpoint', report
    fields = dict(report[1:])
    assert fields['status'] == 'CaptureRefused' and fields['initial-state'] == 'NotStarted'
    assert fields['replay-authenticated'] == 'False' and fields['accepted-owner'] == 'Absent'
    assert 'must-not-run\n' not in gate.receipts[-1]['stdout']
    gate.passed_case('exact replay refuses an unrecorded cost account before executing its body')


def fixed(gate):
    source = '(= (rows) A) (= (rows) B) '
    _, measured = gate.run('ordinary reflected run', source + '!(cost:run (rows))')
    result = measured[-1]
    counts = receipt(result['cost'])['counts']
    assert result['values'] == ['A', 'B'] and result['closed'] == 'True'
    assert counts['equation-activation'] == 2
    assert counts['quantum'] == result['work']
    assert counts['candidate-row'] >= 2 and counts['index-visit'] >= 2
    assert counts['queue-insert'] == counts['queue-take'] == result['work']
    _, plain = gate.run('unobserved run comparison', source +
        '!(select 0 (search-policy (control FIFO (demand all) (retain True))) (rows))')
    assert plain[-1]['values'] == result['values'] and plain[-1]['work'] == result['work']
    gate.passed_case('native cost execution preserves source answers and declares its operation profile')

    source += f'!(let {PATTERN} (cost:run {policy(5)} (rows)) '
    source += '(Reads $receipt (cost:inspect $residual) (cost:trace $residual)))'
    trees, _ = gate.run('read before charge', source)
    first, second, third = trees[-1][0][1:]
    a, b, c = receipt(first), receipt(second), receipt(third, trace=True)
    assert a['scope'] == b['scope'] == c['scope']
    assert b['work'][1] == a['work'][1] + 1 and c['work'][1] == b['work'][1] + 1
    assert [a['counts']['cost-read'], b['counts']['cost-read'], c['counts']['cost-read']] == [0, 1, 2]
    assert a['read'][1] == c['expanded'][-2][0]
    assert b['read'][1] == c['expanded'][-1][0]
    gate.passed_case('receipt reuse is inert data; reads charge the following snapshot and preserve RLE identities')

    source = '(= (weighted) (weight:where V 3)) (= (duplicate $x) (Pair $x $x)) (= (independent $x $y) (Pair $x $y)) '
    _, shared = gate.run('shared weighted cost', source +
        '!(cost:run (weight:read (algebra 0 1 + *) (duplicate (weighted))))')
    _, separate = gate.run('independent weighted cost', source +
        '!(cost:run (weight:read (algebra 0 1 + *) (independent (weighted) (weighted))))')
    a, b = receipt(shared[-1]['cost']), receipt(separate[-1]['cost'])
    assert a['counts']['coefficient-append'] == 1 and b['counts']['coefficient-append'] == 2
    assert a['counts']['equation-activation'] == 2 and b['counts']['equation-activation'] == 3
    assert shared[-1]['values'][0][2] == 3 and separate[-1]['values'][0][2] == 9
    assert a['counts']['heap-lookup'] > 0 and a['counts']['heap-update'] > 0
    gate.passed_case('one shared producer is charged once; independent occurrences retain two contributions')

    source = '(= (rows) A) '
    source += '!(let (Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t)) '
    source += '(select 1 (search-policy (control FIFO (retain True))) (rows)) '
    source += '(UntrackedCase (cost:inspect $r) '
    source += f'(select 1 {policy()} $r)))'
    trees, _ = gate.run('untracked and forged cost scopes', source +
        ' !(cost:inspect (NativeHandle "prime-search" 1))')
    assert trees[-2][0][1] == ['NativeCost', 'Untracked']
    assert trees[-2][0][2][0] == 'select'
    assert trees[-1][0][0] == 'cost:inspect'
    gate.passed_case('untracked or forged scopes cannot acquire retrospective exact receipts')

    source = '(= (rows) A) (= (rows) B) '
    source += f'!(let {PATTERN} (cost:run (select 0 {policy(extra="(demand all)")} (rows))) '
    source += ('(let (Observation $iv (satisfied $is) (closed $ic) (residual $ir) '
               '(work $iw) (status $it) (cost $ip)) (car-atom $values) '
               '(Nested (cost:trace $residual) (cost:trace $ir))))')
    trees, _ = gate.run('nested physical event projections', source)
    outer = receipt(trees[-1][0][1], trace=True)
    inner = receipt(trees[-1][0][2], trace=True)
    outer_events = {event[0]: event for event in outer['expanded']}
    inner_events = {event[0]: event for event in inner['expanded']}
    assert outer['scope'] != inner['scope']
    assert inner_events and all(outer_events[i] == event for i, event in inner_events.items())
    assert outer['work'][1] + inner['work'][1] > outer['work'][1]
    shared_work = sum(event[4] for event in outer['expanded'] if event[0] in inner_events)
    own_work = sum(event[4] for event in outer['expanded'] if event[0] not in inner_events)
    assert shared_work == inner['work'][1]
    assert shared_work + own_work == outer['work'][1]
    gate.passed_case('nested scopes share event identities; only disjoint event partitions add')


def handled_contributions(gate):
    algebra = '(algebra 0 1 + *)'
    def read(body):
        return f'(weight:read {algebra} {body})'
    cases = [
        ('empty read', '', read('V'), 0),
        ('zero remains a production', '', read('(weight:where V 0)'), 1),
        ('inner and outer own distinct productions', '',
         read(f'(Pair {read("(weight:where V 2)")} (weight:where W 3))'), 2),
        ('shared production is handled once',
         '(=w (coin) H 2) ' +
         f'(= (twice $x) (Pair {read("$x")} {read("$x")})) ',
         read('(twice (coin))'), 1),
        ('callback production belongs to enclosing read',
         '(= (product $a $b) (weight:where (* $a $b) 7)) ',
         read('(weight:read (algebra 0 1 + product) (weight:where V 3))'), 2),
        ('captured prefix remains outer',
         '(=w (inner) V 2) ' +
         f'(=w (outer) {read("(inner)")} 7) ', read('(outer)'), 2),
    ]
    for name, source, expression, expected in cases:
        _, whole = gate.run('claim accounting ' + name,
            source + f'!(select 7 {policy(2000)} {expression})')
        final = whole[-1]
        total = receipt(final['cost'])
        assert final['closed'] == 'True' and final['values'], (name, final)
        assert total['counts']['coefficient-handle'] == expected, (name, total)
        assert total['counts']['coefficient-append'] == expected, (name, total)
        for cut in (0, 1, 19, 71):
            trees, parts = gate.run(f'claim accounting {name} cut{cut}', source +
                f'!(let {PATTERN} (select 7 {policy(cut)} {expression}) '
                f'(Split $values $receipt (select 7 {policy(2000-cut)} $residual)))')
            before = receipt(trees[-1][0][2])
            after = receipt(parts[-1]['cost'])
            assert before['counts']['coefficient-handle'] <= expected
            assert after['counts']['coefficient-handle'] == expected, (name, cut, after)
            assert after['counts']['quantum'] == total['counts']['quantum']
            assert parts[-1]['work'] == final['work']
            assert parts[-1]['closed'] == 'True'
        gate.passed_case(name + ': accepted claims charge once across suspension')


def split_runs(gate, cases):
    source = ('(= (later $n $v) (if (== $n 0) $v (later (- $n 1) $v))) '
              '(= (rows) (weight:where A (later 7 2))) '
              '(= (rows) (weight:where B 3)) ')
    expression = '(weight:read (algebra 0 1 + *) (rows))'
    rng = random.Random(0xC057)
    for case in range(cases):
        first, second = rng.randrange(1, 80), rng.randrange(100, 1200)
        _, combined = gate.run(f'cost combined {case}', source +
            f'!(select 3 {policy(first + second)} {expression})')
        program = source + f'!(let {PATTERN} (select 3 {policy(first)} {expression}) '
        program += f'(Split $values $receipt (select 3 {policy(second)} $residual)))'
        trees, split = gate.run(f'cost split {case}', program)
        prefix = trees[-1][0][1]
        before = receipt(trees[-1][0][2])
        after = receipt(split[-1]['cost'])
        whole = receipt(combined[-1]['cost'])
        def erase_ids(values):
            return [[x[1], x[2], [f[3] for f in x[3][1]]] for x in values]
        assert erase_ids(prefix + split[-1]['values']) == erase_ids(combined[-1]['values'])
        assert after['scope'] == before['scope']
        assert all(after['counts'][k] >= v for k, v in before['counts'].items())
        assert after['counts']['quantum'] == whole['counts']['quantum']
        assert after['counts']['cost-read'] == whole['counts']['cost-read'] + 1
        for field in ('work', 'closed', 'satisfied', 'status'):
            assert split[-1][field] == combined[-1][field]
        gate.passed_case(f'whole residual cost ledger survives weighted budget split {case}')


def analysis_fields(value):
    assert value[0] == 'CostAnalysis', value
    fields = {item[0]: item[1] if len(item) == 2 else item[1:] for item in value[1:]}
    assert fields['metric'] == 'equation-activation'
    assert fields['demand'] == 'CompleteUnfolding'
    assert fields['source-profile'] == 'FiniteGroundCallsUngradedRows'
    assert fields['residual'][:2] == ['NativeHandle', 'prime-cost-analysis']
    fields['cost'] = receipt(fields['analysis-cost'])
    return fields


def storage(value, recorded=True):
    assert value[:2] == ['NativeStorage', 'native-owned-storage-v1'], value
    fields = dict(value[2:])
    assert fields['ownership'] == 'Complete', value
    assert fields['accounted-live'][0] == fields['requested-retained'][0] == 'Exact'
    assert fields['requested-retained'][1] >= fields['accounted-live'][1] > 0
    assert fields['identities'] > 0 and fields['inspection-probes'] > 0
    assert fields['snapshot'] == 'BeforeInspectionCharge'
    assert fields['environment'] == 'BorrowedSourceAndGlobalPools'
    if recorded:
        assert fields['read'][4:] == ['cost-read', 1, 1]
    else:
        assert fields['read'] == ['UnrecordedCostRead', 'cost-read', 1]
    return fields


def storage_controls(gate):
    source = '(= (rows) A) (= (rows) B) '
    trees, _ = gate.run('owned search census and charged reads', source +
        f'!(let {PATTERN} (select 1 {policy(5, "(retain True)")} (rows)) '
        '(Storage (cost:storage $residual) (cost:storage $residual) (cost:trace $residual)))')
    a, b = map(storage, trees[-1][0][1:3])
    trace = receipt(trees[-1][0][3], trace=True)
    assert trace['counts']['storage-probe'] == a['inspection-probes'] + b['inspection-probes']
    assert trace['counts']['cost-read'] == 3
    assert a['read'][2] == b['read'][2] == trace['scope']
    assert a['identities'] == b['identities'] and a['repeated-visits'] > 0
    gate.passed_case('owned census includes pending frames; repeated inspection charges its actual probe work')

    source = ('(= (later $n $v) (if (== $n 0) $v (later (- $n 1) $v))) '
              '(= (rows) (weight:where A (later 20 3))) ')
    trees, obs = gate.run('pending semantic coefficient ownership', source +
        f'!(let {PATTERN} (select 1 {policy(25, "(retain True)")} '
        '(weight:read (algebra 0 1 + *) (rows))) '
        f'(Pending $values (cost:storage $residual) (select 1 {policy()} $residual)))')
    assert trees[-1][0][1] == []
    measured = storage(trees[-1][0][2])
    assert measured['repeated-visits'] > 0
    assert obs[-1]['values'][0][:3] == ['Weighted', 'A', 3]
    assert receipt(obs[-1]['cost'])['counts']['storage-probe'] == measured['inspection-probes']
    gate.passed_case('coefficient continuations and shared Need owners survive census and resumption')

    source = '(= (a) (b)) (= (b) (c)) (= (c) Done) '
    trees, _ = gate.run('owned analyzer census', source +
        f'!(let {ANALYSIS_PATTERN} (cost:analyze (quote (a)) 2) '
        '(Analysis (cost:storage $residual) (cost:analyze $residual 100)))')
    measured = storage(trees[-1][0][1])
    result = analysis_fields(trees[-1][0][2])
    assert result['result'] == ['Bound', 3]
    assert result['cost']['counts']['storage-probe'] == measured['inspection-probes']
    gate.passed_case('analysis census accounts for retained graph, source cursor, bindings and cost ledger')

    trees, _ = gate.run('closed source still retains storage', '(= (rows) A) (= (rows) B) ' +
        f'!(let {PATTERN} (select 0 {policy(10000, "(demand all) (retain True)")} (rows)) '
        '(Done $values $closed (cost:storage $residual)))')
    assert trees[-1][0][1:3] == [['A', 'B'], 'True']
    storage(trees[-1][0][3])
    gate.passed_case('closure does not erase retained witnesses or report an empty frontier as zero storage')

    source = ('(= (storage:sumdown $n) '
              '(if (== $n 0) 0 (+ 1 (storage:sumdown (- $n 1))))) '
              '(= (storage:jobs) (storage:sumdown 40)) '
              '(= (storage:jobs) (storage:sumdown 45)) ')
    trees, observed = gate.run('inspect suspended workers before resumption', source +
        f'!(let {PATTERN} (select 1 '
        f'{policy(31, "(workers 2) (quantum 3) (retain True)")} (storage:jobs)) '
        '(Retained $values $closed (cost:storage $residual) '
        f'(select 0 {policy(5000, "(workers 2) (quantum 3) (retain True) (demand all)")} '
        '$residual)))')
    assert trees[-1][0][1:3] == [[], 'False']
    measured = storage(trees[-1][0][3])
    assert measured['repeated-visits'] > 0
    assert observed[-1]['values'] == [40, 45] and observed[-1]['closed'] == 'True'
    gate.passed_case('suspended worker census preserves shared owners and the complete resumed result')

    trees, _ = gate.run('forged storage capability remains data',
        '!(cost:storage (NativeHandle "prime-search" 1)) !(cost:storage Unknown)')
    assert all(tree[0][0] == 'cost:storage' for tree in trees)
    gate.passed_case('storage inspection requires an authentic retained capability')


def cost_clients(gate):
    source = (ROOT / 'tests/prime/native_cost/self_budgeted_search.metta').read_text()
    trees, _ = gate.run('self-budgeted native seven routes', source)
    result = trees[-1][0]
    assert result[:4] == ['BudgetedRoutes', [['Route', i] for i in range(7)], 'True', 'False']
    before, retained, after = (compact_receipt(result[4]), storage(result[5], recorded=False),
                               compact_receipt(result[6]))
    assert before['work'][1] >= 100 and after['scope'] == before['scope']
    assert before['counts']['quantum'] < after['counts']['quantum']
    assert after['counts']['storage-probe'] == retained['inspection-probes']
    assert retained['inspection-probes'] >= retained['identities'] + retained['repeated-visits']
    gate.passed_case('program budgets native resumptions from cumulative work, then retains and completes seven routes')

    # Certified work is a function of what the program does.  Fresh processes
    # place the same owned objects at different addresses; the census and
    # every receipt must not see that.
    for attempt in range(3):
        again, _ = gate.run(f'self-budgeted seven routes, fresh process {attempt + 1}', source)
        assert again[-1][0] == result, (again[-1][0], result)
    gate.passed_case('certified work and the storage census do not depend on allocation addresses')

    source = (ROOT / 'tests/prime/native_cost/analyzed_seven_routes.metta').read_text()
    trees, obs = gate.run('analyzed seven-route admission', source)
    assert trees[0][0][:2] == ['Admitted', 8]
    assert trees[1][0][:2] == ['Deferred', 8]
    assert obs[0]['values'] == [['Route', i] for i in range(7)] and obs[0]['satisfied'] == 'True'
    assert receipt(obs[0]['cost'])['counts']['equation-activation'] <= 8
    gate.passed_case('program admits or defers seven-route search using the finite source bound and its explicit metric')


def analysis_controls(gate, cases):
    source = '(= (f $x $x) (g $x)) (= (g A) X) (= (f A B) Y) '
    trees, observations = gate.run('bound through actual candidate bindings', source +
        '!(cost:analyze (quote (f A A)) 1000) !(cost:run (f A A))')
    analysis = analysis_fields(trees[-2][0])
    assert analysis['result'] == ['Bound', 2]
    assert observations[-1]['values'] == ['X']
    assert receipt(observations[-1]['cost'])['counts']['equation-activation'] == 2
    assert analysis['cost']['counts']['match-attempt'] > 0
    gate.passed_case('quoted analysis instantiates the actual shared matcher and candidate bindings')

    for name, source, body, expected in (
        ('duplicate rows and shared successor', '(= (f) (g)) (= (f) (g)) (= (g) A) (= (g) B)', '(f)', 6),
        ('wildcard head coverage', '(= (target A) X) (= ($head A) Y)', '(target A)', 2),
        ('rigidly refuted row', '(= (target A A) X) (= (target $x $x) Y)', '(target A B)', 0),
        ('inert constructor', '', '(Tree A (Leaf B))', 0),
    ):
        trees, observations = gate.run(name, source +
            f' !(cost:analyze (quote {body}) 1000) !(cost:run {body})')
        analysis = analysis_fields(trees[-2][0])
        assert analysis['result'] == ['Bound', expected], (name, analysis)
        assert observations[-1]['closed'] == 'True'
        assert receipt(observations[-1]['cost'])['counts']['equation-activation'] == expected
        gate.passed_case(f'analysis coverage retains {name}')

    for name, source, body, reason in (
        ('cycle', '(= (f) (f))', '(f)', 'Cycle'),
        ('growing graph', '(= (f $x) (f (S $x)))', '(f Z)', 'Pending'),
        ('effect', '', '(println! "UNAUTHORIZED-ANALYSIS-EFFECT")', 'UnsupportedSource'),
        ('hidden constructor call', '(= (f) A)', '(Tree (f))', 'UnsupportedSource'),
        ('semantic weight', '(= (f) (weight:where A 3))', '(f)', 'UnsupportedSuccessor'),
        ('declared function type', '(: f (-> Number Number)) (= (f $x) $x)', '(f 3)', 'UnsupportedSource'),
    ):
        trees, _ = gate.run(name, source + f' !(cost:analyze (quote {body}) 40)')
        analysis = analysis_fields(trees[-1][0])
        assert analysis['result'] == ['Unknown', reason], (name, analysis)
        # Held syntax prints as its authored term; reusing the owned receipt
        # must not run that term (the cycle/effect cases discriminate this).
        assert analysis['input'] == parse(body)[0]
        if name == 'effect':
            assert not any(line == 'UNAUTHORIZED-ANALYSIS-EFFECT'
                           for line in gate.receipts[-1]['stdout'].splitlines())
        gate.passed_case(f'{name} retains its original analysis obligation without a false bound')

    source = '(= (f) A) '
    trees, _ = gate.run('analysis source revision', source +
        f'!(bind! &saved-analysis (let {ANALYSIS_PATTERN} (cost:analyze (quote (f)) 2) $residual)) '
        '!(add-atom &self (= (f) B)) !(cost:analyze &saved-analysis 100)')
    assert analysis_fields(trees[-1][0])['result'] == ['Unknown', 'StaleSource']
    gate.passed_case('changed physical source cannot reuse an earlier coverage bound')

    trees, _ = gate.run('analysis captured absence',
        f'!(bind! &saved-analysis (let {ANALYSIS_PATTERN} '
        '(cost:analyze (quote &unbound-analysis-input) 1) $residual)) '
        '!(bind! &unbound-analysis-input (quote NewValue)) !(cost:analyze &saved-analysis 100)')
    assert analysis_fields(trees[-1][0])['result'] == ['Unknown', 'StaleBindings']
    gate.passed_case('analysis retains and validates consulted absence across suspension')

    trees, _ = gate.run('analysis capability boundary',
        '!(cost:analyze (Tree A B) 100) '
        '!(cost:analyze (NativeHandle "prime-cost-analysis" 1) 100)')
    assert all(tree[0][0] == 'cost:analyze' for tree in trees)
    gate.passed_case('unquoted computations and forged residual identifiers remain uninterpreted')

    rng = random.Random(84691)
    for case in range(cases):
        count = rng.randrange(3, 10)
        graph = []
        source = []
        for node in range(count):
            rows = []
            for row in range(rng.randrange(1, 4)):
                successor = rng.randrange(node + 1, count + 1)
                rows.append(successor)
                rhs = f'(node {successor})' if successor < count else f'(Leaf {node} {row})'
                source.append(f'(= (node {node}) {rhs})')
            graph.append(rows)
        expected = [0] * (count + 1)
        for node in reversed(range(count)):
            expected[node] = sum(1 + expected[child] for child in graph[node])
        program = ' '.join(source)
        trees, observed = gate.run(f'generated physical graph {case}', program +
            ' !(cost:analyze (quote (node 0)) 10000) !(cost:run (node 0))')
        whole = analysis_fields(trees[-2][0])
        assert whole['result'] == ['Bound', expected[0]], (graph, whole)
        assert observed[-1]['closed'] == 'True'
        assert receipt(observed[-1]['cost'])['counts']['equation-activation'] == expected[0]
        first = rng.randrange(0, whole['steps'] + 1)
        trees, _ = gate.run(f'generated analysis split {case}', program +
            f' !(let {ANALYSIS_PATTERN} (cost:analyze (quote (node 0)) {first}) '
            f'(cost:analyze $residual {10000 - first}))')
        split = analysis_fields(trees[-1][0])
        assert split['result'] == whole['result'] and split['steps'] == whole['steps']
        assert split['cost']['counts']['cost-read'] == whole['cost']['counts']['cost-read'] + 1
        assert all(split['cost']['counts'][name] == number
                   for name, number in whole['cost']['counts'].items() if name != 'cost-read')
        gate.passed_case(f'generated DAG {case}: complete physical coverage, measured activations and retained split cursor agree')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=ROOT / 'cetta')
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--cases', type=int, default=8)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--population', choices=('all', 'compact'), default='all')
    parser.add_argument('--history-controls', action='store_true',
                        help='Check receipt-enabled forgetting and replay boundaries.')
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    gate = Gate(args)
    try:
        compact_accounts(gate)
        if args.history_controls:
            compact_history(gate)
        if args.population == 'all':
            fixed(gate)
            handled_contributions(gate)
            split_runs(gate, args.cases)
            analysis_controls(gate, args.cases)
            storage_controls(gate)
            cost_clients(gate)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True, exist_ok=True)
            args.receipt.write_text(json.dumps({
                'binary_sha256': sha256(args.binary.read_bytes()).hexdigest(),
                'eval_sha256': sha256((ROOT / 'src/eval.c').read_bytes()).hexdigest(),
                'passed': gate.passed, 'receipts': gate.receipts}, indent=2) + '\n')
    print(f'Prime native costs: {gate.passed} passed, 0 failed')


if __name__ == '__main__':
    main()
