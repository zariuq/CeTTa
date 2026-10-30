/* HE's interpretation policy over owned call, argument and return frames.
 * The source interpreter supplies control and native leaves; an authored
 * argument or equation body is a saved task, rather than a nested C call. */
typedef enum {
    HE_CANONICAL_BIND,
    HE_CANONICAL_EVALUATE,
    HE_CANONICAL_CALL,
    HE_CANONICAL_CALLER,
    HE_CANONICAL_DISPATCH,
    HE_CANONICAL_ARGUMENTS,
    HE_CANONICAL_TUPLE,
    HE_CANONICAL_RESULT,
    HE_CANONICAL_CONTROL,
    HE_CANONICAL_PATTERN,
    HE_CANONICAL_INSTRUCTION,
    HE_CANONICAL_MATCH,
    HE_CANONICAL_CAPTURE,
    HE_CANONICAL_SPACE,
    HE_CANONICAL_STATE,
    HE_CANONICAL_MATCH_DIAGNOSTIC,
} HeCanonicalKind;

typedef struct {
    Atom *op;
    Atom **source;
    Atom **types;
    Atom **prefix;
    CettaExprLen len;
    EvalGcRootFrame root;
} HeCanonicalRegisters;

/* A run of relay frames delivers into one buffer: the target of the run's
 * first relay. A relay is a frame whose only work after its children is to
 * publish their results; it remembers where its range of that buffer starts
 * and does at its pop only what the range's items need. Positions of the
 * items that are not plain are kept as they arrive, so a plain answer costs
 * the same however many relays it passes. */
typedef struct HeDelivery {
    OutcomeSet *set;
    CettaCount classified;
    CettaCount *odd;
    bool *special;
    size_t odd_len;
    size_t odd_cap;
} HeDelivery;

/* Argument and tuple machines: only their frames carry this state. */
typedef union {
    InterpretFunctionArgsFrame argument;
    InterpretTupleFrame tuple;
} HeCanonicalArgumentState;

typedef struct HeCanonicalFrame {
    CettaOwnedFrameLink link;
    HeCanonicalKind kind;
    unsigned phase;
    CettaControlOperationPlan control;
    Space *space;
    Arena *arena;
    Atom *atom;
    Atom *type;
    Atom *live[4];
    Bindings env;
    OutcomeSet *target;
    OutcomeSet child;
    OutcomeSet work;
    OutcomeSet paths;
    int fuel;
    bool preserve_bindings;
    bool typed;
    bool selected;
    bool has_functions;
    bool has_non_functions;
    bool query_closed;
    CettaCount index;
    CettaCount head_index;
    CettaCount path_index;
    Atom **types;
    uint32_t type_count;
    EvalGcRootFrame lexical_root;
    EvalGcRootFrame child_root;
    EvalGcRootFrame work_root;
    EvalGcRootFrame type_root;
    HeCanonicalRegisters *registers;
    bool owns_registers;
    HeCanonicalArgumentState *argument_state;
    bool argument_initialized;
    bool tuple_initialized;
    MettaEvalDepthGuard evaluation_depth;
    EvalObservationDemandGuard observation;
    Space **match_spaces;
    bool sourced_match;
    EvalGcRootFrame observation_root;
    /* A chain of more than two legs planned on a certificate: each row's
     * body is shown pure before it runs. */
    bool match_certifying;
    /* The certificate failed once: the chain runs on the source schedule. */
    bool match_uncertified;
    DeferredSpaceSet deferred_spaces;
    CettaOwnedFrameStack instructions;
    CettaEvaluatorOptions *saved_options;
    /* A bounded run (see the notes on bounded observation): the frame whose
     * answer set this frame's target is, or the run's own answers; whether
     * each answer this frame publishes becomes one answer of the run; how
     * many of this frame's answers have gone, and whether a success has. */
    struct HeCanonicalFrame *stream_owner;
    bool stream_to_root;
    bool stream_spine;
    bool stream_control_paths;
    bool stream_passed_success;
    CettaCount streamed;
    /* A control consuming its source value by value: whether it does and
     * the source still runs, the source length a let waits beyond, whether
     * the body of a value is running, and the ready queue. */
    bool stream_consumer;
    bool stream_source_open;
    struct HeCanonicalFrame *stream_source;
    bool stream_body_running;
    bool stream_queued;
    CettaCount stream_wait_len;
    struct HeCanonicalFrame *stream_body_of;
    struct HeCanonicalFrame *stream_next;
    /* A relay (see HeDelivery): the run's buffer, where this relay's range
     * of it starts, whether this relay opened the run, whether its children
     * deliver there and whether it has published, and whether a closed call
     * above it settles its answers' environment. */
    HeDelivery *delivery;
    CettaCount range_start;
    bool owns_delivery;
    bool relay_redirected;
    bool relay_finished;
    bool env_settled_above;
} HeCanonicalFrame;

typedef struct {
    CettaOwnedFrameStack stack;
    /* Popped frames, kept for the next push of this run. */
    void *free_frames;
    OutcomeSet *root_target;
    Arena *arena;
    ArenaMark gc_anchor;
    size_t gc_survivor_floor_bytes;
    uint64_t advances;
    /* A bounded run stops once its answers hold this many answers (0: the
     * run is not bounded); it counts them from the answers' length when it
     * began. */
    uint64_t answer_limit;
    uint64_t root_visible;
    CettaCount root_seen;
    bool satisfied;
    bool abandoning;
    HeCanonicalFrame *resumed;
    bool resumed_popped;
    HeCanonicalFrame *stream_ready;
} HeCanonicalDriver;

static __thread HeCanonicalDriver *g_he_canonical_driver;

#include "eval_he_value_index.inc"

static bool he_canonical_query_closed(bool source_closed) {
    HeCanonicalFrame *frame = g_he_canonical_driver
        ? (HeCanonicalFrame *)g_he_canonical_driver->stack.top : NULL;
    return frame && frame->kind == HE_CANONICAL_CALL
        ? frame->query_closed : source_closed;
}

static void metta_call_impl(Space *s, Arena *a, Atom *atom, Atom *etype,
    int fuel, bool preserve_bindings, const Bindings *initial_env,
    int strict_ready_argument,
    OutcomeSet *os);

static bool he_canonical_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_HE_CANONICAL_EXECUTION");
        enabled = !setting || strcmp(setting, "1") == 0;
    }
    /* The optional unbounded implementation switch cannot disable the
     * machine that enforces an active bounded observation's effect policy. */
    bool bounded = g_he_canonical_driver && g_he_canonical_driver->answer_limit;
    return (enabled || bounded) && eval_current_language_id() == CETTA_LANGUAGE_HE;
}

/* ── Bounded observation: lazy selection ─────────────────────────────────
 * Under `(pragma! select-mode lazy)` a selection that asks HE's extended
 * profiles for k answers runs its expression on a stack that passes each
 * answer on as soon as it is complete, and stops once the run's answers
 * hold k visible answers.  The frames still on the stack are abandoned
 * unrun, so the alternatives after the selected answers, and their effects,
 * never happen.  Every frame publishes exactly what it publishes at its
 * completion, in the same order:
 *   - a frame that publishes answer by answer passes each new answer on;
 *   - a frame that keeps HE's visible frontier (errors and Empty are dropped
 *     once a success exists) passes successes on at once and decides the
 *     rest when it completes;
 *   - a control (let, if, case, switch, assert) consumes its source value by
 *     value: the body of one value runs before the source computes the next.
 * An unbounded run does none of this. */
typedef enum {
    HE_STREAM_PASS,
    HE_STREAM_INVOKE,
    HE_STREAM_RAW_RETURN,
    HE_STREAM_FILTER,
    HE_STREAM_EVALUATE,
} HeStreamRule;

static bool he_canonical_control_streams(SymbolId head) {
    return head == g_builtin_syms.let || head == g_builtin_syms.if_text ||
        head == g_builtin_syms.case_text || head == g_builtin_syms.switch_text ||
        head == g_builtin_syms.assert_text;
}

/* The answers a frame publishes, and how; NULL while its sets hold work
 * that is not yet an answer. */
static OutcomeSet *he_canonical_stream_set(HeCanonicalFrame *frame,
                                           HeStreamRule *rule) {
    switch (frame->kind) {
    case HE_CANONICAL_BIND:
    case HE_CANONICAL_CALLER:
        *rule = HE_STREAM_FILTER;
        return frame->phase ? &frame->child : NULL;
    case HE_CANONICAL_EVALUATE:
        *rule = HE_STREAM_EVALUATE;
        return frame->phase ? &frame->child : NULL;
    case HE_CANONICAL_CALL:
    case HE_CANONICAL_CAPTURE:
        *rule = HE_STREAM_PASS;
        return frame->phase ? &frame->child : NULL;
    case HE_CANONICAL_RESULT:
        *rule = frame->selected ? HE_STREAM_RAW_RETURN : HE_STREAM_PASS;
        return frame->phase == 1u ? &frame->child : NULL;
    case HE_CANONICAL_CONTROL:
        *rule = HE_STREAM_INVOKE;
        return frame->phase && frame->stream_control_paths
            ? &frame->paths : NULL;
    case HE_CANONICAL_DISPATCH:
        *rule = HE_STREAM_INVOKE;
        return frame->phase >= 5u ? &frame->paths : NULL;
    case HE_CANONICAL_MATCH:
    case HE_CANONICAL_MATCH_DIAGNOSTIC:
    case HE_CANONICAL_INSTRUCTION:
    case HE_CANONICAL_PATTERN:
        *rule = HE_STREAM_PASS;
        return &frame->paths;
    default:
        return NULL;
    }
}

/* Whether a frame's next resumption only publishes its answers and pops. */
static bool he_canonical_frame_finishing(HeCanonicalFrame *frame) {
    switch (frame->kind) {
    case HE_CANONICAL_BIND:
    case HE_CANONICAL_CALLER:
    case HE_CANONICAL_EVALUATE:
    case HE_CANONICAL_CALL:
    case HE_CANONICAL_CAPTURE:
        return frame->phase != 0u;
    case HE_CANONICAL_RESULT:
        return frame->phase == 1u ||
            (frame->phase == 2u && frame->index >= frame->work.len);
    case HE_CANONICAL_DISPATCH:
        return (frame->phase == 5u && frame->index >= frame->child.len) ||
            frame->phase == 7u;
    default:
        return false;
    }
}

/* Record where a new frame's answers go: into the run's answers, or into
 * an answer set of a frame below it. */
static __attribute__((noinline)) void he_canonical_stream_attach(
    HeCanonicalDriver *driver, HeCanonicalFrame *frame, OutcomeSet *target) {
    frame->stream_control_paths = frame->kind == HE_CANONICAL_CONTROL &&
        he_canonical_control_streams(atom_head_symbol_id(frame->atom));
    if (target == driver->root_target) {
        frame->stream_to_root = true;
        frame->stream_spine = true;
        return;
    }
    HeCanonicalFrame *parent = (HeCanonicalFrame *)driver->stack.top;
    if (parent && target == parent->target) {
        frame->stream_owner = parent->stream_owner;
        frame->stream_to_root = parent->stream_to_root;
        frame->stream_spine = parent->stream_spine;
        return;
    }
    unsigned scanned = 0u;
    for (HeCanonicalFrame *owner = parent; owner && scanned < 8u;
         owner = (HeCanonicalFrame *)owner->link.next, scanned++) {
        if (target != &owner->child && target != &owner->paths &&
            target != &owner->work)
            continue;
        HeStreamRule rule;
        frame->stream_owner = owner;
        frame->stream_spine = owner->stream_spine &&
            he_canonical_stream_set(owner, &rule) == target;
        return;
    }
}

static HeCanonicalFrame *he_canonical_push(HeCanonicalKind kind,
    Space *s, Arena *a, Atom *atom, Atom *type, int fuel,
    const Bindings *env, bool preserve, OutcomeSet *target) {
    HeCanonicalFrame *pusher =
        (HeCanonicalFrame *)g_he_canonical_driver->stack.top;
    HeCanonicalFrame *frame = g_he_canonical_driver->free_frames;
    if (frame)
        g_he_canonical_driver->free_frames = *(void **)frame;
    else
        frame = cetta_malloc(sizeof(*frame));
    memset(frame, 0, sizeof(*frame));
    /* A frame joins its pusher's run when it delivers into that run's
     * buffer. Any other relay opens its own run, possibly on the same
     * buffer: an enclosing run classifies nothing while it is open. */
    if (pusher && pusher->delivery && pusher->delivery->set == target) {
        frame->delivery = pusher->delivery;
        frame->env_settled_above = pusher->env_settled_above ||
            (pusher->kind == HE_CANONICAL_DISPATCH && pusher->relay_redirected);
    }
    frame->kind = kind;
    frame->space = s;
    frame->arena = a;
    frame->atom = atom;
    frame->type = type;
    frame->fuel = fuel;
    frame->target = target;
    frame->preserve_bindings = preserve;
    if (kind == HE_CANONICAL_EVALUATE) {
        frame->evaluation_depth.entered = true;
        g_metta_eval_depth++;
    }
    frame->query_closed = eval_gc_enabled() && atom && !atom_contains_vars(atom);
    bindings_init(&frame->env);
    outcome_set_init(&frame->child);
    outcome_set_init(&frame->work);
    outcome_set_init(&frame->paths);
    if (env && !bindings_copy(&frame->env, env)) {
        metta_eval_depth_guard_leave(&frame->evaluation_depth);
        *(void **)frame = g_he_canonical_driver->free_frames;
        g_he_canonical_driver->free_frames = frame;
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return NULL;
    }
    eval_gc_root_frame_enter(&frame->lexical_root,
        &frame->atom, &frame->env, &frame->type);
    /* All remaining atom-bearing fields are registered below or by the
     * argument/register owner. There is no suspended C-local state in this
     * lexical frame; collection occurs between continuation operations. */
    eval_gc_root_frame_suspend_precisely(&frame->lexical_root);
    /* A run's buffer is rooted once, by the relay that opened the run. */
    eval_gc_root_frame_enter_outcome_continuation(&frame->child_root,
        frame->live, 4u, &frame->child, frame->delivery ? NULL : target);
    eval_gc_root_frame_enter_outcome_continuation(&frame->work_root,
        NULL, 0u, &frame->work, &frame->paths);
    eval_gc_root_frame_enter_evaluation_retry(&frame->observation_root,
        &frame->observation.previous_root);
    if (g_he_canonical_driver->answer_limit)
        he_canonical_stream_attach(g_he_canonical_driver, frame, target);
    cetta_owned_frame_push(&g_he_canonical_driver->stack, &frame->link);
    return frame;
}

static void he_delivery_truncate(HeDelivery *d, CettaCount start) {
    OutcomeSet *set = d->set;
    for (CettaCount i = start; i < set->len; i++)
        outcome_free_fields(&set->items[i]);
    if (set->len > start)
        set->len = start;
    if (d->classified > start)
        d->classified = start;
    while (d->odd_len && d->odd[d->odd_len - 1u] >= start)
        d->odd_len--;
}

static void he_canonical_stream_unqueue(HeCanonicalDriver *driver,
                                        HeCanonicalFrame *consumer);
static void he_canonical_stream(HeCanonicalDriver *driver,
                                HeCanonicalFrame *frame);
static void he_canonical_stream_root(HeCanonicalDriver *driver);

static inline __attribute__((always_inline)) void he_canonical_release(
    HeCanonicalFrame *frame) {
    /* A relay that leaves without publishing drops its children's results,
     * as the unpublished buffer did. */
    if (frame->relay_redirected && !frame->relay_finished)
        he_delivery_truncate(frame->delivery, frame->range_start);
    if (frame->owns_delivery) {
        HeDelivery *d = frame->delivery;
        free(d->odd);
        free(d->special);
        free(d);
    }
    if (frame->saved_options) {
        *active_eval_options() = *frame->saved_options;
        free(frame->saved_options);
    }
    while (frame->instructions.top)
        he_instruction_pop(&frame->instructions);
    if (frame->argument_initialized)
        interpret_function_args_frame_cleanup(&frame->argument_state->argument);
    if (frame->tuple_initialized)
        interpret_tuple_frame_cleanup(&frame->argument_state->tuple);
    free(frame->argument_state);
    if (frame->owns_registers) {
        eval_gc_root_frame_leave(&frame->registers->root);
        free(frame->registers->source);
        free(frame->registers->types);
        free(frame->registers->prefix);
        free(frame->registers);
    }
    if (frame->types) {
        eval_gc_root_frame_leave(&frame->type_root);
        free(frame->types);
    }
    eval_observation_demand_leave(&frame->observation);
    metta_eval_depth_guard_leave(&frame->evaluation_depth);
    eval_gc_root_frame_leave(&frame->observation_root);
    eval_gc_root_frame_leave(&frame->work_root);
    eval_gc_root_frame_leave(&frame->child_root);
    eval_gc_root_frame_leave(&frame->lexical_root);
    outcome_set_free(&frame->paths);
    outcome_set_free(&frame->work);
    outcome_set_free(&frame->child);
    bindings_free(&frame->env);
    free(frame->match_spaces);
    deferred_space_set_free(&frame->deferred_spaces);
    *(void **)frame = g_he_canonical_driver->free_frames;
    g_he_canonical_driver->free_frames = frame;
}

/* A bounded run's pop: the frame's completion has published its last
 * answers into its target, which pass on from there. */
static __attribute__((noinline)) void he_canonical_pop_bounded(
    HeCanonicalDriver *driver, HeCanonicalFrame *frame) {
    HeCanonicalFrame *owner = frame->stream_owner;
    HeCanonicalFrame *body_of = frame->stream_body_of;
    bool to_root = frame->stream_to_root;
    if (frame == driver->resumed)
        driver->resumed_popped = true;
    if (frame->stream_queued)
        he_canonical_stream_unqueue(driver, frame);
    if (owner && owner->stream_consumer && frame->target == &owner->child) {
        owner->stream_source_open = false;
        owner->stream_source = NULL;
    }
    he_canonical_release(frame);
    if (driver->abandoning)
        return;
    if (body_of)
        body_of->stream_body_running = false;
    if (to_root)
        he_canonical_stream_root(driver);
    else
        he_canonical_stream(driver, owner);
}

static void he_canonical_pop(void) {
    HeCanonicalDriver *driver = g_he_canonical_driver;
    HeCanonicalFrame *frame = (HeCanonicalFrame *)cetta_owned_frame_pop(
        &driver->stack);
    if (driver->answer_limit)
        he_canonical_pop_bounded(driver, frame);
    else
        he_canonical_release(frame);
}

/* Free the frames a run kept for reuse, once its stack is empty. */
static void he_canonical_free_frames(HeCanonicalDriver *driver) {
    while (driver->free_frames) {
        void *next = *(void **)driver->free_frames;
        free(driver->free_frames);
        driver->free_frames = next;
    }
}

