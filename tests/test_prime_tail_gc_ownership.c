/* Exercise the real tail-dispatch collector boundary with explicit ownership.
 * The unit replaces eval.o; it does not copy the collector or dispatch code. */
#include "../src/eval.c"
#include <assert.h>

static void gc_guard_case(unsigned mode) {
    Arena arena, owner;
    arena_init(&arena);
    arena_init(&owner);
    arena_set_hashcons(&arena, NULL);
    arena_set_hashcons(&owner, NULL);
    Space space;
    space_init(&space);
    Bindings env;
    bindings_init(&env);
    OutcomeSet results;
    outcome_set_init(&results);
    PrimeEvalStackDriver driver = {0};
    driver.arena = &arena;
    driver.gc_anchor = arena_mark(&arena);
    driver.root_target = &results;
    driver.root_call_running = true;
    driver.collection_admitted = mode != 3u;
    g_prime_eval_stack_driver = &driver;
    g_prime_need_owner = &owner;
    g_prime_need_episode_owner = &owner;
    prime_need_snapshot_init(&g_prime_need_active);
    prime_need_branch_state_init(&g_prime_need_branch_state_active);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_receipt_init(&g_prime_need_receipt_active);
#endif
    assert(arena_alloc(&arena, 4096u));
    Atom *leaf = atom_int(&arena, 7);
    Atom *held = atom_expr3(&arena, atom_symbol(&arena, "Pair"), leaf, leaf);
    Atom *type = NULL;
    Atom *before = held;
    EvalGcRootFrame root = {0};
    eval_gc_root_frame_enter(&root, &held, &env, &type);
    if (mode != 0u) eval_gc_root_frame_set_precise(&root, true);
    eval_gc_external_owner_enter();
    if (mode == 1u) eval_gc_external_owner_enter();
    if (mode == 2u) outcome_set_add(&results, atom_int(&arena, 42), &env);
    size_t old_collections = g_eval_gc.collections;
    size_t old_reclaimed = g_eval_gc.reclaimed_bytes;
    cetta_runtime_stats_reset();
    metta_call_impl(&space, &arena, atom_int(&arena, 42), NULL,
                   -1, true, &env, -1, &results);
    assert(!eval_unwinding());
    assert(results.len == (mode == 2u ? 2u : 1u));
    for (CettaCount i = 0u; i < results.len; i++) {
        Atom *value = outcome_atom_materialize(&arena, &results.items[i]);
        assert(value && value->kind == ATOM_GROUNDED &&
               value->ground.gkind == GV_INT && value->ground.ival == 42);
    }
    assert(held && held->kind == ATOM_EXPR && held->expr.len == 3u &&
           held->expr.elems[1] == held->expr.elems[2] &&
           held->expr.elems[1]->ground.ival == 7);
    if (mode == 4u) {
        assert(g_eval_gc.collections == old_collections + 1u);
        assert(g_eval_gc.reclaimed_bytes > old_reclaimed);
        assert(held != before);
    } else {
        assert(g_eval_gc.collections == old_collections);
        assert(g_eval_gc.reclaimed_bytes == old_reclaimed);
        assert(held == before);
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_COLLECTION_CANDIDATE] == 1u);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_IMPRECISE_ROOT] ==
           (mode == 0u || mode == 3u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_EXTERNAL_OWNER] == (mode == 1u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_LIVE_OUTCOME] == (mode == 2u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_COLLECTED] == (mode == 4u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_PRIME_EVAL_STACK_GC_FRAME_SAFE_POINT] == (mode == 4u));
#endif
    printf("GCGuard %u answers %zu collected %u alias 1\n", mode,
           (size_t)results.len, mode == 4u);
    if (mode == 1u) eval_gc_external_owner_leave();
    eval_gc_external_owner_leave();
    eval_gc_root_frame_leave(&root);
    assert(!g_eval_gc.roots && !g_eval_gc.imprecise_lexical_roots &&
           !g_eval_gc.external_owner_depth && !g_eval_gc.region_depth);
    assert(!driver.top && !driver.task_ready && !driver.raw_call_depth);
    outcome_set_free(&results);
    bindings_free(&env);
    g_prime_eval_stack_driver = NULL;
    g_prime_need_owner = NULL;
    g_prime_need_episode_owner = NULL;
    space_free(&space);
    arena_free(&arena);
    arena_free(&owner);
    eval_gc_survivor_reset();
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    CettaLibraryContext context = {0};
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    cetta_eval_session_init(&context.session, CETTA_LANGUAGE_PRIME,
                           cetta_profile_prime_default());
    eval_set_default_fuel(-1);
    eval_set_library_context(&context);
    eval_gc_init_once();
    g_eval_gc.enabled = true;
    g_eval_gc.budget_bytes = 1024u;
    cetta_runtime_stats_enable();
    for (unsigned mode = 0u; mode < 5u; mode++) gc_guard_case(mode);
    eval_set_library_context(NULL);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: tail collection retains unsafe owners and preserves rooted aliases");
    return 0;
}
