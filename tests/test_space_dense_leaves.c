/* The leaves of a dense relation's rows, seen from inside the store.
 *
 * Over ids whose numbers spread across many chunks: random adds, removals
 * and membership tests agree with a model, and so do walks and counts by
 * either column; every row keeps its leaves as the store says (in chunk
 * order, each chunk's numbers in one leaf, arrays sorted, at most 4,096
 * numbers and more than a quarter full, bits within their chunk, counted,
 * and within the byte rule's bound, the bytes as kept), through
 * renumbering too.  Rows given one far number each grow by a leaf, not by
 * bits up to the number, and take back their bytes when it goes; a far
 * number put and taken again and again leaves a dense row as it was; and a
 * sparse row built in descending order keeps its bytes in proportion to its
 * numbers. */
#include "../src/space_dense.c"

#include <assert.h>
#include <stdio.h>

static uint64_t rng_state = 0x9e3779b97f4a7c15u;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* ── The store's invariants ── */

/* A leaf of chunks [lo, hi) (an inline array: any): its count. */
static uint32_t check_leaf(const void *items, uint32_t len, uint32_t lo,
                           uint32_t hi, bool in_place) {
    uint32_t count = dense_len_count(len);
    assert(count > 0u);
    if (dense_len_bits(len)) {
        uint32_t words = dense_len_words(len);
        assert(words >= 1u && words <= 1024u && (words & (words - 1u)) == 0u);
        assert(hi == lo + 1u);
        assert(!in_place || lo == 0u);
        const uint64_t *bits = items;
        uint32_t seen = 0u;
        for (uint32_t w = 0u; w < words; w++)
            seen += (uint32_t)__builtin_popcountll(bits[w]);
        assert(seen == count);
        assert(!dense_bits_sparse(len));
        return count;
    }
    uint32_t capacity = dense_len_capacity(len);
    assert(count <= DENSE_LEAF_MAX && capacity >= count &&
           (capacity <= 2u || 4u * count > capacity));
    const uint32_t *numbers = items;
    uint32_t run = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        if (i) {
            assert(numbers[i - 1u] < numbers[i]);
            run = dense_chunk(numbers[i - 1u]) == dense_chunk(numbers[i])
                ? run + 1u : 1u;
        } else {
            run = 1u;
        }
        assert(run <= 2048u);
        assert(in_place || (dense_chunk(numbers[i]) >= lo &&
                            dense_chunk(numbers[i]) < hi));
    }
    return count;
}

static uint32_t check_directory_tree(const CettaDenseSide *side,
                                     const CettaDenseRowNode *node,
                                     uint64_t low, uint64_t high) {
    if (!node)
        return 0u;
    assert(node->number >= side->row_cap && node->number >= low &&
           node->number < high && node->items && node->length);
    uint32_t left = check_directory_tree(side, node->left, low, node->number);
    uint32_t right = check_directory_tree(side, node->right,
                                         (uint64_t)node->number + 1u, high);
    uint32_t lh = dense_tree_height(node->left), rh = dense_tree_height(node->right);
    assert(lh <= rh + 1u && rh <= lh + 1u);
    assert(node->height == (lh > rh ? lh : rh) + 1u);
    assert(node->subtree_count == left + right + 1u);
    return left + right + 1u;
}

static void check_side(const CettaDenseRelation *rel,
                       const CettaDenseSide *side) {
    uint64_t bytes = 0u;
    DenseSideIterator iterator;
    dense_side_iterator_init(&iterator, side);
    DenseRowRef row;
    uint32_t n, count = 0u, previous = 0u;
    while (dense_side_next(&iterator, &n, &row)) {
        assert(!count || previous < n);
        previous = n;
        count++;
        assert(n < rel->len && rel->holds[n]);
        bytes += dense_row_bytes(side, n);
        if (!dense_row_listed(side, n)) {
            (void)check_leaf(*row.items, *row.length, 0u,
                             dense_len_bits(*row.length) ? 1u : 0u, true);
            continue;
        }
        const DenseLeaves *list = *row.items;
        assert(list->len >= 1u && list->len <= list->cap);
        assert(!(list->len == 1u && (!dense_len_bits(list->leaf[0].len) ||
                                     list->leaf[0].lo == 0u)));
        uint64_t total = 0u;
        for (uint32_t i = 0u; i < list->len; i++) {
            const DenseLeaf *leaf = &list->leaf[i];
            assert(leaf->lo < leaf->hi);
            assert(i == 0u || list->leaf[i - 1u].hi <= leaf->lo);
            total += check_leaf(leaf->items, leaf->len, leaf->lo, leaf->hi,
                                false);
        }
        assert(total == list->count);
    }
    assert(count == side->row_count);
    (void)check_directory_tree(side, side->overflow, 0u, UINT64_C(1) << 32);
    if (side->row_bins) {
        uint32_t bins[DENSE_DIRECTORY_BINS] = {0};
        dense_side_iterator_init(&iterator, side);
        while (dense_side_next(&iterator, &n, NULL))
            bins[dense_directory_bin(n)]++;
        assert(memcmp(bins, side->row_bins, sizeof(bins)) == 0);
    }
    uint64_t best = (uint64_t)count * sizeof(CettaDenseRowNode);
    for (uint32_t cut = 64u;; cut *= 2u) {
        uint32_t outside = 0u;
        dense_side_iterator_init(&iterator, side);
        while (dense_side_next(&iterator, &n, NULL))
            outside += n >= cut;
        uint64_t candidate = 12u * (uint64_t)cut + 40u * (uint64_t)outside;
        if (candidate < best)
            best = candidate;
        if (cut == (UINT32_C(1) << 31))
            break;
    }
    if (!side->directory_maintenance_failed)
        assert(dense_side_control_bytes(side) <= 3u * best);
    assert(bytes == side->row_bytes);
}

