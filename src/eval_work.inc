/* Retained work: a computation a program holds by handle, as its residual.
 *
 * (work-new results) answers a handle on the computation of `results`, not
 * yet advanced: a native handle of kind "work", which the arenas holding
 * its atoms own, so the last holder's release, or an explicit cancel,
 * releases the residual.  (work-take h k) advances it until k more
 * answers, its completion or its budget, and answers
 * (taken (a1 ... aj) state), the state `more-possible`, `complete` or
 * `paused`; the rest stays in the handle.  (work-budget h n) lets each
 * later take make at most n transitions before it pauses at an answer
 * boundary (0: no bound).  (work-cancel h) releases the residual; effects
 * already made stay.  (work-route h r) chooses the route: for a work not
 * yet advanced, depth-first, fifo or ratio on the general machine, the
 * latter two scheduling its continuations by the hub's selection
 * automaton, or completed; for a work advanced depth first, fifo or ratio,
 * which moves its residual into the hub as one owned continuation where
 * the machine's state can be captured.  (work-record h) describes the
 * routes the work has run on and the residual each transfer carried.
 *
 * A take observes a prefix and commits nothing, unlike select: the answers
 * after it are the residual's, delivered by the next take in the order the
 * computation produces them.  A source the general machine admits runs
 * there, one answer per step, its state kept between takes (route
 * `general`); any other source is evaluated whole by its first take, and its
 * answers are read in turn (route `completed`). */

#include "relational_cursor.h"

typedef enum {
    EVAL_WORK_FRESH = 0,
    EVAL_WORK_LIVE,
    EVAL_WORK_COMPLETE,
    EVAL_WORK_FAILED,
} EvalWorkState;

typedef enum {
    EVAL_WORK_ROUTE_PENDING = 0,
    EVAL_WORK_ROUTE_GENERAL,
    EVAL_WORK_ROUTE_COMPLETED,
    EVAL_WORK_ROUTE_COUNTED,
} EvalWorkRoute;

/* How the general machine orders a work's alternatives: depth first, or
 * by a selection automaton over its owned continuations (the hub). */
typedef enum {
    EVAL_WORK_DEPTH_FIRST = 0,
    EVAL_WORK_FIFO,
    EVAL_WORK_RATIO,
} EvalWorkStrategy;

static const char *const eval_work_strategy_names[] = {
    "depth-first", "fifo", "ratio",
};

/* A residual moved between the routes of a work after it produced
 * answers: the routes, the answers and transitions before the move, and
 * the owned continuation's storage, whole and by component (authority,
 * terms, bindings, control, obligations, readout). */
typedef struct {
    EvalWorkStrategy from, to;
    uint64_t answers_before;
    uint64_t transitions_before;
    size_t shared_bytes, exclusive_bytes;
    size_t component_bytes[CETTA_CONTINUATION_COMPONENT_COUNT];
} EvalWorkTransfer;

static const char *const eval_work_component_names[
    CETTA_CONTINUATION_COMPONENT_COUNT] = {
    [CETTA_CONTINUATION_COMPONENT_AUTHORITY] = "authority",
    [CETTA_CONTINUATION_COMPONENT_TERMS] = "terms",
    [CETTA_CONTINUATION_COMPONENT_BINDINGS] = "bindings",
    [CETTA_CONTINUATION_COMPONENT_CONTROL] = "control",
    [CETTA_CONTINUATION_COMPONENT_OBLIGATIONS] = "obligations",
    [CETTA_CONTINUATION_COMPONENT_READOUT] = "readout",
};

typedef struct {
    uint64_t id;
    EvalWorkState state;
    EvalWorkRoute route;
    CettaLanguageId language;
    Space *space;
    /* The space's revision when the work was made: the read view its
     * footprint names. */
    uint64_t space_revision;
    int fuel;
    /* The work's own atoms: its source, and a completed route's answers. */
    Arena storage;
    /* The general machine publishes answers here; a take copies them out
     * and resets it. */
    Arena answers;
    Atom *source;
    PettaEvalMachineContext *context;
    PettaMachineHost host;
    /* The language's portable relational contract (HE). */
    bool portable;
    PettaMachine machine;
    bool machine_ready;
    HeSharedExecutionPlan he_plan;
    EvalWorkStrategy strategy;
    /* Under fifo or ratio: the work's owned continuations, and whether the
     * machine runs one of them now. */
    CettaContinuationHub hub;
    bool hub_ready;
    bool controller_live;
    EvalWorkTransfer *transfers;
    size_t transfer_len, transfer_cap;
    Atom **completed;
    size_t completed_len, completed_next;
    CettaEvalCompletion completed_stop;
    /* Transitions a take may make (0: no bound). */
    uint64_t budget;
    uint64_t transitions;
    uint64_t taken;
    uint64_t takes;
    uint64_t pauses;
    /* What ended a failed work, and whether its machine declined it. */
    Atom *failure;
    bool declined;
    bool busy;
    /* Peeking advances the producer once but holds its next answer here.
     * Counting uses the same producer with a scalar accumulator. */
    Atom *pending;
    Arena pending_storage;
    ArenaMark pending_mark;
    bool counting;
    uint64_t counted;
    CettaRelationalPlan *count_plan;
    CettaRelationalCountCursor *count_cursor;
    uint64_t count_transitions;
    const char *observer;
    const char *observer_law;
} EvalWork;

static const char eval_work_kind[] = "work";

/* The general machine's state: its machine, hub and context. */
static void eval_work_release_machine(EvalWork *work) {
    if (work->hub_ready)
        cetta_continuation_hub_destroy(&work->hub);
    work->hub_ready = false;
    work->controller_live = false;
    if (work->machine_ready)
        petta_machine_destroy(&work->machine);
    work->machine_ready = false;
    if (work->context)
        prepared_pure_program_cache_free(&work->context->prepared_pure_cache);
    free(work->context);
    work->context = NULL;
    if (work->route == EVAL_WORK_ROUTE_GENERAL)
        work->route = EVAL_WORK_ROUTE_PENDING;
}

static void eval_work_release_count(EvalWork *work) {
    cetta_relational_count_cursor_free(work->count_cursor);
    cetta_relational_plan_free(work->count_plan);
    work->count_cursor = NULL;
    work->count_plan = NULL;
}

static void eval_work_release(EvalWork *work) {
    eval_work_release_count(work);
    eval_work_release_machine(work);
    free(work->transfers);
    work->transfers = NULL;
    work->transfer_len = work->transfer_cap = 0u;
    free(work->completed);
    work->completed = NULL;
    work->completed_len = work->completed_next = 0u;
    arena_free(&work->pending_storage);
    arena_free(&work->answers);
    arena_free(&work->storage);
}

