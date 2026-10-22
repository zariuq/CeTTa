#include "tensor_dense_v1.h"
#include "atom_wire.h"
#include "durable_value.h"
#include "tests/test_runtime_stats_stubs.h"
#include <assert.h>
#include <math.h>
#include <float.h>
#include <fenv.h>
#include <pthread.h>
#include <stdlib.h>
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define TEST_HAS_MXCSR 1
#else
#define TEST_HAS_MXCSR 0
#endif

static CettaDenseTensorV1 *make(CettaDenseTensorV1DType dtype, const size_t *shape, size_t rank,
    const void *data, size_t bytes) {
    CettaDenseTensorV1 *tensor = NULL;
    assert(cetta_dense_tensor_v1_new(dtype, shape, rank, data, bytes, &tensor) == CETTA_DENSE_TENSOR_V1_OK);
    return tensor;
}

static void *copy_worker(void *input) {
    CettaDenseTensorV1 *source = input;
    for (unsigned i = 0; i < 400u; ++i) {
        Arena arena; arena_init_detached(&arena);
        Atom *atom = atom_tensor(&arena, source);
        assert(atom && cetta_dense_tensor_v1_equal(atom->ground.ptr, source));
        CettaDenseTensorV1 *view = NULL;
        size_t shape[] = {2u, 2u};
        assert(cetta_dense_tensor_v1_reshape(source, shape, 2u, &view) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_shares_storage(source, view));
        cetta_dense_tensor_v1_release(view);
        arena_free(&arena);
    }
    return NULL;
}

static void codecs(Arena *arena, Atom *source) {
    unsigned char *bytes = NULL; size_t size = 0;
    Atom *decoded = NULL;
    assert(cetta_atom_wire_encode(source, &bytes, &size) == ATOM_WIRE_OK);
    assert(cetta_atom_wire_decode(arena, bytes, size, &decoded) == ATOM_WIRE_OK);
    assert(atom_eq(source, decoded) && atom_hash(source) == atom_hash(decoded));
    assert(cetta_atom_wire_decode(arena, bytes, size - 1u, &decoded) != ATOM_WIRE_OK);
    free(bytes); bytes = NULL;
    assert(cetta_durable_value_encode(source, &bytes, &size) == DURABLE_OK);
    assert(cetta_durable_value_decode(arena, bytes, size, &decoded) == DURABLE_OK);
    assert(atom_eq(source, decoded) && atom_hash(source) == atom_hash(decoded));
    free(bytes);
}

static void arithmetic_reference(void) {
    /* Small exact binary fractions keep the reference independent of reduction
     * tolerance. Check every output entry, all supported dtypes and empty input. */
    for (unsigned dtype = CETTA_DENSE_TENSOR_V1_I64; dtype <= CETTA_DENSE_TENSOR_V1_F32; ++dtype) {
        for (size_t n = 0; n <= 32u; ++n) {
            unsigned char entries[32u * 8u];
            double float_sum = 0.0, float_dot = 0.0;
            int64_t int_sum = 0, int_dot = 0;
            size_t width = cetta_dense_tensor_v1_itemsize(dtype);
            for (size_t i = 0; i < n; ++i) {
                int64_t integer = dtype == CETTA_DENSE_TENSOR_V1_U8 ? (int64_t)(i % 8u) : (int64_t)(i % 11u) - 5;
                double floating = (double)integer / 4.0;
                if (dtype == CETTA_DENSE_TENSOR_V1_F32) { float compact = floating; memcpy(entries + i*width, &compact, width); }
                else if (dtype == CETTA_DENSE_TENSOR_V1_F64) memcpy(entries + i * width, &floating, width);
                else if (dtype == CETTA_DENSE_TENSOR_V1_I64) memcpy(entries + i * width, &integer, width);
                else entries[i] = (unsigned char)integer;
                float_sum += floating; float_dot += floating * floating;
                int_sum += integer; int_dot += integer * integer;
            }
            CettaDenseTensorV1 *source = make(dtype, &n, 1u, entries, n * width);
            CettaDenseTensorV1Scalar result;
            assert(cetta_dense_tensor_v1_reduce(source, NULL, &result) == CETTA_DENSE_TENSOR_V1_OK);
            if (dtype == CETTA_DENSE_TENSOR_V1_F64 || dtype == CETTA_DENSE_TENSOR_V1_F32) assert(result.value.floating == float_sum);
            else assert(result.value.integer == int_sum);
            assert(cetta_dense_tensor_v1_reduce(source, source, &result) == CETTA_DENSE_TENSOR_V1_OK);
            if (dtype == CETTA_DENSE_TENSOR_V1_F64 || dtype == CETTA_DENSE_TENSOR_V1_F32) assert(result.value.floating == float_dot);
            else assert(result.value.integer == int_dot);
            for (unsigned multiply = 0; multiply < 2u; ++multiply) {
                CettaDenseTensorV1 *output = NULL;
                assert(cetta_dense_tensor_v1_binary(source, source, multiply, &output) == CETTA_DENSE_TENSOR_V1_OK);
                const unsigned char *data = cetta_dense_tensor_v1_data(output);
                for (size_t i = 0; i < n; ++i) {
                    int64_t integer = dtype == CETTA_DENSE_TENSOR_V1_U8 ? (int64_t)(i % 8u) : (int64_t)(i % 11u) - 5;
                    double floating = (double)integer / 4.0;
                    if (dtype == CETTA_DENSE_TENSOR_V1_F64 || dtype == CETTA_DENSE_TENSOR_V1_F32) {
                        double actual;
                        if (dtype == CETTA_DENSE_TENSOR_V1_F32) { float compact; memcpy(&compact, data + i*width, width); actual = compact; }
                        else memcpy(&actual, data + i * width, width);
                        assert(actual == (multiply ? floating * floating : floating + floating));
                    } else {
                        int64_t actual;
                        if (dtype == CETTA_DENSE_TENSOR_V1_U8) actual = data[i];
                        else memcpy(&actual, data + i * width, width);
                        assert(actual == (multiply ? integer * integer : integer + integer));
                    }
                }
                cetta_dense_tensor_v1_release(output);
            }
            unsigned char *encoded; size_t size; CettaDenseTensorV1 *decoded = NULL;
            assert(cetta_dense_tensor_v1_encode(source, &encoded, &size) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_decode(encoded, size, &decoded) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_equal(source, decoded));
            free(encoded); cetta_dense_tensor_v1_release(decoded); cetta_dense_tensor_v1_release(source);
        }
    }
}

