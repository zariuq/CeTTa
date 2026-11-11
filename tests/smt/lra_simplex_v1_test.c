static unsigned optima, unbounded_seen;

/* The simplex over bounds against its own checks: random linear forms over a
 * few reals and random bounds asserted one at a time, some undone.  After
 * each check, values are checked exactly against every bound in force, and
 * a conflict's explanation by the Farkas check; a bound set made of the
 * conflict's explanation and no more is refuted alone.  Also fixed cases:
 * strict bounds meeting, a pivot chain, a slack shared by equal forms. */
#include "lra_simplex_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static unsigned long long rng_state = 0x9E3779B97F4A7C15ull;

static uint32_t rnd(uint32_t n) {
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(rng_state >> 33) % n;
}

static void fail(const char *what, unsigned seed) {
    printf("FAIL: %s (case %u)\n", what, seed);
    failures++;
}

/* exact fractions for checking values: n / d, d > 0 */
typedef struct { __int128 n, d; } Frac;

static __int128 gcd128(__int128 a, __int128 b) {
    if (a < 0) a = -a;
    while (b) { __int128 t = a % b; a = b; b = t; }
    return a ? a : 1;
}
static Frac fr(__int128 n, __int128 d) {
    if (d < 0) { n = -n; d = -d; }
    __int128 g = gcd128(n, d);
    return (Frac){n / g, d / g};
}
static Frac parse_frac(const char *t) {
    long long n = 0, d = 1;
    if (strchr(t, '/'))
        sscanf(t, "%lld/%lld", &n, &d);
    else
        sscanf(t, "%lld", &n);
    return fr(n, d);
}
static Frac add(Frac a, Frac b) { return fr(a.n * b.d + b.n * a.d, a.d * b.d); }
static Frac mulk(Frac a, long long k) { return fr(a.n * k, a.d); }
static int cmp(Frac a, Frac b) {
    __int128 x = a.n * b.d, y = b.n * a.d;
    return (x > y) - (x < y);
}

enum { MAXV = 5, MAXF = 6, MAXB = 40, MAXT = 4 };

typedef struct {
    uint32_t n;
    uint32_t vars[MAXT];
    long long coefs[MAXT];
    char coef_text[MAXT][24];
    const char *coef_ptr[MAXT];
} TForm;

typedef struct {
    int form;      /* index into the forms */
    bool upper, strict;
    long long bound;
    char bound_text[24];
    char weight[64];
} TBound;

