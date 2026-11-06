#!/usr/bin/env python3
"""CTL model checking: explicit-state labelling over a Kripke structure.

The model is Dijkstra's dining philosophers: N philosophers around a table,
one fork between each pair of neighbours.  Philosopher i thinks (t), gets
hungry (h), picks up its left fork (l), then its right fork and eats (e),
then puts both down.  Interleaving semantics; the reachable states are found
here by breadth-first search and numbered in BFS order (state 0 is the
initial state, everyone thinking).  The state where every philosopher holds
its left fork is a deadlock; it gets a self-loop so the transition relation is
total, and the label `dead`.  States: 7,269 / 25,889 / 92,205 / 328,393 /
1,169,589 / 4,165,553 for N = 7..12, with about 6-10 transitions per state.

The program checks a battery of CTL properties about philosophers 0 and 1
(neighbours sharing fork 1): deadlock reachability, mutual exclusion,
starvation freedom, home-state reachability, until-properties, a nested
next-state property that holds nowhere.  The derived operators are unfolded
into the core set {atom, true, not, and, or, EX, EU, EG}, as the CTL example
in MORK's wiki does (AX p = not EX not p, EF p = E[true U p],
AF p = not EG not p, AG p = not E[true U not p],
A[p U q] = not E[not q U (not p and not q)] and not EG not q), and equal
subformulas are shared.  Each core subformula's satisfaction set is computed
bottom-up:

  atom   the label index                       (lab P S), label-first
  not    complement: all states, minus the operand's set (batched subtraction)
  and    intersection: a two-way join on the state
  or     union
  EX     pre-image: a join with the reverse transition view (rt S2 S)
  EU     least fixpoint, semi-naive: a frontier is joined with rt and the
         left operand's set, candidates already in the set are subtracted in
         one batch, the rest are promoted and become the next frontier
  EG     greatest fixpoint, by removal frontiers: states of the operand's set
         without a successor in the set are removed; the predecessors of
         removed states are rechecked (a three-way join with t and the set)

The compared output is the size of every subformula's set, (nsat F N), and
whether the initial state satisfies each property of the battery, (holds F).
The oracle evaluates the same subformula DAG in Python, and checks each
property again with the derived operators' own fixpoint characterisations.

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.values, NAME.expect.json,
NAME.oracle_values.json.
usage: gen_ctl.py N [--outdir DIR]
"""
import argparse, json
from collections import deque
from pathlib import Path

# ---------------------------------------------------------------- the model

def dp_successors(st, n):
    """Local states: 0 thinking, 1 hungry, 2 holds left fork, 3 eating.
    Philosopher i's left fork is fork i, its right fork is fork (i+1) mod n."""
    held = [False] * n
    for i, s in enumerate(st):
        if s == 2:
            held[i] = True
        elif s == 3:
            held[i] = True
            held[(i + 1) % n] = True
    out = []
    for i, s in enumerate(st):
        if s == 0:
            out.append(st[:i] + (1,) + st[i + 1:])
        elif s == 1 and not held[i]:
            out.append(st[:i] + (2,) + st[i + 1:])
        elif s == 2 and not held[(i + 1) % n]:
            out.append(st[:i] + (3,) + st[i + 1:])
        elif s == 3:
            out.append(st[:i] + (0,) + st[i + 1:])
    return out

def dining_philosophers(n):
    init = (0,) * n
    ids = {init: 0}
    order = [init]
    q = deque([init])
    trans = []
    dead = set()
    while q:
        s = q.popleft()
        i = ids[s]
        ss = dp_successors(s, n)
        if not ss:
            dead.add(i)
            trans.append((i, i))
        for t in ss:
            if t not in ids:
                ids[t] = len(order)
                order.append(t)
                q.append(t)
            trans.append((i, ids[t]))
    trans = sorted(set(trans))
    return order, trans, dead

LOCAL = 'thle'   # thinking, hungry, left fork held, eating

