#define _POSIX_C_SOURCE 200809L
#include "tensor_screening.h"

#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static size_t checks;
#define CHECK(test) do { checks++; assert(test); } while (0)

static CettaTensor *tensor(CettaTensorDType dtype, const size_t *shape,
    size_t rank, const void *values, size_t bytes) {
    CettaTensor *result = NULL;
    CHECK(cetta_tensor_new(dtype, shape, rank, values, bytes, &result) == CETTA_TENSOR_OK);
    return result;
}

static void compare(const CettaTensorScreenReceipt *receipt) {
    CettaTensor *full = NULL, *expected_indices = NULL, *expected_scores = NULL;
    CettaTensor *actual_indices = NULL, *actual_scores = NULL;
    CettaTensorStatus reference = cetta_tensor_matvec(cetta_tensor_screen_source(receipt),
        cetta_tensor_screen_query(receipt), CETTA_TENSOR_SEQUENTIAL, &full);
    if (reference == CETTA_TENSOR_OK)
        reference = cetta_tensor_top_k(full, cetta_tensor_screen_mask(receipt),
            cetta_tensor_screen_k(receipt), &expected_indices);
    if (reference == CETTA_TENSOR_OK)
        reference = cetta_tensor_gather(full, expected_indices, &expected_scores);
    CettaTensorStatus actual = cetta_tensor_screen_rerank(receipt, &actual_indices, &actual_scores);
    CHECK(actual == reference);
    if (reference == CETTA_TENSOR_OK) {
        CHECK(cetta_tensor_equal(actual_indices, expected_indices));
        CHECK(cetta_tensor_equal(actual_scores, expected_scores));
    } else CHECK(!actual_indices && !actual_scores);
    if (cetta_tensor_screen_is_certified(receipt)) {
        CHECK(reference == CETTA_TENSOR_OK);
        CettaTensor *lower = cetta_tensor_screen_lower(receipt);
        CettaTensor *upper = cetta_tensor_screen_upper(receipt);
        CHECK(lower && upper && cetta_tensor_count(lower) == cetta_tensor_count(full));
        for (size_t i = 0; i < cetta_tensor_count(full); i++) {
            bool present; CettaTensorScalar score, low, high;
            CHECK(cetta_tensor_get(full, i, &present, &score) == CETTA_TENSOR_OK && present);
            CHECK(cetta_tensor_get(lower, i, &present, &low) == CETTA_TENSOR_OK && present);
            CHECK(cetta_tensor_get(upper, i, &present, &high) == CETTA_TENSOR_OK && present);
            CHECK(low.f64 <= score.f64 && score.f64 <= high.f64);
        }
    }
    cetta_tensor_release(full); cetta_tensor_release(expected_indices);
    cetta_tensor_release(expected_scores); cetta_tensor_release(actual_indices);
    cetta_tensor_release(actual_scores);
}

static CettaTensorScreenReceipt *screen(CettaTensor *matrix, CettaTensor *query, size_t k,
    bool compact) {
    CettaTensorScreenIndex *index = NULL;
    CettaTensorScreenReceipt *receipt = NULL;
    CHECK(cetta_tensor_screen_index(matrix, &index) == CETTA_TENSOR_OK);
    CHECK(cetta_tensor_screen_index_source(index) == matrix);
    CHECK(cetta_tensor_screen_index_has_compact(index) == compact);
    CHECK(cetta_tensor_screen(index, query, NULL, k, &receipt) == CETTA_TENSOR_OK);
    CHECK(cetta_tensor_screen_source(receipt) == matrix);
    CHECK(cetta_tensor_screen_query(receipt) == query);
    cetta_tensor_screen_index_release(index);
    return receipt;
}

