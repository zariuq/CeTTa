/* The worklist closures against the naive changed-loop definitions, on fixed
 * controls (empty graph, chain, unseeded and seeded cycles, self-loops,
 * repeated edges) and on seeded random graphs. */
#include "fixpoint.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(test, label) do { checks++; if (!(test)) { failures++; \
    fprintf(stderr, "FAIL: %s\n", label); } } while (0)

static void least_reference(const CettaFixpointGraph *g, bool *in) {
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t n = 0u; n < g->nodes; n++)
            for (size_t e = g->first_edge[n]; !in[n] && e < g->first_edge[n + 1u]; e++)
                if (in[g->targets[e]])
                    in[n] = changed = true;
    }
}

static void greatest_reference(const CettaFixpointGraph *g, bool *in) {
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t n = 0u; n < g->nodes; n++)
            for (size_t e = g->first_edge[n]; in[n] && e < g->first_edge[n + 1u]; e++)
                if (!in[g->targets[e]]) {
                    in[n] = false;
                    changed = true;
                }
    }
}

/* Both closures from `seed`, against both references. */
static void agree(const CettaFixpointGraph *g, const bool *seed, const char *what) {
    bool *a = malloc(g->nodes + 1u), *b = malloc(g->nodes + 1u);
    char label[128];
    memcpy(a, seed, g->nodes);
    memcpy(b, seed, g->nodes);
    snprintf(label, sizeof label, "%s: least closure", what);
    CHECK(cetta_fixpoint_least(g, a), label);
    least_reference(g, b);
    CHECK(!memcmp(a, b, g->nodes), label);
    memcpy(a, seed, g->nodes);
    memcpy(b, seed, g->nodes);
    snprintf(label, sizeof label, "%s: greatest closure", what);
    CHECK(cetta_fixpoint_greatest(g, a), label);
    greatest_reference(g, b);
    CHECK(!memcmp(a, b, g->nodes), label);
    free(a);
    free(b);
}

int main(void) {
    /* Empty graph. */
    size_t none[1] = {0u};
    CettaFixpointGraph empty = {0u, none, NULL};
    CHECK(cetta_fixpoint_least(&empty, NULL) && cetta_fixpoint_greatest(&empty, NULL),
          "an empty graph closes");

    /* Chain 0 -> 1 -> 2 -> 3 seeded at 3: every node reaches it. */
    size_t chain_first[] = {0, 1, 2, 3, 3}, chain_targets[] = {1, 2, 3};
    CettaFixpointGraph chain = {4u, chain_first, chain_targets};
    bool in[4] = {false, false, false, true};
    CHECK(cetta_fixpoint_least(&chain, in) && in[0] && in[1] && in[2] && in[3],
          "a chain reaches its seed from every node");
    /* Admitting all but 3 leaves nothing: each node needs its successor. */
    bool admitted[4] = {true, true, true, false};
    CHECK(cetta_fixpoint_greatest(&chain, admitted) &&
              !admitted[0] && !admitted[1] && !admitted[2] && !admitted[3],
          "a chain into an unadmitted node keeps none");

    /* Cycle 0 -> 1 -> 0 and 2 -> 2: unseeded, the least closure is empty;
     * all admitted, the greatest keeps everything. */
    size_t cycle_first[] = {0, 1, 2, 3}, cycle_targets[] = {1, 0, 2};
    CettaFixpointGraph cycle = {3u, cycle_first, cycle_targets};
    bool unseeded[3] = {false, false, false};
    CHECK(cetta_fixpoint_least(&cycle, unseeded) &&
              !unseeded[0] && !unseeded[1] && !unseeded[2],
          "an unseeded cycle stays out of the least closure");
    bool all[3] = {true, true, true};
    CHECK(cetta_fixpoint_greatest(&cycle, all) && all[0] && all[1] && all[2],
          "an admitted cycle stays in the greatest closure");
    bool seeded[3] = {false, true, false};
    CHECK(cetta_fixpoint_least(&cycle, seeded) && seeded[0] && seeded[1] && !seeded[2],
          "a seeded cycle closes over its members only");

    /* Repeated edges change nothing. */
    size_t repeat_first[] = {0, 3, 3}, repeat_targets[] = {1, 1, 1};
    CettaFixpointGraph repeat = {2u, repeat_first, repeat_targets};
    bool rep[2] = {false, true};
    CHECK(cetta_fixpoint_least(&repeat, rep) && rep[0], "repeated edges close once");

    /* Seeded random graphs, sparse to dense, against the references. */
    uint64_t x = UINT64_C(0x2545F4914F6CDD1D);
    for (unsigned round = 0u; round < 300u; round++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        size_t nodes = 1u + (size_t)(x % 64u);
        size_t degree = (size_t)((x >> 8) % 5u);
        size_t *first = malloc((nodes + 1u) * sizeof(*first));
        size_t *targets = malloc((nodes * degree + 1u) * sizeof(*targets));
        bool *seed = malloc(nodes);
        size_t edges = 0u;
        for (size_t n = 0u; n < nodes; n++) {
            first[n] = edges;
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            size_t out = (size_t)(x % (degree + 1u));
            for (size_t k = 0u; k < out; k++) {
                x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                targets[edges++] = (size_t)(x % nodes);
            }
            seed[n] = (x >> 40) % 4u == 0u;
        }
        first[nodes] = edges;
        CettaFixpointGraph g = {nodes, first, targets};
        agree(&g, seed, "random graph");
        free(first);
        free(targets);
        free(seed);
    }
    printf("fixpoint: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
