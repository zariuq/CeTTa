/* Tail-safe-point arena reclamation for the MeTTa evaluator.
 *
 * Long tail-recursive evaluations can run as a single top-level ! form.  The
 * per-form eval arena is normally reset only after that form returns, so dead
 * intermediates can accumulate for the whole run.  This module reclaims those
 * intermediates at the evaluator trampoline's tail-safe point after evacuating
 * the continuation roots.
 *
 * The generated evaluator-root-frames component
 * (CETTA_EVAL_GC_FRAME_KIND_ROWS / CETTA_EVAL_GC_ARM_* in the generated
 * header) defines the typed root-frame kinds and their evacuation programs.
 * Evaluator argument frames, the Prime stack driver, and the prepared pure
 * call machine all consume those generated programs.  Legacy evaluator
 * regions without a complete typed frame chain remain outside nested
 * collection; collection is enabled only at boundaries whose entire live
 * continuation is represented by generated roots.
 *
 * GC is normal runtime behavior.  CETTA_GC=0 is retained as a temporary
 * differential/debug escape hatch while this path is being validated.
 */
#ifndef CETTA_EVAL_GC_H
#define CETTA_EVAL_GC_H

typedef struct {
    Atom **items;
    size_t len;
} CettaEvalGcAtomSpan;

typedef bool (*CettaEvalGcEphemeronVisitor)(
    AtomDeepCopySession *session, void *context);

typedef struct {
    CettaEvalGcEphemeronVisitor visit;
    void *context;
} CettaEvalGcEphemeronAtomMap;

/* The presentation emits each payload's field sequence.  Expanding it here
 * with representation types makes the payload layout and its traversal arm
 * two interpretations of the same generated declaration. */
#define CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SLOT(name) Atom **name;
#define CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SPAN(name) CettaEvalGcAtomSpan name;
#define CETTA_EVAL_GC_DECLARE_LOGICAL_BINDINGS(name) Bindings *name;
#define CETTA_EVAL_GC_DECLARE_OUTCOME_SET(name) OutcomeSet *name;
#define CETTA_EVAL_GC_DECLARE_VARIANT_INSTANCE(name) VariantInstance *name;
#define CETTA_EVAL_GC_DECLARE_EPHEMERON_ATOM_MAP(name) \
    CettaEvalGcEphemeronAtomMap name;
#define CETTA_EVAL_GC_DECLARE_PAYLOAD(kind, upper)                         \
    typedef struct {                                                      \
        CETTA_EVAL_GC_FRAME_FIELDS_##kind(                                \
            CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SLOT,                       \
            CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SPAN,                       \
            CETTA_EVAL_GC_DECLARE_LOGICAL_BINDINGS,                       \
            CETTA_EVAL_GC_DECLARE_OUTCOME_SET,                            \
            CETTA_EVAL_GC_DECLARE_VARIANT_INSTANCE,                       \
            CETTA_EVAL_GC_DECLARE_EPHEMERON_ATOM_MAP)                     \
    } CettaEvalGcPayload_##kind;
CETTA_EVAL_GC_FRAME_KIND_ROWS(CETTA_EVAL_GC_DECLARE_PAYLOAD)
#undef CETTA_EVAL_GC_DECLARE_PAYLOAD
#undef CETTA_EVAL_GC_DECLARE_EPHEMERON_ATOM_MAP
#undef CETTA_EVAL_GC_DECLARE_OUTCOME_SET
#undef CETTA_EVAL_GC_DECLARE_VARIANT_INSTANCE
#undef CETTA_EVAL_GC_DECLARE_LOGICAL_BINDINGS
#undef CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SPAN
#undef CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SLOT

typedef union {
#define CETTA_EVAL_GC_DECLARE_PAYLOAD_MEMBER(kind, upper) \
    CettaEvalGcPayload_##kind kind;
    CETTA_EVAL_GC_FRAME_KIND_ROWS(CETTA_EVAL_GC_DECLARE_PAYLOAD_MEMBER)
#undef CETTA_EVAL_GC_DECLARE_PAYLOAD_MEMBER
} CettaEvalGcRootPayload;

typedef struct EvalGcRootFrame {
    CettaEvalGcFrameKind kind;
    CettaEvalGcRootPayload payload;
    struct EvalGcRootFrame *previous;
    bool linked;
    bool precise_suspension;
} EvalGcRootFrame;

typedef struct {
    Arena survivor;
    EvalGcRootFrame *roots;
    Arena *region_arena;
    ArenaMark region_anchor;
    uint32_t region_depth;
    bool ready;
    bool enabled;
    uint32_t external_owner_depth;
    size_t budget_bytes;
    uint64_t collections;
    uint64_t reclaimed_bytes;
} EvalGc;

/* Every evacuation carries its lifetime boundary explicitly.  Atom payloads
 * move into the fresh semispace, while persistent Prime branch state must
 * remain owned by the top-level Prime episode arena.  `old_survivor` and
 * `collected_arena` identify exactly the two owners invalidated by commit. */
typedef struct {
    AtomDeepCopySession *atom_copy_session;
    const Arena *collected_arena;
    const Arena *old_survivor;
    Arena *prime_episode_owner;
} EvalGcEvacuationContext;

static __thread EvalGc g_eval_gc;

