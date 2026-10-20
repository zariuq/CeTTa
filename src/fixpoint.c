#include "fixpoint.h"

#include <stdlib.h>
#include <string.h>

/* The reversed edges: for each node m, the nodes n with an edge n -> m. */
typedef struct {
    size_t *first;
    size_t *sources;
} FixpointReversed;

static bool fixpoint_reverse(const CettaFixpointGraph *graph,
                             FixpointReversed *out) {
    size_t nodes = graph->nodes;
    size_t edges = graph->first_edge[nodes];
    out->first = calloc(nodes + 1u, sizeof(*out->first));
    out->sources = malloc((edges ? edges : 1u) * sizeof(*out->sources));
    if (!out->first || !out->sources) {
        free(out->first);
        free(out->sources);
        return false;
    }
    for (size_t e = 0u; e < edges; e++)
        out->first[graph->targets[e] + 1u]++;
    for (size_t m = 0u; m < nodes; m++)
        out->first[m + 1u] += out->first[m];
    size_t *fill = malloc((nodes ? nodes : 1u) * sizeof(*fill));
    if (!fill) {
        free(out->first);
        free(out->sources);
        return false;
    }
    memcpy(fill, out->first, nodes * sizeof(*fill));
    for (size_t n = 0u; n < nodes; n++)
        for (size_t e = graph->first_edge[n]; e < graph->first_edge[n + 1u]; e++)
            out->sources[fill[graph->targets[e]]++] = n;
    free(fill);
    return true;
}

bool cetta_fixpoint_least(const CettaFixpointGraph *graph, bool *in) {
    size_t nodes = graph->nodes;
    if (nodes == 0u)
        return true;
    FixpointReversed reversed;
    size_t *work = malloc(nodes * sizeof(*work));
    if (!work || !fixpoint_reverse(graph, &reversed)) {
        free(work);
        return false;
    }
    size_t len = 0u;
    for (size_t n = 0u; n < nodes; n++)
        if (in[n])
            work[len++] = n;
    while (len > 0u) {
        size_t m = work[--len];
        for (size_t s = reversed.first[m]; s < reversed.first[m + 1u]; s++) {
            size_t n = reversed.sources[s];
            if (!in[n]) {
                in[n] = true;
                work[len++] = n;
            }
        }
    }
    free(work);
    free(reversed.first);
    free(reversed.sources);
    return true;
}

bool cetta_fixpoint_greatest(const CettaFixpointGraph *graph, bool *in) {
    size_t nodes = graph->nodes;
    bool *out = malloc((nodes ? nodes : 1u) * sizeof(*out));
    if (!out)
        return false;
    for (size_t n = 0u; n < nodes; n++)
        out[n] = !in[n];
    if (!cetta_fixpoint_least(graph, out)) {
        free(out);
        return false;
    }
    for (size_t n = 0u; n < nodes; n++)
        in[n] = !out[n];
    free(out);
    return true;
}