def labels(order, dead, phils):
    """(proposition, state) pairs for the propositions the battery uses."""
    lab = []
    for i, st in enumerate(order):
        for p in phils:
            lab.append((f"{LOCAL[st[p]]}{p}", i))
    for i in sorted(dead):
        lab.append(('dead', i))
    return lab

# ---------------------------------------------------------------- formulas

def A(p): return ('ap', p)
TT = ('tt',)
def NOT(f): return ('not', f)
def AND(f, g): return ('and', f, g)
def OR(f, g): return ('or', f, g)
def IMP(f, g): return ('imp', f, g)
def EX(f): return ('ex', f)
def AX(f): return ('ax', f)
def EF(f): return ('ef', f)
def AF(f): return ('af', f)
def EG(f): return ('eg', f)
def AG(f): return ('ag', f)
def EU(f, g): return ('eu', f, g)
def AU(f, g): return ('au', f, g)

BATTERY = [
    ('deadlock_reachable', EF(A('dead'))),
    ('neighbours_exclusive', AG(NOT(AND(A('e0'), A('e1'))))),
    ('no_starvation', AG(IMP(A('h0'), AF(A('e0'))))),
    ('home_state', AG(EF(A('t0')))),
    ('may_starve', EG(NOT(A('e0')))),
    ('zero_before_one', EU(NOT(A('e1')), A('e0'))),
    ('eats_before_neighbour', AU(NOT(A('e1')), A('e0'))),
    ('left_then_eat', AG(IMP(A('l0'), EF(A('e0'))))),
    ('eat_then_neighbour', EF(AND(A('e0'), EX(A('e1'))))),
    ('next_next_hungry', AX(AX(OR(A('h0'), A('t0'))))),
]

def desugar(f):
    k = f[0]
    if k in ('ap', 'tt'):
        return f
    if k == 'not':
        g = desugar(f[1])
        return g[1] if g[0] == 'not' else ('not', g)
    if k in ('and', 'or', 'eu'):
        return (k, desugar(f[1]), desugar(f[2]))
    if k in ('ex', 'eg'):
        return (k, desugar(f[1]))
    if k == 'imp':
        return desugar(OR(NOT(f[1]), f[2]))
    if k == 'ax':
        return desugar(NOT(EX(NOT(f[1]))))
    if k == 'ef':
        return desugar(EU(TT, f[1]))
    if k == 'af':
        return desugar(NOT(EG(NOT(f[1]))))
    if k == 'ag':
        return desugar(NOT(EU(TT, NOT(f[1]))))
    if k == 'au':
        p, q = f[1], f[2]
        return desugar(AND(NOT(EU(NOT(q), AND(NOT(p), NOT(q)))), NOT(EG(NOT(q)))))
    raise ValueError(k)

class DAG:
    """Core subformulas, shared, numbered so that operands come first."""
    def __init__(self):
        self.ids = {}
        self.nodes = []
    def add(self, f):
        if f in self.ids:
            return self.ids[f]
        args = [self.add(g) for g in f[1:] if isinstance(g, tuple)]
        node = (f[0],) + tuple(args) + tuple(g for g in f[1:] if not isinstance(g, tuple))
        fid = f"f{len(self.nodes) + 1:03d}"
        self.ids[f] = fid
        self.nodes.append((fid, node))
        return fid

# ---------------------------------------------------------------- the oracle