static void random_case(unsigned seed) {
    uint32_t nv = 1u + rnd(MAXV), nf = 1u + rnd(MAXF);
    TForm forms[MAXF];
    LraSolver *lra = lra_new(nv);
    int32_t col[MAXF];
    for (uint32_t f = 0u; f < nf; f++) {
        TForm *t = &forms[f];
        t->n = 1u + rnd(MAXT < nv ? MAXT : nv);
        for (uint32_t i = 0u; i < t->n; i++) {
            t->vars[i] = rnd(nv);
            t->coefs[i] = (long long)rnd(7) - 3;
            if (t->coefs[i] == 0)
                t->coefs[i] = 1;
            snprintf(t->coef_text[i], sizeof t->coef_text[i], "%lld", t->coefs[i]);
            t->coef_ptr[i] = t->coef_text[i];
        }
        col[f] = lra_form(lra, t->vars, t->coef_ptr, t->n);
        if (col[f] < 0) {
            fail("a form is refused", seed);
            lra_free(lra);
            return;
        }
    }
    TBound bounds[MAXB];
    uint32_t nb = 0u, levels[MAXB], nlevels = 0u;
    uint32_t heights[MAXB];
    uint32_t steps = 6u + rnd(30);
    for (uint32_t step = 0u; step < steps && nb < MAXB; step++) {
        if (nlevels && rnd(5) == 0) {          /* undo to a level */
            nlevels--;
            lra_undo(lra, heights[nlevels]);
            nb = levels[nlevels];
            continue;
        }
        if (rnd(3) == 0) {                     /* open a level */
            levels[nlevels] = nb;
            heights[nlevels] = lra_height(lra);
            nlevels++;
        }
        TBound *b = &bounds[nb];
        b->form = (int)rnd(nf);
        b->upper = rnd(2) == 0;
        b->strict = rnd(3) == 0;
        b->bound = (long long)rnd(13) - 6;
        snprintf(b->bound_text, sizeof b->bound_text, "%lld", b->bound);
        uint32_t before = lra_height(lra);
        LraResult r = lra_assert(lra, (uint32_t)col[b->form], b->upper, b->bound_text,
                                 b->strict, (int32_t)nb);
        nb++;
        if (r == LRA_OK)
            r = lra_check(lra);
        if (r == LRA_ERROR) {
            fail("an error", seed);
            break;
        }
        if (r == LRA_OK && rnd(3) == 0) {
            /* the least value of a form under the bounds in force: attained
             * by values meeting every bound (when the infimum is), and a
             * conflict, which the Farkas check accepts, below it */
            uint32_t f = rnd(nf);
            bool unb = false;
            char *c = NULL, *k = NULL;
            uint32_t h = lra_height(lra);
            if (lra_minimize(lra, (uint32_t)col[f], &unb, &c, &k) != LRA_OK) {
                fail("minimize failed", seed);
            } else if (unb) {
                unbounded_seen++;
                /* unbounded: far below every bound it is still feasible */
                LraResult u = lra_assert(lra, (uint32_t)col[f], true, "-100000", false, -1);
                if (u != LRA_OK || lra_check(lra) != LRA_OK)
                    fail("an unbounded form cannot go below -100000", seed);
            } else {
                optima++;
                bool attained = strcmp(k, "0") == 0;
                if (attained) {
                    Frac val[MAXV];
                    for (uint32_t v = 0u; v < nv; v++) {
                        char *text = lra_value(lra, v);
                        val[v] = text ? parse_frac(text) : (Frac){0, 1};
                        free(text);
                    }
                    const TForm *t = &forms[f];
                    Frac sum = {0, 1};
                    for (uint32_t i = 0u; i < t->n; i++)
                        sum = add(sum, mulk(val[t->vars[i]], t->coefs[i]));
                    if (cmp(sum, parse_frac(c)) != 0)
                        fail("the values do not attain the optimum", seed);
                }
                /* below the optimum: a conflict */
                LraResult below = lra_assert(lra, (uint32_t)col[f], true, c, attained, -2);
                if (below == LRA_OK)
                    below = lra_check(lra);
                if (below != LRA_CONFLICT) {
                    fail("a value below the optimum is feasible", seed);
                } else {
                    /* its explanation: bounds in force and the one below */
                    const int32_t *rs = NULL;
                    const char *const *ws = NULL;
                    uint32_t ne = lra_explanation(lra, &rs, &ws);
                    LraFarkasBound fb[MAXB + 1];
                    bool known = ne > 0u && ne <= MAXB + 1u;
                    for (uint32_t q = 0u; known && q < ne; q++) {
                        if (rs[q] == -2) {
                            const TForm *t = &forms[f];
                            fb[q] = (LraFarkasBound){t->vars, t->coef_ptr, t->n, true, attained, c, ws[q]};
                        } else if (rs[q] >= 0 && (uint32_t)rs[q] < nb) {
                            const TBound *b2 = &bounds[rs[q]];
                            const TForm *t = &forms[b2->form];
                            fb[q] = (LraFarkasBound){t->vars, t->coef_ptr, t->n, b2->upper, b2->strict,
                                                     b2->bound_text, ws[q]};
                        } else {
                            known = false;
                        }
                    }
                    if (!known || !lra_farkas_check(fb, ne))
                        fail("the conflict below the optimum fails the Farkas check", seed);
                }
            }
            free(c);
            free(k);
            lra_undo(lra, h);
            r = lra_check(lra);
        }
        if (r == LRA_OK) {
            /* values: every bound in force holds */
            Frac val[MAXV];
            for (uint32_t v = 0u; v < nv; v++) {
                char *text = lra_value(lra, v);
                if (!text) {
                    fail("no value", seed);
                    lra_free(lra);
                    return;
                }
                val[v] = parse_frac(text);
                free(text);
            }
            for (uint32_t k = 0u; k < nb; k++) {
                const TForm *t = &forms[bounds[k].form];
                Frac s = {0, 1};
                for (uint32_t i = 0u; i < t->n; i++)
                    s = add(s, mulk(val[t->vars[i]], t->coefs[i]));
                int c = cmp(s, fr(bounds[k].bound, 1));
                bool holds = bounds[k].upper ? (bounds[k].strict ? c < 0 : c <= 0)
                                             : (bounds[k].strict ? c > 0 : c >= 0);
                if (!holds)
                    fail("values break a bound in force", seed);
            }
            continue;
        }
        /* a conflict: its explanation's bounds, weighed, refute themselves */
        const int32_t *reasons = NULL;
        const char *const *coefs = NULL;
        uint32_t ne = lra_explanation(lra, &reasons, &coefs);
        LraFarkasBound fb[MAXB];
        bool known = ne > 0u;
        for (uint32_t k = 0u; k < ne && known; k++) {
            known = reasons[k] >= 0 && (uint32_t)reasons[k] < nb;
            if (!known)
                break;
            const TBound *b2 = &bounds[reasons[k]];
            const TForm *t = &forms[b2->form];
            fb[k] = (LraFarkasBound){t->vars, t->coef_ptr, t->n, b2->upper, b2->strict,
                                     b2->bound_text, coefs[k]};
        }
        if (!known)
            fail("an explanation names a bound not in force", seed);
        else if (!lra_farkas_check(fb, ne))
            fail("an explanation fails the Farkas check", seed);
        /* the checker refuses a weight changed */
        if (known && ne >= 2u) {
            LraFarkasBound mutated[MAXB];
            memcpy(mutated, fb, sizeof(*fb) * ne);
            mutated[0].weight = "0";
            bool still = lra_farkas_check(mutated, ne);
            /* with weight 0 the sum may still refute (a redundant bound);
             * dropping every weight cannot */
            for (uint32_t k = 0u; k < ne; k++)
                mutated[k].weight = "0";
            if (lra_farkas_check(mutated, ne))
                fail("zero weights pass the Farkas check", seed);
            (void)still;
        }
        /* the conflict is the last bound's: drop it and go on */
        nb--;
        lra_undo(lra, before);
    }
    lra_free(lra);
}

