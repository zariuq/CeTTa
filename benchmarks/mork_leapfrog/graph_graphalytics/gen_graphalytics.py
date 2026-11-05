#!/usr/bin/env python3
"""LDBC Graphalytics kernels for MORK (MM2) and CeTTa (MeTTa, PeTTa dialect).

Source: LDBC Graphalytics (Iosup et al., PVLDB 9(13) 2016; specification
arXiv:2011.15028).  Five of its six kernels are written for both engines:

  bfs   breadth-first depths from the dataset's source vertex
  wcc   weakly connected components, labelled by their smallest vertex id
  cdlp  deterministic label propagation (in- and out-neighbour labels both
        counted, most frequent label, smallest on ties), fixed iterations
  lcc   local clustering coefficient, reported exactly as (lcc v t d) with
        LCC(v) = t / (d (d - 1)), for every vertex with t > 0
  sssp  single-source shortest paths; the decimal weights are read as
        integers in units of 10^-k (exact; k from the data), so both engines
        do integer arithmetic

PageRank is not written for MM2: MM2's sum sinks reduce the set of distinct
(group, value) pairs, so two in-neighbours with equal contributions are
summed once.  See variants/pr_probe.mm2.

Graphs come either from an official Graphalytics dataset directory (with its
.properties parameters and reference outputs) or from the Graph500 Kronecker
generator (the generator behind Graphalytics' graph500-N datasets), at a
given scale.

Writes gen/NAME_KERNEL.{mm2,metta,heads,values,expect.json,oracle_values.json}.
usage: gen_graphalytics.py (--dataset DIR | --graph500 SCALE) [--kernels bfs,wcc,cdlp,lcc,sssp]
                           [--seed 1] [--cdlp-iters 10] [--values full|summary]
"""
import argparse, json, random, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import galgo as G

HERE = Path(__file__).resolve().parent

INC = "(O (pure (succ $k1 $x) $x (i64_to_string (sum_i64 (i64_from_string $k1) (i64_from_string 1)))))"
INCI = "(O (pure (succ $i1 $x) $x (i64_to_string (sum_i64 (i64_from_string $i1) (i64_from_string 1)))))"


def facts(lines):
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------- BFS

