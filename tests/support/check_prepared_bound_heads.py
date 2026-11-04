#!/usr/bin/env python3
"""Prepared machines against the evaluator, where data and calls are decided.

An application whose head is a variable, or an expression computing the head,
is a call or data according to the head's value: a call when the value names
a function, is a function applied in part or is a closure; data when it is a
number, a string, a symbol with no definition or an expression (an expression
naming a function among them).  A held argument is data too.  Every machine
that runs such an application (the prepared pure machine's inline, coded,
frame and answer-step executors, its classifiers and templates, the compiled
equation tier, the search machine) must decide it as the evaluator does.  A
machine that admits a node another one tests at run time answers data for a
call, or a call for data.

PeTTa programs are the product of these axes, every combination of which runs:
  * the head's value: a function, a builtin, a partial application, a
    closure, a closure over a captured value, a closure applied in part, a
    number, a float, a string, a symbol with no definition, an expression,
    and an expression naming a function;
  * the arity of the lambda that applies it, one to four parameters, where
    a closure's captured variables count as parameters;
  * the context: the top level, a lambda made inside an equation, a closure
    made in one equation and applied in another, nested closures, and an
    equation body without a lambda;
  * the form around the application: none, let, a let that rebinds the
    head, let*, case, collapse, superpose, if, a constructor, an identity
    call, a head computed by a call, and the head applied twice;
  * heads written in the source rather than bound, and natives (map-atom,
    foldl-atom, filter-atom) applying the head's value.
HE programs vary the same head values over equations of one to four
parameters and the forms HE has (chain among them), and a library native's
answer is read back after a rule for one of its parts appears, which shows
the marks each route gave it (as tests/support/check_he_value_marks.py
does).  Prime programs pass held (Atom) arguments to bodies that return
them, wrap them or bind them.

Laws checked:
  * every route answers each program as the lane's reference route, the
    prepared machines (and the compiled tier) off, answers it;
  * PeTTa's reference answers equal SWI-PeTTa stable-fixed's, recorded in
    prepared_bound_heads_expected.json (--record PETTA_ROOT writes it),
    up to variable spelling and lambda numbering, except where an entry of
    SWI_DECIDED says CeTTa decides otherwise and why;
  * a Prime program of held arguments answers as CeTTa's HE lane does,
    which answers as Rust HE and SWI-PeTTa do.
A divergence is shrunk to the definitions it needs.  --oracle PETTA_ROOT
compares with SWI-PeTTa live instead of the recorded answers.
"""

import argparse
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

EXPECTED = Path(__file__).with_name('prepared_bound_heads_expected.json')

# Where CeTTa answers otherwise than SWI-PeTTa stable-fixed, by case name.
# A divergence no entry covers is a failure, and --record refuses to write.
SWI_DECIDED = [
    (re.compile(r'^written\.(top|body\.p\d|lambda\.l\d)\.quoted\.'),
     'unsettled, recorded 2026-11-04 for Zar: SWI-PeTTa folds (quote X) '
     'while translating and then reads the folded X as written source, so '
     'a call inside it runs ((partial bh-addk (7)) 3); its own let-bound form '
     '(let $f (quote X) ($f 3)) answers data, as CeTTa answers both'),
]

PETTA_PRELUDE = """\
(= (bh-add7 $x) (+ $x 7))
(= (bh-addk $k $x) (+ $x $k))
(= (bh-id $x) $x)
(= (bh-pick $f) $f)
(= (bh-big $x) (> $x 3))
(= (apply1 $l $a) ($l $a))
(= (apply2 $l $a $b) ($l $a $b))
(= (apply3 $l $a $b $c) ($l $a $b $c))
(= (apply4 $l $a $b $c $d) ($l $a $b $c $d))
"""

