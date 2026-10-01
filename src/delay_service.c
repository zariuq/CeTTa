#include "delay_service.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    DELAY_UNDO_SUSPEND = 1,
    DELAY_UNDO_WAKE,
    DELAY_UNDO_TAKE,
    DELAY_UNDO_WITHDRAW,
} DelayUndoKind;

typedef struct {
    DelayUndoKind kind;
    uint32_t trail_mark;
    uint32_t goal;
} DelayUndo;

/* One variable a goal waits on.  A bucket chains the records of the goals
 * still suspended, newest first: a woken or withdrawn goal's records leave
 * their chains, and return to their place in that order when the change is
 * undone, so a chain never holds a goal that no longer waits. */
typedef struct {
    VarId var;
    uint32_t goal;
    uint32_t next;
} DelayWatch;

typedef struct {
    CettaDelayGoal goal;
    ArenaMark storage;
    uint32_t first_watch;
    /* Its watch records: one per variable while it waits, none when it was
     * queued at once. */
    uint32_t watch_count;
} DelayGoalRecord;

struct CettaDelayService {
    CettaDelayOwnerOps owner_ops;
    void *owner;
    Arena arena;
    DelayGoalRecord *goals;
    uint32_t goal_len;
    uint32_t goal_cap;
    DelayWatch *watches;
    uint32_t watch_len;
    uint32_t watch_cap;
    /* Records in the chains: those of the suspended goals. */
    uint32_t linked;
    uint32_t *buckets;
    uint32_t bucket_cap;
    uint32_t suspended;
    uint32_t *queue;
    uint32_t queue_head;
    uint32_t queue_len;
    uint32_t queue_cap;
    DelayUndo *log;
    uint32_t log_len;
    uint32_t log_cap;
};

static uint32_t delay_bucket(VarId var, uint32_t cap) {
    uint64_t h = var * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(h >> 32) & (cap - 1u);
}

static bool delay_grow(void **items, uint32_t *cap, uint32_t need,
                       size_t size) {
    if (need <= *cap)
        return true;
    uint32_t next = *cap ? *cap : 8u;
    while (next < need) {
        if (next > UINT32_MAX / 2u)
            return false;
        next *= 2u;
    }
    void *grown = cetta_realloc(*items, size * (size_t)next);
    if (!grown)
        return false;
    *items = grown;
    *cap = next;
    return true;
}

static bool delay_log(CettaDelayService *service, DelayUndoKind kind,
                      uint32_t trail_mark, uint32_t goal) {
    if (!delay_grow((void **)&service->log, &service->log_cap,
                    service->log_len + 1u, sizeof(*service->log)))
        return false;
    service->log[service->log_len++] =
        (DelayUndo){kind, trail_mark, goal};
    return true;
}

/* Chain the records of every suspended goal into buckets of capacity `cap`,
 * oldest first, so each chain is newest first again. */
static bool delay_rehash(CettaDelayService *service, uint32_t cap) {
    uint32_t *buckets = cetta_malloc(sizeof(*buckets) * (size_t)cap);
    if (!buckets)
        return false;
    memset(buckets, 0, sizeof(*buckets) * (size_t)cap);
    for (uint32_t index = 0u; index < service->watch_len; index++) {
        DelayWatch *watch = &service->watches[index];
        if (!service->goals[watch->goal].goal.suspended)
            continue;
        uint32_t bucket = delay_bucket(watch->var, cap);
        watch->next = buckets[bucket];
        buckets[bucket] = index + 1u;
    }
    free(service->buckets);
    service->buckets = buckets;
    service->bucket_cap = cap;
    return true;
}

/* Take record `index` out of its chain. */
static void delay_unlink(CettaDelayService *service, uint32_t index) {
    uint32_t *link = &service->buckets[
        delay_bucket(service->watches[index].var, service->bucket_cap)];
    while (*link && *link != index + 1u)
        link = &service->watches[*link - 1u].next;
    if (*link)
        *link = service->watches[index].next;
}

/* Put record `index` back in its chain, before the older records. */
static void delay_relink(CettaDelayService *service, uint32_t index) {
    uint32_t *link = &service->buckets[
        delay_bucket(service->watches[index].var, service->bucket_cap)];
    while (*link && *link > index + 1u)
        link = &service->watches[*link - 1u].next;
    service->watches[index].next = *link;
    *link = index + 1u;
}

