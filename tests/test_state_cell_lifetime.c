#include "state_cell.h"
#include "symbol.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Include the production implementation to check the actual AVL metadata,
 * rather than trusting its exported height as its own oracle. */
#define CETTA_STATE_CELL_TEST_HOOKS 1
#include "../src/state_cell.c"

void *__real_cetta_fallible_calloc(size_t, size_t);
void *__real_cetta_fallible_malloc(size_t);
void __real_free(void *);
StateCellLease __real_state_cell_lease_into(Arena *, StateCell *);
static size_t fail_after;
static size_t fail_state_hold_after;
static bool reuse_enabled, reuse_available;
static void *reuse_slot;
static _Atomic size_t notifications;
static pthread_mutex_t zero_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t zero_condition = PTHREAD_COND_INITIALIZER;
static _Atomic(StateCellImage *) pause_zero_image;
static bool zero_paused, zero_resume;

void state_cell_test_image_zero_before_unlink(StateCellImage *image) {
    if (atomic_load_explicit(&pause_zero_image, memory_order_acquire) != image)
        return;
    pthread_mutex_lock(&zero_mutex);
    zero_paused = true;
    pthread_cond_broadcast(&zero_condition);
    while (!zero_resume)
        pthread_cond_wait(&zero_condition, &zero_mutex);
    pthread_mutex_unlock(&zero_mutex);
}

static bool refuse_allocation(void) {
    return fail_after && --fail_after == 0u;
}

void *__wrap_cetta_fallible_calloc(size_t count, size_t width) {
    if (refuse_allocation())
        return NULL;
    if (reuse_enabled && count == 1u && width == sizeof(ManagedStateNode)) {
        if (!reuse_slot)
            reuse_slot = __real_cetta_fallible_calloc(count, width);
        assert(reuse_slot && reuse_available);
        reuse_available = false;
        memset(reuse_slot, 0, width);
        return reuse_slot;
    }
    return __real_cetta_fallible_calloc(count, width);
}

void *__wrap_cetta_fallible_malloc(size_t bytes) {
    return refuse_allocation() ? NULL : __real_cetta_fallible_malloc(bytes);
}

void __wrap_free(void *memory) {
    if (memory && memory == reuse_slot) {
        assert(!reuse_available);
        reuse_available = true;
    } else {
        __real_free(memory);
    }
}

bool __wrap_eval_resource_descriptor_hold(Arena *arena, GroundedKind kind,
                                            void *descriptor) {
    if (kind == GV_STATE && fail_state_hold_after && --fail_state_hold_after == 0u)
        return false;
    return kind != GV_STATE ||
        state_cell_lease_into(arena, descriptor) != STATE_CELL_CAPACITY;
}

StateCellLease __wrap_state_cell_lease_into(Arena *arena, StateCell *cell) {
    if (fail_state_hold_after && --fail_state_hold_after == 0u)
        return STATE_CELL_CAPACITY;
    return __real_state_cell_lease_into(arena, cell);
}

void __wrap_eval_resource_retirement_notify(void) {
    atomic_fetch_add_explicit(&notifications, 1u, memory_order_relaxed);
    /* Also proves notification does not run under the registry mutex. */
    (void)state_cell_diagnostics();
}

static void drain(void) {
    size_t attempts = 0u;
    while (state_cell_reclamation_due()) {
        state_cell_reclaim_bounded(7u);
        assert(++attempts < 100000u);
    }
}

static void empty_registry(void) {
    drain();
    StateCellDiagnostics d = state_cell_diagnostics();
    assert(d.nodes == 0u && d.images == 0u && d.queued == 0u &&
           d.image_bytes == 0u && d.registry_height == 0u);
}

static StateCellImage *integer_image(int64_t value, int64_t type, bool canonical) {
    Arena source;
    arena_init_detached(&source);
    StateCellImage *image = state_cell_image_prepare(atom_int(&source, value),
        atom_int(&source, type), canonical);
    arena_free(&source);
    assert(image);
    return image;
}

static StateCell *new_at_reused_address(StateCellImage *image) {
    /* Restrict the allocator override to the node constructor. A different
     * control record may legitimately have the same sizeof as a node. */
    assert(!reuse_enabled);
    reuse_enabled = true;
    StateCell *cell = state_cell_managed_new(image);
    reuse_enabled = false;
    return cell;
}

static void aliases_and_snapshots(void) {
    StateCell *cell = state_cell_managed_new(integer_image(17, 101, false));
    assert(cell && state_cell_is_managed(cell));
    Arena first, copied, escaped;
    arena_init_detached(&first);
    arena_init_detached(&copied);
    arena_init_detached(&escaped);
    Atom *a = atom_state(&first, cell);
    Atom *b = atom_deep_copy(&copied, a);
    assert(a && b && a->ground.ptr == b->ground.ptr);
    state_cell_creator_release(cell);
    StateCellSnapshot before;
    assert(state_cell_snapshot_acquire(cell, &before));
    assert(state_cell_publish_prepared(cell, integer_image(23, 103, true)));
    StateCellSnapshot now;
    assert(state_cell_snapshot_into(&escaped, cell, &now) == STATE_CELL_HELD);
    assert(!now.image && now.value->ground.ival == 23 && now.content_type->ground.ival == 103);
    assert(before.value->ground.ival == 17 && before.content_type->ground.ival == 101);
    arena_free(&first);
    assert(state_cell_is_managed(cell));
    arena_free(&copied);
    drain();
    assert(!state_cell_is_managed(cell));
    assert(now.value->ground.ival == 23 && before.value->ground.ival == 17);
    assert(state_cell_diagnostics().images == 2u);
    state_cell_snapshot_release(&before);
    assert(state_cell_diagnostics().images == 1u);
    arena_free(&escaped);
    empty_registry();
}

static void revival_and_unmanaged(void) {
    StateCell legacy = {0};
    assert(!state_cell_is_managed(&legacy));
    assert(!state_cell_is_managed((StateCell *)(uintptr_t)1u));
    Arena holder;
    arena_init_detached(&holder);
    assert(state_cell_lease_into(&holder, &legacy) == STATE_CELL_UNMANAGED);
    StateCellSnapshot missing;
    assert(!state_cell_snapshot_acquire(&legacy, &missing));
    StateCell *cell = state_cell_managed_new(NULL);
    assert(cell && !state_cell_snapshot_acquire(cell, &missing));
    state_cell_creator_release(cell);
    assert(state_cell_reclamation_due() && state_cell_is_managed(cell));
    assert(state_cell_lease_into(&holder, cell) == STATE_CELL_HELD);
    assert(state_cell_reclaim_bounded(1u) == 0u);
    assert(!state_cell_reclamation_due() && state_cell_is_managed(cell));
    assert(state_cell_publish_prepared(cell, integer_image(31, 7, false)));
    arena_free(&holder);
    assert(state_cell_reclaim_bounded(1u) == 1u);
    assert(!state_cell_is_managed(cell));
    empty_registry();
}

typedef struct { size_t holds, retains, releases; } Authority;
static void authority_retain(void *p) {
    Authority *owner = p;
    owner->holds++;
    owner->retains++;
}
static void authority_release(void *p) {
    Authority *owner = p;
    assert(owner->holds);
    owner->holds--;
    owner->releases++;
}

static void payload_owners_and_binary_strings(void) {
    for (unsigned canonical = 0u; canonical < 2u; canonical++) {
        Authority owner = {0};
        Arena source, escaped;
        arena_init_detached(&source);
        arena_init_detached(&escaped);
        Atom *handle = atom_native_handle_identifier(&source, 73, &owner,
            authority_retain, authority_release);
        StateCellImage *image = state_cell_image_prepare(handle, handle, canonical);
        assert(image && atom_eq(image->value, image->content_type) &&
               atom_native_handle_identifier_matches(image->value, &owner,
                   authority_retain, authority_release) &&
               atom_native_handle_identifier_matches(image->content_type, &owner,
                   authority_retain, authority_release));
        StateCell *cell = state_cell_managed_new(image);
        assert(cell);
        arena_free(&source);
        assert(owner.holds > 0u);
        StateCellSnapshot old;
        assert(state_cell_snapshot_acquire(cell, &old));
        assert(state_cell_snapshot_retain_into(&escaped, &old));
        assert(atom_native_handle_identifier_matches(old.value, &owner,
            authority_retain, authority_release));
        arena_init_detached(&source);
        char bytes[8192];
        for (size_t i = 0u; i < sizeof(bytes); i++)
            bytes[i] = (char)(i % 251u);
        image = state_cell_image_prepare(atom_string_n(&source, bytes, sizeof(bytes)),
            atom_symbol(&source, "String"), canonical);
        assert(image && state_cell_publish_prepared(cell, image));
        arena_free(&source);
        StateCellSnapshot current;
        assert(state_cell_snapshot_acquire(cell, &current));
        assert(current.value->ground.gkind == GV_STRING &&
               current.value->ground.slen == sizeof(bytes) &&
               memcmp(current.value->ground.sval, bytes, sizeof(bytes)) == 0);
        state_cell_creator_release(cell);
        drain();
        assert(owner.holds > 0u);
        state_cell_snapshot_release(&old);
        assert(owner.holds > 0u);
        arena_free(&escaped);
        assert(!owner.holds && owner.retains == owner.releases);
        assert(memcmp(current.value->ground.sval, bytes, sizeof(bytes)) == 0);
        state_cell_snapshot_release(&current);
        empty_registry();
    }
}

