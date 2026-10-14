#ifndef CETTA_NATIVE_COST_GRAPH_H
#define CETTA_NATIVE_COST_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Physical source rows stay distinct even when their right sides agree.
 * SIZE_MAX denotes an operationally inert terminal, established by the source
 * adapter. The graph alone does not certify source coverage or inertness. */
typedef struct {
    uint64_t physical;
    size_t successor;
} CettaCostGraphRow;

typedef struct {
    CettaCostGraphRow *rows;
    size_t length;
} CettaCostGraphNode;

typedef struct {
    CettaCostGraphNode *nodes;
    size_t length;
} CettaCostGraph;

typedef enum {
    CETTA_COST_GRAPH_PENDING,
    CETTA_COST_GRAPH_BOUND,
    CETTA_COST_GRAPH_CYCLE,
    CETTA_COST_GRAPH_INVALID,
    CETTA_COST_GRAPH_OVERFLOW
} CettaCostGraphStatus;

typedef struct {
    size_t node;
    size_t row;
    uint64_t total;
} CettaCostGraphFrame;

typedef struct {
    unsigned char *colors;
    uint64_t *bounds;
    CettaCostGraphFrame *stack;
    size_t capacity;
    size_t depth;
    size_t root;
    uint64_t ticks;
    CettaCostGraphStatus status;
} CettaCostGraphAnalysis;

bool cetta_cost_graph_init(CettaCostGraphAnalysis *analysis, size_t nodes, size_t root);
void cetta_cost_graph_free(CettaCostGraphAnalysis *analysis);
/* One graph-row or completed-node operation, with the entire DFS cursor and
 * memo retained. Cycle/invalid/overflow are unknown bounds, not source closure. */
void cetta_cost_graph_step(const CettaCostGraph *graph, CettaCostGraphAnalysis *analysis);
void cetta_cost_graph_run(const CettaCostGraph *graph,
    CettaCostGraphAnalysis *analysis, uint64_t budget);
bool cetta_cost_graph_bound(const CettaCostGraphAnalysis *analysis, uint64_t *bound);

#endif
