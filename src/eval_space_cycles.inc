/* Resource cycles are traced at the same exclusive boundary as stored ids.
 * Ordinary stored records contain no resource edges. Only opaque images,
 * their sparse container owners and explicit overlay holds enter this graph.
 * A foreign owner, saturated id count or scoped image reader is a root. */
typedef struct {
    SpaceHold *hold;
    Space *space;
    void *image;
    const Arena *syntax;
    uint64_t incoming;
    uint32_t first;
    bool marked;
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
        .marked = clients != 0u || count == UINT16_MAX || count != owned});
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
    if (release != space_hold_release)
        return;
    SpaceHold *hold = opaque;
    uint32_t to;
    if (hold->space && cetta_u64_index_find(&graph->addresses,
                                           (uintptr_t)hold->space, &to)) {
        space_resource_edge(graph, graph->current, to);
        graph->nodes[to].incoming++;
    }
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

void space_reclaim_resource_cycles(TermUniverse *universe) {
    if (!universe || !universe->counting ||
        cetta_shared_transition_scope_active())
        return;
    if (!g_listed_spaces.len && !g_made_spaces.len)
        return;
    /* This also registers newly created, still held spaces in this thread's
     * list. No invocation banks are cleared at a resource boundary. */
    eval_release_unheld_spaces();
    if (!g_listed_spaces.len)
        return;
    SpaceResourceGraph graph = {.universe = universe};
    pthread_mutex_lock(&g_space_hold_mutex);
    for (CettaCount i = 0u; i < g_listed_spaces.len; i++) {
        Space *space = g_listed_spaces.items[i].space;
        if (space->native.universe == universe)
            space_resource_node(&graph, space, (SpaceResourceNode){
                .hold = space->arena_hold, .space = space,
                .marked = space->arena_hold->pinned});
    }
    size_t spaces = graph.length;
    term_universe_visit_resource_images(universe, space_resource_image, &graph);
    for (size_t i = spaces; !graph.failed && i < graph.length; i++) {
        graph.current = (uint32_t)i;
        term_universe_visit_resource_image_owners(graph.nodes[i].image,
            space_resource_image_owner, &graph);
        arena_visit_retained_owners(graph.nodes[i].syntax,
            space_resource_image_hold, &graph);
    }
    for (size_t i = 0u; !graph.failed && i < spaces; i++) {
        SpaceHold *base = graph.nodes[i].hold->base_hold;
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
        if (node->incoming > node->hold->holds)
            graph.failed = true;
        if (node->incoming != node->hold->holds)
            node->marked = true;
    }
    uint32_t *pending = graph.length <= SIZE_MAX / sizeof(*pending)
        ? malloc(graph.length * sizeof(*pending)) : NULL;
    if (!pending)
        graph.failed = true;
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
        /* Protect all doomed hold records until all spaces have detached.
         * A base edge or image finalizer may otherwise release a peer's last
         * hold while this pass still has its record in the snapshot. */
        for (size_t i = 0u; i < spaces; i++)
            if (!graph.nodes[i].marked)
                graph.nodes[i].hold->holds++;
    }
    pthread_mutex_unlock(&g_space_hold_mutex);
    if (!graph.failed) {
        for (size_t i = 0u; i < spaces; i++)
            if (!graph.nodes[i].marked)
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
    }
    free(pending);
    free(graph.nodes);
    free(graph.edges);
    cetta_u64_index_free(&graph.addresses);
    cetta_u64_index_free(&graph.ids);
}
