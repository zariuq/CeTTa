/* rulec_validate.c - independent validator of the decision DAGs rulec writes.
 *
 *   rulec_validate WIRE DAG
 *
 * It shares no code with rulec.  It reads the module's core rows (the wire) and the DAG file, and
 * for every judgment enumerates every path of its DAG, with the tests on the path (positive: the
 * subterm at an occurrence has constructor c; negative: it has none of the constructors of a
 * switch's cases).  It checks:
 *   W  well-formed tests: a switch tests an argument, or a child of an occurrence whose
 *      constructor the path has fixed, at an index below that constructor's arity; its cases are
 *      distinct data constructors of the occurrence's sort; a switch without a default lists them all;
 *   S  soundness of a leaf for each rule r it lists: every constructor of r's left side is a positive
 *      test of the path at its occurrence, so every input that reaches the leaf matches r (and the
 *      leaf's reads of r's variables are reads of subterms the path has fixed);
 *   E  exclusivity: a rule s the leaf does not list (for a failure leaf: any rule) applies to no input
 *      that satisfies the path's tests: some test of the path contradicts s's left side, or s's
 *      premises cannot hold for any input on the path (G below);
 *   X  a leaf listing several rules: no input on the path satisfies the premises of two of them (by G,
 *      or because both call one judgment on the same arguments first and their targets for that call
 *      clash, which needs that judgment to have one result: so it is by induction on derivations,
 *      over all leaves at once, and only when no rule queries a relation for unknowns);
 *   R  every rule of the judgment is reached by some leaf, or excluded wherever it is not.
 * G evaluates premises on the path's input (the constructors its positive tests fix, unknowns
 * elsewhere): a premise whose call has no result, or whose value clashes with its target, cannot hold.
 * A call's value is what every rule that may apply to it gives (each with its own premises evaluated
 * the same way); unknown when they differ, when a rule's result has an unknown of its own, or when a
 * bound on depth and work is reached.  This is written independently of rulec's evaluation.
 * Every input reaches exactly one leaf (each switch is exhaustive by W); by S, E and X at most one
 * rule applies to it, and that rule is listed at its leaf.  Paths whose tests contradict each other
 * are reached by no input and are skipped.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *f, ...) { va_list a; va_start(a, f); fputs("rulec_validate: ", stderr); vfprintf(stderr, f, a); fputc('\n', stderr); va_end(a); exit(2); }

/* minimal reader: atoms are symbols, strings or lists */
typedef struct X { int list; char *s; int n; struct X **v; } X;
static X *rd(const char **pp) {
    const char *p = *pp;
    while (isspace((unsigned char)*p)) p++;
    if (!*p) return NULL;
    X *x = calloc(1, sizeof *x);
    if (*p == '(') {
        p++; x->list = 1;
        int cap = 0;
        for (;;) {
            while (isspace((unsigned char)*p)) p++;
            if (*p == ')') { p++; break; }
            if (!*p) die("unterminated list");
            *pp = p; X *e = rd(pp); p = *pp;
            if (x->n == cap) { cap = cap ? 2 * cap : 8; x->v = realloc(x->v, (size_t)cap * sizeof(X *)); }
            x->v[x->n++] = e;
        }
    } else if (*p == '"') {
        p++;
        char *b = malloc(strlen(p) + 1); int n = 0;
        while (*p != '"') { if (!*p) die("unterminated string"); if (*p == '\\') p++; b[n++] = *p++; }
        p++; b[n] = 0; x->s = b;
    } else {
        const char *q = p;
        while (*q && !isspace((unsigned char)*q) && *q != '(' && *q != ')') q++;
        x->s = strndup(p, (size_t)(q - p)); p = q;
    }
    *pp = p;
    return x;
}
static bool is(const X *x, const char *s) { return x && !x->list && !strcmp(x->s, s); }
static bool hd(const X *x, const char *s) { return x && x->list && x->n && is(x->v[0], s); }

/* signature */
#define MAXC 4096
static char *cname[MAXC]; static int csort[MAXC], carity[MAXC], *cargs[MAXC], nc;
static bool judgment[MAXC];
static char *sname[1024]; static int ns;
static int sort_of(const char *s) { for (int i = 0; i < ns; i++) if (!strcmp(sname[i], s)) return i; die("unknown sort %s", s); return -1; }
static int ctor_of(const char *s) { for (int i = 0; i < nc; i++) if (!strcmp(cname[i], s)) return i; return -1; }

/* rules: left side, right side, premises; variables numbered per rule */
typedef struct P { int var; int c; int n; struct P **k; int vi; } P;   /* var: 1 variable (vi), 0 constructor */
typedef struct { bool rel; P *src, *tgt; } Q;                             /* a premise: call src ~> tgt, or a relation query */
typedef struct { char *label; int head; P *lhs, *rhs; int nv; Q *prem; int np; } R;
static R rules[MAXC]; static int nr;
static bool binding_queries;   /* some relation query has an argument with a variable unbound before it */
static char *vnames[256]; static int nvnames;   /* the variables of the rule being read */

