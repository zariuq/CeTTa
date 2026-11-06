#!/usr/bin/env python3
"""Types never slow a fast path: each typed probe against its untyped twin.

Usage: check_typed_fast_paths.py CETTA_BINARY [--open] [--only PATTERN]

Each family pairs an untyped program with typed variants that add only
declarations consistent with it, in the PeTTa lane, the HE lane or both.
A typed variant passes when its user-mode instruction count is at most
1.25 times its twin's plus 3,000,000, it answers what its twin answers,
and, for a family measured at two sizes, its cost above an empty program
grows by at most 1.5 times what its twin's grows.  An Atom range holds its
answer, which an untyped program spells with quote: that family's twin
quotes, and HE's printed quote is removed before the answers are compared.

Families still open are listed in OPEN with the reason and skipped unless
--open is given; a run reports any of them that now passes.  The counts
come from perf; without it the check is skipped.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

BOUND_FACTOR = 1.25
BOUND_SLACK = 3_000_000
GROWTH_FACTOR = 1.5

# Open families, by typed probe name prefix and lane, with the reason.
OPEN = {}


def probes():
    """name -> (lanes, text); lanes: p PeTTa, h HE."""
    p = {}

    def add(name, lanes, text):
        p[name] = (lanes, text)

    loop = ("(= (cnt $n $acc) (if (== $n 0) $acc (cnt (- $n 1) (+ $acc 1))))\n"
            "!(cnt 100000 0)\n")
    add('numloop_U_100000', 'ph', loop)
    add('numloop_T_100000', 'ph', '(: cnt (-> Number Number Number))\n' + loop)
    add('numloop_Tundef_100000', 'ph', '(: cnt (-> Number %Undefined% Number))\n' + loop)
    add('numloop_Tvar_100000', 'ph', '(: cnt (-> Number $t $t))\n' + loop)
    add('numloop_Tplus_100000', 'ph', '(: + (-> Number Number Number))\n' + loop)
    add('numloop_Tunrel_100000', 'ph',
        '(: quoteit (-> Atom Atom))\n(= (quoteit $x) $x)\n' + loop)
    add('numhold_U_100000', 'ph',
        '(= (cnt $n $acc) (quote (if (== $n 0) $acc (cnt (- $n 1) (+ $acc 1)))))\n'
        '!(cnt 100000 0)\n')
    add('numhold_Tatom_100000', 'ph', '(: cnt (-> Number Number Atom))\n' + loop)

    fib = "(= (fib $n) (if (< $n 2) $n (+ (fib (- $n 1)) (fib (- $n 2)))))\n!(fib 20)\n"
    add('fib_U_20', 'ph', fib)
    add('fib_T_20', 'ph', '(: fib (-> Number Number))\n' + fib)

    peano = ("(= (fromint $n) (if (== $n 0) Z (S (fromint (- $n 1)))))\n"
             "(= (add Z $m) $m)\n(= (add (S $k) $m) (S (add $k $m)))\n"
             "(= (toint Z) 0)\n(= (toint (S $k)) (+ 1 (toint $k)))\n"
             "!(toint (add (fromint @N@) (fromint @N@)))\n")
    nat = "(: Nat Type)\n(: Z Nat)\n(: S (-> Nat Nat))\n"
    funs = "(: fromint (-> Number Nat))\n(: add (-> Nat Nat Nat))\n(: toint (-> Nat Number))\n"
    for n in (150, 300):
        body = peano.replace('@N@', str(n))
        add(f'peano_U_{n}', 'ph', body)
        add(f'peano_Tctor_{n}', 'ph', nat + body)
        add(f'peano_T_{n}', 'ph', nat + funs + body)
        add(f'peano_Tadd_{n}', 'ph', nat + '(: add (-> Nat Nat Nat))\n' + body)

    hof = ("(= (inc $x) (+ $x 1))\n"
           "(= (app $n $f $acc) (if (== $n 0) $acc (app (- $n 1) $f ($f $acc))))\n"
           "!(app 10000 inc 0)\n")
    add('hof_U_10000', 'ph', hof)
    add('hof_Tinc_10000', 'ph', '(: inc (-> Number Number))\n' + hof)
    add('hof_Tapp_10000', 'ph', '(: inc (-> Number Number))\n'
        '(: app (-> Number (-> Number Number) Number Number))\n' + hof)
    add('hof_Tunrel_10000', 'ph', '(: quoteit (-> Atom Atom))\n' + hof)

    nd = ("(= (sq $x) (* $x $x))\n"
          "(= (range $a $b) (if (>= $a $b) (empty) (superpose ($a (range (+ $a 1) $b)))))\n")
    add('ndfold_U_2000', 'p', nd + '!(foldall + (sq (range 0 2000)) 0)\n')
    add('ndfold_T_2000', 'p', '(: sq (-> Number Number))\n' + nd +
        '!(foldall + (sq (range 0 2000)) 0)\n')
    col = '!(let $l (collapse (sq (range 0 2000))) (foldl-atom $l 0 $a $b (+ $a $b)))\n'
    add('ndcol_U_2000', 'h', nd + col)
    add('ndcol_T_2000', 'h', '(: sq (-> Number Number))\n' + nd + col)

    facts = ''.join(f'(item {i})\n' for i in range(3000))
    fold = facts + "(= (step $i $a) (+ $i $a))\n!(foldall step (match &self (item $x) $x) 0)\n"
    add('nfold_U_3000', 'p', fold)
    add('nfold_Tnum_3000', 'p', '(: step (-> Number Number Number))\n' + fold)
    add('nfold_Tvar_3000', 'p', '(: step (-> $a $a $a))\n' + fold)
    add('nfold_Tundef_3000', 'p', '(: step (-> Number Number %Undefined%))\n' + fold)
    add('nfold_Titem_3000', 'p', '(: item (-> Number Item))\n' + fold)

    edges2k = ''.join(f'(edge {i} {(i * 7 + 3) % 2000})\n' for i in range(2000))
    edge_decl = '(: Edge Type)\n(: edge (-> Number Number Edge))\n'
    join = edges2k + '!(foldall + (match &self (, (edge $x $y) (edge $y $z)) 1) 0)\n'
    add('join_U_2000', 'p', join)
    add('join_Tedge_2000', 'p', edge_decl + join)
    joinhe = edges2k + ('!(let $l (collapse (match &self (, (edge $x $y) (edge $y $z)) 1))'
                        ' (foldl-atom $l 0 $a $b (+ $a $b)))\n')
    add('joinhe_U_2000', 'h', joinhe)
    add('joinhe_Tedge_2000', 'h', edge_decl + joinhe)

    edges3k = ''.join(f'(edge {i} {(i * 7 + 3) % 3000})\n' for i in range(3000))
    nq = '!(let $r (collapse (match &self (, (edge $x $y) (edge $y $z)) (p $x $z))) (size-atom $r))\n'
    p_decl = '(: P Type)\n(: p (-> Number Number P))\n'
    for family, lanes in (('nq', 'p'), ('nqhe', 'h')):
        add(f'{family}_U_3000', lanes, edges3k + nq)
        add(f'{family}_Tedge_3000', lanes, edge_decl + edges3k + nq)
        add(f'{family}_Tp_3000', lanes, p_decl + edges3k + nq)
        add(f'{family}_Tboth_3000', lanes, edge_decl + p_decl + edges3k + nq)
    nq2 = ('!(let $h (collapse (match &self (, (edge $x $y) (edge $y $z)) (r $x $z)))'
           ' (size-atom (sort-atom $h)))\n')
    r_decl = '(: R Type)\n(: r (-> Number Number R))\n'
    add('nq2_U_3000', 'ph', edges3k + nq2)
    add('nq2_Tedge_3000', 'ph', edge_decl + edges3k + nq2)
    add('nq2_Tr_3000', 'ph', r_decl + edges3k + nq2)
    add('nq2_Tredge_3000', 'ph', edge_decl + r_decl + edges3k + nq2)

    memo = ("!(import! &self (library lib_memo))\n"
            "(= (fibm $n) (if (< $n 2) $n (+ (fibm (- $n 1)) (fibm (- $n 2)))))\n"
            "!(memoize fibm)\n!(fibm 300)\n")
    add('memo_U_300', 'p', memo)
    add('memo_T_300', 'p', '(: fibm (-> Number Number))\n' + memo)

    lst = ("(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))\n"
           "(= (sum Nil) 0)\n(= (sum (Cons $x $xs)) (+ $x (sum $xs)))\n"
           "!(sum (build @N@ Nil))\n")
    lctor = "(: List (-> Type Type))\n(: Nil (List $a))\n(: Cons (-> $a (List $a) (List $a)))\n"
    for n in (200, 400):
        body = lst.replace('@N@', str(n))
        add(f'list_U_{n}', 'ph', body)
        add(f'list_Tctor_{n}', 'ph', lctor + body)
        add(f'list_T_{n}', 'ph', lctor + '(: build (-> Number (List Number) (List Number)))\n'
            '(: sum (-> (List Number) Number))\n' + body)
        add(f'list_Tsum_{n}', 'ph', lctor + '(: sum (-> (List Number) Number))\n' + body)
        add(f'list_Tbuild_{n}', 'ph', lctor +
            '(: build (-> Number (List Number) (List Number)))\n' + body)
        add(f'list_Tpoly_{n}', 'ph', lctor + '(: build (-> Number (List $a) (List $a)))\n'
            '(: sum (-> (List $a) Number))\n' + body)

    # tsize, not size: a user function spelled as a static builtin, such as
    # size/1, stays off the fast engines whether it is typed or not.
    tree = ("(= (mk $n) (if (== $n 0) Leaf (Node (mk (- $n 1)) (mk (- $n 1)))))\n"
            "(= (tsize Leaf) 1)\n(= (tsize (Node $l $r)) (+ 1 (+ (tsize $l) (tsize $r))))\n"
            "!(tsize (mk @N@))\n")
    tctor = "(: Tree Type)\n(: Leaf Tree)\n(: Node (-> Tree Tree Tree))\n"
    for n in (10, 12):
        body = tree.replace('@N@', str(n))
        add(f'tree_U_{n}', 'ph', body)
        add(f'tree_Tctor_{n}', 'ph', tctor + body)
        add(f'tree_T_{n}', 'ph', tctor + '(: mk (-> Number Tree))\n(: tsize (-> Tree Number))\n'
            + body)
    return p


LANES = {'p': 'petta', 'h': 'he'}


def measure(binary, directory, name, lang, text):
    path = os.path.join(directory, f'{name}.{lang}.metta')
    with open(path, 'w') as handle:
        handle.write(text)
    counts = os.path.join(directory, f'{name}.{lang}.perf')
    run = subprocess.run(
        ['perf', 'stat', '-x,', '-e', 'instructions:u', '-o', counts, '--',
         binary, '--lang', lang, path],
        capture_output=True, text=True, timeout=600)
    instructions = None
    with open(counts) as handle:
        for line in handle:
            fields = line.split(',')
            if len(fields) > 2 and 'instructions' in fields[2] and fields[0].isdigit():
                instructions = int(fields[0])
    return run.returncode, run.stdout, instructions


def unquote(answer):
    """HE prints a quoted twin's answer as (quote X); compare X."""
    match = re.fullmatch(r'\[\(quote (.*)\)\]\n?', answer.strip() + '\n', re.S)
    return f'[{match.group(1)}]\n' if match else answer