static void overwrite_storage(void) {
    StateCell *cell = state_cell_managed_new(integer_image(0, 1, false));
    assert(cell);
    StateCellSnapshot first;
    assert(state_cell_snapshot_acquire(cell, &first));
    size_t maximum = 0u;
    for (int64_t i = 1; i <= 40000; i++) {
        assert(state_cell_publish_prepared(cell, integer_image(i, 1, false)));
        StateCellDiagnostics d = state_cell_diagnostics();
        assert(d.nodes == 1u && d.images == 2u);
        if (d.image_bytes > maximum)
            maximum = d.image_bytes;
    }
    assert(maximum < 8192u && first.value->ground.ival == 0);
    StateCellSnapshot final;
    assert(state_cell_snapshot_acquire(cell, &final));
    assert(final.value->ground.ival == 40000);
    state_cell_snapshot_release(&final);
    state_cell_snapshot_release(&first);
    state_cell_creator_release(cell);
    empty_registry();
}

static uint64_t random_state = UINT64_C(0x9e3779b97f4a7c15);
static uint64_t random_word(void) {
    random_state ^= random_state << 13u;
    random_state ^= random_state >> 7u;
    random_state ^= random_state << 17u;
    return random_state;
}

static size_t validate_tree(ManagedStateNode *node, uintptr_t lower,
                             uintptr_t upper, uint32_t *computed) {
    if (!node) {
        *computed = 0u;
        return 0u;
    }
    uintptr_t key = (uintptr_t)&node->cell;
    assert(lower < key && key < upper);
    uint32_t left, right;
    size_t count = 1u + validate_tree(node->left, lower, key, &left) +
        validate_tree(node->right, key, upper, &right);
    *computed = (left > right ? left : right) + 1u;
    assert(node->height == *computed && left <= right + 1u && right <= left + 1u);
    return count;
}

static void directory_oracle(void) {
    enum { COUNT = 4096, STEPS = 100000 };
    StateCell *model[COUNT] = {0};
    size_t expected = 0u;
    for (size_t step = 0u; step < STEPS; step++) {
        size_t slot = random_word() % COUNT;
        if (model[slot]) {
            StateCell *removed = model[slot];
            state_cell_creator_release(removed);
            model[slot] = NULL;
            expected--;
            drain();
            assert(!state_cell_is_managed(removed));
        } else {
            model[slot] = state_cell_managed_new(NULL);
            assert(model[slot]);
            expected++;
        }
        if (step % 97u == 0u) {
            uint32_t actual_height;
            size_t count = validate_tree(registry, 0u, UINTPTR_MAX, &actual_height);
            assert(count == expected && state_cell_diagnostics().nodes == expected);
            assert(actual_height < 32u);
            for (size_t i = 0u; i < COUNT; i++)
                if (model[i])
                    assert(state_cell_is_managed(model[i]));
        }
    }
    for (size_t i = 0u; i < COUNT; i++)
        if (model[i])
            state_cell_creator_release(model[i]);
    empty_registry();
}

static void pointer_reuse_and_saturation(void) {
    reuse_available = true;
    StateCell *old = new_at_reused_address(NULL);
    assert(old);
    state_cell_creator_release(old);
    drain();
    assert(!state_cell_is_managed(old) && reuse_available);
    StateCell *fresh = new_at_reused_address(NULL);
    assert(fresh == old && state_cell_is_managed(fresh));
    assert(!fresh->value && !fresh->content_type);
    ManagedStateNode *node = (ManagedStateNode *)fresh;
    node->references = UINT64_MAX - 1u;
    void *pin = state_cell_identity_pin(fresh);
    assert(node->references == UINT64_MAX);
    state_cell_identity_unpin(pin);
    state_cell_creator_release(fresh);
    assert(node->references == UINT64_MAX && !state_cell_reclamation_due());
    StateCellGraphSnapshot *graph = state_cell_graph_snapshot_acquire();
    bool selected = true;
    uint64_t saturated = UINT64_MAX;
    assert(graph && state_cell_graph_nodes(graph)[0].saturated &&
        !state_cell_graph_retire(graph, &selected, NULL, &saturated, NULL));
    state_cell_graph_snapshot_release(graph);
    node->references = 1u;
    state_cell_creator_release(fresh);
    drain();
    assert(reuse_available);
    reuse_enabled = false;
    void *slot = reuse_slot;
    reuse_slot = NULL;
    __real_free(slot);
    StateCellImage *image = integer_image(17, 1, false);
    atomic_store(&image->references, UINT64_MAX - 1u);
    state_cell_image_retain(image);
    state_cell_image_release(image);
    assert(atomic_load(&image->references) == UINT64_MAX);
    graph = state_cell_graph_snapshot_acquire();
    assert(graph && state_cell_graph_images(graph)[0].saturated &&
        !state_cell_graph_retire(graph, NULL, &selected, NULL, &saturated));
    state_cell_graph_snapshot_release(graph);
    atomic_store(&image->references, 1u);
    state_cell_image_release(image);
    empty_registry();
}

static void preparation_refusal(void) {
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    assert(cell);
    Arena source;
    arena_init_detached(&source);
    Atom *value = atom_int(&source, 23), *type = atom_int(&source, 1);
    for (size_t failure = 1u; failure <= 1u; failure++) {
        fail_after = failure;
        StateCellImage *image = state_cell_image_prepare(value, type, false);
        fail_after = 0u;
        assert(!image);
        StateCellSnapshot snapshot;
        assert(state_cell_snapshot_acquire(cell, &snapshot));
        assert(snapshot.value->ground.ival == 17);
        state_cell_snapshot_release(&snapshot);
        assert(state_cell_diagnostics().nodes == 1u && state_cell_diagnostics().images == 1u);
    }
    StateCell *child = state_cell_managed_new(integer_image(19, 1, false));
    assert(child);
    Atom *child_ref = atom_state(&source, child);
    Atom *child_type = atom_state(&source, child);
    uint64_t before_refs = ((ManagedStateNode *)child)->references;
    fail_state_hold_after = 2u;
    assert(!state_cell_image_prepare(child_ref, child_type, false));
    fail_state_hold_after = 0u;
    assert(((ManagedStateNode *)child)->references == before_refs &&
           state_cell_diagnostics().images == 2u);
    state_cell_creator_release(child);
    StateCellImage *image = state_cell_image_prepare(value, type, false);
    assert(image);
    fail_after = 1u;
    assert(!state_cell_managed_new(image));
    fail_after = 0u;
    assert(atomic_load(&image->references) == 1u);
    StateCell legacy = {0};
    assert(!state_cell_publish_prepared(&legacy, image));
    assert(atomic_load(&image->references) == 1u);
    assert(state_cell_publish_prepared(cell, image));
    arena_free(&source);
    state_cell_creator_release(cell);
    empty_registry();
}

typedef struct { StateCell *cell; _Atomic bool done; } Concurrent;
static void *publish_pairs(void *argument) {
    Concurrent *test = argument;
    for (int64_t i = 1; i <= 10000; i++)
        assert(state_cell_publish_prepared(test->cell, integer_image(i, i ^ 85, false)));
    atomic_store_explicit(&test->done, true, memory_order_release);
    return NULL;
}
static void *read_pairs(void *argument) {
    Concurrent *test = argument;
    size_t reads = 0u;
    do {
        StateCellSnapshot snapshot;
        assert(state_cell_snapshot_acquire(test->cell, &snapshot));
        assert(snapshot.content_type->ground.ival == (snapshot.value->ground.ival ^ 85));
        state_cell_snapshot_release(&snapshot);
        reads++;
    } while (reads < 1000u || !atomic_load_explicit(&test->done, memory_order_acquire));
    return NULL;
}

static void concurrent_value_type_pair(void) {
    Concurrent test = {.cell = state_cell_managed_new(integer_image(0, 85, false))};
    assert(test.cell);
    pthread_t writer, reader;
    assert(!pthread_create(&writer, NULL, publish_pairs, &test));
    assert(!pthread_create(&reader, NULL, read_pairs, &test));
    assert(!pthread_join(writer, NULL) && !pthread_join(reader, NULL));
    state_cell_creator_release(test.cell);
    empty_registry();
}

typedef struct {
    StateCell *a, *b;
    size_t nodes, images, edges;
} CycleCensus;

static void census_node(void *context, StateCell *cell, const StateCellImage *image,
                         uint64_t references, bool saturated) {
    CycleCensus *census = context;
    assert((cell == census->a || cell == census->b) && image &&
           references == 1u && !saturated);
    assert(cell->value == image->value && cell->content_type == image->content_type);
    census->nodes++;
}

