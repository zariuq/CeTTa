/* Resource cycles are traced at the same exclusive boundary as stored ids.
 * Strong edges come from owners, not repeated Atom occurrences. Independent
 * State snapshots pin their controls without being counted as client roots;
 * foreign owners, saturation and unmatched reference counts remain roots. */
typedef struct {
    SpaceHold *hold;
    Space *space;
    void *image;
    StateCell *state;
    StateCellImage *state_image;
    const Arena *syntax;
    void *detached_base;
    uint64_t incoming;
    uint64_t references;
    uint32_t first;
    bool marked;
    bool universe_image;
} SpaceResourceNode;

typedef struct {
    uint32_t to, next;
} SpaceResourceEdge;

typedef struct {
    SpaceResourceNode *nodes;
    SpaceResourceEdge *edges;
    size_t length, capacity, edge_len, edge_cap;
    CettaU64Index addresses, ids;
    TermUniverse *universe;
    uint32_t current;
    bool failed;
} SpaceResourceGraph;

static uint32_t space_resource_node(SpaceResourceGraph *graph, void *key,
                                     SpaceResourceNode node) {
    if (graph->failed || graph->length == UINT32_MAX)
        return graph->failed = true, UINT32_MAX;
    if (graph->length == graph->capacity) {
        size_t cap = graph->capacity ? 2u * graph->capacity : 32u;
        if (cap < graph->capacity || cap > SIZE_MAX / sizeof(*graph->nodes))
            return graph->failed = true, UINT32_MAX;
        SpaceResourceNode *more = realloc(graph->nodes, cap * sizeof(*more));
        if (!more)
            return graph->failed = true, UINT32_MAX;
        graph->nodes = more;
        graph->capacity = cap;
    }
    uint32_t index = (uint32_t)graph->length++;
    node.first = UINT32_MAX;
    graph->nodes[index] = node;
    if (cetta_u64_index_insert(&graph->addresses, (uintptr_t)key, index) !=
        CETTA_GSLT_U32_INDEX_INSERTED_V1)
        graph->failed = true;
    return index;
}

static void space_resource_edge(SpaceResourceGraph *graph,
                                 uint32_t from, uint32_t to) {
    if (graph->failed)
        return;
    if (graph->edge_len == UINT32_MAX) {
        graph->failed = true;
        return;
    }
    if (graph->edge_len == graph->edge_cap) {
        size_t cap = graph->edge_cap ? 2u * graph->edge_cap : 32u;
        if (cap < graph->edge_cap || cap > SIZE_MAX / sizeof(*graph->edges)) {
            graph->failed = true;
            return;
        }
        SpaceResourceEdge *more = realloc(graph->edges, cap * sizeof(*more));
        if (!more) {
            graph->failed = true;
            return;
        }
        graph->edges = more;
        graph->edge_cap = cap;
    }
    graph->edges[graph->edge_len] = (SpaceResourceEdge){
        .to = to, .next = graph->nodes[from].first};
    graph->nodes[from].first = (uint32_t)graph->edge_len++;
}

static void space_resource_image(void *context, AtomId id, void *image,
        const Arena *syntax, uint64_t clients, uint64_t owned, uint16_t count) {
    SpaceResourceGraph *graph = context;
    uint32_t index = space_resource_node(graph, image, (SpaceResourceNode){
        .image = image, .syntax = syntax,
        .references = clients, .universe_image = true,
        .marked = count == UINT16_MAX || count != owned});
    if (!graph->failed && cetta_u64_index_insert(&graph->ids, id, index) !=
            CETTA_GSLT_U32_INDEX_INSERTED_V1)
        graph->failed = true;
}

static void space_resource_image_owner(void *context, void *owner) {
    SpaceResourceGraph *graph = context;
    uint32_t from;
    if (cetta_u64_index_find(&graph->addresses, (uintptr_t)owner, &from))
        space_resource_edge(graph, from, graph->current);
    else
        graph->nodes[graph->current].marked = true;
}