static inline __attribute__((always_inline)) void he_canonical_publish_one(
    HeCanonicalFrame *frame, Outcome *out, bool invocation) {
    /* The call returns to its caller here: Rust's return stage keeps the
     * caller's variables. */
    if (invocation && frame->atom &&
        !he_return_retain_invocation_outcome(frame->arena, frame->atom,
                                             &frame->env, out))
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
    Atom *value = outcome_preview_atom(out);
    /* The callee's bindings reach the caller only through the call, its
     * expected type (a delivery's owner's too) and the answer: keep what
     * those, and the answer's delayed goals, reach, then merge with the
     * caller's environment. */
    Outcome projected;
    bool use_projected = false;
    if (invocation && frame->preserve_bindings &&
        out->kind == CETTA_OUTCOME_INLINE &&
        !variant_instance_present(&out->variant) &&
        bindings_has_any_entries(&out->env)) {
        Atom *roots[5];
        size_t root_count = 0u;
        roots[root_count++] = frame->atom;
        roots[root_count++] = value;
        if (frame->type)
            roots[root_count++] = frame->type;
        if (frame->live[2])
            roots[root_count++] = frame->live[2];
        if (out->delayed)
            roots[root_count++] = out->delayed;
        Bindings env;
        if (bindings_project_reachable(&out->env, roots, root_count,
                                       &env)) {
            projected = *out;
            projected.env = env;
            use_projected = true;
        }
    }
    outcome_set_add_prefixed_outcome(frame->arena, frame->target,
        use_projected ? &projected : out,
        invocation ? &frame->env : NULL, frame->preserve_bindings);
    if (use_projected)
        bindings_free(&projected.env);
}

static void he_canonical_publish_raw_return(HeCanonicalFrame *frame, Outcome *out) {
    if (frame->atom &&
        !he_return_retain_invocation_outcome(frame->arena, frame->atom,
                                             NULL, out))
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
    Atom *value = outcome_atom_materialize(frame->arena, out);
    outcome_set_add_prefixed(frame->arena, frame->target,
        he_call_return_atom(frame->atom, value), &out->env,
        NULL, frame->preserve_bindings);
}

/* Retire only input roots whose consumer has finished. CALL phase one is a
 * non-invocation relay: publication reads its answers, not its original call,
 * type or prefix. A typed dispatch frontend can do the same after its sole
 * delivery owns the complete invocation and its independent return target.
 * Invocation/control frames with remaining publication work retain theirs. */
static void he_canonical_retire_input_roots(HeCanonicalFrame *frame) {
    frame->atom = NULL;
    frame->type = NULL;
    memset(frame->live, 0, sizeof(frame->live));
    bindings_free(&frame->env);
    bindings_init(&frame->env);
}

static void he_canonical_publish(HeCanonicalFrame *frame,
    OutcomeSet *source, bool invocation) {
    for (CettaCount i = 0u; i < source->len; i++)
        he_canonical_publish_one(frame, &source->items[i], invocation);
}

/* An answer equal to the atom evaluated is a value (Rust HE's
 * check_alternatives); the mark goes on a copy. */
static Atom *he_canonical_evaluated_answer(HeCanonicalFrame *frame,
                                           Atom *answer) {
    if (answer && answer->kind == ATOM_EXPR &&
        !atom_is_he_evaluated(answer) &&
        !atom_is_legacy_empty_or_error(answer) &&
        atom_eq(answer, frame->atom))
        return atom_he_evaluated_copy(frame->arena, answer);
    return answer;
}

static inline __attribute__((always_inline)) void
he_canonical_publish_evaluated(HeCanonicalFrame *frame, Outcome *out) {
    Bindings empty;
    bindings_init(&empty);
    Atom *value=outcome_atom_materialize_traced(frame->arena,out,
        CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_TOP_LEVEL);
    if (!value) return;
    outcome_set_add(frame->target,
        he_canonical_evaluated_answer(frame,value),
        &empty);
}

static bool he_canonical_stream_success(Arena *a, Outcome *out) {
    return !outcome_atom_is_empty_result(a, out) &&
        !outcome_atom_is_error(a, out);
}

/* The answers of a frame that publishes answer by answer, from the first
 * one it has not passed on yet. */
static __attribute__((noinline)) void he_canonical_publish_rest(
    HeCanonicalFrame *frame, OutcomeSet *source, bool invocation) {
    for (CettaCount i = frame->streamed; i < source->len; i++)
        he_canonical_publish_one(frame, &source->items[i], invocation);
}

static inline __attribute__((always_inline)) void he_canonical_finish(
    HeCanonicalFrame *frame, OutcomeSet *source, bool invocation) {
    if (frame->streamed)
        he_canonical_publish_rest(frame, source, invocation);
    else
        he_canonical_publish(frame, source, invocation);
}

/* HE's visible frontier of a frame's answers, at its completion.  Before
 * any success has gone, it is the frontier of the whole set; once one has,
 * every error and Empty is dropped, and the successes not yet passed on
 * remain. */
static __attribute__((noinline)) void he_canonical_finish_passed(
    HeCanonicalFrame *frame, bool evaluated) {
    Arena *a = frame->arena;
    for (CettaCount i = frame->streamed; i < frame->child.len; i++) {
        Outcome *out = &frame->child.items[i];
        if (!he_canonical_stream_success(a, out))
            continue;
        if (evaluated)
            he_canonical_publish_evaluated(frame, out);
        else
            he_canonical_publish_one(frame, out, false);
    }
}

static void he_canonical_finish_visible(HeCanonicalFrame *frame,
                                        bool evaluated) {
    if (frame->stream_passed_success) {
        he_canonical_finish_passed(frame, evaluated);
        return;
    }
    Arena *a = frame->arena;
    outcome_set_normalize_visible_frontier(a, &frame->child);
    if (!evaluated) {
        he_canonical_publish(frame, &frame->child, false);
        return;
    }
    Bindings empty;
    bindings_init(&empty);
    for (CettaCount i = 0u; i < frame->child.len; i++) {
        Atom *value=outcome_atom_materialize_traced(a,&frame->child.items[i],
            CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_TOP_LEVEL);
        if (!value) { if (eval_unwinding()) break; else continue; }
        outcome_set_add(frame->target,
            he_canonical_evaluated_answer(frame,value),
            &empty);
    }
}

/* A delivery publishes its owner's answers into the owner's target, so the
 * owner's requested type (live[2]) is observed beside the delivery's own,
 * refined, type. */
static bool he_frame_call_closed(const HeCanonicalFrame *frame) {
    return frame->atom && !atom_has_vars(frame->atom) &&
        !bindings_value_has_captured_support(frame->atom) &&
        (!frame->type || !atom_has_vars(frame->type)) &&
        !bindings_value_has_captured_support(frame->type) &&
        (!frame->live[2] || !atom_has_vars(frame->live[2])) &&
        !bindings_value_has_captured_support(frame->live[2]);
}

static bool he_relay_may_redirect(const HeCanonicalFrame *frame) {
    /* A bounded run passes each frame's answers on from the frame's own
     * buffer as they arrive (see the notes on bounded observation). */
    if (!frame->preserve_bindings || g_he_canonical_driver->answer_limit)
        return false;
    switch (frame->kind) {
    case HE_CANONICAL_CALL:
    case HE_CANONICAL_BIND:
    case HE_CANONICAL_CALLER:
    case HE_CANONICAL_RESULT:
    case HE_CANONICAL_MATCH:
    case HE_CANONICAL_MATCH_DIAGNOSTIC:
        return true;
    case HE_CANONICAL_DISPATCH:
        /* With the call and its type ground, the invocation publishes a plain
         * answer as (answer, caller environment). */
        return he_frame_call_closed(frame);
    default:
        return false;
    }
}

/* Where a relay's children deliver: its own buffer, or the run's buffer. */
static OutcomeSet *he_relay_sink(HeCanonicalFrame *frame, OutcomeSet *own) {
    if (frame->relay_redirected)
        return frame->delivery->set;
    if (own->len || !frame->target || !he_relay_may_redirect(frame))
        return own;
    if (!frame->delivery) {
        HeDelivery *d = cetta_malloc(sizeof(*d));
        memset(d, 0, sizeof(*d));
        d->set = frame->target;
        d->classified = frame->target->len;
        frame->delivery = d;
        frame->owns_delivery = true;
    }
    frame->relay_redirected = true;
    frame->range_start = frame->target->len;
    return frame->target;
}

/* Projection onto ground roots keeps an environment's constraints, owners
 * and Prime state, and nothing else. */
static bool he_bindings_vanish_under_projection(const Bindings *env) {
    return !env->eq_len && !env->owners && !bindings_prime_present(env);
}

/* 0: plain, a ground value whose publication through a closed call is
 * (value, caller environment); 1: needs the invocation transform; 2: may be
 * Empty or an error, which normalization reads. */
static unsigned he_delivery_item_class(const Outcome *out) {
    if (out->kind != CETTA_OUTCOME_INLINE || !out->atom ||
        variant_instance_present(&out->variant))
        return 2u;
    Atom *value = out->atom;
    if (value->kind == ATOM_VAR || atom_is_legacy_empty_sentinel(value) ||
        atom_is_error(value))
        return 2u;
    if (out->delayed || atom_has_vars(value) || he_not_reducible(value) ||
        !he_bindings_vanish_under_projection(&out->env))
        return 1u;
    return 0u;
}

/* Classify the items that arrived since the last finish. A plain item in a
 * range whose closed call above will publish it with that call's
 * environment carries no environment of its own from here on. */
static void he_delivery_classify(HeDelivery *d, CettaCount start,
    bool settled) {
    OutcomeSet *set = d->set;
    for (CettaCount i = d->classified; i < set->len; i++) {
        Outcome *item = &set->items[i];
        unsigned cls = he_delivery_item_class(item);
        if (!cls) {
            if (settled && i >= start && bindings_has_any_entries(&item->env)) {
                bindings_free(&item->env);
                bindings_init(&item->env);
            }
            continue;
        }
        if (d->odd_len == d->odd_cap) {
            d->odd_cap = d->odd_cap ? d->odd_cap * 2u : 16u;
            d->odd = cetta_realloc(d->odd, d->odd_cap * sizeof(*d->odd));
            d->special = cetta_realloc(d->special,
                d->odd_cap * sizeof(*d->special));
        }
        d->odd[d->odd_len] = i;
        d->special[d->odd_len++] = cls == 2u;
    }
    d->classified = set->len;
}

/* Publish a relay's results: the old publication on its own buffer (from
 * the first answer a bounded run has not passed on yet, and with HE's visible
 * frontier when `normalize`, which reads the frame's child answers), or on
 * its range of the run's buffer only where the range's items need it. */
static void he_relay_finish(HeCanonicalFrame *frame, OutcomeSet *own,
    bool invocation, bool normalize) {
    if (!frame->relay_redirected) {
        if (normalize)
            he_canonical_finish_visible(frame, false);
        else
            he_canonical_finish(frame, own, invocation);
        return;
    }
    frame->relay_finished = true;
    if (!invocation && !normalize)
        return;
    HeDelivery *d = frame->delivery;
    CettaCount start = frame->range_start;
    if (d->set->len == start)
        return;
    /* A plain answer's environment vanishes under a closed call's
     * publication; an open call or type keeps what it reaches. */
    he_delivery_classify(d, start,
        (invocation && he_frame_call_closed(frame)) || frame->env_settled_above);
    size_t low = 0u, high = d->odd_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (d->odd[middle] < start)
            low = middle + 1u;
        else
            high = middle;
    }
    bool rework;
    if (invocation) {
        /* A closed call above publishes a plain answer with its own
         * environment; this call's environment vanishes under it. */
        rework = low < d->odd_len || !frame->env_settled_above ||
            !he_bindings_vanish_under_projection(&frame->env);
    } else {
        rework = false;
        for (size_t i = low; !rework && i < d->odd_len; i++)
            rework = d->special[i];
    }
    if (!rework)
        return;
    OutcomeSet range;
    outcome_set_init(&range);
    OutcomeSet *set = d->set;
    for (CettaCount i = start; i < set->len; i++)
        outcome_set_add_existing_move(&range, &set->items[i]);
    he_delivery_truncate(d, start);
    if (normalize)
        outcome_set_normalize_visible_frontier(frame->arena, &range);
    he_canonical_publish(frame, &range, invocation);
    outcome_set_free(&range);
}

static void he_canonical_stream_queue(HeCanonicalDriver *driver,
                                      HeCanonicalFrame *consumer) {
    if (consumer->stream_queued)
        return;
    consumer->stream_queued = true;
    consumer->stream_next = driver->stream_ready;
    driver->stream_ready = consumer;
}

static void he_canonical_stream_unqueue(HeCanonicalDriver *driver,
                                        HeCanonicalFrame *consumer) {
    for (HeCanonicalFrame **link = &driver->stream_ready; *link;
         link = &(*link)->stream_next) {
        if (*link == consumer) {
            *link = consumer->stream_next;
            break;
        }
    }
    consumer->stream_queued = false;
    consumer->stream_next = NULL;
}

/* Count the run's answers as they arrive.  The run's root keeps HE's visible
 * frontier, so what reaches the answers before the root completes is a
 * success; at its completion the run ends anyway. */
static void he_canonical_stream_root(HeCanonicalDriver *driver) {
    driver->root_visible = driver->root_target->len - driver->root_seen;
#ifndef CETTA_HE_LAZY_MUTATION_NO_STOP
    if (driver->root_visible >= driver->answer_limit)
        driver->satisfied = true;
#endif
}

/* Pass a frame's new answers on, and its owner's in turn, until a frame has
 * nothing new to pass on.  A control whose source has produced values it
 * has not consumed is queued to consume them. */
static void he_canonical_stream(HeCanonicalDriver *driver,
                                HeCanonicalFrame *frame) {
    while (frame && !driver->satisfied) {
        if (frame->stream_consumer && frame->stream_source_open &&
            !frame->stream_body_running &&
            frame->index < frame->child.len &&
            frame->child.len > frame->stream_wait_len)
            he_canonical_stream_queue(driver, frame);
        HeStreamRule rule;
        OutcomeSet *answers = he_canonical_stream_set(frame, &rule);
        if (!answers || frame->streamed >= answers->len)
            return;
        bool passed = false;
        bool visible = rule == HE_STREAM_FILTER || rule == HE_STREAM_EVALUATE;
        for (; frame->streamed < answers->len; frame->streamed++) {
            Outcome *out = &answers->items[frame->streamed];
            if (visible) {
#ifndef CETTA_HE_LAZY_MUTATION_ERRORS_EARLY
                if (!he_canonical_stream_success(frame->arena, out))
                    continue;
#endif
                frame->stream_passed_success = true;
            }
            if (rule == HE_STREAM_EVALUATE)
                he_canonical_publish_evaluated(frame, out);
            else if (rule == HE_STREAM_RAW_RETURN)
                he_canonical_publish_raw_return(frame, out);
            else
                he_canonical_publish_one(frame, out,
                                         rule == HE_STREAM_INVOKE);
            passed = true;
        }
        if (!passed)
            return;
        if (frame->stream_to_root) {
            he_canonical_stream_root(driver);
            return;
        }
        frame = frame->stream_owner;
    }
}

static bool he_canonical_argument_schedule(Space *s, Arena *a, Atom *type,
    Atom *atom, int fuel, const Bindings *env, OutcomeSet *os) {
    if (!g_he_canonical_driver)
        return false;
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_BIND,
        s, a, atom, type, fuel, env, true, os);
    if (frame)
        frame->typed = true;
    return true;
}

static bool he_canonical_caller_schedule(Space *s, Arena *a, Atom *type,
    Atom *atom, int fuel, const Bindings *prefix, bool preserve,
    OutcomeSet *os) {
    HeCanonicalFrame *frame = g_he_canonical_driver
        ? (HeCanonicalFrame *)g_he_canonical_driver->stack.top : NULL;
    if (!frame || frame->kind != HE_CANONICAL_INSTRUCTION ||
        !frame->instructions.top)
        return false;
    HeInstructionFrame *instruction =
        (HeInstructionFrame *)frame->instructions.top;
    /* Stack-local foreign result sets cannot become suspended targets. */
    if (os != &instruction->inner && os != instruction->target)
        return false;
    he_canonical_push(HE_CANONICAL_CALLER, s, a, atom, type, fuel,
        prefix, preserve, os);
    return true;
}

static void he_canonical_instruction_push(HeCanonicalFrame *parent,
    Atom *atom, Atom *type, const Bindings *env, bool public_instruction,
    bool allow_tail, OutcomeSet *target) {
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_INSTRUCTION,
        parent->space, parent->arena, atom, type, parent->fuel,
        env, public_instruction ? parent->preserve_bindings : true, target);
    if (frame) {
        frame->typed = public_instruction;
        frame->selected = allow_tail;
        frame->query_closed = parent->query_closed;
        frame->control = he_instruction_control_plan(atom);
        frame->live[0] = atom;
    }
}

static void he_canonical_arguments_push(HeCanonicalFrame *parent,
    Atom *op, Atom **source, Atom **types, CettaExprLen len,
    const Bindings *env, OutcomeSet *target, bool typed) {
    HeCanonicalFrame *frame = he_canonical_push(
        typed ? HE_CANONICAL_ARGUMENTS : HE_CANONICAL_TUPLE,
        parent->space, parent->arena, NULL, NULL, parent->fuel,
        env, true, target);
    if (!frame)
        return;
    HeCanonicalRegisters *registers = cetta_malloc(sizeof(*registers));
    memset(registers, 0, sizeof(*registers));
    registers->op = op;
    registers->len = len;
    if (len) {
        registers->source = cetta_malloc(sizeof(Atom *) * (size_t)len);
        registers->prefix = cetta_malloc(sizeof(Atom *) * (size_t)len);
        memset(registers->prefix, 0, sizeof(Atom *) * (size_t)len);
        memcpy(registers->source, source, sizeof(Atom *) * (size_t)len);
        if (typed) {
            registers->types = cetta_malloc(sizeof(Atom *) * (size_t)len);
            memcpy(registers->types, types, sizeof(Atom *) * (size_t)len);
        }
    }
    frame->registers = registers;
    frame->owns_registers = true;
    if (typed) {
        eval_gc_root_frame_enter_function_args_machine(&registers->root,
            &registers->op, registers->source, (size_t)len,
            registers->types, (size_t)len, registers->prefix,
            (size_t)len, target);
        frame->argument_state = cetta_malloc(sizeof(*frame->argument_state));
        interpret_function_args_frame_init(&frame->argument_state->argument, 0u,
            &frame->env);
        frame->argument_initialized = true;
    } else {
        eval_gc_root_frame_enter_tuple_machine(&registers->root,
            NULL, 0u, registers->source, (size_t)len,
            registers->prefix, (size_t)len, &frame->env, target, NULL);
        frame->argument_state = cetta_malloc(sizeof(*frame->argument_state));
        interpret_tuple_frame_init(&frame->argument_state->tuple, 0u,
            &frame->env, NULL);
        frame->tuple_initialized = true;
    }
}

