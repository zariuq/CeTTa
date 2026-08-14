#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "abt.h"
#include "atom.h"
#include "match.h"
#include "symbol.h"

enum {
    ABT_BENCH_TREE_DEPTH = 14,
    ABT_BENCH_SHARED_DAG_DEPTH = 20,
    ABT_BENCH_NAME_TREE_DEPTH = 12,
    ABT_BENCH_SCOPE_DEPTH = 2048,
    ABT_BENCH_TELESCOPE_DEPTH = 1024,
    ABT_BENCH_REPEATS = 9,
};

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
           (uint64_t)ts.tv_nsec;
}

static int compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static Atom *abt_leaf(Arena *arena) {
    return atom_expr2(arena, atom_symbol(arena, "idx"), atom_int(arena, 0));
}

static Atom *build_balanced_abt(Arena *arena, uint32_t depth) {
    if (depth == 0u) return abt_leaf(arena);
    Atom *left = build_balanced_abt(arena, depth - 1u);
    Atom *right = build_balanced_abt(arena, depth - 1u);
    return atom_expr3(arena, atom_symbol(arena, "App"), left, right);
}

static Atom *build_balanced_metavars(Arena *arena, uint32_t depth) {
    if (depth == 0u)
        return atom_var_with_id(arena, "x", fresh_var_id());
    Atom *left = build_balanced_metavars(arena, depth - 1u);
    Atom *right = build_balanced_metavars(arena, depth - 1u);
    return atom_expr3(arena, atom_symbol(arena, "App"), left, right);
}

static Atom *build_shared_abt(Arena *arena, uint32_t depth) {
    if (depth == 0u) return abt_leaf(arena);
    Atom *child = build_shared_abt(arena, depth - 1u);
    return atom_expr3(arena, atom_symbol(arena, "App"), child, child);
}

static Atom *build_balanced_with_leaf(Arena *arena, uint32_t depth,
                                      Atom *leaf) {
    if (depth == 0u) return leaf;
    Atom *left = build_balanced_with_leaf(arena, depth - 1u, leaf);
    Atom *right = build_balanced_with_leaf(arena, depth - 1u, leaf);
    return atom_expr3(arena, atom_symbol(arena, "App"), left, right);
}

static Atom *build_deep_binder(Arena *arena, const char *head,
                               uint32_t depth) {
    assert(depth > 0u);
    Atom *term = atom_expr2(
        arena, atom_symbol(arena, "idx"), atom_int(arena, depth - 1u));
    Atom *type = atom_symbol(arena, "A");
    for (uint32_t i = 0; i < depth; i++)
        term = atom_expr3(arena, atom_symbol(arena, head), type, term);
    return term;
}

static void assert_scratch_reuse(const Arena *scratch, uint32_t repeat,
                                 size_t *stable_reserved_bytes) {
    assert(scratch->live_bytes == 0u);
    if (repeat == 0u)
        *stable_reserved_bytes = scratch->reserved_bytes;
    else
        assert(scratch->reserved_bytes == *stable_reserved_bytes);
}

