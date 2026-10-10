#include "producer_dependencies.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation, fail_at;
void *__real_calloc(size_t count, size_t width);
void *__wrap_calloc(size_t count, size_t width) {
    allocation++;
    return fail_at && allocation == fail_at ? NULL : __real_calloc(count, width);
}

enum { VERTICES = 64 };
static unsigned reference[VERTICES][VERTICES];

typedef struct {
    size_t producer;
    unsigned seen[VERTICES];
} Visit;

static bool visit(void *context, size_t source) {
    Visit *observation = context;
    assert(source < VERTICES);
    observation->seen[source]++;
    return observation->seen[source] <= reference[source][observation->producer];
}

static bool stop(void *context, size_t source) {
    (void)source;
    (*(size_t *)context)++;
    return false;
}

static void compare(const CettaProducerDependencies *graph) {
    size_t edges = 0u;
    for (size_t target = 0u; target < VERTICES; target++) {
        Visit observation = {.producer = target};
        assert(cetta_producer_dependencies_readers(graph, target, visit, &observation));
        for (size_t source = 0u; source < VERTICES; source++) {
            assert(observation.seen[source] == reference[source][target]);
            edges += reference[source][target];
        }
    }
    assert(graph->live_edges == edges);
}

static void clear_reference(size_t vertex, bool retire) {
    for (size_t other = 0u; other < VERTICES; other++) {
        reference[vertex][other] = 0u;
        if (retire)
            reference[other][vertex] = 0u;
    }
}

int main(void) {
    CettaProducerDependencies graph;
    cetta_producer_dependencies_init(&graph);
    assert(cetta_producer_dependencies_bytes(&graph) == 0u);
    assert(!cetta_producer_dependencies_readers(&graph, 0u, NULL, NULL));
    for (size_t failed = 1u; failed <= 2u; failed++) {
        fail_at = failed;
        allocation = 0u;
        assert(!cetta_producer_dependencies_link(&graph, 0u, 1u));
        compare(&graph);
        fail_at = 0u;
        assert(cetta_producer_dependencies_link(&graph, 0u, 1u));
        reference[0][1]++;
        compare(&graph);
        size_t visited = 0u;
        assert(!cetta_producer_dependencies_readers(&graph, 1u, stop, &visited));
        assert(visited == 1u);
        cetta_producer_dependencies_free(&graph);
        memset(reference, 0, sizeof(reference));
    }
    assert(!cetta_producer_dependencies_link(&graph, UINT32_MAX, 0u));
    assert(!cetta_producer_dependencies_link(&graph, 0u, SIZE_MAX));
    compare(&graph);

    uint32_t random = 29u;
    for (size_t event = 0u; event < 10000u; event++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        size_t source = (random >> 8u) % VERTICES;
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        size_t target = (random >> 8u) % VERTICES;
        switch (event % 13u) {
        case 0:
            cetta_producer_dependencies_clear_reads(&graph, source);
            clear_reference(source, false);
            break;
        case 1:
            cetta_producer_dependencies_retire(&graph, source);
            clear_reference(source, true);
            break;
        default:
            fail_at = event % 101u == 0u ? allocation + 1u : 0u;
            if (cetta_producer_dependencies_link(&graph, source, target))
                reference[source][target]++;
            fail_at = 0u;
            break;
        }
        compare(&graph);
    }
    for (size_t vertex = 0u; vertex < VERTICES; vertex++) {
        cetta_producer_dependencies_retire(&graph, vertex);
        clear_reference(vertex, true);
        compare(&graph);
    }
    size_t retained = cetta_producer_dependencies_bytes(&graph);
    size_t before = allocation;
    for (size_t iteration = 0u; iteration < 10000u; iteration++) {
        assert(cetta_producer_dependencies_link(&graph, 0u, 0u));
        reference[0][0]++;
        compare(&graph);
        cetta_producer_dependencies_retire(&graph, 0u);
        clear_reference(0u, true);
        compare(&graph);
    }
    assert(allocation == before && cetta_producer_dependencies_bytes(&graph) == retained);
    cetta_producer_dependencies_free(&graph);
    assert(graph.live_edges == 0u && cetta_producer_dependencies_bytes(&graph) == 0u);
    puts("PASS: reverse dependencies match an independent matrix through 10000 updates and 10000 self-edge retirements");
    puts("PASS: allocation failure, visitor cancellation, slot reuse and complete release preserve the dependency graph");
    return 0;
}
