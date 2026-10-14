#!/usr/bin/env python3
"""Native agenda controls using each extended dialect's retained evaluator."""
from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import random

from test_prime_native_control import Gate, ROOT, bag, observations, parse, unchanged_policy_splits
from test_prime_native_parallel import check_cost
from test_prime_weights import (matched_coefficients, matched_source_scopes, retained_queries,
    nested_coefficients, scoped_reads, weight_handler_authority, imported_coefficients, ordered_coefficients, authored_coefficients, admitted_coefficients,
    nested_selection_boundary, authored_algebra_clients, authored_readouts, captured_source_unchanged,
    authored_programs, authored_pln_client, callable_admission, weight_licence_dependencies,
    weight_licence_progress, family_boundaries, ordering_refusal_recovery, controller_agreement)


PATTERN = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'


def policy(budget=3000, mode='FIFO', extra=''):
    return f'(search-policy (control {mode} (budget {budget}) (retain True) {extra}))'


class DialectGate(Gate):
    def __init__(self, args, language):
        super().__init__(args)
        self.language = language

    def run(self, name, source, **kwargs):
        trees, found = super().run(name, source, language=self.language,
                                  profile='extended', **kwargs)
        if self.language == 'petta':
            trees = [[parse(line)[0]] for line in self.receipts[-1]['stdout'].splitlines()
                     if line.startswith('(')]
            found = list(observations(trees))
        return trees, found

    def passed_case(self, name):
        super().passed_case(f'{self.language}: {name}')


def fixed(gate):
    unchanged_policy_splits(gate)
    finite = '(= (rows) 1) (= (rows) 2) (= (rows) 2) '
    _, results = gate.run('closed shortage', finite + f'!(select 7 {policy()} (rows))')
    answer = results[-1]
    assert answer['values'] == [1, 2, 2] and answer['closed'] == 'True'
    assert answer['satisfied'] == 'False'
    gate.passed_case('closed shortage retains duplicate physical rows')

    generators = [
        ('recursive-first', '(= (gen $n) (gen (+ $n 1))) (= (gen $n) $n)',
         '(gen 0)', list(range(7))),
        ('constructor return', '(= (gen) (S (gen))) (= (gen) Z)', '(gen)', None),
    ]
    constructors = ['Z']
    for _ in range(6):
        constructors.append(['S', constructors[-1]])
    for name, source, expression, values in generators:
        _, results = gate.run(name, source + f' !(select 7 {policy()} {expression})')
        answer = results[-1]
        assert answer['values'] == (values if values is not None else constructors), answer
        assert answer['satisfied'] == 'True' and answer['closed'] == 'False'
        gate.passed_case(name + ' yields a live seven-answer prefix')

    source = '(= (coin) 2) (= (coin) 3) (= (f $x) (Pair $x $x)) '
    _, results = gate.run('eager shared argument', source + f'!(select 7 {policy()} (f (coin)))')
    assert results[-1]['closed'] == 'True'
    assert bag(results[-1]['values']) == bag([['Pair', 2, 2], ['Pair', 3, 3]])
    gate.passed_case('one eager argument choice is shared by both uses')

    source = '(= (spin) (spin)) (= (ignore $x) 7) '
    _, results = gate.run('eager unused argument', source + f'!(select 1 {policy(100)} (ignore (spin)))')
    assert results[-1]['values'] == [] and results[-1]['closed'] == 'False'
    assert results[-1]['status'] == 'suspended'
    gate.passed_case('an unused argument keeps the dialect eager')

    source = '(= (rows) (Item 9)) (= (rows) (Item 1)) (= (rows) (Item 5)) '
    _, results = gate.run('candidate priority', source +
        f'!(select 2 {policy(mode="WO", extra="(key (Item $n) $n) (age 32)")} (rows))')
    assert results[-1]['values'] == [['Item', 1], ['Item', 5]], results
    gate.passed_case('candidate bindings supply priorities before body publication')
    _, results = gate.run('zero priority', source +
        f'!(select 0 {policy(mode="WO", extra="(key $x 0) (demand all)")} (rows))')
    assert bag(results[-1]['values']) == bag([['Item', 9], ['Item', 1], ['Item', 5]])
    assert results[-1]['closed'] == 'True'
    gate.passed_case('zero priority preserves every occurrence')

    _, results = gate.run('best two', source +
        f'!(select 2 {policy(extra="(best (Item $n) $n)")} (rows))')
    assert results[-1]['values'] == [['Item', 1], ['Item', 5]]
    assert results[-1]['closed'] == 'True'
    gate.passed_case('finite best-k compares actual completed values')

    source = '(= (rows) (Item 1)) (= (rows) (Item 2)) (= (rows) (Item 3)) '
    source += '(= (valid ((Item 1) (Item 2))) False) '
    source += '(= (valid ((Item 1) (Item 3))) True) '
    source += '(= (valid ((Item 2) (Item 3))) False) '
    _, results = gate.run('joint revision', source +
        f'!(select 2 {policy(extra="(goal $xs (valid $xs))")} (rows))')
    assert results[-1]['values'] == [['Item', 1], ['Item', 3]]
    assert results[-1]['satisfied'] == 'True'
    gate.passed_case('joint search revises its first incompatible pair')

    source = '(= (rows) (Item 1)) (= (rows) (Item 1)) '
    source += '(= (valid ((Item 1) (Item 1))) True) '
    _, results = gate.run('joint duplicate values', source +
        f'!(select 2 {policy(extra="(goal $xs (valid $xs))")} (rows))')
    assert results[-1]['values'] == [['Item', 1], ['Item', 1]]
    gate.passed_case('joint candidates retain equal-valued distinct occurrences')

    source = '(= (loop) (loop)) (= (rows) (Item 1)) (= (rows) (Item 2)) '
    _, results = gate.run('divergent advice', source +
        f'!(select 0 {policy(400, "WO", "(key $x (loop)) (age 2) (demand all)")} (rows))')
    assert results[-1]['values'] == [['Item', 1], ['Item', 2]]
    assert results[-1]['closed'] == 'True', results
    gate.passed_case('divergent pure advice does not obstruct semantic closure')

    source = '(= (rows) 1) (= (rows) 2) '
    source += f'!(let {PATTERN} (select 2 {policy(0)} (rows)) '
    source += f'(let $_ (add-atom &self (= (rows) 3)) (select 2 {policy()} $r)))'
    _, results = gate.run('source mutation', source)
    assert results[-1]['status'] == 'invalidated' and results[-1]['values'] == []
    assert results[-1]['closed'] == 'False'
    gate.passed_case('source mutation invalidates the owned residual')

    source = '(= (rows) 1) '
    source += f'!(select 1 {policy(extra="(key $x (println! FORBIDDEN-GRADE-EFFECT))", mode="WO")} (rows))'
    _, results = gate.run('advice effect', source)
    assert results[-1]['status'] == 'fault'
    assert 'FORBIDDEN-GRADE-EFFECT' not in gate.receipts[-1]['stdout']
    gate.passed_case('pure advice does not acquire source effect authority')


def splits(gate):
    finite = '(= (rows) 1) (= (rows) 2) (= (rows) 2) (= (rows) 3) '
    for budget in (0, 1, 2, 3, 5, 9, 13, 17, 29):
        first = policy(budget)
        second = policy(3000, extra='(demand all)')
        trees, results = gate.run(f'finite split {budget}', finite +
            f'!(let {PATTERN} (select 7 {first} (rows)) (Split $v (select 0 {second} $r)))')
        assert bag(trees[-1][0][1] + results[-1]['values']) == bag([1, 2, 2, 3])
        assert results[-1]['closed'] == 'True'
        gate.passed_case(f'actual machine images preserve finite split {budget}')

    source = '(= (numbers $n) (numbers (+ $n 1))) (= (numbers $n) $n) '
    trees, results = gate.run('resumed key computation', source +
        f'!(let {PATTERN} (select 7 {policy(10)} (numbers 0)) '
        f'(Split $v (select 7 {policy(3000, "PO", "(key (numbers $n) $n)")} $r)))')
    assert trees[-1][0][1] + results[-1]['values'] == list(range(7)), results
    gate.passed_case('regrading retained work preserves the seven-answer demand')


