/* A relation kept as a table of tuples agrees with a plain list of its
 * tuples under random adds, removals, membership tests, walks and counts
 * with random bound columns, for every supported arity and id ranges small enough
 * to collide; walks build sides on the way, which later adds and removals
 * keep in step. */
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
    CettaDenseId t[CETTA_TUPLE_MAX_ARITY], fixed[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t step = 0; step < steps; step++) {
        for (uint32_t c = 0; c < arity; c++)
            t[c] = 1000u + rnd() % range;
        uint32_t op = rnd() % 10u;
        if (op < 5) {
            bool oom = false;
            bool added = cetta_tuple_table_add(&table, t, &oom);
            assert(!oom);
            assert(added == plain_add(&plain, t));
        } else if (op < 8) {
            assert(cetta_tuple_table_remove(&table, t) == plain_remove(&plain, t));
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
        }
    }
    printf("arity %u, ids %u, %u steps: %zu tuples held, %llu bytes; sides kept:",
           arity, range, steps, plain.len,
           (unsigned long long)cetta_tuple_table_bytes(&table));
    for (uint32_t c = 0; c < arity; c++)
        printf(" %d", table.side_kept[c]);
    printf("\n");
    cetta_tuple_table_free(&table);
    free(plain.items);
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
    }
    printf("PASS: tables of tuples agree with a list of their tuples under adds, removals, walks and counts\n");
    return 0;
}
