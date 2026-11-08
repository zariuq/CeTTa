/* The general simplex over bounds (lra_simplex_v1.h). */
#include "lra_simplex_v1.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_BUILD_WITH_GMP
#define CETTA_BUILD_WITH_GMP 0
#endif

/* ---- exact rationals --------------------------------------------------- */

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>

typedef __mpq_struct lq_s;
#define lq_init(x) mpq_init(x)
#define lq_clear(x) mpq_clear(x)
#define lq_set(r, a) mpq_set(r, a)
#define lq_set_si(r, n) mpq_set_si(r, n, 1u)
#define lq_add(r, a, b) mpq_add(r, a, b)
#define lq_sub(r, a, b) mpq_sub(r, a, b)
#define lq_mul(r, a, b) mpq_mul(r, a, b)
#define lq_div(r, a, b) mpq_div(r, a, b)
#define lq_neg(r, a) mpq_neg(r, a)
#define lq_sgn(a) mpq_sgn(a)
#define lq_cmp(a, b) mpq_cmp(a, b)
#define lq_failed() false
#define lq_reset_failure() ((void)0)

static bool lq_parse(lq_s *r, const char *text) {
    if (!text || !*text)
        return false;
    for (const char *p = text; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '/'))
            return false;
    if (mpq_set_str(r, text, 10) != 0 || mpz_sgn(mpq_denref(r)) == 0)
        return false;
    mpq_canonicalize(r);
    return true;
}

static char *lq_text(const lq_s *a) {
    size_t size = mpz_sizeinbase(mpq_numref(a), 10) +
                  mpz_sizeinbase(mpq_denref(a), 10) + 3u;
    char *out = malloc(size);
    if (out)
        mpq_get_str(out, 10, a);
    return out;
}
#else
/* 64-bit fractions, d > 0 and in lowest terms; an operation whose exact
 * result does not fit sets the failure flag and gives 0. */
typedef struct {
    int64_t n;
    int64_t d;
} lq_s;

static _Thread_local bool lq_overflow;

#define lq_failed() lq_overflow
#define lq_reset_failure() (lq_overflow = false)

static void lq_init(lq_s *x) { x->n = 0; x->d = 1; }
static void lq_clear(lq_s *x) { (void)x; }
static void lq_set(lq_s *r, const lq_s *a) { *r = *a; }
static void lq_set_si(lq_s *r, int64_t n) { r->n = n; r->d = 1; }

static void lq_make(lq_s *r, __int128 n, __int128 d) {
    if (d == 0) {
        lq_overflow = true;
        r->n = 0;
        r->d = 1;
        return;
    }
    if (d < 0) {
        n = -n;
        d = -d;
    }
    __int128 a = n < 0 ? -n : n, b = d;
    while (b) {
        __int128 t = a % b;
        a = b;
        b = t;
    }
    if (a > 1) {
        n /= a;
        d /= a;
    }
    if (n > INT64_MAX || n < -INT64_MAX || d > INT64_MAX) {
        lq_overflow = true;
        r->n = 0;
        r->d = 1;
        return;
    }
    r->n = (int64_t)n;
    r->d = (int64_t)d;
}

static void lq_add(lq_s *r, const lq_s *a, const lq_s *b) {
    lq_make(r, (__int128)a->n * b->d + (__int128)b->n * a->d, (__int128)a->d * b->d);
}
static void lq_sub(lq_s *r, const lq_s *a, const lq_s *b) {
    lq_make(r, (__int128)a->n * b->d - (__int128)b->n * a->d, (__int128)a->d * b->d);
}
static void lq_mul(lq_s *r, const lq_s *a, const lq_s *b) {
    lq_make(r, (__int128)a->n * b->n, (__int128)a->d * b->d);
}
static void lq_div(lq_s *r, const lq_s *a, const lq_s *b) {
    lq_make(r, (__int128)a->n * b->d, (__int128)a->d * b->n);
}
static void lq_neg(lq_s *r, const lq_s *a) { r->n = -a->n; r->d = a->d; }
static int lq_sgn(const lq_s *a) { return (a->n > 0) - (a->n < 0); }
static int lq_cmp(const lq_s *a, const lq_s *b) {
    __int128 x = (__int128)a->n * b->d, y = (__int128)b->n * a->d;
    return (x > y) - (x < y);
}

static bool lq_parse(lq_s *r, const char *text) {
    if (!text || !*text)
        return false;
    char *end = NULL;
    errno = 0;
    long long n = strtoll(text, &end, 10);
    long long d = 1;
    if (end == text || errno || n == LLONG_MIN)
        return false;
    if (*end == '/') {
        const char *q = end + 1;
        if (!(*q >= '0' && *q <= '9'))
            return false;
        d = strtoll(q, &end, 10);
        if (errno || d <= 0)
            return false;
    }
    if (*end)
        return false;
    bool before = lq_overflow;
    lq_make(r, n, d);
    bool fits = !lq_overflow;
    lq_overflow = before;
    return fits;
}

static char *lq_text(const lq_s *a) {
    char *out = malloc(48u);
    if (!out)
        return NULL;
    if (a->d == 1)
        snprintf(out, 48u, "%lld", (long long)a->n);
    else
        snprintf(out, 48u, "%lld/%lld", (long long)a->n, (long long)a->d);
    return out;
}
#endif

typedef lq_s LQ[1];

/* ---- delta-rationals: c + k*delta ---------------------------------------- */

typedef struct {
    LQ c;
    LQ k;
} LD;

