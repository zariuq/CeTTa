/* check.c - see rulec.h */
#include "rulec.h"

/* ------------------------------------------------------------------- checks */

bool pat_has_judgment(const Pat *p) {
    if (p->var != -1) return false;
    if (p->ctor >= 0 && ctors[p->ctor].defined) return true;
    for (int i = 0; i < p->n; i++) if (pat_has_judgment(p->k[i])) return true;
    return false;
}
void pat_vars(const Pat *p, int *count) {  /* count[v]++ for each occurrence */
    if (p->var >= 0) { count[p->var]++; return; }
    for (int i = 0; i < p->n; i++) pat_vars(p->k[i], count);
}
bool pat_tests_bound(const Pat *p, const bool *eqv) {
    if (!eqv) return false;
    if (p->var >= 0) return eqv[p->var];
    for (int i = 0; i < p->n; i++) if (pat_tests_bound(p->k[i], eqv)) return true;
    return false;
}
bool pat_unify(const Pat *a, const Pat *b) {  /* linear, disjoint variables */
    if (a->var != -1 || b->var != -1) return true;
    if (a->ctor != b->ctor) return false;
    for (int i = 0; i < a->n; i++) if (!pat_unify(a->k[i], b->k[i])) return false;
    return true;
}
bool pat_equal(const Pat *a, const Pat *b) {
    if ((a->var != -1) != (b->var != -1)) return false;
    if (a->var != -1) return a->var == b->var;
    if (a->ctor != b->ctor || a->n != b->n) return false;
    for (int i = 0; i < a->n; i++) if (!pat_equal(a->k[i], b->k[i])) return false;
    return true;
}

/* ---- overlapping rules ----
 * Two rules of one judgment overlap when their left sides unify.  Each overlapping pair is classified
 * (OverlapClass) and recorded for the census; a pair that is neither disjoint by its premises nor a
 * stated relation fails the build.  Rule order and priority are never used: rulec only compiles an
 * overlap it has shown cannot matter.
 *
 * Terms of the analysis live in one store (UT) with one variable space; each rule taken into the
 * analysis gets fresh variables.  Bindings are recorded on a trail so that a trial can be undone. */
