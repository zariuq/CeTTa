/* rulec.c - compiler for rule modules: moded procedures with decision DAGs.
 *
 * Input: the rows of a module's core exactly as CeTTa's module reader produces them
 * (front/dump_wire.metta prints them): TypeDecl, GrammarRuleV2, ScopedRewriteRuleV1.
 * The module stays the specification; nothing here adds meaning to it.
 *
 *   rulec WIRE [--layers L=label,...] [--census FILE] [--census-tsv FILE] [--dag FILE]
 *         [--emit FILE.c] [--safepoint NAME] [--native-sort SORT=KIND]... [--native-op NAME=FN]...
 *
 * Stages:
 *   1. Rule IR.  Signature layer: sorts, constructors with argument and result sorts.  Procedure
 *      layer: every symbol that heads a left side is a judgment; its rules are its dispatch group.
 *   2. Checks (any failure fails the build): declared constructors with their arities, declared
 *      variables, well-sorted sides and premises, constructor discipline (left sides, premise
 *      arguments and right sides contain no judgment), linear left sides, premise sources bound,
 *      premise targets fresh, right sides bound, no two left sides of one judgment unify.
 *   3. Modes and determinism.  Every judgment is used in the mode (in, ..., in) -> out.  A judgment
 *      is det when its group covers every input and every premise of every rule calls a det
 *      judgment with an irrefutable target (greatest fixpoint); otherwise semidet (non-overlap
 *      gives at most one result).  Termination is not analysed.
 *   4. Decision DAGs: the group's left sides as a pattern matrix, compiled column by column
 *      (specialisation by constructor, default matrix), nodes hash-consed into a DAG.  The DAG is
 *      written as data for the independent validator (rulec_validate.c).
 *   5. C: one procedure per judgment; the DAG as a switch on constructor tags; a leaf binds the
 *      rule's variables, runs the premises in written order, matches each target, builds the right
 *      side.  A last premise whose target is the right side is a tail call.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ utilities */

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("rulec: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("out of memory"); return p; }
static void *xcalloc(size_t a, size_t b) { void *p = calloc(a ? a : 1, b ? b : 1); if (!p) die("out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) die("out of memory"); return p; }
static char *xstrdup(const char *s) { char *d = strdup(s); if (!d) die("out of memory"); return d; }

#define VEC_PUSH(arr, len, cap, val) do { \
    if ((len) == (cap)) { (cap) = (cap) ? 2 * (cap) : 16; (arr) = xrealloc((arr), (size_t)(cap) * sizeof(*(arr))); } \
    (arr)[(len)++] = (val); } while (0)

/* string -> int map (open addressing) */
typedef struct { char **keys; int *vals; size_t cap, len; } Map;
static uint64_t hash_str(const char *s) { uint64_t h = 1469598103934665603ull; while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ull; } return h; }
static int map_get(const Map *m, const char *k) {
    if (!m->cap) return -1;
    for (size_t i = hash_str(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
        if (!m->keys[i]) return -1;
        if (!strcmp(m->keys[i], k)) return m->vals[i];
    }
}
static void map_put(Map *m, const char *k, int v) {
    if (2 * (m->len + 1) > m->cap) {
        size_t oc = m->cap; char **ok = m->keys; int *ov = m->vals;
        m->cap = oc ? 2 * oc : 64; m->keys = xcalloc(m->cap, sizeof(char *)); m->vals = xcalloc(m->cap, sizeof(int)); m->len = 0;
        for (size_t i = 0; i < oc; i++) if (ok[i]) map_put(m, ok[i], ov[i]);
        free(ok); free(ov);
    }
    size_t i = hash_str(k) & (m->cap - 1);
    while (m->keys[i]) { if (!strcmp(m->keys[i], k)) { m->vals[i] = v; return; } i = (i + 1) & (m->cap - 1); }
    m->keys[i] = xstrdup(k); m->vals[i] = v; m->len++;
}

/* ------------------------------------------------------------- S-expressions */

enum { SX_SYM, SX_STR, SX_LIST };
typedef struct Sx { int k; char *s; int n; struct Sx **v; } Sx;

static Sx *sx_read(const char **pp) {
    const char *p = *pp;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) { *pp = p; return NULL; }
    Sx *x = xcalloc(1, sizeof *x);
    if (*p == '(') {
        p++;
        int cap = 0;
        x->k = SX_LIST;
        for (;;) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) die("unterminated list");
            if (*p == ')') { p++; break; }
            *pp = p;
            Sx *e = sx_read(pp);
            p = *pp;
            VEC_PUSH(x->v, x->n, cap, e);
        }
    } else if (*p == '"') {
        p++;
        size_t cap = 16, n = 0;
        char *b = xmalloc(cap);
        while (*p != '"') {
            if (!*p) die("unterminated string");
            char c = *p++;
            if (c == '\\') {
                c = *p++;
                if (c == 'n') c = '\n'; else if (c == 't') c = '\t';
                else if (c != '"' && c != '\\') die("unsupported escape \\%c", c);
            }
            if (n + 2 > cap) { cap *= 2; b = xrealloc(b, cap); }
            b[n++] = c;
        }
        p++;
        b[n] = 0;
        x->k = SX_STR; x->s = b; x->n = (int)n;
    } else if (*p == ')') {
        die("unexpected )");
    } else {
        const char *q = p;
        while (*q && !isspace((unsigned char)*q) && *q != '(' && *q != ')' && *q != '"') q++;
        x->k = SX_SYM; x->s = strndup(p, (size_t)(q - p)); x->n = (int)(q - p);
        p = q;
    }
    *pp = p;
    return x;
}

static bool sx_is_sym(const Sx *x, const char *s) { return x && x->k == SX_SYM && !strcmp(x->s, s); }
static bool sx_head(const Sx *x, const char *s) { return x && x->k == SX_LIST && x->n > 0 && sx_is_sym(x->v[0], s); }
static const char *sx_str(const Sx *x, const char *what) {
    if (!x || x->k != SX_STR) die("expected a string for %s", what);
    return x->s;
}
/* LCons/LNil list -> items */
static int sx_items(const Sx *x, Sx ***out) {
    int n = 0, cap = 0;
    Sx **items = NULL;
    while (sx_head(x, "LCons")) {
        if (x->n != 3) die("malformed LCons");
        VEC_PUSH(items, n, cap, x->v[1]);
        x = x->v[2];
    }
    if (!sx_is_sym(x, "LNil")) die("malformed list (expected LNil)");
    *out = items;
    return n;
}

/* ---------------------------------------------------------------- Rule IR */

typedef struct { char *name; int ndata; int *data; int datacap; int native; char *repname; int *extctors; int nextctors; } Sort;
typedef struct {
    char *name; int sort; int arity; int *asort; char **aname;
    bool defined; int nrules; int *rules; int rcap;
    int det;          /* 1 det, 0 semidet (judgments) */
    bool complete;    /* the group covers every input */
    int dag_root;
    int native_op;    /* index into native_ops or -1 */
    int scc;          /* call-graph component */
    bool overlap_open;  /* two of its left sides overlap and their premises are not shown exclusive */
    bool recursive;
} Ctor;
typedef struct Pat { int var; int ctor; int n; struct Pat **k; int sort; } Pat;  /* var >= 0: variable; var == -1: app; var == -2: wildcard */
/* a premise is a call J(args) ~> target, or a query of a relation of the module (no target): the
 * relation's rows are host data; such premises are counted by the census but not compiled */
typedef struct { Pat *src, *tgt; bool *eqv; char *relation; int nrel; Pat **rel; bool binds; } Prem;
typedef struct {
    int id; char *label; int nv; char **vn; int *vs;
    Pat *lhs, *rhs; int np; Prem *p; int head; int layer;
    bool tail;        /* the last premise is a tail call */
} Rule;

static Sort *sorts; static int nsorts, sortcap;
static Ctor *ctors; static int nctors, ctorcap;
static Rule *rules; static int nrules, rulecap;
static Map sort_map, ctor_map;
static int nerrors;
static char *module_name = "?", *theory_name = "?";

static void err(const Rule *r, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rulec: check failed%s%s%s: ", r ? " in rule \"" : "", r ? r->label : "", r ? "\"" : "");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    nerrors++;
}

static int sort_id(const char *s) { return map_get(&sort_map, s); }

static Pat *pat_from(Sx *x, Rule *r) {
    Pat *p = xcalloc(1, sizeof *p);
    if (sx_head(x, "FVar")) {
        const char *v = sx_str(x->v[1], "variable");
        p->var = -1;
        for (int i = 0; i < r->nv; i++) if (!strcmp(r->vn[i], v)) p->var = i;
        if (p->var < 0) { err(r, "undeclared variable %s", v); p->var = 0; p->sort = -1; return p; }
        p->sort = r->vs[p->var];
        return p;
    }
    if (!sx_head(x, "PApp")) die("unknown pattern form in rule %s", r->label);
    const char *c = sx_str(x->v[1], "constructor");
    p->var = -1;
    p->ctor = map_get(&ctor_map, c);
    Sx **items;
    p->n = sx_items(x->v[2], &items);
    p->k = xcalloc((size_t)p->n, sizeof(Pat *));
    for (int i = 0; i < p->n; i++) p->k[i] = pat_from(items[i], r);
    free(items);
    if (p->ctor < 0) { err(r, "undeclared constructor %s", c); p->sort = -1; return p; }
    Ctor *k = &ctors[p->ctor];
    p->sort = k->sort;
    if (p->n != k->arity) { err(r, "constructor %s has arity %d, used with %d", c, k->arity, p->n); return p; }
    for (int i = 0; i < p->n; i++)
        if (p->k[i]->sort >= 0 && p->k[i]->sort != k->asort[i])
            err(r, "argument %d of %s has sort %s, expected %s", i, c, sorts[p->k[i]->sort].name, sorts[k->asort[i]].name);
    return p;
}

