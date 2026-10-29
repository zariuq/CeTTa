/* The dense relation store against a model: a plain array of the pairs
 * held.  Random adds, removals, membership tests, counts and walks with
 * either column fixed or both free must agree with the model at every step,
 * over ids drawn from a small and from a sparse range, 0 among them, and
 * with first columns many and second columns few, or first columns few and
 * second columns many: rows that stay sorted arrays, rows that turn to
 * bits, and rows that removals empty and adds fill again. */
#include "space_dense.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { CettaDenseId a, b; } Pair;
typedef struct { Pair *items; size_t len, cap; } Model;

static size_t model_find(const Model *m, CettaDenseId a, CettaDenseId b) {
    for (size_t i = 0u; i < m->len; i++)
        if (m->items[i].a == a && m->items[i].b == b)
            return i;
    return SIZE_MAX;
}

static uint64_t rng_state = 0x243f6a8885a308d3u;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

typedef struct { const Model *model; size_t seen; bool ok; } WalkCheck;
static bool walk_visit(void *context, CettaDenseId a, CettaDenseId b) {
    WalkCheck *w = context;
    if (model_find(w->model, a, b) == SIZE_MAX)
        w->ok = false;
    w->seen++;
    return true;
}

static uint64_t model_count(const Model *m, CettaDenseId a, CettaDenseId b) {
    uint64_t n = 0u;
    for (size_t i = 0u; i < m->len; i++)
        n += (a == CETTA_DENSE_ANY || m->items[i].a == a) &&
             (b == CETTA_DENSE_ANY || m->items[i].b == b);
    return n;
}

static void run(CettaDenseId range, CettaDenseId b_range, CettaDenseId stride,
                unsigned steps) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    Model m = {0};
    for (unsigned step = 0u; step < steps; step++) {
        CettaDenseId a = (rng() % range) * stride;
        CettaDenseId b = (rng() % b_range) * stride;
        unsigned op = (unsigned)(rng() % 10u);
        if (op < 5u) {
            bool oom = false;
            bool added = cetta_dense_relation_add(&rel, a, b, &oom);
            assert(!oom);
            size_t at = model_find(&m, a, b);
            assert(added == (at == SIZE_MAX));
            if (added) {
                if (m.len == m.cap) {
                    m.cap = m.cap ? m.cap * 2u : 64u;
                    m.items = realloc(m.items, m.cap * sizeof(*m.items));
                }
                m.items[m.len++] = (Pair){a, b};
            }
        } else if (op < 8u) {
            bool removed = cetta_dense_relation_remove(&rel, a, b);
            size_t at = model_find(&m, a, b);
            assert(removed == (at != SIZE_MAX));
            if (removed)
                m.items[at] = m.items[--m.len];
        } else {
            assert(cetta_dense_relation_contains(&rel, a, b) ==
                   (model_find(&m, a, b) != SIZE_MAX));
        }
        assert(cetta_dense_relation_pairs(&rel) == m.len);
        if (step % 97u == 0u) {
            CettaDenseId fixes[4][2] = {
                {CETTA_DENSE_ANY, CETTA_DENSE_ANY}, {a, CETTA_DENSE_ANY},
                {CETTA_DENSE_ANY, b}, {a, b}};
            for (int f = 0; f < 4; f++) {
                WalkCheck w = {&m, 0u, true};
                assert(cetta_dense_relation_each(&rel, fixes[f][0], fixes[f][1],
                                                 walk_visit, &w));
                uint64_t expected = model_count(&m, fixes[f][0], fixes[f][1]);
                assert(w.ok && w.seen == expected);
                assert(cetta_dense_relation_count(&rel, fixes[f][0],
                                                  fixes[f][1]) == expected);
            }
        }
    }
    /* Every numbered id appears in some pair added at some point. */
    size_t numbered = 0u;
    for (uint32_t n = 0u; n < rel.len; n++)
        numbered += cetta_dense_relation_number(&rel, rel.ids[n]) == n;
    assert(numbered == rel.len);
    printf("PASS: dense relation agrees with its model over %u steps "
           "(columns of %llu and %llu ids, %llu pairs held, %llu bytes)\n",
           steps, (unsigned long long)range, (unsigned long long)b_range,
           (unsigned long long)m.len,
           (unsigned long long)cetta_dense_relation_bytes(&rel));
    cetta_dense_relation_free(&rel);
    free(m.items);
}

int main(void) {
    run(16u, 16u, 1u, 20000u);
    run(200u, 200u, 1u, 50000u);
    run(300u, 300u, 1000003u, 50000u);
    run(3000u, 3u, 1u, 30000u);
    run(4u, 5000u, 7u, 30000u);
    return 0;
}
