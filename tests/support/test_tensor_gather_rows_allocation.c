#include "tensor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t bytes);
void *__real_calloc(size_t count, size_t bytes);
void *__real_realloc(void *pointer, size_t bytes);

static size_t position, failure;
static bool armed, injected;
static unsigned checks, failures;

static bool fail_now(void) {
    if (!armed || position++ != failure) return false;
    injected = true;
    return true;
}

void *__wrap_malloc(size_t bytes) {
    return fail_now() ? NULL : __real_malloc(bytes);
}
void *__wrap_calloc(size_t count, size_t bytes) {
    return fail_now() ? NULL : __real_calloc(count, bytes);
}
void *__wrap_realloc(void *pointer, size_t bytes) {
    return fail_now() ? NULL : __real_realloc(pointer, bytes);
}

static void check(bool value) { checks++; assert(value); }

static void qualify(CettaTensor *source, CettaTensor *rows) {
    uint64_t source_hash = cetta_tensor_hash(source), row_hash = cetta_tensor_hash(rows);
    CettaTensor *expected = NULL;
    check(cetta_tensor_gather_rows(source, rows, &expected) == CETTA_TENSOR_OK);
    for (failure = 0;; failure++) {
        CettaTensor *result = NULL;
        position = 0; injected = false; armed = true;
        CettaTensorStatus status = cetta_tensor_gather_rows(source, rows, &result);
        armed = false;
        if (!injected) {
            check(status == CETTA_TENSOR_OK && result);
            check(cetta_tensor_equal(result, expected));
            cetta_tensor_release(result);
            break;
        }
        failures++;
        check(status == CETTA_TENSOR_NOMEM && !result);
        check(cetta_tensor_hash(source) == source_hash && cetta_tensor_hash(rows) == row_hash);
        CettaTensor *source_copy = NULL, *row_copy = NULL;
        check(cetta_tensor_copy(source, &source_copy) == CETTA_TENSOR_OK);
        check(cetta_tensor_copy(rows, &row_copy) == CETTA_TENSOR_OK);
        check(cetta_tensor_equal(source_copy, source) && cetta_tensor_equal(row_copy, rows));
        cetta_tensor_release(source_copy); cetta_tensor_release(row_copy);
    }
    cetta_tensor_release(expected);
}

int main(void) {
    CettaTensor *dense = NULL, *sparse = NULL, *rows = NULL, *empty = NULL;
    check(cetta_tensor_new(CETTA_TENSOR_F64, (size_t[]){2, 3}, 2,
        (double[]){-0.0, 1, 2, 3, 4, 5}, 6 * sizeof(double), &dense) == CETTA_TENSOR_OK);
    check(cetta_tensor_coo(CETTA_TENSOR_F64, (size_t[]){2, 3}, 2,
        (size_t[]){0, 4}, (double[]){-0.0, 7}, 2, CETTA_TENSOR_REJECT, &sparse) == CETTA_TENSOR_OK);
    check(cetta_tensor_new(CETTA_TENSOR_INDEX, (size_t[]){3}, 1,
        (uint64_t[]){1, 0, 1}, 3 * sizeof(uint64_t), &rows) == CETTA_TENSOR_OK);
    check(cetta_tensor_new(CETTA_TENSOR_F64, (size_t[]){2, 0}, 2,
        NULL, 0, &empty) == CETTA_TENSOR_OK);
    qualify(dense, rows); qualify(sparse, rows); qualify(empty, rows);
    check(failures > 0);
    cetta_tensor_release(dense); cetta_tensor_release(sparse);
    cetta_tensor_release(rows); cetta_tensor_release(empty);
    printf("TensorGatherRowsAllocation %u %u 0\n", checks, failures);
    return 0;
}
