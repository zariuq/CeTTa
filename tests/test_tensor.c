#include "tensor.h"
#include "atom_wire.h"
#include "durable_value.h"
#include "tests/test_runtime_stats_stubs.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>

static CettaTensor *make(CettaTensorDType dtype, const size_t *shape, size_t rank,
    const void *data, size_t bytes) {
    CettaTensor *tensor = NULL;
    assert(cetta_tensor_new(dtype, shape, rank, data, bytes, &tensor) == CETTA_TENSOR_OK);
    return tensor;
}

static void *copy_worker(void *input) {
    CettaTensor *source = input;
    for (unsigned i = 0; i < 400u; ++i) {
        Arena arena; arena_init_detached(&arena);
        Atom *atom = atom_tensor(&arena, source);
        assert(atom && cetta_tensor_equal(atom->ground.ptr, source));
        CettaTensor *view = NULL;
        size_t shape[] = {2u, 2u};
        assert(cetta_tensor_reshape(source, shape, 2u, &view) == CETTA_TENSOR_OK);
        assert(cetta_tensor_shares_storage(source, view));
        cetta_tensor_release(view);
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
    for (unsigned dtype = CETTA_TENSOR_I64; dtype <= CETTA_TENSOR_U8; ++dtype) {
        for (size_t n = 0; n <= 32u; ++n) {
            unsigned char entries[32u * 8u];
            double float_sum = 0.0, float_dot = 0.0;
            int64_t int_sum = 0, int_dot = 0;
            size_t width = cetta_tensor_itemsize(dtype);
            for (size_t i = 0; i < n; ++i) {
                int64_t integer = dtype == CETTA_TENSOR_U8 ? (int64_t)(i % 8u) : (int64_t)(i % 11u) - 5;
                double floating = (double)integer / 4.0;
                if (dtype == CETTA_TENSOR_F64) memcpy(entries + i * width, &floating, width);
                else if (dtype == CETTA_TENSOR_I64) memcpy(entries + i * width, &integer, width);
                else entries[i] = (unsigned char)integer;
                float_sum += floating; float_dot += floating * floating;
                int_sum += integer; int_dot += integer * integer;
            }
            CettaTensor *source = make(dtype, &n, 1u, entries, n * width);
            CettaTensorScalar result;
            assert(cetta_tensor_reduce(source, NULL, &result) == CETTA_TENSOR_OK);
            if (dtype == CETTA_TENSOR_F64) assert(result.value.floating == float_sum);
            else assert(result.value.integer == int_sum);
            assert(cetta_tensor_reduce(source, source, &result) == CETTA_TENSOR_OK);
            if (dtype == CETTA_TENSOR_F64) assert(result.value.floating == float_dot);
            else assert(result.value.integer == int_dot);
            for (unsigned multiply = 0; multiply < 2u; ++multiply) {
                CettaTensor *output = NULL;
                assert(cetta_tensor_binary(source, source, multiply, &output) == CETTA_TENSOR_OK);
                const unsigned char *data = cetta_tensor_data(output);
                for (size_t i = 0; i < n; ++i) {
                    int64_t integer = dtype == CETTA_TENSOR_U8 ? (int64_t)(i % 8u) : (int64_t)(i % 11u) - 5;
                    double floating = (double)integer / 4.0;
                    if (dtype == CETTA_TENSOR_F64) {
                        double actual; memcpy(&actual, data + i * width, width);
                        assert(actual == (multiply ? floating * floating : floating + floating));
                    } else {
                        int64_t actual;
                        if (dtype == CETTA_TENSOR_U8) actual = data[i];
                        else memcpy(&actual, data + i * width, width);
                        assert(actual == (multiply ? integer * integer : integer + integer));
                    }
                }
                cetta_tensor_release(output);
            }
            unsigned char *encoded; size_t size; CettaTensor *decoded = NULL;
            assert(cetta_tensor_encode(source, &encoded, &size) == CETTA_TENSOR_OK);
            assert(cetta_tensor_decode(encoded, size, &decoded) == CETTA_TENSOR_OK);
            assert(cetta_tensor_equal(source, decoded));
            free(encoded); cetta_tensor_release(decoded); cetta_tensor_release(source);
        }
    }
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    g_hashcons = NULL; g_var_intern = NULL;
    Arena origin, survivor; arena_init_detached(&origin); arena_init_detached(&survivor);
    size_t shape[] = {4u}; int64_t entries[] = {1, 2, 3, 4};
    CettaTensor *tensor = make(CETTA_TENSOR_I64, shape, 1u, entries, sizeof(entries));
    Atom *atom = atom_tensor(&origin, tensor);
    assert(origin.external_bytes >= cetta_tensor_retained_bytes(tensor));
    Atom *copy = atom_deep_copy(&survivor, atom);
    assert(copy && copy->arena_id == survivor.identity);
    assert(atom_eq(copy, atom) && atom_hash(copy) == atom_hash(atom));
    assert(cetta_tensor_shares_storage(copy->ground.ptr, tensor));
    codecs(&survivor, atom);
    ArenaMark mark = arena_mark(&survivor);
    atom_tensor(&survivor, tensor);
    assert(survivor.external_bytes >= mark.external_bytes + cetta_tensor_retained_bytes(tensor));
    arena_reset(&survivor, mark);
    assert(survivor.external_bytes == mark.external_bytes);
    assert(atom_eq(copy, atom));

    /* A view outlives both its producing descriptor and original arena. */
    size_t reshaped[] = {2u, 2u}; CettaTensor *view = NULL;
    assert(cetta_tensor_reshape(tensor, reshaped, 2u, &view) == CETTA_TENSOR_OK);
    assert(cetta_tensor_shares_storage(tensor, view));
    assert(!cetta_tensor_equal(tensor, view));
    cetta_tensor_release(tensor);
    arena_free(&origin);
    assert(((const int64_t *)cetta_tensor_data(view))[3] == 4);

    CettaTensor *updated = NULL; int64_t replacement = 99;
    assert(cetta_tensor_with(copy->ground.ptr, 0u, &replacement, &updated) == CETTA_TENSOR_OK);
    assert(!cetta_tensor_shares_storage(copy->ground.ptr, updated));
    assert(((const int64_t *)cetta_tensor_data(copy->ground.ptr))[0] == 1);
    assert(((const int64_t *)cetta_tensor_data(updated))[0] == 99);
    cetta_tensor_release(updated);
    assert(cetta_tensor_with(copy->ground.ptr, 4u, &replacement, &updated) == CETTA_TENSOR_INDEX);
    assert(!updated);
    CettaTensorScalar sum;
    assert(cetta_tensor_reduce(copy->ground.ptr, NULL, &sum) == CETTA_TENSOR_OK && sum.value.integer == 10);
    assert(cetta_tensor_reduce(copy->ground.ptr, copy->ground.ptr, &sum) == CETTA_TENSOR_OK && sum.value.integer == 30);
    assert(cetta_tensor_reduce(view, view, &sum) == CETTA_TENSOR_SHAPE);
    assert(cetta_tensor_binary(view, copy->ground.ptr, false, &updated) == CETTA_TENSOR_SHAPE);
    cetta_tensor_release(view);

    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, copy_worker, copy->ground.ptr));
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));

    /* Exact bit identity, independent of scalar IEEE comparison. */
    uint64_t bits[] = {UINT64_C(0x7ff8000000000001), UINT64_C(0x8000000000000000)};
    size_t pair[] = {2u};
    CettaTensor *nan = make(CETTA_TENSOR_F64, pair, 1u, bits, sizeof(bits));
    CettaTensor *same = NULL;
    assert(cetta_tensor_copy(nan, &same) == CETTA_TENSOR_OK);
    assert(cetta_tensor_equal(nan, same) && cetta_tensor_compare(nan, same) == 0);
    assert(cetta_tensor_hash(nan) == cetta_tensor_hash(same));
    codecs(&survivor, atom_tensor(&survivor, nan));
    cetta_tensor_release(same);
    bits[1] = 0u;
    same = make(CETTA_TENSOR_F64, pair, 1u, bits, sizeof(bits));
    assert(!cetta_tensor_equal(nan, same) && cetta_tensor_compare(nan, same) != 0);
    cetta_tensor_release(same); cetta_tensor_release(nan);

    /* The codec has an independently specified little-endian golden value. */
    int64_t scalar = -2;
    tensor = make(CETTA_TENSOR_I64, NULL, 0u, &scalar, 8u);
    const unsigned char golden[] = {'C','T','N','1',1,0,0,0,0,254,255,255,255,255,255,255,255};
    unsigned char *encoded = NULL; size_t size = 0;
    assert(cetta_tensor_encode(tensor, &encoded, &size) == CETTA_TENSOR_OK);
    assert(size == sizeof(golden) && !memcmp(encoded, golden, size));
    assert(cetta_tensor_decode(golden, sizeof(golden), &same) == CETTA_TENSOR_OK);
    assert(cetta_tensor_equal(tensor, same)); cetta_tensor_release(same);
    for (size_t i = 0; i < size; ++i) {
        same = NULL;
        assert(cetta_tensor_decode(encoded, i, &same) != CETTA_TENSOR_OK && !same);
    }
    encoded[4] = 255u;
    assert(cetta_tensor_decode(encoded, size, &same) == CETTA_TENSOR_DTYPE && !same);
    free(encoded); cetta_tensor_release(tensor);

    /* Transport limits are checked before allocating a serialized tensor. */
    size_t large_shape[] = {CETTA_ATOM_WIRE_BYTES};
    unsigned char *large = calloc(large_shape[0], 1u);
    assert(large);
    tensor = make(CETTA_TENSOR_U8, large_shape, 1u, large, large_shape[0]);
    free(large);
    assert(cetta_tensor_encoded_size(tensor, &size) == CETTA_TENSOR_OK);
    assert(size == large_shape[0] + 17u);
    atom = atom_tensor(&survivor, tensor);
    encoded = NULL;
    assert(cetta_atom_wire_encode(atom, &encoded, &size) == ATOM_WIRE_LIMIT && !encoded);
    assert(cetta_durable_value_encode(atom, &encoded, &size) == DURABLE_LIMIT && !encoded);
    cetta_tensor_release(tensor);

    size_t count, bytes, enormous[] = {SIZE_MAX, 2u, 0u, 3u};
    assert(cetta_tensor_byte_count(CETTA_TENSOR_F64, enormous, 4u, &count, &bytes) == CETTA_TENSOR_OK);
    assert(!count && !bytes);
    tensor = make(CETTA_TENSOR_F64, enormous, 4u, NULL, 0u);
    assert(cetta_tensor_reduce(tensor, NULL, &sum) == CETTA_TENSOR_OK && sum.value.floating == 0.0);
    cetta_tensor_release(tensor);
    assert(cetta_tensor_byte_count(CETTA_TENSOR_F64, enormous, 2u, &count, &bytes) == CETTA_TENSOR_OVERFLOW);
    assert(cetta_tensor_byte_count(CETTA_TENSOR_F64, enormous, 1u, &count, &bytes) == CETTA_TENSOR_OVERFLOW);
    assert(cetta_tensor_new(CETTA_TENSOR_I64, shape, 1u, entries, 8u, &tensor) == CETTA_TENSOR_COUNT && !tensor);

    int64_t over[] = {INT64_MAX, 1};
    tensor = make(CETTA_TENSOR_I64, pair, 1u, over, sizeof(over));
    assert(cetta_tensor_reduce(tensor, NULL, &sum) == CETTA_TENSOR_OVERFLOW);
    assert(cetta_tensor_binary(tensor, tensor, false, &same) == CETTA_TENSOR_OVERFLOW && !same);
    assert(((const int64_t *)cetta_tensor_data(tensor))[0] == INT64_MAX);
    cetta_tensor_release(tensor);
    unsigned char u8[] = {255, 1};
    tensor = make(CETTA_TENSOR_U8, pair, 1u, u8, sizeof(u8));
    assert(cetta_tensor_binary(tensor, tensor, false, &same) == CETTA_TENSOR_OVERFLOW && !same);
    assert(cetta_tensor_reduce(tensor, NULL, &sum) == CETTA_TENSOR_OK && sum.value.integer == 256);
    cetta_tensor_release(tensor);
    arithmetic_reference();
    arena_free(&survivor); symbol_table_free(&symbols);
    puts("PASS: native tensor ownership, alternatives, arithmetic, bounds and codecs");
    return 0;
}