static void census_edge(void *context, void *owner, void (*release)(void *)) {
    CycleCensus *census = context;
    StateCell *cell;
    StateCellImage *image;
    assert(state_cell_classify_owner(owner, release, &cell, &image));
    assert(cell && !image && (cell == census->a || cell == census->b));
    census->edges++;
}

static void census_image(void *context, const StateCellImage *image,
                          const Arena *syntax, uint64_t references, bool saturated) {
    CycleCensus *census = context;
    assert(image && syntax && !syntax->resource_scope && references == 1u && !saturated);
    state_cell_image_visit_owners(image, census_edge, context);
    census->images++;
}

static void cyclic_clone_identity(void) {
    StateCell *a = state_cell_managed_new(NULL), *b = state_cell_managed_new(NULL);
    assert(a && b && a != b);
    Arena source;
    arena_init_detached(&source);
    StateCellImage *ia = state_cell_image_prepare(atom_state(&source, b),
        atom_symbol(&source, "State"), false);
    StateCellImage *ib = state_cell_image_prepare(atom_state(&source, a),
        atom_symbol(&source, "State"), false);
    assert(ia && ib && state_cell_publish_prepared(a, ia) &&
           state_cell_publish_prepared(b, ib));
    arena_free(&source);
    state_cell_creator_release(a);
    state_cell_creator_release(b);
    assert(!state_cell_reclamation_due());
    CycleCensus census = {.a = a, .b = b};
    state_cell_visit_managed(census_node, &census);
    state_cell_visit_images(census_image, &census);
    assert(census.nodes == 2u && census.images == 2u && census.edges == 2u);
    StateCell *unknown_cell = a;
    StateCellImage *unknown_image = ia;
    assert(!state_cell_classify_owner((void *)(uintptr_t)1u, authority_release,
        &unknown_cell, &unknown_image) && !unknown_cell && !unknown_image);
    StateCellSnapshot snapshot;
    assert(state_cell_snapshot_acquire(a, &snapshot) && snapshot.value->ground.ptr == b);
    state_cell_snapshot_release(&snapshot);
    /* The reference-only backend represents the cycle. Its complete census
     * is separate; explicitly break both edges while independent pins live. */
    void *pa = state_cell_identity_pin(a), *pb = state_cell_identity_pin(b);
    assert(pa && pb);
    assert(state_cell_publish_prepared(a, integer_image(17, 1, false)));
    assert(state_cell_publish_prepared(b, integer_image(19, 1, false)));
    state_cell_identity_unpin(pa);
    state_cell_identity_unpin(pb);
    empty_registry();
}

static void repeated_self_reference_storage(void) {
    StateCell *cell = state_cell_managed_new(NULL);
    assert(cell);
    Arena source;
    arena_init_detached(&source);
    Atom *self = atom_state(&source, cell);
    Atom *type = atom_symbol(&source, "State");
    size_t first_bytes = 0u;
    for (unsigned i = 0u; i < 4000u; i++) {
        StateCellImage *image = state_cell_image_prepare(self, type, false);
        assert(image && state_cell_publish_prepared(cell, image));
        size_t current_bytes = state_cell_image_bytes(image);
        if (!i)
            first_bytes = current_bytes;
        assert(current_bytes == first_bytes);
        assert(state_cell_diagnostics().nodes == 1u && state_cell_diagnostics().images == 1u);
    }
    assert(first_bytes < 4096u);
    assert(state_cell_publish_prepared(cell, integer_image(17, 1, false)));
    arena_free(&source);
    state_cell_creator_release(cell);
    empty_registry();
}

enum { GRAPH_TEST_LIMIT = 16 };
typedef struct {
    StateCellGraphSnapshot *snapshot;
    size_t nodes, images, from;
    bool edges[GRAPH_TEST_LIMIT][GRAPH_TEST_LIMIT];
    bool live[GRAPH_TEST_LIMIT];
    bool dead_nodes[GRAPH_TEST_LIMIT], dead_images[GRAPH_TEST_LIMIT];
    uint64_t incoming_nodes[GRAPH_TEST_LIMIT], incoming_images[GRAPH_TEST_LIMIT];
} GraphOracle;

static size_t graph_cell_index(const GraphOracle *graph, StateCell *cell) {
    const StateCellGraphNode *nodes = state_cell_graph_nodes(graph->snapshot);
    for (size_t i = 0u; i < graph->nodes; i++)
        if (nodes[i].cell == cell)
            return i;
    return SIZE_MAX;
}

static size_t graph_image_index(const GraphOracle *graph, const StateCellImage *image) {
    const StateCellGraphImage *images = state_cell_graph_images(graph->snapshot);
    for (size_t i = 0u; i < graph->images; i++)
        if (images[i].image == image)
            return graph->nodes + i;
    return SIZE_MAX;
}

static void graph_oracle_owner(void *context, void *owner, void (*release)(void *)) {
    GraphOracle *graph = context;
    StateCell *cell;
    StateCellImage *image;
    if (!state_cell_classify_owner(owner, release, &cell, &image)) {
        graph->live[graph->from] = true;
        return;
    }
    size_t target = cell ? graph_cell_index(graph, cell) : graph_image_index(graph, image);
    if (target == SIZE_MAX) {
        graph->live[graph->from] = true;
        return;
    }
    assert(!graph->edges[graph->from][target]);
    graph->edges[graph->from][target] = true;
    if (target < graph->nodes)
        graph->incoming_nodes[target]++;
    else
        graph->incoming_images[target - graph->nodes]++;
}

static void graph_oracle_capture(GraphOracle *graph) {
    *graph = (GraphOracle){0};
    graph->snapshot = state_cell_graph_snapshot_acquire();
    assert(graph->snapshot);
    graph->nodes = state_cell_graph_node_count(graph->snapshot);
    graph->images = state_cell_graph_image_count(graph->snapshot);
    assert(graph->nodes + graph->images < GRAPH_TEST_LIMIT);
    const StateCellGraphNode *nodes = state_cell_graph_nodes(graph->snapshot);
    const StateCellGraphImage *images = state_cell_graph_images(graph->snapshot);
    for (size_t i = 0u; i < graph->nodes; i++) {
        if (!nodes[i].current_image)
            continue;
        size_t target = graph_image_index(graph, nodes[i].current_image);
        assert(target != SIZE_MAX);
        graph->edges[i][target] = true;
        graph->incoming_images[target - graph->nodes]++;
    }
    for (size_t i = 0u; i < graph->images; i++) {
        graph->from = graph->nodes + i;
        state_cell_image_visit_owners(images[i].image, graph_oracle_owner, graph);
    }
    for (size_t i = 0u; i < graph->nodes; i++)
        graph->live[i] = graph->live[i] || nodes[i].saturated ||
            nodes[i].references != graph->incoming_nodes[i];
    for (size_t i = 0u; i < graph->images; i++)
        graph->live[graph->nodes + i] = graph->live[graph->nodes + i] ||
            images[i].saturated || images[i].references != graph->incoming_images[i];
    bool changed;
    do {
        changed = false;
        for (size_t i = 0u; i < graph->nodes + graph->images; i++)
            for (size_t j = 0u; j < graph->nodes + graph->images; j++)
                if (graph->live[i] && graph->edges[i][j] && !graph->live[j]) {
                    graph->live[j] = true;
                    changed = true;
                }
    } while (changed);
    for (size_t i = 0u; i < graph->nodes; i++)
        graph->dead_nodes[i] = !graph->live[i];
    for (size_t i = 0u; i < graph->images; i++)
        graph->dead_images[i] = !graph->live[graph->nodes + i];
}

static bool graph_oracle_retire(GraphOracle *graph) {
    return state_cell_graph_retire(graph->snapshot, graph->dead_nodes,
        graph->dead_images, graph->incoming_nodes, graph->incoming_images);
}

static StateCell *make_unrooted_self_cycle(void) {
    StateCell *cell = state_cell_managed_new(NULL);
    assert(cell);
    Arena source;
    arena_init_detached(&source);
    StateCellImage *image = state_cell_image_prepare(atom_state(&source, cell),
        atom_int(&source, 1), false);
    assert(image && state_cell_publish_prepared(cell, image));
    arena_free(&source);
    state_cell_creator_release(cell);
    return cell;
}

static void pinned_self_cycle_retirement(void) {
    StateCell *cell = make_unrooted_self_cycle();
    GraphOracle graph;
    graph_oracle_capture(&graph);
    assert(graph.nodes == 1u && graph.images == 1u &&
           graph.dead_nodes[0] && graph.dead_images[0]);
    Atom *before = cell->value;
    /* Omitting the real image->cell edge must conservatively refuse. */
    graph.incoming_nodes[0]--;
    assert(!graph_oracle_retire(&graph) && cell->value == before && cell->value);
    graph.incoming_nodes[0]++;
    assert(graph_oracle_retire(&graph));
    assert(!cell->value && !cell->content_type);
    assert(state_cell_reclaim_bounded(16u) == 0u);
    assert(state_cell_is_managed(cell));
    assert(state_cell_graph_node_count(graph.snapshot) == 1u);
    StateCellGraphSnapshot *later = state_cell_graph_snapshot_acquire();
    assert(later && !state_cell_graph_node_count(later) && !state_cell_graph_image_count(later));
    state_cell_graph_snapshot_release(later);
    state_cell_graph_snapshot_release(graph.snapshot);
    empty_registry();
}

