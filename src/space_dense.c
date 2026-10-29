#include "space_dense.h"

#include <stdlib.h>
#include <string.h>

void cetta_dense_relation_init(CettaDenseRelation *rel) {
    if (rel)
        memset(rel, 0, sizeof(*rel));
}

void cetta_dense_relation_free(CettaDenseRelation *rel) {
    if (!rel)
        return;
    for (uint32_t i = 0u; i < rel->row_cap; i++)
        free(rel->rows[i]);
    free(rel->rows);
    free(rel->row_len);
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

/* Rows.  A row is a sorted array of second-column numbers while it takes
 * fewer bytes than bits up to its largest number, then bits; an array's
 * capacity is its count's power of two, from 2. */

static inline bool dense_row_bits(const CettaDenseRelation *rel, uint32_t na) {
    return (rel->row_len[na] & CETTA_DENSE_ROW_BITS) != 0u;
}

static inline uint32_t dense_row_size(const CettaDenseRelation *rel,
                                      uint32_t na) {
    return rel->row_len[na] & ~CETTA_DENSE_ROW_BITS;
}

static inline uint32_t dense_array_capacity(uint32_t count) {
    uint32_t capacity = 2u;
    while (capacity < count)
        capacity *= 2u;
    return capacity;
}

/* The place of NB in a sorted array of COUNT numbers: where it is, or
 * where it would go. */
static uint32_t dense_array_place(const uint32_t *items, uint32_t count,
                                  uint32_t nb, bool *found) {
    uint32_t low = 0u, high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (items[middle] < nb)
            low = middle + 1u;
        else
            high = middle;
    }
    *found = low < count && items[low] == nb;
    return low;
}

/* Room for a row at A. */
static bool dense_rows_reserve(CettaDenseRelation *rel, uint32_t a) {
    if (a < rel->row_cap)
        return true;
    uint32_t cap = rel->row_cap ? rel->row_cap : 64u;
    while (cap <= a) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    void **rows = realloc(rel->rows, cap * sizeof(*rows));
    if (!rows)
        return false;
    rel->rows = rows;
    uint32_t *len = realloc(rel->row_len, cap * sizeof(*len));
    if (!len)
        return false;
    rel->row_len = len;
    memset(rel->rows + rel->row_cap, 0,
           (cap - rel->row_cap) * sizeof(*rel->rows));
    memset(rel->row_len + rel->row_cap, 0,
           (cap - rel->row_cap) * sizeof(*rel->row_len));
    rel->row_cap = cap;
    return true;
}

static bool dense_row_has(const CettaDenseRelation *rel, uint32_t na,
                          uint32_t nb) {
    if (na >= rel->row_cap || !rel->rows[na])
        return false;
    if (dense_row_bits(rel, na)) {
        const uint64_t *words = rel->rows[na];
        return nb / 64u < dense_row_size(rel, na) &&
               ((words[nb / 64u] >> (nb & 63u)) & 1u);
    }
    bool found = false;
    (void)dense_array_place(rel->rows[na], dense_row_size(rel, na), nb, &found);
    return found;
}

/* Bits of WORDS words, from 1 doubled, covering number NB. */
static uint32_t dense_bits_words(uint32_t words, uint32_t nb) {
    uint32_t need = nb / 64u + 1u;
    if (words == 0u)
        words = 1u;
    while (words < need)
        words = words > UINT32_MAX / 2u ? need : words * 2u;
    return words;
}

/* An array row given over to bits, with NB set too. */
static bool dense_row_to_bits(CettaDenseRelation *rel, uint32_t na,
                              uint32_t nb) {
    const uint32_t *items = rel->rows[na];
    uint32_t count = dense_row_size(rel, na);
    uint32_t largest = count && items[count - 1u] > nb ? items[count - 1u] : nb;
    uint32_t words = dense_bits_words(0u, largest);
    uint64_t *bits = calloc(words, sizeof(*bits));
    if (!bits)
        return false;
    for (uint32_t i = 0u; i < count; i++)
        bits[items[i] / 64u] |= UINT64_C(1) << (items[i] & 63u);
    bits[nb / 64u] |= UINT64_C(1) << (nb & 63u);
    rel->row_bytes -= (uint64_t)dense_array_capacity(count) * sizeof(*items);
    rel->row_bytes += (uint64_t)words * sizeof(*bits);
    free(rel->rows[na]);
    rel->rows[na] = bits;
    rel->row_len[na] = words | CETTA_DENSE_ROW_BITS;
    return true;
}

