#include "foreign_region.h"

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_BITS 5u
#define SLOT_WIDTH (1u << SLOT_BITS)
#define SLOT_LEVELS ((sizeof(size_t) * CHAR_BIT + SLOT_BITS - 1u) / SLOT_BITS)

typedef struct {
    fid_t frame;
    uint32_t mark;
    size_t variables;
    bool rewindable;
} ForeignCheckpoint;

struct CettaForeignRegion {
    CettaForeignRegion *outer;
    CettaForeignRegion *engine_root;
    CettaForeignRegion *inner;
    CettaForeignRegion *next_engine_owner;
    PL_engine_t engine;
    qid_t caller_query;
    pthread_t thread;
    fid_t owner;
    term_t root;
    functor_t slots;
    uint64_t *keys;
    size_t len;
    size_t cap;
    size_t *index;
    size_t index_cap;
    ForeignCheckpoint *checkpoints;
    size_t checkpoint_len;
    size_t checkpoint_cap;
    fid_t operation;
    size_t operation_variables;
    qid_t query;
    int last_status;
    record_t body_fault;
    CettaForeignRegionStats stats;
};

struct CettaForeignSnapshot {
    record_t graph;
    uint64_t *keys;
    size_t len;
};

/* Resource ownership metadata, not a solver store. A detached engine may
 * later be claimed by another native thread. That thread must not open a
 * second region below, and eventually discard, the first owner's frames. */
static pthread_mutex_t engine_owners_lock = PTHREAD_MUTEX_INITIALIZER;
static CettaForeignRegion *engine_owners;

static bool available(const CettaForeignRegion *region) {
    return region && region->engine == PL_current_engine() &&
           pthread_equal(region->thread, pthread_self()) &&
           region->engine_root->inner == region;
}

static size_t key_hash(uint64_t key) {
    key ^= key >> 30;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27;
    key *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(key ^ (key >> 31));
}

static void index_insert(CettaForeignRegion *region, size_t slot) {
    size_t at = key_hash(region->keys[slot]) & (region->index_cap - 1u);
    while (region->index[at])
        at = (at + 1u) & (region->index_cap - 1u);
    region->index[at] = slot + 1u;
}

static void index_rebuild(CettaForeignRegion *region) {
    if (region->index_cap) {
        memset(region->index, 0, region->index_cap * sizeof(*region->index));
        for (size_t slot = 0; slot < region->len; slot++)
            index_insert(region, slot);
    }
}

static bool reserve_key(CettaForeignRegion *region) {
    if (region->len == SIZE_MAX)
        return false;
    if (region->len == region->cap) {
        size_t cap = region->cap ? region->cap * 2u : 32u;
        if (cap <= region->cap || cap > SIZE_MAX / sizeof(*region->keys))
            return false;
        uint64_t *keys = realloc(region->keys, cap * sizeof(*keys));
        if (!keys)
            return false;
        region->keys = keys;
        region->cap = cap;
    }
    if (region->len + 1u > region->index_cap / 2u) {
        size_t cap = region->index_cap ? region->index_cap * 2u : 64u;
        if (cap <= region->index_cap || cap > SIZE_MAX / sizeof(*region->index))
            return false;
        size_t *index = calloc(cap, sizeof(*index));
        if (!index)
            return false;
        free(region->index);
        region->index = index;
        region->index_cap = cap;
        index_rebuild(region);
    }
    return true;
}

/* Only root is a persistent term reference. Intermediate references belong
 * to the operation. New nodes are unified into an older rooted variable,
 * so checkpoint rollback restores the path; closing a frame cannot leave a
 * persistent reference to that frame's deleted term-reference slots. */
static term_t slot_term(CettaForeignRegion *region, size_t slot) {
    term_t at = PL_new_term_refs(2);
    if (!at || !PL_put_term(at, region->root))
        return 0;
    for (size_t level = SLOT_LEVELS; level > 0; level--) {
        size_t shift = (level - 1u) * SLOT_BITS;
        int part = (int)((slot >> shift) & (SLOT_WIDTH - 1u)) + 1;
        region->stats.slot_steps++;
        if (PL_is_variable(at)) {
            if (!PL_put_functor(at + 1, region->slots) || !PL_unify(at, at + 1))
                return 0;
        }
        if (!PL_get_arg(part, at, at + 1) || !PL_put_term(at, at + 1))
            return 0;
    }
    return at;
}