static void delay_unlink_goal(CettaDelayService *service, uint32_t goal) {
    const DelayGoalRecord *record = &service->goals[goal];
    for (uint32_t w = 0u; w < record->watch_count; w++)
        delay_unlink(service, record->first_watch + w);
    service->linked -= record->watch_count;
}

static void delay_relink_goal(CettaDelayService *service, uint32_t goal) {
    const DelayGoalRecord *record = &service->goals[goal];
    for (uint32_t w = record->watch_count; w > 0u; w--)
        delay_relink(service, record->first_watch + w - 1u);
    service->linked += record->watch_count;
}

CettaDelayService *cetta_delay_service_new(void) {
    CettaDelayService *service = cetta_malloc(sizeof(*service));
    if (!service)
        return NULL;
    *service = (CettaDelayService){0};
    arena_init_detached(&service->arena);
    arena_set_block_capacity(&service->arena, 4096u);
    if (!delay_rehash(service, 64u)) {
        arena_free(&service->arena);
        free(service);
        return NULL;
    }
    return service;
}

void cetta_delay_service_free(CettaDelayService *service) {
    if (!service)
        return;
    if (service->owner)
        service->owner_ops.free(service->owner);
    arena_free(&service->arena);
    free(service->goals);
    free(service->watches);
    free(service->buckets);
    free(service->queue);
    free(service->log);
    free(service);
}

bool cetta_delay_owner_install(CettaDelayService *service,
                               const CettaDelayOwnerOps *ops, void *state) {
    if (!service || service->owner || !ops || !state ||
        ops->client != CETTA_DELAY_CLIENT_PROLOG || !ops->clone || !ops->free || !ops->detach ||
        !ops->frontier || !ops->rollback || !ops->commit || !ops->rebase || !ops->watches ||
        !ops->bound || !ops->withdraw || !ops->reset || !ops->pending || !ops->sync ||
        !ops->roots || !ops->goals)
        return false;
    service->owner_ops = *ops;
    service->owner = state;
    return true;
}

void *cetta_delay_owner(const CettaDelayService *service, CettaDelayClient client) {
    return service && service->owner && service->owner_ops.client == client
        ? service->owner : NULL;
}

bool cetta_delay_holds(const CettaDelayService *service) {
    return service && (service->owner || service->suspended ||
                        service->queue_head < service->queue_len);
}

bool cetta_delay_owner_detach(const CettaDelayService *service) {
    return !service || !service->owner || service->owner_ops.detach(service->owner);
}

bool cetta_delay_owner_frontier(const CettaDelayService *service,
                                const uint32_t *kept, uint32_t count) {
    return !service || !service->owner ||
           service->owner_ops.frontier(service->owner, kept, count);
}

bool cetta_delay_owner_pending(const CettaDelayService *service) {
    return service && service->owner &&
           service->owner_ops.pending(service->owner);
}

CettaDelaySyncStatus cetta_delay_owner_sync(
    CettaDelayService *service, uint32_t mark, const Bindings *bindings, Arena *arena,
    Bindings *delta, Atom **raised) {
    if (!bindings || !arena || !delta || !raised)
        return CETTA_DELAY_SYNC_CAPACITY;
    *raised = NULL;
    if (!cetta_delay_owner_pending(service))
        return CETTA_DELAY_SYNC_READY;
    CettaDelaySyncStatus result = service->owner_ops.sync(
        service->owner, mark, bindings, arena, delta, raised);
    if ((result == CETTA_DELAY_SYNC_RAISED && !*raised) ||
        (result == CETTA_DELAY_SYNC_READY &&
         cetta_delay_owner_pending(service)))
        return CETTA_DELAY_SYNC_CAPACITY;
    return result;
}

bool cetta_delay_visit_owned_roots(
    const CettaDelayService *service,
    bool (*visit)(void *context, Atom *root), void *context) {
    return !service || !service->owner ||
           service->owner_ops.roots(service->owner, visit, context);
}

bool cetta_delay_watching(const CettaDelayService *service) {
    /* An owner may watch variables even when no serialized goal exists. */
    return service && (service->suspended > 0u || service->owner);
}

bool cetta_delay_serialized(const CettaDelayService *service) {
    return service && service->suspended > 0u;
}

