#include "space_dense.h"

#include <stdlib.h>
#include <string.h>

void cetta_dense_relation_init(CettaDenseRelation *rel) {
    if (rel)
        memset(rel, 0, sizeof(*rel));
}

static void dense_row_release(void *items, uint32_t len);

static void dense_side_free(CettaDenseSide *side) {
    for (uint32_t i = 0u; i < side->row_cap; i++) {
        if (side->rows[i])
            dense_row_release(side->rows[i], side->row_len[i]);
    }
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
    free(rel->holds);
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
        uint32_t *holds = realloc(rel->holds, cap * sizeof(*holds));
        if (!holds)
            return UINT32_MAX;
        rel->holds = holds;
        rel->cap = cap;
    }
    number = rel->len++;
    rel->ids[number] = id;
    /* Dead until a pair holds it. */
    rel->holds[number] = 0u;
    rel->dead++;
    uint32_t i = dense_hash(id) & rel->slot_mask;
    while (rel->slots[i] != 0u)
        i = (i + 1u) & rel->slot_mask;
    rel->slots[i] = number + 1u;
    return number;
}

/* Rows of a side.
 *
 * A row holds the numbers of the other column paired with its number.
 * Numbers fall in chunks of 2^16, and a row keeps them in leaves ordered by
 * chunk, each chunk's numbers in one leaf: a sorted array of at most 4,096
 * numbers from a run of whole chunks, or the numbers of one chunk as bits
 * from the chunk's start, made while they take fewer bytes so than as an
 * array.  A row of one leaf keeps it in place (rows[n] its numbers and
 * row_len[n] its form and size: an array of any chunks, or bits of chunk
 * 0); a longer row keeps the list of its leaves, marked DENSE_ROW_LEAVES.
 *
 * An array keeps between a quarter and all of its capacity; bits grow only
 * while the byte rule holds and are made anew once they take four times the
 * bytes of their numbers as an array; an emptied leaf goes, and so does an
 * emptied row.  So every leaf takes at most 16 bytes a number it holds, a
 * number far from the others takes a leaf of its own rather than bits up
 * to it, and an add or a removal reads at most one leaf, or two that
 * merge, and moves the list (DenseRowCost). */

#define DENSE_ROW_LEAVES UINT32_C(0x40000000)
/* No number: rows are numbered below 2^31. */
#define DENSE_NONE UINT32_MAX

enum {
    DENSE_CHUNK_SHIFT = 16,
    /* An array leaf's numbers at most, and the most two neighbouring
     * arrays hold when they merge. */
    DENSE_LEAF_MAX = 4096,
    DENSE_LEAF_MERGE = 2048,
};

/* A leaf's form and size, as row_len keeps an inline row's: an array's
 * count (bits 0-15) and capacity, a power of two (its log in bits 16-20);
 * or, with CETTA_DENSE_ROW_BITS, bits' words (bits 0-10) and count (bits
 * 11-27). */
static inline bool dense_len_bits(uint32_t len) {
    return (len & CETTA_DENSE_ROW_BITS) != 0u;
}

static inline uint32_t dense_len_count(uint32_t len) {
    return dense_len_bits(len) ? (len >> 11) & 0x1ffffu : len & 0xffffu;
}

static inline uint32_t dense_len_words(uint32_t len) {
    return len & 0x7ffu;
}

static inline uint32_t dense_len_capacity(uint32_t len) {
    return UINT32_C(1) << ((len >> 16) & 31u);
}

static inline uint32_t dense_array_len(uint32_t count, uint32_t capacity) {
    return count | (uint32_t)__builtin_ctz(capacity) << 16;
}

static inline uint32_t dense_bits_len(uint32_t words, uint32_t count) {
    return CETTA_DENSE_ROW_BITS | words | count << 11;
}

static inline uint64_t dense_len_bytes(uint32_t len) {
    return dense_len_bits(len)
        ? (uint64_t)dense_len_words(len) * sizeof(uint64_t)
        : (uint64_t)dense_len_capacity(len) * sizeof(uint32_t);
}

/* A leaf of a longer row: the chunks [lo, hi) whose numbers it may hold
 * (bits: one), its form and size, and its numbers. */
typedef struct {
    uint32_t lo, hi;
    uint32_t len;
    void *items;
} DenseLeaf;

typedef struct {
    uint64_t count;
    uint32_t len, cap;
    DenseLeaf leaf[];
} DenseLeaves;

static inline uint64_t dense_list_bytes(uint32_t cap) {
    return sizeof(DenseLeaves) + (uint64_t)cap * sizeof(DenseLeaf);
}

static inline uint32_t dense_chunk(uint32_t m) {
    return m >> DENSE_CHUNK_SHIFT;
}

static inline uint32_t dense_chunk_base(uint32_t k) {
    return k << DENSE_CHUNK_SHIFT;
}

static inline bool dense_row_listed(const CettaDenseSide *side, uint32_t n) {
    return (side->row_len[n] & DENSE_ROW_LEAVES) != 0u;
}

/* The byte rule: whether COUNT numbers of a chunk, the largest LARGEST past
 * the chunk's start, take fewer bytes as bits up to it than as an array. */
static inline bool dense_prefer_bits(uint32_t count, uint32_t largest) {
    return (uint64_t)count * sizeof(uint32_t) >
           (uint64_t)(largest / 64u + 1u) * sizeof(uint64_t);
}

/* Bits grown sparse: four times the bytes of their numbers as an array. */
static inline bool dense_bits_sparse(uint32_t len) {
    return (uint64_t)dense_len_words(len) * sizeof(uint64_t) >
           4u * (uint64_t)dense_len_count(len) * sizeof(uint32_t);
}