def bfs_task(g, root, values):
    E, directed = g['edges'], g['directed']
    edges = facts(f"(e {u} {v})" for u, v in E)
    sym = '' if directed else "(exec (0 0 0) (, (e $u $v)) (, (e $v $u)))\n"
    mm2 = f"""; {g['name']} bfs from {root}: Graphalytics breadth-first search ({'directed' if directed else 'undirected'})
{edges}(dist {root} 0) (front 0 {root}) (succ 0 1) (live 0)
{sym}; round k: expand the frontier, drop candidates already visited, promote the rest;
; the controller re-emits itself while the last round promoted something
(exec (2 0 c) (, (exec (2 $k c) $ps $ts) (succ $k $k1) (live $k))
              (, (exec (2 $k1 c) $ps $ts)
                 (exec (1 $k a) (, (front $k $u) (e $u $v)) (, (cand $k1 $v)))
                 (exec (1 $k b) (, (cand $k1 $v) (dist $v $d)) (O (- (cand $k1 $v))))
                 (exec (1 $k c) (, (cand $k1 $v)) (O (+ (dist $v $k1)) (+ (front $k1 $v)) (+ (live $k1)) (- (cand $k1 $v))))
                 (exec (1 $k d) (, (front $k $u)) (O (- (front $k $u))))
                 (exec (1 $k e) (, (live $k1)) {INC})))
(exec (3 0 0) (, (dist $v $d)) (O (count (level $d $n) $n $v)))
"""
    nb = "(e $u $v)" if directed else "(n $u $v)"
    symm = "" if directed else """!(bind! &n (new-space set))
!(size-atom (collapse (match &self (e $u $v) (let $_ (add-atom &n (n $u $v)) (add-atom &n (n $v $u))))))
"""
    space = "&self" if directed else "&n"
    metta = f"""; {g['name']} bfs from {root}: the same search in MeTTa (PeTTa dialect), the frontier carried as a list
{edges}{symm}!(bind! &d (new-space set))
!(bind! &s (new-space set))
!(add-atom &d (dist {root} 0))
!(add-atom &s (seen {root}))
; the first visit of a vertex fixes its depth and puts it on the next frontier
(= (visit $v $k1)
   (if (== (collapse (match &s (seen $v) t)) ())
       (let* (($_ (add-atom &s (seen $v))) ($_ (add-atom &d (dist $v $k1)))) $v)
       (empty)))
(= (bfs $front $k)
   (let* (($k1 (+ $k 1))
          ($next (collapse (let $u (superpose $front) (match {space} {nb} (visit $v $k1))))))
     (if (== $next ()) $k
         (let* (($n (size-atom $next)) ($_ (add-atom &d (level $k1 $n)))) (bfs $next $k1)))))
!(add-atom &d (level 0 1))
!(bfs ({root}) 0)
!(match &d (level $k $n) (level $k $n))
{'!(match &d (dist $v $x) (dist $v $x))' if values == 'full' else ''}
!(size-atom (collapse (match &d (dist $v $x) t)))
!(size-atom (collapse (match &d (level $k $n) t)))
"""
    V = g['vertices']
    out, inn = G.adjacency(V, E, directed)
    depth = G.bfs(V, out, root)
    reach = {v: d for v, d in depth.items() if d != G.INF_INT}
    hist = Counter(reach.values())
    expect = {'dist': len(reach), 'level': len(hist)}
    ov = {'level': sorted(f"(level {d} {n})" for d, n in hist.items())}
    vals = ['level']
    if values == 'full':
        ov['dist'] = sorted(f"(dist {v} {d})" for v, d in reach.items())
        vals.append('dist')
    return mm2, metta, ['dist', 'level'], vals, expect, ov


# ---------------------------------------------------------------- WCC

