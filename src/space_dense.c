#include "space_dense.h"

#include <stdlib.h>
#include <string.h>

void cetta_dense_relation_init(CettaDenseRelation *rel) {
    if (rel)
        memset(rel, 0, sizeof(*rel));
}

static void dense_side_free(CettaDenseSide *side) {
    for (uint32_t i = 0u; i < side->row_cap; i++)
        free(side->rows[i]);
    free(side->rows);
    free(side->row_len);
    memset(side, 0, sizeof(*side));
}

void cetta_dense_relation_free(CettaDenseRelation *rel) {
    if (!rel)
        return;
    dense_side_free(&rel->first);
    dense_side_free(&rel->second);
    free(rel->ids);
    free(rel->slots);
    memset(rel, 0, sizeof(*rel));
}

static inline uint32_t dense_hash(CettaDenseId id) {
    uint64_t h = id * UINT64_C(0x9E3779B97F4A7C15);
    return (uint32_t)(h >> 32);
}

uint32_t cetta_dense_relation_number(const CettaDenseRelation *rel,
                                     CettaDenseId id) {
    if (!rel || !rel->slots || id == CETTA_DENSE_ANY)
        return UINT32_MAX;
    for (uint32_t i = dense_hash(id) & rel->slot_mask;;
         i = (i + 1u) & rel->slot_mask) {
        uint32_t slot = rel->slots[i];
        if (slot == 0u)
            return UINT32_MAX;
        if (rel->ids[slot - 1u] == id)
            return slot - 1u;
    }
}

static bool dense_slots_grow(CettaDenseRelation *rel) {
    uint32_t size = rel->slots ? (rel->slot_mask + 1u) * 2u : 64u;
    if (size == 0u || size > (UINT32_C(1) << 31))
        return false;
    uint32_t *slots = calloc(size, sizeof(*slots));
    if (!slots)
        return false;
    uint32_t mask = size - 1u;
    for (uint32_t n = 0u; n < rel->len; n++) {
        uint32_t i = dense_hash(rel->ids[n]) & mask;
        while (slots[i] != 0u)
            i = (i + 1u) & mask;
        slots[i] = n + 1u;
    }
    free(rel->slots);
    rel->slots = slots;
    rel->slot_mask = mask;
    return true;
}

/* The number of ID, numbering it when it is new; UINT32_MAX without room. */
static uint32_t dense_number_or_add(CettaDenseRelation *rel, CettaDenseId id) {
    uint32_t number = cetta_dense_relation_number(rel, id);
    if (number != UINT32_MAX)
        return number;
    if ((!rel->slots || (uint64_t)(rel->len + 1u) * 4u >
                            (uint64_t)(rel->slot_mask + 1u) * 3u) &&
        !dense_slots_grow(rel))
        return UINT32_MAX;
    if (rel->len == rel->cap) {
        uint32_t cap = rel->cap ? rel->cap * 2u : 64u;
        if (cap <= rel->cap)
            return UINT32_MAX;
        CettaDenseId *ids = realloc(rel->ids, cap * sizeof(*ids));
        if (!ids)
            return UINT32_MAX;
        rel->ids = ids;
        rel->cap = cap;
    }
    number = rel->len++;
    rel->ids[number] = id;
    uint32_t i = dense_hash(id) & rel->slot_mask;
    while (rel->slots[i] != 0u)
        i = (i + 1u) & rel->slot_mask;
    rel->slots[i] = number + 1u;
    return number;
}

/* Rows of a side.  A row is a sorted array of the other column's numbers
 * while it takes fewer bytes than bits up to its largest number, then bits;
 * an array's capacity is its count's power of two, from 2. */

static inline bool dense_row_bits(const CettaDenseSide *side, uint32_t n) {
    return (side->row_len[n] & CETTA_DENSE_ROW_BITS) != 0u;
}

static inline uint32_t dense_row_size(const CettaDenseSide *side, uint32_t n) {
    return side->row_len[n] & ~CETTA_DENSE_ROW_BITS;
}

