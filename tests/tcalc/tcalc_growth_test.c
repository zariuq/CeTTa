/* Growth gates for the tcalc core: each program runs at a size n and at 2n,
 * and the work the core counts (tc_outcomes_unifications, rows_met,
 * rows_stored) is compared.  Work linear in the size doubles; quadratic work
 * quadruples.  A gate fails only when the ratio says the growth class is
 * wrong (a total at least 3 where 2 is linear; a count per row derived at
 * least 1.5 where it is flat), never on a count itself, and the counts do
 * not depend on time or load.
 *
 *   tcalc_growth_test          the gates of step every over a set store
 *   tcalc_growth_test step-one the gate of a branching program under step one
 *
 * Gates of step every over a set store, a program that only reads and makes:
 *   chain      reach along a chain of n edges: n steps, each adding one row.
 *              Work and rows held grow linearly: a step meets the row the step
 *              before added, joined through an index, and the store is one.
 *   closure    the transitive closure of a chain: n^2/2 rows derived.  Work
 *              per row derived stays flat (semi-naive: linear in derivations).
 *   demand     the length of a list of n cells read by demand: a value row
 *              meets only the demand it answers, through the fixed items of
 *              the demand's call, never every demand.
 *   joins      the same list beside k relations whose rules read demands and
 *              values of their own calls, none asked: a step starts only from
 *              the rule patterns waiting on the rows it adds (their call's
 *              head), so the patterns tried do not grow with k.
 * Gates of the rules' join plans (tcalc_plan.c), each beside a degraded
 * variant that must show the class it rules out:
 *   keyed      A(x), B(x,y), C(y) of n rows each, the rule written A, C, B: the
 *              plan meets B by x after A, then C by y, so the unifications
 *              per row derived stay flat in n; joined through equations
 *              instead of shared locals (degraded), no key binds B or C and
 *              each A row meets every B row.
 *   guard      a guard on A's value, the rule written B, A: the plan meets A
 *              first and decides the guard before B, so the unifications per
 *              row of A stay flat in n where a guard that reads both
 *              patterns (degraded) is decided after B, linear per A row.
 *   indexes    k rules each searching one relation by a prefix of its
 *              arguments, the first i for rule i: one index serves them all
 *              (a chain of searches), so the indexes held stay flat in k;
 *              searching it by argument i alone (degraded), no search holds
 *              another and each needs its own index.
 * The gates of step one (tcalc_growth_test step-one), where a state is a map
 * of rows shared with the state it was made from:
 *   step-one   two tokens taken, in either order, beside n rows no rule
 *              touches: four states.  The rows held per state, beyond the
 *              rows of the first state, must not grow with the rows no step
 *              changes (the store is not copied per state).
 *   takes      a run of n steps, each taking a row of a store of 2n rows (a
 *              branch that removes rows): the rows held per state, beyond the
 *              first state's, stay flat.
 *   bag        a multiset store: c copies of one row, a step taking one copy
 *              at a time: a copy is a count, not a row held, so the rows held
 *              per state stay flat in c.
 *   dedup      k tokens taken in every order beside n rows: the 2^k states
 *              (two orders reaching one state are one state), and the work of
 *              deciding that per state (map nodes compared: a node both states
 *              share is not read) stays flat in n.  Every firing is computed
 *              (shortcuts none: no second order is looked up), so each order
 *              reaches its state by its own successor, found equal.
 * Each runs beside a degraded variant: the same program whose untouched rows
 * mention unknowns.  The canonical names of unknowns are a state's own, so
 * those rows are kept by each state anew, which is the class the gate rules
 * out; the gate's count must see it (a ratio at least the bound), or the gate
 * proves nothing. */
#include "tcalc_core.h"
#include "tcalc_syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    size_t len, cap;
} Text;

