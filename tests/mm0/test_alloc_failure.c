/* Allocation-failure injection for the native MM0 core.
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc, so that any
 * one allocation made by the core can be made to fail.  For each n, the
 * n-th allocation of an operation fails; the operation must then report
 * NOMEM (or succeed, when the failure was absorbed), leave the theory as it
 * was, and leak nothing (the sanitized build runs under LeakSanitizer).
 *
 * Operations: loading an MMB file with its specification (all or nothing),
 * and two proofs given as data on the semantic route (a theorem
 * application with ordered premises, and a conversion by unfolding).
 *
 *   test_alloc_failure HOL.MMB HOL.MM0 [PEANO.MMB PEANO.MM0 STRIDE] */
#include "mm0.h"
#include "mm0_internal.h"
#include "mm0_semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t count, size_t n);
void *__real_realloc(void *p, size_t n);

static unsigned long long g_count, g_fail_at;   /* g_fail_at 0: never fail */
static bool g_failed;

static bool fail_now(void) {
    if (++g_count != g_fail_at) return false;
    g_failed = true;
    return true;
}
void *__wrap_malloc(size_t n) { return fail_now() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t count, size_t n) { return fail_now() ? NULL : __real_calloc(count, n); }
void *__wrap_realloc(void *p, size_t n) { return fail_now() ? NULL : __real_realloc(p, n); }

static void arm(unsigned long long n) { g_count = 0; g_fail_at = n; g_failed = false; }
static void disarm(void) { g_fail_at = 0; }

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

typedef struct { unsigned char *mmb, *spec; size_t mmb_len, spec_len; } Files;

static Mm0Status load(const Files *f, Mm0Theory *th) {
    Mm0Options options;
    mm0_options_init(&options);
    Mm0Error error;
    return mm0_load_mmb(th, f->mmb, f->mmb_len, (const char *)f->spec, f->spec_len, &options, &error);
}

/* ---- loading: all or nothing ---------------------------------------------- */

static void load_failures(const char *label, const Files *f, unsigned long long stride) {
    Mm0Theory *th = mm0_theory_new();
    arm(0);
    Mm0Status st = load(f, th);
    unsigned long long total = g_count;
    Mm0TheoryStats full = mm0_theory_stats(th);
    mm0_theory_free(th);
    CHECK(st == MM0_OK, "%s loads without injected failures", label);
    unsigned long long tried = 0, absorbed = 0;
    for (unsigned long long n = 1; n <= total + 1; n += (n == total + 1 ? 1 : stride)) {
        ++tried;
        arm(n);
        th = mm0_theory_new();
        if (!th) { disarm(); CHECK(g_failed, "%s: theory allocation fails only when told", label); continue; }
        st = load(f, th);
        disarm();
        Mm0TheoryStats s = mm0_theory_stats(th);
        if (!g_failed) {
            CHECK(st == MM0_OK && s.theorems == full.theorems, "%s: allocation %llu past the end", label, n);
        } else if (st == MM0_OK) {
            ++absorbed;
            CHECK(s.sorts == full.sorts && s.terms == full.terms && s.theorems == full.theorems,
                  "%s: allocation %llu absorbed with a complete theory", label, n);
        } else {
            CHECK(st == MM0_NOMEM, "%s: allocation %llu fails with NOMEM, not %s", label, n, mm0_status_name(st));
            CHECK(s.sorts == 0 && s.terms == 0 && s.theorems == 0,
                  "%s: allocation %llu leaves the theory empty", label, n);
            /* the same theory still loads afterwards */
            CHECK(load(f, th) == MM0_OK, "%s: a theory loads after the failure at %llu", label, n);
        }
        mm0_theory_free(th);
    }
    printf("%s: %llu allocations, %llu failure points tried, %llu absorbed\n", label, total, tried, absorbed);
}

/* ---- the semantic route: nothing is admitted on failure ------------------- */