static void mutual_cycle_retirement(void) {
    StateCell *a = state_cell_managed_new(NULL), *b = state_cell_managed_new(NULL);
    Arena source;
    arena_init_detached(&source);
    StateCellImage *ia = state_cell_image_prepare(atom_state(&source, b), atom_int(&source, 1), false);
    StateCellImage *ib = state_cell_image_prepare(atom_state(&source, a), atom_int(&source, 1), false);
    assert(a && b && ia && ib && state_cell_publish_prepared(a, ia) &&
        state_cell_publish_prepared(b, ib));
    arena_free(&source);
    state_cell_creator_release(a);
    state_cell_creator_release(b);
    GraphOracle graph;
    graph_oracle_capture(&graph);
    assert(graph.nodes == 2u && graph.images == 2u && graph.dead_nodes[0] &&
        graph.dead_nodes[1] && graph.dead_images[0] && graph.dead_images[1]);
    assert(graph_oracle_retire(&graph));
    assert(state_cell_reclaim_bounded(16u) == 0u && !a->value && !b->value);
    state_cell_graph_snapshot_release(graph.snapshot);
    empty_registry();
}

static void retained_cycle_reader(void) {
    StateCell *cell = make_unrooted_self_cycle();
    StateCellSnapshot reader;
    assert(state_cell_snapshot_acquire(cell, &reader));
    GraphOracle graph;
    graph_oracle_capture(&graph);
    assert(!graph.dead_nodes[0] && !graph.dead_images[0]);
    bool force[] = {true};
    Atom *before = cell->value;
    assert(!state_cell_graph_retire(graph.snapshot, force, force,
        graph.incoming_nodes, graph.incoming_images));
    assert(cell->value == before && reader.value->ground.ptr == cell);
    state_cell_graph_snapshot_release(graph.snapshot);
    state_cell_snapshot_release(&reader);
    graph_oracle_capture(&graph);
    assert(graph.dead_nodes[0] && graph.dead_images[0] && graph_oracle_retire(&graph));
    state_cell_graph_snapshot_release(graph.snapshot);
    empty_registry();
}

static void image_only_cycle_retirement(void) {
    StateCellImage *a = integer_image(17, 1, false), *b = integer_image(19, 1, false);
    /* Private pre-exposure graph construction uses real known image leases. */
    StateCellSnapshot sa = {.image = a}, sb = {.image = b};
    assert(state_cell_snapshot_retain_into(&a->syntax, &sb));
    assert(state_cell_snapshot_retain_into(&b->syntax, &sa));
    StateCellImage *pair[] = {a, b};
    pthread_mutex_lock(&registry_mutex);
    for (size_t i = 0u; i < 2u; i++) {
        size_t bytes = size_add(sizeof(*pair[i]), size_add(pair[i]->syntax.reserved_bytes,
            pair[i]->syntax.external_bytes - pair[i]->syntax.retained_resource_bytes));
        image_bytes += bytes - pair[i]->bytes;
        pair[i]->bytes = bytes;
    }
    pthread_mutex_unlock(&registry_mutex);
    state_cell_image_release(a);
    state_cell_image_release(b);
    GraphOracle graph;
    graph_oracle_capture(&graph);
    assert(!graph.nodes && graph.images == 2u && graph.dead_images[0] && graph.dead_images[1]);
    assert(graph_oracle_retire(&graph));
    assert(state_cell_diagnostics().images == 2u);
    state_cell_graph_snapshot_release(graph.snapshot);
    empty_registry();
}

static void graph_capture_refusal_and_growth(void) {
    StateCell *cell = make_unrooted_self_cycle();
    ManagedStateNode *node = (ManagedStateNode *)cell;
    for (size_t fail = 1u; fail <= 3u; fail++) {
        fail_after = fail;
        assert(!state_cell_graph_snapshot_acquire());
        fail_after = 0u;
        assert(node->references == 1u && atomic_load(&node->current->references) == 1u);
    }
    GraphOracle graph;
    graph_oracle_capture(&graph);
    StateCellSnapshot late_reader;
    assert(state_cell_snapshot_acquire(cell, &late_reader));
    Atom *before = cell->value;
    assert(!graph_oracle_retire(&graph) && cell->value == before);
    state_cell_snapshot_release(&late_reader);
    assert(graph_oracle_retire(&graph));
    state_cell_graph_snapshot_release(graph.snapshot);
    empty_registry();
}

static void graph_current_image_aba(void) {
    StateCellImage *a = integer_image(17, 1, false);
    StateCell *cell = state_cell_managed_new(a);
    state_cell_creator_release(cell);
    GraphOracle graph;
    graph_oracle_capture(&graph);
    assert(graph.nodes == 1u && graph.images == 1u);
    state_cell_image_retain(a);
    assert(state_cell_publish_prepared(cell, integer_image(23, 2, false)));
    assert(state_cell_publish_prepared(cell, a));
    assert(cell->value->ground.ival == 17 && !graph_oracle_retire(&graph));
    assert(cell->value->ground.ival == 17);
    state_cell_graph_snapshot_release(graph.snapshot);
    empty_registry();
}

static void *release_image_thread(void *argument) {
    state_cell_image_release(argument);
    return NULL;
}

static void dying_zero_image_capture(void) {
    StateCellImage *dying = integer_image(17, 1, false);
    StateCellImage *live = integer_image(19, 1, false);
    zero_paused = zero_resume = false;
    atomic_store(&pause_zero_image, dying);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, release_image_thread, dying));
    pthread_mutex_lock(&zero_mutex);
    while (!zero_paused)
        pthread_cond_wait(&zero_condition, &zero_mutex);
    pthread_mutex_unlock(&zero_mutex);
    StateCellGraphSnapshot *snapshot = state_cell_graph_snapshot_acquire();
    assert(snapshot && !state_cell_graph_node_count(snapshot) &&
        state_cell_graph_image_count(snapshot) == 1u &&
        state_cell_graph_images(snapshot)[0].image == live);
    state_cell_graph_snapshot_release(snapshot);
    pthread_mutex_lock(&zero_mutex);
    zero_resume = true;
    pthread_cond_broadcast(&zero_condition);
    pthread_mutex_unlock(&zero_mutex);
    assert(!pthread_join(thread, NULL));
    atomic_store(&pause_zero_image, NULL);
    state_cell_image_release(live);
    empty_registry();
}

static void state_root_acquisition_marker(void) {
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    Arena arena;
    arena_init_detached(&arena);
    uint64_t marker = arena.state_root_acquisition_generation;
    assert(state_cell_lease_into(&arena, cell) == STATE_CELL_HELD);
    assert(arena.state_root_acquisition_generation == marker + 1u);
    assert(state_cell_lease_into(&arena, cell) == STATE_CELL_HELD);
    assert(arena.state_root_acquisition_generation == marker + 1u);
    StateCellSnapshot snapshot;
    assert(state_cell_snapshot_acquire(cell, &snapshot));
    assert(state_cell_snapshot_retain_into(&arena, &snapshot));
    assert(arena.state_root_acquisition_generation == marker + 2u);
    assert(state_cell_snapshot_retain_into(&arena, &snapshot));
    assert(arena.state_root_acquisition_generation == marker + 2u);
    state_cell_snapshot_release(&snapshot);
    state_cell_creator_release(cell);
    arena_free(&arena);
    empty_registry();
}

static void pressure_watch_one(StateCellPressureObserver *observer, StateCell *cell,
                               int64_t expected, uint64_t *accepted) {
    state_cell_pressure_observer_begin(observer);
    StateCellSnapshot view;
    size_t control = 0u;
    assert(state_cell_pressure_observer_watch(observer, cell, &view, &control));
    assert(control == sizeof(ManagedStateNode) && view.image &&
           view.value->ground.ival == expected);
    state_cell_snapshot_release(&view);
    assert(state_cell_pressure_observer_finish(observer, accepted));
    assert(*accepted == state_cell_pressure_observer_epoch(observer));
}