static int list_items(X *x, X ***out) {
    int n = 0, cap = 0; X **v = NULL;
    while (hd(x, "LCons")) { if (n == cap) { cap = cap ? 2 * cap : 8; v = realloc(v, (size_t)cap * sizeof(X *)); } v[n++] = x->v[1]; x = x->v[2]; }
    if (!is(x, "LNil")) die("bad list");
    *out = v; return n;
}
static P *pat(X *x) {
    P *p = calloc(1, sizeof *p);
    if (hd(x, "FVar")) {
        p->var = 1; p->vi = -1;
        for (int i = 0; i < nvnames; i++) if (!strcmp(vnames[i], x->v[1]->s)) p->vi = i;
        if (p->vi < 0) { if (nvnames == 256) die("too many variables in a rule"); vnames[nvnames] = x->v[1]->s; p->vi = nvnames++; }
        return p;
    }
    if (!hd(x, "PApp")) die("bad pattern");
    p->c = ctor_of(x->v[1]->s);
    if (p->c < 0) die("unknown constructor %s", x->v[1]->s);
    X **it; p->n = list_items(x->v[2], &it);
    p->k = calloc((size_t)(p->n ? p->n : 1), sizeof(P *));
    for (int i = 0; i < p->n; i++) p->k[i] = pat(it[i]);
    free(it);
    return p;
}

static void mark_vars(const P *p, bool *bound) {
    if (p->var) { bound[p->vi] = true; return; }
    for (int i = 0; i < p->n; i++) mark_vars(p->k[i], bound);
}
static bool all_bound(const P *p, const bool *bound) {
    if (p->var) return bound[p->vi];
    for (int i = 0; i < p->n; i++) if (!all_bound(p->k[i], bound)) return false;
    return true;
}

static P *vflatten(R *r, P *p, Q *out, int *nout) {
    if (p->var) return p;
    for (int i = 0; i < p->n; i++) p->k[i] = vflatten(r, p->k[i], out, nout);
    if (!judgment[p->c]) return p;
    if (r->nv == 256) die("too many variables in rule %s", r->label);
    P *v = calloc(1, sizeof *v);
    v->var = 1; v->vi = r->nv++;
    if (*nout == 512) die("too many premises");
    out[*nout].rel = false; out[*nout].src = p; out[*nout].tgt = v;
    (*nout)++;
    P *w = calloc(1, sizeof *w);
    w->var = 1; w->vi = v->vi;
    return w;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) die("cannot open %s", path);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) die("read %s", path); b[n] = 0; fclose(f); return b;
}

/* DAG */
typedef struct { int kind; int rule; int occ[64]; int olen; int ncase; int *cc; int *ch; int dflt; int ncand; int cand[64]; } N;   /* kind 0 fail 1 leaf 2 switch */
static N *nodes; static int nn;
static int root_of[MAXC];

/* path tests */
typedef struct { int occ[64]; int olen; int pos; int c; int nneg; int *neg; } Test;   /* pos: 1 positive (c), 0 negative (neg[]) */
static Test path[512]; static int depth;
static long npaths, nerr, nleaves;
static bool *rule_reached;
static int cur_j;

static bool occ_eq(const int *a, int al, const int *b, int bl) { return al == bl && !memcmp(a, b, (size_t)al * sizeof(int)); }

/* the constraint a left side puts on an occurrence: -1 none (a variable at or above it), else its constructor */
static int lhs_at(const P *lhs, const int *occ, int olen) {
    const P *p = lhs->k[occ[0]];
    for (int l = 1; l < olen; l++) {
        if (p->var) return -1;
        if (occ[l] >= p->n) return -2;   /* not a subterm of this constructor: contradiction */
        p = p->k[occ[l]];
    }
    return p->var ? -1 : p->c;
}
/* is some test of the path contradicted by rule s's left side? */
static bool contradicts(const P *lhs) {
    for (int t = 0; t < depth; t++) {
        /* a test below a constructor of s that differs from the path's test on the parent is already
           caught at the parent; a test whose parent is not fixed by s leaves s unconstrained */
        int c = lhs_at(lhs, path[t].occ, path[t].olen);
        if (c == -1) continue;
        if (c == -2) return true;
        if (path[t].pos) { if (c != path[t].c) return true; }
        else for (int i = 0; i < path[t].nneg; i++) if (path[t].neg[i] == c) return true;
    }
    return false;
}
/* does the path fix the constructor at an occurrence? -1 no */
static int fixed_at(const int *occ, int olen) {
    for (int t = 0; t < depth; t++) if (path[t].pos && occ_eq(path[t].occ, path[t].olen, occ, olen)) return path[t].c;
    return -1;
}
static bool excluded_at(const int *occ, int olen, int c) {
    for (int t = 0; t < depth; t++) if (!path[t].pos && occ_eq(path[t].occ, path[t].olen, occ, olen))
        for (int i = 0; i < path[t].nneg; i++) if (path[t].neg[i] == c) return true;
    return false;
}
/* sort of an occurrence along the path (its parents must be fixed) */
static int occ_sort(const int *occ, int olen) {
    int s = cargs[cur_j][occ[0]];
    for (int l = 1; l < olen; l++) {
        int c = fixed_at(occ, l);
        if (c < 0) return -1;
        if (occ[l] >= carity[c]) return -2;
        s = cargs[c][occ[l]];
    }
    return s;
}
static void report(const char *what) {
    nerr++;
    if (nerr <= 40) {
        fprintf(stderr, "rulec_validate: %s: %s; path:", cname[cur_j], what);
        for (int t = 0; t < depth; t++) {
            fputc(' ', stderr);
            for (int l = 0; l < path[t].olen; l++) fprintf(stderr, "%s%d", l ? "." : "", path[t].occ[l]);
            if (path[t].pos) fprintf(stderr, "=%s", cname[path[t].c]); else fprintf(stderr, "!=(%d ctors)", path[t].nneg);
        }
        fputc('\n', stderr);
    }
}