static void load_wire(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open %s", path);
    size_t cap = 1 << 20, n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n + 65536 > cap) { cap *= 2; buf = xrealloc(buf, cap); }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        if (!r) break;
        n += r;
    }
    buf[n] = 0;
    fclose(f);
    int ends = 0;
    for (char *line = buf; line && *line;) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        const char *p = line;
        while (*p == ' ') p++;
        if (*p == '(') {
            Sx *x = sx_read(&p);
            if (sx_head(x, "wire-module")) {
                module_name = xstrdup(sx_str(x->v[1], "module name"));
                theory_name = xstrdup(sx_str(x->v[2], "theory name"));
            } else if (sx_head(x, "TypeDecl")) {
                Sort s = {0};
                s.name = xstrdup(sx_str(x->v[1], "sort"));
                s.native = 0;
                if (sort_id(s.name) >= 0) die("sort %s declared twice", s.name);
                map_put(&sort_map, s.name, nsorts);
                VEC_PUSH(sorts, nsorts, sortcap, s);
            } else if (sx_head(x, "GrammarRuleV2")) {
                Ctor c = {0};
                c.name = xstrdup(sx_str(x->v[1], "constructor"));
                c.sort = sort_id(sx_str(x->v[2], "constructor sort"));
                if (c.sort < 0) die("constructor %s of undeclared sort %s", c.name, x->v[2]->s);
                Sx **ps;
                c.arity = sx_items(x->v[3], &ps);
                c.asort = xcalloc((size_t)c.arity, sizeof(int));
                c.aname = xcalloc((size_t)c.arity, sizeof(char *));
                for (int i = 0; i < c.arity; i++) {
                    if (!sx_head(ps[i], "TermSimple") || !sx_head(ps[i]->v[2], "TBase"))
                        die("constructor %s: unsupported parameter form", c.name);
                    c.aname[i] = xstrdup(sx_str(ps[i]->v[1], "parameter"));
                    c.asort[i] = sort_id(sx_str(ps[i]->v[2]->v[1], "parameter sort"));
                    if (c.asort[i] < 0) die("constructor %s: undeclared parameter sort", c.name);
                }
                free(ps);
                if (!sx_is_sym(x->v[5], "EvalNone") || !sx_is_sym(x->v[6], "AlgebraNone"))
                    die("constructor %s carries an evaluation or algebra (unsupported)", c.name);
                if (map_get(&ctor_map, c.name) >= 0) die("constructor %s declared twice", c.name);
                c.dag_root = -1; c.native_op = -1; c.det = 0;
                map_put(&ctor_map, c.name, nctors);
                VEC_PUSH(ctors, nctors, ctorcap, c);
            } else if (sx_head(x, "ScopedRewriteRuleV1")) {
                Rule r = {0};
                r.id = nrules;
                r.label = xstrdup(sx_str(x->v[1], "rule label"));
                Sx **vs;
                r.nv = sx_items(x->v[2], &vs);
                r.vn = xcalloc((size_t)r.nv, sizeof(char *));
                r.vs = xcalloc((size_t)r.nv, sizeof(int));
                for (int i = 0; i < r.nv; i++) {
                    if (!sx_head(vs[i], "TypeBinding") || !sx_head(vs[i]->v[2], "TBase"))
                        die("rule %s: unsupported variable binding", r.label);
                    r.vn[i] = xstrdup(sx_str(vs[i]->v[1], "variable"));
                    r.vs[i] = sort_id(sx_str(vs[i]->v[2]->v[1], "variable sort"));
                    if (r.vs[i] < 0) die("rule %s: variable %s of undeclared sort", r.label, r.vn[i]);
                    for (int j = 0; j < i; j++) if (!strcmp(r.vn[j], r.vn[i])) die("rule %s: variable %s declared twice", r.label, r.vn[i]);
                }
                free(vs);
                Sx **ps;
                r.np = sx_items(x->v[3], &ps);
                r.p = xcalloc((size_t)r.np, sizeof(Prem));
                for (int i = 0; i < r.np; i++) {
                    if (sx_head(ps[i], "RelationQuery") && ps[i]->n == 3) {
                        r.p[i].relation = xstrdup(sx_str(ps[i]->v[1], "relation"));
                        Sx **as;
                        r.p[i].nrel = sx_items(ps[i]->v[2], &as);
                        r.p[i].rel = xcalloc((size_t)(r.p[i].nrel ? r.p[i].nrel : 1), sizeof(Pat *));
                        for (int a = 0; a < r.p[i].nrel; a++) r.p[i].rel[a] = pat_from(as[a], &r);
                        free(as);
                        continue;
                    }
                    if (!sx_head(ps[i], "Congruence") || ps[i]->n != 3) die("rule %s: unsupported premise form", r.label);
                    r.p[i].src = pat_from(ps[i]->v[1], &r);
                    r.p[i].tgt = pat_from(ps[i]->v[2], &r);
                }
                free(ps);
                r.lhs = pat_from(x->v[4], &r);
                r.rhs = pat_from(x->v[5], &r);
                if (!sx_is_sym(x->v[6], "RuleBindingNone")) die("rule %s: unsupported binding annotation", r.label);
                VEC_PUSH(rules, nrules, rulecap, r);
            } else if (sx_head(x, "wire-end")) {
                ends++;
                if (x->n != 5 || atoi(x->v[1]->s) != nsorts || atoi(x->v[2]->s) != nctors ||
                    atoi(x->v[3]->s) != 0 || atoi(x->v[4]->s) != nrules)
                    die("wire-end counts do not match the rows read (%d sorts, %d constructors, %d rules)", nsorts, nctors, nrules);
            } else if (sx_head(x, "wire-refused")) {
                die("the front end refused the module");
            } else {
                die("unknown wire row");
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    if (ends != 1) die("missing wire-end row");
    free(buf);
}

/* ------------------------------------------------------------------- checks */

static bool pat_has_judgment(const Pat *p) {
    if (p->var != -1) return false;
    if (p->ctor >= 0 && ctors[p->ctor].defined) return true;
    for (int i = 0; i < p->n; i++) if (pat_has_judgment(p->k[i])) return true;
    return false;
}
static void pat_vars(const Pat *p, int *count) {  /* count[v]++ for each occurrence */
    if (p->var >= 0) { count[p->var]++; return; }
    for (int i = 0; i < p->n; i++) pat_vars(p->k[i], count);
}
static bool pat_tests_bound(const Pat *p, const bool *eqv) {
    if (!eqv) return false;
    if (p->var >= 0) return eqv[p->var];
    for (int i = 0; i < p->n; i++) if (pat_tests_bound(p->k[i], eqv)) return true;
    return false;
}
static bool pat_unify(const Pat *a, const Pat *b) {  /* linear, disjoint variables */
    if (a->var != -1 || b->var != -1) return true;
    if (a->ctor != b->ctor) return false;
    for (int i = 0; i < a->n; i++) if (!pat_unify(a->k[i], b->k[i])) return false;
    return true;
}
static bool pat_equal(const Pat *a, const Pat *b) {
    if ((a->var != -1) != (b->var != -1)) return false;
    if (a->var != -1) return a->var == b->var;
    if (a->ctor != b->ctor || a->n != b->n) return false;
    for (int i = 0; i < a->n; i++) if (!pat_equal(a->k[i], b->k[i])) return false;
    return true;
}

/* ---- guard exclusivity of two overlapping rules ----
 * Both rules' patterns live in one variable space (a's variables 0..na-1, b's na..na+nb-1).  The left
 * sides are unified; then the premises are walked in lockstep: when both rules call the same judgment
 * on arguments equal under the unifier, both calls have the same (single) result, so the two targets
 * are unified; a clash means no input satisfies both rules' premises.  The walk stops at the first
 * pair of premises it cannot identify. */
static bool census_only;
static int n_overlaps, n_overlaps_exclusive, n_overlaps_unresolved;
typedef struct { int var; int ctor; int n; int k[16]; } UT;   /* var >= 0: variable; else constructor */
static UT *ut; static int nut, utcap;
static int *ubind; static int nubind;
static int ut_from(const Pat *p, int off) {
    UT x = {-1, -1, 0, {0}};
    if (p->var >= 0) x.var = p->var + off;
    else if (p->var == -2) die("internal: wildcard in a rule");
    else {
        x.ctor = p->ctor; x.n = p->n;
        if (p->n > 16) die("constructor arity above 16");
        for (int i = 0; i < p->n; i++) x.k[i] = ut_from(p->k[i], off);
    }
    VEC_PUSH(ut, nut, utcap, x);
    return nut - 1;
}
static int u_resolve(int t) {
    while (ut[t].var >= 0 && ubind[ut[t].var] >= 0) t = ubind[ut[t].var];
    return t;
}
static bool u_occurs(int v, int t) {
    t = u_resolve(t);
    if (ut[t].var >= 0) return ut[t].var == v;
    for (int i = 0; i < ut[t].n; i++) if (u_occurs(v, ut[t].k[i])) return true;
    return false;
}
static bool u_unify(int a, int b) {
    a = u_resolve(a); b = u_resolve(b);
    if (a == b) return true;
    if (ut[a].var >= 0 && ut[b].var >= 0 && ut[a].var == ut[b].var) return true;
    if (ut[a].var >= 0) { if (u_occurs(ut[a].var, b)) return false; ubind[ut[a].var] = b; return true; }
    if (ut[b].var >= 0) { if (u_occurs(ut[b].var, a)) return false; ubind[ut[b].var] = a; return true; }
    if (ut[a].ctor != ut[b].ctor || ut[a].n != ut[b].n) return false;
    for (int i = 0; i < ut[a].n; i++) if (!u_unify(ut[a].k[i], ut[b].k[i])) return false;
    return true;
}
static bool u_equal(int a, int b) {   /* identical under the current bindings */
    a = u_resolve(a); b = u_resolve(b);
    if (a == b) return true;
    if (ut[a].var >= 0 || ut[b].var >= 0) return ut[a].var >= 0 && ut[b].var >= 0 && ut[a].var == ut[b].var;
    if (ut[a].ctor != ut[b].ctor || ut[a].n != ut[b].n) return false;
    for (int i = 0; i < ut[a].n; i++) if (!u_equal(ut[a].k[i], ut[b].k[i])) return false;
    return true;
}
static bool guards_exclusive(const Rule *a, const Rule *b) {
    nut = 0;
    int off = a->nv;
    nubind = a->nv + b->nv;
    ubind = xrealloc(ubind, (size_t)(nubind ? nubind : 1) * sizeof(int));
    for (int i = 0; i < nubind; i++) ubind[i] = -1;
    if (!u_unify(ut_from(a->lhs, 0), ut_from(b->lhs, off))) return true;   /* not even overlapping */
    for (int j = 0; j < a->np && j < b->np; j++) {
        const Prem *pa = &a->p[j], *pb = &b->p[j];
        if (pa->relation || pb->relation) return false;
        if (pa->src->ctor != pb->src->ctor) return false;
        int sa = ut_from(pa->src, 0), sb = ut_from(pb->src, off);
        if (!u_equal(sa, sb)) return false;
        /* the same call: one result, which both targets must match */
        if (!u_unify(ut_from(pa->tgt, 0), ut_from(pb->tgt, off))) return true;
    }
    return false;
}

static void check_rules(void) {
    /* judgments: heads of left sides */
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        if (r->lhs->var != -1) { err(r, "left side is a variable"); continue; }
        if (r->lhs->ctor < 0) continue;
        r->head = r->lhs->ctor;
        Ctor *c = &ctors[r->head];
        c->defined = true;
        VEC_PUSH(c->rules, c->nrules, c->rcap, i);
    }
    /* data constructors of each sort */
    for (int c = 0; c < nctors; c++)
        if (!ctors[c].defined) { Sort *s = &sorts[ctors[c].sort]; VEC_PUSH(s->data, s->ndata, s->datacap, c); }
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        if (r->lhs->var != -1 || r->lhs->ctor < 0) continue;
        if (r->lhs->sort >= 0 && r->rhs->sort >= 0 && r->lhs->sort != r->rhs->sort)
            err(r, "left side of sort %s, right side of sort %s", sorts[r->lhs->sort].name, sorts[r->rhs->sort].name);
        for (int a = 0; a < r->lhs->n; a++)
            if (pat_has_judgment(r->lhs->k[a])) err(r, "argument %d of the left side contains a judgment", a);
        int *cnt = xcalloc((size_t)r->nv, sizeof(int));
        pat_vars(r->lhs, cnt);
        bool *bound = xcalloc((size_t)r->nv, sizeof(bool));
        for (int v = 0; v < r->nv; v++) {
            if (cnt[v] > 1) err(r, "left side is not linear in %s", r->vn[v]);
            bound[v] = cnt[v] > 0;
        }
        for (int j = 0; j < r->np; j++) {
            Prem *p = &r->p[j];
            if (p->relation) {
                /* a relation query: the arguments bound before it are inputs; the others become bound */
                memset(cnt, 0, (size_t)r->nv * sizeof(int));
                for (int a = 0; a < p->nrel; a++) { if (pat_has_judgment(p->rel[a])) err(r, "premise %d: a relation argument contains a judgment", j); pat_vars(p->rel[a], cnt); }
                for (int v = 0; v < r->nv; v++) if (cnt[v]) { if (!bound[v]) p->binds = true; bound[v] = true; }
                continue;
            }
            if (p->src->var != -1) { err(r, "premise %d: the source is a variable", j); continue; }
            if (p->src->ctor < 0) continue;
            if (!ctors[p->src->ctor].defined) err(r, "premise %d: %s has no rules", j, ctors[p->src->ctor].name);
            for (int a = 0; a < p->src->n; a++)
                if (pat_has_judgment(p->src->k[a])) err(r, "premise %d: an argument contains a judgment", j);
            if (pat_has_judgment(p->tgt)) err(r, "premise %d: the target contains a judgment", j);
            if (p->src->sort >= 0 && p->tgt->sort >= 0 && p->src->sort != p->tgt->sort)
                err(r, "premise %d: source of sort %s, target of sort %s", j, sorts[p->src->sort].name, sorts[p->tgt->sort].name);
            memset(cnt, 0, (size_t)r->nv * sizeof(int));
            pat_vars(p->src, cnt);
            for (int v = 0; v < r->nv; v++) if (cnt[v] && !bound[v]) err(r, "premise %d: %s is not bound", j, r->vn[v]);
            memset(cnt, 0, (size_t)r->nv * sizeof(int));
            pat_vars(p->tgt, cnt);
            p->eqv = xcalloc((size_t)(r->nv ? r->nv : 1), sizeof(bool));
            for (int v = 0; v < r->nv; v++) {
                if (cnt[v] > 1) err(r, "premise %d: target not linear in %s", j, r->vn[v]);
                /* a variable already bound in a target is an equality test: the premise holds when the
                 * result equals the value bound (that is what matching a bound variable means) */
                if (cnt[v] && bound[v]) p->eqv[v] = true;
            }
            for (int v = 0; v < r->nv; v++) if (cnt[v]) bound[v] = true;
        }
        if (pat_has_judgment(r->rhs)) err(r, "right side contains a judgment");
        memset(cnt, 0, (size_t)r->nv * sizeof(int));
        pat_vars(r->rhs, cnt);
        for (int v = 0; v < r->nv; v++) if (cnt[v] && !bound[v]) err(r, "right side: %s is not bound", r->vn[v]);
        r->tail = r->np > 0 && !r->p[r->np - 1].relation && r->p[r->np - 1].tgt->var >= 0 && r->rhs->var >= 0 &&
                  r->rhs->var == r->p[r->np - 1].tgt->var && !r->p[r->np - 1].eqv[r->p[r->np - 1].tgt->var];
        free(cnt);
        free(bound);
    }
    /* overlaps: two left sides of one judgment that unify.  Each pair is either shown exclusive by its
     * guards (see guards_exclusive) or left unresolved; an unresolved pair fails the build unless the
     * run only takes the census */
    for (int c = 0; c < nctors; c++) {
        Ctor *k = &ctors[c];
        for (int i = 0; i < k->nrules; i++)
            for (int j = i + 1; j < k->nrules; j++) {
                Rule *a = &rules[k->rules[i]], *b = &rules[k->rules[j]];
                if (!pat_unify(a->lhs, b->lhs)) continue;
                n_overlaps++;
                if (guards_exclusive(a, b)) { n_overlaps_exclusive++; continue; }
                k->overlap_open = true;
                if (census_only) { n_overlaps_unresolved++; if (n_overlaps_unresolved <= 12) fprintf(stderr, "rulec: census: \"%s\" and \"%s\" overlap; not shown exclusive\n", a->label, b->label); }
                else err(a, "left side overlaps rule \"%s\" and their premises are not shown exclusive", b->label);
            }
    }
}

