#include "tensor.h"
#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned retains, releases;
static void retain(void *context) { (void)context; retains++; }
static void release(void *context) { (void)context; releases++; }
static const CettaTensorBufferOwner owner = {NULL, retain, release};
static bool host_little(void) { const uint16_t one = 1; return *(const uint8_t *)&one == 1; }
static CettaTensorByteOrder foreign_order(void) {
    return host_little() ? CETTA_TENSOR_BYTE_ORDER_BIG : CETTA_TENSOR_BYTE_ORDER_LITTLE;
}
static void encode_word(unsigned char *out, uint64_t word, size_t width, bool little) {
    for (size_t i = 0; i < width; ++i)
        out[i] = (uint8_t)(word >> (8u * (little ? i : width - i - 1u)));
}
static void foreign_bytes(const void *native, unsigned char *out, size_t count, size_t width) {
    for (size_t i = 0; i < count; ++i) {
        uint64_t word = 0;
        if (width == 8) memcpy(&word, (const unsigned char *)native + i * width, 8);
        else if (width == 4) { uint32_t value; memcpy(&value, (const unsigned char *)native + i * width, 4); word = value; }
        else word = ((const uint8_t *)native)[i];
        encode_word(out + i * width, word, width, !host_little());
    }
}
static CettaTensor *borrow(CettaTensorDType dtype, const size_t *shape, size_t rank,
        const void *bytes, size_t size) {
    CettaTensor *tensor = NULL;
    assert(rank <= 2);
    ptrdiff_t strides[2], step = 1;
    for (size_t i = rank; i > 0; --i) {
        strides[i - 1] = step;
        step *= (ptrdiff_t)shape[i - 1];
    }
    assert(cetta_tensor_borrow(dtype, foreign_order(), bytes, size,
        shape, strides, rank, 0, &owner, &tensor) == CETTA_TENSOR_OK);
    return tensor;
}
static void same(const CettaTensor *reference, const CettaTensor *observed) {
    assert(cetta_tensor_hash(reference) == cetta_tensor_hash(observed));
    assert(cetta_tensor_equal(reference, observed));
    assert(cetta_tensor_compare(reference, observed) == 0);
    assert(cetta_tensor_compare(observed, reference) == 0);
}
static uint64_t random_word(uint64_t *state) {
    *state ^= *state << 13; *state ^= *state >> 7; *state ^= *state << 17;
    return *state;
}
static void scalar_cases(void) {
    uint64_t random = UINT64_C(0x49cda73853e2af17);
    CettaTensorDType types[] = {CETTA_TENSOR_I64, CETTA_TENSOR_INDEX,
        CETTA_TENSOR_F32, CETTA_TENSOR_F64, CETTA_TENSOR_U8, CETTA_TENSOR_BOOL};
    for (size_t kind = 0; kind < sizeof(types) / sizeof(*types); ++kind) {
        CettaTensorDType dtype = types[kind];
        size_t width = cetta_tensor_itemsize(dtype), shape[] = {2, 4};
        for (unsigned round = 0; round < 64; ++round) {
            _Alignas(uint64_t) unsigned char native[64], raw[64], copied[64];
            for (size_t i = 0; i < 8; ++i) {
                uint64_t word = random_word(&random);
                if (dtype == CETTA_TENSOR_BOOL) word &= 1;
                if (dtype == CETTA_TENSOR_F64 && i < 3) {
                    const uint64_t special[] = {UINT64_C(0x8000000000000000), UINT64_C(0x7ff8000000001234), UINT64_C(0x7ff0000000000000)};
                    word = special[i];
                }
                encode_word(native + i * width, word, width, host_little());
            }
            foreign_bytes(native, raw, 8, width);
            CettaTensor *reference = NULL, *observed = borrow(dtype, shape, 2, raw, 8 * width);
            assert(cetta_tensor_new(dtype, shape, 2, native, 8 * width, &reference) == CETTA_TENSOR_OK);
            CettaTensorDenseSpan span;
            assert(cetta_tensor_dense_span(observed, &span) && span.byte_order == foreign_order());
            assert(span.allocation == raw && !memcmp(raw, span.allocation, 8 * width));
            for (size_t i = 0; i < 8; ++i) {
                CettaTensorScalar value; bool present;
                assert(cetta_tensor_get(observed, i, &present, &value) == CETTA_TENSOR_OK && present);
                assert(!memcmp(&value, native + i * width, width));
            }
            same(reference, observed);
            if (!round) {
                _Alignas(uint64_t) unsigned char different[64];
                memcpy(different, raw, 8 * width); different[0] ^= 1u;
                CettaTensor *other = borrow(dtype, shape, 2, different, 8 * width);
                int expected = cetta_tensor_compare(reference, other);
                assert(expected && cetta_tensor_compare(observed, other) == expected);
                cetta_tensor_release(other);
            }
            assert(cetta_tensor_all_finite(reference) == cetta_tensor_all_finite(observed));
            assert(cetta_tensor_copy_values(observed, copied, 8 * width) == CETTA_TENSOR_OK);
            assert(!memcmp(native, copied, 8 * width));
            CettaTensor *copy = NULL;
            assert(cetta_tensor_copy(observed, &copy) == CETTA_TENSOR_OK);
            same(reference, copy); cetta_tensor_release(copy);
            if (dtype <= CETTA_TENSOR_F32) {
                unsigned char *a = NULL, *b = NULL; size_t an, bn;
                assert(cetta_tensor_encode(reference, &a, &an) == CETTA_TENSOR_OK);
                assert(cetta_tensor_encode(observed, &b, &bn) == CETTA_TENSOR_OK);
                assert(an == bn && !memcmp(a, b, an)); free(a); free(b);
            }
            if (!round) for (CettaTensorLayout layout = CETTA_TENSOR_COO; layout <= CETTA_TENSOR_CSC; ++layout) {
                assert(cetta_tensor_convert(observed, layout, &copy) == CETTA_TENSOR_OK);
                same(reference, copy); cetta_tensor_release(copy);
            }
            CettaTensor *reverse = NULL, *reverse_reference = NULL;
            assert(cetta_tensor_view(observed, (size_t[]){3}, (ptrdiff_t[]){-2}, 1, 6, &reverse) == CETTA_TENSOR_OK);
            assert(cetta_tensor_view(reference, (size_t[]){3}, (ptrdiff_t[]){-2}, 1, 6, &reverse_reference) == CETTA_TENSOR_OK);
            cetta_tensor_release(observed); cetta_tensor_release(reference);
            same(reverse_reference, reverse);
            assert(cetta_tensor_dense_span(reverse, &span) && span.byte_order == foreign_order());
            cetta_tensor_release(reverse); cetta_tensor_release(reverse_reference);
        }
    }
}
static void numeric_cases(void) {
    const double matrix[] = {1, 2, 3, 4, 5, 6}, query[] = {2, 3, 5};
    const uint64_t ids[] = {1, 0, 1};
    _Alignas(uint64_t) unsigned char raw_matrix[sizeof(matrix)], raw_query[sizeof(query)], raw_ids[sizeof(ids)];
    foreign_bytes(matrix, raw_matrix, 6, 8); foreign_bytes(query, raw_query, 3, 8); foreign_bytes(ids, raw_ids, 3, 8);
    CettaTensor *m = NULL, *q = NULL, *indices = borrow(CETTA_TENSOR_INDEX, (size_t[]){3}, 1, raw_ids, sizeof(raw_ids));
    assert(cetta_tensor_new(CETTA_TENSOR_F64, (size_t[]){2, 3}, 2, matrix, sizeof(matrix), &m) == CETTA_TENSOR_OK);
    assert(cetta_tensor_new(CETTA_TENSOR_F64, (size_t[]){3}, 1, query, sizeof(query), &q) == CETTA_TENSOR_OK);
    CettaTensor *fm = borrow(CETTA_TENSOR_F64, (size_t[]){2, 3}, 2, raw_matrix, sizeof(raw_matrix));
    CettaTensor *fq = borrow(CETTA_TENSOR_F64, (size_t[]){3}, 1, raw_query, sizeof(raw_query));
    CettaTensor *matrices[] = {m, fm}, *queries[] = {q, fq};
    for (unsigned a = 0; a < 2; ++a) for (unsigned b = 0; b < 2; ++b) {
        for (CettaTensorSchedule schedule = CETTA_TENSOR_SEQUENTIAL; schedule <= CETTA_TENSOR_FIXED4; ++schedule) {
            CettaTensor *result = NULL;
            assert(cetta_tensor_matvec(matrices[a], queries[b], schedule, &result) == CETTA_TENSOR_OK);
            double values[2]; assert(cetta_tensor_copy_values(result, values, sizeof(values)) == CETTA_TENSOR_OK);
            assert(values[0] == 23 && values[1] == 53);
            cetta_tensor_release(result);
        }
        CettaTensor *selected = NULL;
        assert(cetta_tensor_matvec_rows(matrices[a], queries[b], indices, &selected) == CETTA_TENSOR_OK);
        double values[3]; assert(cetta_tensor_copy_values(selected, values, sizeof(values)) == CETTA_TENSOR_OK);
        assert(values[0] == 53 && values[1] == 23 && values[2] == 53);
        cetta_tensor_release(selected);
    }
    for (CettaTensorLayout layout = CETTA_TENSOR_COO; layout <= CETTA_TENSOR_CSC; ++layout) {
        CettaTensor *sparse = NULL, *result = NULL;
        assert(cetta_tensor_convert(fm, layout, &sparse) == CETTA_TENSOR_OK);
        assert(cetta_tensor_matvec(sparse, fq, CETTA_TENSOR_SEQUENTIAL, &result) == CETTA_TENSOR_OK);
        double values[2]; assert(cetta_tensor_copy_values(result, values, sizeof(values)) == CETTA_TENSOR_OK);
        assert(values[0] == 23 && values[1] == 53);
        cetta_tensor_release(result); cetta_tensor_release(sparse);
    }
    cetta_tensor_release(m); cetta_tensor_release(q); cetta_tensor_release(indices);
    cetta_tensor_release(fm); cetta_tensor_release(fq);
}
static void public_buffer_case(void) {
    _Alignas(uint64_t) unsigned char raw[16];
    encode_word(raw, UINT64_C(0x12345678abcdef01), 8, !host_little());
    encode_word(raw + 8, 7, 8, !host_little());
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = "i64",
        .byte_order = (cetta_byte_order_t)foreign_order(), .allocation = raw,
        .allocation_bytes = sizeof(raw), .rank = 1, .shape = (size_t[]){2}};
    cetta_buffer_t *buffer = NULL, *view = NULL;
    assert(cetta_embed_buffer_copy(&spec, &buffer) == CETTA_OK);
    cetta_buffer_descriptor_t info = {.size = sizeof(info)};
    assert(cetta_embed_buffer_describe(buffer, &info) == CETTA_OK && info.byte_order == spec.byte_order);
    const void *allocation = info.allocation;
    assert(cetta_embed_buffer_reinterpret(buffer, "i64", CETTA_BYTE_ORDER_NATIVE,
        (size_t[]){2}, (ptrdiff_t[]){8}, 1, 0, &view) == CETTA_OK);
    cetta_embed_buffer_release(buffer);
    assert(cetta_embed_buffer_describe(view, &info) == CETTA_OK && info.byte_order == CETTA_BYTE_ORDER_NATIVE);
    assert(info.allocation == allocation && !memcmp(info.allocation, raw, sizeof(raw)));
    cetta_embed_buffer_release(view);
}
int main(void) {
    scalar_cases(); numeric_cases(); public_buffer_case();
    assert(retains == releases);
    printf("tensor byte order: 384 bit-pattern fixtures, mixed-order kernels, retained views and ABI reinterpretation pass (%u owners released)\n", releases);
    return 0;
}