# (name, source of the head's value, arguments it takes).  A head taking two
# arguments is applied to the argument and 1.
PETTA_HEADS = [
    ('fun', 'bh-add7', 1),
    ('builtin', '+', 2),
    ('partial', '(bh-addk 7)', 1),
    ('closure', '(|-> ($y) (* $y 2))', 1),
    ('capture', '(let $k 4 (|-> ($y) (+ $y $k)))', 1),
    ('lambda-partial', '((|-> ($a $b) (+ $a $b)) 7)', 1),
    ('number', '5', 1),
    ('float', '2.5', 1),
    ('string', '"s"', 1),
    ('nodef', 'nodef', 1),
    ('expression', '(quote (a b))', 1),
    ('expression-fun', '(quote (bh-addk 7))', 1),
]

# Forms around the application of head {f} to argument {x}: {app} is the
# application, {app_g} the same with the head rebound to $g, {app_pick} with
# the head computed by a call, {app_twice} the head applied to its own answer.
PETTA_FORMS = [
    ('plain', '{app}'),
    ('let', '(let $r {app} $r)'),
    ('let-head', '(let $g {f} {app_g})'),
    ('let-star', '(let* (($g {f}) ($r {app_g})) $r)'),
    ('case', '(case {app} (($r $r)))'),
    ('collapse', '(collapse {app})'),
    ('superpose', '(superpose ({app} 0))'),
    ('if', '(if True {app} none)'),
    ('ctor', '(Box {app})'),
    ('ctor-pair', '(Pair {app} {x})'),
    ('id', '(bh-id {app})'),
    ('computed-head', '{app_pick}'),
    ('twice', '{app_twice}'),
]

# Heads written in the source rather than bound: literals, a call computing
# the head, a quoted expression, a lambda.  {x} is the argument.
PETTA_WRITTEN_HEADS = [
    ('number', '(5 {x})'),
    ('float', '(2.5 {x})'),
    ('string', '("s" {x})'),
    ('integer-call', '((+ 1 2) {x})'),
    ('partial-call', '((bh-addk 7) {x})'),
    ('function-call', '((bh-pick bh-add7) {x})'),
    ('data-call', '((bh-pick 5) {x})'),
    ('quoted', '((quote (bh-addk 7)) {x})'),
    ('quoted-data', '((quote (a b)) {x})'),
    ('lambda', '((|-> ($y) (* $y 2)) {x})'),
    ('nodef', '(nodef {x})'),
    ('true', '(True {x})'),
]

# A native applying the head's value to the elements of a list, with the
# heads it is given.
PETTA_CALLBACKS = [
    ('map', '(map-atom (3 4) {f})', ('fun', 'partial', 'closure', 'capture',
                                     'lambda-partial', 'number', 'string', 'nodef',
                                     'expression', 'expression-fun')),
    ('fold', '(foldl-atom (3 4) 0 {f})', ('builtin',)),
    ('filter', '(filter-atom (3 4 5) {f})', ('predicate', 'predicate-closure',
                                            'number', 'nodef')),
]
PETTA_PREDICATES = [
    ('predicate', 'bh-big'),
    ('predicate-closure', '(|-> ($y) (> $y 3))'),
]

HE_PRELUDE = """\
(= (bh-add7 $x) (+ $x 7))
(= (bh-id $x) $x)
(= (bh-pick $f) $f)
"""

HE_HEADS = [
    ('fun', 'bh-add7', 1),
    ('builtin', '+', 2),
    ('number', '5', 1),
    ('string', '"s"', 1),
    ('nodef', 'nodef', 1),
    ('expression', '(a b)', 1),
]

HE_FORMS = [
    ('plain', '{app}'),
    ('let', '(let $r {app} $r)'),
    ('let-head', '(let $g {f} {app_g})'),
    ('let-star', '(let* (($g {f}) ($r {app_g})) $r)'),
    ('case', '(case {app} (($r $r)))'),
    ('collapse', '(collapse {app})'),
    ('superpose', '(superpose ({app} 0))'),
    ('if', '(if True {app} none)'),
    ('ctor', '(Box {app})'),
    ('id', '(bh-id {app})'),
    ('chain', '(chain {app} $r $r)'),
    ('computed-head', '{app_pick}'),
    ('twice', '{app_twice}'),
]