CettaForeignRegion *cetta_foreign_region_open(void) {
    PL_engine_t engine = PL_current_engine();
    if (!engine)
        return NULL;
    pthread_mutex_lock(&engine_owners_lock);
    CettaForeignRegion *root = engine_owners;
    while (root && root->engine != engine)
        root = root->next_engine_owner;
    CettaForeignRegion *outer = root ? root->inner : NULL;
    pthread_mutex_unlock(&engine_owners_lock);
    /* SWI admits one attached thread per engine. The registry lookup above
     * therefore needs no long-lived lock around that engine's private stack. */
    if (outer && !available(outer))
        return NULL;
    CettaForeignRegion *region = calloc(1, sizeof(*region));
    if (!region)
        return NULL;
    region->owner = PL_open_foreign_frame();
    region->root = region->owner ? PL_new_term_ref() : 0;
    atom_t name = PL_new_atom("$cetta_foreign_slots");
    region->slots = name ? PL_new_functor(name, SLOT_WIDTH) : 0;
    if (name)
        PL_unregister_atom(name);
    if (!region->owner || !region->root || !region->slots) {
        if (region->owner)
            PL_discard_foreign_frame(region->owner);
        free(region);
        return NULL;
    }
    region->outer = outer;
    region->engine_root = root ? root : region;
    region->engine = engine;
    region->caller_query = PL_current_query();
    region->thread = pthread_self();
    if (!region->outer) {
        pthread_mutex_lock(&engine_owners_lock);
        region->next_engine_owner = engine_owners;
        engine_owners = region;
        pthread_mutex_unlock(&engine_owners_lock);
    }
    region->engine_root->inner = region;
    return region;
}

CettaForeignStatus cetta_foreign_region_adopt(CettaForeignRegion *region) {
    if (!region || region->engine != PL_current_engine() ||
        region->engine_root->inner != region)
        return CETTA_FOREIGN_INVALID;
    for (CettaForeignRegion *at = region; at; at = at->outer) {
        if (at->operation)
            return CETTA_FOREIGN_INVALID;
        if (PL_current_query() != at->caller_query)
            return CETTA_FOREIGN_NOT_INNER;
    }
    for (CettaForeignRegion *at = region; at; at = at->outer)
        at->thread = pthread_self();
    return CETTA_FOREIGN_OK;
}

CettaForeignStatus cetta_foreign_region_free(CettaForeignRegion *region) {
    if (!available(region) || region->operation)
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    PL_discard_foreign_frame(region->owner);
    region->engine_root->inner = region->outer;
    if (!region->outer) {
        pthread_mutex_lock(&engine_owners_lock);
        CettaForeignRegion **link = &engine_owners;
        while (*link && *link != region)
            link = &(*link)->next_engine_owner;
        if (*link)
            *link = region->next_engine_owner;
        pthread_mutex_unlock(&engine_owners_lock);
    }
    free(region->keys);
    free(region->index);
    free(region->checkpoints);
    free(region);
    return CETTA_FOREIGN_OK;
}

CettaForeignStatus cetta_foreign_region_checkpoint(
    CettaForeignRegion *region, uint32_t mark) {
    if (!available(region) || region->operation ||
        (region->checkpoint_len &&
         region->checkpoints[region->checkpoint_len - 1u].rewindable &&
         region->checkpoints[region->checkpoint_len - 1u].mark > mark))
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    if (region->checkpoint_len == region->checkpoint_cap) {
        size_t cap = region->checkpoint_cap ? region->checkpoint_cap * 2u : 8u;
        if (cap <= region->checkpoint_cap ||
            cap > SIZE_MAX / sizeof(*region->checkpoints))
            return CETTA_FOREIGN_CAPACITY;
        ForeignCheckpoint *checkpoints = realloc(
            region->checkpoints, cap * sizeof(*checkpoints));
        if (!checkpoints)
            return CETTA_FOREIGN_CAPACITY;
        region->checkpoints = checkpoints;
        region->checkpoint_cap = cap;
    }
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return CETTA_FOREIGN_CAPACITY;
    region->checkpoints[region->checkpoint_len++] =
        (ForeignCheckpoint){frame, mark, region->len, true};
    return CETTA_FOREIGN_OK;
}

/* Prefix anchors may be closed only after all younger rewindable frames are
 * gone. Closing an older foreign frame early would destroy the younger frame
 * references even though its logical history can no longer be rolled back. */
static void close_anchors(CettaForeignRegion *region) {
    while (region->checkpoint_len &&
           !region->checkpoints[region->checkpoint_len - 1u].rewindable)
        PL_close_foreign_frame(region->checkpoints[--region->checkpoint_len].frame);
}