static void check_store(const CettaDenseRelation *rel) {
    check_side(rel, &rel->first);
    if (rel->second_kept)
        check_side(rel, &rel->second);
}

/* ── Rows over numbers spread across chunks, against a model ── */

typedef struct {
    const uint8_t *model;
    uint32_t ids;
    CettaDenseId row_base;
    uint64_t seen;
    bool ok;
} Walk;

static bool walk_visit(void *context, CettaDenseId a, CettaDenseId b) {
    Walk *w = context;
    if (a < w->row_base || b >= w->ids ||
        !w->model[(size_t)(a - w->row_base) * w->ids + b])
        w->ok = false;
    w->seen++;
    return true;
}

static bool spine_visit(void *context, CettaDenseId a, CettaDenseId b) {
    (void)a;
    (void)b;
    (*(uint64_t *)context)++;
    return true;
}

/* A second column id from a mix: anywhere, a dense block at the start, a
 * dense block two chunks in, or the last few ids. */
static uint32_t pick_column(uint32_t ids) {
    uint32_t kind = (uint32_t)(rng() % 10u);
    if (kind < 4u)
        return (uint32_t)(rng() % ids);
    if (kind < 7u)
        return (uint32_t)(rng() % 3000u);
    if (kind < 9u)
        return 131072u + (uint32_t)(rng() % 3000u);
    return ids - 1u - (uint32_t)(rng() % 50u);
}

