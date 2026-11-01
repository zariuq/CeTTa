#include "atom_wire.h"
#include "tensor_dense_v1.h"
#include "tests/test_runtime_stats_stubs.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void text_boundaries(Arena *source, Arena *target) {
    size_t length = 2u * CETTA_ATOM_WIRE_BYTES;
    unsigned char *data = malloc(length);
    assert(data);
    for (size_t i = 0; i < length; ++i) data[i] = (unsigned char)(i % 251u);
    Atom *value = atom_string_n(source, (const char *)data, length);
    unsigned char *wire = (unsigned char *)(uintptr_t)1;
    size_t size = 1;
    assert(cetta_atom_wire_encode(value, &wire, &size) == ATOM_WIRE_LIMIT);
    assert(!wire && !size);
    /* Header + tag + four-byte string length. */
    assert(cetta_atom_wire_encode_bounded(value, length + 8u, &wire, &size) == ATOM_WIRE_LIMIT);
    assert(!wire && !size);
    assert(cetta_atom_wire_encode_bounded(value, length + 9u, &wire, &size) == ATOM_WIRE_OK);
    assert(size == length + 9u && !memcmp(wire, "CAV1T", 5));
    Atom *decoded = value;
    assert(cetta_atom_wire_decode(target, wire, size, &decoded) == ATOM_WIRE_LIMIT && !decoded);
    assert(cetta_atom_wire_decode_bounded(target, wire, size, size - 1u, &decoded) == ATOM_WIRE_LIMIT && !decoded);
    assert(cetta_atom_wire_decode_bounded(target, wire, size, size, &decoded) == ATOM_WIRE_OK);
    assert(decoded->kind == ATOM_GROUNDED && decoded->ground.gkind == GV_STRING);
    assert(atom_string_len(decoded) == length && !memcmp(decoded->ground.sval, data, length));
    assert(cetta_atom_wire_decode_bounded(target, wire, size - 1u, size, &decoded) == ATOM_WIRE_CORRUPT && !decoded);
    free(wire); free(data);

    for (length = 0; length <= 128u; ++length) {
        value = atom_string_n(source, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", length);
        unsigned char *legacy = NULL; size_t legacy_size = 0;
        assert(cetta_atom_wire_encode(value, &legacy, &legacy_size) == ATOM_WIRE_OK);
        assert(cetta_atom_wire_encode_bounded(value, legacy_size, &wire, &size) == ATOM_WIRE_OK);
        assert(size == legacy_size && !memcmp(wire, legacy, size));
        free(wire); free(legacy);
    }
}

static void variable_and_value_distinctions(Arena *source, Arena *target) {
    Atom *variable = atom_var(source, "x");
    Atom *value = atom_expr(source, (Atom *[]){atom_symbol(source, "row"), variable, variable,
        atom_bool(source, true), atom_symbol(source, "True"), atom_int(source, 1), atom_float(source, 1.0)}, 7);
    unsigned char *wire = NULL; size_t size = 0;
    assert(cetta_atom_wire_encode_bounded(value, 1024u, &wire, &size) == ATOM_WIRE_OK);
    Atom *first = NULL, *second = NULL;
    assert(cetta_atom_wire_decode_bounded(target, wire, size, size, &first) == ATOM_WIRE_OK);
    assert(cetta_atom_wire_decode_bounded(target, wire, size, SIZE_MAX, &second) == ATOM_WIRE_OK);
    assert(var_id_eq(first->expr.elems[1]->var_id, first->expr.elems[2]->var_id));
    assert(!var_id_eq(first->expr.elems[1]->var_id, second->expr.elems[1]->var_id));
    assert(first->expr.elems[3]->ground.gkind == GV_BOOL && first->expr.elems[4]->kind == ATOM_SYMBOL);
    assert(first->expr.elems[5]->ground.gkind == GV_INT && first->expr.elems[6]->ground.gkind == GV_FLOAT);
    /* The allowance cannot authorize trailing bytes or a truncated value. */
    unsigned char *extra = malloc(size + 1u); assert(extra); memcpy(extra, wire, size); extra[size] = 0;
    assert(cetta_atom_wire_decode_bounded(target, extra, size + 1u, SIZE_MAX, &first) == ATOM_WIRE_CORRUPT && !first);
    free(extra); free(wire);
    wire = (unsigned char *)(uintptr_t)1; size = 99;
    assert(cetta_atom_wire_encode_bounded(value, 0, &wire, &size) == ATOM_WIRE_LIMIT && !wire && !size);
    first = value;
    assert(cetta_atom_wire_decode_bounded(NULL, (const unsigned char *)"CAV1", 4, 4, &first) == ATOM_WIRE_INVALID && !first);
    assert(cetta_atom_wire_encode_bounded(value, SIZE_MAX, &wire, &size) == ATOM_WIRE_OK);
    free(wire);
}

static void tensor_allowance(Arena *source, Arena *target) {
    size_t shape[] = {2u * CETTA_ATOM_WIRE_BYTES};
    unsigned char *data = calloc(shape[0], 1u); assert(data); data[shape[0] - 1u] = 123;
    CettaDenseTensorV1 *tensor = NULL;
    assert(cetta_dense_tensor_v1_new(CETTA_DENSE_TENSOR_V1_U8, shape, 1, data, shape[0], &tensor) == CETTA_DENSE_TENSOR_V1_OK);
    free(data);
    Atom *value = atom_tensor(source, tensor), *decoded = NULL;
    cetta_dense_tensor_v1_release(tensor);
    unsigned char *wire = NULL; size_t size = 0;
    assert(cetta_atom_wire_encode(value, &wire, &size) == ATOM_WIRE_LIMIT && !wire);
    assert(cetta_atom_wire_encode_bounded(value, shape[0] + 26u, &wire, &size) == ATOM_WIRE_OK);
    assert(size == shape[0] + 26u);
    assert(cetta_atom_wire_decode_bounded(target, wire, size, size, &decoded) == ATOM_WIRE_OK);
    assert(decoded->ground.gkind == GV_TENSOR && cetta_dense_tensor_v1_equal(value->ground.ptr, decoded->ground.ptr));
    unsigned char *refused = (unsigned char *)(uintptr_t)1;
    size_t refused_size = 1;
    assert(cetta_atom_wire_encode_bounded(value, size - 1u, &refused, &refused_size) == ATOM_WIRE_LIMIT);
    assert(!refused && !refused_size);
    free(wire);
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    g_hashcons = NULL; g_var_intern = NULL;
    Arena source, target; arena_init_detached(&source); arena_init_detached(&target);
    text_boundaries(&source, &target);
    variable_and_value_distinctions(&source, &target);
    tensor_allowance(&source, &target);
    arena_free(&target); arena_free(&source); symbol_table_free(&symbols);
    puts("atom-wire byte allowances: exact boundaries, legacy bytes, variables and tensor passed");
    return 0;
}
