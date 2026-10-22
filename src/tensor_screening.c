#include "tensor_screening.h"

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

struct CettaTensorScreenIndex {
    _Atomic size_t holds;
    CettaTensor *source, *compact;
    size_t rows, columns;
    double *difference, *magnitude;
};

struct CettaTensorScreenReceipt {
    CettaTensorScreenIndex *index;
    CettaTensor *query, *mask, *candidates, *lower, *upper;
    size_t k;
    bool certified;
};

static bool rounding_admitted(void) {
#if FLT_EVAL_METHOD != 0 || defined(__FAST_MATH__)
    return false;
#else
    if (fegetround() != FE_TONEAREST) return false;
    /* Check gradual output and input in both precisions without assuming a
     * particular instruction set. Volatile operands prevent constant folding. */
    volatile double wide = DBL_MIN, half = .5, twice = 2.;
    volatile double small = wide * half;
    if (small * twice != DBL_MIN) return false;
    volatile float narrow = FLT_MIN, half32 = .5f, twice32 = 2.f;
    volatile float small32 = narrow * half32;
    return small32 * twice32 == FLT_MIN;
#endif
}

static bool current_rounding_admitted(void) {
    fenv_t saved;
    if (feholdexcept(&saved) != 0) return false;
    bool admitted = rounding_admitted();
    (void)fesetenv(&saved);
    return admitted;
}

/* These operations bound nonnegative real quantities outward. They are not
 * another public rounding schedule for tensor reductions. */
static double up(double value) {
    if (!isfinite(value)) return value;
    if (value == 0) return DBL_TRUE_MIN;
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    if (value > 0) bits++; else bits--;
    memcpy(&value, &bits, sizeof(value)); return value;
}
static double down(double value) {
    if (!isfinite(value)) return value;
    if (value == 0) return -DBL_TRUE_MIN;
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    if (value > 0) bits--; else bits++;
    memcpy(&value, &bits, sizeof(value)); return value;
}
static double add_up(double a, double b) { return up(a + b); }
static double mul_up(double a, double b) { return up(a * b); }

static bool descriptor_admitted(void) {
#define OBSERVE(name, policy) \
    if (!strcmp(name, "screen")) return !strcmp(policy, \
        "OwnerQueryMaskKBound|NearestGradualSequential|InclusiveThreshold|CoordinateAscendingTies|ExactRerank|FullScanFallback");
    CETTA_TENSOR_OBSERVATION_ROWS(OBSERVE)
#undef OBSERVE
    return false;
}

static CettaTensorStatus index_vector(uint64_t *values, size_t count, CettaTensor **out) {
    CettaTensorStatus status = cetta_tensor_new_take(CETTA_TENSOR_INDEX, &count, 1,
        values, count * sizeof(*values), out);
    if (status != CETTA_TENSOR_OK) free(values);
    return status;
}

static bool row_enabled(const CettaTensor *mask, size_t row) {
    if (!mask) return true;
    bool present; CettaTensorScalar scalar;
    return cetta_tensor_get(mask, row, &present, &scalar) == CETTA_TENSOR_OK &&
        present && scalar.u8;
}

static CettaTensorStatus eligible_rows(size_t rows, const CettaTensor *mask, CettaTensor **out) {
    CettaTensor *coordinates = NULL, *bits = NULL;
    size_t extent = rows;
    CettaTensorStatus status = CETTA_TENSOR_OK;
    if (mask && cetta_tensor_layout(mask) != CETTA_TENSOR_DENSE) {
        status = cetta_tensor_entries(mask, &coordinates, &bits);
        if (status != CETTA_TENSOR_OK) return status;
        extent = cetta_tensor_count(bits);
        mask = bits;
    }
    size_t count = mask ? 0 : extent;
    /* Reject impossible unmasked results before traversing their domain. */
    if (!mask && count > SIZE_MAX / sizeof(uint64_t)) return CETTA_TENSOR_OVERFLOW;
    if (mask) for (size_t i = 0; i < extent; i++) if (row_enabled(mask, i)) count++;
    uint64_t *values = NULL;
    if (count > SIZE_MAX / sizeof(*values)) { status = CETTA_TENSOR_OVERFLOW; goto done; }
    values = count ? malloc(count * sizeof(*values)) : NULL;
    if (count && !values) { status = CETTA_TENSOR_NOMEM; goto done; }
    size_t next = 0;
    for (size_t i = 0; i < extent; i++) if (row_enabled(mask, i)) {
        if (coordinates) {
            bool present; CettaTensorScalar key;
            status = cetta_tensor_get(coordinates, i, &present, &key);
            if (status != CETTA_TENSOR_OK || !present) {
                status = CETTA_TENSOR_VALUE; goto done;
            }
            values[next++] = key.index;
        } else values[next++] = i;
    }
    status = index_vector(values, count, out);
    values = NULL;
done:
    free(values); cetta_tensor_release(coordinates); cetta_tensor_release(bits);
    return status;
}