static void he_canonical_argument_next(HeCanonicalFrame *frame,
    CettaExprIndex index, const Bindings *env,
    const VariantInstance *variant) {
    HeCanonicalFrame *child = he_canonical_push(frame->kind,
        frame->space, frame->arena, NULL, NULL, frame->fuel,
        env, true, frame->target);
    if (!child)
        return;
    child->registers = frame->registers;
    if (frame->kind == HE_CANONICAL_ARGUMENTS) {
        child->argument_state = cetta_malloc(sizeof(*child->argument_state));
        interpret_function_args_frame_init(&child->argument_state->argument,
            index, &child->env);
        child->argument_initialized = true;
    } else {
        child->argument_state = cetta_malloc(sizeof(*child->argument_state));
        interpret_tuple_frame_init(&child->argument_state->tuple, (uint32_t)index,
            &child->env, variant);
        child->tuple_initialized = true;
    }
}

static void he_canonical_resume_arguments(HeCanonicalFrame *frame) {
    InterpretFunctionArgsFrame *argument = &frame->argument_state->argument;
    if (argument->state == INTERPRET_FUNCTION_ARGS_FRAME_AFTER_ITER_CHILD) {
        interpret_function_args_frame_finish_child(argument);
        argument->state = INTERPRET_FUNCTION_ARGS_FRAME_ITERATE;
    } else if (argument->state == INTERPRET_FUNCTION_ARGS_FRAME_AFTER_ONLY_CHILD) {
        he_canonical_pop();
        return;
    }
    const Bindings *child_env = NULL;
    InterpretFunctionArgsAction action = interpret_function_args_frame_step(
        frame->space, frame->arena, &frame->registers->op,
        frame->registers->source, frame->registers->types,
        frame->registers->len, frame->registers->prefix, frame->fuel,
        frame->target, argument, &child_env, NULL, true);
    if (action == INTERPRET_FUNCTION_ARGS_POP)
        he_canonical_pop();
    else if (action == INTERPRET_FUNCTION_ARGS_PUSH)
        he_canonical_argument_next(frame, argument->idx + 1u, child_env, NULL);
}

static void he_canonical_resume_tuple(HeCanonicalFrame *frame) {
    InterpretTupleFrame *tuple = &frame->argument_state->tuple;
    HeCanonicalRegisters *registers = frame->registers;
    Arena *a = frame->arena;
    if (tuple->state == INTERPRET_TUPLE_FRAME_AFTER_CHILD) {
        interpret_tuple_frame_finish_child(tuple);
    }
    if (tuple->idx == registers->len) {
        Atom *value = interpret_tuple_completed_value(
            a, registers->prefix, registers->len,
            tuple->ctx, tuple->prefix_variant);
        if (tuple->prefix_variant && variant_instance_present(tuple->prefix_variant))
            outcome_set_add_with_variant(frame->target, value,
                tuple->ctx, tuple->prefix_variant);
        else
            outcome_set_add(frame->target, value, tuple->ctx);
        he_canonical_pop();
        return;
    }
    if (tuple->state == INTERPRET_TUPLE_FRAME_ENTER) {
        outcome_set_init(&tuple->sub);
        tuple->sub_initialized = true;
        frame->phase = 1u;
        tuple->state = INTERPRET_TUPLE_FRAME_ITERATE;
        Atom *elem = bindings_apply_if_vars(tuple->ctx, a,
            registers->source[tuple->idx]);
        he_canonical_argument_schedule(frame->space, a, NULL, elem,
            frame->fuel, tuple->ctx, &tuple->sub);
        return;
    }
    if (frame->phase == 1u) {
        frame->phase = 2u;
        if (!tuple->sub.len || !bindings_builder_init(
                &tuple->merged_builder, tuple->ctx)) {
            he_canonical_pop();
            return;
        }
        tuple->builder_initialized = true;
    }
    while (tuple->sub_index < tuple->sub.len) {
        Outcome *item = &tuple->sub.items[tuple->sub_index++];
        Atom *value = item->atom;
        if (atom_is_legacy_empty_sentinel(value) || outcome_atom_is_error(a, item)) {
            Bindings empty;
            bindings_init(&empty);
            outcome_set_add(frame->target, outcome_atom_materialize(a, item), &empty);
            continue;
        }
        const VariantInstance *variant = tuple->prefix_variant;
        if (variant_instance_present(&item->variant)) {
            if (variant && variant_instance_present(variant) &&
                !variant_instance_clone(&tuple->active_variant, variant))
                continue;
            if (!variant_instance_append_rebased(a, &tuple->active_variant,
                    &value, item->atom, &item->variant)) {
                variant_instance_free(&tuple->active_variant);
                continue;
            }
            tuple->active_variant_owned = true;
            variant = &tuple->active_variant;
        }
        BindingsMergeAttempt attempt;
        if (!bindings_builder_merge_or_clone(&tuple->merged_builder,
                tuple->ctx, &item->env, &attempt)) {
            if (tuple->active_variant_owned) {
                variant_instance_free(&tuple->active_variant);
                tuple->active_variant_owned = false;
            }
            continue;
        }
        tuple->active_attempt = attempt;
        if (!tuple->active_attempt.used_builder)
            tuple->active_attempt.env = &tuple->active_attempt.owned;
        tuple->active_attempt_initialized = true;
        registers->prefix[tuple->idx] = value;
        tuple->state = INTERPRET_TUPLE_FRAME_AFTER_CHILD;
        he_canonical_argument_next(frame, tuple->idx + 1u,
            tuple->active_attempt.env, variant);
        return;
    }
    he_canonical_pop();
}

/* Mark a metta-level frame's answers: its own buffer, or the range its
 * children delivered into the run's buffer. */
static void he_frame_mark_answers(HeCanonicalFrame *frame, OutcomeSet *own) {
    if (frame->relay_redirected && frame->delivery)
        he_mark_answers_of(frame->arena, frame->atom, frame->delivery->set,
            frame->range_start);
    else
        he_mark_answers_of(frame->arena, frame->atom, own, 0u);
}

static void he_answer_evaluated(HeCanonicalFrame *frame, Atom *value,
    Atom *type, bool check, const Bindings *env, OutcomeSet *sink) {
    he_answer_evaluated_into(frame->space, frame->arena, value, type, check,
        frame->preserve_bindings, env, sink);
}

static void he_canonical_result_push(HeCanonicalFrame *parent,
    Atom *type, Outcome *seed, OutcomeSet *target) {
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_RESULT,
        parent->space, parent->arena, parent->atom, type, parent->fuel,
        NULL, parent->preserve_bindings, target);
    if (frame)
        outcome_set_add_existing(&frame->work, seed);
}

/* Transfer the raw frontier to an owned return sequencer. Its saved index
 * advances before each child, so a pause cannot repeat a producer read. */
static bool he_canonical_results_schedule(Space *s, Arena *a,
    Atom *invocation, Atom *declared_type, int fuel, bool preserve_bindings,
    OutcomeSet *raw, OutcomeSet *target) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    HeCanonicalFrame *result = he_canonical_push(HE_CANONICAL_RESULT,
        s, a, invocation, declared_type, fuel, NULL, preserve_bindings, target);
    if (result) {
        for (CettaCount i = 0u; i < raw->len; i++)
            outcome_set_add_existing_move(&result->work, &raw->items[i]);
        result->phase = 2u;
    }
    return true;
}

static void he_canonical_resume_result(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase) {
        if (frame->selected) {
            /* A function delimiter's results are raw at this invocation;
             * its retained original call is the post-argument call. */
            for (CettaCount i = frame->streamed; i < frame->child.len; i++)
                he_canonical_publish_raw_return(frame, &frame->child.items[i]);
        } else {
            he_relay_finish(frame, &frame->child, false, false);
        }
        he_canonical_pop();
        return;
    }
    Outcome *seed = &frame->work.items[0];
    Atom *preview = outcome_preview_atom(seed);
    Atom *candidate = outcome_atom_materialize(a, seed);
    if (he_not_reducible(candidate)) {
        outcome_set_add_prefixed(a, frame->target,
            he_stuck_call_value(a, he_call_return_atom(frame->atom, candidate)),
            &seed->env, NULL, frame->preserve_bindings);
        he_canonical_pop();
        return;
    }
    if (atom_is_legacy_empty_sentinel(preview) ||
        outcome_atom_is_error(a, seed)) {
        he_canonical_publish(frame, &frame->work, false);
        he_canonical_pop();
        return;
    }
    if (atom_head_symbol_id(candidate) == g_builtin_syms.function) {
        frame->phase = 1u;
        frame->selected = true;
        he_canonical_instruction_push(frame, candidate,
            frame->type, &seed->env, false, false, &frame->child);
        return;
    }
    if (atom_is_he_evaluated(candidate)) {
        he_answer_evaluated(frame, candidate, frame->type, true, &seed->env,
            &frame->child);
        he_canonical_publish(frame, &frame->child, false);
        he_canonical_pop();
        return;
    }
    if (preview->kind != ATOM_EXPR || !preview->expr.len ||
        (!prime_need_atom_has_observable_ref(candidate) &&
         atom_is_constructor_normal_form(frame->space, a, candidate, frame->fuel))) {
        bool defer = false;
        if (he_inert_result_cast(frame->space, a, frame->type, seed,
                frame->preserve_bindings, &frame->child, &defer)) {
            he_canonical_publish(frame, &frame->child, false);
            he_canonical_pop();
            return;
        }
        if (!defer) {
            if (preview->kind == ATOM_EXPR && preview->expr.len &&
                (!frame->type || eval_atom_type_demand(a, candidate,
                    frame->type) == HE_TYPE_INTERPRET))
                he_mark_answers_of(a, candidate, &frame->work, 0u);
            he_canonical_publish(frame, &frame->work, false);
            he_canonical_pop();
            return;
        }
    }
    frame->phase = 1u;
    he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
        outcome_atom_materialize_variant_only(a, seed), frame->type,
        frame->fuel, &seed->env, frame->preserve_bindings,
        he_relay_sink(frame, &frame->child));
}

static void he_canonical_resume_bind(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase) {
        he_frame_mark_answers(frame, &frame->child);
        he_relay_finish(frame, &frame->child, false, true);
        he_canonical_pop();
        return;
    }
    if (!eval_completion_step()) {
        he_canonical_pop();
        return;
    }
    if (frame->typed && !frame->type && bindings_logically_empty(&frame->env)) {
        /* The public typed wrapper delegates to the ordinary bind operation. */
        frame->typed = false;
        return;
    }
    if (frame->typed) {
        frame->atom = bindings_apply_if_vars(&frame->env, a, frame->atom);
        if (frame->type)
            frame->type = bindings_apply_if_vars(&frame->env, a, frame->type);
        if (frame->fuel == 0) {
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
            he_canonical_pop();
            return;
        }
    } else if (frame->fuel == 0 && frame->atom->kind == ATOM_EXPR &&
               frame->atom->expr.len)
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
    Atom *registered = registry_lookup_atom(frame->atom);
    if (registered) {
        outcome_set_add(frame->target, registered, &frame->env);
        he_canonical_pop();
        return;
    }
    frame->atom = materialize_runtime_token(frame->space, a, frame->atom);
    Atom *type = frame->type ? frame->type : atom_undefined_type(a);
    HeTypeDemand demand = frame->typed
        ? eval_atom_type_demand(a, frame->atom, type) : HE_TYPE_INTERPRET;
    if (atom_is_he_evaluated(frame->atom)) {
        he_answer_evaluated(frame, frame->atom, frame->typed ? type : NULL,
            frame->typed, &frame->env, frame->target);
        he_canonical_pop();
        return;
    }
    bool kept = atom_is_legacy_empty_or_error(frame->atom) ||
        demand == HE_TYPE_KEEP || frame->atom->kind == ATOM_VAR ||
        (!frame->typed && atom_eval_is_immediate_value(frame->atom, frame->fuel));
    if (kept ||
        (atom_is_symbol_id(type, g_builtin_syms.undefined_type) &&
         eval_has_normal_constructor_value(frame->space, a, frame->atom, frame->fuel)) ||
        (demand == HE_TYPE_INTERPRET &&
         he_value_certified(frame->space, frame->fuel, frame->atom,
             frame->typed ? type : NULL))) {
        outcome_set_add(frame->target,
            kept ? frame->atom : he_answer_as_value(a, frame->atom),
            &frame->env);
        he_canonical_pop();
    } else if (demand == HE_TYPE_CAST) {
        type_cast_outcomes(frame->space, a, frame->atom, type,
            &frame->env, frame->target);
        he_canonical_pop();
    } else {
        frame->phase = 1u;
        HeCanonicalFrame *call = he_canonical_push(HE_CANONICAL_CALL, frame->space, a,
            frame->atom, frame->typed ? type : NULL,
            frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            &frame->env, true, he_relay_sink(frame, &frame->child));
        if (call)
            call->query_closed = frame->query_closed;
    }
}

/* Atom publication delimits the logical frontier. This is the ResultSet
 * observation of evaluation, rather than the binding-producing operation. */
static void he_canonical_resume_evaluate(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    Bindings empty;
    bindings_init(&empty);
    if (frame->phase) {
        he_canonical_finish_visible(frame, true);
        he_canonical_pop();
        return;
    }
    if (!eval_completion_step()) {
        he_canonical_pop();
        return;
    }
    if (frame->fuel == 0) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
        he_canonical_pop();
        return;
    }
    Atom *registered = registry_lookup_atom(frame->atom);
    if (registered) {
        outcome_set_add(frame->target, registered, &empty);
        he_canonical_pop();
        return;
    }
    frame->atom = materialize_runtime_token(frame->space, a, frame->atom);
    Atom *type = frame->type ? frame->type : atom_undefined_type(a);
    HeTypeDemand demand = eval_atom_type_demand(a, frame->atom, type);
    if (atom_is_he_evaluated(frame->atom)) {
        he_answer_evaluated(frame, frame->atom, type, true, &empty,
            frame->target);
        he_canonical_pop();
        return;
    }
    bool kept = atom_is_legacy_empty_or_error(frame->atom) ||
        demand == HE_TYPE_KEEP || frame->atom->kind == ATOM_VAR;
    if (kept ||
        (atom_is_symbol_id(type, g_builtin_syms.undefined_type) &&
         eval_has_normal_constructor_value(frame->space, a, frame->atom, frame->fuel)) ||
        (demand == HE_TYPE_INTERPRET &&
         he_value_certified(frame->space, frame->fuel, frame->atom, type))) {
        outcome_set_add(frame->target,
            kept ? frame->atom : he_answer_as_value(a, frame->atom), &empty);
        he_canonical_pop();
    } else if (demand == HE_TYPE_CAST) {
        ResultSet cast;
        result_set_init(&cast);
        type_cast_fn(frame->space, a, frame->atom, type, frame->fuel, &cast);
        for (CettaCount i = 0u; i < cast.len; i++)
            outcome_set_add(frame->target, cast.items[i], &empty);
        result_set_free(&cast);
        he_canonical_pop();
    } else {
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_CALL, frame->space, a,
            frame->atom, type, frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            NULL, false, &frame->child);
    }
}

static void he_canonical_resume_caller(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase) {
        he_relay_finish(frame, &frame->child, false, true);
        he_canonical_pop();
        return;
    }
    frame->phase = 1u;
    Atom *applied = bindings_apply_if_vars(&frame->env, a, frame->atom);
    if (atom_is_he_evaluated(applied)) {
        he_answer_evaluated(frame, applied, frame->type, true, &frame->env,
            he_relay_sink(frame, &frame->child));
        return;
    }
    bool direct = !frame->type || atom_is_symbol_id(frame->type, g_builtin_syms.undefined_type);
    if (!frame->preserve_bindings && bindings_logically_empty(&frame->env) && direct) {
        CettaCallOutcome producer;
        bool handled = !prime_need_atom_has_observable_ref(applied) &&
            eval_native_application_on(frame->space, a, applied,
                &frame->env, frame->fuel, EVAL_NATIVE_UNTYPED_IMMEDIATE_ARGUMENTS,
                &producer);
        Atom *value = NULL;
        if (handled && sequence_native_call(frame->space, a, producer, false,
                frame->type, &frame->env, he_relay_sink(frame, &frame->child),
                &value))
            return;
        /* What remains is a raw expression result. */
        if (value)
            value = he_call_return_atom(applied, value);
        if (value && (value->kind != ATOM_EXPR || atom_eq(value, applied))) {
            if (!atom_is_legacy_empty_sentinel(value))
                outcome_set_add(he_relay_sink(frame, &frame->child),
                    value, &frame->env);
            return;
        }
        if (value)
            applied = value;
        if (atom_eval_is_immediate_value(applied, frame->fuel)) {
            outcome_set_add(he_relay_sink(frame, &frame->child),
                applied, &frame->env);
            return;
        }
    }
    HeCanonicalFrame *bind = he_canonical_push(HE_CANONICAL_BIND,
        frame->space, a, applied, frame->type, frame->fuel,
        &frame->env, true, he_relay_sink(frame, &frame->child));
    if (bind) {
        bind->typed = true;
        bind->query_closed = frame->query_closed;
    }
}

static bool he_canonical_control_schedule(Space *s, Arena *a, Atom *atom,
    Atom *type, Atom *source, int fuel, const Bindings *env,
    bool preserve, bool query_closed, bool direct_source, OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_CONTROL,
        s, a, atom, type, fuel, env, preserve, os);
    if (frame) {
        cetta_control_operation_decode(atom_head_symbol_id(atom), expr_nargs(atom),
            &frame->control, &g_builtin_syms);
        frame->live[0] = source;
        frame->query_closed = query_closed;
        frame->selected = direct_source;
    }
    return true;
}

static bool he_canonical_state_schedule(Space *s, Arena *a, Atom *atom,
    int fuel, const Bindings *env, bool preserve, OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    he_canonical_push(HE_CANONICAL_STATE, s, a, atom, NULL,
        fuel, env, preserve, os);
    return true;
}

