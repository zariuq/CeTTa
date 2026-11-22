/* tcalc's native kernel against the core's C: its unifier
 * (native/kernel/tcunify.metta) against unify_c, and its term builder
 * (tcbuild.metta) against resolve_from and close_from.
 *
 * Unification: random equations between random terms, over a solver with random
 * assignments already made, two frames of locals and unknowns. Both run from
 * equal copies of the solver, the native one through tc_kernel_unify with the
 * core's C close, so a term to close is closed alike. They must agree on the
 * answer, and when it is a solution, on every assignment and the trail, entry
 * for entry. Some equations are wide enough to grow the work stack, some bind
 * enough variables to grow the trail, and some start one step from the end of
 * the walk epochs.
 *
 * Term making: from the same solvers, a term resolved, a term closed, and
 * several closed in one walk (as append_made closes a rule's made rows). Both
 * must give the same terms, the same fresh unknowns, and the same trail.
 *
 *   tcalc_native_unify_test [EQUATIONS] [SEED] */
#include "../../native/tcalc/tcalc_core.export.c"
#include <stdio.h>
#include <stdlib.h>

enum { UNKNOWNS = 40, LOCALS = 20, FRAMES = 2 };
static uint64_t rng = 88172645463325252u;
static unsigned pick(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return n ? (unsigned)(rng % n) : 0; }

typedef struct { TcTerms *t; TcTerm sym[4], num[2], unk[UNKNOWNS], loc[LOCALS]; } World;

/* A term; with locals, those of one frame (the caller fixes which). Unknowns
 * below `floor` are left out, so assignments made in order stay acyclic. */
static TcTerm term(World *w, unsigned depth, bool locals, unsigned unknown_floor, unsigned local_floor, unsigned width) {
    unsigned c = pick(depth ? 10 : 6);
    if (c < 2) return w->sym[pick(4)];
    if (c == 2) return w->num[pick(2)];
    if (c == 3 && unknown_floor < UNKNOWNS) return w->unk[unknown_floor + pick(UNKNOWNS - unknown_floor)];
    if ((c == 4 || c == 5) && locals && local_floor < LOCALS) return w->loc[local_floor + pick(LOCALS - local_floor)];
    if (!depth) return w->sym[pick(4)];
    TcTerm items[64];
    unsigned n = pick(width + 1);
    if (n > 64) n = 64;
    for (unsigned i = 0; i < n; ++i)
        items[i] = i == 0 && pick(2) ? w->sym[pick(4)] : term(w, depth - 1, locals, unknown_floor, local_floor, width);
    return tc_expression(w->t, items, n);
}

static void solver_copy(Solver *to, const Solver *from) {
    *to = *from;
    to->local = calloc(from->local_cap, sizeof *to->local);
    to->unknown = calloc(from->unknown_cap, sizeof *to->unknown);
    to->trail = calloc(from->trail_cap ? from->trail_cap : 1, sizeof *to->trail);
    to->work = calloc(from->work_cap ? from->work_cap : 1, sizeof *to->work);
    to->build = NULL; to->build_len = to->build_cap = 0;
    memcpy(to->local, from->local, from->local_cap * sizeof *to->local);
    memcpy(to->unknown, from->unknown, from->unknown_cap * sizeof *to->unknown);
    if (from->trail_len) memcpy(to->trail, from->trail, from->trail_len * sizeof *to->trail);
    if (from->work_len) memcpy(to->work, from->work, from->work_len * sizeof *to->work);
}

static bool same_tables(const Solver *x, const Solver *y) {
    if (x->trail_len != y->trail_len || x->unknowns != y->unknowns || x->work_len != y->work_len) return false;
    if (memcmp(x->trail, y->trail, x->trail_len * sizeof *x->trail)) return false;
    for (size_t i = 0; i < FRAMES * LOCALS; ++i) if (x->local[i] != y->local[i]) return false;
    for (size_t i = 0; i < x->unknowns; ++i) if (x->unknown[i] != y->unknown[i]) return false;
    return true;
}