void cetta_tensor_screen_index_retain(CettaTensorScreenIndex *index) {
    if (index && atomic_fetch_add_explicit(&index->holds, 1, memory_order_relaxed) == SIZE_MAX)
        abort();
}

void cetta_tensor_screen_index_release(void *raw) {
    CettaTensorScreenIndex *index = raw;
    if (!index || atomic_fetch_sub_explicit(&index->holds, 1, memory_order_acq_rel) != 1)
        return;
    cetta_tensor_release(index->source);
    cetta_tensor_release(index->compact);
    free(index->difference);
    free(index->magnitude);
    free(index);
}

static void prepare_compact(CettaTensorScreenIndex *index) {
    if (!index->rows || !index->columns || !descriptor_admitted() || !rounding_admitted() ||
        cetta_tensor_dtype(index->source) != CETTA_TENSOR_F64 ||
        cetta_tensor_layout(index->source) != CETTA_TENSOR_DENSE ||
        !cetta_tensor_all_finite(index->source)) return;
    size_t count = cetta_tensor_count(index->source);
    if (count > SIZE_MAX / sizeof(double) || index->rows > SIZE_MAX / sizeof(double)) return;
    double *wide = count ? malloc(count * sizeof(*wide)) : NULL;
    float *narrow = count ? malloc(count * sizeof(*narrow)) : NULL;
    double *difference = index->rows ? malloc(index->rows * sizeof(*difference)) : NULL;
    double *magnitude = index->rows ? malloc(index->rows * sizeof(*magnitude)) : NULL;
    if ((count && (!wide || !narrow)) || (index->rows && (!difference || !magnitude)))
        goto decline;
    if (cetta_tensor_copy_values(index->source, wide, count * sizeof(*wide)) != CETTA_TENSOR_OK)
        goto decline;
    for (size_t row = 0; row < index->rows; row++) {
        double error = 0, absolute = 0;
        for (size_t col = 0; col < index->columns; col++) {
            size_t i = row * index->columns + col;
            if (fabs(wide[i]) > FLT_MAX) goto decline;
            narrow[i] = (float)wide[i];
            if (!isfinite(narrow[i])) goto decline;
            error = add_up(error, up(fabs(wide[i] - (double)narrow[i])));
            absolute = add_up(absolute, add_up(fabs(wide[i]), fabs((double)narrow[i])));
        }
        if (!isfinite(error) || !isfinite(absolute)) goto decline;
        difference[row] = error;
        magnitude[row] = absolute;
    }
    CettaTensor *compact = NULL;
    if (cetta_tensor_new_take(CETTA_TENSOR_F32, cetta_tensor_shape(index->source), 2,
            narrow, count * sizeof(*narrow), &compact) != CETTA_TENSOR_OK)
        goto decline;
    narrow = NULL;
    (void)cetta_tensor_all_finite(compact);
    index->compact = compact;
    index->difference = difference;
    index->magnitude = magnitude;
    free(wide);
    return;
decline:
    free(wide); free(narrow); free(difference); free(magnitude);
}