def evaluate(dag, nstates, trans, lab):
    succ = [[] for _ in range(nstates)]
    pred = [[] for _ in range(nstates)]
    for a, b in trans:
        succ[a].append(b)
        pred[b].append(a)
    bylab = {}
    for p, s in lab:
        bylab.setdefault(p, set()).add(s)
    allst = set(range(nstates))
    sat = {}
    for fid, node in dag.nodes:
        k = node[0]
        if k == 'ap':
            r = set(bylab.get(node[1], ()))
        elif k == 'tt':
            r = set(allst)
        elif k == 'not':
            r = allst - sat[node[1]]
        elif k == 'and':
            r = sat[node[1]] & sat[node[2]]
        elif k == 'or':
            r = sat[node[1]] | sat[node[2]]
        elif k == 'ex':
            r = {a for b in sat[node[1]] for a in pred[b]}
        elif k == 'eu':
            left = sat[node[1]]
            r = set(sat[node[2]])
            todo = deque(r)
            while todo:
                b = todo.popleft()
                for a in pred[b]:
                    if a in left and a not in r:
                        r.add(a)
                        todo.append(a)
        elif k == 'eg':
            r = set(sat[node[1]])
            cnt = {s: sum(1 for b in succ[s] if b in r) for s in r}
            todo = deque(s for s in r if cnt[s] == 0)
            while todo:
                s = todo.popleft()
                if s not in r:
                    continue
                r.discard(s)
                for a in pred[s]:
                    if a in r:
                        cnt[a] -= 1
                        if cnt[a] == 0:
                            todo.append(a)
        else:
            raise ValueError(k)
        sat[fid] = r
    return sat, succ, pred

def direct(f, nstates, succ, pred, bylab):
    """Each operator by its own fixpoint characterisation (no unfolding)."""
    allst = set(range(nstates))
    def pre_e(z):
        return {a for b in z for a in pred[b]}
    def pre_a(z):
        return {s for s in allst if all(b in z for b in succ[s])}
    def lfp(step):
        z = set()
        while True:
            z2 = step(z)
            if z2 == z:
                return z
            z = z2
    def gfp(step):
        z = set(allst)
        while True:
            z2 = step(z)
            if z2 == z:
                return z
            z = z2
    def go(f):
        k = f[0]
        if k == 'ap': return set(bylab.get(f[1], ()))
        if k == 'tt': return set(allst)
        if k == 'not': return allst - go(f[1])
        if k == 'and': return go(f[1]) & go(f[2])
        if k == 'or': return go(f[1]) | go(f[2])
        if k == 'imp': return (allst - go(f[1])) | go(f[2])
        if k == 'ex': return pre_e(go(f[1]))
        if k == 'ax': return pre_a(go(f[1]))
        if k == 'ef':
            p = go(f[1]); return lfp(lambda z: p | pre_e(z))
        if k == 'af':
            p = go(f[1]); return lfp(lambda z: p | pre_a(z))
        if k == 'eg':
            p = go(f[1]); return gfp(lambda z: p & pre_e(z))
        if k == 'ag':
            p = go(f[1]); return gfp(lambda z: p & pre_a(z))
        if k == 'eu':
            p, q = go(f[1]), go(f[2]); return lfp(lambda z: q | (p & pre_e(z)))
        if k == 'au':
            p, q = go(f[1]), go(f[2]); return lfp(lambda z: q | (p & pre_a(z)))
        raise ValueError(k)
    return go(f)

# ---------------------------------------------------------------- programs