static void compact_screening_reference(void) {
    for (size_t width = 0; width <= 67; ++width) {
        size_t shape[] = {37, width};
        double data[37*67], q[67];
        for (size_t j = 0; j < width; ++j)
            q[j] = (double)((int)(j*13 % 37)-18) / 19.;
        for (size_t i = 0; i < 37; ++i) for (size_t j = 0; j < width; ++j)
            data[i*width+j] = sin((double)((i+1)*(j+3))) * .25;
        /* Two distinct f64 values with the same compact value test ambiguity. */
        if (width) { data[0] = .5; data[width] = nextafter(.5, INFINITY); }
        CettaDenseTensorV1 *wide = make(CETTA_DENSE_TENSOR_V1_F64, shape, 2, data, 37*width*8);
        CettaDenseTensorV1 *query = make(CETTA_DENSE_TENSOR_V1_F64, &width, 1, q, width*8);
        CettaDenseTensorV1 *compact = NULL, *cert = NULL, *lo = NULL, *hi = NULL, *exact = NULL;
        assert(cetta_dense_tensor_v1_cast(wide, CETTA_DENSE_TENSOR_V1_F32, &compact) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_nbytes(compact) == 37*width*4);
        assert(cetta_dense_tensor_v1_row_error_bounds(wide, compact, &cert) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_matvec_bounds(compact, query, cert, NULL, 1, &lo, &hi) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_matvec(wide, query, NULL, &exact) == CETTA_DENSE_TENSOR_V1_OK);
        const double *l = cetta_dense_tensor_v1_data(lo), *h = cetta_dense_tensor_v1_data(hi), *s = cetta_dense_tensor_v1_data(exact);
        for (size_t i = 0; i < 37; ++i) assert(l[i] <= s[i] && s[i] <= h[i]);
        /* Provider agreement also covers short tails and zero-width rows. */
        CettaDenseTensorV1 *scalar = NULL, *automatic = NULL;
        assert(cetta_dense_tensor_v1_matvec_with_provider(compact, query, NULL, CETTA_MATVEC_SCALAR4, &scalar) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_matvec(compact, query, NULL, &automatic) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_equal(scalar, automatic));
        cetta_dense_tensor_v1_release(scalar); cetta_dense_tensor_v1_release(automatic);
        for (size_t k = 0; k <= 38; ++k) {
            CettaDenseTensorV1 *candidates = NULL, *target = NULL, *selected = NULL, *top = NULL, *mapped = NULL;
            assert(cetta_dense_tensor_v1_top_k_candidates(lo, hi, k, &candidates) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_top_k(exact, NULL, k, &target) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_matvec(wide, query, candidates, &selected) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_top_k(selected, NULL, k, &top) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_gather(candidates, top, &mapped) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_equal(target, mapped));
            cetta_dense_tensor_v1_release(candidates); cetta_dense_tensor_v1_release(target); cetta_dense_tensor_v1_release(selected);
            cetta_dense_tensor_v1_release(top); cetta_dense_tensor_v1_release(mapped);
        }
        if (width == 67) {
            int old = fegetround(); assert(fesetround(FE_DOWNWARD) == 0);
            CettaDenseTensorV1 *fallback_lo = NULL, *fallback_hi = NULL, *all = NULL;
            assert(cetta_dense_tensor_v1_matvec_bounds(compact, query, cert, NULL, 1, &fallback_lo, &fallback_hi) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_top_k_candidates(fallback_lo, fallback_hi, 5, &all) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_count(all) == 37);
            assert(fesetround(old) == 0);
            cetta_dense_tensor_v1_release(fallback_lo); cetta_dense_tensor_v1_release(fallback_hi); cetta_dense_tensor_v1_release(all);
#if TEST_HAS_MXCSR
            unsigned csr = _mm_getcsr();
            for (unsigned mode = 0; mode < 2; ++mode) {
                _mm_setcsr(csr | (mode ? (1u << 6) : (1u << 15)));
                fallback_lo = NULL; fallback_hi = NULL; all = NULL;
                assert(cetta_dense_tensor_v1_matvec_bounds(compact, query, cert, NULL, 1, &fallback_lo, &fallback_hi) == CETTA_DENSE_TENSOR_V1_OK);
                assert(cetta_dense_tensor_v1_top_k_candidates(fallback_lo, fallback_hi, 5, &all) == CETTA_DENSE_TENSOR_V1_OK);
                assert(cetta_dense_tensor_v1_count(all) == 37);
                _mm_setcsr(csr);
                cetta_dense_tensor_v1_release(fallback_lo); cetta_dense_tensor_v1_release(fallback_hi); cetta_dense_tensor_v1_release(all);
            }
#endif
        }
        cetta_dense_tensor_v1_release(wide); cetta_dense_tensor_v1_release(query); cetta_dense_tensor_v1_release(compact);
        cetta_dense_tensor_v1_release(cert); cetta_dense_tensor_v1_release(lo); cetta_dense_tensor_v1_release(hi); cetta_dense_tensor_v1_release(exact);
    }
    /* Inclusive threshold retains ties; reversed intervals are refused. */
    size_t n = 3; double lowers[] = {3, 2, 0}, uppers[] = {3, 2, 2};
    CettaDenseTensorV1 *lo = make(CETTA_DENSE_TENSOR_V1_F64, &n, 1, lowers, sizeof(lowers));
    CettaDenseTensorV1 *hi = make(CETTA_DENSE_TENSOR_V1_F64, &n, 1, uppers, sizeof(uppers)), *candidates = NULL;
    assert(cetta_dense_tensor_v1_top_k_candidates(lo, hi, 2, &candidates) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_count(candidates) == 3); cetta_dense_tensor_v1_release(candidates);
    lowers[2] = 4;
    CettaDenseTensorV1 *bad = make(CETTA_DENSE_TENSOR_V1_F64, &n, 1, lowers, sizeof(lowers));
    assert(cetta_dense_tensor_v1_top_k_candidates(bad, hi, 0, &candidates) == CETTA_DENSE_TENSOR_V1_VALUE && !candidates);
    cetta_dense_tensor_v1_release(bad); cetta_dense_tensor_v1_release(lo); cetta_dense_tensor_v1_release(hi);
    /* Naive f32 top-1 drops the true winner; bound/refine must keep it. */
    size_t pair_shape[] = {2,1}, one=1;
    double close_values[] = {.5,nextafter(.5,INFINITY)}, query_value=1;
    CettaDenseTensorV1 *wide = make(CETTA_DENSE_TENSOR_V1_F64,pair_shape,2,close_values,sizeof(close_values));
    CettaDenseTensorV1 *query = make(CETTA_DENSE_TENSOR_V1_F64,&one,1,&query_value,8), *narrow=NULL, *cert=NULL, *scores=NULL, *top=NULL;
    assert(cetta_dense_tensor_v1_cast(wide,CETTA_DENSE_TENSOR_V1_F32,&narrow)==CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_matvec(narrow,query,NULL,&scores)==CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_top_k(scores,NULL,1,&top)==CETTA_DENSE_TENSOR_V1_OK);
    assert(((const int64_t *)cetta_dense_tensor_v1_data(top))[0]==0);
    assert(cetta_dense_tensor_v1_row_error_bounds(wide,narrow,&cert)==CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_matvec_bounds(narrow,query,cert,NULL,1,&lo,&hi)==CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_top_k_candidates(lo,hi,1,&candidates)==CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_count(candidates)==2);
    cetta_dense_tensor_v1_release(scores); cetta_dense_tensor_v1_release(top);
    assert(cetta_dense_tensor_v1_matvec(wide,query,candidates,&scores)==CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_top_k(scores,NULL,1,&top)==CETTA_DENSE_TENSOR_V1_OK);
    assert(((const int64_t *)cetta_dense_tensor_v1_data(top))[0]==1);
    cetta_dense_tensor_v1_release(wide); cetta_dense_tensor_v1_release(query); cetta_dense_tensor_v1_release(narrow);
    cetta_dense_tensor_v1_release(cert); cetta_dense_tensor_v1_release(lo); cetta_dense_tensor_v1_release(hi);
    cetta_dense_tensor_v1_release(candidates); cetta_dense_tensor_v1_release(scores); cetta_dense_tensor_v1_release(top);
    /* Portable bits include signed zero and NaN payloads. */
    uint32_t bits[] = {0, UINT32_C(0x80000000), UINT32_C(0x7fc00001)};
    CettaDenseTensorV1 *source = make(CETTA_DENSE_TENSOR_V1_F32, &n, 1, bits, sizeof(bits)), *copy = NULL;
    assert(cetta_dense_tensor_v1_copy(source, &copy) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_compare(source, copy) == 0 && cetta_dense_tensor_v1_equal(source, copy));
    unsigned char *bytes; size_t size;
    assert(cetta_dense_tensor_v1_encode(source, &bytes, &size) == CETTA_DENSE_TENSOR_V1_OK);
    assert(size == 29 && bytes[4] == 4 && bytes[24] == 128 && bytes[25] == 1);
    CettaDenseTensorV1 *decoded = NULL;
    assert(cetta_dense_tensor_v1_decode(bytes, size, &decoded) == CETTA_DENSE_TENSOR_V1_OK && cetta_dense_tensor_v1_equal(source, decoded));
    cetta_dense_tensor_v1_release(source); cetta_dense_tensor_v1_release(copy); cetta_dense_tensor_v1_release(decoded); free(bytes);
}