static void eval_work_free(void *resource) {
    EvalWork *work = resource;
    if (!work)
        return;
    eval_work_release(work);
    free(work);
}

/* The work a handle names, or NULL once it is released. */
static EvalWork *eval_work_lookup(Atom *handle) {
    uint64_t id = 0u;
    if (!g_library_context ||
        !cetta_native_handle_arg(handle, eval_work_kind, &id))
        return NULL;
    return cetta_native_handle_get(g_library_context, eval_work_kind, id);
}

static Atom *eval_work_handle(Arena *a, const EvalWork *work) {
    return cetta_native_handle_owned_atom(g_library_context, a,
                                          eval_work_kind, work->id);
}

/* The general machine's match semantics for this work's language. */
static CettaMatchDecisionSemanticIdentity eval_work_match_semantics(
    const EvalWork *work) {
    return petta_eval_machine_match_semantics(
        g_library_context, work->language == CETTA_LANGUAGE_HE);
}

/* Admit the work's source to the general machine and build the machine,
 * as the dialect's own evaluation would run it there; false leaves the
 * work for the completed route.  A typed-checking profile and Prime keep
 * their own drivers.  A work is a boundary of its own, not the leaf of a
 * running machine: its computation is admitted, and later run, with machine
 * admission reopened, as an aggregate's branch is
 * (eval_petta_relational_host_branch). */
static bool eval_work_start_general_reopened(EvalWork *work, Arena *scratch,
                                             EvalWorkStrategy strategy);

static bool eval_work_start_general(EvalWork *work, Arena *scratch,
                                    EvalWorkStrategy strategy) {
    uint32_t enclosing_depth = g_petta_machine_host_depth;
    g_petta_machine_host_depth = 0u;
    bool started = eval_work_start_general_reopened(work, scratch, strategy);
    g_petta_machine_host_depth = enclosing_depth;
    return started;
}

/* A selection automaton runs over owned continuations: the root's relation
 * must admit owned multishot capture, and the automaton must revisit its
 * oldest lane (the frontier controller's admission). */
static bool eval_work_frontier_admitted(EvalWork *work,
                                        EvalWorkStrategy strategy,
                                        CettaSelectionAutomaton *automaton) {
    bool built = strategy == EVAL_WORK_FIFO
        ? cetta_selection_automaton_fifo(automaton)
        : cetta_selection_automaton_ratio(8u, automaton);
    return built && cetta_selection_automaton_has_recurrent_oldest(automaton) &&
        petta_eval_machine_owned_multishot_admits_root(work->space,
                                                       work->source);
}

static bool eval_work_start_general_reopened(EvalWork *work, Arena *scratch,
                                             EvalWorkStrategy strategy) {
    Space *space = work->space;
    Atom *source = work->source;
    bool frontier = strategy != EVAL_WORK_DEPTH_FIRST;
    if (space_has_dependencies(space) ||
        work->language == CETTA_LANGUAGE_PRIME)
        return false;
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    if (cetta_petta_profile_admits_native_typecheck_v2())
        return false;
#endif
    bool he = work->language == CETTA_LANGUAGE_HE;
    if (he) {
        if (!he_shared_execution_candidate(space, source, NULL,
                                           work->fuel,
                                           g_prime_need_rule_use_observed))
            return false;
        CETTA_SCOPED_SHARED_TRANSITION(work_admission);
        if (!he_shared_execution_admit(space, scratch, source, false,
                                       &work->he_plan))
            return false;
        if (!g_library_context->petta_program)
            g_library_context->petta_program = petta_program_new_with_rules(
                eval_admitted_rule_forms());
        if (!g_library_context->petta_program ||
            !petta_program_synchronize_space(
                g_library_context->petta_program, space))
            return false;
    } else {
        PettaRelationSafety safety = PETTA_RELATION_SAFETY_UNSAFE;
        if (!petta_eval_machine_admits_root(
                space, scratch, source,
                atom_symbol_id(scratch, g_builtin_syms.undefined_type),
                work->fuel, NULL, &safety))
            return false;
    }
    CettaSelectionAutomaton automaton;
    if (frontier && !eval_work_frontier_admitted(work, strategy, &automaton))
        return false;
    CettaObservationContract observation = {
        .demand = petta_eval_machine_root_observation(source),
        .algebra = CETTA_OBSERVATION_ALGEBRA_EXACT_OCCURRENCES,
    };
    CettaControlPlan control_plan;
    if (!cetta_control_plan_derive(observation.demand,
                                   CETTA_CONTROL_BRANCH_GENERAL,
                                   CETTA_CONTROL_BATCH_SINGLETON_ONLY,
                                   &control_plan))
        return false;
    work->context = calloc(1u, sizeof(*work->context));
    if (!work->context)
        return false;
    *work->context = (PettaEvalMachineContext){
        .fuel = work->fuel,
        .data_ops = cetta_petta_profile_admits_typecheck_ops(),
        .controller_policy = frontier
            ? (strategy == EVAL_WORK_FIFO ? CETTA_SEARCH_CONTROLLER_FIFO
                                          : CETTA_SEARCH_CONTROLLER_RATIO)
            : CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST,
        .controller_admitted = frontier,
        .controller_measure_representation =
            petta_eval_machine_stats_enabled(),
        /* Every transition is counted against the take's quantum. */
        .controller_quantum_active = true,
        .controller_quantum_limit = UINT64_MAX,
        .observation_contract = observation,
        .control_plan = control_plan,
        .semantic_authority_epoch = 1u,
        .library_context = g_library_context,
        .program_projection = g_petta_program_revision_projection,
        .prepared_pure_cache = {.root_space = space},
    };
    bool portable = cetta_language_portable_relational_contract(
        work->language) ==
        CETTA_PORTABLE_RELATIONAL_EAGER_STRUCTURAL_EQUATIONS_V1;
    bool source_output_constraints = !portable &&
        petta_eval_machine_source_output_constraints_required(space, source);
    work->portable = portable;
    /* The host takes HE's rules from the shared execution's flags, as the
     * HE driver sets them around its machine. */
    bool enclosing_shared = g_he_shared_execution;
    bool enclosing_effect_free = g_he_shared_execution_effect_free;
    bool enclosing_rereads = g_he_shared_execution_rereads;
    g_he_shared_execution = he;
    if (he) {
        g_he_shared_execution_effect_free = !work->he_plan.effects;
        g_he_shared_execution_rereads = work->he_plan.rereads;
    }
    work->host = petta_eval_machine_host(
        work->context, work->fuel, false, portable, frontier,
        source_output_constraints, false, eval_work_match_semantics(work));
    work->host.match_decisions = petta_eval_session_decisions(
        g_library_context, space);
    work->host.export_variables = NULL;
    /* A work is metered: every transition counts against its take. */
    work->host.permit_transition =
        petta_eval_machine_permit_controller_transition;
    work->host.unlimited_transition_budget = false;
    bool initialized = petta_machine_init(&work->machine, space,
                                          &work->answers, source, NULL,
                                          &work->host);
    g_he_shared_execution = enclosing_shared;
    g_he_shared_execution_effect_free = enclosing_effect_free;
    g_he_shared_execution_rereads = enclosing_rereads;
    if (!initialized) {
        free(work->context);
        work->context = NULL;
        return false;
    }
    work->machine_ready = true;
    if (frontier) {
        if (!cetta_continuation_hub_init(&work->hub, &automaton,
                                         CETTA_SELECTION_DUTY_RECURRENT_OLDEST,
                                         &work->context->control_plan)) {
            eval_work_release_machine(work);
            return false;
        }
        work->hub_ready = true;
        /* The root runs first, as the frontier controller's does. */
        work->controller_live = true;
    }
    work->strategy = strategy;
    work->route = EVAL_WORK_ROUTE_GENERAL;
    return true;
}