static void fixed_cases(void) {
    /* x < 1 and x > 1: refuted with strict bounds */
    LraSolver *lra = lra_new(1u);
    uint32_t x0 = 0u;
    const char *one[1] = {"1"};
    if (lra_form(lra, &x0, one, 1u) != 0)
        fail("a single variable is its own column", 0u);
    if (lra_assert(lra, 0u, true, "1", true, 7) != LRA_OK)
        fail("x < 1 alone", 0u);
    if (lra_assert(lra, 0u, false, "1", true, 8) != LRA_CONFLICT)
        fail("x < 1 and x > 1 meet", 0u);
    lra_free(lra);
    /* x + y <= 2, x - y >= 3, y >= 0: x >= 3 + y and x <= 2 - y, so
     * 3 + y <= 2 - y, y <= -1/2: refuted with y >= 0 */
    lra = lra_new(2u);
    uint32_t xy[2] = {0u, 1u};
    const char *pp[2] = {"1", "1"}, *pm[2] = {"1", "-1"};
    int32_t s = lra_form(lra, xy, pp, 2u), d = lra_form(lra, xy, pm, 2u);
    int32_t s2 = lra_form(lra, xy, pp, 2u);
    if (s < 2 || d < 2 || s2 != s)
        fail("equal forms share a slack", 0u);
    LraResult r = lra_assert(lra, (uint32_t)s, true, "2", false, 1);
    r = r == LRA_OK ? lra_assert(lra, (uint32_t)d, false, "3", false, 2) : r;
    r = r == LRA_OK ? lra_check(lra) : r;
    if (r != LRA_OK)
        fail("x + y <= 2, x - y >= 3 is satisfiable", 0u);
    char *xv = lra_value(lra, 0u), *yv = lra_value(lra, 1u);
    if (!xv || !yv)
        fail("values", 0u);
    free(xv);
    free(yv);
    uint32_t h = lra_height(lra);
    r = lra_assert(lra, 1u, false, "0", false, 3);
    r = r == LRA_OK ? lra_check(lra) : r;
    if (r != LRA_CONFLICT)
        fail("with y >= 0 it is not", 0u);
    lra_undo(lra, h);
    r = lra_check(lra);
    if (r != LRA_OK)
        fail("undone, it is again", 0u);
    lra_free(lra);
}

/* Two reals, nonstrict bounds a x + b y <= c with a box keeping the region
 * bounded: the least value of an objective is at a vertex, two of the
 * constraints met with equality, so brute force over the pairs gives it
 * exactly, independently of the simplex. */
static unsigned vertex_cases;

