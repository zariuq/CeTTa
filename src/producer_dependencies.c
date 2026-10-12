#include "producer_dependencies.h"

#include <stdlib.h>
#include <string.h>

#define NO_EDGE UINT32_MAX

struct CettaProducerDependencyNode {
    uint32_t outgoing, incoming;
};

struct CettaProducerDependencyEdge {
    uint32_t source, target;
    uint32_t next_source, previous_source, next_target, previous_target;
};

void cetta_producer_dependencies_init(CettaProducerDependencies *graph) {
    *graph = (CettaProducerDependencies){.free_edge = NO_EDGE};
}

void cetta_producer_dependencies_free(CettaProducerDependencies *graph) {
    free(graph->nodes);
    free(graph->edges);
    cetta_producer_dependencies_init(graph);
}

static size_t next_capacity(size_t old, size_t required, size_t width) {
    size_t capacity = old ? old : 16u;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2u)
            return 0u;
        capacity *= 2u;
    }
    return capacity <= SIZE_MAX / width ? capacity : 0u;
}

static bool reserve_nodes(CettaProducerDependencies *graph, size_t required) {
    if (required <= graph->node_capacity)
        return true;
    size_t capacity = next_capacity(graph->node_capacity, required,
        sizeof(*graph->nodes));
    if (!capacity)
        return false;
    CettaProducerDependencyNode *nodes = calloc(capacity, sizeof(*nodes));
    if (!nodes)
        return false;
    for (size_t i = 0u; i < capacity; i++)
        nodes[i] = (CettaProducerDependencyNode){NO_EDGE, NO_EDGE};
    if (graph->node_capacity)
        memcpy(nodes, graph->nodes, graph->node_capacity * sizeof(*nodes));
    free(graph->nodes);
    graph->nodes = nodes;
    graph->node_capacity = capacity;
    return true;
}

static bool reserve_edge(CettaProducerDependencies *graph) {
    if (graph->free_edge != NO_EDGE || graph->edge_length < graph->edge_capacity)
        return true;
    size_t capacity = next_capacity(graph->edge_capacity, graph->edge_length + 1u,
        sizeof(*graph->edges));
    if (!capacity)
        return false;
    CettaProducerDependencyEdge *edges = calloc(capacity, sizeof(*edges));
    if (!edges)
        return false;
    if (graph->edge_length)
        memcpy(edges, graph->edges, graph->edge_length * sizeof(*edges));
    free(graph->edges);
    graph->edges = edges;
    graph->edge_capacity = capacity;
    return true;
}

bool cetta_producer_dependencies_link(CettaProducerDependencies *graph,
    size_t reader, size_t producer) {
    if (reader >= UINT32_MAX || producer >= UINT32_MAX)
        return false;
    size_t required = (reader > producer ? reader : producer) + 1u;
    if (!reserve_nodes(graph, required) || !reserve_edge(graph))
        return false;
    uint32_t index;
    if (graph->free_edge != NO_EDGE) {
        index = graph->free_edge;
        graph->free_edge = graph->edges[index].next_source;
    } else {
        index = (uint32_t)graph->edge_length++;
    }
    CettaProducerDependencyNode *source = graph->nodes + reader;
    CettaProducerDependencyNode *target = graph->nodes + producer;
    graph->edges[index] = (CettaProducerDependencyEdge){
        (uint32_t)reader, (uint32_t)producer,
        source->outgoing, NO_EDGE, target->incoming, NO_EDGE};
    if (source->outgoing != NO_EDGE)
        graph->edges[source->outgoing].previous_source = index;
    if (target->incoming != NO_EDGE)
        graph->edges[target->incoming].previous_target = index;
    source->outgoing = target->incoming = index;
    graph->live_edges++;
    return true;
}

static void remove_edge(CettaProducerDependencies *graph, uint32_t index) {
    CettaProducerDependencyEdge *edge = graph->edges + index;
    if (edge->previous_source == NO_EDGE)
        graph->nodes[edge->source].outgoing = edge->next_source;
    else
        graph->edges[edge->previous_source].next_source = edge->next_source;
    if (edge->next_source != NO_EDGE)
        graph->edges[edge->next_source].previous_source = edge->previous_source;
    if (edge->previous_target == NO_EDGE)
        graph->nodes[edge->target].incoming = edge->next_target;
    else
        graph->edges[edge->previous_target].next_target = edge->next_target;
    if (edge->next_target != NO_EDGE)
        graph->edges[edge->next_target].previous_target = edge->previous_target;
    edge->source = NO_EDGE;
    edge->next_source = graph->free_edge;
    graph->free_edge = index;
    graph->live_edges--;
}

void cetta_producer_dependencies_clear_reads(CettaProducerDependencies *graph,
    size_t reader) {
    if (reader >= graph->node_capacity)
        return;
    while (graph->nodes[reader].outgoing != NO_EDGE)
        remove_edge(graph, graph->nodes[reader].outgoing);
}

void cetta_producer_dependencies_retire(CettaProducerDependencies *graph,
    size_t vertex) {
    if (vertex >= graph->node_capacity)
        return;
    cetta_producer_dependencies_clear_reads(graph, vertex);
    while (graph->nodes[vertex].incoming != NO_EDGE)
        remove_edge(graph, graph->nodes[vertex].incoming);
}

bool cetta_producer_dependencies_readers(const CettaProducerDependencies *graph,
    size_t producer, CettaProducerReaderFn visit, void *context) {
    if (!visit)
        return false;
    if (producer >= graph->node_capacity)
        return true;
    uint32_t edge = graph->nodes[producer].incoming;
    while (edge != NO_EDGE) {
        if (!visit(context, graph->edges[edge].source))
            return false;
        edge = graph->edges[edge].next_target;
    }
    return true;
}

size_t cetta_producer_dependencies_bytes(const CettaProducerDependencies *graph) {
    return graph->node_capacity * sizeof(*graph->nodes) +
        graph->edge_capacity * sizeof(*graph->edges);
}