/* The completed route: the source evaluated whole by the dialect's own
 * evaluator, its answers kept in the work's storage. */
static void eval_work_complete_now(EvalWork *work, Space *space, Arena *a) {
    if (work->budget) {
        /* This method has no retained instruction-level residual. Refusing
         * it is an unresolved preparation obligation, not impossibility of
         * a resumable interpretation and not exhausted zero-answer search. */
        work->failure = atom_symbol(&work->storage, "WorkBudgetRequiresResumableRoute");
        work->state = EVAL_WORK_FAILED;
        return;
    }
    EvalOutcome observation;
    eval_outcome_init(&observation);
    metta_eval_outcome(space, a, NULL, work->source, work->fuel, &observation);
    work->completed_stop = observation.completion;
    work->completed = observation.results.len
        ? calloc(observation.results.len, sizeof(*work->completed)) : NULL;
    if (observation.results.len && !work->completed) {
        work->failure = atom_symbol(&work->storage, "Capacity");
        work->state = EVAL_WORK_FAILED;
        eval_outcome_free(&observation);
        return;
    }
    for (size_t index = 0u; index < observation.results.len; index++) {
        Atom *kept = atom_deep_copy(&work->storage, observation.results.items[index]);
        if (kept)
            work->completed[work->completed_len++] = kept;
        else work->completed_stop = CETTA_EVAL_INCOMPLETE_CAPACITY;
    }
    eval_outcome_free(&observation);
    work->route = EVAL_WORK_ROUTE_COMPLETED;
}

/* Copy an answer of the general machine out to the taker's arena. */
static bool eval_work_deliver(EvalWork *work, Arena *a, Atom *answer,
                              Bindings *environment, Atom ***out,
                              size_t *out_len, size_t *out_cap) {
    if (work->counting && !petta_machine_last_answer_raised(&work->machine)) {
        bindings_free(environment);
        work->counted++;
        return true;
    }
    if (!work->portable)
        answer = petta_flatten_closed_open_cons(&work->answers, answer);
    Atom *value = environment->len
        ? bindings_apply(environment, a, answer) : answer;
    value = value ? atom_deep_copy(a, value) : NULL;
    bindings_free(environment);
    if (!value) {
        work->state = EVAL_WORK_FAILED;
        work->failure = atom_symbol(&work->storage, "WorkCapacity");
        return false;
    }
    if (work->counting) {
        work->counted++;
    } else {
        if (*out_len == *out_cap) {
            *out_cap = *out_cap ? *out_cap * 2u : 8u;
            *out = cetta_realloc(*out, *out_cap * sizeof(**out));
        }
        (*out)[(*out_len)++] = value;
    }
    /* A raised error ends the run it is raised in. */
    if (petta_machine_last_answer_raised(&work->machine)) {
        work->state = EVAL_WORK_FAILED;
        work->failure = atom_deep_copy(&work->storage, value);
        return false;
    }
    return true;
}

static void eval_work_fail(EvalWork *work, PettaMachineStep step) {
    work->state = EVAL_WORK_FAILED;
    work->declined = step == PETTA_MACHINE_STEP_DECLINED;
    work->failure = atom_symbol(&work->storage,
        step == PETTA_MACHINE_STEP_DECLINED ? "WorkDeclined"
        : step == PETTA_MACHINE_STEP_INVALIDATED ? "WorkInvalidated"
        : step == PETTA_MACHINE_STEP_STACK ? "StackOverflow"
        : step == PETTA_MACHINE_STEP_HOST_ERROR ? "WorkHostError"
        : "WorkCapacity");
}

/* Keep the live machine state as one more continuation of the hub. */
static bool eval_work_park(EvalWork *work, CettaContinuationMachine endpoint) {
    CettaOwnedContinuation remainder;
    cetta_owned_continuation_init(&remainder);
    CettaContinuationStatus captured = petta_eval_controller_capture(
        work->context, endpoint, &remainder);
    if (captured == CETTA_CONTINUATION_READY &&
        cetta_continuation_hub_append(&work->hub, &remainder)) {
        work->controller_live = false;
        return true;
    }
    cetta_owned_continuation_destroy(&remainder);
    eval_work_fail(work, captured == CETTA_CONTINUATION_READY
        ? PETTA_MACHINE_STEP_CAPACITY
        : petta_eval_controller_status_step(captured));
    if (captured == CETTA_CONTINUATION_DEFERRED ||
        captured == CETTA_CONTINUATION_UNSUPPORTED)
        work->failure = atom_symbol(&work->storage,
                                    "SearchControllerAdmissionRefused");
    return false;
}

/* One take under a selection automaton: the frontier controller's loop.
 * Each round runs one transition of the live continuation; an answer
 * parks the rest of it, an expired round expands its choice into the
 * hub, and an exhausted continuation leaves the next one to the
 * automaton.  The take's budget counts the transitions of all its rounds. */