# A library native's answer (json's parser declares its range Atom), read
# again after a rule for one of its parts appears: NUM is the case's own
# number, so no case's rule reaches another's value.
HE_NATIVE_PRELUDE = """\
!(import! &self json)
(= (bh-pass $x) $x)
"""
HE_NATIVE_WRAPS = [
    ('direct', '(= (CASE-f) (__cetta_lib_json_parse_v1 "[NUM, 2]"))'),
    ('parameter', '(= (CASE-p $s) (__cetta_lib_json_parse_v1 $s))\n'
                  '(= (CASE-f) (CASE-p "[NUM, 2]"))'),
    ('argument', '(= (CASE-f) (bh-pass (__cetta_lib_json_parse_v1 "[NUM, 2]")))'),
    ('let', '(= (CASE-f) (let $v (__cetta_lib_json_parse_v1 "[NUM, 2]") $v))'),
    ('ctor', '(= (CASE-f) (let $v (__cetta_lib_json_parse_v1 "[NUM, 2]") (Box $v)))'),
]
HE_NATIVE_RULE = '(add-atom &self (= (JsonNumberV1 "NUM") rewritten))'
HE_NATIVE_OBSERVERS = [
    ('top', f'(let $x (CASE-f) (let $_ {HE_NATIVE_RULE} $x))'),
    ('kept', f'(let $x (CASE-f) (let $_ {HE_NATIVE_RULE} (Keep $x)))'),
    ('again', f'(let $_ {HE_NATIVE_RULE} (CASE-f))'),
]

# Held arguments: a body returns a parameter declared Atom, wraps it, binds
# it, or returns it under another range.  (f a) reduces, so a held (f a)
# that is evaluated answers 5.  Under an Atom range the body comes back as
# written in both reference dialects, so those rows answer as the HE lane;
# under the others Prime evaluates the body without demanding the fields of
# a constructor, which HE evaluates, and only the routes are compared.
PRIME_PRELUDE = """\
(= (f a) 5)
(= (bh-idu $y) $y)
"""
PRIME_SIGNATURES = [
    ('bare', '(-> Atom Atom)', '$x'),
    ('second', '(-> Atom Atom Atom)', '$y'),
    ('first', '(-> Atom Atom Atom)', '$x'),
    ('wrapped', '(-> Atom Atom)', '(wrap $x)'),
    ('called', '(-> Atom Atom)', '(h $x)'),
    ('bound', '(-> Atom Atom)', '(let $z $x $z)'),
    ('if', '(-> Atom Atom)', '(if True $x none)'),
    ('identity', '(-> Atom Atom)', '(bh-idu $x)'),
    ('undefined-range', '(-> Atom %Undefined%)', '$x'),
    ('expression-range', '(-> Atom Expression)', '$x'),
]
PRIME_ARGUMENTS = [
    ('arith', '(+ 1 2)'),
    ('call', '(f a)'),
    ('wrapped-call', '(wrap (f a))'),
    ('number', '5'),
    ('nodef', 'nodef'),
    ('quoted', '(quote (f a))'),
]

PETTA_ROUTES = [
    ('reference', {'CETTA_PREPARED_PURE_REFERENCE': '1',
                   'CETTA_OPEN_EQUATIONS_REFERENCE': '1'}),
    ('default', {}),
    ('prepared-off', {'CETTA_PREPARED_PURE_REFERENCE': '1'}),
    ('tier-off', {'CETTA_OPEN_EQUATIONS_REFERENCE': '1'}),
    ('node-executor', {'CETTA_PREPARED_PURE_CODE_REFERENCE': '1'}),
    ('scalar-tree-off', {'CETTA_PREPARED_PURE_SCALAR_TREE_REFERENCE': '1'}),
]
HE_ROUTES = [
    ('reference', {'CETTA_PREPARED_PURE_REFERENCE': '1',
                   'CETTA_HE_SHARED_EXECUTION': '0'}),
    ('prepared', {'CETTA_HE_SHARED_EXECUTION': '0'}),
    ('shared', {}),
]
HE_PROFILES = ['extended', 'he-compat']
PRIME_ROUTES = [
    ('reference', {'CETTA_PREPARED_PURE_REFERENCE': '1'}),
    ('default', {}),
]


