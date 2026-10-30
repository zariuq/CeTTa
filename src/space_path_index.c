#include "space_path_index.h"

#include "space.h"
#include "stats.h"
#include <stdlib.h>
#include <string.h>

/* A key's rows: 24 bytes a key.  The capacity of `many` is 0 or a power
 * of two, kept as its logarithm plus one beside the length. */
typedef struct {
    uint64_t key;
    uint64_t len : 56;
    uint64_t cap_log : 8;
    union {
        CettaIndex single;
        CettaIndex *many;
    } positions;
} PathPosting;

struct CettaSpacePathIndex {
    CettaSpacePathIndex *next;
    TermUniverse *universe;
    uint64_t universe_instance, universe_epoch;
    uint64_t prefix_epoch;
    CettaExprIndex *path;
    size_t path_len;
    CettaSpacePathKeyKind kind;
    PathPosting *keys;
    size_t key_cap, key_len, bytes;
    /* Rows whose keyed field is open. Those with a head symbol are kept by
     * that head, so a query reads only its own head's; the rest (open or
     * compound heads, rows without a stored header) are read by every query
     * and refuted one by one. */
    PathPosting uncertain;
    PathPosting *uncertain_heads;
    size_t uncertain_head_cap, uncertain_head_len;
    /* The slot the last query's head probed. Linear probing never moves a
     * slot until the table grows, which clears this. */
    SymbolId probed_head;
    PathPosting *probed_slot;
    CettaCount source_len, keyed_len, uncertain_len;
    /* The flat-pattern shape that last demanded this index (0: none): a
     * query of that shape selects this index's coordinate again. */
    uint64_t shape;
    /* The head whose rows the index holds; SYMBOL_ID_NONE for every row. A
     * row of another head is a miss; a row whose head is open stays a
     * candidate. */
    SymbolId head;
};

static uint64_t path_key_hash(uint64_t key) {
    key ^= key >> 30u;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27u;
    key *= UINT64_C(0x94d049bb133111eb);
    return key ^ (key >> 31u);
}

static void path_charge(CettaSpacePathIndex *index, size_t added) {
    if (added > SIZE_MAX - index->bytes)
        cetta_oom(SIZE_MAX);
    index->bytes += added;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_ALLOC_BYTES,
                           added);
}

/* A slot holds a key once it was given one: a key whose rows were all
 * removed keeps its slot with an empty posting (`cap` marks it), so the
 * probe chains through it stay intact. Growth drops empty postings. */
#define PATH_POSTING_EMPTIED ((CettaCount)1)

static inline CettaCount posting_cap(const PathPosting *posting) {
    return posting->cap_log ? (CettaCount)1 << (posting->cap_log - 1u) : 0u;
}

static inline void posting_set_cap(PathPosting *posting, CettaCount cap) {
    posting->cap_log = cap ? (uint64_t)__builtin_ctzll(cap) + 1u : 0u;
}

static inline bool posting_occupied(const PathPosting *posting) {
    return posting->len != 0u || posting->cap_log != 0u;
}

static CettaIndex posting_at(const PathPosting *posting, CettaCount position) {
    return posting->len == 1u ? posting->positions.single
                             : posting->positions.many[position];
}

static void posting_append(CettaSpacePathIndex *index, PathPosting *posting,
                           CettaIndex position) {
    if (posting->len == 0u) {
        posting->positions.single = position;
    } else if (posting->len == 1u) {
        CettaIndex first = posting->positions.single;
        posting_set_cap(posting, 4u);
        posting->positions.many = cetta_malloc(4u * sizeof(CettaIndex));
        path_charge(index, 4u * sizeof(CettaIndex));
        posting->positions.many[0] = first;
        posting->positions.many[1] = position;
    } else {
        CettaCount cap = posting_cap(posting);
        if (posting->len == cap) {
            if (cap > SIZE_MAX / sizeof(CettaIndex) / 2u)
                cetta_oom(SIZE_MAX);
            CettaCount next = cap * 2u;
            posting->positions.many = cetta_realloc(posting->positions.many,
                (size_t)next * sizeof(CettaIndex));
            path_charge(index, (size_t)(next - cap) * sizeof(CettaIndex));
            posting_set_cap(posting, next);
        }
        posting->positions.many[posting->len] = position;
    }
    posting->len++;
}

static inline __attribute__((always_inline))
PathPosting *path_table_slot(PathPosting *table, size_t cap, uint64_t key) {
    size_t slot = (size_t)path_key_hash(key) & (cap - 1u);
    while (posting_occupied(&table[slot]) && table[slot].key != key) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_KEY_PROBE);
        slot = (slot + 1u) & (cap - 1u);
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_KEY_PROBE);
    return &table[slot];
}

/* Past every row: an exhausted posting never wins the merge. */
#define PATH_POSITION_END ((CettaIndex)-1)

static int path_position_cmp(const void *left, const void *right) {
    CettaIndex a = *(const CettaIndex *)left, b = *(const CettaIndex *)right;
    return (a > b) - (a < b);
}

static PathPosting *path_key_slot(CettaSpacePathIndex *index, uint64_t key) {
    return path_table_slot(index->keys, index->key_cap, key);
}

static void path_grow_table(CettaSpacePathIndex *index, PathPosting **table,
                            size_t *cap, size_t *occupied) {
    size_t next = *cap ? *cap * 2u : 16u;
    if (next < *cap || next > SIZE_MAX / sizeof(PathPosting))
        cetta_oom(SIZE_MAX);
    PathPosting *old = *table;
    size_t old_cap = *cap;
    PathPosting *grown = cetta_malloc(next * sizeof(*grown));
    memset(grown, 0, next * sizeof(*grown));
    path_charge(index, (next - old_cap) * sizeof(*grown));
    size_t kept = 0u;
    for (size_t slot = 0u; slot < old_cap; slot++) {
        if (old[slot].len != 0u) {
            *path_table_slot(grown, next, old[slot].key) = old[slot];
            kept++;
        }
    }
    *occupied = kept;
    free(old);
    *table = grown;
    *cap = next;
}

static void path_free_table(PathPosting *table, size_t cap) {
    for (size_t slot = 0u; slot < cap; slot++) {
        if (table[slot].len > 1u)
            free(table[slot].positions.many);
    }
    free(table);
}

static void path_grow_keys(CettaSpacePathIndex *index) {
    path_grow_table(index, &index->keys, &index->key_cap, &index->key_len);
}

typedef enum { PATH_MISS, PATH_KEY, PATH_UNCERTAIN } PathProjection;

/* A bound prefix's key: its columns' ids mixed in order. */
#define PATH_PREFIX_SEED UINT64_C(0x6a09e667f3bcc909)

static uint64_t path_prefix_mix(uint64_t mixed, AtomId id) {
    return path_key_hash(mixed ^ ((uint64_t)id + UINT64_C(0x9e3779b97f4a7c15)));
}

/* Terms deeper than this are not keyed by a prefix: their rows stay open. */
#define PATH_PREFIX_DEPTH 4096u

/* Whether a stored term is equal only to itself: a symbol, an integer, a
 * string, a truth value, or an expression of them, however nested. */
static bool path_prefix_exact_term(const TermUniverse *universe, AtomId id,
                                   unsigned depth) {
    const CettaTermHdr *header = tu_hdr(universe, id);
    if (!header)
        return false;
    if (header->tag == ATOM_SYMBOL)
        return true;
    if (header->tag == ATOM_GROUNDED) {
        GroundedKind kind = tu_ground_kind(universe, id);
        return kind == GV_INT || kind == GV_STRING || kind == GV_BOOL;
    }
    if (header->tag != ATOM_EXPR || depth == PATH_PREFIX_DEPTH ||
        tu_has_vars(universe, id))
        return false;
    CettaExprLen arity = tu_arity(universe, id);
    if (arity != 0u && tu_internal_tag(universe, tu_child(universe, id, 0u)) != 0)
        return false;
    for (CettaExprIndex child = 0u; child < arity; child++) {
        if (!path_prefix_exact_term(universe, tu_child(universe, id, child),
                                    depth + 1u))
            return false;
    }
    return true;
}

/* A row's column under a prefix key: its id when only a query's equal
 * value meets it; NONE when the row may meet a query there without holding
 * the query's id: a variable, or a number another number may equal. */
static AtomId path_prefix_column(const TermUniverse *universe, AtomId column) {
    return path_prefix_exact_term(universe, column, 0u) ? column
                                                        : CETTA_ATOM_ID_NONE;
}


/* A row under a bound prefix's key, kept out of line: the projection of
 * every other index stays small. */