static void space_resource_image_hold(void *context, void *opaque,
                                       void (*release)(void *)) {
    SpaceResourceGraph *graph = context;
    uint32_t to;
    void *target = NULL;
    TermUniverseResourceImageView stored = {0};
    if (term_universe_resource_image_owner_view(opaque, release, &stored)) {
        target = stored.image;
        if (!cetta_u64_index_find(&graph->addresses, (uintptr_t)target, &to))
            (void)space_resource_node(graph, target, (SpaceResourceNode){
                .image = target, .syntax = stored.syntax,
                .references = stored.references, .marked = stored.references == UINT64_MAX});
    } else if (release == space_hold_release) {
        target = ((SpaceHold *)opaque)->space;
    } else {
        StateCell *cell = NULL;
        StateCellImage *image = NULL;
        if (!state_cell_classify_owner(opaque, release, &cell, &image))
            return;
        target = cell ? (void *)cell : (void *)image;
    }
    if (target && cetta_u64_index_find(&graph->addresses, (uintptr_t)target, &to)) {
        space_resource_edge(graph, graph->current, to);
        graph->nodes[to].incoming++;
    }
}

typedef enum {
    SPACE_RESOURCE_CYCLES_COMPLETE,
    SPACE_RESOURCE_CYCLES_DEFERRED,
    SPACE_RESOURCE_CYCLES_CAPACITY
} SpaceResourceCycleResult;

static _Thread_local bool g_space_resource_collection_running;
static _Thread_local SpaceResourceCycleResult g_space_resource_cycle_result;

typedef struct {
    TermUniverse *universe;
    CettaSharedTransitionGuard transition;
    bool entered_scope;
    bool active;
} SpaceResourceBoundary;

static void space_resource_boundary_leave(SpaceResourceBoundary *boundary) {
    if (!boundary->active)
        return;
    term_universe_collection_inhibit_leave(boundary->universe);
    g_space_resource_collection_running = false;
    cetta_shared_transition_guard_leave(&boundary->transition);
    if (boundary->entered_scope)
        cetta_shared_transition_scope_leave();
}

static void space_resource_provider_id(void *context, AtomId id) {
    SpaceResourceGraph *graph = context;
    uint32_t to;
    if (!cetta_u64_index_find(&graph->ids, id, &to))
        return;
    if (graph->current == UINT32_MAX)
        graph->nodes[to].marked = true;
    else
        space_resource_edge(graph, graph->current, to);
}