def application(head, argument, arity):
    extra = ' 1' if arity == 2 else ''
    return f'({head} {argument}{extra})'


def body_form(form, arity, argument):
    """FORM around the application of the head $f to ARGUMENT."""
    return form.format(
        app=application('$f', argument, arity), f='$f', x=argument,
        app_g=application('$g', argument, arity),
        app_pick=application('(bh-pick $f)', argument, arity),
        app_twice=application('$f', application('$f', argument, arity), arity))


def parameters(params):
    """The head, its argument (from two parameters on) and unused ones."""
    names = ['$f'] + (['$x'] if params > 1 else []) + \
        ['$u', '$w'][:max(0, params - 2)]
    argument = '3' if params == 1 else '$x'
    return names, argument


def petta_cases():
    """(name, definitions, query) for every combination of the axes."""
    for (head_name, head, arity), (form_name, form) in itertools.product(
            PETTA_HEADS, PETTA_FORMS):
        tag = f'{head_name}.{form_name}'
        # Top level: a lambda of one to four parameters applies the head.
        for params in range(1, 5):
            names, argument = parameters(params)
            lam = f'(|-> ({" ".join(names)}) {body_form(form, arity, argument)})'
            args = [head] + (['3'] if params > 1 else []) + ['0'] * max(0, params - 2)
            yield (f'top.l{params}.{tag}', '',
                   f'(apply{params} {lam} {" ".join(args)})')
        # A lambda made inside an equation, the head one of its parameters.
        for params in range(1, 5):
            names, argument = parameters(params)
            lam = f'(|-> ({" ".join(names)}) {body_form(form, arity, argument)})'
            outer = ['$g'] + (['$y'] if params > 1 else [])
            inner = ['$g'] + (['$y'] if params > 1 else []) + ['0'] * max(0, params - 2)
            yield (f'eq.l{params}.{tag}',
                   f'(= (CASE {" ".join(outer)}) (apply{params} {lam} {" ".join(inner)}))',
                   f'(CASE {head}' + (' 3)' if params > 1 else ')'))
        # A closure made in one equation, over the head (and the argument),
        # applied in another: its captured variables are parameters too.
        for own in range(1, 3):
            names = ['$i', '$j'][:own]
            yield (f'made.both.c{own + 2}.{tag}',
                   f'(= (CASE-mk $f $x) (|-> ({" ".join(names)}) {body_form(form, arity, "$x")}))\n'
                   f'(= (CASE $f $x) (apply{own} (CASE-mk $f $x) {" ".join(["done"] * own)}))',
                   f'(CASE {head} 3)')
            yield (f'made.head.c{own + 1}.{tag}',
                   f'(= (CASE-mk $f) (|-> ($x{"".join(" " + n for n in names[1:])}) {body_form(form, arity, "$x")}))\n'
                   f'(= (CASE $f $x) (apply{own} (CASE-mk $f) $x{" done" * (own - 1)}))',
                   f'(CASE {head} 3)')
        # Nested closures: the outer one's parameter is captured by the inner.
        for outer_params in range(1, 3):
            outer = ['$f', '$u'][:outer_params]
            outer_args = [head] + ['0'] * (outer_params - 1)
            inner = f'(|-> ($x) {body_form(form, arity, "$x")})'
            yield (f'nested.o{outer_params}.{tag}', '',
                   f'(apply1 (apply{outer_params} (|-> ({" ".join(outer)}) {inner}) '
                   f'{" ".join(outer_args)}) 3)')
        # An equation body applying the head, one to four parameters.
        for params in range(1, 5):
            names, argument = parameters(params)
            args = [head] + (['3'] if params > 1 else []) + ['0'] * max(0, params - 2)
            yield (f'body.p{params}.{tag}',
                   f'(= (CASE {" ".join(names)}) {body_form(form, arity, argument)})',
                   f'(CASE {" ".join(args)})')