/* -------------------------------------------------------------- decision DAGs */

/* An occurrence: a path into the arguments (p[0] the argument, p[1..] child indices) together with
 * the sort at every step (s[l] is the sort of the subterm reached by p[0..l]).  The same path under
 * different constructors can have different sorts, so both are part of its identity. */
typedef struct { int len; int *p; int *s; } Occ;
static Occ *occs; static int noccs, occcap;
static Map occ_map;
static int occ_intern(const int *p, const int *s, int len) {
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
static int occ_child(int occ, int k, int sort) {
    int buf[256], sb[256];
    Occ *o = &occs[occ];
    if (o->len >= 255) die("pattern too deep");
    memcpy(buf, o->p, (size_t)o->len * sizeof(int));
    memcpy(sb, o->s, (size_t)o->len * sizeof(int));
    buf[o->len] = k;
    sb[o->len] = sort;
    return occ_intern(buf, sb, o->len + 1);
}

enum { N_FAIL, N_LEAF, N_SWITCH };
/* a leaf lists the rules whose left sides match every input reaching it (one rule in a module whose
 * left sides do not overlap; several, in module order, where overlapping rules are told apart by
 * their premises) */
typedef struct { int kind; int rule; int occ; int sort; int ncase; int *cctor; int *cchild; int dflt; int ncand; int *cand; } Node;
static Node *nodes; static int nnodes, nodecap;
static Map node_map;

static int node_intern(Node *n) {
    size_t cap = 64 + 32 * (size_t)n->ncase + 16 * (size_t)n->ncand, o = 0;
    char *key = xmalloc(cap);
    o += (size_t)snprintf(key + o, cap - o, "%d:%d:%d:%d:%d|", n->kind, n->rule, n->occ, n->sort, n->dflt);
    for (int i = 0; i < n->ncand && o + 16 < cap; i++) o += (size_t)snprintf(key + o, cap - o, "c%d,", n->cand[i]);
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

static Pat wildcard = {-2, -1, 0, NULL, -1};

typedef struct { int rule; Pat **cols; } Row;

static int dag_compile(int ncol, const int *occ, const int *osort, Row *rows, int nrow) {
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
        n.kind = N_LEAF; n.rule = rows[0].rule; n.ncand = nrow;
        n.cand = xmalloc((size_t)nrow * sizeof(int));
        for (int r = 0; r < nrow; r++) n.cand[r] = rows[r].rule;
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
        n.cchild[i] = dag_compile(nc, nocc, nsort, nr, nn);
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

static void build_dags(void) {
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
        k->dag_root = dag_compile(k->arity, occ, k->asort, rows, k->nrules);
        for (int i = 0; i < k->nrules; i++) free(rows[i].cols);
        free(rows); free(occ);
    }
}

/* nodes reachable from a root, in a stable order (depth first) */
static void dag_reach(int n, bool *seen, int *order, int *norder) {
    if (seen[n]) return;
    seen[n] = true;
    order[(*norder)++] = n;
    Node *x = &nodes[n];
    if (x->kind == N_SWITCH) {
        for (int i = 0; i < x->ncase; i++) dag_reach(x->cchild[i], seen, order, norder);
        if (x->dflt >= 0) dag_reach(x->dflt, seen, order, norder);
    }
}
static bool dag_has_fail(int root) {
    bool *seen = xcalloc((size_t)nnodes, 1); int *order = xmalloc((size_t)nnodes * sizeof(int)); int no = 0;
    dag_reach(root, seen, order, &no);
    bool f = false;
    for (int i = 0; i < no; i++) if (nodes[order[i]].kind == N_FAIL) f = true;
    free(seen); free(order);
    return f;
}
static int dag_size(int root, int *switches, int *depth) {
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

/* --------------------------------------------------------- determinism, calls */

static bool irrefutable(const Pat *p) {
    if (p->var != -1) return true;
    if (sorts[p->sort].ndata != 1) return false;
    for (int i = 0; i < p->n; i++) if (!irrefutable(p->k[i])) return false;
    return true;
}

static bool prem_irrefutable(const Prem *p) {
    if (p->relation) return false;
    if (!irrefutable(p->tgt)) return false;
    int cnt[1024] = {0};
    (void)cnt;
    return !pat_tests_bound(p->tgt, p->eqv);
}
static bool relational_rule(const Rule *r) {   /* a relation query that binds a variable: possibly several answers */
    for (int j = 0; j < r->np; j++) if (r->p[j].relation && r->p[j].binds) return true;
    return false;
}

static void analyse_determinism(void) {
    for (int c = 0; c < nctors; c++)
        if (ctors[c].defined) { ctors[c].complete = !dag_has_fail(ctors[c].dag_root); ctors[c].det = ctors[c].complete; }
    for (bool changed = true; changed;) {
        changed = false;
        for (int c = 0; c < nctors; c++) {
            Ctor *k = &ctors[c];
            if (!k->defined || !k->det) continue;
            if (k->overlap_open) { k->det = 0; changed = true; continue; }
            for (int i = 0; i < k->nrules && k->det; i++) {
                Rule *r = &rules[k->rules[i]];
                for (int j = 0; j < r->np; j++)
                    if (r->p[j].relation || !ctors[r->p[j].src->ctor].det || !prem_irrefutable(&r->p[j])) { k->det = 0; changed = true; break; }
            }
        }
    }
}

/* strongly connected components of the call graph (Tarjan) */
static int *tj_index, *tj_low, *tj_stack, tj_sp, tj_counter, nscc;
static bool *tj_on;
static void tarjan(int v) {
    tj_index[v] = tj_low[v] = tj_counter++;
    tj_stack[tj_sp++] = v; tj_on[v] = true;
    Ctor *k = &ctors[v];
    for (int i = 0; i < k->nrules; i++) {
        Rule *r = &rules[k->rules[i]];
        for (int j = 0; j < r->np; j++) {
            if (r->p[j].relation) continue;
            int w = r->p[j].src->ctor;
            if (tj_index[w] < 0) { tarjan(w); if (tj_low[w] < tj_low[v]) tj_low[v] = tj_low[w]; }
            else if (tj_on[w] && tj_index[w] < tj_low[v]) tj_low[v] = tj_index[w];
        }
    }
    if (tj_low[v] == tj_index[v]) {
        int size = 0, w, first = tj_sp;
        do { w = tj_stack[--tj_sp]; tj_on[w] = false; ctors[w].scc = nscc; size++; first--; } while (w != v);
        (void)first;
        for (int i = tj_sp; i < tj_sp + size; i++) ctors[tj_stack[i]].recursive = size > 1;
        nscc++;
    }
}
static void analyse_calls(void) {
    tj_index = xmalloc((size_t)nctors * sizeof(int)); tj_low = xmalloc((size_t)nctors * sizeof(int));
    tj_stack = xmalloc((size_t)nctors * sizeof(int)); tj_on = xcalloc((size_t)nctors, 1);
    for (int c = 0; c < nctors; c++) tj_index[c] = -1;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined && tj_index[c] < 0) tarjan(c);
    /* self recursion */
    for (int c = 0; c < nctors; c++) {
        Ctor *k = &ctors[c];
        for (int i = 0; i < k->nrules; i++) {
            Rule *r = &rules[k->rules[i]];
            for (int j = 0; j < r->np; j++) if (!r->p[j].relation && r->p[j].src->ctor == c) k->recursive = true;
        }
    }
}

/* ---------------------------------------------------------------------- census */

#define MAXLAYERS 16
static char *layer_name[MAXLAYERS]; static char *layer_first[MAXLAYERS]; static int nlayers;

static void assign_layers(void) {
    int cur = -1;
    for (int i = 0; i < nrules; i++) {
        for (int l = 0; l < nlayers; l++) if (!strcmp(rules[i].label, layer_first[l])) cur = l;
        rules[i].layer = cur;
    }
}

static int cmp_group_desc(const void *a, const void *b) {
    const Ctor *x = &ctors[*(const int *)a], *y = &ctors[*(const int *)b];
    if (x->nrules != y->nrules) return y->nrules - x->nrules;
    return strcmp(x->name, y->name);
}

static int pat_size(const Pat *p) { int s = 1; if (p->var == -1) for (int i = 0; i < p->n; i++) s += pat_size(p->k[i]); return s; }
static int pat_depth(const Pat *p) { int d = 0; if (p->var == -1) for (int i = 0; i < p->n; i++) { int e = pat_depth(p->k[i]); if (e > d) d = e; } return p->var == -1 ? d + 1 : 0; }

static const char *sort_kind_of_judgment(const Ctor *k);

static void write_census(const char *path, const char *tsv) {
    FILE *o = fopen(path, "w");
    if (!o) die("cannot write %s", path);
    int nj = 0, ndata = 0, ndet = 0, ndetrules = 0, ncomplete = 0, nrec = 0, ntail = 0, nprem = 0, nrefut = 0, nrulesemi = 0;
    int nrelq = 0, nrelbind = 0, neqtest = 0, nrelational = 0;
    for (int c = 0; c < nctors; c++) {
        if (ctors[c].defined) {
            nj++;
            if (ctors[c].det) { ndet++; ndetrules += ctors[c].nrules; } else nrulesemi += ctors[c].nrules;
            if (ctors[c].complete) ncomplete++;
            if (ctors[c].recursive) nrec++;
        } else ndata++;
    }
    for (int i = 0; i < nrules; i++) {
        if (rules[i].tail) ntail++;
        nprem += rules[i].np;
        for (int j = 0; j < rules[i].np; j++) {
            if (rules[i].p[j].relation) { nrelq++; if (rules[i].p[j].binds) nrelbind++; continue; }
            if (!prem_irrefutable(&rules[i].p[j])) nrefut++;
            if (pat_tests_bound(rules[i].p[j].tgt, rules[i].p[j].eqv)) neqtest++;
        }
    }
    fprintf(o, "rule census of %s, theory %s\n\n", module_name, theory_name);
    fprintf(o, "sorts %d; constructors %d (data %d, judgments %d); rules %d; premises %d\n", nsorts, nctors, ndata, nj, nrules, nprem);
    fprintf(o, "mode: every judgment (in, ..., in) -> out; every premise source has its inputs bound (checked)\n");
    fprintf(o, "judgments det %d (%d rules), semidet %d (%d rules); groups covering every input %d of %d\n",
            ndet, ndetrules, nj - ndet, nrulesemi, ncomplete, nj);
    int nopen = 0, nopenrules = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined && ctors[c].overlap_open) { nopen++; nopenrules += ctors[c].nrules; }
    fprintf(o, "share of rules in det judgments: %.1f%%; judgments whose determinism is not established (an overlap not shown exclusive): %d (%d rules)\n",
            100.0 * ndetrules / nrules, nopen, nopenrules);
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined) continue;
        for (int i = 0; i < ctors[c].nrules; i++) if (relational_rule(&rules[ctors[c].rules[i]])) { nrelational++; break; }
    }
    fprintf(o, "premises with a refutable target (a constructor pattern or a bound variable that can fail): %d of %d\n", nrefut, nprem);
    fprintf(o, "premise targets that test a variable already bound (an equality test): %d\n", neqtest);
    fprintf(o, "overlapping left sides in one judgment: %d pairs; shown exclusive by their premises: %d; not shown: %d\n",
            n_overlaps, n_overlaps_exclusive, n_overlaps_unresolved);
    fprintf(o, "relation premises (queries of the module's relations): %d, of which %d bind a variable; judgments with one that binds (possibly several answers): %d\n",
            nrelq, nrelbind, nrelational);
    fprintf(o, "rules whose last premise is a tail call: %d; recursive judgments (self or mutual): %d\n\n", ntail, nrec);
    /* layers */
    if (nlayers) {
        fprintf(o, "layer        rules  judgments  det-judgments\n");
        for (int l = 0; l < nlayers; l++) {
            int nr = 0, njl = 0, nd = 0;
            for (int i = 0; i < nrules; i++) if (rules[i].layer == l) nr++;
            for (int c = 0; c < nctors; c++) {
                if (!ctors[c].defined) continue;
                if (rules[ctors[c].rules[0]].layer != l) continue;
                njl++; if (ctors[c].det) nd++;
            }
            fprintf(o, "%-12s %5d  %9d  %13d\n", layer_name[l], nr, njl, nd);
        }
        fputc('\n', o);
    }
    /* group sizes */
    int *order = xmalloc((size_t)nctors * sizeof(int)), no = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) order[no++] = c;
    qsort(order, (size_t)no, sizeof(int), cmp_group_desc);
    int hist[8] = {0};
    for (int i = 0; i < no; i++) {
        int s = ctors[order[i]].nrules;
        hist[s == 1 ? 0 : s == 2 ? 1 : s == 3 ? 2 : s <= 5 ? 3 : s <= 9 ? 4 : s <= 16 ? 5 : s <= 32 ? 6 : 7]++;
    }
    fprintf(o, "dispatch-group sizes (rules per judgment): 1:%d 2:%d 3:%d 4-5:%d 6-9:%d 10-16:%d 17-32:%d >32:%d\n\n",
            hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    fprintf(o, "largest dispatch groups:\n");
    fprintf(o, "  %-32s %5s %5s %8s %6s %6s %8s %s\n", "judgment", "rules", "arity", "class", "nodes", "tests", "max-test", "argument sorts");
    for (int i = 0; i < no && i < 25; i++) {
        Ctor *k = &ctors[order[i]];
        int sw, depth, sz = dag_size(k->dag_root, &sw, &depth);
        fprintf(o, "  %-32s %5d %5d %8s %6d %6d %8d ", k->name, k->nrules, k->arity, k->det ? "det" : "semidet", sz, sw, depth);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%s%s", a ? "," : "", sorts[k->asort[a]].name);
        fputc('\n', o);
    }
    /* premise dependency structure */
    int chain = 0, indep = 0, maxp = 0;
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        if (r->np > maxp) maxp = r->np;
        for (int j = 1; j < r->np; j++) {
            /* does premise j read a variable bound by an earlier target? */
            int *cnt = xcalloc((size_t)r->nv, sizeof(int)), *tc = xcalloc((size_t)r->nv, sizeof(int));
            if (r->p[j].relation) { for (int a = 0; a < r->p[j].nrel; a++) pat_vars(r->p[j].rel[a], cnt); }
            else pat_vars(r->p[j].src, cnt);
            for (int q = 0; q < j; q++) {
                if (r->p[q].relation) { for (int a = 0; a < r->p[q].nrel; a++) pat_vars(r->p[q].rel[a], tc); }
                else pat_vars(r->p[q].tgt, tc);
            }
            bool dep = false;
            for (int v = 0; v < r->nv; v++) if (cnt[v] && tc[v]) dep = true;
            if (dep) chain++; else indep++;
            free(cnt); free(tc);
        }
    }
    fprintf(o, "\npremise data flow: %d later premises read an earlier premise's output, %d read only the left side; at most %d premises in a rule\n",
            chain, indep, maxp);
    /* primitive hot spots: judgments over the data layer's sorts */
    fprintf(o, "\njudgments whose arguments are names or numerals only (candidates for native primitives):\n");
    for (int i = 0; i < no; i++) {
        Ctor *k = &ctors[order[i]];
        const char *kind = sort_kind_of_judgment(k);
        if (kind) fprintf(o, "  %-28s %-9s %3d rules  (%s)\n", k->name, kind, k->nrules, k->det ? "det" : "semidet");
    }
    fclose(o);
    if (tsv) {
        FILE *t = fopen(tsv, "w");
        if (!t) die("cannot write %s", tsv);
        fprintf(t, "judgment\tlayer\trules\tarity\tclass\tcomplete\trecursive\tdag_nodes\tdag_tests\tdag_depth\trule_ids\n");
        for (int c = 0; c < nctors; c++) {
            Ctor *k = &ctors[c];
            if (!k->defined) continue;
            int sw, depth, sz = dag_size(k->dag_root, &sw, &depth);
            int l = rules[k->rules[0]].layer;
            fprintf(t, "%s\t%s\t%d\t%d\t%s\t%s\t%s\t%d\t%d\t%d\t", k->name, l >= 0 ? layer_name[l] : "-", k->nrules, k->arity,
                    k->det ? "det" : "semidet", k->complete ? "yes" : "no", k->recursive ? "yes" : "no", sz, sw, depth);
            for (int i = 0; i < k->nrules; i++) fprintf(t, "%s%d", i ? "," : "", k->rules[i]);
            fputc('\n', t);
        }
        fprintf(t, "#rule\tid\tlabel\tjudgment\tlayer\tpremises\trefutable\ttail\tlhs_size\tlhs_depth\n");
        for (int i = 0; i < nrules; i++) {
            Rule *r = &rules[i];
            int rf = 0;
            for (int j = 0; j < r->np; j++) if (!prem_irrefutable(&r->p[j])) rf++;
            fprintf(t, "rule\t%d\t%s\t%s\t%s\t%d\t%d\t%s\t%d\t%d\n", r->id, r->label, ctors[r->head].name,
                    r->layer >= 0 ? layer_name[r->layer] : "-", r->np, rf, r->tail ? "yes" : "no", pat_size(r->lhs), pat_depth(r->lhs));
        }
        fclose(t);
    }
    free(order);
}

