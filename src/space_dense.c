#include "space_dense.h"

#include <stdlib.h>
#include <string.h>

void cetta_dense_relation_init(CettaDenseRelation *rel) {
    if (rel)
        memset(rel, 0, sizeof(*rel));
}

static void dense_row_release(void *items, uint32_t len);

/* Sparse descriptors keep complete canonical numbers. Rotations and erasure
 * transplant nodes; a borrowed descriptor keeps naming its original row. */
typedef struct CettaDenseRowNode {
    void *items;
    struct CettaDenseRowNode *left, *right;
    uint32_t length, number, subtree_count, height;
} CettaDenseRowNode;

_Static_assert(sizeof(CettaDenseRowNode) == 40u,
               "adaptive directory cost assumes forty-byte descriptors");

enum { DENSE_DIRECTORY_BINS = 32u, DENSE_DIRECTORY_MIN_PREFIX = 64u };

static uint32_t dense_tree_height(const CettaDenseRowNode *node) {
    return node ? node->height : 0u;
}

static uint32_t dense_tree_count(const CettaDenseRowNode *node) {
    return node ? node->subtree_count : 0u;
}

static uint32_t dense_tree_count_below(const CettaDenseRowNode *node,
                                        uint32_t cut) {
    uint32_t count = 0u;
    while (node) {
        if (node->number < cut) {
            count += dense_tree_count(node->left) + 1u;
            node = node->right;
        } else {
            node = node->left;
        }
    }
    return count;
}

static void dense_tree_update(CettaDenseRowNode *node) {
    uint32_t left = dense_tree_height(node->left);
    uint32_t right = dense_tree_height(node->right);
    node->height = (left > right ? left : right) + 1u;
    node->subtree_count = dense_tree_count(node->left) +
                          dense_tree_count(node->right) + 1u;
}

static CettaDenseRowNode *dense_tree_left(CettaDenseRowNode *root) {
    CettaDenseRowNode *top = root->right;
    root->right = top->left;
    top->left = root;
    dense_tree_update(root);
    dense_tree_update(top);
    return top;
}

static CettaDenseRowNode *dense_tree_right(CettaDenseRowNode *root) {
    CettaDenseRowNode *top = root->left;
    root->left = top->right;
    top->right = root;
    dense_tree_update(root);
    dense_tree_update(top);
    return top;
}

static CettaDenseRowNode *dense_tree_balance(CettaDenseRowNode *root) {
    dense_tree_update(root);
    uint32_t left = dense_tree_height(root->left);
    uint32_t right = dense_tree_height(root->right);
    if (left > right + 1u) {
        if (dense_tree_height(root->left->right) >
            dense_tree_height(root->left->left))
            root->left = dense_tree_left(root->left);
        return dense_tree_right(root);
    }
    if (right > left + 1u) {
        if (dense_tree_height(root->right->left) >
            dense_tree_height(root->right->right))
            root->right = dense_tree_right(root->right);
        return dense_tree_left(root);
    }
    return root;
}

static CettaDenseRowNode *dense_tree_find(CettaDenseRowNode *root,
                                          uint32_t number) {
    while (root && root->number != number)
        root = number < root->number ? root->left : root->right;
    return root;
}

static CettaDenseRowNode *dense_tree_insert(CettaDenseRowNode *root,
                                            CettaDenseRowNode *node) {
    if (!root)
        return node;
    if (node->number < root->number)
        root->left = dense_tree_insert(root->left, node);
    else
        root->right = dense_tree_insert(root->right, node);
    return dense_tree_balance(root);
}

static CettaDenseRowNode *dense_tree_take_first(CettaDenseRowNode *root,
                                                CettaDenseRowNode **first) {
    if (!root->left) {
        *first = root;
        return root->right;
    }
    root->left = dense_tree_take_first(root->left, first);
    return dense_tree_balance(root);
}

static CettaDenseRowNode *dense_tree_erase(CettaDenseRowNode *root,
                                           uint32_t number,
                                           CettaDenseRowNode **removed) {
    if (!root)
        return NULL;
    if (number < root->number)
        root->left = dense_tree_erase(root->left, number, removed);
    else if (number > root->number)
        root->right = dense_tree_erase(root->right, number, removed);
    else {
        *removed = root;
        if (!root->left)
            return root->right;
        if (!root->right)
            return root->left;
        CettaDenseRowNode *next;
        CettaDenseRowNode *right = dense_tree_take_first(root->right, &next);
        next->left = root->left;
        next->right = right;
        root = next;
    }
    return dense_tree_balance(root);
}