static void eval_gc_init_survivor_arena(Arena *arena) {
    arena_init(arena);
    arena_set_hashcons(arena, NULL);
    arena_set_runtime_kind(
        arena, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
}

static void eval_gc_init_once(void) {
    if (g_eval_gc.ready)
        return;

    const char *enabled = getenv("CETTA_GC");
    g_eval_gc.enabled = !(enabled && enabled[0] == '0' && enabled[1] == '\0');

    const char *budget = getenv("CETTA_GC_BUDGET_MB");
    size_t mb = (budget && budget[0]) ? (size_t)strtoull(budget, NULL, 10) : 64u;
    if (mb == 0)
        mb = 64u;
    g_eval_gc.budget_bytes = mb * (size_t)1024u * 1024u;
    g_eval_gc.collections = 0;
    g_eval_gc.reclaimed_bytes = 0;
    g_eval_gc.roots = NULL;
    g_eval_gc.region_arena = NULL;
    g_eval_gc.region_depth = 0u;
    g_eval_gc.external_owner_depth = 0u;

    eval_gc_init_survivor_arena(&g_eval_gc.survivor);
    g_eval_gc.ready = true;
}

static inline bool eval_gc_enabled(void) {
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    return g_eval_gc.enabled;
}

typedef struct {
    Arena *arena;
    bool joined;
} EvalGcRegionGuard;

static ArenaMark eval_gc_region_enter(EvalGcRegionGuard *guard,
                                      Arena *arena) {
    ArenaMark local = arena_mark(arena);
    if (!guard || !arena)
        return local;
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    guard->arena = arena;
    guard->joined = false;
    if (g_eval_gc.region_depth == 0u) {
        g_eval_gc.region_arena = arena;
        g_eval_gc.region_anchor = local;
        g_eval_gc.region_depth = 1u;
        guard->joined = true;
        return g_eval_gc.region_anchor;
    }
    if (g_eval_gc.region_arena == arena &&
        g_eval_gc.region_depth < UINT32_MAX) {
        g_eval_gc.region_depth++;
        guard->joined = true;
        return g_eval_gc.region_anchor;
    }
    return local;
}

static void eval_gc_region_leave(EvalGcRegionGuard *guard) {
    if (!guard || !guard->joined)
        return;
    assert(g_eval_gc.region_arena == guard->arena);
    assert(g_eval_gc.region_depth > 0u);
    g_eval_gc.region_depth--;
    if (g_eval_gc.region_depth == 0u)
        g_eval_gc.region_arena = NULL;
    guard->arena = NULL;
    guard->joined = false;
}

/*
 * The C evaluator still has lexical frames outside its tail-reentry loop.
 * Register the three roots a collected frame may move so a later inner-frame
 * collection can evacuate every pointer into the current survivor semispace,
 * rather than retaining all prior survivor generations until form exit.
 */
static void eval_gc_root_frame_link(EvalGcRootFrame *frame,
                                    CettaEvalGcFrameKind kind) {
    if (!frame)
        return;
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    assert(!frame->linked);
    frame->kind = kind;
    frame->previous = g_eval_gc.roots;
    frame->linked = true;
    frame->precise_suspension = false;
    g_eval_gc.roots = frame;
}

static void eval_gc_root_frame_enter(EvalGcRootFrame *frame,
                                     Atom **atom_io,
                                     Bindings *env,
                                     Atom **etype_io) {
    if (!frame)
        return;
    frame->payload.lexical.atom_io = atom_io;
    frame->payload.lexical.env = env;
    frame->payload.lexical.etype_io = etype_io;
    eval_gc_root_frame_link(frame, CETTA_EVAL_GC_FRAME_LEXICAL);
}

static void eval_gc_root_frame_enter_function_args(
    EvalGcRootFrame *frame, Atom **orig_arg, Atom **arg_type,
    Bindings *env, OutcomeSet *arg_os, Bindings *merged,
    Bindings *active, Bindings *child) {
    if (!frame)
        return;
    frame->payload.function_args.orig_arg = orig_arg;
    frame->payload.function_args.arg_type = arg_type;
    frame->payload.function_args.env = env;
    frame->payload.function_args.arg_os = arg_os;
    frame->payload.function_args.merged = merged;
    frame->payload.function_args.active = active;
    frame->payload.function_args.child = child;
    eval_gc_root_frame_link(frame, CETTA_EVAL_GC_FRAME_FUNCTION_ARGS);
}

static void eval_gc_root_frame_enter_function_args_machine(
    EvalGcRootFrame *frame, Atom **op,
    Atom **orig_args, size_t orig_args_len,
    Atom **arg_types, size_t arg_types_len,
    Atom **prefix, size_t prefix_len,
    OutcomeSet *outcomes) {
    if (!frame)
        return;
    frame->payload.function_args_machine.op = op;
    frame->payload.function_args_machine.orig_args =
        (CettaEvalGcAtomSpan){orig_args, orig_args_len};
    frame->payload.function_args_machine.arg_types =
        (CettaEvalGcAtomSpan){arg_types, arg_types_len};
    frame->payload.function_args_machine.prefix =
        (CettaEvalGcAtomSpan){prefix, prefix_len};
    frame->payload.function_args_machine.outcomes = outcomes;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_FUNCTION_ARGS_MACHINE);
}

static void eval_gc_root_frame_enter_typed_application_continuation(
    EvalGcRootFrame *frame,
    Atom **live_atoms, size_t live_atom_count,
    Atom **overload_types, size_t overload_type_count,
    Atom **return_contracts, size_t return_contract_count,
    Atom **applicability_errors, size_t applicability_error_count,
    Atom **argument_types, size_t argument_type_count,
    OutcomeSet *function_results, OutcomeSet *heads,
    OutcomeSet *parent_outcomes) {
    if (!frame)
        return;
    frame->payload.typed_application_continuation.live_atoms =
        (CettaEvalGcAtomSpan){live_atoms, live_atom_count};
    frame->payload.typed_application_continuation.overload_types =
        (CettaEvalGcAtomSpan){overload_types, overload_type_count};
    frame->payload.typed_application_continuation.return_contracts =
        (CettaEvalGcAtomSpan){return_contracts, return_contract_count};
    frame->payload.typed_application_continuation.applicability_errors =
        (CettaEvalGcAtomSpan){
            applicability_errors, applicability_error_count};
    frame->payload.typed_application_continuation.argument_types =
        (CettaEvalGcAtomSpan){argument_types, argument_type_count};
    frame->payload.typed_application_continuation.function_results =
        function_results;
    frame->payload.typed_application_continuation.heads = heads;
    frame->payload.typed_application_continuation.parent_outcomes =
        parent_outcomes;
    eval_gc_root_frame_link(
        frame,
        CETTA_EVAL_GC_FRAME_TYPED_APPLICATION_CONTINUATION);
}

static void eval_gc_root_frame_enter_observation_normalization(
    EvalGcRootFrame *frame,
    Atom **source, Atom **value,
    Atom **children, size_t child_count,
    Bindings *env, OutcomeSet *child_outcomes,
    OutcomeSet *parent_outcomes) {
    if (!frame)
        return;
    frame->payload.observation_normalization.source = source;
    frame->payload.observation_normalization.value = value;
    frame->payload.observation_normalization.children =
        (CettaEvalGcAtomSpan){children, child_count};
    frame->payload.observation_normalization.env = env;
    frame->payload.observation_normalization.child_outcomes =
        child_outcomes;
    frame->payload.observation_normalization.parent_outcomes =
        parent_outcomes;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_OBSERVATION_NORMALIZATION);
}