static void ld_init(LD *x) { lq_init(x->c); lq_init(x->k); }
static void ld_clear(LD *x) { lq_clear(x->c); lq_clear(x->k); }
static void ld_set(LD *r, const LD *a) { lq_set(r->c, a->c); lq_set(r->k, a->k); }
static int ld_cmp(const LD *a, const LD *b) {
    int c = lq_cmp(a->c, b->c);
    return c ? c : lq_cmp(a->k, b->k);
}

/* ---- the solver ----------------------------------------------------------- */

typedef struct {
    uint32_t n;
    uint32_t *vars;
    lq_s *coefs;
} Form;

typedef struct {
    uint32_t var;
    bool upper;
    bool had;
    int32_t reason;
    LD old;
} Undo;

struct LraSolver {
    uint32_t nvars;
    Form *forms;
    uint32_t nforms, capforms;
    bool frozen;
    uint32_t ncols;
    lq_s **rows;          /* per row, over every column */
    uint32_t *row_basic;  /* the row's basic variable */
    int32_t *row_of;      /* per variable: its row, or -1 when nonbasic */
    LD *beta;
    LD *lo, *hi;
    bool *has_lo, *has_hi;
    int32_t *lo_reason, *hi_reason;
    Undo *undo;
    uint32_t nundo, capundo;
    int32_t *exp_reasons;
    char **exp_coefs;
    uint32_t nexp, capexp;
};

LraSolver *lra_new(uint32_t nvars) {
    LraSolver *lra = calloc(1u, sizeof(*lra));
    if (lra)
        lra->nvars = nvars;
    return lra;
}

static void exp_clear(LraSolver *lra) {
    for (uint32_t i = 0u; i < lra->nexp; i++)
        free(lra->exp_coefs[i]);
    lra->nexp = 0u;
}

void lra_free(LraSolver *lra) {
    if (!lra)
        return;
    for (uint32_t f = 0u; f < lra->nforms; f++) {
        for (uint32_t i = 0u; i < lra->forms[f].n; i++)
            lq_clear(&lra->forms[f].coefs[i]);
        free(lra->forms[f].vars);
        free(lra->forms[f].coefs);
    }
    free(lra->forms);
    if (lra->frozen) {
        for (uint32_t r = 0u; r < lra->nforms; r++) {
            for (uint32_t c = 0u; c < lra->ncols; c++)
                lq_clear(&lra->rows[r][c]);
            free(lra->rows[r]);
        }
        for (uint32_t v = 0u; v < lra->ncols; v++) {
            ld_clear(&lra->beta[v]);
            ld_clear(&lra->lo[v]);
            ld_clear(&lra->hi[v]);
        }
    }
    free(lra->rows);
    free(lra->row_basic);
    free(lra->row_of);
    free(lra->beta);
    free(lra->lo);
    free(lra->hi);
    free(lra->has_lo);
    free(lra->has_hi);
    free(lra->lo_reason);
    free(lra->hi_reason);
    for (uint32_t i = 0u; i < lra->nundo; i++)
        ld_clear(&lra->undo[i].old);
    free(lra->undo);
    exp_clear(lra);
    free(lra->exp_reasons);
    free(lra->exp_coefs);
    free(lra);
}

static bool form_equal(const Form *f, const uint32_t *vars, const lq_s *coefs, uint32_t n) {
    if (f->n != n)
        return false;
    for (uint32_t i = 0u; i < n; i++)
        if (f->vars[i] != vars[i] || lq_cmp(&f->coefs[i], &coefs[i]) != 0)
            return false;
    return true;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

int32_t lra_form(LraSolver *lra, const uint32_t *vars, const char *const *coefs,
                 uint32_t n) {
    if (!lra || lra->frozen)
        return -1;
    /* the form summed per variable, in variable order, zeros left out */
    uint32_t *order = malloc(sizeof(*order) * (n ? n : 1u));
    uint32_t *fv = malloc(sizeof(*fv) * (n ? n : 1u));
    lq_s *fc = malloc(sizeof(*fc) * (n ? n : 1u));
    if (!order || !fv || !fc) {
        free(order);
        free(fv);
        free(fc);
        return -1;
    }
    bool ok = true;
    for (uint32_t i = 0u; i < n; i++) {
        order[i] = vars[i];
        ok = ok && vars[i] < lra->nvars;
    }
    qsort(order, n, sizeof(*order), cmp_u32);
    uint32_t m = 0u;
    for (uint32_t i = 0u; ok && i < n; i++)
        if (m == 0u || fv[m - 1u] != order[i]) {
            fv[m] = order[i];
            lq_init(&fc[m]);
            m++;
        }
    free(order);
    LQ c;
    lq_init(c);
    lq_reset_failure();
    for (uint32_t i = 0u; ok && i < n; i++) {
        ok = lq_parse(c, coefs[i]);
        uint32_t lo = 0u, hi = m;
        while (ok && lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (fv[mid] < vars[i])
                lo = mid + 1u;
            else
                hi = mid;
        }
        if (ok)
            lq_add(&fc[lo], &fc[lo], c);
    }
    lq_clear(c);
    ok = ok && !lq_failed();
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < m; i++) {
        if (ok && lq_sgn(&fc[i]) != 0) {
            fv[kept] = fv[i];
            if (kept != i) {
                lq_set(&fc[kept], &fc[i]);
            }
            kept++;
        }
    }
    for (uint32_t i = kept; i < m; i++)
        lq_clear(&fc[i]);
    int32_t result = -1;
    if (ok) {
        LQ one;
        lq_init(one);
        lq_set_si(one, 1);
        if (kept == 1u && lq_cmp(&fc[0], one) == 0)
            result = (int32_t)fv[0];
        lq_clear(one);
        for (uint32_t f = 0u; result < 0 && f < lra->nforms; f++)
            if (form_equal(&lra->forms[f], fv, fc, kept))
                result = (int32_t)(lra->nvars + f);
    }
    if (!ok || result >= 0) {
        for (uint32_t i = 0u; i < kept; i++)
            lq_clear(&fc[i]);
        free(fv);
        free(fc);
        return result;
    }
    if (lra->nforms == lra->capforms) {
        uint32_t cap = lra->capforms ? 2u * lra->capforms : 8u;
        Form *grown = realloc(lra->forms, sizeof(*grown) * cap);
        if (!grown) {
            for (uint32_t i = 0u; i < kept; i++)
                lq_clear(&fc[i]);
            free(fv);
            free(fc);
            return -1;
        }
        lra->forms = grown;
        lra->capforms = cap;
    }
    lra->forms[lra->nforms] = (Form){kept, fv, fc};
    return (int32_t)(lra->nvars + lra->nforms++);
}