bool cetta_delay_watches(const CettaDelayService *service, VarId var) {
    if (!cetta_delay_watching(service))
        return false;
    if (service->owner && service->owner_ops.watches(service->owner, var))
        return true;
    for (uint32_t cursor = service->buckets[
             delay_bucket(var, service->bucket_cap)];
         cursor; cursor = service->watches[cursor - 1u].next) {
        const DelayWatch *watch = &service->watches[cursor - 1u];
        if (watch->var == var && service->goals[watch->goal].goal.suspended)
            return true;
    }
    return false;
}

bool cetta_delay_suspend(CettaDelayService *service, uint32_t trail_mark,
                         CettaDelayClient client, Atom *goal,
                         const VarId *vars, uint32_t var_len,
                         bool bound_now) {
    if (!service || !goal || (var_len && !vars) ||
        !delay_grow((void **)&service->goals, &service->goal_cap,
                    service->goal_len + 1u, sizeof(*service->goals)) ||
        !delay_grow((void **)&service->watches, &service->watch_cap,
                    service->watch_len + var_len, sizeof(*service->watches)))
        return false;
    if ((service->linked + var_len) * 2u > service->bucket_cap &&
        !delay_rehash(service, service->bucket_cap * 2u))
        return false;
    ArenaMark storage = arena_mark(&service->arena);
    Atom *owned = atom_deep_copy(&service->arena, goal);
    VarId *owned_vars = var_len
        ? arena_alloc(&service->arena, sizeof(*owned_vars) * var_len) : NULL;
    if (!owned || (var_len && !owned_vars)) {
        arena_reset(&service->arena, storage);
        return false;
    }
    if (var_len)
        memcpy(owned_vars, vars, sizeof(*owned_vars) * var_len);
    uint32_t index = service->goal_len;
    if (!delay_log(service, DELAY_UNDO_SUSPEND, trail_mark, index)) {
        arena_reset(&service->arena, storage);
        return false;
    }
    service->goals[service->goal_len++] = (DelayGoalRecord){
        .goal = {client, owned, owned_vars, var_len, !bound_now},
        .storage = storage,
        .first_watch = service->watch_len,
        .watch_count = bound_now ? 0u : var_len,
    };
    if (bound_now) {
        if (!delay_grow((void **)&service->queue, &service->queue_cap,
                        service->queue_len + 1u, sizeof(*service->queue)) ||
            !delay_log(service, DELAY_UNDO_WAKE, trail_mark, index))
            return false;
        service->queue[service->queue_len++] = index;
        return true;
    }
    for (uint32_t v = 0u; v < var_len; v++) {
        uint32_t bucket = delay_bucket(vars[v], service->bucket_cap);
        service->watches[service->watch_len] =
            (DelayWatch){vars[v], index, service->buckets[bucket]};
        service->buckets[bucket] = ++service->watch_len;
    }
    service->linked += var_len;
    service->suspended++;
    return true;
}

/* The goals suspended on `var`, newest first, appended to the queue's
 * free tail; returns how many. */
static bool delay_gather(CettaDelayService *service, VarId var,
                         uint32_t *count) {
    *count = 0u;
    for (uint32_t cursor = service->buckets[
             delay_bucket(var, service->bucket_cap)];
         cursor; cursor = service->watches[cursor - 1u].next) {
        const DelayWatch *watch = &service->watches[cursor - 1u];
        if (watch->var != var || !service->goals[watch->goal].goal.suspended)
            continue;
        if (!delay_grow((void **)&service->queue, &service->queue_cap,
                        service->queue_len + *count + 1u,
                        sizeof(*service->queue)))
            return false;
        service->queue[service->queue_len + (*count)++] = watch->goal;
    }
    return true;
}

bool cetta_delay_bound(CettaDelayService *service, uint32_t trail_mark,
                       VarId var) {
    if (!cetta_delay_watching(service))
        return true;
    if (service->owner && service->owner_ops.watches(service->owner, var) &&
        !service->owner_ops.bound(service->owner, trail_mark, var))
        return false;
    uint32_t count = 0u;
    if (!delay_gather(service, var, &count))
        return false;
    /* The chain is newest first; wake in the order goals were suspended. */
    uint32_t *woken = service->queue + service->queue_len;
    for (uint32_t low = 0u, high = count; low + 1u < high; low++, high--) {
        uint32_t held = woken[low];
        woken[low] = woken[high - 1u];
        woken[high - 1u] = held;
    }
    for (uint32_t index = 0u; index < count; index++) {
        uint32_t goal = service->queue[service->queue_len];
        if (!delay_log(service, DELAY_UNDO_WAKE, trail_mark, goal))
            return false;
        service->goals[goal].goal.suspended = false;
        service->suspended--;
        delay_unlink_goal(service, goal);
        service->queue_len++;
    }
    return true;
}

