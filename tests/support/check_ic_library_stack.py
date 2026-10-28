#!/usr/bin/env python3
"""Joint helper libraries: independent bytes, expansion and native resumption."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess
import struct

from check_binary_codec import ROOT, encode as frame, parse_forms, tensor
from check_binary_tree import packed
from check_ic_archive import syntax, uint
from check_ic_context_library import candidate as deferred_candidate, sites as deferred_sites, mentions, split
from check_ic_library import candidate as entered_candidate, sites as entered_sites, clause, is_symbol, lc1, plain, SCALARS
from check_ic_search import check_controls
from check_source_codec import encode_value, reference, text_arg


def encoded(layers, document, version=b'LM1', codec=b'LM1'):
    descriptors = [('node', uint(10), [('leaf', label.encode()), ('leaf', name.encode())])
                   for label, name in layers]
    tree = ('node', uint(11), [('leaf', version), ('node', b'', descriptors),
                             encode_value('Syntax:Document', document)])
    return frame([b'SIC1', codec, packed(tree)])


def malformed_record(document, extra_layer=False):
    fields = [('leaf', b'LC1'), ('leaf', b'ic-learned:first')]
    if extra_layer:
        fields.append(('leaf', b'extra'))
    children = [('leaf', b'LM1'), ('node', b'', [('node', uint(10), fields)]),
                encode_value('Syntax:Document', document)]
    if not extra_layer:
        children.append(('leaf', b'extra'))
    return frame([b'SIC1', b'LM1', packed(('node', uint(11), children))])


def waves(layers, document):
    names = [name for _codec, name in layers]
    if len(set(names)) != len(names) or len(layers) > len(document[3]):
        return None
    bodies = {}
    for name, form in zip(names, document[3]):
        parsed = clause(form)
        if not parsed or not is_symbol(parsed[0][0], name):
            return None
        bodies[name] = parsed[1]
    pending, result = names[:], []
    while pending:
        ready = [name for name in pending if not any(mentions(bodies[name], other) for other in pending)]
        if not ready:
            return None
        result.append(ready)
        pending = [name for name in pending if name not in ready]
    return result


def expand_one(codec, name, document):
    """Expand the stored source definition, not the native template catalogue."""
    result = deepcopy(document)
    definition = result[3].pop(0)
    parsed = clause(definition)
    assert parsed and is_symbol(parsed[0][0], name)
    head, body = parsed
    arity = len(head) - 1
    locals_ = definition[1][arity:]
    count = 0

    def substitute(term, args, ports):
        if term[0] == 'Syntax:Variable':
            index = int(term[1])
            return deepcopy(args[index]) if index < arity else ['Syntax:Variable', str(ports + index - arity)]
        if term[0] == 'Syntax:Expr':
            return ['Syntax:Expr', [substitute(child, args, ports) for child in term[1]]]
        return deepcopy(term)

    def beta(term, args):
        if term[0] == 'Syntax:Variable':
            value = args[int(term[1])]
            assert value[0] == 'Syntax:Variable' or value[:1] == ['Syntax:Literal'] and value[1] in SCALARS
            return deepcopy(value)
        if term[0] == 'Syntax:Expr' and len(term[1]) == 1 and term[1][0][0] == 'Syntax:Variable':
            thunk = args[int(term[1][0][1])]
            assert thunk[0] == 'Syntax:Expr' and len(thunk[1]) == 3
            assert is_symbol(thunk[1][0], '|->') and thunk[1][1] == ['Syntax:Expr', []]
            return deepcopy(thunk[1][2])
        if term[0] == 'Syntax:Expr':
            from check_ic_context_library import variables
            if not variables(term):
                return deepcopy(term)
            operation = split(term)
            assert operation
            return ['Syntax:Expr', [deepcopy(term[1][0])] + [beta(child, args) for child in operation[1]]]
        return deepcopy(term)

    if codec == 'LC1':
        for form in result[3]:
            found = clause(form)
            if not found or found[1][0] != 'Syntax:Expr' or not found[1][1] or not is_symbol(found[1][1][0], name):
                continue
            args = found[1][1][1:]
            assert len(args) == arity
            form[2][1][2] = substitute(body, args, len(found[0])-1)
            form[1].extend(deepcopy(locals_))
            count += 1
        assert count >= 2
    else:
        assert codec == 'LC2' and not locals_

        def walk(term):
            nonlocal count
            if term[0] != 'Syntax:Expr':
                return term
            if term[1] and is_symbol(term[1][0], name):
                assert len(term[1]) == arity + 1
                count += 1
                return beta(body, term[1][1:])
            return ['Syntax:Expr', [walk(child) for child in term[1]]]
        for form in result[3]:
            form[2] = walk(form[2])
        assert count >= 1
    assert not any(mentions(form[2], name) for form in result[3])
    return result


def restore(layers, document):
    assert waves(layers, document) is not None
    for codec, name in layers:
        document = expand_one(codec, name, document)
    return document


def render(document):
    """Independent renderer for the literal sorts used by these fixtures."""
    def term(value, names):
        if value[0] == 'Syntax:Variable':
            return '$' + json.loads(names[int(value[1])][1])
        if value[0] == 'Syntax:Expr':
            return '(' + ' '.join(term(child, names) for child in value[1]) + ')'
        assert value[0] == 'Syntax:Literal'
        kind, data = value[1:]
        if kind == 'String':
            return json.dumps(data.decode(), ensure_ascii=False)
        if kind == 'I64':
            return str(struct.unpack('>q', data)[0])
        assert kind == 'Symbol', kind
        return data.decode()
    return '\n'.join(term(form[2], form[1]) for form in document[3])


def execute(cetta, output, number, original, changed, queries):
    variants = []
    for label, source in [('original', render(original)), ('learned', changed)]:
        commands = '\n'.join(f'!(println! (StackAnswer {i} (collapse {query})))'
                             for i, (query, _answers) in enumerate(queries))
        path = output/f'execute-{number}-{label}.metta'
        path.write_text(source+'\n'+commands+'\n')
        run = subprocess.run([str(cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                             cwd=ROOT, capture_output=True, text=True, timeout=30)
        (output/f'execute-{number}-{label}.log').write_text(run.stdout+run.stderr)
        forms = parse_forms(run.stdout)
        answers = [form for form in forms if isinstance(form, list) and form[:1] == ['StackAnswer']]
        effects = [form for form in forms if isinstance(form, list) and form[:1] == ['StackEffect']]
        event_order = [(['effect', *form[1:]] if form[0] == 'StackEffect' else ['answer', form[1]])
                       for form in forms if isinstance(form, list) and form[:1] in (['StackEffect'], ['StackAnswer'])]
        passed = run.returncode == 0 and not run.stderr and len(answers) == len(queries) and all(
            row[1] == str(i) and Counter(map(json.dumps, row[2])) == Counter(map(json.dumps, expected))
            for i, (row, (_query, expected)) in enumerate(zip(answers, queries)))
        variants.append({'variant': label, 'passed': passed, 'answers': answers,
                         'effects': effects, 'event_order': event_order})
    return {'case': number, 'passed': all(row['passed'] for row in variants)
            and variants[0]['event_order'] == variants[1]['event_order'], 'variants': variants}


HELPERS = r'''
!(import! &self (library lib_ic_library_stack))
!(import! &self (library lib_ic_library_transfer))
(= (stack-test:search-view $expected $original $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let $closed (ic:release $state)
         (StackSearch (== $best $expected) (== (ic:library-unpack $best) (IC:Some $original))
           $reason (> $seen 0) (> $accepted 0) $faults $first)))
      ($bad-search $bad-search))))
(= (stack-test:pieces $expected $original (IC:PreparedSearch $state))
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (StackPaused BudgetExhausted (stack-test:search-view $expected $original (ic:resume $next 2048))))
      ($not-paused $not-paused))))
(= (stack-test:cancel $expected $original (IC:PreparedSearch $state))
   (case (ic:cancel $state)
     (((IC:SearchResult $best $stats Cancelled $fault $next)
       (StackCancelled Cancelled (stack-test:search-view $expected $original (ic:resume $next 2048)))))))
(= (stack-test:forged (IC:PreparedSearch $state))
   (case $state
     (((IC:SearchState (IC:Task (Library:StackModel $mode $layers $prepared $name $region) $original $observation $guards $quantum)
         $queue $best $statistics $fault)
       (let* (($result (ic:resume (IC:SearchState
                   (IC:Task (Library:StackModel $mode () $prepared $name $region) $original $observation $guards $quantum)
                   $queue $best $statistics $fault) 0)) ($closed (ic:release $state))) $result)))))
(= (stack-test:forged-region (IC:PreparedSearch $state))
   (case $state
     (((IC:SearchState (IC:Task (Library:StackModel $mode $layers $prepared $name $region) $original $observation $guards $quantum)
         $queue $best $statistics $fault)
       (let* (($result (ic:resume (IC:SearchState
                   (IC:Task (Library:StackModel $mode $layers $prepared $name (Library:Seeds ())) $original $observation $guards $quantum)
                   $queue $best $statistics $fault) 0)) ($closed (ic:release $state))) $result)))))
(= (stack-test:empty $expected (IC:PreparedSearch $state))
   (case (ic:resume $state 64)
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $next)
       (let $closed (ic:release $next) (StackEmpty (== $expected $best) $seen $accepted $faults $reason $fault))))))
(= (stack-test:counts $expected (IC:PreparedSearch $state))
   (case (ic:resume $state 64)
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $next)
       (let $closed (ic:release $next) (StackCounts (== $expected $best) $seen $accepted $faults $reason $fault))))))
(= (stack-test:changed (IC:PreparedSearch $state))
   (let* (($change (add-atom &self StackDependencyChanged)) ($result (ic:resume $state 0))
          ($closed (ic:release $state))) $result))
(= (stack-test:join $layers $document $candidate $expected)
   (case (library-stack:extend $layers $document $candidate)
     (((Library:Extended $bytes) (== $bytes $expected)) ($failure $failure))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seeded-search', action='store_true',
                        help='use an explicit two-seed region for repeated controller comparisons')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('lib_ic_library.metta', 'lib_ic_library_stack.metta', 'lib_ic_library_transfer.metta'):
        check_controls((ROOT/'lib/petta'/name).read_text())
    code, expected, executable = [HELPERS], [], []

    def add(call, result):
        number = len(expected)
        code.append(f'!(println! (StackStart {number}))')
        code.append(f'!(println! (StackCase {number} {call}))')
        expected.append(['StackCase', str(number), result])

    def add_executable(raw, document, queries):
        number = len(executable)
        original = restore(document[0], document[1])
        executable.append((original, document[1], queries))
        code.append(f'''!(println! (StackExecutable {number} (case (ic:export-library {tensor(raw)})
          (((Library:ExportMany $layers $document $text $original) (tensor:inspect (binary:utf8 $text)))
           ($invalid-export $invalid-export)))))''')

    names = ['ic-learned:first', 'ic-learned:second']
    texts = [
        f'(= ({names[0]} $p0) (+ $p0 1)) (= ({names[1]} $p0) (* $p0 2)) '
        f'(= (fa $x) ({names[0]} $x)) (= (fb $y) ({names[0]} $y)) '
        f'(= (ga $x) ({names[1]} $x)) (= (gb $y) ({names[1]} $y)) '
        '(= (ic-learned:authored $x) (+ $x 99))',
        f'(= ({names[0]} $p0) (+ ({names[1]} 3) $p0)) (= ({names[1]} $p0) (* $p0 2)) '
        f'(= (fa $x) ({names[0]} $x)) (= (fb $y) ({names[0]} $y)) '
        f'(= (ga $x) ({names[1]} $x)) (= (gb $y) ({names[1]} $y))',
    ]
    good = []
    for index, text in enumerate(texts):
        document = reference(text)
        for labels in ([('LC1', names[0]), ('LC1', names[1])],
                       [('LC2', names[0]), ('LC2', names[1])],
                       [('LC2', names[0]), ('LC1', names[1])]):
            try:
                original = restore(labels, document)
            except (AssertionError, IndexError):
                continue
            raw = encoded(labels, document)
            source = syntax(document)
            layers = '('+' '.join(f'(Library:Layer "{label}" "{name}")' for label, name in labels)+')'
            add(f'(== (library-stack:pack {layers} {source}) {tensor(raw)})', 'true')
            add(f'(== (ic:library-unpack {tensor(raw)}) (IC:Some {syntax(original)}))', 'true')
            add(f'''(case (ic:export-library {tensor(raw)})
                   (((Library:ExportMany $layers $doc $text $original)
                     (StackExport (size-atom $layers) (== $doc {source})
                       (== $original {syntax(original)}) (== (syntax:read $text) $doc)))
                    ($bad-export $bad-export)))''', ['StackExport', '2', 'true', 'true', 'true'])
            good.append((labels, document, raw))
            base = 6 if index == 0 else 11
            queries = [('(fa 5)', [str(base)]), ('(fb -2)', ['-1' if index == 0 else '4']),
                       ('(ga 3)', ['6']), ('(gb -2)', ['-4']),
                       ('(ga (superpose (1 2 1)))', ['2', '4', '2'])]
            if index == 0:
                queries.append(('(ic-learned:authored 3)', ['102']))
            add_executable(raw, (labels, document), queries)
    assert len(good) >= 4
    layers, document, raw = good[0]
    for bad_layers, bad_document in [
        (layers[:1], document), ([], document), (layers[::-1], document),
        ([layers[0], layers[0]], document), ([('Unknown', names[0]), layers[1]], document),
    ]:
        add(f'(ic:library-unpack {tensor(encoded(bad_layers, bad_document))})', 'IC:None')
    cyclic = reference(f'(= ({names[0]} $x) ({names[1]} 1)) (= ({names[1]} $x) ({names[0]} 1)) '
                       f'(= (f $x) ({names[0]} $x))')
    assert waves(layers, cyclic) is None
    add(f'(ic:library-unpack {tensor(encoded(layers, cyclic))})', 'IC:None')
    self_cycle = deepcopy(document)
    self_cycle[3][0] = reference(f'(= ({names[0]} $p0) ({names[0]} $p0))')[3][0]
    add(f'(ic:library-unpack {tensor(encoded(layers, self_cycle))})', 'IC:None')
    foreign = deepcopy(document)
    foreign[2][1] = '"he"'
    add(f'(ic:library-unpack {tensor(encoded(layers, foreign))})', 'IC:None')
    add(f'(ic:library-unpack {tensor(encoded(layers, document, version=b"wrong"))})', 'IC:None')
    add(f'(ic:library-unpack {tensor(raw[:-1])})', 'IC:None')
    for bad in (malformed_record(document), malformed_record(document, extra_layer=True)):
        add(f'(ic:library-unpack {tensor(bad)})', 'IC:None')
    add(f'(ic:reuse-library Deferred {tensor(raw)} (syntax:read ""))',
        ['Library:Refused', ['Library:TransferStep', json.dumps(layers[-1][1]), 'InsufficientSupport']])
    add('''(case (library-stack:waves ((Library:Helper "A" (Syntax:Literal Symbol (binary:utf8 "B")))
                         (Library:Helper "B" (Syntax:Literal Symbol (binary:utf8 "C")))
                         (Library:Helper "C" (Syntax:Expr ()))) 3 ())
        (((Library:Waves $waves) (map-atom $waves $wave (map-atom $wave $entry
           (case $entry (((Library:Helper $name $body) $name)))))) ($bad $bad)))''',
        [['"C"'], ['"B"'], ['"A"']])
    add('(library-stack:waves ((Library:Helper "A" (Syntax:Literal Symbol (binary:utf8 "A")))) 0 ())',
        ['Library:Refused', 'DependencyBound'])
    add('(library-stack:waves ((Library:Helper "A" (Syntax:Literal Symbol (binary:utf8 "A")))) 1 ())',
        ['Library:Refused', 'CyclicLibrary'])
    add('''(library-stack:waves ((Library:Helper "A" (Syntax:Expr
       ((Syntax:Literal Symbol (binary:utf8 "quote")) (Syntax:Literal Symbol (binary:utf8 "A")))))) 1 ())''',
        ['Library:Refused', 'CyclicLibrary'])

    effects_text = f'''(= ({names[0]} $p0 $p1) (if (> ($p0) 0) ($p1) (superpose ())))
       (= ({names[1]} $p0) (+ $p0 $p0))
       (= (f $x) ({names[0]} (|-> () ({names[1]} $x)) (|-> () (observe $x))))
       (= (g $x) ({names[0]} (|-> () ({names[1]} $x)) (|-> () (observe $x))))
       (= (observe $x) (let $noted (println! (StackEffect $x)) $x))'''
    effect_layers = [('LC2', names[0]), ('LC2', names[1])]
    effect_document = reference(effects_text)
    effect_original = restore(effect_layers, effect_document)
    effect_bytes = encoded(effect_layers, effect_document)
    add(f'(== (ic:library-unpack {tensor(effect_bytes)}) (IC:Some {syntax(effect_original)}))', 'true')
    add_executable(effect_bytes, (effect_layers, effect_document),
                   [('(f 2)', ['2']), ('(f -1)', []), ('(g 0)', []),
                    ('(g (superpose (2 -1 2)))', ['2', '2'])])

    # Two separate source families, then independently computed exact bytes.
    first_text, second_text = 'alpha-' * 64, 'beta-' * 64
    a = [f'(= (a{i} $x) (if (> $x {i+1}) "{first_text}" 0))' for i in range(4)]
    b = [f'(= (b{i} $x) (if (< $x {i+1}) "{second_text}" 0))' for i in range(4)]
    original = reference('\n'.join(a+b))
    first_doc, _support = entered_candidate(original, names[0], 0, 1)
    first_bytes = lc1(names[0], first_doc)
    original_bytes = plain(original)
    lengths = []
    for mode, label, candidates, sites in (
        ('Entered', 'LC1', entered_candidate, entered_sites),
        ('Deferred', 'LC2', deferred_candidate, deferred_sites),
    ):
        available = sites(first_doc)
        seeds = [i for i, site in enumerate(available)
                 if is_symbol(clause(first_doc[3][site[0]])[0][0], 'b0') or
                    is_symbol(clause(first_doc[3][site[0]])[0][0], 'b1')]
        if mode == 'Deferred':
            seeds = [i for i in seeds if available[i][1] == (2,)]
        assert len(seeds) == 2, (mode, seeds)
        next_doc = candidates(first_doc, names[1], *seeds)[0]
        next_single = lc1(names[1], next_doc) if mode == 'Entered' else __import__('check_ic_context_library').encoded(names[1], next_doc)
        next_layers = [(label, names[1]), ('LC1', names[0])]
        best = encoded(next_layers, next_doc)
        assert restore(next_layers, next_doc) == original
        assert len(best) < len(first_bytes) < len(original_bytes), (mode, len(original_bytes),len(first_bytes),len(best))
        lengths.append({'mode': mode, 'original_bytes': len(original_bytes), 'first_bytes':len(first_bytes),'joint_bytes':len(best)})
        first_layers = f'((Library:Layer "LC1" "{names[0]}"))'
        add(f'(stack-test:join {first_layers} {syntax(first_doc)} {tensor(next_single)} {tensor(best)})', 'true')
        add(f'(== (ic:library-unpack {tensor(best)}) (IC:Some {syntax(original)}))', 'true')
        add(f'''(case (ic:library-check {tensor(original_bytes)} {tensor(first_bytes)} {tensor(best)})
                    (((IC:PackedAccepted $bytes $before $after) (StackSaved $before $after)) ($bad $bad)))''',
            ['StackSaved', str(len(first_bytes)), str(len(best))])
        add(f'(ic:library-check {tensor(original_bytes)} {tensor(first_bytes)} {tensor(next_single)})',
            ['IC:PackedRejected','Reconstruction'])
        region = f' (Library:Seeds (({seeds[0]} {seeds[1]}) ({seeds[0]} {seeds[1]})))' if args.seeded_search else ''
        prepared = f'(ic:continue-library {mode} {tensor(first_bytes)} "second" 64{region})'
        view = f'(stack-test:search-view {tensor(best)} {syntax(original)} '
        search = ['StackSearch','true','true','RegionExhausted','true','true','0','IC:NoFault']
        add(view+f'(case {prepared} (((IC:PreparedSearch $state) (ic:resume $state 2048)) ($bad $bad))))', search)
        add(f'(stack-test:pieces {tensor(best)} {syntax(original)} {prepared})', ['StackPaused','BudgetExhausted',search])
        add(f'(stack-test:cancel {tensor(best)} {syntax(original)} {prepared})', ['StackCancelled','Cancelled',search])
        add(f'(stack-test:forged {prepared})', ['IC:ResumeRejected','InvalidArtifact'])
        add(f'(stack-test:forged-region {prepared})', ['IC:ResumeRejected','InvalidArtifact'])
        add(f'(stack-test:empty {tensor(first_bytes)} (ic:continue-library {mode} {tensor(first_bytes)} "second" 64 (Library:Seeds ())))',
            ['StackEmpty', 'true', '0', '0', '0', 'RegionExhausted', 'IC:NoFault'])
        add(f'(stack-test:counts {tensor(best)} (ic:continue-library {mode} {tensor(first_bytes)} "second" 64 '
            f'(Library:Seeds (({seeds[0]} {seeds[1]}) ({seeds[0]} {seeds[1]})))))',
            ['StackCounts', 'true', '2', '1', '0', 'RegionExhausted', 'IC:NoFault'])
        add(f'(ic:continue-library {mode} {tensor(first_bytes)} "second" 64 (Library:Seeds ((999 0))))',
            ['IC:SearchRefused','InvalidLibraryRegion'])
        add(f'(ic:continue-library {mode} {tensor(first_bytes)} "second" 64 (Library:Seeds ((0 -1))))',
            ['IC:SearchRefused','InvalidLibraryRegion'])
        add_executable(best, (next_layers, next_doc),
                       [('(a0 3)', [json.dumps(first_text)]), ('(a1 -1)', ['0']),
                        ('(b0 0)', [json.dumps(second_text)]), ('(b1 5)', ['0']),
                        ('(a0 (superpose (3 0 3)))', [json.dumps(first_text), '0', json.dumps(first_text)])])
    add(f'(ic:continue-library Deferred {tensor(first_bytes)} "first" 64)', ['IC:SearchRefused','InvalidLibraryName'])
    add(f'(ic:continue-library Deferred {tensor(first_bytes)} "second" 0)', ['IC:SearchRefused','InvalidQuantum'])
    add(f'(stack-test:changed (ic:continue-library Deferred {tensor(first_bytes)} "second" 64))',
        ['IC:ResumeRejected','ChangedDependencies'])
    program = '\n'.join(code)+'\n'
    check_controls(program)
    path = args.output/'stack.metta'
    path.write_text(program)
    try:
        process = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                                 cwd=ROOT, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired as error:
        def captured(value):
            return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
        stdout, stderr = captured(error.stdout), captured(error.stderr)
        (args.output/'stdout.log').write_text(stdout)
        (args.output/'stderr.log').write_text(stderr)
        markers = [form for form in parse_forms(stdout) if isinstance(form, list) and form[:1] == ['StackStart']]
        receipt = {'passed': False, 'reason': 'timeout', 'last_started_case': markers[-1] if markers else None}
        (args.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(json.dumps(receipt))
        return 1
    (args.output/'stdout.log').write_text(process.stdout)
    (args.output/'stderr.log').write_text(process.stderr)
    actual = [form for form in parse_forms(process.stdout) if isinstance(form,list) and form[:1] == ['StackCase']]
    errors = [{'case':i,'expected':want,'actual':got} for i,(want,got) in enumerate(itertools.zip_longest(expected,actual)) if want != got]
    texts = {int(row[1]): row[2] for row in parse_forms(process.stdout)
             if isinstance(row, list) and row[:1] == ['StackExecutable']}
    executions = []
    for number, (original, changed, queries) in enumerate(executable):
        value = texts.get(number)
        if not isinstance(value, list) or value[:2] != ['TensorData', 'u8']:
            errors.append({'export': number, 'actual': value})
            continue
        source = bytes(int(row[1]) for row in value[3]).decode()
        if reference(source) != changed:
            errors.append({'export': number, 'reason': 'rendered source changed'})
            continue
        executions.append(execute(args.cetta, args.output, number, original, source, queries))
    receipt = {'passed':process.returncode==0 and not errors and len(texts) == len(executable)
                        and len(executions) == len(executable) and all(row['passed'] for row in executions),
               'checks':len(expected),'observations':len(actual), 'executions': executions,
               'repeated_search_region': 'two-declared-seeds' if args.seeded_search else 'all-pairs',
               'errors':errors,'costs':lengths,'scope':'joint source bytes, explicit decoder layers, graph admission, both source profiles, common resumed search and fresh whole-program execution'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({**receipt, 'errors':len(errors), 'executions':len(executions)}))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
