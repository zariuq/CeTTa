/* The delay service against the laws of ConstraintPropagation's goal table:
 * binding wakes every goal waiting on the variable, in the order suspended,
 * each once; an unwatched binding does nothing; a goal suspended on a bound
 * variable is queued at once; rollback to a mark restores everything after
 * it; withdrawal removes without waking; after the trail is compacted, a
 * rollback to a kept checkpoint undoes what it undid before; a variable
 * reaches the goals connected to it, in suspension order. */
#include "delay_service.h"

#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); \
        failures++; \
    } \
} while (0)

static Atom *goal_named(Arena *arena, const char *name) {
    return atom_symbol(arena, name);
}

static bool count_live(void *context, const CettaDelayGoal *goal) {
    (void)goal;
    (*(uint32_t *)context)++;
    return true;
}

static const char *take_name(CettaDelayService *service, uint32_t mark) {
    CettaDelayGoal goal;
    if (!cetta_delay_take_woken(service, mark, &goal))
        return NULL;
    return symbol_bytes(g_symbols, goal.goal->sym_id);
}

static void repeated_watches(Arena *arena) {
    const uint32_t widths[] = {1u, 3u, 65u, 257u, 1025u, 4097u};
    for (unsigned size = 0u; size < sizeof(widths) / sizeof(widths[0]); size++)
    for (uint32_t repeats = 1u; repeats <= 3u; repeats++)
    for (uint32_t copies = 1u; copies <= 3u; copies++)
    for (unsigned withdraw = 0u; withdraw < 2u; withdraw++) {
        uint32_t width = widths[size], count = width * repeats;
        VarId *vars = malloc(sizeof(*vars) * count);
        CHECK(vars != NULL, "allocate repeated watch input");
        if (!vars) return;
        for (uint32_t at = 0u; at < count; at++)
            vars[at] = var_id_from_base(10000u + at % width);
        CettaDelayService *service = cetta_delay_service_new();
        CHECK(service != NULL, "allocate repeated watch service");
        if (!service) { free(vars); return; }
        for (uint32_t copy = 0u; copy < copies; copy++)
            CHECK(cetta_delay_suspend(service, copy + 1u,
                CETTA_DELAY_CLIENT_PROLOG, goal_named(arena, "same-goal"),
                vars, count, false), "retain distinct equal goals");
        /* A clone rebuilds its index from the exact retained lists. */
        CettaDelayService *cloned = cetta_delay_service_clone(service);
        for (unsigned route = 0u; route < 2u; route++) {
            CettaDelayService *current = route ? cloned : service;
            CHECK(current != NULL, "clone repeated watch service");
            if (!current) continue;
            const CettaDelayGoal *goals[3];
            CHECK(cetta_delay_goals_on(current, vars[0], goals, 3u) == copies,
                "watch lookup returns each goal once, retaining equal occurrences");
            const CettaDelayGoal **reached = NULL;
            uint32_t reached_count = 0u;
            CHECK(cetta_delay_reach(current, vars, count, &reached, &reached_count)
                && reached_count == copies, "reach agrees with watch lookup");
            free(reached);
            if (withdraw) {
                CHECK(cetta_delay_withdraw(current, 100u, vars[0]),
                    "withdraw repeated watches once");
                CHECK(!cetta_delay_has_woken(current) && !cetta_delay_watching(current),
                    "withdrawal leaves no watch or counter residue");
                cetta_delay_rollback(current, 100u);
            }
            CHECK(cetta_delay_bound(current, 101u, vars[0]), "bind repeated watches");
            for (uint32_t copy = 0u; copy < copies; copy++) {
                CettaDelayGoal goal;
                bool taken = cetta_delay_take_woken(current, 102u, &goal);
                CHECK(taken && goal.var_len == count,
                    "queue preserves each distinct goal and exact variable list");
                if (taken) CHECK(memcmp(goal.vars, vars, sizeof(*vars) * count) == 0,
                    "retained variable occurrences remain exact");
            }
            CHECK(!cetta_delay_has_woken(current) && !cetta_delay_watching(current),
                "one wake per goal leaves no watch or counter residue");
            cetta_delay_rollback(current, 101u);
            CHECK(cetta_delay_goals_on(current, vars[width - 1u], goals, 3u) == copies,
                "rollback restores every unique watch");
        }
        cetta_delay_service_free(cloned);
        cetta_delay_service_free(service);
        free(vars);
    }
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init(&arena);
    CettaDelayService *service = cetta_delay_service_new();
    CHECK(service != NULL, "service allocates");
    CHECK(!cetta_delay_watching(service), "a new service watches nothing");

    const VarId x = var_id_from_base(11u), y = var_id_from_base(12u), z = var_id_from_base(13u);
    VarId xy[2] = {x, y};
    CHECK(cetta_delay_suspend(service, 1u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "first"), &x, 1u, false),
          "suspend first on x");
    CHECK(cetta_delay_suspend(service, 2u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "both"), xy, 2u, false),
          "suspend both on x and y");
    CHECK(cetta_delay_suspend(service, 3u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "second"), &x, 1u, false),
          "suspend second on x");
    CHECK(cetta_delay_watches(service, x) && cetta_delay_watches(service, y),
          "x and y are watched");
    CHECK(!cetta_delay_watches(service, z), "z is not watched");

    /* An unwatched binding changes nothing (table_bind_unwatched). */
    CHECK(cetta_delay_bound(service, 4u, z), "bind z");
    CHECK(!cetta_delay_has_woken(service), "binding z wakes nothing");

    /* Binding x wakes its three goals in suspension order
     * (table_bind_wakes); `both` leaves y's index too. */
    CHECK(cetta_delay_bound(service, 5u, x), "bind x");
    const char *a = take_name(service, 6u);
    const char *b = take_name(service, 6u);
    const char *c = take_name(service, 6u);
    CHECK(a && b && c && strcmp(a, "first") == 0 &&
              strcmp(b, "both") == 0 && strcmp(c, "second") == 0,
          "x's goals wake in the order suspended");
    CHECK(!cetta_delay_has_woken(service), "each goal is queued once");
    CHECK(!cetta_delay_watches(service, y), "a woken goal leaves y's index");
    CHECK(cetta_delay_bound(service, 7u, y), "bind y");
    CHECK(!cetta_delay_has_woken(service),
          "binding y does not wake `both` again (table_bind_wf)");

    /* Rollback to mark 5 undoes the wakes and takes. */
    cetta_delay_rollback(service, 5u);
    CHECK(!cetta_delay_has_woken(service) && cetta_delay_watches(service, x) &&
              cetta_delay_watches(service, y),
          "rollback to before the binding restores the index");
    /* Rollback to mark 2 removes `both` and `second`. */
    cetta_delay_rollback(service, 2u);
    CHECK(cetta_delay_watches(service, x) && !cetta_delay_watches(service, y),
          "rollback to 2 keeps only `first`");
    CHECK(cetta_delay_bound(service, 8u, x), "bind x again");
    a = take_name(service, 9u);
    CHECK(a && strcmp(a, "first") == 0 && !cetta_delay_has_woken(service),
          "only `first` wakes after the rollback");

    /* A goal suspended on a bound variable is queued at once
     * (table_suspend_bound). */
    CHECK(cetta_delay_suspend(service, 10u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "late"), &z, 1u, true),
          "suspend late on bound z");
    a = take_name(service, 11u);
    CHECK(a && strcmp(a, "late") == 0, "a goal on a bound variable is queued");

    /* Withdrawal removes without waking, and rolls back. */
    CHECK(cetta_delay_suspend(service, 12u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "kept"), &y, 1u, false),
          "suspend kept on y");
    CHECK(cetta_delay_withdraw(service, 13u, y), "withdraw y's goals");
    CHECK(!cetta_delay_watches(service, y) && !cetta_delay_has_woken(service),
          "withdrawal queues nothing and leaves the index");
    cetta_delay_rollback(service, 13u);
    CHECK(cetta_delay_watches(service, y), "rollback restores a withdrawal");

    /* A clone keeps the suspended goals and the queue. */
    CHECK(cetta_delay_bound(service, 14u, y), "bind y");
    CettaDelayService *copy = cetta_delay_service_clone(service);
    a = copy ? take_name(copy, 1u) : NULL;
    CHECK(a && strcmp(a, "kept") == 0, "the clone keeps the queue");
    cetta_delay_service_free(copy);

    cetta_delay_service_free(service);

    /* Rebase: the trail kept checkpoints 10 and 20 as positions 0 and 1; a
     * change at 5 predates both and stays, one at 12 belongs to 10, one at
     * 25 to 20 (rollback_rebase). */
    CettaDelayService *history = cetta_delay_service_new();
    const VarId p = var_id_from_base(21u), q = var_id_from_base(22u), r = var_id_from_base(23u);
    CHECK(cetta_delay_suspend(history, 5u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "old"), &p, 1u, false) &&
              cetta_delay_suspend(history, 12u, CETTA_DELAY_CLIENT_PROLOG,
                                  goal_named(&arena, "middle"), &q, 1u,
                                  false) &&
              cetta_delay_suspend(history, 25u, CETTA_DELAY_CLIENT_PROLOG,
                                  goal_named(&arena, "new"), &r, 1u, false),
          "suspend at 5, 12 and 25");
    const uint32_t kept[2] = {10u, 20u};
    cetta_delay_rebase(history, kept, 2u);
    cetta_delay_rollback(history, 1u);
    CHECK(cetta_delay_watches(history, p) && cetta_delay_watches(history, q) &&
              !cetta_delay_watches(history, r),
          "rolling back to the second kept checkpoint undoes only `new`");
    cetta_delay_rollback(history, 0u);
    CHECK(cetta_delay_watches(history, p) && !cetta_delay_watches(history, q),
          "rolling back to the first undoes `middle`, never `old`");

    /* Reach: goals connected to a variable through shared variables, in
     * suspension order; live goals include the woken, untaken ones. */
    VarId pq[2] = {p, q};
    CHECK(cetta_delay_suspend(history, 30u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "link"), pq, 2u, false) &&
              cetta_delay_suspend(history, 31u, CETTA_DELAY_CLIENT_PROLOG,
                                  goal_named(&arena, "apart"), &r, 1u, false),
          "suspend link on p q, apart on r");
    uint32_t reached_len = 0u;
    const CettaDelayGoal **reached = NULL;
    CHECK(cetta_delay_reach(history, &q, 1u, &reached, &reached_len) &&
              reached_len == 2u && reached &&
              strcmp(symbol_bytes(g_symbols, reached[0]->goal->sym_id),
                     "old") == 0 &&
              strcmp(symbol_bytes(g_symbols, reached[1]->goal->sym_id),
                     "link") == 0,
          "q reaches link and, through p, old, in suspension order");
    free(reached);
    CHECK(cetta_delay_bound(history, 32u, r), "bind r");
    uint32_t live = 0u;
    CHECK(cetta_delay_visit_live(history, count_live, &live) && live == 3u,
          "live goals: old and link suspended, apart woken");
    cetta_delay_service_free(history);

    /* Only waiting goals are indexed; a rollback across a rehash puts a
     * woken goal back in its place. */
    CettaDelayService *indexed = cetta_delay_service_new();
    const VarId av = var_id_from_base(41u);
    CHECK(cetta_delay_suspend(indexed, 1u, CETTA_DELAY_CLIENT_PROLOG,
                              goal_named(&arena, "a1"), &av, 1u, false) &&
              cetta_delay_suspend(indexed, 2u, CETTA_DELAY_CLIENT_PROLOG,
                                  goal_named(&arena, "a2"), &av, 1u, false) &&
              cetta_delay_bound(indexed, 3u, av),
          "suspend a1 and a2 on a, bind a");
    const CettaDelayGoal *on_a[4];
    CHECK(cetta_delay_goals_on(indexed, av, on_a, 4u) == 0u,
          "woken goals leave the index");
    bool filled = true;
    for (uint32_t i = 0u; i < 100u; i++) {
        VarId filler = var_id_from_base(100u + i);
        filled = filled &&
            cetta_delay_suspend(indexed, 4u + i, CETTA_DELAY_CLIENT_PROLOG,
                                goal_named(&arena, "filler"), &filler, 1u,
                                false);
    }
    CHECK(filled, "a hundred more suspensions grow the index");
    cetta_delay_rollback(indexed, 3u);
    uint32_t back = cetta_delay_goals_on(indexed, av, on_a, 4u);
    CHECK(back == 2u &&
              strcmp(symbol_bytes(g_symbols, on_a[0]->goal->sym_id), "a2") == 0 &&
              strcmp(symbol_bytes(g_symbols, on_a[1]->goal->sym_id), "a1") == 0,
          "after the rollback both wait on a again, newest first");
    CHECK(cetta_delay_bound(indexed, 4u, av), "bind a again");
    {
        const char *first = take_name(indexed, 5u);
        const char *second = take_name(indexed, 5u);
        CHECK(first && second && strcmp(first, "a1") == 0 &&
                  strcmp(second, "a2") == 0,
              "and wake in the order they were suspended");
    }
    for (uint32_t i = 0u; i < 1000u; i++) {
        VarId churn = var_id_from_base(7u);
        cetta_delay_suspend(indexed, 10u + 2u * i, CETTA_DELAY_CLIENT_PROLOG,
                            goal_named(&arena, "churn"), &churn, 1u, false);
        cetta_delay_withdraw(indexed, 11u + 2u * i, churn);
    }
    const VarId churn_var = var_id_from_base(7u);
    CHECK(cetta_delay_goals_on(indexed, churn_var, on_a, 4u) == 0u &&
              !cetta_delay_watches(indexed, churn_var),
          "a thousand withdrawn goals leave nothing indexed");
    cetta_delay_service_free(indexed);

    repeated_watches(&arena);
    arena_free(&arena);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("PASS: the delay service wakes each waiting goal once, in order, "
           "and rolls back to any mark, compacted or not\n");
    return 0;
}