bool cetta_delay_withdraw(CettaDelayService *service, uint32_t trail_mark,
                          VarId var) {
    if (!cetta_delay_watching(service))
        return true;
    if (service->owner && service->owner_ops.watches(service->owner, var) &&
        !service->owner_ops.withdraw(service->owner, trail_mark, var))
        return false;
    uint32_t count = 0u;
    if (!delay_gather(service, var, &count))
        return false;
    /* The gathered goals sit past the queue's end, which does not move. */
    for (uint32_t index = 0u; index < count; index++) {
        uint32_t goal = service->queue[service->queue_len + index];
        if (!delay_log(service, DELAY_UNDO_WITHDRAW, trail_mark, goal))
            return false;
        service->goals[goal].goal.suspended = false;
        service->suspended--;
        delay_unlink_goal(service, goal);
    }
    return true;
}

bool cetta_delay_has_woken(const CettaDelayService *service) {
    return service && service->queue_head < service->queue_len;
}

bool cetta_delay_take_woken(CettaDelayService *service, uint32_t trail_mark,
                            CettaDelayGoal *goal) {
    if (!cetta_delay_has_woken(service))
        return false;
    uint32_t index = service->queue[service->queue_head];
    if (!delay_log(service, DELAY_UNDO_TAKE, trail_mark, index))
        return false;
    service->queue_head++;
    *goal = service->goals[index].goal;
    return true;
}

void cetta_delay_rollback(CettaDelayService *service, uint32_t trail_mark) {
    if (!service)
        return;
    if (service->owner)
        service->owner_ops.rollback(service->owner, trail_mark);
    while (service->log_len > 0u &&
           service->log[service->log_len - 1u].trail_mark >= trail_mark) {
        DelayUndo undo = service->log[--service->log_len];
        DelayGoalRecord *record = &service->goals[undo.goal];
        switch (undo.kind) {
        case DELAY_UNDO_SUSPEND:
            /* The newest goal: its records are the newest, each at the head
             * of its chain while it waits. */
            if (record->goal.suspended) {
                delay_unlink_goal(service, undo.goal);
                service->suspended--;
            }
            service->watch_len = record->first_watch;
            arena_reset(&service->arena, record->storage);
            service->goal_len--;
            break;
        case DELAY_UNDO_WAKE:
            /* Undone before its suspension's own entry: back to suspended,
             * which that entry then removes. */
            service->queue_len--;
            record->goal.suspended = true;
            service->suspended++;
            delay_relink_goal(service, undo.goal);
            break;
        case DELAY_UNDO_TAKE:
            service->queue_head--;
            break;
        case DELAY_UNDO_WITHDRAW:
            record->goal.suspended = true;
            service->suspended++;
            delay_relink_goal(service, undo.goal);
            break;
        }
    }
}

void cetta_delay_commit(CettaDelayService *service) {
    if (service) {
        if (service->owner)
            service->owner_ops.commit(service->owner);
        service->log_len = 0u;
    }
}

void cetta_delay_rebase(CettaDelayService *service, const uint32_t *kept,
                        uint32_t kept_len) {
    if (!service)
        return;
    if (service->owner)
        service->owner_ops.rebase(service->owner, kept, kept_len);
    /* Marks along the log never decrease, so the changes older than every
     * kept checkpoint are a prefix, and the map is monotone. */
    uint32_t from = 0u;
    while (from < service->log_len &&
           (kept_len == 0u || service->log[from].trail_mark < kept[0]))
        from++;
    uint32_t position = 0u;
    for (uint32_t index = from; index < service->log_len; index++) {
        uint32_t mark = service->log[index].trail_mark;
        while (position + 1u < kept_len && kept[position + 1u] <= mark)
            position++;
        DelayUndo undo = service->log[index];
        undo.trail_mark = position;
        service->log[index - from] = undo;
    }
    service->log_len -= from;
}

bool cetta_delay_visit_live(const CettaDelayService *service,
                            bool (*visit)(void *context,
                                          const CettaDelayGoal *goal),
                            void *context) {
    if (!service)
        return true;
    for (uint32_t index = 0u; index < service->goal_len; index++) {
        const CettaDelayGoal *goal = &service->goals[index].goal;
        if (goal->suspended && !visit(context, goal))
            return false;
    }
    for (uint32_t index = service->queue_head; index < service->queue_len;
         index++) {
        if (!visit(context, &service->goals[service->queue[index]].goal))
            return false;
    }
    return true;
}