static inline uint32_t dense_array_capacity(uint32_t count) {
    uint32_t capacity = 2u;
    while (capacity < count)
        capacity *= 2u;
    return capacity;
}

/* The place of M in a sorted array of COUNT numbers: where it is, or
 * where it would go. */
static uint32_t dense_array_place(const uint32_t *items, uint32_t count,
                                  uint32_t m, bool *found) {
    uint32_t low = 0u, high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (items[middle] < m)
            low = middle + 1u;
        else
            high = middle;
    }
    *found = low < count && items[low] == m;
    return low;
}

/* Room for a row at N. */
static bool dense_rows_reserve(CettaDenseSide *side, uint32_t n) {
    if (n < side->row_cap)
        return true;
    uint32_t cap = side->row_cap ? side->row_cap : 64u;
    while (cap <= n) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    void **rows = realloc(side->rows, cap * sizeof(*rows));
    if (!rows)
        return false;
    side->rows = rows;
    uint32_t *len = realloc(side->row_len, cap * sizeof(*len));
    if (!len)
        return false;
    side->row_len = len;
    memset(side->rows + side->row_cap, 0,
           (cap - side->row_cap) * sizeof(*side->rows));
    memset(side->row_len + side->row_cap, 0,
           (cap - side->row_cap) * sizeof(*side->row_len));
    side->row_cap = cap;
    return true;
}

static bool dense_row_has(const CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (n >= side->row_cap || !side->rows[n])
        return false;
    if (dense_row_bits(side, n)) {
        const uint64_t *words = side->rows[n];
        return m / 64u < dense_row_size(side, n) &&
               ((words[m / 64u] >> (m & 63u)) & 1u);
    }
    bool found = false;
    (void)dense_array_place(side->rows[n], dense_row_size(side, n), m, &found);
    return found;
}

/* Bits of WORDS words, from 1 doubled, covering number M. */
static uint32_t dense_bits_words(uint32_t words, uint32_t m) {
    uint32_t need = m / 64u + 1u;
    if (words == 0u)
        words = 1u;
    while (words < need)
        words = words > UINT32_MAX / 2u ? need : words * 2u;
    return words;
}

/* An array row given over to bits, with M set too. */
static bool dense_row_to_bits(CettaDenseSide *side, uint32_t n, uint32_t m) {
    const uint32_t *items = side->rows[n];
    uint32_t count = dense_row_size(side, n);
    uint32_t largest = count && items[count - 1u] > m ? items[count - 1u] : m;
    uint32_t words = dense_bits_words(0u, largest);
    uint64_t *bits = calloc(words, sizeof(*bits));
    if (!bits)
        return false;
    for (uint32_t i = 0u; i < count; i++)
        bits[items[i] / 64u] |= UINT64_C(1) << (items[i] & 63u);
    bits[m / 64u] |= UINT64_C(1) << (m & 63u);
    side->row_bytes -= (uint64_t)dense_array_capacity(count) * sizeof(*items);
    side->row_bytes += (uint64_t)words * sizeof(*bits);
    free(side->rows[n]);
    side->rows[n] = bits;
    side->row_len[n] = words | CETTA_DENSE_ROW_BITS;
    return true;
}

/* Put M in row N: 1 when it is new, 0 when the row held it, -1 without
 * room. */