bool census_only;
int n_overlaps, n_overlaps_exclusive, n_overlaps_unresolved;
int n_overlaps_class[OV_COUNT];
OverlapPair *overlap_pairs; int noverlap_pairs, overlap_pair_cap;
const char *overlap_class_name[OV_COUNT] = {"exclusive-by-targets", "exclusive-by-evaluated-guards", "unresolved"};
typedef struct { int var; int ctor; int n; int k[16]; } UT;   /* var >= 0: variable; else constructor */
UT *ut; int nut, utcap;
int *ubind; int nubind, ubindcap;
int *utrail; int nutrail, utrailcap;
int ut_from(const Pat *p, int off) {
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
void u_reset(void) { nut = 0; nubind = 0; nutrail = 0; }
int u_fresh(int n) {   /* n new unbound variables; the first one's number */
    int base = nubind;
    nubind += n;
    if (nubind > ubindcap) { ubindcap = 2 * nubind + 64; ubind = xrealloc(ubind, (size_t)ubindcap * sizeof(int)); }
    for (int i = base; i < nubind; i++) ubind[i] = -1;
    return base;
}
static void u_bind(int v, int t) { ubind[v] = t; VEC_PUSH(utrail, nutrail, utrailcap, v); }
void u_undo(int mark) { while (nutrail > mark) ubind[utrail[--nutrail]] = -1; }
int u_resolve(int t) {
    while (ut[t].var >= 0 && ubind[ut[t].var] >= 0) t = ubind[ut[t].var];
    return t;
}
bool u_occurs(int v, int t) {
    t = u_resolve(t);
    if (ut[t].var >= 0) return ut[t].var == v;
    for (int i = 0; i < ut[t].n; i++) if (u_occurs(v, ut[t].k[i])) return true;
    return false;
}
bool u_unify(int a, int b) {
    a = u_resolve(a); b = u_resolve(b);
    if (a == b) return true;
    if (ut[a].var >= 0 && ut[b].var >= 0 && ut[a].var == ut[b].var) return true;
    if (ut[a].var >= 0) { if (u_occurs(ut[a].var, b)) return false; u_bind(ut[a].var, b); return true; }
    if (ut[b].var >= 0) { if (u_occurs(ut[b].var, a)) return false; u_bind(ut[b].var, a); return true; }
    if (ut[a].ctor != ut[b].ctor || ut[a].n != ut[b].n) return false;
    for (int i = 0; i < ut[a].n; i++) if (!u_unify(ut[a].k[i], ut[b].k[i])) return false;
    return true;
}
bool u_equal(int a, int b) {   /* identical under the current bindings */
    a = u_resolve(a); b = u_resolve(b);
    if (a == b) return true;
    if (ut[a].var >= 0 || ut[b].var >= 0) return ut[a].var >= 0 && ut[b].var >= 0 && ut[a].var == ut[b].var;
    if (ut[a].ctor != ut[b].ctor || ut[a].n != ut[b].n) return false;
    for (int i = 0; i < ut[a].n; i++) if (!u_equal(ut[a].k[i], ut[b].k[i])) return false;
    return true;
}
/* t with every bound variable replaced by its value (a new term); *local is set when a variable
 * numbered from base on remains */
static int u_zonk(int t, int base, bool *local) {
    t = u_resolve(t);
    if (ut[t].var >= 0) { if (ut[t].var >= base) *local = true; return t; }
    if (ut[t].n == 0) return t;
    UT x = ut[t];
    for (int i = 0; i < x.n; i++) x.k[i] = u_zonk(x.k[i], base, local);
    VEC_PUSH(ut, nut, utcap, x);
    return nut - 1;
}

/* ---- evaluation of guards ----
 * eval_call(t) answers, for a call t (a judgment applied to terms with variables), one of
 *   EV_UNKNOWN  nothing is claimed;
 *   EV_NONE     no instance of t has a result (every rule whose left side unifies with t is excluded);
 *   v >= 0      every result of every instance tθ is vθ (v is a term over t's variables).
 * It takes every rule whose left side unifies with t, with fresh variables, and evaluates the rule's
 * premises in order (eval_premises): a premise whose call is EV_NONE, or whose value does not unify
 * with its target, excludes the rule; an EV_UNKNOWN premise leaves its target's variables free; a
 * relation query constrains nothing.  A rule not excluded contributes its right side, which must
 * mention no variable of its own; all contributions must be identical.  Claims are about derivations
 * (by induction on a derivation's height), so they hold whether or not a judgment is deterministic;
 * evaluation is cut off by depth and by a budget, which gives EV_UNKNOWN. */
#define EV_UNKNOWN (-1)
#define EV_NONE (-2)
static long ev_budget;
static int eval_call(int t, int depth);
static bool eval_premises(const Rule *r, int base, int depth) {
    for (int j = 0; j < r->np; j++) {
        const Prem *p = &r->p[j];
        if (p->relation) continue;
        int v = eval_call(ut_from(p->src, base), depth + 1);
        if (v == EV_NONE) return false;
        if (v == EV_UNKNOWN) continue;
        if (!u_unify(v, ut_from(p->tgt, base))) return false;
    }
    return true;
}
static int eval_call(int t, int depth) {
    t = u_resolve(t);
    if (ut[t].var >= 0) return EV_UNKNOWN;
    int c = ut[t].ctor;
    if (c < 0 || !ctors[c].defined) return EV_UNKNOWN;
    if (depth > 64 || --ev_budget < 0) return EV_UNKNOWN;
    const Ctor *k = &ctors[c];
    int result = EV_NONE;
    for (int i = 0; i < k->nrules; i++) {
        const Rule *r = &rules[k->rules[i]];
        int mark = nutrail, base = u_fresh(r->nv);
        if (!u_unify(ut_from(r->lhs, base), t) || !eval_premises(r, base, depth)) { u_undo(mark); continue; }
        bool local = false;
        int rv = u_zonk(ut_from(r->rhs, base), base, &local);
        u_undo(mark);
        if (local) return EV_UNKNOWN;
        if (result == EV_NONE) result = rv;
        else if (!u_equal(result, rv)) return EV_UNKNOWN;
    }
    return result;
}

/* judgments that may give several results (a rule with a relation query that binds, directly or
 * through a call); the lockstep argument below needs a callee with at most one result */
static bool *multi_valued;
static void find_multi_valued(void) {
    multi_valued = xcalloc((size_t)(nctors ? nctors : 1), sizeof(bool));
    for (bool changed = true; changed;) {
        changed = false;
        for (int c = 0; c < nctors; c++) {
            if (!ctors[c].defined || multi_valued[c]) continue;
            for (int i = 0; i < ctors[c].nrules && !multi_valued[c]; i++) {
                const Rule *r = &rules[ctors[c].rules[i]];
                for (int j = 0; j < r->np; j++)
                    if ((r->p[j].relation && r->p[j].binds) || (!r->p[j].relation && r->p[j].src->ctor >= 0 && multi_valued[r->p[j].src->ctor])) {
                        multi_valued[c] = true; changed = true; break;
                    }
            }
        }
    }
}

/* the class of an overlapping pair a, b (their left sides unify):
 *   OV_TARGETS   the premises, walked in lockstep, call the same single-valued judgment on equal
 *                arguments and the two targets clash (the call has one result, which cannot match
 *                both).  That the callee is single-valued is the statement being proved for all
 *                judgments at once, by induction on the height of derivations; it holds when every
 *                overlap of the module is shown exclusive, which a successful build requires;
 *   OV_EVALUATED the premises of a and b, evaluated under the unifier of the left sides (and the
 *                lockstep equalities), cannot all hold: some premise's call has no result, or its
 *                value clashes with its target (eval_call);
 *   OV_UNRESOLVED neither. */
int overlap_class(const Rule *a, const Rule *b) {
    u_reset();
    int oa = u_fresh(a->nv), ob = u_fresh(b->nv);
    if (!u_unify(ut_from(a->lhs, oa), ut_from(b->lhs, ob))) die("internal: overlap_class on disjoint rules");
    for (int j = 0; j < a->np && j < b->np; j++) {
        const Prem *pa = &a->p[j], *pb = &b->p[j];
        if (pa->relation || pb->relation) break;
        if (pa->src->ctor != pb->src->ctor || multi_valued[pa->src->ctor]) break;
        if (!u_equal(ut_from(pa->src, oa), ut_from(pb->src, ob))) break;
        if (!u_unify(ut_from(pa->tgt, oa), ut_from(pb->tgt, ob))) return OV_TARGETS;
    }
    ev_budget = 200000;
    if (!eval_premises(a, oa, 0) || !eval_premises(b, ob, 0)) return OV_EVALUATED;
    return OV_UNRESOLVED;
}

/* a candidate rule at a decision leaf: excluded when its premises cannot hold for any input that
 * reaches the leaf.  input is the leaf's knowledge of the arguments (a term built by the caller with
 * u_reset/u_fresh/ut store terms: the judgment applied to the tested constructors, variables
 * elsewhere).  The bindings are undone. */
bool rule_excluded_at(const Rule *r, int input) {
    int mark = nutrail, base = u_fresh(r->nv);
    ev_budget = 200000;
    bool excluded = !u_unify(ut_from(r->lhs, base), input) || !eval_premises(r, base, 0);
    u_undo(mark);
    return excluded;
}
int ut_push(int ctor, int n, const int *kids) {
    UT x = {-1, ctor, n, {0}};
    if (n > 16) die("constructor arity above 16");
    for (int i = 0; i < n; i++) x.k[i] = kids[i];
    VEC_PUSH(ut, nut, utcap, x);
    return nut - 1;
}
int ut_var(void) {
    UT x = {u_fresh(1), -1, 0, {0}};
    VEC_PUSH(ut, nut, utcap, x);
    return nut - 1;
}

void check_rules(void) {
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
    /* overlaps: two left sides of one judgment that unify; each pair is classified (overlap_class);
     * an unresolved pair fails the build unless the run only takes the census */
    find_multi_valued();
    for (int c = 0; c < nctors; c++) {
        Ctor *k = &ctors[c];
        for (int i = 0; i < k->nrules; i++)
            for (int j = i + 1; j < k->nrules; j++) {
                Rule *a = &rules[k->rules[i]], *b = &rules[k->rules[j]];
                if (!pat_unify(a->lhs, b->lhs)) continue;
                n_overlaps++;
                int cls = overlap_class(a, b);
                n_overlaps_class[cls]++;
                OverlapPair op = {c, a->id, b->id, cls};
                VEC_PUSH(overlap_pairs, noverlap_pairs, overlap_pair_cap, op);
                if (cls != OV_UNRESOLVED) { n_overlaps_exclusive++; continue; }
                k->overlap_open = true;
                n_overlaps_unresolved++;
                if (census_only) { if (n_overlaps_unresolved <= 12) fprintf(stderr, "rulec: census: \"%s\" and \"%s\" overlap; not shown exclusive\n", a->label, b->label); }
                else err(a, "left side overlaps rule \"%s\" and their premises are not shown exclusive", b->label);
            }
    }
}

/* --------------------------------------------------------- determinism, calls */

bool irrefutable(const Pat *p) {
    if (p->var != -1) return true;
    if (sorts[p->sort].ndata != 1) return false;
    for (int i = 0; i < p->n; i++) if (!irrefutable(p->k[i])) return false;
    return true;
}

bool prem_irrefutable(const Prem *p) {
    if (p->relation) return false;
    if (!irrefutable(p->tgt)) return false;
    int cnt[1024] = {0};
    (void)cnt;
    return !pat_tests_bound(p->tgt, p->eqv);
}
bool relational_rule(const Rule *r) {   /* a relation query that binds a variable: possibly several answers */
    for (int j = 0; j < r->np; j++) if (r->p[j].relation && r->p[j].binds) return true;
    return false;
}

void analyse_determinism(void) {
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
int *tj_index, *tj_low, *tj_stack, tj_sp, tj_counter, nscc;
bool *tj_on;
void tarjan(int v) {
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
void analyse_calls(void) {
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

/* every call of the safepoint judgment, and of every judgment that reaches it, is a tail call, or a
 * call whose result only a tail call of a wrapper takes (t := J(...) then W(..., t, ...), W not
 * reaching the safepoint), made a pending wrapper (emit_c.c): then at the safepoint no frame below
 * holds a term, and a run can stop there and be resumed from the safepoint's arguments */
bool *pend_required;   /* per rule: its call into the safepoint is made a pending wrapper */
static bool pend_shape(const Rule *r, int j) {
    if (!r->tail || j != r->np - 2 || r->np < 2) return false;
    const Prem *pj = &r->p[j], *pw = &r->p[r->np - 1];
    if (pw->relation) return false;
    int t = pj->tgt->var;
    if (t < 0 || (pj->eqv && pj->eqv[t])) return false;
    int uses = 0, whole = 0;
    for (int a = 0; a < pw->src->n; a++) {
        if (pw->src->k[a]->var == t) whole++;
        uses += pat_count_var(pw->src->k[a], t);
    }
    return whole == 1 && uses == 1;
}
void check_safepoint(void) {
    pend_required = xcalloc((size_t)(nrules ? nrules : 1), 1);
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
                if (!(j == r->np - 1 && r->tail) && !pend_shape(r, j)) err(r, "calls %s (which reaches the safepoint) other than as a tail call", ctors[r->p[j].src->ctor].name);
                if (!in[r->head]) { in[r->head] = true; changed = true; }
            }
        }
    }
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        if (r->np < 2 || r->p[r->np - 2].relation || !in[r->p[r->np - 2].src->ctor] || !pend_shape(r, r->np - 2)) continue;
        if (in[r->p[r->np - 1].src->ctor]) err(r, "its wrapper %s reaches the safepoint", ctors[r->p[r->np - 1].src->ctor].name);
        pend_required[i] = true;
    }
    int n = 0;
    for (int c = 0; c < nctors; c++) if (in[c]) n++;
    fprintf(stderr, "rulec: safepoint %s reached through tail calls only, from %d judgments\n", safepoint_name, n);
    check_memo_safepoint(in);
    free(in);
}