static __attribute__((noinline)) PathProjection path_project_prefix(
    const CettaSpacePathIndex *index, AtomId id, uint64_t *key) {
    const TermUniverse *universe = index->universe;
    const CettaTermHdr *row = tu_hdr(universe, id);
    if (!row || row->tag == ATOM_VAR)
        return PATH_UNCERTAIN;
    if (row->tag != ATOM_EXPR)
        return PATH_MISS;
    CettaExprLen arity = tu_arity(universe, id);
    if (arity != 0u && tu_internal_tag(universe, tu_child(universe, id, 0u)) != 0)
        return PATH_UNCERTAIN;
    if (arity <= index->path_len)
        return PATH_MISS;
    uint64_t mixed = PATH_PREFIX_SEED;
    for (CettaExprIndex column = 1u; column <= index->path_len; column++) {
        AtomId value = path_prefix_column(universe,
                                          tu_child(universe, id, column));
        if (value == CETTA_ATOM_ID_NONE)
            return PATH_UNCERTAIN;
        mixed = path_prefix_mix(mixed, value);
    }
    *key = mixed;
    return PATH_KEY;
}

static PathProjection path_project(const CettaSpacePathIndex *index,
                                   AtomId id, uint64_t *key) {
    const TermUniverse *universe = index->universe;
    if (index->head != SYMBOL_ID_NONE) {
        const CettaTermHdr *row = tu_hdr(universe, id);
        SymbolId head = SYMBOL_ID_NONE;
        if (row) {
            if (row->tag != ATOM_EXPR)
                return row->tag == ATOM_VAR ? PATH_UNCERTAIN : PATH_MISS;
            head = row->sym_or_head;
        } else {
            const Atom *decoded = term_universe_get_atom(universe, id);
            if (decoded && decoded->kind == ATOM_EXPR && decoded->expr.len > 0u &&
                decoded->expr.elems[0]->kind == ATOM_SYMBOL)
                head = decoded->expr.elems[0]->sym_id;
        }
        if (head == SYMBOL_ID_NONE)
            return PATH_UNCERTAIN;
        if (head != index->head)
            return PATH_MISS;
    }
    if (__builtin_expect(index->kind == CETTA_SPACE_PATH_PREFIX_KEY, 0))
        return path_project_prefix(index, id, key);
    for (size_t depth = 0u; depth < index->path_len; depth++) {
        const CettaTermHdr *header = tu_hdr(universe, id);
        if (!header || header->tag == ATOM_VAR)
            return PATH_UNCERTAIN;
        if (header->tag != ATOM_EXPR)
            return PATH_MISS;
        CettaExprLen arity = tu_arity(universe, id);
        /* Private list/callable/compound carriers can have a logical shape
         * different from their expression encoding. Never prune through one. */
        if (arity != 0u && tu_internal_tag(universe, tu_child(universe, id, 0u)) != 0)
            return PATH_UNCERTAIN;
        if (index->path[depth] >= arity)
            return PATH_MISS;
        id = tu_child(universe, id, index->path[depth]);
    }
    const CettaTermHdr *header = tu_hdr(universe, id);
    if (!header || header->tag == ATOM_VAR)
        return PATH_UNCERTAIN;
    if (index->kind == CETTA_SPACE_PATH_TERM_KEY) {
        if (tu_has_vars(universe, id))
            return PATH_UNCERTAIN;
        *key = id;
        return PATH_KEY;
    }
    if (index->kind == CETTA_SPACE_PATH_SYMBOL_KEY) {
        if (header->tag != ATOM_SYMBOL)
            return PATH_MISS;
        *key = tu_sym(universe, id);
        return PATH_KEY;
    }
    if (header->tag != ATOM_GROUNDED)
        return PATH_MISS;
    if (tu_ground_kind(universe, id) != GV_INT)
        return PATH_UNCERTAIN;
    *key = (uint64_t)tu_int(universe, id);
    return PATH_KEY;
}

/* The posting that holds a row: its key's, its head's open rows, or the open
 * rows without a head; NULL for a row the index does not hold (a miss, or a
 * posting that does not exist when `create` is false). */
static inline __attribute__((always_inline))
PathPosting *path_row_posting(CettaSpacePathIndex *index, AtomId id,
                                     bool create, PathProjection *projection_out) {
    uint64_t key = 0u;
    PathProjection projection = path_project(index, id, &key);
    *projection_out = projection;
    if (projection == PATH_MISS)
        return NULL;
    if (projection == PATH_UNCERTAIN) {
        const CettaTermHdr *header = tu_hdr(index->universe, id);
        SymbolId head = header && header->tag == ATOM_EXPR
            ? header->sym_or_head : SYMBOL_ID_NONE;
        if (!header) {
            const Atom *row = term_universe_get_atom(index->universe, id);
            if (row && row->kind == ATOM_EXPR && row->expr.len > 0u &&
                row->expr.elems[0]->kind == ATOM_SYMBOL)
                head = row->expr.elems[0]->sym_id;
        }
        if (head == SYMBOL_ID_NONE)
            return &index->uncertain;
        if (create && (index->uncertain_head_cap == 0u ||
                index->uncertain_head_len >=
                    index->uncertain_head_cap - index->uncertain_head_cap / 4u)) {
            path_grow_table(index, &index->uncertain_heads,
                            &index->uncertain_head_cap, &index->uncertain_head_len);
            index->probed_head = SYMBOL_ID_NONE;
        }
        if (index->uncertain_head_cap == 0u)
            return NULL;
        PathPosting *posting = path_table_slot(index->uncertain_heads,
            index->uncertain_head_cap, head);
        if (!posting_occupied(posting)) {
            if (!create)
                return NULL;
            posting->key = head;
            index->uncertain_head_len++;
        }
        return posting;
    }
    if (create && (index->key_cap == 0u ||
            index->key_len >= index->key_cap - index->key_cap / 4u))
        path_grow_keys(index);
    if (index->key_cap == 0u)
        return NULL;
    PathPosting *posting = path_key_slot(index, key);
    if (!posting_occupied(posting)) {
        if (!create)
            return NULL;
        posting->key = key;
        index->key_len++;
    }
    return posting;
}

/* Remove a row from a posting kept in row order. */
static bool posting_remove(CettaSpacePathIndex *index, PathPosting *posting,
                           CettaIndex position) {
    if (posting->len == 0u)
        return false;
    if (posting->len == 1u) {
        if (posting->positions.single != position)
            return false;
        posting->len = 0u;
        posting_set_cap(posting, PATH_POSTING_EMPTIED);
        return true;
    }
    CettaIndex *items = posting->positions.many;
    CettaCount low = 0u, high = posting->len;
    while (low < high) {
        CettaCount middle = low + (high - low) / 2u;
        if (items[middle] < position)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low == posting->len || items[low] != position)
        return false;
    memmove(items + low, items + low + 1u,
            (size_t)(posting->len - low - 1u) * sizeof(*items));
    posting->len--;
    if (posting->len == 1u) {
        CettaIndex only = items[0];
        size_t freed = (size_t)posting_cap(posting) * sizeof(*items);
        free(items);
        index->bytes -= freed;
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_FREE_BYTES,
                               freed);
        posting->positions.single = only;
        posting_set_cap(posting, 0u);
    }
    return true;
}

/* Insert a row into a posting kept in row order. */
static void posting_insert(CettaSpacePathIndex *index, PathPosting *posting,
                           CettaIndex position) {
    if (posting->len == 0u ||
        posting_at(posting, posting->len - 1u) < position) {
        posting_append(index, posting, position);
        return;
    }
    if (posting->len == 1u) {
        CettaIndex later = posting->positions.single;
        posting->positions.single = position;
        posting_append(index, posting, later);
        return;
    }
    posting_append(index, posting, position);
    CettaIndex *items = posting->positions.many;
    CettaCount at = posting->len - 1u;
    while (at > 0u && items[at - 1u] > position) {
        items[at] = items[at - 1u];
        at--;
    }
    items[at] = position;
}

static void path_append_row(CettaSpacePathIndex *index, AtomId id,
                            CettaIndex position) {
    PathProjection projection;
    PathPosting *posting = path_row_posting(index, id, true, &projection);
    if (posting)
        posting_append(index, posting, position);
    if (projection == PATH_UNCERTAIN)
        index->uncertain_len++;
    else if (projection == PATH_KEY)
        index->keyed_len++;
    index->source_len++;
}

static bool path_query_key(const Atom *atom, CettaSpacePathKeyKind *kind,
                            uint64_t *key) {
    if (atom && atom->kind == ATOM_SYMBOL) {
        *kind = CETTA_SPACE_PATH_SYMBOL_KEY;
        *key = atom->sym_id;
        return true;
    }
    if (atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT) {
        *kind = CETTA_SPACE_PATH_INTEGER_KEY;
        *key = (uint64_t)atom->ground.ival;
        return true;
    }
    return false;
}

bool space_path_index_term_key_admits(const Atom *element) {
    return element && element->kind == ATOM_EXPR && !atom_has_vars(element) &&
        (element->structural_facts & ATOM_STRUCTURAL_SYMBOLIC) != 0u;
}

/* A ground symbolic term's key: its id, or an id no row holds when the
 * universe has never made the term. */
static bool path_query_term_key(const TermUniverse *universe, const Atom *element,
                                uint64_t *key) {
    if (!space_path_index_term_key_admits(element))
        return false;
    AtomId id = term_universe_lookup_atom_id(universe, (Atom *)element);
    *key = id == CETTA_ATOM_ID_NONE ? UINT64_MAX : (uint64_t)id;
    return true;
}

typedef struct { Atom *term; CettaExprIndex child; } PathQueryFrame;

