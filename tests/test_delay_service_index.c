/* Count comparisons in the actual index constructor. The fixed input ladder
 * detects batch-size scaling independently of elapsed time and machine load. */
#include "atom.h"
#include "symbol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t comparisons;
static bool watch_var_equal(VarId left, VarId right) {
    comparisons++;
    return var_id_eq(left, right);
}
#define var_id_eq watch_var_equal
#include "../src/delay_service.c"
#undef var_id_eq

static void colliding_history(Arena *arena) {
    enum { width = 12, count = 24 };
    VarId distinct[width], vars[count];
    uint32_t found = 0u;
    uint32_t bucket = delay_bucket(var_id_from_base(1u), 256u);
    for (uint32_t candidate = 1u; found < width && candidate < 100000u; candidate++) {
        VarId var = var_id_from_base(candidate);
        if (delay_bucket(var, 256u) == bucket) distinct[found++] = var;
    }
    assert(found == width);
    for (unsigned at = 0u; at < count; at++) vars[at] = distinct[at % width];
    CettaDelayService *service = cetta_delay_service_new();
    assert(service);
    for (unsigned goal = 0u; goal < 3u; goal++)
        assert(cetta_delay_suspend(service, goal + 1u, CETTA_DELAY_CLIENT_PROLOG,
            atom_int(arena, goal), vars, count, false));
    assert(service->bucket_cap <= 256u && service->linked == 3u * width);
    assert(cetta_delay_withdraw(service, 10u, distinct[3]));
    assert(!cetta_delay_watching(service));
    cetta_delay_rollback(service, 10u);
    assert(service->linked == 3u * width);
    assert(cetta_delay_bound(service, 11u, distinct[7]));
    CettaDelayService *copy = cetta_delay_service_clone(service);
    assert(copy);
    for (unsigned route = 0u; route < 2u; route++) {
        CettaDelayService *current = route ? copy : service;
        for (unsigned index = 0u; index < 3u; index++) {
            CettaDelayGoal goal;
            assert(cetta_delay_take_woken(current, 12u, &goal));
            assert(atom_eq(goal.goal, atom_int(arena, index)));
            assert(goal.var_len == count &&
                memcmp(goal.vars, vars, sizeof(vars)) == 0);
        }
        assert(!cetta_delay_has_woken(current) && !cetta_delay_watching(current));
    }
    cetta_delay_rollback(service, 11u);
    assert(service->linked == 3u * width && !cetta_delay_has_woken(service));
    assert(!cetta_delay_watching(copy));
    cetta_delay_service_free(copy);
    cetta_delay_service_free(service);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init(&arena);
    const uint32_t widths[] = {256u, 1024u, 4096u, 16384u};
    for (uint32_t repeats = 1u; repeats <= 3u; repeats += 2u) {
        uint64_t previous = 0u;
        for (unsigned size = 0u; size < sizeof(widths) / sizeof(widths[0]); size++) {
            uint32_t width = widths[size], count = width * repeats;
            VarId *vars = malloc(sizeof(*vars) * count);
            assert(vars);
            for (uint32_t at = 0u; at < count; at++)
                vars[at] = var_id_from_base(100000u + at % width);
            CettaDelayService *service = cetta_delay_service_new();
            assert(service);
            comparisons = 0u;
            assert(cetta_delay_suspend(service, 1u, CETTA_DELAY_CLIENT_PROLOG,
                atom_symbol(&arena, "goal"), vars, count, false));
            uint64_t work = comparisons;
            /* This fixed sequential-ID cohort is the complexity control.
             * Arbitrary colliding hashes have separate semantic controls. */
            assert(work <= 2u * count);
            if (previous) assert(work <= 6u * previous);
            previous = work;
            assert(service->bucket_cap >= (uint64_t)count * 2u);
            assert(service->bucket_cap < (uint64_t)count * 4u);
            assert(service->linked == width && service->goals[0].watch_count == width);
            assert(cetta_delay_bound(service, 2u, vars[width / 2u]));
            CettaDelayGoal goal;
            assert(cetta_delay_take_woken(service, 3u, &goal));
            assert(goal.var_len == count &&
                memcmp(goal.vars, vars, sizeof(*vars) * count) == 0);
            assert(!cetta_delay_has_woken(service) && !cetta_delay_watching(service));
            cetta_delay_rollback(service, 2u);
            assert(cetta_delay_watches(service, vars[width / 2u]));
            cetta_delay_service_free(service);
            free(vars);
        }
    }
    colliding_history(&arena);
    arena_free(&arena);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: watch-index ladder and colliding histories preserve exact goals, single wake and linear construction work");
    return 0;
}