typedef struct {
    const CettaDelayGoal **items;
    uint32_t len, cap;
} DelayReachGoals;

static bool delay_reach_goal(void *context, const CettaDelayGoal *goal) {
    DelayReachGoals *goals = context;
    if (!goal || !goal->goal || (goal->var_len && !goal->vars) ||
        goals->len == UINT32_MAX ||
        !delay_grow((void **)&goals->items, &goals->cap, goals->len + 1u,
                     sizeof(*goals->items)))
        return false;
    goals->items[goals->len++] = goal;
    return true;
}

bool cetta_delay_materialize(CettaDelayService *service, uint32_t mark) {
    if (!service || !service->owner)
        return true;
    DelayReachGoals owned = {0};
    bool ok = service->owner_ops.goals(service->owner, delay_reach_goal, &owned);
    for (uint32_t at = 0; ok && at < owned.len; at++) {
        const CettaDelayGoal *goal = owned.items[at];
        ok = cetta_delay_suspend(service, mark, goal->client, goal->goal,
                                  goal->vars, goal->var_len, false);
    }
    free(owned.items);
    return ok && service->owner_ops.reset(service->owner, mark);
}

bool cetta_delay_reach(const CettaDelayService *service, const VarId *vars,
                       uint32_t var_len, const CettaDelayGoal ***goals,
                       uint32_t *count) {
    *goals = NULL;
    *count = 0u;
    if (!service || var_len == 0u)
        return true;
    if (!vars)
        return false;
    DelayReachGoals owned = {0};
    bool ok = !service->owner || service->owner_ops.goals(
        service->owner, delay_reach_goal, &owned);
    if (!ok) {
        free(owned.items);
        return false;
    }
    /* Build an observation-local index for retained goal views. Ordinary
     * goals keep their existing index; neither inventory is duplicated in
     * the persistent branch state. Both participate in the same closure. */
    uint32_t watch_len = 0u, bucket_cap = 64u;
    for (uint32_t at = 0; ok && at < owned.len; at++) {
        if (owned.items[at]->var_len > UINT32_MAX - watch_len)
            ok = false;
        else
            watch_len += owned.items[at]->var_len;
    }
    while (ok && bucket_cap / 2u < watch_len) {
        if (bucket_cap > UINT32_MAX / 2u)
            ok = false;
        else
            bucket_cap *= 2u;
    }
    uint32_t *buckets = ok && watch_len
        ? calloc(bucket_cap, sizeof(*buckets)) : NULL;
    DelayWatch *watches = ok && watch_len
        ? cetta_malloc((size_t)watch_len * sizeof(*watches)) : NULL;
    bool *reached = service->goal_len
        ? calloc(service->goal_len, sizeof(*reached)) : NULL;
    bool *owned_reached = owned.len ? calloc(owned.len, sizeof(*owned_reached)) : NULL;
    VarId *pending = NULL;
    uint32_t pending_len = 0u, pending_cap = 0u;
    ok = ok && (!watch_len || (buckets && watches)) &&
         (!service->goal_len || reached) && (!owned.len || owned_reached) &&
         delay_grow((void **)&pending, &pending_cap, var_len, sizeof(*pending));
    uint32_t used = 0u;
    for (uint32_t at = 0; ok && at < owned.len; at++) {
        for (uint32_t v = 0; v < owned.items[at]->var_len; v++) {
            VarId var = owned.items[at]->vars[v];
            uint32_t bucket = delay_bucket(var, bucket_cap);
            watches[used] = (DelayWatch){var, at, buckets[bucket]};
            buckets[bucket] = ++used;
        }
    }
    if (ok) {
        memcpy(pending, vars, (size_t)var_len * sizeof(*pending));
        pending_len = var_len;
    }
    uint32_t found = 0u;
    while (ok && pending_len) {
        VarId var = pending[--pending_len];
        for (unsigned family = 0; ok && family < 2; family++) {
            const DelayWatch *index = family ? watches : service->watches;
            uint32_t cursor = family
                ? (watch_len ? buckets[delay_bucket(var, bucket_cap)] : 0u)
                : service->buckets[delay_bucket(var, service->bucket_cap)];
            for (; ok && cursor; cursor = index[cursor - 1u].next) {
                const DelayWatch *watch = &index[cursor - 1u];
                bool *seen = family ? owned_reached : reached;
                const CettaDelayGoal *goal = family ? owned.items[watch->goal]
                    : &service->goals[watch->goal].goal;
                if (watch->var != var || seen[watch->goal] ||
                    (!family && !goal->suspended))
                    continue;
                if (found == UINT32_MAX || goal->var_len > UINT32_MAX - pending_len ||
                    !delay_grow((void **)&pending, &pending_cap,
                                pending_len + goal->var_len, sizeof(*pending))) {
                    ok = false;
                    break;
                }
                seen[watch->goal] = true;
                found++;
                if (goal->var_len)
                    memcpy(pending + pending_len, goal->vars,
                           (size_t)goal->var_len * sizeof(*pending));
                pending_len += goal->var_len;
            }
        }
    }
    const CettaDelayGoal **out = ok && found
        ? cetta_malloc((size_t)found * sizeof(*out)) : NULL;
    ok = ok && (!found || out);
    for (uint32_t at = 0; ok && at < service->goal_len; at++) {
        if (reached[at])
            out[(*count)++] = &service->goals[at].goal;
    }
    for (uint32_t at = 0; ok && at < owned.len; at++) {
        if (owned_reached[at])
            out[(*count)++] = owned.items[at];
    }
    free(pending);
    free(reached);
    free(owned_reached);
    free(watches);
    free(buckets);
    free(owned.items);
    if (!ok) {
        free(out);
        *count = 0u;
        return false;
    }
    *goals = out;
    return true;
}

