/* Directory ownership and allocation failure are checked independently of
 * the pair-store random model. Faults affect only the operation under test. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/space_dense.h"

static unsigned allocation, failure;
static bool armed, injected;
static CettaDenseRelation *publishing_relation;
static size_t publishing_width;
static void *moving_ids;
static size_t moving_ids_bytes;
static bool refuse(void) {
    if (!armed)
        return false;
    allocation++;
    if (allocation != failure)
        return false;
    injected = true;
    return true;
}
static void *checked_malloc(size_t n) { return refuse() ? NULL : malloc(n); }
static void *checked_calloc(size_t n, size_t size) {
    /* The complete300-row inverse has its512-slot construction prefix.
     * These requests are the final256-slot minimum publication draft. */
    if (publishing_relation && publishing_relation->second.row_count == 300u &&
        publishing_relation->second.row_cap == 512u && n == 256u &&
        size == publishing_width) {
        injected = true;
        return NULL;
    }
    return refuse() ? NULL : calloc(n, size);
}
static void *checked_realloc(void *p, size_t n) {
    if (refuse())
        return NULL;
    if (p && p == moving_ids) {
        void *grown = malloc(n);
        if (!grown)
            return NULL;
        memcpy(grown, p, n < moving_ids_bytes ? n : moving_ids_bytes);
        free(p);
        moving_ids = NULL;
        return grown;
    }
    return realloc(p, n);
}
#define malloc checked_malloc
#define calloc checked_calloc
#define realloc checked_realloc
#include "../src/space_dense.c"
#undef malloc
#undef calloc
#undef realloc

static void inject(unsigned at) {
    allocation = 0u;
    failure = at;
    injected = false;
    armed = true;
}

static void build(CettaDenseSide *side) {
    memset(side, 0, sizeof(*side));
    for (uint32_t n = 0u; n < 96u; n++) {
        assert(dense_row_put(side, n, 7u) == 1);
        assert(dense_side_maintain(side));
        assert(dense_row_put(side, n + 1000u, 7u) == 1);
        assert(dense_side_maintain(side));
    }
    assert(dense_side_change_cut(side, 128u));
}

static void check(const CettaDenseSide *side, void *const *payloads) {
    DenseSideIterator iterator;
    dense_side_iterator_init(&iterator, side);
    DenseRowRef row;
    uint32_t n, count = 0u;
    while (dense_side_next(&iterator, &n, &row)) {
        uint32_t wanted = count < 96u ? count : count - 96u + 1000u;
        assert(n == wanted && dense_row_count(side, n) == 1u);
        assert(dense_row_has(side, n, 7u) && !dense_row_has(side, n, 8u));
        if (payloads)
            assert(*row.items == payloads[count]);
        count++;
    }
    assert(count == 192u && side->row_count == count);
}

static void conversion_faults(uint32_t cut) {
    unsigned failures = 0u, successes = 0u;
    for (unsigned trial = 1u; trial <= 196u; trial++) {
        CettaDenseSide side;
        build(&side);
        void *payloads[192];
        for (uint32_t i = 0u; i < 192u; i++) {
            uint32_t key = i < 96u ? i : i - 96u + 1000u;
            payloads[i] = *dense_side_row_ref(&side, key).items;
        }
        CettaDenseSide before = side;
        inject(trial);
        bool changed = dense_side_change_cut(&side, cut);
        armed = false;
        if (injected) {
            failures++;
            assert(!changed && memcmp(&before, &side, sizeof(side)) == 0);
        } else {
            successes++;
            assert(changed && side.row_cap == cut);
        }
        check(&side, payloads);
        dense_side_free(&side);
    }
    assert(failures && successes);
    printf("PASS: cut %u has %u atomic allocation refusals and %u successful ownership transfers\n",
           cut, failures, successes);
}

