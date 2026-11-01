#include "space_tuples.h"

#include <stdlib.h>
#include <string.h>

static uint64_t tuple_mix(uint64_t x) {
    x ^= x >> 31;
    x *= UINT64_C(0x7fb5d329728ea185);
    x ^= x >> 27;
    x *= UINT64_C(0x81dadef4bc2dd44d);
    x ^= x >> 33;
    return x;
}

/* ── The numbering ───────────────────────────────────────────────────── */

static inline uint8_t hash_tag(uint64_t h) {
    return (uint8_t)(h >> 56);
}

static uint32_t number_find(const CettaTupleTable *table, CettaDenseId id) {
    if (!table->slots)
        return UINT32_MAX;
    uint64_t h = tuple_mix(id);
    uint8_t tag = hash_tag(h);
    for (uint32_t pos = (uint32_t)h & table->slot_mask;;
         pos = (pos + 1u) & table->slot_mask) {
        uint32_t slot = table->slots[pos];
        if (!slot)
            return UINT32_MAX;
        if (table->slot_tags[pos] == tag && table->ids[slot - 1u] == id)
            return slot - 1u;
    }
}

/* Room in the id table for NEED numbers at three quarters' load. */
static bool number_reserve(CettaTupleTable *table, uint32_t need) {
    uint32_t cap = table->slots ? table->slot_mask + 1u : 64u;
    while ((uint64_t)need * 4u > (uint64_t)cap * 3u) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (table->slots && cap == table->slot_mask + 1u)
        return true;
    uint32_t *slots = calloc(cap, sizeof(*slots));
    uint8_t *tags = malloc(cap);
    if (!slots || !tags) {
        free(slots);
        free(tags);
        return false;
    }
    for (uint32_t n = 0u; n < table->len; n++) {
        uint64_t h = tuple_mix(table->ids[n]);
        uint32_t pos = (uint32_t)h & (cap - 1u);
        while (slots[pos])
            pos = (pos + 1u) & (cap - 1u);
        slots[pos] = n + 1u;
        tags[pos] = hash_tag(h);
    }
    free(table->slots);
    free(table->slot_tags);
    table->slots = slots;
    table->slot_tags = tags;
    table->slot_mask = cap - 1u;
    return true;
}

/* The number of ID, numbered now when it is new; UINT32_MAX when memory
 * runs out. */
static uint32_t number_of(CettaTupleTable *table, CettaDenseId id) {
    uint32_t number = number_find(table, id);
    if (number != UINT32_MAX)
        return number;
    if (table->len >= UINT32_MAX - 1u ||
        !number_reserve(table, table->len + 1u))
        return UINT32_MAX;
    if (table->len == table->cap) {
        uint32_t cap = table->cap ? table->cap * 2u : 64u;
        CettaDenseId *ids = realloc(table->ids, (size_t)cap * sizeof(*ids));
        if (!ids)
            return UINT32_MAX;
        table->ids = ids;
        table->cap = cap;
    }
    number = table->len++;
    table->ids[number] = id;
    uint64_t h = tuple_mix(id);
    uint32_t pos = (uint32_t)h & table->slot_mask;
    while (table->slots[pos])
        pos = (pos + 1u) & table->slot_mask;
    table->slots[pos] = number + 1u;
    table->slot_tags[pos] = hash_tag(h);
    return number;
}

/* ── The tuples and their hash ───────────────────────────────────────── */

static const uint32_t *tuple_at(const CettaTupleTable *table, uint32_t pos) {
    return table->tuples + (size_t)pos * table->arity;
}

static inline bool tuple_equal(const uint32_t *a, const uint32_t *b,
                               uint32_t arity) {
    for (uint32_t c = 0u; c < arity; c++)
        if (a[c] != b[c])
            return false;
    return true;
}

static uint64_t tuple_hash(const uint32_t *numbers, uint32_t arity) {
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15) * (arity + 1u);
    for (uint32_t c = 0u; c < arity; c++)
        h = tuple_mix(h ^ numbers[c]);
    return h;
}

/* The position of the tuple NUMBERS, whose hash is H, or UINT32_MAX; *SLOT
 * gets its slot, or the empty slot that ends its probe. */