def twin_of(name):
    family, _, size = name.split('_', 2)[0], None, name.rsplit('_', 1)[1]
    return f'{family}_U_{size}'


def is_open(name, lang):
    for (prefix, lane), reason in OPEN.items():
        if lane == lang and name.startswith(prefix + '_') and \
                name[len(prefix) + 1:].isdigit():
            return reason
    return None


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    binary = os.path.abspath(args[0])
    run_open = '--open' in args
    only = args[args.index('--only') + 1] if '--only' in args else None
    if not shutil.which('perf') or subprocess.run(
            ['perf', 'stat', '-x,', '-e', 'instructions:u', '--', 'true'],
            capture_output=True).returncode != 0:
        print('SKIP: typed fast paths need perf to count user instructions')
        return 0
    table = probes()
    failures, opened, now_passing, passed = [], [], [], 0
    with tempfile.TemporaryDirectory(prefix='typed-fast-paths-') as directory:
        cache = {}

        def run(name, lang):
            key = (name, lang)
            if key not in cache:
                cache[key] = measure(binary, directory, name, lang, table[name][1])
            return cache[key]

        for lang in ('petta', 'he'):
            base = measure(binary, directory, 'empty', lang, '!(+ 1 1)\n')[2] or 0
            for name, (lanes, _) in table.items():
                parts = name.split('_')
                if lang not in [LANES[c] for c in lanes] or len(parts) < 3 or \
                        not parts[1].startswith('T'):
                    continue
                if only and not re.search(only, name):
                    continue
                reason = is_open(name, lang)
                if reason and not run_open:
                    opened.append((name, lang, reason))
                    continue
                twin = twin_of(name)
                rc_t, out_t, ins_t = run(name, lang)
                rc_u, out_u, ins_u = run(twin, lang)
                answer_u = unquote(out_u) if name.startswith('numhold_') else out_u
                problems = []
                if rc_t != 0 or rc_u != 0 or ins_t is None or ins_u is None:
                    problems.append(f'not measured (rc {rc_t}/{rc_u})')
                else:
                    bound = BOUND_FACTOR * ins_u + BOUND_SLACK
                    if ins_t > bound:
                        problems.append(f'{ins_t:,} instructions over the bound {bound:,.0f} '
                                        f'({ins_t / ins_u:.2f}x untyped)')
                    if out_t != answer_u:
                        problems.append('answers differ from the untyped twin')
                size = int(parts[-1])
                smaller = [n for n in table if n.startswith('_'.join(parts[:-1]) + '_')
                           and int(n.rsplit('_', 1)[1]) < size]
                if smaller and not problems:
                    small = max(smaller, key=lambda n: int(n.rsplit('_', 1)[1]))
                    _, _, ins_ts = run(small, lang)
                    _, _, ins_us = run(twin_of(small), lang)
                    if ins_ts and ins_us:
                        grow_t = max(ins_t - base, 1) / max(ins_ts - base, 1)
                        grow_u = max(ins_u - base, 1) / max(ins_us - base, 1)
                        if grow_t > GROWTH_FACTOR * max(grow_u, 1.0):
                            problems.append(f'grows {grow_t:.2f}x where untyped grows '
                                            f'{grow_u:.2f}x')
                if problems and not reason:
                    failures.append((name, lang, '; '.join(problems)))
                elif problems:
                    opened.append((name, lang, f"{reason} ({'; '.join(problems)})"))
                elif reason:
                    now_passing.append((name, lang))
                else:
                    passed += 1
    for name, lang, reason in opened:
        print(f'OPEN: {name} ({lang}): {reason}')
    for name, lang in now_passing:
        print(f'NOW PASSES: {name} ({lang}); remove it from OPEN')
    for name, lang, problem in failures:
        print(f'FAIL: {name} ({lang}): {problem}')
    if failures:
        return 1
    print(f'PASS: {passed} typed probes within {BOUND_FACTOR}x of their untyped twins '
          f'plus {BOUND_SLACK:,} instructions, with equal answers and scaling; '
          f'{len(opened)} open, {len(now_passing)} open ones now passing')
    return 0


if __name__ == '__main__':
    sys.exit(main())