static void eval_gc_root_frame_enter_tuple_frame(
    EvalGcRootFrame *frame, Bindings *env, OutcomeSet *sub,
    Bindings *merged, Bindings *active,
    VariantInstance *prefix_variant,
    VariantInstance *active_variant) {
    if (!frame)
        return;
    frame->payload.tuple_frame.env = env;
    frame->payload.tuple_frame.sub = sub;
    frame->payload.tuple_frame.merged = merged;
    frame->payload.tuple_frame.active = active;
    frame->payload.tuple_frame.prefix_variant = prefix_variant;
    frame->payload.tuple_frame.active_variant = active_variant;
    eval_gc_root_frame_link(frame, CETTA_EVAL_GC_FRAME_TUPLE_FRAME);
}

static void eval_gc_root_frame_enter_tuple_machine(
    EvalGcRootFrame *frame,
    Atom **continuation_atoms, size_t continuation_atom_count,
    Atom **orig_elems, size_t orig_count,
    Atom **prefix, size_t prefix_count,
    Bindings *env, OutcomeSet *outcomes,
    OutcomeSet *parent_outcomes) {
    if (!frame)
        return;
    frame->payload.tuple_machine.continuation_atoms =
        (CettaEvalGcAtomSpan){continuation_atoms, continuation_atom_count};
    frame->payload.tuple_machine.orig_elems =
        (CettaEvalGcAtomSpan){orig_elems, orig_count};
    frame->payload.tuple_machine.prefix =
        (CettaEvalGcAtomSpan){prefix, prefix_count};
    frame->payload.tuple_machine.env = env;
    frame->payload.tuple_machine.outcomes = outcomes;
    frame->payload.tuple_machine.parent_outcomes = parent_outcomes;
    eval_gc_root_frame_link(frame, CETTA_EVAL_GC_FRAME_TUPLE_MACHINE);
}

static void eval_gc_root_frame_enter_outcome_continuation(
    EvalGcRootFrame *frame,
    Atom **live_atoms, size_t live_atom_count,
    OutcomeSet *child_outcomes,
    OutcomeSet *parent_outcomes) {
    if (!frame)
        return;
    frame->payload.outcome_continuation.live_atoms =
        (CettaEvalGcAtomSpan){live_atoms, live_atom_count};
    frame->payload.outcome_continuation.child_outcomes = child_outcomes;
    frame->payload.outcome_continuation.parent_outcomes = parent_outcomes;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_OUTCOME_CONTINUATION);
}

static void eval_gc_root_frame_enter_evaluation_retry(
    EvalGcRootFrame *frame, Atom **input) {
    if (!frame)
        return;
    frame->payload.evaluation_retry.input = input;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_EVALUATION_RETRY);
}

static void eval_gc_root_frame_enter_type_cast_continuation(
    EvalGcRootFrame *frame, Atom **subject, Atom **expected_type) {
    if (!frame)
        return;
    frame->payload.type_cast_continuation.subject = subject;
    frame->payload.type_cast_continuation.expected_type = expected_type;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_TYPE_CAST_CONTINUATION);
}

static void eval_gc_root_frame_enter_let_branch_continuation(
    EvalGcRootFrame *frame,
    Atom **canonical,
    Atom **pattern,
    Atom **source,
    Atom **scoped_body,
    Atom **fresh,
    size_t fresh_count,
    Bindings *prefix,
    OutcomeSet *source_outcomes,
    OutcomeSet *parent_outcomes) {
    if (!frame)
        return;
    frame->payload.let_branch_continuation.canonical = canonical;
    frame->payload.let_branch_continuation.pattern = pattern;
    frame->payload.let_branch_continuation.source = source;
    frame->payload.let_branch_continuation.scoped_body = scoped_body;
    frame->payload.let_branch_continuation.fresh =
        (CettaEvalGcAtomSpan){fresh, fresh_count};
    frame->payload.let_branch_continuation.prefix = prefix;
    frame->payload.let_branch_continuation.source_outcomes =
        source_outcomes;
    frame->payload.let_branch_continuation.parent_outcomes =
        parent_outcomes;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_LET_BRANCH_CONTINUATION);
}

static void eval_gc_root_frame_enter_case_branch_continuation(
    EvalGcRootFrame *frame,
    Atom **live_atoms, size_t live_atom_count,
    Bindings *lexical_env, Bindings *branch_env,
    OutcomeSet *source_outcomes,
    OutcomeSet *parent_outcomes) {
    if (!frame)
        return;
    frame->payload.case_branch_continuation.live_atoms =
        (CettaEvalGcAtomSpan){live_atoms, live_atom_count};
    frame->payload.case_branch_continuation.lexical_env = lexical_env;
    frame->payload.case_branch_continuation.branch_env = branch_env;
    frame->payload.case_branch_continuation.source_outcomes =
        source_outcomes;
    frame->payload.case_branch_continuation.parent_outcomes =
        parent_outcomes;
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_CASE_BRANCH_CONTINUATION);
}

static void eval_gc_root_frame_enter_static_branch_walk(
    EvalGcRootFrame *frame, Atom **pending, size_t pending_count) {
    if (!frame)
        return;
    frame->payload.static_branch_walk.pending =
        (CettaEvalGcAtomSpan){pending, pending_count};
    eval_gc_root_frame_link(
        frame, CETTA_EVAL_GC_FRAME_STATIC_BRANCH_WALK);
}

static void eval_gc_root_frame_update_static_branch_walk(
    EvalGcRootFrame *frame, Atom **pending, size_t pending_count) {
    assert(frame && frame->linked &&
           frame->kind == CETTA_EVAL_GC_FRAME_STATIC_BRANCH_WALK);
    frame->payload.static_branch_walk.pending =
        (CettaEvalGcAtomSpan){pending, pending_count};
}

static void eval_gc_root_frame_suspend_precisely(EvalGcRootFrame *frame) {
    assert(frame && frame->linked &&
           frame->kind == CETTA_EVAL_GC_FRAME_LEXICAL);
    frame->precise_suspension = true;
}

static void eval_gc_root_frame_resume(EvalGcRootFrame *frame) {
    assert(frame && frame->linked &&
           frame->kind == CETTA_EVAL_GC_FRAME_LEXICAL);
    frame->precise_suspension = false;
}

static void eval_gc_root_frame_leave(EvalGcRootFrame *frame);

/* A recursive evaluator call may collect only when the suspended caller has
 * named every arena pointer that remains live across the call.  Most such
 * boundaries retain the caller's published outcomes and one child OutcomeSet
 * in addition to the lexical atom/environment/type triple.  Keep that
 * protocol in one object so special forms do not each invent a subtly
 * different root order or forget to resume the lexical frame. */
