#!/usr/bin/env python3
"""LDBC Graphalytics kernels, written from the specification's pseudo-code
(Iosup et al., "The LDBC Graphalytics Benchmark", arXiv:2011.15028, chapter
"Pseudo-code for Algorithms"), plus the dataset reader and the validator.

This is the independent oracle for the graph_graphalytics family.  It is
checked against the official reference outputs that ship with the
Graphalytics validation graphs (`validate_official.py`), and its BFS, WCC and
SSSP are cross-checked against scipy.sparse.csgraph on the generated graphs.

Datasets are read as data only: vertex ids are integers, weights decimals.
"""
import heapq
from collections import defaultdict, deque
from decimal import Decimal
from pathlib import Path

INF_INT = 9223372036854775807


def read_properties(path):
    props = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        props[k.strip()] = v.strip()
    return props


def load_dataset(ddir):
    """Read an extracted Graphalytics dataset directory.

    Returns dict(name, directed, vertices, edges, weights, params) where edges
    is a list of (u, v) exactly as listed (each undirected edge once) and
    weights a parallel list of decimal strings (or None)."""
    ddir = Path(ddir)
    name = ddir.name
    props = read_properties(ddir / f'{name}.properties')
    pre = f'graph.{name}.'
    get = lambda k, d=None: props.get(pre + k, d)
    directed = get('directed', 'true').lower() == 'true'
    vertices = [int(x) for x in (ddir / f'{name}.v').read_text().split()]
    edges, weights = [], []
    weighted = bool(get('edge-properties.names'))
    for line in (ddir / f'{name}.e').read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        edges.append((int(parts[0]), int(parts[1])))
        if weighted and len(parts) > 2:
            weights.append(parts[2])
    params = {}
    for alg in ('bfs.source-vertex', 'cdlp.max-iterations', 'pr.damping-factor',
                'pr.num-iterations', 'sssp.source-vertex'):
        val = get(alg)
        if val is not None:
            params[alg] = val
    algorithms = [a.strip() for a in get('algorithms', '').split(',') if a.strip()]
    return dict(name=name, directed=directed, vertices=vertices, edges=edges,
                weights=weights if weighted else None, params=params,
                algorithms=algorithms, dir=ddir)


def adjacency(vertices, edges, directed):
    """N_out and N_in as sets; for undirected graphs every edge is bidirectional."""
    out, inn = defaultdict(set), defaultdict(set)
    for u, v in edges:
        out[u].add(v); inn[v].add(u)
        if not directed:
            out[v].add(u); inn[u].add(v)
    return out, inn


def bfs(vertices, out, root):
    depth = {v: INF_INT for v in vertices}
    depth[root] = 0
    q = deque([root])
    while q:
        v = q.popleft()
        for u in out[v]:
            if depth[u] == INF_INT:
                depth[u] = depth[v] + 1
                q.append(u)
    return depth


def wcc(vertices, out, inn):
    """Component label = smallest vertex id in the component (the fixpoint of
    the specification's min-label propagation), computed by traversal."""
    comp = {}
    for s in sorted(vertices):
        if s in comp:
            continue
        comp[s] = s
        stack = [s]
        while stack:
            v = stack.pop()
            for u in out[v] | inn[v]:
                if u not in comp:
                    comp[u] = s
                    stack.append(u)
    return comp


def cdlp(vertices, out, inn, iterations, directed):
    labels = {v: v for v in vertices}
    for _ in range(iterations):
        new = {}
        for v in vertices:
            hist = defaultdict(int)
            for u in inn[v]:
                hist[labels[u]] += 1
            for u in out[v]:
                hist[labels[u]] += 1
            if not hist:
                new[v] = labels[v]
                continue
            freq = max(hist.values())
            new[v] = min(l for l, c in hist.items() if c == freq)
        labels = new
    return labels


def lcc_counts(vertices, out, inn):
    """Per vertex (t, d): d = |N_in u N_out|, t = ordered pairs (u, w) of
    neighbours with (u, w) an edge.  LCC = t / (d (d - 1)) when d >= 2."""
    res = {}
    for v in vertices:
        nb = out[v] | inn[v]
        d = len(nb)
        t = 0
        if d >= 2:
            for u in nb:
                t += len(out[u] & nb)
        res[v] = (t, d)
    return res


def lcc_value(t, d):
    return t / (d * (d - 1)) if d >= 2 else 0.0


def sssp_fixed(vertices, edges, weights, directed, root, scale):
    """Dijkstra over integer weights (decimal weight * scale, exact)."""
    adj = defaultdict(list)
    for (u, v), w in zip(edges, weights):
        iw = to_fixed(w, scale)
        adj[u].append((v, iw))
        if not directed:
            adj[v].append((u, iw))
    dist = {root: 0}
    h = [(0, root)]
    while h:
        d, v = heapq.heappop(h)
        if d > dist.get(v, d):
            continue
        for w, c in adj[v]:
            nd = d + c
            if nd < dist.get(w, nd + 1):
                dist[w] = nd
                heapq.heappush(h, (nd, w))
    return dist


def decimals_needed(weights):
    k = 0
    for w in weights:
        e = Decimal(w).normalize().as_tuple().exponent
        k = max(k, -e if e < 0 else 0)
    return k


def to_fixed(w, scale):
    q = Decimal(w) * scale
    if q != q.to_integral_value():
        raise ValueError(f'weight {w} has more decimals than the scale {scale}')
    return int(q)


def pagerank(vertices, out, inn, iterations, d):
    n = len(vertices)
    rank = {v: 1.0 / n for v in vertices}
    for _ in range(iterations):
        sink = sum(rank[w] for w in vertices if not out[w])
        rank = {v: (1 - d) / n + d * sum(rank[u] / len(out[u]) for u in inn[v]) + d / n * sink
                for v in vertices}
    return rank


# ---- validation against the official reference outputs ----

def read_reference(path, kind):
    ref = {}
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        v = int(parts[0])
        if kind == 'int':
            ref[v] = int(parts[1])
        else:
            ref[v] = float('inf') if parts[1] == 'infinity' else float(parts[1])
    return ref


def epsilon_match(ref, got, eps=1e-4):
    if set(ref) != set(got):
        return False
    for v, r in ref.items():
        s = got[v]
        if r == float('inf') or s == float('inf'):
            if r != s:
                return False
        elif abs(r - s) > eps * abs(r):
            return False
    return True


def equivalence_match(ref, got):
    if set(ref) != set(got):
        return False
    f, g = {}, {}
    for v in ref:
        a, b = ref[v], got[v]
        if f.setdefault(a, b) != b or g.setdefault(b, a) != a:
            return False
    return True
