#!/usr/bin/env python3
"""k-cliques of a random graph as one conjunctive query: MORK's own `clique` benchmark.

MORK's default benchmark set includes `clique` (kernel/src/main.rs,
bench_clique_no_unify): a random graph with n nodes and m edges, each edge
stored once as (edge i j) with i < j (the graph oriented by node order, so
every clique is found once), and for k = 3, 4, 5 the query

    (exec 0 (, (edge $x0 $x1) (edge $x0 $x2) ... (edge $x(k-2) $x(k-1)))
            (, (k-clique $x0 ... $x(k-1))))

a self-join of the edge relation with k(k-1)/2 conjuncts: a triangle query at
k = 3, six conjuncts at k = 4, ten at k = 5.  This is the textbook case for
worst-case-optimal joins (Ngo, Porat, Re, Rudra; Veldhuizen's leapfrog
triejoin), and the reason MORK has an experimental `leapfrog` build.  MORK's
benchmark runs n = 200, m = 3600 (7,824 triangles, 2,320 4-cliques, 102
5-cliques in its own graph); the generator here draws its own graph with the
same parameters, and scales n and m.

Outputs: (c3 ...), (c4 ...), (c5 ...) for every clique, counted per relation.
The oracle counts cliques by adjacency-set intersection in Python.

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.expect.json.
usage: gen_clique.py N M [--kmax 5] [--seed 0] [--mm2-order lex|early] [--metta-order lex|early]
"""
import argparse, json, random
from pathlib import Path

def graph(n, m, rng):
    edges = set()
    while len(edges) < m:
        i, j = rng.randrange(n), rng.randrange(n)
        if i != j:
            edges.add((min(i, j), max(i, j)))
    return sorted(edges)

def cliques(n, edges, kmax):
    out = {u: set() for u in range(n)}
    for i, j in edges:
        out[i].add(j)
    counts = {}
    def extend(clique, cands, k):
        if len(clique) == k:
            counts[k] = counts.get(k, 0) + 1
            return
        for v in sorted(cands):
            extend(clique + [v], cands & out[v], k)
    for k in range(3, kmax + 1):
        counts[k] = 0
        for u in range(n):
            extend([u], out[u], k)
    return counts

def conj(k, order='lex'):
    """lex: MORK's benchmark order, (0,1) (0,2) .. (0,k-1) (1,2) ..;
    early: each new vertex's edges to all earlier vertices right after it is introduced."""
    if order == 'lex':
        return [(i, j) for i in range(k) for j in range(i + 1, k)]
    return [(i, j) for j in range(1, k) for i in range(j)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('N', type=int)
    ap.add_argument('M', type=int)
    ap.add_argument('--kmax', type=int, default=5)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--mm2-order', choices=('lex', 'early'), default='lex')
    ap.add_argument('--metta-order', choices=('lex', 'early'), default='lex')
    ap.add_argument('--suffix', default='')
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    a = ap.parse_args()
    rng = random.Random(a.seed)
    edges = graph(a.N, a.M, rng)
    counts = cliques(a.N, edges, a.kmax)
    name = f'clique_n{a.N}_m{a.M}_k{a.kmax}{a.suffix}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    facts = '\n'.join(f"(edge {i} {j})" for i, j in edges)
    mm2 = [f"; {name}: k-cliques for k = 3..{a.kmax} of a random graph, {a.N} nodes, {len(edges)} edges (i < j)", facts]
    metta = [f"; {name}: the same clique queries in MeTTa (PeTTa dialect)", facts, "!(bind! &c (new-space set))"]
    for k in range(3, a.kmax + 1):
        xs = ' '.join(f"$x{i}" for i in range(k))
        pats = ' '.join(f"(edge $x{i} $x{j})" for i, j in conj(k, a.mm2_order))
        mm2.append(f"(exec {k} (, {pats}) (, (c{k} {xs})))")
        body = f"(add-atom &c (c{k} {xs}))"
        for i, j in reversed(conj(k, a.metta_order)):
            body = f"(match &self (edge $x{i} $x{j}) {body})"
        metta.append(f"!(size-atom (collapse {body}))")
    for k in range(3, a.kmax + 1):
        xs = ' '.join(f"$x{i}" for i in range(k))
        metta.append(f"!(size-atom (collapse (match &c (c{k} {xs}) t)))")
    (out / f'{name}.mm2').write_text('\n'.join(mm2) + '\n')
    (out / f'{name}.metta').write_text('\n'.join(metta) + '\n')
    (out / f'{name}.heads').write_text(' '.join(f"c{k}" for k in range(3, a.kmax + 1)) + '\n')
    (out / f'{name}.expect.json').write_text(json.dumps({f"c{k}": counts[k] for k in range(3, a.kmax + 1)}))
    print(name, 'edges', len(edges), {f"c{k}": counts[k] for k in range(3, a.kmax + 1)})

if __name__ == '__main__':
    main()