int main(void) {
    SymbolTable symbols;
    Arena inputs;
    Arena scratch;
    AbtSignature signature;
    uint64_t subst_ns[ABT_BENCH_REPEATS];
    uint64_t freshen_ns[ABT_BENCH_REPEATS];
    uint64_t shared_ns[ABT_BENCH_REPEATS];
    uint64_t print_ns[ABT_BENCH_REPEATS];
    uint64_t parse_ns[ABT_BENCH_REPEATS];
    uint64_t symbol_close_ns[ABT_BENCH_REPEATS];
    uint64_t tagged_close_ns[ABT_BENCH_REPEATS];
    uint64_t deep_scope_ns[ABT_BENCH_REPEATS];
    uint64_t telescope_scope_ns[ABT_BENCH_REPEATS];
    size_t subst_reserved = 0u;
    size_t print_reserved = 0u;
    size_t parse_reserved = 0u;
    size_t freshen_reserved = 0u;
    size_t shared_reserved = 0u;
    size_t symbol_close_reserved = 0u;
    size_t tagged_close_reserved = 0u;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&inputs);
    arena_init(&scratch);
    abt_signature_init(&signature);
    assert(abt_signature_add_defaults(&signature, &inputs));

    Atom *canonical = build_balanced_abt(&inputs, ABT_BENCH_TREE_DEPTH);
    Atom *metavariables = build_balanced_metavars(
        &inputs, ABT_BENCH_TREE_DEPTH);
    Atom *shared = build_shared_abt(&inputs, ABT_BENCH_SHARED_DAG_DEPTH);
    Atom *replacement = atom_symbol(&inputs, "z");
    Atom *printable = atom_expr3(
        &inputs, atom_symbol(&inputs, "Lam"), atom_symbol(&inputs, "A"),
        canonical);
    Atom *syntax = abt_print(&signature, &inputs, printable);
    assert(syntax);
    Atom *symbol_name = atom_symbol(&inputs, "x");
    Atom *tagged_name = atom_expr2(
        &inputs, atom_symbol(&inputs, "Free"), atom_string(&inputs, "x"));
    Atom *symbol_name_tree = build_balanced_with_leaf(
        &inputs, ABT_BENCH_NAME_TREE_DEPTH, symbol_name);
    Atom *tagged_name_tree = build_balanced_with_leaf(
        &inputs, ABT_BENCH_NAME_TREE_DEPTH, tagged_name);
    Atom *deep_scope = build_deep_binder(
        &inputs, "Lam", ABT_BENCH_SCOPE_DEPTH);
    Atom *deep_telescope = build_deep_binder(
        &inputs, "Pi", ABT_BENCH_TELESCOPE_DEPTH);
    assert(abt_scope_check(&signature, 0u, deep_scope));
    assert(abt_scope_check(&signature, 0u, deep_telescope));

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_subst(
            &signature, &scratch, 0u, replacement, canonical);
        subst_ns[i] = monotonic_ns() - start;
        assert(result && result != canonical);
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &subst_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_print(&signature, &scratch, printable);
        print_ns[i] = monotonic_ns() - start;
        assert(result);
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &print_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_parse(&signature, &scratch, syntax);
        parse_ns[i] = monotonic_ns() - start;
        assert(result && abt_alpha_eq(result, printable));
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &parse_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = atom_freshen_epoch(
            &scratch, metavariables, i + 1u);
        freshen_ns[i] = monotonic_ns() - start;
        assert(result && result != metavariables);
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &freshen_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_subst(
            &signature, &scratch, 0u, replacement, shared);
        shared_ns[i] = monotonic_ns() - start;
        assert(result && result != shared);
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &shared_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_close(
            &signature, &scratch, symbol_name, symbol_name_tree);
        symbol_close_ns[i] = monotonic_ns() - start;
        assert(result && result != symbol_name_tree);
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &symbol_close_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_close(
            &signature, &scratch, tagged_name, tagged_name_tree);
        tagged_close_ns[i] = monotonic_ns() - start;
        assert(result && result != tagged_name_tree);
        arena_reset(&scratch, mark);
        assert_scratch_reuse(&scratch, i, &tagged_close_reserved);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        uint64_t start = monotonic_ns();
        assert(abt_scope_check(&signature, 0u, deep_scope));
        deep_scope_ns[i] = monotonic_ns() - start;
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        uint64_t start = monotonic_ns();
        assert(abt_scope_check(&signature, 0u, deep_telescope));
        telescope_scope_ns[i] = monotonic_ns() - start;
    }

    qsort(subst_ns, ABT_BENCH_REPEATS, sizeof(subst_ns[0]), compare_u64);
    qsort(freshen_ns, ABT_BENCH_REPEATS, sizeof(freshen_ns[0]), compare_u64);
    qsort(shared_ns, ABT_BENCH_REPEATS, sizeof(shared_ns[0]), compare_u64);
    qsort(print_ns, ABT_BENCH_REPEATS, sizeof(print_ns[0]), compare_u64);
    qsort(parse_ns, ABT_BENCH_REPEATS, sizeof(parse_ns[0]), compare_u64);
    qsort(symbol_close_ns, ABT_BENCH_REPEATS,
          sizeof(symbol_close_ns[0]), compare_u64);
    qsort(tagged_close_ns, ABT_BENCH_REPEATS,
          sizeof(tagged_close_ns[0]), compare_u64);
    qsort(deep_scope_ns, ABT_BENCH_REPEATS,
          sizeof(deep_scope_ns[0]), compare_u64);
    qsort(telescope_scope_ns, ABT_BENCH_REPEATS,
          sizeof(telescope_scope_ns[0]), compare_u64);
    uint64_t workload_nodes =
        (UINT64_C(1) << (ABT_BENCH_TREE_DEPTH + 1u)) - 1u;
    puts("PASS: ABT balanced-tree performance smoke");
    printf("(ABTBenchSummary nodes=%" PRIu64
           " repeats=%u abt-subst-median-ns=%" PRIu64
           " epoch-freshen-median-ns=%" PRIu64
           " shared-dag-depth=%u abt-shared-dag-median-ns=%" PRIu64
           " abt-print-median-ns=%" PRIu64
           " abt-parse-median-ns=%" PRIu64
           " name-tree-depth=%u symbol-close-median-ns=%" PRIu64
           " tagged-close-median-ns=%" PRIu64
           " deep-scope-depth=%u deep-scope-median-ns=%" PRIu64
           " telescope-depth=%u telescope-scope-median-ns=%" PRIu64
           " scratch-reserved-bytes=%zu)\n",
           workload_nodes, ABT_BENCH_REPEATS,
           subst_ns[ABT_BENCH_REPEATS / 2u],
           freshen_ns[ABT_BENCH_REPEATS / 2u],
           ABT_BENCH_SHARED_DAG_DEPTH,
           shared_ns[ABT_BENCH_REPEATS / 2u],
           print_ns[ABT_BENCH_REPEATS / 2u],
           parse_ns[ABT_BENCH_REPEATS / 2u],
           ABT_BENCH_NAME_TREE_DEPTH,
           symbol_close_ns[ABT_BENCH_REPEATS / 2u],
           tagged_close_ns[ABT_BENCH_REPEATS / 2u],
           ABT_BENCH_SCOPE_DEPTH,
           deep_scope_ns[ABT_BENCH_REPEATS / 2u],
           ABT_BENCH_TELESCOPE_DEPTH,
           telescope_scope_ns[ABT_BENCH_REPEATS / 2u],
           scratch.reserved_bytes);

    abt_signature_free(&signature);
    arena_free(&scratch);
    arena_free(&inputs);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
