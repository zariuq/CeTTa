#include "state_cell.h"
#include "fallible_alloc.h"
#include "term_universe.h"
#include "u64_index.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ManagedStateNode ManagedStateNode;
typedef struct StateCellPressureWatch StateCellPressureWatch;

struct StateCellImage {
    _Atomic uint64_t references;
    Arena syntax;
    Atom *value;
    Atom *content_type;
    size_t bytes;
    StateCellImage *next;
    StateCellImage **link;
    bool retiring;
    bool contents_cleared;
    bool may_reference_resources;
    StateCell **mutable_leaves;
    size_t mutable_leaf_count, mutable_leaf_capacity;
    bool mutable_leaves_complete;
};

struct ManagedStateNode {
    StateCell cell;
    StateCellImage *current;
    uint64_t references;
    uint64_t publication_epoch;
    ManagedStateNode *left, *right;
    ManagedStateNode *pending_next;
    uint32_t height;
    bool queued;
    bool retiring;
    StateCellPressureWatch *watches;
};

struct StateCellPressureObserver {
    _Atomic uint64_t epoch;
    _Atomic uint64_t uncovered_epoch;
    uint64_t topology_epoch;
    size_t exposure_bytes;
    StateCellPressureWatch *accepted;
    StateCellPressureWatch *pending;
    StateCellPressureWatch *dirty_head, *dirty_tail;
    uint64_t next_identifier;
    size_t watch_count;
    bool collecting;
    bool pending_invalid;
};

struct StateCellPressureWatch {
    ManagedStateNode *node;
    StateCellPressureObserver *observer;
    uint64_t publication_epoch;
    uint64_t identifier;
    uint64_t inflight_capture;
    StateCellPressureWatch *node_next, **node_link;
    StateCellPressureWatch *set_next, **set_link;
    StateCellPressureWatch *dirty_next, *dirty_previous;
    bool pending;
    bool covered_by_accepted;
    bool queued;
    bool control_charged;
};

struct StateCellGraphSnapshot {
    StateCellGraphNode *nodes;
    StateCellGraphImage *images;
    size_t node_count, image_count;
    bool retired;
};

static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static ManagedStateNode *registry;
static ManagedStateNode *pending;
static StateCellImage *images;
static size_t node_count, image_count, image_bytes, pending_count;
static size_t dying_image_bytes, peak_image_bytes;
static _Atomic bool reclamation_due;

extern void eval_resource_retirement_notify(void) __attribute__((weak));