CettaTensorStatus cetta_tensor_screen_index(CettaTensor *source, CettaTensorScreenIndex **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!source || cetta_tensor_rank(source) != 2) return CETTA_TENSOR_SHAPE;
    if (cetta_tensor_dtype(source) != CETTA_TENSOR_F64 &&
        cetta_tensor_dtype(source) != CETTA_TENSOR_F32) return CETTA_TENSOR_DTYPE;
    CettaTensorScreenIndex *index = calloc(1, sizeof(*index));
    if (!index) return CETTA_TENSOR_NOMEM;
    atomic_init(&index->holds, 1);
    index->source = source; cetta_tensor_retain(source);
    index->rows = cetta_tensor_shape(source)[0];
    index->columns = cetta_tensor_shape(source)[1];
    fenv_t saved;
    if (feholdexcept(&saved) == 0) {
        prepare_compact(index);
        (void)fesetenv(&saved);
    }
    *out = index;
    return CETTA_TENSOR_OK;
}

void cetta_tensor_screen_receipt_release(void *raw) {
    CettaTensorScreenReceipt *receipt = raw;
    if (!receipt) return;
    cetta_tensor_screen_index_release(receipt->index);
    cetta_tensor_release(receipt->query);
    cetta_tensor_release(receipt->mask);
    cetta_tensor_release(receipt->candidates);
    cetta_tensor_release(receipt->lower);
    cetta_tensor_release(receipt->upper);
    free(receipt);
}

/* A trace admitted by the relative-plus-absolute model has row error at
 * most 4*n*(u*A+eta), provided n*u <= 1/4. Bound both exact-target and
 * compact traces, and add the source/compact coefficient discrepancy. */
static bool screen_bounds(CettaTensorScreenReceipt *receipt) {
    CettaTensorScreenIndex *index = receipt->index;
    const char *reference = getenv("CETTA_TENSOR_SCREEN_REFERENCE");
    if ((reference && *reference && strcmp(reference, "0")) ||
        !descriptor_admitted() || !index->compact || !rounding_admitted() ||
        cetta_tensor_dtype(receipt->query) != CETTA_TENSOR_F64 ||
        cetta_tensor_layout(receipt->query) != CETTA_TENSOR_DENSE ||
        !cetta_tensor_all_finite(receipt->query) ||
        index->columns > (UINT64_C(1) << (CETTA_TENSOR_PRECISION_F64 - 2)) ||
        index->columns > SIZE_MAX / sizeof(double) ||
        index->rows > SIZE_MAX / sizeof(double)) return false;
    double *query = index->columns ? malloc(index->columns * sizeof(*query)) : NULL;
    double *lower = index->rows ? malloc(index->rows * sizeof(*lower)) : NULL;
    double *upper = index->rows ? malloc(index->rows * sizeof(*upper)) : NULL;
    CettaTensor *approximate = NULL, *lo = NULL, *hi = NULL, *order = NULL;
    uint64_t *candidates = NULL;
    bool success = false;
    if ((index->columns && !query) || (index->rows && (!lower || !upper))) goto done;
    if (cetta_tensor_copy_values(receipt->query, query, index->columns * sizeof(*query)) != CETTA_TENSOR_OK)
        goto done;
    double qmax = 0;
    for (size_t col = 0; col < index->columns; col++)
        if (fabs(query[col]) > qmax) qmax = fabs(query[col]);
    if (cetta_tensor_matvec(index->compact, receipt->query, CETTA_TENSOR_SEQUENTIAL,
            &approximate) != CETTA_TENSOR_OK) goto done;
    double coefficient = mul_up(4. * (double)index->columns, DBL_EPSILON / 2.);
    double underflow = mul_up(8. * (double)index->columns, DBL_TRUE_MIN);
    for (size_t row = 0; row < index->rows; row++) {
        bool present; CettaTensorScalar scalar;
        if (cetta_tensor_get(approximate, row, &present, &scalar) != CETTA_TENSOR_OK || !present)
            goto done;
        double absolute = mul_up(qmax, index->magnitude[row]);
        double radius = add_up(add_up(mul_up(qmax, index->difference[row]),
            mul_up(coefficient, absolute)), underflow);
        if (!isfinite(absolute) || absolute > DBL_MAX / 8. ||
            !isfinite(scalar.f64) || !isfinite(radius)) goto done;
        lower[row] = down(scalar.f64 - radius);
        upper[row] = up(scalar.f64 + radius);
        if (!isfinite(lower[row]) || !isfinite(upper[row])) goto done;
    }
    if (cetta_tensor_new_take(CETTA_TENSOR_F64, &index->rows, 1, lower,
            index->rows * sizeof(*lower), &lo) != CETTA_TENSOR_OK) goto done;
    lower = NULL;
    if (cetta_tensor_new_take(CETTA_TENSOR_F64, &index->rows, 1, upper,
            index->rows * sizeof(*upper), &hi) != CETTA_TENSOR_OK) goto done;
    upper = NULL;
    if (!receipt->k || cetta_tensor_top_k(lo, receipt->mask, receipt->k, &order) != CETTA_TENSOR_OK)
        goto done;
    size_t k = cetta_tensor_count(order);
    if (!k) goto done;
    bool present; CettaTensorScalar ordinal, bound;
    if (cetta_tensor_get(order, k - 1, &present, &ordinal) != CETTA_TENSOR_OK || !present ||
        cetta_tensor_get(lo, (size_t)ordinal.index, &present, &bound) != CETTA_TENSOR_OK || !present)
        goto done;
    double threshold = bound.f64;
    size_t count = 0;
    for (size_t row = 0; row < index->rows; row++) {
        if (!row_enabled(receipt->mask, row)) continue;
        if (cetta_tensor_get(hi, row, &present, &bound) != CETTA_TENSOR_OK || !present)
            goto done;
        if (bound.f64 >= threshold) count++;
    }
    candidates = count ? malloc(count * sizeof(*candidates)) : NULL;
    if (count && !candidates) goto done;
    size_t next = 0;
    for (size_t row = 0; row < index->rows; row++) {
        if (!row_enabled(receipt->mask, row)) continue;
        if (cetta_tensor_get(hi, row, &present, &bound) != CETTA_TENSOR_OK || !present)
            goto done;
        if (bound.f64 >= threshold) candidates[next++] = row;
    }
    CettaTensor *selected = NULL;
    if (index_vector(candidates, count, &selected) != CETTA_TENSOR_OK) {
        candidates = NULL; goto done;
    }
    candidates = NULL;
    receipt->candidates = selected;
    receipt->lower = lo; lo = NULL;
    receipt->upper = hi; hi = NULL;
    receipt->certified = true;
    success = true;
done:
    free(query); free(lower); free(upper); free(candidates);
    cetta_tensor_release(approximate); cetta_tensor_release(lo);
    cetta_tensor_release(hi); cetta_tensor_release(order);
    return success;
}

