/* A relation kept as a table of tuples agrees with a plain list of its
 * tuples under random adds, removals, membership tests, walks and counts
 * with random bound columns, for every supported arity and id ranges small enough
 * to collide; walks build sides on the way, which later adds and removals
 * keep in step.  The ids the table tells its caller it holds are exactly the
 * ids the list holds, and trimming after each removal keeps the dead numbers
 * within twice the columns the tuples fill plus 64 and the tuples' room
 * within four times them (at least 64), the hash within twice the room:
 * through random runs, a table emptied down to a few tuples, and churn over
 * ids never seen before. */
#include "space_tuples.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaDenseId *items;
    size_t len, cap;
    uint32_t arity;
} Plain;

static bool plain_find(const Plain *p, const CettaDenseId *t, size_t *at) {
    for (size_t i = 0; i < p->len; i++)
        if (!memcmp(p->items + i * p->arity, t, p->arity * sizeof(*t))) {
            if (at)
                *at = i;
            return true;
        }
    return false;
}

static bool plain_add(Plain *p, const CettaDenseId *t) {
    if (plain_find(p, t, NULL))
        return false;
    if (p->len == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->items = realloc(p->items, p->cap * p->arity * sizeof(*t));
    }
    memcpy(p->items + p->len * p->arity, t, p->arity * sizeof(*t));
    p->len++;
    return true;
}

static bool plain_remove(Plain *p, const CettaDenseId *t) {
    size_t at;
    if (!plain_find(p, t, &at))
        return false;
    p->len--;
    memmove(p->items + at * p->arity, p->items + p->len * p->arity,
            p->arity * sizeof(*t));
    return true;
}

static bool plain_matches(const Plain *p, size_t i, const CettaDenseId *fixed) {
    for (uint32_t c = 0; c < p->arity; c++)
        if (fixed[c] != CETTA_DENSE_ANY && p->items[i * p->arity + c] != fixed[c])
            return false;
    return true;
}

typedef struct {
    const Plain *plain;
    const CettaDenseId *fixed;
    uint64_t seen;
    bool ok;
} Check;

static bool check_visit(void *context, const CettaDenseId *tuple) {
    Check *check = context;
    check->seen++;
    for (uint32_t c = 0; c < check->plain->arity; c++)
        if (check->fixed[c] != CETTA_DENSE_ANY && tuple[c] != check->fixed[c])
            check->ok = false;
    if (!plain_find(check->plain, tuple, NULL))
        check->ok = false;
    return true;
}

/* The ids the caller holds, as the table told it: each id told held while
 * not held, released while held. */
typedef struct {
    uint8_t *held;
    size_t cap;
    bool ok;
} Held;

static void held_mark(Held *h, CettaDenseId id, uint8_t value) {
    if (id >= h->cap) {
        size_t cap = h->cap ? h->cap : 1024u;
        while (cap <= id)
            cap *= 2u;
        h->held = realloc(h->held, cap);
        memset(h->held + h->cap, 0, cap - h->cap);
        h->cap = cap;
    }
    if (h->held[id] == value)
        h->ok = false;
    h->held[id] = value;
}

static void on_held(void *context, CettaDenseId id) {
    held_mark(context, id, 1u);
}

static void on_released(void *context, CettaDenseId id) {
    held_mark(context, id, 0u);
}

static void on_numbered(void *context, CettaDenseId id) {
    Held *seen = context;
    held_mark(seen, id, 1u);
}

/* The caller holds exactly the ids the list holds, and the table lists
 * exactly those; its bounds hold after trimming. */
static void check_held(const CettaTupleTable *table, const Plain *plain,
                       const Held *held) {
    assert(held->ok);
    Held listed = {.ok = true};
    cetta_tuple_table_each_numbered(table, on_numbered, &listed);
    assert(listed.ok);
    uint8_t *expected = calloc(held->cap ? held->cap : 1u, 1u);
    for (size_t i = 0; i < plain->len * plain->arity; i++) {
        assert(plain->items[i] < held->cap);
        expected[plain->items[i]] = 1u;
    }
    uint32_t live = 0u;
    for (size_t id = 0; id < held->cap; id++) {
        assert(held->held[id] == expected[id]);
        assert((id < listed.cap ? listed.held[id] : 0u) == expected[id]);
        live += expected[id];
    }
    assert(table->len - table->dead == live);
    free(expected);
    free(listed.held);
}