def wcc_task(g, values, wcc_mm2='bfs'):
    E, V = g['edges'], g['vertices']
    data = facts(f"(v {x})" for x in V) + facts(f"(e {u} {v})" for u, v in E)
    if wcc_mm2 == 'lp':
        mm2 = f"""; {g['name']} wcc: Graphalytics weakly connected components, labelled by the smallest vertex id
{data}(succ 0 1) (live 0)
(exec (0 0 0) (, (e $u $v)) (, (n $u $v) (n $v $u)))
(exec (0 0 1) (, (v $x)) (, (lab $x $x) (chg 0 $x $x)))
; min-label propagation from the vertices whose label changed in the last round;
; the current label joins the candidates, so an equal minimum means no change
(exec (2 0 c) (, (exec (2 $k c) $ps $ts) (succ $k $k1) (live $k))
              (, (exec (2 $k1 c) $ps $ts)
                 (exec (1 $k a) (, (chg $k $u $l) (n $u $w)) (, (cand $k1 $w $l)))
                 (exec (1 $k b) (, (cand $k1 $w $l) (lab $w $c)) (, (cand $k1 $w $c)))
                 (exec (1 $k c) (, (cand $k1 $w $l)) (O (fmin (best $k1 $w $m) $m $l) (- (cand $k1 $w $l))))
                 (exec (1 $k d) (, (best $k1 $w $m) (lab $w $m)) (O (- (best $k1 $w $m))))
                 (exec (1 $k e) (, (best $k1 $w $m) (lab $w $c))
                                (O (- (lab $w $c)) (+ (lab $w $m)) (+ (chg $k1 $w $m)) (+ (live $k1)) (- (best $k1 $w $m))))
                 (exec (1 $k f) (, (chg $k $u $l)) (O (- (chg $k $u $l))))
                 (exec (1 $k g) (, (live $k1)) {INC})))
(exec (3 0 0) (, (lab $v $c)) (O (count (csize $c $n) $n $v)))
"""
    else:
        mm2 = f"""; {g['name']} wcc: Graphalytics weakly connected components, labelled by the smallest vertex id
{data}(succ 0 1)
(exec (0 0 0) (, (e $u $v)) (, (n $u $v) (n $v $u)))
(exec (0 0 1) (, (v $x)) (, (un $x)))
; one component at a time: its root is the smallest unvisited vertex (head 1 keeps the
; smallest key, and decimal ids sort numerically), then a breadth-first search labels it
(exec (4 0 c) (, (exec (4 $i c) $ps $ts) (succ $i $i1) (un $any))
   (, (exec (4 $i1 c) $ps $ts)
      (exec (1 $i r) (, (un $x)) (O (head 1 (root $i $x))))
      (exec (1 $i s) (, (root $i $r))
         (O (- (un $r)) (- (root $i $r)) (+ (lab $r $r)) (+ (fr 0 $r)) (+ (live 0))
            (+ (exec (3 0 c) (, (exec (3 $k c) $qs $us) (succ $k $k1) (live $k))
                  (, (exec (3 $k1 c) $qs $us)
                     (exec (2 $k a) (, (fr $k $u) (n $u $w)) (, (cand $k1 $w)))
                     (exec (2 $k b) (, (cand $k1 $w) (lab $w $l)) (O (- (cand $k1 $w))))
                     (exec (2 $k c) (, (cand $k1 $w)) (O (+ (lab $w $r)) (+ (fr $k1 $w)) (+ (live $k1)) (- (un $w)) (- (cand $k1 $w))))
                     (exec (2 $k d) (, (fr $k $u)) (O (- (fr $k $u)) (- (live $k))))
                     (exec (2 $k e) (, (live $k1)) {INC}))))))
      (exec (1 $i t) (, (succ $i $i1)) {INCI})))
(exec (5 0 0) (, (lab $v $c)) (O (count (csize $c $n) $n $v)))
"""
    metta = f"""; {g['name']} wcc: the same components in MeTTa (PeTTa dialect), one traversal per component
{data}!(bind! &n (new-space set))
!(size-atom (collapse (match &self (e $u $v) (let $_ (add-atom &n (n $u $v)) (add-atom &n (n $v $u))))))
!(bind! &c (new-space set))
(= (unseen $x) (== (collapse (match &c (seen $x) t)) ()))
; breadth-first rounds from a root, the frontier carried as a list; members are filed under the root
(= (grow $r $front)
   (let $next (collapse (let $u (superpose $front) (match &n (n $u $w)
                (if (unseen $w)
                    (let* (($_ (add-atom &c (seen $w))) ($_ (add-atom &c (m $r $w)))) $w)
                    (empty)))))
     (if (== $next ()) done (grow $r $next))))
; a component is labelled by its smallest member
(= (component $x)
   (if (unseen $x)
       (let* (($_ (add-atom &c (seen $x))) ($_ (add-atom &c (m $x $x)))
              ($_ (grow $x ($x)))
              ($min (min-atom (collapse (match &c (m $x $y) $y))))
              ($size (size-atom (collapse (match &c (m $x $y) (add-atom &c (lab $y $min)))))))
         (add-atom &c (csize $min $size)))
       (empty)))
!(size-atom (collapse (match &self (v $x) (component $x))))
!(match &c (csize $c $n) (csize $c $n))
{'!(match &c (lab $v $c) (lab $v $c))' if values == 'full' else ''}
!(size-atom (collapse (match &c (lab $v $c) t)))
!(size-atom (collapse (match &c (csize $c $n) t)))
"""
    out, inn = G.adjacency(V, E, g['directed'])
    comp = G.wcc(V, out, inn)
    sizes = Counter(comp.values())
    expect = {'lab': len(comp), 'csize': len(sizes)}
    ov = {'csize': sorted(f"(csize {c} {n})" for c, n in sizes.items())}
    vals = ['csize']
    if values == 'full':
        ov['lab'] = sorted(f"(lab {v} {c})" for v, c in comp.items())
        vals.append('lab')
    return mm2, metta, ['lab', 'csize'], vals, expect, ov


