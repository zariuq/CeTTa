/* Typed host entry to the existing retained-work owner. Language Work forms
 * retain their own observation adapter; neither adapter interprets the
 * other's printed handle or status syntax. */
struct CettaEvalQuery {
    CettaLibraryContext *library;
    Space *space;
    Registry *registry;
    Arena *persistent;
    Arena storage;
    Atom *handle;
    EvalExecutionContext *execution;
    CettaEvalEpisode episode;
    int fuel;
    bool literal;
    bool finished;
    bool raised;
};

typedef struct {
    Registry *registry;
    Space *root;
    Arena *persistent;
    EvalExecutionContext *execution;
    CettaEvalEpisode *episode;
    Atom *raise;
    PettaDirectiveReport report;
    EvalGcExternalOwnerGuard gc;
    EvalWorkResourceGuard resources;
} EvalQueryScope;

static void eval_query_enter(CettaEvalQuery *query, EvalQueryScope *scope) {
    *scope = (EvalQueryScope){.registry = g_registry, .root = g_eval_root_space,
        .persistent = g_eval_fallback_universe.persistent_arena,
        .execution = g_eval_execution, .episode = g_eval_completion_tracker,
        .raise = petta_eval_take_raise(), .report = g_petta_directive_report};
    g_registry = query->registry;
    g_eval_root_space = query->space;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, query->persistent);
    g_eval_execution = query->execution;
    g_eval_completion_tracker = &query->episode;
    g_petta_directive_report = (PettaDirectiveReport){0};
    eval_gc_external_owner_guard_enter(&scope->gc);
    EvalWork *work = query->handle ? eval_work_lookup(query->handle) : NULL;
    eval_work_resource_enter(work, &scope->resources);
}

static void eval_query_leave(EvalQueryScope *scope) {
    eval_gc_external_owner_guard_leave(&scope->gc);
    eval_work_resource_leave(&scope->resources);
    g_registry = scope->registry;
    g_eval_root_space = scope->root;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, scope->persistent);
    g_eval_execution = scope->execution;
    g_eval_completion_tracker = scope->episode;
    g_petta_pending_raise = scope->raise;
    g_petta_directive_report = scope->report;
}

Atom *eval_query_pattern_variables(Arena *arena, Atom *pattern) {
    MatchVisibleVarSet visible;
    match_visible_var_set_init(&visible);
    bool ok = collect_match_visible_vars_rec(pattern, &visible);
    Atom **variables = visible.len
        ? arena_alloc(arena, sizeof(*variables) * (size_t)visible.len) : NULL;
    if (visible.len && !variables) ok = false;
    for (CettaExprIndex i = 0; ok && i < visible.len; ++i) {
        MatchVisibleVarRef *v = &visible.items[i];
        variables[i] = atom_var_with_presentation(arena, v->spelling, v->name_key, v->var_id);
        if (!variables[i]) ok = false;
    }
    Atom *result = ok ? atom_expr(arena, variables, visible.len) : NULL;
    match_visible_var_set_free(&visible);
    return result;
}

