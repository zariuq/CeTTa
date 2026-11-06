#!/usr/bin/env python3
"""Fuzzy matching through masked views: the Hamming graph of sequencing k-mers.

Approximate matching by exact prefix search over rotated or masked views
(the masked-key form of the pigeonhole method): two strings of length k
differ in at most one position exactly when they agree on one of the k
masked keys, the string with position i replaced by a wildcard.  Grouping
the k views by masked key and pairing the members of each group finds every
pair at Hamming distance 1 without comparing all pairs.  Sequencing error
correctors build this Hamming graph of k-mers to cluster erroneous k-mers
with the true ones (Hammer, Medvedev et al. 2011; BayesHammer, Nikolenko et
al. 2013).

Data: a random reference genome; reads are k-mer windows of it at random
positions, a third of them carrying one substitution error, so erroneous
k-mers sit at distance 1 from true ones.  The input is the set of distinct
read k-mers, (km (b1 .. bk)), bases A C G T.

The program
  1. writes the k masked views, (mv KEY b X): KEY is X with one base
     replaced by N (the IUPAC any-base code; its place names the view), b is
     the masked base;
  2. joins the views with themselves on KEY, keeping pairs whose masked
     bases differ ((diff b1 b2), a 12-fact table): the Hamming-graph edges
     (e X Y), both directions;
  3. counts every k-mer's neighbours, (deg X n), and the degree
     distribution, (dd n count).

The oracle builds the same graph in Python with dictionaries.

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.values, NAME.expect.json,
NAME.oracle_values.json.
usage: gen_hamming.py READS [--k 12] [--genome 200000] [--error 0.33] [--seed 0]
"""
import argparse, json, random
from collections import Counter, defaultdict
from pathlib import Path

BASES = 'ACGT'

def reads(n, k, glen, err, rng):
    g = ''.join(rng.choice(BASES) for _ in range(glen))
    out = set()
    for _ in range(n):
        p = rng.randrange(glen - k + 1)
        w = list(g[p:p + k])
        if rng.random() < err:
            i = rng.randrange(k)
            w[i] = rng.choice([b for b in BASES if b != w[i]])
        out.add(''.join(w))
    return sorted(out)

def hamming_graph(kmers, k):
    groups = defaultdict(list)
    for x in kmers:
        for i in range(k):
            groups[(i, x[:i] + 'N' + x[i + 1:])].append(x)
    edges = set()
    for xs in groups.values():
        for x in xs:
            for y in xs:
                if x != y:
                    edges.add((x, y))
    deg = Counter(x for x, _ in edges)
    dd = Counter(deg.values())
    return edges, deg, dd

def tup(x):
    return '(' + ' '.join(x) + ')'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('READS', type=int)
    ap.add_argument('--k', type=int, default=12)
    ap.add_argument('--genome', type=int, default=200000)
    ap.add_argument('--error', type=float, default=0.33)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    a = ap.parse_args()
    rng = random.Random(a.seed)
    k = a.k
    kmers = reads(a.READS, k, a.genome, a.error, rng)
    edges, deg, dd = hamming_graph(kmers, k)
    name = f'hamming_r{a.READS}_k{k}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    facts = '\n'.join(f"(km {tup(x)})" for x in kmers)
    diff = ' '.join(f"(diff {p} {q})" for p in BASES for q in BASES if p != q)
    cs = [f"$c{i}" for i in range(k)]
    xs = tup(cs)
    views_tpl = ' '.join(f"(mv {tup(cs[:i] + ['N'] + cs[i + 1:])} {cs[i]} {xs})" for i in range(k))
    mm2 = f"""; {name}: the Hamming graph of {len(kmers)} distinct {k}-mers, through {k} masked views
{facts}
{diff}
; the masked views: one base replaced by N, the masked base and the k-mer kept
(exec (0 0) (, (km {xs})) (, {views_tpl}))
; pairs in one view group whose masked bases differ: the edges, both directions
(exec (1 0) (, (mv $key $b1 $x) (mv $key $b2 $y) (diff $b1 $b2)) (, (e $x $y)))
; neighbours per k-mer, and the degree distribution
(exec (2 0) (, (e $x $y)) (O (count (deg $x $n) $n $y)))
(exec (3 0) (, (deg $x $n)) (O (count (dd $n $c) $c $x)))
"""
    (out / f'{name}.mm2').write_text(mm2)
    adds = ' '.join(f"(add-atom &v (mv {tup(cs[:i] + ['N'] + cs[i + 1:])} {cs[i]} {xs}))" for i in range(k))
    metta = f"""; {name}: the same masked-view join in MeTTa (PeTTa dialect)
{facts}
!(bind! &v (new-space set))   ; masked views
!(bind! &e (new-space set))   ; edges
!(bind! &d (new-space set))   ; degrees
!(bind! &h (new-space set))   ; degree distribution
!(size-atom (collapse (match &self (km {xs}) (progn {adds}))))
!(size-atom (collapse (match &v (mv $key $b1 $x) (match &v (mv $key $b2 $y) (if (== $b1 $b2) (empty) (add-atom &e (e $x $y)))))))
!(size-atom (collapse (match &self (km $x) (let $n (size-atom (collapse (match &e (e $x $y) t))) (if (> $n 0) (add-atom &d (deg $x $n)) (empty))))))
!(size-atom (collapse (match &d (deg $x $n) (add-atom &h (dg $n)))))
!(size-atom (collapse (match &h (dg $n) (let $c (size-atom (collapse (match &d (deg $x $n) t))) (add-atom &h (dd $n $c))))))
!(match &h (dd $n $c) (dd $n $c))
!(size-atom (collapse (match &v (mv $key $b $x) t)))
!(size-atom (collapse (match &e (e $x $y) t)))
!(size-atom (collapse (match &d (deg $x $n) t)))
!(size-atom (collapse (match &h (dd $n $c) t)))
"""
    (out / f'{name}.metta').write_text(metta)
    (out / f'{name}.heads').write_text('mv e deg dd\n')
    (out / f'{name}.values').write_text('dd\n')
    (out / f'{name}.expect.json').write_text(json.dumps({'mv': k * len(kmers), 'e': len(edges), 'deg': len(deg), 'dd': len(dd),
                                                        'kmers': len(kmers)}))
    (out / f'{name}.oracle_values.json').write_text(json.dumps({'dd': sorted(f"(dd {d} {c})" for d, c in dd.items())}))
    print(name, 'kmers', len(kmers), 'view facts', k * len(kmers), 'edges (both directions)', len(edges),
          'k-mers with neighbours', len(deg), 'degree distribution', sorted(dd.items()))

if __name__ == '__main__':
    main()
