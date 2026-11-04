/* The dense relation store against a model: a plain array of the pairs
 * held.  Random adds, removals, membership tests, counts and walks with
 * either column fixed or both free must agree with the model at every step,
 * over ids drawn from a small and from a sparse range, 0 among them, and
 * with first columns many and second columns few, or first columns few and
 * second columns many: rows that stay sorted arrays, rows that turn to
 * bits, and rows that removals empty and adds fill again.  The ids the
 * relation tells its caller it holds are exactly the ids the pairs hold;
 * trimming after each removal keeps the dead numbers within twice the
 * columns the pairs fill plus 64, and a dead number keeps no row: through
 * random runs, a relation emptied down to a few pairs, and churn over ids
 * never seen before. */
#include "space_dense.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* The ids the caller holds, as the relation told it, kept by a small open
 * table since ids may be sparse. */
typedef struct {
    CettaDenseId *ids;
    uint8_t *state;
    size_t cap, len;
    bool ok;
} Held;

static size_t held_slot(Held *h, CettaDenseId id) {
    if ((h->len + 1u) * 2u > h->cap) {
        Held grown = {.cap = h->cap ? h->cap * 2u : 1024u, .ok = h->ok};
        grown.ids = calloc(grown.cap, sizeof(*grown.ids));
        grown.state = calloc(grown.cap, 1u);
        for (size_t i = 0u; i < h->cap; i++) {
            if (!h->state[i])
                continue;
            size_t s = (h->ids[i] * 0x9E3779B97F4A7C15u >> 20) & (grown.cap - 1u);
            while (grown.state[s])
                s = (s + 1u) & (grown.cap - 1u);
            grown.ids[s] = h->ids[i];
            grown.state[s] = h->state[i];
            grown.len++;
        }
        free(h->ids);
        free(h->state);
        *h = grown;
    }
    size_t s = (id * 0x9E3779B97F4A7C15u >> 20) & (h->cap - 1u);
    while (h->state[s] && h->ids[s] != id)
        s = (s + 1u) & (h->cap - 1u);
    if (!h->state[s]) {
        h->ids[s] = id;
        h->state[s] = 1u;
        h->len++;
    }
    return s;
}

/* State 1: known, not held; 2: held. */
static void held_mark(Held *h, CettaDenseId id, uint8_t held) {
    size_t s = held_slot(h, id);
    if ((h->state[s] == 2u) == (held != 0u))
        h->ok = false;
    h->state[s] = held ? 2u : 1u;
}

static void on_held(void *context, CettaDenseId id) {
    held_mark(context, id, 1u);
}

static void on_released(void *context, CettaDenseId id) {
    held_mark(context, id, 0u);
}

static bool held_has(Held *h, CettaDenseId id) {
    return h->state[held_slot(h, id)] == 2u;
}

/* The caller holds exactly the ids the pairs hold, the relation lists
 * exactly those, and a dead number keeps no row. */
static void check_held(const CettaDenseRelation *rel, const Model *m,
                       Held *held) {
    assert(held->ok);
    Held listed = {.ok = true};
    cetta_dense_relation_each_numbered(rel, on_held, &listed);
    assert(listed.ok);
    Held expected = {.ok = true};
    for (size_t i = 0u; i < m->len; i++) {
        size_t s = held_slot(&expected, m->items[i].a);
        expected.state[s] = 2u;
        s = held_slot(&expected, m->items[i].b);
        expected.state[s] = 2u;
    }
    size_t live = 0u;
    for (size_t i = 0u; i < expected.cap; i++) {
        if (expected.state[i] != 2u)
            continue;
        live++;
        assert(held_has(held, expected.ids[i]));
        assert(held_has(&listed, expected.ids[i]));
    }
    size_t told = 0u, listed_count = 0u;
    for (size_t i = 0u; i < held->cap; i++)
        told += held->state[i] == 2u;
    for (size_t i = 0u; i < listed.cap; i++)
        listed_count += listed.state[i] == 2u;
    assert(told == live && listed_count == live);
    assert(rel->len - rel->dead == live);
    for (uint32_t n = 0u; n < rel->len; n++) {
        if (rel->holds[n])
            continue;
        assert(n >= rel->first.row_cap || !rel->first.rows[n]);
        assert(n >= rel->second.row_cap || !rel->second.rows[n]);
    }
    free(listed.ids);
    free(listed.state);
    free(expected.ids);
    free(expected.state);
}