static void screening_exponent_reference(void) {
    /* Exercise cancellation, f32 underflow, extreme query/scale exponents,
     * and repeated/permuted selectors. Unsafe arithmetic must keep all rows.
     * These are implementation checks, not a proof of IEEE error analysis. */
    uint32_t seed = 901107;
    const size_t widths[] = {1, 3, 4, 7, 31, 137};
    const int source_exp[] = {-1073, -1022, -150, -126, -30, 0, 30, 120};
    const int query_exp[] = {-1020, -150, -30, 0, 30, 700, 1000};
    const int scale_exp[] = {-1000, -30, 0, 30, 1000};
    size_t count = 8;
    int64_t rows[] = {6, 0, 6, 2, 5, 1, 4, 3};
    CettaDenseTensorV1 *indices = make(CETTA_DENSE_TENSOR_V1_I64, &count, 1, rows, sizeof(rows));
    for (size_t w = 0; w < sizeof(widths)/sizeof(widths[0]); ++w) {
        size_t width = widths[w], shape[] = {7, width};
        for (unsigned trial = 0; trial < 80; ++trial) {
            double data[7*137], q[137];
            for (size_t j = 0; j < 7*width; ++j) {
                seed = seed*1664525u + 1013904223u;
                double value = ((double)(seed%2047u)-1023)/1024;
                data[j] = ldexp(value, source_exp[trial%8]);
            }
            for (size_t j = 0; j < width; ++j) {
                seed = seed*1664525u + 1013904223u;
                q[j] = ldexp(((double)(seed%2047u)-1023)/1024, query_exp[(trial/8)%7]);
            }
            double scale = ldexp(1., scale_exp[trial%5]);
            CettaDenseTensorV1 *wide = make(CETTA_DENSE_TENSOR_V1_F64, shape, 2, data, 7*width*8);
            CettaDenseTensorV1 *query = make(CETTA_DENSE_TENSOR_V1_F64, &width, 1, q, width*8);
            CettaDenseTensorV1 *compact=NULL, *cert=NULL, *lo=NULL, *hi=NULL, *exact=NULL;
            assert(cetta_dense_tensor_v1_cast(wide, CETTA_DENSE_TENSOR_V1_F32, &compact) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_row_error_bounds(wide, compact, &cert) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_matvec_bounds(compact, query, cert, indices, scale, &lo, &hi) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_matvec(wide, query, indices, &exact) == CETTA_DENSE_TENSOR_V1_OK);
            const double *l=cetta_dense_tensor_v1_data(lo), *h=cetta_dense_tensor_v1_data(hi), *s=cetta_dense_tensor_v1_data(exact);
            for (size_t i = 0; i < count; ++i) {
                double score = s[i]*scale;
                if (isfinite(score)) assert(l[i] <= score && score <= h[i]);
                else assert(l[i] == -INFINITY && h[i] == INFINITY);
            }
            cetta_dense_tensor_v1_release(wide); cetta_dense_tensor_v1_release(query); cetta_dense_tensor_v1_release(compact);
            cetta_dense_tensor_v1_release(cert); cetta_dense_tensor_v1_release(lo); cetta_dense_tensor_v1_release(hi); cetta_dense_tensor_v1_release(exact);
        }
    }
    cetta_dense_tensor_v1_release(indices);
}