static void far_rows_and_oom(void) {
    CettaDenseSide side = {0};
    for (uint32_t i = 0u; i < 14u; i++) {
        assert(dense_row_put(&side, i * 10000u, 1u) == 1);
        assert(dense_side_maintain(&side));
    }
    assert(side.row_cap == 0u && dense_side_control_bytes(&side) == 560u);
    assert(dense_side_change_cut(&side, 131072u));
    inject(1u);
    side.directory_events = UINT32_MAX;
    assert(!dense_side_maintain(&side));
    armed = false;
    assert(injected && side.directory_maintenance_failed && side.row_cap == 131072u);
    assert(dense_side_directory_bytes(&side) >= UINT64_C(131072) * 12u);
    assert(dense_side_maintain(&side));
    assert(!side.directory_maintenance_failed && side.row_cap == 0u &&
           dense_side_control_bytes(&side) == 560u);
    dense_side_free(&side);

    for (uint32_t n = 0u; n < 1024u; n++) {
        assert(dense_row_put(&side, n, 1u) == 1);
        assert(dense_side_maintain(&side));
    }
    side.directory_events = UINT32_MAX;
    assert(dense_side_maintain(&side));
    uint32_t prefix = side.row_cap;
    assert(prefix >= 64u);
    for (uint32_t n = 0u; n < 64u; n++) {
        DenseRowRef row = dense_side_row_ref(&side, n);
        assert(row.items == &side.rows[n] && row.length == &side.row_len[n]);
    }
    for (unsigned turn = 0u; turn < 2000u; turn++) {
        assert(dense_row_put(&side, UINT32_C(1) << 30, 1u) == 1);
        assert(dense_side_maintain(&side));
        assert(dense_row_take(&side, UINT32_C(1) << 30, 1u));
        assert(dense_side_maintain(&side));
        assert(side.row_cap == prefix && side.row_count == 1024u);
    }
    dense_side_free(&side);
    puts("PASS: fourteen far rows use 560 control bytes; failed maintenance stays charged; far outlier cannot thrash prefix");
}

static void failed_new_row(void) {
    for (unsigned trial = 1u; trial <= 2u; trial++) {
        CettaDenseSide side = {0};
        inject(trial);
        assert(dense_row_put(&side, UINT32_C(1) << 30, 7u) == -1);
        armed = false;
        assert(injected && side.row_count == 0u && side.overflow == NULL &&
               side.row_bytes == 0u && !dense_side_has_row(&side, UINT32_C(1) << 30));
        dense_side_free(&side);
    }
    puts("PASS: failed sparse header or leaf allocation publishes no empty row");
}

static void scheduled_prefix_growth(void) {
    for (unsigned trial = 0u; trial < 3u; trial++) {
        CettaDenseSide side = {0};
        for (uint32_t n = 0u; n < 64u; n++) {
            assert(dense_row_put(&side, n, 7u) == 1);
            assert(dense_side_maintain(&side));
        }
        assert(side.row_cap == 64u);
        /* Begin an exact valid decision interval, then show that a birth
         * alone cannot trigger promotion before that interval is due. */
        side.directory_base = 64u * 12u;
        side.directory_events = 0u;
        assert(dense_row_put(&side, 64u, 7u) == 1);
        assert(dense_side_maintain(&side) && side.row_cap == 64u &&
               side.directory_events == 1u);
        uint32_t count = 65u;
        while (side.directory_events <= side.directory_base / 160u)
            assert(dense_row_put(&side, count++, 7u) == 1);
        void *payloads[80];
        assert(count <= 80u);
        for (uint32_t n = 0u; n < count; n++)
            payloads[n] = *dense_side_row_ref(&side, n).items;
        CettaDenseSide before = side;
        if (trial)
            inject(trial);
        bool maintained = dense_side_maintain(&side);
        armed = false;
        if (trial) {
            before.directory_maintenance_failed = true;
            assert(injected && !maintained &&
                   memcmp(&before, &side, sizeof(side)) == 0);
        } else {
            assert(maintained && side.row_cap == 128u && !side.overflow &&
                   side.directory_events == 0u);
        }
        for (uint32_t n = 0u; n < count; n++) {
            assert(*dense_side_row_ref(&side, n).items == payloads[n]);
            assert(dense_row_count(&side, n) == 1u && dense_row_has(&side, n, 7u));
        }
        assert(dense_side_maintain(&side) && side.row_cap == 128u && !side.overflow);
        dense_side_free(&side);
    }

    CettaDenseSide side = {0};
    for (uint32_t n = 0u; n < 10000u; n++) {
        assert(dense_row_put(&side, n, n + 3u) == 1);
        assert(dense_side_maintain(&side));
    }
    assert(side.row_cap == 16384u && !side.overflow && side.row_count == 10000u);
    for (uint32_t n = 0u; n < 10000u; n++)
        assert(dense_row_count(&side, n) == 1u && dense_row_has(&side, n, n + 3u));
    dense_side_free(&side);
    puts("PASS: scheduled growth preserves rows on either array fault, waits for its event interval, and keeps 10000 dense rows direct");
}