static void spread(uint32_t rows, uint32_t ids, unsigned steps) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    const CettaDenseId spine = UINT64_C(1) << 40, row_base = UINT64_C(1) << 41;
    bool oom = false;
    /* The spine pairs with every id in order: their numbers follow the
     * ids, so the rows' numbers spread as the ids do. */
    for (uint32_t b = 0u; b < ids; b++)
        assert(cetta_dense_relation_add(&rel, spine, b, &oom, NULL, NULL));
    check_store(&rel);
    uint8_t *model = calloc((size_t)rows * ids, 1u);
    uint64_t pairs = ids;
    for (unsigned step = 0u; step < steps; step++) {
        uint32_t r = (uint32_t)(rng() % rows), b = pick_column(ids);
        CettaDenseId a = row_base + r;
        uint8_t *held = &model[(size_t)r * ids + b];
        uint32_t op = (uint32_t)(rng() % 20u);
        if (op < 11u) {
            bool added = cetta_dense_relation_add(&rel, a, b, &oom, NULL, NULL);
            assert(!oom && added == !*held);
            pairs += added;
            *held = 1u;
        } else if (op < 18u) {
            bool removed = cetta_dense_relation_remove(&rel, a, b, NULL, NULL);
            assert(removed == (*held != 0u));
            pairs -= removed;
            *held = 0u;
            assert(cetta_dense_relation_trim(&rel));
        } else {
            assert(cetta_dense_relation_contains(&rel, a, b) == (*held != 0u));
        }
        assert(cetta_dense_relation_pairs(&rel) == pairs);
        if (step % 4999u == 0u) {
            check_store(&rel);
            uint64_t in_row = 0u, in_column = 0u;
            for (uint32_t c = 0u; c < ids; c++)
                in_row += model[(size_t)r * ids + c];
            for (uint32_t s = 0u; s < rows; s++)
                in_column += model[(size_t)s * ids + b];
            Walk w = {model, ids, row_base, 0u, true};
            assert(cetta_dense_relation_each(&rel, a, CETTA_DENSE_ANY,
                                             walk_visit, &w));
            assert(w.ok && w.seen == in_row);
            assert(cetta_dense_relation_count(&rel, a, CETTA_DENSE_ANY) ==
                   in_row);
            /* The second column's probes build the second side in time. */
            uint64_t by_column = 0u;
            assert(cetta_dense_relation_each(&rel, CETTA_DENSE_ANY, b,
                                             spine_visit, &by_column));
            assert(by_column == in_column + 1u);
            assert(cetta_dense_relation_count(&rel, CETTA_DENSE_ANY, b) ==
                   in_column + 1u);
        }
    }
    check_store(&rel);
    uint64_t column_zero = 1u;
    for (uint32_t r = 0u; r < rows; r++)
        column_zero += model[(size_t)r * ids];
    for (uint64_t probe = 0u; probe <= rel.pairs && !rel.second_kept; probe++)
        assert(cetta_dense_relation_count(&rel, CETTA_DENSE_ANY, 0u) == column_zero);
    assert(rel.second_kept);
    check_store(&rel);
    bool second = rel.second_kept;
    /* Without the spine and most pairs most numbers die: renumbering
     * rebuilds every row under its new numbers. */
    uint32_t numbered = rel.len;
    for (uint32_t b = 0u; b < ids; b++) {
        assert(cetta_dense_relation_remove(&rel, spine, b, NULL, NULL));
        pairs--;
        assert(cetta_dense_relation_trim(&rel));
    }
    for (uint32_t r = 0u; r < rows; r++) {
        for (uint32_t b = 0u; b < ids; b++) {
            uint8_t *held = &model[(size_t)r * ids + b];
            if (!*held || rng() % 40u == 0u)
                continue;
            assert(cetta_dense_relation_remove(&rel, row_base + r, b, NULL,
                                               NULL));
            pairs--;
            *held = 0u;
            assert(cetta_dense_relation_trim(&rel));
        }
        check_store(&rel);
    }
    assert(cetta_dense_relation_pairs(&rel) == pairs);
    assert(rel.len < numbered / 8u);
    check_store(&rel);
    uint64_t left = 0u;
    for (uint32_t r = 0u; r < rows; r++) {
        for (uint32_t b = 0u; b < ids; b++) {
            bool held = model[(size_t)r * ids + b] != 0u;
            left += held;
            assert(cetta_dense_relation_contains(&rel, row_base + r, b) ==
                   held);
        }
    }
    assert(left == pairs);
    Walk w = {model, ids, row_base, 0u, true};
    assert(cetta_dense_relation_each(&rel, CETTA_DENSE_ANY, CETTA_DENSE_ANY,
                                     walk_visit, &w));
    assert(w.ok && w.seen == pairs);
    printf("PASS: rows over %u ids spread across chunks agree with their "
           "model over %u steps and through renumbering (%u numbers, then "
           "%u; %llu pairs left, second side %s, %llu bytes)\n",
           ids, steps, numbered, rel.len, (unsigned long long)pairs,
           second ? "kept" : "not kept",
           (unsigned long long)cetta_dense_relation_bytes(&rel));
    cetta_dense_relation_free(&rel);
    free(model);
}

/* ── A far number a row ── */

static uint64_t rows_bytes(const CettaDenseRelation *rel) {
    return rel->first.row_bytes + rel->second.row_bytes;
}

static void far_numbers(uint32_t rows, uint32_t filler) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    const CettaDenseId row_base = UINT64_C(1) << 41, fill = UINT64_C(1) << 40;
    bool oom = false;
    for (uint32_t r = 0u; r < rows; r++)
        for (uint32_t j = 0u; j < 64u; j++)
            assert(cetta_dense_relation_add(&rel, row_base + r, j, &oom, NULL,
                                            NULL));
    for (uint32_t v = 0u; v < filler; v++)
        assert(cetta_dense_relation_add(&rel, fill, 1000u + v, &oom, NULL,
                                        NULL));
    CettaDenseId far = 1000u + filler - 1u;
    uint64_t before = rows_bytes(&rel);
    for (uint32_t r = 0u; r < rows; r++)
        assert(cetta_dense_relation_add(&rel, row_base + r, far, &oom, NULL,
                                        NULL));
    check_store(&rel);
    uint64_t with = rows_bytes(&rel);
    for (uint32_t r = 0u; r < rows; r++) {
        assert(cetta_dense_relation_contains(&rel, row_base + r, far));
        assert(cetta_dense_relation_count(&rel, row_base + r,
                                          CETTA_DENSE_ANY) == 65u);
    }
    /* Bits up to the far number would take its number of bits a row. */
    assert(with - before <= (uint64_t)rows * 128u);
    for (uint32_t r = 0u; r < rows; r++)
        assert(cetta_dense_relation_remove(&rel, row_base + r, far, NULL,
                                           NULL));
    check_store(&rel);
    uint64_t after = rows_bytes(&rel);
    assert(after == before);
    printf("PASS: %u rows of 64 numbers given one number past %u others: "
           "%llu bytes, %llu with it (bits up to it: %llu), %llu after\n",
           rows, filler, (unsigned long long)before, (unsigned long long)with,
           (unsigned long long)(before + (uint64_t)rows *
                                (dense_bits_words(filler) * 8u)),
           (unsigned long long)after);
    cetta_dense_relation_free(&rel);
}