static uint32_t hash_find(const CettaTupleTable *table, const uint32_t *numbers,
                          uint64_t h, uint32_t *slot) {
    if (!table->hash)
        return UINT32_MAX;
    uint8_t tag = hash_tag(h);
    for (uint32_t s = (uint32_t)h & table->hash_mask;;
         s = (s + 1u) & table->hash_mask) {
        uint32_t value = table->hash[s];
        if (!value) {
            if (slot)
                *slot = s;
            return UINT32_MAX;
        }
        if (table->hash_tags[s] == tag &&
            tuple_equal(tuple_at(table, value - 1u), numbers, table->arity)) {
            if (slot)
                *slot = s;
            return value - 1u;
        }
    }
}

/* Room in the tuple hash for NEED tuples at three quarters' load. */
static bool hash_reserve(CettaTupleTable *table, uint32_t need) {
    uint32_t cap = table->hash ? table->hash_mask + 1u : 64u;
    while ((uint64_t)need * 4u > (uint64_t)cap * 3u) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (table->hash && cap == table->hash_mask + 1u)
        return true;
    uint32_t *hash = calloc(cap, sizeof(*hash));
    uint8_t *tags = malloc(cap);
    if (!hash || !tags) {
        free(hash);
        free(tags);
        return false;
    }
    for (uint32_t pos = 0u; pos < table->count; pos++) {
        uint64_t h = tuple_hash(tuple_at(table, pos), table->arity);
        uint32_t s = (uint32_t)h & (cap - 1u);
        while (hash[s])
            s = (s + 1u) & (cap - 1u);
        hash[s] = pos + 1u;
        tags[s] = hash_tag(h);
    }
    free(table->hash);
    free(table->hash_tags);
    table->hash = hash;
    table->hash_tags = tags;
    table->hash_mask = cap - 1u;
    return true;
}

/* Empty SLOT, shifting the entries after it back while their homes allow,
 * so every remaining tuple stays on its probe path.  It reads the tuples
 * the entries name, which must be in place. */
static void hash_delete(CettaTupleTable *table, uint32_t slot) {
    uint32_t mask = table->hash_mask;
    uint32_t hole = slot;
    for (uint32_t next = (hole + 1u) & mask; table->hash[next];
         next = (next + 1u) & mask) {
        uint32_t home = (uint32_t)tuple_hash(
            tuple_at(table, table->hash[next] - 1u), table->arity) & mask;
        if (((next - home) & mask) >= ((next - hole) & mask)) {
            table->hash[hole] = table->hash[next];
            table->hash_tags[hole] = table->hash_tags[next];
            hole = next;
        }
    }
    table->hash[hole] = 0u;
}

/* ── Sides ───────────────────────────────────────────────────────────── */

static void side_free(CettaTupleSide *side) {
    free(side->first);
    free(side->len);
    free(side->next);
    free(side->prev);
    memset(side, 0, sizeof(*side));
}

static bool grow_u32(uint32_t **array, uint32_t old_cap, uint32_t cap) {
    uint32_t *grown = realloc(*array, (size_t)cap * sizeof(*grown));
    if (!grown)
        return false;
    memset(grown + old_cap, 0, (size_t)(cap - old_cap) * sizeof(*grown));
    *array = grown;
    return true;
}

/* The room for NEED after CAP: exactly NEED (at least 64) at first, as a
 * side is built over the tuples it has, then doubling. */
static uint32_t grown_cap(uint32_t cap, uint32_t need) {
    if (!cap)
        return need > 64u ? need : 64u;
    while (cap < need) {
        if (cap > UINT32_MAX / 2u)
            return 0u;
        cap *= 2u;
    }
    return cap;
}

/* Room for numbers below NUMBERS and positions below TUPLES. */
static bool side_reserve(CettaTupleSide *side, uint32_t numbers,
                         uint32_t tuples) {
    if (numbers > side->number_cap) {
        uint32_t cap = grown_cap(side->number_cap, numbers);
        if (!cap || !grow_u32(&side->first, side->number_cap, cap) ||
            !grow_u32(&side->len, side->number_cap, cap))
            return false;
        side->number_cap = cap;
    }
    if (tuples > side->tuple_cap) {
        uint32_t cap = grown_cap(side->tuple_cap, tuples);
        if (!cap || !grow_u32(&side->next, side->tuple_cap, cap) ||
            !grow_u32(&side->prev, side->tuple_cap, cap))
            return false;
        side->tuple_cap = cap;
    }
    return true;
}