typedef struct {
    EvalGcRootFrame continuation;
    EvalGcRootFrame *lexical;
    bool active;
} EvalGcOutcomeSuspension;

static void eval_gc_outcome_suspension_end(
    EvalGcOutcomeSuspension *suspension) {
    if (!suspension || !suspension->active)
        return;
    eval_gc_root_frame_resume(suspension->lexical);
    eval_gc_root_frame_leave(&suspension->continuation);
    suspension->lexical = NULL;
    suspension->active = false;
}

static void eval_gc_outcome_suspension_begin_with_atoms(
    EvalGcOutcomeSuspension *suspension,
    EvalGcRootFrame *lexical,
    OutcomeSet *parent_outcomes,
    OutcomeSet *child_outcomes,
    Atom **live_atoms,
    size_t live_atom_count) {
    assert(suspension && lexical && lexical->linked &&
           lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL &&
           parent_outcomes && child_outcomes);
    assert(!suspension->active);
    assert(live_atom_count == 0u || live_atoms != NULL);
    eval_gc_root_frame_enter_outcome_continuation(
        &suspension->continuation,
        live_atoms, live_atom_count,
        child_outcomes, parent_outcomes);
    eval_gc_root_frame_suspend_precisely(lexical);
    suspension->lexical = lexical;
    suspension->active = true;
}

static void eval_gc_outcome_suspension_begin(
    EvalGcOutcomeSuspension *suspension,
    EvalGcRootFrame *lexical,
    OutcomeSet *parent_outcomes,
    OutcomeSet *child_outcomes) {
    eval_gc_outcome_suspension_begin_with_atoms(
        suspension, lexical, parent_outcomes, child_outcomes,
        NULL, 0u);
}

typedef struct {
    EvalGcRootFrame continuation;
    EvalGcRootFrame *lexical;
    bool active;
} EvalGcLetBranchSuspension;

static void eval_gc_let_branch_suspension_end(
    EvalGcLetBranchSuspension *suspension) {
    if (!suspension || !suspension->active)
        return;
    eval_gc_root_frame_resume(suspension->lexical);
    eval_gc_root_frame_leave(&suspension->continuation);
    suspension->lexical = NULL;
    suspension->active = false;
}

static void eval_gc_let_branch_suspension_begin(
    EvalGcLetBranchSuspension *suspension,
    EvalGcRootFrame *lexical,
    Atom **canonical,
    Atom **pattern,
    Atom **source,
    Atom **scoped_body,
    Atom **fresh,
    size_t fresh_count,
    Bindings *prefix,
    OutcomeSet *source_outcomes,
    OutcomeSet *parent_outcomes) {
    assert(suspension && lexical && lexical->linked &&
           lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL &&
           canonical && pattern && source && scoped_body &&
           prefix && source_outcomes && parent_outcomes);
    assert(!suspension->active);
    assert(fresh_count == 0u || fresh != NULL);
    eval_gc_root_frame_enter_let_branch_continuation(
        &suspension->continuation,
        canonical, pattern, source, scoped_body, fresh, fresh_count,
        prefix, source_outcomes, parent_outcomes);
    eval_gc_root_frame_suspend_precisely(lexical);
    suspension->lexical = lexical;
    suspension->active = true;
}

typedef struct {
    EvalGcRootFrame continuation;
    EvalGcRootFrame *lexical;
    bool active;
} EvalGcCaseBranchSuspension;

static void eval_gc_case_branch_suspension_end(
    EvalGcCaseBranchSuspension *suspension) {
    if (!suspension || !suspension->active)
        return;
    eval_gc_root_frame_resume(suspension->lexical);
    eval_gc_root_frame_leave(&suspension->continuation);
    suspension->lexical = NULL;
    suspension->active = false;
}

static void eval_gc_case_branch_suspension_begin(
    EvalGcCaseBranchSuspension *suspension,
    EvalGcRootFrame *lexical,
    Atom **live_atoms, size_t live_atom_count,
    Bindings *lexical_env, Bindings *branch_env,
    OutcomeSet *source_outcomes,
    OutcomeSet *parent_outcomes) {
    assert(suspension && lexical && lexical->linked &&
           lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL &&
           lexical_env && branch_env && source_outcomes &&
           parent_outcomes);
    assert(!suspension->active);
    assert(live_atom_count == 0u || live_atoms != NULL);
    eval_gc_root_frame_enter_case_branch_continuation(
        &suspension->continuation,
        live_atoms, live_atom_count,
        lexical_env, branch_env,
        source_outcomes, parent_outcomes);
    eval_gc_root_frame_suspend_precisely(lexical);
    suspension->lexical = lexical;
    suspension->active = true;
}

typedef struct {
    EvalGcRootFrame continuation;
    EvalGcRootFrame *lexical;
    bool active;
} EvalGcTypedApplicationSuspension;

static void eval_gc_typed_application_suspension_end(
    EvalGcTypedApplicationSuspension *suspension) {
    if (!suspension || !suspension->active)
        return;
    eval_gc_root_frame_resume(suspension->lexical);
    eval_gc_root_frame_leave(&suspension->continuation);
    suspension->lexical = NULL;
    suspension->active = false;
}

static void eval_gc_typed_application_suspension_begin(
    EvalGcTypedApplicationSuspension *suspension,
    EvalGcRootFrame *lexical,
    Atom **live_atoms, size_t live_atom_count,
    Atom **overload_types, size_t overload_type_count,
    Atom **return_contracts, size_t return_contract_count,
    Atom **applicability_errors, size_t applicability_error_count,
    Atom **argument_types, size_t argument_type_count,
    OutcomeSet *function_results, OutcomeSet *heads,
    OutcomeSet *parent_outcomes) {
    assert(suspension && lexical && lexical->linked &&
           lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL &&
           parent_outcomes);
    assert(!suspension->active);
    assert(live_atom_count == 0u || live_atoms != NULL);
    assert(overload_type_count == 0u || overload_types != NULL);
    assert(return_contract_count == 0u || return_contracts != NULL);
    assert(applicability_error_count == 0u ||
           applicability_errors != NULL);
    assert(argument_type_count == 0u || argument_types != NULL);
    eval_gc_root_frame_enter_typed_application_continuation(
        &suspension->continuation,
        live_atoms, live_atom_count,
        overload_types, overload_type_count,
        return_contracts, return_contract_count,
        applicability_errors, applicability_error_count,
        argument_types, argument_type_count,
        function_results, heads, parent_outcomes);
    eval_gc_root_frame_suspend_precisely(lexical);
    suspension->lexical = lexical;
    suspension->active = true;
}

