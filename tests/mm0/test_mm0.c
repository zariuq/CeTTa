/* Tests of the native MM0 core: MMB loading, the semantic route and
 * declaration admission.  Usage: test_mm0 HOL.MMB HOL.MM0 */
#include "mm0.h"
#include "mm0_internal.h"
#include "mm0_semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond, ...) do { \
    ++checks; \
    if (!(cond)) { ++failures; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) { buf[n] = 0; *len = (size_t)n; }
    return buf;
}

/* A poll function that asks to stop at its third call. */
static bool stop_after_three(void *context) {
    long *calls = context;
    return ++*calls >= 3;
}

/* ---- a small s-expression reader for test statements ---------------------
 * Symbols are variables of the builder; (f a b) applies term f.  Variable
 * names are the builder's binder names. */
typedef struct { Mm0Decl *d; Mm0Theory *th; const char **names; uint32_t nnames; } Reader;

static void skip(const char **p) { while (**p == ' ' || **p == '\n') ++*p; }

static Mm0Node *read_expr(Reader *r, const char **p) {
    skip(p);
    if (**p == '(') {
        ++*p;
        skip(p);
        const char *start = *p;
        while (**p && **p != ' ' && **p != ')' && **p != '(') ++*p;
        uint32_t term;
        if (!mm0_names_get(&r->th->term_names, start, (size_t)(*p - start), &term)) return NULL;
        Mm0Node *args[16];
        uint32_t n = 0;
        for (;;) {
            skip(p);
            if (**p == ')') { ++*p; break; }
            if (n == 16 || !**p) return NULL;
            if (!(args[n++] = read_expr(r, p))) return NULL;
        }
        return mm0_decl_app(r->d, term, args, n);
    }
    const char *start = *p;
    while (**p && **p != ' ' && **p != ')' && **p != '(' && **p != '\n') ++*p;
    for (uint32_t i = 0; i < r->nnames; ++i)
        if (strlen(r->names[i]) == (size_t)(*p - start) && !strncmp(r->names[i], start, (size_t)(*p - start)))
            return mm0_decl_var(r->d, i);
    return NULL;
}

static Mm0Node *expr(Reader *r, const char *text) {
    const char *p = text;
    return read_expr(r, &p);
}

static uint32_t sort_of(Mm0Theory *th, const char *name) {
    uint32_t s = UINT32_MAX;
    mm0_names_get(&th->sort_names, name, strlen(name), &s);
    return s;
}
static uint32_t term_of(Mm0Theory *th, const char *name) {
    uint32_t t = UINT32_MAX;
    mm0_names_get(&th->term_names, name, strlen(name), &t);
    return t;
}
static uint32_t thm_of(Mm0Theory *th, const char *name) {
    uint32_t t = UINT32_MAX;
    mm0_names_get(&th->thm_names, name, strlen(name), &t);
    return t;
}

/* ---- MMB loading --------------------------------------------------------- */