/* Chain POS first among NUMBER's tuples. */
static void side_link(CettaTupleSide *side, uint32_t number, uint32_t pos) {
    uint32_t head = side->first[number];
    side->next[pos] = head;
    side->prev[pos] = 0u;
    if (head)
        side->prev[head - 1u] = pos + 1u;
    side->first[number] = pos + 1u;
    side->len[number]++;
}

static void side_unlink(CettaTupleSide *side, uint32_t number, uint32_t pos) {
    uint32_t before = side->prev[pos];
    uint32_t after = side->next[pos];
    if (before)
        side->next[before - 1u] = after;
    else
        side->first[number] = after;
    if (after)
        side->prev[after - 1u] = before;
    side->len[number]--;
}

/* The tuple at FROM, chained under NUMBER, moves to the free position TO. */
static void side_move(CettaTupleSide *side, uint32_t number, uint32_t from,
                      uint32_t to) {
    uint32_t before = side->prev[from];
    uint32_t after = side->next[from];
    side->next[to] = after;
    side->prev[to] = before;
    if (before)
        side->next[before - 1u] = to + 1u;
    else
        side->first[number] = to + 1u;
    if (after)
        side->prev[after - 1u] = to + 1u;
}

/* NUMBER's chain: its first tuple plus one, and its length; a number the
 * side has no room for yet (numbered by an add that ran out of memory) has
 * none. */
static uint32_t side_first(const CettaTupleSide *side, uint32_t number) {
    return number < side->number_cap ? side->first[number] : 0u;
}

static uint32_t side_len(const CettaTupleSide *side, uint32_t number) {
    return number < side->number_cap ? side->len[number] : 0u;
}

/* Build column C's side over the tuples held now. */
static bool side_build(CettaTupleTable *table, uint32_t c) {
    CettaTupleSide *side = &table->side[c];
    if (!side_reserve(side, table->len, table->count)) {
        side_free(side);
        return false;
    }
    for (uint32_t pos = 0u; pos < table->count; pos++)
        side_link(side, tuple_at(table, pos)[c], pos);
    table->side_kept[c] = true;
    return true;
}

/* ── The table ───────────────────────────────────────────────────────── */

bool cetta_tuple_table_init(CettaTupleTable *table, uint32_t arity) {
    if (!table)
        return false;
    memset(table, 0, sizeof(*table));
    if (arity == 0u || arity > CETTA_TUPLE_MAX_ARITY)
        return false;
    table->arity = arity;
    return true;
}

void cetta_tuple_table_free(CettaTupleTable *table) {
    if (!table)
        return;
    free(table->ids);
    free(table->slots);
    free(table->slot_tags);
    free(table->tuples);
    free(table->hash);
    free(table->hash_tags);
    for (uint32_t c = 0u; c < CETTA_TUPLE_MAX_ARITY; c++)
        side_free(&table->side[c]);
    uint32_t arity = table->arity;
    memset(table, 0, sizeof(*table));
    table->arity = arity;
}

