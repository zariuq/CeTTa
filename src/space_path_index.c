#include "space_path_index.h"

#include "space.h"
#include "stats.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t key;
    CettaCount len, cap;
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

static inline bool posting_occupied(const PathPosting *posting) {
    return posting->len != 0u || posting->cap != 0u;
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
        posting->cap = 4u;
        posting->positions.many = cetta_malloc(4u * sizeof(CettaIndex));
        path_charge(index, 4u * sizeof(CettaIndex));
        posting->positions.many[0] = first;
        posting->positions.many[1] = position;
    } else {
        if (posting->len == posting->cap) {
            if (posting->cap > SIZE_MAX / sizeof(CettaIndex) / 2u)
                cetta_oom(SIZE_MAX);
            CettaCount next = posting->cap * 2u;
            posting->positions.many = cetta_realloc(posting->positions.many,
                (size_t)next * sizeof(CettaIndex));
            path_charge(index, (size_t)(next - posting->cap) * sizeof(CettaIndex));
            posting->cap = next;
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
static PathPosting *path_row_posting(CettaSpacePathIndex *index, AtomId id,
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
        posting->cap = PATH_POSTING_EMPTIED;
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
        size_t freed = (size_t)posting->cap * sizeof(*items);
        free(items);
        index->bytes -= freed;
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_FREE_BYTES,
                               freed);
        posting->positions.single = only;
        posting->cap = 0u;
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

typedef struct { Atom *term; CettaExprIndex child; } PathQueryFrame;

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
            if (path_query_key(pattern->expr.elems[position], &kind, key)) {
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
        for (index = space->match_backend.native.path_indexes; index;
             index = index->next) {
            if (index->kind != kind || index->path_len != depth ||
                index->head != index_head)
                continue;
            size_t position = 0u;
            while (position < depth && index->path[position] ==
                   (selected_head
                       ? head_path[position] : frames[position].child))
                position++;
            if (position == depth)
                break;
        }
        if (!index) {
            index = cetta_malloc(sizeof(*index));
            *index = (CettaSpacePathIndex){
                .next = space->match_backend.native.path_indexes,
                .universe = space->native.universe,
                .universe_instance = space->native.universe->instance_id,
                .universe_epoch = space->native.universe->storage_epoch,
                .prefix_epoch = space->prefix_epoch,
                .path_len = depth, .kind = kind, .head = index_head,
            };
            index->path = cetta_malloc(depth * sizeof(*index->path));
            path_charge(index, sizeof(*index) + depth * sizeof(*index->path));
            for (size_t position = 0u; position < depth; position++)
                index->path[position] = selected_head
                    ? head_path[position] : frames[position].child;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BUILD);
            for (CettaIndex position = 0u; position < space->native.len; position++) {
                path_append_row(index, space_get_atom_id_at64(space, position), position);
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BUILD_ROW);
            }
            space->match_backend.native.path_indexes = index;
        }
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

bool space_path_index_candidates(Space *space, Atom *pattern,
                                CettaIndex **out, CettaIndex *count) {
    if (!space || !space->native.universe || !out || !count || space->overlay_base ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !space_path_index_may_select(pattern))
        return false;
    if (!path_generation_current(space))
        space_path_indexes_retire(space);
    uint64_t key = 0u;
    uint64_t shape = path_flat_shape(pattern);
    CettaSpacePathIndex *index = NULL;
    SymbolId pattern_head = pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? pattern->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    for (CettaSpacePathIndex *known = shape ? space->match_backend.native.path_indexes : NULL;
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
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_LOOKUP);
    PathPosting *posting = index->key_cap ? path_key_slot(index, key) : NULL;
    CettaCount known = posting ? posting->len : 0u;
    SymbolId query_head = pattern->expr.len != 0u &&
            pattern->expr.elems[0] && pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? pattern->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    /* A query with a head symbol reads the open rows of that head; one
     * without reads every open row. */
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
    CettaCount candidates = known + index->uncertain.len + headed_len;
    if (candidates >= space->native.len - space->native.len / 4u) {
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

bool space_path_index_existing_candidates(Space *space, Atom *pattern,
                                          CettaIndex **out, CettaIndex *count) {
    if (!space || !out || !count || !space_path_index_may_select(pattern) ||
        !path_generation_current(space))
        return false;
    uint64_t shape = path_flat_shape(pattern);
    if (!shape)
        return false;
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->shape != shape || index->path_len != 1u ||
            (index->head != SYMBOL_ID_NONE &&
             (pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
              index->head != pattern->expr.elems[0]->sym_id)))
            continue;
        uint64_t key = 0u;
        CettaSpacePathKeyKind kind;
        if (!path_query_key(pattern->expr.elems[index->path[0]], &kind, &key) ||
            kind != index->kind || index->uncertain_len != 0u)
            return false;
        PathPosting *posting = index->key_cap ? path_key_slot(index, key) : NULL;
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
    return false;
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
            posting->cap = PATH_POSTING_EMPTIED;
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
        size_t freed = (size_t)posting->cap * sizeof(*items);
        free(items);
        index->bytes -= freed;
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_FREE_BYTES,
                               freed);
        posting->positions.single = only;
        posting->cap = write ? 0u : PATH_POSTING_EMPTIED;
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

void space_path_indexes_note_mark(Space *space, CettaIndex row, AtomId id) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->source_len != space->native.len ||
            index->universe != space->native.universe ||
            index->prefix_epoch != space->prefix_epoch) {
            space_path_indexes_retire(space);
            return;
        }
        PathProjection projection;
        PathPosting *posting = path_row_posting(index, id, false, &projection);
        if (projection == PATH_MISS)
            continue;
        if (!posting || !posting_remove(index, posting, row)) {
            space_path_indexes_retire(space);
            return;
        }
        if (projection == PATH_KEY)
            index->keyed_len--;
        else
            index->uncertain_len--;
    }
}

void space_path_indexes_adopt_prefix_epoch(Space *space, uint64_t followed) {
    for (CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
         index; index = index->next) {
        if (index->prefix_epoch == followed &&
            index->source_len == space->native.len)
            index->prefix_epoch = space->prefix_epoch;
    }
}

void space_path_indexes_retire(Space *space) {
    CettaSpacePathIndex *index = space->match_backend.native.path_indexes;
    space->match_backend.native.path_indexes = NULL;
    while (index) {
        CettaSpacePathIndex *next = index->next;
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