def generated(gate, count):
    rng = random.Random(43813)
    for case in range(count):
        values = [rng.randrange(6) for _ in range(rng.randrange(1, 12))]
        k = rng.randrange(len(values) + 3)
        source = ' '.join(f'(= (rows) (Item {x}))' for x in values)
        _, results = gate.run(f'generated best {case}', source +
            f' !(select {k} {policy(extra="(best (Item $n) $n)")} (rows))')
        assert [x[1] for x in results[-1]['values']] == sorted(values)[:k]
        assert (results[-1]['satisfied'] == 'True') == (k <= len(values))
        gate.passed_case(f'generated duplicate-sensitive best {case}')


def agreement(gate, count):
    source = '(= (coin) A) (= (coin) B) (= (coin) B) '
    source += '(= (dialectIdentity $x) $x) (= (dialectSame $x) (Pair $x $x)) '
    source += '(= (dialectPair $x $y) (Pair $x $y)) (= (dialectFirst (Pair $x $y)) $x) '
    cases = ['(Box (coin))', '(dialectSame (coin))', '(dialectPair (coin) (coin))',
             '(dialectFirst (dialectPair (coin) (coin)))', '(Error foo bad)', 'Empty',
             '(Box Empty)', '(Mystery A B)', '(+ 3 4)', '(+ "bad" 4)']
    rng = random.Random(9419)
    def expression(depth):
        if not depth:
            return rng.choice(['A', 'B', '(coin)'])
        form = rng.choice(['Box', 'dialectIdentity', 'dialectSame', 'dialectPair'])
        a = expression(depth - 1)
        return f'({form} {a} {expression(depth - 1)})' if form == 'dialectPair' else f'({form} {a})'
    cases += [expression(2) for _ in range(count)]
    for case, body in enumerate(cases):
        if gate.language == 'petta' and body == '(+ "bad" 4)':
            gate.run(f'ordinary raised reference {case}', source + f'!(collapse {body})',
                     expected_exit=2)
            ordinary_error = gate.receipts[-1]['stderr']
            gate.run(f'controlled raised reference {case}', source +
                     f'!(select 0 {policy(20000, extra="(demand all)")} {body})',
                     expected_exit=2)
            assert gate.receipts[-1]['stderr'] == ordinary_error
            assert 'uncaught PeTTa error' in ordinary_error
            gate.passed_case(f'ordinary raised-error agreement {case}')
            continue
        ordinary, _ = gate.run(f'ordinary reference {case}', source + f'!(collapse {body})')
        _, results = gate.run(f'controlled reference {case}', source +
            f'!(select 0 {policy(20000, extra="(demand all)")} {body})')
        assert results and results[-1]['closed'] == 'True', (body, results)
        assert bag(results[-1]['values']) == bag(ordinary[-1][0]), (body, ordinary, results)
        gate.passed_case(f'ordinary dialect agreement {case}')

    typed = '(: f (-> Number Number)) (= (f $x) $x) '
    for body in ('(f 3)', '(f "bad")'):
        ordinary, _ = gate.run('ordinary typed', typed + f'!(collapse {body})')
        controlled, results = gate.run('native typed boundary', typed +
            f'!(select 0 {policy(extra="(demand all)")} {body})')
        assert results and results[-1]['closed'] == 'True', controlled
        assert bag(results[-1]['values']) == bag(ordinary[-1][0])
        gate.passed_case('typed contract stays with its qualified dialect provider')


def he_completed_returns(gate):
    """Completed producers and equation continuations keep the HE demand."""
    def type_variables(values):
        # Only this fixture's diagnostic type variables are alpha-renamed;
        # repeated and distinct variable identities remain distinguishable.
        names = {}
        def visit(value):
            if isinstance(value, list):
                return [visit(item) for item in value]
            if isinstance(value, str) and value.startswith('$'):
                return names.setdefault(value, f'$type{len(names)}')
            return value
        return visit(values)

    wrong = lambda value: ['Error', value, ['BadType', 'String', 'Number']]
    cases = [
        ('untyped nullary return', '(= (g) 3)', '(metta (g) String &self)', [wrong(3)]),
        ('untyped applied return', '(= (g $x) 3)', '(metta (g 1) String &self)', [wrong(3)]),
        ('typed single RHS call', '(: f (-> Number String)) (= (f $x) (g)) (= (g) 3)',
         '(f 1)', [wrong(3)]),
        ('typed duplicate RHS calls',
         '(: f (-> Number String)) (= (f $x) (g)) (= (g) 3) (= (g) 3) (= (g) 5)',
         '(f 1)', [wrong(3), wrong(3), wrong(5)]),
        ('first inferred contract',
         '(: multi String) (: multi Number) (: f (-> $t $t)) '
         '(= (f $x) (g)) (= (g) 3) (= (g) 3) (= (g) 5)',
         '(f multi)', [wrong(3), wrong(3), wrong(5)]),
        ('later compatible expected contract',
         '(: multi String) (: multi Number) (: f (-> $t $t)) '
         '(= (f $x) (g)) (= (g) 3) (= (g) 3) (= (g) 5)',
         '(metta (f multi) Number &self)', [3, 3, 5]),
        ('duplicate variable refinement', '(= (g) 3) (= (g) 3)',
         '(let $v (metta (g) $T &self) (Pair $v $T))',
         [['Pair', 3, 'Number'], ['Pair', 3, 'Number']]),
        ('independent result refinements', '(= (g) 3) (= (g) "text")',
         '(let $v (metta (g) $T &self) (Pair $v $T))',
         [['Pair', 3, 'Number'], ['Pair', 'text', 'String']]),
        ('repeated variable refinement',
         '(: pairv (PairType Number Number)) (= (g) pairv) (= (g) pairv)',
         '(let $v (metta (g) (PairType $T $T) &self) (Pair $v $T))',
         [['Pair', 'pairv', 'Number'], ['Pair', 'pairv', 'Number']]),
        ('incompatible repeated refinement',
         '(: pairv (PairType Number String)) (= (g) pairv)',
         '(metta (g) (PairType $T $T) &self)',
         [['Error', 'pairv', ['BadType', ['PairType', '$T', '$T'],
                              ['PairType', 'Number', 'String']]]]),
    ]
    total = 1500
    for name, source, body, expected in cases:
        for profile in ('extended', 'he-compat'):
            for fuel in (-1, 1000):
                trees, _ = Gate.run(gate, name + f' {profile} fuel{fuel}',
                    source + f' !(collapse {body})', language='he', profile=profile,
                    arguments=('--fuel', str(fuel)))
                assert trees[-1][0] == expected, (name, profile, fuel, trees)
        for width in (1, 2):
            def control(allowance):
                return policy(allowance, extra=f'(demand all) (workers {width})')
            _, completed = gate.run(name + f' width{width}', source +
                f' !(select 0 {control(total)} {body})')
            whole = completed[-1]
            assert type_variables(whole['values']) == type_variables(expected) and \
                whole['closed'] == 'True', (name, whole)
            for cut in (0, 1, 7, 23, 41):
                trees, parts = gate.run(name + f' width{width} cut{cut}', source +
                    f' !(let {PATTERN} (select 0 {control(cut)} {body}) '
                    f'(Split (quote $v) (select 0 {control(total-cut)} $r)))')
                assert trees[-1][0][1][0] == 'quote', trees
                joined = {**parts[-1], 'values': trees[-1][0][1][1] + parts[-1]['values']}
                for field in ('values', 'work', 'satisfied', 'closed', 'status'):
                    assert joined[field] == whole[field], (name, width, cut, field, whole, joined)
        gate.passed_case(name + ' preserves completed typing, bindings and complete split work')


