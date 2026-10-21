#ifndef CETTA_FIXPOINT_H
#define CETTA_FIXPOINT_H

#include <stdbool.h>
#include <stddef.h>

/* Monotone closures over a finite graph, each one worklist pass.
 *
 * The graph has nodes 0 .. nodes - 1 and, for each node n, the edges
 * n -> targets[first_edge[n]] .. targets[first_edge[n + 1] - 1].
 *
 * Least closure: in(n) holds when n is seeded, or when some edge n -> m has
 * in(m).  The set grows backward from the seeds along the reversed edges, and
 * each edge is crossed once: time and space linear in the graph.
 *
 * Greatest closure: in(n) holds when n is admitted and every edge n -> m has
 * in(m).  It is the complement of the least closure seeded with the nodes
 * not admitted. */
typedef struct {
    size_t nodes;
    const size_t *first_edge;  /* nodes + 1 offsets into targets */
    const size_t *targets;
} CettaFixpointGraph;

/* `in` holds the seeds on entry and the least closure on return.  False only
 * when the reversed graph cannot be allocated; `in` is then unchanged. */
bool cetta_fixpoint_least(const CettaFixpointGraph *graph, bool *in);

/* `in` holds the admitted nodes on entry and the greatest closure on return.
 * False only on allocation failure, `in` then unchanged. */
bool cetta_fixpoint_greatest(const CettaFixpointGraph *graph, bool *in);

#endif /* CETTA_FIXPOINT_H */