def mm2_execs(fid, node, idx):
    """MM2 for one core subformula; locations (IDX PHASE K X) keep every
    exec of subformula IDX (its fixpoint rounds included) before IDX+1."""
    I = idx
    k = node[0]
    if k == 'ap':
        return [f"(exec ({I} 0 0 a) (, (lab {node[1]} $x)) (, (sat {fid} $x)))"]
    if k == 'tt':
        return [f"(exec ({I} 0 0 a) (, (st $x)) (, (sat {fid} $x)))"]
    if k == 'not':
        a = node[1]
        return [f"(exec ({I} 0 0 a) (, (st $x)) (, (sat {fid} $x)))",
                f"(exec ({I} 0 0 b) (, (sat {a} $x)) (O (- (sat {fid} $x))))"]
    if k == 'and':
        a, b = node[1], node[2]
        return [f"(exec ({I} 0 0 a) (, (sat {a} $x) (sat {b} $x)) (, (sat {fid} $x)))"]
    if k == 'or':
        a, b = node[1], node[2]
        return [f"(exec ({I} 0 0 a) (, (sat {a} $x)) (, (sat {fid} $x)))",
                f"(exec ({I} 0 0 b) (, (sat {b} $x)) (, (sat {fid} $x)))"]
    if k == 'ex':
        a = node[1]
        return [f"(exec ({I} 0 0 a) (, (sat {a} $y) (rt $y $x)) (, (sat {fid} $x)))"]
    if k == 'eu':
        a, b = node[1], node[2]
        return [
            f"(exec ({I} 0 0 a) (, (sat {b} $x)) (, (sat {fid} $x) (fr {fid} 0 $x) (live {fid} 0)))",
            f"""(exec ({I} 2 0 c) (, (exec ({I} 2 $k c) $ps $ts) (live {fid} $k) (flip $k $k1))
     (, (exec ({I} 2 $k1 c) $ps $ts)
        (exec ({I} 1 $k1 a) (, (fr {fid} $k $y) (rt $y $x) (sat {a} $x)) (, (cd {fid} $x)))
        (exec ({I} 1 $k1 b) (, (cd {fid} $x) (sat {fid} $x)) (O (- (cd {fid} $x))))
        (exec ({I} 1 $k1 c) (, (cd {fid} $x)) (O (+ (sat {fid} $x)) (+ (fr {fid} $k1 $x)) (+ (live {fid} $k1)) (- (cd {fid} $x))))
        (exec ({I} 1 $k1 d) (, (fr {fid} $k $y)) (O (- (fr {fid} $k $y)) (- (live {fid} $k))))))"""]
    if k == 'eg':
        a = node[1]
        return [
            f"(exec ({I} 0 0 a) (, (sat {a} $x)) (, (sat {fid} $x) (ck {fid} $x)))",
            f"(exec ({I} 0 0 b) (, (ck {fid} $x) (t $x $y) (sat {fid} $y)) (O (- (ck {fid} $x))))",
            f"(exec ({I} 0 0 c) (, (ck {fid} $x)) (O (+ (rm {fid} 0 $x)) (+ (live {fid} 0)) (- (sat {fid} $x)) (- (ck {fid} $x))))",
            f"""(exec ({I} 2 0 c) (, (exec ({I} 2 $k c) $ps $ts) (live {fid} $k) (flip $k $k1))
     (, (exec ({I} 2 $k1 c) $ps $ts)
        (exec ({I} 1 $k1 a) (, (rm {fid} $k $y) (rt $y $x) (sat {fid} $x)) (, (ck {fid} $x)))
        (exec ({I} 1 $k1 b) (, (ck {fid} $x) (t $x $y) (sat {fid} $y)) (O (- (ck {fid} $x))))
        (exec ({I} 1 $k1 c) (, (ck {fid} $x)) (O (+ (rm {fid} $k1 $x)) (+ (live {fid} $k1)) (- (sat {fid} $x)) (- (ck {fid} $x))))
        (exec ({I} 1 $k1 d) (, (rm {fid} $k $y)) (O (- (rm {fid} $k $y)) (- (live {fid} $k))))))"""]
    raise ValueError(k)

