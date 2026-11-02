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
void *__wrap_malloc(size_t bytes) { return fail_now() ? NULL : __real_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t bytes) { return fail_now() ? NULL : __real_calloc(count, bytes); }
void *__wrap_realloc(void *pointer, size_t bytes) { return fail_now() ? NULL : __real_realloc(pointer, bytes); }

static void check(bool value) { checks++; assert(value); }

static void qualify(CettaTensor *mask, CettaTensor *left, CettaTensor *right) {
    CettaTensor *inputs[] = {mask, left, right}, *expected = NULL;
    uint64_t hashes[3];
    for (size_t i = 0; i < 3; i++) hashes[i] = cetta_tensor_hash(inputs[i]);
    check(cetta_tensor_where(mask, left, right, &expected) == CETTA_TENSOR_OK);
    for (failure = 0;; failure++) {
        CettaTensor *result = NULL;
        position = 0; injected = false; armed = true;
        CettaTensorStatus status = cetta_tensor_where(mask, left, right, &result);
        armed = false;
        if (!injected) {
            check(status == CETTA_TENSOR_OK && result && cetta_tensor_equal(result, expected));
            cetta_tensor_release(result);
            break;
        }
        failures++;
        check(status == CETTA_TENSOR_NOMEM && !result);
        for (size_t i = 0; i < 3; i++) {
            CettaTensor *copy = NULL;
            check(cetta_tensor_hash(inputs[i]) == hashes[i]);
            check(cetta_tensor_copy(inputs[i], &copy) == CETTA_TENSOR_OK);
            check(cetta_tensor_equal(copy, inputs[i]));
            cetta_tensor_release(copy);
        }
    }
    cetta_tensor_release(expected);
}

int main(void) {
    CettaTensor *mask = NULL, *left = NULL, *right = NULL, *out = NULL;
    size_t shape[] = {2, 2};
    uint64_t bits[] = {UINT64_C(0x8000000000000000), UINT64_C(0x7ff8000000000017),
                       UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000)};
    check(cetta_tensor_new(CETTA_TENSOR_BOOL, shape, 2, (uint8_t[]){1, 0, 1, 0}, 4, &mask) == CETTA_TENSOR_OK);
    check(cetta_tensor_new(CETTA_TENSOR_F64, shape, 2, bits, sizeof(bits), &left) == CETTA_TENSOR_OK);
    check(cetta_tensor_new(CETTA_TENSOR_F64, shape, 2, bits, sizeof(bits), &right) == CETTA_TENSOR_OK);
    check(cetta_tensor_where(mask, left, right, &out) == CETTA_TENSOR_OK);
    check(cetta_tensor_equal(out, left));
    for (size_t i = 0; i < 4; i++) {
        bool present = false; CettaTensorScalar value;
        check(cetta_tensor_get(out, i, &present, &value) == CETTA_TENSOR_OK && present);
        check(memcmp(&value.f64, &bits[i], sizeof(double)) == 0);
    }
    cetta_tensor_release(out); out = NULL;
    qualify(mask, left, right);
    cetta_tensor_release(left); cetta_tensor_release(right);
    check(cetta_tensor_coo(CETTA_TENSOR_F64, shape, 2, (size_t[]){0, 2}, bits, 2,
                          CETTA_TENSOR_REJECT, &left) == CETTA_TENSOR_OK);
    check(cetta_tensor_coo(CETTA_TENSOR_F64, shape, 2, (size_t[]){0, 1}, bits + 2, 2,
                          CETTA_TENSOR_REJECT, &right) == CETTA_TENSOR_OK);
    qualify(mask, left, right);
    cetta_tensor_release(mask);
    check(cetta_tensor_coo(CETTA_TENSOR_BOOL, shape, 2, (size_t[]){0, 1, 3}, (uint8_t[]){1, 0, 1}, 3,
                          CETTA_TENSOR_REJECT, &mask) == CETTA_TENSOR_OK);
    qualify(mask, left, right);
    cetta_tensor_release(mask); cetta_tensor_release(left); cetta_tensor_release(right);
    shape[1] = 0;
    check(cetta_tensor_new(CETTA_TENSOR_BOOL, shape, 2, NULL, 0, &mask) == CETTA_TENSOR_OK);
    check(cetta_tensor_new(CETTA_TENSOR_I64, shape, 2, NULL, 0, &left) == CETTA_TENSOR_OK);
    qualify(mask, left, left);
    cetta_tensor_release(mask); cetta_tensor_release(left);
    /* Logical shape size is unrelated to output storage when support is sparse. */
    size_t huge = SIZE_MAX / 16, coordinate = huge - 1;
    check(cetta_tensor_coo(CETTA_TENSOR_BOOL, &huge, 1, &coordinate, (uint8_t[]){0}, 1,
                          CETTA_TENSOR_REJECT, &mask) == CETTA_TENSOR_OK);
    check(cetta_tensor_coo(CETTA_TENSOR_F64, &huge, 1, &coordinate, bits, 1,
                          CETTA_TENSOR_REJECT, &left) == CETTA_TENSOR_OK);
    check(cetta_tensor_coo(CETTA_TENSOR_F64, &huge, 1, &coordinate, bits + 1, 1,
                          CETTA_TENSOR_REJECT, &right) == CETTA_TENSOR_OK);
    check(cetta_tensor_where(mask, left, right, &out) == CETTA_TENSOR_OK);
    check(cetta_tensor_stored(out) == 1 && cetta_tensor_count(out) == huge && cetta_tensor_equal(out, right));
    cetta_tensor_release(out); out = NULL;
    qualify(mask, left, right);
    check(cetta_tensor_where(NULL, left, right, &out) == CETTA_TENSOR_VALUE && !out);
    check(cetta_tensor_where(mask, left, right, NULL) == CETTA_TENSOR_VALUE);
    check(cetta_tensor_where(left, left, right, &out) == CETTA_TENSOR_DTYPE && !out);
    check(cetta_tensor_where(mask, left, mask, &out) == CETTA_TENSOR_DTYPE && !out);
    check(failures > 0);
    cetta_tensor_release(mask); cetta_tensor_release(left); cetta_tensor_release(right);
    printf("TensorWhereAllocation %u %u 0\n", checks, failures);
    return 0;
}