/* The tableau of the forms, every slack basic, every real nonbasic at 0. */
static bool freeze(LraSolver *lra) {
    if (lra->frozen)
        return true;
    uint32_t nc = lra->nvars + lra->nforms, nr = lra->nforms;
    lra->ncols = nc;
    lra->rows = calloc(nr ? nr : 1u, sizeof(*lra->rows));
    lra->row_basic = malloc(sizeof(*lra->row_basic) * (nr ? nr : 1u));
    lra->row_of = malloc(sizeof(*lra->row_of) * (nc ? nc : 1u));
    lra->beta = malloc(sizeof(*lra->beta) * (nc ? nc : 1u));
    lra->lo = malloc(sizeof(*lra->lo) * (nc ? nc : 1u));
    lra->hi = malloc(sizeof(*lra->hi) * (nc ? nc : 1u));
    lra->has_lo = calloc(nc ? nc : 1u, sizeof(*lra->has_lo));
    lra->has_hi = calloc(nc ? nc : 1u, sizeof(*lra->has_hi));
    lra->lo_reason = malloc(sizeof(*lra->lo_reason) * (nc ? nc : 1u));
    lra->hi_reason = malloc(sizeof(*lra->hi_reason) * (nc ? nc : 1u));
    if (!lra->rows || !lra->row_basic || !lra->row_of || !lra->beta || !lra->lo ||
        !lra->hi || !lra->has_lo || !lra->has_hi || !lra->lo_reason || !lra->hi_reason)
        return false;
    for (uint32_t r = 0u; r < nr; r++) {
        lra->rows[r] = malloc(sizeof(**lra->rows) * (nc ? nc : 1u));
        if (!lra->rows[r]) {
            for (uint32_t q = 0u; q < r; q++) {
                for (uint32_t c = 0u; c < nc; c++)
                    lq_clear(&lra->rows[q][c]);
                free(lra->rows[q]);
                lra->rows[q] = NULL;
            }
            return false;
        }
    }
    for (uint32_t r = 0u; r < nr; r++) {
        for (uint32_t c = 0u; c < nc; c++)
            lq_init(&lra->rows[r][c]);
        const Form *f = &lra->forms[r];
        for (uint32_t i = 0u; i < f->n; i++)
            lq_set(&lra->rows[r][f->vars[i]], &f->coefs[i]);
        lra->row_basic[r] = lra->nvars + r;
    }
    for (uint32_t v = 0u; v < nc; v++) {
        lra->row_of[v] = v < lra->nvars ? -1 : (int32_t)(v - lra->nvars);
        ld_init(&lra->beta[v]);
        ld_init(&lra->lo[v]);
        ld_init(&lra->hi[v]);
        lra->lo_reason[v] = lra->hi_reason[v] = 0;
    }
    lra->frozen = true;
    return true;
}

static bool exp_add(LraSolver *lra, int32_t reason, const lq_s *coef) {
    if (lra->nexp == lra->capexp) {
        uint32_t cap = lra->capexp ? 2u * lra->capexp : 8u;
        int32_t *r = realloc(lra->exp_reasons, sizeof(*r) * cap);
        if (!r)
            return false;
        lra->exp_reasons = r;
        char **c = realloc(lra->exp_coefs, sizeof(*c) * cap);
        if (!c)
            return false;
        lra->exp_coefs = c;
        lra->capexp = cap;
    }
    char *text = lq_text(coef);
    if (!text)
        return false;
    lra->exp_reasons[lra->nexp] = reason;
    lra->exp_coefs[lra->nexp++] = text;
    return true;
}

uint32_t lra_explanation(const LraSolver *lra, const int32_t **reasons,
                         const char *const **coefs) {
    *reasons = lra->exp_reasons;
    *coefs = (const char *const *)lra->exp_coefs;
    return lra->nexp;
}

uint32_t lra_height(const LraSolver *lra) { return lra->nundo; }

void lra_undo(LraSolver *lra, uint32_t height) {
    while (lra->nundo > height) {
        Undo *u = &lra->undo[--lra->nundo];
        if (u->upper) {
            lra->has_hi[u->var] = u->had;
            ld_set(&lra->hi[u->var], &u->old);
            lra->hi_reason[u->var] = u->reason;
        } else {
            lra->has_lo[u->var] = u->had;
            ld_set(&lra->lo[u->var], &u->old);
            lra->lo_reason[u->var] = u->reason;
        }
        ld_clear(&u->old);
    }
}

