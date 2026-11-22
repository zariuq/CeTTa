/* emit_c.c - see rulec.h */
#include "rulec.h"

bool pat_has_var(const Pat *p) {
    if (p->var >= 0) return true;
    for (int i = 0; i < p->n; i++) if (pat_has_var(p->k[i])) return true;
    return false;
}

void occ_expr(char *out, size_t cap, int occ) {
    Occ *x = &occs[occ];
    char tmp[8192];
    snprintf(out, cap, "a%d", x->p[0]);
    for (int l = 1; l < x->len; l++) {
        snprintf(tmp, sizeof tmp, "RC_KID_%s(%s, %d)", sort_rep(x->s[l - 1]), out, x->p[l]);
        snprintf(out, cap, "%s", tmp);
    }
}

/* With --cache-constants, a ground data term (no variable, at least one child) of a right side or of
 * a call's arguments is a kept constant: built at its first use and kept until the native store is
 * freed (rc_reset_constants clears it).  Only sorts whose values never change are kept: symbolic,
 * interned and immediate representations. */
static int nground;
static bool ground_keepable(const Pat *p) {
    if (p->var != -1 || p->ctor < 0 || ctors[p->ctor].defined) return false;
    int r = sorts[ctors[p->ctor].sort].native;
    if (!(r == RC_REP_SYMBOLIC || r == RC_REP_INTERN || r == RC_REP_BYTES || REP_IMMNAT(r) || r == RC_REP_WORD || r == RC_REP_BINARY || r == RC_REP_SLICES))
        return false;
    for (int i = 0; i < p->n; i++) if (!ground_keepable(p->k[i])) return false;
    return true;
}
static void ground_mark(Pat *p) {
    if (p->var != -1 || p->ctor < 0) return;
    if (!ctors[p->ctor].defined && p->n > 0 && ground_keepable(p)) { if (!p->ground) p->ground = ++nground; return; }
    for (int i = 0; i < p->n; i++) ground_mark(p->k[i]);
}
void number_ground(void) {
    nground = 0;
    if (!cache_constants) return;
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        ground_mark(r->rhs);
        for (int j = 0; j < r->np; j++) {
            if (r->p[j].relation) { for (int a = 0; a < r->p[j].nrel; a++) ground_mark(r->p[j].rel[a]); continue; }
            ground_mark(r->p[j].src);
        }
    }
}
/* A data term the rule already holds is not built again: a subterm of the right side (or of a call's
 * arguments) equal to a subpattern of the left side, or of the target of an earlier premise, is that
 * matched value (values are immutable, and a matched value is the value of its pattern).  Not for
 * sorts whose values are cells updated in place (tables, external representations). */
static const Rule *occ_rule;   /* the rule being emitted (NULL: no reuse) */
static int occ_upto;           /* the premises whose targets are bound */
static bool pat_same(const Pat *x, const Pat *y) {
    if (x->var >= 0 || y->var >= 0) return x->var == y->var;
    if (x->var != -1 || y->var != -1 || x->ctor != y->ctor || x->n != y->n) return false;
    for (int i = 0; i < x->n; i++) if (!pat_same(x->k[i], y->k[i])) return false;
    return true;
}
static bool occ_in(const Pat *s, const Pat *p, const char *e, char *out, size_t cap) {
    if (s->var != -1 || s->ctor < 0) return false;
    if (pat_same(s, p)) { snprintf(out, cap, "%s", e); return true; }
    for (int i = 0; i < s->n; i++) {
        char sub[4096];
        snprintf(sub, sizeof sub, "RC_KID_%s(%s, %d)", sort_rep(s->sort), e, i);
        if (occ_in(s->k[i], p, sub, out, cap)) return true;
    }
    return false;
}
static bool occ_reusable(const Pat *p) {
    if (p->var != -1 || p->ctor < 0 || ctors[p->ctor].defined || p->n == 0) return false;
    int r = sorts[ctors[p->ctor].sort].native;
    return !(r == RC_REP_TABLE || r == RC_REP_RECORDS);
}
static bool occ_find(const Rule *r, int upto, const Pat *p, char *out, size_t cap) {
    if (!r || !occ_reusable(p)) return false;
    for (int a = 0; a < r->lhs->n; a++) {
        char e[16];
        snprintf(e, sizeof e, "a%d", a);
        if (occ_in(r->lhs->k[a], p, e, out, cap)) return true;
    }
    for (int j = 0; j < upto && j < r->np; j++) {
        if (r->p[j].relation) continue;
        char e[16];
        snprintf(e, sizeof e, "t%d", j);
        if (occ_in(r->p[j].tgt, p, e, out, cap)) return true;
    }
    return false;
}
/* the variables a build reads, skipping subterms that are reused */
static void mark_used_build(const Rule *r, int upto, const Pat *p) {
    char e[8192];
    if (occ_find(r, upto, p, e, sizeof e)) return;
    if (p->var >= 0) { var_used[p->var] = true; return; }
    for (int i = 0; i < p->n; i++) mark_used_build(r, upto, p->k[i]);
}
static void emit_build_now(FILE *o, const Pat *p);
void emit_build(FILE *o, const Pat *p) {
    char e[8192];
    if (!p->ground && occ_find(occ_rule, occ_upto, p, e, sizeof e)) { fputs(e, o); return; }
    if (p->ground) {
        fprintf(o, "(rc_ground[%d] ? rc_ground[%d] : (rc_ground[%d] = rc_make_static(", p->ground - 1, p->ground - 1, p->ground - 1);
        emit_build_now(o, p);
        fputs(")))", o);
        return;
    }
    emit_build_now(o, p);
}
static void emit_build_now(FILE *o, const Pat *p) {
    if (p->var >= 0) { fprintf(o, "v%d", p->var); return; }
    const Ctor *c = &ctors[p->ctor];
    int rep = sorts[c->sort].native;
    if (rep == RC_REP_RECORDS) die("rule builds %s, but its sort is read through the record cursor", c->name);
    if (p->n == 0) { fprintf(o, "RC_NUL_%s(%d)", sort_rep(c->sort), p->ctor); return; }
    if (p->n > 8) {
        /* more than eight children: the representation's array form */
        fprintf(o, "RC_MKV_%s(%d, %d, ((T *[]){", sort_rep(c->sort), p->ctor, p->n);
        for (int i = 0; i < p->n; i++) { if (i) fputs(", ", o); emit_build(o, p->k[i]); }
        fputs("}))", o);
        return;
    }
    fprintf(o, "RC_MK%d_%s(%d", p->n, sort_rep(c->sort), p->ctor);
    for (int i = 0; i < p->n; i++) { fputs(", ", o); emit_build(o, p->k[i]); }
    fputc(')', o);
}

/* variables the code reads: those of premise sources and of the right side (a tail call's result
 * is returned directly, so its target variable is never read) */
bool *var_used;
bool pat_has_used_var(const Pat *p) {
    if (p->var >= 0) return var_used[p->var];
    for (int i = 0; i < p->n; i++) if (pat_has_used_var(p->k[i])) return true;
    return false;
}
void mark_used(const Pat *p) {
    if (p->var >= 0) { var_used[p->var] = true; return; }
    for (int i = 0; i < p->n; i++) mark_used(p->k[i]);
}

void emit_lhs_bind(FILE *o, const Pat *p, const char *e) {
    if (p->var >= 0) { if (var_used[p->var]) fprintf(o, "        T *v%d = %s;\n", p->var, e); return; }
    for (int i = 0; i < p->n; i++) {
        if (!pat_has_used_var(p->k[i])) continue;
        char sub[8192];
        snprintf(sub, sizeof sub, "RC_KID_%s(%s, %d)", sort_rep(p->sort), e, i);
        emit_lhs_bind(o, p->k[i], sub);
    }
}

int tmpno;
const bool *cur_eqv;   /* variables of the current premise's target that are equality tests */
/* what failing code does: a rule alone at its leaf returns the failure (with its trace event); a
 * candidate of a trial leaf goes on to the next candidate; the last candidate ends the trial and
 * returns the failure */
