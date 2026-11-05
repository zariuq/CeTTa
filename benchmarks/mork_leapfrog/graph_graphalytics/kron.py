#!/usr/bin/env python3
"""Graph500 Kronecker graph generator, after the Graph500 specification's
reference generator (initiator A=0.57, B=0.19, C=0.19, D=0.05; edge factor
16; 2^SCALE vertex labels; random relabeling of vertices).  The LDBC
Graphalytics graph500-N datasets are this generator's output turned into a
simple undirected graph: self-loops and duplicate edges removed and isolated
vertices dropped.  The same clean-up is applied here.

Returns sorted vertex ids and the edge list (u < v, each undirected edge
once), deterministic in (scale, edgefactor, seed).
"""
import numpy as np


def kronecker(scale, edgefactor=16, seed=1, A=0.57, B=0.19, C=0.19):
    rng = np.random.default_rng(seed)
    N = 1 << scale
    M = edgefactor * N
    ab = A + B
    c_norm = C / (1.0 - ab)
    a_norm = A / ab
    i = np.zeros(M, dtype=np.int64)
    j = np.zeros(M, dtype=np.int64)
    for ib in range(scale):
        ii_bit = rng.random(M) > ab
        jj_bit = rng.random(M) > np.where(ii_bit, c_norm, a_norm)
        i += ii_bit.astype(np.int64) << ib
        j += jj_bit.astype(np.int64) << ib
    perm = rng.permutation(N)
    i, j = perm[i], perm[j]
    keep = i != j
    i, j = i[keep], j[keep]
    lo, hi = np.minimum(i, j), np.maximum(i, j)
    pairs = np.unique(lo * N + hi)
    u, v = pairs // N, pairs % N
    verts = np.unique(np.concatenate([u, v]))
    return verts.tolist(), list(zip(u.tolist(), v.tolist()))


if __name__ == '__main__':
    import sys
    s = int(sys.argv[1])
    V, E = kronecker(s)
    deg = {}
    for a, b in E:
        deg[a] = deg.get(a, 0) + 1
        deg[b] = deg.get(b, 0) + 1
    d = sorted(deg.values())
    print(f'scale {s}: {len(V)} vertices, {len(E)} undirected edges, max degree {d[-1]}, '
          f'sum d^2 {sum(x * x for x in d)}')
