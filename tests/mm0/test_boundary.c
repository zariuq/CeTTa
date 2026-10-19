/* test_boundary.c - the three boundaries of the semantic route repaired
 * after review (mm0-review/REVIEW_FOR_CLAUDE.md):
 *
 *   1. ownership: a conversion is an identifier its builder issued, and an
 *      expression belongs to its builder; forged, foreign and guessed
 *      values are refused and admit nothing;
 *   2. steps: every proof row and conversion witness is charged, with exact
 *      per-row costs, exact budget boundaries, monotonicity, interruption on
 *      long streams, resumption with a new budget and an unlimited default;
 *   3. admission stays atomic when the budget runs out inside it.
 *
 *   test_boundary
 *
 * Prints (Mm0BoundarySummary CHECKS FAILURES). */
#include "mm0.h"
#include "mm0_semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_checks, g_failures;

#define CHECK(cond, what) do { \
        ++g_checks; \
        if (!(cond)) { ++g_failures; printf("FAIL %s (line %d)\n", (what), __LINE__); } \
    } while (0)

static Mm0Status status(const Mm0Decl *d) { return mm0_decl_error(d)->status; }

/* wff: strict provable.  P, Q: nullary terms.  axP: P.  im: wff -> wff -> wff
 * with axioms mp (a, a -> b |- b) and id (|- a -> a). */
typedef struct {
    Mm0Theory *t;
    uint32_t wff, P, Q, im, axP, mp, id;
} Fix;

static uint32_t term0(Mm0Theory *t, const char *name) {
    uint32_t index = 0;
    Mm0Decl *d = mm0_decl_new(t, NULL);
    if (mm0_decl_finish_term(d, name, 0, NULL, 0, &index) != MM0_OK) abort();
    mm0_decl_free(d);
    return index;
}

static Fix fixture(void) {
    Fix f = {mm0_theory_new(), 0, 0, 0, 0, 0, 0, 0};
    Mm0Error e;
    if (mm0_add_sort(f.t, "wff", 6 /* strict provable */, &e) != MM0_OK) abort();
    f.P = term0(f.t, "P");
    f.Q = term0(f.t, "Q");
    Mm0Decl *d = mm0_decl_new(f.t, NULL);
    mm0_decl_binder(d, "a", f.wff, false, NULL, 0);
    mm0_decl_binder(d, "b", f.wff, false, NULL, 0);
    if (mm0_decl_finish_term(d, "im", f.wff, NULL, 0, &f.im) != MM0_OK) abort();
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    if (mm0_decl_concl(d, mm0_decl_app(d, f.P, NULL, 0)) != MM0_OK ||
        mm0_decl_finish_axiom(d, "axP", &f.axP) != MM0_OK) abort();
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    mm0_decl_binder(d, "a", f.wff, false, NULL, 0);
    mm0_decl_binder(d, "b", f.wff, false, NULL, 0);
    Mm0Node *a = mm0_decl_var(d, 0), *b = mm0_decl_var(d, 1);
    Mm0Node *ab[2] = {a, b};
    mm0_decl_hyp(d, a);
    mm0_decl_hyp(d, mm0_decl_app(d, f.im, ab, 2));
    mm0_decl_concl(d, b);
    if (mm0_decl_finish_axiom(d, "mp", &f.mp) != MM0_OK) abort();
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    mm0_decl_binder(d, "a", f.wff, false, NULL, 0);
    a = mm0_decl_var(d, 0);
    Mm0Node *aa[2] = {a, a};
    mm0_decl_concl(d, mm0_decl_app(d, f.im, aa, 2));
    if (mm0_decl_finish_axiom(d, "id", &f.id) != MM0_OK) abort();
    mm0_decl_free(d);
    return f;
}

static size_t theorems(const Fix *f) { return mm0_theory_stats(f->t).theorems; }

