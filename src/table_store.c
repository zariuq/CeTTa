#include "table_store.h"
#include "stats.h"
#include "term_universe.h"
#include "variant_instance.h"
#include "variant_shape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AnswerRef *items;
    CettaCount len;
    CettaCount cap;
} TableStoredAnswers;

struct TableStoreEntry {
    Arena arena;
    Space *space;
    uint64_t space_instance_id;
    uint64_t revision;
    Atom *goal_key;
    uint32_t goal_hash;
    TableStoredAnswers results;
};

typedef struct {
    TableStoreEntry *exact;
    TableStoreEntry *reusable;
    uint32_t exact_index;
    uint32_t reusable_index;
    bool stale;
} TableStoreMatch;

typedef struct {
    TableStore *store;
    TableStoreEntry staged;
    CettaVarMap query_map;
    CettaVarMap goal_instantiation;
    uint32_t target_index;
    bool target_reusable;
    bool target_reserved_new;
    bool target_stale;
} TableQueryState;

static const CettaVariantShapeOptions kTableStoreVariantOptions = {
    .slot_policy = CETTA_VARIANT_SLOT_ORDINAL_NAME,
    .slot_name = "$T",
    .share_immutable = false,
};

static void table_stored_answers_init(TableStoredAnswers *answers) {
    if (!answers)
        return;
    answers->items = NULL;
    answers->len = 0;
    answers->cap = 0;
}

static void table_stored_answers_free(TableStoredAnswers *answers) {
    if (!answers)
        return;
    free(answers->items);
    answers->items = NULL;
    answers->len = 0;
    answers->cap = 0;
}

static CettaCount table_stored_answers_capacity_limit(void) {
    size_t limit = SIZE_MAX / sizeof(AnswerRef);
    if ((uint64_t)limit > CETTA_ANSWER_BANK_MAX_RECORDS)
        return CETTA_ANSWER_BANK_MAX_RECORDS;
    return (CettaCount)limit;
}

static bool table_stored_answers_reserve(TableStoredAnswers *answers,
                                         CettaCount needed) {
    CettaCount next_cap;
    CettaCount limit;
    if (!answers)
        return false;
    limit = table_stored_answers_capacity_limit();
    if (needed > limit)
        return false;
    if (needed <= answers->cap)
        return true;
    next_cap = answers->cap ? answers->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    if (next_cap > limit)
        next_cap = limit;
    if (next_cap < needed)
        return false;
    answers->items = answers->items
        ? cetta_realloc(answers->items, sizeof(AnswerRef) * (size_t)next_cap)
        : cetta_malloc(sizeof(AnswerRef) * (size_t)next_cap);
    answers->cap = next_cap;
    return true;
}

static bool table_stored_answers_push_move(TableStoredAnswers *answers,
                                           AnswerRef ref) {
    if (!answers || ref == CETTA_ANSWER_REF_NONE ||
        !table_stored_answers_reserve(answers, answers->len + 1)) {
        return false;
    }
    answers->items[answers->len++] = ref;
    return true;
}

static void table_store_entry_init(TableStoreEntry *entry) {
    arena_init(&entry->arena);
    entry->space = NULL;
    entry->space_instance_id = 0;
    entry->revision = 0;
    entry->goal_key = NULL;
    entry->goal_hash = 0;
    table_stored_answers_init(&entry->results);
}

static void table_store_entry_free(TableStoreEntry *entry) {
    table_stored_answers_free(&entry->results);
    arena_free(&entry->arena);
    entry->space = NULL;
    entry->space_instance_id = 0;
    entry->revision = 0;
    entry->goal_key = NULL;
    entry->goal_hash = 0;
}

static void table_query_state_free(TableQueryState *state, bool free_staged) {
    if (!state)
        return;
    if (free_staged)
        table_store_entry_free(&state->staged);
    cetta_var_map_free(&state->query_map);
    cetta_var_map_free(&state->goal_instantiation);
    free(state);
}