static SpaceResourceCycleResult space_reclaim_resource_cycles_checked(TermUniverse *universe) {
    if (!universe || !universe->counting || g_space_resource_collection_running ||
        (cetta_shared_transition_scope_active() &&
         universe != g_eval_private_collection_universe))
        return SPACE_RESOURCE_CYCLES_DEFERRED;
    StateCellDiagnostics state_counts = state_cell_diagnostics();
    if (!g_listed_spaces.len && !g_made_spaces.len &&
        !state_counts.nodes && !state_counts.images)
        return SPACE_RESOURCE_CYCLES_COMPLETE;
    __attribute__((cleanup(space_resource_boundary_leave)))
    SpaceResourceBoundary boundary = {.universe = universe,
        .entered_scope = !cetta_shared_transition_scope_active()};
    if (boundary.entered_scope)
        cetta_shared_transition_scope_enter();
    cetta_shared_transition_guard_enter(&boundary.transition);
    term_universe_collection_inhibit_enter(universe);
    boundary.active = true;
    g_space_resource_collection_running = true;

    /* Only reference cleanup is permitted before the complete census. */
    eval_release_unheld_spaces();
    StateCellGraphSnapshot *states = NULL;
    if (state_counts.nodes || state_counts.images) {
        states = state_cell_graph_snapshot_acquire();
        if (!states)
            return SPACE_RESOURCE_CYCLES_CAPACITY;
    }
    size_t state_len = state_cell_graph_node_count(states);
    size_t image_len = state_cell_graph_image_count(states);
    const StateCellGraphNode *state_nodes = state_cell_graph_nodes(states);
    const StateCellGraphImage *state_images = state_cell_graph_images(states);
    bool *dead_states = state_len ? calloc(state_len, sizeof(*dead_states)) : NULL;
    bool *dead_images = image_len ? calloc(image_len, sizeof(*dead_images)) : NULL;
    uint64_t *state_incoming = state_len ? calloc(state_len, sizeof(*state_incoming)) : NULL;
    uint64_t *image_incoming = image_len ? calloc(image_len, sizeof(*image_incoming)) : NULL;
    SpaceResourceGraph graph = {.universe = universe};
    bool deferred = false;
    graph.failed = (state_len && (!dead_states || !state_incoming)) ||
                   (image_len && (!dead_images || !image_incoming));
    pthread_mutex_lock(&g_space_hold_mutex);
    for (CettaCount i = 0u; !graph.failed && i < g_listed_spaces.len; i++) {
        Space *space = g_listed_spaces.items[i].space;
        if (space->native.universe == universe)
            space_resource_node(&graph, space, (SpaceResourceNode){
                .hold = space->arena_hold, .space = space,
                .marked = space->arena_hold->pinned});
    }
    size_t spaces = graph.length;
    if (!graph.failed)
        term_universe_visit_resource_images(universe, space_resource_image, &graph);
    size_t state_begin = graph.length;
    for (size_t i = 0u; !graph.failed && i < state_len; i++) {
        const StateCellGraphNode *node = &state_nodes[i];
        space_resource_node(&graph, node->cell, (SpaceResourceNode){
            .state = node->cell, .references = node->references,
            .marked = node->saturated});
    }
    size_t image_begin = graph.length;
    for (size_t i = 0u; !graph.failed && i < image_len; i++) {
        const StateCellGraphImage *image = &state_images[i];
        space_resource_node(&graph, image->image, (SpaceResourceNode){
            .state_image = image->image, .syntax = image->syntax,
            .references = image->references, .marked = image->saturated});
    }
    /* First enumerate every control address, then inspect immutable owners.
     * This counts one strong owner lease, not each repeated syntax leaf. */
    for (size_t i = 0u; !graph.failed && i < state_len; i++) {
        StateCellImage *image = state_nodes[i].current_image;
        uint32_t to;
        if (image) {
            if (!cetta_u64_index_find(&graph.addresses, (uintptr_t)image, &to)) {
                graph.failed = true;
                break;
            }
            space_resource_edge(&graph, (uint32_t)(state_begin + i), to);
            graph.nodes[to].incoming++;
        }
    }
    /* Immutable stored images can outlive the universe that created them.
     * Discover these intermediate owners through certified leases, including
     * wrappers inside State images, before closing the graph. */
    for (size_t i = spaces; !graph.failed && i < graph.length; i++) {
        graph.current = (uint32_t)i;
        if (graph.nodes[i].universe_image)
            term_universe_visit_resource_image_owners(graph.nodes[i].image,
                space_resource_image_owner, &graph);
        const Arena *syntax = graph.nodes[i].syntax;
        if (syntax)
            arena_visit_retained_owners(syntax, space_resource_image_hold, &graph);
    }
    for (size_t i = 0u; !graph.failed && i < spaces; i++) {
        SpaceHold *base = space_lifetime_pin_record(graph.nodes[i].hold->base_hold);
        uint32_t to;
        if (base && base->space && cetta_u64_index_find(&graph.addresses,
                (uintptr_t)base->space, &to)) {
            space_resource_edge(&graph, (uint32_t)i, to);
            graph.nodes[to].incoming++;
        }
    }
    for (size_t i = 0u; !graph.failed && i < universe->root_provider_len; i++) {
        TermUniverseRootProviderEntry provider = universe->root_providers[i];
        if (provider.counted)
            continue;
        graph.current = UINT32_MAX;
        if (provider.resource_owner)
            (void)cetta_u64_index_find(&graph.addresses,
                (uintptr_t)provider.resource_owner, &graph.current);
        provider.fn(universe, space_resource_provider_id, &graph, provider.ctx);
    }
    for (size_t i = 0u; !graph.failed && i < spaces; i++) {
        SpaceResourceNode *node = &graph.nodes[i];
        if (node->incoming > node->hold->holds) {
            graph.failed = true;
            deferred = true;
        }
        if (node->incoming != node->hold->holds)
            node->marked = true;
    }
    for (size_t i = spaces; !graph.failed && i < graph.length; i++) {
        SpaceResourceNode *node = &graph.nodes[i];
        if (node->incoming > node->references) {
            graph.failed = true;
            deferred = true;
        }
        if (node->incoming != node->references)
            node->marked = true;
    }
    uint32_t *pending = graph.length && graph.length <= SIZE_MAX / sizeof(*pending)
        ? malloc(graph.length * sizeof(*pending)) : NULL;
    if (graph.length && !pending)
        graph.failed = true;
    bool protected_spaces = false;
    if (!graph.failed) {
        size_t len = 0u;
        for (size_t i = 0u; i < graph.length; i++)
            if (graph.nodes[i].marked)
                pending[len++] = (uint32_t)i;
        while (len) {
            SpaceResourceNode *node = &graph.nodes[pending[--len]];
            for (uint32_t edge = node->first; edge != UINT32_MAX;
                 edge = graph.edges[edge].next) {
                uint32_t to = graph.edges[edge].to;
                if (!graph.nodes[to].marked) {
                    graph.nodes[to].marked = true;
                    pending[len++] = to;
                }
            }
        }
        for (size_t i = 0u; i < state_len; i++) {
            dead_states[i] = !graph.nodes[state_begin + i].marked;
            state_incoming[i] = graph.nodes[state_begin + i].incoming;
        }
        for (size_t i = 0u; i < image_len; i++) {
            dead_images[i] = !graph.nodes[image_begin + i].marked;
            image_incoming[i] = graph.nodes[image_begin + i].incoming;
        }
        /* These records survive State image finalizers and every peer's
         * detach/free sequence, including overlay base-edge release. */
        for (size_t i = 0u; i < spaces; i++)
            if (!graph.nodes[i].marked)
                graph.nodes[i].hold->holds++;
        protected_spaces = true;
    }
    pthread_mutex_unlock(&g_space_hold_mutex);
    if (!graph.failed && states && !state_cell_graph_retire(states,
            dead_states, dead_images, state_incoming, image_incoming)) {
        graph.failed = true;
        deferred = true;
    }
    if (!graph.failed) {
        for (size_t i = 0u; i < spaces; i++)
            if (!graph.nodes[i].marked)
                graph.nodes[i].detached_base =
                    space_hold_detach(graph.nodes[i].hold->space);
        for (size_t i = 0u; i < spaces; i++) {
            if (graph.nodes[i].marked)
                continue;
            SpaceHold *hold = graph.nodes[i].hold;
            eval_free_held_space((EvalTrackedSpace){
                .space = graph.nodes[i].space,
                .petta_program = hold->petta_program});
            space_hold_release(hold);
        }
        for (size_t i = 0u; i < spaces; i++)
            if (!graph.nodes[i].marked)
                space_lifetime_unpin(graph.nodes[i].detached_base);
    } else if (protected_spaces) {
        for (size_t i = 0u; i < spaces; i++)
            if (!graph.nodes[i].marked)
                space_hold_release(graph.nodes[i].hold);
    }
    free(pending);
    free(dead_states);
    free(dead_images);
    free(state_incoming);
    free(image_incoming);
    free(graph.nodes);
    free(graph.edges);
    cetta_u64_index_free(&graph.addresses);
    cetta_u64_index_free(&graph.ids);
    state_cell_graph_snapshot_release(states);
    (void)state_cell_reclaim_bounded(256u);
    return !graph.failed ? SPACE_RESOURCE_CYCLES_COMPLETE
        : deferred ? SPACE_RESOURCE_CYCLES_DEFERRED : SPACE_RESOURCE_CYCLES_CAPACITY;
}

void space_reclaim_resource_cycles(TermUniverse *universe) {
    g_space_resource_cycle_result = space_reclaim_resource_cycles_checked(universe);
}