static void he_canonical_resume_state(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (!frame->phase) {
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_BIND, frame->space, a,
            expr_arg(frame->atom, 0u), NULL, frame->fuel,
            NULL, true, &frame->child);
        return;
    }
    if (frame->phase == 2u) {
        Outcome *reference = &frame->child.items[frame->index];
        if (frame->path_index < frame->work.len) {
            Outcome *value = &frame->work.items[frame->path_index++];
            Atom *materialized=outcome_atom_materialize(a,value);
            if (!materialized) {
                if (eval_unwinding()) he_canonical_pop();
                return;
            }
            BindingsBuilder builder;
            if (!bindings_builder_init(&builder, &reference->env))
                return;
            BindingsMergeAttempt attempt;
            if (bindings_builder_merge_or_clone(&builder, &reference->env,
                    &value->env, &attempt)) {
                state_change_publish_value(frame->space, a, frame->atom,
                    frame->live[0], materialized,
                    attempt.env, &frame->env, frame->preserve_bindings, frame->target);
                bindings_merge_attempt_finish(&builder, &attempt);
            }
            bindings_builder_free(&builder);
            return;
        }
        outcome_set_free(&frame->work);
        outcome_set_init(&frame->work);
        frame->live[0] = NULL;
        frame->index++;
        frame->phase = 1u;
    }
    if (frame->index >= frame->child.len) {
        he_canonical_pop();
        return;
    }
    Outcome *reference = &frame->child.items[frame->index];
    Atom *state_ref = resolve_registry_refs(a,
        outcome_atom_materialize(a, reference));
    if (!state_ref) {
        if (eval_unwinding()) he_canonical_pop();
        else frame->index++;
        return;
    }
    if (state_ref->kind != ATOM_GROUNDED || state_ref->ground.gkind != GV_STATE) {
        outcome_set_add_prefixed(a, frame->target, state_bad_arg_type_error(
            frame->space, a, frame->atom, 1, state_ref), &reference->env,
            &frame->env, frame->preserve_bindings);
        frame->index++;
        return;
    }
    if (atom_head_symbol_id(frame->atom) == g_builtin_syms.get_state) {
        StateCell *cell = eval_resolve_state_read(state_ref->ground.ptr);
        outcome_set_add_prefixed(a, frame->target, payload_rebind_resources(a, cell->value),
            &reference->env, &frame->env, frame->preserve_bindings);
        frame->index++;
        return;
    }
    StateCell *cell = payload_resolve_state_write(a, state_ref->ground.ptr);
    if (!cell) {
        outcome_set_add_prefixed(a, frame->target, atom_error(a, frame->atom,
            atom_symbol(a, "PayloadIsolationFailed")), &reference->env,
            &frame->env, frame->preserve_bindings);
        frame->index++;
        return;
    }
    frame->live[0] = atom_state(a, cell);
    Atom *value_source = bindings_apply_if_vars(&reference->env, a,
        expr_arg(frame->atom, 1u));
    frame->path_index = 0u;
    frame->phase = 2u;
    he_canonical_push(HE_CANONICAL_BIND, frame->space, a,
        value_source, NULL, frame->fuel, NULL, true, &frame->work);
}

static void he_canonical_control_body(HeCanonicalFrame *frame,
    Atom *body, const Bindings *env, bool tail, Atom *type) {
    if (tail) {
        body = resolve_registry_refs(frame->arena, body);
        if (frame->fuel == 0) {
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
            return;
        }
    }
    Bindings merged;
    bindings_init(&merged);
    if (bindings_try_merge_live(&merged, &frame->env) &&
        bindings_try_merge_live(&merged, env)) {
        HeCanonicalFrame *body_frame = he_canonical_push(tail ? HE_CANONICAL_CALL : HE_CANONICAL_CALLER,
            frame->space, frame->arena, body, type,
            tail && frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            &merged, frame->preserve_bindings, &frame->paths);
        if (body_frame && tail)
            body_frame->query_closed = frame->query_closed;
    }
    bindings_free(&merged);
}

typedef struct {
    HeCanonicalFrame *frame;
    CettaCount index;
} HeCanonicalConsumedSource;

/* A control's source frontier is closed before its bodies run. Once index
 * advances, that occurrence is never inspected again. The selected body or
 * published answer already owns what it needs; keep the publication frame's
 * environment and original call intact, but retire the consumed source slot.
 * A symbolic if retains its source until both alternatives are scheduled. */
static void he_canonical_consumed_source_release(HeCanonicalConsumedSource *source) {
    if (source->frame->index > source->index)
        outcome_free_fields(&source->frame->child.items[source->index]);
}

/* An if call with its condition's value: the call a NotReducible branch, or a
 * condition that is not a truth value, leaves unreduced. */
static Atom *he_canonical_if_call(Arena *a, Atom *call, Atom *condition) {
    Atom *elems[] = { call->expr.elems[0], condition, expr_arg(call, 1),
        expr_nargs(call) == 3u ? expr_arg(call, 2) : NULL };
    return atom_expr(a, elems, call->expr.len);
}

/* Whether a let's source has shown a value: its rules for errors are then
 * those of a source with a value, whatever else the source produces. */
static bool he_canonical_let_source_decided(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    for (CettaCount i = 0u; i < frame->child.len; i++) {
        Atom *candidate = outcome_atom_materialize(a, &frame->child.items[i]);
        if (!atom_is_error(candidate) &&
            !atom_is_legacy_empty_sentinel(candidate))
            return true;
    }
    return false;
}

/* `source_open`: the control consumes a value while its source still runs
 * (a bounded run); otherwise its source is complete. */
static void he_canonical_resume_control_in(HeCanonicalFrame *frame,
                                           bool source_open) {
    Arena *a = frame->arena;
    SymbolId head = atom_head_symbol_id(frame->atom);
    if (!frame->phase) {
        frame->phase = 1u;
        bool observation = eval_is_reify_head(head);
        if (observation)
            frame->observation = eval_observation_demand_enter(
                (CettaObservationDemand){ .completion = CETTA_OBSERVATION_COMPLETE_BAG },
                frame->live[0]);
        bool capture_source = head == g_builtin_syms.case_text;
        bool atoms_only = observation ||
            head == g_builtin_syms.switch_text || head == g_builtin_syms.assert_text ||
            head == g_builtin_syms.nop || head == g_builtin_syms.bind_bang;
        HeCanonicalFrame *source = he_canonical_push(
            capture_source ? HE_CANONICAL_CALLER :
                (atoms_only ? HE_CANONICAL_EVALUATE : HE_CANONICAL_BIND),
            frame->space, a,
            frame->live[0], NULL, frame->fuel,
            capture_source ? &frame->env : NULL, true, &frame->child);
#ifdef CETTA_HE_LAZY_MUTATION_SOURCE_WHOLE
        /* Fault injection: the source is evaluated in full first. */
        if (source && false) {
#else
        if (source && g_he_canonical_driver->answer_limit &&
            he_canonical_control_streams(head)) {
#endif
            frame->stream_consumer = true;
            frame->stream_source_open = true;
            frame->stream_source = source;
        }
        return;
    }
    if (eval_is_reify_head(head)) {
        Atom **items = frame->child.len
            ? arena_alloc(a, sizeof(Atom *) * (size_t)frame->child.len) : NULL;
        CettaExprLen len = 0u;
        for (CettaCount i = 0u; i < frame->child.len; i++) {
            Atom *item = outcome_atom_materialize(a, &frame->child.items[i]);
            if (!item) {
                if (!eval_unwinding()) continue;
                he_canonical_pop();return;
            }
            if (!atom_is_legacy_empty_sentinel(item))
                items[len++] = item;
        }
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(frame->target, atom_expr(a, items, len), &empty);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.bind_bang) {
        Atom *first = frame->child.len
            ? outcome_atom_materialize(a, &frame->child.items[0]) : NULL;
        if (frame->child.len && !first) {
            he_canonical_pop();return;
        }
        bind_publish_value(a, frame->atom, first,
            &frame->env, frame->preserve_bindings, frame->target);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.nop || head == g_builtin_syms.collapse_bind) {
        Bindings empty;
        bindings_init(&empty);
        Atom *result = atom_unit(a);
        if (head == g_builtin_syms.collapse_bind) {
            Atom **pairs = frame->child.len
                ? arena_alloc(a, sizeof(Atom *) * (size_t)frame->child.len) : NULL;
            CettaExprLen count = 0u;
            for (CettaCount i = 0u; i < frame->child.len; i++) {
                Outcome *out = &frame->child.items[i];
                Atom *value = outcome_atom_materialize(a, out);
                if (!value) {
                    if (!eval_unwinding()) continue;
                    he_canonical_pop();return;
                }
                if (!atom_is_legacy_empty_sentinel(value))
                    pairs[count++] = atom_expr2(a, value, bindings_capture_value(a, &out->env));
            }
            result = atom_expr(a, pairs, count);
        }
        outcome_set_add_prefixed(a, frame->target, result, &empty,
            &frame->env, frame->preserve_bindings);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.case_text && !source_open && frame->phase == 1u) {
        he_case_capture_complete(a, &frame->child, &frame->index);
        frame->phase = 2u;
    }
    if (head == g_builtin_syms.case_text && !source_open && !frame->child.len) {
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(&frame->child, atom_empty(a), &empty);
    }
    if (frame->index >= frame->child.len) {
        he_canonical_finish(frame, &frame->paths, true);
        he_canonical_pop();
        return;
    }
    Outcome *source = &frame->child.items[frame->index];
    __attribute__((cleanup(he_canonical_consumed_source_release)))
    HeCanonicalConsumedSource consumed = {frame, frame->index};
    Atom *value = outcome_atom_materialize(a, source);
    if (head == g_builtin_syms.case_text && source_open &&
        atom_is_legacy_empty_sentinel(value)) {
        frame->index++;
        return;
    }
    /* While the source runs, no value is known to be its only one. */
    bool single = !source_open && frame->child.len == 1u;
    if (frame->control.operation == CETTA_CONTROL_SEQUENCE && frame->phase == 1u) {
        if (source_open) {
            /* Until the source shows a value, its errors wait for the rest
             * of the source. */
            if (!he_canonical_let_source_decided(frame)) {
                frame->stream_wait_len = frame->child.len;
                return;
            }
            frame->has_functions = false;
            frame->has_non_functions = true;
        } else {
            frame->has_functions = frame->child.len > 0u;
            for (CettaCount i = 0u; i < frame->child.len; i++) {
                Atom *candidate = outcome_atom_materialize(a, &frame->child.items[i]);
                bool error = atom_is_error(candidate);
                frame->has_functions &= error;
                frame->has_non_functions |= !error && !atom_is_legacy_empty_sentinel(candidate);
            }
        }
        frame->phase = 2u;
    }
    if (head == g_builtin_syms.case_text || head == g_builtin_syms.switch_text) {
        frame->index++;
        Atom *branches = expr_arg(frame->atom, 1);
        for (CettaExprIndex i = 0u; branches->kind == ATOM_EXPR &&
                i < branches->expr.len; i++) {
            Atom *branch = branches->expr.elems[i];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u)
                continue;
            BindingsBuilder matched;
            bool capture_source = head == g_builtin_syms.case_text;
            if (!bindings_builder_init(&matched,
                    capture_source ? &frame->env : NULL))
                return;
            bool match = capture_source
                ? he_case_match_source_builder(branch->expr.elems[0], value,
                    &source->env, &matched, a)
                : eval_case_match_builder(branch->expr.elems[0], value, &matched, a);
            if (match) {
                const Bindings *env = bindings_builder_bindings(&matched);
                Atom *body = bindings_apply_if_vars(env, a, branch->expr.elems[1]);
                Bindings empty;
                bindings_init(&empty);
                he_canonical_control_body(frame, body,
                    single && !frame->preserve_bindings && !capture_source ? &empty : env,
                    single, single ? frame->type : NULL);
            }
            bindings_builder_free(&matched);
            if (match)
                break;
        }
        return;
    }
    if (head == g_builtin_syms.assert_text) {
        frame->index++;
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(&frame->paths, is_true_atom(value) ? atom_unit(a)
            : atom_error(a, frame->atom, atom_expr3(a, frame->live[0],
                atom_symbol(a, "not"), atom_bool(a, true))), &empty);
        return;
    }
    if (frame->control.operation == CETTA_CONTROL_BRANCH) {
        bool truth = is_true_atom(value);
        bool falsity = is_false_atom(value);
        if (truth || falsity) {
            frame->index++;
            Atom *body = truth ? expr_arg(frame->atom, frame->control.first)
                : frame->control.last != CETTA_CONTROL_OPERAND_ABSENT ? expr_arg(frame->atom, frame->control.last)
                : legacy_empty_sentinel(a);
            if (he_not_reducible(body)) {
                outcome_set_add(&frame->paths,
                    he_canonical_if_call(a, frame->atom, value), &source->env);
            } else {
                body = bindings_apply_if_vars(&source->env, a, body);
                he_canonical_control_body(frame, body, &source->env,
                    single, frame->type);
            }
        } else if (atom_is_legacy_empty_sentinel(value)) {
            frame->index++;
        } else if (atom_is_error(value)) {
            frame->index++;
            outcome_set_add_existing(&frame->paths, source);
        } else if (value->kind == ATOM_VAR) {
            BindingsBuilder refined;
            bool initialized = bindings_builder_init(&refined, &source->env);
            unsigned branch = (unsigned)frame->head_index++;
            if (frame->head_index == 2u) {
                frame->index++;
                frame->head_index = 0u;
            }
            if (initialized && bindings_builder_add_var_fresh(&refined, value,
                    atom_symbol_id(a, branch ? g_builtin_syms.false_text
                        : g_builtin_syms.true_text))) {
                Atom *body = !branch ? expr_arg(frame->atom, frame->control.first)
                    : frame->control.last != CETTA_CONTROL_OPERAND_ABSENT ? expr_arg(frame->atom, frame->control.last)
                    : legacy_empty_sentinel(a);
                if (he_not_reducible(body))
                    outcome_set_add(&frame->paths,
                        he_canonical_if_call(a, frame->atom, value),
                        bindings_builder_bindings(&refined));
                else
                    he_canonical_control_body(frame, body,
                        bindings_builder_bindings(&refined), false, frame->type);
            }
            if (initialized)
                bindings_builder_free(&refined);
        } else {
            frame->index++;
            Atom **types = NULL;
            uint32_t count = eval_get_atom_types_profiled(frame->space, a, value, &types);
            Atom *boolean = atom_symbol(a, "Bool");
            bool concrete = false, may_bool = false;
            for (uint32_t i = 0u; i < count; i++) {
                if (atom_is_symbol_id(types[i], g_builtin_syms.undefined_type) ||
                    atom_is_meta_type(types[i]))
                    continue;
                if (atom_eq_fast(types[i], boolean))
                    may_bool = true;
                else
                    concrete = true;
            }
            free(types);
            if ((concrete || value->kind == ATOM_GROUNDED) && !may_bool)
                outcome_set_add(&frame->paths, bad_arg_type_error(frame->space, a,
                    frame->atom, 1, boolean, value), &source->env);
            else
                outcome_set_add(&frame->paths,
                    he_canonical_if_call(a, frame->atom, value), &source->env);
        }
        return;
    }
    assert(frame->control.operation == CETTA_CONTROL_SEQUENCE);
    frame->index++;
    if (atom_is_legacy_empty_sentinel(value))
        return;
    if ((frame->selected && atom_is_error(value)) || frame->has_functions) {
        if (frame->selected && frame->has_non_functions)
            return;
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(&frame->paths, value, &empty);
        return;
    }
    Atom *pattern = expr_arg(frame->atom, frame->control.first);
    Atom *body = expr_arg(frame->atom, frame->control.last);
    BindingsBuilder matched;
    if (!bindings_builder_init(&matched, &source->env))
        return;
    bool admitted = pattern->kind == ATOM_VAR
        ? bindings_builder_add_var_fresh(&matched, pattern, value)
        : match_atoms_builder(value, pattern, &matched, a);
    if (admitted && pattern->kind != ATOM_VAR &&
        !he_orient_let_value(a, value, &matched)) {
        bindings_builder_free(&matched);
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return;
    }
    if (admitted) {
        Bindings visible;
        if (bindings_project_body_visible_env(a, body,
                bindings_builder_bindings(&matched), &visible)) {
            body = bindings_apply_projected_body_visible(&visible, a, body);
            Bindings empty;
            bindings_init(&empty);
            bool retained = frame->selected
                ? !(pattern->kind == ATOM_VAR &&
                    !atom_contains_vars(expr_arg(frame->atom, frame->control.last)))
                : !single || (frame->preserve_bindings && !frame->query_closed);
            he_canonical_control_body(frame, body, retained
                ? bindings_builder_bindings(&matched) : &empty,
                single && !frame->selected,
                single && !frame->selected ? frame->type : NULL);
            bindings_free(&visible);
        }
    }
    bindings_builder_free(&matched);
}

static void he_canonical_resume_control(HeCanonicalFrame *frame) {
    he_canonical_resume_control_in(frame, false);
}

/* Whether a control's source, and every frame above it, has no work left. */
static bool he_canonical_source_finishing(HeCanonicalDriver *driver,
                                          HeCanonicalFrame *consumer) {
    HeCanonicalFrame *source = consumer->stream_source;
    for (HeCanonicalFrame *frame = (HeCanonicalFrame *)driver->stack.top;
         source && frame; frame = (HeCanonicalFrame *)frame->link.next) {
        if (!he_canonical_frame_finishing(frame))
            return false;
        if (frame == source)
            return true;
    }
    return false;
}

/* Consume one value of each queued control whose source still runs.  The
 * body it starts runs before the source computes another value; its
 * answers go to the control's answers.  Returns whether a control made
 * progress. */
static bool he_canonical_stream_consume(HeCanonicalDriver *driver) {
    while (driver->stream_ready && !driver->satisfied) {
        HeCanonicalFrame *consumer = driver->stream_ready;
        he_canonical_stream_unqueue(driver, consumer);
        if (!consumer->stream_source_open || consumer->stream_body_running ||
            consumer->index >= consumer->child.len)
            continue;
        /* A source with no work left completes before it could compute
         * another value; the control then reads it whole, as an unbounded
         * run does (a single value becomes the body's tail call). */
        if (he_canonical_source_finishing(driver, consumer))
            continue;
        size_t depth = driver->stack.depth;
        CettaCount index = consumer->index;
        CettaCount head_index = consumer->head_index;
        CettaCount paths = consumer->paths.len;
        he_canonical_resume_control_in(consumer, true);
        if (driver->stack.depth > depth) {
            HeCanonicalFrame *body = (HeCanonicalFrame *)driver->stack.top;
            body->stream_owner = consumer;
            body->stream_to_root = false;
            body->stream_spine = consumer->stream_spine;
            body->stream_body_of = consumer;
            consumer->stream_body_running = true;
        }
        bool progressed = driver->stack.depth > depth ||
            consumer->index != index || consumer->head_index != head_index ||
            consumer->paths.len != paths;
        he_canonical_stream(driver, consumer);
        if (progressed)
            return true;
    }
    return false;
}