static TableStoreMatch table_store_find_match(TableStore *store,
                                              Space *space,
                                              uint64_t revision,
                                              Atom *goal_key,
                                              uint32_t goal_hash) {
    TableStoreMatch match = {0};
    if (!store)
        return match;
    uint64_t instance_id = space_instance_id(space);
    for (uint32_t i = 0; i < store->len; i++) {
        TableStoreEntry *entry = &store->entries[i];
        if (!entry->goal_key)
            continue;
        if (entry->space != space || entry->goal_hash != goal_hash)
            continue;
        if (!atom_eq(entry->goal_key, goal_key))
            continue;
        if (entry->space_instance_id != instance_id ||
            entry->revision != revision) {
            match.stale = true;
            if (!match.reusable) {
                match.reusable = entry;
                match.reusable_index = i;
            }
            continue;
        }
        match.exact = entry;
        match.exact_index = i;
        return match;
    }
    return match;
}

static bool table_store_reserve(TableStore *store, uint32_t needed) {
    if (needed <= store->cap)
        return true;
    uint32_t next_cap = store->cap ? store->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    store->entries = store->entries
        ? cetta_realloc(store->entries, sizeof(TableStoreEntry) * next_cap)
        : cetta_malloc(sizeof(TableStoreEntry) * next_cap);
    store->cap = next_cap;
    return true;
}

void table_store_init(TableStore *store, CettaTableMode mode,
                      AnswerBank *answer_bank) {
    if (!store)
        return;
    store->entries = NULL;
    store->len = 0;
    store->cap = 0;
    store->mode = mode;
    store->answer_bank = answer_bank;
    store->ground = NULL;
}

bool canonical_ground_call_key(Atom *call, Arena *arena,
                               Atom **out_canonical, uint64_t *out_hash) {
    if (!call || !arena || atom_has_vars(call))
        return false;
    Atom *canonical = term_universe_canonicalize_atom(arena, call);
    if (!canonical)
        return false;
    if (out_canonical)
        *out_canonical = canonical;
    if (out_hash)
        *out_hash = (uint64_t)atom_hash(canonical);
    return true;
}

/* ── Ground-call memo store (CETTA_TABLE_MODE_GROUND_CALL) ─────────────────
 *
 * A flat set of entries keyed by (hash, atom_eq on the canonical ground call).
 * Each entry owns a deep copy of the canonical call in a dedicated arena and a
 * whole answer bag as AnswerRefs into the shared AnswerBank.  Invalidation is
 * the process-global mutation epoch captured at commit: a lookup hits only if
 * no space has been mutated since (a sound over-approximation of "every space
 * this call consulted is unchanged").  Stale entries are refreshed in place on
 * re-commit, never duplicated. */
typedef struct {
    Atom *canonical;
    uint64_t hash;
    uint64_t epoch;
    TableStoredAnswers answers;
} GroundCallEntry;

struct GroundCallStore {
    Arena arena;
    GroundCallEntry *entries;
    uint32_t len;
    uint32_t cap;
    AnswerBank *answer_bank;
};