CettaTensorStatus cetta_tensor_screen(CettaTensorScreenIndex *index,
    CettaTensor *query, CettaTensor *mask, size_t k, CettaTensorScreenReceipt **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!index || !query || cetta_tensor_rank(query) != 1 ||
        cetta_tensor_shape(query)[0] != index->columns ||
        cetta_tensor_stored(query) != index->columns) return CETTA_TENSOR_SHAPE;
    if (cetta_tensor_dtype(query) != CETTA_TENSOR_F32 &&
        cetta_tensor_dtype(query) != CETTA_TENSOR_F64) return CETTA_TENSOR_DTYPE;
    if (mask && (cetta_tensor_dtype(mask) != CETTA_TENSOR_BOOL ||
        cetta_tensor_rank(mask) != 1 || cetta_tensor_count(mask) != index->rows))
        return CETTA_TENSOR_SHAPE;
    CettaTensorScreenReceipt *receipt = calloc(1, sizeof(*receipt));
    if (!receipt) return CETTA_TENSOR_NOMEM;
    receipt->index = index; cetta_tensor_screen_index_retain(index);
    receipt->query = query; cetta_tensor_retain(query);
    receipt->mask = mask; cetta_tensor_retain(mask);
    receipt->k = k;
    fenv_t saved;
    if (feholdexcept(&saved) == 0) {
        (void)screen_bounds(receipt);
        (void)fesetenv(&saved);
    }
    if (!receipt->certified) {
        CettaTensorStatus status = eligible_rows(index->rows, mask, &receipt->candidates);
        if (status != CETTA_TENSOR_OK) { cetta_tensor_screen_receipt_release(receipt); return status; }
    }
    *out = receipt;
    return CETTA_TENSOR_OK;
}

/* Selected rows follow ascending source coordinates. Therefore local
 * top-k's ascending-position tie rule is the source-coordinate tie rule. */