static void basic(void) {
    size_t shape[] = {3, 2}, width = 2;
    double values[] = {10, 1, 3, -2, -5, 1}, q[] = {1, 2};
    CettaTensor *matrix = tensor(CETTA_TENSOR_F64, shape, 2, values, sizeof(values));
    CettaTensor *query = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    CettaTensorScreenReceipt *receipt = screen(matrix, query, 1, true);
    CHECK(cetta_tensor_screen_is_certified(receipt));
    CHECK(cetta_tensor_count(cetta_tensor_screen_candidates(receipt)) == 1);
    /* The receipt, not these caller references or the index handle, owns
     * the source and query until its eventual release. */
    cetta_tensor_release(matrix); cetta_tensor_release(query);
    compare(receipt);
    cetta_tensor_screen_receipt_release(receipt);

    double tied[] = {1, 2, 1, 2, 0, 0}, zeros[] = {-0., 0.};
    matrix = tensor(CETTA_TENSOR_F64, shape, 2, tied, sizeof(tied));
    query = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    receipt = screen(matrix, query, 1, true);
    CHECK(cetta_tensor_count(cetta_tensor_screen_candidates(receipt)) == 2);
    compare(receipt); cetta_tensor_screen_receipt_release(receipt);
    receipt = screen(matrix, query, 0, true);
    CHECK(!cetta_tensor_screen_is_certified(receipt));
    compare(receipt); cetta_tensor_screen_receipt_release(receipt);
    receipt = screen(matrix, query, 99, true);
    compare(receipt); cetta_tensor_screen_receipt_release(receipt);
    CettaTensor *zero_query = tensor(CETTA_TENSOR_F64, &width, 1, zeros, sizeof(zeros));
    receipt = screen(matrix, zero_query, 1, true);
    CHECK(cetta_tensor_count(cetta_tensor_screen_candidates(receipt)) == 3);
    compare(receipt); cetta_tensor_screen_receipt_release(receipt);
    cetta_tensor_release(zero_query); cetta_tensor_release(matrix); cetta_tensor_release(query);
}

static void fallback(void) {
    size_t shape[] = {3, 2}, width = 2;
    float compact[] = {10, 1, 3, -2, -5, 1};
    double q[] = {1, 2}, huge[] = {1e200, 0, -1e200, 0, 0, 0};
    CettaTensor *query = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    CettaTensor *matrix = tensor(CETTA_TENSOR_F32, shape, 2, compact, sizeof(compact));
    CettaTensorScreenReceipt *receipt = screen(matrix, query, 2, false);
    CHECK(!cetta_tensor_screen_is_certified(receipt)); compare(receipt);
    cetta_tensor_screen_receipt_release(receipt); cetta_tensor_release(matrix);
    matrix = tensor(CETTA_TENSOR_F64, shape, 2, huge, sizeof(huge));
    receipt = screen(matrix, query, 2, false);
    CHECK(!cetta_tensor_screen_is_certified(receipt)); compare(receipt);
    cetta_tensor_screen_receipt_release(receipt); cetta_tensor_release(matrix);

    size_t coordinates[] = {0, 3, 5}; double sparse[] = {-0., 2, -1};
    matrix = NULL;
    CHECK(cetta_tensor_coo(CETTA_TENSOR_F64, shape, 2, coordinates, sparse, 3,
        CETTA_TENSOR_REJECT, &matrix) == CETTA_TENSOR_OK);
    receipt = screen(matrix, query, 2, false);
    CHECK(!cetta_tensor_screen_is_certified(receipt)); compare(receipt);
    cetta_tensor_screen_receipt_release(receipt); cetta_tensor_release(matrix);

    double special[] = {INFINITY, 1, NAN, 1, 0, 0};
    matrix = tensor(CETTA_TENSOR_F64, shape, 2, special, sizeof(special));
    receipt = screen(matrix, query, 1, false);
    CHECK(!cetta_tensor_screen_is_certified(receipt)); compare(receipt);
    cetta_tensor_screen_receipt_release(receipt);
    uint8_t bits[] = {0, 0, 1}; size_t rows = 3;
    CettaTensor *mask = tensor(CETTA_TENSOR_BOOL, &rows, 1, bits, sizeof(bits));
    CettaTensorScreenIndex *index = NULL;
    CHECK(cetta_tensor_screen_index(matrix, &index) == CETTA_TENSOR_OK);
    CHECK(cetta_tensor_screen(index, query, mask, 1, &receipt) == CETTA_TENSOR_OK);
    compare(receipt);
    cetta_tensor_screen_receipt_release(receipt); cetta_tensor_screen_index_release(index);
    cetta_tensor_release(mask); cetta_tensor_release(matrix);
    cetta_tensor_release(query);
}