/* A builder for the theorem |- Q whose first row proves P (by axP). */
static Mm0Decl *q_from_p(const Fix *f, const Mm0Options *o, uint32_t *row0, Mm0Node **pn) {
    Mm0Decl *d = mm0_decl_new(f->t, o);
    *pn = mm0_decl_app(d, f->P, NULL, 0);
    mm0_decl_concl(d, mm0_decl_app(d, f->Q, NULL, 0));
    mm0_row_thm(d, f->axP, NULL, 0, NULL, 0, NULL, row0);
    return d;
}

/* ---- 1. ownership ------------------------------------------------------- */

static void forged_conversions(void) {
    Fix f = fixture();
    size_t before = theorems(&f);

    /* The reviewer's control: a conversion P = Q that no constructor
     * checked.  Its representation is now an identifier, so the forger can
     * only write numbers: none of them names a P = Q conversion. */
    unsigned refused = 0, attempts = 0;
    for (uint64_t issuer = 0; issuer < 600; ++issuer) {
        for (uint32_t index = 0; index < 3; ++index) {
            uint32_t row0, row1, th;
            Mm0Node *pn;
            Mm0Decl *d = q_from_p(&f, NULL, &row0, &pn);
            /* the builder also issued one real conversion, P = P */
            Mm0Conv real;
            if (!mm0_conv_refl(d, pn, &real)) abort();
            Mm0Conv guess = {issuer, index};
            ++attempts;
            Mm0Status row = mm0_row_conv(d, &guess, row0, NULL, &row1);
            Mm0Status fin = mm0_decl_finish_theorem(d, "Q_forged", row == MM0_OK ? row1 : row0, &th);
            if (fin != MM0_OK) ++refused;
            mm0_decl_free(d);
        }
    }
    CHECK(refused == attempts, "no guessed conversion identifier admits Q");
    CHECK(theorems(&f) == before, "the theory gained no theorem from guessed conversions");

    /* A conversion of another builder. */
    uint32_t row0, row1, th;
    Mm0Node *pn;
    Mm0Decl *other = mm0_decl_new(f.t, NULL);
    Mm0Node *opn = mm0_decl_app(other, f.P, NULL, 0);
    Mm0Conv theirs;
    CHECK(mm0_conv_refl(other, opn, &theirs), "another builder issues P = P");
    Mm0Decl *d = q_from_p(&f, NULL, &row0, &pn);
    CHECK(mm0_row_conv(d, &theirs, row0, NULL, &row1) == MM0_FOREIGN,
          "a conversion of another builder is foreign");
    CHECK(mm0_decl_finish_theorem(d, "Q_foreign", row0, &th) != MM0_OK, "nothing is admitted");
    mm0_decl_free(d);
    CHECK(mm0_conv_left(other, theirs) == opn && mm0_conv_right(other, theirs) == opn,
          "its issuer reads its sides");

    /* Its own conversions are accepted, but only for what they prove. */
    d = q_from_p(&f, NULL, &row0, &pn);
    CHECK(mm0_conv_left(d, theirs) == NULL, "another builder reads no sides of it");
    Mm0Conv mine;
    CHECK(mm0_conv_refl(d, pn, &mine) && mm0_row_conv(d, &mine, row0, NULL, &row1) == MM0_OK,
          "its own P = P converts the P row");
    CHECK(mm0_decl_finish_theorem(d, "Q_by_refl", row1, &th) == MM0_REFUSED,
          "and the P row does not prove Q");
    mm0_decl_free(d);

    /* Neighbouring identifiers of a real one. */
    d = q_from_p(&f, NULL, &row0, &pn);
    CHECK(mm0_conv_refl(d, pn, &mine), "refl P");
    Mm0Conv next = {mine.issuer, mine.index + 1}, prev_builder = {mine.issuer - 1, mine.index};
    CHECK(mm0_row_conv(d, &next, row0, NULL, &row1) == MM0_FOREIGN, "an index not yet issued");
    mm0_decl_free(d);
    d = q_from_p(&f, NULL, &row0, &pn);
    CHECK(mm0_row_conv(d, &prev_builder, row0, NULL, &row1) == MM0_FOREIGN,
          "the identifier of an earlier builder");
    mm0_decl_free(d);
    d = q_from_p(&f, NULL, &row0, &pn);
    Mm0Conv zero = {0, 0};
    CHECK(mm0_row_conv(d, &zero, row0, NULL, &row1) == MM0_FOREIGN, "the zero value");
    mm0_decl_free(d);

    /* Witness constructors refuse foreign inputs the same way. */
    d = q_from_p(&f, NULL, &row0, &pn);
    Mm0Conv out;
    CHECK(!mm0_conv_symm(d, &theirs, &out) && status(d) == MM0_FOREIGN, "symm of a foreign conversion");
    mm0_decl_free(d);
    d = q_from_p(&f, NULL, &row0, &pn);
    CHECK(mm0_conv_refl(d, pn, &mine), "refl P");
    CHECK(!mm0_conv_trans(d, &mine, &theirs, &out) && status(d) == MM0_FOREIGN,
          "trans with a foreign conversion");
    mm0_decl_free(d);
    d = q_from_p(&f, NULL, &row0, &pn);
    Mm0Node *qn = mm0_decl_app(d, f.Q, NULL, 0);
    Mm0Conv parts[2];
    CHECK(mm0_conv_refl(d, qn, &parts[0]), "refl Q");
    parts[1] = theirs;
    CHECK(!mm0_conv_cong(d, f.im, parts, 2, &out) && status(d) == MM0_FOREIGN,
          "congruence over a foreign conversion");
    mm0_decl_free(d);
    CHECK(theorems(&f) == before, "the theory is unchanged");
    mm0_decl_free(other);
    mm0_theory_free(f.t);
}