static void check_trimmed(const CettaTupleTable *table) {
    assert((uint64_t)table->dead <=
           2u * (uint64_t)table->count * table->arity + 64u);
    assert(table->tuple_cap < 128u ||
           4u * (uint64_t)table->count >= table->tuple_cap);
    assert(!table->hash || table->hash_mask + 1u <= 2u * table->tuple_cap);
}

static uint64_t rnd_state = 0x9e3779b97f4a7c15u;
static uint64_t rnd(void) {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 7;
    rnd_state ^= rnd_state << 17;
    return rnd_state;
}

static void run(uint32_t arity, uint32_t range, uint32_t steps) {
    CettaTupleTable table;
    assert(cetta_tuple_table_init(&table, arity));
    Plain plain = {.arity = arity};
    Held held = {.ok = true};
    CettaDenseId t[CETTA_TUPLE_MAX_ARITY], fixed[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t step = 0; step < steps; step++) {
        for (uint32_t c = 0; c < arity; c++)
            t[c] = 1000u + rnd() % range;
        uint32_t op = rnd() % 10u;
        if (op < 5) {
            bool oom = false;
            bool added = cetta_tuple_table_add(&table, t, &oom, on_held, &held);
            assert(!oom);
            assert(added == plain_add(&plain, t));
        } else if (op < 8) {
            assert(cetta_tuple_table_remove(&table, t, on_released, &held) ==
                   plain_remove(&plain, t));
            assert(cetta_tuple_table_trim(&table));
            check_trimmed(&table);
        } else {
            assert(cetta_tuple_table_contains(&table, t) == plain_find(&plain, t, NULL));
        }
        assert(cetta_tuple_table_tuples(&table) == plain.len);
        if (step % 7u == 0u) {
            for (uint32_t c = 0; c < arity; c++)
                fixed[c] = rnd() % 3u ? CETTA_DENSE_ANY : 1000u + rnd() % range;
            uint64_t expected = 0;
            for (size_t i = 0; i < plain.len; i++)
                expected += plain_matches(&plain, i, fixed);
            Check check = {.plain = &plain, .fixed = fixed, .ok = true};
            assert(cetta_tuple_table_each(&table, fixed, check_visit, &check));
            assert(check.ok && check.seen == expected);
            assert(cetta_tuple_table_count(&table, fixed) == expected);
            check_held(&table, &plain, &held);
        }
    }
    check_held(&table, &plain, &held);
    printf("arity %u, ids %u, %u steps: %zu tuples held, %llu bytes; sides kept:",
           arity, range, steps, plain.len,
           (unsigned long long)cetta_tuple_table_bytes(&table));
    for (uint32_t c = 0; c < arity; c++)
        printf(" %d", table.side_kept[c]);
    printf("\n");
    cetta_tuple_table_free(&table);
    free(plain.items);
    free(held.held);
}

/* Fill a table with FILL tuples over a wide range, walking to build every
 * side, then remove all but KEEP: the room and the numbering come down to
 * the few, and the tuples left answer as before. */