static bool path_term_holds_term(const Atom *term) {
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (term->expr.elems[index]->kind == ATOM_EXPR)
            return true;
    }
    return false;
}

/* The shape of a flat pattern: its length, then two bits per element
 * (variable, symbol, integer, other leaf). Coordinate selection reads only
 * this shape. 0 for a nested or long pattern. */
static uint64_t path_flat_shape(const Atom *pattern) {
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
        pattern->expr.len > 28u)
        return 0u;
    uint64_t shape = pattern->expr.len;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        const Atom *element = pattern->expr.elems[index];
        uint64_t code;
        if (!element || element->kind == ATOM_EXPR)
            return 0u;
        if (element->kind == ATOM_VAR)
            code = 1u;
        else if (element->kind == ATOM_SYMBOL)
            code = 2u;
        else if (element->kind == ATOM_GROUNDED && element->ground.gkind == GV_INT)
            code = 3u;
        else
            code = 0u;
        shape |= code << (8u + 2u * index);
    }
    return shape;
}

/* The Space's index of `kind` at `path` over the rows of `head`
 * (SYMBOL_ID_NONE: every row), if it keeps one. */
static inline __attribute__((always_inline)) CettaSpacePathIndex *path_index_find(
    Space *space, CettaSpacePathKeyKind kind, const CettaExprIndex *path,
    size_t depth, SymbolId head) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->kind != kind || index->path_len != depth || index->head != head)
            continue;
        size_t position = 0u;
        while (position < depth && index->path[position] == path[position])
            position++;
        if (position == depth)
            return index;
    }
    return NULL;
}

/* A new index of `kind` at `path` over the rows of `head`, built from the
 * rows. */
static __attribute__((noinline)) CettaSpacePathIndex *path_index_build(
    Space *space, CettaSpacePathKeyKind kind, const CettaExprIndex *path,
    size_t depth, SymbolId head) {
    CettaSpacePathIndex *index = cetta_malloc(sizeof(*index));
    *index = (CettaSpacePathIndex){
        .next = space->match_backend.native.path_indexes,
        .universe = space->native.universe,
        .universe_instance = space->native.universe->instance_id,
        .universe_epoch = space->native.universe->storage_epoch,
        .prefix_epoch = space->prefix_epoch,
        .path_len = depth, .kind = kind, .head = head,
    };
    index->path = cetta_malloc(depth * sizeof(*index->path));
    path_charge(index, sizeof(*index) + depth * sizeof(*index->path));
    memcpy(index->path, path, depth * sizeof(*index->path));
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BUILD);
    for (CettaIndex position = 0u; position < space->native.len; position++) {
        path_append_row(index, space_get_atom_id_at64(space, position), position);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BUILD_ROW);
    }
    space->match_backend.native.path_indexes = index;
    return index;
}

/* The Space's index of `kind` at `path` over the rows of `head`
 * (SYMBOL_ID_NONE: every row), built from the rows when it has none and
 * `build` asks for one. */
static inline CettaSpacePathIndex *path_index_for(Space *space, CettaSpacePathKeyKind kind,
                                                  const CettaExprIndex *path, size_t depth,
                                                  SymbolId head, bool build) {
    CettaSpacePathIndex *index = path_index_find(space, kind, path, depth, head);
    return index || !build ? index : path_index_build(space, kind, path, depth, head);
}

/* Choose a rigid coordinate after a variable in the trie's structural order.
 * Prefix-selective patterns keep their existing access path. Explicit frames
 * make the coordinate search independent of the C call stack. */
static CettaSpacePathIndex *path_demand(Space *space, Atom *pattern,
                                      uint64_t *key) {
    if (!pattern || pattern->kind != ATOM_EXPR || !atom_has_vars(pattern) ||
        atom_structural_may_have_list_carrier(pattern) ||
        atom_structural_may_have_internal_tag(pattern))
        return NULL;
    PathQueryFrame local[16];
    PathQueryFrame *frames = local;
    size_t depth = 1u, capacity = 16u;
    frames[0] = (PathQueryFrame){.term = pattern};
    bool unbound = false;
    CettaSpacePathKeyKind kind = CETTA_SPACE_PATH_SYMBOL_KEY;
    CettaExprIndex *head_path = NULL;
    size_t head_depth = 0u;
    CettaSpacePathKeyKind head_kind = CETTA_SPACE_PATH_SYMBOL_KEY;
    uint64_t head_key = 0u;
    bool found = false;
    while (depth != 0u) {
        PathQueryFrame *frame = &frames[depth - 1u];
        if (frame->child == frame->term->expr.len) {
            depth--;
            if (depth != 0u)
                frames[depth - 1u].child++;
            continue;
        }
        Atom *child = frame->term->expr.elems[frame->child];
        if (child && child->kind == ATOM_VAR)
            unbound = true;
        if (unbound && path_query_key(child, &kind, key)) {
            if (frame->child != 0u) {
                found = true;
                break;
            }
            /* A rigid nested head is useful when no later value is bound.
             * Prefer the first bound value over its surrounding constructor. */
            if (!head_path) {
                head_depth = depth;
                head_kind = kind;
                head_key = *key;
                head_path = cetta_malloc(depth * sizeof(*head_path));
                for (size_t position = 0u; position < depth; position++)
                    head_path[position] = frames[position].child;
            }
        }
        if (child && child->kind == ATOM_EXPR && !atom_has_vars(child)) {
            /* A ground term whose leaves are all symbols is one id however
             * deep: after a variable a term holding a term is the coordinate
             * itself, where its deepest leaf would ask for a path as deep as
             * the term.  A flat one is keyed by its first leaf after its
             * head: a row whose term there holds variables is then a miss
             * unless the leaf the key reads is open in it, where keyed whole
             * it would stay a candidate.  Before any variable a ground term
             * holds no coordinate to find. */
            if (unbound && frame->child != 0u && path_term_holds_term(child) &&
                path_query_term_key(space->native.universe, child, key)) {
                kind = CETTA_SPACE_PATH_TERM_KEY;
                found = true;
                break;
            }
            if (!unbound) {
                frame->child++;
                continue;
            }
        }
        if (child && child->kind == ATOM_EXPR) {
            if (depth == capacity) {
                if (capacity > SIZE_MAX / sizeof(*frames) / 2u)
                    cetta_oom(SIZE_MAX);
                capacity *= 2u;
                PathQueryFrame *next = cetta_malloc(capacity * sizeof(*next));
                memcpy(next, frames, depth * sizeof(*next));
                if (frames != local)
                    free(frames);
                frames = next;
            }
            frames[depth++] = (PathQueryFrame){.term = child};
        } else {
            frame->child++;
        }
    }
    /* A pattern whose bound scalars all lead: its first one, over the rows
     * of the pattern's head alone, since the head already selects them. */
    SymbolId lead_head = SYMBOL_ID_NONE;
    if (!found && !head_path) {
        for (CettaExprIndex position = 1u; position < pattern->expr.len; position++) {
            bool keyed = path_query_key(pattern->expr.elems[position], &kind, key);
            if (!keyed && path_query_term_key(space->native.universe,
                                              pattern->expr.elems[position], key)) {
                kind = CETTA_SPACE_PATH_TERM_KEY;
                keyed = true;
            }
            if (keyed) {
                frames[0] = (PathQueryFrame){.term = pattern, .child = position};
                depth = 1u;
                found = true;
                if (pattern->expr.elems[0]->kind == ATOM_SYMBOL)
                    lead_head = pattern->expr.elems[0]->sym_id;
                break;
            }
        }
    }
    SymbolId index_head = SYMBOL_ID_NONE;
    bool selected_head = !found && head_path;
    if (selected_head) {
        depth = head_depth;
        kind = head_kind;
        *key = head_key;
        found = true;
    }
    CettaSpacePathIndex *index = NULL;
    if (found) {
        index_head = lead_head;
        CettaExprIndex *path = cetta_malloc(depth * sizeof(*path));
        for (size_t position = 0u; position < depth; position++)
            path[position] = selected_head
                ? head_path[position] : frames[position].child;
        index = path_index_for(space, kind, path, depth, index_head, true);
        free(path);
    }
    if (frames != local)
        free(frames);
    free(head_path);
    return index;
}

static bool path_generation_current(const Space *space) {
    const CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
    return !index || (index->universe == space->native.universe &&
        index->universe_instance == space->native.universe->instance_id &&
        index->universe_epoch == space->native.universe->storage_epoch &&
        index->prefix_epoch == space->prefix_epoch &&
        index->source_len == space->native.len);
}

/* The candidates of a query that reads open rows: its key's posting merged
 * in source order with the open rows of its head (or every open row, for a
 * query without a head symbol), each open row refuted by its head. */
