#!/usr/bin/env python3
"""Independent contextual templates, deferred source export and execution."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import itertools
import json
from functools import lru_cache
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, encode as frame, observation, parse_forms, tensor
from check_binary_tree import packed
from check_ic_archive import q, uint, syntax as construct_source
from check_ic_library import clause, plain, symbol, is_symbol
from check_ic_search import check_controls
from check_source_codec import encode_value, reference, text_arg


OPS = {name: 2 for name in ("+", "-", "*", "/", "%", "==", "<", ">", "<=", ">=", "and", "or")}
OPS.update({"if": 3, "not": 1, "get-metatype": 1, "size-atom": 1})
SCALARS = {"I64", "F64", "BigInt", "Rational", "Bool", "String"}


def variables(term):
    if term[0] == "Syntax:Variable":
        return {int(term[1])}
    return set().union(*(variables(child) for child in term[1])) if term[0] == "Syntax:Expr" else set()


def call(term):
    if term[0] != "Syntax:Expr" or not term[1] or term[1][0][:2] != ["Syntax:Literal", "Symbol"]:
        return None
    return term[1][0][2].decode(), term[1][1:]


def split(term):
    found = call(term)
    return found if found and OPS.get(found[0]) == len(found[1]) else None


def sites(document):
    result = []

    def walk(owner, path, bound, term):
        operation = split(term)
        if operation:
            if variables(term) <= bound:
                result.append((owner, path, bound, term))
            for i, child in enumerate(operation[1], 1):
                walk(owner, path + (i,), bound, child)
            return
        found = call(term)
        if not found:
            return
        name, args = found
        if name == "let" and len(args) == 3:
            pattern, value, body = args
            walk(owner, path + (2,), bound, value)
            walk(owner, path + (3,), bound | variables(pattern), body)
        if name == "let*" and len(args) == 2 and args[0][0] == "Syntax:Expr":
            active = bound.copy()
            for i, row in enumerate(args[0][1]):
                if row[0] != "Syntax:Expr" or len(row[1]) != 2:
                    return
                pattern, value = row[1]
                walk(owner, path + (1, i, 1), active, value)
                active = active | variables(pattern)
            walk(owner, path + (2,), active, args[1])
        if name == "case" and len(args) == 2 and args[1][0] == "Syntax:Expr":
            walk(owner, path + (1,), bound, args[0])
            for i, row in enumerate(args[1][1]):
                if row[0] == "Syntax:Expr" and len(row[1]) == 2:
                    pattern, body = row[1]
                    walk(owner, path + (2, i, 1), bound | variables(pattern), body)
        if name in ("map-atom", "filter-atom") and len(args) == 3 and args[1][0] == "Syntax:Variable":
            walk(owner, path + (1,), bound, args[0])
            walk(owner, path + (3,), bound | variables(args[1]), args[2])
        if name == "foldl-atom" and len(args) == 5 and all(args[i][0] == "Syntax:Variable" for i in (2, 3)):
            walk(owner, path + (1,), bound, args[0])
            walk(owner, path + (2,), bound, args[1])
            walk(owner, path + (5,), bound | variables(args[2]) | variables(args[3]), args[4])

    annotations = [form[2][1][1] for form in document[3] if form[2][0] == "Syntax:Expr"
                   and len(form[2][1]) == 3 and is_symbol(form[2][1][0], ":")]
    for i, form in enumerate(document[3]):
        parsed = clause(form)
        if parsed and parsed[0][0] not in annotations and not mentions(parsed[1], "&self"):
            walk(i, (2,), set(range(len(parsed[0]) - 1)), parsed[1])
    return result


def mentions(term, name):
    return is_symbol(term, name) or (term[0] == "Syntax:Expr" and any(mentions(child, name) for child in term[1]))


def encoded(name, document):
    payload = packed(("node", uint(9), [("leaf", b"LC2"), ("leaf", name.encode()),
                                       encode_value("Syntax:Document", document)]))
    return frame([b"SIC1", b"LC2", payload])


def framed_length(lengths):
    width = len(lengths).bit_length()
    bits = 2 * width.bit_length() + 1 + width
    bits += sum(2 * (8 * size).bit_length() + 1 + 8 * size for size in lengths)
    return (bits + 7) // 8


def immutable(tree):
    return (tree[0], tree[1]) if tree[0] == 'leaf' else (tree[0], tree[1], tuple(map(immutable, tree[2])))


@lru_cache(maxsize=16384)
def tree_length(tree):
    if tree[0] == 'leaf':
        return framed_length([0, len(tree[1])])
    children = framed_length([tree_length(child) for child in tree[2]])
    return framed_length([1, len(tree[1]), children])


def encoded_length(name, document):
    source = tree_length(immutable(encode_value('Syntax:Document', document)))
    payload = framed_length([1, len(uint(9)), framed_length([
        framed_length([0, 3]), framed_length([0, len(name.encode())]), source])])
    return framed_length([4, 3, payload])


def candidate(document, name, first, second, ready=True, selected=None):
    selected = sites(document) if selected is None else selected
    if not (0 <= first < len(selected) and 0 <= second < len(selected)):
        return None
    pairs = []

    def generalize(a, b):
        left, right = split(a), split(b)
        if left and right and left[0] == right[0]:
            return "call", left[0], [generalize(x, y) for x, y in zip(left[1], right[1])]
        if a == b and not variables(a):
            return "literal", deepcopy(a)
        pair = a, b
        if pair not in pairs:
            pairs.append(deepcopy(pair))
        return "hole", pairs.index(pair)

    pattern = generalize(selected[first][3], selected[second][3])
    def entered(term, owner):
        ports = len(clause(document[3][owner])[0]) - 1
        return (term[0] == "Syntax:Literal" and term[1] in SCALARS) or (
            term[0] == "Syntax:Variable" and int(term[1]) < ports)
    modes = [ready and entered(a, selected[first][0]) and entered(b, selected[second][0]) for a, b in pairs]

    def body(p):
        if p[0] == "hole":
            variable = ["Syntax:Variable", str(p[1])]
            return variable if modes[p[1]] else ["Syntax:Expr", [variable]]
        if p[0] == "literal":
            return deepcopy(p[1])
        return ["Syntax:Expr", [symbol(p[1])] + [body(child) for child in p[2]]]

    def match(p, term, environment, owner):
        if p[0] == "literal":
            return term == p[1]
        if p[0] == "hole":
            if modes[p[1]] and not entered(term, owner):
                return False
            if p[1] in environment:
                return environment[p[1]] == term
            environment[p[1]] = term
            return True
        found = split(term)
        return bool(found and found[0] == p[1] and all(match(child, value, environment, owner)
                                                     for child, value in zip(p[2], found[1])))

    replacements = []
    for owner, path, _bound, term in selected:
        if any(prior == owner and path[:len(where)] == where for prior, where, _value in replacements):
            continue
        environment = {}
        if match(pattern, term, environment, owner):
            args = [deepcopy(environment[i]) if modes[i] else ["Syntax:Expr", [symbol("|->"), ["Syntax:Expr", []],
                    deepcopy(environment[i])]] for i in range(len(pairs))]
            replacements.append((owner, path, ["Syntax:Expr", [symbol(name)] + args]))
    if len(replacements) < 2:
        return None
    changed = deepcopy(document)
    for owner, path, value in replacements:
        pointer = changed[3][owner][2]
        for position in path[:-1]:
            pointer = pointer[1][position]
        pointer[1][path[-1]] = value
    names = [["Syntax:PlainName", q("p" + str(i))] for i in range(len(pairs))]
    left = ["Syntax:Expr", [symbol(name)] + [["Syntax:Variable", str(i)] for i in range(len(pairs))]]
    definition = ["Syntax:Form", names, ["Syntax:Expr", [symbol("="), left, body(pattern)]]]
    changed[3].insert(0, definition)
    return changed, len(replacements), pattern


def criterion_seeds(document):
    """Locate the two authored criterion branches, not incidental site numbers."""
    available = sites(document)
    result = []
    for name in ('ic-arrays:encode-criterion', 'ic-arrays:decode-criterion'):
        found = []
        for index, (owner, _path, _bound, term) in enumerate(available):
            parsed = clause(document[3][owner])
            if not parsed or not is_symbol(parsed[0][0], name):
                continue
            operation = split(term)
            condition = split(operation[1][0]) if operation and operation[0] == 'if' else None
            if condition and condition[0] == 'or':
                found.append(index)
        if len(found) != 1:
            raise AssertionError((name, found))
        result.append(found[0])
    return tuple(result)


def audit_sources(paths, budget):
    findings = []
    for relative in paths:
        document = reference((ROOT / relative).read_text())
        available = sites(document)
        # Homogeneous constructor groups guide seed inspection; other pairs
        # remain part of the learner's declared region rather than being
        # silently omitted by this source audit.
        groups = {}
        for i, (_owner, _path, _bound, term) in enumerate(available):
            groups.setdefault(split(term)[0], []).append(i)
        def shared(a, b):
            first, second = split(a), split(b)
            if first and second and first[0] == second[0]:
                return 1 + sum(shared(x, y) for x, y in zip(first[1], second[1]))
            return int(a == b and not variables(a))
        ranked = [(shared(available[i][3], available[j][3]), i, j) for indices in groups.values()
                  for i, j in itertools.combinations(indices, 2)]
        ranked.sort(key=lambda row: (-row[0], row[1], row[2]))
        best = None
        inspected = 0
        total = sum(len(indices) * (len(indices) - 1) // 2 for indices in groups.values())
        for score, first, second in ranked[:budget]:
            inspected += 1
            proposed = candidate(document, "ic-learned:audit", first, second, selected=available)
            if not proposed:
                continue
            cost = encoded_length("ic-learned:audit", proposed[0])
            if best is None or cost < best['bytes']:
                best = {"seeds": [first, second], "bytes": cost, "support": proposed[1], "shared_score": score}
        if best:
            proposed = candidate(document, "ic-learned:audit", *best['seeds'], selected=available)
            assert best['bytes'] == len(encoded("ic-learned:audit", proposed[0]))
        findings.append({"source": relative, "sites": len(available), "original_bytes": len(plain(document)),
                         "best_same_head_seed": best, "native_discovery_run": False,
                         "seeds_inspected": inspected, "same_head_region_size": total, "audit_complete": inspected == total})
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--audit", nargs="*", help="explicit source-only same-head seed audit")
    parser.add_argument("--seed-budget", type=int, default=64, help="source audit seed bound; not a qualification claim")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.audit is not None:
        result = {"kind": "contextual-source-seed-audit", "sources": audit_sources(args.audit, args.seed_budget),
                  "qualification_claim": False}
        (args.output / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result))
        return 0
    for file in ("lib_ic_context_library.metta", "lib_source_context.metta", "lib_ic_tree_archive.metta"):
        check_controls((ROOT / "lib/petta" / file).read_text())
    examples = [
        '(= (f $x) (if (> $x 0) (+ $x 2) (* $x 3))) (= (g $y) (if (> $y 0) (* $y 2) (+ $y 3)))',
        '(= (f $x) (let $a (+ $x 1) (if (> $a 0) (+ $a 2) (* $a 3)))) '
        '(= (g $x) (let $b (+ $x 1) (if (> $b 0) (* $b 2) (+ $b 3))))',
        '(= (f $x) (if (> $x 0) (println! "YES-F") (superpose ()))) '
        '(= (g $y) (if (> $y 0) (println! "YES-G") (superpose ())))',
        '(= (f $x) (if (> $x 0) (+ $x 2) 0)) (= (g $y) (if (> $y 0) (+ $y 3) 0))',
        '(= (f $xs) (map-atom $xs $item (if (> $item 0) (+ $item 2) (* $item 3)))) '
        '(= (g $ys) (map-atom $ys $entry (if (> $entry 0) (* $entry 2) (+ $entry 3))))',
        '(= (f $xs) (foldl-atom $xs 0 $total $item (if (> $item 0) (+ $total $item) (- $total $item)))) '
        '(= (g $ys) (foldl-atom $ys 0 $sum $entry (if (> $entry 0) (- $sum $entry) (+ $sum $entry))))',
        '(= (f $value) (case $value (((tag $x) (if (> $x 0) (+ $x 2) (* $x 3))) ($other 0)))) '
        '(= (g $value) (case $value (((tag $y) (if (> $y 0) (* $y 2) (+ $y 3))) ($other 0))))',
        '(= (f $x) (if True (quote (datum-f $x)) (superpose ()))) '
        '(= (g $y) (if True (quote (datum-g $y)) (superpose ())))',
        '(= (f $x) (if True (+ (observe $x) (observe $x)) (superpose ()))) '
        '(= (g $y) (if True (+ (observe (+ $y 1)) (observe (+ $y 1))) (superpose ()))) '
        '(= (observe $number) (let $noted (println! $number) $number))',
        '(= (f $x) (if True (/ 1 $x) (superpose ()))) '
        '(= (g $y) (if True (/ 2 $y) (superpose ())))',
        '(= (f $x) (if True $x (superpose ()))) (= (g $y) (if True $y (superpose ())))',
        '(= (f $xs) (filter-atom $xs $item (if (> $item 0) True False))) '
        '(= (g $ys) (filter-atom $ys $entry (if (> $entry 1) True False)))',
    ]
    queries = [
        [('(f 2)', ['4']), ('(g 2)', ['4']), ('(f -1)', ['-3']), ('(g -1)', ['2']),
         ('(f (superpose (1 2 1)))', ['3','4','3'])],
        [('(f 2)', ['5']), ('(g 2)', ['6']), ('(f -2)', ['-3']), ('(g -2)', ['2'])],
        [('(f 2)', ['true']), ('(g 2)', ['true']), ('(f -1)', []), ('(g -1)', [])],
        [('(f 2)', ['4']), ('(g 2)', ['5']), ('(f -2)', ['0'])],
        [('(f (1 -1 2))', [['3','-3','4']]), ('(g (1 -1 2))', [['2','2','4']]), ('(f ())', [[]])],
        [('(f (1 -1 2))', ['4']), ('(g (1 -1 2))', ['-4']), ('(f ())', ['0'])],
        [('(f (tag 2))', ['4']), ('(g (tag -1))', ['2']), ('(f (untagged 9))', ['0'])],
        [('(f (quote (superpose (1 2))))', [['datum-f',['superpose',['1','2']]]]),
         ('(g 7)', [['datum-g','7']])],
        [('(f 2)', ['4']), ('(g 2)', ['6'])],
        [('(catch (f 0))', None), ('(catch (g 0))', None), ('(f 2)', ['0.5'])],
        [('(f (quote (+ 1 2)))', [['+','1','2']]), ('(g (quote (superpose (1 2))))', [['superpose',['1','2']]]),
         ('(f (superpose (1 2 1)))', ['1','2','1'])],
        [('(f (1 -1 2))', [['1','2']]), ('(g (1 -1 2))', [['2']]), ('(f ())', [[]])],
    ]
    corpus = [
        ('lib/petta/lib_pln.metta', (11, 16),
         [('(/ (* (+ 1.0 (/ 1.0 1.0)) 1.0) (+ 1.0 1.0))', ['1.0'], '(ic-learned:test 1.0 1.0 1.0)'),
          ('(/ (* (+ 1.0 (/ 0.0 1.0)) 1.0) (+ 1.0 1.0))', ['0.5'], '(ic-learned:test 0.0 1.0 1.0)')]),
        ('lib/petta/lib_ic_numeric_arrays.metta', criterion_seeds(reference((ROOT/'lib/petta/lib_ic_numeric_arrays.metta').read_text())),
         [('(ic-arrays:encode-criterion "BoolTensor" Numeric:Exact)', [['IC:Some','Numeric:Exact']]),
          ('(ic-arrays:decode-criterion "I64Tensor" Numeric:Exact)', [['IC:Some','Numeric:Exact']]),
          ('(ic-arrays:encode-criterion "F64Tensor" Numeric:Exact)', ['IC:None']),
          ('(ic-arrays:decode-criterion "Unknown" Numeric:Exact)', ['IC:None'])]),
    ]
    controls = len(examples)
    examples.extend((ROOT / relative).read_text() for relative, _seed, _queries in corpus)
    queries.extend(row[2] for row in corpus)
    code = ['!(import! &self (library lib_ic_library))',
            '!(println! (ContextWarm (ic:library-unpack (ic:source-pack (syntax:read "")))))',
            '!(import! &self (library lib_ic_context_library))', '''
(: PretendGrounded String)
(= (context-test:factor $source $first $second)
   (let $original (syntax:read $source)
     (case (context-library:seed (ic:prepare-library Deferred $original) "ic-learned:test" $first $second)
       (((Library:Candidate $bytes $support)
         (ContextFactor $support (tensor:inspect $bytes) (== (ic:library-unpack $bytes) (IC:Some $original))
           (case (ic:export-library $bytes)
             (((Library:Export $name $document $text $restored) (tensor:inspect (binary:utf8 $text))) ($other $other)))))
        ($other $other)))))
(= (context-test:finish $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (if (> $remaining 0) (context-test:finish $next (- $remaining 1))
         (let $closed (ic:release $next) (ContextSearchFault Budget))))
      ($finished $finished))))
(= (context-test:search $text $pieces)
   (case (ic:learn-library Deferred (syntax:read $text) "test" 64)
     (((IC:PreparedSearch $state)
       (case (if $pieces (context-test:finish $state 8) (ic:resume $state 8))
         (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $next)
           (let $closed (ic:release $next)
             (ContextSearch (tensor:inspect $best) $seen $accepted $faults $reason $first)))
          ($bad-result $bad-result)))) ($bad-preparation $bad-preparation))))
(= (context-test:cancelled $text)
   (case (ic:learn-library Deferred (syntax:read $text) "test" 64)
     (((IC:PreparedSearch $state)
       (case (ic:resume $state 0)
         (((IC:SearchResult $initial $stats $pause $first $next)
           (case (ic:cancel $next)
             (((IC:SearchResult $same $same-stats $cancelled $same-fault $later)
               (case (ic:resume $later 8)
                 (((IC:SearchResult $best $final-stats $reason $final-fault $finished)
                   (let $closed (ic:release $finished)
                     (ContextLifecycle $pause $cancelled (tensor:inspect $best) $reason)))
                  ($bad-resume $bad-resume)))) ($bad-cancel $bad-cancel)))) ($bad-pause $bad-pause))))
      ($bad-start $bad-start))))
''']
    controls_expected = []
    def control(call_text, wanted):
        index = len(controls_expected)
        code.append(f'!(println! (ContextControl {index} {call_text}))')
        controls_expected.append(['ContextControl', str(index), wanted])
    shared = '(quote (' + ' '.join(f'payload-{i}' for i in range(40)) + '))'
    search_source = f'(= (f $x) (if $x {shared} (quote no))) (= (g $y) (if $y {shared} (quote no)))'
    search_original = reference(search_source)
    search_candidate, _search_support, _search_pattern = candidate(search_original, 'ic-learned:test', 0, 1)
    search_bytes = encoded('ic-learned:test', search_candidate)
    assert len(search_bytes) < len(plain(search_original))
    for pieces in ('False', 'True'):
        control(f'(context-test:search {text_arg(search_source)} {pieces})',
                ['ContextSearch', observation(search_bytes), '1', '1', '0', 'RegionExhausted', 'IC:NoFault'])
    control(f'(context-test:cancelled {text_arg(search_source)})',
            ['ContextLifecycle', 'BudgetExhausted', 'Cancelled', observation(search_bytes), 'RegionExhausted'])
    control(f'(ic:learn-library Deferred (syntax:read {text_arg(search_source)}) "test" 0)',
            ['IC:SearchRefused', 'InvalidQuantum'])
    base = reference(examples[0])
    base_sites = sites(base)
    first_pair = [next(i for i,(owner,_path,_bound,term) in enumerate(base_sites)
                       if owner==target and split(term)[0]=='if') for target in (0,1)]
    base_changed, _support, _pattern = candidate(base, 'ic-learned:test', *first_pair)
    for mutation in ('computation', 'symbol', 'out-of-range', 'quoted-site', 'unused-computation'):
        damaged = deepcopy(base_changed)
        invocation = damaged[3][1][2][1][2]
        if mutation == 'computation':
            invocation[1][1] = reference('(+ 1 2)')[3][0][2]
        elif mutation == 'symbol':
            invocation[1][1] = symbol('PretendGrounded')
        elif mutation == 'out-of-range':
            invocation[1][1] = ['Syntax:Variable','99']
        elif mutation == 'quoted-site':
            damaged[3][1][2][1][2] = ['Syntax:Expr',[symbol('quote'),deepcopy(invocation)]]
        else:
            damaged[3][0][2][1][2] = ['Syntax:Literal','I64',(1).to_bytes(8,'big',signed=True)]
            invocation[1][1] = reference('(println! "MUST-NOT-EXECUTE")')[3][0][2]
        control(f'(ic:library-unpack {tensor(encoded("ic-learned:test",damaged))})','IC:None')
    local_original = reference(examples[1])
    local_sites = sites(local_original)
    local_pair = [next(i for i,(owner,_path,_bound,term) in enumerate(local_sites)
                       if owner==target and split(term)[0]=='if') for target in (0,1)]
    local_changed, _local_support, _local_pattern = candidate(local_original,'ic-learned:test',*local_pair)
    local_call = local_changed[3][1][2][1][2][1][3]
    local_argument = next(i for i,term in enumerate(local_call[1][1:],1)
                          if term[0]=='Syntax:Expr' and term[1][2][0]=='Syntax:Variable')
    local_call[1][local_argument] = deepcopy(local_call[1][local_argument][1][2])
    control(f'(ic:library-unpack {tensor(encoded("ic-learned:test",local_changed))})','IC:None')
    # Reconstruction authority requires the original task, not a self-declared
    # valid source package. A different in-range alias is well formed but wrong.
    alias_source = '(= (f $x $y) (if (> $x 0) (+ $x $y) 0)) (= (g $a $b) (if (> $a 0) (+ $a $b) 0))'
    alias_original = reference(alias_source)
    alias_sites = sites(alias_original)
    alias_pair = [next(i for i,(owner,_path,_bound,term) in enumerate(alias_sites)
                       if owner==target and split(term)[0]=='if') for target in (0,1)]
    alias_changed, _count, _p = candidate(alias_original,'ic-learned:test',*alias_pair)
    alias_changed[3][1][2][1][2][1][1] = ['Syntax:Variable','1']
    control(f'(ic:library-check {tensor(plain(alias_original))} {tensor(plain(alias_original))} '
            f'{tensor(encoded("ic-learned:test",alias_changed))})',['IC:PackedRejected','Reconstruction'])
    for source in ('(= (+ $a $b) $a)', '(: if (-> Atom Atom Atom Atom))'):
        control(f'(ic:prepare-library Deferred (syntax:read {text_arg(source+examples[0])}))',
                ['Library:Refused','NonstandardOperators'])
    foreign = deepcopy(base);foreign[2][1]=q('he')
    control(f'(ic:prepare-library Deferred {construct_source(foreign)})', ['Library:Refused','DifferentReader'])
    control(f'''(case (ic:learn-library Deferred (syntax:read {text_arg(examples[0])}) "context-revision" 64)
       (((IC:PreparedSearch $state)
         (let* (($change (add-atom &self ContextLibraryRevisionChanged)) ($answer (ic:resume $state 0))
                ($closed (ic:release $state))) $answer)) ($failure $failure)))''',
        ['IC:ResumeRejected','ChangedDependencies'])
    expected, exported = [], []
    for example_index, source in enumerate(examples):
        original = reference(source)
        available = sites(original)
        # Choose the outermost conditional inside each function, not an
        # arbitrary numerical offset in the independent traversal.
        seeds = corpus[example_index - controls][1] if example_index >= controls else [
            next(i for i, (owner, _path, _bound, term) in enumerate(available)
                 if owner == target and split(term)[0] == "if") for target in (0, 1)]
        changed, support, _pattern = candidate(original, "ic-learned:test", *seeds)
        assert encoded_length('ic-learned:test', changed) == len(encoded('ic-learned:test', changed))
        index = len(expected)
        code.append(f'!(println! (ContextCase {index} (context-test:factor {text_arg(source)} {seeds[0]} {seeds[1]})))')
        expected.append((support, encoded("ic-learned:test", changed), changed))
        exported.append(source)
    fixture = args.output / "context.metta"
    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture.write_text(program)
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())], cwd=ROOT,
                            capture_output=True, text=True, timeout=120)
    (args.output / "context.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "ContextCase"]
    found_controls = [f for f in parse_forms(result.stdout) if isinstance(f,list) and f and f[0]=='ContextControl']
    indexed = {int(f[1]): f[2] for f in actual}
    mismatches, emit_results, executions, costs = [], [], [], []
    for i, (support, blob, changed) in enumerate(expected):
        costs.append({'case':i,'original_bytes':len(plain(reference(exported[i]))),
                      'helper_bytes':len(blob),'support':support})
        found = indexed.get(i)
        prefix = ["ContextFactor", str(support), observation(blob), "true"]
        if not isinstance(found, list) or found[:4] != prefix:
            mismatches.append({"case": i, "expected": prefix, "actual": found})
            continue
        data = found[4]
        source = bytes(int(row[1]) for row in data[3]).decode()
        emitted = args.output / f"export-{i}.metta"
        emitted.write_text(source)
        emit_results.append({"case": i, "reader_roundtrip": reference(source) == changed})
        variants = []
        reference_answers = None
        for label, body in [('original', exported[i]), ('learned', source)]:
            query_text = '\n'.join(f'!(println! (ContextAnswer {j} (collapse '
                f'{row[2] if label == "learned" and len(row)>2 else row[0]})))'
                for j, row in enumerate(queries[i])) + '\n'
            path = args.output / f'execute-{i}-{label}.metta'
            path.write_text(body + '\n' + query_text)
            run = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())], cwd=ROOT,
                                 capture_output=True, text=True, timeout=30)
            (args.output / f'execute-{i}-{label}.log').write_text(run.stdout + run.stderr)
            answers = [f for f in parse_forms(run.stdout) if isinstance(f,list) and f and f[0]=='ContextAnswer']
            actual_answers = {int(f[1]): f[2] for f in answers}
            correct = run.returncode == 0 and len(answers)==len(queries[i])
            for j, row in enumerate(queries[i]):
                wanted = row[1]
                observed = actual_answers.get(j, [])
                if wanted is None:
                    correct = correct and len(observed)==1 and isinstance(observed[0],list) and observed[0][0]=='Error'
                else:
                    correct = correct and Counter(map(json.dumps, observed)) == Counter(map(json.dumps, wanted))
            if reference_answers is None:
                reference_answers = actual_answers
            else:
                correct = correct and all(Counter(map(json.dumps, actual_answers.get(j, []))) ==
                                          Counter(map(json.dumps, reference_answers.get(j, []))) for j in range(len(queries[i])))
            effects = [line for line in run.stdout.splitlines() if line and
                       not line.startswith('(ContextAnswer ') and line not in ('true','false')]
            variants.append({'variant':label,'passed':correct,'effects':effects})
        executions.append({'case':i,'passed':all(row['passed'] for row in variants) and
                           variants[0]['effects']==variants[1]['effects'],'variants':variants})
    control_lookup = {f[1]:f for f in found_controls}
    control_errors = [{'case':i,'expected':value,'actual':control_lookup.get(str(i))}
                      for i,value in enumerate(controls_expected) if control_lookup.get(str(i)) != value]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not mismatches
               and 'MUST-NOT-EXECUTE' not in result.stdout
               and all(row['reader_roundtrip'] for row in emit_results) and all(row['passed'] for row in executions),
               "checks": len(expected)+len(controls_expected), "answers": len(actual)+len(found_controls),
               "mismatches": mismatches, "exports": emit_results, 'control_errors':control_errors,
               'executions':executions,
               'complete_costs':costs,
               'real_source_cases':[row[0] for row in corpus],
               "scope": "independent nested-source patterns, LC2 bytes, delayed captures and source reconstruction"}
    receipt['passed'] = receipt['passed'] and not control_errors and len(found_controls)==len(controls_expected)
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [row['case'] for row in mismatches]}))
    for row in mismatches[:2]:
        print(json.dumps(row)[:1800])
    return 0 if receipt['passed'] else 1


if __name__ == "__main__":
    raise SystemExit(main())