/* Join ordered disjoint trees around PIVOT, even with unequal heights. */
static CettaDenseRowNode *dense_tree_join(CettaDenseRowNode *left,
                                          CettaDenseRowNode *pivot,
                                          CettaDenseRowNode *right) {
    if (dense_tree_height(left) > dense_tree_height(right) + 1u) {
        left->right = dense_tree_join(left->right, pivot, right);
        return dense_tree_balance(left);
    }
    if (dense_tree_height(right) > dense_tree_height(left) + 1u) {
        right->left = dense_tree_join(left, pivot, right->left);
        return dense_tree_balance(right);
    }
    pivot->left = left;
    pivot->right = right;
    dense_tree_update(pivot);
    return pivot;
}

static CettaDenseRowNode *dense_tree_concat(CettaDenseRowNode *left,
                                            CettaDenseRowNode *right) {
    if (!left)
        return right;
    if (!right)
        return left;
    CettaDenseRowNode *pivot;
    right = dense_tree_take_first(right, &pivot);
    return dense_tree_join(left, pivot, right);
}

static void dense_tree_split(CettaDenseRowNode *root, uint32_t cut,
                               CettaDenseRowNode **below,
                               CettaDenseRowNode **above) {
    if (!root) {
        *below = *above = NULL;
    } else if (root->number < cut) {
        CettaDenseRowNode *middle;
        dense_tree_split(root->right, cut, &middle, above);
        *below = dense_tree_join(root->left, root, middle);
    } else {
        CettaDenseRowNode *middle;
        dense_tree_split(root->left, cut, below, &middle);
        *above = dense_tree_join(middle, root, root->right);
    }
}

/* Headers alone: leaf ownership moves between directory forms unchanged. */
static void dense_tree_free(CettaDenseRowNode *root) {
    if (!root)
        return;
    dense_tree_free(root->left);
    dense_tree_free(root->right);
    free(root);
}

static void dense_tree_free_chain(CettaDenseRowNode *node) {
    while (node) {
        CettaDenseRowNode *next = node->right;
        free(node);
        node = next;
    }
}

static CettaDenseRowNode *dense_tree_from_chain(CettaDenseRowNode **next,
                                                uint32_t count) {
    if (!count)
        return NULL;
    CettaDenseRowNode *left = dense_tree_from_chain(next, count / 2u);
    CettaDenseRowNode *root = *next;
    *next = root->right;
    root->left = left;
    root->right = dense_tree_from_chain(next, count - count / 2u - 1u);
    dense_tree_update(root);
    return root;
}

/* A row edit borrows descriptor slots for one operation. Adaptive changes
 * occur only after that operation has finished using every such reference. */
typedef struct {
    void **items;
    uint32_t *length;
} DenseRowRef;

static inline DenseRowRef dense_side_row_ref(const CettaDenseSide *side,
                                             uint32_t number) {
    if (number < side->row_cap)
        return (DenseRowRef){&side->rows[number], &side->row_len[number]};
    CettaDenseRowNode *node = dense_tree_find(side->overflow, number);
    return node ? (DenseRowRef){&node->items, &node->length}
                : (DenseRowRef){0};
}

/* Readers resolve the descriptor once and borrow its immutable leaf data
 * for this operation. Edits keep the separate descriptor-slot interface. */
typedef struct {
    void *items;
    uint32_t length;
} DenseRowView;

static inline DenseRowView dense_side_row_view(const CettaDenseSide *side,
                                               uint32_t number) {
    if (__builtin_expect(number < side->row_cap, 1))
        return (DenseRowView){side->rows[number], side->row_len[number]};
    CettaDenseRowNode *node = dense_tree_find(side->overflow, number);
    return node ? (DenseRowView){node->items, node->length} : (DenseRowView){0};
}

static inline uint64_t dense_side_scan_span(const CettaDenseSide *side) {
    return (uint64_t)side->row_cap + dense_tree_count(side->overflow);
}

static inline bool dense_side_has_row(const CettaDenseSide *side,
                                       uint32_t number) {
    DenseRowRef row = dense_side_row_ref(side, number);
    return row.items && *row.items;
}

typedef struct {
    const CettaDenseSide *side;
    uint32_t next, end;
    CettaDenseRowNode *pending;
    CettaDenseRowNode *stack[64];
    uint32_t depth;
} DenseSideIterator;

static void dense_side_iterator_init(DenseSideIterator *iterator,
                                       const CettaDenseSide *side) {
    iterator->side = side;
    iterator->next = 0u;
    iterator->end = UINT32_MAX;
    iterator->pending = side->overflow;
    iterator->depth = 0u;
    /* The traversal writes each stack slot before its first pop. */
}

static void dense_side_iterator_below_init(DenseSideIterator *iterator,
                                             const CettaDenseSide *side,
                                             uint32_t limit) {
    dense_side_iterator_init(iterator, side);
    iterator->end = limit;
}