def petta_written_cases():
    """Heads written in the source, in each form, at the top level, in an
    equation body and in a lambda body of one to four parameters."""
    for (head_name, head), (form_name, form) in itertools.product(
            PETTA_WRITTEN_HEADS, PETTA_FORMS):
        if form_name in ('let-head', 'let-star', 'computed-head', 'twice'):
            continue
        tag = f'{head_name}.{form_name}'

        def body(argument):
            return form.format(app=head.format(x=argument), f='', x=argument,
                               app_g='', app_pick='', app_twice='')
        yield (f'written.top.{tag}', '', body('3'))
        for params in range(1, 5):
            names = ['$x'] + ['$u', '$w', '$v'][:params - 1]
            args = ['3'] + ['0'] * (params - 1)
            yield (f'written.body.p{params}.{tag}',
                   f'(= (CASE {" ".join(names)}) {body("$x")})',
                   f'(CASE {" ".join(args)})')
            yield (f'written.lambda.l{params}.{tag}', '',
                   f'(apply{params} (|-> ({" ".join(names)}) {body("$x")}) {" ".join(args)})')


def petta_callback_cases():
    """A native applies the head's value: directly, through an equation's
    parameter, and through a closure's captured variable."""
    heads = {name: source for name, source, _ in PETTA_HEADS}
    heads.update(dict(PETTA_PREDICATES))
    for name, form, users in PETTA_CALLBACKS:
        items = '(3 4 5)' if name == 'filter' else '(3 4)'
        for head_name in users:
            head = heads[head_name]
            tag = f'{name}.{head_name}'
            yield (f'callback.top.{tag}', '', form.format(f=head))
            yield (f'callback.eq.{tag}', f'(= (CASE $f) {form.format(f="$f")})',
                   f'(CASE {head})')
            yield (f'callback.captured.{tag}',
                   f'(= (CASE-mk $f) (|-> ($l) {form.format(f="$f").replace(items, "$l")}))\n'
                   f'(= (CASE $f) (apply1 (CASE-mk $f) {items}))',
                   f'(CASE {head})')


def he_cases():
    for (head_name, head, arity), (form_name, form) in itertools.product(
            HE_HEADS, HE_FORMS):
        tag = f'{head_name}.{form_name}'
        for params in range(1, 5):
            names, argument = parameters(params)
            args = [head] + (['3'] if params > 1 else []) + ['0'] * max(0, params - 2)
            body = body_form(form, arity, argument)
            yield (f'body.p{params}.{tag}',
                   f'(= (CASE {" ".join(names)}) {body})',
                   f'(CASE {" ".join(args)})')
            yield (f'outer.p{params}.{tag}',
                   f'(= (CASE-in {" ".join(names)}) {body})\n'
                   f'(= (CASE {" ".join(names)}) (CASE-in {" ".join(names)}))',
                   f'(CASE {" ".join(args)})')


def he_native_cases():
    for (wrap, definitions), (observer, read) in itertools.product(
            HE_NATIVE_WRAPS, HE_NATIVE_OBSERVERS):
        yield (f'native.{wrap}.{observer}',
               f'{definitions}\n(= (CASE-g) {read})', '(CASE-g)')


def prime_cases():
    for (shape, signature, body), (argument_name, argument) in itertools.product(
            PRIME_SIGNATURES, PRIME_ARGUMENTS):
        if not signature.endswith(' Atom)'):
            shape = f'{shape}.routes'
        two = shape in ('second', 'first')
        params = '$x $y' if two else '$x'
        args = f'0 {argument}' if shape == 'second' else \
            f'{argument} 0' if two else argument
        yield (f'held.{shape}.{argument_name}',
               f'(: CASE {signature})\n(= (CASE {params}) {body})',
               f'(CASE {args})')


def named(case, index):
    """The case's definitions and query under names of its own."""
    name, definitions, query = case
    unique = f'case{index}'
    number = str(1000 + index)
    return (name, definitions.replace('CASE', unique).replace('NUM', number),
            query.replace('CASE', unique).replace('NUM', number))


def program(prelude, cases):
    lines = [prelude]
    for index, (name, definitions, query) in cases:
        if definitions:
            lines.append(definitions)
        lines.append(f'!(MARK {index})')
        lines.append(f'!{query}')
    return '\n'.join(lines) + '\n'