static size_t size_add(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static void pressure_dirty_locked(StateCellPressureObserver *observer) {
    uint64_t epoch = atomic_load_explicit(&observer->epoch, memory_order_relaxed);
    if (epoch != UINT64_MAX)
        atomic_store_explicit(&observer->epoch, epoch + 1u, memory_order_release);
}

static void pressure_exposure_locked(StateCellPressureObserver *observer,
                                     size_t bytes, bool topology) {
    observer->exposure_bytes = size_add(observer->exposure_bytes, bytes);
    if (topology && observer->topology_epoch != UINT64_MAX)
        observer->topology_epoch++;
    if (observer->exposure_bytes == SIZE_MAX || observer->topology_epoch == UINT64_MAX)
        atomic_store_explicit(&observer->epoch, UINT64_MAX, memory_order_release);
    else
        pressure_dirty_locked(observer);
}

static uint64_t pressure_identifier_locked(StateCellPressureObserver *observer) {
    if (observer->next_identifier == UINT64_MAX) {
        atomic_store_explicit(&observer->epoch, UINT64_MAX, memory_order_release);
        return 0u;
    }
    return ++observer->next_identifier;
}

static void pressure_dequeue_locked(StateCellPressureWatch *watch) {
    if (!watch->queued)
        return;
    StateCellPressureObserver *observer = watch->observer;
    if (watch->dirty_previous)
        watch->dirty_previous->dirty_next = watch->dirty_next;
    else
        observer->dirty_head = watch->dirty_next;
    if (watch->dirty_next)
        watch->dirty_next->dirty_previous = watch->dirty_previous;
    else
        observer->dirty_tail = watch->dirty_previous;
    watch->dirty_previous = watch->dirty_next = NULL;
    watch->queued = false;
}

static void pressure_enqueue_locked(StateCellPressureWatch *watch) {
    if (watch->pending || watch->queued || watch->inflight_capture)
        return;
    StateCellPressureObserver *observer = watch->observer;
    watch->dirty_previous = observer->dirty_tail;
    if (observer->dirty_tail)
        observer->dirty_tail->dirty_next = watch;
    else
        observer->dirty_head = watch;
    observer->dirty_tail = watch;
    watch->queued = true;
}

static void pressure_watch_unlink_locked(StateCellPressureWatch *watch) {
    pressure_dequeue_locked(watch);
    *watch->node_link = watch->node_next;
    if (watch->node_next)
        watch->node_next->node_link = watch->node_link;
    *watch->set_link = watch->set_next;
    if (watch->set_next)
        watch->set_next->set_link = watch->set_link;
    watch->observer->watch_count--;
    free(watch);
}

static void pressure_clear_set_locked(StateCellPressureWatch **set) {
    while (*set)
        pressure_watch_unlink_locked(*set);
}

static StateCellPressureWatch *pressure_watch_new_locked(
    StateCellPressureObserver *observer, ManagedStateNode *node,
    bool is_pending, bool covered_by_accepted) {
    if (observer->next_identifier == UINT64_MAX) {
        atomic_store_explicit(&observer->epoch, UINT64_MAX, memory_order_release);
        return NULL;
    }
    StateCellPressureWatch *watch = cetta_fallible_calloc(1u, sizeof(*watch));
    if (!watch)
        return NULL;
    StateCellPressureWatch **set = is_pending ? &observer->pending : &observer->accepted;
    *watch = (StateCellPressureWatch){.node = node, .observer = observer,
        .publication_epoch = node->publication_epoch,
        .identifier = pressure_identifier_locked(observer), .pending = is_pending,
        .covered_by_accepted = covered_by_accepted,
        .node_next = node->watches, .node_link = &node->watches,
        .set_next = *set, .set_link = set};
    if (watch->node_next)
        watch->node_next->node_link = &watch->node_next;
    if (watch->set_next)
        watch->set_next->set_link = &watch->set_next;
    node->watches = watch;
    *set = watch;
    observer->watch_count++;
    return watch;
}

static ManagedStateNode *find_node(const StateCell *cell);

static bool pressure_child_covered_locked(StateCellPressureObserver *observer,
                                           StateCell *cell) {
    ManagedStateNode *child = find_node(cell);
    if (!child || child->retiring || child->publication_epoch == UINT64_MAX)
        return false;
    for (StateCellPressureWatch *watch = child->watches; watch; watch = watch->node_next)
        if (watch->observer == observer && !watch->pending && !watch->queued &&
            !watch->inflight_capture && watch->publication_epoch == child->publication_epoch)
            return true;
    return false;
}

/* Called before the target's publication increment and dirty enqueue. A
 * self-edge is therefore judged against its genuinely settled old version.
 * No summary traversal or callback capable of taking this mutex occurs here. */
static void pressure_imports_before_publication_locked(ManagedStateNode *node,
                                                        StateCellImage *image) {
    if (!image || image == node->current)
        return;
    for (StateCellPressureWatch *watch = node->watches; watch; watch = watch->node_next) {
        if (watch->pending && watch->covered_by_accepted)
            continue;
        StateCellPressureObserver *observer = watch->observer;
        bool uncovered = !image->mutable_leaves_complete;
        for (size_t i = 0u; !uncovered && i < image->mutable_leaf_count; i++)
            uncovered = !pressure_child_covered_locked(observer, image->mutable_leaves[i]);
        if (!uncovered)
            continue;
        uint64_t epoch = atomic_load_explicit(&observer->uncovered_epoch, memory_order_relaxed);
        if (epoch != UINT64_MAX)
            epoch++;
        atomic_store_explicit(&observer->uncovered_epoch, epoch, memory_order_release);
        if (epoch == UINT64_MAX)
            atomic_store_explicit(&observer->epoch, UINT64_MAX, memory_order_release);
    }
}

static void pressure_node_changed_locked(ManagedStateNode *node,
                                         StateCellImage *old,
                                         StateCellImage *replacement) {
    size_t exposure = old == replacement ? 0u : size_add(
        old ? old->bytes : 0u, replacement ? replacement->bytes : 0u);
    bool topology = node->retiring || (old != replacement &&
        ((old && old->may_reference_resources) ||
         (replacement && replacement->may_reference_resources)));
    for (StateCellPressureWatch *watch = node->watches; watch; watch = watch->node_next)
        if (!watch->pending || !watch->covered_by_accepted) {
            if (node->publication_epoch == UINT64_MAX)
                atomic_store_explicit(&watch->observer->epoch, UINT64_MAX,
                    memory_order_release);
            pressure_exposure_locked(watch->observer, exposure, topology);
            pressure_enqueue_locked(watch);
        }
}

static void pressure_node_gone_locked(ManagedStateNode *node) {
    while (node->watches) {
        StateCellPressureWatch *watch = node->watches;
        if (!watch->pending || !watch->covered_by_accepted)
            pressure_exposure_locked(watch->observer, 0u, true);
        if (watch->pending)
            watch->observer->pending_invalid = true;
        pressure_watch_unlink_locked(watch);
    }
}

static uint32_t height(const ManagedStateNode *node) {
    return node ? node->height : 0u;
}

static void update_height(ManagedStateNode *node) {
    uint32_t left = height(node->left), right = height(node->right);
    node->height = (left > right ? left : right) + 1u;
}

static ManagedStateNode *rotate_left(ManagedStateNode *node) {
    ManagedStateNode *top = node->right;
    node->right = top->left;
    top->left = node;
    update_height(node);
    update_height(top);
    return top;
}

static ManagedStateNode *rotate_right(ManagedStateNode *node) {
    ManagedStateNode *top = node->left;
    node->left = top->right;
    top->right = node;
    update_height(node);
    update_height(top);
    return top;
}

static ManagedStateNode *balance(ManagedStateNode *node) {
    update_height(node);
    uint32_t left = height(node->left), right = height(node->right);
    if (left > right + 1u) {
        if (height(node->left->right) > height(node->left->left))
            node->left = rotate_left(node->left);
        return rotate_right(node);
    }
    if (right > left + 1u) {
        if (height(node->right->left) > height(node->right->right))
            node->right = rotate_right(node->right);
        return rotate_left(node);
    }
    return node;
}

static uintptr_t node_key(const ManagedStateNode *node) {
    return (uintptr_t)&node->cell;
}

static ManagedStateNode *find_node(const StateCell *cell) {
    uintptr_t key = (uintptr_t)cell;
    ManagedStateNode *node = registry;
    while (node) {
        uintptr_t at = node_key(node);
        if (key == at)
            return node;
        node = key < at ? node->left : node->right;
    }
    return NULL;
}

static ManagedStateNode *insert_node(ManagedStateNode *tree,
                                     ManagedStateNode *node) {
    if (!tree)
        return node;
    if (node_key(node) < node_key(tree))
        tree->left = insert_node(tree->left, node);
    else {
        assert(node_key(node) != node_key(tree));
        tree->right = insert_node(tree->right, node);
    }
    return balance(tree);
}

static ManagedStateNode *remove_min(ManagedStateNode *tree,
                                    ManagedStateNode **minimum) {
    if (!tree->left) {
        *minimum = tree;
        return tree->right;
    }
    tree->left = remove_min(tree->left, minimum);
    return balance(tree);
}

static ManagedStateNode *remove_node(ManagedStateNode *tree, uintptr_t key) {
    assert(tree);
    uintptr_t at = node_key(tree);
    if (key < at)
        tree->left = remove_node(tree->left, key);
    else if (key > at)
        tree->right = remove_node(tree->right, key);
    else {
        if (!tree->left)
            return tree->right;
        if (!tree->right)
            return tree->left;
        ManagedStateNode *replacement;
        ManagedStateNode *right = remove_min(tree->right, &replacement);
        replacement->left = tree->left;
        replacement->right = right;
        return balance(replacement);
    }
    return balance(tree);
}

static void node_retain_locked(ManagedStateNode *node) {
    if (node->references != UINT64_MAX)
        node->references++;
}

static void node_retain_owner(void *owner) {
    pthread_mutex_lock(&registry_mutex);
    node_retain_locked(owner);
    pthread_mutex_unlock(&registry_mutex);
}

static void node_release_owner(void *owner) {
    ManagedStateNode *node = owner;
    bool notify = false;
    pthread_mutex_lock(&registry_mutex);
    assert(node->references);
    if (node->references != UINT64_MAX && --node->references == 0u && !node->queued) {
        node->queued = true;
        node->pending_next = pending;
        pending = node;
        pending_count++;
        notify = true;
        atomic_store_explicit(&reclamation_due, true, memory_order_release);
    }
    pthread_mutex_unlock(&registry_mutex);
    if (notify && eval_resource_retirement_notify)
        eval_resource_retirement_notify();
}

static void image_retain_locked(StateCellImage *image) {
    uint64_t references = atomic_load_explicit(&image->references, memory_order_relaxed);
    for (;;) {
        assert(references);
        if (references == UINT64_MAX)
            return;
        if (atomic_compare_exchange_weak_explicit(&image->references, &references,
                references + 1u, memory_order_relaxed, memory_order_relaxed))
            return;
    }
}

void state_cell_image_retain(StateCellImage *image) {
    if (!image)
        return;
    pthread_mutex_lock(&registry_mutex);
    assert(!image->retiring && !image->contents_cleared);
    image_retain_locked(image);
    pthread_mutex_unlock(&registry_mutex);
}

/* A zero-ref image may still be linked while its finalizer waits for the
 * registry mutex. A census must never revive that dying object. */
static bool image_try_retain_positive(StateCellImage *image, uint64_t *before) {
    uint64_t references = atomic_load_explicit(&image->references, memory_order_acquire);
    for (;;) {
        if (!references)
            return false;
        if (references == UINT64_MAX) {
            *before = references;
            return true;
        }
        if (atomic_compare_exchange_weak_explicit(&image->references, &references,
                references + 1u, memory_order_acquire, memory_order_relaxed)) {
            *before = references;
            return true;
        }
    }
}

void state_cell_image_release(StateCellImage *image) {
    if (!image)
        return;
    for (;;) {
        uint64_t references = atomic_load_explicit(&image->references, memory_order_acquire);
        assert(references);
        if (references == UINT64_MAX)
            return;
        if (references == 1u) {
            if (atomic_compare_exchange_weak_explicit(&image->references, &references,
                    0u, memory_order_acq_rel, memory_order_relaxed))
                break;
            continue;
        }
        /* A graph snapshot's pin prevents the final 1->0 transition. All
         * changes to its positive counts therefore serialize with preflight. */
        pthread_mutex_lock(&registry_mutex);
        references = atomic_load_explicit(&image->references, memory_order_relaxed);
        if (references == UINT64_MAX) {
            pthread_mutex_unlock(&registry_mutex);
            return;
        }
        if (references > 1u) {
            atomic_store_explicit(&image->references, references - 1u, memory_order_release);
            pthread_mutex_unlock(&registry_mutex);
            return;
        }
        pthread_mutex_unlock(&registry_mutex);
    }
#ifdef CETTA_STATE_CELL_TEST_HOOKS
    extern void state_cell_test_image_zero_before_unlink(StateCellImage *);
    state_cell_test_image_zero_before_unlink(image);
#endif
    pthread_mutex_lock(&registry_mutex);
    *image->link = image->next;
    if (image->next)
        image->next->link = image->link;
    image_count--;
    if (image_bytes == SIZE_MAX) {
        image_bytes = 0u;
        for (StateCellImage *live = images; live; live = live->next)
            image_bytes = size_add(image_bytes, live->bytes);
    } else {
        image_bytes -= image->bytes;
    }
    size_t dying_bytes = image->bytes;
    dying_image_bytes = size_add(dying_image_bytes, dying_bytes);
    pthread_mutex_unlock(&registry_mutex);
    if (!image->contents_cleared)
        arena_free(&image->syntax);
    free(image->mutable_leaves);
    free(image);
    pthread_mutex_lock(&registry_mutex);
    if (dying_image_bytes != SIZE_MAX)
        dying_image_bytes -= dying_bytes;
    pthread_mutex_unlock(&registry_mutex);
}

static void image_retain_owner(void *owner) {
    state_cell_image_retain(owner);
}

static void image_release_owner(void *owner) {
    state_cell_image_release(owner);
}

static void image_has_owner(void *context, void *owner, void (*release)(void *)) {
    (void)owner;
    (void)release;
    *(bool *)context = true;
}

static bool atom_may_reference_resources(const Atom *atom) {
    return atom &&
        (!(atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) ||
         !(atom->flags & ATOM_FLAG_HASH_STABLE) ||
         atom_has_registry_refs(atom) || atom_has_identity_grounded(atom) ||
         atom_has_thread_local_resource(atom));
}

/* The immutable owner closure is traversed once while its new root owns
 * every borrowed ancestor. Mutable State identities are leaves, not recursive
 * byte weights. Unknown owners and optional allocation refusal retain an
 * incomplete certificate, which publication must treat as an obligation. */
typedef struct {
    StateCellImage *image;
    const Arena **arenas;
    const Arena *inline_arenas[8];
    size_t arena_at, arena_count, arena_capacity;
    CettaU64Index seen_arenas, seen_cells;
    bool cells_indexed, complete;
} StateImageSummary;

static bool image_summary_leaf(StateImageSummary *summary, StateCell *cell) {
    StateCellImage *image = summary->image;
    if (!summary->cells_indexed && image->mutable_leaf_count < 4u) {
        for (size_t i = 0u; i < image->mutable_leaf_count; i++)
            if (image->mutable_leaves[i] == cell)
                return true;
    } else {
        if (!summary->cells_indexed) {
            for (size_t i = 0u; i < image->mutable_leaf_count; i++)
                if (cetta_u64_index_insert(&summary->seen_cells,
                        (uint64_t)(uintptr_t)image->mutable_leaves[i], 0u) !=
                        CETTA_GSLT_U32_INDEX_INSERTED_V1)
                    return false;
            summary->cells_indexed = true;
        }
        CettaGsltU32IndexInsertResultV1 inserted = cetta_u64_index_insert(
            &summary->seen_cells, (uint64_t)(uintptr_t)cell, 0u);
        if (inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1)
            return true;
        if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1)
            return false;
    }
    if (image->mutable_leaf_count == image->mutable_leaf_capacity) {
        size_t next = image->mutable_leaf_capacity
            ? image->mutable_leaf_capacity * 2u : 4u;
        if (next < image->mutable_leaf_capacity ||
            next > SIZE_MAX / sizeof(*image->mutable_leaves))
            return false;
        StateCell **more = cetta_fallible_malloc(next * sizeof(*more));
        if (!more)
            return false;
        if (image->mutable_leaf_count)
            memcpy(more, image->mutable_leaves,
                   image->mutable_leaf_count * sizeof(*more));
        free(image->mutable_leaves);
        image->mutable_leaves = more;
        image->mutable_leaf_capacity = next;
    }
    image->mutable_leaves[image->mutable_leaf_count++] = cell;
    return true;
}