static const char *sort_kind_of_judgment(const Ctor *k) {
    /* names: Str, Byte, Nib; numerals: Nat; with Ord/Flag/Bits4 as the only other sorts */
    bool names = false, nums = false, other = false;
    for (int a = 0; a <= k->arity; a++) {
        const char *s = sorts[a < k->arity ? k->asort[a] : k->sort].name;
        if (!strcmp(s, "Str") || !strcmp(s, "Byte") || !strcmp(s, "Nib")) names = true;
        else if (!strcmp(s, "Nat")) nums = true;
        else if (!strcmp(s, "Ord") || !strcmp(s, "Flag") || !strcmp(s, "Bits4")) {}
        else other = true;
    }
    if (other || (!names && !nums)) return NULL;
    return names && nums ? "names+nums" : names ? "names" : "numerals";
}

/* ----------------------------------------------------------- DAG as data */

static void write_dag(const char *path) {
    FILE *o = fopen(path, "w");
    if (!o) die("cannot write %s", path);
    fprintf(o, "(dag-file \"%s\" \"%s\" %d %d)\n", module_name, theory_name, nrules, nnodes);
    for (int i = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->kind == N_FAIL) fprintf(o, "(node %d fail)\n", i);
        else if (n->kind == N_LEAF) {
            fprintf(o, "(node %d leaf", i);
            for (int c = 0; c < n->ncand; c++) fprintf(o, " \"%s\"", rules[n->cand[c]].label);
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

/* ------------------------------------------------------------------- C emission */

/* representations of sorts: symbolic by default; the record cursor and, in stage 4, native
 * naturals and byte strings, each read through a view that gives back the module's constructors */
/* REP_EXT: a representation the back end does not define itself; the generated code names its
 * constructors (RC_EXT_<name>_C<i>) and uses RC_TAG_<name>, RC_KID_<name>, RC_NUL_<name>,
 * RC_MK<n>_<name> from a header given with --rep-header. */
enum { REP_SYMBOLIC = 0, REP_RECORDS, REP_NAT, REP_STR, REP_EXT };
static const char *rep_name[] = {"symbolic", "records", "nat", "str", "ext"};
static const char *rep_header;
static const char *sort_rep(int s) { return sorts[s].native == REP_EXT ? sorts[s].repname : rep_name[sorts[s].native]; }
static int rep_ctor[4][4];          /* constructor ids a native representation views */
static int str_nib[16];             /* the sixteen nibble constructors of a native byte string's view */
/* A justified block: a judgment answered by an implementation the back end binds (fn), whose
 * contract is the judgment's rules; label is proved, checked or trusted, why names the theorems,
 * the check or the reason.  The table is part of the IR: another back end can bind the same blocks
 * or compile their rules. */
typedef struct { int ctor; char *fn; char *why; char *name; char *label; } NativeOp;
/* a reflexivity law: J(x, x) gives the constant `result`; it lets a call whose two arguments are the
 * same object answer without running J's rules (sound for any J whose rules make it reflexive on
 * every input; the justification names the theorem) */
typedef struct { int ctor; int result; char *why; char *name; char *label; } Law;
static Law laws[64]; static int nlaws;
static NativeOp native_ops[64]; static int nnative_ops;
static char *safepoint_name;
static int safepoint_ctor = -1;
static bool no_musttail;
static bool cache_constants;
static const char *blocks_file;
/* rows of the block table: (block NAME JUDGMENT FN LABEL WHY) and (law NAME JUDGMENT RESULT LABEL WHY) */
typedef struct { bool law; char *name, *judgment, *fn, *label, *why; } BlockRow;
static BlockRow *block_rows; static int nblock_rows, block_row_cap;
static void read_blocks(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open %s", path);
    char *line = NULL; size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '(') continue;
        Sx *x = sx_read(&p);
        if (!x) continue;
        bool law = sx_head(x, "law");
        if (!law && !sx_head(x, "block")) die("%s: unknown row", path);
        if (x->n != 6) die("%s: a %s row has NAME JUDGMENT %s LABEL WHY", path, law ? "law" : "block", law ? "RESULT" : "FN");
        BlockRow r = {law, xstrdup(sx_str(x->v[1], "block name")), xstrdup(sx_str(x->v[2], "judgment")),
                      xstrdup(sx_str(x->v[3], law ? "result" : "implementation")), xstrdup(sx_str(x->v[4], "label")),
                      xstrdup(sx_str(x->v[5], "justification"))};
        if (strcmp(r.label, "proved") && strcmp(r.label, "checked") && strcmp(r.label, "trusted"))
            die("%s: block %s: label is proved, checked or trusted", path, r.name);
        VEC_PUSH(block_rows, nblock_rows, block_row_cap, r);
    }
    free(line);
    fclose(f);
}
static const char *include_rt = "rc_rt.h";
static const char *source_digest;