/* beta[j] := v for nonbasic j, the basic variables following. */
static void update(LraSolver *lra, uint32_t j, const LD *v) {
    LD diff;
    ld_init(&diff);
    lq_sub(diff.c, v->c, lra->beta[j].c);
    lq_sub(diff.k, v->k, lra->beta[j].k);
    LQ t;
    lq_init(t);
    for (uint32_t r = 0u; r < lra->nforms; r++) {
        const lq_s *a = &lra->rows[r][j];
        if (lq_sgn(a) == 0)
            continue;
        LD *b = &lra->beta[lra->row_basic[r]];
        lq_mul(t, a, diff.c);
        lq_add(b->c, b->c, t);
        lq_mul(t, a, diff.k);
        lq_add(b->k, b->k, t);
    }
    ld_set(&lra->beta[j], v);
    lq_clear(t);
    ld_clear(&diff);
}

LraResult lra_assert(LraSolver *lra, uint32_t var, bool upper, const char *bound,
                     bool strict, int32_t reason) {
    if (!lra || !freeze(lra) || var >= lra->ncols)
        return LRA_ERROR;
    exp_clear(lra);
    lq_reset_failure();
    LD b;
    ld_init(&b);
    if (!lq_parse(b.c, bound)) {
        ld_clear(&b);
        return LRA_ERROR;
    }
    lq_set_si(b.k, strict ? (upper ? -1 : 1) : 0);
    LraResult result = LRA_OK;
    bool tighter = upper ? (!lra->has_hi[var] || ld_cmp(&b, &lra->hi[var]) < 0)
                         : (!lra->has_lo[var] || ld_cmp(&b, &lra->lo[var]) > 0);
    if (tighter) {
        /* crossing the opposite bound: the two bounds sum to 0 < 0 */
        bool crossed = upper ? lra->has_lo[var] && ld_cmp(&b, &lra->lo[var]) < 0
                             : lra->has_hi[var] && ld_cmp(&b, &lra->hi[var]) > 0;
        if (crossed) {
            LQ one;
            lq_init(one);
            lq_set_si(one, 1);
            bool ok = exp_add(lra, reason, one) &&
                      exp_add(lra, upper ? lra->lo_reason[var] : lra->hi_reason[var], one);
            lq_clear(one);
            ld_clear(&b);
            return ok ? LRA_CONFLICT : LRA_ERROR;
        }
        if (lra->nundo == lra->capundo) {
            uint32_t cap = lra->capundo ? 2u * lra->capundo : 16u;
            Undo *grown = realloc(lra->undo, sizeof(*grown) * cap);
            if (!grown) {
                ld_clear(&b);
                return LRA_ERROR;
            }
            lra->undo = grown;
            lra->capundo = cap;
        }
        Undo *u = &lra->undo[lra->nundo++];
        u->var = var;
        u->upper = upper;
        u->had = upper ? lra->has_hi[var] : lra->has_lo[var];
        u->reason = upper ? lra->hi_reason[var] : lra->lo_reason[var];
        ld_init(&u->old);
        ld_set(&u->old, upper ? &lra->hi[var] : &lra->lo[var]);
        if (upper) {
            ld_set(&lra->hi[var], &b);
            lra->has_hi[var] = true;
            lra->hi_reason[var] = reason;
        } else {
            ld_set(&lra->lo[var], &b);
            lra->has_lo[var] = true;
            lra->lo_reason[var] = reason;
        }
        /* a nonbasic variable stays within its bounds */
        if (lra->row_of[var] < 0 &&
            (upper ? ld_cmp(&lra->beta[var], &b) > 0 : ld_cmp(&lra->beta[var], &b) < 0))
            update(lra, var, &b);
    }
    ld_clear(&b);
    if (lq_failed())
        result = LRA_ERROR;
    return result;
}

/* Row r's basic variable takes value v, nonbasic j entering the basis. */
static void pivot_and_update(LraSolver *lra, uint32_t r, uint32_t j, const LD *v) {
    uint32_t i = lra->row_basic[r];
    const lq_s *a = &lra->rows[r][j];
    LD theta;
    ld_init(&theta);
    lq_sub(theta.c, v->c, lra->beta[i].c);
    lq_div(theta.c, theta.c, a);
    lq_sub(theta.k, v->k, lra->beta[i].k);
    lq_div(theta.k, theta.k, a);
    ld_set(&lra->beta[i], v);
    lq_add(lra->beta[j].c, lra->beta[j].c, theta.c);
    lq_add(lra->beta[j].k, lra->beta[j].k, theta.k);
    LQ t;
    lq_init(t);
    for (uint32_t q = 0u; q < lra->nforms; q++) {
        if (q == r || lq_sgn(&lra->rows[q][j]) == 0)
            continue;
        LD *b = &lra->beta[lra->row_basic[q]];
        lq_mul(t, &lra->rows[q][j], theta.c);
        lq_add(b->c, b->c, t);
        lq_mul(t, &lra->rows[q][j], theta.k);
        lq_add(b->k, b->k, t);
    }
    ld_clear(&theta);
    /* row r solved for x_j: x_j = (x_i - sum_{k != j} a_k x_k) / a_j */
    LQ inv;
    lq_init(inv);
    LQ one;
    lq_init(one);
    lq_set_si(one, 1);
    lq_div(inv, one, a);
    lq_s *row = lra->rows[r];
    for (uint32_t c = 0u; c < lra->ncols; c++) {
        if (c == j || lq_sgn(&row[c]) == 0)
            continue;
        lq_mul(&row[c], &row[c], inv);
        lq_neg(&row[c], &row[c]);
    }
    lq_set_si(&row[j], 0);
    lq_set(&row[i], inv);
    lra->row_basic[r] = j;
    lra->row_of[j] = (int32_t)r;
    lra->row_of[i] = -1;
    /* every other row's x_j replaced */
    for (uint32_t q = 0u; q < lra->nforms; q++) {
        if (q == r)
            continue;
        lq_s *other = lra->rows[q];
        if (lq_sgn(&other[j]) == 0)
            continue;
        LQ b;
        lq_init(b);
        lq_set(b, &other[j]);
        lq_set_si(&other[j], 0);
        for (uint32_t c = 0u; c < lra->ncols; c++) {
            if (lq_sgn(&row[c]) == 0)
                continue;
            lq_mul(t, b, &row[c]);
            lq_add(&other[c], &other[c], t);
        }
        lq_clear(b);
    }
    lq_clear(one);
    lq_clear(inv);
    lq_clear(t);
}

