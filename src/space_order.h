#ifndef CETTA_SPACE_ORDER_H
#define CETTA_SPACE_ORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* An order over the rows of a store: a weight-balanced search tree whose
 * nodes are the row numbers themselves, so a row is found without a search
 * and nothing is allocated for one beyond its slot.  The tree holds the
 * rows given to it and compares them through its caller; rows the caller's
 * order does not separate stand in row order.  Every operation is
 * logarithmic in the rows held, in the worst case; none moves a row.
 *
 * Balance is Adams' as corrected by Hirai and Yamamoto (delta 3, gamma 2):
 * neither subtree of a node holds more than three times the other's rows,
 * two rows or fewer always passing. */

#define CETTA_ORDER_NONE UINT32_MAX

typedef struct {
    uint32_t left, right, parent;
    /* The rows of this node's subtree; 0 for a row the tree does not hold. */
    uint32_t size;
} CettaOrderNode;

typedef struct {
    CettaOrderNode *nodes; /* by row */
    uint32_t cap;
    uint32_t root;
    uint32_t least;
    uint32_t count;
} CettaOrder;

/* Whether row A stands before (negative), with (zero) or after (positive)
 * row B.  False when the order of the two could not be found. */
typedef bool (*CettaOrderCompare)(void *context, uint32_t a, uint32_t b,
                                  int *order);

static inline void cetta_order_init(CettaOrder *o) {
    *o = (CettaOrder){.root = CETTA_ORDER_NONE, .least = CETTA_ORDER_NONE};
}

static inline void cetta_order_free(CettaOrder *o) {
    free(o->nodes);
    cetta_order_init(o);
}

static inline void cetta_order_clear(CettaOrder *o) {
    if (o->nodes && o->cap)
        memset(o->nodes, 0, (size_t)o->cap * sizeof(*o->nodes));
    o->root = CETTA_ORDER_NONE;
    o->least = CETTA_ORDER_NONE;
    o->count = 0u;
}

static inline bool cetta_order_holds(const CettaOrder *o, uint32_t row) {
    return row < o->cap && o->nodes[row].size != 0u;
}

static inline uint32_t cetta_order_size_(const CettaOrder *o, uint32_t n) {
    return n == CETTA_ORDER_NONE ? 0u : o->nodes[n].size;
}

/* Room for rows [0, rows). */
static inline bool cetta_order_reserve(CettaOrder *o, uint64_t rows) {
    if (rows <= o->cap)
        return true;
    if (rows >= CETTA_ORDER_NONE)
        return false;
    uint64_t cap = o->cap ? o->cap : 16u;
    while (cap < rows)
        cap *= 2u;
    if (cap >= CETTA_ORDER_NONE)
        cap = CETTA_ORDER_NONE - 1u;
    CettaOrderNode *nodes =
        realloc(o->nodes, (size_t)cap * sizeof(*nodes));
    if (!nodes)
        return false;
    memset(nodes + o->cap, 0, (size_t)(cap - o->cap) * sizeof(*nodes));
    o->nodes = nodes;
    o->cap = (uint32_t)cap;
    return true;
}

/* Put NEW where OLD hung under PARENT (the root when PARENT is none). */
static inline void cetta_order_hang_(CettaOrder *o, uint32_t parent,
                                     uint32_t old, uint32_t new_child) {
    if (parent == CETTA_ORDER_NONE)
        o->root = new_child;
    else if (o->nodes[parent].left == old)
        o->nodes[parent].left = new_child;
    else
        o->nodes[parent].right = new_child;
    if (new_child != CETTA_ORDER_NONE)
        o->nodes[new_child].parent = parent;
}

static inline void cetta_order_resize_(CettaOrder *o, uint32_t n) {
    o->nodes[n].size = cetta_order_size_(o, o->nodes[n].left) +
        cetta_order_size_(o, o->nodes[n].right) + 1u;
}

/* The right child of X rises over it. */
static inline uint32_t cetta_order_rotate_left_(CettaOrder *o, uint32_t x) {
    uint32_t y = o->nodes[x].right;
    uint32_t inner = o->nodes[y].left;
    cetta_order_hang_(o, o->nodes[x].parent, x, y);
    o->nodes[x].right = inner;
    if (inner != CETTA_ORDER_NONE)
        o->nodes[inner].parent = x;
    o->nodes[y].left = x;
    o->nodes[x].parent = y;
    cetta_order_resize_(o, x);
    cetta_order_resize_(o, y);
    return y;
}