static GroundCallStore *ground_store_ensure(TableStore *store) {
    if (!store)
        return NULL;
    if (store->ground)
        return store->ground;
    GroundCallStore *ground = cetta_malloc(sizeof(*ground));
    arena_init(&ground->arena);
    arena_set_runtime_kind(&ground->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    ground->entries = NULL;
    ground->len = 0;
    ground->cap = 0;
    ground->answer_bank = store->answer_bank;
    store->ground = ground;
    return ground;
}

static void ground_store_free(GroundCallStore *ground) {
    if (!ground)
        return;
    for (uint32_t i = 0; i < ground->len; i++)
        table_stored_answers_free(&ground->entries[i].answers);
    free(ground->entries);
    arena_free(&ground->arena);
    free(ground);
}

static GroundCallEntry *ground_store_find(GroundCallStore *ground,
                                          Atom *canonical, uint64_t hash) {
    if (!ground || !canonical)
        return NULL;
    for (uint32_t i = 0; i < ground->len; i++) {
        if (ground->entries[i].hash == hash &&
            atom_eq(ground->entries[i].canonical, canonical))
            return &ground->entries[i];
    }
    return NULL;
}

bool table_store_ground_lookup(TableStore *store, Atom *canonical,
                               uint64_t hash,
                               TableGroundAnswerVisitor visit, void *ctx) {
    if (!store || store->mode != CETTA_TABLE_MODE_GROUND_CALL ||
        !store->ground || !canonical || !visit)
        return false;
    GroundCallEntry *entry = ground_store_find(store->ground, canonical, hash);
    if (!entry
#ifndef GROUND_MEMO_MUTATION_IGNORE_EPOCH
        || entry->epoch != space_global_mutation_epoch()
#endif
        )
        return false;
    AnswerBank *bank = store->ground->answer_bank;
    for (CettaCount i = 0; i < entry->answers.len; i++) {
        const AnswerRecord *rec = answer_bank_get(bank, entry->answers.items[i]);
        if (!rec || !rec->result)
            return false;
        if (!visit(rec->result, ctx))
            return false;
#ifdef GROUND_MEMO_MUTATION_BAG_TO_SET
        break; /* planted mutation: replay only the first occurrence */
#endif
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_ANSWER_STAGED);
    return true;
}

bool table_store_ground_commit(TableStore *store, Atom *canonical,
                               uint64_t hash,
                               Atom *const *answers, uint32_t answer_len) {
    if (!store || store->mode != CETTA_TABLE_MODE_GROUND_CALL || !canonical ||
        (answer_len > 0 && !answers))
        return false;
    GroundCallStore *ground = ground_store_ensure(store);
    if (!ground)
        return false;
    AnswerBank *bank = ground->answer_bank;
    if (!bank)
        return false;

    GroundCallEntry *entry = ground_store_find(ground, canonical, hash);
    if (entry) {
        /* Refresh in place: drop the stale bag, keep the canonical key. */
        table_stored_answers_free(&entry->answers);
        table_stored_answers_init(&entry->answers);
    } else {
        if (ground->len == ground->cap) {
            uint32_t next_cap = ground->cap ? ground->cap * 2u : 8u;
            GroundCallEntry *grown = cetta_realloc(
                ground->entries, sizeof(GroundCallEntry) * (size_t)next_cap);
            if (!grown)
                return false;
            ground->entries = grown;
            ground->cap = next_cap;
        }
        entry = &ground->entries[ground->len];
        entry->canonical = atom_deep_copy(&ground->arena, canonical);
        if (!entry->canonical)
            return false;
        entry->hash = hash;
        table_stored_answers_init(&entry->answers);
        ground->len++;
    }

    for (uint32_t i = 0; i < answer_len; i++) {
        AnswerRef ref = CETTA_ANSWER_REF_NONE;
        if (!answers[i] ||
            !answer_bank_add(bank, answers[i], NULL, NULL, &ref) ||
            !table_stored_answers_push_move(&entry->answers, ref)) {
            /* Leave the entry present but mark it stale so it can never hit:
             * an impossible epoch forces a miss until a clean re-commit. */
            entry->epoch = UINT64_MAX;
            return false;
        }
    }
    entry->epoch = space_global_mutation_epoch();
    return true;
}

void table_store_free(TableStore *store) {
    if (!store)
        return;
    for (uint32_t i = 0; i < store->len; i++)
        table_store_entry_free(&store->entries[i]);
    free(store->entries);
    store->entries = NULL;
    store->len = 0;
    store->cap = 0;
    store->answer_bank = NULL;
    ground_store_free(store->ground);
    store->ground = NULL;
}

bool table_store_begin_query(TableStore *store, Space *space, uint64_t revision,
                             Atom *query, TableQueryHandle *handle) {
    if (!store || store->mode != CETTA_TABLE_MODE_VARIANT || !space || !query ||
        !handle || handle->impl) {
        return false;
    }

    Arena probe_arena;
    arena_init(&probe_arena);
    arena_set_hashcons(&probe_arena, NULL);
    CettaVarMap probe_map;
    cetta_var_map_init(&probe_map);
    Atom *probe_key = variant_shape_canonicalize_atom(&probe_arena, query,
                                                      &probe_map, NULL,
                                                      &kTableStoreVariantOptions);
    if (!probe_key) {
        cetta_var_map_free(&probe_map);
        arena_free(&probe_arena);
        return false;
    }
    uint32_t probe_hash = atom_hash(probe_key);
    TableStoreMatch match = table_store_find_match(store, space, revision,
                                                   probe_key, probe_hash);
    cetta_var_map_free(&probe_map);
    arena_free(&probe_arena);

    if (!match.reusable && !match.exact &&
        !table_store_reserve(store, store->len + 1)) {
        return false;
    }

    TableQueryState *state = cetta_malloc(sizeof(TableQueryState));
    memset(state, 0, sizeof(*state));
    state->store = store;
    table_store_entry_init(&state->staged);
    cetta_var_map_init(&state->query_map);
    cetta_var_map_init(&state->goal_instantiation);
    if (match.exact) {
        state->target_index = match.exact_index;
        state->target_reusable = true;
        state->target_stale = false;
    } else if (match.reusable) {
        /* In the current variant-table implementation, stale entries are
           replacement targets only; lookup still treats them as misses until a
           fresh query commits a new answer set. */
        state->target_index = match.reusable_index;
        state->target_reusable = true;
        state->target_stale = true;
    } else {
        state->target_index = store->len;
        state->target_reusable = false;
        state->target_reserved_new = true;
        state->target_stale = false;
    }

    state->staged.space = space;
    state->staged.space_instance_id = space_instance_id(space);
    state->staged.revision = revision;
    CettaVarMap goal_instantiation;
    cetta_var_map_init(&goal_instantiation);
    state->staged.goal_key = variant_shape_canonicalize_atom(&state->staged.arena,
                                                             query,
                                                             &state->query_map,
                                                             &goal_instantiation,
                                                             &kTableStoreVariantOptions);
    if (!state->staged.goal_key) {
        cetta_var_map_free(&goal_instantiation);
        table_query_state_free(state, true);
        return false;
    }
    cetta_provenance_assert_not_transient(state->staged.goal_key,
                                         "table_store.staged.goal_key");
    if (!cetta_var_map_clone_live(&state->staged.arena,
                                  &state->goal_instantiation,
                                  &goal_instantiation)) {
        cetta_var_map_free(&goal_instantiation);
        table_query_state_free(state, true);
        return false;
    }
    cetta_var_map_free(&goal_instantiation);
    state->staged.goal_hash = atom_hash(state->staged.goal_key);
    handle->impl = state;
    return true;
}

bool table_store_query_goal_instantiation(TableQueryHandle *handle,
                                          Arena *dst,
                                          CettaVarMap *out) {
    if (!out)
        return false;
    cetta_var_map_init(out);
    if (!handle || !handle->impl)
        return false;
    TableQueryState *state = handle->impl;
    return cetta_var_map_clone_live(dst, out, &state->goal_instantiation);
}

static bool table_store_factor_answer_result(Arena *dst, Atom *canonical_result,
                                             const Bindings *canonical_bindings,
                                             Atom **factored_result,
                                             VariantInstance *variant_out) {
    VariantBank bank;
    VariantShape shape;
    bool factored = false;

    if (variant_out)
        variant_instance_init(variant_out);
    if (factored_result)
        *factored_result = canonical_result;
    if (!dst || !canonical_result || !canonical_bindings ||
        !factored_result || !variant_out) {
        return canonical_result != NULL;
    }

    variant_bank_init(&bank, kTableStoreVariantOptions);
    variant_shape_init(&shape);
    if (variant_shape_from_bound_atom(&bank, canonical_bindings,
                                      canonical_result, &shape) &&
        variant_instance_from_shape(variant_out, &shape) &&
        variant_instance_present(variant_out)) {
        *factored_result = atom_deep_copy(dst, shape.skeleton);
        factored = true;
    }
    variant_shape_free(&shape);
    variant_bank_free(&bank);

    if (!factored) {
        variant_instance_free(variant_out);
        *factored_result = canonical_result;
    }
    return *factored_result != NULL;
}

bool table_store_add_answer(TableQueryHandle *handle, Atom *result,
                            const Bindings *bindings, AnswerRef *out_ref) {
    if (!handle || !handle->impl || !result)
        return false;
    if (out_ref)
        *out_ref = CETTA_ANSWER_REF_NONE;

    TableQueryState *state = handle->impl;
    AnswerBank *bank = state->store ? state->store->answer_bank : NULL;
    Arena answer_scratch;
    arena_init(&answer_scratch);
    arena_set_runtime_kind(&answer_scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&answer_scratch, NULL);
    CettaVarMap answer_map;
    cetta_var_map_init(&answer_map);
    if (!cetta_var_map_clone(&answer_map, &state->query_map)) {
        arena_free(&answer_scratch);
        cetta_var_map_free(&answer_map);
        return false;
    }

    Bindings empty_bindings;
    const Bindings *source_bindings = bindings;
    if (!source_bindings) {
        bindings_init(&empty_bindings);
        source_bindings = &empty_bindings;
    }

    Atom *canonical_result = variant_shape_canonicalize_atom(&answer_scratch,
                                                             result,
                                                             &answer_map, NULL,
                                                             &kTableStoreVariantOptions);
    Bindings canonical_bindings;
    Atom *factored_result = NULL;
    VariantInstance factored_variant;
    variant_instance_init(&factored_variant);
    if (!canonical_result ||
        !variant_shape_canonicalize_bindings(&answer_scratch,
                                             source_bindings,
                                             &answer_map,
                                             &kTableStoreVariantOptions,
                                             &canonical_bindings)) {
        variant_instance_free(&factored_variant);
        arena_free(&answer_scratch);
        cetta_var_map_free(&answer_map);
        return false;
    }

    if (!table_store_factor_answer_result(&answer_scratch, canonical_result,
                                          &canonical_bindings, &factored_result,
                                          &factored_variant)) {
        variant_instance_free(&factored_variant);
        bindings_free(&canonical_bindings);
        arena_free(&answer_scratch);
        cetta_var_map_free(&answer_map);
        return false;
    }
    AnswerRef ref = CETTA_ANSWER_REF_NONE;
    if (!bank ||
        !answer_bank_add(bank, factored_result, &canonical_bindings,
                         &factored_variant, &ref) ||
        !table_stored_answers_push_move(&state->staged.results, ref)) {
        variant_instance_free(&factored_variant);
        bindings_free(&canonical_bindings);
        arena_free(&answer_scratch);
        cetta_var_map_free(&answer_map);
        return false;
    }
    variant_instance_free(&factored_variant);
    bindings_free(&canonical_bindings);
    arena_free(&answer_scratch);
    cetta_var_map_free(&answer_map);
    if (out_ref)
        *out_ref = ref;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_ANSWER_STAGED);
    return true;
}