LraResult lra_check(LraSolver *lra) {
    if (!lra || !freeze(lra))
        return LRA_ERROR;
    exp_clear(lra);
    lq_reset_failure();
    for (;;) {
        if (lq_failed())
            return LRA_ERROR;
        /* Bland: the least basic variable out of its bounds */
        uint32_t best = UINT32_MAX, row = 0u;
        bool below = false;
        for (uint32_t r = 0u; r < lra->nforms; r++) {
            uint32_t x = lra->row_basic[r];
            if (x >= best)
                continue;
            if (lra->has_lo[x] && ld_cmp(&lra->beta[x], &lra->lo[x]) < 0) {
                best = x;
                row = r;
                below = true;
            } else if (lra->has_hi[x] && ld_cmp(&lra->beta[x], &lra->hi[x]) > 0) {
                best = x;
                row = r;
                below = false;
            }
        }
        if (best == UINT32_MAX)
            return LRA_OK;
        const lq_s *rr = lra->rows[row];
        /* the least nonbasic variable that can move the basic one back */
        uint32_t enter = UINT32_MAX;
        for (uint32_t j = 0u; j < lra->ncols && enter == UINT32_MAX; j++) {
            int s = lq_sgn(&rr[j]);
            if (s == 0 || lra->row_of[j] >= 0)
                continue;
            bool up = below ? s > 0 : s < 0;   /* x_j must increase */
            bool can = up ? (!lra->has_hi[j] || ld_cmp(&lra->beta[j], &lra->hi[j]) < 0)
                          : (!lra->has_lo[j] || ld_cmp(&lra->beta[j], &lra->lo[j]) > 0);
            if (can)
                enter = j;
        }
        if (enter == UINT32_MAX) {
            /* the row x = sum a_j x_j with every x_j at the bound that
             * keeps x out: those bounds and x's own, weighed by |a_j| and 1 */
            LQ w;
            lq_init(w);
            lq_set_si(w, 1);
            bool ok = exp_add(lra, below ? lra->lo_reason[best] : lra->hi_reason[best], w);
            for (uint32_t j = 0u; ok && j < lra->ncols; j++) {
                int s = lq_sgn(&rr[j]);
                if (s == 0 || lra->row_of[j] >= 0)
                    continue;
                bool at_upper = below ? s > 0 : s < 0;
                if (s > 0)
                    lq_set(w, &rr[j]);
                else
                    lq_neg(w, &rr[j]);
                ok = exp_add(lra, at_upper ? lra->hi_reason[j] : lra->lo_reason[j], w);
            }
            lq_clear(w);
            return ok && !lq_failed() ? LRA_CONFLICT : LRA_ERROR;
        }
        pivot_and_update(lra, row, enter, below ? &lra->lo[best] : &lra->hi[best]);
    }
}

/* How far x_j may move in direction d (+1 or -1) before it or a basic
 * variable meets a bound: *limit the least step and *leaving the row of the
 * basic variable that limits it (UINT32_MAX for x_j's own bound, or none);
 * false when nothing limits the move. */
static bool ratio_test(LraSolver *lra, uint32_t j, int d, LD *limit, uint32_t *leaving) {
    bool found = false;
    LD step;
    ld_init(&step);
    LQ rate;
    lq_init(rate);
    *leaving = UINT32_MAX;
    /* x_j's own bound */
    if (d < 0 ? lra->has_lo[j] : lra->has_hi[j]) {
        const LD *b = d < 0 ? &lra->lo[j] : &lra->hi[j];
        lq_sub(step.c, b->c, lra->beta[j].c);
        lq_sub(step.k, b->k, lra->beta[j].k);
        if (d < 0) {
            lq_neg(step.c, step.c);
            lq_neg(step.k, step.k);
        }
        ld_set(limit, &step);
        found = true;
    }
    /* each basic variable on x_j's column: x_i moves by a_ij * d per unit */
    uint32_t best_basic = UINT32_MAX;
    for (uint32_t r = 0u; r < lra->nforms; r++) {
        const lq_s *a = &lra->rows[r][j];
        if (lq_sgn(a) == 0)
            continue;
        uint32_t i = lra->row_basic[r];
        lq_set(rate, a);
        if (d < 0)
            lq_neg(rate, rate);
        bool up = lq_sgn(rate) > 0;
        if (up ? !lra->has_hi[i] : !lra->has_lo[i])
            continue;
        const LD *b = up ? &lra->hi[i] : &lra->lo[i];
        lq_sub(step.c, b->c, lra->beta[i].c);
        lq_sub(step.k, b->k, lra->beta[i].k);
        lq_div(step.c, step.c, rate);
        lq_div(step.k, step.k, rate);
        int c = found ? ld_cmp(&step, limit) : -1;
        if (c < 0 || (c == 0 && *leaving != UINT32_MAX && i < best_basic)) {
            ld_set(limit, &step);
            *leaving = r;
            best_basic = i;
            found = true;
        }
    }
    lq_clear(rate);
    ld_clear(&step);
    return found;
}