static int dense_row_put(CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (!dense_rows_reserve(side, n))
        return -1;
    if (!side->rows[n]) {
        uint32_t *items = malloc(dense_array_capacity(1u) * sizeof(*items));
        if (!items)
            return -1;
        items[0] = m;
        side->rows[n] = items;
        side->row_len[n] = 1u;
        side->row_bytes += dense_array_capacity(1u) * sizeof(*items);
        return 1;
    }
    uint32_t size = dense_row_size(side, n);
    if (dense_row_bits(side, n)) {
        uint64_t *words = side->rows[n];
        if (m / 64u >= size) {
            uint32_t grown = dense_bits_words(size, m);
            words = realloc(words, grown * sizeof(*words));
            if (!words)
                return -1;
            memset(words + size, 0, (grown - size) * sizeof(*words));
            side->row_bytes += (uint64_t)(grown - size) * sizeof(*words);
            side->rows[n] = words;
            side->row_len[n] = grown | CETTA_DENSE_ROW_BITS;
        }
        uint64_t bit = UINT64_C(1) << (m & 63u);
        if (words[m / 64u] & bit)
            return 0;
        words[m / 64u] |= bit;
        return 1;
    }
    uint32_t *items = side->rows[n];
    bool found = false;
    uint32_t place = dense_array_place(items, size, m, &found);
    if (found)
        return 0;
    /* Bits once an array of one more number would take more bytes.  A row
     * that removals emptied keeps its array. */
    uint32_t largest = size && items[size - 1u] > m ? items[size - 1u] : m;
    if ((uint64_t)(size + 1u) * sizeof(*items) >
        (uint64_t)(largest / 64u + 1u) * sizeof(uint64_t))
        return dense_row_to_bits(side, n, m) ? 1 : -1;
    uint32_t capacity = dense_array_capacity(size);
    if (size + 1u > capacity) {
        uint32_t grown = dense_array_capacity(size + 1u);
        items = realloc(items, grown * sizeof(*items));
        if (!items)
            return -1;
        side->row_bytes += (uint64_t)(grown - capacity) * sizeof(*items);
        side->rows[n] = items;
    }
    memmove(items + place + 1u, items + place,
            (size - place) * sizeof(*items));
    items[place] = m;
    side->row_len[n] = size + 1u;
    return 1;
}

/* Take M from row N: true when the row held it. */
static bool dense_row_take(CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (!dense_row_has(side, n, m))
        return false;
    if (dense_row_bits(side, n)) {
        uint64_t *words = side->rows[n];
        words[m / 64u] &= ~(UINT64_C(1) << (m & 63u));
        return true;
    }
    uint32_t *items = side->rows[n];
    uint32_t size = dense_row_size(side, n);
    bool found = false;
    uint32_t place = dense_array_place(items, size, m, &found);
    memmove(items + place, items + place + 1u,
            (size - place - 1u) * sizeof(*items));
    side->row_len[n] = size - 1u;
    return true;
}

static uint64_t dense_row_count(const CettaDenseSide *side, uint32_t n) {
    if (n >= side->row_cap || !side->rows[n])
        return 0u;
    if (!dense_row_bits(side, n))
        return dense_row_size(side, n);
    const uint64_t *words = side->rows[n];
    uint64_t count = 0u;
    for (uint32_t w = 0u; w < dense_row_size(side, n); w++)
        count += (uint64_t)__builtin_popcountll(words[w]);
    return count;
}

/* Visit row N of SIDE in ascending order of the other column's numbers, as
 * pairs (first, second): SECOND tells that N numbers the second column. */
static bool dense_each_in_row(const CettaDenseRelation *rel,
                              const CettaDenseSide *side, uint32_t n,
                              bool second, CettaDensePairVisit visit,
                              void *context) {
    if (n >= side->row_cap || !side->rows[n])
        return true;
    uint32_t size = dense_row_size(side, n);
    if (!dense_row_bits(side, n)) {
        const uint32_t *items = side->rows[n];
        for (uint32_t i = 0u; i < size; i++) {
            bool kept = second
                ? visit(context, rel->ids[items[i]], rel->ids[n])
                : visit(context, rel->ids[n], rel->ids[items[i]]);
            if (!kept)
                return false;
        }
        return true;
    }
    const uint64_t *words = side->rows[n];
    for (uint32_t w = 0u; w < size; w++) {
        uint64_t bits = words[w];
        while (bits) {
            uint32_t m = w * 64u + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1u;
            bool kept = second ? visit(context, rel->ids[m], rel->ids[n])
                               : visit(context, rel->ids[n], rel->ids[m]);
            if (!kept)
                return false;
        }
    }
    return true;
}

