#!/usr/bin/env python3
"""Native Prime demand, priority and retained-work conformance controls.

The optional Lean reference compares exact finite joint selection against an
independently defined sublist search. Runtime observations are also checked
against exhaustive Python semantics; these checks are not a C refinement proof.
"""
from __future__ import annotations

import argparse
from collections import Counter
from hashlib import sha256
from itertools import combinations
import json
from pathlib import Path
import random
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
TOKENS = re.compile(r'"(?:[^"\\]|\\.)*"|[()\[\]]|[^\s()\[\],]+')


def parse(text):
    tokens = TOKENS.findall(text)
    position = 0
    def term():
        nonlocal position
        value = tokens[position]
        position += 1
        if value in ('(', '['):
            end = ')' if value == '(' else ']'
            items = []
            while tokens[position] != end:
                items.append(term())
            position += 1
            return items
        if value.startswith('"'):
            return json.loads(value)
        try:
            return int(value)
        except ValueError:
            return value
    result = []
    while position < len(tokens):
        result.append(term())
    return result


def observations(value):
    if isinstance(value, list):
        if value and value[0] == 'Observation':
            yield {'values': value[1], **{item[0]: item[1] for item in value[2:]}}
        else:
            for child in value:
                yield from observations(child)


def bag(values):
    return Counter(json.dumps(x, sort_keys=True) for x in values)


def sexpr(values):
    return '(' + ' '.join(sexpr(x) if isinstance(x, list) else str(x) for x in values) + ')'


def route_source(query):
    source = [f'(= (routes) (Route {i} {sexpr(query["coverage"][i])} {query["costs"][i]}))'
              for i in query['values']]
    if not source:
        source = ['(= (routes) (empty))']
    return '\n'.join(source)


def valid(query, chosen):
    covered = set().union(*(set(query['coverage'][i]) for i in chosen)) if chosen else set()
    return set(query['required']) <= covered and not any(
        a in chosen and b in chosen for a, b in query['incompatible'])