LraResult lra_minimize(LraSolver *lra, uint32_t var, bool *unbounded, char **c, char **k) {
    *unbounded = false;
    *c = *k = NULL;
    if (!lra || !freeze(lra) || var >= lra->ncols)
        return LRA_ERROR;
    lq_reset_failure();
    LD limit, target;
    ld_init(&limit);
    ld_init(&target);
    LraResult result = LRA_OK;
    for (;;) {
        if (lq_failed()) {
            result = LRA_ERROR;
            break;
        }
        /* the entering variable and its direction: var itself when
         * nonbasic, else the least nonbasic of var's row that can move so as
         * to lower var (Bland) */
        uint32_t enter = UINT32_MAX;
        int dir = 0;
        if (lra->row_of[var] < 0) {
            if (!lra->has_lo[var] || ld_cmp(&lra->beta[var], &lra->lo[var]) > 0) {
                enter = var;
                dir = -1;
            }
        } else {
            const lq_s *row = lra->rows[lra->row_of[var]];
            for (uint32_t j = 0u; j < lra->ncols && enter == UINT32_MAX; j++) {
                int sgn = lq_sgn(&row[j]);
                if (sgn == 0 || lra->row_of[j] >= 0)
                    continue;
                int d = sgn > 0 ? -1 : 1;    /* lower var: move x_j against a_j */
                bool can = d < 0 ? (!lra->has_lo[j] || ld_cmp(&lra->beta[j], &lra->lo[j]) > 0)
                                 : (!lra->has_hi[j] || ld_cmp(&lra->beta[j], &lra->hi[j]) < 0);
                if (can) {
                    enter = j;
                    dir = d;
                }
            }
        }
        if (enter == UINT32_MAX)
            break;                           /* no move lowers var: optimal */
        uint32_t leaving;
        if (!ratio_test(lra, enter, dir, &limit, &leaving)) {
            *unbounded = true;
            break;
        }
        if (leaving == UINT32_MAX) {
            /* x_enter reaches its own bound */
            ld_set(&target, dir < 0 ? &lra->lo[enter] : &lra->hi[enter]);
            update(lra, enter, &target);
        } else {
            /* the basic variable of row `leaving` reaches its bound and
             * leaves; x_enter enters */
            uint32_t i = lra->row_basic[leaving];
            LQ rate;
            lq_init(rate);
            lq_set(rate, &lra->rows[leaving][enter]);
            if (dir < 0)
                lq_neg(rate, rate);
            ld_set(&target, lq_sgn(rate) > 0 ? &lra->hi[i] : &lra->lo[i]);
            lq_clear(rate);
            pivot_and_update(lra, leaving, enter, &target);
        }
    }
    if (result == LRA_OK && !*unbounded) {
        *c = lq_text(lra->beta[var].c);
        *k = lq_text(lra->beta[var].k);
        if (!*c || !*k || lq_failed()) {
            free(*c);
            free(*k);
            *c = *k = NULL;
            result = LRA_ERROR;
        }
    }
    ld_clear(&limit);
    ld_clear(&target);
    return result;
}

char *lra_value(LraSolver *lra, uint32_t var) {
    if (!lra || !freeze(lra) || var >= lra->nvars)
        return NULL;
    lq_reset_failure();
    /* delta: no larger than 1, nor than any bound a strict part allows */
    LQ delta, t, u;
    lq_init(delta);
    lq_init(t);
    lq_init(u);
    lq_set_si(delta, 1);
    for (uint32_t v = 0u; v < lra->ncols; v++) {
        const LD *b = &lra->beta[v];
        if (lra->has_lo[v]) {
            const LD *l = &lra->lo[v];
            if (lq_cmp(l->c, b->c) < 0 && lq_cmp(l->k, b->k) > 0) {
                lq_sub(t, b->c, l->c);
                lq_sub(u, l->k, b->k);
                lq_div(t, t, u);
                if (lq_cmp(t, delta) < 0)
                    lq_set(delta, t);
            }
        }
        if (lra->has_hi[v]) {
            const LD *h = &lra->hi[v];
            if (lq_cmp(b->c, h->c) < 0 && lq_cmp(b->k, h->k) > 0) {
                lq_sub(t, h->c, b->c);
                lq_sub(u, b->k, h->k);
                lq_div(t, t, u);
                if (lq_cmp(t, delta) < 0)
                    lq_set(delta, t);
            }
        }
    }
    lq_mul(t, lra->beta[var].k, delta);
    lq_add(t, t, lra->beta[var].c);
    char *text = lq_failed() ? NULL : lq_text(t);
    lq_clear(delta);
    lq_clear(t);
    lq_clear(u);
    return text;
}