def he_applicability_bindings(gate):
    """A suspended HE application owns its caller-visible type refinement."""
    cases = [
        ('caller type and duplicate rows',
         '(: corr (-> $t $t)) (= (corr $x) $x) (= (corr $x) $x)',
         '(let $value (metta (corr 3) $T &self) (Pair $value $T))',
         [['Pair', 3, 'Number'], ['Pair', 3, 'Number']]),
        ('incompatible result refinement',
         '(: corr (-> $t $t)) (= (corr $x) "wrong")',
         '(let $value (metta (corr 3) $T &self) (Pair $value $T))',
         [['Error', 'wrong', ['BadType', 'Number', 'String']]]),
        ('incompatible argument refinement',
         '(: corr (-> $t $t $t)) (= (corr $x $y) $x)',
         '(corr 3 "wrong")',
         [['Error', ['corr', 3, 'wrong'], ['BadArgType', 2, 'Number', 'String']]]),
    ]
    total = 1500
    for name, source, body, expected in cases:
        ordinary, _ = gate.run('ordinary HE ' + name, source + f' !(collapse {body})')
        assert ordinary[-1][0] == expected, (name, ordinary)
        for width in (1, 2):
            def control(allowance):
                return policy(allowance, extra=f'(demand all) (workers {width})')
            _, complete = gate.run(name + f' width{width}', source +
                f' !(select 0 {control(total)} {body})')
            whole = complete[-1]
            assert whole['values'] == expected and whole['closed'] == 'True', whole
            for cut in (0, 1, 2, 3, 7, 13, 23, 41):
                trees, parts = gate.run(name + f' width{width} cut{cut}', source +
                    f' !(let {PATTERN} (select 0 {control(cut)} {body}) '
                    f'(Split (quote $v) (select 0 {control(total-cut)} $r)))')
                assert trees[-1][0][1][0] == 'quote', trees
                joined = {**parts[-1], 'values': trees[-1][0][1][1] + parts[-1]['values']}
                for field in ('values', 'work', 'satisfied', 'closed', 'status'):
                    assert joined[field] == whole[field], (name, width, cut, field, whole, joined)
            gate.passed_case(name + f' retains bindings, duplicates and work through width{width} splits')

    for profile in ('extended', 'he-compat'):
        for name, body, expected in (
            ('decons', '(decons-atom (a b))', ['a', ['b']]),
            ('cons', '(cons-atom a (b))', ['a', 'b']),
            ('chain', '(function (chain (decons-atom (a b)) $pair (return $pair)))',
             ['a', ['b']]),
            ('eval-wrapped chain',
             '(function (chain (eval (decons-atom (a b))) $pair (return $pair)))',
             ['function', ['return', 'NotReducible']] if profile == 'he-compat'
             else ['a', ['b']]),
        ):
            trees, _ = Gate.run(gate, f'bare-space {name} {profile}',
                f'!(bind! &bare (new-space)) !(metta {body} %Undefined% &bare)',
                language='he', profile=profile)
            assert trees[-1] == [expected], (name, profile, trees)
        gate.passed_case(profile + ' interprets embedded instructions in a bare Space')


def host_owned_operand_demands(gate):
    """A retained host call keeps that dialect's operand demand and return."""
    cases = [
        ('held recursive deconstruction', '''
(: held-tail (-> Atom String))
(= (held-tail $nodes)
   (let ($head $tail) (decons-atom $nodes)
     (unify $head Skip (held-tail $tail) (repr $head))))
''', '(held-tail (Skip Skip (quote (println! "HELD-PAYLOAD"))))', []),
        ('held construction', '''
(: held-cons (-> Atom String))
(= (held-cons $tail)
   (let $value (cons-atom (quote (println! "HELD-PAYLOAD")) $tail)
     (repr $value)))
''', '(held-cons (Tail))', []),
        ('required host condition', '''
(= (strict-host)
   (let $_ (assert (let $_ (println! "REQUIRED-DEMAND") (== (+ 1 2) 3))) 7))
''', '(strict-host)', ['REQUIRED-DEMAND']),
    ]

    def effects():
        return [line.strip('"') for line in gate.receipts[-1]['stdout'].splitlines()
                if line.strip('"') in ('HELD-PAYLOAD', 'REQUIRED-DEMAND')]

    for name, source, body, required in cases:
        ordinary, _ = gate.run(name + ' canonical', source + f' !(collapse {body})')
        expected = ordinary[-1][0]
        assert expected and effects() == required, (name, ordinary, effects())
        if name == 'held recursive deconstruction' and gate.language == 'he':
            assert expected == ['(quote (println! "HELD-PAYLOAD"))'], expected
        if name == 'required host condition':
            assert expected == [7], expected
        for graded in (False, True):
            definitions = source + ('\n(=w (unrelated-grade) 0 2)\n' if graded else '')
            actual, _ = gate.run(name + f' ordinary graded={graded}',
                                 definitions + f' !(collapse {body})')
            assert actual[-1][0] == expected and effects() == required, (
                name, graded, expected, actual, effects())
            for width in (1, 2):
                def control(allowance):
                    return policy(allowance, extra=f'(demand all) (workers {width})')
                _, results = gate.run(name + f' graded={graded} width{width}',
                    definitions + f' !(select 0 {control(2000)} {body})')
                whole = results[-1]
                assert whole['values'] == expected and whole['closed'] == 'True' \
                    and effects() == required, (name, graded, width, whole, effects())
                for cut in (0, 1, 4, 9, 17, 31):
                    trees, parts = gate.run(name + f' graded={graded} width{width} cut{cut}',
                        definitions + f' !(let {PATTERN} '
                        f'(select 0 {control(cut)} {body}) '
                        f'(Split (quote $v) (select 0 {control(2000-cut)} $r)))')
                    prefix = trees[-1][0][1]
                    if gate.language == 'he':
                        assert prefix[0] == 'quote', prefix
                        prefix = prefix[1]
                    joined = {**parts[-1], 'values': prefix + parts[-1]['values']}
                    for field in ('values', 'work', 'satisfied', 'closed', 'status'):
                        assert joined[field] == whole[field], (
                            name, graded, width, cut, field, whole, joined)
                    assert effects() == required, (name, graded, width, cut, effects())
                gate.passed_case(name + f' graded={graded} width{width} preserves exact returns and cuts')