bool table_store_commit_query(TableQueryHandle *handle) {
    if (!handle || !handle->impl)
        return false;

    TableQueryState *state = handle->impl;
    TableStoreEntry *target = NULL;
    if (state->target_reusable) {
        target = &state->store->entries[state->target_index];
        table_store_entry_free(target);
        if (state->target_stale)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_REUSE);
    } else {
        /* Table queries can re-enter the same store before an outer query
           commits. Pick the append slot at commit time so nested commits cannot
           reserve the same store->len slot and leave a counted hole behind. */
        if (!table_store_reserve(state->store, state->store->len + 1))
            return false;
        target = &state->store->entries[state->store->len];
        state->store->len++;
    }

    *target = state->staged;
    cetta_var_map_free(&state->query_map);
    cetta_var_map_free(&state->goal_instantiation);
    free(state);
    handle->impl = NULL;
    return true;
}

void table_store_abort_query(TableQueryHandle *handle) {
    if (!handle || !handle->impl)
        return;
    TableQueryState *state = handle->impl;
    handle->impl = NULL;
    table_query_state_free(state, true);
}

typedef struct {
    QueryResults *results;
} QueryResultsCollectCtx;

typedef struct {
    Arena *out_arena;
    QueryResultVisitor visitor;
    void *ctx;
} TableMaterializeVisitorCtx;

