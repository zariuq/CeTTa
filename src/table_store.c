#include "table_store.h"
#include "stats.h"
#include "variant_shape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TableStoreEntry {
    Arena arena;
    Space *space;
    uint64_t revision;
    Atom *goal_key;
    uint32_t goal_hash;
    QueryResults results;
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
    uint32_t target_index;
    bool target_reusable;
    bool target_stale;
} TableQueryState;

static const CettaVariantShapeOptions kTableStoreVariantOptions = {
    .slot_policy = CETTA_VARIANT_SLOT_ORDINAL_NAME,
    .slot_name = "$T",
    .share_immutable = false,
};

static void table_store_entry_init(TableStoreEntry *entry) {
    arena_init(&entry->arena);
    entry->space = NULL;
    entry->revision = 0;
    entry->goal_key = NULL;
    entry->goal_hash = 0;
    query_results_init(&entry->results);
}

static void table_store_entry_free(TableStoreEntry *entry) {
    query_results_free(&entry->results);
    arena_free(&entry->arena);
    entry->space = NULL;
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
    for (uint32_t i = 0; i < store->len; i++) {
        TableStoreEntry *entry = &store->entries[i];
        if (entry->space != space || entry->goal_hash != goal_hash)
            continue;
        if (!atom_eq(entry->goal_key, goal_key))
            continue;
        if (entry->revision != revision) {
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

void table_store_init(TableStore *store, CettaTableMode mode) {
    if (!store)
        return;
    store->entries = NULL;
    store->len = 0;
    store->cap = 0;
    store->mode = mode;
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
    state->store = store;
    table_store_entry_init(&state->staged);
    cetta_var_map_init(&state->query_map);
    if (match.exact) {
        state->target_index = match.exact_index;
        state->target_reusable = true;
        state->target_stale = false;
    } else if (match.reusable) {
        state->target_index = match.reusable_index;
        state->target_reusable = true;
        state->target_stale = true;
    } else {
        state->target_index = store->len;
        state->target_reusable = false;
        state->target_stale = false;
    }

    state->staged.space = space;
    state->staged.revision = revision;
    state->staged.goal_key = variant_shape_canonicalize_atom(&state->staged.arena,
                                                             query,
                                                             &state->query_map,
                                                             NULL,
                                                             &kTableStoreVariantOptions);
    if (!state->staged.goal_key) {
        table_query_state_free(state, true);
        return false;
    }
    state->staged.goal_hash = atom_hash(state->staged.goal_key);
    handle->impl = state;
    return true;
}

bool table_store_add_answer(TableQueryHandle *handle, Atom *result,
                            const Bindings *bindings) {
    if (!handle || !handle->impl || !result)
        return false;

    TableQueryState *state = handle->impl;
    CettaVarMap answer_map;
    cetta_var_map_init(&answer_map);
    if (!cetta_var_map_clone(&answer_map, &state->query_map)) {
        cetta_var_map_free(&answer_map);
        return false;
    }

    Bindings empty_bindings;
    const Bindings *source_bindings = bindings;
    if (!source_bindings) {
        bindings_init(&empty_bindings);
        source_bindings = &empty_bindings;
    }

    Atom *canonical_result = variant_shape_canonicalize_atom(&state->staged.arena,
                                                             result,
                                                             &answer_map, NULL,
                                                             &kTableStoreVariantOptions);
    Bindings canonical_bindings;
    if (!canonical_result ||
        !variant_shape_canonicalize_bindings(&state->staged.arena,
                                             source_bindings,
                                             &answer_map,
                                             &kTableStoreVariantOptions,
                                             &canonical_bindings)) {
        cetta_var_map_free(&answer_map);
        return false;
    }

    query_results_push_move(&state->staged.results, canonical_result,
                            &canonical_bindings);
    cetta_var_map_free(&answer_map);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_ANSWER_STAGED);
    return true;
}

bool table_store_commit_query(TableQueryHandle *handle) {
    if (!handle || !handle->impl)
        return false;

    TableQueryState *state = handle->impl;
    TableStoreEntry *target = &state->store->entries[state->target_index];
    if (state->target_reusable) {
        table_store_entry_free(target);
        if (state->target_stale)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_REUSE);
    } else {
        state->store->len++;
    }

    *target = state->staged;
    cetta_var_map_free(&state->query_map);
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

static bool query_results_collect_visit(Atom *result, const Bindings *bindings,
                                        void *ctx) {
    QueryResultsCollectCtx *collect = ctx;
    query_results_push(collect->results, result, (Bindings *)bindings);
    return true;
}

static uint32_t table_store_entry_visit(const TableStoreEntry *entry,
                                        Arena *out_arena,
                                        const CettaVarMap *goal_instantiation,
                                        QueryResultVisitor visitor,
                                        void *ctx) {
    uint32_t visited = 0;
    if (!entry || !visitor)
        return 0;
    for (uint32_t i = 0; i < entry->results.len; i++) {
        CettaVarMap local_map;
        cetta_var_map_init(&local_map);
        Atom *result = variant_shape_materialize_atom(out_arena,
                                                      entry->results.items[i].result,
                                                      goal_instantiation,
                                                      &local_map);
        Bindings materialized;
        if (!variant_shape_materialize_bindings(out_arena,
                                                &entry->results.items[i].bindings,
                                                goal_instantiation,
                                                &local_map,
                                                &materialized)) {
            cetta_var_map_free(&local_map);
            continue;
        }
        visited++;
        bool keep_going = visitor(result, &materialized, ctx);
        bindings_free(&materialized);
        cetta_var_map_free(&local_map);
        if (!keep_going)
            break;
    }
    return visited;
}

bool table_store_lookup_visit(TableStore *store, Space *space, uint64_t revision,
                              Atom *query, Arena *out_arena,
                              QueryResultVisitor visitor, void *ctx,
                              uint32_t *visited_out) {
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
        cetta_var_map_free(&probe_map);
        cetta_var_map_free(&goal_instantiation);
        arena_free(&probe_arena);
        cetta_runtime_stats_inc(match.stale ? CETTA_RUNTIME_COUNTER_TABLE_STALE_MISS
                                      : CETTA_RUNTIME_COUNTER_TABLE_MISS);
        return false;
    }
    TableStoreEntry *entry = match.exact;
    uint32_t visited = table_store_entry_visit(entry, out_arena,
                                               &goal_instantiation,
                                               visitor, ctx);

    cetta_var_map_free(&probe_map);
    cetta_var_map_free(&goal_instantiation);
    arena_free(&probe_arena);
    if (visited_out)
        *visited_out = visited;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TABLE_HIT);
    return true;
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

    for (uint32_t i = 0; i < results->len; i++) {
        if (!table_store_add_answer(&handle, results->items[i].result,
                                    &results->items[i].bindings)) {
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