/* Nonempty rows in canonical order; the key bound is independent of cost. */
static inline bool dense_side_next(DenseSideIterator *iterator, uint32_t *number,
                                    DenseRowRef *row) {
    while (iterator->next < iterator->side->row_cap &&
           iterator->next < iterator->end) {
        uint32_t current = iterator->next++;
        if (!iterator->side->rows[current])
            continue;
        if (number)
            *number = current;
        if (row)
            *row = (DenseRowRef){&iterator->side->rows[current],
                                 &iterator->side->row_len[current]};
        return true;
    }
    while (iterator->pending || iterator->depth) {
        while (iterator->pending) {
            if (iterator->depth == 64u)
                abort();
            iterator->stack[iterator->depth++] = iterator->pending;
            iterator->pending = iterator->pending->left;
        }
        CettaDenseRowNode *node = iterator->stack[--iterator->depth];
        iterator->pending = node->right;
        if (node->number >= iterator->end)
            return false;
        if (!node->items)
            continue;
        if (number)
            *number = node->number;
        if (row)
            *row = (DenseRowRef){&node->items, &node->length};
        return true;
    }
    return false;
}

static bool dense_rows_reserve(CettaDenseSide *side, uint32_t number) {
    if (number < side->row_cap || dense_tree_find(side->overflow, number))
        return true;
    CettaDenseRowNode *node = calloc(1u, sizeof(*node));
    if (!node)
        return false;
    node->number = number;
    node->height = node->subtree_count = 1u;
    side->overflow = dense_tree_insert(side->overflow, node);
    return true;
}

static void dense_side_forget_empty(CettaDenseSide *side, uint32_t number) {
    if (number < side->row_cap)
        return;
    CettaDenseRowNode *node = dense_tree_find(side->overflow, number);
    if (node && !node->items) {
        side->overflow = dense_tree_erase(side->overflow, number, &node);
        free(node);
    }
}

static void dense_side_free_directory(CettaDenseSide *side) {
    free(side->rows);
    free(side->row_len);
    free(side->row_bins);
    dense_tree_free(side->overflow);
    memset(side, 0, sizeof(*side));
}

static uint64_t dense_side_control_bytes(const CettaDenseSide *side) {
    return (uint64_t)side->row_cap *
               (sizeof(*side->rows) + sizeof(*side->row_len)) +
           (uint64_t)dense_tree_count(side->overflow) * sizeof(CettaDenseRowNode);
}

static uint64_t dense_side_directory_bytes(const CettaDenseSide *side) {
    return dense_side_control_bytes(side) +
           (side->row_bins ? DENSE_DIRECTORY_BINS * sizeof(*side->row_bins) : 0u);
}

static uint32_t dense_directory_bin(uint32_t number) {
    return number < DENSE_DIRECTORY_MIN_PREFIX ? 0u
        : 26u - (uint32_t)__builtin_clz(number);
}

static void dense_side_born(CettaDenseSide *side, uint32_t number) {
    side->row_count++;
    side->directory_events++;
    if (side->row_bins)
        side->row_bins[dense_directory_bin(number)]++;
}

static void dense_side_died(CettaDenseSide *side, uint32_t number) {
    if (side->row_bins)
        side->row_bins[dense_directory_bin(number)]--;
    side->row_count--;
    side->directory_events++;
    if (number < side->row_cap) {
        side->rows[number] = NULL;
        side->row_len[number] = 0u;
    } else {
        CettaDenseRowNode *node = NULL;
        side->overflow = dense_tree_erase(side->overflow, number, &node);
        free(node);
    }
    if (!side->row_count)
        dense_side_free_directory(side);
}