static uint32_t sort_named(Mm0Theory *th, const char *name) {
    uint32_t s = UINT32_MAX;
    mm0_names_get(&th->sort_names, name, strlen(name), &s);
    return s;
}
static uint32_t term_named(Mm0Theory *th, const char *name) {
    uint32_t t = UINT32_MAX;
    mm0_names_get(&th->term_names, name, strlen(name), &t);
    return t;
}
static uint32_t thm_named(Mm0Theory *th, const char *name) {
    uint32_t t = UINT32_MAX;
    mm0_names_get(&th->thm_names, name, strlen(name), &t);
    return t;
}

static Mm0Node *app2(Mm0Decl *d, uint32_t t, Mm0Node *a, Mm0Node *b) {
    Mm0Node *args[] = {a, b};
    return a && b ? mm0_decl_app(d, t, args, 2) : NULL;
}

/* swap (P Q: wff): $ P /\ Q -> Q /\ P $ from anr, anl and ian */
static Mm0Status prove_swap(Mm0Theory *th, const char *name) {
    Mm0Decl *d = mm0_decl_new(th, NULL);
    if (!d) return MM0_NOMEM;
    uint32_t wff = sort_named(th, "wff"), an = term_named(th, "an"), im = term_named(th, "im");
    mm0_decl_binder(d, "P", wff, false, NULL, 0);
    mm0_decl_binder(d, "Q", wff, false, NULL, 0);
    Mm0Node *P = mm0_decl_var(d, 0), *Q = mm0_decl_var(d, 1);
    Mm0Node *pq = app2(d, an, P, Q), *qp = app2(d, an, Q, P);
    Mm0Node *concl = app2(d, im, pq, qp);
    if (concl) mm0_decl_concl(d, concl);
    uint32_t r1 = 0, r2 = 0, r3 = 0;
    Mm0Node *a_pq[] = {P, Q}, *a_ian[] = {pq, Q, P};
    if (pq) {
        mm0_row_thm(d, thm_named(th, "anr"), a_pq, 2, NULL, 0, NULL, &r1);
        mm0_row_thm(d, thm_named(th, "anl"), a_pq, 2, NULL, 0, NULL, &r2);
        uint32_t parents[] = {r1, r2};
        mm0_row_thm(d, thm_named(th, "ian"), a_ian, 3, parents, 2, NULL, &r3);
    }
    mm0_decl_finish_theorem(d, name, r3, NULL);
    Mm0Status st = mm0_decl_error(d)->status;
    mm0_decl_free(d);
    return st;
}

/* bi_eq (G: wff) (a b: term): $ G -> thm (bi a b) $ > $ G -> thm (eqc bool a b) $
 * by unfolding bi under thm and im */
static Mm0Status prove_bi_eq(Mm0Theory *th, const char *name) {
    Mm0Decl *d = mm0_decl_new(th, NULL);
    if (!d) return MM0_NOMEM;
    uint32_t wff = sort_named(th, "wff"), term = sort_named(th, "term");
    uint32_t im = term_named(th, "im"), thm = term_named(th, "thm"), bi = term_named(th, "bi");
    uint32_t eqc = term_named(th, "eqc"), boolean = term_named(th, "bool");
    mm0_decl_binder(d, "G", wff, false, NULL, 0);
    mm0_decl_binder(d, "a", term, false, NULL, 0);
    mm0_decl_binder(d, "b", term, false, NULL, 0);
    Mm0Node *G = mm0_decl_var(d, 0), *a = mm0_decl_var(d, 1), *b = mm0_decl_var(d, 2);
    Mm0Node *bab = app2(d, bi, a, b);
    Mm0Node *tbi = bab ? mm0_decl_app(d, thm, &bab, 1) : NULL;
    Mm0Node *bool_node = mm0_decl_app(d, boolean, NULL, 0);
    Mm0Node *eq_args[] = {bool_node, a, b};
    Mm0Node *eq = bool_node ? mm0_decl_app(d, eqc, eq_args, 3) : NULL;
    Mm0Node *teq = eq ? mm0_decl_app(d, thm, &eq, 1) : NULL;
    Mm0Node *hyp = app2(d, im, G, tbi), *concl = app2(d, im, G, teq);
    if (hyp) mm0_decl_hyp(d, hyp);
    if (concl) mm0_decl_concl(d, concl);
    Mm0Conv unf, in_thm, refl_g, whole;
    Mm0Node *ab[] = {a, b};
    uint32_t r1 = 0, r2 = 0;
    if (mm0_conv_unfold(d, bi, ab, 2, NULL, 0, &unf) && mm0_conv_cong(d, thm, &unf, 1, &in_thm) &&
        mm0_conv_refl(d, G, &refl_g)) {
        Mm0Conv parts[2] = {refl_g, in_thm};
        if (mm0_conv_cong(d, im, parts, 2, &whole)) {
            mm0_row_hyp(d, 0, NULL, &r1);
            mm0_row_conv(d, &whole, r1, NULL, &r2);
        }
    }
    mm0_decl_finish_theorem(d, name, r2, NULL);
    Mm0Status st = mm0_decl_error(d)->status;
    mm0_decl_free(d);
    return st;
}