static void eval_work_take_frontier(EvalWork *work, Arena *a, uint64_t limit,
                                    Atom ***out, size_t *out_len,
                                    size_t *out_cap) {
    PettaEvalMachineContext *context = work->context;
    CettaContinuationMachine endpoint =
        petta_machine_continuation_machine(&work->machine);
    uint64_t spent = 0u;
    uint64_t taken = 0u;
    while (taken < limit) {
        if (work->budget && spent >= work->budget) {
            work->pauses++;
            break;
        }
        if (!work->controller_live) {
            size_t pending = cetta_continuation_hub_length(&work->hub);
            CettaSelectionLane lane = CETTA_SELECTION_LANE_OLDEST;
            size_t selected = 0u;
            if (pending != 0u &&
                !cetta_continuation_hub_select(&work->hub, &lane, &selected))
                selected = 0u;
            CettaOwnedContinuation next;
            cetta_owned_continuation_init(&next);
            if (pending == 0u ||
                !cetta_continuation_hub_take(&work->hub, selected, &next)) {
                work->state = EVAL_WORK_COMPLETE;
                break;
            }
            CettaContinuationStatus restored = petta_eval_controller_restore(
                context, endpoint, &next);
            if (restored != CETTA_CONTINUATION_READY) {
                cetta_owned_continuation_destroy(&next);
                eval_work_fail(work, petta_eval_controller_status_step(restored));
                break;
            }
            work->controller_live = true;
        }
        context->controller_quantum_active = true;
        context->controller_quantum_expired = false;
        context->controller_quantum_transitions = 0u;
        context->controller_quantum_limit = 1u;
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step =
            petta_machine_next(&work->machine, &answer, &environment);
        spent += context->controller_quantum_transitions;
        work->transitions += context->controller_quantum_transitions;
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            if (!eval_work_deliver(work, a, answer, &environment, out,
                                   out_len, out_cap))
                break;
            taken++;
            if (cetta_continuation_hub_length(&work->hub) != 0u &&
                !eval_work_park(work, endpoint))
                break;
            continue;
        }
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
            work->controller_live = false;
            continue;
        }
        if (step == PETTA_MACHINE_STEP_SUSPENDED &&
            (context->controller_quantum_expired ||
             petta_machine_external_branch_ready(&work->machine))) {
            CettaContinuationBatch successors;
            cetta_continuation_batch_init(&successors);
            CettaContinuationStatus expanded = petta_eval_controller_expand(
                context, endpoint, &successors);
            if (expanded == CETTA_CONTINUATION_READY) {
                if (!cetta_continuation_hub_append_batch(&work->hub,
                                                         &successors)) {
                    cetta_continuation_batch_destroy(&successors);
                    eval_work_fail(work, PETTA_MACHINE_STEP_CAPACITY);
                    break;
                }
                work->controller_live = false;
                continue;
            }
            cetta_continuation_batch_destroy(&successors);
            if (expanded == CETTA_CONTINUATION_INVALIDATED ||
                expanded == CETTA_CONTINUATION_CAPACITY) {
                eval_work_fail(work, petta_eval_controller_status_step(expanded));
                break;
            }
            /* Nothing to branch: the continuation keeps running unless
             * others wait their turn. */
            if (cetta_continuation_hub_length(&work->hub) == 0u)
                continue;
            if (!eval_work_park(work, endpoint))
                break;
            continue;
        }
        if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            work->pauses++;
            break;
        }
        eval_work_fail(work, step);
        break;
    }
}

/* One take depth first: the machine's next answers, until `limit`, its
 * exhaustion or the take's budget. */
static void eval_work_take_depth_first(EvalWork *work, Arena *a,
                                       uint64_t limit, Atom ***out,
                                       size_t *out_len, size_t *out_cap) {
    work->context->controller_quantum_transitions = 0u;
    work->context->controller_quantum_expired = false;
    work->context->controller_quantum_limit =
        work->budget ? work->budget : UINT64_MAX;
    uint64_t taken = 0u;
    while (taken < limit) {
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step =
            petta_machine_next(&work->machine, &answer, &environment);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            if (!eval_work_deliver(work, a, answer, &environment, out,
                                   out_len, out_cap))
                break;
            taken++;
            continue;
        }
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
            work->state = EVAL_WORK_COMPLETE;
        } else if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            /* The budget, or a cancellation or interrupt of the
             * evaluation taking it: the machine resumes where it
             * stopped. */
            work->pauses++;
        } else {
            eval_work_fail(work, step);
        }
        break;
    }
    work->transitions += work->context->controller_quantum_transitions;
}

/* The flags the dialect's own driver sets around its machine, saved for
 * the enclosing evaluation. */
typedef struct {
    bool shared, effect_free, rereads, active, raised;
    uint32_t depth;
} EvalWorkScope;

static EvalWorkScope eval_work_enter(EvalWork *work) {
    EvalWorkScope saved = {
        .shared = g_he_shared_execution,
        .effect_free = g_he_shared_execution_effect_free,
        .rereads = g_he_shared_execution_rereads,
        .active = g_active_petta_search_machine,
        .raised = g_petta_machine_run_raised,
        .depth = g_petta_machine_host_depth,
    };
    bool he = work->language == CETTA_LANGUAGE_HE;
    g_petta_machine_host_depth = 0u;
    g_he_shared_execution = he;
    if (he) {
        g_he_shared_execution_effect_free = !work->he_plan.effects;
        g_he_shared_execution_rereads = work->he_plan.rereads;
        if (g_library_context->petta_program)
            (void)petta_program_synchronize_space(
                g_library_context->petta_program, work->space);
    }
    g_active_petta_search_machine = true;
    g_petta_machine_run_raised = false;
    return saved;
}

static void eval_work_leave(const EvalWorkScope *saved) {
    g_he_shared_execution = saved->shared;
    g_he_shared_execution_effect_free = saved->effect_free;
    g_he_shared_execution_rereads = saved->rereads;
    g_active_petta_search_machine = saved->active;
    g_petta_machine_run_raised = saved->raised;
    g_petta_machine_host_depth = saved->depth;
}

/* One take on the general machine: up to `limit` answers into `out`,
 * copied into `a`. */
static void eval_work_take_general(EvalWork *work, Arena *a, uint64_t limit,
                                   Atom ***out, size_t *out_len,
                                   size_t *out_cap) {
    EvalWorkScope saved = eval_work_enter(work);
    ArenaMark mark = arena_mark(&work->answers);
    if (work->hub_ready)
        eval_work_take_frontier(work, a, limit, out, out_len, out_cap);
    else
        eval_work_take_depth_first(work, a, limit, out, out_len, out_cap);
    arena_reset(&work->answers, mark);
    eval_work_leave(&saved);
}

static Atom *eval_work_state_atom(Arena *a, const EvalWork *work,
                                  bool paused) {
    switch (work->state) {
    case EVAL_WORK_COMPLETE:
        return atom_symbol(a, "complete");
    case EVAL_WORK_FAILED:
        return work->failure
            ? atom_expr2(a, atom_symbol(a, "failed"), work->failure)
            : atom_symbol(a, "failed");
    default:
        return atom_symbol(a, paused ? "paused" : "more-possible");
    }
}

/* Advance one residual. The observer decides whether to keep values or
 * just count them; both remove precisely the same answer occurrences. */
