#include "native_cost_graph.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    CettaCostGraphRow root[] = {{1u, 1u}, {2u, 1u}, {3u, SIZE_MAX}};
    CettaCostGraphRow leaf[] = {{4u, SIZE_MAX}, {5u, SIZE_MAX}};
    CettaCostGraphNode nodes[] = {{root, 3u}, {leaf, 2u}};
    CettaCostGraph graph = {nodes, 2u};
    CettaCostGraphAnalysis whole, split;
    assert(cetta_cost_graph_init(&whole, 2u, 0u));
    assert(cetta_cost_graph_init(&split, 2u, 0u));
    cetta_cost_graph_run(&graph, &whole, 50u);
    uint64_t bound = 0u;
    assert(cetta_cost_graph_bound(&whole, &bound) && bound == 7u);
    cetta_cost_graph_run(&graph, &split, 2u);
    assert(!cetta_cost_graph_bound(&split, &bound));
    cetta_cost_graph_run(&graph, &split, 48u);
    assert(cetta_cost_graph_bound(&split, &bound) && bound == 7u);
    assert(split.ticks == whole.ticks);
    assert(!memcmp(split.bounds, whole.bounds, 2u * sizeof(uint64_t)));
    cetta_cost_graph_free(&whole);
    cetta_cost_graph_free(&split);
    root[0].successor = 0u;
    assert(cetta_cost_graph_init(&whole, 2u, 0u));
    cetta_cost_graph_run(&graph, &whole, 50u);
    assert(whole.status == CETTA_COST_GRAPH_CYCLE && !cetta_cost_graph_bound(&whole, &bound));
    cetta_cost_graph_free(&whole);
    root[0].successor = 1u;
    root[1].physical = 1u;
    assert(cetta_cost_graph_init(&whole, 2u, 0u));
    cetta_cost_graph_run(&graph, &whole, 50u);
    assert(whole.status == CETTA_COST_GRAPH_INVALID);
    cetta_cost_graph_free(&whole);
    root[1].physical = 2u;
    root[1].successor = 99u;
    assert(cetta_cost_graph_init(&whole, 2u, 0u));
    cetta_cost_graph_run(&graph, &whole, 50u);
    assert(whole.status == CETTA_COST_GRAPH_INVALID);
    cetta_cost_graph_free(&whole);
    CettaCostGraphRow doubling_rows[65][2];
    CettaCostGraphNode doubling_nodes[65];
    for (size_t i = 0u; i < 65u; i++) {
        size_t next = i == 64u ? SIZE_MAX : i + 1u;
        doubling_rows[i][0] = (CettaCostGraphRow){2u * i + 1u, next};
        doubling_rows[i][1] = (CettaCostGraphRow){2u * i + 2u, next};
        doubling_nodes[i] = (CettaCostGraphNode){doubling_rows[i], 2u};
    }
    CettaCostGraph doubling = {doubling_nodes, 65u};
    assert(cetta_cost_graph_init(&whole, 65u, 0u));
    cetta_cost_graph_run(&doubling, &whole, 1000u);
    assert(whole.status == CETTA_COST_GRAPH_OVERFLOW && !cetta_cost_graph_bound(&whole, &bound));
    cetta_cost_graph_free(&whole);
    puts("PASS: physical-row DAG recurrence, shared subgraph, resume, cycle, overflow and invalid-row controls");
    return 0;
}