typedef struct {
    EvalGcRootFrame continuation;
    EvalGcRootFrame *lexical;
    bool lexical_was_precise;
    bool active;
} EvalGcObservationNormalizationSuspension;

static void eval_gc_observation_normalization_suspension_end(
    EvalGcObservationNormalizationSuspension *suspension) {
    if (!suspension || !suspension->active)
        return;
    assert(suspension->lexical && suspension->lexical->linked &&
           suspension->lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL);
    suspension->lexical->precise_suspension =
        suspension->lexical_was_precise;
    eval_gc_root_frame_leave(&suspension->continuation);
    suspension->lexical = NULL;
    suspension->active = false;
}

static void eval_gc_observation_normalization_suspension_begin(
    EvalGcObservationNormalizationSuspension *suspension,
    EvalGcRootFrame *lexical,
    Atom **source, Atom **value,
    Atom **children, size_t child_count,
    Bindings *env, OutcomeSet *child_outcomes,
    OutcomeSet *parent_outcomes) {
    if (!suspension || !lexical)
        return;
    assert(lexical->linked &&
           lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL &&
           parent_outcomes && !suspension->active);
    assert(child_count == 0u || children != NULL);
    eval_gc_root_frame_enter_observation_normalization(
        &suspension->continuation,
        source, value, children, child_count,
        env, child_outcomes, parent_outcomes);
    suspension->lexical_was_precise = lexical->precise_suspension;
    eval_gc_root_frame_suspend_precisely(lexical);
    suspension->lexical = lexical;
    suspension->active = true;
}

typedef struct {
    EvalGcRootFrame continuation;
    EvalGcRootFrame *lexical;
    bool lexical_was_precise;
    bool active;
} EvalGcTupleMachineSuspension;

static void eval_gc_tuple_machine_suspension_end(
    EvalGcTupleMachineSuspension *suspension) {
    if (!suspension || !suspension->active)
        return;
    if (suspension->lexical) {
        assert(suspension->lexical->linked &&
               suspension->lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL);
        suspension->lexical->precise_suspension =
            suspension->lexical_was_precise;
    }
    eval_gc_root_frame_leave(&suspension->continuation);
    suspension->lexical = NULL;
    suspension->active = false;
}

static void eval_gc_tuple_machine_suspension_begin(
    EvalGcTupleMachineSuspension *suspension,
    EvalGcRootFrame *lexical,
    Atom **continuation_atoms, size_t continuation_atom_count,
    Atom **orig_elems, size_t orig_count,
    Atom **prefix, size_t prefix_count,
    Bindings *env, OutcomeSet *outcomes,
    OutcomeSet *parent_outcomes) {
    if (!suspension)
        return;
    assert(!suspension->active);
    assert(continuation_atom_count == 0u || continuation_atoms != NULL);
    assert(orig_count == 0u || orig_elems != NULL);
    assert(prefix_count == 0u || prefix != NULL);
    eval_gc_root_frame_enter_tuple_machine(
        &suspension->continuation,
        continuation_atoms, continuation_atom_count,
        orig_elems, orig_count, prefix, prefix_count,
        env, outcomes, parent_outcomes);
    if (lexical) {
        assert(lexical->linked &&
               lexical->kind == CETTA_EVAL_GC_FRAME_LEXICAL);
        suspension->lexical_was_precise = lexical->precise_suspension;
        eval_gc_root_frame_suspend_precisely(lexical);
        suspension->lexical = lexical;
    }
    suspension->active = true;
}

static __attribute__((unused)) void eval_gc_external_owner_enter(void) {
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    g_eval_gc.external_owner_depth++;
}

static __attribute__((unused)) void eval_gc_external_owner_leave(void) {
    assert(g_eval_gc.external_owner_depth > 0u);
    g_eval_gc.external_owner_depth--;
}

static void eval_gc_root_frame_leave(EvalGcRootFrame *frame) {
    if (!frame || !frame->linked)
        return;
    if (g_eval_gc.roots == frame) {
        g_eval_gc.roots = frame->previous;
    } else {
        EvalGcRootFrame *newer = g_eval_gc.roots;
        while (newer && newer->previous != frame)
            newer = newer->previous;
        if (newer)
            newer->previous = frame->previous;
    }
    frame->previous = NULL;
    frame->linked = false;
}

static inline bool eval_gc_can_collect_arena(const Arena *arena) {
    /* Hash-cons entries own their interned atoms on the heap.  Eligible eval
       children are interned before they can appear inside an interned parent,
       so resetting the eval arena does not leave hash-cons entries pointing
       into reclaimed eval blocks. */
    return arena != NULL;
}

static inline bool eval_gc_root_chain_is_precise(
    const EvalGcRootFrame *frame) {
    bool precise_chain = frame && g_eval_gc.roots == frame;
    for (const EvalGcRootFrame *root = frame ? frame->previous : NULL;
         precise_chain && root; root = root->previous) {
        if (root->kind == CETTA_EVAL_GC_FRAME_LEXICAL &&
            !root->precise_suspension)
            precise_chain = false;
    }
    return precise_chain;
}

static inline bool eval_gc_safe_point(const Arena *arena,
                                      const EvalGcRootFrame *frame,
                                      size_t os_len,
                                      size_t live_above_anchor) {
    /*
     * The recursive C evaluator has no compiler stack maps for arbitrary
     * OutcomeSet and Atom locals held by older lexical frames.  Moving an
     * arena while such a frame exists would require guessing its roots.  A
     * nested tail-dispatch frame becomes precise only while each suspended
     * lexical ancestor has registered its remaining live carriers through
     * generated root frames; explicit machines have complete frame scanners.
     */
    if (!eval_gc_enabled() || !eval_gc_can_collect_arena(arena) ||
        g_eval_gc.region_arena != arena ||
        live_above_anchor < g_eval_gc.budget_bytes) {
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_COLLECTION_CANDIDATE);
    bool precise_chain = eval_gc_root_chain_is_precise(frame);
    if (!precise_chain) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_IMPRECISE_ROOT);
    }
    if (g_eval_gc.external_owner_depth != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_EXTERNAL_OWNER);
    }
    if (os_len != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_LIVE_OUTCOME);
    }
    return precise_chain && g_eval_gc.external_owner_depth == 0u &&
           os_len == 0u;
}