static inline uint32_t cetta_order_rotate_right_(CettaOrder *o, uint32_t x) {
    uint32_t y = o->nodes[x].left;
    uint32_t inner = o->nodes[y].right;
    cetta_order_hang_(o, o->nodes[x].parent, x, y);
    o->nodes[x].left = inner;
    if (inner != CETTA_ORDER_NONE)
        o->nodes[inner].parent = x;
    o->nodes[y].right = x;
    o->nodes[x].parent = y;
    cetta_order_resize_(o, x);
    cetta_order_resize_(o, y);
    return y;
}

/* Recount X and restore its balance; the node now heading its subtree. */
static inline uint32_t cetta_order_balance_(CettaOrder *o, uint32_t x) {
    cetta_order_resize_(o, x);
    uint64_t l = cetta_order_size_(o, o->nodes[x].left);
    uint64_t r = cetta_order_size_(o, o->nodes[x].right);
    if (l + r <= 1u)
        return x;
    if (r > 3u * l) {
        uint32_t y = o->nodes[x].right;
        if ((uint64_t)cetta_order_size_(o, o->nodes[y].left) >=
            2u * (uint64_t)cetta_order_size_(o, o->nodes[y].right))
            (void)cetta_order_rotate_right_(o, y);
        return cetta_order_rotate_left_(o, x);
    }
    if (l > 3u * r) {
        uint32_t y = o->nodes[x].left;
        if ((uint64_t)cetta_order_size_(o, o->nodes[y].right) >=
            2u * (uint64_t)cetta_order_size_(o, o->nodes[y].left))
            (void)cetta_order_rotate_left_(o, y);
        return cetta_order_rotate_right_(o, x);
    }
    return x;
}

static inline void cetta_order_rebalance_up_(CettaOrder *o, uint32_t n) {
    while (n != CETTA_ORDER_NONE)
        n = o->nodes[cetta_order_balance_(o, n)].parent;
}

/* Take ROW into the order.  False, with the order unchanged, when there is
 * no room for it, it is held already, or a comparison could not be made. */
static inline bool cetta_order_insert(CettaOrder *o, uint32_t row,
                                      CettaOrderCompare compare,
                                      void *context) {
    if (!cetta_order_reserve(o, (uint64_t)row + 1u) ||
        o->nodes[row].size != 0u)
        return false;
    uint32_t parent = CETTA_ORDER_NONE;
    uint32_t at = o->root;
    bool leftmost = true;
    bool to_left = false;
    while (at != CETTA_ORDER_NONE) {
        int order = 0;
        if (!compare(context, row, at, &order))
            return false;
        if (order == 0)
            order = row < at ? -1 : 1;
        parent = at;
        to_left = order < 0;
        if (to_left) {
            at = o->nodes[at].left;
        } else {
            at = o->nodes[at].right;
            leftmost = false;
        }
    }
    o->nodes[row] = (CettaOrderNode){
        .left = CETTA_ORDER_NONE, .right = CETTA_ORDER_NONE,
        .parent = parent, .size = 1u};
    if (parent == CETTA_ORDER_NONE)
        o->root = row;
    else if (to_left)
        o->nodes[parent].left = row;
    else
        o->nodes[parent].right = row;
    if (leftmost)
        o->least = row;
    o->count++;
    cetta_order_rebalance_up_(o, parent);
    return true;
}

static inline uint32_t cetta_order_leftmost_(const CettaOrder *o, uint32_t n) {
    while (o->nodes[n].left != CETTA_ORDER_NONE)
        n = o->nodes[n].left;
    return n;
}

static inline uint32_t cetta_order_rightmost_(const CettaOrder *o,
                                              uint32_t n) {
    while (o->nodes[n].right != CETTA_ORDER_NONE)
        n = o->nodes[n].right;
    return n;
}

/* The least row held; none when the order is empty. */
static inline uint32_t cetta_order_least(const CettaOrder *o) {
    return o->least;
}

/* The row after ROW, which the order holds; none after the greatest. */
static inline uint32_t cetta_order_next(const CettaOrder *o, uint32_t row) {
    if (o->nodes[row].right != CETTA_ORDER_NONE)
        return cetta_order_leftmost_(o, o->nodes[row].right);
    uint32_t parent = o->nodes[row].parent;
    while (parent != CETTA_ORDER_NONE && o->nodes[parent].right == row) {
        row = parent;
        parent = o->nodes[row].parent;
    }
    return parent;
}

