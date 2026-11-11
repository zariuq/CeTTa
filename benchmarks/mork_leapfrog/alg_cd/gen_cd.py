#!/usr/bin/env python3
"""Condensed detachment: forward saturation over theorem schemas with variables.

Meredith's condensed detachment (CD) is modus ponens with most general
unification: from a major premise (> A B) and a minor premise C, with the two
premises' variables kept apart, if A and C unify with most general unifier s
then B s is a theorem.  It is the inference rule of the classical
single-axiom studies of the implicational and equivalential calculi
(Lukasiewicz, Meredith, Prior; Wos's OTTER experiments).

Starting from an axiom system, every round applies CD to every pair of known
theorems, majors and minors alike, and adds the conclusions:

    T(r+1) = T(r) + { CD(P, Q) : P, Q in T(r) }

Theorems are schemas: they contain variables, they are identified up to
renaming of variables (alpha-equivalence), and the occurs check is
enforced.  This is forward saturation, where storing every derived formula is
the algorithm: a round is one unification join between the stored majors'
antecedents and the stored theorems, both sides holding variables.

Axiom systems (the size knob is the system plus the number of rounds):
  lukC  Lukasiewicz's shortest single axiom for the implicational calculus,
        CCCpqrCCrpCsp: 1, 2, 4, 11, 40, 490, 24994 theorems after rounds 0-6
  luk3  Lukasiewicz's three axioms for the calculus with negation (the
        axioms of MORK's bfc benchmark): 3, 9, 36, 247, 6813 after rounds 0-4
  KS    Hilbert's K and S: 2, 6, 21, 112, 2346 after rounds 0-4
  XGK   Kalman's single axiom for the equivalential calculus,
        e(x, e(e(y, e(z, x)), e(z, y))), detaching on e: 1, 2, 5, 24, 255 after rounds 0-4

Outputs: (thm X) for every theorem, and (nthm r n), the number of
theorems after round r.  The oracle is a Python closure with its own
unification and renaming-canonical forms.

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.values, NAME.expect.json,
NAME.oracle_values.json.
usage: gen_cd.py SYSTEM ROUNDS [--outdir DIR]
"""
import argparse, json, sys, time
from pathlib import Path

sys.setrecursionlimit(100000)

SYSTEMS = {
    'lukC': ('>', ["(> (> (> $p $q) $r) (> (> $r $p) (> $s $p)))"]),
    'luk3': ('>', ["(> $p (> $s $p))",
                   "(> (> $p (> $s $x)) (> (> $p $s) (> $p $x)))",
                   "(> (> (! $p) (! $s)) (> $s $p))"]),
    'KS':   ('>', ["(> $p (> $q $p))",
                   "(> (> $p (> $q $r)) (> (> $p $q) (> $p $r)))"]),
    'XGK':  ('e', ["(e $x (e (e $y (e $z $x)) (e $z $y)))"]),
}

# ---------------------------------------------------------------- terms
# a term is a symbol (str), a variable (int >= 0), or a tuple of terms

def parse(s):
    toks = s.replace('(', ' ( ').replace(')', ' ) ').split()
    pos = 0
    names = {}
    def go():
        nonlocal pos
        t = toks[pos]; pos += 1
        if t == '(':
            xs = []
            while toks[pos] != ')':
                xs.append(go())
            pos += 1
            return tuple(xs)
        if t.startswith('$'):
            return names.setdefault(t, len(names))
        return t
    return go()

def walk(t, s):
    while isinstance(t, int) and t in s:
        t = s[t]
    return t

def occurs(v, t, s):
    t = walk(t, s)
    if t == v:
        return True
    return isinstance(t, tuple) and any(occurs(v, x, s) for x in t)

def unify(a, b):
    s = {}
    stack = [(a, b)]
    while stack:
        x, y = stack.pop()
        x, y = walk(x, s), walk(y, s)
        if x == y:
            continue
        if isinstance(x, int):
            if occurs(x, y, s):
                return None
            s[x] = y
        elif isinstance(y, int):
            if occurs(y, x, s):
                return None
            s[y] = x
        elif isinstance(x, tuple) and isinstance(y, tuple) and len(x) == len(y):
            stack.extend(zip(x, y))
        else:
            return None
    return s

def subst(t, s):
    t = walk(t, s)
    return tuple(subst(x, s) for x in t) if isinstance(t, tuple) else t