static void numbering_growth_faults(void) {
    for (unsigned trial = 1u; trial <= 2u; trial++) {
        CettaDenseRelation rel;
        cetta_dense_relation_init(&rel);
        for (uint32_t n = 0u; n < 64u; n++) {
            bool oom = false;
            assert(cetta_dense_relation_add(&rel, n, 0u, &oom, NULL, NULL) && !oom);
        }
        assert(rel.len == 64u && rel.cap == 64u && rel.slot_mask == 127u);
        CettaDenseRelation before = rel;
        uint64_t bytes = cetta_dense_relation_bytes(&rel);
        moving_ids = rel.ids;
        moving_ids_bytes = rel.cap * sizeof(*rel.ids);
        inject(trial);
        bool oom = false;
        assert(!cetta_dense_relation_add(&rel, 64u, 0u, &oom, NULL, NULL));
        armed = false;
        moving_ids = NULL;
        assert(injected && oom && memcmp(&rel, &before, sizeof(rel)) == 0 &&
               cetta_dense_relation_bytes(&rel) == bytes);
        for (uint32_t n = 0u; n < 64u; n++)
            assert(cetta_dense_relation_contains(&rel, n, 0u));
        assert(!cetta_dense_relation_contains(&rel, 64u, 0u) && rel.pairs == 64u);
        assert(cetta_dense_relation_add(&rel, 64u, 0u, &oom, NULL, NULL) && !oom &&
               rel.cap == 128u && rel.len == 65u);
        cetta_dense_relation_free(&rel);
    }
    puts("PASS: either numbering-array failure preserves both old allocations, exact capacity bytes, mappings and all original pairs");
}

typedef struct { uint32_t height, count; } TreeCheck;

static TreeCheck check_tree(const CettaDenseRowNode *node,
                             uint64_t low, uint64_t high) {
    if (!node)
        return (TreeCheck){0};
    assert(node->items && node->number >= low && node->number < high);
    TreeCheck left = check_tree(node->left, low, node->number);
    TreeCheck right = check_tree(node->right, (uint64_t)node->number + 1u, high);
    uint32_t height = (left.height > right.height ? left.height : right.height) + 1u;
    assert(left.height <= right.height + 1u && right.height <= left.height + 1u);
    assert(node->height == height && node->subtree_count == left.count + right.count + 1u);
    return (TreeCheck){height, left.count + right.count + 1u};
}