/* Move only descriptors. All allocations precede the no-fail tree commit. */
static bool dense_side_change_cut(CettaDenseSide *side, uint32_t cut) {
    if (cut == side->row_cap)
        return true;
    void **rows = cut ? calloc(cut, sizeof(*rows)) : NULL;
    uint32_t *lengths = cut ? calloc(cut, sizeof(*lengths)) : NULL;
    if (cut && (!rows || !lengths)) {
        free(rows);
        free(lengths);
        return false;
    }
    if (cut > side->row_cap) {
        if (side->row_cap) {
            memcpy(rows, side->rows, side->row_cap * sizeof(*rows));
            memcpy(lengths, side->row_len, side->row_cap * sizeof(*lengths));
        }
        DenseSideIterator iterator;
        dense_side_iterator_below_init(&iterator, side, cut);
        iterator.next = side->row_cap;
        uint32_t number;
        DenseRowRef row;
        while (dense_side_next(&iterator, &number, &row)) {
            if (number < side->row_cap)
                continue;
            rows[number] = *row.items;
            lengths[number] = *row.length;
        }
        CettaDenseRowNode *moved, *remaining;
        dense_tree_split(side->overflow, cut, &moved, &remaining);
        side->overflow = remaining;
        dense_tree_free(moved);
    } else {
        CettaDenseRowNode *chain = NULL, **tail = &chain;
        uint32_t count = 0u;
        for (uint32_t number = cut; number < side->row_cap; number++) {
            if (!side->rows[number])
                continue;
            CettaDenseRowNode *node = malloc(sizeof(*node));
            if (!node) {
                dense_tree_free_chain(chain);
                free(rows);
                free(lengths);
                return false;
            }
            *node = (CettaDenseRowNode){.items = side->rows[number],
                .length = side->row_len[number], .number = number,
                .height = 1u, .subtree_count = 1u};
            *tail = node;
            tail = &node->right;
            count++;
        }
        if (cut) {
            memcpy(rows, side->rows, cut * sizeof(*rows));
            memcpy(lengths, side->row_len, cut * sizeof(*lengths));
        }
        CettaDenseRowNode *next = chain;
        CettaDenseRowNode *moved = dense_tree_from_chain(&next, count);
        side->overflow = dense_tree_concat(moved, side->overflow);
    }
    free(side->rows);
    free(side->row_len);
    side->rows = rows;
    side->row_len = lengths;
    side->row_cap = cut;
    return true;
}

static void dense_directory_best(const uint32_t *bins, uint32_t count,
                                  uint32_t *best_cut, uint64_t *best_cost) {
    *best_cut = 0u;
    *best_cost = (uint64_t)count * sizeof(CettaDenseRowNode);
    uint32_t inside = 0u, bin = 0u;
    for (uint32_t cut = DENSE_DIRECTORY_MIN_PREFIX;; cut *= 2u, bin++) {
        inside += bins[bin];
        uint64_t cost = (uint64_t)cut * (sizeof(void *) + sizeof(uint32_t)) +
                        (uint64_t)(count - inside) * sizeof(CettaDenseRowNode);
        if (cost <= *best_cost) {
            *best_cost = cost;
            *best_cut = cut;
        }
        if (cut == (UINT32_C(1) << 31))
            break;
    }
}

/* Each row event changes every candidate's cost by at most forty bytes.
 * Decisions keep current<=2*best; at most best/160 events between them keep
 * current<=3*every candidate (AdaptiveRowDirectoryCost.scheduled_bound).
 * A failed allocation leaves the complete old representation charged. */
static __attribute__((noinline)) bool dense_side_maintain_due(CettaDenseSide *side) {
    if (!side->row_count)
        return true;
    if (side->row_count < 20u && side->row_cap == 0u) {
        side->directory_base = (uint64_t)side->row_count * sizeof(CettaDenseRowNode);
        side->directory_events = 0u;
        side->directory_maintenance_failed = false;
        return true;
    }
    if (!side->row_bins) {
        uint32_t *bins = calloc(DENSE_DIRECTORY_BINS, sizeof(*bins));
        if (!bins)
            goto failed;
        DenseSideIterator iterator;
        dense_side_iterator_init(&iterator, side);
        uint32_t number;
        while (dense_side_next(&iterator, &number, NULL))
            bins[dense_directory_bin(number)]++;
        side->row_bins = bins;
    } else if (!side->directory_maintenance_failed &&
               side->directory_events <= side->directory_base / 160u) {
        return true;
    }
    uint32_t cut;
    uint64_t best;
    dense_directory_best(side->row_bins, side->row_count, &cut, &best);
    if (dense_side_control_bytes(side) > 2u * best) {
        if (!dense_side_change_cut(side, cut))
            goto failed;
    } else if (side->overflow && side->row_cap < (UINT32_C(1) << 31)) {
        /* The due event budget also pays for a geometric prefix growth.
         * Admit it by exact bytes, without pulling distant rows into it. */
        uint32_t growth = side->row_cap ? side->row_cap * 2u
            : DENSE_DIRECTORY_MIN_PREFIX;
        uint32_t absorbed = dense_tree_count_below(side->overflow, growth);
        uint64_t cost = (uint64_t)growth *
                           (sizeof(*side->rows) + sizeof(*side->row_len)) +
                       (uint64_t)(dense_tree_count(side->overflow) - absorbed) *
                           sizeof(CettaDenseRowNode);
        if (absorbed && cost <= 2u * best &&
            !dense_side_change_cut(side, growth))
            goto failed;
    }
    side->directory_base = best;
    side->directory_events = 0u;
    side->directory_maintenance_failed = false;
    return true;
failed:
    side->directory_maintenance_failed = true;
    return false;
}
/* Keep an ordinary row event out of the iterator-sized slow frame. The
 * multiplication is exact: directory_events is only thirty-two bits. */
