/* Included only by a CETTA_TEST_HOOKS evaluator build. Exercise the actual
 * transaction callbacks with native StateCell values, whose opaque resource
 * kind is distinct from PeTTa's named-state data wrapper. */
bool cetta_test_transaction_state_views(Space *root, Arena *arena,
                                         int64_t observed[8]) {
    if (!root || !arena || !observed || g_petta_worker_named_state ||
        g_petta_named_state_authority)
        return false;
    Registry registry;
    registry_init(&registry);
    Registry *previous = g_registry;
    g_registry = &registry;
    registry_bind_id(&registry, g_builtin_syms.self, atom_space(arena, root));
    PettaEvalMachineContext context = {.fuel = -1};
    Bindings empty;
    bindings_init(&empty);
    bool ok = false;
    StateCell original = {.value = atom_int(arena, 17),
        .content_type = atom_undefined_type(arena)};
    StateCell fresh = {.value = atom_int(arena, 31),
        .content_type = atom_undefined_type(arena)};
    void *outer = NULL, *child = NULL;
    Space *outer_space = NULL, *child_space = NULL;

#define TRANSACTION_STATE_REQUIRE(condition) do { if (!(condition)) goto finish; } while (0)
    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_begin(
        &context, root, arena, &outer, &outer_space));
    StateCell *parent_view = petta_eval_transaction_clone_state(outer, &original);
    TRANSACTION_STATE_REQUIRE(parent_view);
    parent_view->value = atom_int(arena, 18);
    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_begin(
        &context, outer_space, arena, &child, &child_space));
    StateCell *child_view = petta_eval_transaction_clone_state(child, &original);
    TRANSACTION_STATE_REQUIRE(child_view && child_view->value->ground.gkind == GV_INT);
    observed[0] = child_view->value->ground.ival;
    child_view->value = atom_int(arena, 19);
    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_commit(&context, child));
    observed[1] = original.value->ground.ival;
    parent_view = petta_eval_transaction_clone_state(outer, &original);
    TRANSACTION_STATE_REQUIRE(parent_view);
    observed[2] = parent_view->value->ground.ival;
    petta_eval_machine_transaction_rollback(&context, outer);
    observed[3] = original.value->ground.ival;

    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_begin(
        &context, root, arena, &outer, &outer_space));
    parent_view = petta_eval_transaction_clone_state(outer, &original);
    TRANSACTION_STATE_REQUIRE(parent_view);
    parent_view->value = atom_int(arena, 23);
    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_commit(&context, outer));
    observed[4] = original.value->ground.ival;

    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_begin(
        &context, root, arena, &outer, &outer_space));
    parent_view = petta_eval_transaction_clone_state(outer, &fresh);
    TRANSACTION_STATE_REQUIRE(parent_view);
    parent_view->value = atom_int(arena, 32);
    Atom *name = atom_symbol(arena, "native-cell-outside-owner");
    OutcomeSet writes;
    outcome_set_init(&writes);
    CettaCallOutcome end;
    bool published = petta_eval_machine_named_state(
        &context, outer_space, arena, PETTA_FORM_CHANGE_STATE, name,
        atom_state(arena, parent_view), &empty, &writes, &end);
    bool published_once = published && writes.len == 1u;
    outcome_set_free(&writes);
    TRANSACTION_STATE_REQUIRE(published_once);
    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_begin(
        &context, outer_space, arena, &child, &child_space));
    child_view = petta_eval_transaction_clone_state(child, parent_view);
    TRANSACTION_STATE_REQUIRE(child_view);
    observed[5] = child_view->value->ground.ival;
    child_view->value = atom_int(arena, 33);
    TRANSACTION_STATE_REQUIRE(petta_eval_machine_transaction_commit(&context, child));
    petta_eval_machine_transaction_rollback(&context, outer);
    Atom *saved = registry_state_lookup(&registry, name->sym_id);
    observed[6] = saved && saved->kind == ATOM_GROUNDED &&
        saved->ground.gkind == GV_STATE && saved->ground.ptr == &fresh;
    observed[7] = fresh.value->ground.ival;
    ok = true;

finish:
    while (context.transaction)
        petta_eval_machine_transaction_rollback(&context, context.transaction);
    bindings_free(&empty);
    registry_free(&registry);
    g_registry = previous;
    return ok;
#undef TRANSACTION_STATE_REQUIRE
}

static size_t cetta_test_context_provider_count(const TermUniverse *universe,
                                                const Space *space) {
    size_t count = 0u;
    for (size_t i = 0u; i < universe->root_provider_len; i++)
        count += universe->root_providers[i].resource_owner == space;
    return count;
}

static Space *cetta_test_new_managed_space(Space *root, Arena *arena) {
    ResultSet result;
    result_set_init(&result);
    Atom *request[] = {atom_symbol_id(arena, g_builtin_syms.new_space)};
    eval_top(root, arena, atom_expr(arena, request, 1u), &result);
    assert(result.len == 1u && result.items[0]->kind == ATOM_GROUNDED &&
           result.items[0]->ground.gkind == GV_SPACE);
    Space *space = result.items[0]->ground.ptr;
    assert(space && space->arena_hold &&
           space->native.universe == (g_eval_term_universe_override
               ? g_eval_term_universe_override : root->native.universe));
    result_set_free(&result);
    return space;
}

static void cetta_test_capture_answers(Space *space, Arena *arena,
                                        Atom *capture, Atom *query,
                                        const int64_t *expected, size_t count) {
    Bindings empty;
    bindings_init(&empty);
    OutcomeSet answers;
    outcome_set_init(&answers);
    dispatch_capture_outcomes(space, arena, capture, &query, 1u, -1,
        &empty, false, &answers);
    assert(answers.len == count);
    bool seen[3] = {false, false, false};
    assert(count <= sizeof(seen) / sizeof(seen[0]));
    for (CettaCount i = 0u; i < answers.len; i++) {
        Atom *value = outcome_atom_materialize(arena, &answers.items[i]);
        assert(value && value->kind == ATOM_GROUNDED &&
               value->ground.gkind == GV_INT);
        size_t index = 0u;
        while (index < count && (seen[index] ||
               value->ground.ival != expected[index]))
            index++;
        assert(index < count);
        seen[index] = true;
    }
    outcome_set_free(&answers);
    bindings_free(&empty);
}