static bool pat_has_var(const Pat *p) {
    if (p->var >= 0) return true;
    for (int i = 0; i < p->n; i++) if (pat_has_var(p->k[i])) return true;
    return false;
}

static void occ_expr(char *out, size_t cap, int occ) {
    Occ *x = &occs[occ];
    char tmp[8192];
    snprintf(out, cap, "a%d", x->p[0]);
    for (int l = 1; l < x->len; l++) {
        snprintf(tmp, sizeof tmp, "RC_KID_%s(%s, %d)", sort_rep(x->s[l - 1]), out, x->p[l]);
        snprintf(out, cap, "%s", tmp);
    }
}

static void emit_build(FILE *o, const Pat *p) {
    if (p->var >= 0) { fprintf(o, "v%d", p->var); return; }
    const Ctor *c = &ctors[p->ctor];
    int rep = sorts[c->sort].native;
    if (rep == REP_RECORDS) die("rule builds %s, but its sort is read through the record cursor", c->name);
    if (p->n == 0) { fprintf(o, "RC_NUL_%s(%d)", sort_rep(c->sort), p->ctor); return; }
    fprintf(o, "RC_MK%d_%s(%d", p->n, sort_rep(c->sort), p->ctor);
    for (int i = 0; i < p->n; i++) { fputs(", ", o); emit_build(o, p->k[i]); }
    fputc(')', o);
}

/* variables the code reads: those of premise sources and of the right side (a tail call's result
 * is returned directly, so its target variable is never read) */
static bool *var_used;
static bool pat_has_used_var(const Pat *p) {
    if (p->var >= 0) return var_used[p->var];
    for (int i = 0; i < p->n; i++) if (pat_has_used_var(p->k[i])) return true;
    return false;
}
static void mark_used(const Pat *p) {
    if (p->var >= 0) { var_used[p->var] = true; return; }
    for (int i = 0; i < p->n; i++) mark_used(p->k[i]);
}

static void emit_lhs_bind(FILE *o, const Pat *p, const char *e) {
    if (p->var >= 0) { if (var_used[p->var]) fprintf(o, "        T *v%d = %s;\n", p->var, e); return; }
    for (int i = 0; i < p->n; i++) {
        if (!pat_has_used_var(p->k[i])) continue;
        char sub[8192];
        snprintf(sub, sizeof sub, "RC_KID_%s(%s, %d)", sort_rep(p->sort), e, i);
        emit_lhs_bind(o, p->k[i], sub);
    }
}

static int tmpno;
static const bool *cur_eqv;   /* variables of the current premise's target that are equality tests */
static void emit_tgt_match(FILE *o, const Pat *p, const char *e, int judgment) {
    if (p->var >= 0) {
        if (cur_eqv && cur_eqv[p->var]) fprintf(o, "        if (!rc_term_equal(%s, v%d)) return rc_fail_target(%d);\n", e, p->var, judgment);
        else if (var_used[p->var]) fprintf(o, "        T *v%d = %s;\n", p->var, e);
        return;
    }
    if (!irrefutable(p))
        fprintf(o, "        if (RC_TAG_%s(%s) != %d) return rc_fail_target(%d);\n", sort_rep(p->sort), e, p->ctor, judgment);
    for (int i = 0; i < p->n; i++) {
        if (!pat_has_used_var(p->k[i]) && irrefutable(p->k[i]) && !pat_tests_bound(p->k[i], cur_eqv)) continue;
        int t = tmpno++;
        fprintf(o, "        T *m%d = RC_KID_%s(%s, %d);\n", t, sort_rep(p->sort), e, i);
        char sub[64];
        snprintf(sub, sizeof sub, "m%d", t);
        emit_tgt_match(o, p->k[i], sub, judgment);
    }
}

static const char *target_label(int child, char *buf, size_t cap) {
    Node *x = &nodes[child];
    if (x->kind == N_LEAF) snprintf(buf, cap, "r%d", x->rule);
    else if (x->kind == N_FAIL) snprintf(buf, cap, "f");
    else snprintf(buf, cap, "n%d", child);
    return buf;
}

static void emit_params(FILE *o, int arity) {
    fputc('(', o);
    if (!arity) fputs("void", o);
    for (int a = 0; a < arity; a++) fprintf(o, "%sT *a%d", a ? ", " : "", a);
    fputc(')', o);
}

static void emit_rules_body(FILE *o, int c) {
    Ctor *k = &ctors[c];
    char lab[64], e[8192];
    bool *seen = xcalloc((size_t)nnodes, 1);
    int *order = xmalloc((size_t)nnodes * sizeof(int)), no = 0;
    dag_reach(k->dag_root, seen, order, &no);
    bool fail_used = nodes[k->dag_root].kind == N_FAIL;
    fprintf(o, "    goto %s;\n", target_label(k->dag_root, lab, sizeof lab));
    for (int i = 0; i < no; i++) {
        Node *x = &nodes[order[i]];
        if (x->kind != N_SWITCH) continue;
        occ_expr(e, sizeof e, x->occ);
        Occ *oc = &occs[x->occ];
        fprintf(o, "n%d: switch (RC_TAG_%s(%s)) {", order[i], sort_rep(oc->s[oc->len - 1]), e);
        for (int j = 0; j < x->ncase; j++) {
            fprintf(o, " case %d: goto %s;", x->cctor[j], target_label(x->cchild[j], lab, sizeof lab));
            if (nodes[x->cchild[j]].kind == N_FAIL) fail_used = true;
        }
        fprintf(o, " default: goto %s; }\n", x->dflt >= 0 ? target_label(x->dflt, lab, sizeof lab) : "f");
        if (x->dflt < 0 || nodes[x->dflt].kind == N_FAIL) fail_used = true;
    }
    for (int i = 0; i < k->nrules; i++) {
        Rule *r = &rules[k->rules[i]];
        var_used = xcalloc((size_t)(r->nv ? r->nv : 1), 1);
        for (int j = 0; j < r->np; j++) {
            mark_used(r->p[j].src);
            for (int v = 0; r->p[j].eqv && v < r->nv; v++) if (r->p[j].eqv[v]) var_used[v] = true;   /* compared */
        }
        if (!r->tail) mark_used(r->rhs);
        fprintf(o, "r%d: { /* %s */\n", r->id, r->label);
        for (int a = 0; a < k->arity; a++) {
            char arg[16];
            snprintf(arg, sizeof arg, "a%d", a);
            emit_lhs_bind(o, r->lhs->k[a], arg);
        }
        fprintf(o, "        RC_RULE(%d);\n", r->id);
        tmpno = 0;
        for (int j = 0; j < r->np; j++) {
            Prem *p = &r->p[j];
            int callee = p->src->ctor;
            bool last_tail = j == r->np - 1 && r->tail;
            /* a guaranteed tail call needs no more stack argument slots than the caller has (x86-64:
             * six arguments in registers) */
            int caller_slots = k->arity > 6 ? k->arity - 6 : 0, callee_slots = ctors[callee].arity > 6 ? ctors[callee].arity - 6 : 0;
            bool must = !no_musttail && callee_slots <= caller_slots;
            if (last_tail && !must && (c == safepoint_ctor || callee == safepoint_ctor))
                die("rule %s: the tail call into the safepoint cycle cannot be guaranteed", r->label);
            if (last_tail) fprintf(o, "        %sreturn p%d(", must ? "RC_TAIL " : "", callee);
            else fprintf(o, "        T *t%d = p%d(", j, callee);
            for (int a = 0; a < p->src->n; a++) { if (a) fputs(", ", o); emit_build(o, p->src->k[a]); }
            fputs(");\n", o);
            if (last_tail) break;
            fprintf(o, "        if (!t%d) return (T *)0;\n", j);
            char tv[16];
            snprintf(tv, sizeof tv, "t%d", j);
            cur_eqv = p->eqv;
            emit_tgt_match(o, p->tgt, tv, c);
            cur_eqv = NULL;
        }
        if (!r->tail) { fputs("        return ", o); emit_build(o, r->rhs); fputs(";\n", o); }
        fputs("    }\n", o);
        free(var_used);
        var_used = NULL;
    }
    if (fail_used) fprintf(o, "f:  return rc_no_rule(%d);\n", c);
    free(seen); free(order);
}

static int native_op_of(int c) { for (int i = 0; i < nnative_ops; i++) if (native_ops[i].ctor == c) return i; return -1; }
static void write_rule_labels(FILE *o, int c) {
    fputs(" (rules", o);
    for (int i = 0; i < ctors[c].nrules; i++) fprintf(o, " \"%s\"", rules[ctors[c].rules[i]].label);
    fputc(')', o);
}
static void write_census_blocks(const char *path) {
    FILE *o = fopen(path, "a");
    if (!o) die("cannot write %s", path);
    fprintf(o, "\njustified blocks (the judgment's rules are the contract):\n");
    fprintf(o, "  %-22s %-26s %5s  %-8s %s\n", "block", "judgment", "rules", "label", "justification");
    for (int i = 0; i < nnative_ops; i++)
        fprintf(o, "  %-22s %-26s %5d  %-8s %s\n", native_ops[i].name ? native_ops[i].name : native_ops[i].fn, ctors[native_ops[i].ctor].name,
                ctors[native_ops[i].ctor].nrules, native_ops[i].label ? native_ops[i].label : "", native_ops[i].why ? native_ops[i].why : "");
    for (int i = 0; i < nlaws; i++)
        fprintf(o, "  %-22s %-26s %5d  %-8s law %s(x, x) = %s; %s\n", laws[i].name ? laws[i].name : "", ctors[laws[i].ctor].name,
                ctors[laws[i].ctor].nrules, laws[i].label ? laws[i].label : "", ctors[laws[i].ctor].name, ctors[laws[i].result].name,
                laws[i].why ? laws[i].why : "");
    int nsym = 0;
    for (int s2 = 0; s2 < nsorts; s2++) if (sorts[s2].native) nsym++;
    fprintf(o, "sorts with a native representation: %d\n", nsym);
    for (int s2 = 0; s2 < nsorts; s2++)
        if (sorts[s2].native) fprintf(o, "  %-10s %s\n", sorts[s2].name, sort_rep(s2));
    fclose(o);
}
static void write_block_table(const char *path, const char *mode) {
    FILE *o = fopen(path, mode);
    if (!o) die("cannot write %s", path);
    for (int i = 0; i < nnative_ops; i++) {
        fprintf(o, "(block \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", native_ops[i].name ? native_ops[i].name : "",
                ctors[native_ops[i].ctor].name, native_ops[i].fn, native_ops[i].label ? native_ops[i].label : "",
                native_ops[i].why ? native_ops[i].why : "");
        write_rule_labels(o, native_ops[i].ctor);
        fputs(")\n", o);
    }
    for (int i = 0; i < nlaws; i++) {
        fprintf(o, "(law \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", laws[i].name ? laws[i].name : "", ctors[laws[i].ctor].name,
                ctors[laws[i].result].name, laws[i].label ? laws[i].label : "", laws[i].why ? laws[i].why : "");
        write_rule_labels(o, laws[i].ctor);
        fputs(")\n", o);
    }
    fclose(o);
}
static int law_of(int c) { for (int i = 0; i < nlaws; i++) if (laws[i].ctor == c) return i; return -1; }