/* The second side: for each second-column number, the first-column numbers
 * paired with it.  A probe that binds only the second column reads every
 * first-column row without it: it pays a rent of the rows it reads, and
 * once the rents paid reach what building the second side reads (every row
 * and every pair), the side is built and kept from then on, and such
 * probes read only their answers (rent, then buy: at most twice the cost of
 * the best choice of when to build, plus one probe's rent).  A side that
 * cannot grow is dropped, and probes read the rows again.  The side is a
 * cache the reads keep: it leaves the pairs as they are. */
static void dense_second_drop(CettaDenseRelation *rel) {
    dense_side_free(&rel->second);
    rel->second_kept = false;
    rel->second_rent = 0u;
}

static void dense_second_build(CettaDenseRelation *rel) {
    for (uint32_t na = 0u; na < rel->first.row_cap; na++) {
        const CettaDenseSide *first = &rel->first;
        if (!first->rows[na])
            continue;
        uint32_t size = dense_row_size(first, na);
        if (!dense_row_bits(first, na)) {
            const uint32_t *items = first->rows[na];
            for (uint32_t i = 0u; i < size; i++) {
                if (dense_row_put(&rel->second, items[i], na) < 0) {
                    dense_second_drop(rel);
                    return;
                }
            }
            continue;
        }
        const uint64_t *words = first->rows[na];
        for (uint32_t w = 0u; w < size; w++) {
            uint64_t bits = words[w];
            while (bits) {
                uint32_t nb = w * 64u + (uint32_t)__builtin_ctzll(bits);
                bits &= bits - 1u;
                if (dense_row_put(&rel->second, nb, na) < 0) {
                    dense_second_drop(rel);
                    return;
                }
            }
        }
    }
    rel->second_kept = true;
}

/* A probe binding only the second column read every first-column row:
 * its rent, and the second side built once the rents reach its price. */
static void dense_second_rent(const CettaDenseRelation *rel) {
    CettaDenseRelation *kept = (CettaDenseRelation *)rel;
    kept->second_rent += rel->first.row_cap;
    if (kept->second_rent >= rel->pairs + rel->first.row_cap)
        dense_second_build(kept);
}

bool cetta_dense_relation_add(CettaDenseRelation *rel, CettaDenseId a,
                              CettaDenseId b, bool *oom) {
    if (oom)
        *oom = false;
    if (!rel || a == CETTA_DENSE_ANY || b == CETTA_DENSE_ANY) {
        if (oom)
            *oom = true;
        return false;
    }
    uint32_t na = dense_number_or_add(rel, a);
    uint32_t nb = na == UINT32_MAX ? UINT32_MAX : dense_number_or_add(rel, b);
    int put = nb == UINT32_MAX ? -1 : dense_row_put(&rel->first, na, nb);
    if (put < 0) {
        if (oom)
            *oom = true;
        return false;
    }
    if (put == 0)
        return false;
    if (rel->second_kept && dense_row_put(&rel->second, nb, na) < 0)
        dense_second_drop(rel);
    rel->pairs++;
    return true;
}

bool cetta_dense_relation_contains(const CettaDenseRelation *rel,
                                   CettaDenseId a, CettaDenseId b) {
    uint32_t na = cetta_dense_relation_number(rel, a);
    if (na == UINT32_MAX)
        return false;
    uint32_t nb = cetta_dense_relation_number(rel, b);
    return nb != UINT32_MAX && dense_row_has(&rel->first, na, nb);
}

bool cetta_dense_relation_remove(CettaDenseRelation *rel, CettaDenseId a,
                                 CettaDenseId b) {
    uint32_t na = cetta_dense_relation_number(rel, a);
    uint32_t nb = na == UINT32_MAX ? UINT32_MAX
                                   : cetta_dense_relation_number(rel, b);
    if (nb == UINT32_MAX || !dense_row_take(&rel->first, na, nb))
        return false;
    if (rel->second_kept)
        (void)dense_row_take(&rel->second, nb, na);
    rel->pairs--;
    return true;
}