static void cetta_test_capture_cycles(Space *root, Arena *persistent) {
    TermUniverse *universe = root->native.universe;
    TermUniverseCollectionMode previous_mode = universe->collection_mode;
    term_universe_set_collection_mode(universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(term_universe_collect(universe, NULL));
    assert(universe->counting);
    const size_t baseline = universe->root_provider_len;
    for (size_t count = 1u; count <= 2u; count++) {
        Arena source, kept, observation;
        arena_init_detached(&source);
        arena_init_detached(&kept);
        arena_init_detached(&observation);
        Space *spaces[2] = {NULL, NULL};
        Atom *captures[2] = {NULL, NULL};
        Space *previous_root = g_eval_root_space;
        for (size_t i = 0u; i < count; i++) {
            spaces[i] = cetta_test_new_managed_space(root, &source);
            g_eval_root_space = spaces[i];
            captures[i] = materialize_runtime_token(spaces[i], &source,
                atom_symbol_id(&source, g_builtin_syms.capture));
            assert(captures[i] && captures[i]->ground.gkind == GV_CAPTURE &&
                   ((CaptureClosure *)captures[i]->ground.ptr)->space_ptr == spaces[i]);
        }
        g_eval_root_space = previous_root;
        for (size_t i = 0u; i < count; i++)
            assert(space_admit_atom(spaces[i], persistent, captures[(i + 1u) % count]));
        Atom *external = atom_deep_copy(&kept, captures[0]);
        assert(external && external->ground.ptr == captures[0]->ground.ptr);
        arena_free(&source);
        assert(space_reclaim_unheld());
        space_reclaim_resource_cycles(universe);
        /* The external capture roots its context and all contexts reachable
         * through the stored capture cycle. */
        for (size_t i = 0u; i < count; i++) {
            assert(cetta_test_context_provider_count(universe, spaces[i]) == 2u);
            assert(space_length64(spaces[i]) == 1u);
            Atom *stored = space_view_at64(spaces[i], 0u, &observation);
            assert(stored && stored->kind == ATOM_GROUNDED &&
                   stored->ground.gkind == GV_CAPTURE);
            CaptureClosure *closure = stored->ground.ptr;
            assert(closure && closure->space_ptr == spaces[(i + 1u) % count]);
        }
        arena_free(&kept);
        assert(space_reclaim_unheld());
        space_reclaim_resource_cycles(universe);
        /* The scoped image observer is independently live after the last
         * explicit capture wrapper goes away. */
        for (size_t i = 0u; i < count; i++)
            assert(cetta_test_context_provider_count(universe, spaces[i]) == 2u);
        arena_free(&observation);
        assert(space_reclaim_unheld());
        space_reclaim_resource_cycles(universe);
        for (size_t i = 0u; i < count; i++)
            assert(cetta_test_context_provider_count(universe, spaces[i]) == 0u);
        assert(term_universe_collect(universe, NULL));
        assert(space_reclaim_unheld());
        assert(universe->root_provider_len == baseline);
    }
    term_universe_set_collection_mode(universe, previous_mode);
}

bool cetta_test_transaction_capture_context(Space *root, Arena *persistent) {
    assert(root && persistent && !g_petta_named_state_authority);
    TermUniverse *universe = root->native.universe;
    const size_t baseline = universe->root_provider_len;
    Arena source, saved, work;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&work);
    Space *canonical = cetta_test_new_managed_space(root, &source);
    Atom *probe = atom_symbol(persistent, "native-capture-context-probe");
    Atom *query = atom_expr(persistent, &probe, 1u);
    Atom *rule17 = atom_expr3(persistent,
        atom_symbol_id(persistent, g_builtin_syms.equals), query,
        atom_int(persistent, 17));
    Atom *rule19 = atom_expr3(persistent,
        atom_symbol_id(persistent, g_builtin_syms.equals), query,
        atom_int(persistent, 19));
    Atom *rule23 = atom_expr3(persistent,
        atom_symbol_id(persistent, g_builtin_syms.equals), query,
        atom_int(persistent, 23));
    assert(space_admit_atom(canonical, persistent, rule17));
    Registry registry;
    registry_init(&registry);
    registry_bind_id(&registry, g_builtin_syms.self, atom_space(&source, canonical));
    Registry *previous_registry = g_registry;
    Space *previous_root = g_eval_root_space;
    g_registry = &registry;
    g_eval_root_space = canonical;
    PettaEvalMachineContext context = {.fuel = -1};
    void *outer = NULL, *child = NULL;
    Space *outer_space = NULL, *child_space = NULL;
    assert(petta_eval_machine_transaction_begin(&context, canonical, &work,
        &outer, &outer_space));
    assert(space_admit_atom(outer_space, persistent, rule19));
    PettaNamedStateAuthorityScope outer_scope = {0};
    petta_named_state_authority_scope_enter(&outer_scope, &registry,
        petta_named_state_transaction_transport,
        petta_named_space_transaction_publish, outer);
    Atom *capture = materialize_runtime_token(outer_space, &source,
        atom_symbol_id(&source, g_builtin_syms.capture));
    assert(capture && capture->ground.gkind == GV_CAPTURE);
    CaptureClosure *closure = capture->ground.ptr;
    assert(closure && closure->space_ptr == canonical);
    assert(arena_owns_ptr(persistent, closure) && arena_owns_ptr(&source, capture));
    Atom *kept = atom_deep_copy(&saved, capture);
    assert(kept && kept->ground.ptr == closure);
    Atom *imported = petta_named_state_transaction_transport(
        outer, &work, kept, true);
    Atom *exported = petta_named_state_transaction_transport(
        outer, &saved, imported, false);
    assert(imported && exported && imported->ground.ptr == closure &&
           exported->ground.ptr == closure);
    const int64_t outer_expected[] = {17, 19};
    cetta_test_capture_answers(outer_space, &work, kept, query,
        outer_expected, 2u);
    /* Inline workers change named-state authority, while the physical
     * transaction view of this already captured resource stays active. */
    Registry worker_names;
    registry_init(&worker_names);
    Registry *previous_worker = g_petta_worker_named_state;
    g_petta_worker_named_state = &worker_names;
    assert(!petta_named_state_current_authority());
    cetta_test_capture_answers(outer_space, &work, kept, query,
        outer_expected, 2u);
    g_petta_worker_named_state = previous_worker;
    registry_free(&worker_names);

    assert(petta_eval_machine_transaction_begin(&context, outer_space, &work,
        &child, &child_space));
    assert(space_admit_atom(child_space, persistent, rule23));
    PettaNamedStateAuthorityScope child_scope = {0};
    petta_named_state_authority_scope_enter(&child_scope, &registry,
        petta_named_state_transaction_transport,
        petta_named_space_transaction_publish, child);
    Atom *nested_capture = materialize_runtime_token(child_space, &source,
        atom_symbol_id(&source, g_builtin_syms.capture));
    assert(nested_capture &&
           ((CaptureClosure *)nested_capture->ground.ptr)->space_ptr == canonical);
    const int64_t child_expected[] = {17, 19, 23};
    cetta_test_capture_answers(child_space, &work, kept, query,
        child_expected, 3u);
    petta_named_state_authority_scope_leave(&child_scope);
    assert(petta_eval_machine_transaction_commit(&context, child));
    cetta_test_capture_answers(outer_space, &work, kept, query,
        child_expected, 3u);
    petta_named_state_authority_scope_leave(&outer_scope);
    petta_eval_machine_transaction_rollback(&context, outer);
    const int64_t canonical_expected[] = {17};
    cetta_test_capture_answers(canonical, &work, kept, query,
        canonical_expected, 1u);
    assert(kept->ground.ptr == closure && closure->space_ptr == canonical);
    registry_free(&registry);
    g_registry = previous_registry;
    g_eval_root_space = previous_root;
    arena_free(&work);
    arena_free(&source);
    assert(space_reclaim_unheld());
    assert(cetta_test_context_provider_count(universe, canonical) == 2u);
    arena_free(&saved);
    assert(space_reclaim_unheld());
    assert(cetta_test_context_provider_count(universe, canonical) == 0u);
    assert(universe->root_provider_len == baseline);
    cetta_test_capture_cycles(root, persistent);
    return true;
}

typedef struct {
    Space *root;
    Arena *persistent;
    Atom *fact[3];
    Atom *pattern;
    Space *kept;
    pthread_barrier_t barrier;
    size_t baseline;
    CettaNameContext names;
} CettaTestWorkerResourceLeases;