static __attribute__((noinline)) CettaCount path_merge_open_rows(
    Space *space, CettaSpacePathIndex *index, PathPosting *posting,
    CettaCount known, PathPosting *headed, CettaCount headed_len,
    SymbolId query_head, CettaCount candidates, CettaIndex *positions) {
    CettaCount open_len = index->uncertain.len + headed_len;
    /* The open rows a query without a head symbol reads: every one, in
     * source order. A query with a head reads two postings directly. */
    CettaIndex *open = NULL;
    if (query_head == SYMBOL_ID_NONE) {
        open = cetta_malloc((size_t)open_len * sizeof(*open));
        CettaCount next = 0u;
        for (CettaCount item = 0u; item < index->uncertain.len; item++)
            open[next++] = posting_at(&index->uncertain, item);
        for (size_t slot = 0u; slot < index->uncertain_head_cap; slot++)
            for (CettaCount item = 0u; item < index->uncertain_heads[slot].len; item++)
                open[next++] = posting_at(&index->uncertain_heads[slot], item);
        qsort(open, open_len, sizeof(*open), path_position_cmp);
    }
    CettaCount exact_next = 0u, general_next = 0u, own_next = 0u;
    CettaCount retained = 0u;
    for (CettaCount position = 0u; position < candidates; position++) {
        CettaIndex exact = exact_next < known
            ? posting_at(posting, exact_next) : PATH_POSITION_END;
        CettaIndex general = open
            ? (general_next < open_len ? open[general_next] : PATH_POSITION_END)
            : (general_next < index->uncertain.len
                ? posting_at(&index->uncertain, general_next) : PATH_POSITION_END);
        CettaIndex own = headed && own_next < headed_len
            ? posting_at(headed, own_next) : PATH_POSITION_END;
        CettaIndex source_position;
        bool uncertain = true;
        if (exact <= general && exact <= own) {
            source_position = exact;
            exact_next++;
            uncertain = false;
        } else if (general <= own) {
            source_position = general;
            general_next++;
        } else {
            source_position = own;
            own_next++;
        }
        /* Uncertainty at the selected field does not erase a rigid outer
         * symbol. Refute its clash without entering the full matcher; variable
         * heads, tag-headed private carriers and unrecorded rows stay candidates.
         * Keyed postings keep their existing canonical check without a second
         * row read. TermPathPostingRefinement proves that selectively refining
         * uncertain postings preserves the ordered matching occurrences. */
        if (uncertain && query_head != SYMBOL_ID_NONE) {
            AtomId row_id = space_get_atom_id_at64(space, source_position);
            const CettaTermHdr *header = tu_hdr(space->native.universe, row_id);
            if (header && header->tag == ATOM_EXPR &&
                header->sym_or_head != SYMBOL_ID_NONE &&
                header->sym_or_head != query_head)
                continue;
            /* A row kept only in decoded form (one holding a space or another
             * identity-bearing value) refutes by its head the same way. */
            if (!header) {
                const Atom *row = term_universe_get_atom(
                    space->native.universe, row_id);
                if (row && row->kind == ATOM_EXPR && row->expr.len > 0u &&
                    row->expr.elems[0]->kind == ATOM_SYMBOL &&
                    row->expr.elems[0]->sym_id != query_head)
                    continue;
            }
        }
        positions[retained++] = source_position;
    }
    free(open);
    return retained;
}

/* The rows an index offers a query under `key` before its open rows are
 * refuted: the key's posting, the open rows without a head, and the open
 * rows of the query's head (every open row, for a query without one). */
static inline __attribute__((always_inline))
CettaCount path_offer(CettaSpacePathIndex *index, uint64_t key,
                             SymbolId query_head, PathPosting **posting_out,
                             PathPosting **headed_out, CettaCount *headed_len_out) {
    PathPosting *posting = index->key_cap ? path_key_slot(index, key) : NULL;
    PathPosting *headed = NULL;
    CettaCount headed_len = 0u;
    if (query_head != SYMBOL_ID_NONE) {
        if (index->uncertain_head_cap) {
            if (index->probed_head != query_head) {
                index->probed_slot = path_table_slot(index->uncertain_heads,
                    index->uncertain_head_cap, query_head);
                index->probed_head = query_head;
            }
            headed = index->probed_slot;
            headed_len = headed->len;
        }
    } else {
        headed_len = index->uncertain_len - index->uncertain.len;
    }
    *posting_out = posting;
    *headed_out = headed;
    *headed_len_out = headed_len;
    return (posting ? posting->len : 0u) + index->uncertain.len + headed_len;
}

/* ── Choosing among a flat shape's columns ─────────────────────────────── */

/* A flat shape whose lookups bind several columns after its first variable
 * reads, of the indexes the space keeps for those columns, the one whose
 * posting for the lookup's key is shortest.  The rows its lookups read
 * beyond a few each, while some column has no index, are rent toward one:
 * once the rent reaches the rows a build reads, the next column is built,
 * over the shape's head, a column whose key has changed between lookups (a
 * value bound from outside) before a constant one.  A lookup that reads a
 * few rows pays nothing, so a shape already served by a selective column
 * builds nothing more.  Counted in rows beyond those few, and when the
 * built column serves its lookups within them, a build when the rent
 * reaches it costs at most twice the cheaper of building at the first
 * lookup and never building, plus one lookup's rows
 * (rentOrBuyVar_le_two_mul_opt_add, Mettapedia's RelationStoreRentOrBuy). */
#define PATH_SHAPES_KEPT 16u
#define PATH_RENT_FREE_ROWS 8u

struct CettaSpacePathShape {
    CettaSpacePathShape *next;
    uint64_t shape;
    CettaCount rent;
    /* Bit p: the key at position p changed between two lookups. */
    uint32_t varied;
    /* The key each column held at the last lookup. */
    uint64_t last[];
};

/* A flat shape's columns after its first variable: bit p for a symbol or an
 * integer at position p. */
static uint32_t path_shape_columns(uint64_t shape) {
    size_t len = (size_t)(shape & 0xffu);
    uint32_t columns = 0u;
    bool unbound = false;
    for (size_t position = 0u; position < len; position++) {
        uint64_t code = (shape >> (8u + 2u * position)) & 3u;
        if (code == 1u)
            unbound = true;
        else if (unbound && (code == 2u || code == 3u))
            columns |= UINT32_C(1) << position;
    }
    return columns;
}

/* A flat shape's keyed columns after its head, bound before or after its
 * variables: bit p for a symbol or an integer at position p. */
static uint32_t path_shape_keyed_columns(uint64_t shape) {
    size_t len = (size_t)(shape & 0xffu);
    uint32_t columns = 0u;
    for (size_t position = 1u; position < len; position++) {
        uint64_t code = (shape >> (8u + 2u * position)) & 3u;
        if (code == 2u || code == 3u)
            columns |= UINT32_C(1) << position;
    }
    return columns;
}

static uint64_t path_column_key(const Atom *pattern, unsigned position) {
    CettaSpacePathKeyKind kind;
    uint64_t key = 0u;
    (void)path_query_key(pattern->expr.elems[position], &kind, &key);
    return key;
}

/* The state of a flat shape, made at its first lookup with an unserved
 * column; NULL once the space keeps PATH_SHAPES_KEPT shapes. */
static CettaSpacePathShape *path_shape_state(Space *space, uint64_t shape,
                                            const Atom *pattern, uint32_t columns) {
    size_t kept = 0u;
    for (CettaSpacePathShape *state = space->match_backend.native.path_shapes;
         state; state = state->next, kept++) {
        if (state->shape == shape)
            return state;
    }
    if (kept >= PATH_SHAPES_KEPT)
        return NULL;
    CettaSpacePathShape *state = cetta_malloc(
        sizeof(*state) + (size_t)pattern->expr.len * sizeof(state->last[0]));
    *state = (CettaSpacePathShape){
        .next = space->match_backend.native.path_shapes, .shape = shape,
    };
    for (CettaExprIndex position = 0u; position < pattern->expr.len; position++)
        state->last[position] = (columns >> position) & 1u
            ? path_column_key(pattern, (unsigned)position) : 0u;
    space->match_backend.native.path_shapes = state;
    return state;
}

/* Of the indexes the space keeps for a flat pattern's columns, over every
 * row or over the pattern's head's, the one offering the fewest rows, with
 * its key and offer (only indexes without open rows when `closed`).
 * `served` gets a bit for each column some index keeps.  NULL when none
 * qualifies. */
static CettaSpacePathIndex *path_shortest_offer(Space *space, const Atom *pattern,
                                                uint32_t columns, bool closed,
                                                uint64_t *key_out, CettaCount *offer_out,
                                                uint32_t *served_out) {
    SymbolId head = pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? pattern->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    CettaSpacePathIndex *best = NULL;
    CettaCount best_offer = 0u;
    uint32_t served = 0u;
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->path_len != 1u || index->path[0] >= 32u ||
            !((columns >> index->path[0]) & 1u) ||
            (index->head != SYMBOL_ID_NONE && index->head != head))
            continue;
        CettaSpacePathKeyKind kind;
        uint64_t key = 0u;
        if (!path_query_key(pattern->expr.elems[index->path[0]], &kind, &key) ||
            kind != index->kind)
            continue;
        served |= UINT32_C(1) << index->path[0];
        if (closed && index->uncertain_len != 0u)
            continue;
        PathPosting *posting, *headed;
        CettaCount headed_len;
        CettaCount offer = path_offer(index, key, head, &posting, &headed, &headed_len);
        if (!best || offer < best_offer) {
            best = index;
            best_offer = offer;
            *key_out = key;
        }
    }
    *offer_out = best_offer;
    *served_out = served;
    return best;
}