bool cetta_dense_relation_each(const CettaDenseRelation *rel,
                               CettaDenseId a_fixed, CettaDenseId b_fixed,
                               CettaDensePairVisit visit, void *context) {
    if (!rel || !visit)
        return true;
    if (a_fixed != CETTA_DENSE_ANY && b_fixed != CETTA_DENSE_ANY)
        return !cetta_dense_relation_contains(rel, a_fixed, b_fixed) ||
               visit(context, a_fixed, b_fixed);
    if (a_fixed != CETTA_DENSE_ANY) {
        uint32_t na = cetta_dense_relation_number(rel, a_fixed);
        if (na == UINT32_MAX)
            return true;
        return dense_each_in_row(rel, &rel->first, na, false, visit, context);
    }
    if (b_fixed == CETTA_DENSE_ANY) {
        for (uint32_t na = 0u; na < rel->first.row_cap; na++) {
            if (!dense_each_in_row(rel, &rel->first, na, false, visit, context))
                return false;
        }
        return true;
    }
    uint32_t nb = cetta_dense_relation_number(rel, b_fixed);
    if (nb == UINT32_MAX)
        return true;
    if (rel->second_kept)
        return dense_each_in_row(rel, &rel->second, nb, true, visit, context);
    for (uint32_t na = 0u; na < rel->first.row_cap; na++) {
        if (dense_row_has(&rel->first, na, nb) &&
            !visit(context, rel->ids[na], b_fixed))
            return false;
    }
    dense_second_rent(rel);
    return true;
}

uint64_t cetta_dense_relation_count(const CettaDenseRelation *rel,
                                    CettaDenseId a_fixed,
                                    CettaDenseId b_fixed) {
    if (!rel)
        return 0u;
    if (a_fixed == CETTA_DENSE_ANY && b_fixed == CETTA_DENSE_ANY)
        return rel->pairs;
    if (a_fixed != CETTA_DENSE_ANY && b_fixed != CETTA_DENSE_ANY)
        return cetta_dense_relation_contains(rel, a_fixed, b_fixed);
    if (a_fixed != CETTA_DENSE_ANY) {
        uint32_t na = cetta_dense_relation_number(rel, a_fixed);
        return na == UINT32_MAX ? 0u : dense_row_count(&rel->first, na);
    }
    uint32_t nb = cetta_dense_relation_number(rel, b_fixed);
    if (nb == UINT32_MAX)
        return 0u;
    if (rel->second_kept)
        return dense_row_count(&rel->second, nb);
    uint64_t count = 0u;
    for (uint32_t na = 0u; na < rel->first.row_cap; na++)
        count += dense_row_has(&rel->first, na, nb);
    dense_second_rent(rel);
    return count;
}

void cetta_dense_relation_each_numbered(const CettaDenseRelation *rel,
                                        CettaDenseIdVisit visit,
                                        void *context) {
    if (!rel || !visit)
        return;
    for (uint32_t n = 0u; n < rel->len; n++)
        visit(context, rel->ids[n]);
}

static uint64_t dense_side_bytes(const CettaDenseSide *side) {
    return (uint64_t)side->row_cap * (sizeof(*side->rows) +
                                      sizeof(*side->row_len)) +
           side->row_bytes;
}

uint64_t cetta_dense_relation_bytes(const CettaDenseRelation *rel) {
    if (!rel)
        return 0u;
    return (uint64_t)rel->cap * sizeof(*rel->ids) +
           (rel->slots ? (uint64_t)(rel->slot_mask + 1u) * sizeof(*rel->slots)
                       : 0u) +
           dense_side_bytes(&rel->first) + dense_side_bytes(&rel->second);
}