static void foreign_expressions(void) {
    Fix f = fixture();
    size_t before = theorems(&f);
    Mm0Decl *other = mm0_decl_new(f.t, NULL);
    mm0_decl_binder(other, "a", f.wff, false, NULL, 0);
    Mm0Node *alien = mm0_decl_var(other, 0);
    Mm0Node *alien_q = mm0_decl_app(other, f.Q, NULL, 0);
    Mm0Node *alien_pair[2] = {alien, alien};
    Mm0Conv c;
    uint32_t row, th;

    Mm0Decl *d = mm0_decl_new(f.t, NULL);
    CHECK(mm0_decl_app(d, f.im, alien_pair, 2) == NULL && status(d) == MM0_FOREIGN,
          "an application to another builder's expressions");
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    CHECK(mm0_decl_concl(d, alien_q) == MM0_FOREIGN, "another builder's conclusion");
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    CHECK(mm0_decl_hyp(d, alien_q) == MM0_FOREIGN, "another builder's hypothesis");
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    CHECK(!mm0_conv_refl(d, alien_q, &c) && status(d) == MM0_FOREIGN, "refl of another builder's expression");
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    mm0_decl_concl(d, mm0_decl_app(d, f.P, NULL, 0));
    CHECK(mm0_row_thm(d, f.id, &alien_q, 1, NULL, 0, NULL, &row) == MM0_FOREIGN,
          "a theorem argument of another builder");
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    mm0_decl_concl(d, mm0_decl_app(d, f.P, NULL, 0));
    Mm0Node *alien_p = mm0_decl_app(other, f.P, NULL, 0);
    CHECK(mm0_row_thm(d, f.axP, NULL, 0, NULL, 0, alien_p, &row) == MM0_FOREIGN,
          "a claim of another builder");
    CHECK(mm0_decl_finish_theorem(d, "P_alien", 0, &th) != MM0_OK, "nothing is admitted");
    mm0_decl_free(d);
    d = mm0_decl_new(f.t, NULL);
    CHECK(mm0_decl_finish_def(d, "Pd", f.wff, NULL, 0, alien_p, &th) == MM0_FOREIGN,
          "a definition body of another builder");
    mm0_decl_free(d);
    CHECK(theorems(&f) == before, "the theory is unchanged");
    mm0_decl_free(other);
    mm0_theory_free(f.t);
}

/* ---- 2. steps ----------------------------------------------------------- */

/* Rows of one kind, as many as possible under `budget`, up to `max`. */
typedef enum { HYP_ROWS, CONV_ROWS, THM_ROWS } RowKind;

