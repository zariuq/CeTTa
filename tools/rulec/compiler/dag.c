/* dag.c - see rulec.h */
#include "rulec.h"

/* -------------------------------------------------------------- decision DAGs */

/* An occurrence: a path into the arguments (p[0] the argument, p[1..] child indices) together with
 * the sort at every step (s[l] is the sort of the subterm reached by p[0..l]).  The same path under
 * different constructors can have different sorts, so both are part of its identity. */
Occ *occs; int noccs, occcap;
Map occ_map;
int occ_intern(const int *p, const int *s, int len) {
    char key[2048]; int o = 0;
    for (int i = 0; i < len; i++) o += snprintf(key + o, sizeof key - (size_t)o, "%d:%d.", p[i], s[i]);
    int id = map_get(&occ_map, key);
    if (id >= 0) return id;
    Occ x = {len, xmalloc((size_t)len * sizeof(int)), xmalloc((size_t)len * sizeof(int))};
    memcpy(x.p, p, (size_t)len * sizeof(int));
    memcpy(x.s, s, (size_t)len * sizeof(int));
    map_put(&occ_map, key, noccs);
    VEC_PUSH(occs, noccs, occcap, x);
    return noccs - 1;
}
int occ_child(int occ, int k, int sort) {
    int buf[256], sb[256];
    Occ *o = &occs[occ];
    if (o->len >= 255) die("pattern too deep");
    memcpy(buf, o->p, (size_t)o->len * sizeof(int));
    memcpy(sb, o->s, (size_t)o->len * sizeof(int));
    buf[o->len] = k;
    sb[o->len] = sort;
    return occ_intern(buf, sb, o->len + 1);
}

/* a leaf lists the rules whose left sides match every input reaching it (one rule in a module whose
 * left sides do not overlap; where they overlap, the rules not excluded on the leaf's path, which
 * check.c has shown pairwise exclusive, so that at most one of them applies to any input).  A rule
 * whose left side matches but whose premises cannot hold for any input on the path (rule_excluded_at,
 * on the constructors the path has tested) is recorded as excluded and not compiled there. */
Node *nodes; int nnodes, nodecap;
Map node_map;

int node_intern(Node *n) {
    size_t cap = 64 + 32 * (size_t)n->ncase + 16 * (size_t)(n->ncand + n->nexcl), o = 0;
    char *key = xmalloc(cap);
    o += (size_t)snprintf(key + o, cap - o, "%d:%d:%d:%d:%d|", n->kind, n->rule, n->occ, n->sort, n->dflt);
    for (int i = 0; i < n->ncand && o + 16 < cap; i++) o += (size_t)snprintf(key + o, cap - o, "c%d,", n->cand[i]);
    for (int i = 0; i < n->nexcl && o + 16 < cap; i++) o += (size_t)snprintf(key + o, cap - o, "x%d,", n->excl[i]);
    for (int i = 0; i < n->ncase; i++) o += (size_t)snprintf(key + o, cap - o, "%d>%d,", n->cctor[i], n->cchild[i]);
    int id = map_get(&node_map, key);
    if (id < 0) {
        id = nnodes;
        map_put(&node_map, key, id);
        VEC_PUSH(nodes, nnodes, nodecap, *n);
    } else {
        free(n->cctor); free(n->cchild);
    }
    free(key);
    return id;
}

Pat wildcard = {-2, -1, 0, NULL, -1, 0};

/* the constructors tested on the path from the root to the node being compiled */
static int dag_judgment, path_occ[512], path_ctor[512], npath;
/* the input as the path knows it: a term of the analysis store (check.c) */
static int known_term(const int *p, int len) {
    for (int i = 0; i < npath; i++) {
        const Occ *o = &occs[path_occ[i]];
        if (o->len != len || memcmp(o->p, p, (size_t)len * sizeof(int))) continue;
        const Ctor *c = &ctors[path_ctor[i]];
        int kids[16], q[256];
        if (len >= 255) die("pattern too deep");
        memcpy(q, p, (size_t)len * sizeof(int));
        for (int x = 0; x < c->arity; x++) { q[len] = x; kids[x] = known_term(q, len + 1); }
        return ut_push(path_ctor[i], c->arity, kids);
    }
    return ut_var();
}