static void cetta_test_resource_barrier(CettaTestWorkerResourceLeases *test) {
    int result = pthread_barrier_wait(&test->barrier);
    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void *cetta_test_resource_owner_thread(void *argument) {
    CettaTestWorkerResourceLeases *test = argument;
    CettaNameContext previous = cetta_name_context_current();
    cetta_name_context_install(&test->names);
    hyperpose_worker_enter(NULL, NULL);
    Arena source;
    arena_init_detached(&source);
    {
        CETTA_SCOPED_SHARED_TRANSITION(make_resources);
        test->kept = cetta_test_new_managed_space(test->root, &source);
        (void)cetta_test_new_managed_space(test->root, &source);
        test->kept->kind = SPACE_KIND_STACK;
        for (size_t i = 0u; i < 3u; i++)
            assert(space_admit_atom(test->kept, test->persistent, test->fact[i]));
        assert(test->root->native.universe->root_provider_len == test->baseline + 4u);
    }
    cetta_test_resource_barrier(test);
    cetta_test_resource_barrier(test);
    arena_free(&source);
    assert(space_reclaim_unheld());
    assert(test->root->native.universe->root_provider_len == test->baseline + 2u);
    assert(cetta_test_context_provider_count(
        test->root->native.universe, test->kept) == 2u);
    cetta_test_resource_barrier(test);
    cetta_test_resource_barrier(test);
    assert(space_reclaim_unheld());
    assert(test->root->native.universe->root_provider_len == test->baseline);
    cetta_test_resource_barrier(test);

    arena_init_detached(&source);
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    registry_init(&branch.registry);
    Space *adopted;
    {
        CETTA_SCOPED_SHARED_TRANSITION(make_adopted);
        void *previous_scope = arena_set_resource_scope(branch.storage);
        adopted = cetta_test_new_managed_space(test->root, &source);
        assert(hyperpose_adopt_tracked_spaces(&branch));
        arena_set_resource_scope(previous_scope);
        assert(branch.storage->made_spaces.len == 1u && g_made_spaces.len == 0u &&
               adopted->arena_hold->home == NULL);
    }
    arena_free(&source);
    assert(space_reclaim_unheld());
    assert(cetta_test_context_provider_count(
        test->root->native.universe, adopted) == 2u);
    {
        CETTA_SCOPED_SHARED_TRANSITION(free_adopted);
        hyperpose_thread_branch_free(&branch);
    }
    assert(test->root->native.universe->root_provider_len == test->baseline);
    hyperpose_worker_leave(NULL, NULL);
    cetta_name_context_install(&previous);
    return NULL;
}

static void *cetta_test_resource_reader_thread(void *argument) {
    CettaTestWorkerResourceLeases *test = argument;
    CettaNameContext previous = cetta_name_context_current();
    cetta_name_context_install(&test->names);
    hyperpose_worker_enter(NULL, NULL);
    Arena lease;
    arena_init_detached(&lease);
    cetta_test_resource_barrier(test);
    assert(atom_space(&lease, test->kept));
    SpaceOccurrenceCursor cursor;
    assert(space_occurrence_cursor_init(test->kept, test->pattern, &cursor));
    cetta_test_resource_barrier(test);
    cetta_test_resource_barrier(test);
    /* The kept space is a stack, so its reads answer newest first. */
    for (size_t i = 0u; i < 3u; i++) {
        size_t row = 2u - i;
        Atom *atom = NULL;
        CettaIndex index = UINT64_MAX;
        assert(space_occurrence_cursor_next(&cursor, &index, &atom) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == row && atom && atom_eq(atom, test->fact[row]));
    }
    assert(space_occurrence_cursor_next(&cursor, NULL, NULL) ==
           SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&cursor);
    arena_free(&lease);
    /* A foreign thread may drain the release queue, but only the owning
     * thread removes a record from its private home set. */
    assert(space_reclaim_unheld());
    assert(cetta_test_context_provider_count(
        test->root->native.universe, test->kept) == 2u);
    cetta_test_resource_barrier(test);
    cetta_test_resource_barrier(test);
    hyperpose_worker_leave(NULL, NULL);
    cetta_name_context_install(&previous);
    return NULL;
}

bool cetta_test_worker_resource_leases(Space *root, Arena *persistent) {
    assert(root && persistent);
    CettaTestWorkerResourceLeases test = {.root = root, .persistent = persistent,
        .baseline = root->native.universe->root_provider_len,
        .names = cetta_name_context_current()};
    test.fact[0] = atom_int(persistent, 17);
    test.fact[1] = atom_int(persistent, 19);
    test.fact[2] = test.fact[0];
    test.pattern = atom_var_with_id(persistent, "$worker-resource-fact", fresh_var_id());
    assert(pthread_barrier_init(&test.barrier, NULL, 2u) == 0);
    pthread_t owner, reader;
    assert(pthread_create(&owner, NULL, cetta_test_resource_owner_thread, &test) == 0);
    assert(pthread_create(&reader, NULL, cetta_test_resource_reader_thread, &test) == 0);
    assert(pthread_join(owner, NULL) == 0);
    assert(pthread_join(reader, NULL) == 0);
    assert(pthread_barrier_destroy(&test.barrier) == 0);
    assert(root->native.universe->root_provider_len == test.baseline);
    return true;
}

static StateCellDiagnostics cetta_test_state_quiescent(void) {
    for (unsigned pass = 0u; pass < 64u && state_cell_reclamation_due(); pass++)
        (void)state_cell_reclaim_bounded(64u);
    assert(!state_cell_reclamation_due());
    return state_cell_diagnostics();
}

static void cetta_test_state_string(StateCell *cell, const char *value, size_t length) {
    StateCellSnapshot snapshot;
    assert(state_cell_snapshot_acquire(cell, &snapshot));
    assert(snapshot.value && snapshot.content_type &&
           snapshot.value->kind == ATOM_GROUNDED &&
           snapshot.value->ground.gkind == GV_STRING &&
           snapshot.value->ground.slen == length &&
           memcmp(snapshot.value->ground.sval, value, length) == 0);
    state_cell_snapshot_release(&snapshot);
}

/* Private native controller: real constructor/publication/reader routes. */
static bool cetta_test_worker_storage_owner_case(Space *root, Arena *persistent,
                                                  unsigned observer_case) {
    bool borrow_private_image = observer_case == 1u;
    bool detached_cursor = observer_case == 2u;
    bool borrow_plain_view = observer_case == 3u;
    assert(root && persistent && space_length64(root) == 0u);
    StateCellDiagnostics states_before = cetta_test_state_quiescent();
    size_t owners_before = atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                                memory_order_relaxed);
    TermUniverse *universe = root->native.universe;
    TermUniverseCollectionMode old_mode = universe->collection_mode;
    term_universe_set_collection_mode(universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(term_universe_collect(universe, NULL));
    const size_t providers_before = universe->root_provider_len;
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    registry_init(&branch.registry);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    HyperposeStorageOwner *owner = branch.storage;
    void *previous_scope = arena_set_resource_scope(owner);
    EvalTermUniverseScope universe_scope = {0};
    eval_term_universe_scope_enter(&universe_scope, &owner->term_universe);
    Space *previous_root = g_eval_root_space;
    Space *space = cetta_test_new_managed_space(root, &owner->eval_arena);
    Space *peer = cetta_test_new_managed_space(root, &owner->eval_arena);
    g_eval_root_space = space;
    Atom *capture = materialize_runtime_token(space, &owner->eval_arena,
        atom_symbol_id(&owner->eval_arena, g_builtin_syms.capture));
    assert(capture && capture->ground.gkind == GV_CAPTURE);
    CaptureClosure *closure = capture->ground.ptr;
    assert(closure && closure->space_ptr == space);
    ResultSet states;
    result_set_init(&states);
    eval_top(root, &owner->eval_arena, atom_expr2(&owner->eval_arena,
        atom_symbol_id(&owner->eval_arena, g_builtin_syms.new_state),
        atom_string(&owner->eval_arena, "before")), &states);
    assert(states.len == 1u && states.items[0]->kind == ATOM_GROUNDED &&
           states.items[0]->ground.gkind == GV_STATE);
    StateCell *cell = states.items[0]->ground.ptr;
    assert(state_cell_is_managed(cell) &&
           !arena_owns_ptr(&owner->persistent_arena, cell));
    Atom *state = states.items[0];
    result_set_free(&states);
    SymbolId key = symbol_intern_cstr(g_symbols, "worker-owned-key");
    Atom *fact17 = atom_expr2(&owner->persistent_arena,
        atom_symbol_id(&owner->persistent_arena, key), atom_int(&owner->persistent_arena, 17));
    Atom *fact19 = atom_expr2(&owner->persistent_arena,
        atom_symbol_id(&owner->persistent_arena, key), atom_int(&owner->persistent_arena, 19));
    assert(space_admit_atom(space, &owner->persistent_arena, fact17));
    Atom *probe = atom_symbol(persistent, "worker-owned-capture-probe");
    Atom *query = atom_expr(persistent, &probe, 1u);
    Atom *rule = atom_expr3(&owner->persistent_arena,
        atom_symbol_id(&owner->persistent_arena, g_builtin_syms.equals), query,
        atom_int(&owner->persistent_arena, 29));
    assert(space_admit_atom(space, &owner->persistent_arena, rule));
    AtomId rule_id = term_universe_store_atom_id(
        &owner->term_universe, &owner->persistent_arena, rule);
    assert(rule_id != CETTA_ATOM_ID_NONE);
    assert(space_admit_atom(space, &owner->persistent_arena, capture));
    assert(space_admit_atom(space, &owner->persistent_arena,
        atom_space(&owner->eval_arena, peer)));
    assert(space_admit_atom(peer, &owner->persistent_arena, capture));
    Atom *space_atom_value = atom_space(&owner->eval_arena, space);
    Atom *parts[] = {atom_symbol(&owner->eval_arena, "owned-publication"),
        space_atom_value, space_atom_value, capture, capture, state, state};
    Atom *payload = atom_expr(&owner->eval_arena, parts, 7u);
    assert(atomic_load_explicit(&owner->references, memory_order_relaxed) == 1u);
    assert(eval_admit_atom(root, &owner->eval_arena, persistent, payload));
    assert(atomic_load_explicit(&owner->references, memory_order_relaxed) > 1u);
    assert(space_admit_atom(space, &owner->persistent_arena, fact19));
    ResultSet changed;
    result_set_init(&changed);
    eval_top(root, &owner->eval_arena, atom_expr3(&owner->eval_arena,
        atom_symbol_id(&owner->eval_arena, g_builtin_syms.change_state_bang),
        state, atom_string(&owner->eval_arena, "after")), &changed);
    assert(changed.len == 1u && changed.items[0]->ground.gkind == GV_STATE &&
           changed.items[0]->ground.ptr == cell);
    result_set_free(&changed);
    AtomId private_capture = term_universe_store_atom_id(
        &owner->term_universe, &owner->persistent_arena, capture);
    assert(private_capture != CETTA_ATOM_ID_NONE);
    assert(hyperpose_adopt_tracked_spaces(&branch));
    assert(g_made_spaces.len == 0u && g_listed_spaces.len == 0u &&
           g_new_space_track.len == 0u);
    eval_term_universe_scope_leave(&universe_scope);
    arena_set_resource_scope(previous_scope);
    g_eval_root_space = previous_root;
    hyperpose_thread_branch_free(&branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before + 1u);

    Arena observing, copied, work;
    arena_init_detached(&observing);
    arena_init_detached(&copied);
    arena_init_detached(&work);
    Atom *stored = space_view_at64(root, 0u, &observing);
    assert(stored && stored->kind == ATOM_EXPR && stored->expr.len == 7u);
    Atom *foreign = atom_deep_copy(&copied, stored);
    assert(foreign && foreign->expr.elems[1]->ground.ptr == space &&
           foreign->expr.elems[2]->ground.ptr == space &&
           foreign->expr.elems[3]->ground.ptr == closure &&
           foreign->expr.elems[4]->ground.ptr == closure &&
           foreign->expr.elems[5]->ground.ptr == cell &&
           foreign->expr.elems[6]->ground.ptr == cell);
    cetta_test_state_string(cell, "after", sizeof("after") - 1u);
    const int64_t expected[] = {29};
    cetta_test_capture_answers(space, &work, foreign->expr.elems[3], query, expected, 1u);
    arena_free(&work);
    Atom *variable = atom_var_with_id(persistent, "$worker-owned-row", fresh_var_id());
    Atom *pattern = detached_cursor
        ? atom_expr3(persistent, atom_symbol_id(persistent, g_builtin_syms.equals), query, variable)
        : atom_expr2(persistent, atom_symbol_id(persistent, key), variable);
    SpaceOccurrenceCursor cursor;
    assert(space_occurrence_cursor_init(space, pattern, &cursor));
    if (detached_cursor) {
        assert(!cursor.kept);
        assert(space_remove_atom_id(space, rule_id));
        assert(!space->match_backend.native.pinned);
    }
    AtomId row = space_get_atom_id_at64(root, 0u);
    assert(row != CETTA_ATOM_ID_NONE && space_remove_atom_id(root, row));
    assert(space_length64(root) == 0u);
    assert(term_universe_collect(universe, NULL));
    arena_free(&observing);
    assert(term_universe_collect(universe, NULL));
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before + 1u);
    arena_free(&copied);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before + 1u);
    const int64_t expected_rows[] = {detached_cursor ? 29 : 17, 19};
    const size_t expected_count = detached_cursor ? 1u : 2u;
    if (detached_cursor)
        assert(term_universe_collect(space_occurrence_cursor_universe(&cursor), NULL));
    bool seen[2] = {false, false};
    size_t answers = 0u;
    Arena matching;
    arena_init_detached(&matching);
    for (;;) {
        AtomId id = CETTA_ATOM_ID_NONE;
        size_t kept = SIZE_MAX;
        SpaceOccurrenceCursorStep step = space_occurrence_cursor_next_kept(
            &cursor, &id, &kept);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        assert(step == SPACE_OCCURRENCE_CURSOR_ITEM);
        if (kept != SIZE_MAX)
            id = space_occurrence_cursor_lend_kept(&cursor, kept);
        assert(id != CETTA_ATOM_ID_NONE);
        Atom *candidate = term_universe_view_atom(
            space_occurrence_cursor_universe(&cursor), &matching, id);
        assert(candidate);
        Bindings bindings;
        bindings_init(&bindings);
        if (simple_match(pattern, candidate, &bindings, &matching)) {
            Atom *value = bindings_apply(&bindings, &matching, variable);
            assert(value && value->kind == ATOM_GROUNDED &&
                   value->ground.gkind == GV_INT);
            int64_t integer = value->ground.ival;
            size_t selected = integer == expected_rows[0] ? 0u : 1u;
            assert(integer == expected_rows[selected] && !seen[selected]);
            seen[selected] = true;
            answers++;
        }
        bindings_free(&bindings);
    }
    assert(answers == expected_count && seen[0] &&
           (expected_count == 1u || seen[1]));
    arena_free(&matching);
    Arena borrowed;
    arena_init_detached(&borrowed);
    Atom *private_image = borrow_private_image
        ? term_universe_scoped_atom(space->native.universe, &borrowed, private_capture)
        : NULL;
    assert(!borrow_private_image || (private_image &&
        private_image->ground.gkind == GV_CAPTURE && private_image->ground.ptr == closure));
    Atom *plain_view = NULL;
    if (borrow_plain_view) {
        assert(term_universe_get_atom(space->native.universe, rule_id));
        for (size_t i = 0u; i < 4u; i++)
            plain_view = term_universe_view_atom(space->native.universe, &borrowed, rule_id);
        assert(plain_view);
    }
    space_occurrence_cursor_release(&cursor);
    if (borrow_private_image || borrow_plain_view) {
        assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                   memory_order_relaxed) == owners_before + 1u);
    }
    if (borrow_plain_view)
        assert(plain_view->kind == ATOM_EXPR && plain_view->expr.len == 3u &&
               plain_view->expr.elems[2]->ground.gkind == GV_INT &&
               plain_view->expr.elems[2]->ground.ival == 29);
    if (borrow_private_image) {
        arena_init_detached(&work);
        cetta_test_capture_answers(space, &work, private_image, query, expected, 1u);
        arena_free(&work);
    }
    arena_free(&borrowed);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before);
    assert(space_reclaim_unheld());
    assert(universe->root_provider_len == providers_before);
    StateCellDiagnostics states_after = cetta_test_state_quiescent();
    assert(states_after.nodes == states_before.nodes &&
           states_after.images == states_before.images &&
           states_after.image_bytes == states_before.image_bytes);
    term_universe_set_collection_mode(universe, old_mode);
    return true;
}

