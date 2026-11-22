/* tcalc_decide_test.c - tc_decide, a firing decided over a host's store, with
 * a store of rows in an array: a match admitted and committed; a pattern that
 * does not meet its row; absence and counts read through the store's cursor;
 * takes that must be distinct occurrences; tells; and each way the host can
 * answer (its policy refuses, it fails after effects, its view is invalidated,
 * it cancels), with the store unchanged wherever the contract says so. */
#include "tcalc_core.h"
#include "tcalc_syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static void check(bool ok, const char *what) {
    checks++;
    if (!ok) { printf("FAIL: tc_decide: %s\n", what); failures++; }
}

/* ---- A store of rows in an array; an occurrence is a row's index ---------- */

typedef struct {
    TcTerm row[32];
    bool live[32];
    size_t n;
    TcCommitStatus answer;      /* what commit answers when not DONE */
    long invalidate_after;      /* next fails INVALIDATED after this many rows; -1 never */
    long budget;                /* spend cancels when it runs out; -1 never */
    int commits;
} Store;

typedef struct { Store *s; size_t i; long yielded; } Cursor;

static TcStoreStatus s_open(void *store, const TcTerms *t, TcTerm pattern, void **cursor) {
    (void)t; (void)pattern;   /* every live row: a superset is fine */
    Cursor *c = malloc(sizeof *c);
    if (!c) return TC_STORE_NO_MEMORY;
    *c = (Cursor){store, 0, 0};
    *cursor = c;
    return TC_STORE_ROW;
}

static TcStoreStatus s_next(void *cursor, TcOccurrence *occurrence) {
    Cursor *c = cursor;
    if (c->s->invalidate_after >= 0 && c->yielded >= c->s->invalidate_after) return TC_STORE_INVALIDATED;
    while (c->i < c->s->n && !c->s->live[c->i]) c->i++;
    if (c->i == c->s->n) return TC_STORE_END;
    *occurrence = c->i++;
    c->yielded++;
    return TC_STORE_ROW;
}

static void s_close(void *cursor) { free(cursor); }

static TcStoreStatus s_import(void *store, TcDecideScope *scope, TcTerms *t, TcOccurrence at, TcTerm *row) {
    (void)scope; (void)t;   /* ground rows: nothing to open apart */
    Store *s = store;
    if (at >= s->n || !s->live[at]) return TC_STORE_INVALIDATED;
    *row = s->row[at];
    return TC_STORE_ROW;
}

static TcCommitStatus s_commit(void *store, const TcTerms *t, const TcOccurrence *taken, size_t ntaken,
                               const TcTerm *made, size_t nmade) {
    (void)t;
    Store *s = store;
    if (s->answer != TC_COMMIT_DONE) return s->answer;
    if (s->n + nmade > 32) return TC_COMMIT_NO_MEMORY;
    s->commits++;
    for (size_t i = 0; i < nmade; i++) { s->row[s->n] = made[i]; s->live[s->n++] = true; }
    for (size_t i = 0; i < ntaken; i++) s->live[taken[i]] = false;
    return TC_COMMIT_DONE;
}

static int s_spend(void *store, uint64_t units) {
    Store *s = store;
    if (s->budget < 0) return 1;
    if ((uint64_t)s->budget < units) return 0;
    s->budget -= (long)units;
    return 1;
}

static TcStoreOps ops_of(Store *s) {
    return (TcStoreOps){s, s_open, s_next, s_close, s_import, s_commit, s_spend};
}

/* ---- Programs ---------------------------------------------------------------- */

/* The program of TEXT, and its initial rows as the store's rows. */
static TcProgram *program_of(const char *text, TcTerms **terms, Store *s) {
    TcDeclarations d;
    TcSyntaxError e;
    *terms = tc_terms_new();
    TcProgram *p = tc_read_program(*terms, text, strlen(text), &d, &e);
    free(d.region);
    if (!p) { printf("FAIL: tc_decide: reading %s\n", text); failures++; return NULL; }
    TcInterpretation run = {.step = TC_STEP_ONE, .ask = TC_ASK_AFTER_TELLS, .store = TC_STORE_MULTISET,
                            .matches = TC_MATCHES_SYMMETRIC, .budget = 1, .backend = TC_BACKEND_C,
                            .observe = TC_OBSERVE_RUN};
    TcOutcomes *first = tc_explore(p, &run);
    size_t count = 0;
    const TcTerm *r = first && tc_outcomes_count(first) ? tc_outcome_rows(first, 0, &count) : NULL;
    *s = (Store){.answer = TC_COMMIT_DONE, .invalidate_after = -1, .budget = -1};
    for (size_t i = 0; r && i < count && i < 32; i++) { s->row[i] = r[i]; s->live[i] = true; }
    s->n = count < 32 ? count : 32;
    tc_outcomes_free(first);
    return p;
}

/* The occurrence of the store's first live row printed as TEXT. */
static TcOccurrence find(const TcTerms *t, const Store *s, const char *text) {
    for (size_t i = 0; i < s->n; i++) {
        if (!s->live[i]) continue;
        char *buf = NULL; size_t len = 0;
        FILE *f = open_memstream(&buf, &len);
        if (!f) continue;
        tc_print(t, s->row[i], f);
        fclose(f);
        bool same = buf && strcmp(buf, text) == 0;
        free(buf);
        if (same) return i;
    }
    return 999;
}

static bool live_count(const Store *s, size_t expected) {
    size_t n = 0;
    for (size_t i = 0; i < s->n; i++) n += s->live[i];
    return n == expected;
}