static char fail_target_act[256], fail_call_act[256], fail_row_act[256];
static void set_fail_actions(int judgment, const char *next, const char *trial_end) {
    if (next) {
        snprintf(fail_target_act, sizeof fail_target_act, "goto %s;", next);
        snprintf(fail_call_act, sizeof fail_call_act, "goto %s;", next);
        snprintf(fail_row_act, sizeof fail_row_act, "goto %s;", next);
    } else if (trial_end) {
        snprintf(fail_target_act, sizeof fail_target_act, "{ %s return rc_fail_target(%d); }", trial_end, judgment);
        snprintf(fail_call_act, sizeof fail_call_act, "{ %s return (T *)0; }", trial_end);
        snprintf(fail_row_act, sizeof fail_row_act, "{ %s return rc_no_row(%d); }", trial_end, judgment);
    } else {
        snprintf(fail_target_act, sizeof fail_target_act, "return rc_fail_target(%d);", judgment);
        snprintf(fail_call_act, sizeof fail_call_act, "return (T *)0;");
        snprintf(fail_row_act, sizeof fail_row_act, "return rc_no_row(%d);", judgment);
    }
}
void emit_tgt_match(FILE *o, const Pat *p, const char *e, int judgment) {
    (void)judgment;
    if (p->var >= 0) {
        /* an equality test: by identity or structure, or through the views for a sort whose equal
         * values may differ as words (byte strings as slices) */
        if (cur_eqv && cur_eqv[p->var] && sorts[p->sort].native == RC_REP_SLICES)
            fprintf(o, "        if (!rc_value_equal(%d, %s, v%d)) %s\n", p->sort, e, p->var, fail_target_act);
        else if (cur_eqv && cur_eqv[p->var]) fprintf(o, "        if (!rc_term_equal(%s, v%d)) %s\n", e, p->var, fail_target_act);
        else if (var_used[p->var]) fprintf(o, "        T *v%d = %s;\n", p->var, e);
        return;
    }
    if (!irrefutable(p))
        fprintf(o, "        if (RC_TAG_%s(%s) != %d) %s\n", sort_rep(p->sort), e, p->ctor, fail_target_act);
    for (int i = 0; i < p->n; i++) {
        if (!pat_has_used_var(p->k[i]) && irrefutable(p->k[i]) && !pat_tests_bound(p->k[i], cur_eqv)) continue;
        int t = tmpno++;
        fprintf(o, "        T *m%d = RC_KID_%s(%s, %d);\n", t, sort_rep(p->sort), e, i);
        char sub[64];
        snprintf(sub, sizeof sub, "m%d", t);
        emit_tgt_match(o, p->k[i], sub, judgment);
    }
}

const char *target_label(int child, char *buf, size_t cap) {
    Node *x = &nodes[child];
    if (x->kind == N_LEAF && x->ncand > 1) snprintf(buf, cap, "l%d", child);
    else if (x->kind == N_LEAF) snprintf(buf, cap, "r%d", x->rule);
    else if (x->kind == N_FAIL) snprintf(buf, cap, "f");
    else snprintf(buf, cap, "n%d", child);
    return buf;
}

void emit_params(FILE *o, int arity) {
    fputc('(', o);
    if (!arity) fputs("void", o);
    for (int a = 0; a < arity; a++) fprintf(o, "%sT *a%d", a ? ", " : "", a);
    fputc(')', o);
}

/* the path of variable v in a left side */
static bool var_path(const Pat *p, int v, int *path, int *len) {
    if (p->var == v) return true;
    for (int i = 0; i < p->n; i++) {
        path[(*len)++] = i;
        if (var_path(p->k[i], v, path, len)) return true;
        (*len)--;
    }
    return false;
}
/* two premise sources that are the same call on every input of a leaf: the same constructors, and
 * variables bound at the same paths of the arguments */
static bool src_same(const Pat *x, const Rule *rx, const Pat *y, const Rule *ry) {
    if ((x->var >= 0) != (y->var >= 0)) return false;
    if (x->var >= 0) {
        int px[256], py[256], lx = 0, ly = 0;
        if (!var_path(rx->lhs, x->var, px, &lx) || !var_path(ry->lhs, y->var, py, &ly)) return false;
        return lx == ly && !memcmp(px, py, (size_t)lx * sizeof(int));
    }
    if (x->ctor != y->ctor || x->n != y->n) return false;
    for (int i = 0; i < x->n; i++) if (!src_same(x->k[i], rx, y->k[i], ry)) return false;
    return true;
}

static int relation_id(const char *name);
/* the code of rule r: bind the left side's variables, run the premises in order, build the right
 * side.  shared: premise 0's value is s_ (computed once for the leaf); trial: the rule is a
 * candidate of a trial leaf (its success ends the trial; RC_RULE's event was reserved at the
 * trial's start) */
/* Tail recursion modulo a wrapper.  A rule alone at its leaf whose last two steps are t := J(...)
 * (J in the rule's own recursion, compiled from rules) and the tail call W(..., t, ...), t a variable
 * used only there as a whole argument, pushes the frame (W, t's position, W's other arguments) and
 * tail-calls J instead (rc_rt.h: pending wrappers).  Every call that is not a tail call then applies
 * the frames its callee left (rc_pend_unwind), and so does the host's entry. */
static int *trmc_w, *trmc_k;
static bool *may_pend;   /* per rule: the wrapper and t's position, or -1 */
int n_trmc = -1;
int pat_count_var(const Pat *p, int v) {
    if (p->var >= 0) return p->var == v;
    int n = 0;
    for (int i = 0; i < p->n; i++) n += pat_count_var(p->k[i], v);
    return n;
}
void number_trmc(void) {
    if (n_trmc >= 0) return;
    trmc_w = xmalloc((size_t)(nrules ? nrules : 1) * sizeof(int));
    trmc_k = xmalloc((size_t)(nrules ? nrules : 1) * sizeof(int));
    for (int i = 0; i < nrules; i++) trmc_w[i] = trmc_k[i] = -1;
    n_trmc = 0;
    bool *seen = xcalloc((size_t)(nnodes ? nnodes : 1), 1);
    int *order = xmalloc((size_t)(nnodes ? nnodes : 1) * sizeof(int));
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined || native_op_of(c) >= 0) continue;
        int no = 0;
        memset(seen, 0, (size_t)nnodes);
        dag_reach(ctors[c].dag_root, seen, order, &no);
        for (int i = 0; i < no; i++) {
            Node *x = &nodes[order[i]];
            if (x->kind != N_LEAF || x->ncand != 1) continue;
            const Rule *r = &rules[x->rule];
            if (!r->tail || r->np < 2) continue;
            const Prem *pj = &r->p[r->np - 2], *pw = &r->p[r->np - 1];
            if (pj->relation || pw->relation) continue;
            int t = pj->tgt->var, J = pj->src->ctor, W = pw->src->ctor;
            if (t < 0 || (pj->eqv && pj->eqv[t])) continue;
            /* a call into the safepoint (which must be one), or the recursion of the safepoint's own
             * loop: the loop over the records needs no C stack and keeps no record on it.  Elsewhere
             * recursion stays on the C stack (a pushed frame and its later application cost more
             * than the call they replace) */
            bool loop = safepoint_ctor >= 0 && ctors[c].scc == ctors[safepoint_ctor].scc && ctors[J].scc == ctors[c].scc &&
                        native_op_of(J) < 0;
            if (!(pend_required && pend_required[r->id]) && !loop) continue;
            if (W == safepoint_ctor || ctors[W].arity > 8) continue;
            int k = -1;
            for (int a = 0; a < pw->src->n; a++) if (pw->src->k[a]->var == t) k = a;
            if (k < 0 || pat_count_var(pw->src, t) != 1) continue;
            trmc_w[r->id] = W;
            trmc_k[r->id] = k;
            n_trmc++;
        }
    }
    free(seen);
    free(order);
    for (int i = 0; pend_required && i < nrules; i++)
        if (pend_required[i] && trmc_w[i] < 0)
            die("rule %s: its call into the safepoint cannot be made a pending wrapper (the rule shares its leaf)", rules[i].label);
    /* the judgments whose calls may return with frames pending: a rule that pushes one, or a tail call
     * into such a judgment (a fixed point) */
    may_pend = xcalloc((size_t)(nctors ? nctors : 1), sizeof(bool));
    for (bool changed = n_trmc > 0; changed;) {
        changed = false;
        for (int i = 0; i < nrules; i++) {
            const Rule *r = &rules[i];
            if (may_pend[r->head]) continue;
            bool leaves = trmc_w[r->id] >= 0;
            if (!leaves && r->tail && r->np > 0 && !r->p[r->np - 1].relation) leaves = may_pend[r->p[r->np - 1].src->ctor];
            if (leaves) { may_pend[r->head] = true; changed = true; }
        }
    }
}
/* the wrapper of a call that is not a tail call: RC_CLOSED when the callee may leave frames */
static const char *closed(int callee) { return may_pend && may_pend[callee] ? "RC_CLOSED" : ""; }
/* a call that is not a tail call: the frames its callee leaves are applied to its result
 * (RC_CLOSED, defined in the generated code), when it may leave any */
static void emit_call_open(FILE *o, int callee) { fprintf(o, "%s(", closed(callee)); }
static void emit_call_close(FILE *o) { fputc(')', o); }

