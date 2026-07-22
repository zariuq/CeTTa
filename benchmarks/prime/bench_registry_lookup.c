#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "atom.h"
#include "space.h"
#include "symbol.h"

#define LOOKUP_TRIALS 5u
#define LOOKUP_ITERATIONS 1000000u

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
           (uint64_t)ts.tv_nsec;
}

static int compare_double(const void *lhs, const void *rhs) {
    double a = *(const double *)lhs;
    double b = *(const double *)rhs;
    return (a > b) - (a < b);
}

static Atom *structural_key(Arena *arena, uint32_t slot) {
    return atom_expr2(
        arena, atom_symbol(arena, "agent"),
        atom_expr2(arena, atom_symbol(arena, "slot"),
                   atom_int(arena, (int64_t)slot)));
}

static int run_scale(Arena *arena, uint32_t entry_count) {
    Registry registry;
    Atom **names = cetta_malloc(sizeof(*names) * entry_count);
    Atom **values = cetta_malloc(sizeof(*values) * entry_count);
    SymbolId *symbols = cetta_malloc(sizeof(*symbols) * entry_count);
    registry_init(&registry);

    for (uint32_t i = 0u; i < entry_count; i++) {
        char spelling[64];
        snprintf(spelling, sizeof(spelling), "&bench-%" PRIu32, i);
        symbols[i] = symbol_intern_cstr(g_symbols, spelling);
        names[i] = structural_key(arena, i);
        values[i] = atom_int(arena, (int64_t)i);
        registry_bind_id(&registry, symbols[i], values[i]);
        if (!registry_bind_name(&registry, names[i], values[i])) {
            fprintf(stderr, "structural registry setup failed\n");
            return 1;
        }
    }

    for (uint32_t i = 0u; i < entry_count; i += entry_count / 16u) {
        if (registry_lookup_id(&registry, symbols[i]) != values[i] ||
            registry_lookup_name(&registry, names[i]) != values[i]) {
            fprintf(stderr, "registry correctness precondition failed\n");
            return 1;
        }
    }

    volatile uintptr_t sink = 0u;
    for (uint32_t warm = 0u; warm < entry_count * 4u; warm++) {
        uint32_t index = (warm * UINT32_C(2654435761)) & (entry_count - 1u);
        sink ^= (uintptr_t)registry_lookup_id(&registry, symbols[index]);
        sink ^= (uintptr_t)registry_lookup_name(&registry, names[index]);
    }

    double symbol_trials[LOOKUP_TRIALS];
    double structural_trials[LOOKUP_TRIALS];
    for (uint32_t trial = 0u; trial < LOOKUP_TRIALS; trial++) {
        uint64_t start = now_ns();
        for (uint32_t i = 0u; i < LOOKUP_ITERATIONS; i++) {
            uint32_t index =
                (i * UINT32_C(2654435761) + trial) & (entry_count - 1u);
            sink ^= (uintptr_t)registry_lookup_id(&registry, symbols[index]);
        }
        uint64_t symbol_elapsed = now_ns() - start;
        start = now_ns();
        for (uint32_t i = 0u; i < LOOKUP_ITERATIONS; i++) {
            uint32_t index =
                (i * UINT32_C(2654435761) + trial) & (entry_count - 1u);
            sink ^= (uintptr_t)registry_lookup_name(&registry, names[index]);
        }
        uint64_t structural_elapsed = now_ns() - start;
        symbol_trials[trial] =
            (double)symbol_elapsed / (double)LOOKUP_ITERATIONS;
        structural_trials[trial] =
            (double)structural_elapsed / (double)LOOKUP_ITERATIONS;
        printf("RegistryLookupTrial,%" PRIu32 ",%" PRIu32
               ",%.3f,%.3f\n",
               entry_count, trial + 1u, symbol_trials[trial],
               structural_trials[trial]);
    }
    qsort(symbol_trials, LOOKUP_TRIALS, sizeof(double), compare_double);
    qsort(structural_trials, LOOKUP_TRIALS, sizeof(double), compare_double);
    printf("RegistryLookupMedian,%" PRIu32 ",%.3f,%.3f,%.3f\n",
           entry_count, symbol_trials[LOOKUP_TRIALS / 2u],
           structural_trials[LOOKUP_TRIALS / 2u],
           structural_trials[LOOKUP_TRIALS / 2u] /
               symbol_trials[LOOKUP_TRIALS / 2u]);

    if (sink == UINTPTR_MAX)
        fprintf(stderr, "lookup sink: %" PRIuPTR "\n", sink);
    registry_free(&registry);
    free(symbols);
    free(values);
    free(names);
    return 0;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    const uint32_t scales[] = {64u, 1024u, 16384u};
    printf("RegistryLookupColumns,entries,trial,symbol-ns,structural-ns\n");
    for (size_t i = 0u; i < sizeof(scales) / sizeof(scales[0]); i++)
        if (run_scale(&arena, scales[i]) != 0)
            return 1;
    printf("(RegistryLookupBenchmarkSummary 3 5 1000000)\n");

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