static void environments(void) {
    size_t shape[] = {3, 2}, width = 2;
    double values[] = {10, 1, 3, -2, -5, 1}, q[] = {1, 2};
    CettaTensor *matrix = tensor(CETTA_TENSOR_F64, shape, 2, values, sizeof(values));
    CettaTensor *query = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    CettaTensorScreenIndex *index = NULL;
    CHECK(cetta_tensor_screen_index(matrix, &index) == CETTA_TENSOR_OK);
    CettaTensorScreenReceipt *receipt = NULL;
    CHECK(cetta_tensor_screen(index, query, NULL, 1, &receipt) == CETTA_TENSOR_OK);
    CHECK(cetta_tensor_screen_is_certified(receipt));
    CHECK(fesetround(FE_DOWNWARD) == 0);
    compare(receipt);
    cetta_tensor_screen_receipt_release(receipt);
    CHECK(cetta_tensor_screen(index, query, NULL, 1, &receipt) == CETTA_TENSOR_OK);
    CHECK(!cetta_tensor_screen_is_certified(receipt)); compare(receipt);
    cetta_tensor_screen_receipt_release(receipt);
    CettaTensorScreenIndex *reference = NULL;
    CHECK(cetta_tensor_screen_index(matrix, &reference) == CETTA_TENSOR_OK);
    CHECK(!cetta_tensor_screen_index_has_compact(reference));
    CHECK(fesetround(FE_TONEAREST) == 0);
    CHECK(cetta_tensor_screen(reference, query, NULL, 1, &receipt) == CETTA_TENSOR_OK);
    CHECK(!cetta_tensor_screen_is_certified(receipt)); compare(receipt);
    cetta_tensor_screen_receipt_release(receipt);
    cetta_tensor_screen_index_release(reference); cetta_tensor_screen_index_release(index);
    cetta_tensor_release(matrix); cetta_tensor_release(query);
}

static void views_and_empty(void) {
    size_t shape[] = {3, 2}, width = 2; ptrdiff_t strides[] = {-2, 1};
    double values[] = {10, 1, 3, -2, -5, 1}, q[] = {1, 2};
    CettaTensor *source = tensor(CETTA_TENSOR_F64, shape, 2, values, sizeof(values));
    CettaTensor *view = NULL, *query = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    CHECK(cetta_tensor_view(source, shape, strides, 2, 4, &view) == CETTA_TENSOR_OK);
    CettaTensorScreenReceipt *receipt = screen(view, query, 1, true);
    cetta_tensor_release(source); cetta_tensor_release(view); cetta_tensor_release(query);
    compare(receipt); cetta_tensor_screen_receipt_release(receipt);
    for (size_t rows = 0; rows < 3; rows++) {
        size_t empty_shape[] = {rows, 0}, zero = 0;
        source = tensor(CETTA_TENSOR_F64, empty_shape, 2, NULL, 0);
        query = tensor(CETTA_TENSOR_F64, &zero, 1, NULL, 0);
        receipt = screen(source, query, 1, false);
        compare(receipt); cetta_tensor_screen_receipt_release(receipt);
        cetta_tensor_release(source); cetta_tensor_release(query);
    }
}

