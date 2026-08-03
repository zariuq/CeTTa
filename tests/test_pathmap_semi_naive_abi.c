#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "space.h"
#include "space_match_backend.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

static Arena *g_persistent_arena = NULL;

Arena *eval_current_persistent_arena(void) {
    return g_persistent_arena;
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static Atom *edge_fact(Arena *a, SymbolId edge, SymbolId lhs, SymbolId rhs) {
    Atom *elems[3] = {
        atom_symbol_id(a, edge),
        atom_symbol_id(a, lhs),
        atom_symbol_id(a, rhs),
    };
    return atom_expr(a, elems, 3);
}

static Atom *edge_pattern(Arena *a, SymbolId edge,
                          const char *lhs_name, VarId lhs,
                          const char *rhs_name, VarId rhs) {
    Atom *elems[3] = {
        atom_symbol_id(a, edge),
        atom_var_with_spelling(
            a, symbol_intern_cstr(g_symbols, lhs_name), lhs),
        atom_var_with_spelling(
            a, symbol_intern_cstr(g_symbols, rhs_name), rhs),
    };
    return atom_expr(a, elems, 3);
}

typedef struct {
    VarId x;
    VarId y;
    VarId z;
    SymbolId a;
    SymbolId b;
    SymbolId c;
    uint64_t rows;
} RowWitness;

static bool check_join_row(const Bindings *bindings, void *ctx_ptr) {
    RowWitness *ctx = ctx_ptr;
    Atom *x = bindings_lookup_id((Bindings *)bindings, ctx->x);
    Atom *y = bindings_lookup_id((Bindings *)bindings, ctx->y);
    Atom *z = bindings_lookup_id((Bindings *)bindings, ctx->z);
    assert(x && x->kind == ATOM_SYMBOL && x->sym_id == ctx->a);
    assert(y && y->kind == ATOM_SYMBOL && y->sym_id == ctx->b);
    assert(z && z->kind == ATOM_SYMBOL && z->sym_id == ctx->c);
    ctx->rows++;
    return true;
}

enum {
    CLOSURE_NODE_COUNT = 24,
};

typedef struct {
    VarId from;
    VarId to;
    const SymbolId *nodes;
    bool *next;
    uint64_t rows;
} ClosureWitness;

static CettaIndex closure_node_index(const ClosureWitness *ctx,
                                     SymbolId symbol) {
    for (CettaIndex i = 0; i < CLOSURE_NODE_COUNT; i++) {
        if (ctx->nodes[i] == symbol)
            return i;
    }
    return UINT64_MAX;
}

static bool collect_closure_row(const Bindings *bindings, void *ctx_ptr) {
    ClosureWitness *ctx = ctx_ptr;
    Atom *from = bindings_lookup_id((Bindings *)bindings, ctx->from);
    Atom *to = bindings_lookup_id((Bindings *)bindings, ctx->to);
    CettaIndex from_idx;
    CettaIndex to_idx;

    assert(from && from->kind == ATOM_SYMBOL);
    assert(to && to->kind == ATOM_SYMBOL);
    from_idx = closure_node_index(ctx, from->sym_id);
    to_idx = closure_node_index(ctx, to->sym_id);
    assert(from_idx != UINT64_MAX);
    assert(to_idx != UINT64_MAX);
    assert(from_idx < to_idx);
    ctx->next[from_idx * CLOSURE_NODE_COUNT + to_idx] = true;
    ctx->rows++;
    return true;
}

static uint64_t bool_matrix_count(const bool *matrix) {
    uint64_t count = 0;
    for (CettaIndex i = 0;
         i < CLOSURE_NODE_COUNT * CLOSURE_NODE_COUNT; i++) {
        if (matrix[i])
            count++;
    }
    return count;
}

static void run_transitive_closure_gate(
    Arena *arena, TermUniverse *universe, SymbolId edge, SymbolId reach) {
    const VarId from_var = 2001;
    const VarId mid_var = 2002;
    const VarId to_var = 2003;
    SymbolId nodes[CLOSURE_NODE_COUNT];
    bool reached[CLOSURE_NODE_COUNT * CLOSURE_NODE_COUNT] = {false};
    bool frontier[CLOSURE_NODE_COUNT * CLOSURE_NODE_COUNT] = {false};
    Atom *patterns[2];
    Space old;
    Space known;
    uint64_t semi_naive_rows = 0;
    uint64_t full_rederivation_rows = 0;
    uint64_t rounds = 0;

    for (CettaIndex i = 0; i < CLOSURE_NODE_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof(name), "closure-node-%llu",
                 (unsigned long long)i);
        nodes[i] = symbol_intern_cstr(g_symbols, name);
    }

    space_init_with_universe(&old, universe);
    old.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&old, SPACE_ENGINE_PATHMAP));
    for (CettaIndex i = 0; i + 1 < CLOSURE_NODE_COUNT; i++) {
        AtomId edge_id = term_universe_store_atom_id(
            universe, NULL,
            edge_fact(arena, edge, nodes[i], nodes[i + 1]));
        assert(edge_id != CETTA_ATOM_ID_NONE);
        space_add_atom_id(&old, edge_id);
    }

    space_init_with_universe(&known, universe);
    known.kind = SPACE_KIND_HASH;
    assert(space_match_backend_snapshot_clone(&known, &old));
    for (CettaIndex i = 0; i + 1 < CLOSURE_NODE_COUNT; i++) {
        AtomId reach_id = term_universe_store_atom_id(
            universe, NULL,
            edge_fact(arena, reach, nodes[i], nodes[i + 1]));
        assert(reach_id != CETTA_ATOM_ID_NONE);
        space_add_atom_id(&known, reach_id);
        reached[i * CLOSURE_NODE_COUNT + i + 1] = true;
        frontier[i * CLOSURE_NODE_COUNT + i + 1] = true;
    }

    patterns[0] =
        edge_pattern(arena, reach, "closure-from", from_var,
                     "closure-mid", mid_var);
    patterns[1] =
        edge_pattern(arena, edge, "closure-mid-again", mid_var,
                     "closure-to", to_var);
    test_runtime_stats_reset_counters();

    while (bool_matrix_count(frontier) != 0u) {
        bool next[CLOSURE_NODE_COUNT * CLOSURE_NODE_COUNT] = {false};
        ClosureWitness witness = {
            .from = from_var,
            .to = to_var,
            .nodes = nodes,
            .next = next,
            .rows = 0,
        };
        Space next_old;

        rounds++;
        assert(space_match_backend_visit_conjunction_semi_naive(
            &known, &old, arena, patterns, 2, NULL,
            collect_closure_row, &witness));
        semi_naive_rows += witness.rows;

        for (CettaIndex from = 0; from < CLOSURE_NODE_COUNT; from++) {
            for (CettaIndex to = 0; to + 1 < CLOSURE_NODE_COUNT; to++) {
                if (reached[from * CLOSURE_NODE_COUNT + to])
                    full_rederivation_rows++;
            }
        }

        space_init_with_universe(&next_old, universe);
        next_old.kind = SPACE_KIND_HASH;
        assert(space_match_backend_snapshot_clone(&next_old, &known));
        space_free(&old);
        old = next_old;

        memset(frontier, 0, sizeof(frontier));
        for (CettaIndex from = 0; from < CLOSURE_NODE_COUNT; from++) {
            for (CettaIndex to = 0; to < CLOSURE_NODE_COUNT; to++) {
                CettaIndex slot = from * CLOSURE_NODE_COUNT + to;
                if (!next[slot] || reached[slot])
                    continue;
                AtomId reach_id = term_universe_store_atom_id(
                    universe, NULL,
                    edge_fact(arena, reach, nodes[from], nodes[to]));
                assert(reach_id != CETTA_ATOM_ID_NONE);
                space_add_atom_id(&known, reach_id);
                reached[slot] = true;
                frontier[slot] = true;
            }
        }
    }

    {
        const uint64_t expected_reach =
            CLOSURE_NODE_COUNT * (CLOSURE_NODE_COUNT - 1u) / 2u;
        const uint64_t expected_derived =
            (CLOSURE_NODE_COUNT - 1u) *
            (CLOSURE_NODE_COUNT - 2u) / 2u;
        const uint64_t indexed_queries = test_runtime_stats_counter(
            CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_QUERY);
        const uint64_t indexed_rows = test_runtime_stats_counter(
            CETTA_RUNTIME_COUNTER_PATHMAP_INDEXED_ROW_EMIT);
        assert(bool_matrix_count(reached) == expected_reach);
        assert(semi_naive_rows == expected_derived);
        assert(rounds == CLOSURE_NODE_COUNT - 1u);
        assert(semi_naive_rows * 4u < full_rederivation_rows);
        if (indexed_queries != rounds || indexed_rows != semi_naive_rows) {
            fprintf(stderr,
                    "unexpected indexed closure stats: queries=%llu/%llu, "
                    "rows=%llu/%llu\n",
                    (unsigned long long)indexed_queries,
                    (unsigned long long)rounds,
                    (unsigned long long)indexed_rows,
                    (unsigned long long)semi_naive_rows);
        }
        assert(indexed_queries == rounds);
        assert(indexed_rows == semi_naive_rows);
        printf("PASS: C PathMap closure uses delta frontiers "
               "(rounds=%llu, indexed-queries=%llu, delta-rows=%llu, "
               "full-rederive-rows=%llu)\n",
               (unsigned long long)rounds,
               (unsigned long long)indexed_queries,
               (unsigned long long)semi_naive_rows,
               (unsigned long long)full_rederivation_rows);
    }

    space_free(&known);
    space_free(&old);
}