static inline __attribute__((always_inline)) bool dense_side_maintain(
    CettaDenseSide *side) {
    if (side->row_bins && !side->directory_maintenance_failed &&
        (uint64_t)side->directory_events * 160u <= side->directory_base)
        return true;
    return dense_side_maintain_due(side);
}

/* A complete private side has no published iterator to invalidate. Choose
 * its exact minimum before exposing it, rather than carrying construction
 * hysteresis into later point reads. Failure remains the caller's draft. */
static bool dense_side_minimize_private(CettaDenseSide *side) {
    if (!dense_side_maintain(side))
        return false;
    uint32_t cut = 0u;
    uint64_t best = (uint64_t)side->row_count * sizeof(CettaDenseRowNode);
    if (side->row_bins)
        dense_directory_best(side->row_bins, side->row_count, &cut, &best);
    if (!dense_side_change_cut(side, cut)) {
        side->directory_maintenance_failed = true;
        return false;
    }
    side->directory_base = best;
    side->directory_events = 0u;
    return true;
}

static void dense_side_free(CettaDenseSide *side) {
    if (!side->overflow) {
        for (uint32_t n = 0u; n < side->row_cap; n++)
            if (side->rows[n])
                dense_row_release(side->rows[n], side->row_len[n]);
        dense_side_free_directory(side);
        return;
    }
    DenseSideIterator iterator;
    dense_side_iterator_init(&iterator, side);
    DenseRowRef row;
    while (dense_side_next(&iterator, NULL, &row))
        dense_row_release(*row.items, *row.length);
    dense_side_free_directory(side);
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
    /* Keep the absent key's empty bucket unless growing replaces the table. */
    uint32_t place = 0u;
    if (rel->slots) {
        place = dense_hash(id) & rel->slot_mask;
        while (rel->slots[place]) {
            uint32_t number = rel->slots[place] - 1u;
            if (rel->ids[number] == id)
                return number;
            place = (place + 1u) & rel->slot_mask;
        }
    }
    if (!rel->slots || (uint64_t)(rel->len + 1u) * 4u >
                          (uint64_t)(rel->slot_mask + 1u) * 3u) {
        if (!dense_slots_grow(rel))
            return UINT32_MAX;
        place = dense_hash(id) & rel->slot_mask;
        while (rel->slots[place])
            place = (place + 1u) & rel->slot_mask;
    }
    if (rel->len == rel->cap) {
        uint32_t cap = rel->cap ? rel->cap * 2u : 64u;
        if (cap <= rel->cap)
            return UINT32_MAX;
        /* Publish both capacities together; a failed second allocation
         * must not leave a larger ID allocation hidden by the old cap. */
        CettaDenseId *ids = malloc(cap * sizeof(*ids));
        uint32_t *holds = malloc(cap * sizeof(*holds));
        if (!ids || !holds) {
            free(ids);
            free(holds);
            return UINT32_MAX;
        }
        if (rel->len) {
            memcpy(ids, rel->ids, rel->len * sizeof(*ids));
            memcpy(holds, rel->holds, rel->len * sizeof(*holds));
        }
        free(rel->ids);
        free(rel->holds);
        rel->ids = ids;
        rel->holds = holds;
        rel->cap = cap;
    }
    uint32_t number = rel->len++;
    rel->ids[number] = id;
    /* Dead until a pair holds it. */
    rel->holds[number] = 0u;
    rel->dead++;
    rel->slots[place] = number + 1u;
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    return (*descriptor.length & DENSE_ROW_LEAVES) != 0u;
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    if (!dense_row_listed(side, n))
        return dense_len_bytes(*descriptor.length);
    const DenseLeaves *list = *descriptor.items;
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

static inline __attribute__((always_inline)) uint64_t dense_row_count(
    const CettaDenseSide *side, uint32_t n) {
    DenseRowView descriptor = dense_side_row_view(side, n);
    if (!descriptor.items)
        return 0u;
    if (descriptor.length & DENSE_ROW_LEAVES)
        return ((const DenseLeaves *)descriptor.items)->count;
    return dense_len_count(descriptor.length);
}

/* Row N's numbers in order into OUT, room for its count. */
static void dense_row_numbers(const CettaDenseSide *side, uint32_t n,
                              uint32_t *out) {
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    if (!dense_row_listed(side, n)) {
        (void)dense_leaf_numbers(*descriptor.items, *descriptor.length, 0u, out);
        return;
    }
    const DenseLeaves *list = *descriptor.items;
    for (uint32_t i = 0u; i < list->len; i++)
        out += dense_leaf_numbers(list->leaf[i].items, list->leaf[i].len,
                                  dense_chunk_base(list->leaf[i].lo), out);
}

/* Row N takes the prepared leaves LEAVES[0, count) of NUMBERS numbers: in
 * place when one can stand there (an array, or bits of chunk 0), else in a
 * list.  False without memory, the leaves still the caller's. */
static bool dense_row_install(CettaDenseSide *side, uint32_t n,
                              const DenseLeaf *leaves, uint32_t count,
                              uint64_t numbers) {
    bool existed = dense_side_has_row(side, n);
    if (!dense_rows_reserve(side, n))
        return false;
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    if (count == 1u &&
        (!dense_len_bits(leaves[0].len) || leaves[0].lo == 0u)) {
        *descriptor.items = leaves[0].items;
        *descriptor.length = leaves[0].len;
        side->row_bytes += dense_len_bytes(leaves[0].len);
        if (!existed)
            dense_side_born(side, n);
        return true;
    }
    DenseLeaves *list = malloc(dense_list_bytes(count));
    if (!list) {
        dense_side_forget_empty(side, n);
        return false;
    }
    list->count = numbers;
    list->len = list->cap = count;
    memcpy(list->leaf, leaves, count * sizeof(*leaves));
    *descriptor.items = list;
    *descriptor.length = DENSE_ROW_LEAVES;
    side->row_bytes += dense_list_bytes(count);
    for (uint32_t i = 0u; i < count; i++)
        side->row_bytes += dense_len_bytes(leaves[i].len);
    if (!existed)
        dense_side_born(side, n);
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
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
    void *old = *descriptor.items;
    uint32_t old_len = *descriptor.length;
    uint64_t old_bytes = dense_row_bytes(side, n);
    bool ok = dense_row_build(side, n, numbers, count);
    free(numbers);
    if (!ok)
        return false;
    side->row_bytes -= old_bytes;
    dense_row_release(old, old_len);
    return true;
}

/* Free row N, holding nothing or held by a number that died. */
static void dense_row_free(CettaDenseSide *side, uint32_t n) {
    if (!dense_side_has_row(side, n))
        return;
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    side->row_bytes -= dense_row_bytes(side, n);
    dense_row_release(*descriptor.items, *descriptor.length);
    dense_side_died(side, n);
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    DenseLeaves *list = *descriptor.items;
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
        *descriptor.items = list = grown;
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
            *descriptor.items = fewer;
        }
    }
    return true;
}

