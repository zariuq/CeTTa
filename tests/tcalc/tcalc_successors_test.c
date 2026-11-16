/* tcalc_successors_test.c - tc_successors, one step of a discipline from a
 * given state: the states reached, a quiescent state, a set store's firing
 * that changes nothing, an ambiguous state under unique, identical rows that
 * reach one state, and a row that is not a row.  Then tc_quiescent, and
 * tc_canonical_key: one key for one state written two ways, two keys for
 * states that differ only in how their rows share an unknown. */
#include "tcalc_core.h"
#include "tcalc_syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what) {
    if (!ok) { printf("FAIL: tc_successors: %s\n", what); failures++; }
}

/* The program's successors from its own initial rows under `how`. */
static TcOutcomes *step_of(const char *text, TcInterpretation how, TcTerms **terms,
                           TcProgram **program, TcTerm *rows, size_t *n, size_t cap) {
    TcDeclarations d;
    TcSyntaxError e;
    *terms = tc_terms_new();
    *program = tc_read_program(*terms, text, strlen(text), &d, &e);
    free(d.region);
    if (!*program) { printf("FAIL: tc_successors: reading %s\n", text); failures++; return NULL; }
    /* the initial rows, read back as rows of a state */
    TcInterpretation run = how;
    run.observe = TC_OBSERVE_RUN;
    run.budget = 1;
    TcOutcomes *first = tc_explore(*program, &run);
    size_t count = 0;
    const TcTerm *r = first && tc_outcomes_count(first) ? tc_outcome_rows(first, 0, &count) : NULL;
    *n = count < cap ? count : cap;
    if (r) memcpy(rows, r, *n * sizeof *rows);
    tc_outcomes_free(first);
    return tc_successors(*program, &how, rows, *n);
}

static TcInterpretation interpretation(TcStep step, TcStore store) {
    return (TcInterpretation){.step = step, .ask = TC_ASK_AFTER_TELLS, .store = store,
                              .matches = TC_MATCHES_SYMMETRIC, .budget = 1000,
                              .backend = TC_BACKEND_C, .observe = TC_OBSERVE_OUTCOMES};
}