CettaForeignStatus cetta_foreign_region_rollback(
    CettaForeignRegion *region, uint32_t mark) {
    if (!available(region) || region->operation)
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    while (region->checkpoint_len &&
           region->checkpoints[region->checkpoint_len - 1u].rewindable &&
           region->checkpoints[region->checkpoint_len - 1u].mark >= mark) {
        ForeignCheckpoint checkpoint =
            region->checkpoints[--region->checkpoint_len];
        PL_discard_foreign_frame(checkpoint.frame);
        region->len = checkpoint.variables;
    }
    close_anchors(region);
    index_rebuild(region);
    return CETTA_FOREIGN_OK;
}

CettaForeignStatus cetta_foreign_region_commit(CettaForeignRegion *region) {
    if (!available(region) || region->operation)
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    while (region->checkpoint_len)
        PL_close_foreign_frame(region->checkpoints[--region->checkpoint_len].frame);
    return CETTA_FOREIGN_OK;
}

CettaForeignStatus cetta_foreign_region_rebase(
    CettaForeignRegion *region, const uint32_t *kept, size_t count) {
    if (!available(region) || region->operation || (count && !kept) ||
        count > UINT32_MAX)
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    for (size_t index = 1; index < count; index++)
        if (kept[index] <= kept[index - 1u])
            return CETTA_FOREIGN_INVALID;
    size_t position = 0;
    for (size_t index = 0; index < region->checkpoint_len; index++) {
        ForeignCheckpoint *checkpoint = &region->checkpoints[index];
        if (!checkpoint->rewindable)
            continue;
        if (!count || checkpoint->mark < kept[0]) {
            checkpoint->rewindable = false;
            continue;
        }
        while (position + 1u < count && kept[position + 1u] <= checkpoint->mark)
            position++;
        checkpoint->mark = (uint32_t)position;
    }
    close_anchors(region);
    return CETTA_FOREIGN_OK;
}

CettaForeignStatus cetta_foreign_region_frontier(
    CettaForeignRegion *region, const uint32_t *kept, size_t count) {
    if (!available(region) || region->operation || (count && !kept))
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    for (size_t at = 1u; at < count; at++)
        if (kept[at] <= kept[at - 1u])
            return CETTA_FOREIGN_INVALID;
    if (!count)
        return cetta_foreign_region_commit(region);
    size_t position = 0u;
    for (size_t at = 0u; at < region->checkpoint_len; at++) {
        ForeignCheckpoint *checkpoint = &region->checkpoints[at];
        if (!checkpoint->rewindable)
            continue;
        if (checkpoint->mark < kept[0]) {
            checkpoint->rewindable = false;
            continue;
        }
        while (position + 1u < count && kept[position + 1u] <= checkpoint->mark)
            position++;
        checkpoint->mark = kept[position];
    }
    close_anchors(region);
    return CETTA_FOREIGN_OK;
}