/* ---- G: premises evaluated on a path's input ---- */
typedef struct { int c; int n; int k[16]; int ref; } GN;   /* c >= 0 a constructor; c == -1 an unknown (ref: its value, or -1) */
static GN *gn; static int ngn, gncap;
static int *gtrail; static int ngtrail, gtrailcap;
static long gwork;
enum { G_UNKNOWN = -1, G_NONE = -2 };
static int gnew(int c, int n) {
    if (ngn == gncap) { gncap = gncap ? 2 * gncap : 4096; gn = realloc(gn, (size_t)gncap * sizeof(GN)); if (!gn) die("out of memory"); }
    gn[ngn].c = c; gn[ngn].n = n; gn[ngn].ref = -1;
    return ngn++;
}
static int gderef(int t) { while (gn[t].c < 0 && gn[t].ref >= 0) t = gn[t].ref; return t; }
static bool goccurs(int v, int t) {
    t = gderef(t);
    if (t == v) return true;
    for (int i = 0; i < gn[t].n; i++) if (goccurs(v, gn[t].k[i])) return true;
    return false;
}
static void gbind(int v, int t) {
    gn[v].ref = t;
    if (ngtrail == gtrailcap) { gtrailcap = gtrailcap ? 2 * gtrailcap : 1024; gtrail = realloc(gtrail, (size_t)gtrailcap * sizeof(int)); if (!gtrail) die("out of memory"); }
    gtrail[ngtrail++] = v;
}
static void gundo(int mark) { while (ngtrail > mark) gn[gtrail[--ngtrail]].ref = -1; }
static bool gunify(int a, int b) {
    a = gderef(a); b = gderef(b);
    if (a == b) return true;
    if (gn[a].c < 0) { if (goccurs(a, b)) return false; gbind(a, b); return true; }
    if (gn[b].c < 0) { if (goccurs(b, a)) return false; gbind(b, a); return true; }
    if (gn[a].c != gn[b].c) return false;
    for (int i = 0; i < gn[a].n; i++) if (!gunify(gn[a].k[i], gn[b].k[i])) return false;
    return true;
}
static bool gsame(int a, int b) {
    a = gderef(a); b = gderef(b);
    if (a == b) return true;
    if (gn[a].c < 0 || gn[b].c < 0 || gn[a].c != gn[b].c) return false;
    for (int i = 0; i < gn[a].n; i++) if (!gsame(gn[a].k[i], gn[b].k[i])) return false;
    return true;
}
/* a pattern with the rule's unknowns vars[] (made on first use) */
static int ginst(const P *p, int *vars) {
    if (p->var) { if (vars[p->vi] < 0) vars[p->vi] = gnew(-1, 0); return vars[p->vi]; }
    if (p->n > 16) die("constructor arity above 16");
    int k[16];
    for (int i = 0; i < p->n; i++) k[i] = ginst(p->k[i], vars);
    int t = gnew(p->c, p->n);
    memcpy(gn[t].k, k, (size_t)p->n * sizeof(int));
    return t;
}
/* t with known unknowns replaced by their values; *own when an unknown made since `since` remains */
static int gcopy(int t, int since, bool *own) {
    t = gderef(t);
    if (gn[t].c < 0) { if (t >= since) *own = true; return t; }
    if (!gn[t].n) return t;
    int k[16], n = gn[t].n, c = gn[t].c;
    for (int i = 0; i < n; i++) k[i] = gcopy(gn[t].k[i], since, own);
    int u = gnew(c, n);
    memcpy(gn[u].k, k, (size_t)n * sizeof(int));
    return u;
}
static int gcall(int t, int depth);
static bool gprems(const R *r, int *vars, int depth) {
    for (int j = 0; j < r->np; j++) {
        const Q *q = &r->prem[j];
        if (q->rel) continue;
        int v = gcall(ginst(q->src, vars), depth + 1);
        if (v == G_NONE) return false;
        if (v == G_UNKNOWN) continue;
        if (!gunify(v, ginst(q->tgt, vars))) return false;
    }
    return true;
}
/* the value of a call: G_NONE when no rule can apply, a term every applicable rule gives, or G_UNKNOWN */
static int gcall(int t, int depth) {
    t = gderef(t);
    if (gn[t].c < 0 || !judgment[gn[t].c]) return G_UNKNOWN;
    if (depth > 64 || ++gwork > 400000) return G_UNKNOWN;
    int val = G_NONE, head = gn[t].c;
    for (int r = 0; r < nr; r++) {
        if (rules[r].head != head) continue;
        int mark = ngtrail, since = ngn, vars[256];
        for (int v = 0; v < rules[r].nv; v++) vars[v] = -1;
        if (!gunify(ginst(rules[r].lhs, vars), t) || !gprems(&rules[r], vars, depth)) { gundo(mark); continue; }
        bool own = false;
        int res = gcopy(ginst(rules[r].rhs, vars), since, &own);
        gundo(mark);
        if (own) return G_UNKNOWN;
        if (val == G_NONE) val = res;
        else if (!gsame(val, res)) return G_UNKNOWN;
    }
    return val;
}
/* the input on the path: the constructors fixed by positive tests, unknowns elsewhere */
static int gpath(int *occ, int olen) {
    int c = fixed_at(occ, olen);
    if (c < 0) return gnew(-1, 0);
    int k[16];
    if (carity[c] > 16) die("constructor arity above 16");
    for (int i = 0; i < carity[c]; i++) { occ[olen] = i; k[i] = gpath(occ, olen + 1); }
    int t = gnew(c, carity[c]);
    memcpy(gn[t].k, k, (size_t)carity[c] * sizeof(int));
    return t;
}
static int gpath_input(void) {
    ngn = 0; ngtrail = 0; gwork = 0;
    int k[16], occ[64];
    for (int a = 0; a < carity[cur_j]; a++) { occ[0] = a; k[a] = gpath(occ, 1); }
    int t = gnew(cur_j, carity[cur_j]);
    memcpy(gn[t].k, k, (size_t)carity[cur_j] * sizeof(int));
    return t;
}
/* E by G: rule s's premises cannot hold for any input on the path */
static bool g_excluded(int s) {
    int in = gpath_input(), vars[256];
    for (int v = 0; v < rules[s].nv; v++) vars[v] = -1;
    return !gunify(ginst(rules[s].lhs, vars), in) || !gprems(&rules[s], vars, 0);
}
/* X: no input on the path satisfies the premises of both a and b */
static bool g_exclusive(int a, int b) {
    int in = gpath_input(), va[256], vb[256];
    for (int v = 0; v < rules[a].nv; v++) va[v] = -1;
    for (int v = 0; v < rules[b].nv; v++) vb[v] = -1;
    if (!gunify(ginst(rules[a].lhs, va), in) || !gunify(ginst(rules[b].lhs, vb), in)) return true;
    if (!binding_queries)
        for (int j = 0; j < rules[a].np && j < rules[b].np; j++) {
            const Q *qa = &rules[a].prem[j], *qb = &rules[b].prem[j];
            if (qa->rel || qb->rel || qa->src->c != qb->src->c) break;
            if (!gsame(ginst(qa->src, va), ginst(qb->src, vb))) break;
            if (!gunify(ginst(qa->tgt, va), ginst(qb->tgt, vb))) return true;
        }
    return !gprems(&rules[a], va, 0) || !gprems(&rules[b], vb, 0);
}
static long nexcluded, nexclusive_pairs;

