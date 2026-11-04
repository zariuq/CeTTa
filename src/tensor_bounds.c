#include "tensor.h"
#include "numeric_bounds.h"

#include <stdlib.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

static double floating_value(CettaTensorDType dtype, CettaTensorScalar value) {
    return dtype == CETTA_TENSOR_F32 ? (double)value.f32 : value.f64;
}

/* The center and bound describe the same ordered stored-occurrence trace.
 * Missing coordinates add no floating operations; stored zeroes do. */
static CettaTensorStatus bounded_rows(const CettaTensor *matrix, const CettaTensor *vector,
    bool dot, double *center, double *lower, double *upper) {
    size_t rows = dot ? 1u : cetta_tensor_shape(matrix)[0];
    size_t columns = cetta_tensor_shape(vector)[0];
    CettaTensor *coordinates = NULL, *values = NULL;
    CettaTensorStatus status = cetta_tensor_entries(matrix, &coordinates, &values);
    if (status != CETTA_TENSOR_OK) return status;
    size_t stored = cetta_tensor_count(values), cursor = 0u;
    bool narrow = cetta_tensor_dtype(matrix) == CETTA_TENSOR_F32 ||
        cetta_tensor_dtype(vector) == CETTA_TENSOR_F32;
    if (stored && !cetta_numeric_rounding_admitted(narrow)) {
        status = CETTA_TENSOR_UNCERTIFIED; goto done;
    }
    if (!cetta_tensor_all_finite(matrix) || !cetta_tensor_all_finite(vector)) {
        status = CETTA_TENSOR_VALUE; goto done;
    }
    for (size_t row = 0u; row < rows; ++row) {
        double sum = 0., magnitude = 0.;
        size_t count = 0u;
        while (cursor < stored) {
            bool present;
            CettaTensorScalar key, left, right;
            status = cetta_tensor_get(coordinates, cursor, &present, &key);
            if (status != CETTA_TENSOR_OK || !present) { status = CETTA_TENSOR_VALUE; goto done; }
            if (!columns) { status = CETTA_TENSOR_SHAPE; goto done; }
            size_t coordinate = (size_t)key.index;
            size_t source_row = dot ? 0u : coordinate / columns;
            if (source_row != row) break;
            status = cetta_tensor_get(values, cursor, &present, &left);
            if (status != CETTA_TENSOR_OK || !present) { status = CETTA_TENSOR_VALUE; goto done; }
            status = cetta_tensor_get(vector, coordinate % columns, &present, &right);
            if (status != CETTA_TENSOR_OK || !present) { status = CETTA_TENSOR_SHAPE; goto done; }
            double a = floating_value(cetta_tensor_dtype(matrix), left);
            double b = floating_value(cetta_tensor_dtype(vector), right);
            double product = a * b;
            sum = sum + product;
            magnitude = cetta_numeric_add_up(magnitude,
                cetta_numeric_mul_up(fabs(a), fabs(b)));
            count++; cursor++;
            if (!isfinite(sum) || !isfinite(magnitude) || magnitude > DBL_MAX / 8. ||
                count > (UINT64_C(1) << 51)) {
                status = CETTA_TENSOR_UNCERTIFIED; goto done;
            }
        }
        center[row] = sum;
        if (!count) { lower[row] = upper[row] = 0.; continue; }
        double steps = 4. * (double)count;
        double coefficient = cetta_numeric_mul_up(steps, DBL_EPSILON / 2.);
        double main = cetta_numeric_mul_up(coefficient, magnitude);
        double tail = cetta_numeric_mul_up(steps, DBL_TRUE_MIN);
        double radius = cetta_numeric_add_up(main, tail);
        lower[row] = cetta_numeric_down(sum - radius);
        upper[row] = cetta_numeric_up(sum + radius);
        if (!isfinite(radius) || !isfinite(lower[row]) || !isfinite(upper[row])) {
            status = CETTA_TENSOR_UNCERTIFIED; goto done;
        }
    }
    if (cursor != stored) status = CETTA_TENSOR_VALUE;
done:
    cetta_tensor_release(coordinates); cetta_tensor_release(values);
    return status;
}