static void pressure_targeting_and_historical_image(void) {
    StateCellPressureObserver *a = state_cell_pressure_observer_new();
    StateCellPressureObserver *b = state_cell_pressure_observer_new();
    assert(a && b);
    StateCell *first = state_cell_managed_new(integer_image(17, 1, false));
    StateCell *second = state_cell_managed_new(integer_image(19, 2, false));
    uint64_t ae, be;
    pressure_watch_one(a, first, 17, &ae);
    pressure_watch_one(b, second, 19, &be);
    StateCellSnapshot old, exact;
    assert(state_cell_snapshot_acquire(first, &old));
    assert(state_cell_publish_prepared(second, integer_image(23, 3, false)));
    assert(state_cell_pressure_observer_epoch(a) == ae &&
           state_cell_pressure_observer_epoch(b) > be);
    assert(state_cell_publish_prepared(first, integer_image(29, 4, false)));
    assert(state_cell_pressure_observer_epoch(a) > ae);
    assert(state_cell_image_snapshot_acquire(old.image, &exact));
    state_cell_snapshot_release(&old);
    assert(exact.value->ground.ival == 17 && exact.content_type->ground.ival == 1);
    assert(state_cell_snapshot_acquire(first, &old));
    assert(old.value->ground.ival == 29 && old.content_type->ground.ival == 4);
    state_cell_snapshot_release(&old);
    state_cell_snapshot_release(&exact);
    state_cell_creator_release(first);
    state_cell_creator_release(second);
    drain();
    assert(state_cell_pressure_observer_bytes(a) == sizeof(*a) &&
           state_cell_pressure_observer_bytes(b) == sizeof(*b));
    state_cell_pressure_observer_free(a);
    state_cell_pressure_observer_free(b);
    empty_registry();
}

static void pressure_refusal_aba_and_bounded_metadata(void) {
    fail_after = 1u;
    assert(!state_cell_pressure_observer_new());
    fail_after = 0u;
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    assert(observer && cell);
    uint64_t accepted;
    pressure_watch_one(observer, cell, 17, &accepted);
    size_t accepted_bytes = sizeof(*observer) + sizeof(StateCellPressureWatch);
    assert(state_cell_pressure_observer_bytes(observer) == accepted_bytes);
    state_cell_pressure_observer_begin(observer);
    StateCellSnapshot captured;
    size_t control;
    assert(state_cell_pressure_observer_watch(observer, cell, &captured, &control));
    StateCellImage *original = captured.image;
    state_cell_image_retain(original);
    assert(state_cell_publish_prepared(cell, integer_image(23, 2, false)));
    assert(state_cell_publish_prepared(cell, original));
    uint64_t refused_epoch = UINT64_C(0xdeadbeef);
    assert(!state_cell_pressure_observer_finish(observer, &refused_epoch));
    assert(refused_epoch == UINT64_C(0xdeadbeef));
    state_cell_snapshot_release(&captured);
    state_cell_pressure_observer_cancel(observer);
    assert(state_cell_pressure_observer_epoch(observer) > accepted &&
           state_cell_pressure_observer_bytes(observer) == accepted_bytes);

    state_cell_pressure_observer_begin(observer);
    fail_after = 1u;
    assert(!state_cell_pressure_observer_watch(observer, cell, &captured, &control));
    fail_after = 0u;
    assert(!captured.image && !control &&
           !state_cell_pressure_observer_finish(observer, &refused_epoch));
    assert(refused_epoch == UINT64_C(0xdeadbeef));
    state_cell_pressure_observer_cancel(observer);
    assert(state_cell_pressure_observer_bytes(observer) == accepted_bytes);
    for (unsigned i = 0u; i < 1000u; i++) {
        pressure_watch_one(observer, cell, 17, &accepted);
        assert(state_cell_pressure_observer_bytes(observer) == accepted_bytes);
        state_cell_pressure_observer_begin(observer);
        StateCellSnapshot duplicate;
        assert(state_cell_pressure_observer_watch(observer, cell, &captured, &control));
        assert(state_cell_pressure_observer_watch(observer, cell, &duplicate, &control));
        /* Each successful watch returns its own caller-owned image pin. */
        state_cell_snapshot_release(&captured);
        state_cell_snapshot_release(&duplicate);
        assert(state_cell_pressure_observer_bytes(observer) == accepted_bytes +
               sizeof(StateCellPressureWatch));
        state_cell_pressure_observer_cancel(observer);
    }
    state_cell_pressure_observer_begin(observer);
    assert(state_cell_pressure_observer_finish(observer, &accepted));
    assert(state_cell_pressure_observer_bytes(observer) == sizeof(*observer));
    state_cell_creator_release(cell);
    state_cell_pressure_observer_free(observer);
    empty_registry();
}

static void pressure_weak_retirement_and_pointer_reuse(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    assert(observer);
    reuse_available = true;
    StateCell *cell = new_at_reused_address(NULL);
    assert(cell);
    state_cell_pressure_observer_begin(observer);
    StateCellSnapshot view;
    size_t control;
    assert(state_cell_pressure_observer_watch(observer, cell, &view, &control));
    assert(!view.image && !view.value && !view.content_type &&
           control == sizeof(ManagedStateNode));
    uint64_t epoch = state_cell_pressure_observer_epoch(observer);
    state_cell_creator_release(cell);
    drain();
    assert(!state_cell_is_managed(cell) && reuse_available &&
           state_cell_pressure_observer_epoch(observer) > epoch &&
           state_cell_pressure_observer_bytes(observer) == sizeof(*observer));
    StateCell *replacement = new_at_reused_address(integer_image(31, 7, false));
    assert(replacement == cell);
    uint64_t refused_epoch = 77u;
    assert(!state_cell_pressure_observer_finish(observer, &refused_epoch) &&
           refused_epoch == 77u);
    state_cell_pressure_observer_cancel(observer);
    pressure_watch_one(observer, replacement, 31, &epoch);
    assert(((ManagedStateNode *)replacement)->references == 1u);
    state_cell_creator_release(replacement);
    drain();
    assert(reuse_available && state_cell_pressure_observer_epoch(observer) > epoch);
    reuse_enabled = false;
    __real_free(reuse_slot);
    reuse_slot = NULL;
    state_cell_pressure_observer_free(observer);
    empty_registry();
}

static void pressure_saturation_and_observer_free(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    assert(observer && cell);
    uint64_t epoch;
    pressure_watch_one(observer, cell, 17, &epoch);
    atomic_store(&observer->epoch, UINT64_MAX);
    state_cell_pressure_observer_begin(observer);
    StateCellSnapshot view;
    size_t control;
    assert(state_cell_pressure_observer_watch(observer, cell, &view, &control));
    state_cell_snapshot_release(&view);
    epoch = 99u;
    assert(!state_cell_pressure_observer_finish(observer, &epoch) && epoch == 99u);
    state_cell_pressure_observer_cancel(observer);
    assert(state_cell_pressure_observer_epoch(observer) == UINT64_MAX);
    state_cell_pressure_observer_free(observer);
    assert(!((ManagedStateNode *)cell)->watches);
    assert(state_cell_publish_prepared(cell, integer_image(19, 2, false)));
    state_cell_creator_release(cell);
    empty_registry();
}

static void pressure_concurrent_publication(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    Concurrent test = {.cell = state_cell_managed_new(integer_image(0, 85, false))};
    assert(observer && test.cell);
    pthread_t writer;
    assert(!pthread_create(&writer, NULL, publish_pairs, &test));
    size_t reads = 0u;
    do {
        state_cell_pressure_observer_begin(observer);
        StateCellSnapshot view;
        size_t control;
        assert(state_cell_pressure_observer_watch(observer, test.cell, &view, &control));
        assert(view.content_type->ground.ival == (view.value->ground.ival ^ 85));
        state_cell_snapshot_release(&view);
        uint64_t accepted = UINT64_MAX;
        if (state_cell_pressure_observer_finish(observer, &accepted)) {
            assert(accepted != UINT64_MAX &&
                   accepted <= state_cell_pressure_observer_epoch(observer));
        } else {
            assert(accepted == UINT64_MAX);
            state_cell_pressure_observer_cancel(observer);
        }
        assert(state_cell_pressure_observer_bytes(observer) <=
            sizeof(*observer) + 2u * sizeof(StateCellPressureWatch));
        reads++;
    } while (reads < 1000u || !atomic_load_explicit(&test.done, memory_order_acquire));
    assert(!pthread_join(writer, NULL));
    state_cell_creator_release(test.cell);
    drain();
    assert(state_cell_pressure_observer_bytes(observer) == sizeof(*observer));
    state_cell_pressure_observer_free(observer);
    empty_registry();
}

static StateCellPressureNotice pressure_notice(StateCellPressureObserver *observer) {
    uint64_t epoch = state_cell_pressure_observer_epoch(observer);
    StateCellPressureNotice notice;
    assert(state_cell_pressure_observer_notice(observer, epoch, &notice));
    assert(notice.epoch == epoch && !notice.saturated);
    return notice;
}