def typed_demands(gate):
    if gate.language == 'he':
        he_applicability_bindings(gate)
        he_completed_returns(gate)
        for name, source, body, expected in (
            ('nested Empty', '(= (f) (Box Empty))', '(f)', []),
            ('Empty argument', '(= (f $x) (Box $x))', '(f Empty)', []),
            ('Empty branch', '(= (f) (Box Empty)) (= (f) Good)', '(f)', ['Good']),
        ):
            for fuel in (-1, 100):
                ordinary, _ = gate.run(name + ' fuel ' + str(fuel),
                    source + f' !(collapse {body})', arguments=('--fuel', str(fuel)))
                assert bag(ordinary[-1][0]) == bag(expected), (name, fuel, ordinary)
            for budget in (0, 200):
                _, results = gate.run(name + ' native budget ' + str(budget), source +
                    f' !(select 0 {policy(budget, extra="(demand all)")} {body})')
                assert results[-1]['closed'] == ('True' if budget else 'False')
                assert bag(results[-1]['values']) == bag(expected if budget else [])
                if not budget:
                    assert results[-1]['status'] == 'suspended'
            gate.passed_case(name + ' agrees across prepared, canonical and suspended paths')
        ambiguous = '(: multi String) (: multi Number) (: poly (-> $t $t)) '
        finite = ambiguous + '(= (poly $x) True) '
        ordinary, _ = gate.run('ordinary multiple result contracts', finite +
            '!(collapse (poly multi))')
        _, results = gate.run('controlled multiple result contracts', finite +
            f'!(select 0 {policy(extra="(demand all)")} (poly multi))')
        assert bag(results[-1]['values']) == bag(ordinary[-1][0])
        assert results[-1]['closed'] == 'True'
        gate.passed_case('the first applicable HE contract owns the result judgment')

        recursive = ambiguous + '(= (poly $x) (gen 0)) '
        recursive += '(= (gen $n) (gen (+ $n 1))) (= (gen $n) $n) '
        _, results = gate.run('fair first applicable result contract', recursive +
            f'!(select 4 {policy()} (poly multi))')
        assert results[-1]['satisfied'] == 'True' and results[-1]['closed'] == 'False', results
        contract_prefix = [
            ['Error', n, ['BadType', 'String', 'Number']] for n in range(4)]
        assert bag(results[-1]['values']) == bag(contract_prefix), results
        gate.passed_case('the accepted correlated contract survives recursive native work')
        for budget in (0, 1, 2, 3, 5, 8, 14, 32, 60):
            trees, results = gate.run('multiple-contract split', recursive +
                f'!(let {PATTERN} (select 4 {policy(budget)} (poly multi)) '
                f'(Split (quote $v) (select 4 {policy()} $r)))')
            assert trees[-1][0][1][0] == 'quote'
            assert bag(trees[-1][0][1][1] + results[-1]['values']) == bag(contract_prefix)
            assert results[-1]['satisfied'] == 'True' and results[-1]['closed'] == 'False'
            gate.passed_case(f'first applicable contract frontier survives split {budget}')
        trees, results = gate.run('multiple-contract cancellation', recursive +
            '!(select 4 (search-policy (control FIFO (budget 3000) (retain False))) (poly multi))')
        assert bag(trees[-1][0]) == bag(contract_prefix)
        assert results == []
        gate.passed_case('cancellation releases the remaining typed source frontier')
    cases = [
        ('result mismatch', '(: f (-> Number Number)) (= (f $x) "wrong")', '(f 3)'),
        ('nested argument mismatch', '(: f (-> Number Number)) (= (f $x) $x) (= (g) (f "wrong"))', '(g)'),
        ('nested result demand', '(: f (-> Number Number)) (= (f $x) $x) (= (g) "wrong")', '(f (g))'),
        ('correlated result', '(: f (-> $t $t)) (= (f $x) "wrong")', '(f 3)'),
        ('first applicable overload', '(: f (-> Number Number)) (: f (-> Number String)) (= (f $x) "wrong")', '(f 3)'),
        ('later applicable overload', '(: f (-> Number Number)) (: f (-> String String)) (= (f $x) $x)', '(f "hello")'),
        ('held Atom', '(: f (-> Atom Number)) (= (f $x) 7) (= (spin) (spin))', '(f (spin))'),
        ('held Empty', '(: f (-> Atom Number)) (= (f $x) 7)', '(f Empty)'),
        ('held expression', '(: f (-> Expression Number)) (= (f $x) 8) (= (spin) (spin))', '(f (spin))'),
        ('typed duplicate choices', '(: f (-> Number Number)) (= (f $x) $x) (= (coin) 2) (= (coin) 2) (= (coin) 3)', '(f (coin))'),
        ('typed shared argument', '(: f (-> Number Atom)) (= (f $x) (Pair $x $x)) (= (coin) 2) (= (coin) 3)', '(f (coin))'),
        ('unmatched typed call', '(: f (-> Number Number)) (= (f 3) 4)', '(f 9)'),
        ('unmatched plain call', '(= (f 3) 4)', '(f 9)'),
        ('nested error', '', '(Box (Error foo bad))'),
        ('nested Empty', '(= (f) (Box Empty))', '(f)'),
    ]
    # The Expression domain has different PeTTa source-demand rules.
    for name, source, body in cases:
        if gate.language == 'petta' and name == 'held expression':
            continue
        ordinary, _ = gate.run('ordinary ' + name, source + f' !(collapse {body})')
        _, results = gate.run('controlled ' + name, source +
            f' !(select 0 {policy(20000, extra="(demand all)")} {body})')
        assert results and results[-1]['closed'] == 'True', (name, results)
        assert bag(results[-1]['values']) == bag(ordinary[-1][0]), (name, ordinary, results)
        gate.passed_case('typed demand agreement: ' + name)

    source = '(: gen (-> Number Number)) (= (gen $n) (gen (+ $n 1))) (= (gen $n) $n) '
    for allowance in (0, 1, 3, 11, 37):
        trees, results = gate.run('typed recursive split', source +
            f' !(let {PATTERN} (select 7 {policy(allowance)} (gen 0)) '
            f'(Split $v (select 7 {policy(3000)} $r)))')
        assert trees[-1][0][1] + results[-1]['values'] == list(range(7)), (trees, results)
        assert results[-1]['satisfied'] == 'True' and results[-1]['closed'] == 'False'
        gate.passed_case(f'typed recursive demand retains its return contract at split {allowance}')


def boolean_returns(gate):
    cases = [
        ('untyped scalar true', '(= (bool-result $x) (== $x 0))', '(bool-result 0)'),
        ('untyped scalar false', '(= (bool-result $x) (== $x 0))', '(bool-result 1)'),
        ('typed scalar true', '(: bool-result (-> Number Bool)) '
         '(= (bool-result $x) (== $x 0))', '(bool-result 0)'),
        ('literal lowercase value', '(= (literal-result) true)', '(literal-result)'),
        ('structural false', '', '(== (A 1) ())'),
        ('structural true', '', '(== (A 1) (A 1))'),
        ('structural condition', '', '(if (== (A 1) ()) Yes No)'),
        ('computed condition', '', '(if (== (+ 1 1) 2) Yes No)'),
        ('captured condition', '(= (inspect $x) (if (== $x ()) Nil Nonempty))',
         '(inspect (1 2))'),
        ('returned comparison', '(= (comparison $x) (== $x (A 1))) '
         '(= (items) (A 1)) (= (items) (A 2)) (= (items) (A 1))',
         '(comparison (items))'),
        ('literal lowercase condition', '', '(if false Yes No)'),
    ]
    for name, source, body in cases:
        if name in ('untyped scalar true', 'untyped scalar false',
                    'typed scalar true', 'literal lowercase value'):
            value = 'False' if name == 'untyped scalar false' else 'True'
            if gate.language == 'petta' or name == 'literal lowercase value':
                value = value.lower()
            for fuel in (-1, 1000):
                gate.run(f'completed Boolean {name} fuel={fuel}', source + f' !{body}',
                         arguments=('--fuel', str(fuel)))
                printed = value if gate.language == 'petta' else '[' + value + ']'
                assert gate.receipts[-1]['stdout'].strip() == printed, gate.receipts[-1]
        ordinary, _ = gate.run('canonical Boolean ' + name,
                               source + f' !(collapse {body})')
        expected = ordinary[-1][0]
        for width in (1, 2):
            extra = f'(demand all) (workers {width})'
            _, whole = gate.run('controlled Boolean ' + name, source +
                f' !(select 0 {policy(2000, extra=extra)} {body})')
            assert whole[-1]['closed'] == 'True', (name, whole)
            assert bag(whole[-1]['values']) == bag(expected), (name, ordinary, whole)
            for cut in (0, 5, 17):
                prefix = '(quote $v)' if gate.language == 'he' else '$v'
                trees, parts = gate.run(f'Boolean {name} split{cut}', source +
                    f' !(let {PATTERN} (select 0 {policy(cut, extra=extra)} {body}) '
                    f'(Split {prefix} (select 0 {policy(2000-cut, extra=extra)} $r)))')
                saved = trees[-1][0][1]
                if gate.language == 'he':
                    assert saved[0] == 'quote'
                    saved = saved[1]
                actual = {**parts[-1], 'values': saved + parts[-1]['values']}
                for field in ('values', 'work', 'satisfied', 'closed', 'status'):
                    assert actual[field] == whole[-1][field], (name, width, cut, field, whole, parts)
            gate.passed_case(f'{name} retains host Boolean results and split state on {width} workers')


