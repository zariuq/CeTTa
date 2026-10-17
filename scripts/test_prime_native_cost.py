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


def equation_receipt(value):
    assert value[0] == 'ExecutionCost', value
    fields = dict(value[1:])
    assert fields['source-profile'] == 'equation-admissions-v1', value
    assert fields['source-domain'] == 'PrimeApplicationEquations', value
    assert fields['charged-operation'] == 'RetainedMatchedRHS', value
    assert fields['strategy'] == 'OwnedEquationFrontier', value
    assert fields['work'][0] == 'Exact', value
    assert fields['span'][:1] == ['OwnedWorkerSchedule'] and fields['span'][1][0] == 'Exact', value
    assert 0 <= fields['span'][1][1] <= fields['work'][1], value
    fields['telemetry'] = (None if fields['native'] == ['NativeCost', 'Untracked'] else
        compact_receipt(fields['native']) if fields['native'][1] == 'native-counts-v1' else
        cost_fields(fields['native'], 'native-events-v1'))
    return fields


def equation_accounts(gate, history=False):
    def source_policy(budget=10000, width=1, extra='', native=None):
        profile = f'(equation-admissions-v1 {native})' if native else 'equation-admissions-v1'
        return policy(budget, f'(demand all) (workers {width}) {extra}', profile)

    graph = ('(= (account-root) (account-leaf)) (= (account-root) (account-leaf)) '
             '(= (account-root) Ready) (= (account-leaf) A) (= (account-leaf) B) ')
    cases = [
        ('one admission', '(= (account-one) A) ', '(account-one)', ['A'], 1),
        ('empty admitted body', '(= (account-none) (empty)) ', '(account-none)', [], 1),
        ('duplicate equations', '(= (account-dup) A) (= (account-dup) A) ', '(account-dup)', ['A', 'A'], 2),
        ('empty body before an answer', '(= (account-choice) (empty)) (= (account-choice) A) ',
         '(account-choice)', ['A'], 2),
        ('repeated successor', graph, '(account-root)', ['Ready', 'A', 'B', 'A', 'B'], 7),
        ('shared demand', '(= (account-coin) 3) (= (account-coin) 7) '
         '(= (account-pair $x) (Pair $x $x)) ', '(account-pair (account-coin))',
         [['Pair', 3, 3], ['Pair', 7, 7]], 3),
    ]
    for width in (1, 2):
        for name, definitions, expression, expected, work in cases:
            _, parts = gate.run(f'equation account {name} w{width}', definitions +
                f'!(select 0 {source_policy(width=width)} {expression})')
            result = parts[-1]
            account = equation_receipt(result['cost'])
            assert result['closed'] == 'True' and bag(result['values']) == bag(expected), (name, result)
            assert account['work'] == ['Exact', work], (name, account)
            assert 'history' not in result and account['telemetry'] is None
            gate.passed_case(f'source admissions preserve {name} with {width} workers')

    trees, parts = gate.run('retained source admissions precede body dispatch', graph +
        f'!(select 0 {source_policy(4, native='native-counts-v1')} (account-root))')
    account = equation_receipt(parts[-1]['cost'])
    assert parts[-1]['closed'] == 'False' and account['work'] == ['Exact', 3]
    assert account['telemetry']['counts']['equation-activation'] == 0
    gate.passed_case('source admissions charge retained pending branches, independently of body dispatch')

    for width in (1, 2):
        for cut in (0, 4, 9, 17):
            trees, parts = gate.run(f'resumed source account w{width} c{cut}', graph +
                f'!(let {PATTERN} (select 0 {source_policy(cut, width)} (account-root)) '
                f'(SourceSplit $values $receipt (select 0 {source_policy(width=width)} $residual)))')
            row = trees[-1][0]
            before = equation_receipt(row[2])
            after = equation_receipt(parts[-1]['cost'])
            assert row[0] == 'SourceSplit' and parts[-1]['closed'] == 'True'
            assert bag(row[1] + parts[-1]['values']) == bag(['Ready', 'A', 'B', 'A', 'B'])
            assert after['work'] == ['Exact', 7] and before['work'][1] <= 7
            assert before['telemetry'] is after['telemetry'] is None
            gate.passed_case(f'resumption retains source admissions w{width} c{cut}')

    trees, parts = gate.run('source reads and refused unit replacement', graph +
        f'!(let {PATTERN} (select 0 {source_policy(4, native="native-counts-v1")} (account-root)) '
        '(SourceReads $receipt (cost:inspect $residual) (cost:trace $residual) '
        f'(select 0 {policy(0, "(demand all)", "native-counts-v1")} $residual) '
        '(select 0 (search-policy (control FIFO (budget 10000) (demand all))) $residual)))')
    row = trees[-1][0]
    before, inspected, traced = map(equation_receipt, row[1:4])
    assert row[4][0] == 'select', row
    assert before['work'] == inspected['work'] == traced['work'] == ['Exact', 3]
    assert [x['telemetry']['counts']['cost-read'] for x in (before, inspected, traced)] == [0, 1, 2]
    assert equation_receipt(parts[-1]['cost'])['work'] == ['Exact', 7]
    gate.passed_case('reads preserve source units; refused replacement leaves the source account resumable')

    trees, parts = gate.run('storage inspection does not enable native telemetry', graph +
        f'!(let {PATTERN} (select 0 {source_policy(4)} (account-root)) '
        '(SourceStorage $receipt (cost:storage $residual) (cost:inspect $residual) '
        f'(select 0 {source_policy()} $residual)))')
    row = trees[-1][0]
    assert equation_receipt(row[1])['work'] == equation_receipt(row[3])['work'] == ['Exact', 3]
    stored = dict(row[2][2:])
    assert row[2][:2] == ['NativeStorage', 'native-owned-storage-v1']
    assert stored['inspection-probes'] > 0 and stored['read'] == ['UntrackedCostRead', 'cost-read']
    assert equation_receipt(parts[-1]['cost'])['work'] == ['Exact', 7]
    gate.passed_case('requested storage inspection leaves the source account intact and native telemetry untracked')

    for count in (0, 20):
        definitions = '(= (account-match $x) Good) ' + ''.join(
            '(= (account-match (Pair $v $v)) Refuted) ' for _ in range(count))
        _, parts = gate.run(f'index work separated from source units {count}', definitions +
            f'!(select 0 {source_policy(native="native-counts-v1")} (account-match (Pair A B)))')
        account = equation_receipt(parts[-1]['cost'])
        assert parts[-1]['values'] == ['Good'] and account['work'] == ['Exact', 1]
        if count == 0:
            first_native = account['telemetry']['work'][1]
        else:
            assert account['telemetry']['work'][1] > first_native
    gate.passed_case('refuted equation candidates increase native work without charging source admissions')

    for width in (1, 2):
        _, parts = gate.run(f'explicit joint source and native event account w{width}', graph +
            f'!(select 0 {source_policy(width=width, native="native-events-v1")} (account-root))')
        account = equation_receipt(parts[-1]['cost'])
        assert account['work'] == ['Exact', 7] and account['telemetry']['trace-status'] == 'Complete'
        assert account['telemetry']['work'][1] > 7 and parts[-1]['closed'] == 'True'
        gate.passed_case('explicit joint observation preserves both source units and native events w' + str(width))

    trees, parts = gate.run('source-only account refuses retrospective native telemetry', graph +
        f'!(let {PATTERN} (select 0 {source_policy(4)} (account-root)) '
        f'(SourceOnly $receipt (select 0 {source_policy(native="native-counts-v1")} $residual) '
        f'(select 0 {source_policy()} $residual)))')
    row = trees[-1][0]
    assert row[0] == 'SourceOnly' and row[2][0] == 'select'
    assert equation_receipt(row[1])['telemetry'] is None
    assert equation_receipt(parts[-1]['cost'])['work'] == ['Exact', 7]
    gate.passed_case('source-only execution refuses retroactive native observation without losing its remainder')

    rng = random.Random(127)
    for sample in range(6):
        rows = [[rng.randrange(i + 1, 6) if i < 5 and rng.randrange(2) else f'V{i}-{j}'
                 for j in range(1 + rng.randrange(2))] for i in range(6)]
        def evaluate(node):
            values, work = [], 0
            for successor in rows[node]:
                work += 1
                if isinstance(successor, int):
                    next_values, next_work = evaluate(successor)
                    values.extend(next_values)
                    work += next_work
                else:
                    values.append(successor)
            return values, work
        expected, work = evaluate(0)
        definitions = ' '.join(f'(= (account-node {i}) ' +
            (f'(account-node {child}))' if isinstance(child, int) else f'{child})')
            for i, children in enumerate(rows) for child in children)
        trees, parts = gate.run(f'acyclic source account {sample}', definitions +
            ' !(cost:analyze (quote (account-node 0)) 10000) ' +
            f'!(select 0 {source_policy(width=1 + sample % 2)} (account-node 0))')
        assert analysis_fields(trees[0][0])['result'] == ['Bound', work]
        assert bag(parts[-1]['values']) == bag(expected) and parts[-1]['closed'] == 'True'
        assert equation_receipt(parts[-1]['cost'])['work'] == ['Exact', work]
        gate.passed_case('independent source execution, quoted bound and native account agree on acyclic graph ' + str(sample))

    if history:
        record = '(history (selections 512) (nodes 32) (bytes 65536) (owners 10000))'
        for width in (1, 2):
            _, parts = gate.run(f'equation account with history w{width}', graph +
                f'!(select 0 {source_policy(width=width, extra=record)} (account-root))')
            assert equation_receipt(parts[-1]['cost'])['work'] == ['Exact', 7]
            assert parts[-1]['closed'] == 'True' and 'history' in parts[-1]
            gate.passed_case('source units remain independent of requested history w' + str(width))

        recorded_pattern = PATTERN.replace('(cost $receipt)', '(history $history) (cost $receipt)')
        for width in (1, 2):
            definitions = graph + '(= (account-effect) (chain (println! source-once) $ignored (account-root))) '
            trees, parts = gate.run(f'source account survives physical forgetting w{width}', definitions +
                f'!(bind! &source-saved (select 0 {source_policy(18, width, record)} (account-effect))) '
                f'!(let {recorded_pattern} &source-saved (SourceSaved $values $receipt)) '
                f'!(bind! &source-tail (let {recorded_pattern} &source-saved $residual)) '
                '!(history:forget &source-saved 131072) '
                f'!(select 0 {source_policy(width=width)} &source-tail)')
            saved = trees[1][0]
            before = equation_receipt(saved[2])
            after = equation_receipt(parts[-1]['cost'])
            assert before['work'][1] <= 8 and after['work'] == ['Exact', 8]
            assert bag(saved[1] + parts[-1]['values']) == bag(['Ready', 'A', 'B', 'A', 'B'])
            assert parts[-1]['closed'] == 'True' and 'history' not in parts[-1]
            assert trees[-2][0][0] == 'HistoryPolicy' and ['status', 'Unrecorded'] in trees[-2][0]
            assert gate.receipts[-1]['stdout'].splitlines().count('source-once') == 1
            gate.passed_case('physical history release preserves the source account and completed effect w' + str(width))

    _, parts = gate.run('source work includes a faulting body', '(= (account-fault) (/ 1 0)) ' +
        f'!(select 0 {source_policy()} (account-fault))')
    assert equation_receipt(parts[-1]['cost'])['work'] == ['Exact', 1]
    assert len(parts[-1]['values']) == 1 and parts[-1]['values'][0][0] == 'Error'
    gate.passed_case('an admitted equation remains paid when its body returns a fault')

    marker = 'SOURCE-REPORT-MUST-NOT-RUN'
    trees, _ = gate.run('source account remains completed returned data',
        f'(= (ExecutionCost $a $b $c $d $e $f $g) (println! "{marker}")) ' + graph +
        f'!(let {PATTERN} (select 0 {source_policy()} (account-root)) $receipt)')
    assert equation_receipt(trees[-1][0])['work'] == ['Exact', 7]
    assert marker not in gate.receipts[-1]['stdout'].splitlines()
    gate.passed_case('source account inspection does not execute the returned report as code')


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

    history = '(history (selections 512) (nodes 32) (bytes 65536) (owners 10000))'
    source = ('(= (coin) 3) (= (coin) 3) (= (coin) 6) '
              '(= (twice $x) (Pair $x $x)) ')
    expression = '(chain (println! joint-once) $ignored (twice (coin)))'
    baseline = None
    for recording in (False, True):
        for counting in (False, True):
            choices = (history if recording else '') + (
                ' (costs native-counts-v1)' if counting else '')
            _, observed = gate.run(f'joint observers history={recording} counts={counting}',
                source + '!(select 0 (search-policy (control FIFO (budget 10000) '
                f'(demand all) {choices})) {expression})')
            result = observed[-1]
            actual = {key: result[key] for key in ('values', 'satisfied', 'closed', 'status')}
            assert bag(result['values']) == bag([['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 6, 6]])
            assert result['closed'] == 'True'
            if baseline is None:
                baseline = actual
            assert actual == baseline, (recording, counting, baseline, actual)
            assert gate.receipts[-1]['stdout'].splitlines().count('joint-once') == 1
            assert ('cost' in result) == counting and ('history' in result) == recording
            if counting:
                compact_receipt(result['cost'])
            if recording:
                assert next(row[1] for row in result['history'][1:] if row[0] == 'nodes') > 0
            gate.passed_case(f'history={recording} counts={counting}: shared occurrences and one effect')

    recorded_pattern = PATTERN.replace('(cost $receipt)', '(history $history) (cost $receipt)')
    source = '(= (rows) A) (= (rows) B) (= (rows) B) '
    trees, observed = gate.run('compact account through recording erasure', source +
        f'!(bind! &saved (select 0 {compact(12, "(demand all) " + history)} '
        '(chain (println! joint-once) $ignored (rows)))) '
        f'!(let {recorded_pattern} &saved (SavedPrefix $values $receipt $history)) '
        f'!(bind! &tail (let {recorded_pattern} &saved $residual)) '
        '!(println! joint-prefix) '
        '!(history:forget &saved 131072) '
        '!(select 0 (search-policy (control FIFO (budget 10000) (demand all))) &tail)')
    saved = trees[1][0]
    assert saved[0] == 'SavedPrefix' and saved[3][0] == 'History', saved
    assert next(row[1] for row in saved[3][1:] if row[0] == 'nodes') > 0
    before = compact_receipt(saved[2])
    change = dict(trees[-2][0][1:])
    assert trees[-2][0][0] == 'HistoryPolicy' and change['status'] == 'Unrecorded'
    assert observed[-1]['closed'] == 'True'
    assert bag(trees[1][0][1] + observed[-1]['values']) == bag(['A', 'B', 'B'])
    after = compact_receipt(observed[-1]['cost'])
    assert all(after['counts'][key] >= value for key, value in before['counts'].items())
    assert after['counts']['receipt-append'] == before['counts']['receipt-append']
    lines = gate.receipts[-1]['stdout'].splitlines()
    assert lines.count('joint-once') == 1 and lines.index('joint-once') < lines.index('joint-prefix')
    assert 'history' not in observed[-1]
    gate.passed_case('forgetting a real diary preserves paid counts, residual and the completed effect')

    _, observed = gate.run('full recording prefix keeps numerical observation',
        '(= (coin) 3) (= (coin) 3) (= (coin) 6) (= (twice $x) (Pair $x $x)) ' +
        '!(select 0 ' + compact(extra='(demand all) ' +
            history.replace('(selections 512)', '(selections 0)')) +
        ' (chain (println! joint-once) $ignored (twice (coin))))')
    result = observed[-1]
    assert result['closed'] == 'True'
    assert bag(result['values']) == bag([['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 6, 6]])
    assert compact_receipt(result['cost'])['work'][1] > 0
    assert next(row[1] for row in result['history'][1:] if row[0] == 'status') == 'Incomplete'
    assert next(row[1] for row in result['history'][1:] if row[0] == 'nodes') == 0
    assert next(row[1] for row in result['history'][1:] if row[0] == 'omitted-selections') > 0
    assert gate.receipts[-1]['stdout'].splitlines().count('joint-once') == 1
    gate.passed_case('zero history capacity retains answers, the single effect and paid counts')

    trees, observed = gate.run('start recording after counted work', source +
        f'!(bind! &saved (select 0 {compact(12, "(demand all)")} '
        '(chain (println! joint-once) $ignored (rows)))) '
        f'!(let {PATTERN} &saved (SavedPrefix $values $receipt $quanta)) '
        f'!(bind! &tail (let {PATTERN} &saved $residual)) '
        '!(println! joint-prefix) '
        f'!(select 0 {compact(extra="(demand all) " + history)} &tail)')
    saved, resumed = trees[1][0], observed[-1]
    assert saved[0] == 'SavedPrefix' and saved[3] > 0
    before, after = compact_receipt(saved[2]), compact_receipt(resumed['cost'])
    assert all(after['counts'][key] >= value for key, value in before['counts'].items())
    assert resumed['closed'] == 'True' and bag(saved[1] + resumed['values']) == bag(['A', 'B', 'B'])
    assert next(row[1] for row in resumed['history'][1:] if row[0] == 'start-work') == saved[3]
    assert next(row[1] for row in resumed['history'][1:] if row[0] == 'earlier') == 'Unrecorded'
    lines = gate.receipts[-1]['stdout'].splitlines()
    assert lines.count('joint-once') == 1 and lines.index('joint-once') < lines.index('joint-prefix')
    gate.passed_case('late recording begins at the retained cut and preserves the earlier account')

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


def analysis_fields(value, metric='equation-activation', recorded=True):
    assert value[0] == 'CostAnalysis', value
    fields = {item[0]: item[1] if len(item) == 2 else item[1:] for item in value[1:]}
    assert fields['metric'] == metric
    assert fields['demand'] == 'CompleteUnfolding'
    assert fields['source-profile'] == 'FiniteGroundCallsUngradedRows'
    assert fields['residual'][:2] == ['NativeHandle', 'prime-cost-analysis']
    fields['cost'] = (receipt if recorded else compact_receipt)(fields['analysis-cost'])
    return fields


def storage(value, recorded=True, tracked=True):
    assert value[:2] == ['NativeStorage', 'native-owned-storage-v1'], value
    fields = dict(value[2:])
    assert fields['ownership'] == 'Complete', value
    assert fields['accounted-live'][0] == fields['requested-retained'][0] == 'Exact'
    assert fields['requested-retained'][1] >= fields['accounted-live'][1] > 0
    assert fields['identities'] > 0 and fields['inspection-probes'] > 0
    assert fields['snapshot'] == 'BeforeInspectionCharge'
    assert fields['environment'] == 'BorrowedSourceAndGlobalPools'
    if not tracked:
        assert fields['read'] == ['UntrackedCostRead', 'cost-read']
    elif recorded:
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


def cost_port_boundaries(gate):
    for language in ('he', 'petta'):
        gate.run('default inspection namespace ' + language,
                 '(= (cost:storage $x) Local) !(cost:storage A)', language=language)
        assert parse(gate.receipts[-1]['stdout'])[-1] in ('Local', ['Local'])
        gate.passed_case(language + ' default preserves authored inspection names')

    marker = 'REENTERED-STORAGE-REPORT'
    definitions = ('(= (rows) A) '
        '(= (NativeStorage $profile $a $b $c $d $e $f $g $h $i) '
        f'(println! "{marker}")) ')
    prefix = (f'!(let {PATTERN} (select 1 '
              f'{policy(0, "(retain True)", "native-counts-v1")} (rows)) ')
    for language in ('prime', 'he', 'petta'):
        profile = None if language == 'prime' else 'extended'
        gate.run('forged inspection ' + language, ' '.join(
            f'!({head} (NativeHandle "prime-search" 1))'
            for head in ('cost:storage', 'cost:inspect', 'cost:trace')),
            language=language, profile=profile)
        stdout = gate.receipts[-1]['stdout']
        assert all(head in stdout for head in ('cost:storage', 'cost:inspect', 'cost:trace'))
        assert 'NativeStorage' not in stdout and 'NativeCost' not in stdout
        gate.passed_case(language + ' inspection refuses written capability identifiers')

        trees, _ = gate.run('direct inspection completion ' + language,
            definitions + prefix + '(cost:storage $residual))',
            language=language, profile=profile)
        stdout = gate.receipts[-1]['stdout']
        returned = parse(stdout)[-1] if language == 'petta' else trees[-1][0]
        storage(returned, recorded=False)
        assert stdout.splitlines().count(marker) == 0
        gate.passed_case(language + ' direct inspection publishes its completed report without invoking its head')

        trees, _ = gate.run('inspection through dialect binder ' + language,
            definitions + prefix + '(let $x (cost:storage $residual) (Result $x)))',
            language=language, profile=profile)
        stdout = gate.receipts[-1]['stdout']
        returned = parse(stdout)[-1] if language == 'petta' else trees[-1][0]
        # HE substitutes a bound result into executable syntax. This is a
        # separate demand from the native read, and retains HE's semantics.
        assert stdout.splitlines().count(marker) == (1 if language == 'he' else 0)
        if language == 'he':
            assert returned == ['Result', []]
        else:
            assert returned[0] == 'Result'
            storage(returned[1], recorded=False)
        gate.passed_case(language + ' binder retains its own interpretation of returned data')


def cost_clients(gate):
    source = (ROOT / 'tests/prime/native_cost/self_budgeted_search.metta').read_text()
    trees, _ = gate.run('self-budgeted native seven routes', source)
    result = trees[0][0]
    assert result[:4] == ['BudgetedRoutes', [['Route', i] for i in range(7)], 'True', 'False']
    before, retained, after = (compact_receipt(result[4]), storage(result[5], recorded=False),
                               compact_receipt(result[6]))
    assert before['work'][1] >= 100 and after['scope'] == before['scope']
    assert before['counts']['quantum'] < after['counts']['quantum']
    assert after['counts']['storage-probe'] == retained['inspection-probes']
    assert retained['inspection-probes'] >= retained['identities'] + retained['repeated-visits']
    gate.passed_case('program budgets native resumptions from cumulative work, then retains and completes seven routes')

    source_result = trees[1][0]
    assert len(trees) == 2
    assert source_result[:4] == ['SourceBudgetedRoutes', [['Route', i] for i in range(7)], 'True', 'False']
    source_before = equation_receipt(source_result[4])
    source_storage = storage(source_result[5], recorded=False, tracked=False)
    source_after = equation_receipt(source_result[6])
    assert source_before['work'][1] >= 4
    assert source_before['work'][1] < source_after['work'][1]
    assert source_before['telemetry'] is source_after['telemetry'] is None
    assert source_storage['read'] == ['UntrackedCostRead', 'cost-read']
    assert gate.receipts[-1]['stdout'].splitlines().count('SOURCE-BUDGET-ONCE') == 1
    gate.passed_case('the same authored budget policy pauses source admissions and resumes with no native telemetry or event history')

    # Certified work is a function of what the program does.  Fresh processes
    # place the same owned objects at different addresses; the census and
    # every receipt must not see that.
    for attempt in range(3):
        again, _ = gate.run(f'self-budgeted seven routes, fresh process {attempt + 1}', source)
        assert again[0][0] == result, (again[0][0], result)
        assert again[1][0] == source_result, (again[1][0], source_result)
    gate.passed_case('certified work and the storage census do not depend on allocation addresses')

    source_policy = policy(0, '(retain True)', 'equation-admissions-v1')
    source_resume = policy(10000, '(retain True)', 'equation-admissions-v1')
    guarded, guarded_observations = gate.run('budget metric mismatch preserves the actual residual', source +
        f'!(let {PATTERN} (select 7 {source_policy} (budget:routes 0)) '
        '(MetricGuard (budget:work native-counts-v1 $receipt) '
        '(budget:work equation-admissions-v1 $receipt) '
        f'(select 7 {source_resume} $residual)))')
    guarded = guarded[-1][0]
    assert guarded[0] == 'MetricGuard' and guarded[1][:2] == ['budget:work', 'native-counts-v1']
    assert guarded[2] == 0
    assert guarded_observations[-1]['values'] == [['Route', i] for i in range(7)]
    assert equation_receipt(guarded_observations[-1]['cost'])['telemetry'] is None
    gate.passed_case('a source cost cannot silently become native units; the real retained producer remains usable')

    crossing_source = source.replace('budget:search equation-admissions-v1 4 100',
                                    'budget:search equation-admissions-v1 1 100')
    crossing, _ = gate.run('source admission batch crosses the requested threshold', crossing_source)
    crossing = crossing[1][0]
    assert crossing[:4] == source_result[:4]
    assert equation_receipt(crossing[4])['work'][1] > 1
    assert equation_receipt(crossing[6])['telemetry'] is None
    gate.passed_case('a source-work threshold is checked between quanta and retains a batch that crosses it')

    # This profile is tied to Prime's application-equation frontier. The
    # telemetry entry is shared with each extended dialect without importing
    # Prime's source metric or evaluating that profile's demonstration.
    port = source.replace('!(budget:source-demo)', '')
    for language in ('he', 'petta'):
        port_trees, _ = gate.run('self-budgeted telemetry ' + language, port,
                                 language=language, profile='extended')
        returned = parse(gate.receipts[-1]['stdout'])[-1] if language == 'petta' else port_trees[-1][0]
        assert returned[:2] == ['BudgetedRoutes', [['Route', i] for i in range(7)]]
        assert str(returned[2]).lower() == 'true' and str(returned[3]).lower() == 'false'
        port_before, port_after = compact_receipt(returned[4]), compact_receipt(returned[6])
        assert port_before['work'][1] >= 100 and port_before['scope'] == port_after['scope']
        storage(returned[5], recorded=False)
        gate.passed_case(language + ' extended runs the same retained telemetry-budget client without event history')

    source = (ROOT / 'tests/prime/native_cost/analyzed_seven_routes.metta').read_text()
    trees, obs = gate.run('analyzed seven-route admission', source)
    assert trees[0][0][:2] == ['Admitted', 8]
    assert trees[1][0][:2] == ['Deferred', 8]
    assert obs[0]['values'] == [['Route', i] for i in range(7)] and obs[0]['satisfied'] == 'True'
    assert receipt(obs[0]['cost'])['counts']['equation-activation'] <= 8
    gate.passed_case('program admits or defers seven-route search using the finite source bound and its explicit metric')
    assert trees[2][0][:4] == ['Planned', 2, 8, 1]
    assert len(obs) == 2 and obs[1]['values'] == [['Route', i] for i in range(7)]
    assert obs[1]['satisfied'] == 'True'
    assert equation_receipt(obs[1]['cost'])['work'] == ['Exact', 8]
    gate.passed_case('an authored work/depth planner selects workers and reads source admissions separately from telemetry')


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



def dependency_analysis_controls(gate):
    metric = 'equation-dependency-depth'
    source = '(= (depth-root) (depth-leaf)) (= (depth-root) (depth-leaf)) '
    source += '(= (depth-root) Ready) (= (depth-leaf) A) (= (depth-leaf) B) '
    query = '(cost:analyze (quote (depth-root)) 2000'
    trees, _ = gate.run('source work and dependency depth', source +
        f'!{query}) !{query} {metric})')
    work, depth = [analysis_fields(trees[i][0], name) for i, name in
                   enumerate(('equation-activation', metric))]
    assert work['result'] == ['Bound', 7] and depth['result'] == ['Bound', 2]
    gate.passed_case('source dependency depth preserves occurrence work without summing independent alternatives')

    trees, _ = gate.run('computed depth analysis parameters', source +
        ' !(cost:analyze (quote (depth-root)) (+ 1999 1) '
        '(if True equation-dependency-depth equation-activation))')
    assert analysis_fields(trees[0][0], metric)['result'] == ['Bound', 2]
    gate.passed_case('source analysis evaluates the allowance and metric while preserving quoted input')

    pair_metric = 'equation-work-depth'
    trees, _ = gate.run('joint source work and dependency depth', source +
        f' !(cost:analyze (quote (depth-root)) 2000 {pair_metric})')
    pair = analysis_fields(trees[0][0], pair_metric)
    assert pair['result'] == ['Bound', ['WorkDepth', 7, 2]]
    assert pair['steps'] == depth['steps'] == work['steps']
    assert pair['cost']['counts']['match-attempt'] == work['cost']['counts']['match-attempt']
    gate.passed_case('joint work/depth interpretation reuses one source graph and matching pass')

    trees, _ = gate.run('joint source analysis without native chronology', source +
        ' !(cost:analyze (quote (depth-root)) 2000 equation-work-depth native-counts-v1)')
    counted = analysis_fields(trees[0][0], pair_metric, recorded=False)
    assert counted['result'] == pair['result'] and counted['steps'] == pair['steps']
    assert counted['cost']['counts'] == pair['cost']['counts']
    gate.passed_case('source work/depth analysis can retain numerical telemetry without event history')

    trees, _ = gate.run('computed source analysis observation profile', source +
        ' !(cost:analyze (quote (depth-root)) (+ 1999 1) '
        '(if True equation-work-depth equation-activation) '
        '(if True native-counts-v1 native-events-v1))')
    assert analysis_fields(trees[0][0], pair_metric, recorded=False)['result'] == pair['result']
    gate.passed_case('computed allowance, metric and observation profile retain the quoted computation as data')

    trace_capture = (f'!(bind! &trace-analysis (let {ANALYSIS_PATTERN} '
        '(cost:analyze (quote (depth-root)) 2 equation-work-depth) $residual)) ')
    trees, _ = gate.run('source analysis drops chronology without losing its account',
        source + trace_capture + '!(cost:analyze &trace-analysis 0) '
        '!(cost:analyze &trace-analysis 0 equation-work-depth native-counts-v1) '
        '!(cost:analyze &trace-analysis 10 equation-work-depth native-events-v1) '
        '!(cost:trace &trace-analysis) !(cost:analyze &trace-analysis 2000)')
    before = analysis_fields(trees[1][0], pair_metric)
    lowered = analysis_fields(trees[2][0], pair_metric, recorded=False)
    assert before['steps'] == lowered['steps'] == 2
    assert before['residual'] == lowered['residual']
    assert lowered['cost']['work'][1] == before['cost']['work'][1] + 1
    assert lowered['cost']['counts']['cost-read'] == before['cost']['counts']['cost-read'] + 1
    assert all(lowered['cost']['counts'][key] == value for key, value in
               before['cost']['counts'].items() if key != 'cost-read')
    assert trees[3][0][0] == 'cost:analyze'
    compact_receipt(trees[4][0], trace=True)
    completed = analysis_fields(trees[5][0], pair_metric, recorded=False)
    assert completed['result'] == pair['result'] and completed['steps'] == pair['steps']
    gate.passed_case('analysis forgets chronology, preserves paid counters and refuses history resurrection')

    capture = (f'!(bind! &depth-analysis (let {ANALYSIS_PATTERN} '
               f'(cost:analyze (quote (depth-root)) 2 {metric}) $residual)) ')
    trees, _ = gate.run('depth resumes and refuses metric replacement', source + capture +
        '!(cost:analyze &depth-analysis 0) '
        '!(cost:analyze &depth-analysis 10 equation-activation) '
        '!(cost:analyze &depth-analysis 0) !(cost:analyze &depth-analysis 2000)')
    first = analysis_fields(trees[1][0], metric)
    unchanged = analysis_fields(trees[3][0], metric)
    final = analysis_fields(trees[4][0], metric)
    assert trees[2][0][0] == 'cost:analyze'
    assert first['steps'] == unchanged['steps'] == 2
    assert first['result'] == unchanged['result'] == ['Unknown', 'Pending']
    assert first['residual'] == unchanged['residual'] == final['residual']
    assert final['result'] == ['Bound', 2] and final['steps'] == depth['steps']
    gate.passed_case('depth analysis retains its metric, cursor and paid prefix across suspension and refused replacement')

    for name, definitions, expression, result in (
        ('inert value', '', '(Tree A (Leaf B))', ['Bound', 0]),
        ('refuted input', '(= (depth-f A A) Y)', '(depth-f A B)', ['Bound', 0]),
        ('cycle', '(= (depth-loop) (depth-loop))', '(depth-loop)', ['Unknown', 'Cycle']),
        ('unsupported operation', '', '(+ 1 2)', ['Unknown', 'UnsupportedSource']),
        ('graded row', '(=w (depth-graded) A 1)', '(depth-graded)', ['Unknown', 'UnsupportedSource']),
    ):
        trees, _ = gate.run('depth ' + name, definitions +
            f' !(cost:analyze (quote {expression}) 1000 {metric})')
        assert analysis_fields(trees[0][0], metric)['result'] == result
        gate.passed_case('source dependency depth handles ' + name)

    doubling = ' '.join(f'(= (depth-node {i}) ' +
        (f'(depth-node {i + 1})) ' if i < 64 else 'End) ') for i in range(65) for _ in range(2))
    trees, _ = gate.run('finite depth with overflowing occurrence work', doubling +
        f' !(cost:analyze (quote (depth-node 0)) 10000 {metric}) '
        '!(cost:analyze (quote (depth-node 0)) 10000)')
    assert analysis_fields(trees[0][0], metric)['result'] == ['Bound', 65]
    assert analysis_fields(trees[1][0])['result'] == ['Unknown', 'Overflow']
    gate.passed_case('finite source depth remains available when total unfolding work overflows')

    trees, _ = gate.run('depth validates mutable source', source + capture +
        '!(add-atom &self (= (depth-root) NewAnswer)) !(cost:analyze &depth-analysis 1000)')
    assert analysis_fields(trees[-1][0], metric)['result'] == ['Unknown', 'StaleSource']
    gate.passed_case('source changes invalidate retained depth analysis before further graph traversal')

    marker = 'DEPTH-ANALYSIS-MUST-NOT-EXECUTE'
    trees, _ = gate.run('unsupported analysis options preserve quoted code',
        f'!(cost:analyze (quote (println! "{marker}")) 1000 unknown-cost-metric) '
        f'!(cost:analyze (quote (println! "{marker}")) 1000 equation-work-depth unknown-cost-profile)')
    assert len(trees) == 2 and all(tree[0][0] == 'cost:analyze' for tree in trees)
    assert gate.receipts[-1]['stdout'].count(marker) == 2  # Written quoted code only.
    assert not any(line.strip('"') == marker for line in gate.receipts[-1]['stdout'].splitlines())
    gate.passed_case('unknown analysis metrics and profiles preserve quoted source without executing it')



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=ROOT / 'cetta')
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--cases', type=int, default=8)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--population', choices=('all', 'compact', 'equations'), default='all')
    parser.add_argument('--history-controls', action='store_true',
                        help='Check receipt-enabled forgetting and replay boundaries.')
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    gate = Gate(args)
    try:
        if args.population in ('all', 'equations'):
            equation_accounts(gate, args.history_controls)
        if args.population != 'equations':
            compact_accounts(gate)
            if args.history_controls:
                compact_history(gate)
        if args.population == 'all':
            fixed(gate)
            handled_contributions(gate)
            split_runs(gate, args.cases)
            analysis_controls(gate, args.cases)
            dependency_analysis_controls(gate)
            storage_controls(gate)
            cost_clients(gate)
            cost_port_boundaries(gate)
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