/* Words of bits covering offset M: from 1, doubled. */
static uint32_t dense_bits_words(uint32_t m) {
    uint32_t need = m / 64u + 1u, words = 1u;
    while (words < need)
        words *= 2u;
    return words;
}

/* An array's capacity for COUNT numbers: a power of two, from 2. */
static inline uint32_t dense_array_capacity(uint32_t count) {
    uint32_t capacity = 2u;
    while (capacity < count)
        capacity *= 2u;
    return capacity;
}

/* The first place in ITEMS[from, to), sorted, holding M or more. */
static uint32_t dense_array_lower(const uint32_t *items, uint32_t from,
                                  uint32_t to, uint32_t m) {
    while (from < to) {
        uint32_t middle = from + (to - from) / 2u;
        if (items[middle] < m)
            from = middle + 1u;
        else
            to = middle;
    }
    return from;
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

/* ── Leaves ── */

/* Whether the leaf ITEMS of form LEN holds M; bits start at BASE, and M
 * lies in their chunk. */
static bool dense_leaf_has(const void *items, uint32_t len, uint32_t base,
                           uint32_t m) {
    if (dense_len_bits(len)) {
        const uint64_t *words = items;
        uint32_t off = m - base;
        return off / 64u < dense_len_words(len) &&
               ((words[off / 64u] >> (off & 63u)) & 1u);
    }
    uint32_t count = dense_len_count(len);
    uint32_t place = dense_array_lower(items, 0u, count, m);
    return place < count && ((const uint32_t *)items)[place] == m;
}

/* The leaf's numbers in order into OUT; their count. */
static uint32_t dense_leaf_numbers(const void *items, uint32_t len,
                                   uint32_t base, uint32_t *out) {
    if (!dense_len_bits(len)) {
        uint32_t count = dense_len_count(len);
        memcpy(out, items, count * sizeof(*out));
        return count;
    }
    const uint64_t *words = items;
    uint32_t count = 0u;
    for (uint32_t w = 0u; w < dense_len_words(len); w++) {
        uint64_t bits = words[w];
        while (bits) {
            out[count++] = base + w * 64u + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1u;
        }
    }
    return count;
}

/* Whether M's chunk, M put at PLACE of the sorted ITEMS[0, count), would
 * take fewer bytes as bits. */
static bool dense_array_chunk_dense(const uint32_t *items, uint32_t count,
                                    uint32_t place, uint32_t m) {
    uint32_t k = dense_chunk(m), base = dense_chunk_base(k);
    /* The chunk holds at most COUNT + 1 of the numbers, the largest at
     * least M: the byte rule needs that much first. */
    if (!dense_prefer_bits(count + 1u, m - base))
        return false;
    uint32_t first = dense_array_lower(items, 0u, place, base);
    uint32_t last = (uint64_t)base + (UINT64_C(1) << DENSE_CHUNK_SHIFT) >
                            UINT32_MAX
        ? count
        : dense_array_lower(items, place, count,
                            base + (UINT32_C(1) << DENSE_CHUNK_SHIFT));
    uint32_t largest = (last > place ? items[last - 1u] : m) - base;
    return dense_prefer_bits(last - first + 1u, largest);
}

/* Put M at PLACE of the array at *ITEMS of form *LEN, doubling it when
 * full; the bytes it grows by to *BYTES. */
static bool dense_array_insert(void **items, uint32_t *len, uint32_t place,
                               uint32_t m, uint64_t *bytes) {
    uint32_t *numbers = *items;
    uint32_t count = dense_len_count(*len);
    uint32_t capacity = dense_len_capacity(*len);
    if (count == capacity) {
        numbers = realloc(numbers, 2u * capacity * sizeof(*numbers));
        if (!numbers)
            return false;
        *bytes += (uint64_t)capacity * sizeof(*numbers);
        *items = numbers;
        capacity *= 2u;
    }
    memmove(numbers + place + 1u, numbers + place,
            (count - place) * sizeof(*numbers));
    numbers[place] = m;
    *len = dense_array_len(count + 1u, capacity);
    return true;
}

/* Take the number at PLACE of the array at *ITEMS of form *LEN, which holds
 * another; halve its capacity once that is four times its count. */
static void dense_array_remove(void **items, uint32_t *len, uint32_t place,
                               uint64_t *bytes) {
    uint32_t *numbers = *items;
    uint32_t count = dense_len_count(*len) - 1u;
    uint32_t capacity = dense_len_capacity(*len);
    memmove(numbers + place, numbers + place + 1u,
            (count - place) * sizeof(*numbers));
    if (capacity > 2u && count * 4u <= capacity) {
        uint32_t *fewer = realloc(numbers, capacity / 2u * sizeof(*numbers));
        if (fewer) {
            *bytes -= (uint64_t)capacity / 2u * sizeof(*numbers);
            *items = fewer;
            capacity /= 2u;
        }
    }
    *len = dense_array_len(count, capacity);
}

/* Put M, past BASE in the bits' chunk, in the bits at *ITEMS of form *LEN:
 * 1 when new, 0 when held, -1 without memory, 2 when growing the bits to M
 * would break the byte rule (nothing changes then). */
static int dense_bits_put(void **items, uint32_t *len, uint32_t base,
                          uint32_t m, uint64_t *bytes) {
    uint64_t *words = *items;
    uint32_t size = dense_len_words(*len), count = dense_len_count(*len);
    uint32_t off = m - base;
    uint64_t bit = UINT64_C(1) << (off & 63u);
    if (off / 64u < size) {
        if (words[off / 64u] & bit)
            return 0;
    } else {
        if (!dense_prefer_bits(count + 1u, off))
            return 2;
        uint32_t grown = dense_bits_words(off);
        words = realloc(words, grown * sizeof(*words));
        if (!words)
            return -1;
        memset(words + size, 0, (grown - size) * sizeof(*words));
        *bytes += (uint64_t)(grown - size) * sizeof(*words);
        *items = words;
        size = grown;
    }
    words[off / 64u] |= bit;
    *len = dense_bits_len(size, count + 1u);
    return 1;
}

/* Take M, past BASE in the bits' chunk, from the bits ITEMS of form *LEN:
 * whether they held it. */
static bool dense_bits_take(void *items, uint32_t *len, uint32_t base,
                            uint32_t m) {
    uint64_t *words = items;
    uint32_t off = m - base;
    if (off / 64u >= dense_len_words(*len))
        return false;
    uint64_t bit = UINT64_C(1) << (off & 63u);
    if (!(words[off / 64u] & bit))
        return false;
    words[off / 64u] &= ~bit;
    *len = dense_bits_len(dense_len_words(*len), dense_len_count(*len) - 1u);
    return true;
}

static bool dense_leaf_array(DenseLeaf *leaf, const uint32_t *numbers,
                             uint32_t count) {
    uint32_t capacity = dense_array_capacity(count);
    uint32_t *items = malloc(capacity * sizeof(*items));
    if (!items)
        return false;
    memcpy(items, numbers, count * sizeof(*items));
    *leaf = (DenseLeaf){dense_chunk(numbers[0]),
                        dense_chunk(numbers[count - 1u]) + 1u,
                        dense_array_len(count, capacity), items};
    return true;
}

static bool dense_leaf_bits(DenseLeaf *leaf, const uint32_t *numbers,
                            uint32_t count) {
    uint32_t k = dense_chunk(numbers[0]), base = dense_chunk_base(k);
    uint32_t size = dense_bits_words(numbers[count - 1u] - base);
    uint64_t *words = calloc(size, sizeof(*words));
    if (!words)
        return false;
    for (uint32_t i = 0u; i < count; i++) {
        uint32_t off = numbers[i] - base;
        words[off / 64u] |= UINT64_C(1) << (off & 63u);
    }
    *leaf = (DenseLeaf){k, k + 1u, dense_bits_len(size, count), words};
    return true;
}

/* The leaves of the sorted numbers NUMBERS[0, count), count > 0: a chunk's
 * numbers as bits when the byte rule says so, the others in arrays of whole
 * chunks, at most DENSE_LEAF_MAX numbers each (a chunk the byte rule leaves
 * an array holds at most 2,048).  Into OUT with their storage when OUT is
 * not NULL; how many either way, or 0 without memory, nothing kept. */
static uint32_t dense_leaves_make(const uint32_t *numbers, uint32_t count,
                                  DenseLeaf *out) {
    uint32_t made = 0u, open = DENSE_NONE;
    for (uint32_t i = 0u;;) {
        bool end = i == count, bits = false;
        uint32_t j = i;
        if (!end) {
            uint32_t k = dense_chunk(numbers[i]);
            while (j < count && dense_chunk(numbers[j]) == k)
                j++;
            bits = dense_prefer_bits(j - i,
                                     numbers[j - 1u] - dense_chunk_base(k));
        }
        if (open != DENSE_NONE && (end || bits || j - open > DENSE_LEAF_MAX)) {
            if (out && !dense_leaf_array(&out[made], numbers + open, i - open))
                goto fail;
            made++;
            open = DENSE_NONE;
        }
        if (end)
            return made;
        if (bits) {
            if (out && !dense_leaf_bits(&out[made], numbers + i, j - i))
                goto fail;
            made++;
        } else if (open == DENSE_NONE) {
            open = i;
        }
        i = j;
    }
fail:
    for (uint32_t l = 0u; l < made; l++)
        free(out[l].items);
    return 0u;
}

/* The leaves of NUMBERS[0, count), allocated; NULL without memory. */
static DenseLeaf *dense_leaves_new(const uint32_t *numbers, uint32_t count,
                                   uint32_t *made) {
    *made = dense_leaves_make(numbers, count, NULL);
    DenseLeaf *leaves = malloc(*made * sizeof(*leaves));
    if (leaves && dense_leaves_make(numbers, count, leaves) == *made)
        return leaves;
    free(leaves);
    return NULL;
}

/* ── Rows ── */

static uint64_t dense_row_bytes(const CettaDenseSide *side, uint32_t n) {
    if (!dense_row_listed(side, n))
        return dense_len_bytes(side->row_len[n]);
    const DenseLeaves *list = side->rows[n];
    uint64_t bytes = dense_list_bytes(list->cap);
    for (uint32_t i = 0u; i < list->len; i++)
        bytes += dense_len_bytes(list->leaf[i].len);
    return bytes;
}

static void dense_row_release(void *items, uint32_t len) {
    if (len & DENSE_ROW_LEAVES) {
        DenseLeaves *list = items;
        for (uint32_t i = 0u; i < list->len; i++)
            free(list->leaf[i].items);
    }
    free(items);
}

static uint64_t dense_row_count(const CettaDenseSide *side, uint32_t n) {
    if (n >= side->row_cap || !side->rows[n])
        return 0u;
    if (dense_row_listed(side, n))
        return ((const DenseLeaves *)side->rows[n])->count;
    return dense_len_count(side->row_len[n]);
}

/* Row N's numbers in order into OUT, room for its count. */
static void dense_row_numbers(const CettaDenseSide *side, uint32_t n,
                              uint32_t *out) {
    if (!dense_row_listed(side, n)) {
        (void)dense_leaf_numbers(side->rows[n], side->row_len[n], 0u, out);
        return;
    }
    const DenseLeaves *list = side->rows[n];
    for (uint32_t i = 0u; i < list->len; i++)
        out += dense_leaf_numbers(list->leaf[i].items, list->leaf[i].len,
                                  dense_chunk_base(list->leaf[i].lo), out);
}

/* Row N, empty, takes the leaves LEAVES[0, count) of NUMBERS numbers: in
 * place when one can stand there (an array, or bits of chunk 0), else in a
 * list.  False without memory, the leaves still the caller's. */
static bool dense_row_install(CettaDenseSide *side, uint32_t n,
                              const DenseLeaf *leaves, uint32_t count,
                              uint64_t numbers) {
    if (count == 1u &&
        (!dense_len_bits(leaves[0].len) || leaves[0].lo == 0u)) {
        side->rows[n] = leaves[0].items;
        side->row_len[n] = leaves[0].len;
        side->row_bytes += dense_len_bytes(leaves[0].len);
        return true;
    }
    DenseLeaves *list = malloc(dense_list_bytes(count));
    if (!list)
        return false;
    list->count = numbers;
    list->len = list->cap = count;
    memcpy(list->leaf, leaves, count * sizeof(*leaves));
    side->rows[n] = list;
    side->row_len[n] = DENSE_ROW_LEAVES;
    side->row_bytes += dense_list_bytes(count);
    for (uint32_t i = 0u; i < count; i++)
        side->row_bytes += dense_len_bytes(leaves[i].len);
    return true;
}

/* Row N, empty, made of the sorted numbers NUMBERS[0, count), count > 0. */
static bool dense_row_build(CettaDenseSide *side, uint32_t n,
                            const uint32_t *numbers, uint32_t count) {
    uint32_t made = 0u;
    DenseLeaf *leaves = dense_leaves_new(numbers, count, &made);
    if (!leaves)
        return false;
    bool ok = dense_row_install(side, n, leaves, made, count);
    if (!ok) {
        for (uint32_t l = 0u; l < made; l++)
            free(leaves[l].items);
    }
    free(leaves);
    return ok;
}

/* Row N made anew from its numbers, with M put among them unless it is
 * DENSE_NONE.  False without memory, the row as it was. */
static bool dense_row_rebuild(CettaDenseSide *side, uint32_t n, uint32_t m) {
    uint32_t count = (uint32_t)dense_row_count(side, n);
    uint32_t *numbers = malloc(((size_t)count + 1u) * sizeof(*numbers));
    if (!numbers)
        return false;
    dense_row_numbers(side, n, numbers);
    if (m != DENSE_NONE) {
        uint32_t place = dense_array_lower(numbers, 0u, count, m);
        memmove(numbers + place + 1u, numbers + place,
                (count - place) * sizeof(*numbers));
        numbers[place] = m;
        count++;
    }
    void *old = side->rows[n];
    uint32_t old_len = side->row_len[n];
    uint64_t old_bytes = dense_row_bytes(side, n);
    side->rows[n] = NULL;
    side->row_len[n] = 0u;
    bool ok = dense_row_build(side, n, numbers, count);
    free(numbers);
    if (!ok) {
        side->rows[n] = old;
        side->row_len[n] = old_len;
        return false;
    }
    side->row_bytes -= old_bytes;
    dense_row_release(old, old_len);
    return true;
}

/* Free row N, holding nothing or held by a number that died. */
static void dense_row_free(CettaDenseSide *side, uint32_t n) {
    if (n >= side->row_cap || !side->rows[n])
        return;
    side->row_bytes -= dense_row_bytes(side, n);
    dense_row_release(side->rows[n], side->row_len[n]);
    side->rows[n] = NULL;
    side->row_len[n] = 0u;
}

/* ── Lists ── */

/* The first leaf of LIST whose chunks end past K. */
static uint32_t dense_list_find(const DenseLeaves *list, uint32_t k) {
    uint32_t low = 0u, high = list->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (list->leaf[middle].hi <= k)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

/* DROP leaves of row N's list from I replaced by WITH[0, count): false
 * without memory when the list must grow, the list as it was.  A list four
 * times longer than its leaves is halved. */
static bool dense_list_splice(CettaDenseSide *side, uint32_t n, uint32_t i,
                              uint32_t drop, const DenseLeaf *with,
                              uint32_t count) {
    DenseLeaves *list = side->rows[n];
    uint32_t len = list->len - drop + count;
    if (len > list->cap) {
        uint32_t cap = list->cap * 2u;
        while (cap < len)
            cap *= 2u;
        DenseLeaves *grown = realloc(list, dense_list_bytes(cap));
        if (!grown)
            return false;
        side->row_bytes += dense_list_bytes(cap) - dense_list_bytes(grown->cap);
        grown->cap = cap;
        side->rows[n] = list = grown;
    }
    memmove(&list->leaf[i + count], &list->leaf[i + drop],
            (list->len - i - drop) * sizeof(*list->leaf));
    if (count)
        memcpy(&list->leaf[i], with, count * sizeof(*with));
    list->len = len;
    if (list->cap > 4u && len * 4u <= list->cap) {
        DenseLeaves *fewer = realloc(list, dense_list_bytes(list->cap / 2u));
        if (fewer) {
            side->row_bytes -= dense_list_bytes(fewer->cap) -
                               dense_list_bytes(fewer->cap / 2u);
            fewer->cap /= 2u;
            side->rows[n] = fewer;
        }
    }
    return true;
}

/* Leaf I of row N's list made anew from its numbers, with M among them
 * unless it is DENSE_NONE: the leaves made take its place.  False without
 * memory, the row as it was. */
static bool dense_leaf_rebuild(CettaDenseSide *side, uint32_t n, uint32_t i,
                               uint32_t m) {
    DenseLeaves *list = side->rows[n];
    DenseLeaf old = list->leaf[i];
    uint32_t count = dense_len_count(old.len);
    uint32_t *numbers = malloc(((size_t)count + 1u) * sizeof(*numbers));
    if (!numbers)
        return false;
    (void)dense_leaf_numbers(old.items, old.len, dense_chunk_base(old.lo),
                             numbers);
    if (m != DENSE_NONE) {
        uint32_t place = dense_array_lower(numbers, 0u, count, m);
        memmove(numbers + place + 1u, numbers + place,
                (count - place) * sizeof(*numbers));
        numbers[place] = m;
        count++;
    }
    uint32_t made = 0u;
    DenseLeaf *leaves = dense_leaves_new(numbers, count, &made);
    free(numbers);
    if (!leaves)
        return false;
    if (!dense_list_splice(side, n, i, 1u, leaves, made)) {
        for (uint32_t l = 0u; l < made; l++)
            free(leaves[l].items);
        free(leaves);
        return false;
    }
    side->row_bytes -= dense_len_bytes(old.len);
    for (uint32_t l = 0u; l < made; l++)
        side->row_bytes += dense_len_bytes(leaves[l].len);
    free(old.items);
    free(leaves);
    return true;
}

/* Leaf I of row N's list, an array, merged with its neighbouring array of
 * fewer numbers when together they hold at most DENSE_LEAF_MERGE. */
static void dense_list_merge(CettaDenseSide *side, uint32_t n, uint32_t i) {
    DenseLeaves *list = side->rows[n];
    uint32_t count = dense_len_count(list->leaf[i].len);
    uint32_t with = DENSE_NONE, fewest = DENSE_NONE;
    for (uint32_t j = i ? i - 1u : i + 1u; j <= i + 1u && j < list->len;
         j += 2u) {
        uint32_t other = list->leaf[j].len;
        if (!dense_len_bits(other) &&
            count + dense_len_count(other) <= DENSE_LEAF_MERGE &&
            dense_len_count(other) < fewest) {
            fewest = dense_len_count(other);
            with = j;
        }
    }
    if (with == DENSE_NONE)
        return;
    uint32_t a = with < i ? with : i;
    const DenseLeaf *left = &list->leaf[a], *right = &list->leaf[a + 1u];
    uint32_t lc = dense_len_count(left->len), rc = dense_len_count(right->len);
    uint32_t capacity = dense_array_capacity(lc + rc);
    uint32_t *items = malloc(capacity * sizeof(*items));
    if (!items)
        return;
    memcpy(items, left->items, lc * sizeof(*items));
    memcpy(items + lc, right->items, rc * sizeof(*items));
    DenseLeaf merged = {left->lo, right->hi,
                        dense_array_len(lc + rc, capacity), items};
    side->row_bytes -= dense_len_bytes(left->len) + dense_len_bytes(right->len);
    side->row_bytes += dense_len_bytes(merged.len);
    free(left->items);
    free(right->items);
    (void)dense_list_splice(side, n, a, 2u, &merged, 1u);
}

/* Row N's list, left with one leaf that can stand in place, moves there. */
static void dense_list_settle(CettaDenseSide *side, uint32_t n) {
    DenseLeaves *list = side->rows[n];
    if (list->len != 1u ||
        (dense_len_bits(list->leaf[0].len) && list->leaf[0].lo != 0u))
        return;
    side->rows[n] = list->leaf[0].items;
    side->row_len[n] = list->leaf[0].len;
    side->row_bytes -= dense_list_bytes(list->cap);
    free(list);
}

/* Put M in row N's list: as dense_row_put. */
static int dense_list_put(CettaDenseSide *side, uint32_t n, uint32_t m) {
    DenseLeaves *list = side->rows[n];
    uint32_t k = dense_chunk(m);
    uint32_t i = dense_list_find(list, k);
    if (i == list->len || list->leaf[i].lo > k) {
        /* No leaf holds M's chunk: a neighbouring array with room takes
         * it, else a leaf of its own. */
        if (i > 0u && !dense_len_bits(list->leaf[i - 1u].len) &&
            dense_len_count(list->leaf[i - 1u].len) < DENSE_LEAF_MAX) {
            list->leaf[--i].hi = k + 1u;
        } else if (i < list->len && !dense_len_bits(list->leaf[i].len) &&
                   dense_len_count(list->leaf[i].len) < DENSE_LEAF_MAX) {
            list->leaf[i].lo = k;
        } else {
            uint32_t *one = malloc(2u * sizeof(*one));
            if (!one)
                return -1;
            one[0] = m;
            DenseLeaf leaf = {k, k + 1u, dense_array_len(1u, 2u), one};
            if (!dense_list_splice(side, n, i, 0u, &leaf, 1u)) {
                free(one);
                return -1;
            }
            side->row_bytes += 2u * sizeof(*one);
            ((DenseLeaves *)side->rows[n])->count++;
            return 1;
        }
    }
    DenseLeaf *leaf = &list->leaf[i];
    int put;
    bool rebuilt = false;
    if (dense_len_bits(leaf->len)) {
        put = dense_bits_put(&leaf->items, &leaf->len,
                             dense_chunk_base(leaf->lo), m, &side->row_bytes);
        if (put == 2) {
            put = dense_leaf_rebuild(side, n, i, m) ? 1 : -1;
            rebuilt = true;
        }
    } else {
        uint32_t count = dense_len_count(leaf->len);
        uint32_t place = dense_array_lower(leaf->items, 0u, count, m);
        if (place < count && ((const uint32_t *)leaf->items)[place] == m) {
            put = 0;
        } else if (count == DENSE_LEAF_MAX ||
                   dense_array_chunk_dense(leaf->items, count, place, m)) {
            put = dense_leaf_rebuild(side, n, i, m) ? 1 : -1;
            rebuilt = true;
        } else {
            put = dense_array_insert(&leaf->items, &leaf->len, place, m,
                                     &side->row_bytes) ? 1 : -1;
        }
    }
    if (put == 1)
        ((DenseLeaves *)side->rows[n])->count++;
    if (rebuilt && put == 1)
        dense_list_settle(side, n);
    return put;
}

/* Take M from row N's list: as dense_row_take.  An emptied leaf goes, bits
 * grown sparse are made anew, an array of fewer than a quarter of
 * DENSE_LEAF_MAX merges with a neighbouring array, a list left with one
 * leaf that can stand in place moves there, and an emptied row is freed.
 * Each step only as memory allows: the row holds the same numbers
 * either way. */
static bool dense_list_take(CettaDenseSide *side, uint32_t n, uint32_t m) {
    DenseLeaves *list = side->rows[n];
    uint32_t k = dense_chunk(m);
    uint32_t i = dense_list_find(list, k);
    if (i == list->len || list->leaf[i].lo > k)
        return false;
    DenseLeaf *leaf = &list->leaf[i];
    if (dense_len_bits(leaf->len)) {
        if (!dense_bits_take(leaf->items, &leaf->len,
                             dense_chunk_base(leaf->lo), m))
            return false;
    } else {
        uint32_t count = dense_len_count(leaf->len);
        uint32_t place = dense_array_lower(leaf->items, 0u, count, m);
        if (place == count || ((const uint32_t *)leaf->items)[place] != m)
            return false;
        if (count > 1u)
            dense_array_remove(&leaf->items, &leaf->len, place,
                               &side->row_bytes);
        else
            leaf->len = dense_array_len(0u, dense_len_capacity(leaf->len));
    }
    if (--list->count == 0u) {
        dense_row_free(side, n);
        return true;
    }
    uint32_t count = dense_len_count(leaf->len);
    if (count == 0u) {
        side->row_bytes -= dense_len_bytes(leaf->len);
        free(leaf->items);
        (void)dense_list_splice(side, n, i, 1u, NULL, 0u);
    } else if (dense_len_bits(leaf->len)) {
        if (dense_bits_sparse(leaf->len) &&
            dense_leaf_rebuild(side, n, i, DENSE_NONE) &&
            !dense_len_bits(((DenseLeaves *)side->rows[n])->leaf[i].len))
            dense_list_merge(side, n, i);
    } else if (count < DENSE_LEAF_MAX / 4u) {
        dense_list_merge(side, n, i);
    }
    dense_list_settle(side, n);
    return true;
}

/* ── A row's operations ── */

static bool dense_row_has(const CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (n >= side->row_cap || !side->rows[n])
        return false;
    if (!dense_row_listed(side, n))
        return (!dense_len_bits(side->row_len[n]) ||
                dense_chunk(m) == 0u) &&
               dense_leaf_has(side->rows[n], side->row_len[n], 0u, m);
    const DenseLeaves *list = side->rows[n];
    uint32_t k = dense_chunk(m), i = dense_list_find(list, k);
    return i < list->len && list->leaf[i].lo <= k &&
           dense_leaf_has(list->leaf[i].items, list->leaf[i].len,
                          dense_chunk_base(list->leaf[i].lo), m);
}

/* Put M in row N: 1 when it is new, 0 when the row held it, -1 without
 * room (the row as it was). */
static int dense_row_put(CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (!dense_rows_reserve(side, n))
        return -1;
    if (!side->rows[n]) {
        uint32_t *items = malloc(2u * sizeof(*items));
        if (!items)
            return -1;
        items[0] = m;
        side->rows[n] = items;
        side->row_len[n] = dense_array_len(1u, 2u);
        side->row_bytes += 2u * sizeof(*items);
        return 1;
    }
    if (dense_row_listed(side, n))
        return dense_list_put(side, n, m);
    uint32_t len = side->row_len[n];
    if (dense_len_bits(len)) {
        if (m / 64u < dense_len_words(len)) {
            uint64_t *words = side->rows[n];
            uint64_t bit = UINT64_C(1) << (m & 63u);
            if (words[m / 64u] & bit)
                return 0;
            words[m / 64u] |= bit;
            side->row_len[n] = dense_bits_len(dense_len_words(len),
                                              dense_len_count(len) + 1u);
            return 1;
        }
        if (dense_chunk(m) != 0u) {
            /* Bits of chunk 0 become the first leaf of a list; M takes a
             * leaf of its own. */
            uint32_t *one = malloc(2u * sizeof(*one));
            DenseLeaves *list = malloc(dense_list_bytes(2u));
            if (!one || !list) {
                free(one);
                free(list);
                return -1;
            }
            one[0] = m;
            list->count = dense_len_count(len) + 1u;
            list->len = list->cap = 2u;
            list->leaf[0] = (DenseLeaf){0u, 1u, len, side->rows[n]};
            list->leaf[1] = (DenseLeaf){dense_chunk(m), dense_chunk(m) + 1u,
                                        dense_array_len(1u, 2u), one};
            side->rows[n] = list;
            side->row_len[n] = DENSE_ROW_LEAVES;
            side->row_bytes += dense_list_bytes(2u) + 2u * sizeof(*one);
            return 1;
        }
        int put = dense_bits_put(&side->rows[n], &side->row_len[n], 0u, m,
                                 &side->row_bytes);
        if (put != 2)
            return put;
        return dense_row_rebuild(side, n, m) ? 1 : -1;
    }
    uint32_t count = dense_len_count(len);
    uint32_t place = dense_array_lower(side->rows[n], 0u, count, m);
    if (place < count && ((const uint32_t *)side->rows[n])[place] == m)
        return 0;
    if (count == DENSE_LEAF_MAX ||
        dense_array_chunk_dense(side->rows[n], count, place, m))
        return dense_row_rebuild(side, n, m) ? 1 : -1;
    return dense_array_insert(&side->rows[n], &side->row_len[n], place, m,
                              &side->row_bytes) ? 1 : -1;
}

/* Take M from row N: whether the row held it.  As in a list, bits grown
 * sparse are made anew, and an emptied row is freed. */
static bool dense_row_take(CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (n >= side->row_cap || !side->rows[n])
        return false;
    if (dense_row_listed(side, n))
        return dense_list_take(side, n, m);
    uint32_t len = side->row_len[n];
    if (dense_len_bits(len)) {
        if (dense_chunk(m) != 0u ||
            !dense_bits_take(side->rows[n], &side->row_len[n], 0u, m))
            return false;
        if (dense_len_count(side->row_len[n]) == 0u)
            dense_row_free(side, n);
        else if (dense_bits_sparse(side->row_len[n]))
            (void)dense_row_rebuild(side, n, DENSE_NONE);
        return true;
    }
    uint32_t count = dense_len_count(len);
    uint32_t place = dense_array_lower(side->rows[n], 0u, count, m);
    if (place == count || ((const uint32_t *)side->rows[n])[place] != m)
        return false;
    if (count == 1u)
        dense_row_free(side, n);
    else
        dense_array_remove(&side->rows[n], &side->row_len[n], place,
                           &side->row_bytes);
    return true;
}

/* Visit a leaf's numbers M in order as pairs (N, M), or (M, N) when SECOND
 * tells that N numbers the second column. */
static bool dense_each_in_leaf(const CettaDenseRelation *rel,
                               const void *items, uint32_t len, uint32_t base,
                               uint32_t n, bool second,
                               CettaDensePairVisit visit, void *context) {
    if (!dense_len_bits(len)) {
        const uint32_t *numbers = items;
        for (uint32_t i = 0u; i < dense_len_count(len); i++) {
            bool kept = second
                ? visit(context, rel->ids[numbers[i]], rel->ids[n])
                : visit(context, rel->ids[n], rel->ids[numbers[i]]);
            if (!kept)
                return false;
        }
        return true;
    }
    const uint64_t *words = items;
    for (uint32_t w = 0u; w < dense_len_words(len); w++) {
        uint64_t bits = words[w];
        while (bits) {
            uint32_t m = base + w * 64u + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1u;
            bool kept = second ? visit(context, rel->ids[m], rel->ids[n])
                               : visit(context, rel->ids[n], rel->ids[m]);
            if (!kept)
                return false;
        }
    }
    return true;
}

/* Visit row N of SIDE in ascending order of the other column's numbers, as
 * pairs (first, second): SECOND tells that N numbers the second column. */
static bool dense_each_in_row(const CettaDenseRelation *rel,
                              const CettaDenseSide *side, uint32_t n,
                              bool second, CettaDensePairVisit visit,
                              void *context) {
    if (n >= side->row_cap || !side->rows[n])
        return true;
    if (!dense_row_listed(side, n))
        return dense_each_in_leaf(rel, side->rows[n], side->row_len[n], 0u, n,
                                  second, visit, context);
    const DenseLeaves *list = side->rows[n];
    for (uint32_t i = 0u; i < list->len; i++) {
        if (!dense_each_in_leaf(rel, list->leaf[i].items, list->leaf[i].len,
                                dense_chunk_base(list->leaf[i].lo), n, second,
                                visit, context))
            return false;
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
    uint32_t *numbers = NULL;
    uint64_t room = 0u;
    for (uint32_t na = 0u; na < rel->first.row_cap; na++) {
        uint64_t count = dense_row_count(&rel->first, na);
        if (count == 0u)
            continue;
        if (count > room) {
            uint32_t *grown = realloc(numbers, count * sizeof(*numbers));
            if (!grown)
                goto drop;
            numbers = grown;
            room = count;
        }
        dense_row_numbers(&rel->first, na, numbers);
        for (uint64_t i = 0u; i < count; i++) {
            if (dense_row_put(&rel->second, numbers[i], na) < 0)
                goto drop;
        }
    }
    free(numbers);
    rel->second_kept = true;
    return;
drop:
    free(numbers);
    dense_second_drop(rel);
}

/* A probe binding only the second column read every first-column row:
 * its rent, and the second side built once the rents reach its price. */
static void dense_second_rent(const CettaDenseRelation *rel) {
    CettaDenseRelation *kept = (CettaDenseRelation *)rel;
    kept->second_rent += rel->first.row_cap;
    if (kept->second_rent >= rel->pairs + rel->first.row_cap)
        dense_second_build(kept);
}

/* A pair holds number N once more: told HELD when it was dead. */
static void dense_hold(CettaDenseRelation *rel, uint32_t n,
                       CettaDenseIdVisit held, void *context) {
    if (rel->holds[n]++ != 0u)
        return;
    rel->dead--;
    if (held)
        held(context, rel->ids[n]);
}

/* A pair holds number N once less: when none does, N dies, its rows go,
 * and RELEASED is told. */
static void dense_unhold(CettaDenseRelation *rel, uint32_t n,
                         CettaDenseIdVisit released, void *context) {
    if (--rel->holds[n] != 0u)
        return;
    rel->dead++;
    dense_row_free(&rel->first, n);
    dense_row_free(&rel->second, n);
    if (released)
        released(context, rel->ids[n]);
}

bool cetta_dense_relation_add(CettaDenseRelation *rel, CettaDenseId a,
                              CettaDenseId b, bool *oom,
                              CettaDenseIdVisit held, void *context) {
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
    dense_hold(rel, na, held, context);
    dense_hold(rel, nb, held, context);
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
                                 CettaDenseId b, CettaDenseIdVisit released,
                                 void *context) {
    uint32_t na = cetta_dense_relation_number(rel, a);
    uint32_t nb = na == UINT32_MAX ? UINT32_MAX
                                   : cetta_dense_relation_number(rel, b);
    if (nb == UINT32_MAX || !dense_row_take(&rel->first, na, nb))
        return false;
    if (rel->second_kept)
        (void)dense_row_take(&rel->second, nb, na);
    rel->pairs--;
    dense_unhold(rel, na, released, context);
    dense_unhold(rel, nb, released, context);
    return true;
}

/* A side's rows made anew under the numbers MAP gives (order kept), as
 * the side FRESH, whose rows are empty.  False without memory, FRESH's
 * rows freed. */
static bool dense_side_renumbered(const CettaDenseSide *side,
                                  const uint32_t *map, uint32_t numbered,
                                  CettaDenseSide *fresh) {
    uint32_t *numbers = NULL;
    uint64_t room = 0u;
    for (uint32_t n = 0u; n < side->row_cap && n < numbered; n++) {
        uint64_t count = dense_row_count(side, n);
        if (count == 0u)
            continue;
        if (count > room) {
            uint32_t *grown = realloc(numbers, count * sizeof(*numbers));
            if (!grown)
                goto fail;
            numbers = grown;
            room = count;
        }
        dense_row_numbers(side, n, numbers);
        for (uint64_t i = 0u; i < count; i++)
            numbers[i] = map[numbers[i]];
        if (!dense_row_build(fresh, map[n], numbers, (uint32_t)count))
            goto fail;
    }
    free(numbers);
    return true;
fail:
    free(numbers);
    for (uint32_t n = 0u; n < fresh->row_cap; n++) {
        if (fresh->rows[n])
            dense_row_release(fresh->rows[n], fresh->row_len[n]);
    }
    return false;
}

/* Number the held ids anew, in order, dropping the dead, with room for
 * twice as many (RelationNumberingCost).  Every row is made anew before
 * any old one goes: on failure the relation stands as it was, or, when
 * only the second side could not be made, without it. */
static bool dense_renumber(CettaDenseRelation *rel) {
    uint32_t live = rel->len - rel->dead;
    uint64_t want = (uint64_t)live * 2u < 64u ? 64u : (uint64_t)live * 2u;
    uint64_t size = 64u;
    while (want * 4u > size * 3u)
        size *= 2u;
    if (want > UINT32_MAX || size > (UINT64_C(1) << 31))
        return false;
    uint32_t cap = (uint32_t)want;
    uint32_t *map = calloc((size_t)rel->len + 1u, sizeof(*map));
    CettaDenseId *ids = malloc((size_t)cap * sizeof(*ids));
    uint32_t *holds = malloc((size_t)cap * sizeof(*holds));
    uint32_t *slots = calloc((size_t)size, sizeof(*slots));
    void **first_rows = calloc(cap, sizeof(*first_rows));
    uint32_t *first_len = calloc(cap, sizeof(*first_len));
    void **second_rows = rel->second_kept ? calloc(cap, sizeof(*second_rows))
                                          : NULL;
    uint32_t *second_len = rel->second_kept ? calloc(cap, sizeof(*second_len))
                                            : NULL;
    if (!map || !ids || !holds || !slots || !first_rows || !first_len ||
        (rel->second_kept && (!second_rows || !second_len))) {
        free(map);
        free(ids);
        free(holds);
        free(slots);
        free(first_rows);
        free(first_len);
        free(second_rows);
        free(second_len);
        return false;
    }
    uint32_t next = 0u;
    for (uint32_t n = 0u; n < rel->len; n++) {
        if (!rel->holds[n]) {
            map[n] = UINT32_MAX;
            continue;
        }
        ids[next] = rel->ids[n];
        holds[next] = rel->holds[n];
        map[n] = next++;
    }
    CettaDenseSide first = {.rows = first_rows, .row_len = first_len,
                            .row_cap = cap};
    CettaDenseSide second = {.rows = second_rows, .row_len = second_len,
                             .row_cap = cap};
    if (!dense_side_renumbered(&rel->first, map, rel->len, &first)) {
        free(map);
        free(ids);
        free(holds);
        free(slots);
        free(first_rows);
        free(first_len);
        free(second_rows);
        free(second_len);
        return false;
    }
    bool second_made = rel->second_kept &&
        dense_side_renumbered(&rel->second, map, rel->len, &second);
    dense_side_free(&rel->first);
    rel->first = first;
    if (second_made) {
        dense_side_free(&rel->second);
        rel->second = second;
    } else {
        dense_second_drop(rel);
        free(second_rows);
        free(second_len);
    }
    uint32_t mask = (uint32_t)size - 1u;
    for (uint32_t n = 0u; n < next; n++) {
        uint32_t i = dense_hash(ids[n]) & mask;
        while (slots[i] != 0u)
            i = (i + 1u) & mask;
        slots[i] = n + 1u;
    }
    free(map);
    free(rel->ids);
    free(rel->holds);
    free(rel->slots);
    rel->ids = ids;
    rel->holds = holds;
    rel->slots = slots;
    rel->slot_mask = mask;
    rel->len = next;
    rel->cap = cap;
    rel->dead = 0u;
    return true;
}

bool cetta_dense_relation_trim(CettaDenseRelation *rel) {
    if (!rel || (uint64_t)rel->dead <= 4u * rel->pairs + 64u)
        return true;
    return dense_renumber(rel);
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
        if (rel->holds[n])
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
    return (uint64_t)rel->cap * (sizeof(*rel->ids) + sizeof(*rel->holds)) +
           (rel->slots ? (uint64_t)(rel->slot_mask + 1u) * sizeof(*rel->slots)
                       : 0u) +
           dense_side_bytes(&rel->first) + dense_side_bytes(&rel->second);
}
