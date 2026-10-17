/* gen_streams.c - deterministic HOLR workloads for matched-work timing of
 * the native kernel and the OCaml oracle (bench/run_bench.sh).
 *
 *   gen-streams KIND SIZE OUT.hrs
 *
 * KIND:
 *   dag      t0 = x, t(i+1) = f t(i) t(i), depth SIZE: a shared DAG of SIZE
 *            nodes whose tree has 2^SIZE leaves; REFL, INST [y/x], TRANS
 *            with itself, EXPECT against the independently built image.
 *   deep     t0 = x, t(i+1) = g t(i), depth SIZE; REFL, INST [y/x], EXPECT.
 *   subst    SIZE INST records with capture renaming and SIZE INST_TYPE
 *            records with binder clashes, on a fixed medium term.
 *   hyps     ASSUME p1 ... pSIZE combined by DEDUCT_ANTISYM_RULE into one
 *            theorem whose hypothesis set grows to SIZE (term_union work).
 *   history  SIZE NEW_TYPE, NEW_CONSTANT and NEW_DEFINITION events, then
 *            SIZE constant uses of the oldest declarations.
 *   d1       (SIZE ignored) the minimal instance of divergence D1
 *            (docs/CONTRACT.md): REFL (\z:A. f x z), INST_TYPE [B/A], then
 *            INST [(x:B, x:B); (z:B, x:B)].  fusion.ml renames z (its
 *            identity pair is not physically the term's x); the hash-consed
 *            kernel does not.  The two results are alpha-equivalent.
 * Every stream ends with EXPECT/EMIT records stating its final results. */
#include "hol_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HolWriter *w;
    uint64_t n, y, t, h;
} Asm;

static void op(Asm *a, uint8_t o) { hol_writer_op(a->w, o); }
static void uv(Asm *a, uint64_t v) { hol_writer_uv(a->w, v); }
static uint64_t NAME(Asm *a, const char *s) {
    op(a, HOLR_NAME);
    uv(a, strlen(s));
    hol_writer_bytes(a->w, s, strlen(s));
    return a->n++;
}
static uint64_t TYVAR(Asm *a, uint64_t n) { op(a, HOLR_TYVAR); uv(a, n); return a->y++; }
static uint64_t TYAPP(Asm *a, uint64_t n, unsigned k, const uint64_t *args) {
    op(a, HOLR_TYAPP);
    uv(a, n);
    uv(a, k);
    for (unsigned i = 0; i < k; i++) uv(a, args[i]);
    return a->y++;
}
static uint64_t FUN(Asm *a, uint64_t nfun, uint64_t x, uint64_t y) {
    uint64_t args[2] = {x, y};
    return TYAPP(a, nfun, 2, args);
}
static uint64_t T2(Asm *a, uint8_t o, uint64_t x, uint64_t y) { op(a, o); uv(a, x); uv(a, y); return a->t++; }
static uint64_t H1(Asm *a, uint8_t o, uint64_t x) { op(a, o); uv(a, x); return a->h++; }
static uint64_t H2(Asm *a, uint8_t o, uint64_t x, uint64_t y) { op(a, o); uv(a, x); uv(a, y); return a->h++; }
static uint64_t INSTR(Asm *a, uint8_t o, unsigned np, const uint64_t *pairs, uint64_t th) {
    op(a, o);
    uv(a, np);
    for (unsigned i = 0; i < 2 * np; i++) uv(a, pairs[i]);
    uv(a, th);
    return a->h++;
}
static void EXPECT(Asm *a, uint64_t th, unsigned nh, const uint64_t *hyps, uint64_t concl) {
    op(a, HOLR_EXPECT);
    uv(a, th);
    uv(a, nh);
    for (unsigned i = 0; i < nh; i++) uv(a, hyps[i]);
    uv(a, concl);
}
static void EMIT(Asm *a, uint64_t th) { op(a, HOLR_EMIT); uv(a, th); }

typedef struct {
    uint64_t nfun, nbool, neq, A, B, bool_, AA, AAA, Ab, AAb;
} Base;

static void base(Asm *a, Base *b) {
    uint64_t nA = NAME(a, "A"), nB = NAME(a, "B");
    b->nbool = NAME(a, "bool");
    b->nfun = NAME(a, "fun");
    b->neq = NAME(a, "=");
    b->A = TYVAR(a, nA);
    b->B = TYVAR(a, nB);
    b->bool_ = TYAPP(a, b->nbool, 0, NULL);
    b->AA = FUN(a, b->nfun, b->A, b->A);
    b->AAA = FUN(a, b->nfun, b->A, b->AA);
    b->Ab = FUN(a, b->nfun, b->A, b->bool_);
    b->AAb = FUN(a, b->nfun, b->A, b->Ab);
}

static uint64_t mk_eq(Asm *a, uint64_t eqc, uint64_t l, uint64_t r) {
    return T2(a, HOLR_COMB, T2(a, HOLR_COMB, eqc, l), r);
}