/* ---- linear forms for a front end ---------------------------------------- */

struct LraLinear {
    uint32_t *vars;    /* increasing */
    lq_s *coefs;
    uint32_t n, cap;
    LQ constant;
};

LraLinear *lra_linear_new(void) {
    LraLinear *lin = calloc(1u, sizeof(*lin));
    if (lin)
        lq_init(lin->constant);
    return lin;
}

void lra_linear_free(LraLinear *lin) {
    if (!lin)
        return;
    for (uint32_t i = 0u; i < lin->n; i++)
        lq_clear(&lin->coefs[i]);
    free(lin->vars);
    free(lin->coefs);
    lq_clear(lin->constant);
    free(lin);
}

/* lin's coefficient of var += c, kept sorted; zeros dropped */
static bool linear_add_q(LraLinear *lin, uint32_t var, const lq_s *c) {
    uint32_t lo = 0u, hi = lin->n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (lin->vars[mid] < var)
            lo = mid + 1u;
        else
            hi = mid;
    }
    if (lo < lin->n && lin->vars[lo] == var) {
        lq_add(&lin->coefs[lo], &lin->coefs[lo], c);
        if (lq_sgn(&lin->coefs[lo]) == 0) {
            lq_clear(&lin->coefs[lo]);
            memmove(&lin->vars[lo], &lin->vars[lo + 1u], sizeof(*lin->vars) * (lin->n - lo - 1u));
            memmove(&lin->coefs[lo], &lin->coefs[lo + 1u], sizeof(*lin->coefs) * (lin->n - lo - 1u));
            lin->n--;
        }
        return true;
    }
    if (lq_sgn(c) == 0)
        return true;
    if (lin->n == lin->cap) {
        uint32_t cap = lin->cap ? 2u * lin->cap : 4u;
        uint32_t *v = realloc(lin->vars, sizeof(*v) * cap);
        if (!v)
            return false;
        lin->vars = v;
        lq_s *q = realloc(lin->coefs, sizeof(*q) * cap);
        if (!q)
            return false;
        lin->coefs = q;
        lin->cap = cap;
    }
    memmove(&lin->vars[lo + 1u], &lin->vars[lo], sizeof(*lin->vars) * (lin->n - lo));
    memmove(&lin->coefs[lo + 1u], &lin->coefs[lo], sizeof(*lin->coefs) * (lin->n - lo));
    lin->vars[lo] = var;
    lq_init(&lin->coefs[lo]);
    lq_set(&lin->coefs[lo], c);
    lin->n++;
    return true;
}

LraLinear *lra_linear_copy(const LraLinear *src) {
    LraLinear *lin = lra_linear_new();
    if (!lin)
        return NULL;
    bool ok = true;
    for (uint32_t i = 0u; ok && i < src->n; i++)
        ok = linear_add_q(lin, src->vars[i], &src->coefs[i]);
    lq_set(lin->constant, src->constant);
    if (!ok) {
        lra_linear_free(lin);
        return NULL;
    }
    return lin;
}

bool lra_linear_add_term(LraLinear *lin, uint32_t var, const char *coef) {
    LQ c;
    lq_init(c);
    lq_reset_failure();
    bool ok = lq_parse(c, coef) && linear_add_q(lin, var, c) && !lq_failed();
    lq_clear(c);
    return ok;
}

bool lra_linear_add_constant(LraLinear *lin, const char *text) {
    LQ c;
    lq_init(c);
    lq_reset_failure();
    bool ok = lq_parse(c, text);
    if (ok)
        lq_add(lin->constant, lin->constant, c);
    ok = ok && !lq_failed();
    lq_clear(c);
    return ok;
}

bool lra_linear_add_scaled(LraLinear *dst, const LraLinear *src, const char *factor) {
    LQ f, t;
    lq_init(f);
    lq_init(t);
    lq_reset_failure();
    bool ok = lq_parse(f, factor);
    /* src may be dst: read it before writing */
    LraLinear *from = ok && src == dst ? lra_linear_copy(src) : (LraLinear *)src;
    ok = ok && from != NULL;
    for (uint32_t i = 0u; ok && i < from->n; i++) {
        lq_mul(t, f, &from->coefs[i]);
        ok = linear_add_q(dst, from->vars[i], t);
    }
    if (ok) {
        lq_mul(t, f, from->constant);
        lq_add(dst->constant, dst->constant, t);
    }
    if (from && from != src)
        lra_linear_free(from);
    ok = ok && !lq_failed();
    lq_clear(f);
    lq_clear(t);
    return ok;
}

bool lra_linear_is_constant(const LraLinear *lin, char **value) {
    if (lin->n)
        return false;
    *value = lq_text(lin->constant);
    return *value != NULL;
}

bool lra_linear_parts(const LraLinear *lin, uint32_t **vars, char ***coefs,
                      uint32_t *n, char **constant) {
    *vars = malloc(sizeof(**vars) * (lin->n ? lin->n : 1u));
    *coefs = calloc(lin->n ? lin->n : 1u, sizeof(**coefs));
    *n = lin->n;
    *constant = lq_text(lin->constant);
    bool ok = *vars && *coefs && *constant;
    for (uint32_t i = 0u; ok && i < lin->n; i++) {
        (*vars)[i] = lin->vars[i];
        (*coefs)[i] = lq_text(&lin->coefs[i]);
        ok = (*coefs)[i] != NULL;
    }
    if (!ok) {
        lra_parts_free(*vars, *coefs, lin->n, *constant);
        *vars = NULL;
        *coefs = NULL;
        *constant = NULL;
    }
    return ok;
}