CettaForeignStatus cetta_foreign_region_begin(CettaForeignRegion *region) {
    if (!available(region) || region->operation)
        return CETTA_FOREIGN_INVALID;
    if (PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    region->operation = PL_open_foreign_frame();
    if (!region->operation)
        return CETTA_FOREIGN_CAPACITY;
    region->operation_variables = region->len;
    region->last_status = -2;
    region->stats.operations++;
    return CETTA_FOREIGN_OK;
}

term_t cetta_foreign_region_variable(CettaForeignRegion *region, uint64_t key) {
    if (!available(region) || !region->operation || region->query ||
        PL_current_query() != region->caller_query)
        return 0;
    if (region->index_cap) {
        size_t at = key_hash(key) & (region->index_cap - 1u);
        while (region->index[at]) {
            size_t slot = region->index[at] - 1u;
            if (region->keys[slot] == key)
                return slot_term(region, slot);
            at = (at + 1u) & (region->index_cap - 1u);
        }
    }
    if (!reserve_key(region))
        return 0;
    term_t term = slot_term(region, region->len);
    if (!term)
        return 0;
    region->keys[region->len] = key;
    index_insert(region, region->len++);
    return term;
}

CettaForeignStatus cetta_foreign_region_start(
    CettaForeignRegion *region, module_t module,
    predicate_t predicate, term_t arguments) {
    if (!available(region) || !region->operation || region->query ||
        !predicate || !arguments || PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_INVALID;
    region->query = PL_open_query(module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION | PL_Q_EXT_STATUS,
        predicate, arguments);
    return region->query ? CETTA_FOREIGN_OK : CETTA_FOREIGN_CAPACITY;
}

int cetta_foreign_region_next(CettaForeignRegion *region) {
    if (!available(region) || !region->query ||
        PL_current_query() != region->query ||
        region->last_status == PL_S_FALSE ||
        region->last_status == PL_S_EXCEPTION)
        return -1;
    int status = PL_next_solution(region->query);
    region->last_status = status;
    if (status == PL_S_TRUE || status == PL_S_LAST)
        region->stats.answers++;
    if (status == PL_S_EXCEPTION) {
        term_t thrown = PL_exception(region->query);
        region->body_fault = thrown ? PL_record(thrown) : 0;
    }
    return status;
}

CettaForeignStatus cetta_foreign_region_finish(
    CettaForeignRegion *region, bool commit, record_t *exception) {
    if (exception)
        *exception = 0;
    if (!available(region) || !region->operation ||
        (commit && (!region->query ||
                    (region->last_status != PL_S_TRUE &&
                     region->last_status != PL_S_LAST))))
        return CETTA_FOREIGN_INVALID;
    if (!region->query && PL_current_query() != region->caller_query)
        return CETTA_FOREIGN_NOT_INNER;
    CettaForeignStatus status = region->body_fault ? CETTA_FOREIGN_FAULT :
        region->last_status == PL_S_EXCEPTION ? CETTA_FOREIGN_CAPACITY : CETTA_FOREIGN_OK;
    if (region->query) {
        int released = commit ? PL_cut_query(region->query)
                              : PL_close_query(region->query);
        if (released == PL_S_NOT_INNER)
            return CETTA_FOREIGN_NOT_INNER;
        region->query = 0;
        if (!released) {
            term_t thrown = PL_exception(0);
            if (!region->body_fault && thrown)
                region->body_fault = PL_record(thrown);
            status = region->body_fault ? CETTA_FOREIGN_FAULT : CETTA_FOREIGN_CAPACITY;
            PL_clear_exception();
        }
    }
    if (commit && status == CETTA_FOREIGN_OK) {
        PL_close_foreign_frame(region->operation);
    } else {
        PL_discard_foreign_frame(region->operation);
        region->len = region->operation_variables;
        index_rebuild(region);
    }
    region->operation = 0;
    if (exception)
        *exception = region->body_fault;
    else if (region->body_fault)
        PL_erase(region->body_fault);
    region->body_fault = 0;
    return status;
}

CettaForeignSnapshot *cetta_foreign_region_snapshot(CettaForeignRegion *region) {
    if (!available(region) || (region->operation &&
        (!region->query || (region->last_status != PL_S_TRUE &&
                           region->last_status != PL_S_LAST))))
        return NULL;
    if (PL_current_query() != (region->query ? region->query : region->caller_query))
        return NULL;
    CettaForeignSnapshot *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot)
        return NULL;
    if (region->len) {
        snapshot->keys = malloc(region->len * sizeof(*snapshot->keys));
        if (!snapshot->keys) {
            free(snapshot);
            return NULL;
        }
        memcpy(snapshot->keys, region->keys, region->len * sizeof(*snapshot->keys));
    }
    snapshot->graph = PL_record(region->root);
    if (!snapshot->graph) {
        free(snapshot->keys);
        free(snapshot);
        return NULL;
    }
    snapshot->len = region->len;
    region->stats.snapshots++;
    region->stats.snapshot_slots += region->len;
    return snapshot;
}

CettaForeignRegion *cetta_foreign_region_restore(
    const CettaForeignSnapshot *snapshot) {
    if (!snapshot)
        return NULL;
    CettaForeignRegion *region = cetta_foreign_region_open();
    if (!region)
        return NULL;
    if (!PL_recorded(snapshot->graph, region->root)) {
        (void)cetta_foreign_region_free(region);
        return NULL;
    }
    for (size_t index = 0; index < snapshot->len; index++) {
        if (!reserve_key(region)) {
            (void)cetta_foreign_region_free(region);
            return NULL;
        }
        region->keys[region->len] = snapshot->keys[index];
        index_insert(region, region->len++);
    }
    return region;
}

void cetta_foreign_snapshot_free(CettaForeignSnapshot *snapshot) {
    if (!snapshot)
        return;
    PL_erase(snapshot->graph);
    free(snapshot->keys);
    free(snapshot);
}

size_t cetta_foreign_region_variable_count(const CettaForeignRegion *region) {
    return region ? region->len : 0;
}

CettaForeignRegionStats cetta_foreign_region_stats(const CettaForeignRegion *region) {
    return region ? region->stats : (CettaForeignRegionStats){0};
}