static void check_trimmed(const CettaDenseRelation *rel) {
    assert((uint64_t)rel->dead <= 4u * rel->pairs + 64u);
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
    Held held = {.ok = true};
    for (unsigned step = 0u; step < steps; step++) {
        CettaDenseId a = (rng() % range) * stride;
        CettaDenseId b = (rng() % b_range) * stride;
        unsigned op = (unsigned)(rng() % 10u);
        if (op < 5u) {
            bool oom = false;
            bool added = cetta_dense_relation_add(&rel, a, b, &oom, on_held,
                                                  &held);
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
            bool removed = cetta_dense_relation_remove(&rel, a, b, on_released,
                                                       &held);
            size_t at = model_find(&m, a, b);
            assert(removed == (at != SIZE_MAX));
            if (removed)
                m.items[at] = m.items[--m.len];
            assert(cetta_dense_relation_trim(&rel));
            check_trimmed(&rel);
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
        if (step % 997u == 0u)
            check_held(&rel, &m, &held);
    }
    check_held(&rel, &m, &held);
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
    free(held.ids);
    free(held.state);
}

static void model_add(Model *m, CettaDenseId a, CettaDenseId b) {
    if (m->len == m->cap) {
        m->cap = m->cap ? m->cap * 2u : 64u;
        m->items = realloc(m->items, m->cap * sizeof(*m->items));
    }
    m->items[m->len++] = (Pair){a, b};
}

/* Fill with FILL pairs (each first column with a few seconds, so rows turn
 * to bits too), probe by the second column until its side is kept, then
 * remove all but KEEP: the numbering comes down to the few, and the pairs
 * left answer as before from both sides. */
static void drain(unsigned fill, unsigned keep) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    Model m = {0};
    Held held = {.ok = true};
    for (unsigned i = 0u; m.len < fill; i++) {
        CettaDenseId a = i / 8u, b = (i * 7919u) % (fill / 2u);
        bool oom = false;
        if (model_find(&m, a, b) != SIZE_MAX)
            continue;
        assert(cetta_dense_relation_add(&rel, a, b, &oom, on_held, &held));
        model_add(&m, a, b);
    }
    uint64_t expected_column = model_count(&m, CETTA_DENSE_ANY, m.items[0].b);
    for (uint64_t probe = 0u; probe <= rel.pairs && !rel.second_kept; probe++)
        assert(cetta_dense_relation_count(&rel, CETTA_DENSE_ANY, m.items[0].b) ==
               expected_column);
    assert(rel.second_kept);
    uint64_t full = cetta_dense_relation_bytes(&rel);
    while (m.len > keep) {
        size_t at = rng() % m.len;
        Pair p = m.items[at];
        assert(cetta_dense_relation_remove(&rel, p.a, p.b, on_released, &held));
        m.items[at] = m.items[--m.len];
        assert(cetta_dense_relation_trim(&rel));
        check_trimmed(&rel);
    }
    check_held(&rel, &m, &held);
    for (size_t i = 0u; i < m.len; i++) {
        CettaDenseId fixes[3][2] = {{m.items[i].a, CETTA_DENSE_ANY},
                                    {CETTA_DENSE_ANY, m.items[i].b},
                                    {m.items[i].a, m.items[i].b}};
        for (int f = 0; f < 3; f++) {
            WalkCheck w = {&m, 0u, true};
            assert(cetta_dense_relation_each(&rel, fixes[f][0], fixes[f][1],
                                             walk_visit, &w));
            assert(w.ok && w.seen == model_count(&m, fixes[f][0], fixes[f][1]));
        }
    }
    uint64_t left = cetta_dense_relation_bytes(&rel);
    assert(rel.len <= 6u * keep + 64u &&
           rel.cap <= (2u * rel.len > 64u ? 2u * rel.len : 64u));
    printf("PASS: dense relation drained from %u to %u pairs: %llu bytes, then %llu\n",
           fill, keep, (unsigned long long)full, (unsigned long long)left);
    cetta_dense_relation_free(&rel);
    free(m.items);
    free(held.ids);
    free(held.state);
}