CettaTensorStatus cetta_tensor_screen_rerank(const CettaTensorScreenReceipt *receipt,
    CettaTensor **indices, CettaTensor **scores) {
    if (!indices || !scores || indices == scores) return CETTA_TENSOR_VALUE;
    *indices = NULL; *scores = NULL;
    if (!receipt) return CETTA_TENSOR_VALUE;
    CettaTensor *values = NULL, *local = NULL, *eligible = NULL;
    CettaTensor *candidates = receipt->candidates;
    CettaTensorStatus status = CETTA_TENSOR_OK;
    bool admitted = receipt->certified && current_rounding_admitted();
    /* Exact eligibility is independent of numerical screening admission.
     * If admission changed, rebuild all eligible rows, not the old screen. */
    if (receipt->certified && !admitted && receipt->mask) {
        status = eligible_rows(receipt->index->rows, receipt->mask, &eligible);
        candidates = eligible;
    }
    if (status != CETTA_TENSOR_OK) return status;
    bool selected = (admitted || receipt->mask) &&
        cetta_tensor_count(candidates) < receipt->index->rows;
    if (selected) status = cetta_tensor_matvec_rows(receipt->index->source,
        receipt->query, candidates, &values);
    else status = cetta_tensor_matvec(receipt->index->source, receipt->query,
        CETTA_TENSOR_SEQUENTIAL, &values);
    if (status == CETTA_TENSOR_OK)
        status = cetta_tensor_top_k(values, selected ? NULL : receipt->mask, receipt->k, &local);
    if (status == CETTA_TENSOR_OK) {
        if (selected) status = cetta_tensor_gather(candidates, local, indices);
        else { *indices = local; cetta_tensor_retain(local); }
    }
    if (status == CETTA_TENSOR_OK) status = cetta_tensor_gather(values, local, scores);
    if (status != CETTA_TENSOR_OK) {
        cetta_tensor_release(*indices); cetta_tensor_release(*scores);
        *indices = NULL; *scores = NULL;
    }
    cetta_tensor_release(values); cetta_tensor_release(local); cetta_tensor_release(eligible);
    return status;
}

CettaTensor *cetta_tensor_screen_index_source(const CettaTensorScreenIndex *index) { return index ? index->source : NULL; }
bool cetta_tensor_screen_index_has_compact(const CettaTensorScreenIndex *index) { return index && index->compact; }
bool cetta_tensor_screen_is_certified(const CettaTensorScreenReceipt *receipt) { return receipt && receipt->certified; }
size_t cetta_tensor_screen_k(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->k : 0; }
CettaTensor *cetta_tensor_screen_query(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->query : NULL; }
CettaTensor *cetta_tensor_screen_mask(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->mask : NULL; }
CettaTensor *cetta_tensor_screen_source(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->index->source : NULL; }
CettaTensor *cetta_tensor_screen_candidates(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->candidates : NULL; }
CettaTensor *cetta_tensor_screen_lower(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->lower : NULL; }
CettaTensor *cetta_tensor_screen_upper(const CettaTensorScreenReceipt *receipt) { return receipt ? receipt->upper : NULL; }

static size_t size_add(size_t a, size_t b) { return a > SIZE_MAX - b ? SIZE_MAX : a + b; }
static size_t tensor_bytes(const CettaTensor *tensor) { return tensor ? cetta_tensor_retained_bytes(tensor) : 0; }

size_t cetta_tensor_screen_index_retained_bound(const CettaTensorScreenIndex *index) {
    if (!index) return 0;
    size_t total = size_add(sizeof(*index), tensor_bytes(index->source));
    total = size_add(total, tensor_bytes(index->compact));
    if (index->difference) total = size_add(total, index->rows * sizeof(double));
    if (index->magnitude) total = size_add(total, index->rows * sizeof(double));
    return total;
}

size_t cetta_tensor_screen_retained_bound(const CettaTensorScreenReceipt *receipt) {
    if (!receipt) return 0;
    size_t total = size_add(sizeof(*receipt), cetta_tensor_screen_index_retained_bound(receipt->index));
    total = size_add(total, tensor_bytes(receipt->query));
    total = size_add(total, tensor_bytes(receipt->mask));
    total = size_add(total, tensor_bytes(receipt->candidates));
    total = size_add(total, tensor_bytes(receipt->lower));
    return size_add(total, tensor_bytes(receipt->upper));
}