# ---------------------------------------------------------------- CDLP

def cdlp_task(g, iters, values):
    E, V, directed = g['edges'], g['vertices'], g['directed']
    data = facts(f"(v {x})" for x in V) + facts(f"(e {u} {v})" for u, v in E)
    succ = ' '.join(f"(succ {i} {i + 1})" for i in range(iters))
    # (to u v d): u's label is counted at v, once per direction d in which u neighbours v
    to = ("(exec (0 0 0) (, (e $u $v)) (, (to $u $v i) (to $v $u o)))" if directed else
          "(exec (0 0 0) (, (e $u $v)) (, (to $u $v o) (to $v $u o)))")
    mm2 = f"""; {g['name']} cdlp, {iters} iterations: Graphalytics label propagation ({'directed' if directed else 'undirected'})
{data}{succ}
{to}
(exec (0 0 1) (, (v $x)) (, (lab 0 $x $x) (iso $x)))
(exec (0 0 2) (, (to $u $x $d)) (O (- (iso $x))))
; iteration t: label histograms by a count sink, their maximum, the smallest label at it
(exec (2 0 c) (, (exec (2 $t c) $ps $ts) (succ $t $t1))
              (, (exec (2 $t1 c) $ps $ts)
                 (exec (1 $t a) (, (lab $t $u $l) (to $u $v $d)) (O (count (freq $t1 $v $l $c) $c ($u $d))))
                 (exec (1 $t b) (, (freq $t1 $v $l $c)) (O (fmax (mx $t1 $v $m) $m $c)))
                 (exec (1 $t c) (, (mx $t1 $v $m) (freq $t1 $v $l $m)) (O (fmin (lab $t1 $v $x) $x $l)))
                 (exec (1 $t d) (, (lab $t $v $l) (iso $v)) (, (lab $t1 $v $l)))
                 (exec (1 $t e) (, (freq $t1 $v $l $c)) (O (- (freq $t1 $v $l $c))))
                 (exec (1 $t f) (, (mx $t1 $v $m)) (O (- (mx $t1 $v $m))))
                 (exec (1 $t g) (, (lab $t $v $l)) (O (- (lab $t $v $l))))))
(exec (3 0 0) (, (lab {iters} $v $l)) (O (count (comm $l $n) $n $v)))
"""
    if directed:
        nbr = """(= (nlabels $t $v) (match &r (r $v $u) (match &L (lab $t $u $l) $l)))
(= (nlabels $t $v) (match &self (e $v $u) (match &L (lab $t $u $l) $l)))"""
        prep = """!(bind! &r (new-space set))
!(size-atom (collapse (match &self (e $u $v) (add-atom &r (r $v $u)))))
"""
    else:
        nbr = "(= (nlabels $t $v) (match &n (n $v $u) (match &L (lab $t $u $l) $l)))"
        prep = """!(bind! &n (new-space set))
!(size-atom (collapse (match &self (e $u $v) (let $_ (add-atom &n (n $u $v)) (add-atom &n (n $v $u))))))
"""
    metta = f"""; {g['name']} cdlp, {iters} iterations: the same label propagation in MeTTa (PeTTa dialect)
{data}{prep}!(bind! &L (new-space set))
!(size-atom (collapse (match &self (v $x) (add-atom &L (lab 0 $x $x)))))
; the labels around v, with multiplicity (a vertex that is both an in- and an out-neighbour counts twice)
{nbr}
; the most frequent label of a sorted list, the smallest one on ties
(= (mode-step ($c $cn $b $bn) $x)
   (if (== $x $c) ($c (+ $cn 1) $b $bn)
       (if (> $cn $bn) ($x 1 $c $cn) ($x 1 $b $bn))))
(= (mode $sorted)
   (let ($c $cn $b $bn) (foldl-atom $sorted (none 0 none 0) $acc $x (mode-step $acc $x))
     (if (> $cn $bn) $c $b)))
(= (relabel $t $t1 $v $l)
   (let $ls (collapse (nlabels $t $v))
     (if (== $ls ()) (add-atom &L (lab $t1 $v $l)) (let $m (mode (msort $ls)) (add-atom &L (lab $t1 $v $m))))))
(= (iterate $t $T)
   (if (== $t $T) done
       (let* (($t1 (+ $t 1))
              ($a (size-atom (collapse (match &L (lab $t $v $l) (relabel $t $t1 $v $l)))))
              ($b (size-atom (collapse (match &L (lab $t $v $l) (remove-atom &L (lab $t $v $l)))))))
         (iterate $t1 $T))))
!(iterate 0 {iters})
!(bind! &R (new-space set))
!(size-atom (collapse (match &L (lab {iters} $v $l) (add-atom &R (cl $l)))))
!(size-atom (collapse (match &R (cl $l) (let $n (size-atom (collapse (match &L (lab {iters} $v $l) t))) (add-atom &R (comm $l $n))))))
!(match &R (comm $l $n) (comm $l $n))
{f'!(match &L (lab {iters} $v $l) (lab {iters} $v $l))' if values == 'full' else ''}
!(size-atom (collapse (match &L (lab {iters} $v $l) t)))
!(size-atom (collapse (match &R (comm $l $n) t)))
"""
    out, inn = G.adjacency(V, E, directed)
    labels = G.cdlp(V, out, inn, iters, directed)
    comm = Counter(labels.values())
    expect = {'lab': len(labels), 'comm': len(comm)}
    ov = {'comm': sorted(f"(comm {l} {n})" for l, n in comm.items())}
    vals = ['comm']
    if values == 'full':
        ov['lab'] = sorted(f"(lab {iters} {v} {l})" for v, l in labels.items())
        vals.append('lab')
    return mm2, metta, ['lab', 'comm'], vals, expect, ov


