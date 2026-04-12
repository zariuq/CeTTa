#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "stats.h"
#include "symbol.h"
#include "variant_shape.h"

void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta) {
    (void)counter;
    (void)delta;
}

static Atom *make_term3(Arena *a, SymbolId head, Atom *x, Atom *y) {
    Atom *elems[3] = {
        atom_symbol_id(a, head),
        x,
        y,
    };
    return atom_expr(a, elems, 3);
}

static Atom *make_term2(Arena *a, SymbolId head, Atom *x) {
    Atom *elems[2] = {
        atom_symbol_id(a, head),
        x,
    };
    return atom_expr(a, elems, 2);
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

int main(void) {
    SymbolTable symbols;
    init_test_symbols(&symbols);

    Arena src_a;
    Arena src_b;
    Arena src_c;
    Arena out;
    arena_init(&src_a);
    arena_init(&src_b);
    arena_init(&src_c);
    arena_init(&out);

    CettaVariantShapeOptions options = {
        .slot_policy = CETTA_VARIANT_SLOT_FIXED_SPELLING,
        .slot_name = "$_slot",
        .share_immutable = true,
    };
    VariantBank bank;
    variant_bank_init(&bank, options);

    SymbolId pair_sym = symbol_intern_cstr(g_symbols, "pair");
    SymbolId wrap_sym = symbol_intern_cstr(g_symbols, "wrap");

    Atom *x = atom_var_with_id(&src_a, "x", 101);
    Atom *pair_term = make_term3(&src_a, pair_sym, x, x);
    VariantShape pair_shape;
    assert(variant_shape_from_atom(&bank, pair_term, &pair_shape));
    assert(pair_shape.slot_env.len == 1);
    assert(pair_shape.skeleton != NULL);
    Atom *pair_roundtrip = variant_shape_materialize(&out, &pair_shape);
    assert(pair_roundtrip != NULL);
    assert(atom_eq(pair_roundtrip, pair_term));

    ArenaMark out_mark = arena_mark(&out);
    Bindings env;
    bindings_init(&env);
    Atom *bx = atom_var_with_id(&src_a, "bx", 111);
    Atom *by = atom_var_with_id(&src_a, "by", 112);
    Atom *bz = atom_var_with_id(&src_a, "bz", 113);
    Atom *bound_term = make_term3(&src_a, pair_sym, bx, by);
    Atom *wrap_bz = make_term2(&src_a, wrap_sym, bz);
    assert(bindings_add_var(&env, bx, wrap_bz));
    assert(bindings_add_var(&env, by, bz));

    VariantShape bound_shape;
    assert(variant_shape_from_bound_atom(&bank, &env, bound_term, &bound_shape));
    assert(bound_shape.slot_env.len == 1);
    Atom *expected_bound = bindings_apply(&env, &src_b, bound_term);
    assert(expected_bound != NULL);
    arena_reset(&out, out_mark);
    Atom *bound_roundtrip = variant_shape_materialize(&out, &bound_shape);
    assert(bound_roundtrip != NULL);
    assert(atom_eq(bound_roundtrip, expected_bound));

    Atom *u = atom_var_with_id(&src_b, "u", 201);
    Atom *v = atom_var_with_id(&src_c, "v", 301);
    Atom *ctx_term1 = make_term3(&src_b, g_builtin_syms.native_handle,
                                 atom_symbol(&src_b, "kind-a"), u);
    Atom *ctx_term2 = make_term3(&src_c, g_builtin_syms.native_handle,
                                 atom_symbol(&src_c, "kind-b"), v);

    VariantShape ctx_shape1;
    VariantShape ctx_shape2;
    assert(variant_shape_from_atom(&bank, ctx_term1, &ctx_shape1));
    assert(ctx_shape1.slot_env.len == 1);
    assert(variant_shape_from_atom(&bank, ctx_term2, &ctx_shape2));
    assert(ctx_shape2.slot_env.len == 1);

    arena_reset(&out, out_mark);
    Atom *ctx_roundtrip1 = variant_shape_materialize(&out, &ctx_shape1);
    assert(ctx_roundtrip1 != NULL);
    assert(atom_eq(ctx_roundtrip1, ctx_term1));

    arena_reset(&out, out_mark);
    Atom *ctx_roundtrip2 = variant_shape_materialize(&out, &ctx_shape2);
    assert(ctx_roundtrip2 != NULL);
    assert(atom_eq(ctx_roundtrip2, ctx_term2));

    variant_shape_free(&ctx_shape2);
    variant_shape_free(&ctx_shape1);
    variant_shape_free(&bound_shape);
    variant_shape_free(&pair_shape);
    bindings_free(&env);
    variant_bank_free(&bank);
    arena_free(&out);
    arena_free(&src_c);
    arena_free(&src_b);
    arena_free(&src_a);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: variant shape roundtrip");
    return 0;
}