static void walk(int id) {
    N *x = &nodes[id];
    if (x->kind == 2) {
        int s = occ_sort(x->occ, x->olen);
        if (x->occ[0] >= carity[cur_j] || s < 0) { report("switch on an occurrence the path has not fixed"); return; }
        /* W: cases distinct data constructors of the sort; complete when no default */
        int ndata = 0;
        for (int c = 0; c < nc; c++) if (!judgment[c] && csort[c] == s) ndata++;
        for (int i = 0; i < x->ncase; i++) {
            if (judgment[x->cc[i]] || csort[x->cc[i]] != s) report("case is not a data constructor of the occurrence's sort");
            for (int j = 0; j < i; j++) if (x->cc[j] == x->cc[i]) report("case listed twice");
        }
        if (x->dflt < 0 && x->ncase != ndata) report("switch without default does not list every constructor");
        int fixed = fixed_at(x->occ, x->olen);
        for (int i = 0; i < x->ncase; i++) {
            if (fixed >= 0 && fixed != x->cc[i]) continue;                  /* no input takes this edge */
            if (excluded_at(x->occ, x->olen, x->cc[i])) continue;
            Test *t = &path[depth++];
            memcpy(t->occ, x->occ, sizeof t->occ); t->olen = x->olen; t->pos = 1; t->c = x->cc[i]; t->nneg = 0;
            walk(x->ch[i]);
            depth--;
        }
        if (x->dflt >= 0) {
            bool feasible = true;
            if (fixed >= 0) for (int i = 0; i < x->ncase; i++) if (x->cc[i] == fixed) feasible = false;
            if (feasible) {
                Test *t = &path[depth++];
                memcpy(t->occ, x->occ, sizeof t->occ); t->olen = x->olen; t->pos = 0; t->nneg = x->ncase; t->neg = x->cc;
                walk(x->dflt);
                depth--;
            }
        }
        return;
    }
    npaths++;
    int ncand = x->kind == 1 ? x->ncand : 0;
    for (int ci = 0; ci < ncand; ci++) {
        int r = x->cand[ci];
        nleaves++;
        if (rules[r].head != cur_j) { report("leaf names a rule of another judgment"); return; }
        rule_reached[r] = true;
        /* S: every constructor of r's left side is fixed by the path */
        struct { const P *p; int occ[64]; int olen; } st[1024]; int sp = 0;
        for (int a = 0; a < rules[r].lhs->n; a++) { st[sp].p = rules[r].lhs->k[a]; st[sp].occ[0] = a; st[sp].olen = 1; sp++; }
        while (sp) {
            sp--;
            const P *p = st[sp].p; int occ[64]; int olen = st[sp].olen;
            memcpy(occ, st[sp].occ, sizeof occ);
            if (p->var) continue;
            if (fixed_at(occ, olen) != p->c) { char m[256]; snprintf(m, sizeof m, "leaf of rule \"%s\" can be reached by an input it does not match", rules[r].label); report(m); break; }
            for (int i = 0; i < p->n; i++) { st[sp].p = p->k[i]; memcpy(st[sp].occ, occ, sizeof occ); st[sp].occ[olen] = i; st[sp].olen = olen + 1; sp++; }
        }
    }
    /* X: the rules of a leaf with several are pairwise exclusive on the path */
    for (int i = 0; i < ncand; i++)
        for (int j = i + 1; j < ncand; j++) {
            if (g_exclusive(x->cand[i], x->cand[j])) { nexclusive_pairs++; continue; }
            char m[256];
            snprintf(m, sizeof m, "rules \"%s\" and \"%s\" share a leaf and are not shown exclusive", rules[x->cand[i]].label, rules[x->cand[j]].label);
            report(m);
        }
    /* E: every rule the leaf does not list is contradicted by the path, or excluded by G */
    for (int s = 0; s < nr; s++) {
        if (rules[s].head != cur_j) continue;
        bool listed = false;
        for (int ci = 0; ci < ncand; ci++) if (x->cand[ci] == s) listed = true;
        if (listed) continue;
        if (!contradicts(rules[s].lhs) && g_excluded(s)) { nexcluded++; continue; }
        if (!contradicts(rules[s].lhs)) {
            char m[256];
            snprintf(m, sizeof m, "an input matching rule \"%s\" reaches %s%s%s", rules[s].label, ncand ? "the leaf of rule \"" : "a failure", ncand ? rules[x->cand[0]].label : "", ncand ? "\"" : "");
            report(m);
        }
    }
}