static void vertex_case(unsigned seed) {
    enum { M = 8 };
    long long a[M + 4], b[M + 4], c[M + 4];
    uint32_t m = 2u + rnd(M - 1u);
    for (uint32_t i = 0u; i < m; i++) {
        a[i] = (long long)rnd(9) - 4;
        b[i] = (long long)rnd(9) - 4;
        c[i] = (long long)rnd(21) - 6;
    }
    /* the box -10 <= x, y <= 10 */
    long long box[4][3] = {{1, 0, 10}, {-1, 0, 10}, {0, 1, 10}, {0, -1, 10}};
    for (uint32_t i = 0u; i < 4u; i++) {
        a[m + i] = box[i][0];
        b[m + i] = box[i][1];
        c[m + i] = box[i][2];
    }
    uint32_t n = m + 4u;
    long long oa = (long long)rnd(9) - 4, ob = (long long)rnd(9) - 4;
    /* brute force */
    bool any = false;
    Frac best = {0, 1};
    for (uint32_t i = 0u; i < n; i++)
        for (uint32_t j = i + 1u; j < n; j++) {
            __int128 det = (__int128)a[i] * b[j] - (__int128)a[j] * b[i];
            if (det == 0)
                continue;
            Frac x = fr((__int128)c[i] * b[j] - (__int128)c[j] * b[i], det);
            Frac y = fr((__int128)a[i] * c[j] - (__int128)a[j] * c[i], det);
            bool feasible = true;
            for (uint32_t k = 0u; feasible && k < n; k++)
                feasible = cmp(add(mulk(x, a[k]), mulk(y, b[k])), fr(c[k], 1)) <= 0;
            if (!feasible)
                continue;
            Frac v = add(mulk(x, oa), mulk(y, ob));
            if (!any || cmp(v, best) < 0)
                best = v;
            any = true;
        }
    /* the simplex */
    LraSolver *lra = lra_new(2u);
    uint32_t xy[2] = {0u, 1u};
    char ta[M + 4][2][24];
    int32_t col[M + 4];
    for (uint32_t i = 0u; i < n; i++) {
        snprintf(ta[i][0], sizeof ta[i][0], "%lld", a[i]);
        snprintf(ta[i][1], sizeof ta[i][1], "%lld", b[i]);
        const char *cf[2] = {ta[i][0], ta[i][1]};
        col[i] = lra_form(lra, xy, cf, 2u);
    }
    char oat[24], obt[24];
    snprintf(oat, sizeof oat, "%lld", oa);
    snprintf(obt, sizeof obt, "%lld", ob);
    const char *ocf[2] = {oat, obt};
    int32_t obj = lra_form(lra, xy, ocf, 2u);
    LraResult r = LRA_OK;
    for (uint32_t i = 0u; r == LRA_OK && i < n; i++) {
        char bt[24];
        snprintf(bt, sizeof bt, "%lld", c[i]);
        r = col[i] < 0 ? LRA_OK : lra_assert(lra, (uint32_t)col[i], true, bt, false, (int32_t)i);
        /* a constraint 0 <= c with both coefficients 0 */
    }
    if (r == LRA_OK)
        r = lra_check(lra);
    bool feasible = r == LRA_OK;
    if (feasible != any && obj >= 0)
        fail("the simplex and the vertices disagree on feasibility", seed);
    if (feasible && any && obj >= 0) {
        bool unb = false;
        char *cv = NULL, *kv = NULL;
        if (lra_minimize(lra, (uint32_t)obj, &unb, &cv, &kv) != LRA_OK || unb)
            fail("a bounded objective is not minimized", seed);
        else if (cmp(parse_frac(cv), best) != 0 || strcmp(kv, "0") != 0)
            fail("the optimum differs from the best vertex", seed);
        else
            vertex_cases++;
        free(cv);
        free(kv);
    }
    lra_free(lra);
}

int main(void) {
    fixed_cases();
    for (unsigned seed = 0u; seed < 2000u; seed++)
        vertex_case(seed);
    unsigned cases = 3000u;
    for (unsigned seed = 0u; seed < cases; seed++)
        random_case(seed);
    if (failures)
        return 1;
    printf("PASS: the simplex over bounds: fixed cases, and %u random bound sets with undo whose values meet every bound in force and whose conflicts the Farkas check accepts; %u optima attained where they are and refuted below, %u unbounded forms; %u two-variable optima equal to the best vertex\n",
           cases, optima, unbounded_seen, vertex_cases);
    return 0;
}