int dag_compile(int ncol, const int *occ, const int *osort, Row *rows, int nrow) {
    Node n = {0};
    n.dflt = -1; n.rule = -1; n.occ = -1; n.sort = -1;
    if (nrow == 0) { n.kind = N_FAIL; return node_intern(&n); }
    int j = -1, best = -1;
    for (int c = 0; c < ncol; c++) {
        if (rows[0].cols[c]->var != -1) continue;
        int score = 0;
        for (int r = 0; r < nrow && rows[r].cols[c]->var == -1; r++) score++;
        if (score > best) { best = score; j = c; }
    }
    if (j < 0) {
        /* the first row matches every input here; a later row with a constructor still needs testing
         * (where left sides overlap) */
        for (int c = 0; c < ncol; c++) {
            int score = 0;
            for (int r = 0; r < nrow; r++) if (rows[r].cols[c]->var == -1) score++;
            if (score > best) { best = score; j = c; }
        }
        if (best <= 0) j = -1;
    }
    if (j < 0) {
        n.kind = N_LEAF;
        n.cand = xmalloc((size_t)nrow * sizeof(int));
        n.excl = xmalloc((size_t)nrow * sizeof(int));
        int input = -1;
        if (nrow > 1) {
            u_reset();
            int kids[16], arity = ctors[dag_judgment].arity;
            for (int a = 0; a < arity; a++) kids[a] = known_term(&a, 1);
            input = ut_push(dag_judgment, arity, kids);
        }
        for (int r = 0; r < nrow; r++) {
            if (input >= 0 && rule_excluded_at(&rules[rows[r].rule], input)) n.excl[n.nexcl++] = rows[r].rule;
            else n.cand[n.ncand++] = rows[r].rule;
        }
        if (n.ncand == 0) n.kind = N_FAIL;
        else n.rule = n.cand[0];
        return node_intern(&n);
    }
    /* constructors of column j, in order of first appearance */
    int *cs = NULL, ncs = 0, cscap = 0;
    for (int r = 0; r < nrow; r++) {
        Pat *p = rows[r].cols[j];
        if (p->var != -1) continue;
        bool seen = false;
        for (int i = 0; i < ncs; i++) if (cs[i] == p->ctor) seen = true;
        if (!seen) VEC_PUSH(cs, ncs, cscap, p->ctor);
    }
    n.kind = N_SWITCH; n.occ = occ[j]; n.sort = osort[j]; n.ncase = ncs;
    n.cctor = xmalloc((size_t)ncs * sizeof(int)); n.cchild = xmalloc((size_t)ncs * sizeof(int));
    for (int i = 0; i < ncs; i++) {
        int c = cs[i], a = ctors[c].arity, nc = ncol - 1 + a;
        int *nocc = xmalloc((size_t)(nc ? nc : 1) * sizeof(int)), *nsort = xmalloc((size_t)(nc ? nc : 1) * sizeof(int));
        int w = 0;
        for (int x = 0; x < j; x++) { nocc[w] = occ[x]; nsort[w++] = osort[x]; }
        for (int x = 0; x < a; x++) { nocc[w] = occ_child(occ[j], x, ctors[c].asort[x]); nsort[w++] = ctors[c].asort[x]; }
        for (int x = j + 1; x < ncol; x++) { nocc[w] = occ[x]; nsort[w++] = osort[x]; }
        Row *nr = xmalloc((size_t)nrow * sizeof(Row)); int nn = 0;
        for (int r = 0; r < nrow; r++) {
            Pat *p = rows[r].cols[j];
            if (p->var == -1 && p->ctor != c) continue;
            Pat **cols = xmalloc((size_t)(nc ? nc : 1) * sizeof(Pat *));
            int u = 0;
            for (int x = 0; x < j; x++) cols[u++] = rows[r].cols[x];
            for (int x = 0; x < a; x++) cols[u++] = p->var == -1 ? p->k[x] : &wildcard;
            for (int x = j + 1; x < ncol; x++) cols[u++] = rows[r].cols[x];
            nr[nn++] = (Row){rows[r].rule, cols};
        }
        n.cctor[i] = c;
        if (npath == 512) die("decision path too long");
        path_occ[npath] = occ[j]; path_ctor[npath++] = c;
        n.cchild[i] = dag_compile(nc, nocc, nsort, nr, nn);
        npath--;
        for (int r = 0; r < nn; r++) free(nr[r].cols);
        free(nr); free(nocc); free(nsort);
    }
    /* default: needed unless the cases cover every data constructor of the sort */
    bool covered = ncs == sorts[osort[j]].ndata;
    if (!covered) {
        int nc = ncol - 1;
        int *nocc = xmalloc((size_t)(nc ? nc : 1) * sizeof(int)), *nsort = xmalloc((size_t)(nc ? nc : 1) * sizeof(int));
        int w = 0;
        for (int x = 0; x < ncol; x++) if (x != j) { nocc[w] = occ[x]; nsort[w++] = osort[x]; }
        Row *nr = xmalloc((size_t)nrow * sizeof(Row)); int nn = 0;
        for (int r = 0; r < nrow; r++) {
            if (rows[r].cols[j]->var == -1) continue;
            Pat **cols = xmalloc((size_t)(nc ? nc : 1) * sizeof(Pat *));
            int u = 0;
            for (int x = 0; x < ncol; x++) if (x != j) cols[u++] = rows[r].cols[x];
            nr[nn++] = (Row){rows[r].rule, cols};
        }
        n.dflt = dag_compile(nc, nocc, nsort, nr, nn);
        for (int r = 0; r < nn; r++) free(nr[r].cols);
        free(nr); free(nocc); free(nsort);
    }
    free(cs);
    return node_intern(&n);
}