/* Give up ROW, which the order holds. */
static inline void cetta_order_remove(CettaOrder *o, uint32_t row) {
    if (!cetta_order_holds(o, row))
        return;
    if (o->least == row)
        o->least = cetta_order_next(o, row);
    CettaOrderNode x = o->nodes[row];
    uint32_t start;
    if (x.left == CETTA_ORDER_NONE || x.right == CETTA_ORDER_NONE) {
        uint32_t child = x.left != CETTA_ORDER_NONE ? x.left : x.right;
        cetta_order_hang_(o, x.parent, row, child);
        start = x.parent;
    } else {
        /* The neighbour from the larger side takes its place. */
        bool from_left = cetta_order_size_(o, x.left) >
            cetta_order_size_(o, x.right);
        uint32_t y = from_left ? cetta_order_rightmost_(o, x.left)
                               : cetta_order_leftmost_(o, x.right);
        uint32_t y_parent = o->nodes[y].parent;
        uint32_t y_child = from_left ? o->nodes[y].left : o->nodes[y].right;
        cetta_order_hang_(o, y_parent, y, y_child);
        start = y_parent == row ? y : y_parent;
        /* ROW's links as they stand now that Y has left its subtree. */
        x = o->nodes[row];
        o->nodes[y].left = x.left;
        o->nodes[y].right = x.right;
        if (x.left != CETTA_ORDER_NONE)
            o->nodes[x.left].parent = y;
        if (x.right != CETTA_ORDER_NONE)
            o->nodes[x.right].parent = y;
        cetta_order_hang_(o, x.parent, row, y);
        o->nodes[y].size = x.size;
    }
    o->nodes[row] = (CettaOrderNode){0};
    o->count--;
    cetta_order_rebalance_up_(o, start);
}

/* Whether a row stands before (negative), with (zero) or after (positive) a
 * key the caller holds.  False when the two could not be compared. */
typedef bool (*CettaOrderProbe)(void *context, uint32_t row, int *order);

/* The first row not before the key; none when every row is before it.
 * False when a comparison could not be made. */
static inline bool cetta_order_lower_bound(const CettaOrder *o,
                                           CettaOrderProbe probe,
                                           void *context, uint32_t *row_out) {
    uint32_t found = CETTA_ORDER_NONE;
    uint32_t at = o->root;
    while (at != CETTA_ORDER_NONE) {
        int order = 0;
        if (!probe(context, at, &order))
            return false;
        if (order >= 0) {
            found = at;
            at = o->nodes[at].left;
        } else {
            at = o->nodes[at].right;
        }
    }
    *row_out = found;
    return true;
}

static inline uint32_t cetta_order_rank(const CettaOrder *o, uint32_t row);
static inline uint32_t cetta_order_select(const CettaOrder *o, uint32_t rank);

/* The first row not before the key, sought from ROW, a held row before it:
 * by ranks doubling away from ROW, then halving, so it costs about twice the
 * logarithm of the rows between them in comparisons, however many the order
 * holds; the row after ROW, then the greatest row, are tried first, so the
 * key past ROW's successor, or past every row, costs one or two.  False when
 * a comparison could not be made. */
static inline bool cetta_order_lower_bound_from(const CettaOrder *o,
                                                CettaOrderProbe probe,
                                                void *context, uint32_t row,
                                                uint32_t *row_out) {
    uint32_t next = cetta_order_next(o, row);
    int order = 1;
    if (next != CETTA_ORDER_NONE && !probe(context, next, &order))
        return false;
    if (order >= 0) {
        *row_out = next;
        return true;
    }
    if (!probe(context, cetta_order_rightmost_(o, o->root), &order))
        return false;
    if (order < 0) {
        *row_out = CETTA_ORDER_NONE;
        return true;
    }
    /* The row after ROW is before the key and the greatest row is not: the
     * key stands at an offset from ROW in (1, span - 1]. */
    uint64_t base = cetta_order_rank(o, row);
    uint64_t span = (uint64_t)o->count - base;
    uint64_t before = 1u, after = 2u;
    while (after < span - 1u) {
        if (!probe(context, cetta_order_select(o, (uint32_t)(base + after)),
                   &order))
            return false;
        if (order >= 0)
            break;
        before = after;
        after *= 2u;
    }
    if (after > span - 1u)
        after = span - 1u;
    while (after - before > 1u) {
        uint64_t mid = before + (after - before) / 2u;
        if (!probe(context, cetta_order_select(o, (uint32_t)(base + mid)),
                   &order))
            return false;
        if (order >= 0)
            after = mid;
        else
            before = mid;
    }
    *row_out = cetta_order_select(o, (uint32_t)(base + after));
    return true;
}

/* How many held rows stand before ROW, which the order holds. */
static inline uint32_t cetta_order_rank(const CettaOrder *o, uint32_t row) {
    uint32_t rank = cetta_order_size_(o, o->nodes[row].left);
    uint32_t parent = o->nodes[row].parent;
    while (parent != CETTA_ORDER_NONE) {
        if (o->nodes[parent].right == row)
            rank += cetta_order_size_(o, o->nodes[parent].left) + 1u;
        row = parent;
        parent = o->nodes[row].parent;
    }
    return rank;
}