static Mm0Theory *load_hol(const char *mmb_path, const char *spec_path) {
    size_t mlen, slen;
    unsigned char *mmb = slurp(mmb_path, &mlen), *spec = slurp(spec_path, &slen);
    CHECK(mmb && spec, "read hol files");
    if (!mmb || !spec) exit(2);
    Mm0Theory *th = mm0_theory_new();
    Mm0Error err;
    Mm0Status st = mm0_load_mmb(th, mmb, mlen, (const char *)spec, slen, NULL, &err);
    CHECK(st == MM0_OK, "hol loads: %s %s", mm0_status_name(st), err.reason ? err.reason : "");
    Mm0TheoryStats stats = mm0_theory_stats(th);
    CHECK(stats.sorts == 3 && stats.terms == 34 && stats.theorems == 127, "hol table sizes");
    CHECK(thm_of(th, "syl") != UINT32_MAX && thm_of(th, "TRUTH") != UINT32_MAX, "spec names retained");

    /* a loaded theory is not reloaded */
    st = mm0_load_mmb(th, mmb, mlen, (const char *)spec, slen, NULL, &err);
    CHECK(st == MM0_REFUSED, "second load into a nonempty theory is refused");
    CHECK(mm0_theory_stats(th).theorems == 127, "refused load leaves the theory");

    /* budgets and interrupts stop the load and leave the target empty */
    Mm0Options opt;
    mm0_options_init(&opt);
    opt.budget = 1000;
    Mm0Theory *limited = mm0_theory_new();
    st = mm0_load_mmb(limited, mmb, mlen, (const char *)spec, slen, &opt, &err);
    CHECK(st == MM0_LIMIT, "budget stops the load");
    CHECK(mm0_theory_stats(limited).sorts == 0 && limited->norder == 0, "limited load is all or nothing");
    mm0_theory_free(limited);
    /* the caller's poll function interrupts (it runs once per window of
     * steps): INTERRUPTED, distinct from LIMIT, and nothing retained */
    long polls = 0;
    mm0_options_init(&opt);
    opt.poll = stop_after_three;
    opt.poll_context = &polls;
    Mm0Theory *stopped = mm0_theory_new();
    st = mm0_load_mmb(stopped, mmb, mlen, (const char *)spec, slen, &opt, &err);
    CHECK(st == MM0_INTERRUPTED && polls == 3, "the poll function interrupts the load (%s, %ld polls)",
          mm0_status_name(st), polls);
    CHECK(mm0_theory_stats(stopped).sorts == 0 && stopped->norder == 0, "an interrupted load is all or nothing");
    CHECK(mm0_load_mmb(stopped, mmb, mlen, (const char *)spec, slen, NULL, &err) == MM0_OK,
          "the theory loads after the interruption");
    mm0_theory_free(stopped);

    /* truncation before the proof stream's final END: never accepted.
     * (What follows it is the index, which is advisory.) */
    size_t end = mm0_le32(mmb + 24);
    while (end < mlen && mmb[end] != 0) end += mmb_cmd(mmb + end).data;
    CHECK(end < mlen, "found the end of the proof stream");
    int accepted = 0;
    for (size_t cut = 0; cut <= end; cut += 97) {
        Mm0Theory *t = mm0_theory_new();
        if (mm0_load_mmb(t, mmb, cut, (const char *)spec, slen, NULL, &err) == MM0_OK) ++accepted;
        mm0_theory_free(t);
    }
    CHECK(accepted == 0, "no truncated file verifies");

    /* a specification that stops early, or continues past the file */
    Mm0Theory *t = mm0_theory_new();
    st = mm0_load_mmb(t, mmb, mlen, (const char *)spec, slen / 2, NULL, &err);
    CHECK(st == MM0_REFUSED && err.line > 0, "short specification refused with a position");
    mm0_theory_free(t);
    size_t extra_len = slen + 64;
    char *extra = malloc(extra_len + 1);
    memcpy(extra, spec, slen);
    strcpy(extra + slen, "\ntheorem extra (P: wff): $ P |- P $;\n");
    t = mm0_theory_new();
    st = mm0_load_mmb(t, mmb, mlen, extra, strlen(extra), NULL, &err);
    CHECK(st == MM0_REFUSED, "specification beyond the file refused");
    mm0_theory_free(t);
    free(extra);

    free(mmb);
    free(spec);
    return th;
}

/* ---- the semantic route on hol ------------------------------------------- */