MARK = re.compile(r'^\[?\(MARK (\d+)\)\]?$')
JANUS = ('ERROR: /usr/local/lib/swipl/library/ext/swipy/janus.pl',
         'ERROR:    /usr/local/lib/swipl/library/ext/swipy/janus.pl',
         'ERROR:    open_shared_object/3: libpython')


def split(text):
    """Answers printed after each marker, by case index."""
    table = {}
    current = None
    for line in text.splitlines():
        line = line.rstrip()
        if line.startswith(JANUS):
            continue
        match = MARK.match(line)
        if match:
            current = int(match.group(1))
            table[current] = []
        elif current is not None and line:
            table[current].append(line)
    return table


VARIABLE = re.compile(r'\$[A-Za-z_][A-Za-z0-9_#]*')
LAMBDA = re.compile(r'\blambda_[0-9]+\b')


def normal(lines):
    return None if lines is None else \
        [LAMBDA.sub('lambda_N', VARIABLE.sub('$_', line)) for line in lines]


def run(command, path, env_extra, timeout):
    """COMMAND on PATH.  SWI-PeTTa's run.sh runs from its own directory and
    takes the program before --silent."""
    env = dict(os.environ)
    env.update(env_extra)
    if command[0].endswith('run.sh'):
        argv = ['sh'] + command + [str(path), '--silent']
        cwd = Path(command[0]).parent
    else:
        argv, cwd = command + [str(path)], path.parent
    try:
        process = subprocess.run(argv, cwd=cwd, capture_output=True,
                                 text=True, timeout=timeout, env=env)
        return process.stdout + process.stderr
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'


class Lane:
    def __init__(self, name, prelude, cases, commands, routes, batch=60):
        self.name = name
        self.prelude = prelude
        self.cases = [named(case, index) for index, case in enumerate(cases)]
        # (profile, argv): each profile runs on every route.
        self.commands = commands
        self.routes = routes
        self.batch = batch


def answers_of(lane, command, env, root, indices, timeout, label):
    path = root / f'{lane.name}-{label}.metta'
    path.write_text(program(lane.prelude,
                            [(i, lane.cases[i]) for i in indices]))
    return split(run(command, path, env, timeout))


def evaluate(lane, root, indices, jobs, timeout, extra=()):
    """Per case, each run's normalized answers (None: the run stopped before
    the case).  EXTRA names further runs: (key, argv, env)."""
    batches = [indices[i:i + lane.batch]
               for i in range(0, len(indices), lane.batch)]
    work = []
    for b, chunk in enumerate(batches):
        for profile, command in lane.commands:
            for route, env in lane.routes:
                work.append((f'{profile}:{route}', command, env, chunk,
                             f'{b}-{profile}-{route}'))
        for key, command, env in extra:
            work.append((key, command, env, chunk, f'{b}-{key}'))

    def task(item):
        key, command, env, chunk, label = item
        table = answers_of(lane, command, env, root, chunk, timeout, label)
        # A run that stopped early (an uncaught error) leaves later cases
        # without a marker: each of those runs alone.
        for i in [i for i in chunk if i not in table]:
            table[i] = answers_of(lane, command, env, root, [i], timeout,
                                  f'{label}-{i}').get(i)
        return key, table

    results = {}
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for key, table in pool.map(task, work):
            for index in table:
                results.setdefault(index, {})[key] = normal(table[index])
    return results


def same(left, right):
    """Equal answers, or the same answers in another order."""
    if left == right:
        return 'equal'
    if left is not None and right is not None and Counter(left) == Counter(right):
        return 'order'
    return None


def decided(name):
    for pattern, reason in SWI_DECIDED:
        if pattern.search(name):
            return reason
    return None


def compare_routes(lane, results, indices):
    """Divergences from the reference route; a run that never reached a case
    is one too."""
    failures, order_only = [], 0
    for index in indices:
        row = results.get(index, {})
        for profile, _ in lane.commands:
            reference = row.get(f'{profile}:reference')
            if reference is None:
                failures.append((index, f'{profile}:reference', None, 'answers'))
                continue
            for route, _ in lane.routes[1:]:
                got = row.get(f'{profile}:{route}')
                verdict = same(got, reference)
                if verdict == 'order':
                    order_only += 1
                elif verdict is None:
                    failures.append((index, f'{profile}:{route}', got, reference))
    return failures, order_only


