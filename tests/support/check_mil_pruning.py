#!/usr/bin/env python3
"""Finite MIL failure guards against independent complete enumeration.

The reference enumerates complete substitutions before checking examples.
Native guards may avoid whole right-hand families, but must retain the same
successful program occurrences. Executable background follows its old route.
"""

from __future__ import annotations

import argparse
from collections import Counter
import itertools
import json
import os
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_search import check_controls
from check_mil_skeletons import holes, instantiate, sort, types
from gen_semiring_fixtures import sexpr


DECLARATIONS = [('dead', 'A', 'A'), ('step', 'A', 'A'), ('step', 'A', 'A'),
                ('finish', 'A', 'A'), ('good', 'A', 'A'), ('bad', 'A', 'A')]
EDGES = [('step', 'a', 'b'), ('step', 'a', 'b'), ('step', 'b', 'c'),
         ('finish', 'b', 'c'), ('good', 'a', 'c'), ('bad', 'a', 'bad')]


def key(value):
    return sexpr(value)


def run(program, value, edges):
    if isinstance(program, str):
        return [target for name, source, target in edges if name == program and source == value]
    op, *arguments = program
    if op == 'MIL.Id':
        return run(arguments[0], value, edges)
    if op == 'MIL.Chain':
        return [answer for middle in run(arguments[0], value, edges)
                for answer in run(arguments[1], middle, edges)]
    if op == 'MIL.Choice':
        return run(arguments[0], value, edges) + run(arguments[1], value, edges)
    if op in ('MIL.Map', 'MIL.HO1'):
        inner = arguments[-1]
        return [list(items) for items in itertools.product(*(run(inner, x, edges) for x in value))]
    if op == 'MIL.Closure':
        def walk(current, seen):
            answers = []
            for target in run(arguments[0], current, edges):
                answers.append(target)
                if key(target) not in seen:
                    answers.extend(walk(target, seen | {key(target)}))
            return answers
        return walk(value, {key(value)})
    raise AssertionError(program)


def consistent(program, positive, negative, mode, edges):
    for source, expected in positive:
        answers = run(program, source, edges)
        if expected not in answers or (mode == 'MIL.Functional' and any(x != expected for x in answers)):
            return False
    return all(expected not in run(program, source, edges) for source, expected in negative)


def safe_prefix(program, positive, negative, mode, edges):
    if isinstance(program, str) or program[0] not in ('MIL.Chain', 'MIL.Choice'):
        return True
    first = program[1]
    if program[0] == 'MIL.Chain':
        return all(run(first, source, edges) for source, _ in positive)
    return (all(expected not in run(first, source, edges) for source, expected in negative)
            and (mode != 'MIL.Functional' or all(
                all(answer == expected for answer in run(first, source, edges))
                for source, expected in positive)))


def complete(skeleton, target, declarations):
    names = list(dict.fromkeys(holes(skeleton)))
    result = []
    for selected in itertools.product(declarations, repeat=len(names)):
        assignment = dict(zip(names, selected))
        result.extend([instantiate(skeleton, assignment)] * types(skeleton, assignment, declarations).count(target))
    return result