static void hol_semantic(Mm0Theory *th) {
    uint32_t wff = sort_of(th, "wff"), term = sort_of(th, "term");
    size_t before = th->nthms;

    /* swap (P Q: wff): $ P; Q |- Q; P $  from ian, anr, anl */
    const char *names[] = {"P", "Q"};
    Mm0Decl *d = mm0_decl_new(th, NULL);
    mm0_decl_binder(d, "P", wff, false, NULL, 0);
    mm0_decl_binder(d, "Q", wff, false, NULL, 0);
    Reader r = {d, th, names, 2};
    mm0_decl_concl(d, expr(&r, "(im (an P Q) (an Q P))"));
    uint32_t r1, r2, r3, idx;
    Mm0Node *pq = expr(&r, "(an P Q)"), *P = mm0_decl_var(d, 0), *Q = mm0_decl_var(d, 1);
    Mm0Node *a_anr[] = {P, Q}, *a_anl[] = {P, Q}, *a_ian[] = {pq, Q, P};
    mm0_row_thm(d, thm_of(th, "anr"), a_anr, 2, NULL, 0, NULL, &r1);
    mm0_row_thm(d, thm_of(th, "anl"), a_anl, 2, NULL, 0, NULL, &r2);
    uint32_t parents[] = {r1, r2};
    mm0_row_thm(d, thm_of(th, "ian"), a_ian, 3, parents, 2, NULL, &r3);
    Mm0Status st = mm0_decl_finish_theorem(d, "swap", r3, &idx);
    CHECK(st == MM0_OK, "swap proved: %s", mm0_decl_error(d)->reason ? mm0_decl_error(d)->reason : "");
    CHECK(th->nthms == before + 1 && thm_of(th, "swap") == idx, "swap admitted under its name");
    mm0_decl_free(d);

    /* an interrupted declaration admits nothing */
    long polls = 0;
    Mm0Options stop;
    mm0_options_init(&stop);
    stop.poll = stop_after_three;
    stop.poll_context = &polls;
    d = mm0_decl_new(th, &stop);
    mm0_decl_binder(d, "P", wff, false, NULL, 0);
    mm0_decl_binder(d, "Q", wff, false, NULL, 0);
    r.d = d;
    mm0_decl_concl(d, expr(&r, "(im (an P Q) (an Q P))"));
    Mm0Node *s_pq[] = {mm0_decl_var(d, 0), mm0_decl_var(d, 1)};   /* nodes belong to their builder */
    for (int i = 0; i < 100000 && mm0_decl_error(d)->status == MM0_OK; ++i)
        mm0_row_thm(d, thm_of(th, "anr"), s_pq, 2, NULL, 0, NULL, &r1);
    CHECK(mm0_decl_error(d)->status == MM0_INTERRUPTED, "the poll function interrupts a declaration (%s)",
          mm0_status_name(mm0_decl_error(d)->status));
    CHECK(mm0_decl_finish_theorem(d, "swap_stopped", r1, &idx) == MM0_INTERRUPTED &&
          thm_of(th, "swap_stopped") == UINT32_MAX, "an interrupted declaration admits nothing");
    mm0_decl_free(d);

    /* parents in the wrong order are refused, and the theory is unchanged */
    d = mm0_decl_new(th, NULL);
    mm0_decl_binder(d, "P", wff, false, NULL, 0);
    mm0_decl_binder(d, "Q", wff, false, NULL, 0);
    r.d = d;
    mm0_decl_concl(d, expr(&r, "(im (an P Q) (an Q P))"));
    pq = expr(&r, "(an P Q)"); P = mm0_decl_var(d, 0); Q = mm0_decl_var(d, 1);
    Mm0Node *b_anr[] = {P, Q}, *b_anl[] = {P, Q}, *b_ian[] = {pq, Q, P};
    mm0_row_thm(d, thm_of(th, "anr"), b_anr, 2, NULL, 0, NULL, &r1);
    mm0_row_thm(d, thm_of(th, "anl"), b_anl, 2, NULL, 0, NULL, &r2);
    uint32_t swapped[] = {r2, r1};
    st = mm0_row_thm(d, thm_of(th, "ian"), b_ian, 3, swapped, 2, NULL, &r3);
    CHECK(st == MM0_REFUSED, "parents are ordered premises");
    st = mm0_decl_finish_theorem(d, "swap2", r3, &idx);
    CHECK(st == MM0_REFUSED && th->nthms == before + 1, "failed proof admits nothing");
    mm0_decl_free(d);

    /* a duplicated parent is two uses of one row: ian with G = P;P from id */
    d = mm0_decl_new(th, NULL);
    mm0_decl_binder(d, "P", wff, false, NULL, 0);
    r.d = d; r.nnames = 1;
    mm0_decl_concl(d, expr(&r, "(im P (an P P))"));
    P = mm0_decl_var(d, 0);
    Mm0Node *c_id[] = {P}, *c_ian[] = {P, P, P};
    mm0_row_thm(d, thm_of(th, "id"), c_id, 1, NULL, 0, NULL, &r1);
    uint32_t twice[] = {r1, r1};
    mm0_row_thm(d, thm_of(th, "ian"), c_ian, 3, twice, 2, NULL, &r2);
    st = mm0_decl_finish_theorem(d, "dup", r2, &idx);
    CHECK(st == MM0_OK, "a row cited twice");
    mm0_decl_free(d);

    /* conversion: from G |- a <=> b to G |- =[bool] a = b by unfolding bi */
    const char *cn[] = {"G", "a", "b"};
    d = mm0_decl_new(th, NULL);
    mm0_decl_binder(d, "G", wff, false, NULL, 0);
    mm0_decl_binder(d, "a", term, false, NULL, 0);
    mm0_decl_binder(d, "b", term, false, NULL, 0);
    r = (Reader){d, th, cn, 3};
    mm0_decl_hyp(d, expr(&r, "(im G (thm (bi a b)))"));
    mm0_decl_concl(d, expr(&r, "(im G (thm (eqc (bool) a b)))"));
    Mm0Node *ab[] = {mm0_decl_var(d, 1), mm0_decl_var(d, 2)};
    Mm0Conv unf = {0}, in_thm = {0}, refl_g = {0}, whole = {0};
    bool ok = mm0_conv_unfold(d, term_of(th, "bi"), ab, 2, NULL, 0, &unf) &&
              mm0_conv_cong(d, term_of(th, "thm"), &unf, 1, &in_thm) &&
              mm0_conv_refl(d, mm0_decl_var(d, 0), &refl_g);
    Mm0Conv parts[2] = {refl_g, in_thm};
    ok = ok && mm0_conv_cong(d, term_of(th, "im"), parts, 2, &whole);
    CHECK(ok, "conversion witnesses check");
    mm0_row_hyp(d, 0, NULL, &r1);
    mm0_row_conv(d, &whole, r1, NULL, &r2);
    st = mm0_decl_finish_theorem(d, "bi_eq", r2, &idx);
    CHECK(st == MM0_OK, "unfolding conversion proves the statement");
    mm0_decl_free(d);

    /* the conversion in the wrong direction does not match the parent */
    d = mm0_decl_new(th, NULL);
    mm0_decl_binder(d, "G", wff, false, NULL, 0);
    mm0_decl_binder(d, "a", term, false, NULL, 0);
    mm0_decl_binder(d, "b", term, false, NULL, 0);
    r.d = d;
    mm0_decl_hyp(d, expr(&r, "(im G (thm (bi a b)))"));
    mm0_decl_concl(d, expr(&r, "(im G (thm (eqc (bool) a b)))"));
    Mm0Node *ab2[] = {mm0_decl_var(d, 1), mm0_decl_var(d, 2)};
    mm0_conv_unfold(d, term_of(th, "bi"), ab2, 2, NULL, 0, &unf);
    mm0_conv_cong(d, term_of(th, "thm"), &unf, 1, &in_thm);
    mm0_conv_refl(d, mm0_decl_var(d, 0), &refl_g);
    Mm0Conv parts2[2] = {refl_g, in_thm};
    mm0_conv_cong(d, term_of(th, "im"), parts2, 2, &whole);
    Mm0Conv back;
    mm0_conv_symm(d, &whole, &back);
    mm0_row_hyp(d, 0, NULL, &r1);
    st = mm0_row_conv(d, &back, r1, NULL, &r2);
    CHECK(st == MM0_REFUSED, "reversed conversion refused");
    mm0_decl_free(d);
}

