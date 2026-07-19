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
    return atom_expr2(arena, atom_symbol(arena, "Var"), atom_int(arena, 0));
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
    Atom *surface = abt_print(&signature, &inputs, printable);
    assert(surface);

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_subst(
            &signature, &scratch, 0u, replacement, canonical);
        subst_ns[i] = monotonic_ns() - start;
        assert(result && result != canonical);
        arena_reset(&scratch, mark);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_print(&signature, &scratch, printable);
        print_ns[i] = monotonic_ns() - start;
        assert(result);
        arena_reset(&scratch, mark);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_parse(&signature, &scratch, surface);
        parse_ns[i] = monotonic_ns() - start;
        assert(result && abt_alpha_eq(result, printable));
        arena_reset(&scratch, mark);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = atom_freshen_epoch(
            &scratch, metavariables, i + 1u);
        freshen_ns[i] = monotonic_ns() - start;
        assert(result && result != metavariables);
        arena_reset(&scratch, mark);
    }

    for (uint32_t i = 0; i < ABT_BENCH_REPEATS; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint64_t start = monotonic_ns();
        Atom *result = abt_subst(
            &signature, &scratch, 0u, replacement, shared);
        shared_ns[i] = monotonic_ns() - start;
        assert(result && result != shared);
        arena_reset(&scratch, mark);
    }

    qsort(subst_ns, ABT_BENCH_REPEATS, sizeof(subst_ns[0]), compare_u64);
    qsort(freshen_ns, ABT_BENCH_REPEATS, sizeof(freshen_ns[0]), compare_u64);
    qsort(shared_ns, ABT_BENCH_REPEATS, sizeof(shared_ns[0]), compare_u64);
    qsort(print_ns, ABT_BENCH_REPEATS, sizeof(print_ns[0]), compare_u64);
    qsort(parse_ns, ABT_BENCH_REPEATS, sizeof(parse_ns[0]), compare_u64);
    uint64_t workload_nodes =
        (UINT64_C(1) << (ABT_BENCH_TREE_DEPTH + 1u)) - 1u;
    puts("PASS: ABT balanced-tree performance smoke");
    printf("(ABTBenchSummary nodes=%" PRIu64
           " repeats=%u abt-subst-median-ns=%" PRIu64
           " epoch-freshen-median-ns=%" PRIu64
           " shared-dag-depth=%u abt-shared-dag-median-ns=%" PRIu64
           " abt-print-median-ns=%" PRIu64
           " abt-parse-median-ns=%" PRIu64 ")\n",
           workload_nodes, ABT_BENCH_REPEATS,
           subst_ns[ABT_BENCH_REPEATS / 2u],
           freshen_ns[ABT_BENCH_REPEATS / 2u],
           ABT_BENCH_SHARED_DAG_DEPTH,
           shared_ns[ABT_BENCH_REPEATS / 2u],
           print_ns[ABT_BENCH_REPEATS / 2u],
           parse_ns[ABT_BENCH_REPEATS / 2u]);

    abt_signature_free(&signature);
    arena_free(&scratch);
    arena_free(&inputs);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