static void gen_dag(Asm *a, unsigned depth) {
    Base b;
    base(a, &b);
    uint64_t x = T2(a, HOLR_VAR, NAME(a, "x"), b.A), y = T2(a, HOLR_VAR, NAME(a, "y"), b.A);
    uint64_t f = T2(a, HOLR_VAR, NAME(a, "f"), b.AAA);
    uint64_t t = x, u = y;
    for (unsigned i = 0; i < depth; i++) {
        t = T2(a, HOLR_COMB, T2(a, HOLR_COMB, f, t), t);
        u = T2(a, HOLR_COMB, T2(a, HOLR_COMB, f, u), u);
    }
    uint64_t th = H1(a, HOLR_REFL, t);
    uint64_t pr[2] = {y, x};
    uint64_t th2 = INSTR(a, HOLR_INST, 1, pr, th);
    uint64_t th3 = H2(a, HOLR_TRANS, th2, th2);
    uint64_t eqA = T2(a, HOLR_CONST, b.neq, b.AAb);
    EXPECT(a, th3, 0, NULL, mk_eq(a, eqA, u, u));
    EMIT(a, th3);
}

static void gen_deep(Asm *a, unsigned depth) {
    Base b;
    base(a, &b);
    uint64_t x = T2(a, HOLR_VAR, NAME(a, "x"), b.A), y = T2(a, HOLR_VAR, NAME(a, "y"), b.A);
    uint64_t g = T2(a, HOLR_VAR, NAME(a, "g"), b.AA);
    uint64_t t = x, u = y;
    for (unsigned i = 0; i < depth; i++) {
        t = T2(a, HOLR_COMB, g, t);
        u = T2(a, HOLR_COMB, g, u);
    }
    uint64_t th = H1(a, HOLR_REFL, t);
    uint64_t pr[2] = {y, x};
    uint64_t th2 = INSTR(a, HOLR_INST, 1, pr, th);
    uint64_t eqA = T2(a, HOLR_CONST, b.neq, b.AAb);
    EXPECT(a, th2, 0, NULL, mk_eq(a, eqA, u, u));
    EMIT(a, th2);
}

/* \y. \z. h (k x y) z with x free: INST [y/x] renames y; INST_TYPE [B/A]
 * on \x:A. q x (x:B) renames the binder. */
static void gen_subst(Asm *a, unsigned n) {
    Base b;
    base(a, &b);
    uint64_t nx = NAME(a, "x"), ny = NAME(a, "y"), nz = NAME(a, "z"), nh = NAME(a, "h"), nk = NAME(a, "k"),
             nq = NAME(a, "q");
    uint64_t x = T2(a, HOLR_VAR, nx, b.A), y = T2(a, HOLR_VAR, ny, b.A), z = T2(a, HOLR_VAR, nz, b.A);
    uint64_t h = T2(a, HOLR_VAR, nh, b.AAA), k = T2(a, HOLR_VAR, nk, b.AAA);
    uint64_t body = T2(a, HOLR_COMB, T2(a, HOLR_COMB, h, T2(a, HOLR_COMB, T2(a, HOLR_COMB, k, x), y)), z);
    uint64_t lam = T2(a, HOLR_ABS, y, T2(a, HOLR_ABS, z, body));
    /* a larger body under the capturing binder: \y. \z. h (k x y) (h (k x y) ... z) */
    uint64_t kxy = T2(a, HOLR_COMB, T2(a, HOLR_COMB, k, x), y), deep = z;
    for (int i = 0; i < 64; i++) deep = T2(a, HOLR_COMB, T2(a, HOLR_COMB, h, kxy), deep);
    uint64_t big = T2(a, HOLR_ABS, y, T2(a, HOLR_ABS, z, deep));
    uint64_t th = H1(a, HOLR_REFL, T2(a, HOLR_COMB, T2(a, HOLR_COMB, h, x), x));
    uint64_t thl = H1(a, HOLR_REFL, lam);
    uint64_t thb = H1(a, HOLR_REFL, big);
    uint64_t pr[2] = {y, x};
    uint64_t last = th;
    for (unsigned i = 0; i < n; i++) {
        INSTR(a, HOLR_INST, 1, pr, thl);
        last = INSTR(a, HOLR_INST, 1, pr, thb);
    }
    uint64_t xB = T2(a, HOLR_VAR, nx, b.B);
    uint64_t qA = T2(a, HOLR_VAR, nq, FUN(a, b.nfun, b.A, FUN(a, b.nfun, b.B, b.bool_)));
    uint64_t clash = T2(a, HOLR_ABS, x, T2(a, HOLR_COMB, T2(a, HOLR_COMB, qA, x), xB));
    uint64_t thc = H1(a, HOLR_REFL, clash);
    uint64_t ty[2] = {b.B, b.A};
    uint64_t lastty = thc;
    for (unsigned i = 0; i < n; i++) lastty = INSTR(a, HOLR_INST_TYPE, 1, ty, thc);
    EMIT(a, last);
    EMIT(a, lastty);
}