/* ---- binding, dependency and dummy cases --------------------------------- */

typedef struct { Mm0Theory *th; uint32_t wff, set, wi, wal, weq, z, ax5, isz, f2, f1; } Bind;

static Bind binding_theory(void) {
    Bind b = {0};
    b.th = mm0_theory_new();
    Mm0Error err;
    mm0_add_sort(b.th, "wff", MM0_SORT_STRICT | MM0_SORT_PROVABLE, &err);
    mm0_add_sort(b.th, "set", 0, &err);
    b.wff = 0; b.set = 1;
    Mm0Decl *d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.wff, false, NULL, 0);
    mm0_decl_binder(d, "b", b.wff, false, NULL, 0);
    CHECK(mm0_decl_finish_term(d, "wi", b.wff, NULL, 0, &b.wi) == MM0_OK, "term wi");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    uint32_t dep0[] = {0};
    mm0_decl_binder(d, "x", b.set, true, NULL, 0);
    mm0_decl_binder(d, "a", b.wff, false, dep0, 1);
    CHECK(mm0_decl_finish_term(d, "wal", b.wff, NULL, 0, &b.wal) == MM0_OK, "term wal");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.set, false, NULL, 0);
    mm0_decl_binder(d, "b", b.set, false, NULL, 0);
    CHECK(mm0_decl_finish_term(d, "weq", b.wff, NULL, 0, &b.weq) == MM0_OK, "term weq");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    CHECK(mm0_decl_finish_term(d, "z", b.set, NULL, 0, &b.z) == MM0_OK, "term z");
    mm0_decl_free(d);
    /* axiom ax5 {x: set} (a: wff): $ a -> A. x a $ */
    const char *n5[] = {"x", "a"};
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "x", b.set, true, NULL, 0);
    mm0_decl_binder(d, "a", b.wff, false, NULL, 0);
    Reader r = {d, b.th, n5, 2};
    mm0_decl_concl(d, expr(&r, "(wi a (wal x a))"));
    CHECK(mm0_decl_finish_axiom(d, "ax5", &b.ax5) == MM0_OK, "axiom ax5");
    mm0_decl_free(d);
    /* def isz {x: set}: wff x = $ x = z $ (its value depends on x) */
    const char *nz[] = {"x"};
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "x", b.set, true, NULL, 0);
    r = (Reader){d, b.th, nz, 1};
    uint32_t ret0[] = {0};
    CHECK(mm0_decl_finish_def(d, "isz", b.wff, ret0, 1, expr(&r, "(weq x (z))"), &b.isz) == MM0_OK,
          "def isz with a declared dependency");
    mm0_decl_free(d);
    /* def f2 {.d1 .d2: set}: wff = $ A. d1 A. d2 d1 = d2 $ */
    const char *nf[] = {"d1", "d2"};
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_dummy(d, "d1", b.set);
    mm0_decl_dummy(d, "d2", b.set);
    r = (Reader){d, b.th, nf, 2};
    CHECK(mm0_decl_finish_def(d, "f2", b.wff, NULL, 0, expr(&r, "(wal d1 (wal d2 (weq d1 d2)))"), &b.f2) == MM0_OK,
          "def f2 with two dummies");
    mm0_decl_free(d);
    /* def f1 {.d .unused: set}: wff = $ A. d A. d d = d $ */
    const char *n1[] = {"d", "unused"};
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_dummy(d, "d", b.set);
    mm0_decl_dummy(d, "unused", b.set);
    r = (Reader){d, b.th, n1, 2};
    CHECK(mm0_decl_finish_def(d, "f1", b.wff, NULL, 0, expr(&r, "(wal d (wal d (weq d d)))"), &b.f1) == MM0_OK,
          "def f1 with an unused dummy");
    mm0_decl_free(d);
    return b;
}

