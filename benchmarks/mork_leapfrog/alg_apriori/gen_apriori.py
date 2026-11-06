#!/usr/bin/env python3
"""Frequent itemset mining with Apriori: support counting, pruning, candidate generation.

Market-basket data in the style of the IBM Quest generator (Agrawal and
Srikant, VLDB 1994): a pool of potentially frequent patterns, consecutive
patterns sharing about half their items; each transaction is the union of
one to three patterns drawn by weight, each item kept with probability 0.85,
plus a few noise items.  Items are integers 0..I-1, stored as (has T I).

Apriori (Agrawal and Srikant 1994) mines every itemset contained in at least
MINSUP transactions, level by level:

  level 1   count the transactions of every item; keep those with support
            >= MINSUP
  level 2   count every pair of frequent items that occur together, from the
            transaction side (a self-join of has on the transaction)
  level k   candidates: two frequent (k-1)-itemsets sharing their first k-2
            items, kept only if every (k-1)-subset is frequent (the Apriori
            prune, a cyclic multi-way join over the previous level); then the
            support of every candidate, counted per transaction by descending
            the candidates' prefix tree with the transaction's items (the
            hash tree of the original paper is a trie here)

Itemsets are written in increasing item order; (lt A B) is the item order
and (ge N) lists the supports that count as frequent.

Outputs: (sK a1 .. aK n) the support of every counted candidate, and
(fK a1 .. aK n) the frequent itemsets with their supports, for K = 1..KMAX.
The oracle is a direct Python Apriori over tid sets.

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.values, NAME.expect.json,
NAME.oracle_values.json.
usage: gen_apriori.py T [--items 300] [--minsup-frac 0.01] [--seed 0] [--kmax 8]
                      [--mm2-form vertical|horizontal] [--metta-form vertical|vertical-all|horizontal]
"""
import argparse, json, math, random
from itertools import combinations
from pathlib import Path