void lra_parts_free(uint32_t *vars, char **coefs, uint32_t n, char *constant) {
    if (coefs)
        for (uint32_t i = 0u; i < n; i++)
            free(coefs[i]);
    free(coefs);
    free(vars);
    free(constant);
}

char *lra_decimal(const char *text) {
    if (!text || !*text)
        return NULL;
    const char *dot = strchr(text, '.');
    if (!dot) {
        LQ q;
        lq_init(q);
        char *out = lq_parse(q, text) ? lq_text(q) : NULL;
        lq_clear(q);
        return out;
    }
    size_t len = strlen(text), frac = len - (size_t)(dot - text) - 1u;
    if (frac == 0u || frac > 4000u)
        return NULL;
    for (const char *c = dot + 1; *c; c++)
        if (*c < '0' || *c > '9')
            return NULL;
    /* the digits without the point, over 10^frac */
    char *joined = malloc(len + frac + 4u);
    if (!joined)
        return NULL;
    size_t k = 0u;
    for (const char *c = text; *c; c++)
        if (c != dot)
            joined[k++] = *c;
    joined[k++] = '/';
    joined[k++] = '1';
    for (size_t i = 0u; i < frac; i++)
        joined[k++] = '0';
    joined[k] = '\0';
    LQ q;
    lq_init(q);
    lq_reset_failure();
    char *out = lq_parse(q, joined) && !lq_failed() ? lq_text(q) : NULL;
    lq_clear(q);
    free(joined);
    return out;
}

char *lra_reciprocal(const char *x) {
    LQ q, one;
    lq_init(q);
    lq_init(one);
    lq_set_si(one, 1);
    lq_reset_failure();
    char *out = NULL;
    if (lq_parse(q, x) && lq_sgn(q) != 0) {
        lq_div(q, one, q);
        if (!lq_failed())
            out = lq_text(q);
    }
    lq_clear(q);
    lq_clear(one);
    return out;
}

char *lra_negation(const char *x) {
    LQ q;
    lq_init(q);
    char *out = NULL;
    if (lq_parse(q, x)) {
        lq_neg(q, q);
        out = lq_text(q);
    }
    lq_clear(q);
    return out;
}

/* ---- evaluation ------------------------------------------------------------ */

bool lra_compare(const uint32_t *vars, const char *const *coefs, uint32_t n,
                 const char *const *values, uint32_t nvalues, const char *bound,
                 int *sign) {
    LQ sum, c, v;
    lq_init(sum);
    lq_init(c);
    lq_init(v);
    lq_reset_failure();
    bool ok = values != NULL || n == 0u;
    for (uint32_t i = 0u; ok && i < n; i++) {
        ok = vars[i] < nvalues && lq_parse(c, coefs[i]) && lq_parse(v, values[vars[i]]);
        if (ok) {
            lq_mul(c, c, v);
            lq_add(sum, sum, c);
        }
    }
    ok = ok && lq_parse(c, bound);
    if (ok) {
        lq_sub(sum, sum, c);
        *sign = lq_sgn(sum);
    }
    ok = ok && !lq_failed();
    lq_clear(sum);
    lq_clear(c);
    lq_clear(v);
    return ok;
}

/* ---- the Farkas check ----------------------------------------------------- */

bool lra_farkas_check(const LraFarkasBound *bounds, uint32_t n) {
    uint32_t width = 0u;
    for (uint32_t b = 0u; b < n; b++)
        for (uint32_t i = 0u; i < bounds[b].n; i++)
            if (bounds[b].vars[i] + 1u > width)
                width = bounds[b].vars[i] + 1u;
    lq_s *sum = malloc(sizeof(*sum) * (width ? width : 1u));
    if (!sum)
        return false;
    for (uint32_t v = 0u; v < width; v++)
        lq_init(&sum[v]);
    LQ total, w, c, t;
    lq_init(total);
    lq_init(w);
    lq_init(c);
    lq_init(t);
    lq_reset_failure();
    bool ok = true, strict = false;
    for (uint32_t b = 0u; ok && b < n; b++) {
        const LraFarkasBound *fb = &bounds[b];
        ok = lq_parse(w, fb->weight) && lq_sgn(w) >= 0 && lq_parse(c, fb->bound);
        if (!ok)
            break;
        if (!fb->upper)     /* form >= c is -form <= -c */
            lq_neg(w, w);
        lq_mul(t, w, c);
        lq_add(total, total, t);
        for (uint32_t i = 0u; ok && i < fb->n; i++) {
            ok = lq_parse(c, fb->coefs[i]);
            if (ok) {
                lq_mul(t, w, c);
                lq_add(&sum[fb->vars[i]], &sum[fb->vars[i]], t);
            }
        }
        strict = strict || (fb->strict && lq_sgn(w) != 0);
    }
    for (uint32_t v = 0u; ok && v < width; v++)
        ok = lq_sgn(&sum[v]) == 0;
    ok = ok && !lq_failed() &&
         (lq_sgn(total) < 0 || (lq_sgn(total) == 0 && strict));
    for (uint32_t v = 0u; v < width; v++)
        lq_clear(&sum[v]);
    free(sum);
    lq_clear(total);
    lq_clear(w);
    lq_clear(c);
    lq_clear(t);
    return ok;
}