/* A lookup of a flat shape with several columns: its shortest offer, after
 * the rent its lookups have paid has bought the next column's index, when
 * `build` lets it.  NULL while no column has one: the first is the one its
 * caller builds. */
static CettaSpacePathIndex *path_choose_column(Space *space, Atom *pattern,
                                               uint64_t shape, uint32_t columns,
                                               uint64_t *key, bool build) {
    CettaCount offer = 0u;
    uint32_t served = 0u;
    CettaSpacePathIndex *index = path_shortest_offer(space, pattern, columns, false,
                                                     key, &offer, &served);
    uint32_t unserved = columns & ~served;
    if (!index || !unserved || !build || offer <= PATH_RENT_FREE_ROWS)
        return index;
    CettaSpacePathShape *state = path_shape_state(space, shape, pattern, columns);
    if (!state)
        return index;
    for (uint32_t rest = columns; rest; rest &= rest - 1u) {
        unsigned position = (unsigned)__builtin_ctz(rest);
        uint64_t column_key = path_column_key(pattern, position);
        if (state->last[position] != column_key)
            state->varied |= UINT32_C(1) << position;
        state->last[position] = column_key;
    }
    state->rent += offer - PATH_RENT_FREE_ROWS;
    if (state->rent < space->native.len)
        return index;
    state->rent = 0u;
    uint32_t next = unserved & state->varied ? unserved & state->varied : unserved;
    unsigned position = (unsigned)__builtin_ctz(next);
    CettaSpacePathKeyKind kind = CETTA_SPACE_PATH_SYMBOL_KEY;
    uint64_t next_key = 0u;
    (void)path_query_key(pattern->expr.elems[position], &kind, &next_key);
    SymbolId head = pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? pattern->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    CettaExprIndex path[1] = {position};
    CettaSpacePathIndex *bought = path_index_for(space, kind, path, 1u, head, true);
    PathPosting *posting, *headed;
    CettaCount headed_len;
    if (path_offer(bought, next_key, head, &posting, &headed, &headed_len) < offer) {
        *key = next_key;
        return bought;
    }
    return index;
}

static bool path_candidates(Space *space, CettaSpacePathIndex *index, Atom *pattern,
                            uint64_t key, bool broad, CettaIndex **out,
                            CettaIndex *count);

bool space_path_index_candidates(Space *space, Atom *pattern,
                                CettaIndex **out, CettaIndex *count) {
    space_dense_settle_for_pattern(space, pattern);
    if (!space || !space->native.universe || !out || !count || space->overlay_base ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !space_path_index_may_select(pattern))
        return false;
    if (!path_generation_current(space))
        space_path_indexes_retire(space);
    uint64_t key = 0u;
    uint64_t shape = path_flat_shape(pattern);
    uint32_t columns = shape ? path_shape_columns(shape) : 0u;
    CettaSpacePathIndex *index = columns & (columns - 1u)
        ? path_choose_column(space, pattern, shape, columns, &key, true) : NULL;
    SymbolId pattern_head = pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? pattern->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    for (CettaSpacePathIndex *known = shape && !index
             ? space->match_backend.native.path_indexes : NULL;
         known; known = known->next) {
        if (known->shape != shape || known->path_len != 1u ||
            (known->head != SYMBOL_ID_NONE && known->head != pattern_head))
            continue;
        CettaSpacePathKeyKind kind;
        if (path_query_key(pattern->expr.elems[known->path[0]], &kind, &key) &&
            kind == known->kind)
            index = known;
        break;
    }
    if (!index) {
        index = path_demand(space, pattern, &key);
        if (index && shape)
            index->shape = shape;
    }
    if (!index)
        return false;
    return path_candidates(space, index, pattern, key, false, out, count);
}

/* The rows an index offers a pattern under `key`: the key's posting and the
 * open rows the pattern's head reads, in row order.  A broad offer, three
 * quarters of the rows or more, is declined unless `broad` admits it. */
/* path_candidates for a pattern headed by QUERY_HEAD, SYMBOL_ID_NONE when
 * its head is not a symbol. */
static bool path_candidates_head(Space *space, CettaSpacePathIndex *index,
                                 SymbolId query_head, uint64_t key, bool broad,
                                 CettaIndex **out, CettaIndex *count) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_LOOKUP);
    PathPosting *posting, *headed;
    CettaCount headed_len;
    CettaCount candidates = path_offer(index, key, query_head, &posting, &headed,
                                       &headed_len);
    CettaCount known = posting ? posting->len : 0u;
    if (!broad && candidates >= space->native.len - space->native.len / 4u) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BROAD_DECLINE);
        return false;
    }
    if (candidates > SIZE_MAX / sizeof(CettaIndex))
        cetta_oom(SIZE_MAX);
    CettaIndex *positions = candidates
        ? cetta_malloc((size_t)candidates * sizeof(*positions)) : NULL;
    CettaCount open_len = index->uncertain.len + headed_len;
    if (open_len == 0u) {
        for (CettaCount next = 0u; next < known; next++)
            positions[next] = posting_at(posting, next);
        *out = positions;
        *count = known;
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_CANDIDATE,
                               known);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_CANDIDATES, known);
        return true;
    }
    CettaCount retained = path_merge_open_rows(space, index, posting, known,
        headed, headed_len, query_head, candidates, positions);
    *out = positions;
    *count = retained;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_CANDIDATE,
                           retained);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_CANDIDATES, retained);
    return true;
}

static bool path_candidates(Space *space, CettaSpacePathIndex *index, Atom *pattern,
                            uint64_t key, bool broad, CettaIndex **out,
                            CettaIndex *count) {
    SymbolId query_head = pattern->expr.len != 0u &&
            pattern->expr.elems[0] && pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? pattern->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    return path_candidates_head(space, index, query_head, key, broad, out,
                                count);
}

bool space_path_index_lead_candidates(Space *space, Atom *pattern, bool build,
                                      CettaIndex **out, CettaIndex *count) {
    space_dense_settle_for_pattern(space, pattern);
    if (!space || !space->native.universe || !out || !count || space->overlay_base ||
        space->native.marked != 0u ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !pattern || pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        !pattern->expr.elems[0] || pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom_structural_may_have_list_carrier(pattern) ||
        atom_structural_may_have_internal_tag(pattern))
        return false;
    if (!path_generation_current(space))
        space_path_indexes_retire(space);
    /* A flat pattern binding several columns reads the one whose posting is
     * shortest, as a lookup of its shape does. */
    uint64_t shape = path_flat_shape(pattern);
    uint32_t columns = shape ? path_shape_keyed_columns(shape) : 0u;
    if (columns & (columns - 1u)) {
        uint64_t key = 0u;
        CettaSpacePathIndex *index = path_choose_column(space, pattern, shape,
                                                        columns, &key, build);
        if (index)
            return path_candidates(space, index, pattern, key, true, out, count);
    }
    for (CettaExprIndex position = 1u; position < pattern->expr.len; position++) {
        CettaSpacePathKeyKind kind;
        uint64_t key = 0u;
        bool keyed = path_query_key(pattern->expr.elems[position], &kind, &key);
        if (!keyed && path_query_term_key(space->native.universe,
                                          pattern->expr.elems[position], &key)) {
            kind = CETTA_SPACE_PATH_TERM_KEY;
            keyed = true;
        }
        if (!keyed)
            continue;
        CettaExprIndex path[1] = {position};
        CettaSpacePathIndex *index = path_index_for(
            space, kind, path, 1u, pattern->expr.elems[0]->sym_id, build);
        return index && path_candidates(space, index, pattern, key, true, out, count);
    }
    return false;
}

/* Whether a pattern's column can be part of a bound prefix: a value only an
 * equal one meets (a symbol, an integer, a string, a truth value, or an
 * expression of them), whose id the universe looks up. */
static bool path_prefix_query_column(const Atom *atom, unsigned depth) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_SYMBOL)
        return true;
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_INT || atom->ground.gkind == GV_STRING ||
            atom->ground.gkind == GV_BOOL;
    if (atom->kind != ATOM_EXPR || depth == PATH_PREFIX_DEPTH ||
        atom_has_vars(atom))
        return false;
    if (space_path_index_term_key_admits(atom))
        return true;
    if (atom_structural_may_have_list_carrier(atom) ||
        atom_structural_may_have_internal_tag(atom))
        return false;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        if (!path_prefix_query_column(atom->expr.elems[child], depth + 1u))
            return false;
    }
    return true;
}

enum { PATH_PREFIX_COLUMNS = 16u };