static bool image_summary_arena(StateImageSummary *summary, const Arena *arena) {
    if (!arena || !arena->identity)
        return false;
    CettaGsltU32IndexInsertResultV1 inserted = cetta_u64_index_insert(
        &summary->seen_arenas, arena->identity, 0u);
    if (inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1)
        return true;
    if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1)
        return false;
    if (summary->arena_count == summary->arena_capacity) {
        size_t next = summary->arena_capacity * 2u;
        if (next < summary->arena_capacity || next > SIZE_MAX / sizeof(*summary->arenas))
            return false;
        const Arena **more = cetta_fallible_malloc(next * sizeof(*more));
        if (!more)
            return false;
        memcpy(more, summary->arenas, summary->arena_count * sizeof(*more));
        if (summary->arenas != summary->inline_arenas)
            free(summary->arenas);
        summary->arenas = more;
        summary->arena_capacity = next;
    }
    summary->arenas[summary->arena_count++] = arena;
    return true;
}

static void image_summary_owner(void *context, void *owner,
                                 void (*release)(void *)) {
    StateImageSummary *summary = context;
    if (!summary->complete || arena_owner_is_atomic_storage(owner, release))
        return;
    StateCell *cell = NULL;
    StateCellImage *image = NULL;
    if (state_cell_classify_owner(owner, release, &cell, &image)) {
        if (cell) {
            summary->complete = image_summary_leaf(summary, cell);
        } else if (!image || !image->mutable_leaves_complete || image->contents_cleared) {
            summary->complete = false;
        } else {
            for (size_t i = 0u; summary->complete && i < image->mutable_leaf_count; i++)
                summary->complete = image_summary_leaf(summary, image->mutable_leaves[i]);
        }
        return;
    }
    TermUniverseResourceImageView wrapper = {0};
    summary->complete = term_universe_resource_image_owner_view(owner, release, &wrapper) &&
        image_summary_arena(summary, wrapper.syntax);
}

