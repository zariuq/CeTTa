#define _POSIX_C_SOURCE 200809L
#include "term_graph.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static bool leaf_equal(Atom *left, Atom *right) {
    return left->kind == ATOM_VAR && right->kind == ATOM_VAR &&
           left->var_id == right->var_id;
}

static uint64_t nanoseconds(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("usage: bench_term_match_graph DEPTH REPETITIONS MODE(0|1|2)\n", stderr);
        return 2;
    }
    size_t depth = strtoull(argv[1], NULL, 10);
    size_t repetitions = strtoull(argv[2], NULL, 10);
    unsigned mode = (unsigned)strtoul(argv[3], NULL, 10);
    if (!repetitions || mode > CETTA_TERM_MATCH_VARIANT ||
        depth > (SIZE_MAX / sizeof(Atom) - 1u) / 2u)
        return 2;
    Atom *nodes = calloc(2u * (depth + 1u), sizeof(*nodes));
    Atom **children = calloc(4u * (depth + 1u), sizeof(*children));
    assert(nodes && children);
    Atom *left = nodes, *right = nodes + depth + 1u;
    left[0].kind = right[0].kind = ATOM_VAR;
    left[0].var_id = 1u; right[0].var_id = 2u;
    for (size_t i = 1u; i <= depth; ++i) {
        left[i].kind = right[i].kind = ATOM_EXPR;
        left[i].expr.len = right[i].expr.len = 2u;
        left[i].expr.elems = &children[4u * i];
        right[i].expr.elems = &children[4u * i + 2u];
        left[i].expr.elems[0] = left[i].expr.elems[1] = &left[i - 1u];
        right[i].expr.elems[0] = right[i].expr.elems[1] = &right[i - 1u];
    }
    CettaTermMatchPair pair = {&left[depth], &right[depth]};
    size_t retained = 0u;
    test_runtime_stats_reset_counters();
    uint64_t started = nanoseconds();
    for (size_t i = 0u; i < repetitions; ++i) {
        Arena arena;
        arena_init(&arena);
        CettaTermMatch result;
        assert(term_graph_match_many(&arena, &pair, 1u, mode,
                                     leaf_equal, &result) == CETTA_TERM_MATCH_OK);
        bool reverse = mode == CETTA_TERM_MATCH_REVERSE;
        assert(result.count == 1u);
        assert(result.variables[0] == (reverse ? right : left));
        assert(result.values[0] == (reverse ? left : right));
        size_t bytes = arena_accounted_live_bytes(&arena);
        if (bytes > retained) retained = bytes;
        arena_free(&arena);
    }
    uint64_t elapsed = nanoseconds() - started;
    uint64_t pairs = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_MATCH_PAIR_VISIT);
    uint64_t subjects = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_TERM_MATCH_SUBJECT_VIEW);
    assert(pairs <= repetitions * (3u * depth + 4u));
    assert(subjects <= repetitions * (3u * depth + 4u));
    printf("{\"depth\":%zu,\"repetitions\":%zu,\"mode\":%u,"
           "\"pair_visits\":%" PRIu64 ",\"subject_views\":%" PRIu64 ","
           "\"published_arena_peak_bytes\":%zu,\"elapsed_ns\":%" PRIu64 "}\n",
           depth, repetitions, mode, pairs, subjects, retained, elapsed);
    free(children); free(nodes);
    return 0;
}