static bool cetta_test_worker_storage_owner_constructor_failure(Space *root) {
    const size_t owners_before = atomic_load_explicit(
        &g_hyperpose_storage_owners_live, memory_order_relaxed);
    const CettaCount rows_before = space_length64(root);
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    registry_init(&branch.registry);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    void *previous_scope = arena_set_resource_scope(branch.storage);
    EvalTermUniverseScope universe_scope = {0};
    eval_term_universe_scope_enter(&universe_scope, &branch.storage->term_universe);
    Space *space = cetta_test_new_managed_space(root, &branch.storage->eval_arena);
    Space *previous_root = g_eval_root_space;
    g_eval_root_space = space;
    CettaEvalEpisode completion;
    eval_completion_child_begin(&completion);
    g_hyperpose_fail_descriptor_note = true;
    Atom *failed = materialize_runtime_token(space, &branch.storage->eval_arena,
        atom_symbol_id(&branch.storage->eval_arena, g_builtin_syms.capture));
    assert(!failed && !g_hyperpose_fail_descriptor_note);
    assert(eval_completion_child_end(&completion) == CETTA_EVAL_INCOMPLETE_CAPACITY);
    assert(space_length64(root) == rows_before &&
           atomic_load_explicit(&branch.storage->references,
                                memory_order_relaxed) == 1u);
    EvalTrackedSpace *inventory = g_made_spaces.items;
    CettaCount inventory_len = g_made_spaces.len;
    assert(inventory && inventory_len == 1u);
    assert(hyperpose_adopt_tracked_spaces(&branch));
    assert(!g_made_spaces.items && branch.storage->made_spaces.items == inventory &&
           branch.storage->made_spaces.len == inventory_len);
    g_eval_root_space = previous_root;
    eval_term_universe_scope_leave(&universe_scope);
    arena_set_resource_scope(previous_scope);
    hyperpose_thread_branch_free(&branch);
    assert(space_reclaim_unheld());
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before);
    return true;
}

static bool cetta_test_worker_overlay_owner_order(Space *root) {
    const size_t owners_before = atomic_load_explicit(
        &g_hyperpose_storage_owners_live, memory_order_relaxed);
    HyperposeThreadBranch base_branch = {0}, overlay_branch = {0};
    bindings_init(&base_branch.capture_env);
    bindings_init(&overlay_branch.capture_env);
    registry_init(&base_branch.registry);
    registry_init(&overlay_branch.registry);
    base_branch.storage = hyperpose_storage_owner_new();
    overlay_branch.storage = hyperpose_storage_owner_new();
    assert(base_branch.storage && overlay_branch.storage);
    void *previous_scope = arena_set_resource_scope(base_branch.storage);
    EvalTermUniverseScope base_universe = {0};
    eval_term_universe_scope_enter(&base_universe, &base_branch.storage->term_universe);
    Space *base = cetta_test_new_managed_space(root, &base_branch.storage->eval_arena);
    assert(space_admit_atom(base, &base_branch.storage->persistent_arena,
        atom_int(&base_branch.storage->persistent_arena, 31)));
    assert(hyperpose_adopt_tracked_spaces(&base_branch));
    eval_term_universe_scope_leave(&base_universe);
    arena_set_resource_scope(overlay_branch.storage);
    EvalTermUniverseScope overlay_universe = {0};
    eval_term_universe_scope_enter(&overlay_universe,
        &overlay_branch.storage->term_universe);
    Space *overlay = cetta_malloc(sizeof(*overlay));
    space_init_overlay(overlay, base);
    held_space_register(overlay, false);
    assert(atom_space(&overlay_branch.storage->eval_arena, overlay));
    assert(overlay->native.universe == &base_branch.storage->term_universe &&
           space_lifetime_pin_keeps_storage(overlay->arena_hold->base_hold));
    assert(space_length64(overlay) == 1u);
    assert(hyperpose_adopt_tracked_spaces(&overlay_branch));
    SpaceHold *detached = overlay->arena_hold;
    space_hold_retain(detached);
    eval_term_universe_scope_leave(&overlay_universe);
    arena_set_resource_scope(previous_scope);
    hyperpose_thread_branch_free(&base_branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before + 2u);
    /* The overlay's final base lease must survive its own provider/row
     * teardown, which still addresses the base owner's private universe. */
    hyperpose_thread_branch_free(&overlay_branch);
    /* An outstanding record observer keeps the foreign universe, while the
     * completed native teardown must already have ended its overlay edge. */
    assert(!detached->space && !detached->base_hold &&
           detached->foreign_universe_owner);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before + 1u);
    space_hold_release(detached);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before);
    assert(space_reclaim_unheld());
    return true;
}