static void semantic_failures(const char *label, const Files *hol, Mm0Status (*prove)(Mm0Theory *, const char *)) {
    Mm0Theory *th = mm0_theory_new();
    CHECK(th && load(hol, th) == MM0_OK, "%s: hol loads", label);
    size_t before = th->nthms;
    arm(0);
    Mm0Status st = prove(th, "target");
    unsigned long long total = g_count;
    CHECK(st == MM0_OK && th->nthms == before + 1, "%s proves without injected failures", label);
    mm0_theory_free(th);
    unsigned long long absorbed = 0;
    for (unsigned long long n = 1; n <= total + 1; ++n) {
        th = mm0_theory_new();
        load(hol, th);
        before = th->nthms;
        arm(n);
        st = prove(th, "target");
        disarm();
        if (!g_failed) {
            CHECK(st == MM0_OK && th->nthms == before + 1, "%s: allocation %llu past the end", label, n);
        } else if (st == MM0_OK) {
            ++absorbed;
            CHECK(th->nthms == before + 1, "%s: allocation %llu absorbed", label, n);
        } else {
            CHECK(st == MM0_NOMEM, "%s: allocation %llu fails with NOMEM, not %s", label, n, mm0_status_name(st));
            CHECK(th->nthms == before && thm_named(th, "target") == UINT32_MAX,
                  "%s: allocation %llu admits nothing", label, n);
            CHECK(prove(th, "target") == MM0_OK && th->nthms == before + 1,
                  "%s: the theory admits the proof after the failure at %llu", label, n);
        }
        mm0_theory_free(th);
    }
    printf("%s: %llu allocations, %llu failure points tried, %llu absorbed\n", label, total, total + 1, absorbed);
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 6) {
        fprintf(stderr, "usage: test_alloc_failure HOL.MMB HOL.MM0 [PEANO.MMB PEANO.MM0 STRIDE]\n");
        return 2;
    }
    Files hol = {0}, peano = {0};
    hol.mmb = slurp(argv[1], &hol.mmb_len);
    hol.spec = slurp(argv[2], &hol.spec_len);
    if (!hol.mmb || !hol.spec) { fprintf(stderr, "cannot read the hol files\n"); return 2; }
    load_failures("hol load", &hol, 1);
    semantic_failures("semantic swap", &hol, prove_swap);
    semantic_failures("semantic bi_eq", &hol, prove_bi_eq);
    if (argc == 6) {
        peano.mmb = slurp(argv[3], &peano.mmb_len);
        peano.spec = slurp(argv[4], &peano.spec_len);
        if (!peano.mmb || !peano.spec) { fprintf(stderr, "cannot read the peano files\n"); return 2; }
        load_failures("peano load", &peano, strtoull(argv[5], NULL, 10));
    }
    free(hol.mmb); free(hol.spec); free(peano.mmb); free(peano.spec);
    printf("Mm0AllocFailureSummary %d %d\n", checks, failures);
    return failures != 0;
}