static void emit_proc(FILE *o, int c) {
    Ctor *k = &ctors[c];
    int nop = native_op_of(c);
    fprintf(o, "\n/* %s: %d rule%s, %s%s */\n", k->name, k->nrules, k->nrules == 1 ? "" : "s", k->det ? "det" : "semidet",
            nop >= 0 ? "; native operation" : "");
    if (nop >= 0) {
        /* the rules stay compiled: check mode runs them and compares */
        fprintf(o, "#ifdef RC_CHECK_NATIVE\nstatic T *p%d_rules", c); emit_params(o, k->arity); fputs(" {\n", o);
        emit_rules_body(o, c);
        fputs("}\n#endif\n", o);
        fprintf(o, "static T *p%d", c); emit_params(o, k->arity); fputs(" {\n", o);
        fprintf(o, "    if (rc_trace_on) rc_trace_push((uint16_t)(RC_EV_NATIVE + %d));\n", nop);
        /* check mode: the rules first, since an implementation may update a native representation
         * in place and the rules must see the inputs as they were; then the implementation; the two
         * results are compared as module data (a representation that has a read-back gives it) */
        fprintf(o, "#ifdef RC_CHECK_NATIVE\n    if (rc_check_native) {\n        int tr_ = rc_trace_on; rc_trace_on = 0;\n        rc_check_depth++;\n        T *s_ = p%d_rules(", c);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fprintf(o, ");\n        rc_check_depth--;\n        rc_trace_on = tr_;\n        T *n_ = %s(", native_ops[nop].fn);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fputs(");\n", o);
        if (sorts[k->sort].native == REP_EXT) {
            const char *rr = sorts[k->sort].repname;
            fprintf(o, "#ifdef RC_READBACK_%s\n        rc_native_checked(%d, n_ ? RC_READBACK_%s(n_) : n_, s_ ? RC_READBACK_%s(s_) : s_);\n#else\n"
                       "        rc_native_checked(%d, n_, s_);\n#endif\n", rr, nop, rr, rr, nop);
        } else {
            fprintf(o, "        rc_native_checked(%d, n_, s_);\n", nop);
        }
        fputs("        return n_;\n    }\n#endif\n", o);
        fprintf(o, "    return %s(", native_ops[nop].fn);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fputs(");\n}\n", o);
        return;
    }
    int law = law_of(c);
    if (law >= 0) {
        int res = laws[law].result;
        fprintf(o, "static T *p%d_rules", c); emit_params(o, k->arity); fputs(" {\n", o);
        emit_rules_body(o, c);
        fputs("}\n", o);
        fprintf(o, "static T *p%d(T *a0, T *a1) {\n"
                   "    if (a0 == a1) {\n"
                   "        if (rc_trace_on) rc_trace_push((uint16_t)(RC_EV_LAW + %d));\n"
                   "#ifdef RC_CHECK_NATIVE\n"
                   "        if (rc_check_native) { int tr_ = rc_trace_on; rc_trace_on = 0; rc_check_depth++; T *s_ = p%d_rules(a0, a1); rc_check_depth--; rc_trace_on = tr_; rc_law_checked(%d, RC_NUL_%s(%d), s_); }\n"
                   "#endif\n"
                   "        return RC_NUL_%s(%d);\n"
                   "    }\n"
                   "    %sreturn p%d_rules(a0, a1);\n}\n", c, law, c, law, sort_rep(ctors[res].sort), res, sort_rep(ctors[res].sort), res,
                no_musttail ? "" : "RC_TAIL ", c);
        return;
    }
    if (cache_constants && k->arity == 0 && c != safepoint_ctor) {
        /* a judgment without arguments is a constant: its rules are pure and give at most one
         * result, so it is evaluated once; the value (or the failure) is kept outside the collected
         * spaces; each use is one trace event */
        fprintf(o, "static T *p%d_rules(void) {\n", c);
        emit_rules_body(o, c);
        fputs("}\n", o);
        fprintf(o, "static T *p%d_value; static int p%d_done;\n", c, c);
        fprintf(o, "static T *p%d(void) {\n"
                   "    if (!p%d_done) { int tr_ = rc_trace_on; rc_trace_on = 0; p%d_value = rc_make_static(p%d_rules()); rc_trace_on = tr_; p%d_done = 1; }\n"
                   "    if (rc_trace_on) { rc_trace_push((uint16_t)(RC_EV_CONST + %d)); if (!p%d_value) rc_trace_push(RC_EV_NO_RULE); }\n"
                   "    return p%d_value;\n}\n", c, c, c, c, c, c, c, c);
        return;
    }
    fprintf(o, "static T *p%d", c); emit_params(o, k->arity); fputs(" {\n", o);
    if (c == safepoint_ctor) {
        for (int a = 0; a < k->arity; a++) fprintf(o, "    rc_roots[%d] = a%d;\n", a, a);
        fprintf(o, "    rc_safepoint(%d);\n", k->arity);
        for (int a = 0; a < k->arity; a++) fprintf(o, "    a%d = rc_roots[%d];\n", a, a);
    }
    emit_rules_body(o, c);
    fputs("}\n", o);
}

static void c_ident(char *out, size_t cap, const char *name) {
    size_t i = 0;
    for (const char *p = name; *p && i + 2 < cap; p++) out[i++] = isalnum((unsigned char)*p) ? *p : '_';
    out[i] = 0;
}

static void emit_views(FILE *o) {
    bool any_ext = false;
    for (int s = 0; s < nsorts; s++) {
        if (sorts[s].native != REP_EXT) continue;
        any_ext = true;
        fprintf(o, "/* %s: represented by %s (constructors in this order) */\n#define RC_EXT_%s_SORT %d\n", sorts[s].name, sorts[s].repname, sorts[s].repname, s);
        for (int i = 0; i < sorts[s].nextctors; i++)
            fprintf(o, "#define RC_EXT_%s_C%d %d /* %s */\n", sorts[s].repname, i, sorts[s].extctors[i], ctors[sorts[s].extctors[i]].name);
    }
    if (any_ext) {
        if (!rep_header) die("external representations need --rep-header");
        fprintf(o, "#include \"%s\"\n", rep_header);
    }
    for (int s = 0; s < nsorts; s++) {
        int rep = sorts[s].native;
        if (rep == REP_RECORDS) {
            fprintf(o, "/* %s: the record cursor; an immediate index into the decoded records, viewed as %s / %s */\n",
                    sorts[s].name, ctors[rep_ctor[rep][0]].name, ctors[rep_ctor[rep][1]].name);
            fprintf(o, "extern uint64_t rc_record_count;\nT *rc_record_term(uint64_t i);\n");
            fprintf(o, "static inline uint16_t RC_TAG_records(T *x) { return RC_IMM_VAL(x) >= rc_record_count ? %d : %d; }\n",
                    rep_ctor[rep][0], rep_ctor[rep][1]);
            fprintf(o, "static inline T *RC_KID_records(T *x, int i) { uint64_t v = RC_IMM_VAL(x); return i == 0 ? rc_record_term(v) : RC_IMM(v + 1); }\n");
        } else if (rep == REP_NAT) {
            int z = rep_ctor[rep][0], b0 = rep_ctor[rep][1], b1 = rep_ctor[rep][2];
            fprintf(o, "/* %s: native naturals (immediates), viewed as %s / %s n (2n, n > 0) / %s n (2n+1) */\n",
                    sorts[s].name, ctors[z].name, ctors[b0].name, ctors[b1].name);
            fprintf(o, "static inline uint16_t RC_TAG_nat(T *x) { uint64_t v = RC_IMM_VAL(x); return v == 0 ? %d : (v & 1u) ? %d : %d; }\n", z, b1, b0);
            fprintf(o, "static inline T *RC_KID_nat(T *x, int i) { (void)i; return RC_IMM(RC_IMM_VAL(x) >> 1); }\n");
            fprintf(o, "#define RC_NUL_nat(c) RC_IMM(0)\n");
            fprintf(o, "static inline T *RC_MK1_nat(uint16_t c, T *m) {\n"
                       "    uint64_t v = RC_IMM_VAL(m);\n"
                       "    if (v > (UINT64_MAX >> 3)) rc_refinement_fail(\"natural too large for an immediate\");\n"
                       "    if (c == %d) { if (v == 0) rc_refinement_fail(\"%s of zero is not a canonical numeral\"); return RC_IMM(2 * v); }\n"
                       "    return RC_IMM(2 * v + 1);\n}\n", b0, ctors[b0].name);
        } else if (rep == REP_STR) {
            int nn = rep_ctor[rep][0], nc = rep_ctor[rep][1], by = rep_ctor[rep][2];
            fprintf(o, "/* %s: native byte strings, viewed as %s / %s (%s hi lo) rest */\n", sorts[s].name, ctors[nn].name, ctors[nc].name, ctors[by].name);
            fprintf(o, "static const uint16_t rc_nib_ctor[16] = {");
            for (int i = 0; i < 16; i++) fprintf(o, "%s%d", i ? ", " : "", str_nib[i]);
            fprintf(o, "};\n");
            fprintf(o, "T *rc_byte_term(unsigned v);\n");
            fprintf(o, "static inline uint16_t RC_TAG_str(T *x) { return x->n == 0 ? %d : %d; }\n", nn, nc);
            fprintf(o, "static inline T *RC_KID_str(T *x, int i) { const uint8_t *b = rc_bytes(x); return i == 0 ? rc_byte_term(b[0]) : rc_bytes_new(b + 1, x->n - 1); }\n");
            fprintf(o, "#define RC_NUL_str(c) ((T *)&rc_empty_bytes)\n");
            fprintf(o, "static inline unsigned rc_nib_value(T *n) { for (unsigned i = 0; i < 16; i++) if (n->tag == rc_nib_ctor[i]) return i; rc_refinement_fail(\"not a nibble\"); }\n");
            fprintf(o, "static inline T *RC_MK2_str(uint16_t c, T *b, T *s) {\n"
                       "    (void)c;\n"
                       "    if (b->tag != %d) rc_refinement_fail(\"not a byte\");\n"
                       "    uint8_t buf[1]; buf[0] = (uint8_t)(16u * rc_nib_value(b->k[0]) + rc_nib_value(b->k[1]));\n"
                       "    T *r = rc_bytes_new(NULL, (size_t)s->n + 1);\n"
                       "    uint8_t *d = (uint8_t *)r->k; d[0] = buf[0]; memcpy(d + 1, rc_bytes(s), s->n);\n"
                       "    return r;\n}\n", by);
        }
    }
}

