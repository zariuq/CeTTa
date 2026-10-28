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
    free(rel->row_words);
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

/* Room for row A to hold bit B. */
static bool dense_row_reserve(CettaDenseRelation *rel, uint32_t a,
                              uint32_t b) {
    if (a >= rel->row_cap) {
        uint32_t cap = rel->row_cap ? rel->row_cap : 64u;
        while (cap <= a)
            cap *= 2u;
        uint64_t **rows = realloc(rel->rows, cap * sizeof(*rows));
        if (!rows)
            return false;
        rel->rows = rows;
        uint32_t *words = realloc(rel->row_words, cap * sizeof(*words));
        if (!words)
            return false;
        rel->row_words = words;
        memset(rel->rows + rel->row_cap, 0,
               (cap - rel->row_cap) * sizeof(*rel->rows));
        memset(rel->row_words + rel->row_cap, 0,
               (cap - rel->row_cap) * sizeof(*rel->row_words));
        rel->row_cap = cap;
    }
    uint32_t need = b / 64u + 1u;
    if (need <= rel->row_words[a])
        return true;
    /* A row grows by doubling to the bits it holds: a row of one pair costs
     * a word, and a full row twice its columns at most. */
    uint32_t words = rel->row_words[a] ? rel->row_words[a] : 1u;
    while (words < need)
        words = words > UINT32_MAX / 2u ? need : words * 2u;
    uint64_t *row = realloc(rel->rows[a], words * sizeof(*row));
    if (!row)
        return false;
    memset(row + rel->row_words[a], 0,
           (words - rel->row_words[a]) * sizeof(*row));
    rel->row_bytes += (uint64_t)(words - rel->row_words[a]) * sizeof(*row);
    rel->rows[a] = row;
    rel->row_words[a] = words;
    return true;
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
    if (nb == UINT32_MAX || !dense_row_reserve(rel, na, nb)) {
        if (oom)
            *oom = true;
        return false;
    }
    uint64_t bit = UINT64_C(1) << (nb & 63u);
    uint64_t *word = &rel->rows[na][nb / 64u];
    if (*word & bit)
        return false;
    *word |= bit;
    rel->pairs++;
    return true;
}

bool cetta_dense_relation_contains(const CettaDenseRelation *rel,
                                   CettaDenseId a, CettaDenseId b) {
    uint32_t na = cetta_dense_relation_number(rel, a);
    if (na == UINT32_MAX || na >= rel->row_cap || !rel->rows[na])
        return false;
    uint32_t nb = cetta_dense_relation_number(rel, b);
    if (nb == UINT32_MAX || nb / 64u >= rel->row_words[na])
        return false;
    return (rel->rows[na][nb / 64u] >> (nb & 63u)) & 1u;
}

bool cetta_dense_relation_remove(CettaDenseRelation *rel, CettaDenseId a,
                                 CettaDenseId b) {
    if (!cetta_dense_relation_contains(rel, a, b))
        return false;
    uint32_t na = cetta_dense_relation_number(rel, a);
    uint32_t nb = cetta_dense_relation_number(rel, b);
    rel->rows[na][nb / 64u] &= ~(UINT64_C(1) << (nb & 63u));
    rel->pairs--;
    return true;
}

static bool dense_each_in_row(const CettaDenseRelation *rel, uint32_t na,
                              CettaDensePairVisit visit, void *context) {
    const uint64_t *row = rel->rows[na];
    for (uint32_t w = 0u; w < rel->row_words[na]; w++) {
        uint64_t bits = row[w];
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
        } else if (nb / 64u < rel->row_words[na] &&
                   ((rel->rows[na][nb / 64u] >> (nb & 63u)) & 1u) &&
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
    uint64_t count = 0u;
    if (a_fixed != CETTA_DENSE_ANY) {
        uint32_t na = cetta_dense_relation_number(rel, a_fixed);
        if (na == UINT32_MAX || na >= rel->row_cap || !rel->rows[na])
            return 0u;
        for (uint32_t w = 0u; w < rel->row_words[na]; w++)
            count += (uint64_t)__builtin_popcountll(rel->rows[na][w]);
        return count;
    }
    uint32_t nb = cetta_dense_relation_number(rel, b_fixed);
    if (nb == UINT32_MAX)
        return 0u;
    for (uint32_t na = 0u; na < rel->row_cap; na++)
        count += rel->rows[na] && nb / 64u < rel->row_words[na] &&
                 ((rel->rows[na][nb / 64u] >> (nb & 63u)) & 1u);
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
                                     sizeof(*rel->row_words)) +
           rel->row_bytes;
}