static void emit_rule_code(FILE *o, int c, const Rule *r, bool shared, bool trial, bool may_tail) {
    Ctor *k = &ctors[c];
    var_used = xcalloc((size_t)(r->nv ? r->nv : 1), 1);
    bool tail = r->tail && may_tail;
    for (int j = 0; j < r->np; j++) {
        if (r->p[j].relation) { for (int a = 0; a < r->p[j].nrel; a++) mark_used_build(r, j, r->p[j].rel[a]); continue; }
        if (!(shared && j == 0)) mark_used_build(r, j, r->p[j].src);
        for (int v = 0; r->p[j].eqv && v < r->nv; v++) if (r->p[j].eqv[v]) var_used[v] = true;   /* compared */
    }
    if (!tail) mark_used_build(r, r->np, r->rhs);
    occ_rule = r;
    for (int a = 0; a < k->arity; a++) {
        char arg[16];
        snprintf(arg, sizeof arg, "a%d", a);
        emit_lhs_bind(o, r->lhs->k[a], arg);
    }
    if (!trial) fprintf(o, "        RC_RULE(%d);\n", r->id);
    tmpno = 0;
    for (int j = 0; j < r->np; j++) {
        Prem *p = &r->p[j];
        occ_upto = j;
        if (p->relation) {
            /* a query of a relation whose rows the host supplies; every argument is bound (a test) */
            if (p->binds) die("rule %s: a relation query that binds a variable is not compiled", r->label);
            fprintf(o, "        { T *q_[] = {");
            for (int a = 0; a < p->nrel; a++) { if (a) fputs(", ", o); emit_build(o, p->rel[a]); }
            fprintf(o, "%s}; if (!rc_relation_holds(%d, %d, q_)) %s }\n", p->nrel ? "" : "0", relation_id(p->relation), p->nrel, fail_row_act);
            continue;
        }
        int callee = p->src->ctor;
        bool last_tail = j == r->np - 1 && tail && !(shared && j == 0);
        /* a guaranteed tail call needs no more stack argument slots than the caller has (x86-64:
         * six arguments in registers); it is needed only where the call may recur (the callee in the
         * caller's component of the call graph) or enters or leaves the safepoint, and elsewhere the C
         * compiler may inline the callee */
        int caller_slots = k->arity > 6 ? k->arity - 6 : 0, callee_slots = ctors[callee].arity > 6 ? ctors[callee].arity - 6 : 0;
        bool recurs = ctors[callee].scc == k->scc || c == safepoint_ctor || callee == safepoint_ctor;
        bool must = !no_musttail && callee_slots <= caller_slots && recurs;
        if (last_tail && !must && (c == safepoint_ctor || callee == safepoint_ctor))
            die("rule %s: the tail call into the safepoint cycle cannot be guaranteed", r->label);
        if (last_tail && trial) fprintf(o, "        if (rc_trace_on) rc_trial_end(tm_, %d);\n        RC_COUNT_RULE(%d);\n", r->id, r->id);
        if (shared && j == 0) fprintf(o, "        T *t0 = s_; (void)t0;\n");
        else if (tail && !trial && trmc_w && trmc_w[r->id] >= 0 && j == r->np - 2) {
            /* the wrapper's frame pushed (its other arguments built now: data only), then the tail call */
            const Prem *pw = &r->p[r->np - 1];
            int W = trmc_w[r->id], kk = trmc_k[r->id];
            occ_upto = r->np - 1;
            fputs("        { T *w_[] = {", o);
            for (int a = 0; a < pw->src->n; a++) {
                if (a) fputs(", ", o);
                if (a == kk) fputs("(T *)0", o); else emit_build(o, pw->src->k[a]);
            }
            fprintf(o, "}; rc_pend_push(%d, %d, %d, w_); }\n", W, kk, pw->src->n);
            fprintf(o, "        %sreturn p%d(", must ? "RC_TAIL " : "", callee);
            for (int a = 0; a < p->src->n; a++) { if (a) fputs(", ", o); emit_build(o, p->src->k[a]); }
            fputs(");\n", o);
            break;
        } else {
            if (last_tail) fprintf(o, "        %sreturn p%d(", must ? "RC_TAIL " : "", callee);
            else { fprintf(o, "        T *t%d = ", j); emit_call_open(o, callee); fprintf(o, "p%d(", callee); }
            for (int a = 0; a < p->src->n; a++) { if (a) fputs(", ", o); emit_build(o, p->src->k[a]); }
            fputs(")", o);
            if (!last_tail) emit_call_close(o);
            fputs(";\n", o);
            if (last_tail) break;
            fprintf(o, "        if (!t%d) %s\n", j, fail_call_act);
        }
        char tv[16];
        snprintf(tv, sizeof tv, "t%d", j);
        cur_eqv = p->eqv;
        emit_tgt_match(o, p->tgt, tv, c);
        cur_eqv = NULL;
    }
    if (!tail || (shared && r->np == 1)) {
        if (trial) fprintf(o, "        if (rc_trace_on) rc_trial_end(tm_, %d);\n        RC_COUNT_RULE(%d);\n", r->id, r->id);
        fputs("        return ", o);
        occ_upto = r->np;
        if (tail) fputs("t0", o); else emit_build(o, r->rhs);
        fputs(";\n", o);
    }
    occ_rule = NULL;
    free(var_used);
    var_used = NULL;
}

/* a leaf with several candidates: the rules not excluded on its path, pairwise exclusive (check.c),
 * so at most one applies to any input and the order of the trials does not matter.  A first premise
 * that is the same call in every candidate is made once.  With a trace, the rule event is reserved
 * at the start and filled with the rule that applied; a failed candidate's events are dropped (the
 * trace stays a derivation) */
static void emit_trial_leaf(FILE *o, int c, int leaf) {
    Node *x = &nodes[leaf];
    const Rule *r0 = &rules[x->cand[0]];
    bool shared = r0->np > 0 && !r0->p[0].relation;
    for (int i = 1; i < x->ncand && shared; i++) {
        const Rule *ri = &rules[x->cand[i]];
        shared = ri->np > 0 && !ri->p[0].relation && ri->p[0].src->ctor == r0->p[0].src->ctor && src_same(r0->p[0].src, r0, ri->p[0].src, ri);
    }
    fprintf(o, "l%d: {\n        size_t tm_ = 0, ts_ = 0; (void)ts_;\n        if (rc_trace_on) tm_ = rc_trial_begin();\n", leaf);
    if (shared) {
        const Prem *p = &r0->p[0];
        var_used = xcalloc((size_t)(r0->nv ? r0->nv : 1), 1);
        mark_used(p->src);
        for (int a = 0; a < ctors[c].arity; a++) { char arg[16]; snprintf(arg, sizeof arg, "a%d", a); emit_lhs_bind(o, r0->lhs->k[a], arg); }
        fputs("        T *s_ = ", o);
        emit_call_open(o, p->src->ctor);
        fprintf(o, "p%d(", p->src->ctor);
        for (int a = 0; a < p->src->n; a++) { if (a) fputs(", ", o); emit_build(o, p->src->k[a]); }
        fputs(")", o);
        emit_call_close(o);
        fprintf(o, ";\n        if (!s_) { if (rc_trace_on) rc_trial_end(tm_, %d); return (T *)0; }\n", r0->id);
        free(var_used); var_used = NULL;
    }
    fputs("        if (rc_trace_on) ts_ = rc_trace_len;\n", o);
    for (int i = 0; i < x->ncand; i++) {
        const Rule *r = &rules[x->cand[i]];
        bool last = i == x->ncand - 1;
        char next[32], end[96];
        snprintf(next, sizeof next, "c%d_%d", leaf, i + 1);
        snprintf(end, sizeof end, "if (rc_trace_on) rc_trial_end(tm_, %d);", r->id);
        set_fail_actions(c, last ? NULL : next, end);
        if (i == 0) fprintf(o, "    { /* %s */\n", r->label);
        else fprintf(o, "    c%d_%d: { /* %s */\n", leaf, i, r->label);
        if (i > 0) fputs("        if (rc_trace_on) rc_trace_len = ts_;\n", o);
        emit_rule_code(o, c, r, shared, true, last);
        fputs("    }\n", o);
    }
    fputs("    }\n", o);
}

void emit_rules_body(FILE *o, int c) {
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
    /* rules alone at a leaf */
    bool *alone = xcalloc((size_t)(nrules ? nrules : 1), 1);
    for (int i = 0; i < no; i++) if (nodes[order[i]].kind == N_LEAF && nodes[order[i]].ncand == 1) alone[nodes[order[i]].rule] = true;
    for (int i = 0; i < k->nrules; i++) {
        Rule *r = &rules[k->rules[i]];
        if (!alone[r->id]) continue;
        set_fail_actions(c, NULL, NULL);
        fprintf(o, "r%d: { /* %s */\n", r->id, r->label);
        emit_rule_code(o, c, r, false, false, true);
        fputs("    }\n", o);
    }
    for (int i = 0; i < no; i++) if (nodes[order[i]].kind == N_LEAF && nodes[order[i]].ncand > 1) emit_trial_leaf(o, c, order[i]);
    if (fail_used) fprintf(o, "f:  return rc_no_rule(%d);\n", c);
    free(alone); free(seen); free(order);
}