size_t space_path_index_prefix_bound(const Atom *pattern) {
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        !pattern->expr.elems[0] || pattern->expr.elems[0]->kind != ATOM_SYMBOL)
        return 0u;
    size_t bound = 0u;
    while (bound < PATH_PREFIX_COLUMNS && bound + 1u < pattern->expr.len &&
           path_prefix_query_column(pattern->expr.elems[bound + 1u], 0u))
        bound++;
    /* The prefix holds all the pattern's bound content: a column after it
     * that binds structure, such as (REG 2 $v), is descended by the trie to
     * the rows it names, where the prefix's index would offer every row of
     * the prefix. */
    for (CettaExprIndex rest = bound + 1u; rest < pattern->expr.len; rest++) {
        if (!pattern->expr.elems[rest] || pattern->expr.elems[rest]->kind != ATOM_VAR)
            return 0u;
    }
    return bound;
}

bool space_path_index_prefix_candidates(Space *space, Atom *pattern, bool build,
                                        CettaIndex **out, CettaIndex *count) {
    space_dense_settle_for_pattern(space, pattern);
    if (!space || !space->native.universe || !out || !count || space->overlay_base ||
        space->native.marked != 0u ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !pattern || pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        !pattern->expr.elems[0] || pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        !atom_has_vars(pattern) ||
        atom_structural_may_have_list_carrier(pattern) ||
        atom_structural_may_have_internal_tag(pattern))
        return false;
    size_t bound = space_path_index_prefix_bound(pattern);
    if (bound == 0u)
        return false;
    if (!path_generation_current(space))
        space_path_indexes_retire(space);
    CettaExprIndex path[PATH_PREFIX_COLUMNS];
    for (size_t column = 0u; column < bound; column++)
        path[column] = column + 1u;
    CettaSpacePathIndex *index = path_index_for(space, CETTA_SPACE_PATH_PREFIX_KEY,
        path, bound, pattern->expr.elems[0]->sym_id, build);
    /* Rows open in the prefix are read by every probe: a head whose rows
     * are mostly open is left to the trie, which descends them by shape. */
    if (!index || index->uncertain_len > index->keyed_len)
        return false;
    /* A value the universe never made is held by no row: its key is one no
     * row's mix of ids is meant to take, and the open rows remain. */
    uint64_t key = PATH_PREFIX_SEED;
    for (size_t column = 1u; column <= bound; column++) {
        AtomId id = term_universe_lookup_atom_id(space->native.universe,
                                                 pattern->expr.elems[column]);
        key = path_prefix_mix(key, id);
    }
    return path_candidates(space, index, pattern, key, true, out, count);
}

bool space_path_index_prefix_candidates_ids(Space *space, SymbolId head,
                                            const AtomId *columns,
                                            CettaExprLen len,
                                            CettaIndex **out,
                                            CettaIndex *count) {
    space_dense_settle_for_head(space, head, len);
    if (!space || !space->native.universe || !out || !count || space->overlay_base ||
        space->native.marked != 0u ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !columns || len < 2u || head == SYMBOL_ID_NONE)
        return false;
    /* The bound prefix, as space_path_index_prefix_bound reads it: every
     * column after it free, and one at least. */
    size_t bound = 0u;
    while (bound < PATH_PREFIX_COLUMNS && bound + 1u < len &&
           columns[bound + 1u] != CETTA_ATOM_ID_NONE)
        bound++;
    if (bound == 0u || bound + 1u >= len)
        return false;
    for (CettaExprIndex rest = bound + 1u; rest < len; rest++) {
        if (columns[rest] != CETTA_ATOM_ID_NONE)
            return false;
    }
    if (!path_generation_current(space))
        space_path_indexes_retire(space);
    CettaExprIndex path[PATH_PREFIX_COLUMNS];
    for (size_t column = 0u; column < bound; column++)
        path[column] = column + 1u;
    CettaSpacePathIndex *index = path_index_for(space, CETTA_SPACE_PATH_PREFIX_KEY,
        path, bound, head, true);
    if (!index || index->uncertain_len > index->keyed_len)
        return false;
    uint64_t key = PATH_PREFIX_SEED;
    for (size_t column = 1u; column <= bound; column++)
        key = path_prefix_mix(key, columns[column]);
    return path_candidates_head(space, index, head, key, true, out, count);
}

bool space_path_index_existing_candidates(Space *space, Atom *pattern,
                                          CettaIndex **out, CettaIndex *count) {
    space_dense_settle_for_pattern(space, pattern);
    if (!space || !out || !count || !space_path_index_may_select(pattern) ||
        !path_generation_current(space))
        return false;
    uint64_t shape = path_flat_shape(pattern);
    if (!shape)
        return false;
    uint32_t columns = path_shape_columns(shape);
    CettaSpacePathIndex *found = NULL;
    uint64_t key = 0u;
    if (columns & (columns - 1u)) {
        CettaCount offer;
        uint32_t served;
        found = path_shortest_offer(space, pattern, columns, true, &key, &offer,
                                    &served);
    }
    for (CettaSpacePathIndex *index = found ? NULL
             : space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->shape != shape || index->path_len != 1u ||
            (index->head != SYMBOL_ID_NONE &&
             (pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
              index->head != pattern->expr.elems[0]->sym_id)))
            continue;
        CettaSpacePathKeyKind kind;
        if (!path_query_key(pattern->expr.elems[index->path[0]], &kind, &key) ||
            kind != index->kind || index->uncertain_len != 0u)
            return false;
        found = index;
        break;
    }
    if (!found)
        return false;
    PathPosting *posting = found->key_cap ? path_key_slot(found, key) : NULL;
    CettaCount known = posting ? posting->len : 0u;
    CettaIndex *positions = cetta_malloc(
        (size_t)(known ? known : 1u) * sizeof(*positions));
    for (CettaCount next = 0u; next < known; next++)
        positions[next] = posting_at(posting, next);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_LOOKUP);
    *out = positions;
    *count = known;
    return true;
}

void space_path_indexes_note_add(Space *space, AtomId id, CettaIndex position) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->source_len != position || index->universe != space->native.universe ||
            index->universe_instance != space->native.universe->instance_id ||
            index->prefix_epoch != space->prefix_epoch ||
            index->universe_epoch != space->native.universe->storage_epoch) {
            space_path_indexes_retire(space);
            return;
        }
        path_append_row(index, id, position);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_APPEND_ROW);
    }
}

/* An unordered removal: the row at `removed_row` leaves and the last row,
 * at `moved_from`, takes its place. Every index keeps both rows' postings in
 * row order; an index it cannot keep is retired. */
void space_path_indexes_note_swap_remove(Space *space, CettaIndex removed_row,
                                         AtomId removed_id, CettaIndex moved_from,
                                         AtomId moved_id) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->source_len != moved_from + 1u ||
            index->universe != space->native.universe ||
            index->universe_instance != space->native.universe->instance_id ||
            index->prefix_epoch != space->prefix_epoch ||
            index->universe_epoch != space->native.universe->storage_epoch) {
            space_path_indexes_retire(space);
            return;
        }
        PathProjection removed_class;
        PathPosting *removed = path_row_posting(index, removed_id, false,
                                                &removed_class);
        if (removed_class != PATH_MISS &&
            (!removed || !posting_remove(index, removed, removed_row))) {
            space_path_indexes_retire(space);
            return;
        }
        if (removed_class == PATH_KEY)
            index->keyed_len--;
        else if (removed_class == PATH_UNCERTAIN)
            index->uncertain_len--;
        if (moved_from != removed_row) {
            PathProjection moved_class;
            PathPosting *moved = path_row_posting(index, moved_id, false,
                                                  &moved_class);
            if (moved_class != PATH_MISS) {
                if (!moved || !posting_remove(index, moved, moved_from)) {
                    space_path_indexes_retire(space);
                    return;
                }
                moved = path_row_posting(index, moved_id, true, &moved_class);
                posting_insert(index, moved, removed_row);
            }
        }
        index->source_len--;
    }
}

/* A posting's surviving rows, renumbered in place; false when a row lies
 * outside the contraction. */
static bool posting_transport(CettaSpacePathIndex *index, PathPosting *posting,
                              const CettaIndex *source_to_target,
                              CettaCount source_len, CettaCount *removed) {
    if (posting->len == 0u)
        return true;
    if (posting->len == 1u) {
        CettaIndex source = posting->positions.single;
        if (source >= source_len)
            return false;
        CettaIndex target = source_to_target[source];
        if (target == SPACE_OCCURRENCE_COORDINATE_REMOVED) {
            posting->len = 0u;
            posting_set_cap(posting, PATH_POSTING_EMPTIED);
            (*removed)++;
        } else {
            posting->positions.single = target;
        }
        return true;
    }
    CettaIndex *items = posting->positions.many;
    CettaCount write = 0u;
    for (CettaCount read = 0u; read < posting->len; read++) {
        if (items[read] >= source_len)
            return false;
        CettaIndex target = source_to_target[items[read]];
        if (target == SPACE_OCCURRENCE_COORDINATE_REMOVED) {
            (*removed)++;
            continue;
        }
        items[write++] = target;
    }
    posting->len = write;
    if (write <= 1u) {
        CettaIndex only = write ? items[0] : 0u;
        size_t freed = (size_t)posting_cap(posting) * sizeof(*items);
        free(items);
        index->bytes -= freed;
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_FREE_BYTES,
                               freed);
        posting->positions.single = only;
        posting_set_cap(posting, write ? 0u : PATH_POSTING_EMPTIED);
    }
    return true;
}