def he_intrinsics(gate):
    if gate.language != 'he':
        return
    error = ['Error', 'e', 'broken']
    cases = [
        ('open condition', '', '(if $c (Yes $c) (No $c))',
         [['Yes', 'True'], ['No', 'False']]),
        ('wrong condition', '', '(if 2 Yes No)',
         [['Error', ['if', 2, 'Yes', 'No'], ['BadArgType', 1, 'Bool', 'Number']]]),
        ('unknown condition', '', '(if x Yes No)', [['if', 'x', 'Yes', 'No']]),
        ('held divergent alternative', '(= (spin) (spin))', '(if True 7 (spin))', [7]),
        ('absent else', '', '(if False 7)', []),
        ('literal source error', '', '(let $x (Error e broken) (Pair $x $x))', [error]),
        ('computed source error', '(= (mix) (Error e broken))',
         '(let $x (mix) (Pair $x $x))', [error]),
        ('preferred source value', '(= (mix) (Error e broken)) (= (mix) 3)',
         '(let $x (mix) (Pair $x $x))', [['Pair', 3, 3]]),
        ('Empty source removal', '(= (mix) (Error e broken)) (= (mix) Empty)',
         '(let $x (mix) (Pair $x $x))', [error]),
        ('nested Empty source removal', '(= (mix) (Error e broken)) (= (mix) (Box Empty))',
         '(let $x (mix) (Pair $x $x))', [error]),
        ('Empty binding source removal', '(= (mix) (Error e broken)) (= (mix) (let $x Empty $x))',
         '(let $x (mix) $x)', [error]),
        ('pattern refutation follows preference', '(= (mix) (Error e broken)) (= (mix) 3)',
         '(let (Box $x) (mix) $x)', []),
        ('held pattern', '', '(let (F $x) (F 3) (Pair $x $x))', [['Pair', 3, 3]]),
        ('source is interpreted before pattern', '(= (F 3) 9)',
         '(let (F $x) (F 3) (Pair $x $x))', []),
        ('duplicate correlated bindings', '(= (mix) 3) (= (mix) 3) (= (mix) 4)',
         '(let $x (mix) (Pair $x $x))', [['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 4, 4]]),
        ('all errors preserve occurrences', '(= (mix) (Error e broken)) '
         '(= (mix) (Error b broken)) (= (mix) (Error e broken))',
         '(let $x (mix) $x)', [error, ['Error', 'b', 'broken'], error]),
        ('condition cohort', '(= (c) (Error e broken)) (= (c) True)', '(if (c) yes no)', ['yes']),
        ('typed argument cohort', '(: f (-> Number Number)) (= (f $x) $x) '
         '(= (mix) (Error e broken)) (= (mix) 3)', '(f (mix))', [3]),
        ('plain argument cohort', '(= (f $x) $x) (= (mix) (Error e broken)) (= (mix) 3)',
         '(f (mix))', [3]),
        ('primitive argument cohort', '(= (mix) (Error e broken)) (= (mix) 3)', '(+ (mix) 2)', [5]),
        ('nested primitive demand', '(= (two) 2) (= (two) 3)', '(* (+ (two) 1) 4)', [12, 16]),
        ('cohort type alternatives', '(: cohort-id (-> $t $t)) (= (cohort-id $x) $x) '
         '(= (coin) 3) (= (coin) "s")', '(cohort-id (coin))', [3, 's']),
        ('source binds caller variable', '(= (pick (Pair $x $x)) $x)',
         '(let $v (pick (Pair $z 5)) (Pair $v $z))', [['Pair', 5, 5]]),
        ('source binds later argument', '(= (pick (Pair $x $x)) $x) (= (f $x $y) (Pair $x $y))',
         '(f (pick (Pair $z 5)) $z)', [['Pair', 5, 5]]),
        ('recursive intrinsic returns', '(= (f $n) (if (== $n 0) 3 '
         '(let $x (f (- $n 1)) (* $x $x))))', '(% (f 5) 97)', [35]),
    ]
    for name, source, body, expected in cases:
        ordinary, _ = gate.run('canonical HE ' + name, source + f' !(collapse {body})')
        actual = ordinary[-1][0] if ordinary else []
        assert bag(actual) == bag(expected), (name, ordinary, expected)
        for width in (1, 2):
            _, results = gate.run(f'HE intrinsic {name} workers={width}', source +
                f' !(select 0 {policy(20000, extra=f"(demand all) (workers {width})")} {body})')
            assert results[-1]['closed'] == 'True', (name, results)
            assert bag(results[-1]['values']) == bag(expected), (name, results, expected)
            gate.passed_case(f'HE {name} preserves its source contract with {width} workers')

    source = '(= (mix) (Error e broken)) (= (mix) 3) (= (mix) 3) (= (mix) 4) '
    body = '(let $x (mix) (Pair $x $x))'
    _, complete = gate.run('HE cohort complete baseline', source +
        f' !(select 0 {policy(20000, extra="(demand all)")} {body})')
    for width in (1, 2):
        for cut in (0, 1, 4, 9, 17, 31, 65):
            trees, results = gate.run(f'HE cohort split {cut} workers={width}', source +
                f' !(let {PATTERN} (select 0 {policy(cut, extra=f"(demand all) (workers {width})")} {body}) '
                f'(Split (quote $v) (select 0 {policy(20000, extra=f"(demand all) (workers {width})")} $r)))')
            prefix = trees[-1][0][1][1]
            assert bag(prefix + results[-1]['values']) == bag(complete[-1]['values']), (trees, results)
            assert results[-1]['closed'] == 'True'
            if width == 1:
                assert results[-1]['work'] == complete[-1]['work'], (cut, results, complete)
            gate.passed_case(f'HE source and caller images survive split {cut} on {width} workers')

    source = '(= (gen $n) (gen (+ $n 1))) (= (gen $n) $n) '
    for width in (1, 2):
        _, results = gate.run('HE live source values', source +
            f' !(select 7 {policy(10000, extra=f"(workers {width})")} '
            '(let $x (gen 0) (Pair $x $x)))')
        assert results[-1]['values'] == [['Pair', i, i] for i in range(7)], results
        assert results[-1]['satisfied'] == 'True' and results[-1]['closed'] == 'False'
        gate.passed_case(f'HE successful source streams before closure on {width} workers')
        _, results = gate.run('HE source errors wait for closure',
            '(= (spin) (spin)) (= (mix) (Error e broken)) (= (mix) (spin)) ' +
            f' !(select 1 {policy(100, extra=f"(workers {width})")} (let $x (mix) $x))')
        assert results[-1]['values'] == [] and results[-1]['closed'] == 'False', results
        assert results[-1]['status'] == 'suspended'
        gate.passed_case(f'HE unfinished source never certifies fallback errors on {width} workers')
        trees, results = gate.run('HE source cancellation', source +
            f' !(select 1 (search-policy (control FIFO (budget 10000) (workers {width}) '
            '(retain False))) (let $x (gen 0) (Pair $x $x)))')
        assert trees[-1][0] == [['Pair', 0, 0]] and not results
        gate.passed_case(f'HE source cancellation releases retained caller and producer on {width} workers')
        _, results = gate.run('HE pending source mutation', source +
            f' !(let {PATTERN} (select 7 {policy(9, extra=f"(workers {width})")} '
            '(let $x (gen 0) (Pair $x $x))) '
            f'(let $_ (add-atom &self (= (gen 0) Later)) (select 7 {policy()} $r)))')
        assert results[-1]['closed'] == 'False' and results[-1]['status'] == 'invalidated'
        gate.passed_case(f'HE pending source keeps its captured revision on {width} workers')


def he_source_priorities(gate):
    if gate.language != 'he':
        return
    source = '(= (rows) (Item -100)) (= (rows) (Item -1)) (= (rows) (Item -1)) '
    source += '(= (burn $n $v) (if (== $n 0) $v (burn (- $n 1) $v))) '
    body = '(let (Item $n) (rows) (if (== $n -100) (burn 8 (Result $n)) (Result $n)))'
    source += f'(= (wrapped) {body}) '
    weighted = [['Result', -100], ['Result', -1], ['Result', -1]]
    unweighted = [['Result', -1], ['Result', -1], ['Result', -100]]
    for width in (1, 2):
        for wrapped in (False, True):
            term = '(wrapped)' if wrapped else body
            extra = f'(workers {width}) (key (Item $n) $n) (age 32)'
            _, results = gate.run('HE source FIFO return', source +
                f' !(select 3 {policy(10000, "FIFO", extra)} {term})')
            assert results[-1]['values'] == unweighted, results
            gate.passed_case(f'HE FIFO source return preserves ready order wrapped={wrapped} workers={width}')
            for score in ('$n', '(+ $n 0)'):
                extra = f'(workers {width}) (key (Item $n) {score}) (age 32)'
                _, results = gate.run('HE inherited source advice', source +
                    f' !(select 3 {policy(10000, "WO", extra)} {term})')
                assert results[-1]['values'] == weighted, results
                gate.passed_case(f'HE candidate advice crosses caller return wrapped={wrapped} score={score} workers={width}')
        for cut in (0, 9, 15, 25, 40, 60):
            before = policy(cut, 'WO', f'(workers {width}) (key (Item $n) $n) (age 32)')
            after = policy(10000, 'WO', f'(workers {width}) (key (Item $n) (- 0 $n)) (age 32)')
            trees, results = gate.run('HE pending source regrade', source +
                f' !(let {PATTERN} (select 3 {before} (wrapped)) '
                f'(Split (quote $v) (select 3 {after} $r)))')
            assert trees[-1][0][1] == ['quote', []], trees
            assert results[-1]['values'] == unweighted, results
            assert results[-1]['satisfied'] == 'True'
            gate.passed_case(f'HE pending source advice regrades at cut {cut} workers={width}')
        for score in ('0', '(score-spin)'):
            extra = f'(workers {width}) (key (Item $n) {score}) (age 2) (demand all)'
            _, results = gate.run('HE advisory source closure',
                '(= (score-spin) (score-spin)) ' + source +
                f' !(select 0 {policy(10000, "WO", extra)} (wrapped))')
            assert bag(results[-1]['values']) == bag(weighted), results
            assert results[-1]['closed'] == 'True', results
            gate.passed_case(f'HE source advice {score} neither prunes nor obstructs closure workers={width}')


def he_computed_heads(gate):
    if gate.language != 'he':
        return
    cases = [
        ('returned compound head',
         '(= ((captured $x) $y) (Pair $x $y)) (= (make $x) (captured $x))',
         '(let $f (make 7) ($f 8))', [['Pair', 7, 8]], []),
        ('typed compound head',
         '(: captured (-> Number (-> Number Number))) '
         '(= ((captured $x) $y) (+ $x $y))',
         '((captured 7) 8)', [15], []),
        ('duplicate computed heads',
         '(= ((captured $x) $y) (Pair $x $y)) '
         '(= (ops) (captured 1)) (= (ops) (captured 1)) (= (ops) (captured 2))',
         '((ops) 8)', [['Pair', 1, 8], ['Pair', 1, 8], ['Pair', 2, 8]], []),
        ('duplicate compound equations',
         '(= ((captured $x) $y) (Pair $x $y)) '
         '(= ((captured $x) $y) (Pair $x $y)) '
         '(= ((other $x) $y) Wrong)',
         '((captured 7) 8)', [['Pair', 7, 8], ['Pair', 7, 8]], []),
        ('head before argument effects',
         '(= (ops) (let $_ (println! "HEAD-EFFECT") picked)) '
         '(= (picked $x) (Pair $x $x))',
         '((ops) (let $_ (println! "ARG-EFFECT") 7))',
         [['Pair', 7, 7]], ['HEAD-EFFECT', 'ARG-EFFECT']),
        ('Empty head holds argument', '(= (ops) Empty)',
         '((ops) (let $_ (println! "ARG-EFFECT") 7))', [], []),
        ('Error head holds argument', '(= (ops) (Error e broken))',
         '((ops) (let $_ (println! "ARG-EFFECT") 7))',
         [['Error', 'e', 'broken']], []),
        ('successful head cohort suppresses errors',
         '(= (ops) (Error e broken)) (= (ops) picked) (= (ops) picked) '
         '(= (picked $x) (Pair $x $x))',
         '((ops) 7)', [['Pair', 7, 7], ['Pair', 7, 7]], []),
    ]

    def effects():
        return [line for line in gate.receipts[-1]['stdout'].splitlines()
                if line in ('HEAD-EFFECT', 'ARG-EFFECT')]

    for name, source, body, expected, expected_effects in cases:
        ordinary, _ = gate.run('ordinary HE ' + name, source + f' !(collapse {body})')
        assert ordinary[-1][0] == expected, (name, ordinary, expected)
        assert effects() == expected_effects, (name, gate.receipts[-1]['stdout'])
        for width in (1, 2):
            extra = f'(demand all) (workers {width})'
            _, whole = gate.run(f'HE computed head {name} workers={width}', source +
                f' !(select 0 {policy(2000, extra=extra)} {body})')
            expected_result = whole[-1]
            assert expected_result['values'] == expected, (name, whole)
            assert expected_result['closed'] == 'True', (name, whole)
            assert effects() == expected_effects, (name, gate.receipts[-1]['stdout'])
            for cut in (0, 1, 4, 9, 17, 31, 45, 61):
                trees, parts = gate.run(f'HE computed head {name} split={cut} workers={width}',
                    source + f' !(let {PATTERN} '
                    f'(select 0 {policy(cut, extra=extra)} {body}) '
                    f'(Split (quote $v) (select 0 {policy(2000-cut, extra=extra)} $r)))')
                assert trees[-1][0][1][0] == 'quote', trees
                prefix = trees[-1][0][1][1]
                assert prefix + parts[-1]['values'] == expected, (name, cut, trees, parts)
                for field in ('work', 'satisfied', 'closed', 'status'):
                    assert parts[-1][field] == expected_result[field], (name, width, cut,
                        field, parts[-1], expected_result)
                assert effects() == expected_effects, (name, cut, gate.receipts[-1]['stdout'])
            gate.passed_case(f'HE {name} retains head, argument and return through cuts with {width} workers')

    # Cut 31 interrupts a two-occurrence preparation grant after its first
    # operation.  The hub still owns both occurrences and any paid result.
    source = '(= (ops) (Error e broken)) (= (ops) picked) (= (ops) picked) '
    source += '(= (picked $x) (Pair $x $x)) '
    extra = '(demand all) (workers 2)'
    for cut in (30, 31, 33):
        trees, parts = gate.run(f'HE computed head pending grant revision {cut}', source +
            f' !(let {PATTERN} (select 0 {policy(cut, extra=extra)} ((ops) 7)) '
            f'(Split (quote $v) (select 0 {policy(2000, "WO", extra + " (key (Pair $x $y) $x)")} $r)))')
        assert trees[-1][0][1][1] + parts[-1]['values'] == [['Pair', 7, 7], ['Pair', 7, 7]], parts
        assert parts[-1]['closed'] == 'True' and parts[-1]['status'] == 'closed', parts
        gate.passed_case(f'HE pending grant retains paid operations through policy revision at {cut}')
        _, parts = gate.run(f'HE computed head pending grant mutation {cut}', source +
            f' !(let {PATTERN} (select 0 {policy(cut, extra=extra)} ((ops) 7)) '
            '(let $_ (add-atom &self (= (picked $x) Later)) '
            f'(select 0 {policy(2000, extra=extra)} $r)))')
        assert parts[-1]['values'] == [] and parts[-1]['status'] == 'invalidated', parts
        assert parts[-1]['closed'] == 'False', parts
        gate.passed_case(f'HE pending grant cannot publish after source mutation at {cut}')
        trees, parts = gate.run(f'HE computed head pending grant cancellation {cut}', source +
            f' !(select 0 (search-policy (control FIFO (budget {cut}) '
            '(workers 2) (demand all) (retain False))) ((ops) 7))')
        assert not parts and trees[-1][0] == [], (trees, parts)
        gate.passed_case(f'HE pending grant cancellation retires both retained occurrences at {cut}')

    source = '(= ((captured $x) $y) (Pair $x $y)) '
    source += '(= (ops $n) (ops (+ $n 1))) (= (ops $n) (captured $n)) '
    expected = [['Pair', i, 8] for i in range(7)]
    for width in (1, 2):
        extra = f'(workers {width})'
        _, whole = gate.run('HE recursive computed head', source +
            f' !(select 7 {policy(3000, extra=extra)} ((ops 0) 8))')
        assert whole[-1]['values'] == expected and whole[-1]['closed'] == 'False', whole
        assert whole[-1]['satisfied'] == 'True', whole
        for cut in (0, 1, 9, 23, 57, 101):
            trees, parts = gate.run('HE recursive head split', source +
                f' !(let {PATTERN} (select 7 {policy(cut, extra=extra)} ((ops 0) 8)) '
                f'(Split (quote $v) (select 7 {policy(3000-cut, extra=extra)} $r)))')
            assert trees[-1][0][1][1] + parts[-1]['values'] == expected, (trees, parts)
            for field in ('work', 'satisfied', 'closed', 'status'):
                assert parts[-1][field] == whole[-1][field], (cut, parts, whole)
        gate.passed_case(f'HE recursive-first computed head yields a retained prefix with {width} workers')

        for cut in (0, 9, 23):
            _, parts = gate.run('HE computed head source mutation', source +
                f' !(let {PATTERN} (select 7 {policy(cut, extra=extra)} ((ops 0) 8)) '
                '(let $_ (add-atom &self (= ((captured 0) 8) Later)) '
                f'(select 7 {policy(3000, extra=extra)} $r)))')
            assert parts[-1]['status'] == 'invalidated' and parts[-1]['closed'] == 'False', parts
            assert parts[-1]['values'] == [], parts
        gate.passed_case(f'HE computed head revalidates its captured physical equations with {width} workers')

        trees, parts = gate.run('HE computed head cancellation before effects',
            '(= (ops) (let $_ (println! "HEAD-EFFECT") picked)) '
            '(= (picked $x) (Pair $x $x)) '
            f' !(select 7 (search-policy (control FIFO (workers {width}) '
            '(budget 0) (retain False))) ((ops) (let $_ (println! "ARG-EFFECT") 7)))')
        assert not parts and trees[-1][0] == [] and effects() == [], (trees, parts)
        gate.passed_case(f'HE cancelled computed head starts no head or argument effect with {width} workers')


def nested_payloads(gate):
    """Independent host episodes retain their caller's publication authority."""
    # This imported module uses HE's function/return and superpose protocol.
    if gate.language != 'he':
        return
    def rho(body, level=0):
        return ('(rhometta:run (rho:par '
                f'(rho:send (rho:quote rho:nil) (rhometta:eval {body})) '
                f'(rho:recv (rho:quote rho:nil) $r{level} (rho:drop $r{level}))))')

    imported = '!(import! &self rhometta) '
    rows = [
        ('ordinary', '(= (gid $x) $x)'),
        ('wrapper', '(= (gid $x) (weight:where $x 2))'),
        ('annotated', '(=w (gid $x) $x 2)'),
        ('trailing', '(= (gid $x) $x (where 2))'),
    ]
    for name, row in rows:
        source = imported + row + ' '
        for depth in (1, 2):
            body = '(gid 7)'
            for level in range(depth):
                body = rho(body, level)
            trees, _ = gate.run(f'{name} payload depth{depth}', source + '!' + body)
            assert trees[-1] == [['rho:val', 7]], trees
            for width in (1, 2):
                _, observed = gate.run(f'{name} payload depth{depth} workers{width}',
                    source + f'!(select 7 {policy(extra=f"(workers {width})")} {body})')
                assert observed[-1]['values'] == [['rho:val', 7]]
                assert observed[-1]['closed'] == 'True', observed
            gate.passed_case(f'{name} payload depth{depth} restores its enclosing driver')

    for kind, body in [('Empty', 'Empty'), ('Error', '(Error Here Bad)'),
                       ('return', '(function (return 7))')]:
        source = imported + f'(=w (gid) {body} 2) (=w (after) 11 3) '
        for width in (1, 2):
            trees, observed = gate.run(f'payload {kind} restores workers{width}',
                source + f'!(select 7 {policy(extra=f"(workers {width})")} {rho(rho("(gid)", 1), 2)})'
                ' !(after)')
            assert trees[-1] == [11], trees
            values = observed[-1]['values']
            if kind == 'Error':
                assert values == [['rho:val', ['Error', 'Here', 'Bad']]], values
            elif kind == 'return':
                assert values == [['rho:val', 7]], values
            else:
                assert len(values) == 1 and values[0][0] == 'rho:par', values
            assert observed[-1]['closed'] == 'True', observed
        gate.passed_case(f'nested {kind} retires only the inner payload owners')

    source = imported + '(=w (gid $x) $x 2) (=w (gid $x) $x 3) '
    body = rho('(weight:read (algebra 0 1 + *) (gid 7))')
    _, observed = gate.run('payload duplicate coefficients',
        source + f'!(select 7 {policy()} {body})')
    values = observed[-1]['values']
    assert len(values) == 2 and all(v[:1] == ['rho:val'] for v in values), values
    assert bag([v[1][1:3] for v in values]) == bag([[7, 2], [7, 3]]), values
    gate.passed_case('payload readout retains separately graded physical rows')

    # Mutation after the inner query but before outer publication must not
    # turn its borrowed snapshot into a fresh source authority. Empty reads
    # have the same dependency as nonempty reads.
    for initial in (False, True):
        setup = imported + '!(bind! &kb (new-space)) !(bind! &other (new-space)) '
        if initial:
            setup += '!(add-atom &kb (F 7)) '
        setup += '(=w (gid $x) $x 2) '
        body = rho('(match &kb (F $x) (gid $x))')
        for width in (1, 2):
            extra = f'(workers {width})'
            _, whole = gate.run(f'payload read {initial} whole{width}',
                setup + f'!(select 7 {policy(extra=extra)} {body})')
            assert whole[-1]['closed'] == 'True' and whole[-1]['work'] > 3, whole
            cut = whole[-1]['work'] - 3
            for mutation, action in [('same', '(add-atom &kb (F 8))'),
                                     ('other', '(add-atom &other (F 8))'),
                                     ('none', '()')]:
                trees, observed = gate.run(f'payload read {initial} {mutation} workers{width}',
                    setup + f'!(let {PATTERN} (select 7 {policy(cut, extra=extra)} {body}) '
                    f'(Before $v $w (let $_ {action} (select 7 {policy(extra=extra)} $r))))')
                result = observed[-1]
                before = trees[-1][0]
                assert before[2] == cut and before[1] == [], trees
                if mutation == 'same':
                    assert result['status'] == 'invalidated' and result['closed'] == 'False'
                    assert result['values'] == [] and result['work'] == cut, result
                    assert result['residual'][0] == 'NativeHandle', result
                else:
                    assert result['status'] == 'closed' and result['closed'] == 'True', result
                    assert result['work'] == whole[-1]['work'], result
                    if initial:
                        assert result['values'] == [['rho:val', 7]], result
                    else:
                        assert len(result['values']) == 1 and result['values'][0][0] == 'rho:par'
            gate.passed_case(f'payload complete read initial={initial} workers{width} retains scope across cuts')


def worker_operand_readiness(gate):
    source = ('(= (operand-tower $n) (if (== $n 0) 3 '
              '(let $x (operand-tower (- $n 1)) (* $x $x)))) ')
    source += ' '.join(f'(= (operand-jobs) (% (operand-tower {n}) 97))'
                       for n in (4, 5, 4, 5))
    expected = [pow(3, 2**n, 97) for n in (4, 5, 4, 5)]
    def scope(width, budget=10000, receipts=False):
        return policy(budget, extra=f'(workers {width}) (quantum 2048) ' +
                      ('(receipts True)' if receipts else ''))
    for width in (1, 2):
        _, results = gate.run(f'captured operand readiness width{width}', source +
            f' !(select 8 {scope(width, receipts=True)} (operand-jobs))')
        result = results[-1]
        assert result['closed'] == 'True' and bag(result['values']) == bag(expected), result
        assert not result.get('effect-barriers'), result
        if width > 1:
            assert result.get('parallel-waves'), result
    _, whole = gate.run('captured operand split reference', source +
        f' !(select 8 {scope(2)} (operand-jobs))')
    for cut in (0, 1, 5, 17, 100, 200):
        trees, results = gate.run(f'captured operand split {cut}', source +
            f' !(let {PATTERN} (select 8 {scope(2, cut)} (operand-jobs)) '
            f'(Split $v (select 8 {scope(2, 10000-cut)} $r)))')
        current = {**results[-1], 'values': trees[-1][0][1] + results[-1]['values']}
        for field in ('values', 'satisfied', 'closed', 'status', 'work'):
            assert current[field] == whole[-1][field], (cut, field, current, whole[-1])
    gate.passed_case('bound operands and source-variable returns remain local with exact cut/resume observations')


def workers(gate):
    worker_operand_readiness(gate)
    cases = [
        ('numeric rows', '(= (rows) (+ 1 2)) (= (rows) (+ 2 3))', '(rows)', [3, 5]),
        ('duplicate rows', '(= (rows) (+ 1 2)) (= (rows) (+ 1 2))', '(rows)', [3, 3]),
        ('shared argument', '(= (coin) 2) (= (coin) 3) (= (f $x) (Pair $x $x))',
         '(f (coin))', [['Pair', 2, 2], ['Pair', 3, 3]]),
        ('recursive return', '(= (inc $x) (+ $x 1)) (= (native-tree-size Z) 0) '
         '(= (native-tree-size (S $n)) (inc (native-tree-size $n))) '
         '(= (rows) (native-tree-size (S (S (S Z))))) (= (rows) (native-tree-size (S (S (S (S Z))))))',
         '(rows)', [3, 4]),
        ('typed return', '(: f (-> Number Number)) (= (f $x) (+ $x 1)) '
         '(= (rows) (f 2)) (= (rows) (f 3))', '(rows)', [3, 4]),
    ]
    worker_policy = lambda n, budget=10000, extra='': policy(budget,
        extra=f'(workers {n}) (quantum 3) (receipts True) (costs native-events-v1) {extra}')
    cut_policy = lambda budget: policy(budget, extra='(workers 2) (quantum 3)')
    resume_policy = lambda width: policy(10000,
        extra=f'(workers {width}) (quantum 3) (receipts True)')
    for name, source, body, expected in cases:
        for width in (1, 2, 4):
            _, result = gate.run(f'owned dialect worker {name} width={width}',
                source + f' !(select 10 {worker_policy(width)} {body})')
            answer = result[-1]
            assert answer['closed'] == 'True' and bag(answer['values']) == bag(expected), answer
            if width > 1:
                assert answer.get('parallel-waves'), answer
            check_cost(answer)
            gate.passed_case(f'owned {name} retains returns and occurrences with {width} workers')

    source = '(= (coin) 2) (= (coin) 3) (= (f $x) (Pair (+ $x 1) (+ $x 1))) '
    for allowance in (0, 1, 3, 8, 19, 41, 97):
        for width in (1, 2):
            trees, results = gate.run(f'dialect worker split={allowance} width={width}', source +
                f' !(let {PATTERN} (select 10 {cut_policy(allowance)} (f (coin))) '
                f'(Split $v (select 10 {resume_policy(width)} $r)))')
            prefix = trees[-1][0][1]
            values = prefix + results[-1]['values']
            assert results[-1]['closed'] == 'True', results
            assert bag(values) == bag([['Pair', 3, 3], ['Pair', 4, 4]]), (trees, results)
            gate.passed_case(f'dialect worker cut {allowance} resumes on {width} workers')

    if gate.language == 'petta':
        source = '(= (sum $n) (if (== $n 0) 0 (+ $n (sum (- $n 1))))) '
        source += '(= (rows) (let $n (sum 8) (let $_ (println! "DIALECT-EFFECT") (Done $n)))) '
        source += '(= (rows) (sum 9)) '
        for allowance in (0, 7, 41, 130):
            if allowance:
                program = (f' !(let {PATTERN} (select 10 {cut_policy(allowance)} (rows)) '
                    f'(Split $v (select 10 {resume_policy(2)} $r)))')
            else:
                program = f' !(select 10 {worker_policy(2)} (rows))'
            trees, results = gate.run(f'dialect effect cut {allowance}', source + program)
            prefix = trees[-1][0][1] if allowance else []
            assert results[-1]['closed'] == 'True', results
            assert bag(prefix + results[-1]['values']) == bag([['Done', 36], 45]), (trees, results)
            assert gate.receipts[-1]['stdout'].splitlines().count('"DIALECT-EFFECT"') == 1
            if not allowance:
                assert any(x[3] == 'PendingDialectOperation' for r in results
                           for x in r.get('effect-barriers', [])), results
            gate.passed_case(f'dialect effect retains its exact return across cut {allowance}')


def worker_order(gate):
    delayed = '(= (lag Z $v) $v) (= (lag (S $n) $v) (lag $n $v)) '
    delayed += '(= (rows) (lag (S (S (S Z))) 1)) (= (rows) (lag Z 2)) '
    delayed += '(= (rows) (lag Z 2)) '
    # Equal semantic shapes can have very different primitive running times.
    # Publication follows the live agenda, never wall-clock completion order.
    large = '7' * 2000
    primitives = '(= (tag $x $v) $v) '
    primitives += f'(= (rows) (tag (* {large} {large}) Earlier)) '
    primitives += '(= (rows) (tag (* 2 2) Later)) (= (rows) (tag (* 3 3) Later)) '
    cases = [
        ('delayed earlier child', delayed, '(rows)', 10, 'FIFO', ''),
        ('unequal primitive work', primitives, '(rows)', 10, 'FIFO', ''),
        ('delayed priority child', delayed, '(rows)', 2, 'WO', '(key (lag $n $v) $v) (age 62)'),
        ('duplicate priority children',
         '(= (rows) (Item 9)) (= (rows) (Item 1)) (= (rows) (Item 1)) (= (rows) (Item 5))',
         '(rows)', 3, 'WO', '(key (Item $n) $n) (age 32)'),
        ('recursive-first order', '(= (gen $n) (gen (+ $n 1))) (= (gen $n) $n)',
         '(gen 0)', 7, 'FIFO', ''),
    ]
    if gate.language == 'he':
        cases.append(('correlated type order',
            '(: multi String) (: multi Number) (: poly (-> $t $t)) '
            '(= (poly $x) (gen 0)) (= (gen $n) (gen (+ $n 1))) (= (gen $n) $n)',
            '(poly multi)', 4, 'FIFO', ''))
    for name, source, body, k, mode, extra in cases:
        _, reference = gate.run(name + ' logical reference', source +
            f' !(select {k} {policy(30000, mode, "(workers 1) " + extra)} {body})')
        expected = reference[-1]
        for width in (2, 4):
            _, observed = gate.run(name + f' physical width={width}', source +
                f' !(select {k} {policy(30000, mode, f"(workers {width}) (receipts True) " + extra)} {body})')
            current = observed[-1]
            for field in ('values', 'satisfied', 'closed', 'status'):
                assert current[field] == expected[field], (name, field, expected, current)
            assert current.get('parallel-waves'), (name, current)
            gate.passed_case(f'{name} keeps exact logical publication with {width} workers')

    source = '(= (rows) (Item 9)) (= (rows) (Item 1)) (= (rows) (Item 1)) (= (rows) (Item 5)) '
    extra = '(workers 2) (key (Item $n) $n) (age 32)'
    for cut in range(3, 13):
        trees, result = gate.run(f'prepared priority split {cut}', source +
            f' !(let {PATTERN} (select 3 {policy(cut, "WO", extra)} (rows)) '
            f'(Split $v (select 3 {policy(3000, "WO", extra)} $r)))')
        assert trees[-1][0][1] + result[-1]['values'] == [['Item', 1], ['Item', 1], ['Item', 5]]
        assert result[-1]['satisfied'] == 'True'
        gate.passed_case(f'prepared operation and duplicate priority survive cut {cut}')

    for cut in (3, 4, 5, 8):
        _, results = gate.run(f'stale prepared operation {cut}', source +
            f' !(let {PATTERN} (select 3 {policy(cut, "WO", extra)} (rows)) '
            f'(let $_ (add-atom &self (= (rows) (Item 0))) '
            f'(select 3 {policy(3000, "WO", extra)} $r)))')
        assert results[-1]['values'] == [] and results[-1]['status'] == 'invalidated', results
        assert results[-1]['closed'] == 'False'
        gate.passed_case(f'stale prepared source cannot publish at cut {cut}')
        trees, results = gate.run(f'cancel prepared operation {cut}', source +
            f' !(select 3 (search-policy (control WO (workers 2) (budget {cut}) '
            '(retain False) (key (Item $n) $n) (age 32))) (rows))')
        assert results == []
        assert trees[-1][0] == [['Item', 1], ['Item', 1], ['Item', 5]][:len(trees[-1][0])]
        gate.passed_case(f'cancellation retires prepared operations at cut {cut}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=20)
    parser.add_argument('--cases', type=int, default=12)
    parser.add_argument('--receipts', type=Path)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    runtime_binary = Path(os.environ.get('CETTA_WRAPPED_BIN', str(args.binary))).resolve()
    gates = []
    try:
        for language in ('he', 'petta'):
            gate = DialectGate(args, language)
            gates.append(gate)
            fixed(gate)
            matched_coefficients(gate)
            matched_source_scopes(gate)
            retained_queries(gate)
            nested_coefficients(gate)
            scoped_reads(gate)
            weight_handler_authority(gate)
            imported_coefficients(gate)
            authored_coefficients(gate)
            admitted_coefficients(gate)
            callable_admission(gate)
            weight_licence_dependencies(gate)
            weight_licence_progress(gate)
            ordered_coefficients(gate)
            nested_selection_boundary(gate)
            authored_algebra_clients(gate)
            authored_pln_client(gate)
            authored_programs(gate)
            authored_readouts(gate)
            family_boundaries(gate)
            ordering_refusal_recovery(gate)
            controller_agreement(gate)
            captured_source_unchanged(gate)
            splits(gate)
            generated(gate, args.cases)
            agreement(gate, args.cases)
            host_owned_operand_demands(gate)
            typed_demands(gate)
            boolean_returns(gate)
            he_intrinsics(gate)
            he_source_priorities(gate)
            he_computed_heads(gate)
            nested_payloads(gate)
            workers(gate)
            worker_order(gate)
    finally:
        if args.receipts:
            args.receipts.write_text(json.dumps({
                'binary': str(runtime_binary),
                'binary_sha256': sha256(runtime_binary.read_bytes()).hexdigest(),
                'launcher': str(args.binary),
                'launcher_sha256': sha256(args.binary.read_bytes()).hexdigest(),
                'passed': sum(g.passed for g in gates),
                'runs': [receipt for g in gates for receipt in g.receipts],
            }, indent=2) + '\n')
    print(f'Native dialect controls: {sum(g.passed for g in gates)} passed, 0 failed')


if __name__ == '__main__':
    main()