static void sparse_mutation_model(void) {
    CettaDenseSide side = {0};
    uint8_t model[2048] = {0};
    uint64_t random = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned turn = 0u; turn < 100000u; turn++) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        uint32_t index = (uint32_t)(random % 2048u);
        uint32_t key = index * 65537u, value = (uint32_t)((random >> 12) % 3u);
        uint8_t bit = (uint8_t)(1u << value);
        bool present = (model[index] & bit) != 0u;
        if (random & (UINT64_C(1) << 31)) {
            assert(dense_row_put(&side, key, value) == (present ? 0 : 1));
            model[index] |= bit;
        } else {
            assert(dense_row_take(&side, key, value) == present);
            model[index] &= (uint8_t)~bit;
        }
        assert(dense_side_maintain(&side));
        if (turn % 997u == 0u) {
            uint32_t count = 0u, n;
            DenseSideIterator iterator;
            dense_side_iterator_init(&iterator, &side);
            for (uint32_t i = 0u; i < 2048u; i++) {
                if (!model[i])
                    continue;
                assert(dense_side_next(&iterator, &n, NULL) && n == i * 65537u);
                assert(dense_row_count(&side, n) == (uint32_t)__builtin_popcount(model[i]));
                for (uint32_t v = 0u; v < 3u; v++)
                    assert(dense_row_has(&side, n, v) == ((model[i] & (1u << v)) != 0u));
                count++;
            }
            assert(!dense_side_next(&iterator, &n, NULL));
            TreeCheck tree = check_tree(side.overflow, side.row_cap, UINT64_C(1) << 32);
            assert(tree.count + 1u >= tree.height && count == side.row_count);
            /* Only the zero key can enter the64-slot bootstrap prefix;
             * all truly distant keys remain exact sparse descriptors. */
            assert(side.row_cap <= 64u);
            uint32_t direct = side.row_cap && model[0] ? 1u : 0u;
            uint64_t expected = (uint64_t)side.row_cap * 12u +
                                (uint64_t)(count - direct) * 40u;
            assert(dense_side_control_bytes(&side) == expected &&
                   expected <= (uint64_t)count * 40u + 64u * 12u);
        }
    }
    dense_side_free(&side);
    puts("PASS: 100000 sparse AVL row mutations preserve independent contents, order, height and subtree counts");
}

typedef struct { bool seen[300][300]; uint32_t limit, count; } MatrixCheck;

static bool matrix_pair(uint32_t a, uint32_t b) {
    return (a * 7u + b * 13u) % 5u != 0u;
}

static bool matrix_visit(void *opaque, CettaDenseId a, CettaDenseId b) {
    MatrixCheck *check = opaque;
    assert(a < check->limit && b < check->limit && matrix_pair((uint32_t)a, (uint32_t)b));
    assert(!check->seen[a][b]);
    check->seen[a][b] = true;
    check->count++;
    return true;
}

static void inverse_publication_faults(void) {
    for (unsigned trial = 0u; trial < 4u; trial++) {
        uint32_t limit = trial ? 300u : 200u;
        CettaDenseRelation rel;
        cetta_dense_relation_init(&rel);
        for (uint32_t a = 0u; a < limit; a++) {
            for (uint32_t b = 0u; b < limit; b++) {
                if (matrix_pair(a, b)) {
                    bool oom = false;
                    assert(cetta_dense_relation_add(&rel, a, b, &oom, NULL, NULL));
                }
            }
        }
        injected = false;
        publishing_relation = trial == 1u || trial == 2u ? &rel : NULL;
        publishing_width = trial == 1u ? sizeof(void *) : sizeof(uint32_t);
        dense_second_build(&rel);
        publishing_relation = NULL;
        if (trial == 1u || trial == 2u) {
            assert(injected && !rel.second_kept && rel.second.row_count == 0u);
        } else {
            assert(rel.second_kept && rel.second.row_cap == 256u &&
                   dense_tree_count(rel.second.overflow) == (limit > 256u ? limit - 256u : 0u));
        }
        MatrixCheck check = {.limit = limit};
        assert(cetta_dense_relation_each(&rel, CETTA_DENSE_ANY, CETTA_DENSE_ANY,
                                          matrix_visit, &check));
        assert(check.count == limit * limit * 4u / 5u && rel.pairs == check.count);
        for (uint32_t id = 0u; id < limit; id++) {
            assert(cetta_dense_relation_count(&rel, id, CETTA_DENSE_ANY) == limit * 4u / 5u);
            assert(cetta_dense_relation_count(&rel, CETTA_DENSE_ANY, id) == limit * 4u / 5u);
        }
        cetta_dense_relation_free(&rel);
    }
    puts("PASS: inverse chooses exact minimum before publication; either final-array fault drops only the cache with all72000 pairs intact");
}

int main(void) {
    conversion_faults(1024u);
    conversion_faults(2048u);
    conversion_faults(64u);
    conversion_faults(0u);
    far_rows_and_oom();
    failed_new_row();
    scheduled_prefix_growth();
    numbering_growth_faults();
    sparse_mutation_model();
    inverse_publication_faults();
    return 0;
}