void space_path_indexes_transport(Space *space, const CettaIndex *source_to_target,
                                  CettaCount source_len, CettaCount target_len) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->source_len != source_len || (source_len && !source_to_target) ||
            index->universe != space->native.universe ||
            index->universe_instance != space->native.universe->instance_id ||
            index->prefix_epoch != space->prefix_epoch ||
            index->universe_epoch != space->native.universe->storage_epoch) {
            space_path_indexes_retire(space);
            return;
        }
        bool kept = true;
        CettaCount removed = 0u;
        for (size_t slot = 0u; kept && slot < index->key_cap; slot++)
            kept = posting_transport(index, &index->keys[slot],
                                     source_to_target, source_len, &removed);
        index->keyed_len -= removed;
        removed = 0u;
        kept = kept && posting_transport(index, &index->uncertain,
                                         source_to_target, source_len, &removed);
        for (size_t slot = 0u; kept && slot < index->uncertain_head_cap; slot++)
            kept = posting_transport(index, &index->uncertain_heads[slot],
                                     source_to_target, source_len, &removed);
        index->uncertain_len -= removed;
        if (!kept) {
            space_path_indexes_retire(space);
            return;
        }
        index->source_len = target_len;
    }
}

/* A row marked removed stays in the position indexes until the marks
 * settle.  While rows are marked the indexes have one reader, the existing
 * candidates, whose caller contracts the rows it answers to logical
 * positions and so drops the marked ones; every other reader declines a
 * marked space.  The settle's transport drops the marked rows from every
 * posting in one pass and corrects the counts of keyed and open rows.
 * Taking each row out at its mark shifted the posting's later rows down a
 * slot, so removing a long posting's rows in ascending order cost its
 * length squared. */
void space_path_indexes_note_mark(Space *space, CettaIndex row, AtomId id) {
    (void)row;
    (void)id;
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->source_len != space->native.len ||
            index->universe != space->native.universe ||
            index->prefix_epoch != space->prefix_epoch) {
            space_path_indexes_retire(space);
            return;
        }
    }
}

void space_head_index_adopt_prefix_epoch(Space *space, uint64_t followed);

void space_path_indexes_adopt_prefix_epoch(Space *space, uint64_t followed) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->prefix_epoch == followed &&
            index->source_len == space->native.len)
            index->prefix_epoch = space->prefix_epoch;
    }
    space_head_index_adopt_prefix_epoch(space, followed);
}

static void path_index_free(CettaSpacePathIndex *index) {
    for (size_t slot = 0u; slot < index->key_cap; slot++) {
        if (index->keys[slot].len > 1u)
            free(index->keys[slot].positions.many);
    }
    if (index->uncertain.len > 1u)
        free(index->uncertain.positions.many);
    path_free_table(index->uncertain_heads, index->uncertain_head_cap);
    free(index->keys);
    free(index->path);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_FREE_BYTES,
                           index->bytes);
    free(index);
}

void space_path_indexes_retire_prefixes(Space *space) {
    CettaSpacePathIndex **link = &space->match_backend.native.path_indexes;
    while (*link) {
        CettaSpacePathIndex *index = *link;
        if (index->kind != CETTA_SPACE_PATH_PREFIX_KEY) {
            link = &index->next;
            continue;
        }
        *link = index->next;
        path_index_free(index);
    }
}

void space_path_indexes_retire(Space *space) {
    CettaSpacePathShape *shape = space->match_backend.native.path_shapes;
    space->match_backend.native.path_shapes = NULL;
    while (shape) {
        CettaSpacePathShape *next = shape->next;
        free(shape);
        shape = next;
    }
    CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
    space->match_backend.native.path_indexes = NULL;
    while (index) {
        CettaSpacePathIndex *next = index->next;
        path_index_free(index);
        index = next;
    }
}

bool space_path_indexes_info(const Space *space, CettaSpacePathIndexInfo *out) {
    if (!space || !out || !space->match_backend.native.path_indexes)
        return false;
    const CettaSpacePathIndex *first = space->match_backend.native.path_indexes;
    *out = (CettaSpacePathIndexInfo){
        .source_rows = first->source_len, .keyed_rows = first->keyed_len,
        .uncertain_rows = first->uncertain_len, .distinct_keys = first->key_len,
        .path_length = first->path_len, .key_kind = first->kind,
        .universe_instance = first->universe_instance,
        .universe_epoch = first->universe_epoch,
        .prefix_epoch = first->prefix_epoch,
    };
    for (const CettaSpacePathIndex *index = first; index; index = index->next) {
        out->index_count++;
        out->retained_bytes += index->bytes;
    }
    return true;
}

/* ── The head index ──────────────────────────────────────────────────── */

typedef struct {
    uint64_t key;
    CettaIndex *rows;
    CettaCount len, cap;
    /* Leading rows known removed by mark. */
    CettaCount front;
} HeadPosting;

struct CettaSpaceHeadIndex {
    TermUniverse *universe;
    uint64_t universe_instance, universe_epoch, prefix_epoch;
    CettaCount source_len;
    /* Postings by id: HEAD_EVERY holds the rows every query reads. */
    HeadPosting *postings;
    uint32_t posting_len, posting_cap;
    /* Open addressing from a key to its posting's id plus one. */
    uint32_t *slots;
    size_t slot_cap, slot_len;
    /* Retired indexes cursors still read, freed with this one. */
    CettaSpaceHeadIndex *next_stale;
};

#define HEAD_EVERY 0u
#define HEAD_NONE UINT32_MAX
#define HEAD_ROW_END ((CettaIndex)-1)

/* A head symbol over `length` elements; SYMBOL_ID_NONE for an open head. */
static uint64_t head_key(SymbolId head, CettaExprLen length) {
    return ((uint64_t)length << 32) | (uint64_t)head;
}

typedef enum {
    HEAD_ROW_NONE = 0, /* no head-selected query meets it */
    HEAD_ROW_KEYED,    /* its head is a symbol */
    HEAD_ROW_OPEN,     /* its head is not a symbol */
    HEAD_ROW_EVERY,    /* a variable, a private carrier or undecoded */
} HeadRowKind;

static HeadRowKind head_row_kind(const TermUniverse *universe, AtomId id,
                                 uint64_t *key) {
    const CettaTermHdr *header = tu_hdr(universe, id);
    if (!header || header->tag == ATOM_VAR)
        return HEAD_ROW_EVERY;
    if (header->tag != ATOM_EXPR)
        return HEAD_ROW_NONE;
    CettaExprLen arity = tu_arity(universe, id);
    if (arity == 0u)
        return HEAD_ROW_NONE;
    /* Private list, callable and compound carriers spell a logical shape
     * other than their encoding. */
    if (tu_internal_tag(universe, tu_child(universe, id, 0u)) != 0 ||
        arity > UINT32_MAX)
        return HEAD_ROW_EVERY;
    *key = head_key(header->sym_or_head, arity);
    return header->sym_or_head != SYMBOL_ID_NONE ? HEAD_ROW_KEYED
                                                 : HEAD_ROW_OPEN;
}

static uint32_t head_posting_find(const CettaSpaceHeadIndex *index,
                                  uint64_t key) {
    if (index->slot_cap == 0u)
        return HEAD_NONE;
    size_t slot = (size_t)path_key_hash(key) & (index->slot_cap - 1u);
    while (index->slots[slot] != 0u) {
        uint32_t id = index->slots[slot] - 1u;
        if (index->postings[id].key == key)
            return id;
        slot = (slot + 1u) & (index->slot_cap - 1u);
    }
    return HEAD_NONE;
}

static uint32_t head_posting_push(CettaSpaceHeadIndex *index, uint64_t key) {
    if (index->posting_len == index->posting_cap) {
        uint32_t next = index->posting_cap ? index->posting_cap * 2u : 8u;
        if (next <= index->posting_cap)
            cetta_oom(SIZE_MAX);
        index->postings = cetta_realloc(index->postings,
                                        (size_t)next * sizeof(*index->postings));
        index->posting_cap = next;
    }
    uint32_t id = index->posting_len++;
    index->postings[id] = (HeadPosting){.key = key};
    return id;
}