uint32_t cetta_delay_goals_on(const CettaDelayService *service, VarId var,
                              const CettaDelayGoal **goals,
                              uint32_t capacity) {
    uint32_t count = 0u;
    if (!cetta_delay_watching(service))
        return 0u;
    for (uint32_t cursor = service->buckets[
             delay_bucket(var, service->bucket_cap)];
         cursor; cursor = service->watches[cursor - 1u].next) {
        const DelayWatch *watch = &service->watches[cursor - 1u];
        const DelayGoalRecord *record = &service->goals[watch->goal];
        if (watch->var != var || !record->goal.suspended)
            continue;
        if (count < capacity)
            goals[count] = &record->goal;
        count++;
    }
    if (service->owner) {
        DelayReachGoals owned = {0};
        if (!service->owner_ops.goals(service->owner, delay_reach_goal, &owned)) {
            free(owned.items);
            return UINT32_MAX;
        }
        for (uint32_t at = 0; at < owned.len; at++) {
            const CettaDelayGoal *goal = owned.items[at];
            for (uint32_t v = 0; v < goal->var_len; v++) {
                if (goal->vars[v] != var)
                    continue;
                if (count == UINT32_MAX) {
                    free(owned.items);
                    return UINT32_MAX;
                }
                if (count < capacity)
                    goals[count] = goal;
                count++;
                break;
            }
        }
        free(owned.items);
    }
    return count;
}

CettaDelayService *cetta_delay_service_clone(const CettaDelayService *service) {
    CettaDelayService *copy = cetta_delay_service_new();
    if (!copy || !service)
        return copy;
    if (service->owner) {
        copy->owner = service->owner_ops.clone(service->owner);
        if (!copy->owner) {
            cetta_delay_service_free(copy);
            return NULL;
        }
        copy->owner_ops = service->owner_ops;
    }
    /* The suspended goals in their order, then the queue in its order: a
     * copy's goals keep their relative order, so wakeups do too. */
    for (uint32_t index = 0u; index < service->goal_len; index++) {
        const CettaDelayGoal *goal = &service->goals[index].goal;
        if (goal->suspended &&
            !cetta_delay_suspend(copy, 0u, goal->client, goal->goal,
                                 goal->vars, goal->var_len, false)) {
            cetta_delay_service_free(copy);
            return NULL;
        }
    }
    for (uint32_t index = service->queue_head; index < service->queue_len;
         index++) {
        const CettaDelayGoal *goal =
            &service->goals[service->queue[index]].goal;
        if (!cetta_delay_suspend(copy, 0u, goal->client, goal->goal,
                                 goal->vars, goal->var_len, true)) {
            cetta_delay_service_free(copy);
            return NULL;
        }
    }
    /* A fresh history: nothing in the copy is undone past its creation. */
    copy->log_len = 0u;
    return copy;
}