static void emit_c(const char *path, const char *hpath, const char *wire_path) {
    FILE *o = fopen(path, "w");
    if (!o) die("cannot write %s", path);
    const char *wire_base = strrchr(wire_path, 0x2f) ? strrchr(wire_path, 0x2f) + 1 : wire_path;
    fprintf(o, "/* Generated by rulec from the core rows of module %s, theory %s (%s%s%s); do not edit.\n"
               " * %d sorts, %d constructors, %d rules.  One procedure per judgment: the decision DAG of its\n"
               " * dispatch group (validated by rulec_validate), then each rule's premises in written order. */\n",
            module_name, theory_name, wire_base, source_digest ? ", module SHA-256 " : "", source_digest ? source_digest : "", nsorts, nctors, nrules);
    fprintf(o, "#include \"%s\"\n#include <stdio.h>\n#include <stdlib.h>\n\n", include_rt);
    if (hpath) fprintf(o, "#include \"%s\"\n", strrchr(hpath, '/') ? strrchr(hpath, '/') + 1 : hpath);
    fprintf(o, "#define RC_NCTORS %d\n#define RC_NRULES %d\n", nctors, nrules);
    fprintf(o, "TH rc_nul[%d] = {\n", nctors);
    for (int c = 0; c < nctors; c++) fprintf(o, "    {%d, RC_SPACE_STATIC, 0, 0},%s", c, (c % 4 == 3 || c == nctors - 1) ? "\n" : "");
    fputs("};\n", o);
    if (nnative_ops || nlaws)
        fputs("#ifdef RC_CHECK_NATIVE\nextern int rc_check_native, rc_check_depth;\nvoid rc_native_checked(int op, T *native_result, T *rule_result);\n"
              "void rc_law_checked(int law, T *law_result, T *rule_result);\n#endif\n", o);
    if (nnative_ops) {
        for (int i = 0; i < nnative_ops; i++) {
            Ctor *k = &ctors[native_ops[i].ctor];
            fprintf(o, "T *%s", native_ops[i].fn); emit_params(o, k->arity); fputs(";\n", o);
        }
    }
    emit_views(o);
    fputc('\n', o);
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined) continue;
        fprintf(o, "static T *p%d", c); emit_params(o, ctors[c].arity); fputs(";\n", o);
    }
    for (int i = 0; i < nrules; i++)
        for (int j = 0; j < rules[i].np; j++)
            if (rules[i].p[j].relation) die("rule %s queries relation %s: relation premises are not compiled", rules[i].label, rules[i].p[j].relation);
    for (int i = 0; i < nnodes; i++)
        if (nodes[i].kind == N_LEAF && nodes[i].ncand > 1)
            die("a leaf of %s lists %d overlapping rules: leaves with several rules are not compiled", ctors[rules[nodes[i].cand[0]].head].name, nodes[i].ncand);
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) emit_proc(o, c);
    /* tables */
    fprintf(o, "\nconst int rc_nctors = %d, rc_nrules = %d, rc_nsorts = %d, rc_nnative_ops = %d;\n", nctors, nrules, nsorts, nnative_ops);
    fputs("const char *const rc_sort_name[] = {", o);
    for (int s = 0; s < nsorts; s++) fprintf(o, "%s\"%s\"", s ? ", " : "", sorts[s].name);
    fputs("};\nconst char *const rc_ctor_name[] = {\n", o);
    for (int c = 0; c < nctors; c++) fprintf(o, "    \"%s\",\n", ctors[c].name);
    fputs("};\nconst uint8_t rc_ctor_arity[] = {", o);
    for (int c = 0; c < nctors; c++) fprintf(o, "%s%d", c ? "," : "", ctors[c].arity);
    fputs("};\nconst uint16_t rc_ctor_sort[] = {", o);
    for (int c = 0; c < nctors; c++) fprintf(o, "%s%d", c ? "," : "", ctors[c].sort);
    fputs("};\nconst uint8_t rc_ctor_judgment[] = {", o);
    for (int c = 0; c < nctors; c++) fprintf(o, "%s%d", c ? "," : "", ctors[c].defined ? (ctors[c].det ? 2 : 1) : 0);
    fputs("};\nconst uint16_t rc_ctor_asort_off[] = {", o);
    { int off = 1; /* rc_ctor_asort[0] is a placeholder */
      for (int c = 0; c < nctors; c++) { fprintf(o, "%s%d", c ? "," : "", off); off += ctors[c].arity; } }
    fputs("};\nconst uint16_t rc_ctor_asort[] = {0", o);
    for (int c = 0; c < nctors; c++) for (int a = 0; a < ctors[c].arity; a++) fprintf(o, ",%d", ctors[c].asort[a]);
    fputs("};\nconst uint8_t rc_sort_rep[] = {", o);
    for (int s = 0; s < nsorts; s++) fprintf(o, "%s%d", s ? "," : "", sorts[s].native);
    fputs("};\nconst char *const rc_rule_label[] = {\n", o);
    for (int i = 0; i < nrules; i++) fprintf(o, "    \"%s\",\n", rules[i].label);
    fputs("};\nconst uint16_t rc_rule_head[] = {", o);
    for (int i = 0; i < nrules; i++) fprintf(o, "%s%d", i ? "," : "", rules[i].head);
    fputs("};\nconst int8_t rc_rule_layer[] = {", o);
    for (int i = 0; i < nrules; i++) fprintf(o, "%s%d", i ? "," : "", rules[i].layer);
    fprintf(o, "};\nconst int rc_nlayers = %d;\nconst char *const rc_layer_name[] = {", nlayers);
    for (int l = 0; l < nlayers; l++) fprintf(o, "%s\"%s\"", l ? ", " : "", layer_name[l]);
    fputs(nlayers ? "};\n" : "\"\"};\n", o);
    fputs("const char *const rc_native_op_block[] = {", o);
    for (int i = 0; i < nnative_ops; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", native_ops[i].name ? native_ops[i].name : "");
    fputs(nnative_ops ? "};\n" : "\"\"};\n", o);
    fputs("const char *const rc_native_op_label[] = {", o);
    for (int i = 0; i < nnative_ops; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", native_ops[i].label ? native_ops[i].label : "");
    fputs(nnative_ops ? "};\n" : "\"\"};\n", o);
    fputs("const char *const rc_law_label[] = {", o);
    for (int i = 0; i < nlaws; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", laws[i].label ? laws[i].label : "");
    fputs(nlaws ? "};\n" : "\"\"};\n", o);
    /* constants are kept per run: a run that changes the native store resets them */
    fputs("void rc_reset_constants(void) {\n", o);
    if (cache_constants)
        for (int c = 0; c < nctors; c++)
            if (ctors[c].defined && ctors[c].arity == 0 && c != safepoint_ctor && native_op_of(c) < 0 && law_of(c) < 0)
                fprintf(o, "    p%d_done = 0;\n", c);
    fputs("}\n", o);
    fputs("const char *const rc_native_op_name[] = {", o);
    for (int i = 0; i < nnative_ops; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", ctors[native_ops[i].ctor].name);
    fputs(nnative_ops ? "};\n" : "\"\"};\n", o);
    fputs("const char *const rc_law_name[] = {", o);
    for (int i = 0; i < nlaws; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", ctors[laws[i].ctor].name);
    fputs(nlaws ? "};\n" : "\"\"};\n", o);
    fputs("const char *const rc_law_justification[] = {", o);
    for (int i = 0; i < nlaws; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", laws[i].why ? laws[i].why : "");
    fputs(nlaws ? "};\n" : "\"\"};\n", o);
    fprintf(o, "const int rc_nlaws = %d;\n", nlaws);
    fputs("const char *const rc_native_op_justification[] = {", o);
    for (int i = 0; i < nnative_ops; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", native_ops[i].why ? native_ops[i].why : "");
    fputs(nnative_ops ? "};\n" : "\"\"};\n", o);
    /* entry: call a judgment by id with an argument vector */
    fputs("\nT *rc_call(int judgment, T **a) {\n    switch (judgment) {\n", o);
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined) continue;
        fprintf(o, "    case %d: return p%d(", c, c);
        for (int a = 0; a < ctors[c].arity; a++) fprintf(o, "%sa[%d]", a ? ", " : "", a);
        fputs(");\n", o);
    }
    fputs("    default: return (T *)0;\n    }\n}\n", o);
    fclose(o);
    if (hpath) {
        FILE *h = fopen(hpath, "w");
        if (!h) die("cannot write %s", hpath);
        fprintf(h, "/* Generated by rulec: constructor ids of module %s, theory %s; do not edit. */\n", module_name, theory_name);
        fputs("#ifndef RC_MODULE_IDS_H\n#define RC_MODULE_IDS_H\n", h);
        Map seen = {0};
        for (int c = 0; c < nctors; c++) {
            char id[256];
            c_ident(id, sizeof id, ctors[c].name);
            if (map_get(&seen, id) >= 0) die("two constructors have the C name %s", id);
            map_put(&seen, id, c);
            fprintf(h, "#define C_%s %d\n", id, c);
        }
        for (int s = 0; s < nsorts; s++) { char id[256]; c_ident(id, sizeof id, sorts[s].name); fprintf(h, "#define S_%s %d\n", id, s); }
        fputs("extern const int rc_nctors, rc_nrules, rc_nsorts, rc_nnative_ops;\n"
              "extern const char *const rc_sort_name[];\nextern const char *const rc_ctor_name[];\n"
              "extern const uint8_t rc_ctor_arity[];\nextern const uint16_t rc_ctor_sort[];\nextern const uint8_t rc_ctor_judgment[];\n"
              "extern const uint16_t rc_ctor_asort_off[];\nextern const uint16_t rc_ctor_asort[];\n"
              "extern const uint8_t rc_sort_rep[];\nextern const char *const rc_rule_label[];\nextern const uint16_t rc_rule_head[];\n"
              "extern const int8_t rc_rule_layer[];\nextern const int rc_nlayers;\nextern const char *const rc_layer_name[];\n"
              "extern const char *const rc_native_op_name[];\nextern const char *const rc_native_op_justification[];\n"
              "extern const char *const rc_law_name[];\nextern const char *const rc_law_justification[];\nextern const int rc_nlaws;\n"
              "extern const char *const rc_native_op_block[];\nextern const char *const rc_native_op_label[];\nextern const char *const rc_law_label[];\n"
              "void rc_reset_constants(void);\n"
              "T *rc_call(int judgment, T **a);\n#endif\n", h);
        fclose(h);
    }
}

/* every call of the safepoint judgment, and of every judgment that reaches it, is a tail call:
 * then at the safepoint no frame below holds a term */
static void check_safepoint(void) {
    if (!safepoint_name) return;
    safepoint_ctor = map_get(&ctor_map, safepoint_name);
    if (safepoint_ctor < 0 || !ctors[safepoint_ctor].defined) die("safepoint %s is not a judgment", safepoint_name);
    bool *in = xcalloc((size_t)nctors, 1);
    in[safepoint_ctor] = true;
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < nrules; i++) {
            Rule *r = &rules[i];
            for (int j = 0; j < r->np; j++) {
                if (r->p[j].relation || !in[r->p[j].src->ctor]) continue;
                if (j != r->np - 1 || !r->tail) err(r, "calls %s (which reaches the safepoint) other than as a tail call", ctors[r->p[j].src->ctor].name);
                if (!in[r->head]) { in[r->head] = true; changed = true; }
            }
        }
    }
    int n = 0;
    for (int c = 0; c < nctors; c++) if (in[c]) n++;
    fprintf(stderr, "rulec: safepoint %s reached through tail calls only, from %d judgments\n", safepoint_name, n);
    free(in);
}

static void parse_native_sort(const char *spec) {
    /* SORT=records(nil,cons) | SORT=nat(zero,b0,b1) | SORT=str(nil,cons,byte,nibprefix) */
    char *s = xstrdup(spec), *eq = strchr(s, '='), *lp, *rp;
    if (!eq || !(lp = strchr(eq, '(')) || !(rp = strrchr(eq, ')'))) die("bad --native-sort %s", spec);
    *eq = 0; *lp = 0; *rp = 0;
    int sort = sort_id(s);
    if (sort < 0) die("--native-sort: unknown sort %s", s);
    char *kind = eq + 1, *args[32]; int na = 0;
    for (char *t = strtok(lp + 1, ","); t && na < 32; t = strtok(NULL, ",")) args[na++] = t;
    if (!strncmp(kind, "ext:", 4)) {
        /* S=ext:NAME(c1,...,cn): every data constructor of S, in the order the header numbers them */
        sorts[sort].native = REP_EXT;
        sorts[sort].repname = xstrdup(kind + 4);
        sorts[sort].extctors = xcalloc((size_t)(na ? na : 1), sizeof(int));
        for (int i = 0; i < na; i++) {
            int c = map_get(&ctor_map, args[i]);
            if (c < 0 || ctors[c].sort != sort || ctors[c].defined) die("--native-sort %s: %s is not a data constructor of %s", spec, args[i], sorts[sort].name);
            sorts[sort].extctors[i] = c;
        }
        if (na != sorts[sort].ndata) die("--native-sort %s: names %d of the %d data constructors", spec, na, sorts[sort].ndata);
        sorts[sort].nextctors = na;
        free(s);
        return;
    }
    int rep = !strcmp(kind, "records") ? REP_RECORDS : !strcmp(kind, "nat") ? REP_NAT : !strcmp(kind, "str") ? REP_STR : -1;
    int want = rep == REP_RECORDS ? 2 : rep == REP_NAT ? 3 : rep == REP_STR ? 4 : -1;
    if (rep < 0 || na != want) die("bad --native-sort %s", spec);
    for (int i = 0; i < (rep == REP_STR ? 3 : na); i++) {
        rep_ctor[rep][i] = map_get(&ctor_map, args[i]);
        if (rep_ctor[rep][i] < 0) die("--native-sort: unknown constructor %s", args[i]);
    }
    int *rc = rep_ctor[rep];
    /* the shapes the views assume */
    if (rep == REP_RECORDS && !(ctors[rc[0]].sort == sort && ctors[rc[0]].arity == 0 && ctors[rc[1]].sort == sort &&
                                ctors[rc[1]].arity == 2 && ctors[rc[1]].asort[1] == sort && sorts[sort].ndata == 2))
        die("--native-sort %s: not a list sort with exactly these two constructors", spec);
    if (rep == REP_NAT && !(sorts[sort].ndata == 3 && ctors[rc[0]].sort == sort && ctors[rc[0]].arity == 0 &&
                            ctors[rc[1]].arity == 1 && ctors[rc[1]].asort[0] == sort && ctors[rc[2]].arity == 1 &&
                            ctors[rc[2]].asort[0] == sort && ctors[rc[1]].sort == sort && ctors[rc[2]].sort == sort))
        die("--native-sort %s: not a binary-numeral sort with exactly these three constructors", spec);
    if (rep == REP_STR) {
        int by = rc[2];
        if (!(sorts[sort].ndata == 2 && ctors[rc[0]].arity == 0 && ctors[rc[1]].arity == 2 && ctors[rc[1]].asort[1] == sort &&
              ctors[rc[1]].asort[0] == ctors[by].sort && ctors[by].arity == 2 && ctors[by].asort[0] == ctors[by].asort[1] &&
              sorts[ctors[by].sort].ndata == 1 && sorts[ctors[by].asort[0]].ndata == 16))
            die("--native-sort %s: not a byte-list sort", spec);
        for (int i = 0; i < 16; i++) {
            char nm[256];
            snprintf(nm, sizeof nm, "%s%x", args[3], i);
            str_nib[i] = map_get(&ctor_map, nm);
            if (str_nib[i] < 0 || ctors[str_nib[i]].arity != 0 || ctors[str_nib[i]].sort != ctors[by].asort[0])
                die("--native-sort %s: nibble constructor %s missing", spec, nm);
        }
    }
    sorts[sort].native = rep;
    free(s);
}

int main(int argc, char **argv) {
    const char *wire = NULL, *census = NULL, *census_tsv = NULL, *dag = NULL, *emit = NULL, *emit_h = NULL;
    const char *native_sorts[16]; int nns = 0;
    const char *native_op_specs[64]; int nos = 0;
    const char *law_specs[64]; int nls = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--census") && i + 1 < argc) census = argv[++i];
        else if (!strcmp(argv[i], "--census-tsv") && i + 1 < argc) census_tsv = argv[++i];
        else if (!strcmp(argv[i], "--dag") && i + 1 < argc) dag = argv[++i];
        else if (!strcmp(argv[i], "--emit") && i + 1 < argc) emit = argv[++i];
        else if (!strcmp(argv[i], "--emit-h") && i + 1 < argc) emit_h = argv[++i];
        else if (!strcmp(argv[i], "--safepoint") && i + 1 < argc) safepoint_name = argv[++i];
        else if (!strcmp(argv[i], "--no-musttail")) no_musttail = true;
        else if (!strcmp(argv[i], "--census-only")) census_only = true;
        else if (!strcmp(argv[i], "--cache-constants")) cache_constants = true;
        else if (!strcmp(argv[i], "--include-rt") && i + 1 < argc) include_rt = argv[++i];
        else if (!strcmp(argv[i], "--rep-header") && i + 1 < argc) rep_header = argv[++i];
        else if (!strcmp(argv[i], "--blocks") && i + 1 < argc) blocks_file = argv[++i];
        else if (!strcmp(argv[i], "--source-digest") && i + 1 < argc) source_digest = argv[++i];
        else if (!strcmp(argv[i], "--native-sort") && i + 1 < argc) native_sorts[nns++] = argv[++i];
        else if (!strcmp(argv[i], "--native-op") && i + 1 < argc) native_op_specs[nos++] = argv[++i];
        else if (!strcmp(argv[i], "--refl-law") && i + 1 < argc) law_specs[nls++] = argv[++i];
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) {
            char *s = xstrdup(argv[++i]);
            for (char *t = strtok(s, ","); t && nlayers < MAXLAYERS; t = strtok(NULL, ",")) {
                char *eq = strchr(t, '=');
                if (!eq) die("bad --layers");
                *eq = 0;
                layer_name[nlayers] = t; layer_first[nlayers++] = eq + 1;
            }
        } else if (argv[i][0] != '-' && !wire) wire = argv[i];
        else die("usage: rulec WIRE [--census F] [--census-tsv F] [--dag F] [--emit F.c] [--emit-h F.h] [--layers L=label,...] [--safepoint J] [--native-sort S=kind(...)] [--native-op J=fn[:why]]");
    }
    if (!wire) die("no wire file");
    load_wire(wire);
    check_rules();
    if (nerrors) { fprintf(stderr, "rulec: %d check failures; nothing emitted\n", nerrors); return 1; }
    for (int i = 0; i < nns; i++) parse_native_sort(native_sorts[i]);
    if (blocks_file) read_blocks(blocks_file);
    for (int i = 0; i < nblock_rows; i++) {
        BlockRow *b = &block_rows[i];
        if (b->law) continue;
        int c = map_get(&ctor_map, b->judgment);
        if (c < 0 || !ctors[c].defined) die("block %s: %s is not a judgment", b->name, b->judgment);
        for (int j = 0; j < nnative_ops; j++) if (native_ops[j].ctor == c) die("block %s: %s is bound twice", b->name, b->judgment);
        native_ops[nnative_ops++] = (NativeOp){c, b->fn, b->why, b->name, b->label};
    }
    for (int i = 0; i < nos; i++) {
        char *s = xstrdup(native_op_specs[i]), *eq = strchr(s, '='), *colon;
        if (!eq) die("bad --native-op");
        *eq = 0;
        colon = strchr(eq + 1, ':');
        if (colon) *colon = 0;
        int c = map_get(&ctor_map, s);
        if (c < 0 || !ctors[c].defined) die("--native-op: %s is not a judgment", s);
        native_ops[nnative_ops++] = (NativeOp){c, eq + 1, colon ? colon + 1 : NULL, xstrdup(s),
            colon && !strncmp(colon + 1, "proved", 6) ? "proved" : colon && !strncmp(colon + 1, "trusted", 7) ? "trusted" : "checked"};
    }
    assign_layers();
    build_dags();
    analyse_determinism();
    analyse_calls();
    check_safepoint();
    for (int i = 0; i < nblock_rows; i++) {
        BlockRow *b = &block_rows[i];
        if (!b->law) continue;
        int c = map_get(&ctor_map, b->judgment), r = map_get(&ctor_map, b->fn);
        if (c < 0 || !ctors[c].defined || ctors[c].arity != 2 || ctors[c].asort[0] != ctors[c].asort[1])
            die("law %s: %s is not a judgment of two arguments of one sort", b->name, b->judgment);
        if (r < 0 || ctors[r].defined || ctors[r].arity != 0 || ctors[r].sort != ctors[c].sort)
            die("law %s: %s is not a nullary constructor of %s's sort", b->name, b->fn, b->judgment);
        if (!ctors[c].complete) die("law %s: %s does not cover every input", b->name, b->judgment);
        for (int j = 0; j < nnative_ops; j++) if (native_ops[j].ctor == c) die("law %s: %s already has a block", b->name, b->judgment);
        laws[nlaws++] = (Law){c, r, b->why, b->name, b->label};
    }
    /* laws need the coverage analysis */
    for (int i = 0; i < nls; i++) {
        /* J=RESULT[@justification]: J has two arguments of one sort; RESULT is a nullary constructor of J's sort */
        char *s = xstrdup(law_specs[i]), *eq = strchr(s, '='), *colon;
        if (!eq) die("bad --refl-law");
        *eq = 0;
        colon = strchr(eq + 1, '@');
        if (colon) *colon = 0;
        int c = map_get(&ctor_map, s), r = map_get(&ctor_map, eq + 1);
        if (c < 0 || !ctors[c].defined || ctors[c].arity != 2 || ctors[c].asort[0] != ctors[c].asort[1])
            die("--refl-law: %s is not a judgment of two arguments of one sort", s);
        if (r < 0 || ctors[r].defined || ctors[r].arity != 0 || ctors[r].sort != ctors[c].sort)
            die("--refl-law: %s is not a nullary constructor of %s's sort", eq + 1, s);
        if (native_op_of(c) >= 0) die("--refl-law: %s already has a native operation", s);
        if (!ctors[c].complete) die("--refl-law: %s does not cover every input, so it cannot be reflexive on every input", s);
        laws[nlaws++] = (Law){c, r, colon ? colon + 1 : NULL, xstrdup(s), colon && !strncmp(colon + 1, "proved", 6) ? "proved" : "checked"};
    }
    if (nerrors) { fprintf(stderr, "rulec: %d check failures; nothing emitted\n", nerrors); return 1; }
    int nj = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) nj++;
    fprintf(stderr, "rulec: %s/%s: %d sorts, %d constructors, %d judgments, %d rules, %d DAG nodes, %d occurrences\n",
            module_name, theory_name, nsorts, nctors, nj, nrules, nnodes, noccs);
    if (census) { write_census(census, census_tsv); write_census_blocks(census); }
    if (dag) { write_dag(dag); write_block_table(dag, "a"); }
    if (emit && census_only) die("--census-only: nothing is emitted");
    if (emit) emit_c(emit, emit_h, wire);
    return 0;
}