METTA_LIB = """
; satisfaction sets live in &s as (sat F S); the reverse view and the state set in &g;
; the fixpoints keep their frontiers in &w
!(bind! &s (new-space set))
!(bind! &g (new-space set))
!(bind! &w (new-space set))
!(size-atom (collapse (match &self (t $x $y) (let $_ (add-atom &g (rt $y $x)) (add-atom &g (st $x))))))
(= (in $f $x) (not (== (collapse (once (match &s (sat $f $x) t))) ())))
(= (do-ap $f $p) (size-atom (collapse (match &self (lab $p $x) (add-atom &s (sat $f $x))))))
(= (do-tt $f) (size-atom (collapse (match &g (st $x) (add-atom &s (sat $f $x))))))
; complement: every state, then the operand's states removed
(= (do-not $f $a) (let $c (size-atom (collapse (match &g (st $x) (add-atom &s (sat $f $x)))))
   (size-atom (collapse (match &s (sat $a $x) (remove-atom &s (sat $f $x)))))))
(= (do-and $f $a $b) (size-atom (collapse (match &s (sat $a $x) (match &s (sat $b $x) (add-atom &s (sat $f $x)))))))
(= (do-or $f $a $b) (+ (size-atom (collapse (match &s (sat $a $x) (add-atom &s (sat $f $x)))))
                       (size-atom (collapse (match &s (sat $b $x) (add-atom &s (sat $f $x)))))))
(= (do-ex $f $a) (size-atom (collapse (match &s (sat $a $y) (match &g (rt $y $x) (add-atom &s (sat $f $x)))))))
(= (drop $f $k) (size-atom (collapse (match &w (fr $f $k $x) (remove-atom &w (fr $f $k $x))))))
; E[a U b]: least fixpoint, one frontier per round
(= (eu-round $f $a $k $k1)
   (size-atom (collapse (match &w (fr $f $k $y) (match &g (rt $y $x) (match &s (sat $a $x)
     (if (in $f $x) (empty) (let $_ (add-atom &s (sat $f $x)) (add-atom &w (fr $f $k1 $x))))))))))
(= (eu-loop $f $a $k)
   (let* (($k1 (+ $k 1)) ($n (eu-round $f $a $k $k1)) ($d (drop $f $k)))
     (if (== $n 0) $k1 (eu-loop $f $a $k1))))
(= (do-eu $f $a $b)
   (let $n (size-atom (collapse (match &s (sat $b $x) (let $_ (add-atom &s (sat $f $x)) (add-atom &w (fr $f 0 $x))))))
     (eu-loop $f $a 0)))
; EG a: greatest fixpoint; removed states are a frontier whose predecessors are rechecked
(= (has-succ $f $x) (not (== (collapse (once (match &self (t $x $y) (match &s (sat $f $y) t)))) ())))
(= (eg-check $f $k1 $x)
   (if (in $f $x) (if (has-succ $f $x) (empty) (let $_ (remove-atom &s (sat $f $x)) (add-atom &w (fr $f $k1 $x)))) (empty)))
(= (eg-round $f $k $k1)
   (size-atom (collapse (match &w (fr $f $k $y) (match &g (rt $y $x) (eg-check $f $k1 $x))))))
(= (eg-loop $f $k)
   (let* (($k1 (+ $k 1)) ($n (eg-round $f $k $k1)) ($d (drop $f $k)))
     (if (== $n 0) $k1 (eg-loop $f $k1))))
(= (do-eg $f $a)
   (let* (($c (size-atom (collapse (match &s (sat $a $x) (add-atom &s (sat $f $x))))))
          ($r (size-atom (collapse (match &s (sat $a $x) (eg-check $f 0 $x))))))
     (eg-loop $f 0)))
(= (sat-size $f) (size-atom (collapse (match &s (sat $f $x) t))))
"""

