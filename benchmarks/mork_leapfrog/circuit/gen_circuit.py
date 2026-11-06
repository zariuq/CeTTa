#!/usr/bin/env python3
"""Circuits: simulation signatures for equivalence checking (the SAT-sweeping front end).

Combinational equivalence checkers (ABC's fraig/&cec and their descendants)
start the same way: simulate the miter of the two circuits on random input
vectors, give every node its signature (the vector of its values), and group
nodes by signature.  Nodes with equal signatures are candidate equivalences,
handed to a SAT solver one class at a time; nodes with different signatures
are certainly different.  The grouping is where most nodes are settled.

Here the miter is two n-bit adders over the same inputs a0..a(n-1),
b0..b(n-1): a ripple-carry adder and a Kogge-Stone parallel-prefix adder,
both as And-Inverter Graphs (AND gates with optionally complemented inputs).
Their carries and sums agree bit for bit, and so do many internal signals,
so the classes are large and structured.

The program
  1. simulates 32 random vectors level by level: a gate's output bit for a
     vector is looked up in a stored truth table (tt x pa y pb z) and written
     as (v NODE VECTOR BIT);
  2. assembles each node's signature as one structured key, two 16-bit
     tuples, by a 33-way join: (sig ((b0 .. b15) (b16 .. b31)) NODE);
  3. counts the nodes of every signature class: (class SIGNATURE COUNT).

Size knob: the adder width n (about 9n + 4n log2 n gates; facts about 32
per node).

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.values, NAME.expect.json.
usage: gen_circuit.py N [--seed 0]
"""
import argparse, json, random
from collections import Counter
from pathlib import Path

V = 32

class AIG:
    def __init__(self):
        self.gates = []          # (id, a, pa, b, pb)
        self.level = {}
        self.pis = []
    def pi(self, name):
        self.pis.append(name); self.level[name] = 0
        return (name, 0)
    def AND(self, x, y):
        nid = f"n{len(self.gates)}"
        self.gates.append((nid, x[0], x[1], y[0], y[1]))
        self.level[nid] = 1 + max(self.level[x[0]], self.level[y[0]])
        return (nid, 0)
    def NOT(self, x):
        return (x[0], 1 - x[1])
    def OR(self, x, y):
        return self.NOT(self.AND(self.NOT(x), self.NOT(y)))
    def XOR(self, x, y):
        return self.OR(self.AND(x, self.NOT(y)), self.AND(self.NOT(x), y))

def ripple(g, a, b):
    s, c = [], None
    for i in range(len(a)):
        t = g.XOR(a[i], b[i])
        if c is None:
            s.append(t); c = g.AND(a[i], b[i])
        else:
            s.append(g.XOR(t, c))
            c = g.OR(g.AND(a[i], b[i]), g.AND(t, c))
    return s + [c]

def kogge_stone(g, a, b):
    n = len(a)
    G = [g.AND(a[i], b[i]) for i in range(n)]
    P = [g.XOR(a[i], b[i]) for i in range(n)]
    p0 = list(P)
    d = 1
    while d < n:
        G2, P2 = list(G), list(P)
        for i in range(d, n):
            G2[i] = g.OR(G[i], g.AND(P[i], G[i - d]))
            P2[i] = g.AND(P[i], P[i - d])
        G, P = G2, P2
        d *= 2
    s = [p0[0]] + [g.XOR(p0[i], G[i - 1]) for i in range(1, n)]
    return s + [G[n - 1]]