bool cetta_test_worker_storage_owner_lifetime(Space *root, Arena *persistent) {
    return cetta_test_worker_storage_owner_case(root, persistent, 0u) &&
           cetta_test_worker_storage_owner_case(root, persistent, 1u) &&
           cetta_test_worker_storage_owner_case(root, persistent, 2u) &&
           cetta_test_worker_storage_owner_case(root, persistent, 3u) &&
           cetta_test_worker_storage_owner_constructor_failure(root) &&
           cetta_test_worker_overlay_owner_order(root);
}

/* Managed identity and immutable value images outlive both the creating
 * worker A and updating worker B; no coarse worker retention is required. */
static bool cetta_test_foreign_state_update_mode(Space *root, Arena *persistent,
                                                 bool transactional) {
    (void)persistent;
    assert(space_length64(root) == 0u);
    StateCellDiagnostics states_before = cetta_test_state_quiescent();
    const size_t count = transactional ? 2u : 1u;
    const size_t baseline = atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                                 memory_order_relaxed);
    HyperposeThreadBranch first = {0}, writer = {0};
    bindings_init(&first.capture_env);
    registry_init(&first.registry);
    first.storage = hyperpose_storage_owner_new();
    assert(first.storage);
    HyperposeStorageOwner *first_owner = first.storage;
    Arena retained, observed;
    arena_init_detached(&retained);
    arena_init_detached(&observed);
    void *previous = arena_set_resource_scope(first_owner);
    EvalTermUniverseScope first_universe = {0};
    eval_term_universe_scope_enter(&first_universe, &first_owner->term_universe);
    ResultSet created;
    result_set_init(&created);
    eval_top(root, &first_owner->eval_arena,
        atom_expr2(&first_owner->eval_arena,
            atom_symbol_id(&first_owner->eval_arena, g_builtin_syms.new_state),
            atom_string(&first_owner->eval_arena, "before")), &created);
    assert(created.len == 1u && created.items[0]->ground.gkind == GV_STATE);
    StateCell *cell = created.items[0]->ground.ptr;
    assert(state_cell_is_managed(cell) &&
           !arena_owns_ptr(&first_owner->persistent_arena, cell) &&
           !arena_owns_ptr(&first_owner->eval_arena, cell));
    Atom *kept = atom_deep_copy(&retained, created.items[0]);
    assert(kept && kept->ground.ptr == cell);
    result_set_free(&created);
    StateCellSnapshot initial;
    assert(state_cell_snapshot_acquire(cell, &initial));
    StateCell *peer_cell = NULL;
    Atom *peer_kept = NULL;
    if (transactional) {
        result_set_init(&created);
        eval_top(root, &first_owner->eval_arena,
            atom_expr2(&first_owner->eval_arena,
                atom_symbol_id(&first_owner->eval_arena, g_builtin_syms.new_state),
                atom_string(&first_owner->eval_arena, "peer before")), &created);
        assert(created.len == 1u && created.items[0]->ground.gkind == GV_STATE);
        peer_cell = created.items[0]->ground.ptr;
        assert(peer_cell != cell && state_cell_is_managed(peer_cell) &&
               !arena_owns_ptr(&first_owner->persistent_arena, peer_cell));
        peer_kept = atom_deep_copy(&retained, created.items[0]);
        assert(peer_kept && peer_kept->ground.ptr == peer_cell);
        result_set_free(&created);
    }
    assert(hyperpose_adopt_tracked_spaces(&first));
    eval_term_universe_scope_leave(&first_universe);
    arena_set_resource_scope(previous);
    hyperpose_thread_branch_free(&first);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline);
    StateCellDiagnostics after_first = cetta_test_state_quiescent();
    assert(after_first.nodes == states_before.nodes + count &&
           after_first.images == states_before.images + count);
    cetta_test_state_string(cell, "before", sizeof("before") - 1u);
    if (peer_cell)
        cetta_test_state_string(peer_cell, "peer before", sizeof("peer before") - 1u);

    bindings_init(&writer.capture_env);
    registry_init(&writer.registry);
    writer.storage = hyperpose_storage_owner_new();
    assert(writer.storage);
    HyperposeStorageOwner *writer_owner = writer.storage;
    previous = arena_set_resource_scope(writer_owner);
    EvalTermUniverseScope writer_universe = {0};
    eval_term_universe_scope_enter(&writer_universe, &writer_owner->term_universe);
    char expected[8192];
    memset(expected, 'v', sizeof(expected));
    expected[0] = 'W';
    expected[sizeof(expected) - 1u] = '\0';
    Atom *replacement = atom_string_n(&writer_owner->eval_arena,
                                       expected, sizeof(expected));
    PettaEvalMachineContext transaction_context = {.fuel = -1};
    Registry *previous_registry = g_registry;
    void *transaction = NULL;
    Space *working_root = NULL;
    StateCell *target_cell = cell, *peer_working = NULL;
    if (transactional) {
        g_registry = &writer.registry;
        registry_bind_id(g_registry, g_builtin_syms.self,
            atom_space(&writer_owner->eval_arena, root));
        assert(petta_eval_machine_transaction_begin(&transaction_context,
            root, &writer_owner->eval_arena, &transaction, &working_root));
        target_cell = petta_eval_transaction_clone_state(transaction, cell);
        peer_working = petta_eval_transaction_clone_state(transaction, peer_cell);
        assert(target_cell && peer_working);
        PettaEvalTransactionState *identity =
            petta_eval_transaction_state_by_original(transaction, cell);
        PettaEvalTransactionState *peer_identity =
            petta_eval_transaction_state_by_original(transaction, peer_cell);
        assert(identity && peer_identity && identity->identity_managed_pin &&
               peer_identity->identity_managed_pin && !identity->identity_owner &&
               !peer_identity->identity_owner);
        /* Each transaction identity pin is now its node's only holder. A
         * retained image keeps data, but cannot keep a node alive. */
        arena_free(&retained);
        StateCellDiagnostics pinned = cetta_test_state_quiescent();
        assert(pinned.nodes == states_before.nodes + count &&
               state_cell_is_managed(cell) && state_cell_is_managed(peer_cell));
        assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                   memory_order_relaxed) == baseline + 1u);
        arena_init_detached(&retained);
        kept = atom_state(&retained, cell);
        peer_kept = atom_state(&retained, peer_cell);
        assert(kept && peer_kept && peer_kept->ground.ptr == peer_cell);
    }
    Atom *reference = atom_state(&writer_owner->eval_arena, target_cell);
    Atom *call = atom_expr3(&writer_owner->eval_arena,
        atom_symbol_id(&writer_owner->eval_arena, g_builtin_syms.change_state_bang),
        reference, replacement);
    Bindings empty;
    bindings_init(&empty);
    OutcomeSet updated;
    outcome_set_init(&updated);
    if (!transactional) {
        StateCellSnapshot before;
        assert(state_cell_snapshot_acquire(cell, &before));
        StateCellDiagnostics before_failure = state_cell_diagnostics();
        CettaEvalEpisode failure = {.completion = CETTA_EVAL_COMPLETE};
        CettaEvalEpisode *previous_tracker = g_eval_completion_tracker;
        g_eval_completion_tracker = &failure;
        g_worker_state_value_fail_after = 1u;
        state_change_publish_value(root, &writer_owner->eval_arena, call,
            reference, replacement, &empty, &empty, false, &updated);
        g_eval_completion_tracker = previous_tracker;
        StateCellSnapshot unchanged;
        assert(state_cell_snapshot_acquire(cell, &unchanged));
        StateCellDiagnostics after_failure = state_cell_diagnostics();
        assert(!updated.len && unchanged.image == before.image &&
               unchanged.value == before.value && unchanged.content_type == before.content_type &&
               failure.completion == CETTA_EVAL_INCOMPLETE_CAPACITY &&
               after_failure.nodes == before_failure.nodes &&
               after_failure.images == before_failure.images &&
               after_failure.image_bytes == before_failure.image_bytes);
        state_cell_snapshot_release(&unchanged);
        state_cell_snapshot_release(&before);
    }
    state_change_publish_value(root, &writer_owner->eval_arena, call,
        reference, replacement, &empty, &empty, false, &updated);
    assert(updated.len == 1u && !atom_is_error(updated.items[0].atom) &&
           updated.items[0].atom->ground.ptr == target_cell);
    outcome_set_free(&updated);
    if (transactional) {
        Atom *peer_ref = atom_state(&writer_owner->eval_arena, peer_working);
        outcome_set_init(&updated);
        state_change_publish_value(root, &writer_owner->eval_arena, call,
            peer_ref, replacement, &empty, &empty, false, &updated);
        assert(updated.len == 1u && !atom_is_error(updated.items[0].atom));
        outcome_set_free(&updated);
        StateCellSnapshot before, peer_before;
        assert(state_cell_snapshot_acquire(cell, &before) &&
               state_cell_snapshot_acquire(peer_cell, &peer_before));
        StateCellDiagnostics before_failure = state_cell_diagnostics();
        Atom *head = atom_symbol(&writer_owner->eval_arena, "state-publication-once");
        Atom *fact = atom_expr(&writer_owner->eval_arena, &head, 1u);
        assert(space_admit_atom(working_root, &writer_owner->eval_arena, fact));
        CettaEvalEpisode failure = {.completion = CETTA_EVAL_COMPLETE};
        CettaEvalEpisode *previous_tracker = g_eval_completion_tracker;
        g_eval_completion_tracker = &failure;
        g_worker_state_value_fail_after = 2u;
        assert(!petta_eval_machine_transaction_commit(&transaction_context, transaction));
        g_eval_completion_tracker = previous_tracker;
        StateCellSnapshot unchanged, peer_unchanged;
        assert(state_cell_snapshot_acquire(cell, &unchanged) &&
               state_cell_snapshot_acquire(peer_cell, &peer_unchanged));
        StateCellDiagnostics after_failure = state_cell_diagnostics();
        assert(failure.completion == CETTA_EVAL_INCOMPLETE_CAPACITY &&
               unchanged.image == before.image && peer_unchanged.image == peer_before.image &&
               after_failure.nodes == before_failure.nodes &&
               after_failure.images == before_failure.images &&
               after_failure.image_bytes == before_failure.image_bytes &&
               space_length64(root) == 0u && transaction_context.transaction == transaction);
        state_cell_snapshot_release(&unchanged);
        state_cell_snapshot_release(&peer_unchanged);
        state_cell_snapshot_release(&before);
        state_cell_snapshot_release(&peer_before);
        assert(petta_eval_machine_transaction_commit(&transaction_context, transaction));
        assert(!transaction_context.transaction);
        assert(space_length64(root) == 1u && space_remove(root, fact));
        g_registry = previous_registry;
    } else {
        /* Repeated real evaluator writes replace the current image rather
         * than retaining an overwrite history in either worker. */
        StateCellDiagnostics steady = state_cell_diagnostics();
        for (unsigned repeat = 0u; repeat < 64u; repeat++) {
            outcome_set_init(&updated);
            state_change_publish_value(root, &writer_owner->eval_arena, call,
                reference, replacement, &empty, &empty, false, &updated);
            assert(updated.len == 1u && updated.items[0].atom->ground.ptr == cell);
            outcome_set_free(&updated);
            StateCellDiagnostics now = state_cell_diagnostics();
            assert(now.nodes == steady.nodes && now.images == steady.images &&
                   now.image_bytes == steady.image_bytes);
        }
    }
    bindings_free(&empty);
    assert(hyperpose_adopt_tracked_spaces(&writer));
    eval_term_universe_scope_leave(&writer_universe);
    arena_set_resource_scope(previous);
    hyperpose_thread_branch_free(&writer);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline);
    assert(cetta_test_state_quiescent().nodes == states_before.nodes + count);
    cetta_test_state_string(cell, expected, sizeof(expected));
    if (peer_cell) {
        assert(peer_kept && peer_kept->ground.ptr == peer_cell);
        cetta_test_state_string(peer_cell, expected, sizeof(expected));
    }
    StateCellSnapshot survivor;
    assert(state_cell_snapshot_acquire(cell, &survivor));
    Arena request;
    arena_init_detached(&request);
    ResultSet read;
    result_set_init(&read);
    eval_top(root, &observed, atom_expr2(&request,
        atom_symbol_id(&request, g_builtin_syms.get_state), kept), &read);
    assert(read.len == 1u && read.items[0]->ground.gkind == GV_STRING &&
           read.items[0]->ground.slen == sizeof(expected) &&
           memcmp(read.items[0]->ground.sval, expected, sizeof(expected)) == 0);
    Atom *snapshot = read.items[0];
    result_set_free(&read);
    arena_free(&request);
    arena_free(&retained);
    StateCellDiagnostics values_only = cetta_test_state_quiescent();
    assert(values_only.nodes == states_before.nodes &&
           values_only.images == states_before.images + 2u);
    assert(initial.value->ground.slen == sizeof("before") - 1u &&
           memcmp(initial.value->ground.sval, "before", sizeof("before") - 1u) == 0);
    assert(survivor.value->ground.slen == sizeof(expected) &&
           memcmp(survivor.value->ground.sval, expected, sizeof(expected)) == 0);
    state_cell_snapshot_release(&initial);
    state_cell_snapshot_release(&survivor);
    assert(state_cell_diagnostics().images <= states_before.images + 1u);
    assert(snapshot->ground.slen == sizeof(expected) &&
           memcmp(snapshot->ground.sval, expected, sizeof(expected)) == 0);
    arena_free(&observed);
    StateCellDiagnostics released = cetta_test_state_quiescent();
    assert(released.nodes == states_before.nodes &&
           released.images == states_before.images &&
           released.image_bytes == states_before.image_bytes);
    return true;
}