static bool query_results_collect_visit(Atom *result, const Bindings *bindings,
                                        void *ctx) {
    QueryResultsCollectCtx *collect = ctx;
    return query_results_push(collect->results, result, (Bindings *)bindings);
}

static bool table_store_materialize_visit(Atom *result,
                                          const Bindings *bindings,
                                          const VariantInstance *variant,
                                          void *ctx) {
    TableMaterializeVisitorCtx *materialize = ctx;
    Atom *materialized = result;
    if (variant_instance_present(variant)) {
        materialized = variant_instance_materialize(materialize->out_arena,
                                                    materialized,
                                                    variant);
        if (!materialized)
            return true;
    }
    return materialize->visitor(materialized, bindings, materialize->ctx);
}

static bool table_store_build_goal_bindings(Arena *out_arena,
                                            const CettaVarMap *goal_instantiation,
                                            Bindings *goal_bindings) {
    bindings_init(goal_bindings);
    if (!goal_instantiation)
        return true;
    for (uint32_t i = 0; i < goal_instantiation->len; i++) {
        Atom *goal_key_var =
            atom_var_with_id(out_arena, "$T", goal_instantiation->items[i].source_id);
        if (!bindings_add_id(goal_bindings, goal_key_var->var_id,
                             goal_key_var->sym_id,
                             goal_instantiation->items[i].mapped_var)) {
            bindings_free(goal_bindings);
            return false;
        }
    }
    return true;
}