static void head_slots_grow(CettaSpaceHeadIndex *index) {
    size_t next = index->slot_cap ? index->slot_cap * 2u : 16u;
    if (next < index->slot_cap || next > SIZE_MAX / sizeof(*index->slots))
        cetta_oom(SIZE_MAX);
    uint32_t *slots = cetta_malloc(next * sizeof(*slots));
    memset(slots, 0, next * sizeof(*slots));
    for (size_t old = 0u; old < index->slot_cap; old++) {
        if (index->slots[old] == 0u)
            continue;
        uint64_t key = index->postings[index->slots[old] - 1u].key;
        size_t slot = (size_t)path_key_hash(key) & (next - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (next - 1u);
        slots[slot] = index->slots[old];
    }
    free(index->slots);
    index->slots = slots;
    index->slot_cap = next;
}

static uint32_t head_posting_get(CettaSpaceHeadIndex *index, uint64_t key) {
    uint32_t id = head_posting_find(index, key);
    if (id != HEAD_NONE)
        return id;
    if (index->slot_cap == 0u ||
        index->slot_len >= index->slot_cap - index->slot_cap / 4u)
        head_slots_grow(index);
    id = head_posting_push(index, key);
    size_t slot = (size_t)path_key_hash(key) & (index->slot_cap - 1u);
    while (index->slots[slot] != 0u)
        slot = (slot + 1u) & (index->slot_cap - 1u);
    index->slots[slot] = id + 1u;
    index->slot_len++;
    return id;
}

static void head_posting_append(HeadPosting *posting, CettaIndex row) {
    if (posting->len == posting->cap) {
        CettaCount next = posting->cap ? posting->cap * 2u : 4u;
        if (next <= posting->cap || next > SIZE_MAX / sizeof(CettaIndex))
            cetta_oom(SIZE_MAX);
        posting->rows = cetta_realloc(posting->rows,
                                      (size_t)next * sizeof(CettaIndex));
        posting->cap = next;
    }
    posting->rows[posting->len++] = row;
}

static void head_index_add_row(CettaSpaceHeadIndex *index, AtomId id,
                               CettaIndex row) {
    uint64_t key = 0u;
    switch (head_row_kind(index->universe, id, &key)) {
    case HEAD_ROW_NONE:
        break;
    case HEAD_ROW_EVERY:
        head_posting_append(&index->postings[HEAD_EVERY], row);
        break;
    case HEAD_ROW_KEYED:
    case HEAD_ROW_OPEN: {
        /* Finding the posting may grow the postings. */
        uint32_t id = head_posting_get(index, key);
        head_posting_append(&index->postings[id], row);
        break;
    }
    }
}

bool space_head_index_admits(const Atom *pattern) {
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        pattern->expr.len > UINT32_MAX ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom_structural_may_have_list_carrier(pattern) ||
        atom_structural_may_have_internal_tag(pattern))
        return false;
    for (CettaExprIndex position = 1u; position < pattern->expr.len; position++) {
        if (pattern->expr.elems[position]->kind != ATOM_VAR)
            return false;
    }
    return true;
}

static bool head_index_current(const Space *space,
                               const CettaSpaceHeadIndex *index) {
    return index->universe == space->native.universe &&
        index->universe_instance == space->native.universe->instance_id &&
        index->universe_epoch == space->native.universe->storage_epoch &&
        index->prefix_epoch == space->prefix_epoch &&
        index->source_len == space->native.len;
}

static CettaSpaceHeadIndex *head_index_build(Space *space) {
    size_t width = cetta_atom_id_storage_width_bytes_from_bits(
        space->native.atom_id_width_bits);
    if (space->native.len != 0u && (width == 0u || !space->native.atom_ids))
        return NULL;
    CettaSpaceHeadIndex *index = cetta_malloc(sizeof(*index));
    *index = (CettaSpaceHeadIndex){
        .universe = space->native.universe,
        .universe_instance = space->native.universe->instance_id,
        .universe_epoch = space->native.universe->storage_epoch,
        .prefix_epoch = space->prefix_epoch,
    };
    (void)head_posting_push(index, 0u);
    /* The rows as they stand.  A row removed by mark is left out: its
     * readers skip it by the marks, and its term a collection may since have
     * reclaimed, so it is not read. */
    const uint64_t *marks = space->native.marked ? space->native.marks : NULL;
    for (CettaIndex row = 0u; row < space->native.len; row++) {
        if (marks && row < space->native.mark_rows &&
            ((marks[row >> 6] >> (row & 63u)) & 1u) != 0u)
            continue;
        AtomId id = cetta_atom_id_storage_load_bits(
            space->native.atom_ids + (size_t)row * width,
            space->native.atom_id_width_bits);
        head_index_add_row(index, id, row);
    }
    index->source_len = space->native.len;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_HEAD_INDEX_BUILD);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_SPACE_HEAD_INDEX_BUILD_ROW,
                            space->native.len);
    return index;
}

bool space_head_index_open(Space *space, const Atom *pattern,
                           CettaSpaceHeadCursor *cursor) {
    space_dense_settle_for_pattern(space, pattern);
    memset(cursor, 0, sizeof(*cursor));
    if (!space || !space->native.universe || space->overlay_base ||
        (space->kind != SPACE_KIND_ATOM && space->kind != SPACE_KIND_SET) ||
        space->native.start != 0u ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !space_head_index_admits(pattern))
        return false;
    SpaceMatchNativeState *state = &space->match_backend.native;
    if (state->head_index && !head_index_current(space, state->head_index))
        space_head_index_retire(space);
    if (!state->head_index)
        state->head_index = head_index_build(space);
    CettaSpaceHeadIndex *index = state->head_index;
    if (!index)
        return false;
    CettaExprLen length = pattern->expr.len;
    cursor->index = index;
    cursor->keyed = head_posting_find(
        index, head_key(pattern->expr.elems[0]->sym_id, length));
    cursor->open = head_posting_find(index, head_key(SYMBOL_ID_NONE, length));
    cursor->keyed_next = cursor->keyed != HEAD_NONE
        ? index->postings[cursor->keyed].front : 0u;
    cursor->open_next = cursor->open != HEAD_NONE
        ? index->postings[cursor->open].front : 0u;
    cursor->every_next = index->postings[HEAD_EVERY].front;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_HEAD_INDEX_CURSOR);
    return true;
}

static inline CettaIndex head_posting_peek(const CettaSpaceHeadIndex *index,
                                           uint32_t id, CettaCount next) {
    if (id == HEAD_NONE || next >= index->postings[id].len)
        return HEAD_ROW_END;
    return index->postings[id].rows[next];
}

bool space_head_index_next(CettaSpaceHeadCursor *cursor, CettaIndex ceiling,
                           const uint64_t *marks, CettaIndex mark_rows,
                           CettaIndex *row_out) {
    CettaSpaceHeadIndex *index = cursor->index;
    if (!index)
        return false;
    for (;;) {
        CettaIndex keyed = head_posting_peek(index, cursor->keyed,
                                             cursor->keyed_next);
        CettaIndex open = head_posting_peek(index, cursor->open,
                                            cursor->open_next);
        CettaIndex every = head_posting_peek(index, HEAD_EVERY,
                                             cursor->every_next);
        uint32_t from;
        CettaCount *next;
        CettaIndex row;
        if (keyed <= open && keyed <= every) {
            from = cursor->keyed;
            next = &cursor->keyed_next;
            row = keyed;
        } else if (open <= every) {
            from = cursor->open;
            next = &cursor->open_next;
            row = open;
        } else {
            from = HEAD_EVERY;
            next = &cursor->every_next;
            row = every;
        }
        /* Each posting is in row order, so the least row past the ceiling
         * ends all three. */
        if (row == HEAD_ROW_END || row >= ceiling)
            return false;
        bool removed = marks && row < mark_rows &&
            ((marks[row >> 6] >> (row & 63u)) & 1u) != 0u;
        HeadPosting *posting = &index->postings[from];
        if (removed && *next == posting->front)
            posting->front++;
        (*next)++;
        if (removed)
            continue;
        *row_out = row;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_HEAD_INDEX_ROW);
        return true;
    }
}

void space_head_index_note_add(Space *space, AtomId id, CettaIndex row) {
    CettaSpaceHeadIndex *index = space->match_backend.native.head_index;
    if (!index)
        return;
    if (index->source_len != row || index->prefix_epoch != space->prefix_epoch) {
        space_head_index_retire(space);
        return;
    }
    head_index_add_row(index, id, row);
    index->source_len++;
}

void space_head_index_adopt_prefix_epoch(Space *space, uint64_t followed) {
    CettaSpaceHeadIndex *index = space->match_backend.native.head_index;
    if (index && index->prefix_epoch == followed &&
        index->source_len == space->native.len)
        index->prefix_epoch = space->prefix_epoch;
}

void space_head_index_free(CettaSpaceHeadIndex *index) {
    while (index) {
        CettaSpaceHeadIndex *next = index->next_stale;
        for (uint32_t id = 0u; id < index->posting_len; id++)
            free(index->postings[id].rows);
        free(index->postings);
        free(index->slots);
        free(index);
        index = next;
    }
}

void space_head_index_retire(Space *space) {
    SpaceMatchNativeState *state = &space->match_backend.native;
    CettaSpaceHeadIndex *index = state->head_index;
    if (!index)
        return;
    state->head_index = NULL;
    /* Cursors pinned to the Space's rows may be reading it. */
    if (state->match_trie_pins == 0u) {
        space_head_index_free(index);
        return;
    }
    index->next_stale = state->stale_head_indexes;
    state->stale_head_indexes = index;
}

CettaSpaceHeadIndex *space_head_index_take(Space *space) {
    SpaceMatchNativeState *state = &space->match_backend.native;
    CettaSpaceHeadIndex *index = state->head_index;
    state->head_index = NULL;
    if (!index) {
        index = state->stale_head_indexes;
    } else {
        index->next_stale = state->stale_head_indexes;
    }
    state->stale_head_indexes = NULL;
    return index;
}