static void drain(uint32_t arity, uint32_t fill, uint32_t keep) {
    CettaTupleTable table;
    assert(cetta_tuple_table_init(&table, arity));
    Plain plain = {.arity = arity};
    Held held = {.ok = true};
    CettaDenseId t[CETTA_TUPLE_MAX_ARITY], fixed[CETTA_TUPLE_MAX_ARITY];
    while (plain.len < fill) {
        for (uint32_t c = 0; c < arity; c++)
            t[c] = rnd() % (4u * fill);
        bool oom = false;
        assert(cetta_tuple_table_add(&table, t, &oom, on_held, &held) ==
               plain_add(&plain, t));
    }
    for (uint32_t c = 0; c < arity; c++) {
        for (uint32_t d = 0; d < arity; d++)
            fixed[d] = d == c ? plain.items[c] : CETTA_DENSE_ANY;
        for (int probe = 0; probe < 4; probe++)
            (void)cetta_tuple_table_count(&table, fixed);
    }
    uint64_t full = cetta_tuple_table_bytes(&table);
    while (plain.len > keep) {
        size_t at = rnd() % plain.len;
        memcpy(t, plain.items + at * arity, arity * sizeof(*t));
        assert(cetta_tuple_table_remove(&table, t, on_released, &held));
        assert(plain_remove(&plain, t));
        assert(cetta_tuple_table_trim(&table));
        check_trimmed(&table);
    }
    check_held(&table, &plain, &held);
    for (size_t i = 0; i < plain.len; i++) {
        for (uint32_t c = 0; c < arity; c++)
            fixed[c] = rnd() % 2u ? CETTA_DENSE_ANY : plain.items[i * arity + c];
        uint64_t expected = 0;
        for (size_t j = 0; j < plain.len; j++)
            expected += plain_matches(&plain, j, fixed);
        Check check = {.plain = &plain, .fixed = fixed, .ok = true};
        assert(cetta_tuple_table_each(&table, fixed, check_visit, &check));
        assert(check.ok && check.seen == expected);
        assert(cetta_tuple_table_contains(&table, plain.items + i * arity));
    }
    uint64_t left = cetta_tuple_table_bytes(&table);
    assert(table.tuple_cap <= 128u && table.len <= 3u * keep * arity + 64u &&
           table.cap <= (2u * table.len > 64u ? 2u * table.len : 64u));
    printf("arity %u drained from %u to %u tuples: %llu bytes, then %llu\n",
           arity, fill, keep, (unsigned long long)full,
           (unsigned long long)left);
    cetta_tuple_table_free(&table);
    free(plain.items);
    free(held.held);
}

/* Tuples over ids never seen before, each added and removed: the dead pile
 * up to the bound and are dropped, while a few tuples stay throughout. */
static void churn(uint32_t arity, uint32_t rounds) {
    CettaTupleTable table;
    assert(cetta_tuple_table_init(&table, arity));
    Plain plain = {.arity = arity};
    Held held = {.ok = true};
    CettaDenseId t[CETTA_TUPLE_MAX_ARITY];
    CettaDenseId fresh = 0u;
    uint32_t most = 0u;
    for (uint32_t round = 0; round < rounds; round++) {
        for (uint32_t c = 0; c < arity; c++)
            t[c] = round % 16u == 0u ? rnd() % 8u : 100u + fresh++;
        bool oom = false;
        bool added = cetta_tuple_table_add(&table, t, &oom, on_held, &held);
        assert(added == plain_add(&plain, t));
        if (round % 16u != 0u) {
            assert(cetta_tuple_table_remove(&table, t, on_released, &held));
            assert(plain_remove(&plain, t));
            assert(cetta_tuple_table_trim(&table));
            check_trimmed(&table);
        }
        if (table.len > most)
            most = table.len;
    }
    check_held(&table, &plain, &held);
    assert(most <= 3u * (uint32_t)plain.len * arity + 64u + arity);
    printf("arity %u churned %u rounds over fresh ids: %zu tuples held, at most %u numbered\n",
           arity, rounds, plain.len, most);
    cetta_tuple_table_free(&table);
    free(plain.items);
    free(held.held);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CettaTupleTable bad;
    assert(!cetta_tuple_table_init(&bad, 0u));
    assert(!cetta_tuple_table_init(&bad, CETTA_TUPLE_MAX_ARITY + 1u));
    for (uint32_t arity = 1; arity <= CETTA_TUPLE_MAX_ARITY; arity++) {
        run(arity, 8u, 6000u);
        run(arity, 64u, 6000u);
        run(arity, 512u, 6000u);
        drain(arity, 6000u, 20u);
        churn(arity, 20000u);
    }
    printf("PASS: tables of tuples agree with a list of their tuples under adds, removals, walks and counts\n");
    return 0;
}