int main(void) {
    const VarId x_var = 1001;
    const VarId y_var = 1002;
    const VarId z_var = 1003;
    const VarId independent_lhs_var = 1004;
    const VarId independent_rhs_var = 1005;
    SymbolTable symbols;
    Arena persistent;
    Arena arena;
    TermUniverse universe;
    Space known;
    Space old;
    Space overlay_base;
    Space overlay;
    Space before_removal;
    Space unrelated;
    SymbolId edge;
    SymbolId reach;
    SymbolId a_sym;
    SymbolId b_sym;
    SymbolId c_sym;
    AtomId edge_ab;
    AtomId edge_bc;
    Atom *patterns[2];
    Atom *independent_patterns[2];
    uint64_t count = 0;
    RowWitness witness;

    assert(setenv("CETTA_PATHMAP_QUERY_INDEX", "1", 1) == 0);
    init_test_symbols(&symbols);
    term_universe_init(&universe);
    arena_init(&persistent);
    term_universe_set_persistent_arena(&universe, &persistent);
    g_persistent_arena = &persistent;
    arena_init(&arena);

    space_init_with_universe(&known, &universe);
    known.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&known, SPACE_ENGINE_PATHMAP));

    edge = symbol_intern_cstr(g_symbols, "edge");
    reach = symbol_intern_cstr(g_symbols, "reach");
    a_sym = symbol_intern_cstr(g_symbols, "a");
    b_sym = symbol_intern_cstr(g_symbols, "b");
    c_sym = symbol_intern_cstr(g_symbols, "c");
    edge_ab = term_universe_store_atom_id(
        &universe, NULL, edge_fact(&arena, edge, a_sym, b_sym));
    edge_bc = term_universe_store_atom_id(
        &universe, NULL, edge_fact(&arena, edge, b_sym, c_sym));
    assert(edge_ab != CETTA_ATOM_ID_NONE);
    assert(edge_bc != CETTA_ATOM_ID_NONE);
    space_add_atom_id(&known, edge_ab);

    space_init_with_universe(&old, &universe);
    old.kind = SPACE_KIND_HASH;
    assert(space_match_backend_snapshot_clone(&old, &known));
    assert(space_length64(&old) == 1);

    space_add_atom_id(&known, edge_bc);
    space_add_atom_id(&known, edge_bc);
    assert(space_length64(&known) == 3);
    assert(space_length64(&old) == 1);

    /*
     * A PathMap direct query returns finalized bindings rather than physical
     * row indices.  An overlay must therefore honor its frozen visible prefix
     * even after the base receives another matching occurrence.
     */
    space_init_with_universe(&overlay_base, &universe);
    overlay_base.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(
        &overlay_base, SPACE_ENGINE_PATHMAP));
    space_add_atom_id(&overlay_base, edge_bc);
    space_add_atom_id(&overlay_base, edge_bc);
    space_init_overlay(&overlay, &overlay_base);
    space_add_atom_id(&overlay_base, edge_bc);
    {
        SubstMatchSet matches;
        Atom *ground_bc = edge_fact(&arena, edge, b_sym, c_sym);
        space_subst_query(&overlay, &arena, ground_bc, &matches);
        assert(matches.len == 2);
        smset_free(&matches);

        assert(space_remove_atom_id(&overlay, edge_bc));
        space_subst_query(&overlay, &arena, ground_bc, &matches);
        assert(matches.len == 1);
        smset_free(&matches);

        space_add_atom_id(&overlay, edge_bc);
        space_subst_query(&overlay, &arena, ground_bc, &matches);
        assert(matches.len == 2);
        smset_free(&matches);
    }
    space_free(&overlay);
    space_free(&overlay_base);

    patterns[0] =
        edge_pattern(&arena, edge, "x", x_var, "y", y_var);
    patterns[1] =
        edge_pattern(&arena, edge, "same-id-new-spelling", y_var,
                     "z", z_var);
    assert(space_match_backend_count_conjunction_semi_naive(
        &known, &old, &arena, patterns, 2, NULL, &count));
    if (count != 2)
        fprintf(stderr, "unexpected semi-naive bag count: %llu\n",
                (unsigned long long)count);
    assert(count == 2);

    witness = (RowWitness){
        .x = x_var,
        .y = y_var,
        .z = z_var,
        .a = a_sym,
        .b = b_sym,
        .c = c_sym,
        .rows = 0,
    };
    assert(space_match_backend_visit_conjunction_semi_naive(
        &known, &old, &arena, patterns, 2, NULL, check_join_row, &witness));
    assert(witness.rows == 2);

    independent_patterns[0] =
        edge_pattern(&arena, edge, "x", x_var, "same-spelling",
                     independent_lhs_var);
    independent_patterns[1] =
        edge_pattern(&arena, edge, "same-spelling",
                     independent_rhs_var, "z", z_var);
    count = 0;
    assert(space_match_backend_count_conjunction_semi_naive(
        &known, &old, &arena, independent_patterns, 2, NULL, &count));
    if (count != 8)
        fprintf(stderr,
                "distinct same-spelling variables aliased: %llu rows\n",
                (unsigned long long)count);
    assert(count == 8);

    space_init_with_universe(&before_removal, &universe);
    before_removal.kind = SPACE_KIND_HASH;
    assert(space_match_backend_snapshot_clone(&before_removal, &known));
    assert(space_remove_atom_id(&known, edge_ab));
    count = UINT64_MAX;
    assert(!space_match_backend_count_conjunction_semi_naive(
        &known, &before_removal, &arena, patterns, 2, NULL, &count));
    assert(count == 0);

    space_init_with_universe(&unrelated, &universe);
    unrelated.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&unrelated, SPACE_ENGINE_PATHMAP));
    count = UINT64_MAX;
    assert(!space_match_backend_count_conjunction_semi_naive(
        &known, &unrelated, &arena, patterns, 2, NULL, &count));
    assert(count == 0);

    run_transitive_closure_gate(&arena, &universe, edge, reach);

    space_free(&unrelated);
    space_free(&before_removal);
    space_free(&old);
    space_free(&known);
    arena_free(&arena);
    g_persistent_arena = NULL;
    term_universe_free(&universe);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    puts("PASS: PathMap semi-naive snapshot ABI");
    return 0;
}