CettaEvalQuery *eval_query_open_exports(Space *space, Arena *persistent, Registry *registry,
                                Atom *source, Atom *variables, int fuel) {
    if (!g_library_context || !space || !source || fuel < -1)
        return NULL;
    CettaEvalQuery *query = calloc(1u, sizeof(*query));
    if (!query) return NULL;
    query->execution = calloc(1u, sizeof(*query->execution));
    if (!query->execution) { free(query); return NULL; }
    *query->execution = (EvalExecutionContext){.root_space = space, .scoped = true};
    query->library = g_library_context;
    query->space = space;
    query->persistent = persistent;
    query->registry = registry;
    query->fuel = fuel;
    query->episode = cetta_eval_episode_begin(NULL, fuel);
    arena_init_detached(&query->storage);
    Atom *owned = atom_deep_copy(&query->storage, source);
    Atom *exports = variables ? atom_deep_copy(&query->storage, variables) : NULL;
    if (!owned || (variables && !exports)) {
        eval_query_close(query);
        return NULL;
    }
    const PettaPlanNode *plan = owned && query->library->petta_program &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA
        ? petta_program_plan_current_in(query->library->petta_program,
              &query->storage, owned) : NULL;
    query->literal = owned && (owned->kind != ATOM_EXPR ||
        owned->expr.len == 0u || atom_is_error(owned));
    EvalQueryScope scope;
    eval_query_enter(query, &scope);
    query->handle = owned ? eval_work_new_with_plan(space, &query->storage,
        owned, fuel, plan, exports) : NULL;
    EvalWork *work = query->handle ? eval_work_lookup(query->handle) : NULL;
    if (work && !query->literal && eval_current_language_id() == CETTA_LANGUAGE_HE) {
        /* HE's complete interpretation policy owns held operands, expected
         * types and final return classification. Subordinate lowerings need
         * their own retained and source-debit-qualified entrance. */
        eval_work_release_machine(work);
        work->canonical = he_retained_new(space, work->source, fuel);
        if (!work->canonical) {
            eval_query_leave(&scope);
            eval_query_close(query);
            return NULL;
        }
    }
    eval_query_leave(&scope);
    if (!query->handle) { eval_query_close(query); return NULL; }
    return query;
}

CettaEvalQuery *eval_query_open(Space *space, Arena *persistent, Registry *registry,
                                Atom *source, int fuel) {
    return eval_query_open_exports(space, persistent, registry, source, NULL, fuel);
}

bool eval_query_has_retained_driver(const CettaEvalQuery *query) {
    if (!query || query->library != g_library_context) return false;
    EvalWork *work = eval_work_lookup(query->handle);
    return work && (query->literal || work->route == EVAL_WORK_ROUTE_GENERAL || work->canonical);
}

static void eval_query_observe_state(const CettaEvalQuery *query, EvalOutcome *outcome) {
    outcome->completion = query->episode.completion;
    outcome->petta_raised_error = query->raised;
    outcome->budget_limited = query->episode.budget_limited;
    outcome->budget_initial = query->episode.budget_initial;
    outcome->budget_remaining = query->episode.budget_remaining;
    outcome->steps_spent = query->episode.steps_spent;
}

static void eval_query_release_work(CettaEvalQuery *query) {
    uint64_t id = 0u;
    if (cetta_native_handle_arg(query->handle, eval_work_kind, &id))
        (void)cetta_native_handle_close(query->library, eval_work_kind, id);
}

static bool eval_query_capture_packet(void *context, Arena *arena,
                                      const CettaOwnedReturn *returned) {
    CettaEvalQueryAnswer *packet = context;
    packet->kind = returned->outcome.kind;
    packet->bindings = bindings_capture_value(arena, returned->environment);
    packet->delayed = returned->outcome.delayed;
    return packet->bindings != NULL;
}