/* A dense row and far numbers put and taken: once each far number has
 * come and gone, the row ends every round as it began, within 16 bytes a
 * number (a far number in its own chunk may grow its bits under the byte
 * rule). */
static void far_toggle(uint32_t dense, unsigned rounds) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    const CettaDenseId row = UINT64_C(1) << 41, fill = UINT64_C(1) << 40;
    bool oom = false;
    for (uint32_t j = 0u; j < dense; j++)
        assert(cetta_dense_relation_add(&rel, row, j, &oom, NULL, NULL));
    for (uint32_t v = 0u; v < 200000u; v++)
        assert(cetta_dense_relation_add(&rel, fill, 1000000u + v, &oom, NULL,
                                        NULL));
    uint32_t number = cetta_dense_relation_number(&rel, row);
    uint64_t before = 0u;
    uint32_t len = 0u;
    for (unsigned i = 0u; i < rounds; i++) {
        CettaDenseId far = 1000000u + 199999u - (CettaDenseId)(i % 7u) * 30000u;
        assert(cetta_dense_relation_add(&rel, row, far, &oom, NULL, NULL));
        assert(cetta_dense_relation_count(&rel, row, CETTA_DENSE_ANY) ==
               dense + 1u);
        assert(cetta_dense_relation_remove(&rel, row, far, NULL, NULL));
        if (i == 6u) {
            before = rows_bytes(&rel);
            len = *dense_side_row_ref(&rel.first, number).length;
            assert(dense_row_bytes(&rel.first, number) <=
                   16u * (uint64_t)dense);
        } else if (i > 6u) {
            assert(rows_bytes(&rel) == before);
            assert(*dense_side_row_ref(&rel.first, number).length == len);
        }
    }
    check_store(&rel);
    printf("PASS: a row of %u numbers given a far number and losing it %u "
           "times ends as it began (%llu bytes)\n",
           dense, rounds, (unsigned long long)before);
    cetta_dense_relation_free(&rel);
}

/* A sparse row of many numbers, put in descending order. */
static void descending(uint32_t count, uint32_t stride) {
    CettaDenseRelation rel;
    cetta_dense_relation_init(&rel);
    const CettaDenseId row = UINT64_C(1) << 41, fill = UINT64_C(1) << 40;
    bool oom = false;
    for (uint32_t v = 0u; v < count * stride; v++)
        assert(cetta_dense_relation_add(&rel, fill, v, &oom, NULL, NULL));
    uint64_t before = rows_bytes(&rel);
    for (uint32_t i = count; i-- > 0u;)
        assert(cetta_dense_relation_add(&rel, row, (CettaDenseId)i * stride,
                                         &oom, NULL, NULL));
    check_store(&rel);
    uint64_t grown = rows_bytes(&rel) - before;
    assert(cetta_dense_relation_count(&rel, row, CETTA_DENSE_ANY) == count);
    assert(grown <= (uint64_t)count * 16u + 4096u);
    for (uint32_t i = 0u; i < count; i += 3u)
        assert(cetta_dense_relation_remove(&rel, row, (CettaDenseId)i * stride,
                                           NULL, NULL));
    check_store(&rel);
    printf("PASS: a row of %u numbers %u apart put in descending order "
           "takes %llu bytes (%.1f a number)\n",
           count, stride, (unsigned long long)grown,
           (double)grown / (double)count);
    cetta_dense_relation_free(&rel);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    spread(6u, 300000u, 400000u);
    far_numbers(2000u, 400000u);
    far_toggle(4000u, 3000u);
    far_toggle(40u, 3000u);
    descending(200000u, 37u);
    return 0;
}
