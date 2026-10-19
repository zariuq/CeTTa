#include "native_cost_graph.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_metric(CettaCostGraphMetric metric) {
    CettaCostGraphRow root[] = {{1u, 1u}, {2u, 1u}, {3u, SIZE_MAX}};
    CettaCostGraphRow leaf[] = {{4u, SIZE_MAX}, {5u, SIZE_MAX}};
    CettaCostGraphNode nodes[] = {{root, 3u}, {leaf, 2u}};
    CettaCostGraph graph = {nodes, 2u};
    CettaCostGraphAnalysis whole, split;
    assert(cetta_cost_graph_init_metric(&whole, 2u, 0u, metric));
    assert(cetta_cost_graph_init_metric(&split, 2u, 0u, metric));
    cetta_cost_graph_run(&graph, &whole, 50u);
    uint64_t bound = 0u;
    assert(cetta_cost_graph_bound(&whole, &bound) && bound == (metric == CETTA_COST_GRAPH_DEPENDENCY_DEPTH ? 2u : 7u));
    cetta_cost_graph_run(&graph, &split, 2u);
    assert(!cetta_cost_graph_bound(&split, &bound));
    cetta_cost_graph_run(&graph, &split, 48u);
    assert(cetta_cost_graph_bound(&split, &bound) && bound == (metric == CETTA_COST_GRAPH_DEPENDENCY_DEPTH ? 2u : 7u));
    assert(split.ticks == whole.ticks);
    uint64_t depth = 99u;
    assert(cetta_cost_graph_depth_bound(&whole, &depth) ==
        (metric != CETTA_COST_GRAPH_UNFOLDING_WORK));
    assert(metric == CETTA_COST_GRAPH_UNFOLDING_WORK ? depth == 99u : depth == 2u);
    if (metric == CETTA_COST_GRAPH_WORK_DEPTH)
        assert(!memcmp(split.depth_bounds, whole.depth_bounds, 2u * sizeof(uint64_t)));
    else
        assert(!whole.depth_bounds && !split.depth_bounds);
    assert(!memcmp(split.bounds, whole.bounds, 2u * sizeof(uint64_t)));
    cetta_cost_graph_free(&whole);
    cetta_cost_graph_free(&split);
    root[0].successor = 0u;
    assert(cetta_cost_graph_init_metric(&whole, 2u, 0u, metric));
    cetta_cost_graph_run(&graph, &whole, 50u);
    assert(whole.status == CETTA_COST_GRAPH_CYCLE && !cetta_cost_graph_bound(&whole, &bound));
    cetta_cost_graph_free(&whole);
    root[0].successor = 1u;
    root[1].physical = 1u;
    assert(cetta_cost_graph_init_metric(&whole, 2u, 0u, metric));
    cetta_cost_graph_run(&graph, &whole, 50u);
    assert(whole.status == CETTA_COST_GRAPH_INVALID);
    cetta_cost_graph_free(&whole);
    root[1].physical = 2u;
    root[1].successor = 99u;
    assert(cetta_cost_graph_init_metric(&whole, 2u, 0u, metric));
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
    assert(cetta_cost_graph_init_metric(&whole, 65u, 0u, metric));
    cetta_cost_graph_run(&doubling, &whole, 1000u);
    if (metric == CETTA_COST_GRAPH_DEPENDENCY_DEPTH)
        assert(cetta_cost_graph_bound(&whole, &bound) && bound == 65u);
    else
        assert(whole.status == CETTA_COST_GRAPH_OVERFLOW && !cetta_cost_graph_bound(&whole, &bound));
    cetta_cost_graph_free(&whole);
    CettaCostGraphNode empty_node = {0};
    CettaCostGraph empty = {&empty_node, 1u};
    assert(cetta_cost_graph_init_metric(&whole, 1u, 0u, metric));
    cetta_cost_graph_run(&empty, &whole, 1u);
    assert(cetta_cost_graph_bound(&whole, &bound) && bound == 0u);
    cetta_cost_graph_free(&whole);
}

int main(void) {
    check_metric(CETTA_COST_GRAPH_UNFOLDING_WORK);
    check_metric(CETTA_COST_GRAPH_DEPENDENCY_DEPTH);
    check_metric(CETTA_COST_GRAPH_WORK_DEPTH);
    CettaCostGraphAnalysis legacy;
    assert(cetta_cost_graph_init(&legacy, 1u, 0u));
    assert(legacy.metric == CETTA_COST_GRAPH_UNFOLDING_WORK);
    cetta_cost_graph_free(&legacy);
    CettaCostGraphAnalysis untouched = {.ticks=17u};
    CettaCostGraphAnalysis before = untouched;
    assert(!cetta_cost_graph_init_metric(&untouched, 1u, 0u, (CettaCostGraphMetric)3));
    assert(!memcmp(&untouched, &before, sizeof(before)));
    puts("PASS: occurrence work/depth folds, shared subgraph, resume, cycle, overflow and invalid-row controls");
    return 0;
}