bool table_store_materialize_answer_ref(const AnswerBank *answer_bank,
                                        AnswerRef ref,
                                        Arena *out_arena,
                                        const CettaVarMap *goal_instantiation,
                                        Atom **out_result,
                                        Bindings *out_bindings,
                                        VariantInstance *out_variant) {
    const AnswerRecord *stored = answer_bank_get(answer_bank, ref);
    CettaVarMap local_map;
    Bindings goal_bindings;
    size_t live_bytes_before = 0;
    bool ok = false;

    if (out_result)
        *out_result = NULL;
    if (out_bindings)
        bindings_init(out_bindings);
    if (out_variant)
        variant_instance_init(out_variant);
    if (!stored || !out_arena || !out_result || !out_bindings || !out_variant)
        return false;

    live_bytes_before = out_arena->live_bytes;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ANSWER_REF_MATERIALIZE_CALL);
    cetta_var_map_init(&local_map);
    if (!table_store_build_goal_bindings(out_arena, goal_instantiation,
                                         &goal_bindings)) {
        cetta_var_map_free(&local_map);
        return false;
    }

    Atom *result =
        variant_shape_materialize_atom(out_arena,
                                       stored->result,
                                       goal_instantiation,
                                       &local_map);
    if (!result)
        goto done;

    if (variant_instance_present(&stored->variant)) {
        if (goal_bindings.len > 0 || goal_bindings.eq_len > 0) {
            if (!variant_instance_sink_env(out_arena, out_variant,
                                           &stored->variant,
                                           &goal_bindings)) {
                goto done;
            }
        } else if (!variant_instance_clone(out_variant, &stored->variant)) {
            goto done;
        }
    }

    if (!variant_shape_materialize_bindings(out_arena,
                                            &stored->bindings,
                                            goal_instantiation,
                                            &local_map,
                                            out_bindings)) {
        goto done;
    }
    *out_result = result;
    if (out_arena->live_bytes > live_bytes_before) {
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ANSWER_REF_MATERIALIZE_BYTES,
                                (uint64_t)(out_arena->live_bytes - live_bytes_before));
    }
    ok = true;