#include "eval_he_pattern_control.inc"

static bool he_canonical_dispatch_schedule(Space *s, Arena *a, Atom *atom,
    Atom *type, int fuel, const Bindings *env, bool preserve, OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    bool closed = he_canonical_query_closed(eval_gc_enabled() &&
        !atom_contains_vars(atom));
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_DISPATCH,
        s, a, atom, type, fuel, env, preserve, os);
    if (frame)
        frame->query_closed = closed;
    return true;
}

static bool he_canonical_project_contract(HeCanonicalFrame *frame,
    Outcome *head, Outcome *path, Bindings *out) {
    MatchVisibleVarSet visible;
    match_visible_var_set_init(&visible);
    collect_match_visible_vars_rec(frame->atom, &visible);
    collect_match_visible_vars_rec(frame->type, &visible);
    collect_match_visible_vars_rec(outcome_atom_materialize(frame->arena, head), &visible);
    BindingsBuilder acquired;
    bool initialized = bindings_builder_init(&acquired, &frame->env);
    bool ok = initialized && bindings_builder_merge_commit(&acquired, &head->env) &&
        bindings_builder_merge_commit(&acquired, &path->env);
    Bindings projected;
    bindings_init(&projected);
    ok = ok && project_match_visible_bindings(frame->arena, &visible,
        bindings_builder_bindings(&acquired), &projected);
    if (initialized)
        bindings_builder_free(&acquired);
    match_visible_var_set_free(&visible);
    BindingsBuilder merged;
    initialized = ok && bindings_builder_init(&merged, &frame->env);
    ok = initialized && bindings_builder_merge_commit(&merged, &projected);
    bindings_free(&projected);
    if (ok)
        bindings_builder_take(&merged, out);
    if (initialized)
        bindings_builder_free(&merged);
    return ok;
}

static void he_canonical_tail_push(HeCanonicalFrame *frame,
    Atom *body, Atom *type, const Bindings *env, OutcomeSet *target) {
    body = resolve_registry_refs(frame->arena, body);
    if (he_not_reducible(body)) {
        outcome_set_add(target, body, env);
    } else if (atom_head_symbol_id(body) == g_builtin_syms.function) {
        he_canonical_instruction_push(frame, body, type,
            env, false, false, target);
    } else if (frame->fuel == 0) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
    } else {
        HeCanonicalFrame *tail = he_canonical_push(HE_CANONICAL_CALL, frame->space, frame->arena,
            body, type ? type : frame->type,
            frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            env, frame->preserve_bindings, target);
        if (tail)
            tail->query_closed = frame->query_closed;
    }
}

static void he_canonical_dispatch_call(HeCanonicalFrame *frame, Outcome *call) {
    Arena *a = frame->arena;
    OutcomeSet *sink = he_relay_sink(frame, &frame->paths);
    Atom *source = outcome_atom_materialize(a, call);
    Bindings *env = &call->env;
    /* A call whose parts all evaluated to themselves is its source: publish
     * that atom, so a value keeps the sharing it was built with. */
    if (source && source != frame->atom && source == call->atom &&
        call->kind == CETTA_OUTCOME_INLINE &&
        !variant_instance_present(&call->variant) &&
        he_value_same_parts(source, frame->atom)) {
        call->atom = frame->atom;
        call->materialized_atom = frame->atom;
        source = frame->atom;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HE_COMPLETED_VALUE_SHARED);
    }
    if (atom_is_legacy_empty_or_error(source)) {
        outcome_set_add_existing(sink, call);
        return;
    }
    source = resolve_registry_refs(a, source);
    Atom *return_type = frame->selected ? frame->live[1] : frame->type;
    if (return_type)
        return_type = bindings_apply_if_vars(env, a, return_type);
    if (he_dispatch_host_read_outcomes(frame->space, a, source, return_type,
            frame->fuel, env, frame->preserve_bindings, sink))
        return;
    /* Curried arrows: a computed partial application applied further is
     * the call of its head with every argument, read afresh. */
    if (source->kind == ATOM_EXPR && source->expr.len > 1u &&
        source->expr.elems[0]->kind == ATOM_EXPR &&
        he_curried_arrows_enabled()) {
        Atom *flat = he_curried_flatten(frame->space, a, source);
        if (flat != source) {
            if (frame->fuel == 0) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
                return;
            }
            HeCanonicalFrame *call = he_canonical_push(HE_CANONICAL_CALL,
                frame->space, a, flat, return_type,
                frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
                env, frame->preserve_bindings, sink);
            if (call)
                call->query_closed = frame->query_closed;
            return;
        }
    }
    /* As on the recursive tuple route, a minimal instruction remains
     * executable in a space without a declared function signature. */
    if ((he_minimal_instruction(source) ||
        (frame->selected && atom_head_symbol_id(source) == g_builtin_syms.superpose)) &&
        atom_head_symbol_id(source) != g_builtin_syms.call_native) {
        he_canonical_instruction_push(frame, source, return_type, env,
            true, frame->typed, sink);
        return;
    }
    if (source->kind == ATOM_EXPR && source->expr.len) {
        Atom *head = source->expr.elems[0];
        CettaExprLen nargs = source->expr.len - 1u;
        Atom *tail = NULL, *tail_type = NULL;
        Bindings tail_env;
        bindings_init(&tail_env);
        bool foreign = dispatch_foreign_outcomes(frame->space, a, head,
            source->expr.elems + 1u, (uint32_t)nargs, return_type, frame->fuel,
            env, true, frame->preserve_bindings, &tail, &tail_type,
            &tail_env, sink);
        if (foreign) {
            if (tail)
                he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                    tail, tail_type, frame->fuel, &tail_env,
                    frame->preserve_bindings, sink);
            bindings_free(&tail_env);
            return;
        }
        bindings_free(&tail_env);
        if (is_capture_closure(head)) {
            if (g_hyperpose_thread_unsafe_requested &&
                eval_mark_hyperpose_thread_unsafe()) {
                outcome_set_add(sink,
                    legacy_empty_sentinel(a), env);
            } else if (nargs != 1u) {
                outcome_set_add(sink, atom_error(a, source,
                    atom_symbol(a, "IncorrectNumberOfArguments")), env);
            } else {
                CaptureClosure *closure = head->ground.ptr;
                he_canonical_push(HE_CANONICAL_CAPTURE,
                    closure->space_ptr, a, source, NULL, frame->fuel,
                    env, frame->preserve_bindings, sink);
            }
            return;
        }
        if (is_petta_runtime_callable(head)) {
            (void)dispatch_petta_callable_outcomes(frame->space, a, head,
                source->expr.elems + 1u, (uint32_t)nargs, frame->fuel,
                env, frame->preserve_bindings, sink);
            return;
        }
        if (he_curried_arrows_enabled() && head->kind == ATOM_SYMBOL &&
            is_grounded_op(atom_symbol_current_id(head)) &&
            he_curried_grounded_partial(atom_symbol_current_id(head), nargs)) {
            /* A grounded operation keeps its arity: applied to fewer
             * arguments, under curried arrows, it is a value. */
            outcome_set_add(sink, source, env);
            return;
        }
        if (head->kind == ATOM_SYMBOL && is_grounded_op(atom_symbol_current_id(head))) {
            CettaCallOutcome produced;
            bool handled = dispatch_native_invocation(frame->space, a,
                &(CettaInvocation){.head = binding_value_from_atom(head),
                    .supplied = {.atoms = source->expr.elems + 1u, .count = nargs}},
                &produced);
            Atom *value = NULL;
            if (handled && sequence_native_call(
                    frame->space, a, produced, eval_grounded_result_is_final(),
                    return_type, env, sink, &value))
                return;
            /* What remains is a raw expression result. */
            if (value) {
                if (he_not_reducible(value)) {
                    outcome_set_add(sink, he_call_return_atom(source, value), env);
                } else if (atom_head_symbol_id(value) == g_builtin_syms.function) {
                    HeCanonicalFrame *returned = he_canonical_push(HE_CANONICAL_RESULT,
                        frame->space, a, source, return_type, frame->fuel,
                        NULL, frame->preserve_bindings, sink);
                    if (returned)
                        outcome_set_add(&returned->work, value, env);
                } else if (frame->typed)
                    he_canonical_tail_push(frame, value, return_type,
                        env, sink);
                else
                    he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                        value, return_type, frame->fuel, env,
                        frame->preserve_bindings, sink);
                return;
            }
        }
    }
    /* Matching completes before bodies run. Every pending occurrence owns
     * its fresh bindings; later calls acquire the source's current revision. */
    OutcomeSet pending;
    outcome_set_init(&pending);
    HeRawQuery query = { .arena = a, .prefix = env, .outcomes = &pending };
    CettaCount matched = eval_query_equations_visit(frame->space, source, a,
        he_raw_query_result, &query);
    if (!matched) {
        /* Its own value, answered with the bindings it was asked under: no
         * argument check or argument added one. */
        if (frame->selected && frame->typed &&
            (source == frame->atom ||
             he_value_same_value(source, frame->atom)) &&
            bindings_eq(&call->env, &frame->env)) {
            he_value_certify(frame->space, a, frame->fuel, frame->atom);
            /* A certified source is its own value with its parts: answering
             * it keeps the sharing it was built with, where the rebuilt
             * call holds a fresh copy of each part. */
            if (source != frame->atom && call->kind == CETTA_OUTCOME_INLINE &&
                !variant_instance_present(&call->variant) &&
                he_value_find(frame->atom)) {
                call->atom = frame->atom;
                call->materialized_atom = frame->atom;
            }
        }
        he_stuck_call_outcome(a, call);
        outcome_set_add_existing(sink, call);
        outcome_set_free(&pending);
        return;
    }
    /* An equation whose body is NotReducible leaves this call unreduced:
     * the return sequencer answers that alternative with the call, as
     * when no equation applies, and the marker never runs as a body. */
    if (frame->typed && pending.len == 1u &&
        !he_call_result_needs_invocation(
            outcome_atom_materialize(a, &pending.items[0]))) {
        SearchContext context;
        if (search_context_init(&context, env, NULL)) {
            Atom *body = NULL, *type = NULL;
            Bindings continuation;
            bindings_init(&continuation);
            Outcome *seed = &pending.items[0];
            if (query_delayed_result_apply_single_tail(NULL, &context,
                    env, a, return_type, frame->preserve_bindings,
                    seed->atom, &seed->env, &seed->variant,
                    &body, &type, &continuation))
                he_canonical_tail_push(frame, body, type,
                    &continuation, sink);
            bindings_free(&continuation);
            search_context_free(&context);
        }
        outcome_set_free(&pending);
        return;
    }
    /* Save this complete producer frontier in a child return sequencer. */
    he_canonical_results_schedule(frame->space, a, source, return_type,
        frame->fuel, frame->preserve_bindings, &pending, sink);
    outcome_set_free(&pending);
}

static void he_canonical_resume_dispatch(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase == 0u) {
        SymbolId head = atom_head_symbol_id(frame->atom);
        if (eval_current_pattern_control_arity(head)) {
            frame->kind = HE_CANONICAL_PATTERN;
            return;
        }
        bool curried = he_curried_arrows_enabled();
        if (curried)
            frame->atom = he_curried_flatten(frame->space, a, frame->atom);
        Atom *op = frame->atom->expr.elems[0];
        uint32_t nargs = 0u;
        if (!expr_nargs_u32(frame->atom, &nargs)) {
            outcome_set_add(frame->target,
                expr_arity_too_large_error(a, frame->atom), &frame->env);
            he_canonical_pop();
            return;
        }
        bool total_structural_eq = atom_is_symbol_id(op, g_builtin_syms.op_eq) &&
            nargs == 2u && active_profile_uses_total_structural_eq();
        frame->type_count = eval_get_atom_types_profiled(frame->space, a, op, &frame->types);
        /* Curried arrows: an over-application `(f a1 .. ak)` is the call
         * `((f a1 .. an) an+1 .. ak)`, n the largest arity whose result
         * arrow takes the rest; a partial application reads its types
         * curried at the arguments given. */
        unsigned curried_rank = curried && nargs
            ? space_curried_call_rank(frame->types, frame->type_count, nargs) : 0u;
        while (curried_rank == 2u) {
            CettaExprLen split = 0u;
            for (uint32_t i = 0u; i < frame->type_count; i++)
                if (space_curried_type_rank(frame->types[i], nargs) == 2u &&
                    get_function_arg_count(frame->types[i]) > split)
                    split = get_function_arg_count(frame->types[i]);
            Atom **inner = arena_alloc(a, sizeof(Atom *) * (size_t)(split + 1u));
            Atom **outer = arena_alloc(a, sizeof(Atom *) * (size_t)(nargs - split + 1u));
            for (CettaExprIndex i = 0u; i <= split; i++)
                inner[i] = frame->atom->expr.elems[i];
            outer[0] = atom_expr(a, inner, split + 1u);
            for (CettaExprIndex i = split + 1u; i <= nargs; i++)
                outer[i - split] = frame->atom->expr.elems[i];
            frame->atom = atom_expr(a, outer, nargs - split + 1u);
            op = frame->atom->expr.elems[0];
            nargs = nargs - split;
            free(frame->types);
            frame->types = NULL;
            frame->type_count = eval_get_atom_types_profiled(frame->space, a,
                op, &frame->types);
            curried_rank = space_curried_call_rank(frame->types,
                frame->type_count, nargs);
        }
        if (frame->types)
            eval_gc_root_frame_enter_outcome_continuation(&frame->type_root,
                frame->types, frame->type_count, NULL, NULL);
        ApplicabilityErrors all_errors;
        applicability_errors_init(&all_errors);
        bool resolved = he_type_judgments() == HE_TYPE_JUDGMENTS_RESOLVED;
        for (uint32_t i = 0u; i < frame->type_count; i++) {
            if (!is_function_type(frame->types[i])) {
                frame->has_non_functions = true;
                continue;
            }
            if (grounded_op_is_sequence_constructor(head) &&
                get_function_arg_count(frame->types[i]) != nargs)
                continue;
            frame->has_functions = true;
            if (curried_rank &&
                space_curried_type_rank(frame->types[i], nargs) != curried_rank)
                continue;
            Atom *fresh = resolved && !atom_has_vars(frame->types[i])
                ? frame->types[i]
                : cetta_instantiate_frame_syntax(a, frame->types[i]);
            Atom *declared = fresh;
            if (curried_rank)
                fresh = space_curried_function_type(a, fresh, nargs);
            ApplicabilityErrors errors;
            applicability_errors_init(&errors);
            ApplicabilityTypes contracts;
            applicability_types_init(&contracts);
            bool ok = (resolved && !curried_rank && frame->type_count == 1u &&
                       he_type_direct_applicable(frame->space, a, frame->atom,
                           fresh, frame->type, &frame->work)) ||
                check_function_applicable(frame->atom, fresh,
                    frame->type ? frame->type : atom_undefined_type(a),
                    frame->space, a, frame->fuel, frame->type_count > 1u,
                    &errors, &contracts, &frame->work);
            applicability_types_free(&contracts);
            if (ok) {
                frame->selected = true;
                frame->live[0] = fresh;
                /* The head is evaluated at its declared type, which its
                 * curried reading only restructures.  (live[2] names the
                 * requested type of a delivery's owner.) */
                frame->live[3] = curried_rank ? declared : NULL;
                applicability_errors_free(&errors);
                break;
            }
            for (uint32_t j = 0u; j < errors.len; j++)
                applicability_errors_push(&all_errors, errors.items[j]);
            applicability_errors_free(&errors);
        }
        if (!frame->selected && frame->has_functions && !total_structural_eq) {
            if (!frame->has_non_functions || eval_type_check_auto_enabled())
                for (uint32_t i = 0u; i < all_errors.len; i++)
                    outcome_set_add(frame->target, all_errors.items[i], &frame->env);
            if (!frame->has_non_functions) {
                applicability_errors_free(&all_errors);
                he_canonical_pop();
                return;
            }
        }
        applicability_errors_free(&all_errors);
        if (frame->selected) {
            frame->phase = 1u;
        } else {
            frame->phase = 5u;
            he_canonical_arguments_push(frame, NULL, frame->atom->expr.elems,
                NULL, frame->atom->expr.len, &frame->env, &frame->child, false);
            return;
        }
    }
    if (frame->phase == 1u) {
        if (frame->path_index >= frame->work.len) {
            he_canonical_pop();
            return;
        }
        frame->live[1] = he_type_result_demand(a,
            outcome_atom_materialize(a, &frame->work.items[frame->path_index]));
        frame->head_index = 0u;
        outcome_set_free(&frame->child);
        outcome_set_init(&frame->child);
        frame->phase = 2u;
        he_canonical_argument_schedule(frame->space, a,
            frame->live[3] ? frame->live[3] : frame->live[0],
            frame->atom->expr.elems[0], frame->fuel, &frame->env, &frame->child);
        return;
    }
    if (frame->phase == 2u) {
        if (frame->head_index >= frame->child.len) {
            frame->path_index++;
            frame->phase = 1u;
            return;
        }
        Outcome *head = &frame->child.items[frame->head_index];
        Atom *op = outcome_atom_materialize(a, head);
        if (atom_is_legacy_empty_or_error(op) && !atom_eq(op, frame->atom->expr.elems[0])) {
            outcome_set_add_prefixed_outcome(a, frame->target, head,
                &frame->env, frame->preserve_bindings);
            frame->head_index++;
            return;
        }
        Bindings applicable;
        bindings_init(&applicable);
        if (!he_canonical_project_contract(frame, head,
                &frame->work.items[frame->path_index], &applicable)) {
            bindings_free(&applicable);
            frame->head_index++;
            return;
        }
        CettaExprLen nargs = frame->atom->expr.len - 1u;
        Atom **domains = nargs ? cetta_malloc(sizeof(Atom *) * (size_t)nargs) : NULL;
        for (CettaExprIndex i = 0u; i < nargs; i++)
            domains[i] = function_domain_refine(
                &frame->work.items[frame->path_index].env, a,
                frame->live[0]->expr.elems[i + 1u]);
        outcome_set_free(&frame->paths);
        outcome_set_init(&frame->paths);
        frame->index = 0u;
        frame->phase = 3u;
        he_canonical_arguments_push(frame, op, frame->atom->expr.elems + 1u,
            domains, nargs, &applicable, &frame->paths, true);
        free(domains);
        bindings_free(&applicable);
        return;
    }
    if (frame->phase == 3u) {
        if (frame->index >= frame->paths.len) {
            /* Completed results are in the target, not an applicability
             * accumulator: a later empty branch cannot erase earlier ones. */
            frame->head_index++;
            frame->phase = 2u;
            return;
        }
        Outcome *call = &frame->paths.items[frame->index++];
        /* The call frontier and return frontier must be different owners. */
        HeCanonicalFrame *delivery = he_canonical_push(HE_CANONICAL_DISPATCH,
            frame->space, a, frame->atom, frame->live[1], frame->fuel,
            &frame->env, frame->preserve_bindings, frame->target);
        if (delivery) {
            delivery->selected = true;
            delivery->query_closed = frame->query_closed;
            delivery->typed = frame->type_count == 1u &&
                frame->work.len == 1u && frame->child.len == 1u &&
                frame->paths.len == 1u;
            delivery->live[1] = frame->live[1];
            delivery->live[2] = frame->type;
            outcome_set_add_existing(&delivery->child, call);
            delivery->phase = 6u;
            if (delivery->typed) {
                /* All applicability, head and argument alternatives are
                 * consumed. Only the delivery publishes this invocation. */
                frame->phase = 8u;
                outcome_set_free(&frame->work);
                outcome_set_free(&frame->child);
                outcome_set_free(&frame->paths);
                if (frame->types) {
                    eval_gc_root_frame_leave(&frame->type_root);
                    free(frame->types);
                    frame->types = NULL;
                    frame->type_count = 0u;
                }
                he_canonical_retire_input_roots(frame);
            }
        }
        return;
    }
    if (frame->phase == 5u) {
        if (frame->index >= frame->child.len) {
            he_relay_finish(frame, &frame->paths, true, false);
            he_canonical_pop();
            return;
        }
        frame->typed = frame->child.len == 1u;
        CettaCount consumed = frame->index++;
        he_canonical_dispatch_call(frame, &frame->child.items[consumed]);
        outcome_free_fields(&frame->child.items[consumed]);
        return;
    }
    if (frame->phase == 6u) {
        frame->phase = 7u;
        he_canonical_dispatch_call(frame, &frame->child.items[0]);
        outcome_free_fields(&frame->child.items[0]);
        return;
    }
    if (frame->phase == 7u) {
        he_relay_finish(frame, &frame->paths, true, false);
        he_canonical_pop();
    } else if (frame->phase == 8u) {
        he_canonical_pop();
    }
}