static void eval_work_advance(Space *space, Arena *a, EvalWork *work,
                              uint64_t limit, Atom ***values, size_t *len,
                              size_t *cap) {
    if (limit == 0u)
        return;
    uint64_t pending = work->pending ? 1u : 0u;
    if (work->pending) {
        if (work->counting) {
            work->counted++;
        } else {
            if (*len == *cap) {
                *cap = *cap ? *cap * 2u : 8u;
                *values = cetta_realloc(*values, *cap * sizeof(**values));
            }
            Atom *value = atom_deep_copy(a, work->pending);
            if (!value) {
                work->state = EVAL_WORK_FAILED;
                work->failure = atom_symbol(&work->storage, "WorkCapacity");
                return;
            }
            (*values)[(*len)++] = value;
        }
        work->pending = NULL;
        arena_reset(&work->pending_storage, work->pending_mark);
    }
    if (pending == limit)
        return;
    if (work->state == EVAL_WORK_FRESH) {
        work->state = EVAL_WORK_LIVE;
        if (work->route == EVAL_WORK_ROUTE_PENDING)
            eval_work_complete_now(work, space, a);
    }
    if (work->state != EVAL_WORK_LIVE)
        return;
    if (work->route == EVAL_WORK_ROUTE_GENERAL) {
        eval_work_take_general(work, a, limit - pending, values, len, cap);
        if (work->state == EVAL_WORK_FAILED && work->taken == 0u &&
            *len == 0u && work->counted == 0u && work->declined) {
            petta_machine_destroy(&work->machine);
            work->machine_ready = false;
            work->state = EVAL_WORK_LIVE;
            work->failure = NULL;
            eval_work_complete_now(work, space, a);
        }
    }
    if (work->route == EVAL_WORK_ROUTE_COMPLETED &&
        work->state == EVAL_WORK_LIVE) {
        while ((work->counting ? work->counted : *len) < limit &&
               work->completed_next < work->completed_len) {
            if (work->counting) {
                work->counted++;
                work->completed_next++;
            } else {
                if (*len == *cap) {
                    *cap = *cap ? *cap * 2u : 8u;
                    *values = cetta_realloc(*values, *cap * sizeof(**values));
                }
                (*values)[(*len)++] = atom_deep_copy(
                    a, work->completed[work->completed_next++]);
            }
        }
        if (work->completed_next == work->completed_len) {
            if (work->completed_stop == CETTA_EVAL_COMPLETE)
                work->state = EVAL_WORK_COMPLETE;
            else {
                work->failure = atom_expr2(&work->storage,
                    atom_symbol(&work->storage, "Incomplete"),
                    atom_symbol(&work->storage,
                        eval_completion_reason(work->completed_stop)));
                work->state = EVAL_WORK_FAILED;
            }
        }
    }
}

/* (work-take h k) */
static Atom *eval_work_take(Space *space, Arena *a, Atom *call,
                            EvalWork *work, int64_t k) {
    Atom **values = NULL;
    size_t len = 0u, cap = 0u;
    uint64_t pauses_before = work->pauses;
    (void)call;
    work->takes++;
    work->observer = "prefix";
    work->observer_law = "next-answer";
    if (k > 0)
        eval_work_release_count(work);
    eval_work_advance(space, a, work, (uint64_t)k, &values, &len, &cap);
    work->taken += len;
    Atom *prefix = atom_expr(a, values, (CettaExprLen)len);
    free(values);
    Atom *parts[3] = {
        atom_symbol(a, "taken"), prefix,
        eval_work_state_atom(a, work, work->pauses != pauses_before),
    };
    return atom_expr(a, parts, 3u);
}

/* A constant scalar template produces one answer per matching row in both
 * dialects. In particular, neither an open template nor a callable tree is
 * admitted by this law. The general producer interprets those templates. */
static bool eval_work_factor_count_source(EvalWork *work,
        Atom *const **patterns, size_t *pattern_count) {
    Atom *source = work->source;
    if (source->kind != ATOM_EXPR || source->expr.len != 4u ||
        !atom_is_symbol_id(source->expr.elems[0], g_builtin_syms.match))
        return false;
    Atom *ref = source->expr.elems[1];
    bool own = atom_is_symbol_id(ref, g_builtin_syms.self) ||
        (ref->kind == ATOM_GROUNDED && ref->ground.gkind == GV_SPACE &&
         ref->ground.ptr == work->space);
    Atom *template = source->expr.elems[3];
    if (!own || template->kind != ATOM_GROUNDED ||
        (template->ground.gkind != GV_INT && template->ground.gkind != GV_FLOAT &&
         template->ground.gkind != GV_BOOL && template->ground.gkind != GV_STRING))
        return false;
    Atom *pattern = source->expr.elems[2];
    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 2u &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        *patterns = pattern->expr.elems + 1u;
        *pattern_count = pattern->expr.len - 1u;
    } else {
        *patterns = &source->expr.elems[2];
        *pattern_count = 1u;
    }
    return true;
}

static bool eval_work_count_factors(EvalWork *work, uint64_t *count) {
    if (work->state != EVAL_WORK_FRESH || work->pending)
        return false;
    if (!work->count_cursor) {
        Atom *const *patterns = NULL;
        size_t pattern_count = 0u;
        if (!eval_work_factor_count_source(work, &patterns, &pattern_count))
            return false;
        Arena scratch;
        arena_init(&scratch);
        bool opened = cetta_relational_plan_prepare(
            work->space, &scratch, patterns, pattern_count, NULL,
            CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &work->count_plan) &&
            cetta_relational_count_cursor_open(work->count_plan, work->space,
                                               &work->count_cursor);
        arena_free(&scratch);
        if (!opened) {
            eval_work_release_count(work);
            return false;
        }
    }
    uint64_t used = 0u;
    CettaRelationalCountStatus status = cetta_relational_count_cursor_advance(
        work->count_cursor, work->space,
        work->budget ? work->budget : UINT64_MAX, &used);
    work->count_transitions += used;
    *count = 0u;
    if (status == CETTA_RELATIONAL_COUNT_PAUSED) {
        work->pauses++;
        return true;
    }
    if (status == CETTA_RELATIONAL_COUNT_COMPLETE &&
        cetta_relational_count_cursor_result(work->count_cursor, work->space,
                                             count, NULL) &&
        *count <= INT64_MAX) {
        /* Publication consumes the whole counted suffix, just as the
         * enumerating observer does. No template result is materialized. */
        work->state = EVAL_WORK_COMPLETE;
        eval_work_release_machine(work);
    } else {
        *count = 0u;
        work->state = EVAL_WORK_FAILED;
        work->failure = atom_symbol(&work->storage,
            status == CETTA_RELATIONAL_COUNT_INVALIDATED ? "WorkInvalidated"
                                                        : "WorkCountOverflow");
    }
    eval_work_release_count(work);
    eval_work_release_machine(work);
    work->route = EVAL_WORK_ROUTE_COUNTED;
    return true;
}