done:
    if (!ok) {
        bindings_free(out_bindings);
        bindings_init(out_bindings);
        variant_instance_free(out_variant);
        variant_instance_init(out_variant);
        if (out_result)
            *out_result = NULL;
    }
    bindings_free(&goal_bindings);
    cetta_var_map_free(&local_map);
    return ok;
}

static CettaCount table_store_entry_visit_delayed(
    const TableStoreEntry *entry, const AnswerBank *answer_bank,
    Arena *out_arena, const CettaVarMap *goal_instantiation,
    TableDelayedResultVisitor visitor, void *ctx) {
    CettaCount visited = 0;
    if (!entry || !visitor)
        return 0;
    for (CettaIndex i = 0; i < entry->results.len; i++) {
        Atom *result = NULL;
        Bindings materialized;
        VariantInstance replay_variant;
        if (!table_store_materialize_answer_ref(answer_bank,
                                                entry->results.items[i],
                                                out_arena,
                                                goal_instantiation,
                                                &result,
                                                &materialized,
                                                &replay_variant)) {
            continue;
        }
        visited++;
        bool keep_going = visitor(result, &materialized, &replay_variant, ctx);
        variant_instance_free(&replay_variant);
        bindings_free(&materialized);
        if (!keep_going)
            break;
    }
    return visited;
}

bool table_store_lookup_visit_delayed(TableStore *store, Space *space,
                                      uint64_t revision,
                                      Atom *query, Arena *out_arena,
                                      TableDelayedResultVisitor visitor,
                                      void *ctx,
                                      CettaCount *visited_out) {
    if (visited_out)
        *visited_out = 0;
    if (!store || store->mode != CETTA_TABLE_MODE_VARIANT || !space || !query ||
        !out_arena || !visitor) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_MISS);
        return false;
    }

    Arena probe_arena;
    arena_init(&probe_arena);
    arena_set_hashcons(&probe_arena, NULL);
    CettaVarMap probe_map;
    CettaVarMap goal_instantiation;
    cetta_var_map_init(&probe_map);
    cetta_var_map_init(&goal_instantiation);
    Atom *goal_key = variant_shape_canonicalize_atom(&probe_arena, query,
                                                     &probe_map,
                                                     &goal_instantiation,
                                                     &kTableStoreVariantOptions);
    uint32_t goal_hash = atom_hash(goal_key);
    TableStoreMatch match = table_store_find_match(store, space, revision,
                                                   goal_key, goal_hash);
    if (!match.exact) {
        /* In the current variant-table implementation, a stale variant match
           is still a lookup miss. */
        cetta_var_map_free(&probe_map);
        cetta_var_map_free(&goal_instantiation);
        arena_free(&probe_arena);
        cetta_runtime_stats_inc(match.stale ? CETTA_RUNTIME_COUNTER_TABLE_STALE_MISS
                                      : CETTA_RUNTIME_COUNTER_TABLE_MISS);
        return false;
    }
    TableStoreEntry *entry = match.exact;
    CettaCount visited = table_store_entry_visit_delayed(
        entry, store->answer_bank, out_arena, &goal_instantiation, visitor,
        ctx);

    cetta_var_map_free(&probe_map);
    cetta_var_map_free(&goal_instantiation);
    arena_free(&probe_arena);
    if (visited_out)
        *visited_out = visited;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_HIT);
    return true;
}