static void gen_hyps(Asm *a, unsigned n) {
    Base b;
    base(a, &b);
    uint64_t acc = 0;
    char nm[32];
    for (unsigned i = 0; i < n; i++) {
        snprintf(nm, sizeof nm, "p%u", (i * 7919u) % n); /* not in alphaorder */
        uint64_t p = T2(a, HOLR_VAR, NAME(a, nm), b.bool_);
        uint64_t as = H1(a, HOLR_ASSUME, p);
        acc = i == 0 ? as : H2(a, HOLR_DEDUCT, acc, as);
    }
    EMIT(a, acc);
}

static void gen_history(Asm *a, unsigned n) {
    Base b;
    base(a, &b);
    char nm[48];
    uint64_t p = T2(a, HOLR_VAR, NAME(a, "p"), b.bool_);
    uint64_t idp = T2(a, HOLR_ABS, p, p);
    uint64_t ybb = FUN(a, b.nfun, b.bool_, b.bool_), yEq = FUN(a, b.nfun, ybb, FUN(a, b.nfun, ybb, b.bool_));
    uint64_t eqbb = T2(a, HOLR_CONST, b.neq, yEq);
    uint64_t rhs = mk_eq(a, eqbb, idp, idp);
    uint64_t eqb = T2(a, HOLR_CONST, b.neq, FUN(a, b.nfun, b.bool_, FUN(a, b.nfun, b.bool_, b.bool_)));
    uint64_t *cn = (uint64_t *)malloc(n * sizeof(uint64_t)), *ty = (uint64_t *)malloc(n * sizeof(uint64_t));
    uint64_t last = 0;
    for (unsigned i = 0; i < n; i++) {
        snprintf(nm, sizeof nm, "ty%u", i);
        uint64_t nt = NAME(a, nm);
        op(a, HOLR_NEW_TYPE);
        uv(a, nt);
        uv(a, 0);
        ty[i] = TYAPP(a, nt, 0, NULL);
        snprintf(nm, sizeof nm, "c%u", i);
        cn[i] = NAME(a, nm);
        op(a, HOLR_NEW_CONSTANT);
        uv(a, cn[i]);
        uv(a, ty[i]);
        snprintf(nm, sizeof nm, "d%u", i);
        uint64_t dv = T2(a, HOLR_VAR, NAME(a, nm), b.bool_);
        last = H1(a, HOLR_NEW_DEFINITION, mk_eq(a, eqb, dv, rhs));
    }
    uint64_t lastref = 0;
    for (unsigned i = 0; i < n; i++) {
        uint64_t c = T2(a, HOLR_CONST, cn[i % 16], ty[i % 16]); /* the oldest declarations */
        lastref = H1(a, HOLR_REFL, c);
    }
    EMIT(a, last);
    EMIT(a, lastref);
    free(cn);
    free(ty);
}

static void gen_d1(Asm *a) {
    Base b;
    base(a, &b);
    uint64_t nx = NAME(a, "x"), nz = NAME(a, "z"), nf = NAME(a, "f");
    uint64_t xA = T2(a, HOLR_VAR, nx, b.A), zA = T2(a, HOLR_VAR, nz, b.A);
    uint64_t f = T2(a, HOLR_VAR, nf, b.AAA);
    uint64_t lam = T2(a, HOLR_ABS, zA, T2(a, HOLR_COMB, T2(a, HOLR_COMB, f, xA), zA));
    uint64_t th = H1(a, HOLR_REFL, lam);
    uint64_t ty[2] = {b.B, b.A};
    uint64_t thB = INSTR(a, HOLR_INST_TYPE, 1, ty, th);
    uint64_t xB = T2(a, HOLR_VAR, nx, b.B), zB = T2(a, HOLR_VAR, nz, b.B);
    uint64_t pr[4] = {xB, xB, zB, xB};
    uint64_t r = INSTR(a, HOLR_INST, 2, pr, thB);
    EMIT(a, r);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: gen-streams dag|deep|subst|hyps|history|d1 SIZE OUT.hrs\n");
        return 2;
    }
    unsigned size = (unsigned)strtoul(argv[2], NULL, 10);
    Asm a;
    memset(&a, 0, sizeof a);
    a.w = hol_writer_new(0, HOL_AXIOMS_OPEN);
    if (!strcmp(argv[1], "dag")) gen_dag(&a, size);
    else if (!strcmp(argv[1], "deep")) gen_deep(&a, size);
    else if (!strcmp(argv[1], "subst")) gen_subst(&a, size);
    else if (!strcmp(argv[1], "hyps")) gen_hyps(&a, size);
    else if (!strcmp(argv[1], "history")) gen_history(&a, size);
    else if (!strcmp(argv[1], "d1")) gen_d1(&a);
    else { fprintf(stderr, "unknown kind %s\n", argv[1]); return 2; }
    size_t len;
    const uint8_t *bytes = hol_writer_finish(a.w, &len);
    FILE *f = fopen(argv[3], "wb");
    if (!f || !bytes || fwrite(bytes, 1, len, f) != len) { perror(argv[3]); return 1; }
    fclose(f);
    printf("%s size=%u records=%llu bytes=%zu\n", argv[1], size,
           (unsigned long long)hol_writer_records(a.w), len);
    hol_writer_free(a.w);
    return 0;
}