/* A copying collector must not recopy a growing live graph after every fixed
 * quantum of fresh allocation.  Requiring at least one survivor-graph's worth
 * of fresh allocation in addition to the configured nursery quantum gives
 * the usual semispace amortization: as the continuation grows, collection
 * intervals grow with it.  Callers that do not retain a multi-root survivor
 * graph pass zero. */
static inline bool eval_gc_budget_reached_with_floor(
    const Arena *arena, ArenaMark anchor, size_t survivor_floor_bytes) {
    if (!eval_gc_enabled() || !eval_gc_can_collect_arena(arena))
        return false;
    size_t accounted_live = arena_accounted_live_bytes(arena);
    size_t accounted_anchor = arena_mark_accounted_live_bytes(anchor);
    size_t live_above_anchor =
        accounted_live >= accounted_anchor
            ? accounted_live - accounted_anchor
            : 0u;
    size_t effective_budget = g_eval_gc.budget_bytes;
    if (survivor_floor_bytes > SIZE_MAX - effective_budget)
        effective_budget = SIZE_MAX;
    else
        effective_budget += survivor_floor_bytes;
    return live_above_anchor >= effective_budget;
}

static inline bool eval_gc_budget_reached(const Arena *arena,
                                          ArenaMark anchor) {
    return eval_gc_budget_reached_with_floor(arena, anchor, 0u);
}

static void eval_gc_note_survivor_usage(void) {
    if (!g_eval_gc.ready)
        return;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_ARENA_LIVE_BYTES_PEAK,
        (uint64_t)arena_accounted_live_bytes(&g_eval_gc.survivor));
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_ARENA_RESERVED_BYTES_PEAK,
        (uint64_t)g_eval_gc.survivor.reserved_bytes);
}

static inline void eval_gc_commit_evacuated(
    Arena *eval_arena, ArenaMark anchor, Arena *evacuated);

/* An initialized, event-free branch-state carrier is the identity element of
 * the persistent event DAG: it owns no arena allocation, but its owner is the
 * allocation authority for the first future event.  Rehome that authority
 * only when collection invalidates its current owner.  Persistent branch
 * state belongs to the Prime episode arena, never to either semispace. */
static bool eval_gc_rehome_empty_prime_branch_state(
    const EvalGcEvacuationContext *context,
    PrimeNeedBranchState *state) {
    if (!state || !prime_need_branch_state_present(state) ||
        prime_need_branch_state_has_events(state))
        return true;
    if (!context ||
        (state->owner != context->collected_arena &&
         state->owner != context->old_survivor))
        return true;
    if (!context->prime_episode_owner ||
        context->prime_episode_owner == context->collected_arena ||
        context->prime_episode_owner == context->old_survivor)
        return false;
    return
        prime_need_branch_state_promote(
            context->prime_episode_owner, state);
}

static bool eval_gc_rehome_empty_prime_bindings(
    const EvalGcEvacuationContext *context, Bindings *env) {
    if (!env || !bindings_prime_present(env))
        return true;
    if (!eval_gc_rehome_empty_prime_branch_state(
            context, bindings_branch_state_mut(env)))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!eval_gc_rehome_empty_prime_branch_state(
            context, bindings_receipt_mut(env)))
        return false;
#endif
    return true;
}

static bool eval_gc_evacuate_outcomes(
    EvalGcEvacuationContext *context, OutcomeSet *outcomes) {
    if (!outcomes)
        return true;
    if (!context || !context->atom_copy_session)
        return false;
    AtomDeepCopySession *session = context->atom_copy_session;
    for (CettaCount i = 0u; i < outcomes->len; i++) {
        Outcome *outcome = &outcomes->items[i];
        if (outcome->atom) {
            Atom *next = atom_deep_copy_session_copy(
                session, outcome->atom);
            if (!next)
                return false;
            outcome->atom = next;
        }
        if (!bindings_promote_logical_atoms_with_session(
                &outcome->env, session) ||
            !eval_gc_rehome_empty_prime_bindings(
                context, &outcome->env) ||
            !variant_instance_promote_atoms_with_session(
                session, &outcome->variant))
            return false;
        for (uint32_t j = 0u;
             j < outcome->answer_ref.goal_instantiation.len; j++) {
            Atom **mapped =
                &outcome->answer_ref.goal_instantiation.items[j].mapped_var;
            if (!*mapped)
                continue;
            Atom *next = atom_deep_copy_session_copy(session, *mapped);
            if (!next)
                return false;
            *mapped = next;
        }
        outcome_refresh_materialized_fast_path(outcome);
    }
    return true;
}