void build_dags(void) {
    for (int c = 0; c < nctors; c++) {
        Ctor *k = &ctors[c];
        if (!k->defined) continue;
        int *occ = xmalloc((size_t)(k->arity ? k->arity : 1) * sizeof(int));
        for (int a = 0; a < k->arity; a++) occ[a] = occ_intern(&a, &k->asort[a], 1);
        Row *rows = xmalloc((size_t)k->nrules * sizeof(Row));
        for (int i = 0; i < k->nrules; i++) {
            Rule *r = &rules[k->rules[i]];
            rows[i].rule = r->id;
            rows[i].cols = xmalloc((size_t)(k->arity ? k->arity : 1) * sizeof(Pat *));
            for (int a = 0; a < k->arity; a++) rows[i].cols[a] = r->lhs->k[a];
        }
        dag_judgment = c; npath = 0;
        k->dag_root = dag_compile(k->arity, occ, k->asort, rows, k->nrules);
        for (int i = 0; i < k->nrules; i++) free(rows[i].cols);
        free(rows); free(occ);
    }
}

/* nodes reachable from a root, in a stable order (depth first) */
void dag_reach(int n, bool *seen, int *order, int *norder) {
    if (seen[n]) return;
    seen[n] = true;
    order[(*norder)++] = n;
    Node *x = &nodes[n];
    if (x->kind == N_SWITCH) {
        for (int i = 0; i < x->ncase; i++) dag_reach(x->cchild[i], seen, order, norder);
        if (x->dflt >= 0) dag_reach(x->dflt, seen, order, norder);
    }
}
bool dag_has_fail(int root) {
    bool *seen = xcalloc((size_t)nnodes, 1); int *order = xmalloc((size_t)nnodes * sizeof(int)); int no = 0;
    dag_reach(root, seen, order, &no);
    bool f = false;
    for (int i = 0; i < no; i++) if (nodes[order[i]].kind == N_FAIL) f = true;
    free(seen); free(order);
    return f;
}
int dag_size(int root, int *switches, int *depth) {
    bool *seen = xcalloc((size_t)nnodes, 1); int *order = xmalloc((size_t)nnodes * sizeof(int)); int no = 0;
    dag_reach(root, seen, order, &no);
    int sw = 0;
    for (int i = 0; i < no; i++) if (nodes[order[i]].kind == N_SWITCH) sw++;
    *switches = sw;
    /* longest path in switches */
    int *d = xcalloc((size_t)nnodes, sizeof(int));
    for (int i = no - 1; i >= 0; i--) {
        Node *x = &nodes[order[i]];
        int m = 0;
        if (x->kind == N_SWITCH) {
            for (int c = 0; c < x->ncase; c++) if (d[x->cchild[c]] > m) m = d[x->cchild[c]];
            if (x->dflt >= 0 && d[x->dflt] > m) m = d[x->dflt];
            m++;
        }
        d[order[i]] = m;
    }
    *depth = d[root];
    free(d); free(seen); free(order);
    return no;
}

/* ----------------------------------------------------------- DAG as data */

void write_dag(const char *path) {
    FILE *o = fopen(path, "w");
    if (!o) die("cannot write %s", path);
    fprintf(o, "(dag-file \"%s\" \"%s\" %d %d)\n", module_name, theory_name, nrules, nnodes);
    for (int i = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->kind == N_FAIL || n->kind == N_LEAF) {
            fprintf(o, "(node %d %s", i, n->kind == N_FAIL ? "fail" : "leaf");
            for (int c = 0; c < n->ncand; c++) fprintf(o, " \"%s\"", rules[n->cand[c]].label);
            if (n->nexcl) {
                fprintf(o, " (excluded");
                for (int c = 0; c < n->nexcl; c++) fprintf(o, " \"%s\"", rules[n->excl[c]].label);
                fputc(')', o);
            }
            fprintf(o, ")\n");
        }
        else {
            fprintf(o, "(node %d switch (occ", i);
            for (int k = 0; k < occs[n->occ].len; k++) fprintf(o, " %d", occs[n->occ].p[k]);
            fprintf(o, ") (cases");
            for (int k = 0; k < n->ncase; k++) fprintf(o, " (\"%s\" %d)", ctors[n->cctor[k]].name, n->cchild[k]);
            fprintf(o, ") (default %d))\n", n->dflt);
        }
    }
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) fprintf(o, "(root \"%s\" %d)\n", ctors[c].name, ctors[c].dag_root);
    fclose(o);
}