/* Apply ax5 to (x, a) in a fresh builder with binders {y z: set} (p: wff)
 * and return the status. */
static Mm0Status apply_ax5(Bind *b, const char *x, const char *a) {
    const char *names[] = {"y", "w", "p"};
    Mm0Decl *d = mm0_decl_new(b->th, NULL);
    mm0_decl_binder(d, "y", b->set, true, NULL, 0);
    mm0_decl_binder(d, "w", b->set, true, NULL, 0);
    mm0_decl_binder(d, "p", b->wff, false, NULL, 0);
    Reader r = {d, b->th, names, 3};
    Mm0Node *args[] = {expr(&r, x), expr(&r, a)};
    uint32_t row;
    Mm0Status st = args[0] && args[1] ? mm0_row_thm(d, b->ax5, args, 2, NULL, 0, NULL, &row)
                                      : mm0_decl_error(d)->status;
    mm0_decl_free(d);
    return st;
}

static void binding_cases(void) {
    Bind b = binding_theory();
    CHECK(apply_ax5(&b, "y", "p") == MM0_OK, "ax5 with a fresh formula");
    CHECK(apply_ax5(&b, "y", "(weq w w)") == MM0_OK, "ax5 where the formula avoids x");
    CHECK(apply_ax5(&b, "y", "(weq y y)") == MM0_REFUSED, "ax5 where the formula uses x");
    /* the definition's value depends on its bound argument: mm0-c's
     * theorem-mode variable set drops it and accepts this */
    CHECK(apply_ax5(&b, "y", "(isz y)") == MM0_REFUSED, "ax5 with isz x: dependency through a bound argument");
    /* mm0.md Vars counts binder positions too, as mm0-rs does */
    CHECK(apply_ax5(&b, "y", "(wal y (weq w w))") == MM0_REFUSED, "x in a binder position of the formula");
    CHECK(apply_ax5(&b, "(z)", "p") == MM0_REFUSED, "a bound binder needs a bound variable");
    CHECK(apply_ax5(&b, "p", "p") == MM0_REFUSED, "a sort mismatch");

    /* definitions: undeclared free variables and dummy sorts */
    Mm0Error err;
    Mm0Decl *d = mm0_decl_new(b.th, NULL);
    const char *nx[] = {"x"};
    mm0_decl_binder(d, "x", b.set, true, NULL, 0);
    Reader r = {d, b.th, nx, 1};
    CHECK(mm0_decl_finish_def(d, "undeclared", b.wff, NULL, 0, expr(&r, "(weq x x)"), NULL) == MM0_REFUSED,
          "a free variable missing from the return dependencies");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    const char *ny[] = {"y"};
    mm0_decl_dummy(d, "y", b.set);
    r = (Reader){d, b.th, ny, 1};
    CHECK(mm0_decl_finish_def(d, "free_dummy", b.wff, NULL, 0, expr(&r, "(weq y y)"), NULL) == MM0_REFUSED,
          "a dummy free in the body");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_dummy(d, "y", b.set);
    r = (Reader){d, b.th, ny, 1};
    CHECK(mm0_decl_finish_def(d, "closed", b.wff, NULL, 0, expr(&r, "(wal y (weq y y))"), NULL) == MM0_OK,
          "a dummy bound in the body");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    CHECK(mm0_decl_dummy(d, "q", b.wff) == MM0_REFUSED, "a dummy of a strict sort");
    mm0_decl_free(d);
    CHECK(mm0_add_sort(b.th, "fsort", MM0_SORT_FREE, &err) == MM0_OK, "free sort");
    d = mm0_decl_new(b.th, NULL);
    CHECK(mm0_decl_dummy(d, "q", sort_of(b.th, "fsort")) == MM0_REFUSED, "a dummy of a free sort");
    mm0_decl_free(d);
    CHECK(mm0_add_sort(b.th, "psort", MM0_SORT_PURE, &err) == MM0_OK, "pure sort");
    d = mm0_decl_new(b.th, NULL);
    CHECK(mm0_decl_finish_term(d, "in_pure", sort_of(b.th, "psort"), NULL, 0, NULL) == MM0_REFUSED,
          "a term in a pure sort");
    mm0_decl_free(d);
    CHECK(mm0_add_sort(b.th, "wff", 0, &err) == MM0_REFUSED, "duplicate sort");

    /* unfolding needs distinct fresh dummy images (FreshDummies.lean) */
    const char *nu[] = {"a", "c", "p"};
    uint32_t a = 0, c = 1;
    Mm0Conv out;
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.set, true, NULL, 0);
    mm0_decl_binder(d, "c", b.set, true, NULL, 0);
    mm0_decl_binder(d, "p", b.wff, false, NULL, 0);
    r = (Reader){d, b.th, nu, 3};
    uint32_t distinct[] = {a, c}, same[] = {a, a};
    CHECK(mm0_conv_unfold(d, b.f2, NULL, 0, distinct, 2, &out), "f2 unfolds with distinct images");
    CHECK(mm0_conv_right(d, out) == expr(&r, "(wal a (wal c (weq a c)))"), "the unfolding substitutes the images");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.set, true, NULL, 0);
    mm0_decl_binder(d, "c", b.set, true, NULL, 0);
    CHECK(!mm0_conv_unfold(d, b.f2, NULL, 0, same, 2, &out) &&
          mm0_decl_error(d)->status == MM0_REFUSED, "two dummies may not share one image");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.set, true, NULL, 0);
    mm0_decl_binder(d, "c", b.set, true, NULL, 0);
    CHECK(mm0_conv_unfold(d, b.f1, NULL, 0, distinct, 2, &out), "an unused dummy still takes an image");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.set, true, NULL, 0);
    uint32_t one[] = {a};
    CHECK(!mm0_conv_unfold(d, b.f1, NULL, 0, one, 1, &out), "too few images");
    mm0_decl_free(d);
    /* an image must be fresh for the arguments: isz x unfolded with x free */
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.set, true, NULL, 0);
    mm0_decl_dummy(d, "y", b.set);
    Mm0Node *arg = mm0_decl_var(d, 0);
    CHECK(mm0_conv_unfold(d, b.isz, &arg, 1, NULL, 0, &out), "isz has no dummies");
    mm0_decl_free(d);

    /* proof dummies: usable in rows, not in the statement */
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "p", b.wff, false, NULL, 0);
    mm0_decl_dummy(d, "y", b.set);
    const char *np[] = {"p", "y"};
    r = (Reader){d, b.th, np, 2};
    CHECK(mm0_decl_concl(d, expr(&r, "(wi p (wal y p))")) == MM0_REFUSED, "a statement may not use a proof dummy");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "p", b.wff, false, NULL, 0);
    mm0_decl_dummy(d, "y", b.set);
    r = (Reader){d, b.th, np, 2};
    mm0_decl_hyp(d, expr(&r, "(wi p (wal y p))"));
    CHECK(mm0_decl_error(d)->status == MM0_REFUSED, "nor a hypothesis");
    mm0_decl_free(d);

    /* binders */
    d = mm0_decl_new(b.th, NULL);
    CHECK(mm0_decl_binder(d, "q", b.wff, true, NULL, 0) == MM0_REFUSED, "a bound variable of a strict sort");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "p", b.wff, false, NULL, 0);
    uint32_t bad_dep[] = {0};
    CHECK(mm0_decl_binder(d, "q", b.wff, false, bad_dep, 1) == MM0_REFUSED, "a dependency on a regular variable");
    mm0_decl_free(d);
    mm0_theory_free(b.th);
}