def quest(T, I, rng, npat=60, pat_mean=4):
    """Patterns: Poisson-ish sizes, half of each drawn from the previous one,
    exponential weights, Zipf-like item popularity for the fresh half."""
    pop = [1.0 / (i + 1) ** 0.7 for i in range(I)]
    pats, prev = [], []
    for _ in range(npat):
        size = max(2, min(8, int(rng.gauss(pat_mean, 1.2) + 0.5)))
        keep = rng.sample(prev, min(len(prev), size // 2)) if prev else []
        fresh = set(keep)
        while len(fresh) < size:
            fresh.add(rng.choices(range(I), pop)[0])
        prev = sorted(fresh)
        pats.append(prev)
    w = [rng.expovariate(1.0) for _ in pats]
    txs = []
    for _ in range(T):
        items = set()
        for p in rng.choices(pats, w, k=rng.choice([1, 1, 2, 2, 3])):
            items.update(x for x in p if rng.random() < 0.85)
        for _ in range(rng.choice([0, 1, 2, 3])):
            items.add(rng.choices(range(I), pop)[0])
        if not items:
            items.add(rng.randrange(I))
        txs.append(sorted(items))
    return txs

def apriori(txs, minsup, kmax):
    """Returns per level k: candidate supports {itemset: n} and frequent {itemset: n},
    with the candidate sets exactly as the programs form them."""
    tid = {}
    for t, items in enumerate(txs):
        for x in items:
            tid.setdefault((x,), set()).add(t)
    cand = {k: len(v) for k, v in tid.items()}
    levels = [(cand, {k: v for k, v in cand.items() if v >= minsup})]
    # level 2 counts every co-occurring pair of frequent items (no candidate list)
    f1 = {k[0] for k in levels[0][1]}
    c2 = {}
    for items in txs:
        fi = [x for x in items if x in f1]
        for a, b in combinations(fi, 2):
            c2[(a, b)] = c2.get((a, b), 0) + 1
    levels.append((c2, {k: v for k, v in c2.items() if v >= minsup}))
    tidsets = {}
    for t, items in enumerate(txs):
        for x in items:
            tidsets.setdefault(x, set()).add(t)
    for k in range(3, kmax + 1):
        prev = set(levels[-1][1])
        byprefix = {}
        for s in prev:
            byprefix.setdefault(s[:-1], []).append(s[-1])
        cands = set()
        for pre, lasts in byprefix.items():
            lasts.sort()
            for i in range(len(lasts)):
                for j in range(i + 1, len(lasts)):
                    c = pre + (lasts[i], lasts[j])
                    if all(c[:m] + c[m + 1:] in prev for m in range(k)):
                        cands.add(c)
        sup = {}
        for c in cands:
            ts = set.intersection(*(tidsets[x] for x in c))
            if ts:
                sup[c] = len(ts)
        levels.append((sup, {s: v for s, v in sup.items() if v >= minsup}, cands))
    return levels

def cand_patterns(k, v):
    """Two frequent (k-1)-sets sharing the first k-2 items, last items ordered,
    and every other (k-1)-subset frequent (the Apriori prune)."""
    pre = v[:k - 2]
    x, y = v[k - 2], v[k - 1]
    pats = [(f"f{k-1}", pre + [x], '$n0'), (f"f{k-1}", pre + [y], '$n1')]
    prune = [(f"f{k-1}", v[:m] + v[m + 1:], f"$n{m + 2}") for m in range(k - 2)]
    return pats, (x, y), prune

def mm2_level(k, form):
    v = [f"$a{i}" for i in range(k)]
    vs = ' '.join(v)
    pats, (x, y), prune = cand_patterns(k, v)
    body = [f"({h} {' '.join(a)} {n})" for h, a, n in pats] + [f"(lt {x} {y})"] + [f"({h} {' '.join(a)} {n})" for h, a, n in prune]
    lines = [f"(exec ({k} 0) (, {' '.join(body)}) (, (c{k} {vs})))"]
    if form == 'horizontal':
        tx = [f"(has $t {v[0]})", f"(c{k} {vs})"] + [f"(has $t {u})" for u in v[1:]]
    else:
        tx = [f"(c{k} {vs})"] + [f"(hi {u} $t)" for u in v]
    lines.append(f"(exec ({k} 1) (, {' '.join(tx)}) (O (count (s{k} {vs} $n) $n $t)))")
    lines.append(f"(exec ({k} 2) (, (s{k} {vs} $n) (ge $n)) (, (f{k} {vs} $n)))")
    return lines

def keep(k, vs):
    return (f"(if (> $n 0) (let $_ (add-atom &r (s{k} {vs} $n)) "
            f"(if (>= $n MINSUP) (add-atom &r (f{k} {vs} $n)) (empty))) (empty))")

def metta_level(k, form):
    v = [f"$a{i}" for i in range(k)]
    vs = ' '.join(v)
    pats, (x, y), prune = cand_patterns(k, v)
    inner = f"(add-atom &c (c{k} {vs}))"
    for h, a, n in reversed(prune):
        inner = f"(match &r ({h} {' '.join(a)} {n}) {inner})"
    (h0, a0, n0), (h1, a1, n1) = pats
    gen = (f"(match &r ({h0} {' '.join(a0)} {n0}) (match &r ({h1} {' '.join(a1)} {n1}) "
           f"(if (< {x} {y}) {inner} (empty))))")
    out = [f"!(size-atom (collapse {gen}))"]
    if form == 'horizontal':
        # one witness per (candidate, transaction), from the transaction side; then one count per candidate
        body = f"(add-atom &w (w{k} {vs} $t))"
        for u in reversed(v[1:]):
            body = f"(match &self (has $t {u}) {body})"
        out.append(f"!(size-atom (collapse (match &self (has $t {v[0]}) (match &c (c{k} {vs}) {body}))))")
        out.append(f"!(size-atom (collapse (match &c (c{k} {vs}) (let $n (size-atom (collapse (match &w (w{k} {vs} $t) t))) {keep(k, vs)}))))")
    else:
        # the support of a candidate is the size of its items' tid-set intersection
        body = 't'
        for u in reversed(v[1:]):
            body = f"(match &v (hi {u} $t) {body})"
        out.append(f"!(size-atom (collapse (match &c (c{k} {vs}) (let $n (size-atom (collapse (match &v (hi {v[0]} $t) {body}))) {keep(k, vs)}))))")
    return out

MM2_FORMS = ('vertical', 'horizontal')
METTA_FORMS = ('vertical', 'vertical-all', 'horizontal')

def write_task(out, name, a, txs, minsup, mm2_form, metta_form, levels):
    K = a.kmax
    nhas = sum(len(t) for t in txs)
    has = '\n'.join(f"(has {t} {x})" for t, items in enumerate(txs) for x in items)
    lt = '\n'.join(' '.join(f"(lt {i} {j})" for j in range(i + 1, a.items)) for i in range(a.items - 1))
    ge = ' '.join(f"(ge {n})" for n in range(minsup, a.T + 1))
    mm2 = [f"; {name}: Apriori over {a.T} transactions ({nhas} item occurrences, {a.items} items), minimum support {minsup}",
           has, lt, ge]
    if mm2_form == 'vertical':
        mm2 += ["; the item-first view: an item's transactions under one prefix",
                "(exec (0 0) (, (has $t $a)) (, (hi $a $t)))"]
    mm2 += ["; level 1: transactions per item",
            "(exec (1 1) (, (has $t $a0)) (O (count (s1 $a0 $n) $n $t)))",
            "(exec (1 2) (, (s1 $a0 $n) (ge $n)) (, (f1 $a0 $n)))",
            "; level 2: every co-occurring pair of frequent items, from the transaction side",
            "(exec (2 1) (, (has $t $a0) (f1 $a0 $n0) (has $t $a1) (lt $a0 $a1) (f1 $a1 $n1)) (O (count (s2 $a0 $a1 $n) $n $t)))",
            "(exec (2 2) (, (s2 $a0 $a1 $n) (ge $n)) (, (f2 $a0 $a1 $n)))",
            "; level k >= 3: candidates by prefix join and prune, then their supports"
            + (" as tid-set intersections" if mm2_form == 'vertical' else " through the candidates' prefix tree")]
    for k in range(3, K + 1):
        mm2.extend(mm2_level(k, mm2_form))
    (out / f'{name}.mm2').write_text('\n'.join(mm2) + '\n')
    metta = [f"; {name}: the same Apriori in MeTTa (PeTTa dialect)", has,
             "!(bind! &r (new-space set))   ; supports and frequent itemsets",
             "!(bind! &c (new-space set))   ; candidates",
             "!(bind! &w (new-space set))   ; (itemset, transaction) witnesses",
             "!(bind! &g (new-space set))   ; groups"]
    if metta_form in ('vertical', 'vertical-all'):
        metta += ["!(bind! &v (new-space set))   ; the item-first view",
                  "!(size-atom (collapse (match &self (has $t $a) (let $_ (add-atom &v (hi $a $t)) (add-atom &g (g1 $a))))))",
                  "; level 1: the size of every item's tid set",
                  "!(size-atom (collapse (match &g (g1 $a0) (let $n (size-atom (collapse (match &v (hi $a0 $t) t))) "
                  "(let $_ (add-atom &r (s1 $a0 $n)) (if (>= $n MINSUP) (add-atom &r (f1 $a0 $n)) (empty)))))))"]
    else:
        metta += ["; level 1: one witness per (item, transaction); one count per item",
                  "!(size-atom (collapse (match &self (has $t $a0) (let $_ (add-atom &w (w1 $a0 $t)) (add-atom &g (g1 $a0))))))",
                  "!(size-atom (collapse (match &g (g1 $a0) (let $n (size-atom (collapse (match &w (w1 $a0 $t) t))) "
                  "(let $_ (add-atom &r (s1 $a0 $n)) (if (>= $n MINSUP) (add-atom &r (f1 $a0 $n)) (empty)))))))"]
    if metta_form == 'vertical-all':
        metta += ["; level 2: every pair of frequent items, its tid-set intersection",
                  "!(size-atom (collapse (match &r (f1 $a0 $n0) (match &r (f1 $a1 $n1) (if (< $a0 $a1) "
                  "(let $n (size-atom (collapse (match &v (hi $a0 $t) (match &v (hi $a1 $t) t)))) " + keep(2, '$a0 $a1') + ") (empty))))))"]
    else:
        metta += ["; level 2: every co-occurring pair of frequent items, from the transaction side",
                  "!(size-atom (collapse (match &self (has $t $a0) (match &r (f1 $a0 $n0) (match &self (has $t $a1) "
                  "(if (< $a0 $a1) (match &r (f1 $a1 $n1) (let $_ (add-atom &w (w2 $a0 $a1 $t)) (add-atom &g (g2 $a0 $a1)))) (empty)))))))",
                  "!(size-atom (collapse (match &g (g2 $a0 $a1) (let $n (size-atom (collapse (match &w (w2 $a0 $a1 $t) t))) "
                  "(let $_ (add-atom &r (s2 $a0 $a1 $n)) (if (>= $n MINSUP) (add-atom &r (f2 $a0 $a1 $n)) (empty)))))))"]
    for k in range(3, K + 1):
        metta.append(f"; level {k}")
        metta.extend(metta_level(k, 'horizontal' if metta_form == 'horizontal' else 'vertical'))
    heads = [f"s{k}" for k in range(1, K + 1)] + [f"f{k}" for k in range(1, K + 1)]
    for k in range(1, K + 1):
        vs = ' '.join(f"$a{i}" for i in range(k))
        metta.append(f"!(match &r (f{k} {vs} $n) (f{k} {vs} $n))")
    for h in heads:
        k = int(h[1:])
        vs = ' '.join(f"$a{i}" for i in range(k))
        metta.append(f"!(size-atom (collapse (match &r ({h} {vs} $n) t)))")
    (out / f'{name}.metta').write_text('\n'.join(metta).replace('MINSUP', str(minsup)) + '\n')
    (out / f'{name}.heads').write_text(' '.join(heads) + '\n')
    (out / f'{name}.values').write_text(' '.join(f"f{k}" for k in range(1, K + 1)) + '\n')
    exp, vals = {}, {}
    for k in range(1, K + 1):
        sup, freq = levels[k - 1][0], levels[k - 1][1]
        exp[f"s{k}"] = len(sup)
        exp[f"f{k}"] = len(freq)
        vals[f"f{k}"] = sorted(f"(f{k} {' '.join(map(str, s))} {n})" for s, n in freq.items())
    exp['has'] = nhas
    (out / f'{name}.expect.json').write_text(json.dumps(exp))
    (out / f'{name}.oracle_values.json').write_text(json.dumps(vals))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('T', type=int)
    ap.add_argument('--items', type=int, default=300)
    ap.add_argument('--minsup-frac', type=float, default=0.01)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--kmax', type=int, default=8)
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    ap.add_argument('--mm2-form', choices=MM2_FORMS, default='vertical')
    ap.add_argument('--metta-form', choices=METTA_FORMS, default='vertical-all')
    ap.add_argument('--suffix', default='', help='appended to the task name (for variants)')
    ap.add_argument('--stats', action='store_true', help='print level sizes only')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    txs = quest(a.T, a.items, rng)
    minsup = max(2, math.ceil(a.minsup_frac * a.T))
    levels = apriori(txs, minsup, a.kmax)
    nhas = sum(len(t) for t in txs)
    stats = [(k + 1, len(lv[0]), len(lv[1])) for k, lv in enumerate(levels)]
    print(f"T={a.T} items={a.items} has={nhas} avg={nhas / a.T:.2f} minsup={minsup}",
          ' '.join(f"L{k}:{c}/{f}" for k, c, f in stats),
          ' '.join(f"C{k}:{len(lv[2])}" for k, lv in enumerate(levels, start=1) if len(lv) > 2))
    if a.stats:
        return
    name = f'apriori_T{a.T}{a.suffix}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    write_task(out, name, a, txs, minsup, a.mm2_form, a.metta_form, levels)

if __name__ == '__main__':
    main()