def execute(binary, source, path):
    check_controls(source)
    path.write_text(source)
    process = subprocess.run([str(binary.resolve()), '--lang', 'petta', str(path.resolve())],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
    path.with_suffix('.log').write_text(process.stdout + process.stderr)
    if process.returncode:
        raise AssertionError((process.returncode, process.stderr[-1200:]))
    return parse_forms(process.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('metagol_core.metta', 'metagol_prime.metta', 'metagol_pruning.metta'):
        check_controls((ROOT / 'lib/ilp' / name).read_text())
    h = lambda name: ['MIL.Hole', name]
    tasks = [
        ('chain-empty-prefix', ['MIL.Chain', h('p'), h('q')], ('A', 'A'),
         [('a', 'c')], [('a', 'bad')], 'MIL.Functional'),
        ('choice-functional', ['MIL.Choice', h('p'), h('q')], ('A', 'A'),
         [('a', 'c')], [('a', 'bad')], 'MIL.Functional'),
        ('choice-relational', ['MIL.Choice', h('p'), h('q')], ('A', 'A'),
         [('a', 'c')], [('a', 'bad')], 'MIL.Relational'),
        ('chain-negative-can-disappear', ['MIL.Chain', h('p'), h('q')], ('A', 'A'),
         [('a', 'c')], [('a', 'b')], 'MIL.Relational'),
        ('shared-hole', ['MIL.Chain', h('p'), h('p')], ('A', 'A'),
         [('a', 'c')], [('a', 'bad')], 'MIL.Functional'),
        ('nested-map', ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', h('p'), h('q')]],
         (('List', 'A'), ('List', 'A')), [(['a', 'a'], ['c', 'c'])], [], 'MIL.Functional'),
        ('closed-prefix', ['MIL.Chain', 'dead', h('q')], ('A', 'A'),
         [('a', 'c')], [], 'MIL.Relational'),
        ('no-positive-examples', ['MIL.Chain', h('p'), h('q')], ('A', 'A'),
         [], [('a', 'bad')], 'MIL.Relational'),
        ('choice-no-positive-examples', ['MIL.Choice', h('p'), h('q')], ('A', 'A'),
         [], [('a', 'bad')], 'MIL.Functional'),
        ('unknown-target-sort', ['MIL.Chain', h('p'), h('q')], ('A', 'Missing'),
         [('a', 'c')], [], 'MIL.Relational'),
        ('identity-root', ['MIL.Id', h('p')], ('A', 'A'),
         [('a', 'c')], [('a', 'bad')], 'MIL.Functional'),
        ('finite-closure', ['MIL.Closure', h('p')], ('A', 'A'),
         [('a', 'c')], [('a', 'bad')], 'MIL.Relational'),
    ]
    tasks = [(*task, DECLARATIONS, EDGES) for task in tasks]
    overloaded = [('p', 'A', 'B'), ('p', 'B', 'C'), ('q', 'B', 'C')]
    typed_edges = [('p', 'a', 'b'), ('p', 'b', 'c'), ('q', 'b', 'c')]
    tasks.extend([
        ('different-intermediate-sort', ['MIL.Chain', h('p'), h('q')], ('A', 'C'),
         [('a', 'c')], [], 'MIL.Functional', overloaded, typed_edges),
        ('shared-hole-cannot-change-sort', ['MIL.Chain', h('p'), h('p')], ('A', 'C'),
         [('a', 'c')], [], 'MIL.Functional', overloaded, typed_edges),
    ])
    pruning = json.dumps(os.path.relpath(
        ROOT / 'lib/ilp/metagol_pruning.metta', args.output.resolve()))
    code = [f'!(import! &self {pruning})',
            '(= (pruning-test:payload $value) '
            '(let $effect (println! PruningLiteralWasExecuted) $value))']
    expected, regions = {}, []
    for number, (name, skeleton, target, positive, negative, mode, declarations, edges) in enumerate(tasks):
        bias = ['MIL.TypeVocabulary', [['MIL.TypeRow', program, ['MIL.Rel', sort(a), sort(b)]]
                                      for program, a, b in declarations]]
        sem = ['MIL.FrozenFacts', [['MIL.Edge', *row] for row in edges]]
        examples = ['MIL.Examples', [['MIL.Ex', *ex] for ex in positive],
                    [['MIL.Ex', *ex] for ex in negative], mode]
        candidates = complete(skeleton, target, declarations)
        pruned = [program for program in candidates if safe_prefix(program, positive, negative, mode, edges)]
        winners = [program for program in candidates if consistent(program, positive, negative, mode, edges)]
        assert [program for program in pruned if consistent(program, positive, negative, mode, edges)] == winners
        base_args = f'{sexpr(bias)} {sexpr(skeleton)} {sexpr(["MIL.Rel", sort(target[0]), sort(target[1])])} 1000'
        guarded_args = f'{sexpr(bias)} {sexpr(sem)} {sexpr(examples)} {sexpr(skeleton)} ' \
                       f'{sexpr(["MIL.Rel", sort(target[0]), sort(target[1])])} 1000'
        baseline = f'(mil:prime:typed-consistent-filled-candidate {sexpr(bias)} {sexpr(sem)} ' \
                   f'{sexpr(examples)} {sexpr(skeleton)} {sexpr(["MIL.Rel", sort(target[0]), sort(target[1])])} 1000)'
        for label, call, values in (
            ('all', f'(mil:prime:typed-filled-candidate {base_args})', candidates),
            ('guarded', f'(mil:prime:pruned-filled-candidate {guarded_args})', pruned),
            ('baseline', baseline, winners),
            ('winners', f'(mil:prime:pruned-consistent-filled-candidate {guarded_args})', winners),
        ):
            tag = f'Pruning{number}_{label}'
            code.append(f'!(println! ({tag} (collapse {call})))')
            expected[tag] = Counter(map(sexpr, values))
        regions.append({'name': name, 'complete_candidates': len(candidates),
                        'after_prefix_guards': len(pruned), 'winner_occurrences': len(winners)})

    boundary = [
        ('(mil:pruning-status (MIL.FrozenFacts ()) (MIL.Examples () () MIL.Relational))', 'MIL.PruningReady'),
        ('(mil:freeze-facts ((MIL.Edge p a b extra)))', ['MIL.PruningRefused', 'Facts']),
        ('(mil:freeze-facts ((MIL.Edge p a (new-space))))', ['MIL.PruningRefused', 'Facts']),
        ('(mil:freeze-facts ((MIL.Edge p a 1.5)))', ['MIL.PruningRefused', 'Facts']),
        ('(mil:freeze-facts ((MIL.Edge p a $variable)))', ['MIL.PruningRefused', 'Facts']),
        ('(mil:freeze-facts NotRows)', ['MIL.PruningRefused', 'Facts']),
        ('(mil:pruning-status (mil:freeze-facts NotRows) (MIL.Examples () () MIL.Relational))',
         ['MIL.PruningRefused', 'Facts']),
        ('(mil:pruning-status (MIL.FrozenFacts ()) (MIL.Examples () () WrongMode))', ['MIL.PruningRefused', 'Mode']),
        ('(mil:pruning-status (MIL.FrozenFacts ()) (MIL.Examples ((MIL.Ex a b extra)) () MIL.Relational))',
         ['MIL.PruningRefused', 'Examples']),
        ('(mil:pruning-status (new-space) (MIL.Examples () () MIL.Relational))', 'MIL.PruningFallback'),
        ('(collapse (mil:run (MIL.FrozenFacts ((MIL.Edge p a b) (MIL.Edge p a b))) p a))', ['b', 'b']),
        ('(collapse (mil:run (MIL.FrozenFacts ((MIL.Edge p a b) (MIL.Edge p b a))) (MIL.Closure p) a))', ['b', 'a']),
        ('''(let $literal (quote (pruning-test:payload a))
              (let $rows (cons-atom (MIL.Edge p a $literal) ())
                (let $sem (mil:freeze-facts $rows)
                  (mil:same-atom (mil:outputs $sem p a) (cons-atom $literal ())))))''', 'true'),
    ]
    for number, (call, value) in enumerate(boundary):
        code.append(f'!(println! (PruningBoundary {number} {call}))')
    forms = execute(args.cetta, '\n'.join(code) + '\n', args.output / 'pruning.metta')
    actual = {form[0]: Counter(map(sexpr, form[1])) for form in forms
              if isinstance(form, list) and len(form) == 2 and form[0] in expected}
    errors = [{'case': tag, 'expected': dict(want), 'actual': dict(actual.get(tag, {}))}
              for tag, want in expected.items() if actual.get(tag) != want]
    if 'PruningLiteralWasExecuted' in forms:
        errors.append({'quoted_fact_payload_was_executed': True})
    actual_boundary = [form for form in forms if isinstance(form, list) and form[:1] == ['PruningBoundary']]
    expected_boundary = [['PruningBoundary', str(i), value] for i, (_, value) in enumerate(boundary)]
    if actual_boundary != expected_boundary:
        errors.append({'boundary': actual_boundary, 'expected_boundary': expected_boundary})

    # Running user code speculatively would duplicate these observable effects.
    fallback = f'''
!(import! &self {pruning})
!(bind! &pruning-exec (new-space))
!(add-atom &pruning-exec (MIL.Primitive trace pruning-test:trace))
!(add-atom &pruning-exec (MIL.Primitive fail pruning-test:fail))
!(add-atom &pruning-exec (: trace (MIL.Rel A A)))
!(add-atom &pruning-exec (: fail (MIL.Rel A A)))
(= (pruning-test:trace $input) (let $logged (println! (PruningEffect $input)) $input))
(= (pruning-test:fail $input) (superpose ()))
!(println! (PruningFallbackStatus (mil:pruning-status &pruning-exec
  (MIL.Examples ((MIL.Ex a a)) () MIL.Functional))))
!(println! (PruningFallbackAnswers (collapse
  (LEARN &pruning-exec &pruning-exec (MIL.Examples ((MIL.Ex a a)) () MIL.Functional)
    (MIL.Chain (MIL.Hole p) (MIL.Hole q)) (MIL.Rel A A) 1000))))
'''
    routes = []
    for name, operation in [('baseline', 'mil:prime:typed-consistent-filled-candidate'),
                            ('pruned', 'mil:prime:pruned-consistent-filled-candidate')]:
        observed = execute(args.cetta, fallback.replace('LEARN', operation), args.output / f'{name}-effects.metta')
        routes.append([form for form in observed if isinstance(form, list) and form
                       and form[0] in ('PruningEffect', 'PruningFallbackStatus', 'PruningFallbackAnswers')])
    if routes[0] != routes[1] or not any(row[0] == 'PruningEffect' for row in routes[0]):
        errors.append({'fallback_routes': routes})
    receipt = {'passed': not errors, 'checks': len(expected) + len(boundary) + 1,
               'errors': errors, 'regions': regions, 'executable_fallback_trace_equal': routes[0] == routes[1],
               'scope': 'root chain/choice failure guards on closed frozen facts; complete right-hand families, duplicates and shared substitutions',
               'not_claimed': ['general Popper constraint learning', 'executable-background purity',
                               'global MIL completeness', 'runtime speedup', 'whole-library cost improvement']}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({**receipt, 'errors': len(errors)}))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