bool cetta_tuple_table_add(CettaTupleTable *table, const CettaDenseId *tuple,
                           bool *oom) {
    if (oom)
        *oom = false;
    if (!table || !tuple)
        return false;
    uint32_t numbers[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t c = 0u; c < table->arity; c++) {
        numbers[c] = number_of(table, tuple[c]);
        if (numbers[c] == UINT32_MAX)
            goto out_of_memory;
    }
    uint64_t h = tuple_hash(numbers, table->arity);
    uint32_t slot = 0u;
    if (hash_find(table, numbers, h, &slot) != UINT32_MAX)
        return false;
    if (table->count >= UINT32_MAX - 1u)
        goto out_of_memory;
    if (table->count == table->tuple_cap) {
        uint32_t cap = table->tuple_cap ? table->tuple_cap * 2u : 64u;
        uint32_t *tuples = realloc(table->tuples,
                                   (size_t)cap * table->arity * sizeof(*tuples));
        if (!tuples)
            goto out_of_memory;
        table->tuples = tuples;
        table->tuple_cap = cap;
    }
    uint32_t mask = table->hash ? table->hash_mask : 0u;
    if (!hash_reserve(table, table->count + 1u))
        goto out_of_memory;
    /* A rebuilt hash puts the probe's end elsewhere. */
    if (table->hash_mask != mask || !mask)
        (void)hash_find(table, numbers, h, &slot);
    for (uint32_t c = 0u; c < table->arity; c++)
        if (table->side_kept[c] &&
            !side_reserve(&table->side[c], table->len, table->count + 1u))
            goto out_of_memory;
    uint32_t pos = table->count++;
    memcpy(table->tuples + (size_t)pos * table->arity, numbers,
           (size_t)table->arity * sizeof(*numbers));
    for (uint32_t c = 0u; c < table->arity; c++)
        if (table->side_kept[c])
            side_link(&table->side[c], numbers[c], pos);
    table->hash[slot] = pos + 1u;
    table->hash_tags[slot] = hash_tag(h);
    return true;
out_of_memory:
    if (oom)
        *oom = true;
    return false;
}

bool cetta_tuple_table_remove(CettaTupleTable *table,
                              const CettaDenseId *tuple) {
    if (!table || !tuple || table->count == 0u)
        return false;
    uint32_t numbers[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t c = 0u; c < table->arity; c++) {
        numbers[c] = number_find(table, tuple[c]);
        if (numbers[c] == UINT32_MAX)
            return false;
    }
    uint32_t slot = 0u;
    uint32_t pos = hash_find(table, numbers, tuple_hash(numbers, table->arity),
                             &slot);
    if (pos == UINT32_MAX)
        return false;
    uint32_t last = table->count - 1u;
    hash_delete(table, slot);
    for (uint32_t c = 0u; c < table->arity; c++)
        if (table->side_kept[c])
            side_unlink(&table->side[c], numbers[c], pos);
    if (pos != last) {
        /* The last tuple takes the freed position. */
        const uint32_t *moved = tuple_at(table, last);
        uint32_t moved_slot = 0u;
        (void)hash_find(table, moved, tuple_hash(moved, table->arity),
                        &moved_slot);
        table->hash[moved_slot] = pos + 1u;
        for (uint32_t c = 0u; c < table->arity; c++)
            if (table->side_kept[c])
                side_move(&table->side[c], moved[c], last, pos);
        memmove(table->tuples + (size_t)pos * table->arity, moved,
                (size_t)table->arity * sizeof(*moved));
    }
    table->count--;
    return true;
}

bool cetta_tuple_table_contains(const CettaTupleTable *table,
                                const CettaDenseId *tuple) {
    if (!table || !tuple || table->count == 0u)
        return false;
    uint32_t numbers[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t c = 0u; c < table->arity; c++) {
        numbers[c] = number_find(table, tuple[c]);
        if (numbers[c] == UINT32_MAX)
            return false;
    }
    return hash_find(table, numbers, tuple_hash(numbers, table->arity),
                     NULL) != UINT32_MAX;
}

/* Visit or count the tuples matching FIXED: VISIT NULL counts into *COUNT.
 * A walk reads the shortest kept side of its bound columns, or every tuple
 * when none is kept.  Each bound column without a side pays what the walk
 * reads as rent; once a column's rent has reached the cost of building its
 * side, the side is built and kept, and the walk may read it. */