static bool he_canonical_match_schedule(Space *s, Arena *a, Atom *atom,
    Atom *type, int fuel, const Bindings *env, bool preserve,
    OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled() ||
        atom_head_symbol_id(atom) != g_builtin_syms.match)
        return false;
    CettaExprLen nargs = expr_nargs(atom);
    if (nargs == 2u) {
        Atom *conjunction = resolve_registry_refs(a, expr_arg(atom, 0));
        if (conjunction->kind != ATOM_EXPR || conjunction->expr.len < 2u ||
            !atom_is_symbol_id(conjunction->expr.elems[0], g_builtin_syms.comma))
            return false;
    } else if (nargs == 3u) {
        CettaMorkSpaceHandle *bridge = NULL;
        Atom *space_ref = resolve_registry_refs(a, expr_arg(atom, 0));
        if (g_library_context && cetta_library_lookup_explicit_mork_bridge(
                g_library_context, space_ref, &bridge) && bridge)
            return false;
    } else
        return false;
    bool closed = he_canonical_query_closed(eval_gc_enabled() &&
        !atom_contains_vars(atom));
    match_chain_configure_observer();
    HeCanonicalKind kind = cetta_gslt_observation_visible(
        CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS, g_match_chain_observer_diagnostic)
        ? HE_CANONICAL_MATCH_DIAGNOSTIC : HE_CANONICAL_MATCH;
    HeCanonicalFrame *frame = he_canonical_push(kind,
        s, a, atom, type, fuel, env, preserve, os);
    if (frame) {
        frame->sourced_match = nargs == 2u;
        frame->query_closed = closed;
    }
    return true;
}

static void he_canonical_match_error(HeCanonicalFrame *frame, const char *reason) {
    outcome_set_add(frame->target, space_arg_error(frame->arena,
        frame->atom, reason), &frame->env);
    he_canonical_pop();
}

static void he_canonical_match_initialize(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    Atom *conjunction = frame->sourced_match
        ? resolve_registry_refs(a, expr_arg(frame->atom, 0)) : NULL;
    CettaExprLen capacity = frame->sourced_match
        ? conjunction->expr.len - 1u : 16u;
    HeCanonicalRegisters *registers = cetta_malloc(sizeof(*registers));
    memset(registers, 0, sizeof(*registers));
    registers->len = frame->sourced_match ? capacity : 1u;
    registers->source = cetta_malloc(sizeof(Atom *) * (size_t)capacity);
    registers->types = cetta_malloc(sizeof(Atom *) * (size_t)capacity);
    registers->prefix = cetta_malloc(sizeof(Atom *) * (size_t)capacity);
    memset(registers->source, 0, sizeof(Atom *) * (size_t)capacity);
    memset(registers->types, 0, sizeof(Atom *) * (size_t)capacity);
    memset(registers->prefix, 0, sizeof(Atom *) * (size_t)capacity);
    frame->match_spaces = cetta_malloc(sizeof(Space *) * (size_t)capacity);
    memset(frame->match_spaces, 0, sizeof(Space *) * (size_t)capacity);
    frame->registers = registers;
    frame->owns_registers = true;
    eval_gc_root_frame_enter_function_args_machine(&registers->root,
        &registers->op, registers->source, (size_t)capacity,
        registers->types, (size_t)capacity, registers->prefix,
        (size_t)capacity, &frame->work);
    if (frame->sourced_match) {
        for (CettaExprIndex i = 0u; i < capacity; i++) {
            Atom *leg = conjunction->expr.elems[i + 1u];
            if (leg->kind != ATOM_EXPR || leg->expr.len != 2u) {
                he_canonical_match_error(frame,
                    "each sourced conjunct must be (space pattern)");
                return;
            }
            registers->source[i] = resolve_registry_refs(a, leg->expr.elems[1]);
            registers->types[i] = leg->expr.elems[0];
        }
        frame->live[0] = resolve_registry_refs(a, expr_arg(frame->atom, 1));
    } else {
        registers->source[0] = resolve_registry_refs(a, expr_arg(frame->atom, 1));
        registers->types[0] = expr_arg(frame->atom, 0);
        frame->live[0] = resolve_registry_refs(a, expr_arg(frame->atom, 2));
    }
    frame->phase = 1u;
}

typedef struct {
    Arena *arena;
    OutcomeSet *rows;
} HeCanonicalJoinedRows;

static bool he_canonical_joined_row(const Bindings *row, void *raw) {
    HeCanonicalJoinedRows *joined = raw;
    outcome_set_add(joined->rows, atom_unit(joined->arena), row);
    return true;
}

/* Whether the first `legs` legs of a fused chain read one PathMap space. */
static bool he_canonical_legs_share_pathmap(const HeCanonicalFrame *frame,
                                            CettaExprLen legs) {
    Space *space = frame->match_spaces[0];
    if (!space || !space_engine_uses_pathmap(space->match_backend.kind))
        return false;
    for (CettaExprIndex leg = 1u; leg < legs; leg++) {
        if (frame->match_spaces[leg] != space)
            return false;
    }
    return true;
}

static void he_canonical_match_plan(HeCanonicalFrame *frame) {
    HeCanonicalRegisters *registers = frame->registers;
    Arena *a = frame->arena;
    if (!frame->sourced_match) {
        Atom *error = guard_mork_space_syntax(a, frame->atom,
            frame->match_spaces[0], "match", "mork:match");
        if (error) {
            outcome_set_add(frame->target, error, &frame->env);
            he_canonical_pop();
            return;
        }
    }
    if (!frame->sourced_match && !space_query_is_connective(registers->source[0])) {
        MatchStep steps[16];
        /* bodies[i]: the body under legs 0..i. */
        Atom *bodies[16];
        uint32_t count = 1u;
        steps[0] = (MatchStep){frame->match_spaces[0], registers->source[0]};
        Atom *body = frame->live[0];
        bodies[0] = body;
        while (count < 16u && body->kind == ATOM_EXPR && body->expr.len == 4u &&
                atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.match)) {
            Atom *pattern = resolve_registry_refs(a, body->expr.elems[2]);
            if (!space_query_can_flatten(pattern))
                break;
            Space *space = g_registry ? resolve_registry_space_payload(g_registry,
                resolve_registry_refs(a, body->expr.elems[1])) : NULL;
            /* A space named by a variable of an earlier leg, or computed, is
             * known only once that leg has matched: the inner match runs on
             * its own then. */
            if (!space && g_registry)
                break;
            steps[count] = (MatchStep){space ? space : frame->space, pattern};
            body = body->expr.elems[3];
            bodies[count++] = body;
        }
        /* Reading a later leg before an earlier row's body runs, or the
         * legs in another order than written, is the source schedule only
         * where no body touches anything.  A chain of more than two legs is
         * planned on a certificate where its template may be pure; any other
         * fuses two legs, and each later leg is a match of its own, read
         * when its branch is reached.  Two fused legs are the source
         * schedule: the second is read for each row of the first when that
         * row is reached. */
        frame->match_certifying = false;
        if (count > 2u) {
            if (!g_he_canonical_driver->answer_limit &&
                !frame->match_uncertified &&
                match_template_may_be_pure(frame->space, body)) {
                frame->match_certifying = true;
            } else {
                count = 2u;
                body = bodies[1];
            }
        }
        match_chain_order_steps(steps, &count);
        registers->len = count;
        frame->live[0] = body;
        for (uint32_t i = 0u; i < count; i++) {
            registers->source[i] = steps[i].pattern;
            frame->match_spaces[i] = steps[i].space;
            registers->prefix[i] = atom_space(a, steps[i].space);
        }
    }
    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(&frame->work, atom_unit(a), &empty);
    CettaExprLen count = registers->len;
    /* A fused chain completes its prefix first. Its final read window is
     * acquired separately for each prefix, exactly where the source adapter
     * acquires it; a completed window is never silently reopened. */
    CettaExprLen prefix = !frame->sourced_match && count > 1u ? count - 1u : count;
    /* No body runs before the prefix completes, so where its legs read one
     * PathMap space their conjunction gives the same rows through the
     * space's index. */
    CettaExprIndex first_leg = 0u;
    if (!frame->sourced_match && prefix > 1u &&
        he_canonical_legs_share_pathmap(frame, prefix)) {
        OutcomeSet joined;
        outcome_set_init(&joined);
        HeCanonicalJoinedRows sink = {.arena = a, .rows = &joined};
        SpaceMatchPullVisitResult visited =
            space_match_backend_try_visit_conjunction_indexed(
                frame->match_spaces[0], a, registers->source, prefix, NULL,
                he_canonical_joined_row, &sink);
        if (visited == SPACE_MATCH_PULL_VISIT_TERMINATED) {
            outcome_set_free(&joined);
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
            return;
        }
        if (visited == SPACE_MATCH_PULL_VISIT_COMPLETE) {
            outcome_set_free(&frame->work);
            outcome_set_init(&frame->work);
            for (CettaCount i = 0u; i < joined.len; i++)
                outcome_set_add_existing_move(&frame->work, &joined.items[i]);
            first_leg = prefix;
        }
        outcome_set_free(&joined);
    }
    for (CettaExprIndex leg = first_leg; leg < prefix; leg++) {
        OutcomeSet next;
        outcome_set_init(&next);
        for (CettaCount i = 0u; i < frame->work.len; i++) {
            BindingSet rows;
            prime_native_observe_space_read(frame->match_spaces[leg]);
            if (!space_query_bindings(frame->match_spaces[leg], a,
                    registers->source[leg], &frame->work.items[i].env, &rows)) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                outcome_set_free(&next);
                return;
            }
            for (CettaIndex j = 0u; j < rows.len; j++)
                outcome_set_add(&next, atom_unit(a), &rows.items[j]);
            binding_set_free(&rows);
        }
        outcome_set_free(&frame->work);
        outcome_set_init(&frame->work);
        for (CettaCount i = 0u; i < next.len; i++)
            outcome_set_add_existing_move(&frame->work, &next.items[i]);
        outcome_set_free(&next);
    }
    frame->phase = 3u;
    frame->index = 0u;
}

/* Resolve an authored space producer through the same continuation driver.
 * Algebra operands are sequenced without nesting the native resolver in C. */
static void he_canonical_resume_space(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (!g_registry) {
        he_canonical_pop();
        return;
    }
    if (!frame->phase) {
        if (petta_space_expression_is_algebra(frame->atom)) {
            frame->phase = 2u;
            frame->index = 1u;
            he_canonical_push(HE_CANONICAL_SPACE, frame->space, a,
                frame->atom->expr.elems[frame->index++], NULL,
                frame->fuel, NULL, false, &frame->child);
            return;
        }
        Atom *direct = resolve_registry_refs(a, frame->atom);
        Space *space = resolve_registry_space_payload(g_registry, direct);
        if (space) {
            outcome_set_add(frame->target, atom_space(a, space), &frame->env);
            he_canonical_pop();
            return;
        }
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_EVALUATE, frame->space, a,
            frame->atom, NULL, frame->fuel, NULL, false, &frame->child);
        return;
    }
    Space *space = NULL;
    for (CettaCount i = 0u; !space && i < frame->child.len; i++)
        space = resolve_registry_space_payload(g_registry,
            outcome_atom_materialize(a, &frame->child.items[i]));
    if (!space) {
        he_canonical_pop();
        return;
    }
    if (frame->phase == 2u) {
        if (frame->live[0]) {
            Space *previous = resolve_registry_space_payload(g_registry, frame->live[0]);
            Space *folded = space_algebra_fresh_result(a);
            if (!folded) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                return;
            }
            if (atom_is_symbol_id(frame->atom->expr.elems[0], g_builtin_syms.comma))
                space_algebra_add_meet(folded, previous, space);
            else
                space_algebra_add_union(folded, previous, space);
            space = folded;
        }
        frame->live[0] = atom_space(a, space);
        outcome_set_free(&frame->child);
        outcome_set_init(&frame->child);
        if (frame->index < frame->atom->expr.len) {
            he_canonical_push(HE_CANONICAL_SPACE, frame->space, a,
                frame->atom->expr.elems[frame->index++], NULL,
                frame->fuel, NULL, false, &frame->child);
            return;
        }
    }
    outcome_set_add(frame->target, atom_space(a, space), &frame->env);
    he_canonical_pop();
}

/* A row of a certified chain whose body is not shown pure.  Every body run
 * so far only answered, so the answers are dropped, and the chain is planned
 * again from its first leg on the source schedule. */
static void he_canonical_match_replan(HeCanonicalFrame *frame) {
    if (frame->relay_redirected)
        he_delivery_truncate(frame->delivery, frame->range_start);
    else {
        outcome_set_free(&frame->paths);
        outcome_set_init(&frame->paths);
    }
    outcome_set_free(&frame->work);
    outcome_set_init(&frame->work);
    outcome_set_free(&frame->child);
    outcome_set_init(&frame->child);
    frame->registers->len = 1u;
    frame->live[0] = resolve_registry_refs(frame->arena,
                                           expr_arg(frame->atom, 2));
    frame->match_uncertified = true;
    he_canonical_match_plan(frame);
}

static inline __attribute__((always_inline)) void he_canonical_resume_match_kernel(
    HeCanonicalFrame *frame, bool diagnostic) {
    Arena *a = frame->arena;
    if (!frame->phase) {
        he_canonical_match_initialize(frame);
        return;
    }
    HeCanonicalRegisters *registers = frame->registers;
    if (frame->phase == 1u) {
        if (frame->index == registers->len) {
            he_canonical_match_plan(frame);
            return;
        }
        Atom *ref = resolve_registry_refs(a, registers->types[frame->index]);
        Space *space = g_registry ? resolve_registry_space_payload(g_registry, ref) : NULL;
        if (!g_registry && !frame->sourced_match)
            space = frame->space;
        if (!space) {
            frame->phase = 2u;
            he_canonical_push(HE_CANONICAL_SPACE, frame->space, a,
                ref, NULL, frame->fuel, NULL, false, &frame->child);
            return;
        }
        frame->match_spaces[frame->index] = space;
        registers->prefix[frame->index++] = atom_space(a, space);
        return;
    }
    if (frame->phase == 2u) {
        Space *space = NULL;
        for (CettaCount i = 0u; g_registry && !space && i < frame->child.len; i++)
            space = resolve_registry_space_payload(g_registry,
                outcome_atom_materialize(a, &frame->child.items[i]));
        if (!space) {
            he_canonical_match_error(frame, frame->sourced_match
                ? "sourced conjunct space did not resolve"
                : "match expects a space as the first argument");
            return;
        }
        frame->match_spaces[frame->index] = space;
        registers->prefix[frame->index++] = atom_space(a, space);
        outcome_set_free(&frame->child);
        outcome_set_init(&frame->child);
        frame->phase = 1u;
        return;
    }
    if (frame->phase == 3u) {
        if (frame->index == frame->work.len) {
            he_relay_finish(frame, &frame->paths, false, false);
            he_canonical_pop();
            return;
        }
        Outcome *prefix = &frame->work.items[frame->index++];
        if (!frame->sourced_match && registers->len > 1u) {
            CettaExprIndex last = registers->len - 1u;
            BindingSet rows;
            prime_native_observe_space_read(frame->match_spaces[last]);
            if (!space_query_bindings(frame->match_spaces[last], a,
                    registers->source[last], &prefix->env, &rows)) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                return;
            }
            for (CettaIndex i = 0u; i < rows.len; i++)
                outcome_set_add(&frame->child, atom_unit(a), &rows.items[i]);
            binding_set_free(&rows);
        } else
            outcome_set_add_existing(&frame->child, prefix);
        frame->head_index = 0u;
        frame->phase = 4u;
    }
    if (frame->phase == 4u) {
        if (frame->head_index == frame->child.len) {
            outcome_set_free(&frame->child);
            outcome_set_init(&frame->child);
            frame->phase = 3u;
            return;
        }
        Outcome *row = &frame->child.items[frame->head_index++];
        MatchVisibleVarSet visible;
        match_visible_var_set_init(&visible);
        bool valid = true;
        for (CettaExprIndex i = 0u; valid && i < registers->len; i++)
            valid = collect_match_visible_vars_rec(registers->source[i], &visible);
        Bindings projected;
        bindings_init(&projected);
        valid = valid && project_match_visible_bindings(a, &visible, &row->env, &projected);
        match_visible_var_set_free(&visible);
        if (!valid) {
            bindings_free(&projected);
            return;
        }
        if (diagnostic && registers->len > 1u)
            match_chain_note_progress((uint32_t)registers->len, "canonical");
        Atom *body = bindings_apply_if_vars(&projected, a, frame->live[0]);
        if (frame->match_certifying &&
            !match_body_shown_pure(frame->space, body)) {
            bindings_free(&projected);
            he_canonical_match_replan(frame);
            return;
        }
        /* A write takes effect when its body runs.  A removal from a space
         * the match reads names the stored atom by its source variables; a
         * write to another space defers that space's secondary index upkeep
         * until the match finishes. */
        Space *target = match_result_target_space(body, a);
        bool queried = match_result_targets_any_query_space(
            target, frame->match_spaces, (uint32_t)registers->len);
        SymbolId head = atom_head_symbol_id(body);
        if (queried && (head == g_builtin_syms.remove_atom ||
                        head == g_builtin_syms.mork_remove_atom)) {
            Atom *restored =
                space_match_backend_restore_opening_provenance(a, body);
            if (restored)
                body = restored;
        }
        if (!frame->preserve_bindings && !queried)
            (void)deferred_space_set_add(&frame->deferred_spaces, target);
        HeCanonicalFrame *body_frame = he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
            body, NULL, frame->fuel, &projected,
            frame->preserve_bindings, he_relay_sink(frame, &frame->paths));
        /* Template substitution continues the established invocation.
         * A local binder in its syntax is not a new caller variable.
         * Keep the incoming matcher refinements, while carrying the
         * original scope through interpretation and return. */
        if (body_frame)
            body_frame->query_closed = frame->query_closed;
        bindings_free(&projected);
        return;
    }
}