def simulate(g, inputs):
    val = dict(inputs)                       # node -> list of V bits
    for nid, a, pa, b, pb in sorted(g.gates, key=lambda x: g.level[x[0]]):
        val[nid] = [(val[a][k] ^ pa) & (val[b][k] ^ pb) for k in range(V)]
    return val

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('N', type=int)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    a = ap.parse_args()
    n = a.N
    rng = random.Random(a.seed)
    g = AIG()
    A = [g.pi(f"a{i}") for i in range(n)]
    B = [g.pi(f"b{i}") for i in range(n)]
    ripple(g, A, B)
    kogge_stone(g, A, B)
    inputs = {p: [rng.randrange(2) for _ in range(V)] for p in g.pis}
    val = simulate(g, inputs)
    nodes = list(g.pis) + [x[0] for x in g.gates]
    sigs = Counter(tuple(val[nd]) for nd in nodes)
    maxl = max(g.level.values())
    def sigtxt(bits):
        return f"(({' '.join(map(str, bits[:16]))}) ({' '.join(map(str, bits[16:]))}))"
    name = f'circuit_add{n}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    tt = ' '.join(f"(tt {x} {pa} {y} {pb} {(x ^ pa) & (y ^ pb)})" for x in (0, 1) for pa in (0, 1) for y in (0, 1) for pb in (0, 1))
    data = [tt,
            '\n'.join(f"(and {nid} {x} {px} {y} {py})" for nid, x, px, y, py in g.gates),
            '\n'.join(f"(lvl {g.level[nid]} {nid})" for nid, *_ in g.gates),
            ' '.join(f"(node {nd})" for nd in nodes),
            ' '.join(f"(next {l} {l + 1})" for l in range(maxl)),
            '\n'.join(f"(vin {p} {k} {inputs[p][k]})" for p in g.pis for k in range(V))]
    data_txt = '\n'.join(data) + '\n'
    bits = [f"$b{k}" for k in range(V)]
    conj = ' '.join(f"(v $n {k} {bits[k]})" for k in range(V))
    sigpat = f"(({' '.join(bits[:16])}) ({' '.join(bits[16:])}))"
    mm2 = f"""; {name}: simulation signatures of a ripple-carry / Kogge-Stone adder miter, {len(g.gates)} AND gates, {V} vectors
{data_txt}
(exec (0 0 0) (, (vin $p $k $x)) (, (v $p $k $x)))
; simulate level by level through the stored truth table
(exec (1 0 c) (, (exec (1 $l c) $ps $ts) (next $l $l1))
              (, (exec (1 $l1 c) $ps $ts)
                 (exec (0 $l1 a) (, (lvl $l1 $n) (and $n $a $pa $b $pb) (v $a $k $x) (v $b $k $y) (tt $x $pa $y $pb $z)) (, (v $n $k $z)))))
; each node's signature as one key, then the size of every signature class
(exec (2 0 0) (, (node $n) {conj}) (, (sig {sigpat} $n)))
(exec (2 1 0) (, (sig $s $n)) (O (count (class $s $c) $c $n)))
"""
    (out / f'{name}.mm2').write_text(mm2)
    nested = 'ADD'
    for k in reversed(range(V)):
        nested = f"(match &t (v $n {k} {bits[k]}) {nested})"
    nested = nested.replace('ADD', f"(add-atom &t (sig {sigpat} $n))")
    metta = f"""; {name}: the same simulation and signature classes in MeTTa (PeTTa dialect)
{data_txt}
!(bind! &t (new-space set))
!(size-atom (collapse (match &self (vin $p $k $x) (add-atom &t (v $p $k $x)))))
(= (sim-level $l) (match &self (lvl $l $n) (match &self (and $n $a $pa $b $pb)
     (match &t (v $a $k $x) (match &t (v $b $k $y) (match &self (tt $x $pa $y $pb $z) (add-atom &t (v $n $k $z))))))))
(= (simulate $l $L) (if (> $l $L) done (let* (($n (size-atom (collapse (sim-level $l)))) ($l1 (+ $l 1))) (simulate $l1 $L))))
!(simulate 1 {maxl})
!(size-atom (collapse (match &self (node $n) {nested})))
!(bind! &g (new-space set))
!(size-atom (collapse (match &t (sig $s $n) (add-atom &g (cls $s)))))
!(size-atom (collapse (match &g (cls $s) (let $c (size-atom (collapse (match &t (sig $s $n) t))) (add-atom &g (class $s $c))))))
!(match &g (class $s $c) (class $s $c))
!(size-atom (collapse (match &t (v $a $b $c) t)))
!(size-atom (collapse (match &t (sig $a $b) t)))
!(size-atom (collapse (match &g (class $a $b) t)))
"""
    (out / f'{name}.metta').write_text(metta)
    (out / f'{name}.heads').write_text('v sig class\n')
    (out / f'{name}.values').write_text('class\n')
    (out / f'{name}.expect.json').write_text(json.dumps({'v': len(nodes) * V, 'sig': len(nodes), 'class': len(sigs)}))
    (out / f'{name}.oracle_values.json').write_text(json.dumps(
        {'class': sorted(f"(class {sigtxt(list(s))} {c})" for s, c in sigs.items())}))
    big = sum(1 for c in sigs.values() if c > 1)
    print(name, 'gates', len(g.gates), 'nodes', len(nodes), 'levels', maxl, 'v facts', len(nodes) * V,
          'classes', len(sigs), 'classes with >1 node', big)

if __name__ == '__main__':
    main()