static void selection_reference(void) {
    /* Independent repeated maximum selection, with ties and integer values
     * above binary64's exact range. This does not use the implementation heap. */
    for (unsigned dtype = CETTA_DENSE_TENSOR_V1_I64; dtype <= CETTA_DENSE_TENSOR_V1_F32; ++dtype) {
        for (size_t n = 0; n <= 65u; ++n) {
            int64_t integers[65]; double floats[65]; float compacts[65]; unsigned char bytes[65], mask[65];
            for (size_t i = 0; i < n; ++i) {
                integers[i] = INT64_C(9007199254740992) + (int64_t)((i * 17u) % 23u);
                floats[i] = (double)((int)((i * 17u) % 23u) - 11) / 4.0;
                compacts[i] = (float)floats[i];
                bytes[i] = (unsigned char)((i * 17u) % 23u);
                mask[i] = (i % 3u) != 1u;
            }
            const void *data = dtype == CETTA_DENSE_TENSOR_V1_I64 ? (const void *)integers :
                               dtype == CETTA_DENSE_TENSOR_V1_F64 ? (const void *)floats :
                               dtype == CETTA_DENSE_TENSOR_V1_F32 ? (const void *)compacts : (const void *)bytes;
            CettaDenseTensorV1 *scores = make(dtype, &n, 1u, data, n * cetta_dense_tensor_v1_itemsize(dtype));
            CettaDenseTensorV1 *enabled = make(CETTA_DENSE_TENSOR_V1_U8, &n, 1u, mask, n);
            for (unsigned masked = 0; masked < 2; ++masked) {
                size_t expected[65], count = 0;
                bool used[65] = {false};
                for (;;) {
                    size_t best = n;
                    for (size_t i = 0; i < n; ++i) if (!used[i] && (!masked || mask[i])) {
                        if (best == n || (dtype == CETTA_DENSE_TENSOR_V1_I64 ? integers[i] > integers[best] :
                            dtype == CETTA_DENSE_TENSOR_V1_F64 ? floats[i] > floats[best] :
                            dtype == CETTA_DENSE_TENSOR_V1_F32 ? compacts[i] > compacts[best] : bytes[i] > bytes[best])) best = i;
                    }
                    if (best == n) break;
                    expected[count++] = best; used[best] = true;
                }
                for (size_t k = 0; k <= n + 1u; ++k) {
                    CettaDenseTensorV1 *top = NULL;
                    assert(cetta_dense_tensor_v1_top_k(scores, masked ? enabled : NULL, k, &top) == CETTA_DENSE_TENSOR_V1_OK);
                    size_t wanted = k < count ? k : count;
                    assert(cetta_dense_tensor_v1_count(top) == wanted);
                    const int64_t *actual = cetta_dense_tensor_v1_data(top);
                    for (size_t i = 0; i < wanted; ++i) assert(actual[i] == (int64_t)expected[i]);
                    CettaDenseTensorV1 *gathered = NULL;
                    assert(cetta_dense_tensor_v1_gather(scores, top, &gathered) == CETTA_DENSE_TENSOR_V1_OK);
                    const unsigned char *selected = cetta_dense_tensor_v1_data(gathered);
                    for (size_t i = 0; i < wanted; ++i) assert(!memcmp(selected + i * cetta_dense_tensor_v1_itemsize(dtype),
                        (const unsigned char *)data + expected[i] * cetta_dense_tensor_v1_itemsize(dtype), cetta_dense_tensor_v1_itemsize(dtype)));
                    cetta_dense_tensor_v1_release(gathered); cetta_dense_tensor_v1_release(top);
                }
            }
            CettaDenseTensorV1 *indices = NULL, *rebuilt = NULL, *merged = NULL;
            assert(cetta_dense_tensor_v1_nonzero(enabled, &indices) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_mask_indices(n, indices, &rebuilt) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_equal(enabled, rebuilt));
            assert(cetta_dense_tensor_v1_mask_binary(enabled, rebuilt, false, &merged) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_equal(enabled, merged)); cetta_dense_tensor_v1_release(merged);
            assert(cetta_dense_tensor_v1_mask_binary(enabled, rebuilt, true, &merged) == CETTA_DENSE_TENSOR_V1_OK);
            assert(cetta_dense_tensor_v1_equal(enabled, merged)); cetta_dense_tensor_v1_release(merged);
            assert(cetta_dense_tensor_v1_mask_not(enabled, &merged) == CETTA_DENSE_TENSOR_V1_OK);
            for (size_t i = 0; i < n; ++i)
                assert(((const unsigned char *)cetta_dense_tensor_v1_data(merged))[i] == !mask[i]);
            CettaDenseTensorV1 *combined = NULL;
            assert(cetta_dense_tensor_v1_mask_binary(enabled, merged, true, &combined) == CETTA_DENSE_TENSOR_V1_OK);
            for (size_t i = 0; i < n; ++i)
                assert(((const unsigned char *)cetta_dense_tensor_v1_data(combined))[i] == 1);
            cetta_dense_tensor_v1_release(combined);
            assert(cetta_dense_tensor_v1_mask_binary(enabled, merged, false, &combined) == CETTA_DENSE_TENSOR_V1_OK);
            for (size_t i = 0; i < n; ++i)
                assert(((const unsigned char *)cetta_dense_tensor_v1_data(combined))[i] == 0);
            cetta_dense_tensor_v1_release(combined);
            cetta_dense_tensor_v1_release(merged);
            cetta_dense_tensor_v1_release(rebuilt); cetta_dense_tensor_v1_release(indices);
            cetta_dense_tensor_v1_release(enabled); cetta_dense_tensor_v1_release(scores);
        }
    }
    size_t n = 3; double nonfinite[] = {1, NAN, INFINITY}; unsigned char bits[] = {1, 0, 0};
    CettaDenseTensorV1 *scores = make(CETTA_DENSE_TENSOR_V1_F64, &n, 1, nonfinite, sizeof(nonfinite));
    CettaDenseTensorV1 *mask = make(CETTA_DENSE_TENSOR_V1_U8, &n, 1, bits, sizeof(bits));
    CettaDenseTensorV1 *result = NULL;
    assert(cetta_dense_tensor_v1_top_k(scores, mask, SIZE_MAX, &result) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_count(result) == 1 && ((const int64_t *)cetta_dense_tensor_v1_data(result))[0] == 0);
    cetta_dense_tensor_v1_release(result);
    assert(cetta_dense_tensor_v1_top_k(scores, NULL, 0, &result) == CETTA_DENSE_TENSOR_V1_VALUE && !result);
    cetta_dense_tensor_v1_release(mask);
    bits[2] = 2; mask = make(CETTA_DENSE_TENSOR_V1_U8, &n, 1, bits, sizeof(bits));
    assert(cetta_dense_tensor_v1_nonzero(mask, &result) == CETTA_DENSE_TENSOR_V1_VALUE && !result);
    assert(cetta_dense_tensor_v1_mask_not(mask, &result) == CETTA_DENSE_TENSOR_V1_VALUE && !result);
    assert(cetta_dense_tensor_v1_top_k(scores, mask, 1, &result) == CETTA_DENSE_TENSOR_V1_VALUE && !result);
    cetta_dense_tensor_v1_release(mask); cetta_dense_tensor_v1_release(scores);

    float compact_nonfinite[] = {1, NAN, INFINITY};
    scores = make(CETTA_DENSE_TENSOR_V1_F32, &n, 1, compact_nonfinite, sizeof(compact_nonfinite));
    unsigned char compact_enabled[] = {1, 0, 0};
    mask = make(CETTA_DENSE_TENSOR_V1_U8, &n, 1, compact_enabled, sizeof(compact_enabled));
    assert(cetta_dense_tensor_v1_top_k(scores, mask, 1, &result) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_count(result) == 1 && ((const int64_t *)cetta_dense_tensor_v1_data(result))[0] == 0);
    cetta_dense_tensor_v1_release(result);
    assert(cetta_dense_tensor_v1_top_k(scores, NULL, 0, &result) == CETTA_DENSE_TENSOR_V1_VALUE && !result);
    cetta_dense_tensor_v1_release(mask); cetta_dense_tensor_v1_release(scores);

    int64_t bad = -1; n = 1;
    CettaDenseTensorV1 *indices = make(CETTA_DENSE_TENSOR_V1_I64, &n, 1, &bad, sizeof(bad));
    assert(cetta_dense_tensor_v1_mask_indices(3, indices, &result) == CETTA_DENSE_TENSOR_V1_INDEX && !result);
    cetta_dense_tensor_v1_release(indices);
    bad = 3; indices = make(CETTA_DENSE_TENSOR_V1_I64, &n, 1, &bad, sizeof(bad));
    assert(cetta_dense_tensor_v1_mask_indices(3, indices, &result) == CETTA_DENSE_TENSOR_V1_INDEX && !result);
    cetta_dense_tensor_v1_release(indices);
}