/* relations the module's rules query, in order of first use */
static char *relation_names[64]; static int nrelation_names;
static int relation_id(const char *name) {
    for (int i = 0; i < nrelation_names; i++) if (!strcmp(relation_names[i], name)) return i;
    if (nrelation_names == 64) die("more than 64 relations");
    relation_names[nrelation_names] = xstrdup(name);
    return nrelation_names++;
}
void emit_relation_table(FILE *o) {
    fprintf(o, "\n/* relations the rules query (rows supplied by the host through rc_relation_hook) */\n"
               "const int rc_nrelations = %d;\nconst char *const rc_relation_names[] = {", nrelation_names);
    for (int i = 0; i < nrelation_names; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", relation_names[i]);
    fprintf(o, "%s};\n", nrelation_names ? "" : "0");
}

/* a law of judgment c: equal arguments give the law's constant; others go to p<c><next> (its rules,
 * or its memo); check mode runs the rules on equal arguments too and compares */
static void emit_law_wrapper(FILE *o, int c, int law, const char *next) {
    int res = laws[law].result;
    fprintf(o, "static T *p%d(T *a0, T *a1) {\n"
               "    if (a0 == a1) {\n"
               "        if (rc_trace_on) rc_trace_push((uint16_t)(RC_EV_LAW + %d));\n"
               "#ifdef RC_CHECK_NATIVE\n"
               "        if (rc_check_native) { int tr_ = rc_trace_on; rc_trace_on = 0; rc_check_depth++; T *s_ = %s(p%d_rules(a0, a1)); rc_check_depth--; rc_trace_on = tr_; rc_law_checked(%d, RC_NUL_%s(%d), s_); }\n"
               "#endif\n"
               "        return RC_NUL_%s(%d);\n"
               "    }\n"
               "    %sreturn p%d%s(a0, a1);\n}\n", c, law, closed(c), c, law, sort_rep(ctors[res].sort), res, sort_rep(ctors[res].sort), res,
            no_musttail ? "" : "RC_TAIL ", c, next);
}

void emit_proc(FILE *o, int c) {
    Ctor *k = &ctors[c];
    int nop = native_op_of(c);
    fprintf(o, "\n/* %s: %d rule%s, %s%s */\n", k->name, k->nrules, k->nrules == 1 ? "" : "s", k->det ? "det" : "semidet",
            nop >= 0 ? "; native operation" : "");
    if (nop >= 0) {
        /* the rules stay compiled: check mode runs them and compares */
        fprintf(o, "#ifdef RC_CHECK_NATIVE\nstatic T *p%d_rules", c); emit_params(o, k->arity); fputs(" {\n", o);
        emit_rules_body(o, c);
        fputs("}\n#endif\n", o);
        fprintf(o, "static RC_BLOCK_INLINE T *p%d", c); emit_params(o, k->arity); fputs(" {\n", o);
        fprintf(o, "    if (rc_trace_on) rc_trace_push((uint16_t)(RC_EV_NATIVE + %d));\n", nop);
        /* check mode: the rules first, since an implementation may update a native representation
         * in place and the rules must see the inputs as they were; then the implementation; the two
         * results are compared as module data (a representation that has a read-back gives it) */
        fprintf(o, "#ifdef RC_CHECK_NATIVE\n    if (rc_check_native) {\n        int tr_ = rc_trace_on; rc_trace_on = 0;\n        rc_check_depth++;\n        T *s_ = ");
        emit_call_open(o, c);
        fprintf(o, "p%d_rules(", c);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fputs(")", o);
        emit_call_close(o);
        fprintf(o, ";\n        rc_check_depth--;\n        rc_trace_on = tr_;\n        T *n_ = %s(", native_ops[nop].fn);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fputs(");\n", o);
        if (sorts[k->sort].native == RC_REP_TABLE) {
            const char *rr = sort_rep(k->sort);
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
    if (law >= 0 && !k->memo) {
        fprintf(o, "static T *p%d_rules", c); emit_params(o, k->arity); fputs(" {\n", o);
        emit_rules_body(o, c);
        fputs("}\n", o);
        emit_law_wrapper(o, c, law, "_rules");
        return;
    }
    if (k->memo && k->memo_slot >= 0) {
        /* the result kept in a slot of the argument's node (rc_intern.h) */
        int mi = 0;
        while (memo_rows[mi].ctor != c) mi++;
        fprintf(o, "static T *p%d_rules(T *a0) {\n", c);
        emit_rules_body(o, c);
        fputs("}\n", o);
        fprintf(o, "static T *p%d(T *a0) {\n    T **s_ = rc_intern_slot(a0, %d), *r_ = *s_;\n    if (r_) {\n"
                   "        if (r_ == RC_SLOT_NONE) r_ = NULL;\n"
                   "        if (rc_trace_on) { rc_trace_push((uint16_t)(RC_EV_MEMO + %d)); if (!r_) rc_trace_push(RC_EV_NO_RULE); }\n"
                   "#ifdef RC_CHECK_NATIVE\n        if (rc_check_native) { int tr_ = rc_trace_on; rc_trace_on = 0; rc_check_depth++; T *t_ = %s(p%d_rules(a0)); rc_check_depth--; rc_trace_on = tr_; rc_memo_checked(%d, r_, t_); }\n#endif\n"
                   "        return r_;\n    }\n    r_ = %s(p%d_rules(a0));\n    *rc_intern_slot(a0, %d) = r_ ? r_ : RC_SLOT_NONE;\n    return r_;\n}\n",
                c, k->memo_slot, c, closed(c), c, mi, closed(c), c, k->memo_slot);
        return;
    }
    if (k->memo) {
        /* results kept per arguments (rc_memo.h); a hit is one trace event; check mode compares a hit
         * with the rules' result on the same arguments */
        int mi = 0;
        while (memo_rows[mi].ctor != c) mi++;
        fprintf(o, "static T *p%d_rules", c); emit_params(o, k->arity); fputs(" {\n", o);
        emit_rules_body(o, c);
        fputs("}\n", o);
        /* a direct-mapped front cache of the tables (rc_memo.h): entries of the current generation */
        fprintf(o, "static struct { T *k[%d]; T *r; uint64_t gen; } p%d_front[RC_MEMO_FRONT];\n", k->arity, c);
        fprintf(o, "static T *p%d%s", c, law >= 0 ? "_memo" : ""); emit_params(o, k->arity); fputs(" {\n    T *k_[] = {", o);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fprintf(o, "}, *r_;\n    size_t f_ = rc_memo_front_slot(%d, k_);\n    if (p%d_front[f_].gen == rc_memo_gen", k->arity, c);
        for (int a = 0; a < k->arity; a++) fprintf(o, " && p%d_front[f_].k[%d] == a%d", c, a, a);
        fprintf(o, ") r_ = p%d_front[f_].r;\n    else if (rc_memo_get(%d, %d, k_, &r_)) {\n        p%d_front[f_].gen = rc_memo_gen;", c, c, k->arity, c);
        for (int a = 0; a < k->arity; a++) fprintf(o, " p%d_front[f_].k[%d] = a%d;", c, a, a);
        fprintf(o, " p%d_front[f_].r = r_;\n    } else {\n        r_ = %s(p%d_rules(", c, closed(c), c);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fprintf(o, "));\n        rc_memo_put(%d, %d, k_, r_);\n        p%d_front[f_].gen = rc_memo_gen;", c, k->arity, c);
        for (int a = 0; a < k->arity; a++) fprintf(o, " p%d_front[f_].k[%d] = a%d;", c, a, a);
        fprintf(o, " p%d_front[f_].r = r_;\n        return r_;\n    }\n", c);
        fprintf(o, "    if (rc_trace_on) { rc_trace_push((uint16_t)(RC_EV_MEMO + %d)); if (!r_) rc_trace_push(RC_EV_NO_RULE); }\n", c);
        fprintf(o, "#ifdef RC_CHECK_NATIVE\n    if (rc_check_native) { int tr_ = rc_trace_on; rc_trace_on = 0; rc_check_depth++; T *s_ = %s(p%d_rules(", closed(c), c);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%sa%d", a ? ", " : "", a);
        fprintf(o, ")); rc_check_depth--; rc_trace_on = tr_; rc_memo_checked(%d, r_, s_); }\n#endif\n    return r_;\n}\n", mi);
        /* a law answers equal arguments before the tables are consulted */
        if (law >= 0) emit_law_wrapper(o, c, law, "_memo");
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
                   "    if (!p%d_done) { int tr_ = rc_trace_on; rc_trace_on = 0; p%d_value = rc_make_static(%s(p%d_rules())); rc_trace_on = tr_; p%d_done = 1; }\n"
                   "    if (rc_trace_on) { rc_trace_push((uint16_t)(RC_EV_CONST + %d)); if (!p%d_value) rc_trace_push(RC_EV_NO_RULE); }\n"
                   "    return p%d_value;\n}\n", c, c, c, closed(c), c, c, c, c, c);
        return;
    }
    fprintf(o, "static T *p%d", c); emit_params(o, k->arity); fputs(" {\n", o);
    if (c == safepoint_ctor) {
        for (int a = 0; a < k->arity; a++) fprintf(o, "    rc_roots[%d] = a%d;\n", a, a);
        fprintf(o, "    bool stop_ = rc_safepoint(%d);\n", k->arity);
        for (int a = 0; a < k->arity; a++) fprintf(o, "    a%d = rc_roots[%d];\n", a, a);
        fputs("    if (stop_) return RC_STOPPED;   /* the host resumes from rc_roots (rc_resume) */\n", o);
    }
    emit_rules_body(o, c);
    fputs("}\n", o);
}

void c_ident(char *out, size_t cap, const char *name) {
    size_t i = 0;
    for (const char *p = name; *p && i + 2 < cap; p++) out[i++] = isalnum((unsigned char)*p) ? *p : '_';
    out[i] = 0;
}

void emit_views(FILE *o) {
    /* the generic components (their headers state the contracts) */
    fprintf(o, "#include \"%src_intern.h\"\n#include \"%src_bytes.h\"\n#include \"%src_nat.h\"\n#include \"%src_order.h\"\n"
               "#include \"%src_memo.h\"\n#include \"%src_set.h\"\n#include \"%src_table.h\"\n",
            component_prefix, component_prefix, component_prefix, component_prefix, component_prefix, component_prefix, component_prefix);
    bool any_bn = false, any_sl = false;
    for (int s = 0; s < nsorts; s++) {
        if (sorts[s].native == RC_REP_BINARY) any_bn = true;
        if (sorts[s].native == RC_REP_SLICES) any_sl = any_bn = true;
    }
    if (any_bn) fprintf(o, "#include \"%src_bignat.h\"\n", component_prefix);
    if (any_sl) fprintf(o, "#include \"%src_slices.h\"\n", component_prefix);
    for (int s = 0; s < nsorts; s++) {
        int rep = sorts[s].native;
        if (rep == RC_REP_RECORDS) {
            /* the host's records: rc_record_count of them now; rc_record_more(i) asks the host for record
             * i (it may read on) and says whether it exists */
            fprintf(o, "/* %s: the record cursor; an immediate index into the host's records, viewed as %s / %s */\n",
                    sorts[s].name, ctors[rep_ctor[rep][0]].name, ctors[rep_ctor[rep][1]].name);
            fprintf(o, "extern uint64_t rc_record_count;\nbool rc_record_more(uint64_t i);\nT *rc_record_term(uint64_t i);\n");
            fprintf(o, "static inline uint16_t RC_TAG_records(T *x) {\n"
                       "    uint64_t i = RC_IMM_VAL(x);\n"
                       "    return i < rc_record_count || rc_record_more(i) ? %d : %d;\n}\n",
                    rep_ctor[rep][1], rep_ctor[rep][0]);
            fprintf(o, "static inline T *RC_KID_records(T *x, int i) { uint64_t v = RC_IMM_VAL(x); return i == 0 ? rc_record_term(v) : RC_IMM(v + 1); }\n");
        } else if (rep == RC_REP_PROMISE) {
            fprintf(o, "/* %s: symbolic terms or the host's promises of them (rc_rt.h: RC_TAG_promise, rc_promise_hook) */\n", sorts[s].name);
        } else if (rep == RC_REP_NAT) {
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
        } else if (rep == RC_REP_WORD) {
            const char *nm = sorts[s].repname;
            int wc = sorts[s].rp[0], b0 = sorts[s].rp[1], b1 = sorts[s].rp[2], w = sorts[s].width;
            fprintf(o, "/* %s: words of %d bits as immediates, viewed as %s with the most significant bit first (%s 0, %s 1) */\n",
                    sorts[s].name, w, ctors[wc].name, ctors[b0].name, ctors[b1].name);
            fprintf(o, "static inline uint16_t RC_TAG_%s(T *x) { (void)x; return %d; }\n", nm, wc);
            fprintf(o, "static inline T *RC_KID_%s(T *x, int i) { return (RC_IMM_VAL(x) >> (%d - i)) & 1u ? RC_NUL_symbolic(%d) : RC_NUL_symbolic(%d); }\n",
                    nm, w - 1, b1, b0);
            /* a bit is one of the two static nullary terms: compared as a pointer (no load) */
            fprintf(o, "static inline uint64_t RC_BIT_%s(T *b) { return b == RC_NUL_symbolic(%d); }\n", nm, b1);
            fprintf(o, "static inline T *RC_MKV_%s(uint16_t c, uint32_t n, T *const *k) {\n"
                       "    (void)c;\n    uint64_t v = 0;\n    for (uint32_t i = 0; i < n; i++) v = (v << 1) | RC_BIT_%s(k[i]);\n    return RC_IMM(v);\n}\n", nm, nm);
            if (w <= 8) {
                fprintf(o, "static inline T *RC_MK%d_%s(uint16_t c", w, nm);
                for (int i = 0; i < w; i++) fprintf(o, ", T *k%d", i);
                fputs(") {\n    T *k[] = {", o);
                for (int i = 0; i < w; i++) fprintf(o, "%sk%d", i ? ", " : "", i);
                fprintf(o, "};\n    return RC_MKV_%s(c, %d, k);\n}\n", nm, w);
            }
        } else if (rep == RC_REP_BINARY) {
            const char *nm = sorts[s].repname;
            int z = sorts[s].rp[0], nb = sorts[s].rp[1], b0 = sorts[s].rp[2], b1 = sorts[s].rp[3];
            fprintf(o, "/* %s: natural numbers of any size (rc_bignat.h), viewed as %s / %s (n mod 2) (n div 2) (%s 0, %s 1) */\n",
                    sorts[s].name, ctors[z].name, ctors[nb].name, ctors[b0].name, ctors[b1].name);
            fprintf(o, "static inline uint16_t RC_TAG_%s(T *x) { return rc_bn_is_zero(x) ? %d : %d; }\n", nm, z, nb);
            fprintf(o, "static inline T *RC_KID_%s(T *x, int i) { return i == 0 ? (rc_bn_low_bit(x) ? RC_NUL_symbolic(%d) : RC_NUL_symbolic(%d)) : rc_bn_shr1(x); }\n",
                    nm, b1, b0);
            fprintf(o, "#define RC_NUL_%s(c) RC_IMM(0)\n", nm);
            fprintf(o, "static inline T *RC_MK2_%s(uint16_t c, T *b, T *n) {\n"
                       "    (void)c;\n    unsigned bit = b == RC_NUL_symbolic(%d);\n"
                       "    if (!bit && rc_bn_is_zero(n)) rc_refinement_fail(\"%s %s %s is not a canonical numeral\");\n"
                       "    return rc_bn_shl1_or(n, bit);\n}\n", nm, b1, ctors[nb].name, ctors[b0].name, ctors[z].name);
        } else if (rep == RC_REP_SLICES) {
            const char *nm = sorts[s].repname;
            int nl = sorts[s].rp[0], cs = sorts[s].rp[1];
            fprintf(o, "/* %s: byte strings as slices of the run's input or interned strings (rc_slices.h), viewed as %s / %s byte rest */\n",
                    sorts[s].name, ctors[nl].name, ctors[cs].name);
            fprintf(o, "static inline uint16_t RC_TAG_%s(T *x) { return rc_sl_empty(x) ? %d : %d; }\n", nm, nl, cs);
            fprintf(o, "static inline T *RC_KID_%s(T *x, int i) { return i == 0 ? RC_IMM(rc_sl_first(x)) : rc_sl_rest(x); }\n", nm);
            fprintf(o, "#define RC_NUL_%s(c) RC_IMM(0)\n", nm);
            fprintf(o, "static inline T *RC_MK2_%s(uint16_t c, T *b, T *s) { (void)c; return rc_sl_cons((unsigned)RC_IMM_VAL(b), s); }\n", nm);
        } else if (rep == RC_REP_UNARY) {
            int z = rep_ctor[rep][0], su = rep_ctor[rep][1];
            fprintf(o, "/* %s: native naturals (immediates), viewed as %s / %s n (n + 1) */\n", sorts[s].name, ctors[z].name, ctors[su].name);
            fprintf(o, "static inline uint16_t RC_TAG_unary(T *x) { return RC_IMM_VAL(x) == 0 ? %d : %d; }\n", z, su);
            fprintf(o, "static inline T *RC_KID_unary(T *x, int i) { (void)i; return RC_IMM(RC_IMM_VAL(x) - 1); }\n");
            fprintf(o, "#define RC_NUL_unary(c) RC_IMM(0)\n");
            fprintf(o, "static inline T *RC_MK1_unary(uint16_t c, T *m) {\n"
                       "    (void)c;\n"
                       "    uint64_t v = RC_IMM_VAL(m);\n"
                       "    if (v >= (UINT64_MAX >> 3)) rc_refinement_fail(\"natural too large for an immediate\");\n"
                       "    return RC_IMM(v + 1);\n}\n");
        }
    }
}

void emit_c(const char *path, const char *hpath, const char *wire_path) {
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
    if (nnative_ops || nlaws || nmemo_rows)
        fputs("#ifdef RC_CHECK_NATIVE\nextern int rc_check_native, rc_check_depth;\nvoid rc_native_checked(int op, T *native_result, T *rule_result);\n"
              "void rc_law_checked(int law, T *law_result, T *rule_result);\nvoid rc_memo_checked(int memo, T *kept_result, T *rule_result);\n#endif\n", o);
    if (nnative_ops) {
        for (int i = 0; i < nnative_ops; i++) {
            Ctor *k = &ctors[native_ops[i].ctor];
            fprintf(o, "%sT *%s", native_ops[i].comp ? "static " : "", native_ops[i].fn); emit_params(o, k->arity); fputs(";\n", o);
        }
    }
    emit_views(o);
    fputc('\n', o);
    number_ground();
    if (nground) fprintf(o, "/* ground terms of the rules, kept per native store */\nstatic T *rc_ground[%d];\n", nground);
    number_trmc();
    if (n_trmc)
        fprintf(o, "/* %d rules push pending wrappers: a call that is not a tail call applies what its callee left */\n"
                   "#define RC_CLOSED(e) ({ size_t rc_m__ = rc_pend_depth; T *rc_v__ = (e); rc_pend_unwind(rc_m__, rc_v__); })\n", n_trmc);
    else fputs("#define RC_CLOSED(e) (e)\n", o);
    /* a judgment answered by a block is a call of the component's glue: inlined where it is called
     * (in check mode it also runs the rules, and stays a call) */
    if (nnative_ops)
        fputs("#ifdef RC_CHECK_NATIVE\n#define RC_BLOCK_INLINE\n#else\n#define RC_BLOCK_INLINE inline __attribute__((always_inline))\n#endif\n", o);
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined) continue;
        fprintf(o, "static %sT *p%d", native_op_of(c) >= 0 ? "RC_BLOCK_INLINE " : "", c); emit_params(o, ctors[c].arity); fputs(";\n", o);
    }
    emit_component_glue(o);
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) emit_proc(o, c);
    /* tables */
    emit_relation_table(o);
    fputs("const uint8_t rc_sort_order[] = {", o);
    for (int s = 0; s < nsorts; s++) {
        int r = sorts[s].native;
        bool nullary_only = r == RC_REP_SYMBOLIC;
        for (int c = 0; c < nctors && nullary_only; c++) if (!ctors[c].defined && ctors[c].sort == s && ctors[c].arity) nullary_only = false;
        fprintf(o, "%s%d", s ? "," : "", r == RC_REP_INTERN ? 1 : r == RC_REP_BYTES ? 2 : (REP_IMMNAT(r) || r == RC_REP_WORD) ? 3 : nullary_only ? 4 : 0);
    }
    fputs("};\nconst uint8_t rc_ctor_slots[] = {", o);
    for (int c = 0; c < nctors; c++)
        fprintf(o, "%s%d", c ? "," : "", (!ctors[c].defined && sorts_memo_slots && sorts[ctors[c].sort].native == RC_REP_INTERN) ? sorts_memo_slots[ctors[c].sort] : 0);
    fputs("};\nconst uint16_t rc_ctor_rank[] = {", o);
    {
        int *rank = xcalloc((size_t)nsorts, sizeof(int));
        for (int c = 0; c < nctors; c++) fprintf(o, "%s%d", c ? "," : "", ctors[c].defined ? 0 : rank[ctors[c].sort]++);
        free(rank);
    }
    fputs("};\n", o);
    fprintf(o, "const int rc_nmemo = %d;\nconst char *const rc_memo_name[] = {", nmemo_rows);
    for (int i = 0; i < nmemo_rows; i++) fprintf(o, "%s\"%s\"", i ? ", " : "", ctors[memo_rows[i].ctor].name);
    fputs(nmemo_rows ? "};\n" : "\"\"};\n", o);
    /* a host's access to module data in the representation each sort was given: build a value of a
     * data constructor, read a value's constructor and children */
    fputs("T *rc_construct(int ctor, T *const *k) {\n    (void)k;\n    switch (ctor) {\n", o);
    for (int c = 0; c < nctors; c++) {
        const Ctor *d = &ctors[c];
        int rep = sorts[d->sort].native;
        if (d->defined || rep == RC_REP_RECORDS) continue;
        if (d->arity == 0) { fprintf(o, "    case %d: return RC_NUL_%s(%d);\n", c, sort_rep(d->sort), c); continue; }
        if (d->arity > 8) { fprintf(o, "    case %d: return RC_MKV_%s(%d, %d, k);\n", c, sort_rep(d->sort), c, d->arity); continue; }
        fprintf(o, "    case %d: return RC_MK%d_%s(%d", c, d->arity, sort_rep(d->sort), c);
        for (int a = 0; a < d->arity; a++) fprintf(o, ", k[%d]", a);
        fputs(");\n", o);
    }
    fputs("    default: return (T *)0;\n    }\n}\n", o);
    fputs("int rc_view(int sort, T *x, T **kids) {\n    int c;\n    switch (sort) {\n", o);
    for (int s = 0; s < nsorts; s++) {
        fprintf(o, "    case %d: c = RC_TAG_%s(x);\n        switch (c) {\n", s, sort_rep(s));
        for (int c = 0; c < nctors; c++) {
            if (ctors[c].defined || ctors[c].sort != s || !ctors[c].arity) continue;
            fprintf(o, "        case %d:", c);
            for (int a = 0; a < ctors[c].arity; a++) fprintf(o, " kids[%d] = RC_KID_%s(x, %d);", a, sort_rep(s), a);
            fputs(" break;\n", o);
        }
        fputs("        default: break;\n        }\n        return c;\n", o);
    }
    fputs("    default: return -1;\n    }\n}\n", o);
    /* a host's direct access to values held natively: a number of a word, binary or immediate natural
     * sort; the content of a byte-string sort (false for any other sort, or a number above 2^64) */
    fputs("bool rc_value_u64(int sort, T *x, uint64_t *v) {\n    switch (sort) {\n", o);
    for (int s = 0; s < nsorts; s++) {
        int r = sorts[s].native;
        if (REP_IMMNAT(r) || r == RC_REP_WORD) fprintf(o, "    case %d: *v = RC_IMM_VAL(x); return true;\n", s);
        else if (r == RC_REP_BINARY) fprintf(o, "    case %d: return rc_bn_to_u64(x, v);\n", s);
    }
    fputs("    default: (void)x; (void)v; return false;\n    }\n}\n", o);
    fputs("bool rc_value_bytes(int sort, T *x, const uint8_t **p, size_t *n) {\n    switch (sort) {\n", o);
    for (int s = 0; s < nsorts; s++) {
        int r = sorts[s].native;
        if (r == RC_REP_SLICES) fprintf(o, "    case %d: *p = rc_sl_data(x); *n = rc_sl_length(x); return true;\n", s);
        else if (r == RC_REP_BYTES) fprintf(o, "    case %d: *p = rc_ibytes(x); *n = rc_ibytes_len(x); return true;\n", s);
    }
    fputs("    default: (void)x; (void)p; (void)n; return false;\n    }\n}\n", o);
    /* and the converse, a host's number or byte string as a value of such a sort (NULL when the sort
     * holds no numbers or byte strings, or not this one) */
    fputs("T *rc_value_of_u64(int sort, uint64_t v) {\n    switch (sort) {\n", o);
    for (int s = 0; s < nsorts; s++) {
        int r = sorts[s].native;
        if (REP_IMMNAT(r)) fprintf(o, "    case %d: return v <= (UINT64_MAX >> 3) ? RC_IMM(v) : (T *)0;\n", s);
        else if (r == RC_REP_WORD) fprintf(o, "    case %d: return v < ((uint64_t)1 << %d) ? RC_IMM(v) : (T *)0;\n", s, sorts[s].width);
        else if (r == RC_REP_BINARY) fprintf(o, "    case %d: return rc_bn_from_u64(v);\n", s);
    }
    fputs("    default: (void)v; return (T *)0;\n    }\n}\n", o);
    fputs("T *rc_value_of_bytes(int sort, const uint8_t *p, size_t n) {\n    switch (sort) {\n", o);
    for (int s = 0; s < nsorts; s++) {
        int r = sorts[s].native;
        if (r == RC_REP_SLICES || r == RC_REP_BYTES) fprintf(o, "    case %d: return n <= UINT32_MAX ? rc_intern_bytes(p, n) : (T *)0;\n", s);
    }
    fputs("    default: (void)p; (void)n; return (T *)0;\n    }\n}\n", o);
    /* a value's items in pre-order, for a host that reads module data in bulk: a number (a sort with a
     * numeric representation), a byte string (a byte-string representation), or a constructor followed
     * by its children's items.  One walker per sort, through the sort's own views; the last child is
     * followed in a loop when it is of the same sort (lists of any length), else by a tail call.
     * rc_list_items gives the items of consecutive elements of a list (a sort whose only constructors
     * are a nullary NIL and CONS (element, rest)) into a buffer, whole elements only, and moves the
     * cursor past them; 0 when it is at the end (or an element does not fit in the empty buffer) */
    {
        #define RCW_FULL "(size_t)-1"
        for (int st = 0; st < nsorts; st++) fprintf(o, "static size_t rcw_%d(T *x, RcItem *b, size_t at, size_t cap);\n", st);
        for (int st = 0; st < nsorts; st++) {
            int r = sorts[st].native;
            const char *rr = sort_rep(st);
            fprintf(o, "static size_t rcw_%d(T *x, RcItem *b, size_t at, size_t cap) {\n", st);
            if (REP_IMMNAT(r) || r == RC_REP_WORD) {
                fputs("    if (at == cap) return " RCW_FULL ";\n    b[at].ctor = -1; b[at].v = RC_IMM_VAL(x); b[at].p = 0; b[at].n = 0;\n    return at + 1;\n}\n", o);
                continue;
            }
            if (r == RC_REP_BINARY) {
                fputs("    uint64_t v;\n    if (at == cap || !rc_bn_to_u64(x, &v)) return " RCW_FULL ";\n"
                      "    b[at].ctor = -1; b[at].v = v; b[at].p = 0; b[at].n = 0;\n    return at + 1;\n}\n", o);
                continue;
            }
            if (r == RC_REP_SLICES || r == RC_REP_BYTES) {
                const char *dp = r == RC_REP_SLICES ? "rc_sl_data(x)" : "rc_ibytes(x)";
                const char *ln = r == RC_REP_SLICES ? "rc_sl_length(x)" : "rc_ibytes_len(x)";
                fprintf(o, "    if (at == cap) return " RCW_FULL ";\n    b[at].ctor = -2; b[at].v = 0; b[at].p = %s; b[at].n = (uint32_t)%s;\n    return at + 1;\n}\n", dp, ln);
                continue;
            }
            if (r == RC_REP_RECORDS || r == RC_REP_TABLE) {
                fputs("    (void)x; (void)b; (void)at; (void)cap;\n    return " RCW_FULL ";\n}\n", o);
                continue;
            }
            fputs("    for (;;) {\n        if (at == cap) return " RCW_FULL ";\n", o);
            fprintf(o, "        uint16_t c = RC_TAG_%s(x);\n        b[at].ctor = c; b[at].v = 0; b[at].p = 0; b[at].n = 0;\n        at++;\n        switch (c) {\n", rr);
            for (int c = 0; c < nctors; c++) {
                if (ctors[c].defined || ctors[c].sort != st) continue;
                int n = ctors[c].arity;
                fprintf(o, "        case %d:", c);
                if (!n) { fputs(" return at;\n", o); continue; }
                fprintf(o, " b[at - 1].n = %d;\n", n);
                for (int i = 0; i + 1 < n; i++)
                    fprintf(o, "            at = rcw_%d(RC_KID_%s(x, %d), b, at, cap);\n            if (at == " RCW_FULL ") return at;\n", ctors[c].asort[i], rr, i);
                if (ctors[c].asort[n - 1] == st) fprintf(o, "            x = RC_KID_%s(x, %d);\n            continue;\n", rr, n - 1);
                else fprintf(o, "            return rcw_%d(RC_KID_%s(x, %d), b, at, cap);\n", ctors[c].asort[n - 1], rr, n - 1);
            }
            fputs("        default: return " RCW_FULL ";\n        }\n    }\n}\n", o);
        }
        fputs("size_t rc_value_items(int sort, T *x, RcItem *buf, size_t cap) {\n    switch (sort) {\n", o);
        for (int st = 0; st < nsorts; st++) fprintf(o, "    case %d: return rcw_%d(x, buf, 0, cap);\n", st, st);
        fputs("    default: return " RCW_FULL ";\n    }\n}\n", o);
        fputs("size_t rc_list_items(int sort, T **cursor, RcItem *buf, size_t cap) {\n    switch (sort) {\n", o);
        for (int st = 0; st < nsorts; st++) {
            int nil = -1, cons = -1;
            for (int c = 0; c < nctors; c++) {
                if (ctors[c].defined || ctors[c].sort != st) continue;
                if (ctors[c].arity == 0) nil = c; else if (ctors[c].arity == 2 && ctors[c].asort[1] == st) cons = c;
            }
            if (sorts[st].ndata != 2 || nil < 0 || cons < 0 || sorts[st].native == RC_REP_RECORDS ||
                sorts[st].native == RC_REP_TABLE || sorts[st].native == RC_REP_SLICES || sorts[st].native == RC_REP_BYTES)
                continue;
            const char *rr = sort_rep(st);
            fprintf(o, "    case %d: {\n        size_t at = 0;\n        for (T *x = *cursor; RC_TAG_%s(x) == %d; x = RC_KID_%s(x, 1)) {\n"
                       "            size_t m = rcw_%d(RC_KID_%s(x, 0), buf, at, cap);\n            if (m == " RCW_FULL ") break;\n"
                       "            at = m;\n            *cursor = RC_KID_%s(x, 1);\n        }\n        return at;\n    }\n",
                    st, rr, cons, rr, ctors[cons].asort[0], rr, rr);
        }
        fputs("    default: (void)cursor; (void)buf; (void)cap; return 0;\n    }\n}\n", o);
        #undef RCW_FULL
    }
    /* equality of two values of a sort as module data, through the views (whatever the representation:
     * two byte strings of equal content are equal whether slices or interned); the last child is
     * followed in a loop, so lists of any length need no stack */
    {
        int maxar = 1;
        for (int c = 0; c < nctors; c++) if (!ctors[c].defined && ctors[c].arity > maxar) maxar = ctors[c].arity;
        fprintf(o, "extern const uint8_t rc_ctor_arity[];\nextern const uint16_t rc_ctor_asort_off[], rc_ctor_asort[];\n"
                   "bool rc_value_equal(int sort, T *a, T *b) {\n"
                   "    T *ka[%d], *kb[%d];\n"
                   "    for (;;) {\n"
                   "        if (a == b) return true;\n"
                   "        if (!a || !b) return false;\n"
                   "        int ca = rc_view(sort, a, ka), cb = rc_view(sort, b, kb);\n"
                   "        if (ca != cb || ca < 0) return false;\n"
                   "        int n = rc_ctor_arity[ca];\n"
                   "        if (!n) return true;\n"
                   "        const uint16_t *as = &rc_ctor_asort[rc_ctor_asort_off[ca]];\n"
                   "        for (int i = 0; i + 1 < n; i++) if (!rc_value_equal(as[i], ka[i], kb[i])) return false;\n"
                   "        sort = as[n - 1]; a = ka[n - 1]; b = kb[n - 1];\n"
                   "    }\n}\n", maxar, maxar);
    }
    /* the components' parameters */
    fputs("void rc_components_init(void) {\n", o);
    for (int s = 0; s < nsorts; s++) {
        if (sorts[s].native == RC_REP_BYTES) {
            fputs("    static const uint16_t nib[16] = {", o);
            for (int i = 0; i < 16; i++) fprintf(o, "%s%d", i ? ", " : "", byte_nib[i]);
            fprintf(o, "};\n    rc_bytes_init(%d, %d, %d, nib);\n", rep_ctor[RC_REP_BYTES][0], rep_ctor[RC_REP_BYTES][1], rep_ctor[RC_REP_BYTES][2]);
        }
        if (sorts[s].native == RC_REP_TABLE)
            fprintf(o, "    rc_table_init(%d, %d, %d, %d);\n", table_ctor[0], table_ctor[1], table_ctor[2], table_ctor[3]);
    }
    fputs("}\n", o);
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
    for (int s = 0; s < nsorts; s++) fprintf(o, "%s%s", s ? "," : "", rep_id_name[sorts[s].native]);
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
    if (nground) fprintf(o, "    memset(rc_ground, 0, sizeof rc_ground);\n");
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
    /* entry: call a judgment by id with an argument vector; a run that stops at the safepoint
     * (RC_STOPPED) keeps its frames, and rc_resume continues it: it leaves the frames above
     * rc_stop_mark to the host (who stops a run takes what it wants from them and applies the rest,
     * rc_pend_apply) */
    fputs("\nstatic T *rc_enter(int judgment, T **a) {\n    switch (judgment) {\n", o);
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined) continue;
        fprintf(o, "    case %d: return p%d(", c, c);
        for (int a = 0; a < ctors[c].arity; a++) fprintf(o, "%sa[%d]", a ? ", " : "", a);
        fputs(");\n", o);
    }
    fputs("    default: return (T *)0;\n    }\n}\n", o);
    fputs("T *rc_call(int judgment, T **a) {\n    size_t m_ = rc_pend_depth;\n    T *v_ = rc_enter(judgment, a);\n"
          "    if (v_ == RC_STOPPED) { rc_stop_mark = m_; return v_; }\n    return rc_pend_unwind(m_, v_);\n}\n", o);
    /* the same entry for a host that takes the frames itself: they are left above rc_stop_mark
     * whether the run stops or ends */
    fputs("T *rc_start(int judgment, T **a) {\n    size_t m_ = rc_pend_depth;\n    T *v_ = rc_enter(judgment, a);\n"
          "    rc_stop_mark = m_;\n    return v_;\n}\n", o);
    fputs("T *rc_resume(void) {\n", o);
    if (safepoint_ctor >= 0) {
        fprintf(o, "    T *v_ = p%d(", safepoint_ctor);
        for (int a = 0; a < ctors[safepoint_ctor].arity; a++) fprintf(o, "%src_roots[%d]", a ? ", " : "", a);
        fputs(");\n    return v_;\n}\n", o);
    } else fputs("    return (T *)0;\n}\n", o);
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
              "void rc_reset_constants(void);\nvoid rc_components_init(void);\nextern const int rc_nmemo;\nextern const char *const rc_memo_name[];\n"
              "T *rc_construct(int ctor, T *const *kids);\nint rc_view(int sort, T *x, T **kids);\nbool rc_value_equal(int sort, T *a, T *b);\nT *rc_resume(void);\n"
              "bool rc_value_u64(int sort, T *x, uint64_t *v);\nbool rc_value_bytes(int sort, T *x, const uint8_t **p, size_t *n);\n"
              "T *rc_value_of_u64(int sort, uint64_t v);\nT *rc_value_of_bytes(int sort, const uint8_t *p, size_t n);\n"
              "size_t rc_value_items(int sort, T *x, RcItem *buf, size_t cap);\nsize_t rc_list_items(int sort, T **cursor, RcItem *buf, size_t cap);\n"
              "T *rc_call(int judgment, T **a);\nT *rc_start(int judgment, T **a);\n#endif\n", h);
        fclose(h);
    }
}