static uint32_t rows_under(const Fix *f, RowKind kind, uint64_t budget, uint32_t max,
                           Mm0Status *st) {
    Mm0Options o;
    mm0_options_init(&o);
    o.budget = budget;
    Mm0Decl *d = mm0_decl_new(f->t, &o);
    mm0_decl_binder(d, "a", f->wff, false, NULL, 0);
    Mm0Node *a = mm0_decl_var(d, 0);
    mm0_decl_hyp(d, a);
    mm0_decl_concl(d, a);
    uint32_t done = 0, row;
    while (done < max && status(d) == MM0_OK) {
        if (kind == HYP_ROWS) {
            if (mm0_row_hyp(d, 0, NULL, &row) != MM0_OK) break;
        } else if (kind == CONV_ROWS) {
            Mm0Conv c;
            if (done == 0 && mm0_row_hyp(d, 0, NULL, &row) != MM0_OK) break;
            if (!mm0_conv_refl(d, a, &c) || mm0_row_conv(d, &c, 0, NULL, &row) != MM0_OK) break;
        } else {
            if (mm0_row_thm(d, f->id, &a, 1, NULL, 0, NULL, &row) != MM0_OK) break;
        }
        ++done;
    }
    *st = status(d);
    mm0_decl_free(d);
    return done;
}