static void matrix_reference(void) {
    for (size_t width = 0; width <= 33; ++width) {
        double data[4*33], query[33], expected[4] = {0};
        for (size_t j = 0; j < width; ++j) query[j] = (double)((int)(j % 5) - 2) / 4;
        for (size_t i = 0; i < 4; ++i) for (size_t j = 0; j < width; ++j) {
            data[i*width+j] = (double)((int)((i*width+j) % 7) - 3) / 4;
            expected[i] += data[i*width+j] * query[j];
        }
        size_t shape[] = {4, width}, count = 3;
        int64_t rows[] = {3, 0, 3};
        CettaDenseTensorV1 *matrix = make(CETTA_DENSE_TENSOR_V1_F64, shape, 2, data, 4*width*8);
        CettaDenseTensorV1 *q = make(CETTA_DENSE_TENSOR_V1_F64, &width, 1, query, width*8);
        CettaDenseTensorV1 *indices = make(CETTA_DENSE_TENSOR_V1_I64, &count, 1, rows, sizeof(rows));
        CettaDenseTensorV1 *out = NULL;
        assert(cetta_dense_tensor_v1_matvec(matrix, q, NULL, &out) == CETTA_DENSE_TENSOR_V1_OK);
        for (size_t i = 0; i < 4; ++i) assert(((const double *)cetta_dense_tensor_v1_data(out))[i] == expected[i]);
        cetta_dense_tensor_v1_release(out);
        assert(cetta_dense_tensor_v1_matvec_with_provider(matrix, q, NULL, CETTA_MATVEC_SCALAR4, &out) == CETTA_DENSE_TENSOR_V1_OK);
        for (size_t i = 0; i < 4; ++i) assert(((const double *)cetta_dense_tensor_v1_data(out))[i] == expected[i]);
        cetta_dense_tensor_v1_release(out);
        assert(cetta_dense_tensor_v1_matvec_with_provider(matrix, q, NULL, (CettaDenseTensorV1MatvecProvider)42, &out)
               == CETTA_DENSE_TENSOR_V1_PROVIDER && !out);
        assert(cetta_dense_tensor_v1_matvec(matrix, q, indices, &out) == CETTA_DENSE_TENSOR_V1_OK);
        for (size_t i = 0; i < count; ++i) assert(((const double *)cetta_dense_tensor_v1_data(out))[i] == expected[rows[i]]);
        cetta_dense_tensor_v1_release(out);
        assert(cetta_dense_tensor_v1_gather(matrix, indices, &out) == CETTA_DENSE_TENSOR_V1_OK);
        for (size_t i = 0; i < count; ++i) if (width) assert(!memcmp(
            (const double *)cetta_dense_tensor_v1_data(out) + i * width, data + rows[i] * width, width * 8));
        cetta_dense_tensor_v1_release(out);
        assert(cetta_dense_tensor_v1_scale(q, 2, &out) == CETTA_DENSE_TENSOR_V1_OK);
        for (size_t i = 0; i < width; ++i) assert(((const double *)cetta_dense_tensor_v1_data(out))[i] == 2*query[i]);
        cetta_dense_tensor_v1_release(out);
        assert(cetta_dense_tensor_v1_div(q, q, &out) == CETTA_DENSE_TENSOR_V1_OK);
        for (size_t i = 0; i < width; ++i) {
            double v = ((const double *)cetta_dense_tensor_v1_data(out))[i];
            assert(query[i] == 0 ? isnan(v) : v == 1);
        }
        cetta_dense_tensor_v1_release(out); cetta_dense_tensor_v1_release(indices);
        cetta_dense_tensor_v1_release(q); cetta_dense_tensor_v1_release(matrix);
    }
    /* Provider conformance uses irregular binary64 values, tails, and repeated
     * row selectors. It checks bit agreement between distinct implementations. */
    size_t width = 1025, shape[] = {7, 1025}, count = 4;
    double data[7*1025], query[1025];
    uint32_t seed = 77109;
    for (size_t j = 0; j < width; ++j) {
        seed = seed * 1664525u + 1013904223u;
        query[j] = ldexp(((double)(seed % 2000001u) - 1000000) / 1000003, (int)(j % 17) - 8);
    }
    for (size_t j = 0; j < 7*width; ++j) {
        seed = seed * 1664525u + 1013904223u;
        data[j] = ((double)(seed % 2000001u) - 1000000) / 1000033;
    }
    int64_t rows[] = {6, 0, 6, 3};
    CettaDenseTensorV1 *matrix = make(CETTA_DENSE_TENSOR_V1_F64, shape, 2, data, sizeof(data));
    CettaDenseTensorV1 *q = make(CETTA_DENSE_TENSOR_V1_F64, &width, 1, query, sizeof(query));
    CettaDenseTensorV1 *indices = make(CETTA_DENSE_TENSOR_V1_I64, &count, 1, rows, sizeof(rows));
    for (unsigned selected = 0; selected < 2; ++selected) {
        CettaDenseTensorV1 *reference = NULL, *actual = NULL;
        assert(cetta_dense_tensor_v1_matvec_with_provider(matrix, q, selected ? indices : NULL,
            CETTA_MATVEC_SCALAR4, &reference) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_matvec(matrix, q, selected ? indices : NULL, &actual) == CETTA_DENSE_TENSOR_V1_OK);
        assert(cetta_dense_tensor_v1_equal(reference, actual)); cetta_dense_tensor_v1_release(actual);
        CettaDenseTensorV1Status status = cetta_dense_tensor_v1_matvec_with_provider(matrix, q, selected ? indices : NULL,
            CETTA_MATVEC_AVX4, &actual);
        if (cetta_dense_tensor_v1_matvec_provider_available(CETTA_MATVEC_AVX4)) {
            assert(status == CETTA_DENSE_TENSOR_V1_OK && cetta_dense_tensor_v1_equal(reference, actual));
            cetta_dense_tensor_v1_release(actual);
        } else assert(status == CETTA_DENSE_TENSOR_V1_PROVIDER && !actual);
        cetta_dense_tensor_v1_release(reference);
    }
    cetta_dense_tensor_v1_release(indices); cetta_dense_tensor_v1_release(q); cetta_dense_tensor_v1_release(matrix);
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    g_hashcons = NULL; g_var_intern = NULL;
    Arena origin, survivor; arena_init_detached(&origin); arena_init_detached(&survivor);
    size_t shape[] = {4u}; int64_t entries[] = {1, 2, 3, 4};
    CettaDenseTensorV1 *tensor = make(CETTA_DENSE_TENSOR_V1_I64, shape, 1u, entries, sizeof(entries));
    Atom *atom = atom_tensor(&origin, tensor);
    assert(origin.external_bytes >= cetta_dense_tensor_v1_retained_bytes(tensor));
    Atom *copy = atom_deep_copy(&survivor, atom);
    assert(copy && copy->arena_id == survivor.identity);
    assert(atom_eq(copy, atom) && atom_hash(copy) == atom_hash(atom));
    assert(cetta_dense_tensor_v1_shares_storage(copy->ground.ptr, tensor));
    codecs(&survivor, atom);
    ArenaMark mark = arena_mark(&survivor);
    atom_tensor(&survivor, tensor);
    assert(survivor.external_bytes >= mark.external_bytes + cetta_dense_tensor_v1_retained_bytes(tensor));
    arena_reset(&survivor, mark);
    assert(survivor.external_bytes == mark.external_bytes);
    assert(atom_eq(copy, atom));

    /* A view outlives both its producing descriptor and original arena. */
    size_t reshaped[] = {2u, 2u}; CettaDenseTensorV1 *view = NULL;
    assert(cetta_dense_tensor_v1_reshape(tensor, reshaped, 2u, &view) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_shares_storage(tensor, view));
    assert(!cetta_dense_tensor_v1_equal(tensor, view));
    cetta_dense_tensor_v1_release(tensor);
    arena_free(&origin);
    assert(((const int64_t *)cetta_dense_tensor_v1_data(view))[3] == 4);

    CettaDenseTensorV1 *updated = NULL; int64_t replacement = 99;
    assert(cetta_dense_tensor_v1_with(copy->ground.ptr, 0u, &replacement, &updated) == CETTA_DENSE_TENSOR_V1_OK);
    assert(!cetta_dense_tensor_v1_shares_storage(copy->ground.ptr, updated));
    assert(((const int64_t *)cetta_dense_tensor_v1_data(copy->ground.ptr))[0] == 1);
    assert(((const int64_t *)cetta_dense_tensor_v1_data(updated))[0] == 99);
    cetta_dense_tensor_v1_release(updated);
    assert(cetta_dense_tensor_v1_with(copy->ground.ptr, 4u, &replacement, &updated) == CETTA_DENSE_TENSOR_V1_INDEX);
    assert(!updated);
    CettaDenseTensorV1Scalar sum;
    assert(cetta_dense_tensor_v1_reduce(copy->ground.ptr, NULL, &sum) == CETTA_DENSE_TENSOR_V1_OK && sum.value.integer == 10);
    assert(cetta_dense_tensor_v1_reduce(copy->ground.ptr, copy->ground.ptr, &sum) == CETTA_DENSE_TENSOR_V1_OK && sum.value.integer == 30);
    assert(cetta_dense_tensor_v1_reduce(view, view, &sum) == CETTA_DENSE_TENSOR_V1_SHAPE);
    assert(cetta_dense_tensor_v1_binary(view, copy->ground.ptr, false, &updated) == CETTA_DENSE_TENSOR_V1_SHAPE);
    cetta_dense_tensor_v1_release(view);

    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, copy_worker, copy->ground.ptr));
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));

    /* Exact bit identity, independent of scalar IEEE comparison. */
    uint64_t bits[] = {UINT64_C(0x7ff8000000000001), UINT64_C(0x8000000000000000)};
    size_t pair[] = {2u};
    CettaDenseTensorV1 *nan = make(CETTA_DENSE_TENSOR_V1_F64, pair, 1u, bits, sizeof(bits));
    CettaDenseTensorV1 *same = NULL;
    assert(cetta_dense_tensor_v1_copy(nan, &same) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_equal(nan, same) && cetta_dense_tensor_v1_compare(nan, same) == 0);
    assert(cetta_dense_tensor_v1_hash(nan) == cetta_dense_tensor_v1_hash(same));
    codecs(&survivor, atom_tensor(&survivor, nan));
    cetta_dense_tensor_v1_release(same);
    bits[1] = 0u;
    same = make(CETTA_DENSE_TENSOR_V1_F64, pair, 1u, bits, sizeof(bits));
    assert(!cetta_dense_tensor_v1_equal(nan, same) && cetta_dense_tensor_v1_compare(nan, same) != 0);
    cetta_dense_tensor_v1_release(same); cetta_dense_tensor_v1_release(nan);

    /* The codec has an independently specified little-endian golden value. */
    int64_t scalar = -2;
    tensor = make(CETTA_DENSE_TENSOR_V1_I64, NULL, 0u, &scalar, 8u);
    const unsigned char golden[] = {'C','T','N','1',1,0,0,0,0,254,255,255,255,255,255,255,255};
    unsigned char *encoded = NULL; size_t size = 0;
    assert(cetta_dense_tensor_v1_encode(tensor, &encoded, &size) == CETTA_DENSE_TENSOR_V1_OK);
    assert(size == sizeof(golden) && !memcmp(encoded, golden, size));
    assert(cetta_dense_tensor_v1_decode(golden, sizeof(golden), &same) == CETTA_DENSE_TENSOR_V1_OK);
    assert(cetta_dense_tensor_v1_equal(tensor, same)); cetta_dense_tensor_v1_release(same);
    for (size_t i = 0; i < size; ++i) {
        same = NULL;
        assert(cetta_dense_tensor_v1_decode(encoded, i, &same) != CETTA_DENSE_TENSOR_V1_OK && !same);
    }
    encoded[4] = 255u;
    assert(cetta_dense_tensor_v1_decode(encoded, size, &same) == CETTA_DENSE_TENSOR_V1_DTYPE && !same);
    free(encoded); cetta_dense_tensor_v1_release(tensor);

    /* Transport limits are checked before allocating a serialized tensor. */
    size_t large_shape[] = {CETTA_ATOM_WIRE_BYTES};
    unsigned char *large = calloc(large_shape[0], 1u);
    assert(large);
    tensor = make(CETTA_DENSE_TENSOR_V1_U8, large_shape, 1u, large, large_shape[0]);
    free(large);
    assert(cetta_dense_tensor_v1_encoded_size(tensor, &size) == CETTA_DENSE_TENSOR_V1_OK);
    assert(size == large_shape[0] + 17u);
    atom = atom_tensor(&survivor, tensor);
    encoded = NULL;
    assert(cetta_atom_wire_encode(atom, &encoded, &size) == ATOM_WIRE_LIMIT && !encoded);
    assert(cetta_durable_value_encode(atom, &encoded, &size) == DURABLE_LIMIT && !encoded);
    cetta_dense_tensor_v1_release(tensor);

    size_t count, bytes, enormous[] = {SIZE_MAX, 2u, 0u, 3u};
    assert(cetta_dense_tensor_v1_byte_count(CETTA_DENSE_TENSOR_V1_F64, enormous, 4u, &count, &bytes) == CETTA_DENSE_TENSOR_V1_OK);
    assert(!count && !bytes);
    tensor = make(CETTA_DENSE_TENSOR_V1_F64, enormous, 4u, NULL, 0u);
    assert(cetta_dense_tensor_v1_reduce(tensor, NULL, &sum) == CETTA_DENSE_TENSOR_V1_OK && sum.value.floating == 0.0);
    cetta_dense_tensor_v1_release(tensor);
    assert(cetta_dense_tensor_v1_byte_count(CETTA_DENSE_TENSOR_V1_F64, enormous, 2u, &count, &bytes) == CETTA_DENSE_TENSOR_V1_OVERFLOW);
    assert(cetta_dense_tensor_v1_byte_count(CETTA_DENSE_TENSOR_V1_F64, enormous, 1u, &count, &bytes) == CETTA_DENSE_TENSOR_V1_OVERFLOW);
    assert(cetta_dense_tensor_v1_new(CETTA_DENSE_TENSOR_V1_I64, shape, 1u, entries, 8u, &tensor) == CETTA_DENSE_TENSOR_V1_COUNT && !tensor);

    int64_t over[] = {INT64_MAX, 1};
    tensor = make(CETTA_DENSE_TENSOR_V1_I64, pair, 1u, over, sizeof(over));
    assert(cetta_dense_tensor_v1_reduce(tensor, NULL, &sum) == CETTA_DENSE_TENSOR_V1_OVERFLOW);
    assert(cetta_dense_tensor_v1_binary(tensor, tensor, false, &same) == CETTA_DENSE_TENSOR_V1_OVERFLOW && !same);
    assert(((const int64_t *)cetta_dense_tensor_v1_data(tensor))[0] == INT64_MAX);
    cetta_dense_tensor_v1_release(tensor);
    unsigned char u8[] = {255, 1};
    tensor = make(CETTA_DENSE_TENSOR_V1_U8, pair, 1u, u8, sizeof(u8));
    assert(cetta_dense_tensor_v1_binary(tensor, tensor, false, &same) == CETTA_DENSE_TENSOR_V1_OVERFLOW && !same);
    assert(cetta_dense_tensor_v1_reduce(tensor, NULL, &sum) == CETTA_DENSE_TENSOR_V1_OK && sum.value.integer == 256);
    cetta_dense_tensor_v1_release(tensor);
    arithmetic_reference();
    selection_reference(); matrix_reference(); compact_screening_reference(); screening_exponent_reference();
    arena_free(&survivor); symbol_table_free(&symbols);
    puts("PASS: native tensor ownership, alternatives, arithmetic, bounds and codecs");
    return 0;
}