# ---------------------------------------------------------------- LCC

def lcc_task(g, values):
    E, V, directed = g['edges'], g['vertices'], g['directed']
    data = facts(f"(e {u} {v})" for u, v in E)
    closing = "(e $u $w)" if directed else "(n $u $w)"
    mm2 = f"""; {g['name']} lcc: Graphalytics local clustering coefficient, as t and d with LCC = t / (d (d - 1))
{data}(exec (0 0 0) (, (e $u $v)) (, (n $u $v) (n $v $u)))
; d: distinct neighbours (in or out); t: ordered neighbour pairs (u, w) joined by an edge u -> w,
; counted by one exec specialized to each vertex (each with its own small count sink)
(exec (1 0 0) (, (n $v $u)) (O (count (deg $v $c) $c $u)))
(exec (1 0 1) (, (deg $v $d)) (, (exec (2 $v 0) (, (n $v $a) (n $v $b) {closing.replace('$u', '$a').replace('$w', '$b')}) (O (count (tri $v $c) $c ($a $b))))))
(exec (3 0 0) (, (tri $v $t) (deg $v $d)) (, (lcc $v $t $d)))
"""
    close_m = "(match &self (e $u $w) t)" if directed else "(match &n (n $u $w) t)"
    metta = f"""; {g['name']} lcc: the same counts in MeTTa (PeTTa dialect), vertex by vertex
{data}!(bind! &n (new-space set))
!(size-atom (collapse (match &self (e $u $v) (let $_ (add-atom &n (n $u $v)) (add-atom &n (n $v $u))))))
!(bind! &r (new-space set))
(= (wedges $v) (match &n (n $v $u) (match &n (n $v $w) {close_m})))
(= (lcc-of $v)
   (let $t (size-atom (collapse (wedges $v)))
     (if (> $t 0) (let $d (size-atom (collapse (match &n (n $v $u) t))) (add-atom &r (lcc $v $t $d))) (empty))))
!(bind! &V (new-space set))
!(size-atom (collapse (match &n (n $v $u) (add-atom &V (nv $v)))))
!(size-atom (collapse (match &V (nv $v) (lcc-of $v))))
{'!(match &r (lcc $v $t $d) (lcc $v $t $d))' if values == 'full' else ''}
!(size-atom (collapse (match &r (lcc $v $t $d) t)))
"""
    out, inn = G.adjacency(V, E, directed)
    cnt = {v: td for v, td in G.lcc_counts(V, out, inn).items() if td[0] > 0}
    expect = {'lcc': len(cnt)}
    ov = {}
    vals = []
    if values == 'full':
        ov['lcc'] = sorted(f"(lcc {v} {t} {d})" for v, (t, d) in cnt.items())
        vals.append('lcc')
    return mm2, metta, ['lcc'], vals, expect, ov