static void selected_rows(void) {
    size_t shape[] = {3, 3}, width = 3, count = 4;
    double values[] = {1.5, -0., 3.2, -9., 2., .5, 4., -6., .125};
    double q[] = {2., -4., 1.};
    uint64_t requested[] = {2, 0, 2, 1};
    CettaTensor *base = tensor(CETTA_TENSOR_F64, shape, 2, values, sizeof(values));
    CettaTensor *query_base = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    CettaTensor *rows = tensor(CETTA_TENSOR_INDEX, &count, 1, requested, sizeof(requested));
    CettaTensorDType dtypes[] = {CETTA_TENSOR_F32, CETTA_TENSOR_F64};
    for (size_t a = 0; a < 2; a++) for (size_t b = 0; b < 2; b++) {
        CettaTensor *matrix = NULL, *query = NULL, *all = NULL, *expected = NULL, *actual = NULL;
        CHECK(cetta_tensor_cast(base, dtypes[a], CETTA_TENSOR_CAST_NEAREST, &matrix) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_cast(query_base, dtypes[b], CETTA_TENSOR_CAST_NEAREST, &query) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_matvec(matrix, query, CETTA_TENSOR_SEQUENTIAL, &all) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_gather(all, rows, &expected) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_matvec_rows(matrix, query, rows, &actual) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_equal(actual, expected));
        for (CettaTensorLayout layout = CETTA_TENSOR_COO; layout <= CETTA_TENSOR_CSC; layout++) {
            CettaTensor *sparse = NULL, *all_sparse = NULL, *expected_sparse = NULL, *actual_sparse = NULL;
            CHECK(cetta_tensor_convert(matrix, layout, &sparse) == CETTA_TENSOR_OK);
            CHECK(cetta_tensor_matvec(sparse, query, CETTA_TENSOR_SEQUENTIAL, &all_sparse) == CETTA_TENSOR_OK);
            CHECK(cetta_tensor_gather(all_sparse, rows, &expected_sparse) == CETTA_TENSOR_OK);
            CHECK(cetta_tensor_matvec_rows(sparse, query, rows, &actual_sparse) == CETTA_TENSOR_OK);
            CHECK(cetta_tensor_equal(actual_sparse, expected_sparse));
            cetta_tensor_release(sparse); cetta_tensor_release(all_sparse);
            cetta_tensor_release(expected_sparse); cetta_tensor_release(actual_sparse);
        }
        cetta_tensor_release(matrix); cetta_tensor_release(query); cetta_tensor_release(all);
        cetta_tensor_release(expected); cetta_tensor_release(actual);
    }
    uint64_t invalid[] = {0, 2, 99};
    CettaTensor *bad = tensor(CETTA_TENSOR_INDEX, &width, 1, invalid, sizeof(invalid)), *actual = NULL;
    CHECK(cetta_tensor_matvec_rows(base, query_base, bad, &actual) == CETTA_TENSOR_INDEX_ERROR && !actual);
    cetta_tensor_release(bad); cetta_tensor_release(base); cetta_tensor_release(query_base);
    cetta_tensor_release(rows);
}

