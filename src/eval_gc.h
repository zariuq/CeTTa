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
#define CETTA_EVAL_GC_DECLARE_EPHEMERON_ATOM_MAP(name) \
    CettaEvalGcEphemeronAtomMap name;
#define CETTA_EVAL_GC_DECLARE_PAYLOAD(kind, upper)                         \
    typedef struct {                                                      \
        CETTA_EVAL_GC_FRAME_FIELDS_##kind(                                \
            CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SLOT,                       \
            CETTA_EVAL_GC_DECLARE_STRONG_ATOM_SPAN,                       \
            CETTA_EVAL_GC_DECLARE_LOGICAL_BINDINGS,                       \
            CETTA_EVAL_GC_DECLARE_OUTCOME_SET,                            \
            CETTA_EVAL_GC_DECLARE_EPHEMERON_ATOM_MAP)                     \
    } CettaEvalGcPayload_##kind;
CETTA_EVAL_GC_FRAME_KIND_ROWS(CETTA_EVAL_GC_DECLARE_PAYLOAD)
#undef CETTA_EVAL_GC_DECLARE_PAYLOAD
#undef CETTA_EVAL_GC_DECLARE_EPHEMERON_ATOM_MAP
#undef CETTA_EVAL_GC_DECLARE_OUTCOME_SET
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
    bool ready;
    bool enabled;
    uint32_t external_owner_depth;
    size_t budget_bytes;
    uint64_t collections;
    uint64_t reclaimed_bytes;
} EvalGc;

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
    g_eval_gc.external_owner_depth = 0u;

    eval_gc_init_survivor_arena(&g_eval_gc.survivor);
    g_eval_gc.ready = true;
}

static inline bool eval_gc_enabled(void) {
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    return g_eval_gc.enabled;
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
    bool precise_chain = eval_gc_root_chain_is_precise(frame);
    return precise_chain &&
           g_eval_gc.external_owner_depth == 0u && eval_gc_enabled() &&
           eval_gc_can_collect_arena(arena) && os_len == 0 &&
           live_above_anchor >= g_eval_gc.budget_bytes;
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

static bool eval_gc_evacuate_outcomes(
    AtomDeepCopySession *session, OutcomeSet *outcomes) {
    if (!outcomes)
        return true;
    if (!session)
        return false;
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

#define CETTA_GC_VISIT_STRONG_ATOM_SLOT(SESSION, SLOT_PTR, FAIL) do {      \
    Atom **cetta_gc_slot__ = (SLOT_PTR);                                  \
    if (cetta_gc_slot__ && *cetta_gc_slot__) {                            \
        Atom *cetta_gc_next__ = atom_deep_copy_session_copy(              \
            (SESSION), *cetta_gc_slot__);                                 \
        if (!cetta_gc_next__) { FAIL; }                                   \
        *cetta_gc_slot__ = cetta_gc_next__;                               \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(SESSION, SPAN, FAIL) do {          \
    CettaEvalGcAtomSpan cetta_gc_span__ = (SPAN);                         \
    for (size_t cetta_gc_i__ = 0u;                                       \
         cetta_gc_i__ < cetta_gc_span__.len; cetta_gc_i__++) {           \
        CETTA_GC_VISIT_STRONG_ATOM_SLOT(                                  \
            (SESSION), &cetta_gc_span__.items[cetta_gc_i__], FAIL);       \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_LOGICAL_BINDINGS(SESSION, ENV_PTR, FAIL) do {       \
    Bindings *cetta_gc_env__ = (ENV_PTR);                                 \
    if (cetta_gc_env__) {                                                 \
        cetta_runtime_stats_update_max(                                   \
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_ENTRIES_PEAK,\
            (uint64_t)cetta_gc_env__->len);                               \
        cetta_runtime_stats_update_max(                                   \
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_CONSTRAINTS_PEAK,\
            (uint64_t)cetta_gc_env__->eq_len);                            \
        if (!bindings_promote_logical_atoms_with_session(                 \
                cetta_gc_env__, (SESSION))) { FAIL; }                     \
    }                                                                     \
} while (0)

#define CETTA_GC_VISIT_OUTCOME_SET(SESSION, OS_PTR, FAIL) do {             \
    if (!eval_gc_evacuate_outcomes((SESSION), (OS_PTR))) { FAIL; }        \
} while (0)

#define CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(SESSION, MAP, FAIL) do {         \
    CettaEvalGcEphemeronAtomMap cetta_gc_map__ = (MAP);                   \
    if (!cetta_gc_map__.visit ||                                         \
        !cetta_gc_map__.visit((SESSION), cetta_gc_map__.context)) {       \
        FAIL;                                                             \
    }                                                                     \
} while (0)

static bool eval_gc_evacuate_root_frame(
    AtomDeepCopySession *session, EvalGcRootFrame *frame) {
    if (!session || !frame)
        return false;
    switch (frame->kind) {
#define CETTA_EVAL_GC_DISPATCH_FRAME(kind, upper)                          \
    case CETTA_EVAL_GC_FRAME_##upper:                                     \
        CETTA_EVAL_GC_ARM_##kind(                                         \
            session, &frame->payload.kind, goto failed);                  \
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
    AtomDeepCopySession *session, EvalGcRootFrame *roots) {
    for (EvalGcRootFrame *frame = roots; frame; frame = frame->previous) {
        if (!eval_gc_evacuate_root_frame(session, frame))
            return false;
    }
    return true;
}

#undef CETTA_GC_VISIT_OUTCOME_SET
#undef CETTA_GC_VISIT_EPHEMERON_ATOM_MAP
#undef CETTA_GC_VISIT_LOGICAL_BINDINGS
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
#undef CETTA_GC_VISIT_STRONG_ATOM_SLOT

static void eval_gc_collect(Arena *eval_arena, ArenaMark anchor,
                            Atom **atom_io, Bindings *env,
                            Atom **etype_io) {
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

    Arena evacuated;
    eval_gc_init_survivor_arena(&evacuated);
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(&evacuated);
    if (!session) {
        arena_free(&evacuated);
        return;
    }

    bool copied = eval_gc_evacuate_root_chain(session, roots);
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
