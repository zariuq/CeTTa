#include "native_cost_graph.h"
#include "native_cost.h"

#include <stdlib.h>
#include <string.h>

bool cetta_cost_graph_init(CettaCostGraphAnalysis *analysis, size_t nodes, size_t root) {
    if (!analysis || root >= nodes ||
        nodes > SIZE_MAX / sizeof(*analysis->stack) ||
        nodes > SIZE_MAX / sizeof(*analysis->bounds))
        return false;
    memset(analysis, 0, sizeof(*analysis));
    analysis->colors = calloc(nodes, sizeof(*analysis->colors));
    analysis->bounds = calloc(nodes, sizeof(*analysis->bounds));
    analysis->stack = calloc(nodes, sizeof(*analysis->stack));
    if (!analysis->colors || !analysis->bounds || !analysis->stack) {
        cetta_cost_graph_free(analysis);
        return false;
    }
    analysis->capacity = nodes;
    analysis->root = root;
    analysis->colors[root] = 1u;
    analysis->stack[0].node = root;
    analysis->depth = 1u;
    analysis->status = CETTA_COST_GRAPH_PENDING;
    return true;
}

void cetta_cost_graph_free(CettaCostGraphAnalysis *analysis) {
    if (!analysis)
        return;
    free(analysis->colors);
    free(analysis->bounds);
    free(analysis->stack);
    memset(analysis, 0, sizeof(*analysis));
}

void cetta_cost_graph_step(const CettaCostGraph *graph, CettaCostGraphAnalysis *analysis) {
    if (!analysis || analysis->status != CETTA_COST_GRAPH_PENDING)
        return;
    if (!graph || !graph->nodes || graph->length != analysis->capacity ||
        !analysis->depth || analysis->ticks == UINT64_MAX) {
        analysis->status = CETTA_COST_GRAPH_INVALID;
        return;
    }
    analysis->ticks++;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    CettaCostGraphFrame *frame = &analysis->stack[analysis->depth - 1u];
    const CettaCostGraphNode *node = &graph->nodes[frame->node];
    if (node->length && !node->rows) {
        analysis->status = CETTA_COST_GRAPH_INVALID;
        return;
    }
    if (frame->row == node->length) {
        analysis->bounds[frame->node] = frame->total;
        analysis->colors[frame->node] = 2u;
        analysis->depth--;
        if (!analysis->depth)
            analysis->status = CETTA_COST_GRAPH_BOUND;
        return;
    }
    const CettaCostGraphRow *row = &node->rows[frame->row];
    if (!row->physical || (row->successor != SIZE_MAX && row->successor >= graph->length)) {
        analysis->status = CETTA_COST_GRAPH_INVALID;
        return;
    }
    for (size_t i = 0u; i < frame->row; i++) {
        cetta_native_cost_charge(CETTA_COST_INDEX_VISIT, 1u);
        if (node->rows[i].physical == row->physical) {
            analysis->status = CETTA_COST_GRAPH_INVALID;
            return;
        }
    }
    uint64_t contribution = 1u;
    if (row->successor != SIZE_MAX) {
        unsigned char color = analysis->colors[row->successor];
        if (color == 1u) {
            analysis->status = CETTA_COST_GRAPH_CYCLE;
            return;
        }
        if (color == 0u) {
            if (analysis->depth == analysis->capacity) {
                analysis->status = CETTA_COST_GRAPH_INVALID;
                return;
            }
            analysis->colors[row->successor] = 1u;
            analysis->stack[analysis->depth++] = (CettaCostGraphFrame){.node = row->successor};
            return;
        }
        if (analysis->bounds[row->successor] == UINT64_MAX) {
            analysis->status = CETTA_COST_GRAPH_OVERFLOW;
            return;
        }
        contribution += analysis->bounds[row->successor];
    }
    if (contribution > UINT64_MAX - frame->total) {
        analysis->status = CETTA_COST_GRAPH_OVERFLOW;
        return;
    }
    frame->total += contribution;
    frame->row++;
}

void cetta_cost_graph_run(const CettaCostGraph *graph,
        CettaCostGraphAnalysis *analysis, uint64_t budget) {
    while (analysis && analysis->status == CETTA_COST_GRAPH_PENDING && budget) {
        cetta_cost_graph_step(graph, analysis);
        budget--;
    }
}

bool cetta_cost_graph_bound(const CettaCostGraphAnalysis *analysis, uint64_t *bound) {
    if (!analysis || !bound || analysis->status != CETTA_COST_GRAPH_BOUND)
        return false;
    *bound = analysis->bounds[analysis->root];
    return true;
}