static void masks(void) {
    size_t shape[] = {4, 1}, width = 1, rows = 4;
    double values[] = {1000., 10., 9., 8.}, q[] = {1.};
    uint8_t enabled[] = {0, 1, 1, 0}, none[] = {0, 0, 0, 0};
    size_t coordinates[] = {1, 2, 3}; uint8_t sparse_values[] = {1, 1, 0};
    CettaTensor *matrix = tensor(CETTA_TENSOR_F64, shape, 2, values, sizeof(values));
    CettaTensor *query = tensor(CETTA_TENSOR_F64, &width, 1, q, sizeof(q));
    CettaTensor *dense = tensor(CETTA_TENSOR_BOOL, &rows, 1, enabled, sizeof(enabled));
    CettaTensor *empty = tensor(CETTA_TENSOR_BOOL, &rows, 1, none, sizeof(none));
    CettaTensor *sparse = NULL, *view = NULL;
    CHECK(cetta_tensor_coo(CETTA_TENSOR_BOOL, &rows, 1, coordinates, sparse_values, 3,
        CETTA_TENSOR_REJECT, &sparse) == CETTA_TENSOR_OK);
    ptrdiff_t stride = -1;
    CHECK(cetta_tensor_view(dense, &rows, &stride, 1, 3, &view) == CETTA_TENSOR_OK);
    CettaTensorScreenIndex *index = NULL;
    CHECK(cetta_tensor_screen_index(matrix, &index) == CETTA_TENSOR_OK);
    CettaTensor *choices[] = {dense, sparse, view, empty};
    for (size_t m = 0; m < 4; m++) for (size_t k = 0; k < 6; k++) {
        CettaTensorScreenReceipt *receipt = NULL;
        CHECK(cetta_tensor_screen(index, query, choices[m], k, &receipt) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_screen_mask(receipt) == choices[m]);
        compare(receipt);
        for (size_t i = 0; i < cetta_tensor_count(cetta_tensor_screen_candidates(receipt)); i++) {
            bool present; CettaTensorScalar row, bit;
            CHECK(cetta_tensor_get(cetta_tensor_screen_candidates(receipt), i, &present, &row)
                == CETTA_TENSOR_OK && present);
            CHECK(cetta_tensor_get(choices[m], (size_t)row.index, &present, &bit)
                == CETTA_TENSOR_OK && present && bit.u8);
        }
        if (m < 3 && k) CHECK(cetta_tensor_screen_is_certified(receipt));
        if (m == 3) CHECK(cetta_tensor_count(cetta_tensor_screen_candidates(receipt)) == 0);
        cetta_tensor_screen_receipt_release(receipt);
    }
    CettaTensorScreenReceipt *receipt = NULL;
    CHECK(cetta_tensor_screen(index, query, sparse, 2, &receipt) == CETTA_TENSOR_OK);
    CHECK(fesetround(FE_UPWARD) == 0);
    compare(receipt); /* Losing numerical admission must not lose the mask. */
    CHECK(fesetround(FE_TONEAREST) == 0);
    cetta_tensor_screen_receipt_release(receipt);
    CHECK(setenv("CETTA_TENSOR_SCREEN_REFERENCE", "1", 1) == 0);
    CHECK(cetta_tensor_screen(index, query, sparse, 1, &receipt) == CETTA_TENSOR_OK);
    CHECK(!cetta_tensor_screen_is_certified(receipt));
    CHECK(cetta_tensor_count(cetta_tensor_screen_candidates(receipt)) == 2);
    compare(receipt); cetta_tensor_screen_receipt_release(receipt);
    CHECK(unsetenv("CETTA_TENSOR_SCREEN_REFERENCE") == 0);
    CettaTensor *wrong = tensor(CETTA_TENSOR_U8, &rows, 1, enabled, sizeof(enabled));
    receipt = NULL;
    CHECK(cetta_tensor_screen(index, query, wrong, 1, &receipt) == CETTA_TENSOR_SHAPE && !receipt);
    cetta_tensor_release(wrong);
    CHECK(cetta_tensor_screen(index, query, sparse, 1, &receipt) == CETTA_TENSOR_OK);
    cetta_tensor_screen_index_release(index);
    cetta_tensor_release(matrix); cetta_tensor_release(query); cetta_tensor_release(dense);
    cetta_tensor_release(empty); cetta_tensor_release(sparse); cetta_tensor_release(view);
    compare(receipt); /* Actual mask and source remain owned after aliases disappear. */
    cetta_tensor_screen_receipt_release(receipt);
}