def shrink(lane, root, index, key, oracle_key, timeout, runs):
    """The case with each of its definitions dropped while it diverges."""
    name, definitions, query = lane.cases[index]
    parts = [d for d in definitions.split('\n') if d]

    def answers(command, env, defs, label):
        probe = Lane(lane.name, lane.prelude, [], lane.commands, lane.routes)
        probe.cases = [(name, '\n'.join(defs), query)]
        return normal(answers_of(probe, command, env, root, [0], timeout,
                                 label).get(0))

    def diverges(defs):
        left = answers(*runs[oracle_key], defs, 'shrink-left')
        right = answers(*runs[key], defs, 'shrink-right')
        return same(left, right) is None

    changed = True
    while changed:
        changed = False
        for i in range(len(parts)):
            trial = parts[:i] + parts[i + 1:]
            if diverges(trial):
                parts = trial
                changed = True
                break
    return ' '.join(parts), query


def report(lane, root, failures, runs, timeout):
    shrunk = set()
    for index, key, got, want in failures:
        name = lane.cases[index][0]
        if key == 'swi-petta':
            print(f'FAIL: {lane.name} {name}: CeTTa answers {got} but SWI-PeTTa {want}')
            left, right = 'petta:reference', 'swi-petta'
        elif key == 'he-lane':
            print(f'FAIL: {lane.name} {name}: Prime answers {got} but the HE lane {want}')
            left, right = 'prime:reference', 'he-lane'
        else:
            print(f'FAIL: {lane.name} {name} on {key}: {got} but the reference answers {want}')
            left, right = key.split(':')[0] + ':reference', key
        if (name, key) in shrunk or len(shrunk) >= 12 or \
                left not in runs or right not in runs:
            continue
        shrunk.add((name, key))
        definitions, query = shrink(lane, root, index, right, left,
                                    timeout, runs)
        print(f'  shrunk: {definitions} !{query}')