def canon(t):
    """Variables renumbered by first occurrence: equal iff alpha-equivalent."""
    m = {}
    def go(t):
        if isinstance(t, int):
            return m.setdefault(t, len(m))
        return tuple(go(x) for x in t) if isinstance(t, tuple) else t
    return go(t)

def nvars(t):
    if isinstance(t, int):
        return t + 1
    return max((nvars(x) for x in t), default=0) if isinstance(t, tuple) else 0

def shift(t, k):
    if isinstance(t, int):
        return t + k
    return tuple(shift(x, k) for x in t) if isinstance(t, tuple) else t

def show(t):
    if isinstance(t, int):
        return f"$v{t}"
    return '(' + ' '.join(show(x) for x in t) + ')' if isinstance(t, tuple) else t

def head_key(t):
    return None if isinstance(t, int) else (t[0], len(t)) if isinstance(t, tuple) else t

def closure(axioms, imp, rounds):
    T = {canon(a) for a in axioms}
    counts = [len(T)]
    for r in range(rounds):
        index = {}
        for t in T:
            index.setdefault(head_key(t), []).append(t)
        new = set()
        for maj in T:
            if not (isinstance(maj, tuple) and len(maj) == 3 and maj[0] == imp):
                continue
            ante, k = maj[1], nvars(maj)
            minors = list(T) if isinstance(ante, int) else index.get(head_key(ante), []) + index.get(None, [])
            for mi in minors:
                s = unify(ante, shift(mi, k))
                if s is not None:
                    new.add(canon(subst(maj[2], s)))
        T |= new
        counts.append(len(T))
    return T, counts

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('system', choices=sorted(SYSTEMS))
    ap.add_argument('rounds', type=int)
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    a = ap.parse_args()
    imp, axioms_src = SYSTEMS[a.system]
    axioms = [parse(x) for x in axioms_src]
    t0 = time.time()
    T, counts = closure(axioms, imp, a.rounds)
    name = f'cd_{a.system}_r{a.rounds}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    R = a.rounds
    ax = '\n'.join(f"(thm {x})" for x in axioms_src)
    mm2 = [f"; {name}: condensed detachment, {R} rounds of every theorem against every theorem",
           ax,
           "; round r: every major (> A B) against every theorem C; A and C unify, B is written under the unifier.",
           "; Each exec reads the space as it was when it fired, so round r sees exactly the theorems of round r-1;",
           "; alpha-equivalent conclusions are one fact.",
           "(exec (0 1) (, (thm $x)) (O (count (nthm 0 $n) $n $x)))"]
    for r in range(1, R + 1):
        mm2.append(f"(exec ({r} 0) (, (thm ({imp} $a $b)) (thm $a)) (, (thm $b)))")
        mm2.append(f"(exec ({r} 1) (, (thm $x)) (O (count (nthm {r} $n) $n $x)))")
    (out / f'{name}.mm2').write_text('\n'.join(mm2) + '\n')
    metta = [f"; {name}: the same saturation in MeTTa (PeTTa dialect)",
             "; theorems live in a set space, which holds alpha-equivalent atoms once",
             "!(bind! &T (new-space set))",
             '\n'.join(f"!(add-atom &T (thm {x}))" for x in axioms_src),
             "(= (theorems) (size-atom (collapse (match &T (thm $x) t))))",
             "; one round: the conclusions go to a fresh set space, then join the theorems",
             f"(= (cd-round) (let* (($N (new-space set))",
             f"                    ($c (size-atom (collapse (match &T (thm ({imp} $a $b)) (match &T (thm $a) (add-atom $N (thm $b)))))))",
             f"                    ($m (size-atom (collapse (match $N (thm $x) (add-atom &T (thm $x)))))))",
             f"                (theorems)))",
             "!(let $n (theorems) (nthm 0 $n))"]
    for r in range(1, R + 1):
        metta.append(f"!(let $n (cd-round) (nthm {r} $n))")
    metta.append("!(theorems)")
    (out / f'{name}.metta').write_text('\n'.join(metta) + '\n')
    (out / f'{name}.heads').write_text('thm\n')
    (out / f'{name}.values').write_text('nthm\n')
    (out / f'{name}.expect.json').write_text(json.dumps({'thm': len(T), 'nthm': counts}))
    (out / f'{name}.oracle_values.json').write_text(json.dumps({'nthm': sorted(f"(nthm {r} {n})" for r, n in enumerate(counts))}))
    print(name, 'theorems per round', counts, 'largest theorem', max(len(show(t)) for t in T), 'chars',
          f'oracle {time.time() - t0:.1f}s')

if __name__ == '__main__':
    main()