int main(void) {
    TcTerms *terms;
    TcProgram *program;
    TcTerm rows[16];
    size_t n;

    const char *race = "(token) (rule a (take (token)) (make (got a))) (rule b (take (token)) (make (got b)))";
    TcOutcomes *o = step_of(race, interpretation(TC_STEP_ONE, TC_STORE_MULTISET), &terms, &program, rows, &n, 16);
    check(o && tc_outcomes_finished(o) && tc_outcomes_count(o) == 2, "a race has two successors");
    if (o && tc_outcomes_count(o) == 2) {   /* each successor is quiescent */
        size_t m;
        const TcTerm *next = tc_outcome_rows(o, 0, &m);
        TcInterpretation one = interpretation(TC_STEP_ONE, TC_STORE_MULTISET);
        TcOutcomes *after = tc_successors(program, &one, next, m);
        check(after && tc_outcomes_finished(after) && tc_outcomes_count(after) == 0,
              "a quiescent state has no successor");
        tc_outcomes_free(after);
    }
    tc_outcomes_free(o);
    TcInterpretation unique = interpretation(TC_STEP_UNIQUE, TC_STORE_MULTISET);
    o = tc_successors(program, &unique, rows, n);
    check(o && tc_outcomes_count(o) == 0 && tc_outcomes_ambiguous(o) == 1,
          "under unique the race is ambiguous");
    tc_outcomes_free(o);
    TcInterpretation one = interpretation(TC_STEP_ONE, TC_STORE_MULTISET);
    check(tc_quiescent(program, &one, rows, n) == 0, "the race's state is not quiescent");
    check(tc_quiescent(program, &unique, rows, n) == 1, "an ambiguous state of unique has no step");
    TcTerm got[2] = {tc_symbol(terms, "got", 3), tc_symbol(terms, "a", 1)};
    TcTerm done = tc_expression(terms, got, 2);
    check(tc_quiescent(program, &one, &done, 1) == 1, "a state with no token is quiescent");
    TcTerm stray = tc_local(terms, 0);
    check(tc_quiescent(program, &one, &stray, 1) == -1, "a row with a local is no state");
    tc_program_free(program);
    tc_terms_free(terms);

    /* canonical keys: (p ?1) (q ?1) and (q ?0) (p ?0) are one state;
     * (p ?0) (q ?1) is another, its rows sharing no unknown */
    terms = tc_terms_new();
    TcTerm p = tc_symbol(terms, "p", 1), q = tc_symbol(terms, "q", 1);
    TcTerm u0 = tc_unknown(terms, 0), u1 = tc_unknown(terms, 1);
    TcTerm p1[2] = {p, u1}, q1[2] = {q, u1}, p0[2] = {p, u0}, q0[2] = {q, u0};
    TcTerm first[2] = {tc_expression(terms, p1, 2), tc_expression(terms, q1, 2)};
    TcTerm second[2] = {tc_expression(terms, q0, 2), tc_expression(terms, p0, 2)};
    TcTerm apart[2] = {tc_expression(terms, p0, 2), tc_expression(terms, q1, 2)};
    size_t ka, kb, kc;
    TcTerm *a = tc_canonical_key(terms, first, 2, TC_STORE_MULTISET, &ka);
    TcTerm *b = tc_canonical_key(terms, second, 2, TC_STORE_MULTISET, &kb);
    TcTerm *c = tc_canonical_key(terms, apart, 2, TC_STORE_MULTISET, &kc);
    check(a && b && ka == 2 && kb == 2 && memcmp(a, b, 2 * sizeof *a) == 0,
          "one state written two ways has one key");
    check(a && c && kc == 2 && memcmp(a, c, 2 * sizeof *a) != 0,
          "rows that share an unknown are not rows that do not");
    free(a); free(b); free(c);
    TcTerm twice[2] = {first[0], first[0]};
    size_t ks, km;
    TcTerm *s = tc_canonical_key(terms, twice, 2, TC_STORE_SET, &ks);
    TcTerm *m = tc_canonical_key(terms, twice, 2, TC_STORE_MULTISET, &km);
    check(s && m && ks == 1 && km == 2, "a set store keeps one of equal rows, a multiset both");
    free(s); free(m);
    tc_terms_free(terms);

    const char *again = "(p) (rule copy (read (p)) (make (p)))";
    o = step_of(again, interpretation(TC_STEP_ONE, TC_STORE_SET), &terms, &program, rows, &n, 16);
    check(o && tc_outcomes_count(o) == 0, "a set store's firing that changes nothing is no step");
    tc_outcomes_free(o);
    TcInterpretation multiset = interpretation(TC_STEP_ONE, TC_STORE_MULTISET);
    o = tc_successors(program, &multiset, rows, n);
    check(o && tc_outcomes_count(o) == 1, "under a multiset store the same firing is a step");
    tc_outcomes_free(o);
    TcTerm local = tc_local(terms, 0);
    check(tc_successors(program, &multiset, &local, 1) == NULL, "a row with a local is refused");
    tc_program_free(program);
    tc_terms_free(terms);

    const char *counter = "(at loop) (c) (c) (c) (rule decrement (take (at loop) (c)) (make (at add)))";
    o = step_of(counter, interpretation(TC_STEP_ONE, TC_STORE_MULTISET), &terms, &program, rows, &n, 16);
    check(o && tc_outcomes_count(o) == 1, "three identical rows: three matches, one successor");
    tc_outcomes_free(o);
    tc_program_free(program);
    tc_terms_free(terms);

    if (!failures)
        printf("PASS: tc_successors (one step from a given state): 7; tc_quiescent: 4; "
               "tc_canonical_key: 3\n");
    return failures ? 1 : 0;
}