bool cetta_test_foreign_state_update_lifetime(Space *root, Arena *persistent) {
    return cetta_test_foreign_state_update_mode(root, persistent, false) &&
        cetta_test_foreign_state_update_mode(root, persistent, true);
}

/* The receipt is deliberately the only node observer after the worker
 * retires: copied scalar values cannot incidentally retain that identity. */
bool cetta_test_worker_state_receipt_lifetime(Space *root, unsigned route,
                                             bool write_event) {
    assert(route < 3u);
    StateCellDiagnostics states_before = cetta_test_state_quiescent();
    const size_t baseline = atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                                 memory_order_relaxed);
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    registry_init(&branch.registry);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    HyperposeStorageOwner *owner = branch.storage;
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    void *previous = arena_set_resource_scope(owner);
    EvalTermUniverseScope universe_scope = {0};
    eval_term_universe_scope_enter(&universe_scope, &owner->term_universe);
    ResultSet created;
    result_set_init(&created);
    eval_top(root, &owner->eval_arena, atom_expr2(&owner->eval_arena,
        atom_symbol_id(&owner->eval_arena, g_builtin_syms.new_state),
        atom_int(&owner->eval_arena, 17)), &created);
    assert(created.len == 1u && created.items[0]->kind == ATOM_GROUNDED &&
           created.items[0]->ground.gkind == GV_STATE);
    StateCell *cell = created.items[0]->ground.ptr;
    assert(state_cell_is_managed(cell) &&
           !arena_owns_ptr(&owner->persistent_arena, cell) &&
           !arena_owns_ptr(&owner->eval_arena, cell));
    result_set_free(&created);
    assert(atomic_load_explicit(&owner->references, memory_order_relaxed) == 1u);

    PrimeNeedBranchState base, receipt;
    prime_need_branch_state_init(&base);
    assert(prime_need_branch_state_begin(&source, &base));
    StateCellSnapshot initial;
    assert(state_cell_snapshot_acquire(cell, &initial));
    if (write_event) {
        assert(prime_need_branch_state_write(&source, &base, cell,
            initial.value, atom_int(&owner->eval_arena, 23), &receipt));
    } else {
        assert(prime_need_branch_state_read(&source, &base, cell,
            initial.value, &receipt));
    }
    state_cell_snapshot_release(&initial);
    assert(hyperpose_adopt_tracked_spaces(&branch));
    eval_term_universe_scope_leave(&universe_scope);
    arena_set_resource_scope(previous);

    if (route != 0u) {
        PrimeNeedBranchState promoted = receipt;
        if (route == 1u) {
            assert(prime_need_branch_state_promote(&destination, &promoted));
        } else {
            assert(prime_need_branch_state_promote_suffix(
                &destination, &source, &promoted));
        }
        assert(prime_need_branch_state_equal(&receipt, &promoted));
        assert(prime_need_branch_state_excludes_arena(&promoted, &source));
        receipt = promoted;
        arena_free(&source);
    }
    hyperpose_thread_branch_free(&branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline);
    /* Drain zero-reference nodes before inspecting identity: an omitted
     * receipt lease must fail here, not pass merely because it is queued. */
    StateCellDiagnostics retained = cetta_test_state_quiescent();
    assert(retained.nodes == states_before.nodes + 1u &&
           retained.images == states_before.images + 1u &&
           state_cell_is_managed(cell));
    StateCellSnapshot current;
    assert(state_cell_snapshot_acquire(cell, &current));
    assert(current.value->kind == ATOM_GROUNDED &&
           current.value->ground.gkind == GV_INT && current.value->ground.ival == 17);
    state_cell_snapshot_release(&current);
    Atom *observed = NULL;
    assert(prime_need_branch_state_value(&receipt, cell, &observed));
    assert(observed && observed->ground.gkind == GV_INT &&
           observed->ground.ival == (write_event ? 23 : 17));
    if (route == 0u)
        arena_free(&source);
    arena_free(&destination);
    StateCellDiagnostics released = cetta_test_state_quiescent();
    assert(released.nodes == states_before.nodes &&
           released.images == states_before.images &&
           released.image_bytes == states_before.image_bytes &&
           atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                memory_order_relaxed) == baseline);
    return true;
}