/* Put NB in row NA: 1 when it is new, 0 when the row held it, -1 without
 * room. */
static int dense_row_put(CettaDenseRelation *rel, uint32_t na, uint32_t nb) {
    if (!dense_rows_reserve(rel, na))
        return -1;
    if (!rel->rows[na]) {
        uint32_t *items = malloc(dense_array_capacity(1u) * sizeof(*items));
        if (!items)
            return -1;
        items[0] = nb;
        rel->rows[na] = items;
        rel->row_len[na] = 1u;
        rel->row_bytes += dense_array_capacity(1u) * sizeof(*items);
        return 1;
    }
    uint32_t size = dense_row_size(rel, na);
    if (dense_row_bits(rel, na)) {
        uint64_t *words = rel->rows[na];
        if (nb / 64u >= size) {
            uint32_t grown = dense_bits_words(size, nb);
            words = realloc(words, grown * sizeof(*words));
            if (!words)
                return -1;
            memset(words + size, 0, (grown - size) * sizeof(*words));
            rel->row_bytes += (uint64_t)(grown - size) * sizeof(*words);
            rel->rows[na] = words;
            rel->row_len[na] = grown | CETTA_DENSE_ROW_BITS;
        }
        uint64_t bit = UINT64_C(1) << (nb & 63u);
        if (words[nb / 64u] & bit)
            return 0;
        words[nb / 64u] |= bit;
        return 1;
    }
    uint32_t *items = rel->rows[na];
    bool found = false;
    uint32_t place = dense_array_place(items, size, nb, &found);
    if (found)
        return 0;
    /* Bits once an array of one more number would take more bytes.  A row
     * that removals emptied keeps its array. */
    uint32_t largest = size && items[size - 1u] > nb ? items[size - 1u] : nb;
    if ((uint64_t)(size + 1u) * sizeof(*items) >
        (uint64_t)(largest / 64u + 1u) * sizeof(uint64_t))
        return dense_row_to_bits(rel, na, nb) ? 1 : -1;
    uint32_t capacity = dense_array_capacity(size);
    if (size + 1u > capacity) {
        uint32_t grown = dense_array_capacity(size + 1u);
        items = realloc(items, grown * sizeof(*items));
        if (!items)
            return -1;
        rel->row_bytes += (uint64_t)(grown - capacity) * sizeof(*items);
        rel->rows[na] = items;
    }
    memmove(items + place + 1u, items + place,
            (size - place) * sizeof(*items));
    items[place] = nb;
    rel->row_len[na] = size + 1u;
    return 1;
}

/* Take NB from row NA: true when the row held it. */
static bool dense_row_take(CettaDenseRelation *rel, uint32_t na, uint32_t nb) {
    if (!dense_row_has(rel, na, nb))
        return false;
    if (dense_row_bits(rel, na)) {
        uint64_t *words = rel->rows[na];
        words[nb / 64u] &= ~(UINT64_C(1) << (nb & 63u));
        return true;
    }
    uint32_t *items = rel->rows[na];
    uint32_t size = dense_row_size(rel, na);
    bool found = false;
    uint32_t place = dense_array_place(items, size, nb, &found);
    memmove(items + place, items + place + 1u,
            (size - place - 1u) * sizeof(*items));
    rel->row_len[na] = size - 1u;
    return true;
}