#define CETTA_GC_VISIT_STRONG_ATOM_SLOT(CONTEXT, SLOT_PTR, FAIL) do {      \
    Atom **cetta_gc_slot__ = (SLOT_PTR);                                  \
    if (cetta_gc_slot__ && *cetta_gc_slot__) {                            \
        Atom *cetta_gc_next__ = atom_deep_copy_session_copy(              \
            (CONTEXT)->atom_copy_session, *cetta_gc_slot__);              \
        if (!cetta_gc_next__) { FAIL; }                                   \
        *cetta_gc_slot__ = cetta_gc_next__;                               \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(CONTEXT, SPAN, FAIL) do {          \
    CettaEvalGcAtomSpan cetta_gc_span__ = (SPAN);                         \
    for (size_t cetta_gc_i__ = 0u;                                       \
         cetta_gc_i__ < cetta_gc_span__.len; cetta_gc_i__++) {           \
        CETTA_GC_VISIT_STRONG_ATOM_SLOT(                                  \
            (CONTEXT), &cetta_gc_span__.items[cetta_gc_i__], FAIL);       \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_LOGICAL_BINDINGS(CONTEXT, ENV_PTR, FAIL) do {       \
    Bindings *cetta_gc_env__ = (ENV_PTR);                                 \
    if (cetta_gc_env__) {                                                 \
        cetta_runtime_stats_update_max(                                   \
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_ENTRIES_PEAK,\
            (uint64_t)cetta_gc_env__->len);                               \
        cetta_runtime_stats_update_max(                                   \
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_CONSTRAINTS_PEAK,\
            (uint64_t)cetta_gc_env__->eq_len);                            \
        if (!bindings_promote_logical_atoms_with_session(                 \
                cetta_gc_env__, (CONTEXT)->atom_copy_session) ||          \
            !eval_gc_rehome_empty_prime_bindings(                         \
                (CONTEXT), cetta_gc_env__)) {                             \
            FAIL;                                                         \
        }                                                                 \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_OUTCOME_SET(CONTEXT, OS_PTR, FAIL) do {             \
    OutcomeSet *cetta_gc_outcomes__ = (OS_PTR);                            \
    if (cetta_gc_outcomes__ &&                                            \
        !eval_gc_evacuate_outcomes(                                       \
            (CONTEXT), cetta_gc_outcomes__)) { FAIL; }                    \
} while (0)

#define CETTA_GC_VISIT_VARIANT_INSTANCE(CONTEXT, INSTANCE_PTR, FAIL) do {  \
    VariantInstance *cetta_gc_instance__ = (INSTANCE_PTR);                 \
    if (cetta_gc_instance__ &&                                            \
        !variant_instance_promote_atoms_with_session(                     \
            (CONTEXT)->atom_copy_session, cetta_gc_instance__)) { FAIL; } \
} while (0)

#define CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(CONTEXT, MAP, FAIL) do {         \
    CettaEvalGcEphemeronAtomMap cetta_gc_map__ = (MAP);                   \
    if (!cetta_gc_map__.visit ||                                         \
        !cetta_gc_map__.visit(                                            \
            (CONTEXT)->atom_copy_session, cetta_gc_map__.context)) {      \
        FAIL;                                                             \
    }                                                                     \
} while (0)

static bool eval_gc_evacuate_root_frame(
    EvalGcEvacuationContext *context, EvalGcRootFrame *frame) {
    if (!context || !context->atom_copy_session || !frame)
        return false;
    switch (frame->kind) {
#define CETTA_EVAL_GC_DISPATCH_FRAME(kind, upper)                          \
    case CETTA_EVAL_GC_FRAME_##upper:                                     \
        CETTA_EVAL_GC_ARM_##kind(                                         \
            context, &frame->payload.kind, goto failed);                  \
        return true;
        CETTA_EVAL_GC_FRAME_KIND_ROWS(CETTA_EVAL_GC_DISPATCH_FRAME)
#undef CETTA_EVAL_GC_DISPATCH_FRAME
    default:
        return false;
    }
failed:
    return false;
}

static bool eval_gc_evacuate_root_chain(
    EvalGcEvacuationContext *context, EvalGcRootFrame *roots) {
    for (EvalGcRootFrame *frame = roots; frame; frame = frame->previous) {
        if (!eval_gc_evacuate_root_frame(context, frame))
            return false;
    }
    return true;
}

#undef CETTA_GC_VISIT_OUTCOME_SET
#undef CETTA_GC_VISIT_VARIANT_INSTANCE
#undef CETTA_GC_VISIT_EPHEMERON_ATOM_MAP
#undef CETTA_GC_VISIT_LOGICAL_BINDINGS
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
#undef CETTA_GC_VISIT_STRONG_ATOM_SLOT

/* Generic recursive evaluation shares the generated root-frame catalogue
 * with the explicit Prime stack, but it cannot discover an unsafe persistent
 * carrier after evacuation has begun: earlier root slots may already point
 * into the fresh semispace.  Audit the Prime components of every generated
 * logical root first, against both arenas that commit will invalidate. */
typedef struct {
    const EvalGcEvacuationContext *evacuation;
    PrimeNeedArenaAudit *collected_audit;
    PrimeNeedArenaAudit *survivor_audit;
} EvalGcPrimePreflightContext;

static bool eval_gc_preflight_prime_carrier(
    EvalGcPrimePreflightContext *context,
    const PrimeNeedBranchState *carrier) {
    if (!carrier || !prime_need_branch_state_present(carrier))
        return true;
    if (!prime_need_branch_state_has_events(carrier)) {
        if (carrier->owner != context->evacuation->collected_arena &&
            carrier->owner != context->evacuation->old_survivor)
            return true;
        Arena *owner = context->evacuation->prime_episode_owner;
        return owner &&
            owner != context->evacuation->collected_arena &&
            owner != context->evacuation->old_survivor;
    }
    return prime_need_arena_audit_branch_state(
               context->collected_audit, carrier) &&
           prime_need_arena_audit_branch_state(
               context->survivor_audit, carrier);
}

static bool eval_gc_preflight_prime_bindings(
    EvalGcPrimePreflightContext *context, const Bindings *env) {
    if (!context || !context->evacuation)
        return false;
    if (!env || !bindings_prime_present(env))
        return true;
    if (!prime_need_arena_audit_snapshot(
            context->collected_audit, bindings_need_view(env)) ||
        !prime_need_arena_audit_snapshot(
            context->survivor_audit, bindings_need_view(env)) ||
        !eval_gc_preflight_prime_carrier(
            context, bindings_branch_state_view(env)))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!eval_gc_preflight_prime_carrier(
            context, bindings_receipt_view(env)))
        return false;
#endif
    return true;
}

static bool eval_gc_preflight_prime_outcomes(
    EvalGcPrimePreflightContext *context, const OutcomeSet *outcomes) {
    if (!outcomes)
        return true;
    for (CettaCount i = 0u; i < outcomes->len; i++) {
        if (!eval_gc_preflight_prime_bindings(
                context, &outcomes->items[i].env))
            return false;
    }
    return true;
}

#define CETTA_GC_VISIT_STRONG_ATOM_SLOT(CONTEXT, SLOT_PTR, FAIL) do {      \
    (void)(CONTEXT);                                                       \
    (void)(SLOT_PTR);                                                      \
} while (0)

#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(CONTEXT, SPAN, FAIL) do {          \
    (void)(CONTEXT);                                                       \
    (void)(SPAN);                                                          \
} while (0)

