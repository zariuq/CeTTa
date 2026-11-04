#include "tensor.h"

#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

static size_t checks;
#define CHECK(x) do { checks++; assert(x); } while (0)

static size_t refuse_allocation = SIZE_MAX, allocation_calls;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static bool refuse(void) {
    return refuse_allocation != SIZE_MAX && allocation_calls++ == refuse_allocation;
}
void *__wrap_malloc(size_t n) { return refuse() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t width) { return refuse() ? NULL : __real_calloc(n, width); }
void *__wrap_realloc(void *p, size_t n) { return refuse() ? NULL : __real_realloc(p, n); }

static CettaTensor *make(CettaTensorDType dtype, const size_t *shape, size_t rank,
    const void *data, size_t bytes) {
    CettaTensor *t = NULL;
    CHECK(cetta_tensor_new(dtype, shape, rank, data, bytes, &t) == CETTA_TENSOR_OK);
    return t;
}
static double at(const CettaTensor *t, size_t index) {
    CettaTensorScalar value; bool present;
    CHECK(cetta_tensor_get(t, index, &present, &value) == CETTA_TENSOR_OK && present);
    return cetta_tensor_dtype(t) == CETTA_TENSOR_F32 ? (double)value.f32 : value.f64;
}
static void exact_row(mpq_t result, const CettaTensor *m, const CettaTensor *v, size_t row, bool dot) {
    mpq_t a, b, product; mpq_inits(a, b, product, NULL);
    mpq_set_ui(result, 0u, 1u);
    size_t columns = cetta_tensor_shape(v)[0];
    for (size_t col = 0; col < columns; ++col) {
        CettaTensorScalar scalar; bool present;
        CHECK(cetta_tensor_get(m, dot ? col : row * columns + col, &present, &scalar) == CETTA_TENSOR_OK);
        if (!present) continue;
        double value = cetta_tensor_dtype(m) == CETTA_TENSOR_F32 ? (double)scalar.f32 : scalar.f64;
        mpq_set_d(a, value); mpq_set_d(b, at(v, col));
        mpq_mul(product, a, b); mpq_add(result, result, product);
    }
    mpq_clears(a, b, product, NULL);
}
static void encloses(double lower, double upper, const mpq_t expected) {
    mpq_t lo, hi; mpq_inits(lo, hi, NULL);
    CHECK(isfinite(lower) && isfinite(upper));
    mpq_set_d(lo, lower); mpq_set_d(hi, upper);
    CHECK(mpq_cmp(lo, expected) <= 0 && mpq_cmp(expected, hi) <= 0);
    mpq_clears(lo, hi, NULL);
}
static void dot_check(const CettaTensor *left, const CettaTensor *right) {
    CettaFloatBounds answer;
    CHECK(cetta_tensor_dot_bounds(left, right, &answer) == CETTA_TENSOR_OK);
    mpq_t expected; mpq_init(expected); exact_row(expected, left, right, 0, true);
    encloses(answer.lower, answer.upper, expected); mpq_clear(expected);
}
static void matvec_check(const CettaTensor *matrix, const CettaTensor *vector) {
    CettaTensor *mid = NULL, *lo = NULL, *hi = NULL;
    CHECK(cetta_tensor_matvec_bounds(matrix, vector, &mid, &lo, &hi) == CETTA_TENSOR_OK);
    CHECK(cetta_tensor_count(mid) == cetta_tensor_shape(matrix)[0]);
    mpq_t expected; mpq_init(expected);
    for (size_t row = 0; row < cetta_tensor_count(mid); ++row) {
        exact_row(expected, matrix, vector, row, false);
        encloses(at(lo, row), at(hi, row), expected);
    }
    mpq_clear(expected); cetta_tensor_release(mid); cetta_tensor_release(lo); cetta_tensor_release(hi);
}
static void cancellation_and_edges(void) {
    size_t n = 3u; double a[] = {1e16, 1., -1e16}, b[] = {1., 1., 1.};
    CettaTensor *left = make(CETTA_TENSOR_F64, &n, 1, a, sizeof(a));
    CettaTensor *right = make(CETTA_TENSOR_F64, &n, 1, b, sizeof(b));
    CettaFloatBounds value; CHECK(cetta_tensor_dot_bounds(left, right, &value) == CETTA_TENSOR_OK);
    CHECK(value.value == 0. && value.lower <= 1. && value.upper >= 1.);
    dot_check(left, right);
    CettaTensor *matrix = NULL; size_t shape[] = {1u, 3u};
    CHECK(cetta_tensor_reshape(left, shape, 2u, &matrix) == CETTA_TENSOR_OK);
    matvec_check(matrix, right);
    for (unsigned layout = CETTA_TENSOR_COO; layout <= CETTA_TENSOR_CSC; ++layout) {
        CettaTensor *sparse = NULL;
        CHECK(cetta_tensor_convert(matrix, (CettaTensorLayout)layout, &sparse) == CETTA_TENSOR_OK);
        matvec_check(sparse, right); cetta_tensor_release(sparse);
    }
    cetta_tensor_release(matrix); cetta_tensor_release(left); cetta_tensor_release(right);
    n = 1u;
    double x = 1. + 0x1p-27, y = 1. + 0x1p-26;
    left = make(CETTA_TENSOR_F64, &n, 1, &x, sizeof(x));
    right = make(CETTA_TENSOR_F64, &n, 1, &y, sizeof(y)); dot_check(left, right);
    cetta_tensor_release(left); cetta_tensor_release(right);
    x = DBL_TRUE_MIN; y = .5;
    left = make(CETTA_TENSOR_F64, &n, 1, &x, sizeof(x));
    right = make(CETTA_TENSOR_F64, &n, 1, &y, sizeof(y)); dot_check(left, right);
    cetta_tensor_release(left); cetta_tensor_release(right);
    float xf = FLT_TRUE_MIN, yf = FLT_MAX;
    left = make(CETTA_TENSOR_F32, &n, 1, &xf, sizeof(xf));
    right = make(CETTA_TENSOR_F32, &n, 1, &yf, sizeof(yf)); dot_check(left, right);
    cetta_tensor_release(left); cetta_tensor_release(right);
    n = 0u;
    left = make(CETTA_TENSOR_F64, &n, 1, NULL, 0u);
    right = make(CETTA_TENSOR_F64, &n, 1, NULL, 0u);
    CHECK(cetta_tensor_dot_bounds(left, right, &value) == CETTA_TENSOR_OK);
    CHECK(value.value == 0. && value.lower == 0. && value.upper == 0.);
    cetta_tensor_release(left); cetta_tensor_release(right);
}
static void omissions_views_and_refusals(void) {
    size_t shape[] = {2u, 2u}, n = 2u, coordinate = 2u;
    double zero = 0., vector[] = {2., -3.}; CettaTensor *matrix = NULL;
    CHECK(cetta_tensor_coo(CETTA_TENSOR_F64, shape, 2u, &coordinate, &zero, 1u,
        CETTA_TENSOR_REJECT, &matrix) == CETTA_TENSOR_OK);
    CettaTensor *v = make(CETTA_TENSOR_F64, &n, 1u, vector, sizeof(vector));
    CettaTensor *mid = NULL, *lo = NULL, *hi = NULL;
    CHECK(cetta_tensor_matvec_bounds(matrix, v, &mid, &lo, &hi) == CETTA_TENSOR_OK);
    CHECK(at(lo, 0u) == 0. && at(hi, 0u) == 0.);
    CHECK(at(lo, 1u) < 0. && at(hi, 1u) > 0.);
    cetta_tensor_release(mid); cetta_tensor_release(lo); cetta_tensor_release(hi); cetta_tensor_release(matrix);
    double data[] = {1., 2., 3., 4.};
    matrix = make(CETTA_TENSOR_F64, shape, 2u, data, sizeof(data));
    CettaTensor *view = NULL;
    CHECK(cetta_tensor_view(matrix, shape, (ptrdiff_t[]){-2, 1}, 2u, 2u, &view) == CETTA_TENSOR_OK);
    matvec_check(view, v); cetta_tensor_release(view);
    CHECK(cetta_tensor_matvec_bounds(matrix, v, &mid, &mid, &hi) == CETTA_TENSOR_VALUE);
    int mode = fegetround(); CHECK(fesetround(FE_DOWNWARD) == 0);
    CHECK(feraiseexcept(FE_INVALID) == 0); int flags = fetestexcept(FE_ALL_EXCEPT);
    CHECK(cetta_tensor_matvec_bounds(matrix, v, &mid, &lo, &hi) == CETTA_TENSOR_UNCERTIFIED);
    CHECK(!mid && !lo && !hi && fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == flags);
    CHECK(fesetround(mode) == 0); feclearexcept(FE_ALL_EXCEPT);
    cetta_tensor_release(matrix); cetta_tensor_release(v);
    n = 1u; double large = DBL_MAX, one = 1.;
    CettaTensor *a = make(CETTA_TENSOR_F64, &n, 1u, &large, sizeof(large));
    CettaTensor *b = make(CETTA_TENSOR_F64, &n, 1u, &one, sizeof(one));
    CettaFloatBounds scalar = {7., 8., 9.};
    CHECK(cetta_tensor_dot_bounds(a, b, &scalar) == CETTA_TENSOR_UNCERTIFIED);
    CHECK(scalar.value == 0. && scalar.lower == 0. && scalar.upper == 0.);
    cetta_tensor_release(a); cetta_tensor_release(b);
    double nan = NAN;
    a = make(CETTA_TENSOR_F64, &n, 1u, &nan, sizeof(nan));
    b = make(CETTA_TENSOR_F64, &n, 1u, &one, sizeof(one));
    CHECK(cetta_tensor_dot_bounds(a, b, &scalar) == CETTA_TENSOR_VALUE);
    cetta_tensor_release(a); cetta_tensor_release(b);
}
static uint64_t random_state = UINT64_C(0x8b57ba216832a4f1);
static uint64_t random_word(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 7; random_state ^= random_state << 17;
    return random_state;
}
static void independent_oracle(void) {
    for (size_t trial = 0u; trial < 240u; ++trial) {
        size_t n = trial % 23u, shape[] = {3u, n}; double a[69], b[23];
        for (size_t i = 0; i < 3u * n; ++i)
            a[i] = ldexp((double)((int)(random_word() % 33u) - 16), (int)(random_word() % 401u) - 200);
        for (size_t i = 0; i < n; ++i)
            b[i] = ldexp((double)((int)(random_word() % 33u) - 16), (int)(random_word() % 401u) - 200);
        CettaTensor *m = make(CETTA_TENSOR_F64, shape, 2u, a, 3u * n * sizeof(*a));
        CettaTensor *v = make(CETTA_TENSOR_F64, &n, 1u, b, n * sizeof(*b));
        matvec_check(m, v);
        CettaTensor *coo = NULL;
        CHECK(cetta_tensor_convert(m, CETTA_TENSOR_COO, &coo) == CETTA_TENSOR_OK);
        matvec_check(coo, v); cetta_tensor_release(coo);
        cetta_tensor_release(m); cetta_tensor_release(v);
    }
}
static void allocation_and_environment(void) {
    size_t shape[] = {1u, 3u}, n = 3u;
    double data[] = {1e16, 1., -1e16}, ones[] = {1., 1., 1.};
    CettaTensor *m = make(CETTA_TENSOR_F64, shape, 2u, data, sizeof(data));
    CettaTensor *v = make(CETTA_TENSOR_F64, &n, 1u, ones, sizeof(ones));
    size_t failures = 0u;
    for (size_t i = 0u; i < 128u; ++i) {
        CettaTensor *mid = NULL, *lo = NULL, *hi = NULL;
        allocation_calls = 0u; refuse_allocation = i;
        CettaTensorStatus status = cetta_tensor_matvec_bounds(m, v, &mid, &lo, &hi);
        refuse_allocation = SIZE_MAX;
        if (status == CETTA_TENSOR_OK) {
            CHECK(allocation_calls <= i);
            cetta_tensor_release(mid); cetta_tensor_release(lo); cetta_tensor_release(hi);
            break;
        }
        CHECK(status == CETTA_TENSOR_NOMEM && !mid && !lo && !hi);
        failures++;
    }
    CHECK(failures >= 6u && failures < 128u);
    CHECK(feraiseexcept(FE_INVALID) == 0);
    int flags = fetestexcept(FE_ALL_EXCEPT);
    matvec_check(m, v);
    CHECK(fetestexcept(FE_ALL_EXCEPT) == flags);
    feclearexcept(FE_ALL_EXCEPT);
#if defined(__SSE2__)
    unsigned saved = _mm_getcsr();
    for (size_t i = 0u; i < 2u; ++i) {
        unsigned mode = saved | (i ? 0x0040u : 0x8000u); /* DAZ, FTZ */
        _mm_setcsr(mode);
        CettaTensor *mid = NULL, *lo = NULL, *hi = NULL;
        CettaTensorStatus status = cetta_tensor_matvec_bounds(m, v, &mid, &lo, &hi);
        unsigned after = _mm_getcsr();
        _mm_setcsr(saved);
        CHECK(status == CETTA_TENSOR_UNCERTIFIED && !mid && !lo && !hi);
        CHECK(after == mode);
    }
#endif
    matvec_check(m, v);
    cetta_tensor_release(m); cetta_tensor_release(v);
}
int main(void) {
    CHECK(fesetround(FE_TONEAREST) == 0);
    cancellation_and_edges(); omissions_views_and_refusals(); independent_oracle();
    allocation_and_environment();
    printf("TensorBoundsNative %zu exact-rational, cancellation, support, fenv and refusal checks pass\n", checks);
    return 0;
}