/* the glue of each component block: the component called with the parameters of its row, its
 * result given as the module's constructors */
void emit_component_glue(FILE *o) {
    for (int i = 0; i < nnative_ops; i++) {
        const NativeOp *b = &native_ops[i];
        if (!b->comp) continue;
        const Ctor *k = &ctors[b->ctor];
        const Sx *x = b->comp;
        const char *kind = x->v[0]->s;
        const char *rr = sort_rep(k->sort);
        #define CT(j) map_get(&ctor_map, x->v[j]->s)
        /* a comparator judgment called by the component: its frames closed before the component reads
         * the answer */
        if ((!strcmp(kind, "set-union") || !strcmp(kind, "set-remove")) && n_trmc)
            fprintf(o, "\nstatic T *rc_cmp_%d(T *a, T *b) { return %s(p%d(a, b)); }", i, closed(CT(1)), CT(1));
        fprintf(o, "\n/* block %s: %s answered by %s */\nstatic T *%s", b->name, k->name, kind, b->fn); emit_params(o, k->arity); fputs(" {\n", o);
        if (!strcmp(kind, "equal")) {
            fprintf(o, "    return a0 == a1 ? RC_NUL_%s(%d) : RC_NUL_%s(%d);\n", rr, CT(1), rr, CT(2));
        } else if (!strcmp(kind, "bytes-order") || !strcmp(kind, "nat-order") || !strcmp(kind, "structural-order")) {
            if (!strcmp(kind, "bytes-order")) fputs("    int c_ = rc_bytes_cmp(a0, a1);\n", o);
            else if (!strcmp(kind, "nat-order") && sorts[k->asort[0]].native == RC_REP_BINARY) fputs("    int c_ = rc_bn_cmp(a0, a1);\n", o);
            else if (!strcmp(kind, "nat-order")) fputs("    int c_ = rc_nat_cmp(a0, a1);\n", o);
            else fprintf(o, "    int c_ = rc_order_cmp(%d, a0, a1);\n", k->asort[0]);
            fprintf(o, "    return c_ < 0 ? RC_NUL_%s(%d) : c_ > 0 ? RC_NUL_%s(%d) : RC_NUL_%s(%d);\n", rr, CT(1), rr, CT(3), rr, CT(2));
        } else if (!strcmp(kind, "bytes-snoc")) {
            const char *hex = x->v[1]->s;
            size_t n = strlen(hex) / 2;
            fputs("    static const uint8_t s_[] = {", o);
            for (size_t j = 0; j < n; j++) fprintf(o, "%s0x%c%c", j ? ", " : "", hex[2 * j], hex[2 * j + 1]);
            fprintf(o, "};\n    return rc_bytes_snoc(a0, s_, %zu);\n", n);
        } else if (!strcmp(kind, "nat-succ")) {
            fputs(sorts[k->sort].native == RC_REP_BINARY ? "    return rc_bn_succ(a0);\n" : "    return rc_nat_succ(a0);\n", o);
        } else if (!strcmp(kind, "nat-add")) {
            fputs(sorts[k->sort].native == RC_REP_BINARY ? "    return rc_bn_add(a0, a1);\n" : "    return rc_nat_add(a0, a1);\n", o);
        } else if (!strcmp(kind, "nat-pred")) {
            fputs("    return rc_bn_pred(a0);\n", o);
        } else if (!strcmp(kind, "nat-xor")) {
            fputs("    return rc_bn_xor(a0, a1);\n", o);
        } else if (!strcmp(kind, "nat-multiply")) {
            fputs("    return rc_bn_mul(a0, a1);\n", o);
        } else if (!strcmp(kind, "nat-subtract")) {
            fputs("    return rc_bn_sub(a0, a1);\n", o);
        } else if (!strcmp(kind, "nat-trim")) {
            fputs("    return rc_bn_trim(a1, a0);\n", o);
        } else if (!strcmp(kind, "nat-push-bit")) {
            fprintf(o, "    return rc_bn_shl1_or(a1, a0 == RC_NUL_symbolic(%d));\n", sorts[k->sort].rp[3]);
        } else if (!strcmp(kind, "word-pack")) {
            fprintf(o, "    return rc_bn_pack(a1, RC_IMM_VAL(a0), %d);\n", sorts[k->asort[0]].width);
        } else if (!strcmp(kind, "bytes-length")) {
            fputs("    return rc_bn_from_u64((uint64_t)rc_sl_length(a0));\n", o);
        } else if (!strcmp(kind, "bytes-take")) {
            fprintf(o, "    uint64_t n_;\n    if (!rc_bn_to_u64(a0, &n_) || n_ > (uint64_t)rc_sl_length(a1)) return RC_NUL_%s(%d);\n"
                       "    return RC_MK2_%s(%d, rc_sl_take(a1, (size_t)n_), rc_sl_drop(a1, (size_t)n_));\n", rr, CT(2), rr, CT(1));
        } else if (!strcmp(kind, "bytes-equal")) {
            fprintf(o, "    return rc_sl_equal(a0, a1) ? RC_NUL_%s(%d) : RC_NUL_%s(%d);\n", rr, CT(1), rr, CT(2));
        } else if (!strcmp(kind, "uleb")) {
            int dc = CT(1), mb = atoi(x->v[4]->s), lb = atoi(x->v[5]->s);
            const char *dr = sort_rep(ctors[dc].sort);
            fprintf(o, "    const uint8_t *d_ = rc_sl_data(a0);\n    size_t n_ = rc_sl_length(a0);\n    uint64_t v_ = 0;\n"
                       "    for (size_t i_ = 0; ; i_++) {\n"
                       "        if (i_ == n_) return RC_NUL_%s(%d);\n"
                       "        unsigned b_ = d_[i_];\n"
                       "        if (i_ == %d) { if (b_ >> %d) return RC_NUL_%s(%d); }\n"
                       "        v_ |= (uint64_t)(b_ & 0x7fu) << (7 * i_);\n"
                       "        if (!(b_ & 0x80u)) {\n"
                       "            if (i_ > 0 && b_ == 0) return RC_NUL_%s(%d);\n"
                       "            return RC_MK2_%s(%d, rc_bn_from_u64(v_), rc_sl_drop(a0, i_ + 1));\n"
                       "        }\n    }\n",
                    dr, CT(2), mb - 1, lb, dr, CT(3), dr, CT(3), dr, dc);
        } else if (!strcmp(kind, "fnv-hash")) {
            unsigned long long basis = strtoull(x->v[1]->s, NULL, 0), prime = strtoull(x->v[2]->s, NULL, 0), bits = strtoull(x->v[3]->s, NULL, 0);
            /* ((h xor b) * PRIME) mod 2^BITS: the product mod 2^64 (unsigned wrap), then the low BITS bits */
            unsigned long long mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
            fprintf(o, "    const uint8_t *d_ = rc_sl_data(a0);\n    size_t n_ = rc_sl_length(a0);\n    uint64_t h_ = %lluull;\n"
                       "    for (size_t i_ = 0; i_ < n_; i_++) h_ = ((h_ ^ d_[i_]) * %lluull) & %lluull;\n    return rc_bn_from_u64(h_);\n",
                    basis, prime, mask);
        } else if (!strcmp(kind, "set-union") || !strcmp(kind, "set-remove")) {
            int ls = !strcmp(kind, "set-union") ? k->asort[0] : k->asort[1], nil = -1, cons = -1;
            for (int c = 0; c < nctors; c++)
                if (!ctors[c].defined && ctors[c].sort == ls) { if (ctors[c].arity == 0) nil = c; else cons = c; }
            fprintf(o, "    static const RcSetDesc d_ = {%d, %d, %d, %d, %d};\n", nil, cons, CT(2), CT(3), CT(4));
            /* the comparator's own derivations are inside the block: a block is one trace event */
            char cmp[32];
            if (n_trmc) snprintf(cmp, sizeof cmp, "rc_cmp_%d", i); else snprintf(cmp, sizeof cmp, "p%d", CT(1));
            fprintf(o, "    int tr_ = rc_trace_on; rc_trace_on = 0;\n    T *r_ = rc_%s(&d_, %s, a0, a1);\n    rc_trace_on = tr_;\n    return r_;\n",
                    !strcmp(kind, "set-union") ? "set_union" : "set_remove", cmp);
        } else if (!strcmp(kind, "table-get")) fputs("    return rc_table_get(a0, a1);\n", o);
        else if (!strcmp(kind, "table-put")) fputs("    return rc_table_put(a0, a1);\n", o);
        else if (!strcmp(kind, "table-set")) fputs("    return rc_table_set(a0, a1, a2);\n", o);
        else if (!strcmp(kind, "table-next")) fputs("    return rc_table_next(a0);\n", o);
        else if (!strcmp(kind, "table-empty")) fputs("    return rc_table_empty();\n", o);
        else die("block %s: no glue for %s", b->name, kind);
        #undef CT
        fputs("}\n", o);
    }
}