/* Counts consume; a paused reply counts only answers consumed by that call.
 * A factor cursor publishes its count atomically when its read validates. */
static Atom *eval_work_count(Space *space, Arena *a, EvalWork *work) {
    uint64_t count = 0u, pauses_before = work->pauses;
    work->observer = "count";
    work->takes++;
    if (eval_work_count_factors(work, &count)) {
        work->observer_law = "relational-count-factors";
    } else {
        work->observer_law = "enumerate";
        Atom **unused = NULL;
        size_t len = 0u, cap = 0u;
        work->counting = true;
        work->counted = 0u;
        eval_work_advance(space, a, work, INT64_MAX, &unused, &len, &cap);
        work->counting = false;
        count = work->counted;
        free(unused);
    }
    work->taken += count;
    Atom *parts[4] = {
        atom_symbol(a, "counted"), atom_int(a, (int64_t)count),
        eval_work_state_atom(a, work, work->pauses != pauses_before),
        atom_symbol(a, work->observer_law),
    };
    return atom_expr(a, parts, 4u);
}

/* Existence retains the actual next answer, including its values and owners.
 * Unknown after suspension is distinct from exhaustion. */
static Atom *eval_work_exists(Space *space, Arena *a, EvalWork *work) {
    uint64_t pauses_before = work->pauses;
    work->observer = "exists";
    work->observer_law = "retained-next-answer";
    eval_work_release_count(work);
    if (!work->pending) {
        Atom **values = NULL;
        size_t len = 0u, cap = 0u;
        eval_work_advance(space, a, work, 1u, &values, &len, &cap);
        if (len == 1u) {
            work->pending = atom_deep_copy(&work->pending_storage, values[0]);
            if (!work->pending) {
                work->state = EVAL_WORK_FAILED;
                work->failure = atom_symbol(&work->storage, "WorkCapacity");
            }
        }
        free(values);
    }
    bool found = work->pending != NULL;
    bool paused = work->pauses != pauses_before;
    Atom *parts[3] = {
        atom_symbol(a, "exists"),
        found ? atom_bool(a, true)
            : work->state == EVAL_WORK_COMPLETE ? atom_bool(a, false)
                                               : atom_symbol(a, "unknown"),
        eval_work_state_atom(a, work, paused),
    };
    return atom_expr(a, parts, 3u);
}

static size_t eval_work_retained_bytes(const EvalWork *work) {
    size_t bytes = arena_accounted_live_bytes(&work->storage) +
        arena_accounted_live_bytes(&work->pending_storage) +
        arena_accounted_live_bytes(&work->answers) +
        work->completed_len * sizeof(*work->completed);
    const CettaRelationalPlanInfo *info = cetta_relational_plan_info(work->count_plan);
    if (info)
        bytes += info->retained_bytes;
    CettaContinuationStorage count_storage = {0};
    if (work->count_cursor && cetta_relational_count_cursor_storage(
            work->count_cursor, &count_storage))
        bytes += count_storage.exclusive_bytes;
    if (work->machine_ready)
        bytes += petta_machine_retained_bytes(&work->machine);
    size_t shared = 0u, exclusive = 0u;
    if (work->hub_ready &&
        cetta_continuation_hub_storage(&work->hub, &shared, &exclusive))
        bytes += shared + exclusive;
    return bytes;
}

static Atom *eval_work_field(Arena *a, const char *name, Atom *value) {
    return atom_expr2(a, atom_symbol(a, name), value);
}

static Atom *eval_work_transfers(Arena *a, const EvalWork *work) {
    Atom **items = arena_alloc(a, sizeof(*items) *
                                  (work->transfer_len ? work->transfer_len : 1u));
    for (size_t index = 0u; index < work->transfer_len; index++) {
        const EvalWorkTransfer *t = &work->transfers[index];
        Atom *parts[CETTA_CONTINUATION_COMPONENT_COUNT + 1u];
        parts[0] = atom_symbol(a, "residual");
        for (size_t c = 0u; c < CETTA_CONTINUATION_COMPONENT_COUNT; c++)
            parts[c + 1u] = eval_work_field(
                a, eval_work_component_names[c],
                atom_int(a, (int64_t)t->component_bytes[c]));
        Atom *fields[7] = {
            atom_symbol(a, "transfer"),
            eval_work_field(a, "from",
                            atom_symbol(a, eval_work_strategy_names[t->from])),
            eval_work_field(a, "to",
                            atom_symbol(a, eval_work_strategy_names[t->to])),
            eval_work_field(a, "after", atom_int(a, (int64_t)t->answers_before)),
            eval_work_field(a, "work",
                            atom_int(a, (int64_t)t->transitions_before)),
            eval_work_field(a, "bytes",
                            atom_int(a, (int64_t)(t->shared_bytes +
                                                  t->exclusive_bytes))),
            atom_expr(a, parts, CETTA_CONTINUATION_COMPONENT_COUNT + 1u),
        };
        items[index] = atom_expr(a, fields, 7u);
    }
    return atom_expr(a, items, (CettaExprLen)work->transfer_len);
}

/* (work-record h): the route the work runs on, as data:
 * (route-record (route R) (strategy S) (law L) (modes M) (observer O)
 *               (footprint F) (work W) (retained B) (owner H) (state S)
 *               (taken N) (transfers T)). */
static Atom *eval_work_record(Arena *a, const EvalWork *work) {
    bool factor = work->count_cursor ||
        work->route == EVAL_WORK_ROUTE_COUNTED;
    const char *route = factor ? "relational-count"
        : work->route == EVAL_WORK_ROUTE_GENERAL ? "general"
        : work->route == EVAL_WORK_ROUTE_COMPLETED ? "completed"
        : "pending";
    const char *law = factor ? "constant-template-factor-count"
        : work->route == EVAL_WORK_ROUTE_GENERAL
        ? (work->language == CETTA_LANGUAGE_HE ? "he-shared-execution"
                                               : "petta-relational-root")
        : work->route == EVAL_WORK_ROUTE_COMPLETED ? "canonical-complete"
        : "unstarted";
    Atom *footprint_parts[3] = {
        atom_symbol(a, "footprint"),
        atom_expr2(a, atom_symbol(a, "head"),
                   work->source->kind == ATOM_EXPR &&
                           work->source->expr.len > 0u
                       ? work->source->expr.elems[0]
                       : work->source),
        atom_expr2(a, atom_symbol(a, "revision"),
                   atom_int(a, (int64_t)work->space_revision)),
    };
    Atom *fields[13] = {
        atom_symbol(a, "route-record"),
        eval_work_field(a, "route", atom_symbol(a, route)),
        eval_work_field(a, "strategy",
                        atom_symbol(a, work->route == EVAL_WORK_ROUTE_GENERAL
                            ? eval_work_strategy_names[work->strategy]
                            : "whole")),
        eval_work_field(a, "law", atom_symbol(a, law)),
        eval_work_field(a, "modes", atom_symbol(a, "source")),
        eval_work_field(a, "observer", atom_symbol(a, work->observer ? work->observer : "prefix")),
        atom_expr(a, footprint_parts, 3u),
        /* The general machine counts its transitions; the canonical
         * evaluator meters no work under unlimited fuel. */
        eval_work_field(a, "work",
                        (factor || work->route == EVAL_WORK_ROUTE_GENERAL)
                            ? atom_int(a, (int64_t)(work->transitions + work->count_transitions))
                            : atom_symbol(a, "unmetered")),
        eval_work_field(a, "retained",
                        atom_int(a, (int64_t)eval_work_retained_bytes(work))),
        eval_work_field(a, "owner", eval_work_handle(a, work)),
        eval_work_field(a, "state", eval_work_state_atom(a, work, false)),
        eval_work_field(a, "taken", atom_int(a, (int64_t)work->taken)),
        eval_work_field(a, "transfers", eval_work_transfers(a, work)),
    };
    return atom_expr(a, fields, 13u);
}