static void pressure_weighted_notices(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    StateCell *child = state_cell_managed_new(integer_image(19, 2, false));
    assert(observer && cell && child);
    uint64_t accepted;
    pressure_watch_one(observer, cell, 17, &accepted);
    StateCellPressureNotice first = pressure_notice(observer);
    StateCellImage *old = ((ManagedStateNode *)cell)->current;
    state_cell_image_retain(old);
    assert(state_cell_publish_prepared(cell, old));
    StateCellPressureNotice same = pressure_notice(observer);
    assert(same.epoch > first.epoch && same.exposure_bytes == first.exposure_bytes &&
           same.topology_epoch == first.topology_epoch);
    StateCellImage *replacement = integer_image(23, 3, false);
    size_t exposure = old->bytes + replacement->bytes;
    state_cell_pressure_observer_begin(observer);
    StateCellSnapshot view;
    size_t control;
    assert(state_cell_pressure_observer_watch(observer, cell, &view, &control));
    state_cell_snapshot_release(&view);
    assert(state_cell_publish_prepared(cell, replacement));
    StateCellPressureNotice scalar = pressure_notice(observer);
    /* Accepted and pending subscriptions must not double-charge one write. */
    assert(scalar.exposure_bytes == same.exposure_bytes + exposure &&
           scalar.topology_epoch == same.topology_epoch);
    StateCellPressureNotice refused = {.epoch = 11, .topology_epoch = 13,
        .exposure_bytes = 17, .saturated = true};
    assert(!state_cell_pressure_observer_notice(observer, same.epoch, &refused));
    assert(refused.epoch == 11 && refused.topology_epoch == 13 &&
           refused.exposure_bytes == 17 && refused.saturated);
    state_cell_pressure_observer_cancel(observer);
    Arena source;
    arena_init_detached(&source);
    replacement = state_cell_image_prepare(atom_state(&source, child),
        atom_int(&source, 7), false);
    assert(replacement && replacement->may_reference_resources);
    arena_free(&source);
    assert(state_cell_publish_prepared(cell, replacement));
    StateCellPressureNotice topology = pressure_notice(observer);
    assert(topology.topology_epoch > scalar.topology_epoch &&
           topology.exposure_bytes > scalar.exposure_bytes);
    observer->exposure_bytes = SIZE_MAX - 1u;
    assert(state_cell_publish_prepared(cell, integer_image(29, 4, false)));
    uint64_t dirty = state_cell_pressure_observer_epoch(observer);
    assert(dirty == UINT64_MAX &&
        state_cell_pressure_observer_notice(observer, dirty, &refused) &&
        refused.saturated && refused.exposure_bytes == SIZE_MAX);
    state_cell_pressure_observer_free(observer);
    state_cell_creator_release(cell);
    state_cell_creator_release(child);
    empty_registry();
}

typedef struct {
    size_t references, outer_bytes;
    StateCellImage *created_during_finalization;
    bool observed;
} OverlapOwner;

static void overlap_retain(void *context) {
    ((OverlapOwner *)context)->references++;
}

static void overlap_release(void *context) {
    OverlapOwner *owner = context;
    assert(owner->references);
    if (--owner->references)
        return;
    StateCellStorageDiagnostics before = state_cell_storage_diagnostics();
    assert(before.dying_image_bytes >= owner->outer_bytes && !before.saturated);
    owner->observed = true;
    owner->created_during_finalization = integer_image(31, 11, false);
    StateCellStorageDiagnostics during = state_cell_storage_diagnostics();
    assert(during.dying_image_bytes >= owner->outer_bytes &&
           during.live_image_bytes >= owner->created_during_finalization->bytes &&
           during.peak_image_bytes >= owner->outer_bytes +
               owner->created_during_finalization->bytes);
}

static void dying_storage_overlap(void) {
    Arena source;
    arena_init_detached(&source);
    OverlapOwner owner = {0};
    Atom *handle = atom_native_handle_identifier(&source, 73, &owner,
        overlap_retain, overlap_release);
    StateCellImage *image = state_cell_image_prepare(handle, NULL, false);
    assert(image);
    owner.outer_bytes = image->bytes;
    arena_free(&source);
    assert(owner.references && !owner.observed);
    state_cell_image_release(image);
    assert(owner.observed && owner.created_during_finalization && !owner.references);
    StateCellStorageDiagnostics after = state_cell_storage_diagnostics();
    assert(!after.dying_image_bytes &&
           after.live_image_bytes == owner.created_during_finalization->bytes &&
           !after.live_node_bytes);
    state_cell_image_release(owner.created_during_finalization);
    after = state_cell_storage_diagnostics();
    assert(!after.live_image_bytes && !after.dying_image_bytes && !after.live_node_bytes);
    empty_registry();
}

static void delta_discovery_and_coalescing(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    bool fresh;
    assert(observer && cell &&
        state_cell_pressure_observer_ensure(observer, cell, &fresh) && fresh);
    size_t bytes = state_cell_pressure_observer_bytes(observer);
    fail_after = 1u;
    assert(state_cell_pressure_observer_ensure(observer, cell, &fresh) && !fresh);
    assert(fail_after == 1u); /* A known member makes no allocation. */
    fail_after = 0u;
    StateCellPressureDelta delta, empty;
    assert(state_cell_pressure_observer_next(observer, &delta) && delta.fresh_node &&
        delta.cell == cell && delta.watch_id && delta.capture_id &&
        delta.node_control_bytes == sizeof(ManagedStateNode) &&
        delta.snapshot.value->ground.ival == 17 &&
        state_cell_image_syntax(delta.snapshot.image) == &delta.snapshot.image->syntax);
    assert(!state_cell_pressure_observer_next(observer, &empty) && !empty.cell);
    assert(state_cell_pressure_observer_settle(observer, &delta, true));
    state_cell_snapshot_release(&delta.snapshot);
    assert(!state_cell_pressure_observer_next(observer, &empty));
    for (int64_t i = 1; i <= 1000; i++)
        assert(state_cell_publish_prepared(cell, integer_image(i, i ^ 85, false)));
    assert(state_cell_pressure_observer_bytes(observer) == bytes);
    assert(state_cell_pressure_observer_next(observer, &delta) && !delta.fresh_node &&
        delta.snapshot.value->ground.ival == 1000 &&
        delta.snapshot.content_type->ground.ival == (1000 ^ 85));
    assert(!state_cell_pressure_observer_next(observer, &empty));
    assert(state_cell_pressure_observer_settle(observer, &delta, true));
    state_cell_snapshot_release(&delta.snapshot);
    state_cell_creator_release(cell);
    drain();
    assert(!state_cell_pressure_observer_next(observer, &empty));
    state_cell_pressure_observer_free(observer);
    empty_registry();
}

static void delta_retirement_and_node_aba(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    reuse_available = true;
    StateCell *cell = new_at_reused_address(integer_image(17, 1, false));
    bool fresh;
    StateCellPressureDelta old, replacement;
    assert(observer && cell && state_cell_pressure_observer_ensure(observer, cell, &fresh));
    assert(state_cell_pressure_observer_next(observer, &old));
    state_cell_creator_release(cell);
    drain();
    assert(reuse_available && !state_cell_is_managed(cell) &&
        old.snapshot.value->ground.ival == 17);
    StateCell *next = new_at_reused_address(integer_image(23, 2, false));
    assert(next == cell && state_cell_pressure_observer_ensure(observer, next, &fresh) && fresh);
    assert(state_cell_pressure_observer_next(observer, &replacement) &&
        replacement.watch_id != old.watch_id && replacement.capture_id != old.capture_id &&
        replacement.publication_epoch == old.publication_epoch);
    assert(!state_cell_pressure_observer_settle(observer, &old, true));
    assert(state_cell_pressure_observer_settle(observer, &replacement, true));
    state_cell_snapshot_release(&replacement.snapshot);
    state_cell_snapshot_release(&old.snapshot);
    state_cell_creator_release(next);
    drain();
    reuse_enabled = false;
    void *slot = reuse_slot;
    reuse_slot = NULL;
    __real_free(slot);
    state_cell_pressure_observer_free(observer);
    empty_registry();
}

static void delta_retry_and_capture_replay(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    bool fresh;
    StateCellPressureDelta first, retry, current, empty;
    assert(observer && cell && state_cell_pressure_observer_ensure(observer, cell, &fresh));
    assert(state_cell_pressure_observer_next(observer, &first));
    assert(!state_cell_pressure_observer_settle(observer, &first, false));
    assert(state_cell_pressure_observer_next(observer, &retry) && !retry.fresh_node &&
        retry.watch_id == first.watch_id && retry.capture_id != first.capture_id &&
        retry.publication_epoch == first.publication_epoch);
    assert(!state_cell_pressure_observer_settle(observer, &first, true));
    assert(state_cell_publish_prepared(cell, integer_image(23, 2, false)));
    assert(!state_cell_pressure_observer_settle(observer, &retry, true));
    assert(retry.snapshot.value->ground.ival == 17 &&
        state_cell_pressure_observer_next(observer, &current) &&
        current.snapshot.value->ground.ival == 23);
    assert(state_cell_pressure_observer_settle(observer, &current, true));
    state_cell_snapshot_release(&first.snapshot);
    state_cell_snapshot_release(&retry.snapshot);
    state_cell_snapshot_release(&current.snapshot);
    assert(!state_cell_pressure_observer_next(observer, &empty));
    state_cell_creator_release(cell);
    state_cell_pressure_observer_free(observer);
    empty_registry();
}