/* Leaf I of row N's list made anew from its numbers, with M among them
 * unless it is DENSE_NONE: the leaves made take its place.  False without
 * memory, the row as it was. */
static bool dense_leaf_rebuild(CettaDenseSide *side, uint32_t n, uint32_t i,
                               uint32_t m) {
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    DenseLeaves *list = *descriptor.items;
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    DenseLeaves *list = *descriptor.items;
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    DenseLeaves *list = *descriptor.items;
    if (list->len != 1u ||
        (dense_len_bits(list->leaf[0].len) && list->leaf[0].lo != 0u))
        return;
    *descriptor.items = list->leaf[0].items;
    *descriptor.length = list->leaf[0].len;
    side->row_bytes -= dense_list_bytes(list->cap);
    free(list);
}

/* Put M in row N's list: as dense_row_put. */
static int dense_list_put(CettaDenseSide *side, uint32_t n, uint32_t m) {
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    DenseLeaves *list = *descriptor.items;
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
            ((DenseLeaves *)*descriptor.items)->count++;
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
        ((DenseLeaves *)*descriptor.items)->count++;
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
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    DenseLeaves *list = *descriptor.items;
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
            !dense_len_bits(((DenseLeaves *)*descriptor.items)->leaf[i].len))
            dense_list_merge(side, n, i);
    } else if (count < DENSE_LEAF_MAX / 4u) {
        dense_list_merge(side, n, i);
    }
    dense_list_settle(side, n);
    return true;
}

/* ── A row's operations ── */

static bool dense_row_has(const CettaDenseSide *side, uint32_t n, uint32_t m) {
    DenseRowView descriptor = dense_side_row_view(side, n);
    if (!descriptor.items)
        return false;
    if (!(descriptor.length & DENSE_ROW_LEAVES))
        return (!dense_len_bits(descriptor.length) ||
                dense_chunk(m) == 0u) &&
               dense_leaf_has(descriptor.items, descriptor.length, 0u, m);
    const DenseLeaves *list = descriptor.items;
    uint32_t k = dense_chunk(m), i = dense_list_find(list, k);
    return i < list->len && list->leaf[i].lo <= k &&
           dense_leaf_has(list->leaf[i].items, list->leaf[i].len,
                          dense_chunk_base(list->leaf[i].lo), m);
}

/* Put M in row N: 1 when it is new, 0 when the row held it, -1 without
 * room (the row as it was). */