def metta_call(fid, node):
    k = node[0]
    if k == 'ap': return f"!(do-ap {fid} {node[1]})"
    if k == 'tt': return f"!(do-tt {fid})"
    if k in ('not', 'ex', 'eg'): return f"!(do-{k} {fid} {node[1]})"
    if k in ('and', 'or', 'eu'): return f"!(do-{k} {fid} {node[1]} {node[2]})"
    raise ValueError(k)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('N', type=int)
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    a = ap.parse_args()
    n = a.N
    order, trans, dead = dining_philosophers(n)
    lab = labels(order, dead, [0, 1])
    dag = DAG()
    tops = []
    for name, f in BATTERY:
        tops.append((name, f, dag.add(desugar(f))))
    sat, succ, pred = evaluate(dag, len(order), trans, lab)
    bylab = {}
    for p, s in lab:
        bylab.setdefault(p, set()).add(s)
    for name, f, fid in tops:
        d = direct(f, len(order), succ, pred, bylab)
        assert d == sat[fid], (name, len(d), len(sat[fid]))
    name = f'ctl_dp{n}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    facts = ('\n'.join(f"(t {x} {y})" for x, y in trans) + '\n'
             + '\n'.join(f"(lab {p} {s})" for p, s in sorted(lab)) + '\n'
             + "(init 0)\n" + ' '.join(f"(top {fid})" for _, _, fid in tops) + '\n')
    width = 3
    execs = []
    for i, (fid, node) in enumerate(dag.nodes, start=1):
        execs.extend(mm2_execs(fid, node, f"{i:0{width}d}"))
    battery_doc = '\n'.join(f";   {fid}  {nm}" for nm, _, fid in tops)
    mm2 = f"""; {name}: CTL model checking of {n} dining philosophers, {len(order)} states, {len(trans)} transitions
; properties checked, by the core subformula that answers each:
{battery_doc}
{facts}(flip 0 1) (flip 1 0)
; the reverse transition view and the state set
(exec ({'0' * width} 0 0 a) (, (t $x $y)) (, (rt $y $x) (st $x)))
; one block of execs per core subformula, operands first
{chr(10).join(execs)}
; the size of every satisfaction set, and the verdicts at the initial state
(exec ({'9' * width} 0 0 a) (, (sat $f $x)) (O (count (nsat $f $n) $n $x)))
(exec ({'9' * width} 0 0 b) (, (top $f) (init $x) (sat $f $x)) (, (holds $f)))
"""
    (out / f'{name}.mm2').write_text(mm2)
    calls = '\n'.join(metta_call(fid, node) for fid, node in dag.nodes)
    metta = f"""; {name}: the same CTL labelling in MeTTa (PeTTa dialect)
; properties checked, by the core subformula that answers each:
{battery_doc}
{facts}{METTA_LIB}
; the core subformulas, operands first
{calls}
(= (holds-at $f $x) (if (in $f $x) (add-atom &w (holds $f)) (empty)))
!(size-atom (collapse (match &self (top $f) (match &self (init $x) (holds-at $f $x)))))
!(match &w (holds $f) (holds $f))
{chr(10).join(f'!(let $n (sat-size {fid}) (if (> $n 0) (nsat {fid} $n) (empty)))' for fid, _ in dag.nodes)}
!(size-atom (collapse (match &s (sat $f $x) t)))
!(size-atom (collapse (match &w (holds $f) t)))
"""
    (out / f'{name}.metta').write_text(metta)
    (out / f'{name}.heads').write_text('sat holds\n')
    (out / f'{name}.values').write_text('nsat holds\n')
    nsat_lines = sorted(f"(nsat {fid} {len(sat[fid])})" for fid, _ in dag.nodes if sat[fid])
    holds = sorted(f"(holds {fid})" for _, _, fid in tops if 0 in sat[fid])
    exp = {'sat': sum(len(v) for v in sat.values()), 'holds': len(holds),
           'states': len(order), 'transitions': len(trans), 'subformulas': len(dag.nodes)}
    (out / f'{name}.expect.json').write_text(json.dumps(exp))
    (out / f'{name}.oracle_values.json').write_text(json.dumps({'nsat': nsat_lines, 'holds': holds}))
    print(name, 'states', len(order), 'transitions', len(trans), 'core subformulas', len(dag.nodes),
          'sat facts', exp['sat'], 'holds', [nm for nm, _, fid in tops if 0 in sat[fid]])
    for nm, _, fid in tops:
        print(f"  {fid} {nm}: {len(sat[fid])} states")

if __name__ == '__main__':
    main()