/* The row with RANK rows before it; none when the order holds no more. */
static inline uint32_t cetta_order_select(const CettaOrder *o,
                                          uint32_t rank) {
    uint32_t at = o->root;
    while (at != CETTA_ORDER_NONE) {
        uint32_t before = cetta_order_size_(o, o->nodes[at].left);
        if (rank < before) {
            at = o->nodes[at].left;
        } else if (rank == before) {
            return at;
        } else {
            rank -= before + 1u;
            at = o->nodes[at].right;
        }
    }
    return CETTA_ORDER_NONE;
}

static inline uint32_t cetta_order_build_(CettaOrder *o, const uint32_t *rows,
                                          uint32_t lo, uint32_t hi,
                                          uint32_t parent) {
    if (lo >= hi)
        return CETTA_ORDER_NONE;
    uint32_t mid = lo + (hi - lo) / 2u;
    uint32_t n = rows[mid];
    o->nodes[n].parent = parent;
    o->nodes[n].size = hi - lo;
    o->nodes[n].left = cetta_order_build_(o, rows, lo, mid, n);
    o->nodes[n].right = cetta_order_build_(o, rows, mid + 1u, hi, n);
    return n;
}

/* Hold exactly ROWS, COUNT distinct rows below LIMIT given in their order.
 * False, leaving the order empty, when there is no room. */
static inline bool cetta_order_build(CettaOrder *o, const uint32_t *rows,
                                     uint32_t count, uint64_t limit) {
    cetta_order_clear(o);
    if (!cetta_order_reserve(o, limit))
        return false;
    o->root = cetta_order_build_(o, rows, 0u, count, CETTA_ORDER_NONE);
    o->least = count ? rows[0] : CETTA_ORDER_NONE;
    o->count = count;
    return true;
}

/* Make DST the order SRC is.  False, leaving DST empty, without room. */
static inline bool cetta_order_copy(CettaOrder *dst, const CettaOrder *src) {
    cetta_order_free(dst);
    if (src->cap) {
        dst->nodes = malloc((size_t)src->cap * sizeof(*dst->nodes));
        if (!dst->nodes)
            return false;
        memcpy(dst->nodes, src->nodes,
               (size_t)src->cap * sizeof(*dst->nodes));
    }
    dst->cap = src->cap;
    dst->root = src->root;
    dst->least = src->least;
    dst->count = src->count;
    return true;
}

/* Renumber the rows held: row R becomes TARGET[R], which keeps their order
 * (a row with TARGET[R] == GONE is given up first).  SOURCE rows are read,
 * LIMIT is one past the greatest new row.  False, leaving the order empty,
 * without room. */
static inline bool cetta_order_transport(CettaOrder *o, const uint64_t *target,
                                         uint64_t source, uint64_t gone,
                                         uint64_t limit) {
    uint32_t reach = source < o->cap ? (uint32_t)source : o->cap;
    for (uint32_t row = 0u; row < reach; row++) {
        if (o->nodes[row].size != 0u && target[row] == gone)
            cetta_order_remove(o, row);
    }
    /* A row past the rows read is not renumbered: the caller reads them all. */
    for (uint32_t row = reach; row < o->cap; row++) {
        if (o->nodes[row].size != 0u)
            cetta_order_remove(o, row);
    }
    if (limit >= CETTA_ORDER_NONE) {
        cetta_order_clear(o);
        return false;
    }
    uint64_t cap = 16u;
    while (cap < limit)
        cap *= 2u;
    CettaOrderNode *nodes = calloc((size_t)cap, sizeof(*nodes));
    if (!nodes) {
        cetta_order_clear(o);
        return false;
    }
#define CETTA_ORDER_MOVED_(n) \
    ((n) == CETTA_ORDER_NONE ? CETTA_ORDER_NONE : (uint32_t)target[(n)])
    for (uint32_t row = 0u; row < reach; row++) {
        const CettaOrderNode *node = &o->nodes[row];
        if (node->size == 0u)
            continue;
        nodes[target[row]] = (CettaOrderNode){
            .left = CETTA_ORDER_MOVED_(node->left),
            .right = CETTA_ORDER_MOVED_(node->right),
            .parent = CETTA_ORDER_MOVED_(node->parent),
            .size = node->size,
        };
    }
    o->root = CETTA_ORDER_MOVED_(o->root);
    o->least = CETTA_ORDER_MOVED_(o->least);
#undef CETTA_ORDER_MOVED_
    free(o->nodes);
    o->nodes = nodes;
    o->cap = (uint32_t)cap;
    return true;
}

static inline size_t cetta_order_bytes(const CettaOrder *o) {
    return (size_t)o->cap * sizeof(*o->nodes);
}

#endif