/* The call a frame's work belongs to: the nearest frame below it that
 * holds an expression other than a minimal instruction, since an
 * instruction is a step of the call that runs it.  NULL at the top level. */
static Atom *he_canonical_enclosing_call(const HeCanonicalFrame *frame) {
    for (const CettaOwnedFrameLink *link = frame->link.next; link;
         link = link->next) {
        const HeCanonicalFrame *below = (const HeCanonicalFrame *)link;
        if (below->atom && below->atom->kind == ATOM_EXPR &&
            !he_minimal_instruction(below->atom))
            return below->atom;
    }
    return NULL;
}

static void he_canonical_resume_instruction(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    SymbolId head = atom_head_symbol_id(frame->atom);
    CettaControlOperationPlan control = frame->control;
    CettaExprLen arity = frame->atom->kind == ATOM_EXPR && frame->atom->expr.len
        ? expr_nargs(frame->atom) : 0u;
    if (!he_instruction_control_valid(control, arity)) {
        eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        he_canonical_pop();
        return;
    }
    bool source_chain = frame->typed && control.operation == CETTA_CONTROL_SEQUENCE &&
        expr_arg(frame->atom, control.first)->kind == ATOM_VAR &&
        !he_minimal_instruction(expr_arg(frame->atom, control.source)) &&
        !active_profile_uses_rust_he_compat_semantics();
    if (!frame->phase) {
        if (source_chain) {
            frame->phase = 1u;
            he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                expr_arg(frame->atom, control.source), NULL, frame->fuel,
                &frame->env, true, &frame->child);
        } else {
            frame->phase = 2u;
            he_instruction_push(&frame->instructions,
                frame->typed && head == g_builtin_syms.superpose
                    ? HE_INSTRUCTION_RAW : HE_INSTRUCTION_RUN,
                frame->space, a, frame->atom, frame->fuel,
                &frame->env, &frame->work, &control, frame->live[0]);
            if (active_profile_uses_rust_he_compat_semantics() &&
                frame->instructions.top &&
                control.operation == CETTA_CONTROL_RETURN_SCOPE) {
                HeCanonicalFrame *parent = (HeCanonicalFrame *)frame->link.next;
                if (parent && parent->atom && parent->atom != frame->atom &&
                    atom_head_symbol_id(parent->atom) != g_builtin_syms.function)
                    ((HeInstructionFrame *)frame->instructions.top)->function_caller =
                        parent->atom;
            }
        }
        return;
    }
    if (frame->phase == 1u) {
        if (frame->index >= frame->child.len) {
            frame->phase = 3u;
            frame->index = 0u;
        } else {
            Outcome *source = &frame->child.items[frame->index++];
            Atom *value = outcome_atom_materialize(a, source);
            Atom *body = instantiate_template_variable_once(a,
                expr_arg(frame->atom, control.last), expr_arg(frame->atom, control.first)->var_id,
                value);
            frame->phase = 2u;
            he_instruction_push(&frame->instructions, HE_INSTRUCTION_RUN,
                frame->space, a, body, frame->fuel, &source->env, &frame->work, NULL, NULL);
        }
        return;
    }
    if (frame->phase == 2u) {
        if (frame->instructions.top) {
            he_instruction_resume(&frame->instructions);
            return;
        }
        /* The instructions' answers return to the call they run in, and
         * their bindings may be dropped from here on: Rust's return stage
         * keeps that call's variables (apply_and_retain). */
        if (!source_chain && active_profile_uses_rust_he_compat_semantics()) {
            Atom *call = he_canonical_enclosing_call(frame);
            for (CettaCount i = 0u; call && i < frame->work.len; i++)
                if (!he_return_retain_invocation_outcome(a, call, NULL,
                        &frame->work.items[i]))
                    eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        }
        frame->phase = source_chain ? 1u : 3u;
        return;
    }
    bool raw_function_result = active_profile_uses_rust_he_compat_semantics() &&
        control.operation == CETTA_CONTROL_RETURN_SCOPE;
    if (frame->phase == 3u) {
        if (!raw_function_result && frame->typed && frame->selected &&
            frame->work.len == 1u && !eval_unwinding()) {
            Outcome *source = &frame->work.items[0];
            Atom *value = outcome_atom_materialize(a, source);
            if (he_instruction_completion(a, value, frame->type, true, true) ==
                    HE_INSTRUCTION_TAIL) {
                frame->phase = 5u;
                he_canonical_tail_push(frame, value, frame->type,
                    &source->env, &frame->paths);
                return;
            }
        }
        frame->phase = 4u;
        frame->index = 0u;
    }
    if (frame->phase == 4u) {
        if (frame->index >= frame->work.len) {
            frame->phase = 5u;
        } else {
            Outcome *source = &frame->work.items[frame->index++];
            Atom *value = outcome_atom_materialize(a, source);
            HeInstructionCompletion completion = he_instruction_completion(
                a, value, frame->type, frame->typed, false);
            if (completion == HE_INSTRUCTION_OMIT)
                return;
            if (completion == HE_INSTRUCTION_REFUSAL) {
                if (frame->typed)
                    value = he_instruction_refusal_readback(
                        frame->live[0], frame->atom, control);
                outcome_set_add(&frame->paths, value, &source->env);
            } else if (raw_function_result && (atom_is_error(value) || atom_is_legacy_empty_sentinel(value))) {
                outcome_set_add(&frame->paths,value,&source->env);
            } else {
                he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                    value, frame->type, frame->fuel, &source->env,
                    frame->preserve_bindings, &frame->paths);
            }
            return;
        }
    }
    if (frame->phase == 5u) {
        he_canonical_finish(frame, &frame->paths, false);
        he_canonical_pop();
    }
}

static void he_canonical_call_run(HeCanonicalFrame *frame, OutcomeSet *sink) {
    if (!he_shared_execution_try(frame->space, frame->arena,
            frame->atom, frame->type, frame->fuel, &frame->env,
            frame->preserve_bindings, sink))
        metta_call_impl(frame->space, frame->arena, frame->atom,
            frame->type, frame->fuel, frame->preserve_bindings,
            &frame->env,
            -1,
            sink);
}

/* A call whose every answer is an answer of a bounded run asks the compiled
 * realizations of a call (a closed call's answer producer, the relational
 * machine) only for the successes the run still needs: an error or Empty is
 * dropped once a success exists, so it does not count.  The bound is the
 * call's own: what the call evaluates in turn is not bounded. */
static __attribute__((noinline)) void he_canonical_call_bounded(
    HeCanonicalFrame *frame) {
    HeCanonicalDriver *driver = g_he_canonical_driver;
    he_canonical_stream_root(driver);
    uint64_t needed = driver->answer_limit > driver->root_visible
        ? driver->answer_limit - driver->root_visible : 1u;
    frame->observation = eval_observation_demand_enter(
        (CettaObservationDemand){
            .completion = CETTA_OBSERVATION_FINITE_PREFIX,
            .prefix_limit = needed,
        }, frame->atom);
    HeAnswerBound enclosing = g_he_answer_bound;
    EvalGcRootFrame enclosing_root = {0};
    EvalGcRootFrame bound_root = {0};
    eval_gc_root_frame_enter_evaluation_retry(&enclosing_root,
                                              &enclosing.root);
    g_he_answer_bound = (HeAnswerBound){.root = frame->atom, .needed = needed};
    eval_gc_root_frame_enter_evaluation_retry(&bound_root,
                                              &g_he_answer_bound.root);
    he_canonical_call_run(frame, &frame->child);
    eval_gc_root_frame_leave(&bound_root);
    g_he_answer_bound = enclosing;
    eval_gc_root_frame_leave(&enclosing_root);
    eval_observation_demand_leave(&frame->observation);
}

/* A call of a program's own symbol: one that a compiled realization of the
 * program may answer. */
static bool he_canonical_call_compilable(Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR || !atom->expr.len ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    SymbolId head = atom_symbol_current_id(atom->expr.elems[0]);
    return !is_grounded_op(head) && !symbol_id_is_builtin_for(head, eval_current_language_id());
}

static void he_canonical_call(HeCanonicalFrame *frame) {
    if (g_he_canonical_driver->answer_limit && frame->stream_spine &&
        he_canonical_call_compilable(frame->atom))
        he_canonical_call_bounded(frame);
    else
        he_canonical_call_run(frame, he_relay_sink(frame, &frame->child));
}

static void he_canonical_resume_match_plain(HeCanonicalFrame *frame) {
    he_canonical_resume_match_kernel(frame, false);
}

static void he_canonical_resume_match_diagnostic(HeCanonicalFrame *frame) {
    he_canonical_resume_match_kernel(frame, true);
}

static inline __attribute__((always_inline)) void he_canonical_resume(
    HeCanonicalFrame *frame) {
    switch (frame->kind) {
    case HE_CANONICAL_BIND: he_canonical_resume_bind(frame); break;
    case HE_CANONICAL_EVALUATE: he_canonical_resume_evaluate(frame); break;
    case HE_CANONICAL_CALLER: he_canonical_resume_caller(frame); break;
    case HE_CANONICAL_DISPATCH: he_canonical_resume_dispatch(frame); break;
    case HE_CANONICAL_ARGUMENTS: he_canonical_resume_arguments(frame); break;
    case HE_CANONICAL_TUPLE: he_canonical_resume_tuple(frame); break;
    case HE_CANONICAL_RESULT:
        if (frame->phase == 2u) {
            if (frame->index >= frame->work.len)
                he_canonical_pop();
            else {
                Outcome *seed = &frame->work.items[frame->index++];
                he_canonical_result_push(frame,
                    result_eval_type_hint(frame->type, outcome_preview_atom(seed)),
                    seed, frame->target);
            }
        } else
            he_canonical_resume_result(frame);
        break;
    case HE_CANONICAL_CONTROL: he_canonical_resume_control(frame); break;
    case HE_CANONICAL_PATTERN: he_canonical_resume_pattern(frame); break;
    case HE_CANONICAL_INSTRUCTION: he_canonical_resume_instruction(frame); break;
    case HE_CANONICAL_MATCH: he_canonical_resume_match_plain(frame); break;
    case HE_CANONICAL_MATCH_DIAGNOSTIC: he_canonical_resume_match_diagnostic(frame); break;
    case HE_CANONICAL_SPACE: he_canonical_resume_space(frame); break;
    case HE_CANONICAL_STATE: he_canonical_resume_state(frame); break;
    case HE_CANONICAL_CAPTURE:
        if (!frame->phase) {
            CaptureClosure *closure = frame->atom->expr.elems[0]->ground.ptr;
            frame->saved_options = cetta_malloc(sizeof(*frame->saved_options));
            *frame->saved_options = *active_eval_options_const();
            *active_eval_options() = closure->options;
            frame->phase = 1u;
            he_canonical_push(HE_CANONICAL_CALLER,
                frame->space, frame->arena, expr_arg(frame->atom, 0u),
                NULL, frame->fuel, &frame->env,
                frame->preserve_bindings, &frame->child);
        } else {
            he_canonical_finish(frame, &frame->child, false);
            he_canonical_pop();
        }
        break;
    case HE_CANONICAL_CALL:
        if (frame->phase) {
            he_frame_mark_answers(frame, &frame->child);
            he_relay_finish(frame, &frame->child, false, false);
            he_canonical_pop();
        } else {
            frame->phase = 1u;
            OutcomeSet *sink = he_relay_sink(frame, &frame->child);
            if (atom_is_he_evaluated(frame->atom)) {
                he_answer_evaluated(frame, frame->atom, frame->type, true,
                    &frame->env, sink);
                if (!g_he_canonical_driver->answer_limit)
                    he_canonical_retire_input_roots(frame);
                break;
            }
            if (he_value_certified(frame->space, frame->fuel, frame->atom,
                    frame->type)) {
                outcome_set_add(sink, he_answer_as_value(frame->arena,
                    frame->atom), &frame->env);
                if (!g_he_canonical_driver->answer_limit)
                    he_canonical_retire_input_roots(frame);
                break;
            }
            he_canonical_call(frame);
            if (!g_he_canonical_driver->answer_limit)
                he_canonical_retire_input_roots(frame);
        }
        break;
    }
}

/* One canonical control operation, shared by synchronous and retained runs.
 * A native callback completes inside this operation; it is not a suspendible
 * C-local frame. No observation projection is applied here. */
static CettaHeAdvance he_canonical_advance(HeCanonicalDriver *driver) {
    assert(driver && g_he_canonical_driver == driver);
    if (!driver->stack.top)
        return CETTA_HE_CLOSED;
    if (eval_unwinding() || eval_cancel_check())
        return CETTA_HE_INTERRUPTED;
    Arena *a = driver->arena;
    if (g_eval_gc.region_arena == a &&
        g_eval_gc.external_owner_depth == 0u &&
        eval_gc_budget_reached_with_floor(a, driver->gc_anchor,
            driver->gc_survivor_floor_bytes) &&
        eval_gc_root_chain_is_precise(g_eval_gc.roots)) {
        uint64_t before = g_eval_gc.collections;
        /* New observation guards save these current roots. Keep their
         * addresses current when a collection moves the observed terms,
         * as well as the older roots already owned by saved guards. */
        EvalGcRootFrame observation_root = {0};
        EvalGcRootFrame prefix_root = {0};
        eval_gc_root_frame_enter_evaluation_retry(
            &observation_root, &g_eval_observation_root);
        eval_gc_root_frame_enter_evaluation_retry(
            &prefix_root, &g_prime_prefix_demand.root);
        eval_gc_collect(a, driver->gc_anchor, NULL, NULL, NULL,
            g_prime_need_episode_owner, &g_prime_need_active,
            &g_prime_need_branch_state_active
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            , &g_prime_need_receipt_active
#endif
            );
        eval_gc_root_frame_leave(&prefix_root);
        eval_gc_root_frame_leave(&observation_root);
        if (g_eval_gc.collections != before)
            driver->gc_survivor_floor_bytes =
                arena_accounted_live_bytes(&g_eval_gc.survivor);
    }
    HeCanonicalFrame *top = (HeCanonicalFrame *)driver->stack.top;
    if (!driver->answer_limit) {
        he_canonical_resume(top);
    } else {
        driver->resumed = top;
        driver->resumed_popped = false;
        he_canonical_resume(top);
        if (!driver->resumed_popped && !he_canonical_frame_finishing(top)) {
            he_canonical_stream(driver, top);
            if (top->stream_to_root)
                he_canonical_stream_root(driver);
            else
                he_canonical_stream(driver, top->stream_owner);
        }
        driver->resumed = NULL;
    }
    if (driver->advances != UINT64_MAX)
        driver->advances++;
    return CETTA_HE_ADVANCED;
}

/* Evaluator-local dynamic state. Spaces and service resources keep their
 * existing shared contents and lifetime contracts. A private execution
 * context owns query banks and a private GC context owns its root chain and
 * survivor arena even while another execution is active. */
typedef struct {
    EvalExecutionContext *execution;
    Registry *registry;
    Space *root_space;
    CettaLibraryContext *library;
    CettaEvaluatorOptions options;
    Arena *persistent;
    TermUniverse *universe;
    TempSpaceSet temporary_spaces;
    TempSpaceSet new_spaces;
    uint32_t evaluation_depth;
    bool observation_present;
    CettaObservationDemand observation;
    Atom *observation_root;
    PrimePrefixDemand prefix_demand;
    Atom *pending_raise;
    _Atomic bool *cancel_requested;
    bool cancel_observed;
    _Atomic bool *thread_unsafe_requested;
    /* Executions remain confined to their creating thread. Saved semantic
     * contexts use stable selectors independently of that restriction. */
    PrimeNeedSavedContext context;
    uint64_t evaluator_id;
    Arena *need_owner;
    Arena *episode_owner;
    Arena *branch_owner;
    PrimeNeedRegionPool *region_pool;
    CettaPrimeNeedAnswerObserver need_observer;
    void *need_observer_context;
    bool controller_requested;
    CettaSearchControllerPolicy controller;
    bool controller_auto;
    CettaSelectionAutomaton selection;
    bool selection_ready;
    bool petta_search;
    bool petta_stats;
    bool table_trace;
    Space *home[64];
    Space *here[64];
    size_t home_depth;
    size_t here_depth;
} HeCanonicalAmbient;

static void he_canonical_ambient_capture(HeCanonicalAmbient *state) {
    state->execution = g_eval_execution;
    state->registry = g_registry;
    state->root_space = g_eval_root_space;
    state->library = g_library_context;
    state->options = *active_eval_options_const();
    state->persistent = g_eval_fallback_universe.persistent_arena;
    state->universe = g_eval_term_universe_override;
    state->temporary_spaces = g_temp_spaces;
    state->new_spaces = g_new_space_track;
    state->evaluation_depth = g_metta_eval_depth;
    state->observation_present = g_eval_observation_demand_present;
    state->observation = g_eval_observation_demand;
    state->observation_root = g_eval_observation_root;
    state->prefix_demand = g_prime_prefix_demand;
    state->pending_raise = g_petta_pending_raise;
    state->cancel_requested = g_eval_cancel_requested;
    state->cancel_observed = g_eval_cancel_observed;
    state->thread_unsafe_requested = g_hyperpose_thread_unsafe_requested;
    state->evaluator_id = g_prime_need_evaluator_id;
    state->need_owner = g_prime_need_owner;
    state->episode_owner = g_prime_need_episode_owner;
    state->branch_owner = g_prime_need_region_branch_owner;
    state->region_pool = g_prime_need_region_pool;
    state->need_observer = g_prime_need_answer_observer;
    state->need_observer_context = g_prime_need_answer_observer_context;
    state->controller_requested = g_active_search_controller_requested;
    state->controller = g_active_search_controller;
    state->controller_auto = g_active_search_controller_auto;
    state->selection = g_active_selection_automaton;
    state->selection_ready = g_active_selection_automaton_ready;
    state->petta_search = g_active_petta_search_machine;
    state->petta_stats = g_active_petta_machine_stats;
    state->table_trace = g_active_petta_table_safety_trace;
    state->home_depth = g_context_home_depth;
    state->here_depth = g_context_here_depth;
    prime_need_saved_context_capture(&state->context);
    memcpy(state->home, g_context_home_stack, sizeof(state->home));
    memcpy(state->here, g_context_here_stack, sizeof(state->here));
}