#define CETTA_GC_VISIT_LOGICAL_BINDINGS(CONTEXT, ENV_PTR, FAIL) do {       \
    if (!eval_gc_preflight_prime_bindings((CONTEXT), (ENV_PTR))) {        \
        FAIL;                                                              \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_OUTCOME_SET(CONTEXT, OS_PTR, FAIL) do {             \
    if (!eval_gc_preflight_prime_outcomes((CONTEXT), (OS_PTR))) {         \
        FAIL;                                                              \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_VARIANT_INSTANCE(CONTEXT, INSTANCE_PTR, FAIL) do {  \
    (void)(CONTEXT);                                                       \
    (void)(INSTANCE_PTR);                                                  \
} while (0)

#define CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(CONTEXT, MAP, FAIL) do {         \
    (void)(CONTEXT);                                                       \
    (void)(MAP);                                                           \
} while (0)

static bool eval_gc_preflight_prime_root_frame(
    EvalGcPrimePreflightContext *context,
    const EvalGcRootFrame *frame) {
    if (!context || !frame)
        return false;
    switch (frame->kind) {
#define CETTA_EVAL_GC_PREFLIGHT_FRAME(kind, upper)                         \
    case CETTA_EVAL_GC_FRAME_##upper:                                     \
        CETTA_EVAL_GC_ARM_##kind(                                         \
            context, &frame->payload.kind, goto failed);                  \
        return true;
        CETTA_EVAL_GC_FRAME_KIND_ROWS(CETTA_EVAL_GC_PREFLIGHT_FRAME)
#undef CETTA_EVAL_GC_PREFLIGHT_FRAME
    default:
        return false;
    }
failed:
    return false;
}

static bool eval_gc_preflight_prime_root_chain(
    const EvalGcEvacuationContext *evacuation,
    const EvalGcRootFrame *roots,
    const PrimeNeedSnapshot *active_need,
    const PrimeNeedBranchState *active_branch_state
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    , const PrimeNeedReceipt *active_receipt
#endif
    ) {
    if (!evacuation || !evacuation->collected_arena ||
        !evacuation->old_survivor)
        return false;
    EvalGcPrimePreflightContext context = {
        .evacuation = evacuation,
        .collected_audit = prime_need_arena_audit_new(
            evacuation->collected_arena),
        .survivor_audit = prime_need_arena_audit_new(
            evacuation->old_survivor),
    };
    bool valid = context.collected_audit && context.survivor_audit;
    if (valid && active_need) {
        valid = prime_need_arena_audit_snapshot(
                    context.collected_audit, active_need) &&
                prime_need_arena_audit_snapshot(
                    context.survivor_audit, active_need);
    }
    if (valid && active_branch_state)
        valid = eval_gc_preflight_prime_carrier(
            &context, active_branch_state);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (valid && active_receipt)
        valid = eval_gc_preflight_prime_carrier(
            &context, active_receipt);
#endif
    for (const EvalGcRootFrame *frame = roots;
         valid && frame; frame = frame->previous) {
        valid = eval_gc_preflight_prime_root_frame(&context, frame);
    }
    prime_need_arena_audit_free(context.collected_audit);
    prime_need_arena_audit_free(context.survivor_audit);
    return valid;
}

#undef CETTA_GC_VISIT_EPHEMERON_ATOM_MAP
#undef CETTA_GC_VISIT_VARIANT_INSTANCE
#undef CETTA_GC_VISIT_OUTCOME_SET
#undef CETTA_GC_VISIT_LOGICAL_BINDINGS
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
#undef CETTA_GC_VISIT_STRONG_ATOM_SLOT

static void eval_gc_collect(Arena *eval_arena, ArenaMark anchor,
                            Atom **atom_io, Bindings *env,
                            Atom **etype_io,
                            Arena *prime_episode_owner,
                            PrimeNeedSnapshot *active_need,
                            PrimeNeedBranchState *active_branch_state
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
                            , PrimeNeedReceipt *active_receipt
#endif
                            ) {
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    if (!eval_gc_can_collect_arena(eval_arena))
        return;

    EvalGcRootFrame fallback = {
        .kind = CETTA_EVAL_GC_FRAME_LEXICAL,
        .payload.lexical = {
            .atom_io = atom_io,
            .env = env,
            .etype_io = etype_io,
        },
    };
    EvalGcRootFrame *roots = g_eval_gc.roots
        ? g_eval_gc.roots : &fallback;

    EvalGcEvacuationContext context = {
        .atom_copy_session = NULL,
        .collected_arena = eval_arena,
        .old_survivor = &g_eval_gc.survivor,
        .prime_episode_owner = prime_episode_owner,
    };
    if (!eval_gc_preflight_prime_root_chain(
            &context, roots, active_need, active_branch_state
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            , active_receipt
#endif
            ))
        return;

    Arena evacuated;
    eval_gc_init_survivor_arena(&evacuated);
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&evacuated);
    if (!session) {
        arena_free(&evacuated);
        return;
    }

    context.atom_copy_session = session;
    bool copied = eval_gc_evacuate_root_chain(&context, roots) &&
        eval_gc_rehome_empty_prime_branch_state(
            &context, active_branch_state);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    copied = copied && eval_gc_rehome_empty_prime_branch_state(
        &context, active_receipt);
#endif
    atom_deep_copy_session_free(session);
    if (!copied) {
        /* Some registered roots may already name the fresh semispace. */
        fputs("fatal: evaluator root evacuation failed\n", stderr);
        abort();
    }

    eval_gc_commit_evacuated(eval_arena, anchor, &evacuated);
}

/* Commit an already-evacuated multi-root collection.  The evacuated arena is
   moved into the stable thread-local survivor slot so external owner pointers
   keep naming one Arena object across collections. */
static inline void eval_gc_commit_evacuated(
    Arena *eval_arena, ArenaMark anchor, Arena *evacuated) {
    if (!eval_arena || !evacuated)
        return;
    if (!g_eval_gc.ready)
        eval_gc_init_once();

    size_t before = arena_accounted_live_bytes(eval_arena);
    eval_gc_note_survivor_usage();
    arena_reset(eval_arena, anchor);

    /* The provenance registry keys arenas by the Arena object's address.
       Unregister the temporary before moving its block lists. */
    arena_set_runtime_kind(
        evacuated, CETTA_ARENA_RUNTIME_KIND_OTHER);
    arena_free(&g_eval_gc.survivor);
    g_eval_gc.survivor = *evacuated;
    memset(evacuated, 0, sizeof(*evacuated));
    arena_set_runtime_kind(
        &g_eval_gc.survivor,
        CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    eval_gc_note_survivor_usage();

    size_t after = arena_accounted_live_bytes(eval_arena);
    size_t reclaimed = before > after ? before - after : 0u;
    g_eval_gc.collections++;
    g_eval_gc.reclaimed_bytes += reclaimed;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_SAFE_POINT_COUNT);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_RECLAIMED_BYTES,
        (uint64_t)reclaimed);
}

static void eval_gc_survivor_reset(void) {
    if (!g_eval_gc.ready)
        return;
    eval_gc_note_survivor_usage();
    size_t reset_bytes = arena_accounted_live_bytes(&g_eval_gc.survivor);
    if (reset_bytes == 0 && g_eval_gc.survivor.reserved_bytes == 0 &&
        g_eval_gc.survivor.spare_bytes == 0) {
        return;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_RESET_COUNT);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_RESET_BYTES,
                            (uint64_t)reset_bytes);
    arena_free(&g_eval_gc.survivor);
    eval_gc_init_survivor_arena(&g_eval_gc.survivor);
}

#endif