CettaEvalQueryStep eval_query_step(CettaEvalQuery *query, Arena *arena,
    uint64_t allowance, EvalOutcome *outcome, CettaEvalQueryAnswer *packet,
    uint64_t *transitions) {
    if (transitions) *transitions = 0u;
    if (packet) *packet = (CettaEvalQueryAnswer){0};
    if (!query || !arena || !outcome || query->library != g_library_context) {
        if (outcome) outcome->completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        return CETTA_QUERY_STOPPED;
    }
    if (query->finished) {
        eval_query_observe_state(query, outcome);
        return outcome->completion == CETTA_EVAL_COMPLETE && !query->raised
            ? CETTA_QUERY_EXHAUSTED : CETTA_QUERY_STOPPED;
    }
    EvalWork *work = eval_work_lookup(query->handle);
    if (!work) {
        query->finished = true;
        query->episode.completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
        eval_query_observe_state(query, outcome);
        return CETTA_QUERY_STOPPED;
    }
    EvalQueryScope scope;
    eval_query_enter(query, &scope);
    if (eval_cancel_check()) query->finished = true;
    if (query->finished || !allowance ||
        (!query->literal && work->route != EVAL_WORK_ROUTE_GENERAL && !work->canonical)) {
        eval_query_observe_state(query, outcome);
        if (query->finished) eval_query_release_work(query);
        eval_query_leave(&scope);
        return query->finished ? CETTA_QUERY_STOPPED
            : !allowance ? CETTA_QUERY_PAUSED : CETTA_QUERY_DRIVER_PENDING;
    }
    uint64_t before = work->transitions;
    if (query->literal) {
        metta_eval_outcome(query->space, arena, NULL, work->source, query->fuel, outcome);
        if (packet && outcome->results.len) {
            Bindings empty;
            bindings_init(&empty);
            packet->bindings = bindings_capture_value(arena, &empty);
            packet->kind = CETTA_CALL_VALUE;
        }
        query->finished = true;
        if (transitions) *transitions = 1u;
    } else if (work->canonical) {
        CettaOwnedExecutionStep step = he_retained_next(work->canonical, arena,
            allowance, outcome, packet, transitions);
        query->raised |= outcome->petta_raised_error;
        if (step == CETTA_OWNED_EXECUTION_COMPLETE || step == CETTA_OWNED_EXECUTION_INTERRUPTED)
            query->finished = true;
    } else {
        Atom **values = NULL;
        size_t count = 0u, capacity = 0u;
        work->budget = allowance;
        work->busy = true;
        work->deliver_packet = packet ? eval_query_capture_packet : NULL;
        work->delivery_context = packet;
        if (work->state == EVAL_WORK_FRESH) work->state = EVAL_WORK_LIVE;
        /* Interpretation has entered. A declined residual is reported;
         * restarting its source through the whole-frontier fallback could
         * replay an already committed prefix. */
        eval_work_take_general(work, arena, 1u, &values, &count, &capacity);
        work->deliver_packet = NULL;
        work->delivery_context = NULL;
        work->busy = false;
        work->taken += count;
        for (size_t i = 0u; i < count; ++i) result_set_add(&outcome->results, values[i]);
        free(values);
        outcome->petta_raise_known = work->language == CETTA_LANGUAGE_PETTA;
        query->raised = work->raised_failure;
        if (transitions) *transitions = work->transitions - before;
        if (work->state == EVAL_WORK_FAILED) {
            if (!work->raised_failure)
                cetta_eval_completion_record(&query->episode.completion,
                    work->failure_completion != CETTA_EVAL_COMPLETE
                        ? work->failure_completion : CETTA_EVAL_INCOMPLETE_CAPACITY);
            query->finished = true;
        } else if (work->state == EVAL_WORK_COMPLETE) query->finished = true;
    }
    if (query->episode.completion != CETTA_EVAL_COMPLETE) query->finished = true;
    eval_query_observe_state(query, outcome);
    eval_execution_publish_results(arena, &outcome->results, 0u, false);
    if (query->finished) eval_query_release_work(query);
    eval_query_leave(&scope);
    return outcome->results.len || outcome->petta_raised_error ? CETTA_QUERY_ANSWER
        : outcome->completion != CETTA_EVAL_COMPLETE ? CETTA_QUERY_STOPPED
        : query->finished ? CETTA_QUERY_EXHAUSTED : CETTA_QUERY_PAUSED;
}

void eval_query_close(CettaEvalQuery *query) {
    if (!query) return;
    assert(query->library == g_library_context);
    EvalQueryScope scope;
    eval_query_enter(query, &scope);
    eval_query_release_work(query);
    eval_execution_clear_current();
    eval_query_leave(&scope);
    arena_free(&query->storage);
    free(query->execution);
    free(query);
}