static void delta_full_trace_and_refusal(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *cell = state_cell_managed_new(integer_image(17, 1, false));
    assert(observer && cell);
    bool fresh = true;
    fail_after = 1u;
    assert(!state_cell_pressure_observer_ensure(observer, cell, &fresh) && !fresh);
    fail_after = 0u;
    assert(state_cell_pressure_observer_bytes(observer) == sizeof(*observer));
    uint64_t accepted;
    pressure_watch_one(observer, cell, 17, &accepted);
    assert(state_cell_pressure_observer_ensure(observer, cell, &fresh) && !fresh);
    StateCellPressureDelta delta, empty;
    assert(!state_cell_pressure_observer_next(observer, &empty));
    assert(state_cell_publish_prepared(cell, integer_image(23, 2, false)));
    state_cell_pressure_observer_begin(observer);
    StateCellSnapshot pending;
    size_t control;
    assert(state_cell_pressure_observer_watch(observer, cell, &pending, &control));
    assert(state_cell_publish_prepared(cell, integer_image(29, 3, false)));
    assert(!state_cell_pressure_observer_finish(observer, &accepted));
    state_cell_snapshot_release(&pending);
    state_cell_pressure_observer_cancel(observer);
    assert(state_cell_pressure_observer_next(observer, &delta) && !delta.fresh_node &&
        delta.snapshot.value->ground.ival == 29);
    pressure_watch_one(observer, cell, 29, &accepted);
    assert(!state_cell_pressure_observer_settle(observer, &delta, true));
    state_cell_snapshot_release(&delta.snapshot);
    assert(!state_cell_pressure_observer_next(observer, &empty));
    observer->next_identifier = UINT64_MAX;
    StateCell *other = state_cell_managed_new(NULL);
    assert(!state_cell_pressure_observer_ensure(observer, other, &fresh) && !fresh &&
        state_cell_pressure_observer_epoch(observer) == UINT64_MAX);
    state_cell_creator_release(other);
    state_cell_creator_release(cell);
    state_cell_pressure_observer_free(observer);
    empty_registry();
}


static void uncovered_cover(StateCellPressureObserver *observer, StateCell *cell) {
    bool fresh;
    assert(state_cell_pressure_observer_ensure(observer, cell, &fresh));
    StateCellPressureDelta delta;
    while (state_cell_pressure_observer_next(observer, &delta)) {
        assert(state_cell_pressure_observer_settle(observer, &delta, true));
        state_cell_snapshot_release(&delta.snapshot);
    }
}

static StateCellImage *uncovered_reference(StateCell *cell, bool canonical) {
    Arena input;
    arena_init_detached(&input);
    Atom *reference = atom_state(&input, cell);
    assert(reference);
    StateCellImage *image = state_cell_image_prepare(
        atom_expr2(&input, atom_symbol(&input, "summary-edge"), reference),
        reference, canonical);
    assert(image && image->mutable_leaves_complete && image->mutable_leaf_count == 1u &&
        image->mutable_leaves[0] == cell);
    arena_free(&input);
    return image;
}

static void uncovered_status_controls(void) {
    for (unsigned mode = 0u; mode < 8u; mode++) {
        StateCellPressureObserver *observer = state_cell_pressure_observer_new();
        StateCell *parent = state_cell_managed_new(integer_image(17, 1, false));
        StateCell *child = state_cell_managed_new(integer_image(19, 1, false));
        assert(observer && parent && child && !state_cell_pressure_observer_uncovered_epoch(observer));
        uncovered_cover(observer, parent);
        StateCellPressureDelta inflight = {0};
        bool fresh;
        if (mode == 1u || mode == 2u)
            assert(state_cell_pressure_observer_ensure(observer, child, &fresh) && fresh);
        if (mode == 2u)
            assert(state_cell_pressure_observer_next(observer, &inflight) && inflight.cell == child);
        if (mode == 3u || mode == 5u || mode == 7u) {
            uncovered_cover(observer, child);
            if (mode == 3u)
                assert(state_cell_publish_prepared(child, integer_image(23, 1, false)));
            if (mode == 7u)
                ((ManagedStateNode *)child)->publication_epoch = UINT64_MAX;
        }
        if (mode == 4u) {
            state_cell_pressure_observer_begin(observer);
            StateCellSnapshot snapshot; size_t control;
            assert(state_cell_pressure_observer_watch(observer, parent, &snapshot, &control));
            state_cell_snapshot_release(&snapshot);
            assert(state_cell_pressure_observer_watch(observer, child, &snapshot, &control));
            state_cell_snapshot_release(&snapshot);
        }
        StateCell *leaf = mode == 6u ? parent : child;
        assert(state_cell_publish_prepared(parent, uncovered_reference(leaf, mode == 0u)));
        uint64_t wanted = mode == 5u || mode == 6u ? 0u : 1u;
        assert(state_cell_pressure_observer_uncovered_epoch(observer) == wanted);
        assert(state_cell_publish_prepared(parent, integer_image(17, 1, false)));
        assert(state_cell_pressure_observer_uncovered_epoch(observer) == wanted);
        if (mode == 4u) {
            uint64_t epoch;
            assert(!state_cell_pressure_observer_finish(observer, &epoch));
            state_cell_pressure_observer_cancel(observer);
        }
        state_cell_pressure_observer_begin(observer);
        state_cell_pressure_observer_cancel(observer);
        uint64_t accepted;
        pressure_watch_one(observer, parent, 17, &accepted);
        assert(state_cell_pressure_observer_uncovered_epoch(observer) == wanted);
        if (inflight.capture_id) {
            assert(!state_cell_pressure_observer_settle(observer, &inflight, true));
            state_cell_snapshot_release(&inflight.snapshot);
        }
        state_cell_pressure_observer_free(observer);
        ((ManagedStateNode *)child)->publication_epoch = 1u;
        state_cell_creator_release(parent); state_cell_creator_release(child);
        empty_registry();
    }
    puts("PASS: uncovered imports distinguish absent, queued, inflight, stale, pending, saturated, covered and pre-publication self coverage");
}

static void uncovered_covered_updates(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *parent = state_cell_managed_new(integer_image(17, 1, false));
    StateCell *child = state_cell_managed_new(integer_image(19, 1, false));
    assert(observer && parent && child);
    uncovered_cover(observer, parent); uncovered_cover(observer, child);
    for (unsigned i = 0u; i < 32u; i++) {
        assert(state_cell_publish_prepared(parent, uncovered_reference(child, false)));
        uncovered_cover(observer, parent);
        assert(!state_cell_pressure_observer_uncovered_epoch(observer));
        StateCellSnapshot same;
        assert(state_cell_snapshot_acquire(parent, &same));
        assert(state_cell_publish_prepared(parent, same.image));
        same.image = NULL; state_cell_snapshot_release(&same);
        uncovered_cover(observer, parent);
        assert(!state_cell_pressure_observer_uncovered_epoch(observer));
    }
    assert(state_cell_publish_prepared(parent, integer_image(17, 1, false)));
    uncovered_cover(observer, parent);
    for (unsigned i = 0u; i < 32u; i++) {
        assert(state_cell_publish_prepared(parent, uncovered_reference(parent, false)));
        uncovered_cover(observer, parent);
        assert(!state_cell_pressure_observer_uncovered_epoch(observer));
    }
    assert(state_cell_publish_prepared(parent, integer_image(17, 1, false)));
    state_cell_pressure_observer_free(observer);
    state_cell_creator_release(parent); state_cell_creator_release(child);
    empty_registry();
    puts("PASS: 32 covered-child updates, same-image publications and settled self writes introduce no uncovered-import obligation");
}

static void uncovered_unknown_and_summary_refusal(void) {
    StateCellPressureObserver *observer = state_cell_pressure_observer_new();
    StateCell *parent = state_cell_managed_new(integer_image(17, 1, false));
    assert(observer && parent); uncovered_cover(observer, parent);
    Authority owner = {0}; Arena input; arena_init_detached(&input);
    Atom *opaque = atom_native_handle_identifier(&input, 73, &owner,
        authority_retain, authority_release);
    StateCellImage *unknown = state_cell_image_prepare(opaque, NULL, false);
    assert(unknown && !unknown->mutable_leaves_complete);
    assert(state_cell_publish_prepared(parent, unknown));
    assert(state_cell_pressure_observer_uncovered_epoch(observer) == 1u);
    assert(state_cell_publish_prepared(parent, integer_image(17, 1, false)));
    assert(state_cell_pressure_observer_uncovered_epoch(observer) == 1u);
    StateCell *children[5]; Atom *references[5];
    for (unsigned i = 0u; i < 5u; i++) {
        children[i] = state_cell_managed_new(integer_image(31 + i, 1, false));
        assert(children[i]); references[i] = atom_state(&input, children[i]); assert(references[i]);
    }
    Atom *value = atom_expr(&input, references, 5u);
    unsigned refused_summaries = 0u;
    for (unsigned failure = 1u; failure <= 32u; failure++) {
        fail_after = failure;
        StateCellImage *image = state_cell_image_prepare(value, NULL, false);
        fail_after = 0u;
        if (!image) continue;
        if (!image->mutable_leaves_complete) {
            refused_summaries++;
            uint64_t before = state_cell_pressure_observer_uncovered_epoch(observer);
            assert(state_cell_publish_prepared(parent, image));
            assert(state_cell_pressure_observer_uncovered_epoch(observer) == before + 1u);
            assert(state_cell_publish_prepared(parent, integer_image(17, 1, false)));
            assert(state_cell_pressure_observer_uncovered_epoch(observer) == before + 1u);
        } else {
            assert(image->mutable_leaf_count == 5u);
            state_cell_image_release(image);
        }
    }
    assert(refused_summaries);
    atomic_store(&observer->uncovered_epoch, UINT64_MAX - 1u);
    assert(state_cell_publish_prepared(parent, uncovered_reference(children[0], false)));
    assert(state_cell_pressure_observer_uncovered_epoch(observer) == UINT64_MAX &&
        state_cell_pressure_observer_epoch(observer) == UINT64_MAX);
    assert(state_cell_publish_prepared(parent, integer_image(17, 1, false)));
    assert(state_cell_pressure_observer_uncovered_epoch(observer) == UINT64_MAX);
    state_cell_pressure_observer_free(observer);
    state_cell_creator_release(parent);
    for (unsigned i = 0u; i < 5u; i++) state_cell_creator_release(children[i]);
    arena_free(&input); empty_registry(); assert(!owner.holds);
    puts("PASS: unknown owners and refused five-child summaries retain obligations through scalar overwrite; epoch exhaustion saturates");
}