bool table_store_lookup_visit_ref(TableStore *store, Space *space,
                                  uint64_t revision,
                                  Atom *query,
                                  Arena *goal_owner,
                                  TableAnswerRefVisitor visitor,
                                  void *ctx,
                                  CettaCount *visited_out) {
    if (visited_out)
        *visited_out = 0;
    if (!store || store->mode != CETTA_TABLE_MODE_VARIANT || !space || !query ||
        !goal_owner || !visitor) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_MISS);
        return false;
    }

    Arena probe_arena;
    arena_init(&probe_arena);
    arena_set_hashcons(&probe_arena, NULL);
    CettaVarMap probe_map;
    CettaVarMap goal_instantiation;
    cetta_var_map_init(&probe_map);
    cetta_var_map_init(&goal_instantiation);
    Atom *goal_key = variant_shape_canonicalize_atom(&probe_arena, query,
                                                     &probe_map,
                                                     &goal_instantiation,
                                                     &kTableStoreVariantOptions);
    uint32_t goal_hash = atom_hash(goal_key);
    TableStoreMatch match = table_store_find_match(store, space, revision,
                                                   goal_key, goal_hash);
    if (!match.exact) {
        cetta_var_map_free(&probe_map);
        cetta_var_map_free(&goal_instantiation);
        arena_free(&probe_arena);
        cetta_runtime_stats_inc(match.stale ? CETTA_RUNTIME_COUNTER_TABLE_STALE_MISS
                                      : CETTA_RUNTIME_COUNTER_TABLE_MISS);
        return false;
    }
    CettaVarMap goal_snapshot;
    if (!cetta_var_map_clone_live(goal_owner, &goal_snapshot, &goal_instantiation)) {
        cetta_var_map_free(&probe_map);
        cetta_var_map_free(&goal_instantiation);
        arena_free(&probe_arena);
        return false;
    }
    CettaCount visited = 0;
    TableStoreEntry *entry = match.exact;
    for (CettaIndex i = 0; i < entry->results.len; i++) {
        visited++;
        if (!visitor(store->answer_bank, entry->results.items[i], &goal_snapshot, ctx))
            break;
    }

    cetta_var_map_free(&goal_snapshot);
    cetta_var_map_free(&probe_map);
    cetta_var_map_free(&goal_instantiation);
    arena_free(&probe_arena);
    if (visited_out)
        *visited_out = visited;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_HIT);
    return true;
}

bool table_store_lookup_visit(TableStore *store, Space *space, uint64_t revision,
                              Atom *query, Arena *out_arena,
                              QueryResultVisitor visitor, void *ctx,
                              CettaCount *visited_out) {
    TableMaterializeVisitorCtx materialize = {
        .out_arena = out_arena,
        .visitor = visitor,
        .ctx = ctx,
    };
    return table_store_lookup_visit_delayed(store, space, revision, query,
                                            out_arena,
                                            table_store_materialize_visit,
                                            &materialize,
                                            visited_out);
}

bool table_store_lookup(TableStore *store, Space *space, uint64_t revision,
                        Atom *query, Arena *out_arena,
                        QueryResults *out) {
    QueryResultsCollectCtx collect = {
        .results = out,
    };
    return table_store_lookup_visit(store, space, revision, query, out_arena,
                                    query_results_collect_visit, &collect,
                                    NULL);
}

bool table_store_put(TableStore *store, Space *space, uint64_t revision,
                     Atom *query, const QueryResults *results) {
    if (!store || store->mode != CETTA_TABLE_MODE_VARIANT || !space || !query ||
        !results) {
        return false;
    }

    TableQueryHandle handle = {0};
    if (!table_store_begin_query(store, space, revision, query, &handle))
        return false;

    for (CettaIndex i = 0; i < results->len; i++) {
        if (!table_store_add_answer(&handle, results->items[i].result,
                                    &results->items[i].bindings, NULL)) {
            table_store_abort_query(&handle);
            return false;
        }
    }

    if (!table_store_commit_query(&handle)) {
        table_store_abort_query(&handle);
        return false;
    }
    return true;
}