static void large_sparse_domains(void) {
    size_t rows = SIZE_MAX, shape[] = {rows, 0}, width = 0;
    size_t coordinate = rows - 1;
    uint8_t enabled = 1;
    CettaTensor *matrix = tensor(CETTA_TENSOR_F64, shape, 2, NULL, 0);
    CettaTensor *query = tensor(CETTA_TENSOR_F64, &width, 1, NULL, 0), *mask = NULL;
    CHECK(cetta_tensor_coo(CETTA_TENSOR_BOOL, &rows, 1, &coordinate, &enabled, 1,
        CETTA_TENSOR_REJECT, &mask) == CETTA_TENSOR_OK);
    for (size_t variant = 0; variant < 2; variant++) {
        CettaTensorScreenIndex *index = NULL;
        CettaTensorScreenReceipt *receipt = NULL;
        CHECK(cetta_tensor_screen_index(matrix, &index) == CETTA_TENSOR_OK);
        CHECK(!cetta_tensor_screen_index_has_compact(index));
        if (!variant)
            CHECK(cetta_tensor_screen(index, query, NULL, 1, &receipt) == CETTA_TENSOR_OVERFLOW && !receipt);
        CHECK(cetta_tensor_screen(index, query, mask, 1, &receipt) == CETTA_TENSOR_OK);
        CHECK(!cetta_tensor_screen_is_certified(receipt));
        CHECK(cetta_tensor_count(cetta_tensor_screen_candidates(receipt)) == 1);
        CettaTensor *ids = NULL, *scores = NULL;
        CHECK(cetta_tensor_screen_rerank(receipt, &ids, &scores) == CETTA_TENSOR_OK);
        bool present; CettaTensorScalar value;
        CHECK(cetta_tensor_get(ids, 0, &present, &value) == CETTA_TENSOR_OK && present && value.index == coordinate);
        CHECK(cetta_tensor_get(scores, 0, &present, &value) == CETTA_TENSOR_OK && present && value.f64 == (variant ? 8. : 0.));
        cetta_tensor_release(ids); cetta_tensor_release(scores);
        cetta_tensor_screen_receipt_release(receipt); cetta_tensor_screen_index_release(index);
        cetta_tensor_release(matrix); cetta_tensor_release(query);
        if (!variant) {
            shape[1] = width = 1; double coefficient = 4., component = 2.;
            matrix = NULL;
            CHECK(cetta_tensor_coo(CETTA_TENSOR_F64, shape, 2, &coordinate, &coefficient, 1,
                CETTA_TENSOR_REJECT, &matrix) == CETTA_TENSOR_OK);
            query = tensor(CETTA_TENSOR_F64, &width, 1, &component, sizeof(component));
        }
    }
    cetta_tensor_release(mask);
}

static uint64_t random_state = 4097;
static uint64_t random_word(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

static void randomized(void) {
    for (size_t trial = 0; trial < 400; trial++) {
        size_t rows = 1 + random_word() % 65, columns = 1 + random_word() % 129;
        size_t shape[] = {rows, columns};
        double *values = malloc(rows * columns * sizeof(*values));
        double *q = malloc(columns * sizeof(*q));
        CHECK(values && q);
        for (size_t i = 0; i < rows * columns; i++)
            values[i] = ((int64_t)(random_word() % 40001) - 20000) / 137.;
        for (size_t i = 0; i < columns; i++)
            q[i] = ((int64_t)(random_word() % 40001) - 20000) / 193.;
        if (trial % 5 == 0) for (size_t i = 0; i < rows * columns; i++) values[i] *= 1e-200;
        if (trial % 7 == 0) for (size_t i = 0; i < columns; i++) q[i] *= 1e-110;
        CettaTensor *matrix = tensor(CETTA_TENSOR_F64, shape, 2, values, rows * columns * sizeof(*values));
        CettaTensor *query = tensor(CETTA_TENSOR_F64, &columns, 1, q, columns * sizeof(*q));
        free(values); free(q);
        CettaTensorScreenReceipt *receipt = screen(matrix, query, random_word() % (rows + 3), true);
        compare(receipt);
        cetta_tensor_screen_receipt_release(receipt);
        uint8_t *bits = malloc(rows); CHECK(bits);
        for (size_t row = 0; row < rows; row++) bits[row] = random_word() % 2;
        CettaTensor *mask = tensor(CETTA_TENSOR_BOOL, &rows, 1, bits, rows);
        free(bits);
        CettaTensorScreenIndex *index = NULL;
        CHECK(cetta_tensor_screen_index(matrix, &index) == CETTA_TENSOR_OK);
        CHECK(cetta_tensor_screen(index, query, mask, random_word() % (rows + 3), &receipt)
            == CETTA_TENSOR_OK);
        compare(receipt);
        cetta_tensor_screen_receipt_release(receipt); cetta_tensor_screen_index_release(index);
        cetta_tensor_release(mask); cetta_tensor_release(matrix); cetta_tensor_release(query);
    }
}

int main(void) {
    CHECK(fesetround(FE_TONEAREST) == 0);
    basic(); fallback(); environments(); views_and_empty(); selected_rows(); masks();
    large_sparse_domains(); randomized();
    printf("TensorScreeningNative %zu 0\n", checks);
    return 0;
}