static void cetta_test_private_collection_pull(Space *root, Arena *answers,
                                               TermUniverse *universe) {
    PettaEvalMachineContext context = {.fuel = -1};
    PettaMachineHost host = petta_eval_machine_host(&context, -1, false,
        false, false, false, false,
        petta_eval_machine_match_semantics(NULL, false));
    assert(host.allocation_universe == universe &&
           host.collect_allocation_universe == petta_eval_collect_allocation_universe);
    Bindings empty, environment;
    bindings_init(&empty);
    PettaMachine machine = {0};
    assert(petta_machine_init(&machine, root, answers,
        atom_int(answers, 42), &empty, &host));
    Atom *answer = NULL;
    assert(petta_machine_next(&machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED &&
           answer->ground.gkind == GV_INT && answer->ground.ival == 42);
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    bindings_free(&empty);
}

bool cetta_test_worker_private_collection(Space *root) {
    const size_t owners_before = atomic_load_explicit(
        &g_hyperpose_storage_owners_live, memory_order_relaxed);
    const uint64_t shared_epoch = root->native.universe->collection_epoch;
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    registry_init(&branch.registry);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    HyperposeStorageOwner *owner = branch.storage;
    TermUniverse *universe = &owner->term_universe;
    term_universe_set_collection_mode(universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    Arena source, retained, borrowed, answers;
    arena_init_detached(&source);
    arena_init_detached(&retained);
    arena_init_detached(&borrowed);
    arena_init_detached(&answers);
    void *previous_scope = arena_set_resource_scope(owner);
    EvalTermUniverseScope allocation = {0};
    eval_term_universe_scope_enter(&allocation, universe);
    cetta_shared_transition_scope_enter();
    Space *kept = cetta_test_new_managed_space(root, &retained);
    Atom *fact = atom_expr2(&source, atom_symbol(&source, "private-kept"),
                            atom_int(&source, 71));
    assert(space_admit_atom(kept, &source, fact));
    AtomId kept_id = space_get_atom_id_at64(kept, 0u);
    assert(kept_id != CETTA_ATOM_ID_NONE);
    Space *left = cetta_test_new_managed_space(root, &source);
    Space *right = cetta_test_new_managed_space(root, &source);
    Atom *left_row = atom_expr2(&source, atom_symbol(&source, "edge"),
                                atom_space(&source, right));
    Atom *right_row = atom_expr2(&source, atom_symbol(&source, "edge"),
                                 atom_space(&source, left));
    assert(space_admit_atom(left, &source, left_row));
    assert(space_admit_atom(right, &source, right_row));
    Atom *scoped = space_view_at64(left, 0u, &borrowed);
    assert(scoped && scoped->kind == ATOM_EXPR);
    arena_free(&source);
    assert(space_reclaim_unheld());
    assert(atomic_load_explicit(&owner->references, memory_order_relaxed) > 1u);
    for (unsigned pass = 0u; pass < 2u; pass++) {
        Arena garbage;
        arena_init_detached(&garbage);
        AtomId last = CETTA_ATOM_ID_NONE;
        for (unsigned i = 0u; i < 65536u; i++) {
            Atom *dead = atom_expr2(&garbage,
                atom_symbol(&garbage, "private-unused"),
                atom_int(&garbage, 100000u + pass * 65536u + i));
            last = term_universe_store_atom_id(universe, &garbage, dead);
            assert(last != CETTA_ATOM_ID_NONE);
        }
        arena_free(&garbage);
        assert(tu_hdr(universe, last) && term_universe_collection_due(universe));
        const uint64_t epoch = universe->collection_epoch;
        /* Calling a foreign universe cannot manufacture a capability. */
        uint64_t shared_due = root->native.universe->collection_due;
        root->native.universe->collection_due = 0u;
        petta_eval_collect_allocation_universe(NULL, root->native.universe);
        root->native.universe->collection_due = shared_due;
        assert(root->native.universe->collection_epoch == shared_epoch);
        arena_set_resource_scope(NULL);
        petta_eval_collect_allocation_universe(NULL, universe);
        arena_set_resource_scope(owner);
        assert(universe->collection_epoch == epoch);
        cetta_test_private_collection_pull(root, &answers, universe);
        /* Reclamation is observable by a non-inserting lookup. Reading the
         * deliberately dead coordinate would trigger the checked reader. */
        Atom *last_key = atom_expr2(&answers,
            atom_symbol(&answers, "private-unused"),
            atom_int(&answers, 100000u + pass * 65536u + 65535u));
        assert(universe->collection_epoch > epoch &&
               term_universe_lookup_atom_id(universe, last_key) == CETTA_ATOM_ID_NONE);
        assert(root->native.universe->collection_epoch == shared_epoch);
        assert(space_length64(kept) == 1u &&
               space_get_atom_id_at64(kept, 0u) == kept_id && tu_hdr(universe, kept_id));
        if (pass == 0u) {
            assert(cetta_test_context_provider_count(universe, left) == 2u &&
                   cetta_test_context_provider_count(universe, right) == 2u);
            assert(scoped->expr.elems[1]->ground.ptr == right);
            arena_free(&borrowed);
        } else {
            assert(cetta_test_context_provider_count(universe, left) == 0u &&
                   cetta_test_context_provider_count(universe, right) == 0u);
        }
    }
    arena_free(&answers);
    arena_free(&retained);
    assert(space_reclaim_unheld());
    assert(hyperpose_adopt_tracked_spaces(&branch));
    cetta_shared_transition_scope_leave();
    eval_term_universe_scope_leave(&allocation);
    arena_set_resource_scope(previous_scope);
    hyperpose_thread_branch_free(&branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == owners_before);
    return true;
}

/* Real snapshot borrowing: the clone's allocation owner and record owner
 * differ. Copies of its wrapper must retain both through final row teardown. */
bool cetta_test_foreign_snapshot_universe(Space *root, bool in_worker,
                                           unsigned clone_route) {
    assert(clone_route < 3u && (clone_route != 2u || in_worker));
    const size_t baseline = atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                                 memory_order_relaxed);
    HyperposeThreadBranch source_branch = {0}, clone_branch = {0};
    bindings_init(&source_branch.capture_env);
    registry_init(&source_branch.registry);
    source_branch.storage = hyperpose_storage_owner_new();
    assert(source_branch.storage);
    HyperposeStorageOwner *source_owner = source_branch.storage;
    Arena source_keep, construction, surviving_copy, observation;
    arena_init_detached(&source_keep);
    arena_init_detached(&construction);
    arena_init_detached(&surviving_copy);
    arena_init_detached(&observation);
    void *previous = arena_set_resource_scope(source_owner);
    EvalTermUniverseScope source_scope = {0};
    eval_term_universe_scope_enter(&source_scope, &source_owner->term_universe);
    Space *source = cetta_test_new_managed_space(root, &source_owner->eval_arena);
    for (unsigned i = 0u; i < 2u; i++) {
        Atom *fact = atom_expr2(&source_owner->eval_arena,
            atom_symbol(&source_owner->eval_arena, "snapshot-value"),
            atom_int(&source_owner->eval_arena, i ? 19 : 17));
        assert(space_admit_atom(source, &source_owner->eval_arena, fact));
    }
    assert(atom_space(&source_keep, source));
    assert(hyperpose_adopt_tracked_spaces(&source_branch));
    eval_term_universe_scope_leave(&source_scope);
    arena_set_resource_scope(previous);
    hyperpose_thread_branch_free(&source_branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline + 1u);

    EvalTermUniverseScope clone_scope = {0};
    void *previous_clone_scope = NULL;
    if (in_worker) {
        bindings_init(&clone_branch.capture_env);
        registry_init(&clone_branch.registry);
        clone_branch.storage = hyperpose_storage_owner_new();
        assert(clone_branch.storage);
        previous_clone_scope = arena_set_resource_scope(clone_branch.storage);
        eval_term_universe_scope_enter(&clone_scope,
                                       &clone_branch.storage->term_universe);
    }
    Space *snapshot = clone_route == 1u ? space_persistent_clone(source, &construction)
        : eval_space_snapshot_clone(source, &construction);
    assert(snapshot && snapshot != source && snapshot->native.universe ==
           &source_owner->term_universe && !snapshot->overlay_base);
    Atom *wrapper = atom_space(&construction, snapshot);
    if (clone_route == 2u) {
        Registry *previous_registry = g_registry;
        g_registry = &clone_branch.registry;
        Bindings empty;
        bindings_init(&empty);
        OutcomeSet published;
        outcome_set_init(&published);
        Atom *name = atom_symbol(&construction, "foreign-snapshot-binding");
        Atom *call = atom_expr3(&construction,
            atom_symbol_id(&construction, g_builtin_syms.bind_bang), name, wrapper);
        assert(space_published_as_copy(snapshot));
        bind_publish_value(&construction, call, wrapper, &empty, false, &published);
        assert(published.len == 1u && !atom_is_error(published.items[0].atom));
        Atom *bound = registry_lookup_id(g_registry, name->sym_id);
        assert(bound && bound->kind == ATOM_GROUNDED &&
               bound->ground.gkind == GV_SPACE && bound->ground.ptr != snapshot);
        snapshot = bound->ground.ptr;
        assert(snapshot->arena_hold && !space_published_as_copy(snapshot) &&
               snapshot->native.universe == &source_owner->term_universe);
        wrapper = bound;
        outcome_set_free(&published);
        bindings_free(&empty);
        g_registry = previous_registry;
    }
    Atom *copy = atom_deep_copy(&surviving_copy, wrapper);
    assert(copy && copy->ground.gkind == GV_SPACE && copy->ground.ptr == snapshot);
    if (in_worker) {
        assert(hyperpose_adopt_tracked_spaces(&clone_branch));
        if (clone_route == 2u)
            assert(clone_branch.storage->made_spaces.len == 2u);
        eval_term_universe_scope_leave(&clone_scope);
        arena_set_resource_scope(previous_clone_scope);
        hyperpose_thread_branch_free(&clone_branch);
    }
    arena_free(&construction);
    arena_free(&source_keep);
    /* Test lifetime independently before reading a possibly retired TU. */
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) ==
           baseline + (in_worker ? 2u : 1u));
    assert(term_universe_collect(snapshot->native.universe, NULL));
    assert(space_length64(snapshot) == 2u);
    for (CettaIndex index = 0u; index < 2u; index++) {
        Atom *value = space_view_at64(snapshot, index, &observation);
        assert(value && value->kind == ATOM_EXPR && value->expr.len == 2u);
        value = value->expr.elems[1];
        assert(value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT &&
               value->ground.ival == (index ? 19 : 17));
    }
    arena_free(&observation);
    if (!in_worker && clone_route == 0u) {
        Arena matching;
        arena_init_detached(&matching);
        Atom *pattern = atom_var_with_id(&matching, "$snapshot-row", fresh_var_id());
        SpaceOccurrenceCursor cursor;
        assert(space_occurrence_cursor_init(snapshot, pattern, &cursor));
        assert(space_reclaim_unheld());
        eval_free_held_space((EvalTrackedSpace){.space = snapshot});
        arena_free(&surviving_copy);
        assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                   memory_order_relaxed) == baseline + 1u);
        assert(term_universe_collect(space_occurrence_cursor_universe(&cursor), NULL));
        size_t rows = 0u;
        for (;;) {
            AtomId id = CETTA_ATOM_ID_NONE;
            size_t kept = SIZE_MAX;
            SpaceOccurrenceCursorStep step = space_occurrence_cursor_next_kept(
                &cursor, &id, &kept);
            if (step == SPACE_OCCURRENCE_CURSOR_END)
                break;
            assert(step == SPACE_OCCURRENCE_CURSOR_ITEM && rows < 2u);
            if (kept != SIZE_MAX)
                id = space_occurrence_cursor_lend_kept(&cursor, kept);
            Atom *value = term_universe_view_atom(
                space_occurrence_cursor_universe(&cursor), &matching, id);
            assert(value && value->kind == ATOM_EXPR && value->expr.len == 2u &&
                   value->expr.elems[1]->ground.gkind == GV_INT &&
                   value->expr.elems[1]->ground.ival == (rows ? 19 : 17));
            rows++;
        }
        assert(rows == 2u);
        arena_free(&matching);
        space_occurrence_cursor_release(&cursor);
    } else {
        arena_free(&surviving_copy);
    }
    assert(space_reclaim_unheld());
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline);
    return true;
}