static void put(Text *t, const char *s) {
    size_t n = strlen(s);
    if (t->len + n + 1 > t->cap) {
        t->cap = (t->len + n + 1) * 2;
        t->text = realloc(t->text, t->cap);
        if (!t->text) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    memcpy(t->text + t->len, s, n + 1);
    t->len += n;
}

static void putf(Text *t, const char *format, long a, long b) {
    char line[256];
    snprintf(line, sizeof line, format, a, b);
    put(t, line);
}

static void chain(Text *t, long n) {
    put(t, "(reach 0)\n(rule hop (read (reach $x) (edge $x $y)) (make (reach $y)))\n");
    for (long i = 0; i < n; i++) putf(t, "(edge %ld %ld)\n", i, i + 1);
}

static void closure(Text *t, long n) {
    put(t, "(rule base (read (edge $x $y)) (make (path $x $y)))\n"
           "(rule step (read (path $x $y) (edge $y $z)) (make (path $x $z)))\n");
    for (long i = 0; i < n; i++) putf(t, "(edge %ld %ld)\n", i, i + 1);
}

static void demand(Text *t, long n) {
    put(t, "(rule len-nil (read (demand (Len nil))) (make (value (Len nil) z)))\n"
           "(rule len-cons/1.ask (read (demand (Len (cons $h $t)))) (make (demand (Len $t))))\n"
           "(rule len-cons/1 (read (demand (Len (cons $h $t))) (value (Len $t) $n))\n"
           "  (make (value (Len (cons $h $t)) (s $n))))\n(demand (Len ");
    for (long i = 0; i < n; i++) putf(t, "(cons %ld ", i, 0);
    put(t, "nil");
    for (long i = 0; i < n; i++) put(t, ")");
    put(t, "))\n");
}

static long joins_k;   /* the relations beside the list */

static void joins(Text *t, long n) {
    demand(t, n);
    for (long k = 0; k < joins_k; k++) {
        putf(t, "(rule other-%ld (read (demand (Other%ld $x)))", k, k);
        putf(t, " (make (value (Other%ld $x) done)))\n", k, 0);
        putf(t, "(rule other-%ld/1 (read (demand (Other%ld (cons $h $t)))", k, k);
        putf(t, " (value (Other%ld $t) $n))", k, 0);
        putf(t, " (make (value (Other%ld (cons $h $t)) $n)))\n", k, 0);
    }
}

/* The rows no step touches, beside a branching program: fixed, or (degraded)
 * each mentioning an unknown of its own. */
static bool degraded;

static void untouched(Text *t, long n) {
    for (long i = 0; i < n; i++)
        if (degraded) putf(t, "(fact %ld $u%ld)\n", i, i);
        else putf(t, "(fact %ld %ld)\n", i, i);
}

static void step_one(Text *t, long n) {
    put(t, "(token a)\n(token b)\n(rule take (take (token $x)) (make (done $x)))\n");
    untouched(t, n);
}

static void takes(Text *t, long n) {
    put(t, "(at 0)\n(rule eat (take (at $i) (cell $i)) (read (succ $i $j)) (make (at $j)))\n");
    for (long i = 0; i < n; i++) {
        putf(t, "(cell %ld)\n", i, 0);
        putf(t, "(succ %ld %ld)\n", i, i + 1);
    }
    untouched(t, n);
}

static void bag(Text *t, long n) {
    put(t, "(purse z)\n(rule spend (take (coin) (purse $n)) (make (purse (s $n))))\n");
    for (long i = 0; i < n; i++) put(t, "(coin)\n");
    untouched(t, n);
}

static long dedup_k;   /* the tokens taken in every order */
static TcShortcuts shortcuts = TC_SHORTCUTS_TAKEN;

static void dedup(Text *t, long n) {
    put(t, "(rule take (take (token $x)) (make (done $x)))\n");
    for (long k = 0; k < dedup_k; k++) putf(t, "(token t%ld)\n", k, 0);
    untouched(t, n);
}

/* Join plans: a keyed chain written in the order of a cross product. */
static void keyed(Text *t, long n) {
    if (degraded) put(t, "(rule join (read (A $x) (C $y) (B $u $v)) (check (= $x $u) (= $y $v)) (make (D $x $y)))\n");
    else put(t, "(rule join (read (A $x) (C $y) (B $x $y)) (make (D $x $y)))\n");
    for (long i = 0; i < n; i++) {
        putf(t, "(A %ld)\n(C %ld)\n", i, i + n);
        putf(t, "(B %ld %ld)\n", i, i + n);
    }
}

/* A guard on the first pattern's value, written after the pattern that does
 * not bind it; degraded, the guard reads both. */
static void guard(Text *t, long n) {
    if (degraded) put(t, "(rule g (read (B $y) (A $x)) (check (< (+ $x $y) -1)) (make (D $x $y)))\n");
    else put(t, "(rule g (read (B $y) (A $x)) (check (< $x 0)) (make (D $x $y)))\n");
    put(t, "(A -1)\n");
    for (long i = 0; i < n; i++) putf(t, "(A %ld)\n(B %ld)\n", i, i);
}

/* k rules searching one relation R of seven arguments: rule i by its first i
 * arguments, or (degraded) by argument i alone. */
static long index_k;

static void indexes(Text *t, long n) {
    static const char *const at[7] = {"$a", "$b", "$c", "$d", "$e", "$f", "$g"};
    for (long i = 1; i <= index_k; i++) {
        char line[512];
        int w = snprintf(line, sizeof line, "(rule s%ld (read (Q%ld", i, i);
        for (long k = 0; k < (degraded ? 1 : i); k++)
            w += snprintf(line + w, sizeof line - (size_t)w, " %s", at[degraded ? i - 1 : k]);
        w += snprintf(line + w, sizeof line - (size_t)w, ") (R");
        for (long k = 0; k < 7; k++) w += snprintf(line + w, sizeof line - (size_t)w, " %s", at[k]);
        snprintf(line + w, sizeof line - (size_t)w, ")) (make (S%ld $g)))\n", i);
        put(t, line);
        putf(t, "(Q%ld", i, 0);
        for (long k = 0; k < (degraded ? 1 : i); k++) put(t, " 0");
        put(t, ")\n");
    }
    for (long r = 0; r < n; r++) putf(t, "(R 0 0 0 0 0 0 %ld)\n", r, 0);
}

typedef struct {
    uint64_t unifications, met, stored, states, tried, identity, indexes;
    size_t rows, first;   /* the outcome's rows; the first state's */
} Work;

static Work run(void (*make)(Text *, long), long n, TcStep step, TcStore store) {
    Text t = {0};
    make(&t, n);
    TcTerms *terms = tc_terms_new();
    TcDeclarations d;
    TcSyntaxError e;
    TcProgram *p = terms ? tc_read_program(terms, t.text, t.len, &d, &e) : NULL;
    if (!p) { fprintf(stderr, "FAIL: a growth program is refused: %s\n", e.message); exit(1); }
    TcInterpretation how = {.step = step, .ask = TC_ASK_AFTER_TELLS, .store = store,
                            .matches = TC_MATCHES_SYMMETRIC, .budget = UINT64_MAX,
                            .backend = TC_BACKEND_C, .observe = TC_OBSERVE_OUTCOMES,
                            .shortcuts = shortcuts};
    TcOutcomes *o = tc_explore(p, &how);
    if (!o || !tc_outcomes_finished(o) || !tc_outcomes_complete_memory(o) || tc_outcomes_count(o) != 1) {
        fprintf(stderr, "FAIL: a growth program did not reach its one outcome\n");
        exit(1);
    }
    Work w = {tc_outcomes_unifications(o), tc_outcomes_rows_met(o), tc_outcomes_rows_stored(o),
              tc_outcomes_states(o), tc_outcomes_patterns_tried(o), tc_outcomes_identity_work(o),
              tc_outcomes_indexes(o), 0, 0};
    tc_outcome_rows(o, 0, &w.rows);
    tc_program_rows(p, &w.first);
    tc_outcomes_free(o);
    tc_program_free(p);
    tc_terms_free(terms);
    free(d.region);
    free(t.text);
    return w;
}

static int failures;

/* One gate: the ratio of a count at 2n to its count at n.  A total linear in
 * n doubles (below 3; quadratic quadruples); a count per unit of output that
 * must stay flat stays near 1 (below 1.5; one linear in n doubles). */
static void gate(const char *name, const char *count, double at_n, double at_2n, double below) {
    double ratio = at_2n / (at_n > 0 ? at_n : 1);
    bool ok = ratio < below;
    printf("%s %s: %s ratio %.2f when doubled\n", ok ? "PASS" : "FAIL", name, count, ratio);
    if (!ok) failures++;
}

/* Rows held per state, beyond the rows of the first state. */
static double held_per_state(Work w) { return ((double)w.stored - (double)w.first) / (double)w.states; }
static double rows_per_state(Work w) { return (double)w.stored / (double)w.states; }
static double identity_per_state(Work w) { return (double)w.identity / (double)w.states; }

/* A gate of step one: the count per state at n and at 2n, flat (below 1.5)
 * for the program, and its degraded variant's growing (1.5 or more). */
static void branching_gate(const char *name, const char *count, void (*make)(Text *, long), long n,
                           double (*per_state)(Work), uint64_t states_at_n, uint64_t states_at_2n,
                           const char *flat, const char *grows) {
    double ratio[2];
    for (int d = 0; d < 2; d++) {
        degraded = d == 1;
        Work a = run(make, n, TC_STEP_ONE, TC_STORE_MULTISET), b = run(make, 2 * n, TC_STEP_ONE, TC_STORE_MULTISET);
        if ((states_at_n && a.states != states_at_n) || (states_at_2n && b.states != states_at_2n)) {
            printf("FAIL %s%s: %llu and %llu states, not %llu and %llu\n", name, degraded ? " (degraded)" : "",
                   (unsigned long long)a.states, (unsigned long long)b.states,
                   (unsigned long long)states_at_n, (unsigned long long)states_at_2n);
            failures++;
            return;
        }
        ratio[d] = per_state(b) / (per_state(a) > 0 ? per_state(a) : 1);
    }
    degraded = false;
    bool ok = ratio[0] < 1.5, sees = ratio[1] >= 1.5;
    printf("%s %s: %s ratio %.2f at 2n over n (%s); degraded variant %.2f (%s)\n", ok && sees ? "PASS" : "FAIL",
           name, count, ratio[0], ok ? flat : grows, ratio[1], sees ? grows : "the gate does not see it");
    if (!ok || !sees) failures++;
}

/* A gate of the join plans, under step every over a set store: a count at
 * two sizes (a and b), flat (below 1.5) for the program and growing (1.5 or
 * more) for its degraded variant. */
static void plan_gate(const char *name, const char *count, void (*make)(Text *, long), long a, long b,
                      double (*measure)(Work, long), const char *flat, const char *grows) {
    double ratio[2];
    for (int d = 0; d < 2; d++) {
        degraded = d == 1;
        Work wa = run(make, a, TC_STEP_EVERY, TC_STORE_SET), wb = run(make, b, TC_STEP_EVERY, TC_STORE_SET);
        ratio[d] = measure(wb, b) / (measure(wa, a) > 0 ? measure(wa, a) : 1);
    }
    degraded = false;
    bool ok = ratio[0] < 1.5, sees = ratio[1] >= 1.5;
    printf("%s %s: %s ratio %.2f at the larger size (%s); degraded variant %.2f (%s)\n", ok && sees ? "PASS" : "FAIL",
           name, count, ratio[0], ok ? flat : grows, ratio[1], sees ? grows : "the gate does not see it");
    if (!ok || !sees) failures++;
}

static double unifications_per_derived(Work w, long n) {
    (void)n;
    return (double)w.unifications / (double)(w.rows > w.first ? w.rows - w.first : 1);
}
static double unifications_per_row(Work w, long n) { return (double)w.unifications / (double)n; }
static double indexes_held(Work w, long n) { (void)n; return (double)w.indexes; }

static void index_gate(void) {
    double ratio[2];
    for (int d = 0; d < 2; d++) {
        degraded = d == 1;
        index_k = 3;
        Work wa = run(indexes, 50, TC_STEP_EVERY, TC_STORE_SET);
        index_k = 6;
        Work wb = run(indexes, 50, TC_STEP_EVERY, TC_STORE_SET);
        ratio[d] = indexes_held(wb, 0) / (indexes_held(wa, 0) > 0 ? indexes_held(wa, 0) : 1);
    }
    degraded = false;
    bool ok = ratio[0] < 1.5, sees = ratio[1] >= 1.5;
    printf("%s indexes: indexes held ratio %.2f at 2k searches over k (%s); degraded variant %.2f (%s)\n",
           ok && sees ? "PASS" : "FAIL", ratio[0], ok ? "prefix searches share one index" : "an index a search",
           ratio[1], sees ? "an index a search" : "the gate does not see it");
    if (!ok || !sees) failures++;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "step-one") == 0) {
        branching_gate("step-one", "rows held per state", step_one, 400, held_per_state, 4, 4,
                       "the store is shared by the states", "every state holds a copy of the store");
        branching_gate("takes", "rows held per state", takes, 300, held_per_state, 301, 601,
                       "a step copies only the paths it changes", "every state holds a copy of the store");
        branching_gate("bag", "rows held per state", bag, 300, rows_per_state, 301, 601,
                       "copies are counts", "every state holds its copies");
        dedup_k = 6;
        shortcuts = TC_SHORTCUTS_NONE;
        branching_gate("dedup", "identity work per state", dedup, 300, identity_per_state, 64, 64,
                       "shared nodes are not compared", "deciding identity reads the store");
        shortcuts = TC_SHORTCUTS_TAKEN;
        return failures ? 1 : 0;
    }
    long n = 400;
    Work a = run(chain, n, TC_STEP_EVERY, TC_STORE_SET), b = run(chain, 2 * n, TC_STEP_EVERY, TC_STORE_SET);
    if (a.states != (uint64_t)n + 1 || b.states != (uint64_t)(2 * n) + 1) {
        fprintf(stderr, "FAIL: chain: a step a row\n");
        return 1;
    }
    gate("chain", "unifications", (double)a.unifications, (double)b.unifications, 3.0);
    gate("chain", "rows met", (double)a.met, (double)b.met, 3.0);
    gate("chain", "rows held", (double)a.stored, (double)b.stored, 3.0);

    n = 60;
    a = run(closure, n, TC_STEP_EVERY, TC_STORE_SET);
    b = run(closure, 2 * n, TC_STEP_EVERY, TC_STORE_SET);
    /* per row derived: the closure derives n(n+1)/2 rows */
    gate("closure", "unifications per row derived", (double)a.unifications / (double)a.rows,
         (double)b.unifications / (double)b.rows, 1.5);
    gate("closure", "rows met per row derived", (double)a.met / (double)a.rows,
         (double)b.met / (double)b.rows, 1.5);

    n = 300;
    a = run(demand, n, TC_STEP_EVERY, TC_STORE_SET);
    b = run(demand, 2 * n, TC_STEP_EVERY, TC_STORE_SET);
    gate("demand", "unifications", (double)a.unifications, (double)b.unifications, 3.0);
    gate("demand", "rows met", (double)a.met, (double)b.met, 3.0);

    joins_k = 50;
    a = run(joins, 200, TC_STEP_EVERY, TC_STORE_SET);
    joins_k = 100;
    b = run(joins, 200, TC_STEP_EVERY, TC_STORE_SET);
    gate("joins", "rule patterns tried, at 2k relations over k", (double)a.tried, (double)b.tried, 1.5);

    plan_gate("keyed", "unifications per row derived", keyed, 200, 400, unifications_per_derived,
              "each pattern after the first met by a key", "a cross product");
    plan_gate("guard", "unifications per row of A", guard, 200, 400, unifications_per_row,
              "the guard decided after the first pattern", "the guard decided after the last");
    index_gate();
    return failures ? 1 : 0;
}
