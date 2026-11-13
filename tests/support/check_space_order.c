#include "space_order.h"

#include <assert.h>
#include <stdio.h>

/* The order against a sorted array, under random and adversarial adds and
 * removals: the walk, the links, the counts, the balance, the least row,
 * rank and select after every operation. */

static int64_t *keys;
static uint64_t compares;
static bool fail_compares;

static bool compare_rows(void *context, uint32_t a, uint32_t b, int *order) {
    (void)context;
    compares++;
    if (fail_compares)
        return false;
    *order = keys[a] < keys[b] ? -1 : keys[a] > keys[b] ? 1 : 0;
    return true;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static int by_key_then_row(const void *pa, const void *pb) {
    uint32_t a = *(const uint32_t *)pa, b = *(const uint32_t *)pb;
    if (keys[a] != keys[b])
        return keys[a] < keys[b] ? -1 : 1;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int64_t probe_key;
static bool probe_row(void *context, uint32_t row, int *order) {
    (void)context;
    *order = keys[row] < probe_key ? -1 : keys[row] > probe_key ? 1 : 0;
    return true;
}

static uint32_t check_node(const CettaOrder *o, uint32_t n, uint32_t parent,
                           uint32_t depth, uint32_t *max_depth) {
    if (n == CETTA_ORDER_NONE)
        return 0u;
    assert(n < o->cap);
    assert(o->nodes[n].parent == parent);
    if (depth > *max_depth)
        *max_depth = depth;
    uint32_t l = check_node(o, o->nodes[n].left, n, depth + 1u, max_depth);
    uint32_t r = check_node(o, o->nodes[n].right, n, depth + 1u, max_depth);
    assert(o->nodes[n].size == l + r + 1u);
    assert(l + r <= 1u || ((uint64_t)l <= 3u * (uint64_t)r &&
                           (uint64_t)r <= 3u * (uint64_t)l));
    return l + r + 1u;
}

static void check(const CettaOrder *o, const uint8_t *held, uint32_t rows,
                  uint32_t *scratch, bool deep) {
    uint32_t n = 0u;
    for (uint32_t row = 0u; row < rows; row++) {
        assert(cetta_order_holds(o, row) == (held[row] != 0u));
        if (held[row])
            scratch[n++] = row;
    }
    assert(o->count == n);
    uint32_t max_depth = 0u;
    assert(check_node(o, o->root, CETTA_ORDER_NONE, 1u, &max_depth) == n);
    /* A tree of this balance is no deeper than log(n) / log(4/3) + 1. */
    uint32_t bound = 2u;
    for (uint64_t reach = 1u; reach < (uint64_t)n + 1u;
         reach = reach * 4u / 3u + 1u)
        bound++;
    assert(max_depth <= bound);
    if (!deep)
        return;
    qsort(scratch, n, sizeof(*scratch), by_key_then_row);
    assert(cetta_order_least(o) == (n ? scratch[0] : CETTA_ORDER_NONE));
    uint32_t at = cetta_order_least(o);
    for (uint32_t i = 0u; i < n; i++) {
        assert(at == scratch[i]);
        assert(cetta_order_rank(o, at) == i);
        assert(cetta_order_select(o, i) == at);
        at = cetta_order_next(o, at);
    }
    assert(at == CETTA_ORDER_NONE);
    assert(cetta_order_select(o, n) == CETTA_ORDER_NONE);
    /* The first row not before a key, for keys held, between and beyond. */
    for (uint32_t trial = 0u; trial < 8u && n != 0u; trial++) {
        probe_key = trial < 6u ? keys[scratch[rng() % n]] + (int64_t)(trial % 3u) - 1
                               : (trial == 6u ? INT64_MIN + 1 : INT64_MAX - 1);
        uint32_t expected = CETTA_ORDER_NONE;
        for (uint32_t i = 0u; i < n; i++) {
            if (keys[scratch[i]] >= probe_key) {
                expected = scratch[i];
                break;
            }
        }
        uint32_t found = 0u;
        assert(cetta_order_lower_bound(o, probe_row, NULL, &found));
        assert(found == expected);
        /* The same sought from a row before the key: the least, the last
         * before it, and one between. */
        uint32_t before = 0u;
        while (before < n && keys[scratch[before]] < probe_key)
            before++;
        for (uint32_t pick = 0u; pick < 3u && before != 0u; pick++) {
            uint32_t from = pick == 0u ? 0u
                : pick == 1u ? before - 1u : (uint32_t)(rng() % before);
            found = 0u;
            assert(cetta_order_lower_bound_from(o, probe_row, NULL,
                                                scratch[from], &found));
            assert(found == expected);
        }
    }
}

static void run(uint32_t rows, uint32_t steps, int pattern, uint32_t spread) {
    CettaOrder o;
    cetta_order_init(&o);
    keys = malloc(rows * sizeof(*keys));
    uint8_t *held = calloc(rows, 1u);
    uint32_t *scratch = malloc(rows * sizeof(*scratch));
    for (uint32_t row = 0u; row < rows; row++) {
        switch (pattern) {
        case 0: keys[row] = (int64_t)(rng() % spread); break;
        case 1: keys[row] = row; break;                 /* ascending */
        case 2: keys[row] = -(int64_t)row; break;        /* descending */
        case 3: keys[row] = 7; break;                    /* all equal */
        default: keys[row] = (row & 1u) ? row : -(int64_t)row; break;
        }
    }
    uint32_t next_row = 0u;
    for (uint32_t step = 0u; step < steps; step++) {
        uint64_t r = rng();
        bool add = next_row < rows && (o.count == 0u || (r & 3u) != 0u);
        if (pattern != 0 && next_row < rows && step < rows)
            add = true;
        if (add) {
            assert(cetta_order_insert(&o, next_row, compare_rows, NULL));
            assert(!cetta_order_insert(&o, next_row, compare_rows, NULL));
            held[next_row++] = 1u;
        } else if (o.count != 0u) {
            uint32_t victim;
            switch ((r >> 8) % 3u) {
            case 0: victim = cetta_order_least(&o); break;
            case 1: victim = cetta_order_select(&o, o.count - 1u); break;
            default:
                victim = cetta_order_select(&o, (uint32_t)((r >> 16) % o.count));
                break;
            }
            cetta_order_remove(&o, victim);
            held[victim] = 0u;
        }
        check(&o, held, rows, scratch, rows <= 64u || step % 257u == 0u);
    }
    check(&o, held, rows, scratch, true);

    /* A copy is the same order; the original may then change alone. */
    CettaOrder copy;
    cetta_order_init(&copy);
    assert(cetta_order_copy(&copy, &o));
    check(&copy, held, rows, scratch, true);

    /* Built from its sorted rows it holds the same rows in the same order. */
    uint32_t n = 0u;
    for (uint32_t row = 0u; row < rows; row++)
        if (held[row])
            scratch[n++] = row;
    qsort(scratch, n, sizeof(*scratch), by_key_then_row);
    uint32_t *sorted = malloc((n ? n : 1u) * sizeof(*sorted));
    memcpy(sorted, scratch, n * sizeof(*sorted));
    CettaOrder built;
    cetta_order_init(&built);
    assert(cetta_order_build(&built, sorted, n, rows));
    check(&built, held, rows, scratch, true);

    /* Renumbered after a contraction it holds the surviving rows, in the
     * same order, under their new numbers. */
    {
        CettaOrder moved;
        cetta_order_init(&moved);
        assert(cetta_order_copy(&moved, &o));
        uint64_t *target = malloc(rows * sizeof(*target));
        uint8_t *kept = calloc(rows, 1u);
        int64_t *old_keys = keys;
        int64_t *new_keys = malloc(rows * sizeof(*new_keys));
        uint32_t next = 0u;
        for (uint32_t row = 0u; row < rows; row++) {
            /* Rows the order does not hold go, and every third it holds. */
            if (!held[row] || (rng() % 3u) == 0u) {
                target[row] = UINT64_MAX;
            } else {
                new_keys[next] = old_keys[row];
                kept[next] = 1u;
                target[row] = next++;
            }
        }
        assert(cetta_order_transport(&moved, target, rows, UINT64_MAX, next));
        keys = new_keys;
        check(&moved, kept, next, scratch, true);
        keys = old_keys;
        free(new_keys);
        free(kept);
        free(target);
        cetta_order_free(&moved);
    }

    /* A comparison that cannot be made leaves the order as it was. */
    if (next_row < rows && o.count != 0u) {
        fail_compares = true;
        assert(!cetta_order_insert(&o, next_row, compare_rows, NULL));
        fail_compares = false;
        check(&o, held, rows, scratch, true);
    }

    /* Draining by the least row sorts. */
    int64_t last = INT64_MIN;
    uint32_t last_row = 0u;
    while (o.count != 0u) {
        uint32_t least = cetta_order_least(&o);
        assert(keys[least] > last ||
               (keys[least] == last && least > last_row) || last == INT64_MIN);
        last = keys[least];
        last_row = least;
        cetta_order_remove(&o, least);
        held[least] = 0u;
    }
    check(&o, held, rows, scratch, true);
    assert(o.root == CETTA_ORDER_NONE && o.least == CETTA_ORDER_NONE);

    free(sorted);
    cetta_order_free(&built);
    cetta_order_free(&copy);
    cetta_order_free(&o);
    free(scratch);
    free(held);
    free(keys);
}

int main(void) {
    for (uint32_t rows = 1u; rows <= 40u; rows++)
        for (int pattern = 0; pattern < 5; pattern++)
            run(rows, rows * 6u, pattern, 5u);
    for (int round = 0; round < 6; round++) {
        run(3000u, 12000u, 0, 1u << (round * 3));
        run(3000u, 9000u, 1 + round % 4, 1u);
    }
    /* Comparisons per add stay logarithmic: a million ascending keys. */
    {
        uint32_t rows = 1u << 20;
        CettaOrder o;
        cetta_order_init(&o);
        keys = malloc(rows * sizeof(*keys));
        for (uint32_t row = 0u; row < rows; row++)
            keys[row] = -(int64_t)row;
        compares = 0u;
        for (uint32_t row = 0u; row < rows; row++)
            assert(cetta_order_insert(&o, row, compare_rows, NULL));
        assert(compares <= (uint64_t)rows * 52u);
        assert(cetta_order_least(&o) == rows - 1u);
        compares = 0u;
        for (uint32_t row = 0u; row < rows; row++)
            cetta_order_remove(&o, cetta_order_least(&o));
        assert(compares == 0u && o.count == 0u);
        cetta_order_free(&o);
        free(keys);
    }
    puts("space order: ok");
    return 0;
}