static void he_canonical_ambient_install(const HeCanonicalAmbient *state) {
    if (state->library != g_library_context)
        runtime_abt_signature_reset();
    eval_swap_library_context(state->library);
    *active_eval_options() = state->options;
    g_eval_execution = state->execution;
    g_registry = state->registry;
    g_eval_root_space = state->root_space;
    term_universe_set_persistent_arena(&g_eval_fallback_universe,
        state->persistent);
    g_eval_term_universe_override = state->universe;
    g_temp_spaces = state->temporary_spaces;
    g_new_space_track = state->new_spaces;
    g_metta_eval_depth = state->evaluation_depth;
    g_eval_observation_demand_present = state->observation_present;
    g_eval_observation_demand = state->observation;
    g_eval_observation_root = state->observation_root;
    g_prime_prefix_demand = state->prefix_demand;
    g_petta_pending_raise = state->pending_raise;
    g_eval_cancel_requested = state->cancel_requested;
    g_eval_cancel_observed = state->cancel_observed;
    g_hyperpose_thread_unsafe_requested = state->thread_unsafe_requested;
    prime_need_saved_context_install(&state->context);
    g_prime_need_evaluator_id = state->evaluator_id;
    g_prime_need_owner = state->need_owner;
    g_prime_need_episode_owner = state->episode_owner;
    g_prime_need_region_branch_owner = state->branch_owner;
    g_prime_need_region_pool = state->region_pool;
    g_prime_need_answer_observer = state->need_observer;
    g_prime_need_answer_observer_context = state->need_observer_context;
    g_active_search_controller_requested = state->controller_requested;
    g_active_search_controller = state->controller;
    g_active_search_controller_auto = state->controller_auto;
    g_active_selection_automaton = state->selection;
    g_active_selection_automaton_ready = state->selection_ready;
    g_active_petta_search_machine = state->petta_search;
    g_active_petta_machine_stats = state->petta_stats;
    g_active_petta_table_safety_trace = state->table_trace;
    g_context_home_depth = state->home_depth;
    g_context_here_depth = state->here_depth;
    memcpy(g_context_home_stack, state->home, sizeof(state->home));
    memcpy(g_context_here_stack, state->here, sizeof(state->here));
}

/* Like GC's existing semispace commit, move block ownership without changing
 * atom identities. Re-register the Arena at its destination address: copying
 * a registered Arena struct alone would leave provenance naming a dead slot. */
static void he_canonical_gc_move(EvalGc *destination, EvalGc *source) {
    assert(destination != source && !destination->ready);
    if (source->ready)
        arena_set_runtime_kind(&source->survivor,
            CETTA_ARENA_RUNTIME_KIND_OTHER);
    *destination = *source;
    memset(source, 0, sizeof(*source));
    if (destination->ready)
        arena_set_runtime_kind(&destination->survivor,
            CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
}

struct CettaHeExecution {
    HeCanonicalDriver driver;
    HeCanonicalAmbient ambient;
    EvalGc gc;
    EvalGcRegionGuard region;
    EvalGcRootFrame ambient_roots[3];
    Arena storage;
    OutcomeSet output;
    Bindings input_environment;
    CettaEvalEpisode completion;
    const CettaProfile *profile;
    pthread_t thread;
    int fuel;
    bool active;
    bool interrupted;
};

typedef struct {
    HeCanonicalAmbient previous;
    EvalGc gc;
    HeCanonicalDriver *driver;
    CettaEvalEpisode *completion;
    CettaEvaluatorOptions borrowed_options;
} HeCanonicalActivation;

static bool he_canonical_owner_available(const CettaHeExecution *owner) {
    return owner && pthread_equal(owner->thread, pthread_self()) &&
        !owner->active;
}

/* The redirect lifecycle surrounds more than its transactional flag: payload
 * setup and result promotion also own scratch maps. An ordinary HE owner has
 * no admission to that speculative region. Refuse activation without changing
 * either execution or granting an escape from the caller's publication rules. */
static bool he_canonical_payload_conflict(void) {
    return g_eval_payload_transactional || g_eval_payload_redirect_owner ||
        g_eval_payload_space_redirects.len != 0u ||
        g_eval_payload_state_redirects.len != 0u ||
        g_eval_payload_scratch_spaces.len != 0u ||
        g_eval_payload_scratch_states.len != 0u;
}

static HeCanonicalActivation *he_canonical_owner_enter(
    CettaHeExecution *owner) {
    assert(he_canonical_owner_available(owner) &&
        !he_canonical_payload_conflict());
    HeCanonicalActivation *activation = cetta_malloc(sizeof(*activation));
    memset(activation, 0, sizeof(*activation));
    he_canonical_ambient_capture(&activation->previous);
    activation->driver = g_he_canonical_driver;
    activation->completion = g_eval_completion_tracker;
    CettaEvalSession *session = owner->ambient.library
        ? &owner->ambient.library->session : fallback_eval_session();
    activation->borrowed_options = session->options;
    he_canonical_gc_move(&activation->gc, &g_eval_gc);
    he_canonical_gc_move(&g_eval_gc, &owner->gc);
    he_canonical_ambient_install(&owner->ambient);
    g_he_canonical_driver = &owner->driver;
    g_eval_completion_tracker = &owner->completion;
    owner->active = true;
    return activation;
}

static void he_canonical_owner_leave(CettaHeExecution *owner,
                                    HeCanonicalActivation *activation) {
    assert(owner->active && g_he_canonical_driver == &owner->driver);
    he_canonical_ambient_capture(&owner->ambient);
    *active_eval_options() = activation->borrowed_options;
    he_canonical_gc_move(&owner->gc, &g_eval_gc);
    he_canonical_gc_move(&g_eval_gc, &activation->gc);
    he_canonical_ambient_install(&activation->previous);
    g_he_canonical_driver = activation->driver;
    g_eval_completion_tracker = activation->completion;
    owner->active = false;
    free(activation);
}

CettaHeExecution *eval_he_owned_begin(Space *space, Registry *registry,
    CettaHeEntry entry, Atom *atom, Atom *type, int fuel,
    const Bindings *bindings, bool preserve_bindings, bool typed) {
    if (!space || !atom || !he_canonical_enabled() ||
        he_canonical_payload_conflict() ||
        entry < CETTA_HE_ENTRY_BIND || entry > CETTA_HE_ENTRY_CALL)
        return NULL;
    CettaHeExecution *owner = cetta_malloc(sizeof(*owner));
    memset(owner, 0, sizeof(*owner));
    owner->thread = pthread_self();
    owner->fuel = fuel;
    owner->profile = active_eval_session()->profile;
    owner->completion.completion = CETTA_EVAL_COMPLETE;
    arena_init_detached(&owner->storage);
    arena_set_runtime_kind(&owner->storage, CETTA_ARENA_RUNTIME_KIND_EVAL);
    outcome_set_init(&owner->output);
    bindings_init(&owner->input_environment);
    he_canonical_ambient_capture(&owner->ambient);
    owner->ambient.execution = cetta_malloc(sizeof(*owner->ambient.execution));
    eval_execution_context_init(owner->ambient.execution,space,true);
    owner->ambient.registry = registry;
    owner->ambient.root_space = space;
    owner->ambient.temporary_spaces = (TempSpaceSet){0};
    owner->ambient.new_spaces = (TempSpaceSet){0};
    owner->ambient.region_pool = NULL;
    owner->ambient.branch_owner = NULL;
    owner->ambient.need_owner = &owner->storage;
    owner->ambient.episode_owner = &owner->storage;
    owner->ambient.context.value.previous_logical_env = &owner->input_environment;
    owner->driver.root_target = &owner->output;
    owner->driver.arena = &owner->storage;
    AtomDeepCopySession *copy = atom_deep_copy_session_new(&owner->storage);
    Atom *saved_atom = copy ? atom_deep_copy_session_copy(copy, atom) : NULL;
    Atom *saved_type = copy && type
        ? atom_deep_copy_session_copy(copy, type) : NULL;
    bool copied = saved_atom && (!type || saved_type) &&
        (!bindings || bindings_copy(&owner->input_environment, bindings)) &&
        bindings_promote_atoms_with_session(&owner->input_environment, &owner->storage, copy);
    if (copied && owner->input_environment.prime_ext) {
        /* Interpret incoming operational metadata at entry, using the same
         * overlay as other callers. Copying it into lexical frames alone
         * does not install the context used by result publication. */
        PrimeNeedActiveGuard surrounding;
        prime_need_active_enter(&surrounding, &owner->input_environment);
        prime_need_saved_context_capture(&owner->ambient.context);
        prime_need_active_leave(&surrounding);
    }
    Atom **dynamic_atoms[] = {&owner->ambient.observation_root,
        &owner->ambient.prefix_demand.root, &owner->ambient.pending_raise};
    for (size_t i = 0u; copied && i < 3u; i++)
        if (*dynamic_atoms[i])
            copied = (*dynamic_atoms[i] =
                atom_deep_copy_session_copy(copy, *dynamic_atoms[i])) != NULL;
    copied = copied && prime_need_saved_context_promote(
        &owner->storage, copy, &owner->ambient.context);
    atom_deep_copy_session_free(copy);
    HeCanonicalActivation *activation = he_canonical_owner_enter(owner);
    owner->driver.gc_anchor = eval_gc_region_enter(&owner->region, &owner->storage);
    /* These slots are installed before the private root chain becomes active.
     * Collection must update the slots read by callbacks, rather than a copy
     * of their addresses' contents. No inactive owner is collected. */
    Atom **active_slots[] = {&g_eval_observation_root,
        &g_prime_prefix_demand.root, &g_petta_pending_raise};
    for (size_t i = 0u; i < 3u; i++)
        eval_gc_root_frame_enter_evaluation_retry(&owner->ambient_roots[i],
            active_slots[i]);
    if (copied) {
        HeCanonicalKind kind = entry == CETTA_HE_ENTRY_BIND ? HE_CANONICAL_BIND
            : entry == CETTA_HE_ENTRY_EVALUATE ? HE_CANONICAL_EVALUATE
            : HE_CANONICAL_CALL;
        HeCanonicalFrame *frame = he_canonical_push(kind, space,
            &owner->storage, saved_atom, saved_type, fuel,
            &owner->input_environment, preserve_bindings, &owner->output);
        if (frame)
            frame->typed = typed;
        else
            copied = false;
    }
    he_canonical_owner_leave(owner, activation);
    if (!copied) {
        (void)eval_he_owned_destroy(owner);
        return NULL;
    }
    return owner;
}

CettaHeAdvance eval_he_owned_advance(CettaHeExecution *owner) {
    if (!he_canonical_owner_available(owner) ||
        he_canonical_payload_conflict())
        return CETTA_HE_BUSY;
    if (owner->interrupted)
        return CETTA_HE_INTERRUPTED;
    CettaEvalSession *session = owner->ambient.library
        ? &owner->ambient.library->session : fallback_eval_session();
    if (session->language_id != CETTA_LANGUAGE_HE ||
        session->profile != owner->profile) {
        owner->completion.completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
        owner->interrupted = true;
        return CETTA_HE_INTERRUPTED;
    }
    HeCanonicalActivation *activation = he_canonical_owner_enter(owner);
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard boundary = {0};
    CettaHeAdvance advanced = CETTA_HE_INTERRUPTED;
    if (eval_c_stack_guard_enter(owner->fuel, &boundary))
        advanced = he_canonical_advance(&owner->driver);
    else
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_STACK);
    if (advanced == CETTA_HE_INTERRUPTED ||
        owner->completion.completion != CETTA_EVAL_COMPLETE)
        owner->interrupted = true;
    he_canonical_owner_leave(owner, activation);
    return owner->interrupted ? CETTA_HE_INTERRUPTED : advanced;
}

bool eval_he_owned_inspect(const CettaHeExecution *owner,
                          CettaHeExecutionView *view) {
    if (!view || !he_canonical_owner_available(owner))
        return false;
    const HeCanonicalFrame *top = (const HeCanonicalFrame *)owner->driver.stack.top;
    *view = (CettaHeExecutionView){
        .outcomes = &owner->output,
        .frames = owner->driver.stack.depth,
        .top_phase = top ? top->phase : 0u,
        .advances = owner->driver.advances,
        .collections = owner->gc.collections,
        .completion = owner->completion.completion,
        .closed = !top && !owner->interrupted &&
            owner->completion.completion == CETTA_EVAL_COMPLETE,
        .interrupted = owner->interrupted,
    };
    return true;
}

bool eval_he_owned_cancel(CettaHeExecution *owner) {
    if (!he_canonical_owner_available(owner))
        return false;
    if (!owner->driver.stack.top && !owner->interrupted)
        return true;
    if (owner->completion.completion == CETTA_EVAL_COMPLETE)
        owner->completion.completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
    owner->interrupted = true;
    return true;
}

bool eval_he_owned_destroy(CettaHeExecution *owner) {
    if (!he_canonical_owner_available(owner) ||
        he_canonical_payload_conflict())
        return false;
    HeCanonicalActivation *activation = he_canonical_owner_enter(owner);
    while (owner->driver.stack.top)
        he_canonical_pop();
    he_canonical_free_frames(&owner->driver);
    for (size_t i = 3u; i > 0u; i--)
        eval_gc_root_frame_leave(&owner->ambient_roots[i - 1u]);
    eval_gc_region_leave(&owner->region);
    outcome_set_free(&owner->output);
    bindings_free(&owner->input_environment);
    eval_cleanup_owned_new_spaces_in_set(&g_new_space_track,
        g_registry, g_eval_root_space);
    /* This owner, rather than a nested callback, is the cleanup boundary. */
    g_eval_execution->scoped = false;
    eval_release_temporary_spaces();
    if (g_eval_gc.ready) {
        assert(!g_eval_gc.roots && g_eval_gc.region_depth == 0u);
        arena_free(&g_eval_gc.survivor);
        memset(&g_eval_gc, 0, sizeof(g_eval_gc));
    }
    he_canonical_owner_leave(owner, activation);
    arena_free(&owner->storage);
    free(owner);
    return true;
}

static void he_canonical_run(HeCanonicalKind kind, Space *s, Arena *a,
    Atom *atom, Atom *type, int fuel, const Bindings *env,
    bool preserve, bool typed, uint64_t answer_limit, OutcomeSet *target) {
    /* Authored calls stay on the owned stack. A synchronous external callback
     * can enter another driver while its native caller is still live; retain
     * the physical stack guard at that service boundary. */
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard boundary = {0};
    if (!eval_c_stack_guard_enter(fuel, &boundary)) {
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(target, atom_error(a, atom, atom_symbol(a, "StackOverflow")),
            env ? env : &empty);
        return;
    }
    EvalGcRegionGuard region = {0};
    HeCanonicalDriver driver = {
        .root_target = target,
        .arena = a,
        .gc_anchor = eval_gc_region_enter(&region, a),
        .answer_limit = answer_limit,
        .root_seen = target->len,
    };
    HeCanonicalDriver *enclosing = g_he_canonical_driver;
    g_he_canonical_driver = &driver;
    if (!enclosing)
        he_value_index_open(a);
    HeCanonicalFrame *frame = he_canonical_push(kind, s, a, atom, type,
        fuel, env, preserve, target);
    if (frame)
        frame->typed = typed;
    if (!answer_limit) {
        while (he_canonical_advance(&driver) == CETTA_HE_ADVANCED) {}
    } else {
        while (!driver.satisfied) {
            if (driver.stream_ready && he_canonical_stream_consume(&driver))
                continue;
            if (he_canonical_advance(&driver) != CETTA_HE_ADVANCED)
                break;
        }
    }
    /* A satisfied bounded run abandons the frames it has not completed. */
    driver.abandoning = true;
    while (driver.stack.top)
        he_canonical_pop();
    he_canonical_free_frames(&driver);
    g_he_canonical_driver = enclosing;
    if (!enclosing)
        he_value_index_close();
    eval_gc_region_leave(&region);
}

static bool he_pattern_control_try(Space *s, Arena *a, Atom *atom,
    Atom *type, int fuel, const Bindings *env, bool preserve, OutcomeSet *out) {
    if (eval_current_language_id() != CETTA_LANGUAGE_HE ||
        !eval_current_pattern_control_arity(atom_head_symbol_id(atom)))
        return false;
    if (g_he_canonical_driver)
        he_canonical_push(HE_CANONICAL_PATTERN, s, a, atom, type, fuel, env, preserve, out);
    else
        he_canonical_run(HE_CANONICAL_PATTERN, s, a, atom, type, fuel, env, preserve, false, 0u, out);
    return true;
}

static bool he_canonical_bind_try(Space *s, Arena *a, Atom *type,
    Atom *atom, int fuel, const Bindings *env, bool typed, OutcomeSet *os) {
    if (!he_canonical_enabled())
        return false;
    he_canonical_run(HE_CANONICAL_BIND, s, a, atom, type, fuel, env,
        true, typed, 0u, os);
    return true;
}

/* A selection's bounded observation of `atom`: the bind of
 * he_canonical_bind_try, stopped once its answers hold `limit` visible
 * answers (see the notes on bounded observation). */
static bool he_canonical_bounded_bind_try(Space *s, Arena *a, Atom *atom,
    int fuel, uint64_t limit, OutcomeSet *os) {
    if (eval_current_language_id() != CETTA_LANGUAGE_HE || limit == 0u)
        return false;
    he_canonical_run(HE_CANONICAL_BIND, s, a, atom, NULL, fuel, NULL,
        true, false, limit, os);
    return true;
}

static bool he_canonical_call_try(Space *s, Arena *a, Atom *atom, Atom *type,
    int fuel, const Bindings *env, bool preserve, OutcomeSet *os) {
    if (!he_canonical_enabled())
        return false;
    he_canonical_run(HE_CANONICAL_CALL, s, a, atom, type, fuel, env,
        preserve, false, 0u, os);
    return true;
}