class Gate:
    def __init__(self, args):
        self.args = args
        self.receipts = []
        self.passed = 0

    def run(self, name, source, language='prime', profile=None):
        command = [str(self.args.binary), '--lang', language]
        if profile:
            command += ['--profile', profile]
        command += ['-e', source]
        before = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                    timeout=self.args.timeout)
        except subprocess.TimeoutExpired as error:
            self.receipts.append({'name': name, 'language': language, 'profile': profile,
                'source': source, 'source_sha256': sha256(source.encode()).hexdigest(),
                'exit': 'timeout', 'seconds': time.monotonic() - before})
            raise AssertionError(f'{name}: execution exceeded {self.args.timeout} seconds') from error
        receipt = {'name': name, 'language': language, 'profile': profile,
                   'source': source, 'source_sha256': sha256(source.encode()).hexdigest(),
                   'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
                   'seconds': time.monotonic() - before}
        self.receipts.append(receipt)
        assert result.returncode == 0, f'{name}: exit {result.returncode}: {result.stderr}'
        trees = [parse(line)[0] for line in result.stdout.splitlines() if line.startswith('[')]
        return trees, list(observations(trees))

    def passed_case(self, name):
        self.passed += 1
        print(f'PASS: {name}', flush=True)

    def fixture(self, name):
        path = ROOT / 'tests/prime/native_control' / f'{name}.metta'
        source = path.read_text().replace('../../../lib/', str(ROOT / 'lib') + '/')
        return self.run(name, source)

    def fixed(self):
        trees, obs = self.fixture('any_seven')
        assert trees[0] == [list(range(7))]
        assert trees[2] == [list(range(10, 17))]
        assert trees[3] == [list(range(7))]
        values = ['Z']
        for _ in range(6):
            values.append(['S', values[-1]])
        assert trees[1] == [values]
        self.passed_case('recursive-first, constructors, non-tail and demanded patterns')
        _, mixed = self.fixture('mixed_lazy_patterns')
        assert mixed[0]['values'] == [7, 0, 1, 2, 3, 4, 5]
        assert mixed[0]['satisfied'] == 'True' and mixed[0]['closed'] == 'False'
        assert bag(mixed[1]['values']) == bag([7, 1, 2]) and mixed[1]['closed'] == 'True'
        assert bag(mixed[2]['values']) == bag([
            ['Pair', ['Box', 1], ['Box', 1]], ['Pair', ['Box', 2], ['Box', 2]],
            ['Pair', ['Other', 3], ['Other', 3]], 1, 2])
        assert mixed[2]['closed'] == 'True'
        self.passed_case('mixed lazy and strict equations conserve independent and shared choices')
        trees, _ = self.fixture('choices_and_nested')
        assert bag(trees[0][0]) == bag([['Pair', 0, 0], ['Pair', 1, 1]])
        assert bag(trees[1][0]) == bag([['Pair', x, y] for x in range(2) for y in range(2)])
        assert 0 in list(flatten(trees[2])) and 2 in list(flatten(trees[2]))
        self.passed_case('shared and independent choices, nested finite demand')
        _, obs = self.fixture('nested_fairness')
        assert obs[-1]['values'] == [42] and obs[-1]['satisfied'] == 'True'
        assert obs[-1]['closed'] == 'False'
        self.passed_case('barren nested demand cannot monopolize outer search')
        _, obs = self.fixture('closure_and_suspension')
        assert obs[0]['values'] == list(range(6)) and obs[0]['closed'] == 'True'
        assert obs[0]['satisfied'] == 'False'
        assert bag(obs[1]['values']) == bag(list(range(6)))
        assert obs[1]['closed'] == 'False' and obs[1]['status'] == 'suspended'
        self.passed_case('closed shortage differs from unfinished barren branch')
        _, obs = self.fixture('priority_before_force')
        assert obs[-1]['values'] == [['Candidate', 9, 'answer']]
        self.passed_case('zero priority is retained and scored before divergent body')
        trees, obs = self.fixture('resume_and_bulk')
        assert trees[0][0][1] == [1, 2] and obs[0]['values'] == [2, 3]
        assert obs[0]['closed'] == 'True' and obs[1]['values'] == list(range(7))
        self.passed_case('owned pause/resume, duplicate occurrence and residual bulk')
        _, obs = self.fixture('priority_regrade')
        assert bag(obs[-1]['values']) == bag([['Item', 'a', 4], ['Item', 'b', 1],
                                             ['Item', 'c', 3], ['Item', 'd', 2]])
        self.passed_case('reprioritization preserves pending occurrence bag')
        _, obs = self.fixture('best_seven')
        assert [r[2] for r in obs[-1]['values']] == [0, 0, 2, 2, 3, 3, 4]
        assert obs[-1]['satisfied'] == 'True' and obs[-1]['closed'] == 'False'
        self.passed_case('live cyclic best seven with equal-cost distinct derivations')
        _, obs = self.fixture('joint_seven')
        chosen = obs[-1]['values']
        assert len(chosen) == 7 and set().union(*(set(r[2]) for r in chosen)) == set(range(7))
        self.passed_case('seven routes cover seven obligations from eight alternatives')
        _, obs = self.fixture('compatibility')
        assert {r[1] for r in obs[-1]['values']} == {'necessary', 'alternate'}
        self.passed_case('joint search revises a locally attractive incompatible choice')
        trees, obs = self.fixture('joint_feedback')
        feedback = trees[-1][0]
        assert feedback[0] == 'Feedback' and feedback[1] == [['Route', 'a', [0], 8]]
        assert feedback[2] == []
        assert {r[1] for r in obs[-1]['values']} == set('bcdefgh')
        arrival = [r[1] for r in obs[-1]['values']]
        assert arrival.index('c') < arrival.index('b')
        assert obs[-1]['satisfied'] == 'True' and obs[-1]['work'] > feedback[3]
        revisions = obs[-1]['consumer-revisions']
        assert len(revisions) == 1
        fields = {x[0]: x[1] for x in revisions[0][1:]}
        assert fields['from'] == 1 and fields['to'] == 2
        assert fields['retained-witnesses'] == 1
        self.passed_case('coverage feedback regrades retained joint work without committing the seed')
        _, obs = self.fixture('shared_derivations')
        assert [r[2] for r in obs[-1]['values']] == [2, 2, 3, 3, 4, 4, 5]
        assert obs[-1]['closed'] == 'True'
        self.passed_case('shared AND/OR derivation graph preserves tied proof occurrences')
        _, obs = self.fixture('publication_boundaries')
        assert obs[0]['values'] == [['Route', 'delayed', 0]]
        assert all(obs[i]['values'] == [] and obs[i]['status'] == 'suspended' for i in (1, 3))
        assert obs[2]['values'] == [['Route', 0]] * 3 and obs[2]['closed'] == 'False'
        self.passed_case('source-derived bounds reject negative and uncovered alternatives')
        trees, obs = self.fixture('best_resume')
        assert trees[0][0][1] == [['Route', i] for i in range(3)]
        assert obs[0]['values'] == [['Route', i] for i in range(3, 7)]
        assert trees[1][0][1] == [['Item', 'b', 2]]
        assert bag(obs[1]['values']) == bag([['Item', 'a', 8], ['Item', 'c', 3]])
        assert obs[2]['values'] == [['Item', 'b', 2], ['Item', 'c', 3], ['Item', 'a', 8]]
        assert obs[2]['satisfied'] == 'True' and obs[2]['closed'] == 'True'
        self.passed_case('best-prefix resumption and residual bulk do not replay accepted answers')
        trees, obs = self.fixture('ownership')
        assert trees[0][0][0] == 'select'
        assert obs[0]['status'] == 'invalidated' and obs[0]['closed'] == 'False'
        assert obs[1]['status'] == 'fault' and obs[1]['values'] == []
        assert '\nGRADE-EFFECT-RAN\n' not in self.receipts[-1]['stdout']
        assert trees[-2][0][0] == 'select' and trees[-1][0][0] == 'select'
        self.passed_case('owned handles, source revision and grading effect boundary')
        _, obs = self.fixture('advisory_scores')
        assert obs[0]['values'] == [['Item', 'a', 1]] and obs[0]['closed'] == 'True'
        assert obs[0]['satisfied'] == 'False'
        assert obs[1]['values'] == [['Item', 'a', 1]] and obs[1]['satisfied'] == 'True'
        assert obs[2]['values'] == [['Job', 1, 'Done']]
        self.passed_case('divergent advisory scores preserve answers; finite scores reprioritize live work')
        _, revised = self.fixture('consumer_revision')
        for obs in revised[:2]:
            assert obs['values'] == [] and obs['closed'] == 'True' and obs['satisfied'] == 'False'
            receipt = obs['consumer-revisions'][0]
            fields = {x[0]: x[1] for x in receipt[1:]}
            assert fields['from'] == 1 and fields['to'] == 2
            assert fields['retained-witnesses'] == 2
            assert fields['cancelled-occurrences'] and fields['cancelled-frames'] > 0
        assert revised[2]['values'] == [1] and revised[2]['satisfied'] == 'True'
        assert revised[3]['values'] == [2] and revised[3]['satisfied'] == 'True'
        assert revised[4]['status'] == 'fault' and revised[4]['values'] == []
        assert revised[5]['values'] == [1, 2] and revised[5]['closed'] == 'True'
        self.passed_case('consumer cancellation receipts isolate new goals, costs and source capabilities')

    def joint_budget_splits(self):
        # The slow goal retains real native Need work across the split.  The
        # false goal also forces preservation of the collection cursor itself.
        scenarios = [
            ('pending-predicate', [1], 1, '(later 30 True)'),
            ('exhausted-collections', list(range(7)), 3, 'False'),
            ('last-compatible-collection', list(range(6)), 3,
             '(later 3 (== $xs (3 4 5)))'),
            ('duplicate-witnesses', [1, 1, 2, 2], 2, '(later 4 (== $xs (2 2)))'),
        ]
        for name, values, k, goal in scenarios:
            source = '\n'.join(f'(= (items) {value})' for value in values)
            source += '\n(= (later $n $value) (if (== $n 0) $value (later (- $n 1) $value)))'
            def policy(budget, receipts=False, key=None):
                strategy = 'WO' if key is not None else 'FIFO'
                scoring = f' (key $candidate {key})' if key is not None else ''
                receipt = ' (receipts True)' if receipts else ''
                return f'(search-policy (control {strategy}{scoring} (goal $xs {goal}) (budget {budget}){receipt}))'
            _, completed = self.run(f'joint-complete-{name}', source +
                f'\n!(select {k} {policy(100000)} (items))')
            expected = completed[-1]
            total = expected['work']
            assert total > 2 and (expected['satisfied'] == 'True' or expected['closed'] == 'True')
            first = total // 2
            second = total - first
            prefix = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
            program = source + f'\n!(let {prefix} (select {k} {policy(first)} (items)) '
            program += f'(Split $p $w (select {k} {policy(second, True)} $r)))'
            trees, resumed = self.run(f'joint-split-{name}', program)
            result = resumed[-1]
            assert trees[-1][0][1] == [], (name, trees)
            assert result['values'] == expected['values'], (name, expected, result)
            assert result['satisfied'] == expected['satisfied'], (name, expected, result)
            assert result['closed'] == expected['closed'], (name, expected, result)
            assert result['work'] == total and result['consumer-revisions'] == [], (name, result)
            self.passed_case(f'joint budget split retains cursor and predicates: {name}')

            # Altering the key starts only advisory scoring; the old predicate
            # generation must finish even though its remaining budget is less
            # than the time required to recompute it from the beginning.
            if name == 'pending-predicate':
                program = source + f'\n!(let {prefix} (select {k} {policy(first)} (items)) '
                program += f'(Regrade $p (select {k} {policy(second + 50, True, "(- 0 $candidate)")} $r)))'
                _, graded = self.run('joint-key-regrade-keeps-predicate', program)
                assert graded[-1]['values'] == expected['values']
                assert graded[-1]['satisfied'] == 'True'
                assert graded[-1]['consumer-revisions'] == []
                self.passed_case('key-only joint regrade preserves the pending predicate generation')

        # Re-observing a satisfied absolute demand does not replay it.  A new
        # cardinality is a genuinely new collection consumer over the same
        # retained witnesses, even when its predicate has identical syntax.
        source = '(= (items) 1)\n(= (items) 2)'
        observation = '(Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
        repeated = '(Observation $q (satisfied $u) (closed $v) (residual $h) (work $z) (status $a))'
        same = '(search-policy (control FIFO (goal $xs True) (retain True)))'
        revised = '(search-policy (control FIFO (goal $xs True) (receipts True) (budget 1000)))'
        program = source + f'\n!(let {observation} (select 1 {same} (items)) '
        program += f'(let {repeated} (select 1 {same} $r) '
        program += f'(Cardinality $p $q $u $w $z (select 2 {revised} $h))))'
        trees, result = self.run('joint-absolute-and-cardinality-revision', program)
        record = trees[-1][0]
        assert record[1] == [1] and record[2] == [] and record[3] == 'True'
        assert record[4] == record[5]
        assert result[-1]['values'] == [1, 2] and result[-1]['satisfied'] == 'True'
        revisions = result[-1]['consumer-revisions']
        assert len(revisions) == 1
        fields = {x[0]: x[1] for x in revisions[0][1:]}
        assert fields['from'] == 1 and fields['to'] == 2
        self.passed_case('joint absolute observation does not replay; cardinality revision retains source')


    def budget_splits(self, count):
        rng = random.Random(0xB0D6E7)
        for case in range(count):
            values = [rng.randrange(4) for _ in range(rng.randrange(1, 10))]
            first, second = rng.randrange(100), rng.randrange(100)
            demand = len(values) + 1
            source = '\n'.join(f'(= (items) {v})' for v in values)
            control = lambda budget: f'(search-policy (control FIFO (budget {budget})))'
            complete = source + f'\n!(select {demand} {control(first + second)} (items))'
            _, combined = self.run(f'budget-combined-{case}', complete)
            split = source + '\n!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t)) '
            split += f'(select {demand} {control(first)} (items)) '
            split += f'(BudgetSplit $p $w (select {demand} {control(second)} $r)))'
            trees, resumed = self.run(f'budget-split-{case}', split)
            assert trees[-1][0][1] + resumed[-1]['values'] == combined[-1]['values']
            for field in ('work', 'closed', 'satisfied', 'status'):
                assert resumed[-1][field] == combined[-1][field], (case, field, resumed, combined)
            self.passed_case(f'generated exact residual budget split {case}')

    def mutation_controls(self, mutation=None):
        kinds = [mutation] if mutation else ['DROP', 'DUPLICATE', 'INVENT', 'FALSE_CLOSED', 'EARLY_BEST']
        for kind in kinds:
            if kind == 'FALSE_CLOSED':
                source = '(= (loop) (loop)) !(select 7 (search-policy (control FIFO (budget 0))) (loop))'
                _, obs = self.run('mutation-control-' + kind, source)
                valid_result = obs[-1]['closed'] == 'False' and obs[-1]['status'] == 'suspended'
            elif kind == 'EARLY_BEST':
                source = '(= (items) (Item immediate 9)) (= (items) (Item delayed (+ (+ 0 0) 0))) '
                source += '!(select 1 (search-policy (control FIFO (best (Item $id $cost) $cost) (budget 1000))) (items))'
                _, obs = self.run('mutation-control-' + kind, source)
                valid_result = obs[-1]['values'] == [['Item', 'delayed', 0]]
            else:
                source = '(= (items) 1) (= (items) 2) (= (items) 2) (= (items) 3) '
                source += '!(select 8 (search-policy (control FIFO (budget 1000))) (items))'
                _, obs = self.run('mutation-control-' + kind, source)
                valid_result = bag(obs[-1]['values']) == bag([1, 2, 2, 3])
            assert valid_result != bool(mutation), (kind, obs)
            self.passed_case(('rejected compiled mutation ' if mutation else 'positive mutation control ') + kind)

    def catalogues(self, count):
        rng = random.Random(0xCA7A106)
        queries = []
        for case in range(count):
            n = rng.randrange(2, 8)
            successors = [[j for j in range(i + 1, n) if rng.randrange(3) == 0] for i in range(n)]
            emissions = [rng.randrange(4) if rng.randrange(4) else None for _ in range(n)]
            costs = [rng.randrange(10) for _ in range(4)]
            roots = [0, rng.randrange(n)]
            def denotation(i):
                return ([] if emissions[i] is None else [emissions[i]]) + [
                    value for child in successors[i] for value in denotation(child)]
            expected = [value for root in roots for value in denotation(root)]
            bounds = [min([costs[value] for value in denotation(i)], default=0) for i in range(n)]
            # A barren child can lower a structural predecessor bound without
            # adding an answer; calculate the local admissible recurrence.
            for i in reversed(range(n)):
                bounds[i] = min(([costs[emissions[i]]] if emissions[i] is not None else []) +
                                [bounds[j] for j in successors[i]], default=0)
            query = {'command': 'publication', 'k': len(expected) + 1, 'roots': roots,
                     'successors': successors, 'emissions': emissions, 'keys': list(range(n)),
                     'costs': costs, 'bounds': bounds, 'budgets': [10000], 'strategy': 'fifo'}
            queries.append((query, expected))
            lines = [f'(= (start) (node{root}))' for root in roots]
            for i in range(n):
                lines += [f'(= (node{i}) (node{j}))' for j in successors[i]]
                if emissions[i] is not None:
                    v = emissions[i]
                    lines.append(f'(= (node{i}) (Answer {v} {costs[v]}))')
                elif not successors[i]:
                    lines.append(f'(= (node{i}) (empty))')
            source = '\n'.join(lines)
            _, obs = self.run(f'catalogue-all-{case}', source +
                '\n!(select 0 (search-policy (control FIFO (demand all) (budget 20000))) (start))')
            assert obs[-1]['closed'] == 'True'
            assert bag([v[1] for v in obs[-1]['values']]) == bag(expected), (case, obs, expected)
            _, best = self.run(f'catalogue-best-{case}', source +
                '\n!(select 1 (search-policy (control WO (best (Answer $value $cost) $cost) (budget 20000))) (start))')
            if expected:
                assert best[-1]['satisfied'] == 'True' and best[-1]['values'][0][2] == min(costs[v] for v in expected)
            else:
                assert best[-1]['values'] == [] and best[-1]['closed'] == 'True'
            self.passed_case(f'independent derivation catalogue and certified minimum {case}')
        if self.args.demand_reference:
            command = ['lake', 'env', 'lean', '--run', str(self.args.demand_reference)]
            result = subprocess.run(command, cwd=self.args.lean_root, text=True,
                input=''.join(json.dumps(q) + '\n' for q, _ in queries), capture_output=True,
                timeout=max(self.args.timeout, 120))
            assert result.returncode == 0, result.stdout + result.stderr
            replies = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            assert len(replies) == len(queries), result.stdout
            for (query, expected), reply in zip(queries, replies):
                assert reply['closed'] and not reply['satisfied'] and reply['localBoundsValid']
                assert bag(reply['answers']) == bag(expected), (query, reply, expected)
                if expected:
                    assert reply['published'] is not None
            self.receipts.append({'name': 'Lean demand and publication reference', 'command': command,
                'reference_sha256': sha256(self.args.demand_reference.read_bytes()).hexdigest(),
                'queries': [q for q, _ in queries], 'replies': replies})
            self.passed_case(f'Lean demand/closure/publication reference agrees on {len(queries)} catalogues')

    def generated(self, count):
        rng = random.Random(0xC377A)
        queries = []
        for case in range(count):
            n = rng.randrange(1, 8)
            query = {'command': 'joint', 'k': rng.randrange(0, n + 2), 'values': list(range(n)),
                     'coverage': [[j for j in range(3) if rng.randrange(3) == 0] for _ in range(n)],
                     'required': [j for j in range(3) if rng.randrange(2)],
                     'incompatible': [[a, b] for a, b in combinations(range(n), 2) if rng.randrange(7) == 0],
                     'costs': [rng.randrange(10) for _ in range(n)]}
            queries.append(query)
            source = f'!(import! &self {ROOT / "lib/native_search_goals.metta"})\n' + route_source(query)
            source += f'\n!(select {query["k"]} (search-policy (control FIFO '
            source += f'(goal $xs (search:joint? {sexpr(query["required"])} {sexpr(query["incompatible"])} $xs)) '
            source += '(budget 200000))) (routes))'
            _, obs = self.run(f'joint-{case}', source)
            answer = obs[-1]
            possible = [list(c) for c in combinations(query['values'], query['k']) if valid(query, c)]
            assert (answer['satisfied'] == 'True') == bool(possible), (case, query, answer)
            if possible:
                chosen = [r[1] for r in answer['values']]
                assert len(chosen) == query['k'] and len(set(chosen)) == len(chosen) and valid(query, chosen)
            else:
                assert answer['closed'] == 'True', (case, answer)
            self.passed_case(f'generated finite joint {case}')
            k = rng.randrange(0, n + 2)
            costs = [2**53 + 1, 2**53] + query['costs'] if case == 0 else query['costs']
            best = '\n'.join(f'(= (items) (Item {i} {cost}))' for i, cost in enumerate(costs))
            best += f'\n!(select {k} (search-policy (control WO (best (Item $id $cost) $cost) (budget 10000))) (items))'
            _, obs = self.run(f'best-{case}', best)
            result = obs[-1]
            assert [r[2] for r in result['values']] == sorted(costs)[:k]
            assert (result['satisfied'] == 'True') == (k <= len(costs))
            assert k == 0 or result['closed'] == 'True'
            self.passed_case(f'generated exact numeric best {case}')
        if self.args.lean_reference:
            command = ['lake', 'env', 'lean', '--run', str(self.args.lean_reference)]
            raw = subprocess.run(command, cwd=self.args.lean_root, text=True,
                                 input=''.join(json.dumps(q) + '\n' for q in queries),
                                 capture_output=True, timeout=max(self.args.timeout, 120))
            assert raw.returncode == 0, raw.stderr + raw.stdout
            replies = [json.loads(line) for line in raw.stdout.splitlines() if line.startswith('{')]
            assert len(replies) == len(queries), raw.stdout
            for query, answer in zip(queries, replies):
                possible = [list(c) for c in combinations(query['values'], query['k']) if valid(query, c)]
                assert answer['found'] == bool(possible)
                assert answer['candidateCount'] == len(possible)
                if answer['found']:
                    assert valid(query, answer['chosen']) and len(answer['chosen']) == query['k']
            self.receipts.append({'name': 'Lean joint reference', 'command': command,
                                  'reference_sha256': sha256(self.args.lean_reference.read_bytes()).hexdigest(),
                                  'queries': queries, 'replies': replies})
            self.passed_case(f'Lean sublist reference agrees on {len(queries)} independently checked cases')


def flatten(value):
    if isinstance(value, list):
        for x in value:
            yield from flatten(x)
    else:
        yield value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=ROOT / 'cetta')
    parser.add_argument('--cases', type=int, default=24)
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--lean-root', type=Path)
    parser.add_argument('--lean-reference', type=Path)
    parser.add_argument('--demand-reference', type=Path)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--mutation', choices=['DROP', 'DUPLICATE', 'INVENT', 'FALSE_CLOSED', 'EARLY_BEST'])
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    gate = Gate(args)
    try:
        if args.mutation:
            gate.mutation_controls(args.mutation)
        else:
            gate.fixed()
            gate.joint_budget_splits()
            gate.generated(args.cases)
            gate.budget_splits(args.cases)
            gate.mutation_controls()
            gate.catalogues(args.cases)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True, exist_ok=True)
            args.receipt.write_text(json.dumps({'binary_sha256': sha256(args.binary.read_bytes()).hexdigest(),
                'eval_sha256': sha256((ROOT / 'src/eval.c').read_bytes()).hexdigest(),
                'passed': gate.passed, 'receipts': gate.receipts}, indent=2) + '\n')
    print(f'Prime native control: {gate.passed} passed, 0 failed')


if __name__ == '__main__':
    main()