/* Pairs over ids never seen before, each added and removed, a few kept: the
 * dead pile up to the bound and are dropped. */
static void churn(unsigned rounds) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    Model m = {0};
    Held held = {.ok = true};
    CettaDenseId fresh = 1000u;
    uint32_t most = 0u;
    for (unsigned round = 0u; round < rounds; round++) {
        bool keep = round % 16u == 0u;
        CettaDenseId a = keep ? rng() % 64u : fresh++;
        CettaDenseId b = keep ? rng() % 64u : fresh++;
        bool oom = false;
        bool added = cetta_dense_relation_add(&rel, a, b, &oom, on_held, &held);
        assert(added == (model_find(&m, a, b) == SIZE_MAX));
        if (added)
            model_add(&m, a, b);
        if (!keep) {
            assert(cetta_dense_relation_remove(&rel, a, b, on_released, &held));
            m.len--;
            assert(cetta_dense_relation_trim(&rel));
            check_trimmed(&rel);
        }
        if (rel.len > most)
            most = rel.len;
    }
    check_held(&rel, &m, &held);
    assert(most <= 6u * (uint32_t)m.len + 64u + 2u);
    printf("PASS: dense relation churned %u rounds over fresh ids: %zu pairs held, at most %u numbered\n",
           rounds, m.len, most);
    cetta_dense_relation_free(&rel);
    free(m.items);
    free(held.ids);
    free(held.state);
}

typedef struct {
    CettaDenseRelation *rel;
    CettaDenseId seen[8];
    uint32_t count;
} MutatingVisit;

static void visit_and_mutate(void *opaque, CettaDenseId id) {
    MutatingVisit *visit = opaque;
    assert(visit->count < 8u);
    visit->seen[visit->count++] = id;
    if (visit->count == 1u) {
        assert(cetta_dense_relation_remove(visit->rel, 2u, 3u, NULL, NULL));
        bool oom = false;
        assert(cetta_dense_relation_add(visit->rel, 6u, 7u, &oom, NULL, NULL) && !oom);
    }
}

static void numbered_visitor_mutation(void) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    for (uint32_t n = 0u; n < 6u; n += 2u) {
        bool oom = false;
        assert(cetta_dense_relation_add(&rel, n, n + 1u, &oom, NULL, NULL) && !oom);
    }
    assert(rel.dead == 0u);
    MutatingVisit visit = {.rel = &rel};
    cetta_dense_relation_each_numbered(&rel, visit_and_mutate, &visit);
    const CettaDenseId expected[] = {0u, 1u, 4u, 5u, 6u, 7u};
    assert(visit.count == sizeof(expected) / sizeof(*expected) &&
           memcmp(visit.seen, expected, sizeof(expected)) == 0);
    assert(rel.pairs == 3u && rel.dead == 2u);
    cetta_dense_relation_free(&rel);
    puts("PASS: numbered visitation rechecks liveness after callbacks, skipping removed IDs and visiting appended IDs");
}

int main(void) {
    numbered_visitor_mutation();
    run(16u, 16u, 1u, 20000u);
    run(200u, 200u, 1u, 50000u);
    run(300u, 300u, 1000003u, 50000u);
    run(3000u, 3u, 1u, 30000u);
    run(4u, 5000u, 7u, 30000u);
    drain(40000u, 30u);
    churn(40000u);
    return 0;
}