/* (work-new results): a handle on the computation, routed now. */
static Atom *eval_work_new(Space *space, Arena *a, Atom *source, int fuel) {
    if (!g_library_context)
        return NULL;
    EvalWork *work = calloc(1u, sizeof(*work));
    if (!work)
        return NULL;
    arena_init(&work->storage);
    arena_init(&work->answers);
    arena_init(&work->pending_storage);
    work->pending_mark = arena_mark(&work->pending_storage);
    work->language = eval_current_language_id();
    work->space = space;
    work->space_revision = space_revision(space);
    work->fuel = fuel;
    work->source = atom_deep_copy(&work->storage, source);
    if (!work->source ||
        !cetta_native_handle_alloc(g_library_context, eval_work_kind, work,
                                   eval_work_free, &work->id)) {
        eval_work_free(work);
        return NULL;
    }
    Atom *handle = eval_work_handle(a, work);
    if (!handle) {
        cetta_native_handle_close(g_library_context, eval_work_kind, work->id);
        return NULL;
    }
    (void)eval_work_start_general(work, a, EVAL_WORK_DEPTH_FIRST);
    return handle;
}

static Atom *eval_work_route_refused(Arena *a, Atom *call, const char *law) {
    return atom_error(a, call, atom_expr2(a,
        atom_symbol(a, "RouteNotQualified"), atom_symbol(a, law)));
}

/* Move a live depth-first residual into the hub under `strategy`: the
 * machine admits branch capture from now on, its state is captured as one
 * owned continuation, and the hub continues it.  A state holding a
 * compiled cursor or a prepared producer declines capture, and the work
 * keeps its route. Equation prefixes can be split: each inner branch keeps
 * its bindings, and one additional continuation owns the older alternatives.
 * Other choice kinds retain their existing capture and expansion rules. */
static Atom *eval_work_route_transfer(Arena *a, Atom *call, EvalWork *work,
                                      EvalWorkStrategy strategy) {
    CettaSelectionAutomaton automaton;
    if (!eval_work_frontier_admitted(work, strategy, &automaton))
        return eval_work_route_refused(a, call, "owned-multishot");
    if (!cetta_continuation_hub_init(&work->hub, &automaton,
                                     CETTA_SELECTION_DUTY_RECURRENT_OLDEST,
                                     &work->context->control_plan))
        return eval_work_route_refused(a, call, "hub-capacity");
    work->hub_ready = true;
    EvalWorkScope saved = eval_work_enter(work);
    petta_machine_set_branch_capture(
        &work->machine, petta_eval_machine_admit_branch_capture);
    work->context->controller_admitted = true;
    CettaOwnedContinuation residual;
    cetta_owned_continuation_init(&residual);
    CettaContinuationStatus captured = petta_eval_controller_capture(
        work->context, petta_machine_continuation_machine(&work->machine),
        &residual);
    eval_work_leave(&saved);
    if (captured != CETTA_CONTINUATION_READY) {
        cetta_owned_continuation_destroy(&residual);
        cetta_continuation_hub_destroy(&work->hub);
        work->hub_ready = false;
        petta_machine_set_branch_capture(&work->machine, NULL);
        work->context->controller_admitted = false;
        return eval_work_route_refused(a, call,
            captured == CETTA_CONTINUATION_INVALIDATED
                ? "branch-capture-invalidated"
            : captured == CETTA_CONTINUATION_CAPACITY
                ? "branch-capture-capacity"
                : "branch-capture-unsupported");
    }
    EvalWorkTransfer transfer = {
        .from = work->strategy, .to = strategy,
        .answers_before = work->taken,
        .transitions_before = work->transitions,
    };
    CettaContinuationStorage storage = {0};
    if (cetta_owned_continuation_storage(&residual, &storage)) {
        transfer.shared_bytes = storage.shared_bytes;
        transfer.exclusive_bytes = storage.exclusive_bytes;
    }
    for (CettaContinuationComponent component =
             CETTA_CONTINUATION_COMPONENT_AUTHORITY;
         component < CETTA_CONTINUATION_COMPONENT_COUNT; component++) {
        CettaContinuationStorage part = {0};
        if (cetta_owned_continuation_component_storage(&residual, component,
                                                       &part))
            transfer.component_bytes[component] =
                part.shared_bytes + part.exclusive_bytes;
    }
    if (work->transfer_len == work->transfer_cap) {
        work->transfer_cap = work->transfer_cap ? work->transfer_cap * 2u : 2u;
        work->transfers = cetta_realloc(
            work->transfers, work->transfer_cap * sizeof(*work->transfers));
    }
    if (!cetta_continuation_hub_append(&work->hub, &residual)) {
        cetta_owned_continuation_destroy(&residual);
        cetta_continuation_hub_destroy(&work->hub);
        work->hub_ready = false;
        petta_machine_set_branch_capture(&work->machine, NULL);
        work->context->controller_admitted = false;
        return eval_work_route_refused(a, call, "hub-capacity");
    }
    work->transfers[work->transfer_len++] = transfer;
    work->controller_live = false;
    work->strategy = strategy;
    return atom_unit(a);
}

/* (work-route h r).  For a work not yet advanced the route is chosen
 * afresh.  A work advanced depth first moves its residual into the hub
 * (eval_work_route_transfer); a frontier of continuations cannot be put
 * back in depth-first order, and a completed route has no residual left to
 * move. */