/* ------------------------------------------------- generated C against the DAG
 * The dispatch code rulec writes is regular: per judgment J a procedure p<J> (p<J>_rules when J has a
 * native operation) whose first statement jumps to the root, one line
 *   n<id>: switch (RC_TAG_<rep>(<occurrence>)) { case <ctor>: goto <label>; ... default: goto <label>; }
 * per switch node, one block "r<id>: { /" "* <label> *" "/" containing RC_RULE(<id>) per rule alone at a
 * leaf, and one block "l<node>: {" per leaf with several rules, holding a block per rule in the leaf's
 * order ("{ /" "* <label> *" "/" for the first, "c<node>_<i>: { ..." for the others), each ending its
 * trial with rc_trial_end(tm_, <rule id>).
 * Labels: r<rule> a leaf of one rule, l<node> a leaf of several, f a failure, n<id> a switch.  This
 * checks that the code is the DAG. */
static int parse_occ(const char *s, int *occ, int *olen) {   /* returns characters consumed or -1 */
    const char *p = s;
    if (!strncmp(p, "RC_KID_", 7)) {
        p += 7;
        while (isalnum((unsigned char)*p) || *p == '_') p++;
        if (*p++ != '(') return -1;
        int n = parse_occ(p, occ, olen);
        if (n < 0) return -1;
        p += n;
        if (*p++ != ',' || *p++ != ' ') return -1;
        char *e; long k = strtol(p, &e, 10);
        if (e == p || *e != ')') return -1;
        occ[(*olen)++] = (int)k;
        p = e + 1;
        return (int)(p - s);
    }
    if (*p != 'a') return -1;
    char *e; long a = strtol(p + 1, &e, 10);
    if (e == p + 1) return -1;
    *olen = 0;
    occ[(*olen)++] = (int)a;
    return (int)(e - s);
}
static bool label_is(const char *lab, int child) {
    N *x = &nodes[child];
    char want[32];
    if (x->kind == 1 && x->ncand > 1) snprintf(want, sizeof want, "l%d", child);
    else if (x->kind == 1) snprintf(want, sizeof want, "r%d", x->rule);
    else if (x->kind == 0) snprintf(want, sizeof want, "f");
    else snprintf(want, sizeof want, "n%d", child);
    return !strcmp(lab, want);
}
static void reach(int id, bool *seen) {
    if (seen[id]) return;
    seen[id] = true;
    if (nodes[id].kind == 2) { for (int i = 0; i < nodes[id].ncase; i++) reach(nodes[id].ch[i], seen); if (nodes[id].dflt >= 0) reach(nodes[id].dflt, seen); }
}
static long check_c(const char *cpath) {
    char *src = slurp(cpath);
    long bad = 0, nsw = 0, nblk = 0;
    int cur = -1;            /* judgment of the current procedure body (the one with the DAG) */
    bool expect_root = false;
    bool *seen_sw = calloc((size_t)nn, 1), *seen_blk = calloc((size_t)nr, 1), *proc_seen = calloc(MAXC, 1);
    bool *reachable = calloc((size_t)nn, 1);
    int blk_rule = -1;
    int trial = -1, trial_i = 0, trial_rule = -1;   /* the trial block being read, its next candidate, the current one */
    bool *seen_trial = calloc((size_t)nn, 1);
    long ntrial = 0;
#define BAD(...) do { bad++; if (bad <= 20) { fprintf(stderr, "rulec_validate: generated C: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
    for (char *line = strtok(src, "\n"); line; line = strtok(NULL, "\n")) {
        int j; char tail[64];
        if (trial >= 0 && (line[0] == 'r' || line[0] == 'l' || line[0] == 'n' || line[0] == 'f' || line[0] == 's' || line[0] == '}')) {
            if (line[0] != '}' || line[1] == 0) {
                if (trial_i != nodes[trial].ncand) BAD("trial block l%d lists %d of its leaf's %d rules", trial, trial_i, nodes[trial].ncand);
                trial = -1;
            }
        }
        if (sscanf(line, "static T *p%d%63[^(]", &j, tail) >= 1 && strchr(line, '{')) {
            bool rules_version = !strncmp(tail, "_rules", 6);
            if (j < 0 || j >= nc || !judgment[j]) { BAD("procedure p%d is not a judgment", j); cur = -1; continue; }
            /* the body with the DAG: p<J>_rules when it exists, else p<J> */
            bool has_rules_version = strstr(src + (line - src) + strlen(line) + 1, "") != NULL && false;
            (void)has_rules_version;
            if (rules_version || !proc_seen[j]) {
                cur = j; expect_root = true;
                memset(reachable, 0, (size_t)nn);
                if (root_of[j] >= 0) reach(root_of[j], reachable);
            } else cur = -1;
            if (rules_version) proc_seen[j] = true;
            continue;
        }
        if (cur < 0) continue;
        const char *p = line;
        while (*p == ' ') p++;
        if (expect_root && !strncmp(p, "goto ", 5)) {
            char lab[32];
            if (sscanf(p, "goto %31[^;];", lab) == 1 && !label_is(lab, root_of[cur])) BAD("p%d (%s) starts at %s, not at its DAG root", cur, cname[cur], lab);
            expect_root = false;
            continue;
        }
        if (expect_root && strncmp(p, "rc_roots", 8) && strncmp(p, "rc_safepoint", 12) && strncmp(p, "a", 1) && strncmp(p, "if (rc_trace_on)", 16) && strncmp(p, "#", 1))
            { /* native-operation wrapper or safepoint prelude lines are skipped until the root jump */ }
        int id;
        if (line[0] == 'n' && sscanf(line, "n%d: switch (RC_TAG_", &id) == 1) {
            nsw++;
            if (id < 0 || id >= nn || nodes[id].kind != 2) { BAD("n%d is not a switch node", id); continue; }
            if (!reachable[id]) { BAD("n%d in p%d is not reachable from %s's root", id, cur, cname[cur]); continue; }
            if (seen_sw[id] && 0) {}
            seen_sw[id] = true;
            N *x = &nodes[id];
            const char *q = strchr(line, '(');                /* "(RC_TAG_rep(" */
            q = strchr(q + 1, '(') + 1;                       /* start of the occurrence */
            int occ[64], olen = 0;
            int used = parse_occ(q, occ, &olen);
            if (used < 0 || !occ_eq(occ, olen, x->occ, x->olen)) { BAD("n%d tests another occurrence than the DAG", id); continue; }
            /* cases */
            int ncase = 0; const char *c = strstr(line, "{");
            bool ok = true; char dlab[32] = "";
            for (const char *t = c; (t = strstr(t, "case ")); ) {
                int ctor; char lab[32];
                if (sscanf(t, "case %d: goto %31[^;];", &ctor, lab) != 2) { ok = false; break; }
                if (ncase >= x->ncase || x->cc[ncase] != ctor || !label_is(lab, x->ch[ncase])) ok = false;
                ncase++;
                t += 5;
            }
            const char *dd = strstr(line, "default: goto ");
            if (!dd || sscanf(dd, "default: goto %31[^;];", dlab) != 1) ok = false;
            else if (x->dflt >= 0 ? !label_is(dlab, x->dflt) : strcmp(dlab, "f")) ok = false;
            if (!ok || ncase != x->ncase) BAD("n%d's cases differ from the DAG", id);
            continue;
        }
        if (line[0] == 'r' && sscanf(line, "r%d: { /*", &id) == 1) {
            nblk++;
            if (id < 0 || id >= nr) { BAD("block r%d names no rule", id); blk_rule = -1; continue; }
            const char *lb = strstr(line, "/* ");
            char label[256];
            if (!lb || sscanf(lb, "/* %255s", label) != 1 || strcmp(label, rules[id].label)) BAD("block r%d is not labelled %s", id, rules[id].label);
            if (rules[id].head != cur) BAD("block of rule %s is in p%d, not in its judgment's procedure", rules[id].label, cur);
            seen_blk[id] = true;
            blk_rule = id;
            continue;
        }
        if (line[0] == 'l' && sscanf(line, "l%d: {", &id) == 1) {
            if (id < 0 || id >= nn || nodes[id].kind != 1 || nodes[id].ncand < 2) { BAD("l%d is not a leaf of several rules", id); continue; }
            if (!reachable[id]) { BAD("l%d in p%d is not reachable from %s's root", id, cur, cname[cur]); continue; }
            seen_trial[id] = true; ntrial++;
            trial = id; trial_i = 0; trial_rule = nodes[id].cand[0];
            continue;
        }
        if (trial >= 0) {
            int ci, cn;
            const char *lb = strstr(p, "/* ");
            bool first = !strncmp(p, "{ /* ", 5), later = sscanf(p, "c%d_%d: { /*", &cn, &ci) == 2;
            if ((first || later) && lb) {
                char label[256];
                if (trial_i >= nodes[trial].ncand) { BAD("trial block l%d has more rules than its leaf", trial); continue; }
                if (later && (cn != trial || ci != trial_i)) BAD("trial block l%d: candidate label c%d_%d out of order", trial, cn, ci);
                if (first && trial_i != 0) BAD("trial block l%d: an unlabelled candidate after the first", trial);
                trial_rule = nodes[trial].cand[trial_i];
                if (sscanf(lb, "/* %255s", label) != 1 || strcmp(label, rules[trial_rule].label)) BAD("trial block l%d: candidate %d is not %s", trial, trial_i, rules[trial_rule].label);
                trial_i++;
                continue;
            }
            const char *te = strstr(p, "rc_trial_end(tm_, ");
            int tr;
            if (te && sscanf(te, "rc_trial_end(tm_, %d)", &tr) == 1 && tr != trial_rule) BAD("trial block l%d: a candidate of rule %d ends its trial as rule %d", trial, trial_rule, tr);
            continue;
        }
        int rr;
        if (blk_rule >= 0 && sscanf(p, "RC_RULE(%d);", &rr) == 1) {
            if (rr != blk_rule) BAD("block r%d records rule %d", blk_rule, rr);
            blk_rule = -1;
        }
    }
    for (int c = 0; c < nc; c++) {
        if (!judgment[c] || root_of[c] < 0) continue;
        bool *rch = calloc((size_t)nn, 1);
        reach(root_of[c], rch);
        for (int i = 0; i < nn; i++) if (rch[i] && nodes[i].kind == 2 && !seen_sw[i]) BAD("switch node %d of %s is not in the code", i, cname[c]);
        free(rch);
    }
    /* every rule alone at a reachable leaf has its block; every reachable leaf of several rules its trial block */
    bool *alone = calloc((size_t)nr, 1);
    for (int c = 0; c < nc; c++) {
        if (!judgment[c] || root_of[c] < 0) continue;
        bool *rch = calloc((size_t)nn, 1);
        reach(root_of[c], rch);
        for (int i = 0; i < nn; i++) {
            if (!rch[i] || nodes[i].kind != 1) continue;
            if (nodes[i].ncand == 1) alone[nodes[i].rule] = true;
            else if (!seen_trial[i]) BAD("leaf %d of %s (several rules) has no trial block", i, cname[c]);
        }
        free(rch);
    }
    for (int r = 0; r < nr; r++) if (alone[r] && !seen_blk[r]) BAD("rule %s has no block", rules[r].label);
    free(alone); free(seen_trial);
    printf("rulec_validate: generated C: %ld switch lines, %ld rule blocks, %ld trial blocks checked against the DAG: %ld problems\n", nsw, nblk, ntrial, bad);
    free(src);
    return bad;
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) die("usage: rulec_validate WIRE DAG [GENERATED.c]");
    char *w = slurp(argv[1]);
    static char *rows_seen_name[MAXC * 2], *rows_seen_text[MAXC * 2]; static bool rows_seen_kind[MAXC * 2]; int nrows_seen = 0;
    for (char *line = strtok(w, "\n"); line; line = strtok(NULL, "\n")) {
        const char *p = line;
        const char *line_text = line;
        X *x = rd(&p);
        if (!x) continue;
        /* a declaration repeated identically (joined theories share it) is one declaration */
        if (hd(x, "TypeDecl") || hd(x, "GrammarRuleV2")) {
            bool seen = false;
            for (int i = 0; i < nrows_seen && !seen; i++)
                if (!strcmp(rows_seen_name[i], x->v[1]->s) && rows_seen_kind[i] == hd(x, "TypeDecl")) {
                    if (strcmp(rows_seen_text[i], line_text)) die("%s declared twice, differently", x->v[1]->s);
                    seen = true;
                }
            if (seen) continue;
            if (nrows_seen == MAXC * 2) die("too many declarations");
            rows_seen_name[nrows_seen] = x->v[1]->s; rows_seen_kind[nrows_seen] = hd(x, "TypeDecl");
            rows_seen_text[nrows_seen++] = strdup(line_text);
        }
        if (hd(x, "TypeDecl")) sname[ns++] = x->v[1]->s;
        else if (hd(x, "GrammarRuleV2")) {
            if (nc == MAXC) die("too many constructors");
            cname[nc] = x->v[1]->s; csort[nc] = sort_of(x->v[2]->s);
            X **ps; carity[nc] = list_items(x->v[3], &ps);
            cargs[nc] = calloc((size_t)(carity[nc] ? carity[nc] : 1), sizeof(int));
            for (int i = 0; i < carity[nc]; i++) cargs[nc][i] = sort_of(ps[i]->v[2]->v[1]->s);
            free(ps);
            nc++;
        } else if (hd(x, "ScopedRewriteRuleV1")) {
            R *r = &rules[nr];
            nvnames = 0;
            r->label = x->v[1]->s;
            r->lhs = pat(x->v[4]);
            r->rhs = pat(x->v[5]);
            r->head = r->lhs->c;
            X **ps; r->np = list_items(x->v[3], &ps);
            r->prem = calloc((size_t)(r->np ? r->np : 1), sizeof(Q));
            /* variables bound so far: those of the left side, then each premise's target */
            bool bound[256] = {0};
            mark_vars(r->lhs, bound);
            for (int j = 0; j < r->np; j++) {
                if (hd(ps[j], "Congruence")) {
                    r->prem[j].src = pat(ps[j]->v[1]); r->prem[j].tgt = pat(ps[j]->v[2]);
                    mark_vars(r->prem[j].tgt, bound);
                } else if (hd(ps[j], "RelationQuery")) {
                    r->prem[j].rel = true;
                    X **as; int na = list_items(ps[j]->v[2], &as);
                    for (int a = 0; a < na; a++) { P *q = pat(as[a]); if (!all_bound(q, bound)) binding_queries = true; mark_vars(q, bound); }
                    free(as);
                } else die("unknown premise form in rule %s", r->label);
            }
            free(ps);
            r->nv = nvnames;
            judgment[r->head] = true;
            nr++;
        }
    }
    /* rules in functional form: a call nested in a premise's arguments or the right side is made a
     * premise before it (call by value, innermost first, left to right; the right side's after every
     * premise), as the module's reading of such rules says; written here independently of rulec */
    for (int i = 0; i < nr; i++) {
        R *r = &rules[i];
        static Q out[512]; int nout = 0;
        for (int j = 0; j < r->np; j++) {
            if (!r->prem[j].rel) for (int a = 0; a < r->prem[j].src->n; a++) r->prem[j].src->k[a] = vflatten(r, r->prem[j].src->k[a], out, &nout);
            if (nout == 512) die("too many premises");
            out[nout++] = r->prem[j];
        }
        r->rhs = vflatten(r, r->rhs, out, &nout);
        if (nout != r->np) {
            r->prem = realloc(r->prem, (size_t)nout * sizeof(Q));
            memcpy(r->prem, out, (size_t)nout * sizeof(Q));
            r->np = nout;
        }
    }
    char *d = slurp(argv[2]);
    for (int i = 0; i < MAXC; i++) root_of[i] = -1;
    int declared_nodes = -1;
    for (char *line = strtok(d, "\n"); line; line = strtok(NULL, "\n")) {
        const char *p = line;
        X *x = rd(&p);
        if (!x) continue;
        if (hd(x, "dag-file")) {
            if (atoi(x->v[3]->s) != nr) die("the DAG file was written for %s rules, the wire has %d", x->v[3]->s, nr);
            declared_nodes = atoi(x->v[4]->s);
            nodes = calloc((size_t)declared_nodes, sizeof(N));
        } else if (hd(x, "node")) {
            int id = atoi(x->v[1]->s);
            if (!nodes || id < 0 || id >= declared_nodes) die("node id out of range");
            N *n = &nodes[id];
            if (id >= nn) nn = id + 1;
            if (is(x->v[2], "fail")) n->kind = 0;   /* an (excluded ...) list is checked by E as any unlisted rule */
            else if (is(x->v[2], "leaf")) {
                n->kind = 1; n->rule = -1; n->ncand = 0;
                for (int c = 3; c < x->n; c++) {
                    if (hd(x->v[c], "excluded")) continue;
                    int found = -1;
                    for (int r = 0; r < nr; r++) if (!strcmp(rules[r].label, x->v[c]->s)) found = r;
                    if (found < 0) die("leaf names an unknown rule %s", x->v[c]->s);
                    if (n->ncand == 64) die("leaf lists too many rules");
                    n->cand[n->ncand++] = found;
                }
                if (!n->ncand) die("leaf lists no rule");
                n->rule = n->cand[0];
            } else if (is(x->v[2], "switch")) {
                n->kind = 2;
                X *o = x->v[3]; n->olen = o->n - 1;
                for (int i = 1; i < o->n; i++) n->occ[i - 1] = atoi(o->v[i]->s);
                X *cs = x->v[4]; n->ncase = cs->n - 1;
                n->cc = calloc((size_t)(n->ncase ? n->ncase : 1), sizeof(int)); n->ch = calloc((size_t)(n->ncase ? n->ncase : 1), sizeof(int));
                for (int i = 1; i < cs->n; i++) {
                    n->cc[i - 1] = ctor_of(cs->v[i]->v[0]->s);
                    if (n->cc[i - 1] < 0) die("case names an unknown constructor");
                    n->ch[i - 1] = atoi(cs->v[i]->v[1]->s);
                }
                n->dflt = atoi(x->v[5]->v[1]->s);
            } else die("unknown node kind");
        } else if (hd(x, "root")) {
            int c = ctor_of(x->v[1]->s);
            if (c < 0 || !judgment[c]) die("root for %s, which heads no rule", x->v[1]->s);
            root_of[c] = atoi(x->v[2]->s);
        }
    }
    rule_reached = calloc((size_t)nr, 1);
    int nj = 0;
    for (int c = 0; c < nc; c++) {
        if (!judgment[c]) continue;
        nj++;
        if (root_of[c] < 0) { cur_j = c; report("judgment has no DAG"); continue; }
        cur_j = c;
        depth = 0;
        walk(root_of[c]);
    }
    for (int r = 0; r < nr; r++) if (!rule_reached[r]) { cur_j = rules[r].head; char m[256]; snprintf(m, sizeof m, "rule \"%s\" is reached by no leaf", rules[r].label); report(m); }
    printf("rulec_validate: %d judgments, %d rules, %d nodes, %ld paths (%ld rule leaves; %ld exclusions by premises, %ld exclusive pairs at shared leaves): %ld problems\n",
           nj, nr, nn, npaths, nleaves, nexcluded, nexclusive_pairs, nerr);
    long cbad = argc == 4 ? check_c(argv[3]) : 0;
    return nerr || cbad ? 1 : 0;
}