def check_swi(lane, results, indices, swi, expected, record):
    """PeTTa's reference answers against SWI-PeTTa's, live or recorded.
    Returns the failures, the number decided otherwise and the record."""
    failures, decided_count, entries = [], 0, {}
    for index in indices:
        name = lane.cases[index][0]
        mine = results.get(index, {}).get('petta:reference')
        if swi:
            theirs = results.get(index, {}).get('swi-petta')
        else:
            entry = expected.get(name)
            if entry is None:
                failures.append((index, 'swi-petta', mine, 'no recorded answers'))
                continue
            theirs = entry['swi']
            if 'cetta' in entry:
                decided_count += 1
                if same(mine, entry['cetta']) is None:
                    failures.append((index, 'petta:reference', mine, entry['cetta']))
                continue
        if mine is not None and same(mine, theirs) is not None:
            entries[name] = {'swi': theirs}
            continue
        reason = decided(name) if mine is not None else None
        if reason:
            decided_count += 1
            entries[name] = {'swi': theirs, 'cetta': mine, 'why': reason}
        else:
            failures.append((index, 'swi-petta', mine, theirs))
    if record:
        if failures:
            print('FAIL: not recorded: each divergence needs a fix or an entry in SWI_DECIDED')
        else:
            EXPECTED.write_text(json.dumps(entries, indent=0, sort_keys=True) + '\n')
            print(f'wrote {EXPECTED.name}: {len(entries)} programs, '
                  f'{decided_count} decided otherwise than SWI-PeTTa')
    return failures, decided_count


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('binary')
    parser.add_argument('--oracle', metavar='PETTA_ROOT',
                        help='compare PeTTa with this SWI-PeTTa checkout live')
    parser.add_argument('--record', metavar='PETTA_ROOT',
                        help="write SWI-PeTTa's answers to the expected file")
    parser.add_argument('--lanes', default='petta,he,he-native,prime')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--filter', default='',
                        help='run only cases whose name contains this text')
    parser.add_argument('--dump', metavar='DIR',
                        help="write each lane's corpus as one program here")
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    swi_root = args.record or args.oracle
    swi = [str(Path(swi_root).resolve() / 'run.sh')] if swi_root else None
    he = [binary, '--lang', 'he', '--profile', 'extended']

    wanted = args.lanes.split(',')
    lanes = []
    if 'petta' in wanted:
        lanes.append(Lane('petta', PETTA_PRELUDE,
                          list(petta_cases()) + list(petta_written_cases()) +
                          list(petta_callback_cases()),
                          [('petta', [binary, '--lang', 'petta'])], PETTA_ROUTES))
    if 'he' in wanted:
        lanes.append(Lane('he', HE_PRELUDE, list(he_cases()),
                          [(p, [binary, '--lang', 'he', '--profile', p])
                           for p in HE_PROFILES], HE_ROUTES))
    if 'he-native' in wanted:
        lanes.append(Lane('he-native', HE_NATIVE_PRELUDE, list(he_native_cases()),
                          [(p, [binary, '--lang', 'he', '--profile', p])
                           for p in HE_PROFILES], HE_ROUTES))
    if 'prime' in wanted:
        lanes.append(Lane('prime', PRIME_PRELUDE, list(prime_cases()),
                          [('prime', [binary, '--lang', 'prime'])], PRIME_ROUTES))

    if args.dump:
        out = Path(args.dump)
        out.mkdir(parents=True, exist_ok=True)
        for lane in lanes:
            (out / f'{lane.name}.metta').write_text(
                program(lane.prelude, list(enumerate(lane.cases))))
        return 0

    expected = None
    if not swi and 'petta' in wanted and not args.filter:
        expected = json.loads(EXPECTED.read_text())

    status = 0
    with tempfile.TemporaryDirectory(prefix='prepared-bound-heads-') as scratch:
        root = Path(scratch)
        for lane in lanes:
            indices = [i for i, case in enumerate(lane.cases)
                       if args.filter in case[0]]
            extra = []
            if lane.name == 'petta' and swi:
                extra.append(('swi-petta', swi, {}))
            if lane.name == 'prime':
                extra.append(('he-lane', he, dict(HE_ROUTES)['reference']))
            results = evaluate(lane, root, indices, args.jobs, args.timeout,
                               extra)
            failures, order_only = compare_routes(lane, results, indices)
            runs = {f'{p}:{r}': (c, e) for p, c in lane.commands
                    for r, e in lane.routes}
            runs.update({k: (c, e) for k, c, e in extra})
            decided_count = 0
            oracle = ''
            if lane.name == 'petta' and (swi or expected is not None):
                swi_failures, decided_count = check_swi(
                    lane, results, indices, swi, expected, args.record)
                failures += swi_failures
                oracle = ' and SWI-PeTTa' + ('' if swi else ' (recorded)')
            if lane.name == 'prime':
                for index in indices:
                    row = results.get(index, {})
                    if '.routes.' in lane.cases[index][0]:
                        continue
                    if same(row.get('prime:reference'), row.get('he-lane')) is None:
                        failures.append((index, 'he-lane',
                                         row.get('prime:reference'), row.get('he-lane')))
                oracle = ' and the HE lane'
            routes = len(lane.routes) * len(lane.commands)
            if failures:
                status = 1
                report(lane, root, failures, runs, args.timeout)
                print(f'FAIL: {lane.name}: {len(failures)} divergences over '
                      f'{len(indices)} programs and {routes} routes')
                continue
            notes = []
            if decided_count:
                notes.append(f'{decided_count} decided otherwise than SWI-PeTTa')
            if order_only:
                notes.append(f'{order_only} differing only in order')
            tail = f' ({", ".join(notes)})' if notes else ''
            print(f'PASS: {lane.name}: {len(indices)} programs answer alike '
                  f'on {routes} routes{oracle}{tail}')
    return status


if __name__ == '__main__':
    sys.exit(main())
