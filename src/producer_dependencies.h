#ifndef CETTA_PRODUCER_DEPENDENCIES_H
#define CETTA_PRODUCER_DEPENDENCIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "storage_census.h"

typedef struct CettaProducerDependencyNode CettaProducerDependencyNode;
typedef struct CettaProducerDependencyEdge CettaProducerDependencyEdge;

/* Vertices are dense, owner-local slots. Retire a vertex before reusing its
 * slot. Edge handles do not escape; cleared edges return to the free pool. */
typedef struct {
    CettaProducerDependencyNode *nodes;
    CettaProducerDependencyEdge *edges;
    size_t node_capacity, edge_capacity, edge_length, live_edges;
    uint32_t free_edge;
} CettaProducerDependencies;

typedef bool (*CettaProducerReaderFn)(void *context, size_t reader);

void cetta_producer_dependencies_init(CettaProducerDependencies *graph);
void cetta_producer_dependencies_free(CettaProducerDependencies *graph);
/* An observed dependency is one edge. Callers that need distinct readers
 * register each pair once. Failure preserves all prior logical edges. */
bool cetta_producer_dependencies_link(CettaProducerDependencies *graph,
    size_t reader, size_t producer);
void cetta_producer_dependencies_clear_reads(CettaProducerDependencies *graph,
    size_t reader);
void cetta_producer_dependencies_retire(CettaProducerDependencies *graph,
    size_t vertex);
/* Visit incoming edges in unspecified order; false stops traversal. The
 * callback must keep the graph and its owner alive and leave it unchanged. */
bool cetta_producer_dependencies_readers(const CettaProducerDependencies *graph,
    size_t producer, CettaProducerReaderFn visit, void *context);
size_t cetta_producer_dependencies_bytes(const CettaProducerDependencies *graph);
void cetta_producer_dependencies_storage_census(
    const CettaProducerDependencies *graph, CettaStorageCensus *census);

#endif