# ---------------------------------------------------------------- SSSP

def sssp_task(g, root, values):
    E, V, directed, W = g['edges'], g['vertices'], g['directed'], g['weights']
    k = G.decimals_needed(W)
    scale = 10 ** k
    iw = [G.to_fixed(w, scale) for w in W]
    data = facts(f"(e {u} {v} {c})" for (u, v), c in zip(E, iw))
    sym = '' if directed else "(exec (0 0 0) (, (e $u $v $c)) (, (e $v $u $c)))\n"
    mm2 = f"""; {g['name']} sssp from {root}: Graphalytics shortest paths, weights in units of 10^-{k}
{data}(sd {root} 0) (sf 0 {root} 0) (succ 0 1) (live 0)
{sym}; Bellman-Ford rounds from the vertices improved in the last round; the current
; distance joins the candidates, so an equal minimum means no improvement
(exec (2 0 c) (, (exec (2 $k c) $ps $ts) (succ $k $k1) (live $k))
              (, (exec (2 $k1 c) $ps $ts)
                 (exec (1 $k a) (, (sf $k $u $d) (e $u $v $c))
                                (O (pure (cand $k1 $v $x) $x (i64_to_string (sum_i64 (i64_from_string $d) (i64_from_string $c))))))
                 (exec (1 $k b) (, (cand $k1 $v $x) (sd $v $y)) (, (cand $k1 $v $y)))
                 (exec (1 $k c) (, (cand $k1 $v $x)) (O (fmin (best $k1 $v $m) $m $x) (- (cand $k1 $v $x))))
                 (exec (1 $k d) (, (best $k1 $v $m) (sd $v $m)) (O (- (best $k1 $v $m))))
                 (exec (1 $k e) (, (best $k1 $v $m) (sd $v $y))
                                (O (- (sd $v $y)) (+ (sd $v $m)) (+ (sf $k1 $v $m)) (+ (live $k1)) (- (best $k1 $v $m))))
                 (exec (1 $k f) (, (best $k1 $v $m)) (O (+ (sd $v $m)) (+ (sf $k1 $v $m)) (+ (live $k1)) (- (best $k1 $v $m))))
                 (exec (1 $k g) (, (sf $k $u $d)) (O (- (sf $k $u $d))))
                 (exec (1 $k h) (, (live $k1)) {INC})))
"""
    symm = '' if directed else """!(bind! &w (new-space set))
!(size-atom (collapse (match &self (e $u $v $c) (let $_ (add-atom &w (e $u $v $c)) (add-atom &w (e $v $u $c))))))
"""
    wspace = '&self' if directed else '&w'
    metta = f"""; {g['name']} sssp from {root}: the same distances in MeTTa (PeTTa dialect), weights in units of 10^-{k}
{data}{symm}!(bind! &s (new-space set))
!(add-atom &s (sd {root} 0))
; relax the edges of the vertices improved in the last round; updates take effect at once,
; and an improved vertex goes on the next frontier (a list, without repeats)
(= (improve $v $nd)
   (let $cur (collapse (match &s (sd $v $y) $y))
     (if (== $cur ())
         (let $_ (add-atom &s (sd $v $nd)) $v)
         (let ($y) $cur
           (if (< $nd $y)
               (let* (($_ (remove-atom &s (sd $v $y))) ($_ (add-atom &s (sd $v $nd)))) $v)
               (empty))))))
(= (relax $u) (match &s (sd $u $d) (match {wspace} (e $u $v $c) (improve $v (+ $d $c)))))
(= (sssp $front $k)
   (let $next (collapse (unique (let $u (superpose $front) (relax $u))))
     (if (== $next ()) $k (sssp $next (+ $k 1)))))
!(sssp ({root}) 0)
{'!(match &s (sd $v $d) (sd $v $d))' if values == 'full' else ''}
!(size-atom (collapse (match &s (sd $v $d) t)))
"""
    dist = G.sssp_fixed(V, E, W, directed, root, scale)
    hist = Counter()
    expect = {'sd': len(dist)}
    ov = {}
    vals = []
    if values == 'full':
        ov['sd'] = sorted(f"(sd {v} {d})" for v, d in dist.items())
        vals.append('sd')
    return mm2, metta, ['sd'], vals, expect, ov