int main(int argc, char **argv) {
    unsigned equations = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 20000;
    if (argc > 2) rng = strtoull(argv[2], NULL, 10) * 2654435761u + 1;
    World w = {.t = tc_terms_new()};
    if (!w.t) return 2;
    static const char *names[] = {"f", "g", "h", "k"};
    for (unsigned i = 0; i < 4; ++i) w.sym[i] = tc_symbol(w.t, names[i], 1);
    for (unsigned i = 0; i < 2; ++i) w.num[i] = tc_integer(w.t, i);
    for (unsigned i = 0; i < UNKNOWNS; ++i) w.unk[i] = tc_unknown(w.t, i);
    for (unsigned i = 0; i < LOCALS; ++i) w.loc[i] = tc_local(w.t, i);
    unsigned solved = 0, refuted = 0, grown_work = 0, grown_trail = 0, renewed = 0, failures = 0;
    unsigned made = 0, resolved = 0, closed_many = 0, grown_out = 0, grown_stack = 0;
    for (unsigned e = 0; e < equations && failures < 10; ++e) {
        Solver base = {0};
        if (!reserve(&base.local, &base.local_cap, FRAMES * LOCALS) || !reserve(&base.unknown, &base.unknown_cap, UNKNOWNS)) return 2;
        base.unknowns = UNKNOWNS;
        /* Assignments made so far: unknowns to terms without locals, locals to
           ground terms, unknowns or terms of their own frame, each mentioning
           only later variables. */
        for (unsigned i = 0; i < UNKNOWNS; ++i)
            if (pick(3) == 0 && !assign(&base, false, i, term(&w, 2, false, i + 1, LOCALS, 3))) return 2;
        for (unsigned f = 0; f < FRAMES; ++f)
            for (unsigned i = 0; i < LOCALS; ++i)
                if (pick(4) == 0 && !assign(&base, true, f * LOCALS + i, term(&w, 2, true, 0, i + 1, 3))) return 2;
        bool wide = pick(8) == 0;
        unsigned width = wide ? 48 : 3, depth = wide ? 2 : 3;
        uint32_t abase = LOCALS * pick(FRAMES), bbase = pick(3) ? abase : LOCALS * pick(FRAMES);
        TcTerm a = term(&w, depth, true, 0, 0, width), b = pick(4) ? term(&w, depth, true, 0, 0, width) : a;
        Solver c, n;
        solver_copy(&c, &base); solver_copy(&n, &base);
        size_t trail_cap = n.trail_cap, work_cap = n.work_cap;
        bool rc = unify_c(w.t, &c, a, abase, b, bbase);
        /* Walk epochs only grow: a record of an earlier walk is never current. */
        if (pick(16) == 0) { w.t->walk = UINT32_MAX; walk_begin(w.t); w.t->walk = UINT32_MAX - pick(3); }
        uint32_t walk = w.t->walk;
        bool rn = tc_kernel_unify(w.t, &n, a, abase, b, bbase);
        grown_work += n.work_cap > (work_cap > 64 ? work_cap : 64); grown_trail += n.trail_cap > trail_cap && trail_cap;
        renewed += w.t->walk < walk;
        bool ok = rc == rn && !c.failed && !n.failed && (!rc || same_tables(&c, &n));
        if (!ok) {
            ++failures;
            fprintf(stderr, "equation %u differs: C %d (failed %d), native %d (failed %d); trail %zu/%zu unknowns %u/%u\n  ",
                    e, rc, c.failed, rn, n.failed, c.trail_len, n.trail_len, c.unknowns, n.unknowns);
            tc_print(w.t, a, stderr); fprintf(stderr, " @%u  =  ", abase); tc_print(w.t, b, stderr); fprintf(stderr, " @%u\n", bbase);
        }
        rc ? ++solved : ++refuted;
        solver_free(&c); solver_free(&n);
        /* Making terms. */
        TcTerm roots[4], made_c[4], made_n[4];
        unsigned count = 1 + pick(4), how = pick(3);
        uint32_t at = LOCALS * pick(FRAMES);
        if (how < 2) count = 1;
        for (unsigned i = 0; i < count; ++i) roots[i] = i && !pick(5) ? roots[i - 1] : term(&w, depth, true, 0, 0, width);
        solver_copy(&c, &base); solver_copy(&n, &base);
        bool made_ok = true;
        walk_begin(w.t);
        for (unsigned i = 0; i < count; ++i)
            made_c[i] = how == 0 ? resolve_from(w.t, &c, roots[i], at) : close_from(w.t, &c, roots[i], at);
        if (pick(16) == 0) { w.t->walk = UINT32_MAX; walk_begin(w.t); w.t->walk = UINT32_MAX - pick(2); }
        made_ok = tc_kernel_output(w.t, &n, roots, count, at, how != 0, made_n);
        grown_out += n.native_out_cap > 64; grown_stack += n.native_stack_cap > 64;
        for (unsigned i = 0; made_ok && i < count; ++i) made_ok = made_c[i] && made_c[i] == made_n[i];
        if (!made_ok || c.failed || n.failed || !same_tables(&c, &n)) {
            ++failures;
            fprintf(stderr, "making %u term(s) (%s) at %u differs: trail %zu/%zu unknowns %u/%u\n  ", count,
                    how == 0 ? "resolve" : "close", at, c.trail_len, n.trail_len, c.unknowns, n.unknowns);
            tc_print(w.t, roots[0], stderr); fprintf(stderr, "  C ");
            if (made_c[0]) tc_print(w.t, made_c[0], stderr);
            fprintf(stderr, "  native ");
            if (made_ok || made_n[0]) tc_print(w.t, made_n[0], stderr);
            fprintf(stderr, "\n");
        }
        made += count; resolved += how == 0; closed_many += how == 2;
        solver_free(&c); solver_free(&n); solver_free(&base);
    }
    tc_terms_free(w.t);
    printf("native tcalc unifier: %u equations, %u solved, %u refuted, each as unify_c with equal assignments and "
           "trail; %u grew the work stack, %u the trail, %u renewed the walk epochs; %u differ\n",
           solved + refuted, solved, refuted, grown_work, grown_trail, renewed, failures);
    printf("native tcalc term builder: %u terms made, %u resolved, %u closed alone, %u closed in groups, each "
           "as resolve_from and close_from make it, with equal fresh unknowns and trail; %u grew the list, %u the "
           "stack\n", made, resolved, solved + refuted - resolved - closed_many, closed_many, grown_out, grown_stack);
    return failures ? 1 : 0;
}