static bool tuple_walk(CettaTupleTable *table, const CettaDenseId *fixed,
                       CettaTupleVisit visit, void *context, uint64_t *count) {
    uint32_t arity = table->arity;
    uint32_t want[CETTA_TUPLE_MAX_ARITY] = {0};
    bool bound[CETTA_TUPLE_MAX_ARITY];
    uint32_t bound_len = 0u;
    for (uint32_t c = 0u; c < arity; c++) {
        bound[c] = fixed && fixed[c] != CETTA_DENSE_ANY;
        if (!bound[c])
            continue;
        want[c] = number_find(table, fixed[c]);
        /* An id no tuple holds selects nothing. */
        if (want[c] == UINT32_MAX)
            return true;
        bound_len++;
    }
    CettaDenseId ids[CETTA_TUPLE_MAX_ARITY];
    if (bound_len == arity) {
        uint32_t pos = hash_find(table, want, tuple_hash(want, arity), NULL);
        if (pos == UINT32_MAX)
            return true;
        if (!visit) {
            (*count)++;
            return true;
        }
        for (uint32_t c = 0u; c < arity; c++)
            ids[c] = table->ids[want[c]];
        return visit(context, ids);
    }
    if (bound_len == 0u && !visit) {
        *count += table->count;
        return true;
    }
    int best = -1;
    uint32_t best_len = UINT32_MAX;
    for (uint32_t c = 0u; c < arity; c++) {
        if (!bound[c] || !table->side_kept[c])
            continue;
        uint32_t len = side_len(&table->side[c], want[c]);
        if (len < best_len) {
            best = (int)c;
            best_len = len;
        }
    }
    uint32_t reads = best >= 0 ? best_len : table->count;
    for (uint32_t c = 0u; c < arity && bound_len > 0u; c++) {
        if (!bound[c] || table->side_kept[c])
            continue;
        if (table->side_rent[c] < table->count || table->count == 0u) {
            table->side_rent[c] += reads;
            continue;
        }
        /* The reads paid so far reach the cost of building the side. */
        if (!side_build(table, c))
            return false;
        uint32_t len = side_len(&table->side[c], want[c]);
        if (len < best_len) {
            best = (int)c;
            best_len = len;
        }
    }
    if (best >= 0 && bound_len == 1u && !visit) {
        *count += best_len;
        return true;
    }
    const CettaTupleSide *side = best >= 0 ? &table->side[best] : NULL;
    uint32_t next = side ? side_first(side, want[best]) : (table->count ? 1u : 0u);
    while (next) {
        uint32_t pos = next - 1u;
        next = side ? side->next[pos] : (pos + 1u < table->count ? pos + 2u : 0u);
        const uint32_t *numbers = tuple_at(table, pos);
        bool match = true;
        for (uint32_t c = 0u; c < arity && match; c++)
            match = !bound[c] || numbers[c] == want[c];
        if (!match)
            continue;
        if (!visit) {
            (*count)++;
            continue;
        }
        for (uint32_t c = 0u; c < arity; c++)
            ids[c] = table->ids[numbers[c]];
        if (!visit(context, ids))
            return false;
    }
    return true;
}

bool cetta_tuple_table_each(CettaTupleTable *table, const CettaDenseId *fixed,
                            CettaTupleVisit visit, void *context) {
    if (!table || !visit)
        return false;
    return tuple_walk(table, fixed, visit, context, NULL);
}

uint64_t cetta_tuple_table_count(CettaTupleTable *table,
                                 const CettaDenseId *fixed) {
    if (!table)
        return 0u;
    uint64_t count = 0u;
    (void)tuple_walk(table, fixed, NULL, NULL, &count);
    return count;
}

void cetta_tuple_table_each_numbered(const CettaTupleTable *table,
                                     CettaDenseIdVisit visit, void *context) {
    if (!table || !visit)
        return;
    for (uint32_t n = 0u; n < table->len; n++)
        visit(context, table->ids[n]);
}

uint64_t cetta_tuple_table_bytes(const CettaTupleTable *table) {
    if (!table)
        return 0u;
    uint64_t bytes = (uint64_t)table->cap * sizeof(*table->ids) +
        (table->slots ? (uint64_t)(table->slot_mask + 1u) *
                            (sizeof(uint32_t) + 1u) : 0u) +
        (uint64_t)table->tuple_cap * table->arity * sizeof(uint32_t) +
        (table->hash ? (uint64_t)(table->hash_mask + 1u) *
                           (sizeof(uint32_t) + 1u) : 0u);
    for (uint32_t c = 0u; c < table->arity; c++)
        bytes += 2u * sizeof(uint32_t) *
            ((uint64_t)table->side[c].number_cap + table->side[c].tuple_cap);
    return bytes;
}