static CettaTensorStatus inputs(const CettaTensor *matrix, const CettaTensor *vector, bool dot) {
    if (!matrix || !vector) return CETTA_TENSOR_VALUE;
    if ((cetta_tensor_dtype(matrix) != CETTA_TENSOR_F64 && cetta_tensor_dtype(matrix) != CETTA_TENSOR_F32) ||
        (cetta_tensor_dtype(vector) != CETTA_TENSOR_F64 && cetta_tensor_dtype(vector) != CETTA_TENSOR_F32))
        return CETTA_TENSOR_DTYPE;
    if (cetta_tensor_rank(matrix) != (dot ? 1u : 2u) || cetta_tensor_rank(vector) != 1u ||
        cetta_tensor_shape(matrix)[dot ? 0u : 1u] != cetta_tensor_shape(vector)[0] ||
        cetta_tensor_stored(vector) != cetta_tensor_count(vector)) return CETTA_TENSOR_SHAPE;
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_dot_bounds(const CettaTensor *left, const CettaTensor *right,
    CettaFloatBounds *out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = (CettaFloatBounds){0};
    CettaTensorStatus status = inputs(left, right, true);
    if (status != CETTA_TENSOR_OK) return status;
    fenv_t saved;
    if (feholdexcept(&saved) != 0) return CETTA_TENSOR_UNCERTIFIED;
    CettaFloatBounds result = {0};
    status = bounded_rows(left, right, true, &result.value, &result.lower, &result.upper);
    if (fesetenv(&saved) != 0) return CETTA_TENSOR_UNCERTIFIED;
    if (status == CETTA_TENSOR_OK) *out = result;
    return status;
}

CettaTensorStatus cetta_tensor_matvec_bounds(const CettaTensor *matrix, const CettaTensor *vector,
    CettaTensor **value, CettaTensor **lower, CettaTensor **upper) {
    if (!value || !lower || !upper) return CETTA_TENSOR_VALUE;
    *value = *lower = *upper = NULL;
    if (value == lower || value == upper || lower == upper) return CETTA_TENSOR_VALUE;
    CettaTensorStatus status = inputs(matrix, vector, false);
    if (status != CETTA_TENSOR_OK) return status;
    size_t rows = cetta_tensor_shape(matrix)[0];
    if (rows > SIZE_MAX / sizeof(double)) return CETTA_TENSOR_OVERFLOW;
    double *mid = rows ? malloc(rows * sizeof(*mid)) : NULL;
    double *lo = rows ? malloc(rows * sizeof(*lo)) : NULL;
    double *hi = rows ? malloc(rows * sizeof(*hi)) : NULL;
    if (rows && (!mid || !lo || !hi)) { status = CETTA_TENSOR_NOMEM; goto done; }
    fenv_t saved;
    if (feholdexcept(&saved) != 0) { status = CETTA_TENSOR_UNCERTIFIED; goto done; }
    status = bounded_rows(matrix, vector, false, mid, lo, hi);
    if (fesetenv(&saved) != 0) status = CETTA_TENSOR_UNCERTIFIED;
    if (status != CETTA_TENSOR_OK) goto done;
    status = cetta_tensor_new_take(CETTA_TENSOR_F64, &rows, 1u, mid, rows * sizeof(*mid), value);
    if (status != CETTA_TENSOR_OK) goto done;
    mid = NULL;
    status = cetta_tensor_new_take(CETTA_TENSOR_F64, &rows, 1u, lo, rows * sizeof(*lo), lower);
    if (status != CETTA_TENSOR_OK) goto done;
    lo = NULL;
    status = cetta_tensor_new_take(CETTA_TENSOR_F64, &rows, 1u, hi, rows * sizeof(*hi), upper);
    if (status == CETTA_TENSOR_OK) hi = NULL;
done:
    free(mid); free(lo); free(hi);
    if (status != CETTA_TENSOR_OK) {
        cetta_tensor_release(*value); cetta_tensor_release(*lower); cetta_tensor_release(*upper);
        *value = *lower = *upper = NULL;
    }
    return status;
}