/* Even a machine with no resource-valued query retains its allocation
 * universe across suspension and refuses collection in a different scope. */
/* A main-caller machine retains an escaped worker root Space independently
 * of the universe in which this machine allocates its own terms. */
static bool cetta_test_worker_machine_root_space_lifetime(Space *root) {
    const size_t baseline = atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                                 memory_order_relaxed);
    /* Allocation belongs to the main caller; only the root Space is foreign. */
    assert(!g_eval_term_universe_override && !arena_current_resource_scope());
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    registry_init(&branch.registry);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    HyperposeStorageOwner *owner = branch.storage;
    void *previous = arena_set_resource_scope(owner);
    EvalTermUniverseScope allocation = {0};
    eval_term_universe_scope_enter(&allocation, &owner->term_universe);
    Space *space = cetta_test_new_managed_space(root, &owner->eval_arena);
    assert(space->arena_hold->storage_owner == owner &&
           !space->arena_hold->foreign_universe_owner &&
           space->native.universe == &owner->term_universe);
    Atom *private_head = atom_symbol(&owner->persistent_arena,
                                      "worker-machine-root-answer");
    Atom *private_call = atom_expr(&owner->persistent_arena, &private_head, 1u);
    Atom *equation = atom_expr3(&owner->persistent_arena,
        atom_symbol_id(&owner->persistent_arena, g_builtin_syms.equals),
        private_call, atom_int(&owner->persistent_arena, 42));
    assert(space_admit_atom(space, &owner->persistent_arena, equation));
    Arena holder;
    arena_init_detached(&holder);
    Atom *escaped = atom_space(&holder, space);
    assert(escaped && escaped->ground.ptr == space);
    assert(hyperpose_adopt_tracked_spaces(&branch));
    eval_term_universe_scope_leave(&allocation);
    arena_set_resource_scope(previous);
    hyperpose_thread_branch_free(&branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline + 1u);

    Arena queries, answers;
    arena_init_detached(&queries);
    arena_init_detached(&answers);
    Atom *head = atom_symbol(&queries, "worker-machine-root-answer");
    Atom *query = atom_expr(&queries, &head, 1u);
    PettaMachine machine = {0};
    /* Public API, no host allocation-universe hook and no resource query. */
    assert(petta_machine_init(&machine, space, &answers, query, NULL, NULL));
    arena_free(&holder);
    /* The machine is now the sole external holder of the source storage.
     * Check its lifetime before dereferencing the Space or its universe. */
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline + 1u);
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED &&
           answer->ground.gkind == GV_INT && answer->ground.ival == 42);
    bindings_free(&environment);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline + 1u);
    petta_machine_destroy(&machine);
    arena_free(&answers);
    arena_free(&queries);
    assert(space_reclaim_unheld());
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline);
    return true;
}

bool cetta_test_worker_machine_scope_lifetime(Space *root) {
    const size_t baseline = atomic_load_explicit(&g_hyperpose_storage_owners_live,
                                                 memory_order_relaxed);
    HyperposeThreadBranch branch = {0};
    bindings_init(&branch.capture_env);
    registry_init(&branch.registry);
    branch.storage = hyperpose_storage_owner_new();
    assert(branch.storage);
    HyperposeStorageOwner *owner = branch.storage;
    void *previous = arena_set_resource_scope(owner);
    EvalTermUniverseScope allocation = {0};
    eval_term_universe_scope_enter(&allocation, &owner->term_universe);
    Arena answers;
    arena_init_detached(&answers);
    PettaEvalMachineContext context = {.fuel = -1};
    PettaMachineHost host = petta_eval_machine_host(&context, -1, false,
        false, false, false, false,
        petta_eval_machine_match_semantics(NULL, false));
    Bindings empty, environment;
    bindings_init(&empty);
    PettaMachine machine = {0};
    assert(petta_machine_init(&machine, root, &answers,
        atom_int(&answers, 42), &empty, &host));
    assert(hyperpose_adopt_tracked_spaces(&branch));
    eval_term_universe_scope_leave(&allocation);
    arena_set_resource_scope(previous);
    hyperpose_thread_branch_free(&branch);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline + 1u);
    uint64_t epoch = owner->term_universe.collection_epoch;
    owner->term_universe.collection_due = 0u;
    Atom *answer = NULL;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->ground.gkind == GV_INT && answer->ground.ival == 42);
    assert(owner->term_universe.collection_epoch == epoch);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    assert(atomic_load_explicit(&g_hyperpose_storage_owners_live,
                               memory_order_relaxed) == baseline);
    bindings_free(&empty);
    arena_free(&answers);
    return cetta_test_worker_machine_root_space_lifetime(root);
}