/* Raw legacy handles do not necessarily carry a managed owner lease. Check
 * immutable source structure as well as owner ancestry. These are optional
 * certification budgets, not limits on the stored value or publication. */
static bool image_summary_source(StateImageSummary *summary, const Atom *atom,
                                   size_t *remaining, unsigned depth) {
    if (!atom)
        return true;
    if (!*remaining)
        return false;
    (*remaining)--;
    if (!atom_has_vars(atom) && !atom_may_reference_resources(atom))
        return true;
    if (depth >= 64u)
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return image_summary_source(summary, atom->name_key, remaining, depth + 1u);
    case ATOM_EXPR:
        if (atom->expr.len > *remaining)
            return false;
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
            if (!image_summary_source(summary, atom->expr.elems[i], remaining, depth + 1u))
                return false;
        return true;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT: case GV_FLOAT: case GV_BOOL: case GV_STRING:
        case GV_BIGINT: case GV_RATIONAL:
            return true;
        case GV_STATE:
            if (summary->cells_indexed) {
                uint32_t unused;
                return cetta_u64_index_find(&summary->seen_cells,
                    (uint64_t)(uintptr_t)atom->ground.ptr, &unused);
            }
            for (size_t i = 0u; i < summary->image->mutable_leaf_count; i++)
                if (summary->image->mutable_leaves[i] == atom->ground.ptr)
                    return true;
            return false;
        default:
            return false;
        }
    }
    return false;
}

static void image_prepare_summary(StateCellImage *image) {
    StateImageSummary summary = {.image = image, .arena_capacity = 8u,
                                .arena_count = 1u, .complete = true};
    summary.arenas = summary.inline_arenas;
    summary.arenas[0] = &image->syntax;
    cetta_u64_index_init(&summary.seen_arenas);
    cetta_u64_index_init(&summary.seen_cells);
    while (summary.complete && summary.arena_at < summary.arena_count)
        arena_visit_retained_owners(summary.arenas[summary.arena_at++],
                                    image_summary_owner, &summary);
    if (summary.complete &&
            ((image->value && (atom_has_vars(image->value) || atom_may_reference_resources(image->value))) ||
             (image->content_type && (atom_has_vars(image->content_type) ||
                                      atom_may_reference_resources(image->content_type))))) {
        size_t remaining = 4096u;
        summary.complete = image_summary_source(&summary, image->value, &remaining, 0u) &&
            image_summary_source(&summary, image->content_type, &remaining, 0u);
    }
    image->mutable_leaves_complete = summary.complete;
    if (!summary.complete) {
        free(image->mutable_leaves);
        image->mutable_leaves = NULL;
        image->mutable_leaf_count = image->mutable_leaf_capacity = 0u;
    }
    if (summary.arenas != summary.inline_arenas)
        free(summary.arenas);
    cetta_u64_index_free(&summary.seen_arenas);
    cetta_u64_index_free(&summary.seen_cells);
}

StateCellImage *state_cell_image_prepare(Atom *value, Atom *content_type,
                                         bool canonical) {
    if (!value)
        return NULL;
    StateCellImage *image = cetta_fallible_calloc(1u, sizeof(*image));
    if (!image)
        return NULL;
    atomic_init(&image->references, 1u);
    arena_init_detached(&image->syntax);
    arena_set_block_growth(&image->syntax, 256u);
    /* Syntax interning identifies an evaluated value with its unmarked code,
     * so a marked value (including marked fields) uses an owned copy. */
    if (canonical && !atom_may_hold_he_mark(value)) {
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &image->syntax);
        image->value = term_universe_store_atom(&universe, &image->syntax, value);
        image->content_type = content_type
            ? term_universe_store_atom(&universe, &image->syntax, content_type) : NULL;
        term_universe_free(&universe);
    } else {
        AtomDeepCopySession *copy = atom_deep_copy_session_new(&image->syntax);
        if (copy) {
            image->value = atom_deep_copy_session_copy(copy, value);
            image->content_type = content_type
                ? atom_deep_copy_session_copy(copy, content_type) : NULL;
            atom_deep_copy_session_free(copy);
        }
    }
    if (!image->value || (content_type && !image->content_type)) {
        arena_free(&image->syntax);
        free(image);
        return NULL;
    }
    image_prepare_summary(image);
    /* Outgoing native-resource weights are graph edges, not storage owned
     * by this image. Including them recursively would carry old image costs
     * through repeated self-reference updates despite constant storage. */
    assert(image->syntax.retained_resource_bytes <= image->syntax.external_bytes);
    image->bytes = size_add(sizeof(*image), size_add(
        image->mutable_leaf_capacity * sizeof(*image->mutable_leaves),
        size_add(image->syntax.reserved_bytes,
            image->syntax.external_bytes - image->syntax.retained_resource_bytes)));
    image->may_reference_resources = atom_may_reference_resources(image->value) ||
        atom_may_reference_resources(image->content_type);
    arena_visit_retained_owners(&image->syntax, image_has_owner,
        &image->may_reference_resources);
    pthread_mutex_lock(&registry_mutex);
    image->next = images;
    image->link = &images;
    if (images)
        images->link = &image->next;
    images = image;
    image_count++;
    image_bytes = size_add(image_bytes, image->bytes);
    size_t total = size_add(image_bytes, dying_image_bytes);
    if (total > peak_image_bytes)
        peak_image_bytes = total;
    pthread_mutex_unlock(&registry_mutex);
    return image;
}