static void uncovered_atomic_and_historical_summaries(void) {
    Arena input; arena_init_detached(&input);
    char bytes[8192]; memset(bytes, 's', sizeof(bytes));
    for (unsigned canonical = 0u; canonical < 2u; canonical++) {
        StateCellImage *image = state_cell_image_prepare(
            atom_string_n(&input, bytes, sizeof(bytes)), NULL, canonical);
        assert(image && image->mutable_leaves_complete && !image->mutable_leaf_count);
        state_cell_image_release(image);
    }
    StateCell *child = state_cell_managed_new(integer_image(19, 1, false));
    StateCellImage *historical = uncovered_reference(child, true);
    StateCell *container = state_cell_managed_new(historical); assert(container);
    StateCellSnapshot old;
    assert(state_cell_snapshot_acquire(container, &old));
    assert(state_cell_publish_prepared(container, integer_image(23, 1, false)));
    /* A private construction fixture keeps an exact historical image owner
     * whose mutable leaves differ from the container's current scalar. */
    StateCellImage draft = {0}; arena_init_detached(&draft.syntax);
    assert(state_cell_snapshot_retain_into(&draft.syntax, &old));
    uint32_t identity;
    assert(arena_retain_identity_certificate(&draft.syntax, &input, &identity));
    image_prepare_summary(&draft);
    assert(draft.mutable_leaves_complete && draft.mutable_leaf_count == 1u &&
        draft.mutable_leaves[0] == child);
    free(draft.mutable_leaves); arena_free(&draft.syntax);
    state_cell_snapshot_release(&old);
    state_cell_creator_release(container); state_cell_creator_release(child);
    arena_free(&input); empty_registry();
    assert(!arena_owner_is_atomic_storage((void *)(uintptr_t)1u, authority_release) &&
        !arena_owner_is_atomic_storage(NULL, authority_release));
    puts("PASS: String and name-only owners certify no mutable edges; canonical wrappers and exact historical images retain their mutable leaves");
}


static void raw_resource_certificate_controls(void) {
    Arena input; arena_init_detached(&input);
    StateCell legacy = {.value=atom_int(&input,7), .content_type=atom_int(&input,1)};
    StateCell *child=state_cell_managed_new(integer_image(19,1,false));
    StateCell *parent=state_cell_managed_new(integer_image(17,1,false));
    StateCellPressureObserver *observer=state_cell_pressure_observer_new();
    assert(child && parent && observer);uncovered_cover(observer,parent);
    Atom *raw=atom_state(&input,&legacy), *managed=atom_state(&input,child);
    assert(raw && managed);
    Atom *name_key=atom_expr2(&input,atom_symbol(&input,"raw-key"),raw);
    Atom *named=atom_var_with_name_key(&input,name_key,(VarId){.base=917u,.epoch={0u}});
    assert(named);
    Atom *values[6]={raw,raw,atom_expr3(&input,atom_symbol(&input,"mixed"),managed,raw),
        atom_int(&input,1),named,atom_expr2(&input,atom_symbol(&input,"named"),named)};
    for (unsigned i=0u;i<6u;i++) {
        StateCellImage *image=state_cell_image_prepare(values[i],i==3u?raw:NULL,i==1u);
        assert(image && !image->mutable_leaves_complete);
        if(i<2u)assert(image->value->kind==ATOM_GROUNDED && image->value->ground.gkind==GV_STATE && image->value->ground.ptr==&legacy);
        uint64_t before=state_cell_pressure_observer_uncovered_epoch(observer);
        assert(state_cell_publish_prepared(parent,image));
        assert(state_cell_pressure_observer_uncovered_epoch(observer)==before+1u);
        uint64_t notice=state_cell_pressure_observer_epoch(observer);
        legacy.value=atom_int(&input,9+i);
        assert(state_cell_pressure_observer_epoch(observer)==notice);
        assert(state_cell_publish_prepared(parent,integer_image(17,1,false)));
        assert(state_cell_pressure_observer_uncovered_epoch(observer)==before+1u);
    }
    Atom *pure=atom_int(&input,1), *head=atom_symbol(&input,"pure-source");
    for(unsigned i=0u;i<5000u;i++)pure=atom_expr3(&input,head,pure,pure);
    StateCellImage *image=state_cell_image_prepare(pure,NULL,false);
    assert(image && image->mutable_leaves_complete && !image->mutable_leaf_count);
    state_cell_image_release(image);
    Atom **wide=malloc(5000u*sizeof(*wide));assert(wide);
    for(unsigned i=0u;i<5000u;i++)wide[i]=atom_int(&input,1);
    image=state_cell_image_prepare(atom_expr(&input,wide,5000u),NULL,false);
    assert(image && image->mutable_leaves_complete);state_cell_image_release(image);
    for(unsigned i=0u;i<5000u;i++)wide[i]=managed;
    image=state_cell_image_prepare(atom_expr(&input,wide,5000u),NULL,false);
    assert(image && !image->mutable_leaves_complete && image->value->expr.len==5000u);
    assert(image->value->expr.elems[0]->ground.ptr==child && image->value->expr.elems[4999]->ground.ptr==child);
    uint64_t before=state_cell_pressure_observer_uncovered_epoch(observer);
    assert(state_cell_publish_prepared(parent,image));
    assert(state_cell_pressure_observer_uncovered_epoch(observer)==before+1u);
    assert(state_cell_publish_prepared(parent,integer_image(17,1,false)));
    Atom *deep=managed;
    for(unsigned i=0u;i<100u;i++)deep=atom_expr2(&input,head,deep);
    image=state_cell_image_prepare(deep,NULL,false);
    assert(image && !image->mutable_leaves_complete);state_cell_image_release(image);
    free(wide);state_cell_pressure_observer_free(observer);
    state_cell_creator_release(parent);state_cell_creator_release(child);arena_free(&input);empty_registry();
    puts("PASS: raw legacy value/type/name-key leaves are incomplete, managed aliases remain owned, pure deep/wide data prunes, and certification budgets never reject publication");
}

int main(void) {
    _Static_assert(sizeof(StateCell) == 32u, "native StateCell ABI changed");
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    aliases_and_snapshots();
    revival_and_unmanaged();
    payload_owners_and_binary_strings();
    overwrite_storage();
    directory_oracle();
    pointer_reuse_and_saturation();
    preparation_refusal();
    concurrent_value_type_pair();
    cyclic_clone_identity();
    repeated_self_reference_storage();
    pinned_self_cycle_retirement();
    mutual_cycle_retirement();
    retained_cycle_reader();
    image_only_cycle_retirement();
    graph_capture_refusal_and_growth();
    graph_current_image_aba();
    dying_zero_image_capture();
    state_root_acquisition_marker();
    pressure_targeting_and_historical_image();
    pressure_refusal_aba_and_bounded_metadata();
    pressure_weak_retirement_and_pointer_reuse();
    pressure_saturation_and_observer_free();
    pressure_concurrent_publication();
    pressure_weighted_notices();
    dying_storage_overlap();
    delta_discovery_and_coalescing();
    delta_retirement_and_node_aba();
    delta_retry_and_capture_replay();
    delta_full_trace_and_refusal();
    uncovered_status_controls();
    uncovered_covered_updates();
    uncovered_unknown_and_summary_refusal();
    uncovered_atomic_and_historical_summaries();
    raw_resource_certificate_controls();
    empty_registry();
    assert(atomic_load(&notifications) > 0u);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: managed state identities, image snapshots, bounded retirement, AVL mutation, pointer reuse, refusal and atomic value/type publication");
    return 0;
}