static __attribute__((noinline)) int dense_row_put_slow(CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (!dense_rows_reserve(side, n))
        return -1;
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    if (!*descriptor.items) {
        uint32_t *items = malloc(2u * sizeof(*items));
        if (!items) {
            dense_side_forget_empty(side, n);
            return -1;
        }
        items[0] = m;
        *descriptor.items = items;
        *descriptor.length = dense_array_len(1u, 2u);
        side->row_bytes += 2u * sizeof(*items);
        dense_side_born(side, n);
        return 1;
    }
    if (*descriptor.length & DENSE_ROW_LEAVES)
        return dense_list_put(side, n, m);
    uint32_t len = *descriptor.length;
    if (dense_len_bits(len)) {
        if (m / 64u < dense_len_words(len)) {
            uint64_t *words = *descriptor.items;
            uint64_t bit = UINT64_C(1) << (m & 63u);
            if (words[m / 64u] & bit)
                return 0;
            words[m / 64u] |= bit;
            *descriptor.length = dense_bits_len(dense_len_words(len),
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
            list->leaf[0] = (DenseLeaf){0u, 1u, len, *descriptor.items};
            list->leaf[1] = (DenseLeaf){dense_chunk(m), dense_chunk(m) + 1u,
                                        dense_array_len(1u, 2u), one};
            *descriptor.items = list;
            *descriptor.length = DENSE_ROW_LEAVES;
            side->row_bytes += dense_list_bytes(2u) + 2u * sizeof(*one);
            return 1;
        }
        int put = dense_bits_put(descriptor.items, descriptor.length, 0u, m,
                                 &side->row_bytes);
        if (put != 2)
            return put;
        return dense_row_rebuild(side, n, m) ? 1 : -1;
    }
    uint32_t count = dense_len_count(len);
    uint32_t place = dense_array_lower(*descriptor.items, 0u, count, m);
    if (place < count && ((const uint32_t *)*descriptor.items)[place] == m)
        return 0;
    if (count == DENSE_LEAF_MAX ||
        dense_array_chunk_dense(*descriptor.items, count, place, m))
        return dense_row_rebuild(side, n, m) ? 1 : -1;
    return dense_array_insert(descriptor.items, descriptor.length, place, m,
                              &side->row_bytes) ? 1 : -1;
}
static inline __attribute__((always_inline)) int dense_row_put(
    CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (__builtin_expect(n < side->row_cap && !side->rows[n], 1)) {
        uint32_t *items = malloc(2u * sizeof(*items));
        if (!items)
            return -1;
        items[0] = m;
        side->rows[n] = items;
        side->row_len[n] = dense_array_len(1u, 2u);
        side->row_bytes += 2u * sizeof(*items);
        dense_side_born(side, n);
        return 1;
    }
    return dense_row_put_slow(side, n, m);
}

/* Take M from row N: whether the row held it.  As in a list, bits grown
 * sparse are made anew, and an emptied row is freed. */
static bool dense_row_take(CettaDenseSide *side, uint32_t n, uint32_t m) {
    if (!dense_side_has_row(side, n))
        return false;
    DenseRowRef descriptor = dense_side_row_ref(side, n);
    if (dense_row_listed(side, n))
        return dense_list_take(side, n, m);
    uint32_t len = *descriptor.length;
    if (dense_len_bits(len)) {
        if (dense_chunk(m) != 0u ||
            !dense_bits_take(*descriptor.items, descriptor.length, 0u, m))
            return false;
        if (dense_len_count(*descriptor.length) == 0u)
            dense_row_free(side, n);
        else if (dense_bits_sparse(*descriptor.length))
            (void)dense_row_rebuild(side, n, DENSE_NONE);
        return true;
    }
    uint32_t count = dense_len_count(len);
    uint32_t place = dense_array_lower(*descriptor.items, 0u, count, m);
    if (place == count || ((const uint32_t *)*descriptor.items)[place] != m)
        return false;
    if (count == 1u)
        dense_row_free(side, n);
    else
        dense_array_remove(descriptor.items, descriptor.length, place,
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
    DenseRowView descriptor = dense_side_row_view(side, n);
    if (!descriptor.items)
        return true;
    if (!(descriptor.length & DENSE_ROW_LEAVES))
        return dense_each_in_leaf(rel, descriptor.items, descriptor.length, 0u, n,
                                  second, visit, context);
    const DenseLeaves *list = descriptor.items;
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
    DenseSideIterator iterator;
    dense_side_iterator_init(&iterator, &rel->first);
    uint32_t na;
    while (dense_side_next(&iterator, &na, NULL)) {
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
            if (dense_row_put(&rel->second, numbers[i], na) < 0 ||
                !dense_side_maintain(&rel->second))
                goto drop;
        }
    }
    if (!dense_side_minimize_private(&rel->second))
        goto drop;
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
    uint64_t scanned = dense_side_scan_span(&rel->first);
    kept->second_rent = scanned > UINT64_MAX - kept->second_rent
        ? UINT64_MAX : kept->second_rent + scanned;
    uint64_t price = scanned > UINT64_MAX - rel->pairs
        ? UINT64_MAX : rel->pairs + scanned;
    if (kept->second_rent >= price)
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
    (void)dense_side_maintain(&rel->first);
    if (rel->second_kept && !dense_side_maintain(&rel->second))
        dense_second_drop(rel);
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
    (void)dense_side_maintain(&rel->first);
    if (rel->second_kept && !dense_side_maintain(&rel->second))
        dense_second_drop(rel);
    return true;
}

/* A side's rows made anew under the numbers MAP gives (order kept), as
 * the side FRESH, whose rows are empty.  False without memory, FRESH's
 * rows freed. */
static bool dense_side_renumbered(const CettaDenseSide *side,
                                  const uint32_t *map, uint32_t numbered,
                                  CettaDenseSide *fresh) {
    uint32_t bins[DENSE_DIRECTORY_BINS] = {0};
    uint32_t rows = 0u, cut;
    uint64_t best;
    DenseSideIterator census;
    dense_side_iterator_below_init(&census, side, numbered);
    uint32_t number;
    while (dense_side_next(&census, &number, NULL)) {
        if (map[number] == UINT32_MAX)
            return false;
        bins[dense_directory_bin(map[number])]++;
        rows++;
    }
    dense_directory_best(bins, rows, &cut, &best);
    if (!dense_side_change_cut(fresh, cut))
        return false;
    if (rows >= 20u) {
        fresh->row_bins = calloc(DENSE_DIRECTORY_BINS, sizeof(*fresh->row_bins));
        if (!fresh->row_bins) {
            dense_side_free(fresh);
            return false;
        }
    }
    uint32_t *numbers = NULL;
    uint64_t room = 0u;
    DenseSideIterator iterator;
    dense_side_iterator_below_init(&iterator, side, numbered);
    uint32_t n;
    while (dense_side_next(&iterator, &n, NULL)) {
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
    fresh->directory_base = best;
    fresh->directory_events = 0u;
    return true;
fail:
    free(numbers);
    dense_side_free(fresh);
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
    if (!map || !ids || !holds || !slots) {
        free(map);
        free(ids);
        free(holds);
        free(slots);
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
    CettaDenseSide first = {0}, second = {0};
    if (!dense_side_renumbered(&rel->first, map, rel->len, &first)) {
        free(map);
        free(ids);
        free(holds);
        free(slots);
        dense_side_free(&first);
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
        dense_side_free(&second);
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
        DenseSideIterator iterator;
        dense_side_iterator_init(&iterator, &rel->first);
        uint32_t na;
        while (dense_side_next(&iterator, &na, NULL)) {
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
    DenseSideIterator iterator;
    dense_side_iterator_init(&iterator, &rel->first);
    uint32_t na;
    while (dense_side_next(&iterator, &na, NULL)) {
        if (dense_row_has(&rel->first, na, nb) &&
            !visit(context, rel->ids[na], b_fixed))
            return false;
    }
    dense_second_rent(rel);
    return true;
}

/* Keep the traversal stack out of direct first/inverse point counts. */
static __attribute__((noinline)) uint64_t dense_count_second_unkept(
    const CettaDenseRelation *rel, uint32_t nb) {
    uint64_t count = 0u;
    DenseSideIterator iterator;
    dense_side_iterator_init(&iterator, &rel->first);
    uint32_t na;
    while (dense_side_next(&iterator, &na, NULL))
        count += dense_row_has(&rel->first, na, nb);
    dense_second_rent(rel);
    return count;
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
    return dense_count_second_unkept(rel, nb);
}

void cetta_dense_relation_each_numbered(const CettaDenseRelation *rel,
                                        CettaDenseIdVisit visit,
                                        void *context) {
    if (!rel || !visit)
        return;
    /* A callback may change liveness, so recheck the all-live case per ID. */
    for (uint32_t n = 0u; n < rel->len; n++)
        if (!rel->dead || rel->holds[n])
            visit(context, rel->ids[n]);
}

static uint64_t dense_side_bytes(const CettaDenseSide *side) {
    return dense_side_directory_bytes(side) + side->row_bytes;
}

uint64_t cetta_dense_relation_bytes(const CettaDenseRelation *rel) {
    if (!rel)
        return 0u;
    return (uint64_t)rel->cap * (sizeof(*rel->ids) + sizeof(*rel->holds)) +
           (rel->slots ? (uint64_t)(rel->slot_mask + 1u) * sizeof(*rel->slots)
                       : 0u) +
           dense_side_bytes(&rel->first) + dense_side_bytes(&rel->second);
}