size_t state_cell_image_bytes(const StateCellImage *image) {
    return image ? image->bytes : 0u;
}

const Arena *state_cell_image_syntax(const StateCellImage *image) {
    return image && !image->contents_cleared ? &image->syntax : NULL;
}

StateCell *state_cell_managed_new(StateCellImage *image) {
    ManagedStateNode *node = cetta_fallible_calloc(1u, sizeof(*node));
    if (!node)
        return NULL;
    node->current = image;
    node->references = 1u;
    node->publication_epoch = 1u;
    node->height = 1u;
    if (image) {
        node->cell.value = image->value;
        node->cell.content_type = image->content_type;
    }
    pthread_mutex_lock(&registry_mutex);
    registry = insert_node(registry, node);
    node_count++;
    pthread_mutex_unlock(&registry_mutex);
    return &node->cell;
}

bool state_cell_is_managed(const StateCell *cell) {
    pthread_mutex_lock(&registry_mutex);
    bool managed = find_node(cell) != NULL;
    pthread_mutex_unlock(&registry_mutex);
    return managed;
}

void *state_cell_identity_pin(StateCell *cell) {
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    if (node && !node->retiring)
        node_retain_locked(node);
    else
        node = NULL;
    pthread_mutex_unlock(&registry_mutex);
    return node;
}

void state_cell_identity_unpin(void *pin) {
    if (pin)
        node_release_owner(pin);
}

void state_cell_creator_release(StateCell *cell) {
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    pthread_mutex_unlock(&registry_mutex);
    if (node)
        node_release_owner(node);
}

StateCellLease state_cell_lease_into(Arena *arena, StateCell *cell) {
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    size_t charge = 0u;
    if (node) {
        if (node->retiring) {
            pthread_mutex_unlock(&registry_mutex);
            return STATE_CELL_CAPACITY;
        }
        node_retain_locked(node);
        charge = size_add(sizeof(*node), state_cell_image_bytes(node->current));
    }
    pthread_mutex_unlock(&registry_mutex);
    if (!node)
        return STATE_CELL_UNMANAGED;
    bool held = arena && arena_retain_state_resource_sized(arena, node,
        node_retain_owner, node_release_owner, charge, charge);
    node_release_owner(node);
    return held ? STATE_CELL_HELD : STATE_CELL_CAPACITY;
}

