/* Tail-safe-point arena reclamation for the MeTTa evaluator.
 *
 * Long tail-recursive evaluations can run as a single top-level ! form.  The
 * per-form eval arena is normally reset only after that form returns, so dead
 * intermediates can accumulate for the whole run.  This module reclaims those
 * intermediates at the evaluator trampoline's tail-safe point after evacuating
 * the continuation roots.
 *
 * GC is normal runtime behavior.  CETTA_GC=0 is retained as a temporary
 * differential/debug escape hatch while this path is being validated.
 */
#ifndef CETTA_EVAL_GC_H
#define CETTA_EVAL_GC_H

typedef struct {
    Arena survivor;
    bool ready;
    bool enabled;
    size_t budget_bytes;
    uint64_t collections;
    uint64_t reclaimed_bytes;
} EvalGc;

static __thread EvalGc g_eval_gc;

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

    arena_init(&g_eval_gc.survivor);
    arena_set_hashcons(&g_eval_gc.survivor, NULL);
    arena_set_runtime_kind(&g_eval_gc.survivor,
                           CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    g_eval_gc.ready = true;
}

static inline bool eval_gc_enabled(void) {
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    return g_eval_gc.enabled;
}

static inline bool eval_gc_can_collect_arena(const Arena *arena) {
    /* Hash-cons entries own their interned atoms on the heap.  Eligible eval
       children are interned before they can appear inside an interned parent,
       so resetting the eval arena does not leave hash-cons entries pointing
       into reclaimed eval blocks. */
    return arena != NULL;
}

static inline bool eval_gc_safe_point(const Arena *arena, size_t os_len,
                                      size_t live_above_anchor) {
    return eval_gc_enabled() && eval_gc_can_collect_arena(arena) && os_len == 0 &&
           live_above_anchor >= g_eval_gc.budget_bytes;
}

static void eval_gc_note_survivor_usage(void) {
    if (!g_eval_gc.ready)
        return;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_ARENA_LIVE_BYTES_PEAK,
        (uint64_t)g_eval_gc.survivor.live_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_ARENA_RESERVED_BYTES_PEAK,
        (uint64_t)g_eval_gc.survivor.reserved_bytes);
}

static void eval_gc_collect(Arena *eval_arena, ArenaMark anchor,
                            Atom **atom_io, Bindings *env,
                            Atom **etype_io) {
    if (!g_eval_gc.ready)
        eval_gc_init_once();
    if (!eval_gc_can_collect_arena(eval_arena))
        return;

    size_t before = eval_arena->live_bytes;
    Arena *survivor = &g_eval_gc.survivor;

    if (atom_io && *atom_io)
        *atom_io = atom_deep_copy(survivor, *atom_io);
    if (env) {
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_ENTRIES_PEAK,
            (uint64_t)env->len);
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_CONSTRAINTS_PEAK,
            (uint64_t)env->eq_len);
        (void)bindings_promote_atoms_to_arena(env, survivor);
    }
    if (etype_io && *etype_io)
        *etype_io = atom_deep_copy(survivor, *etype_io);
    eval_gc_note_survivor_usage();

    arena_reset(eval_arena, anchor);

    size_t after = eval_arena->live_bytes;
    size_t reclaimed = before > after ? before - after : 0;
    g_eval_gc.collections++;
    g_eval_gc.reclaimed_bytes += reclaimed;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SAFE_POINT_COUNT);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_EVAL_TAIL_RECLAIMED_BYTES,
                            (uint64_t)reclaimed);
}

static void eval_gc_survivor_reset(void) {
    if (!g_eval_gc.ready)
        return;
    eval_gc_note_survivor_usage();
    size_t reset_bytes = g_eval_gc.survivor.live_bytes;
    if (reset_bytes == 0 && g_eval_gc.survivor.reserved_bytes == 0 &&
        g_eval_gc.survivor.spare_bytes == 0) {
        return;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_RESET_COUNT);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SURVIVOR_RESET_BYTES,
                            (uint64_t)reset_bytes);
    arena_free(&g_eval_gc.survivor);
    arena_init(&g_eval_gc.survivor);
    arena_set_hashcons(&g_eval_gc.survivor, NULL);
    arena_set_runtime_kind(&g_eval_gc.survivor,
                           CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
}

#endif