static TcInterpretation how(void) {
    return (TcInterpretation){.step = TC_STEP_ONE, .ask = TC_ASK_AFTER_TELLS, .store = TC_STORE_MULTISET,
                              .matches = TC_MATCHES_SYMMETRIC, .budget = 1000, .backend = TC_BACKEND_C,
                              .observe = TC_OBSERVE_OUTCOMES};
}

static TcDecision decide(TcProgram *p, uint32_t rule, Store *s, const TcOccurrence *sel, size_t n,
                         TcTerms *t, size_t *nmade) {
    TcStoreOps ops = ops_of(s);
    TcInterpretation h = how();
    const TcTerm *made;
    return tc_decide(p, rule, &h, &ops, sel, n, t, &made, nmade);
}

int main(void) {
    TcTerms *t;
    Store s;
    size_t nmade;

    TcProgram *p = program_of("(token 1) (rule move (take (token $x)) (make (got $x)))", &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(token 1)")};
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_ADMITTED && nmade == 1,
              "a match is admitted and makes one row");
        check(s.commits == 1 && live_count(&s, 1) && find(t, &s, "(got 1)") != 999,
              "the host committed: the token taken, (got 1) made");
        tc_program_free(p); tc_terms_free(t);
    }

    p = program_of("(other 1) (rule move (take (token $x)) (make (got $x)))", &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(other 1)")};
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_DECLINED && s.commits == 0 && live_count(&s, 1),
              "a pattern that does not meet its row declines, the store unchanged");
        tc_program_free(p); tc_terms_free(t);
    }

    const char *absent = "(p 1) (q 1) (rule r (take (p $x)) (absent (q $x)) (make (r $x)))";
    p = program_of(absent, &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(p 1)")};
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_DECLINED && s.commits == 0,
              "absence read through the store: (q 1) is present, so it declines");
        tc_program_free(p); tc_terms_free(t);
    }
    p = program_of("(p 1) (q 2) (rule r (take (p $x)) (absent (q $x)) (make (r $x)))", &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(p 1)")};
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_ADMITTED, "absence holds when only (q 2) is present");
        tc_program_free(p); tc_terms_free(t);
    }

    p = program_of("(p 1) (q 1) (q 1) (rule c (read (p $x)) (count 2 (q $x)) (make (two $x)))", &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(p 1)")};
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_ADMITTED && live_count(&s, 4),
              "a count of two copies, read through the store; a read is not taken");
        tc_program_free(p); tc_terms_free(t);
    }
    p = program_of("(p 1) (q 1) (rule c (read (p $x)) (count 2 (q $x)) (make (two $x)))", &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(p 1)")};
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_DECLINED, "one copy is not a count of two");
        tc_program_free(p); tc_terms_free(t);
    }

    p = program_of("(a 1) (a 2) (rule pair (take (a $x) (a $y)) (make (b $x $y)))", &t, &s);
    if (p) {
        TcOccurrence same[2] = {find(t, &s, "(a 1)"), find(t, &s, "(a 1)")};
        check(decide(p, 0, &s, same, 2, t, &nmade) == TC_DECIDED_DECLINED && s.commits == 0,
              "two takes of one occurrence are not enabled");
        TcOccurrence two[2] = {find(t, &s, "(a 1)"), find(t, &s, "(a 2)")};
        check(decide(p, 0, &s, two, 2, t, &nmade) == TC_DECIDED_ADMITTED && find(t, &s, "(b 1 2)") != 999,
              "two distinct occurrences are taken");
        check(decide(p, 0, &s, two, 1, t, &nmade) == TC_DECIDED_DECLINED,
              "a selection must give one occurrence per pattern");
        tc_program_free(p); tc_terms_free(t);
    }

    p = program_of("(v 3) (v 4) (rule t (take (v $x)) (tell (= $x 3)) (make (w $x)))", &t, &s);
    if (p) {
        TcOccurrence four[1] = {find(t, &s, "(v 4)")};
        check(decide(p, 0, &s, four, 1, t, &nmade) == TC_DECIDED_DECLINED, "a tell that cannot hold declines");
        TcOccurrence three[1] = {find(t, &s, "(v 3)")};
        check(decide(p, 0, &s, three, 1, t, &nmade) == TC_DECIDED_ADMITTED && find(t, &s, "(w 3)") != 999,
              "a tell that holds is admitted");
        tc_program_free(p); tc_terms_free(t);
    }

    /* the host's answers */
    p = program_of("(token 1) (rule move (take (token $x)) (make (got $x)))", &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(token 1)")};
        s.answer = TC_COMMIT_REFUSED;
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_STORE_REFUSED && live_count(&s, 1),
              "the host's policy refuses: STORE_REFUSED, unchanged");
        s.answer = TC_COMMIT_HOST_ERROR;
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_HOST_ERROR, "the host fails after effects: HOST_ERROR");
        s.answer = TC_COMMIT_INTERRUPTED;
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_INTERRUPTED, "the host's commit is cancelled: INTERRUPTED");
        s.answer = TC_COMMIT_DONE;
        s.budget = 0;
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_INTERRUPTED && s.commits == 0 && live_count(&s, 1),
              "the host cancels before any work: INTERRUPTED, unchanged");
        tc_program_free(p); tc_terms_free(t);
    }
    p = program_of(absent, &t, &s);
    if (p) {
        TcOccurrence sel[1] = {find(t, &s, "(p 1)")};
        s.invalidate_after = 0;
        check(decide(p, 0, &s, sel, 1, t, &nmade) == TC_DECIDED_INVALIDATED && s.commits == 0 && live_count(&s, 2),
              "a check's cursor invalidated: INVALIDATED, never read as the end");
        tc_program_free(p); tc_terms_free(t);
    }

    if (failures) return 1;
    printf("PASS: tc_decide (a firing decided over a host's store): %d checks\n", checks);
    return 0;
}