static uint64_t dense_row_count(const CettaDenseRelation *rel, uint32_t na) {
    if (na >= rel->row_cap || !rel->rows[na])
        return 0u;
    if (!dense_row_bits(rel, na))
        return dense_row_size(rel, na);
    const uint64_t *words = rel->rows[na];
    uint64_t count = 0u;
    for (uint32_t w = 0u; w < dense_row_size(rel, na); w++)
        count += (uint64_t)__builtin_popcountll(words[w]);
    return count;
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
    int put = nb == UINT32_MAX ? -1 : dense_row_put(rel, na, nb);
    if (put < 0) {
        if (oom)
            *oom = true;
        return false;
    }
    if (put == 0)
        return false;
    rel->pairs++;
    return true;
}

bool cetta_dense_relation_contains(const CettaDenseRelation *rel,
                                   CettaDenseId a, CettaDenseId b) {
    uint32_t na = cetta_dense_relation_number(rel, a);
    if (na == UINT32_MAX)
        return false;
    uint32_t nb = cetta_dense_relation_number(rel, b);
    return nb != UINT32_MAX && dense_row_has(rel, na, nb);
}

bool cetta_dense_relation_remove(CettaDenseRelation *rel, CettaDenseId a,
                                 CettaDenseId b) {
    uint32_t na = cetta_dense_relation_number(rel, a);
    uint32_t nb = na == UINT32_MAX ? UINT32_MAX
                                   : cetta_dense_relation_number(rel, b);
    if (nb == UINT32_MAX || !dense_row_take(rel, na, nb))
        return false;
    rel->pairs--;
    return true;
}

static bool dense_each_in_row(const CettaDenseRelation *rel, uint32_t na,
                              CettaDensePairVisit visit, void *context) {
    uint32_t size = dense_row_size(rel, na);
    if (!dense_row_bits(rel, na)) {
        const uint32_t *items = rel->rows[na];
        for (uint32_t i = 0u; i < size; i++) {
            if (!visit(context, rel->ids[na], rel->ids[items[i]]))
                return false;
        }
        return true;
    }
    const uint64_t *words = rel->rows[na];
    for (uint32_t w = 0u; w < size; w++) {
        uint64_t bits = words[w];
        while (bits) {
            uint32_t nb = w * 64u + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1u;
            if (!visit(context, rel->ids[na], rel->ids[nb]))
                return false;
        }
    }
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
        if (na == UINT32_MAX || na >= rel->row_cap || !rel->rows[na])
            return true;
        return dense_each_in_row(rel, na, visit, context);
    }
    uint32_t nb = b_fixed != CETTA_DENSE_ANY
        ? cetta_dense_relation_number(rel, b_fixed) : UINT32_MAX;
    if (b_fixed != CETTA_DENSE_ANY && nb == UINT32_MAX)
        return true;
    for (uint32_t na = 0u; na < rel->row_cap; na++) {
        if (!rel->rows[na])
            continue;
        if (b_fixed == CETTA_DENSE_ANY) {
            if (!dense_each_in_row(rel, na, visit, context))
                return false;
        } else if (dense_row_has(rel, na, nb) &&
                   !visit(context, rel->ids[na], b_fixed)) {
            return false;
        }
    }
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
        return na == UINT32_MAX ? 0u : dense_row_count(rel, na);
    }
    uint32_t nb = cetta_dense_relation_number(rel, b_fixed);
    if (nb == UINT32_MAX)
        return 0u;
    uint64_t count = 0u;
    for (uint32_t na = 0u; na < rel->row_cap; na++)
        count += dense_row_has(rel, na, nb);
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

uint64_t cetta_dense_relation_bytes(const CettaDenseRelation *rel) {
    if (!rel)
        return 0u;
    return (uint64_t)rel->cap * sizeof(*rel->ids) +
           (rel->slots ? (uint64_t)(rel->slot_mask + 1u) * sizeof(*rel->slots)
                       : 0u) +
           (uint64_t)rel->row_cap * (sizeof(*rel->rows) +
                                     sizeof(*rel->row_len)) +
           rel->row_bytes;
}