static Atom *eval_work_route(Arena *a, Atom *call, EvalWork *work,
                             Atom *route) {
    const char *name = route && route->kind == ATOM_SYMBOL
        ? symbol_bytes(g_symbols, atom_symbol_current_id(route)) : NULL;
    bool completed = name && strcmp(name, "completed") == 0;
    EvalWorkStrategy strategy = EVAL_WORK_DEPTH_FIRST;
    bool known = completed;
    for (EvalWorkStrategy index = EVAL_WORK_DEPTH_FIRST;
         !known && index <= EVAL_WORK_RATIO; index++) {
        if (name && strcmp(name, eval_work_strategy_names[index]) == 0) {
            strategy = index;
            known = true;
        }
    }
    if (!known)
        return atom_error(a, call, atom_symbol(a, "UnknownWorkRoute"));
    if (work->state == EVAL_WORK_LIVE && !completed &&
        work->route == EVAL_WORK_ROUTE_GENERAL && !work->hub_ready &&
        strategy != EVAL_WORK_DEPTH_FIRST)
        return eval_work_route_transfer(a, call, work, strategy);
    if (work->state != EVAL_WORK_FRESH)
        return eval_work_route_refused(a, call,
            work->state == EVAL_WORK_LIVE ? "residual-started" : "complete");
    if (completed) {
        eval_work_release_count(work);
        eval_work_release_machine(work);
        work->route = EVAL_WORK_ROUTE_PENDING;
        return atom_unit(a);
    }
    /* Prepare at the work's stable answer-arena address. Only the engine
     * fields of this saved record are owned temporarily; its arenas,
     * source and delivery history continue to belong to WORK. */
    EvalWork previous = *work;
    work->context = NULL;
    work->machine = (PettaMachine){0};
    work->machine_ready = false;
    work->hub = (CettaContinuationHub){0};
    work->hub_ready = false;
    work->controller_live = false;
    work->route = EVAL_WORK_ROUTE_PENDING;
    if (!eval_work_start_general(work, a, strategy)) {
        eval_work_release_machine(work);
        *work = previous;
        return eval_work_route_refused(a, call,
            strategy == EVAL_WORK_DEPTH_FIRST ? "general-machine"
                                              : "owned-multishot");
    }
    eval_work_release_machine(&previous);
    eval_work_release_count(work);
    return atom_unit(a);
}

/* The value of a work form's argument: the first answer of its
 * evaluation. */
static Atom *eval_work_argument(Space *s, Arena *a, Atom *argument,
                                int fuel) {
    ResultSet results;
    result_set_init(&results);
    metta_eval(s, a, NULL, argument, fuel, &results);
    Atom *value = results.len ? results.items[0] : argument;
    result_set_free(&results);
    return value;
}

static bool eval_work_count_argument(Atom *value, int64_t *out) {
    if (!value || value->kind != ATOM_GROUNDED ||
        value->ground.gkind != GV_INT || value->ground.ival < 0)
        return false;
    *out = value->ground.ival;
    return true;
}

static bool eval_is_work_form(SymbolId head) {
    return head == g_builtin_syms.work_new ||
        head == g_builtin_syms.work_take ||
        head == g_builtin_syms.work_budget ||
        head == g_builtin_syms.work_cancel ||
        head == g_builtin_syms.work_record ||
        head == g_builtin_syms.work_route ||
        head == g_builtin_syms.work_count ||
        head == g_builtin_syms.work_exists;
}

/* The work forms; their profiles are the extended ones (session.c). */
static void eval_work_call(Space *s, Arena *a, Atom *atom, SymbolId head,
                           int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    CettaExprLen nargs = expr_nargs(atom);
    if (cetta_parallel_worker_active()) {
        outcome_set_add(os, atom_error(a, atom,
            atom_symbol(a, "WorkUnavailableInParallelBranch")), &empty);
        return;
    }
    if (head == g_builtin_syms.work_new) {
        Atom *handle = nargs == 1u
            ? eval_work_new(s, a, expr_arg(atom, 0), fuel) : NULL;
        outcome_set_add(os, handle ? handle : atom_error(a, atom,
            atom_symbol(a, nargs == 1u ? "WorkCapacity"
                                       : "IncorrectNumberOfArguments")),
            &empty);
        return;
    }
    CettaExprLen want = head == g_builtin_syms.work_take ||
            head == g_builtin_syms.work_budget ||
            head == g_builtin_syms.work_route ? 2u : 1u;
    if (nargs != want) {
        outcome_set_add(os, atom_error(a, atom,
            atom_symbol(a, "IncorrectNumberOfArguments")), &empty);
        return;
    }
    Atom *handle = eval_work_argument(s, a, expr_arg(atom, 0), fuel);
    uint64_t handle_id = 0u;
    if (!cetta_native_handle_arg(handle, eval_work_kind, &handle_id)) {
        outcome_set_add(os, atom_error(a, atom,
            atom_symbol(a, "ExpectedWorkHandle")), &empty);
        return;
    }
    EvalWork *work = eval_work_lookup(handle);
    if (!work) {
        outcome_set_add(os, atom_error(a, atom,
            atom_symbol(a, "WorkReleased")), &empty);
        return;
    }
    if (work->busy) {
        outcome_set_add(os, atom_error(a, atom,
            atom_symbol(a, "WorkBusy")), &empty);
        return;
    }
    if (head == g_builtin_syms.work_route) {
        outcome_set_add(os, eval_work_route(a, atom, work,
            eval_work_argument(s, a, expr_arg(atom, 1), fuel)), &empty);
        return;
    }
    int64_t count = 0;
    if (want == 2u && !eval_work_count_argument(
            eval_work_argument(s, a, expr_arg(atom, 1), fuel), &count)) {
        outcome_set_add(os, atom_error(a, atom,
            atom_symbol(a, "ExpectedNonNegativeCount")), &empty);
        return;
    }
    Atom *answer = NULL;
    if (head == g_builtin_syms.work_take || head == g_builtin_syms.work_count ||
        head == g_builtin_syms.work_exists) {
        work->busy = true;
        if (head == g_builtin_syms.work_take)
            answer = eval_work_take(s, a, atom, work, count);
        else if (head == g_builtin_syms.work_count)
            answer = eval_work_count(s, a, work);
        else
            answer = eval_work_exists(s, a, work);
        work->busy = false;
    } else if (head == g_builtin_syms.work_budget) {
        work->budget = (uint64_t)count;
        answer = atom_unit(a);
    } else if (head == g_builtin_syms.work_cancel) {
        (void)cetta_native_handle_close(g_library_context, eval_work_kind,
                                        work->id);
        answer = atom_unit(a);
    } else {
        answer = eval_work_record(a, work);
    }
    outcome_set_add(os, answer, &empty);
}
