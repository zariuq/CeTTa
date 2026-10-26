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
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
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

/* Pending delivery is an independent root. Its logical world is raised to
 * the episode before both stack and queued collection; its visible payload
 * then moves even though no child/target row points to that payload. */
static void pending_force_gc_case(bool queued) {
    Arena arena, owner; arena_init(&arena); arena_init(&owner);
    arena_set_hashcons(&arena, NULL); arena_set_hashcons(&owner, NULL);
    Space space; space_init(&space);
    PrimeNativeControl control = {0};
    outcome_set_init(&control.output);
    PrimeEvalStackDriver *driver = &control.driver;
    driver->arena = &arena; driver->root_target = &control.output;
    driver->gc_anchor = arena_mark(&arena);
    if (queued) {driver->control = &control; control.collects = true;}
    g_prime_eval_stack_driver = driver;
    g_prime_need_owner = g_prime_need_episode_owner = &owner;
    prime_need_snapshot_init(&g_prime_need_active);
    prime_need_branch_state_init(&g_prime_need_branch_state_active);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
#endif
    PrimeEvalStackFrame *frame = prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_FORCE);
    frame->space = &space; frame->arena = &arena; frame->heap_owner = &owner;
    frame->target = &control.output; frame->fuel = -1;
    bindings_init(&frame->env); frame->env_initialized = true;
    outcome_set_init(&frame->child); frame->child_initialized = true;
    outcome_set_init(&frame->force_pending); frame->force_pending_initialized = true;
    PrimeNeedSnapshot root = {0}, cached;
    assert(prime_need_snapshot_begin(&root));
    assert(prime_need_snapshot_allocate(&owner, &root, atom_symbol(&owner, "pending-gc-source"),
        &frame->before, &frame->thunk_id));
    assert(prime_need_snapshot_lookup(&frame->before, frame->thunk_id, &frame->cell));
    frame->evaluator_id = prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(&owner, &frame->before, frame->thunk_id,
        frame->evaluator_id, &frame->evaluating));
    *bindings_need_mut(&frame->env) = frame->evaluating;
    outcome_set_add(&frame->child, atom_int(&arena, 0), &frame->env);
    Atom *leaf = atom_int(&arena, 7);
    Atom *payload = atom_expr3(&arena, atom_symbol(&arena, "Pair"), leaf, leaf);
    assert(prime_need_snapshot_resolve_ready_value(&owner, &frame->evaluating,
        frame->thunk_id, frame->evaluator_id, atom_deep_copy(&owner, payload), &cached));
    Bindings pending; assert(bindings_clone(&pending, &frame->env));
    *bindings_need_mut(&pending) = cached;
    StateCell *state = arena_alloc(&owner, sizeof(*state)); assert(state);
    *state = (StateCell){.value=atom_int(&owner,0)};
    PrimeNeedBranchState branch = {0};
    assert(prime_need_branch_state_begin(&arena, &branch));
    assert(prime_need_branch_state_write(&arena, &branch, state, state->value,
        atom_int(&arena, 9), bindings_branch_state_mut(&pending)));
    CettaWeightLedger factor = {0};
    assert(cetta_weight_ledger_append(&arena, &factor, prime_need_fresh_source_occurrence(),
        prime_need_fresh_source_occurrence(), atom_int(&arena, 3), &factor));
    *bindings_weight_mut(&pending) = factor;
    assert(bindings_refresh_occurrence_token(&pending));
    uint64_t occurrence = bindings_occurrence_token(&pending);
    outcome_set_add(&frame->force_pending, payload, &pending); bindings_free(&pending);
    frame->force_pending_observed = frame->force_retry_pending = frame->force_published = true;
    if (queued) control.frames = frame;
    else {driver->top = frame; driver->frame_depth = 1u;}
    size_t collections = g_eval_gc.collections;
    eval_gc_external_owner_enter();
    assert(queued ? prime_native_control_collect(&control) : prime_eval_stack_collect(driver));
    eval_gc_external_owner_leave();
    assert(g_eval_gc.collections == collections + 1u);
    Outcome *row = &frame->force_pending.items[0];
    assert(row->atom != payload && row->atom->kind == ATOM_EXPR && row->atom->expr.len == 3u &&
        row->atom->expr.elems[1] == row->atom->expr.elems[2] &&
        row->atom->expr.elems[1]->ground.ival == 7);
    assert(bindings_occurrence_token(&row->env) == occurrence &&
        !arena_owns_ptr(&arena, bindings_branch_state_view(&row->env)->top) &&
        !arena_owns_ptr(&arena, bindings_weight_view(&row->env)->last));
    Atom *read = NULL;
    assert(prime_need_branch_state_value(bindings_branch_state_view(&row->env), state, &read) &&
        read->ground.ival == 9);
    PrimeNeedCellView kept;
    assert(prime_need_snapshot_lookup(bindings_need_view(&row->env), frame->thunk_id, &kept) &&
        kept.cache_state == PRIME_NEED_CACHE_VALUE);
    prime_eval_stack_publish_force_plain(frame);
    assert(control.output.len == 1u && frame->index == 1u && !frame->force_pending.len &&
        !frame->force_retry_pending && bindings_occurrence_token(&control.output.items[0].env) == occurrence);
    driver->top = NULL; control.frames = NULL;
    prime_eval_stack_frame_free(frame); outcome_set_free(&control.output);
    g_prime_eval_stack_driver = NULL; g_prime_need_owner = g_prime_need_episode_owner = NULL;
    space_free(&space); arena_free(&arena); arena_free(&owner); eval_gc_survivor_reset();
    printf("PendingForceGC queued %u collected 1 delivered 1\n", queued);
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
    pending_force_gc_case(false);
    pending_force_gc_case(true);
    eval_set_library_context(NULL);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: tail collection retains unsafe owners and preserves rooted aliases");
    return 0;
}