/* ---- one control per check of the semantic route ------------------------- */

static const char *ctl_names[] = {"x", "y", "p", "s"};

/* A builder over the binding theory with binders {x y: set} (p: wff) (s: set). */
static Mm0Decl *controls_builder(Bind *b, Reader *r) {
    Mm0Decl *d = mm0_decl_new(b->th, NULL);
    mm0_decl_binder(d, "x", b->set, true, NULL, 0);
    mm0_decl_binder(d, "y", b->set, true, NULL, 0);
    mm0_decl_binder(d, "p", b->wff, false, NULL, 0);
    mm0_decl_binder(d, "s", b->set, false, NULL, 0);
    *r = (Reader){d, b->th, ctl_names, 4};
    return d;
}

static bool refused(const Mm0Decl *d) { return mm0_decl_error(d)->status == MM0_REFUSED; }

static void semantic_controls(void) {
    Bind b = binding_theory();
    Reader r;
    Mm0Conv c1, c2, out;
    uint32_t row, row2, idx;
    Mm0Error err;

    /* expressions */
    Mm0Decl *d = controls_builder(&b, &r);
    CHECK(!expr(&r, "(weq p p)") && refused(d), "an argument of the wrong sort");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    Mm0Node *one[] = {mm0_decl_var(d, 2)};
    CHECK(!mm0_decl_app(d, b.wi, one, 1) && refused(d), "an application with too few arguments");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    uint32_t dep_x[] = {0};
    mm0_decl_binder(d, "x", b.set, true, NULL, 0);
    CHECK(mm0_decl_binder(d, "q", b.set, true, dep_x, 1) == MM0_REFUSED, "a bound variable with dependencies");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    CHECK(mm0_decl_concl(d, mm0_decl_var(d, 3)) == MM0_REFUSED, "a statement whose sort is not provable");
    mm0_decl_free(d);

    /* conversion witnesses */
    d = controls_builder(&b, &r);
    bool ok = mm0_conv_refl(d, expr(&r, "(weq x x)"), &c1) && mm0_conv_refl(d, expr(&r, "(weq x x)"), &c2);
    CHECK(ok && mm0_conv_trans(d, &c1, &c2, &out), "transitivity through one middle expression");
    ok = mm0_conv_refl(d, expr(&r, "(weq y y)"), &c2);
    CHECK(ok && !mm0_conv_trans(d, &c1, &c2, &out) && refused(d), "transitivity with different middle expressions");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    Mm0Node *two[] = {mm0_decl_var(d, 2), mm0_decl_var(d, 2)};
    CHECK(!mm0_conv_unfold(d, b.wi, two, 2, NULL, 0, &out) && refused(d), "only definitions unfold");
    mm0_decl_free(d);
    /* dummy images are bound variables of the dummy's sort, also for a
     * dummy that the body leaves unused (f1's second) */
    uint32_t x_s[] = {0, 3};
    d = controls_builder(&b, &r);
    CHECK(!mm0_conv_unfold(d, b.f1, NULL, 0, x_s, 2, &out) && refused(d), "a regular variable as a dummy image");
    mm0_decl_free(d);
    CHECK(mm0_add_sort(b.th, "nat", 0, &err) == MM0_OK, "sort nat");
    d = controls_builder(&b, &r);
    mm0_decl_binder(d, "n", sort_of(b.th, "nat"), true, NULL, 0);
    uint32_t x_n[] = {0, 4};
    CHECK(!mm0_conv_unfold(d, b.f1, NULL, 0, x_n, 2, &out) && refused(d), "a dummy image of another sort");
    mm0_decl_free(d);

    /* rows */
    d = controls_builder(&b, &r);
    Mm0Node *y_p[] = {mm0_decl_var(d, 1), mm0_decl_var(d, 2)};
    CHECK(mm0_row_thm(d, b.ax5, y_p, 2, NULL, 0, expr(&r, "(wi p (wal y p))"), &row) == MM0_OK,
          "a row with its own conclusion as the claim");
    CHECK(mm0_row_thm(d, b.ax5, y_p, 2, NULL, 0, expr(&r, "(wi p (wal x p))"), &row2) == MM0_REFUSED,
          "a row claiming another conclusion");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    CHECK(mm0_row_hyp(d, 3, NULL, &row) == MM0_REFUSED, "a row for a missing hypothesis");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    Mm0Node *y_p2[] = {mm0_decl_var(d, 1), mm0_decl_var(d, 2)};
    mm0_row_thm(d, b.ax5, y_p2, 2, NULL, 0, NULL, &row);
    uint32_t extra[] = {row};
    CHECK(mm0_row_thm(d, b.ax5, y_p2, 2, extra, 1, NULL, &row2) == MM0_REFUSED,
          "a parent for a theorem without hypotheses");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    mm0_decl_concl(d, expr(&r, "(wi p (wal x p))"));
    Mm0Node *y_p3[] = {mm0_decl_var(d, 1), mm0_decl_var(d, 2)};
    mm0_row_thm(d, b.ax5, y_p3, 2, NULL, 0, NULL, &row);
    CHECK(mm0_decl_finish_theorem(d, "wrong_root", row, &idx) == MM0_REFUSED, "a root row proving another statement");
    mm0_decl_free(d);

    /* the side condition also covers a regular binder before the bound one
     * (mm0.md:379 names only binders after it): axiom ax5r (a: wff) {x: set}:
     * $ a -> A. x a $ with a := x = y */
    d = mm0_decl_new(b.th, NULL);
    mm0_decl_binder(d, "a", b.wff, false, NULL, 0);
    mm0_decl_binder(d, "x", b.set, true, NULL, 0);
    const char *n5r[] = {"a", "x"};
    Reader r5 = {d, b.th, n5r, 2};
    mm0_decl_concl(d, expr(&r5, "(wi a (wal x a))"));
    uint32_t ax5r = 0;
    CHECK(mm0_decl_finish_axiom(d, "ax5r", &ax5r) == MM0_OK, "axiom ax5r, its regular binder first");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    Mm0Node *dv_bad[] = {expr(&r, "(weq x y)"), mm0_decl_var(d, 0)};
    CHECK(mm0_row_thm(d, ax5r, dv_bad, 2, NULL, 0, NULL, &row) == MM0_REFUSED,
          "a bound argument inside an earlier regular argument");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    Mm0Node *dv_ok[] = {expr(&r, "(weq y y)"), mm0_decl_var(d, 0)};
    CHECK(mm0_row_thm(d, ax5r, dv_ok, 2, NULL, 0, NULL, &row) == MM0_OK, "the same with a formula avoiding x");
    mm0_decl_free(d);

    /* declarations */
    d = controls_builder(&b, &r);
    uint32_t ret_p[] = {2};
    CHECK(mm0_decl_finish_def(d, "dep_on_regular", b.wff, ret_p, 1, expr(&r, "(weq s s)"), NULL) == MM0_REFUSED,
          "a return dependency on a regular variable");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    CHECK(mm0_decl_finish_def(d, "set_valued", b.wff, NULL, 0, mm0_decl_var(d, 3), NULL) == MM0_REFUSED,
          "a definition whose value has another sort");
    mm0_decl_free(d);
    d = controls_builder(&b, &r);
    mm0_decl_concl(d, expr(&r, "(wi p (wal x p))"));
    CHECK(mm0_decl_finish_axiom(d, "ax5", &idx) == MM0_REFUSED, "a theorem name already in use");
    mm0_decl_free(d);
    d = mm0_decl_new(b.th, NULL);
    CHECK(mm0_decl_finish_term(d, "z", b.set, NULL, 0, &idx) == MM0_REFUSED, "a term name already in use");
    mm0_decl_free(d);
    mm0_theory_free(b.th);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: test_mm0 HOL.MMB HOL.MM0\n");
        return 2;
    }
    Mm0Theory *hol = load_hol(argv[1], argv[2]);
    hol_semantic(hol);
    mm0_theory_free(hol);
    binding_cases();
    semantic_controls();
    printf("Mm0CoreSummary %d %d\n", checks, failures);
    return failures != 0;
}