# ---------------------------------------------------------------- driver

def write_task(outdir, name, mm2, metta, heads, vals, expect, ov):
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / f'{name}.mm2').write_text(mm2)
    (outdir / f'{name}.metta').write_text(metta)
    (outdir / f'{name}.heads').write_text(' '.join(heads) + '\n')
    if vals:
        (outdir / f'{name}.values').write_text(' '.join(vals) + '\n')
    else:
        p = outdir / f'{name}.values'
        if p.exists():
            p.unlink()
    (outdir / f'{name}.expect.json').write_text(json.dumps(expect))
    (outdir / f'{name}.oracle_values.json').write_text(json.dumps(ov))


def graph500_graph(scale, seed, edgefactor):
    from kron import kronecker
    V, E = kronecker(scale, edgefactor=edgefactor, seed=seed)
    rng = random.Random(seed)
    W = [f"{rng.randrange(1000) / 1000:.3f}" for _ in E]   # Graph500 SSSP draws weights in [0, 1)
    deg = Counter()
    for u, v in E:
        deg[u] += 1; deg[v] += 1
    root = rng.choice(sorted(deg))
    return dict(name=f'g500s{scale}', directed=False, vertices=V, edges=E, weights=W,
                params={'bfs.source-vertex': root, 'sssp.source-vertex': root, 'cdlp.max-iterations': 10},
                algorithms=['bfs', 'wcc', 'cdlp', 'lcc', 'sssp'])


def main():
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--dataset')
    src.add_argument('--graph500', type=int)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--edgefactor', type=int, default=16)
    ap.add_argument('--kernels', default='bfs,wcc,cdlp,lcc,sssp')
    ap.add_argument('--cdlp-iters', type=int, default=None)
    ap.add_argument('--values', default='full', choices=['full', 'summary'])
    ap.add_argument('--wcc-mm2', default='bfs', choices=['bfs', 'lp'],
                    help='MM2 WCC: one breadth-first search per component (default) or min-label propagation (the measured alternative)')
    ap.add_argument('--outdir', default=str(HERE / 'gen'))
    a = ap.parse_args()
    if a.dataset:
        g = G.load_dataset(a.dataset)
    else:
        g = graph500_graph(a.graph500, a.seed, a.edgefactor)
    out = Path(a.outdir)
    P = g['params']
    for kern in a.kernels.split(','):
        if kern not in g['algorithms']:
            continue
        name = f"{g['name']}_{kern}"
        if kern == 'bfs':
            t = bfs_task(g, int(P['bfs.source-vertex']), a.values)
        elif kern == 'wcc':
            t = wcc_task(g, a.values, a.wcc_mm2)
        elif kern == 'cdlp':
            t = cdlp_task(g, a.cdlp_iters or int(P.get('cdlp.max-iterations', 10)), a.values)
        elif kern == 'lcc':
            t = lcc_task(g, a.values)
        elif kern == 'sssp':
            if not g['weights']:
                continue
            t = sssp_task(g, int(P['sssp.source-vertex']), a.values)
        else:
            continue
        write_task(out, name, *t)
        print(name, 'vertices', len(g['vertices']), 'edges', len(g['edges']), 'expect', t[4])


if __name__ == '__main__':
    main()