static StateCellLease acquire_image(StateCell *cell, StateCellSnapshot *snapshot) {
    *snapshot = (StateCellSnapshot){0};
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    StateCellLease result = STATE_CELL_UNMANAGED;
    if (node) {
        result = STATE_CELL_CAPACITY;
        if (!node->retiring && node->current) {
            image_retain_locked(node->current);
            *snapshot = (StateCellSnapshot){.value = node->current->value,
                .content_type = node->current->content_type, .image = node->current};
            result = STATE_CELL_HELD;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    return result;
}

bool state_cell_snapshot_acquire(StateCell *cell, StateCellSnapshot *snapshot) {
    return snapshot && acquire_image(cell, snapshot) == STATE_CELL_HELD;
}

bool state_cell_image_snapshot_acquire(StateCellImage *image,
                                       StateCellSnapshot *snapshot) {
    if (!snapshot)
        return false;
    *snapshot = (StateCellSnapshot){0};
    if (!image)
        return false;
    pthread_mutex_lock(&registry_mutex);
    uint64_t before;
    bool held = !image->retiring && !image->contents_cleared &&
        image_try_retain_positive(image, &before);
    if (held)
        *snapshot = (StateCellSnapshot){.value = image->value,
            .content_type = image->content_type, .image = image};
    pthread_mutex_unlock(&registry_mutex);
    return held;
}

void state_cell_snapshot_release(StateCellSnapshot *snapshot) {
    if (snapshot) {
        StateCellImage *image = snapshot->image;
        *snapshot = (StateCellSnapshot){0};
        state_cell_image_release(image);
    }
}

bool state_cell_snapshot_retain_into(Arena *arena,
                                      const StateCellSnapshot *snapshot) {
    return arena && snapshot && snapshot->image &&
        arena_retain_state_resource_sized(arena, snapshot->image,
            image_retain_owner, image_release_owner,
            snapshot->image->bytes, snapshot->image->bytes);
}

StateCellLease state_cell_snapshot_into(Arena *arena, StateCell *cell,
                                        StateCellSnapshot *snapshot) {
    if (!snapshot)
        return state_cell_is_managed(cell) ? STATE_CELL_CAPACITY : STATE_CELL_UNMANAGED;
    StateCellSnapshot retained;
    StateCellLease result = acquire_image(cell, &retained);
    *snapshot = (StateCellSnapshot){0};
    if (result != STATE_CELL_HELD)
        return result;
    if (!state_cell_snapshot_retain_into(arena, &retained))
        result = STATE_CELL_CAPACITY;
    else
        *snapshot = (StateCellSnapshot){.value = retained.value,
            .content_type = retained.content_type};
    state_cell_snapshot_release(&retained);
    return result;
}

bool state_cell_publish_prepared(StateCell *cell, StateCellImage *image) {
    if (!image)
        return false;
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    StateCellImage *old = NULL;
    if (node && !node->retiring && !image->retiring && !image->contents_cleared) {
        old = node->current;
        pressure_imports_before_publication_locked(node, image);
        node->current = image;
        node->cell.value = image->value;
        node->cell.content_type = image->content_type;
        if (node->publication_epoch != UINT64_MAX)
            node->publication_epoch++;
        pressure_node_changed_locked(node, old, image);
    } else {
        node = NULL;
    }
    pthread_mutex_unlock(&registry_mutex);
    if (node)
        state_cell_image_release(old);
    return node != NULL;
}

size_t state_cell_reclaim_bounded(size_t limit) {
    size_t retired = 0u;
    for (size_t processed = 0u; processed < limit; processed++) {
        pthread_mutex_lock(&registry_mutex);
        ManagedStateNode *node = pending;
        if (!node) {
            atomic_store_explicit(&reclamation_due, false, memory_order_release);
            pthread_mutex_unlock(&registry_mutex);
            break;
        }
        pending = node->pending_next;
        pending_count--;
        node->pending_next = NULL;
        node->queued = false;
        bool retire = node->references == 0u;
        if (retire) {
            registry = remove_node(registry, node_key(node));
            node_count--;
            pressure_node_gone_locked(node);
        }
        atomic_store_explicit(&reclamation_due, pending != NULL, memory_order_release);
        pthread_mutex_unlock(&registry_mutex);
        if (retire) {
            StateCellImage *image = node->current;
            free(node);
            state_cell_image_release(image);
            retired++;
        }
    }
    return retired;
}

bool state_cell_reclamation_due(void) {
    return atomic_load_explicit(&reclamation_due, memory_order_acquire);
}

static void visit_nodes(ManagedStateNode *node, StateCellVisit visit, void *context) {
    if (!node)
        return;
    visit_nodes(node->left, visit, context);
    visit(context, &node->cell, node->current, node->references,
        node->references == UINT64_MAX);
    visit_nodes(node->right, visit, context);
}

void state_cell_visit_managed(StateCellVisit visit, void *context) {
    if (!visit)
        return;
    pthread_mutex_lock(&registry_mutex);
    visit_nodes(registry, visit, context);
    pthread_mutex_unlock(&registry_mutex);
}

void state_cell_visit_images(StateCellImageVisit visit, void *context) {
    if (!visit)
        return;
    pthread_mutex_lock(&registry_mutex);
    for (StateCellImage *image = images; image; image = image->next) {
        uint64_t references = atomic_load_explicit(&image->references, memory_order_acquire);
        visit(context, image, &image->syntax, references, references == UINT64_MAX);
    }
    pthread_mutex_unlock(&registry_mutex);
}

void state_cell_image_visit_owners(const StateCellImage *image,
                                   ArenaOwnerVisit visit, void *context) {
    if (image && !image->contents_cleared)
        arena_visit_retained_owners(&image->syntax, visit, context);
}

bool state_cell_classify_owner(void *owner, void (*release)(void *),
                                StateCell **cell, StateCellImage **image) {
    if (cell)
        *cell = NULL;
    if (image)
        *image = NULL;
    if (!owner)
        return false;
    if (release == node_release_owner) {
        if (cell)
            *cell = &((ManagedStateNode *)owner)->cell;
        return true;
    }
    if (release == image_release_owner) {
        if (image)
            *image = owner;
        return true;
    }
    return false;
}

static void graph_capture_nodes(ManagedStateNode *node,
                                 StateCellGraphSnapshot *snapshot) {
    if (!node)
        return;
    graph_capture_nodes(node->left, snapshot);
    if (!node->retiring) {
        StateCellGraphNode *row = &snapshot->nodes[snapshot->node_count++];
        *row = (StateCellGraphNode){.cell = &node->cell,
            .current_image = node->current, .references = node->references,
            .publication_epoch = node->publication_epoch,
            .saturated = node->references >= UINT64_MAX - 1u ||
                node->publication_epoch == UINT64_MAX};
        node_retain_locked(node);
    }
    graph_capture_nodes(node->right, snapshot);
}

StateCellGraphSnapshot *state_cell_graph_snapshot_acquire(void) {
    StateCellGraphSnapshot *snapshot = cetta_fallible_calloc(1u, sizeof(*snapshot));
    if (!snapshot)
        return NULL;
    pthread_mutex_lock(&registry_mutex);
    bool sizes = node_count <= SIZE_MAX / sizeof(*snapshot->nodes) &&
        image_count <= SIZE_MAX / sizeof(*snapshot->images);
    if (sizes && node_count)
        snapshot->nodes = cetta_fallible_calloc(node_count, sizeof(*snapshot->nodes));
    if (sizes && image_count)
        snapshot->images = cetta_fallible_calloc(image_count, sizeof(*snapshot->images));
    if (!sizes || (node_count && !snapshot->nodes) || (image_count && !snapshot->images)) {
        pthread_mutex_unlock(&registry_mutex);
        free(snapshot->nodes);
        free(snapshot->images);
        free(snapshot);
        return NULL;
    }
    /* Every allocation has succeeded before any lifetime pin is acquired. */
    graph_capture_nodes(registry, snapshot);
    for (StateCellImage *image = images; image; image = image->next) {
        uint64_t before;
        if (image->retiring || image->contents_cleared ||
            !image_try_retain_positive(image, &before))
            continue;
        StateCellGraphImage *row = &snapshot->images[snapshot->image_count++];
        *row = (StateCellGraphImage){.image = image, .syntax = &image->syntax,
            .value = image->value, .content_type = image->content_type,
            .references = before, .saturated = before >= UINT64_MAX - 1u};
    }
    pthread_mutex_unlock(&registry_mutex);
    return snapshot;
}

void state_cell_graph_snapshot_release(StateCellGraphSnapshot *snapshot) {
    if (!snapshot)
        return;
    /* Image finalizers can release State edges. Preserve every node control
     * until those callbacks finish, then let queued retirement proceed. */
    for (size_t i = 0u; i < snapshot->image_count; i++)
        state_cell_image_release(snapshot->images[i].image);
    for (size_t i = 0u; i < snapshot->node_count; i++)
        node_release_owner((ManagedStateNode *)snapshot->nodes[i].cell);
    free(snapshot->nodes);
    free(snapshot->images);
    free(snapshot);
}

size_t state_cell_graph_node_count(const StateCellGraphSnapshot *snapshot) {
    return snapshot ? snapshot->node_count : 0u;
}

size_t state_cell_graph_image_count(const StateCellGraphSnapshot *snapshot) {
    return snapshot ? snapshot->image_count : 0u;
}

const StateCellGraphNode *state_cell_graph_nodes(const StateCellGraphSnapshot *snapshot) {
    return snapshot ? snapshot->nodes : NULL;
}

const StateCellGraphImage *state_cell_graph_images(const StateCellGraphSnapshot *snapshot) {
    return snapshot ? snapshot->images : NULL;
}

static bool pinned_count_matches(uint64_t current, uint64_t original) {
    return original < UINT64_MAX - 1u && current == original + 1u;
}

bool state_cell_graph_retire(StateCellGraphSnapshot *snapshot,
                              const bool *dead_nodes, const bool *dead_images,
                              const uint64_t *incoming_nodes,
                              const uint64_t *incoming_images) {
    if (!snapshot || snapshot->retired)
        return false;
    pthread_mutex_lock(&registry_mutex);
    bool valid = true;
    for (size_t i = 0u; valid && i < snapshot->node_count; i++) {
        if (!dead_nodes || !dead_nodes[i])
            continue;
        const StateCellGraphNode *row = &snapshot->nodes[i];
        ManagedStateNode *node = find_node(row->cell);
        valid = incoming_nodes && !row->saturated && node && !node->retiring &&
            node == (ManagedStateNode *)row->cell &&
            row->references == incoming_nodes[i] &&
            pinned_count_matches(node->references, row->references) &&
            node->publication_epoch == row->publication_epoch &&
            node->current == row->current_image &&
            node->cell.value == (node->current ? node->current->value : NULL) &&
            node->cell.content_type == (node->current ? node->current->content_type : NULL);
    }
    for (size_t i = 0u; valid && i < snapshot->image_count; i++) {
        if (!dead_images || !dead_images[i])
            continue;
        const StateCellGraphImage *row = &snapshot->images[i];
        StateCellImage *image = row->image;
        valid = incoming_images && !row->saturated && !image->retiring &&
            !image->contents_cleared && row->references == incoming_images[i] &&
            pinned_count_matches(atomic_load_explicit(&image->references,
                memory_order_acquire), row->references) &&
            image->value == row->value && image->content_type == row->content_type;
    }
    if (!valid) {
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    /* No finalizer runs until every selected container has detached. The
     * caller has already protected its doomed foreign containers as well. */
    for (size_t i = 0u; i < snapshot->node_count; i++) {
        if (!dead_nodes || !dead_nodes[i])
            continue;
        ManagedStateNode *node = (ManagedStateNode *)snapshot->nodes[i].cell;
        node->retiring = true;
        node->current = NULL;
        node->cell.value = NULL;
        node->cell.content_type = NULL;
        node->publication_epoch++;
        pressure_node_changed_locked(node, snapshot->nodes[i].current_image, NULL);
    }
    for (size_t i = 0u; i < snapshot->image_count; i++) {
        if (!dead_images || !dead_images[i])
            continue;
        StateCellImage *image = snapshot->images[i].image;
        image->retiring = true;
        image->contents_cleared = true;
        image->value = NULL;
        image->content_type = NULL;
    }
    snapshot->retired = true;
    pthread_mutex_unlock(&registry_mutex);

    for (size_t i = 0u; i < snapshot->image_count; i++) {
        if (!dead_images || !dead_images[i])
            continue;
        StateCellImage *image = snapshot->images[i].image;
        arena_free(&image->syntax);
        free(image->mutable_leaves);
        image->mutable_leaves = NULL;
        image->mutable_leaf_count = image->mutable_leaf_capacity = 0u;
        image->mutable_leaves_complete = false;
        pthread_mutex_lock(&registry_mutex);
        size_t released = image->bytes - sizeof(*image);
        image->bytes = sizeof(*image);
        if (image_bytes == SIZE_MAX) {
            image_bytes = 0u;
            for (StateCellImage *live = images; live; live = live->next)
                image_bytes = size_add(image_bytes, live->bytes);
        } else {
            image_bytes -= released;
        }
        pthread_mutex_unlock(&registry_mutex);
    }
    for (size_t i = 0u; i < snapshot->node_count; i++)
        if (dead_nodes && dead_nodes[i])
            state_cell_image_release(snapshot->nodes[i].current_image);
    return true;
}

StateCellPressureObserver *state_cell_pressure_observer_new(void) {
    StateCellPressureObserver *observer = cetta_fallible_calloc(1u, sizeof(*observer));
    if (observer) {
        atomic_init(&observer->epoch, 1u);
        atomic_init(&observer->uncovered_epoch, 0u);
    }
    return observer;
}

void state_cell_pressure_observer_free(StateCellPressureObserver *observer) {
    if (!observer)
        return;
    pthread_mutex_lock(&registry_mutex);
    pressure_clear_set_locked(&observer->accepted);
    pressure_clear_set_locked(&observer->pending);
    pthread_mutex_unlock(&registry_mutex);
    free(observer);
}

uint64_t state_cell_pressure_observer_epoch(const StateCellPressureObserver *observer) {
    return observer ? atomic_load_explicit(&observer->epoch, memory_order_acquire) : 0u;
}

uint64_t state_cell_pressure_observer_uncovered_epoch(
    const StateCellPressureObserver *observer) {
    return observer ? atomic_load_explicit(&observer->uncovered_epoch,
                                           memory_order_acquire) : 0u;
}

bool state_cell_pressure_observer_notice(const StateCellPressureObserver *observer,
    uint64_t expected_epoch, StateCellPressureNotice *notice) {
    if (!observer || !notice)
        return false;
    pthread_mutex_lock(&registry_mutex);
    uint64_t epoch = atomic_load_explicit(&observer->epoch, memory_order_relaxed);
    bool matches = epoch == expected_epoch;
    if (matches)
        *notice = (StateCellPressureNotice){.epoch = epoch,
            .topology_epoch = observer->topology_epoch,
            .exposure_bytes = observer->exposure_bytes,
            .saturated = epoch == UINT64_MAX || observer->topology_epoch == UINT64_MAX ||
                observer->exposure_bytes == SIZE_MAX};
    pthread_mutex_unlock(&registry_mutex);
    return matches;
}

void state_cell_pressure_observer_begin(StateCellPressureObserver *observer) {
    if (!observer)
        return;
    pthread_mutex_lock(&registry_mutex);
    pressure_clear_set_locked(&observer->pending);
    observer->collecting = true;
    observer->pending_invalid = false;
    pthread_mutex_unlock(&registry_mutex);
}

bool state_cell_pressure_observer_watch(StateCellPressureObserver *observer,
                                         StateCell *cell,
                                         StateCellSnapshot *snapshot,
                                         size_t *node_control_bytes) {
    if (snapshot)
        *snapshot = (StateCellSnapshot){0};
    if (node_control_bytes)
        *node_control_bytes = 0u;
    if (!observer || !snapshot || !node_control_bytes)
        return false;
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    if (!observer->collecting || !node || node->retiring ||
        node->publication_epoch == UINT64_MAX) {
        if (observer->collecting)
            observer->pending_invalid = true;
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    StateCellPressureWatch *watch = NULL;
    bool accepted = false;
    for (StateCellPressureWatch *at = node->watches; at; at = at->node_next)
        if (at->observer == observer) {
            if (at->pending)
                watch = at;
            else
                accepted = true;
        }
    if (watch && watch->publication_epoch != node->publication_epoch) {
        observer->pending_invalid = true;
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    if (!watch) {
        watch = pressure_watch_new_locked(observer, node, true, accepted);
        if (!watch) {
            observer->pending_invalid = true;
            pthread_mutex_unlock(&registry_mutex);
            return false;
        }
    }
    if (node->current) {
        image_retain_locked(node->current);
        *snapshot = (StateCellSnapshot){.value = node->current->value,
            .content_type = node->current->content_type, .image = node->current};
    }
    *node_control_bytes = sizeof(*node);
    pthread_mutex_unlock(&registry_mutex);
    return true;
}

bool state_cell_pressure_observer_finish(StateCellPressureObserver *observer,
                                          uint64_t *accepted_epoch) {
    if (!observer || !accepted_epoch)
        return false;
    pthread_mutex_lock(&registry_mutex);
    bool valid = observer->collecting && !observer->pending_invalid &&
        atomic_load_explicit(&observer->epoch, memory_order_relaxed) != UINT64_MAX;
    for (StateCellPressureWatch *watch = observer->pending; valid && watch;
         watch = watch->set_next)
        valid = !watch->node->retiring &&
            watch->node->publication_epoch == watch->publication_epoch &&
            watch->publication_epoch != UINT64_MAX;
    if (valid) {
        pressure_clear_set_locked(&observer->accepted);
        observer->accepted = observer->pending;
        observer->pending = NULL;
        if (observer->accepted)
            observer->accepted->set_link = &observer->accepted;
        for (StateCellPressureWatch *watch = observer->accepted; watch; watch = watch->set_next) {
            watch->pending = false;
            watch->control_charged = true;
        }
        observer->collecting = false;
        *accepted_epoch = atomic_load_explicit(&observer->epoch, memory_order_relaxed);
    }
    pthread_mutex_unlock(&registry_mutex);
    return valid;
}

void state_cell_pressure_observer_cancel(StateCellPressureObserver *observer) {
    if (!observer)
        return;
    pthread_mutex_lock(&registry_mutex);
    pressure_clear_set_locked(&observer->pending);
    observer->collecting = false;
    observer->pending_invalid = false;
    pthread_mutex_unlock(&registry_mutex);
}

size_t state_cell_pressure_observer_bytes(const StateCellPressureObserver *observer) {
    if (!observer)
        return 0u;
    pthread_mutex_lock(&registry_mutex);
    size_t records = observer->watch_count > SIZE_MAX / sizeof(StateCellPressureWatch)
        ? SIZE_MAX : observer->watch_count * sizeof(StateCellPressureWatch);
    size_t bytes = size_add(sizeof(*observer), records);
    pthread_mutex_unlock(&registry_mutex);
    return bytes;
}

bool state_cell_pressure_observer_ensure(StateCellPressureObserver *observer,
    StateCell *cell, bool *fresh_member) {
    if (fresh_member)
        *fresh_member = false;
    if (!observer || !fresh_member)
        return false;
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(cell);
    if (observer->collecting || !node || node->retiring ||
        node->publication_epoch == UINT64_MAX) {
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    StateCellPressureWatch *watch = node->watches;
    while (watch && (watch->observer != observer || watch->pending))
        watch = watch->node_next;
    if (!watch) {
        watch = pressure_watch_new_locked(observer, node, false, false);
        if (!watch) {
            pthread_mutex_unlock(&registry_mutex);
            return false;
        }
        pressure_enqueue_locked(watch);
        *fresh_member = true;
    }
    pthread_mutex_unlock(&registry_mutex);
    return true;
}

bool state_cell_pressure_observer_next(StateCellPressureObserver *observer,
    StateCellPressureDelta *delta) {
    if (delta)
        *delta = (StateCellPressureDelta){0};
    if (!observer || !delta)
        return false;
    pthread_mutex_lock(&registry_mutex);
    if (observer->collecting) {
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    StateCellPressureWatch *watch = observer->dirty_head;
    while (watch && watch->node->retiring) {
        pressure_watch_unlink_locked(watch);
        watch = observer->dirty_head;
    }
    if (!watch) {
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    uint64_t capture = pressure_identifier_locked(observer);
    if (!capture) {
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    pressure_dequeue_locked(watch);
    watch->inflight_capture = capture;
    ManagedStateNode *node = watch->node;
    *delta = (StateCellPressureDelta){.cell = &node->cell,
        .watch_id = watch->identifier, .capture_id = capture,
        .publication_epoch = node->publication_epoch,
        .node_control_bytes = sizeof(*node), .fresh_node = !watch->control_charged};
    watch->control_charged = true;
    if (node->current) {
        image_retain_locked(node->current);
        delta->snapshot = (StateCellSnapshot){.image = node->current,
            .value = node->current->value, .content_type = node->current->content_type};
    }
    pthread_mutex_unlock(&registry_mutex);
    return true;
}

bool state_cell_pressure_observer_settle(StateCellPressureObserver *observer,
    const StateCellPressureDelta *delta, bool edges_discovered) {
    if (!observer || !delta || !delta->cell || !delta->watch_id || !delta->capture_id)
        return false;
    pthread_mutex_lock(&registry_mutex);
    ManagedStateNode *node = find_node(delta->cell);
    StateCellPressureWatch *watch = node ? node->watches : NULL;
    while (watch && (watch->observer != observer || watch->pending ||
                     watch->identifier != delta->watch_id))
        watch = watch->node_next;
    if (!watch || watch->inflight_capture != delta->capture_id) {
        pthread_mutex_unlock(&registry_mutex);
        return false;
    }
    watch->inflight_capture = 0u;
    bool complete = edges_discovered && !node->retiring &&
        node->publication_epoch != UINT64_MAX &&
        node->publication_epoch == delta->publication_epoch;
    if (complete)
        watch->publication_epoch = delta->publication_epoch;
    else
        pressure_enqueue_locked(watch);
    pthread_mutex_unlock(&registry_mutex);
    return complete;
}

StateCellDiagnostics state_cell_diagnostics(void) {
    pthread_mutex_lock(&registry_mutex);
    StateCellDiagnostics result = {.nodes = node_count, .images = image_count,
        .image_bytes = image_bytes, .queued = pending_count,
        .registry_height = height(registry)};
    pthread_mutex_unlock(&registry_mutex);
    return result;
}

StateCellStorageDiagnostics state_cell_storage_diagnostics(void) {
    pthread_mutex_lock(&registry_mutex);
    size_t nodes = node_count > SIZE_MAX / sizeof(ManagedStateNode)
        ? SIZE_MAX : node_count * sizeof(ManagedStateNode);
    StateCellStorageDiagnostics result = {.live_image_bytes = image_bytes,
        .dying_image_bytes = dying_image_bytes, .peak_image_bytes = peak_image_bytes,
        .live_node_bytes = nodes, .saturated = image_bytes == SIZE_MAX ||
            dying_image_bytes == SIZE_MAX || peak_image_bytes == SIZE_MAX ||
            nodes == SIZE_MAX};
    pthread_mutex_unlock(&registry_mutex);
    return result;
}