/* The least budget under which `n` rows of a kind all check. */
static uint64_t least_budget(const Fix *f, RowKind kind, uint32_t n) {
    uint64_t lo = 0, hi = 1;
    Mm0Status st;
    while (rows_under(f, kind, hi, n, &st) < n) hi *= 2;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (rows_under(f, kind, mid, n, &st) >= n) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

static void row_budgets(void) {
    Fix f = fixture();
    static const char *names[] = {"hypothesis", "conversion", "theorem-application"};
    for (int k = 0; k < 3; ++k) {
        RowKind kind = (RowKind)k;
        char what[160];
        uint64_t b10 = least_budget(&f, kind, 10), b11 = least_budget(&f, kind, 11),
                 b500 = least_budget(&f, kind, 500);
        uint64_t per = b11 - b10;
        snprintf(what, sizeof what, "%s rows cost a fixed positive number of steps each", names[k]);
        CHECK(per >= 1 && b500 == b10 + 490 * per, what);
        Mm0Status st;
        snprintf(what, sizeof what, "%s rows: the exact boundary admits all and one less refuses", names[k]);
        CHECK(rows_under(&f, kind, b500, 500, &st) == 500 && st == MM0_OK &&
              rows_under(&f, kind, b500 - 1, 500, &st) == 499 && st == MM0_LIMIT, what);
        bool monotone = true;
        for (uint64_t b = b500; b < b500 + 40; ++b)
            monotone = monotone && rows_under(&f, kind, b, 500, &st) == 500 && st == MM0_OK;
        for (uint64_t b = b500 > 40 ? b500 - 40 : 0; b < b500; ++b)
            monotone = monotone && rows_under(&f, kind, b, 500, &st) < 500 && st == MM0_LIMIT;
        snprintf(what, sizeof what, "%s rows: more budget never checks fewer rows", names[k]);
        CHECK(monotone, what);
    }
    /* The reviewer's control: budget 20, 100000 hypothesis rows. */
    Mm0Status st;
    uint32_t got = rows_under(&f, HYP_ROWS, 20, 100000, &st);
    CHECK(got < 20 && st == MM0_LIMIT, "budget 20 stops a stream of hypothesis rows");
    printf("(budget 20: %u hypothesis rows, then %s)\n", got, mm0_status_name(st));
    /* Unlimited by default. */
    got = rows_under(&f, HYP_ROWS, UINT64_MAX, 300000, &st);
    CHECK(got == 300000 && st == MM0_OK, "the default budget is unlimited");
    mm0_theory_free(f.t);
}

typedef struct { unsigned polls, stop_after; } Poller;
static bool poll_fn(void *ctx) {
    Poller *p = ctx;
    return ++p->polls > p->stop_after;
}

static void interruption(void) {
    Fix f = fixture();
    Poller p = {0, 3};
    Mm0Options o;
    mm0_options_init(&o);
    o.poll = poll_fn;
    o.poll_context = &p;
    Mm0Decl *d = mm0_decl_new(f.t, &o);
    mm0_decl_binder(d, "a", f.wff, false, NULL, 0);
    Mm0Node *a = mm0_decl_var(d, 0);
    mm0_decl_hyp(d, a);
    mm0_decl_concl(d, a);
    uint32_t rows = 0, row;
    while (rows < 1000000 && mm0_row_hyp(d, 0, NULL, &row) == MM0_OK) ++rows;
    CHECK(status(d) == MM0_INTERRUPTED, "a long stream of hypothesis rows is interrupted");
    CHECK(p.polls == 4 && rows > 3 * 4096 - 16 && rows < 4 * 4096, "polled every 4096 steps");
    printf("(interrupted after %u rows and %u polls)\n", rows, p.polls);
    /* Conversions alone (no rows) are charged and polled too. */
    Poller q = {0, 1};
    o.poll_context = &q;
    Mm0Decl *e = mm0_decl_new(f.t, &o);
    Mm0Node *pn = mm0_decl_app(e, f.P, NULL, 0);
    unsigned convs = 0;
    Mm0Conv c;
    while (convs < 1000000 && mm0_conv_refl(e, pn, &c)) ++convs;
    CHECK(mm0_decl_error(e)->status == MM0_INTERRUPTED && convs < 2 * 4096,
          "a long stream of conversion witnesses is interrupted");
    mm0_decl_free(e);

    /* Resume: the rows checked so far are kept and the proof finishes. */
    Mm0Options more;
    mm0_options_init(&more);
    CHECK(mm0_decl_resume(d, &more) == MM0_OK, "an interrupted builder resumes");
    CHECK(mm0_row_concl(d, rows - 1) == a, "its rows are retained");
    uint32_t th;
    CHECK(mm0_row_hyp(d, 0, NULL, &row) == MM0_OK && row == rows, "the stopped row is made again");
    size_t before = theorems(&f);
    CHECK(mm0_decl_finish_theorem(d, "a_a", row, &th) == MM0_OK && theorems(&f) == before + 1,
          "and the theorem is admitted");
    mm0_decl_free(d);
    mm0_theory_free(f.t);
}

static void resume(void) {
    Fix f = fixture();
    Mm0Options o;
    mm0_options_init(&o);
    o.budget = 30;
    Mm0Decl *d = mm0_decl_new(f.t, &o);
    mm0_decl_binder(d, "a", f.wff, false, NULL, 0);
    Mm0Node *a = mm0_decl_var(d, 0);
    mm0_decl_hyp(d, a);
    mm0_decl_concl(d, a);
    uint32_t rows = 0, row, th;
    while (mm0_row_hyp(d, 0, NULL, &row) == MM0_OK) ++rows;
    CHECK(status(d) == MM0_LIMIT && rows > 0, "the budget runs out");
    CHECK(mm0_row_hyp(d, 0, NULL, &row) == MM0_LIMIT, "and stays out until resumed");
    o.budget = 5;
    CHECK(mm0_decl_resume(d, &o) == MM0_OK, "resume with a small new budget");
    uint32_t more = 0;
    while (mm0_row_hyp(d, 0, NULL, &row) == MM0_OK) ++more;
    CHECK(more == 5 && status(d) == MM0_LIMIT, "the new budget is exactly 5 rows");
    mm0_options_init(&o);
    CHECK(mm0_decl_resume(d, &o) == MM0_OK && mm0_row_hyp(d, 0, NULL, &row) == MM0_OK &&
          row == rows + more, "resumed again, unlimited, the row numbering continues");
    CHECK(mm0_decl_finish_theorem(d, "resumed", row, &th) == MM0_OK, "the resumed proof is admitted");
    mm0_decl_free(d);

    /* A refusal is final. */
    d = mm0_decl_new(f.t, NULL);
    mm0_decl_concl(d, mm0_decl_app(d, f.P, NULL, 0));
    CHECK(mm0_row_hyp(d, 0, NULL, &row) == MM0_REFUSED, "no hypothesis 0");
    CHECK(mm0_decl_resume(d, NULL) == MM0_MALFORMED && status(d) == MM0_REFUSED,
          "a refused builder does not resume");
    mm0_decl_free(d);
    mm0_theory_free(f.t);
}

/* ---- 3. admission under exhaustion -------------------------------------- */

static void atomic_admission(void) {
    Fix f = fixture();
    /* A proof whose admission copies a large statement: the conclusion
     * im^n(P, P) built by nested application. */
    uint64_t lo = 0;
    for (uint64_t budget = 1;; ++budget) {
        Mm0Options o;
        mm0_options_init(&o);
        o.budget = budget;
        Mm0Decl *d = mm0_decl_new(f.t, &o);
        Mm0Node *e = mm0_decl_app(d, f.P, NULL, 0);
        for (int i = 0; e && i < 40; ++i) {
            Mm0Node *pair[2] = {e, e};
            e = mm0_decl_app(d, f.im, pair, 2);
        }
        if (e) mm0_decl_hyp(d, e);
        if (e) mm0_decl_concl(d, e);
        uint32_t row = 0, th;
        if (e) mm0_row_hyp(d, 0, NULL, &row);
        size_t before = theorems(&f);
        uint64_t bytes = mm0_theory_stats(f.t).bytes;
        Mm0Status st = mm0_decl_finish_theorem(d, NULL, row, &th);
        if (st == MM0_OK) {
            lo = budget;
            CHECK(theorems(&f) == before + 1, "the admitted theorem is in the theory");
            mm0_decl_free(d);
            break;
        }
        if (st != MM0_LIMIT) {
            CHECK(false, "only the budget stops this proof");
            mm0_decl_free(d);
            break;
        }
        if (theorems(&f) != before || mm0_theory_stats(f.t).bytes != bytes) {
            CHECK(false, "a stopped admission leaves the theory as it was");
            mm0_decl_free(d);
            break;
        }
        mm0_decl_free(d);
    }
    CHECK(lo > 0, "some budget admits the proof, every smaller one changes nothing");
    printf("(admission under every budget up to %llu: atomic)\n", (unsigned long long)lo);
    mm0_theory_free(f.t);
}

/* Finishing a theorem copies its statement into the theory: one step per
 * distinct node.  Measured on a builder that built everything first and
 * then resumed with a budget for the finish alone. */
static uint64_t finish_cost(const Fix *f, int depth) {
    for (uint64_t budget = 0;; ++budget) {
        Mm0Decl *d = mm0_decl_new(f->t, NULL);
        Mm0Node *e = mm0_decl_app(d, f->P, NULL, 0);
        for (int i = 0; i < depth; ++i) {
            Mm0Node *pair[2] = {e, e};
            e = mm0_decl_app(d, f->im, pair, 2);
        }
        uint32_t row, th;
        mm0_decl_hyp(d, e);
        mm0_decl_concl(d, e);
        mm0_row_hyp(d, 0, NULL, &row);
        Mm0Options o;
        mm0_options_init(&o);
        o.budget = budget;
        mm0_decl_resume(d, &o);
        size_t before = theorems(f);
        Mm0Status st = mm0_decl_finish_theorem(d, NULL, row, &th);
        mm0_decl_free(d);
        if (st == MM0_OK) return budget;
        if (st != MM0_LIMIT || theorems(f) != before) return UINT64_MAX;
    }
}

static void admission_charges(void) {
    Fix f = fixture();
    uint64_t c40 = finish_cost(&f, 40), c80 = finish_cost(&f, 80);
    CHECK(c40 != UINT64_MAX && c80 != UINT64_MAX && c80 - c40 == 40,
          "admission charges one step per distinct statement node");
    printf("(finishing: %llu steps at depth 40, %llu at depth 80)\n", (unsigned long long)c40,
           (unsigned long long)c80);
    mm0_theory_free(f.t);
}

int main(void) {
    forged_conversions();
    foreign_expressions();
    row_budgets();
    interruption();
    resume();
    atomic_admission();
    admission_charges();
    printf("Mm0BoundarySummary %u %u\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
