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
    PathPosting uncertain;
    CettaCount source_len, keyed_len;
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

static PathPosting *path_key_slot(CettaSpacePathIndex *index, uint64_t key) {
    size_t slot = (size_t)path_key_hash(key) & (index->key_cap - 1u);
    while (index->keys[slot].len != 0u && index->keys[slot].key != key) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_KEY_PROBE);
        slot = (slot + 1u) & (index->key_cap - 1u);
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_KEY_PROBE);
    return &index->keys[slot];
}

static void path_grow_keys(CettaSpacePathIndex *index) {
    size_t next = index->key_cap ? index->key_cap * 2u : 16u;
    if (next < index->key_cap || next > SIZE_MAX / sizeof(PathPosting))
        cetta_oom(SIZE_MAX);
    PathPosting *old = index->keys;
    size_t old_cap = index->key_cap;
    index->keys = cetta_malloc(next * sizeof(*index->keys));
    memset(index->keys, 0, next * sizeof(*index->keys));
    index->key_cap = next;
    path_charge(index, (next - old_cap) * sizeof(*index->keys));
    for (size_t slot = 0u; slot < old_cap; slot++) {
        if (old[slot].len != 0u)
            *path_key_slot(index, old[slot].key) = old[slot];
    }
    free(old);
}

typedef enum { PATH_MISS, PATH_KEY, PATH_UNCERTAIN } PathProjection;

static PathProjection path_project(const CettaSpacePathIndex *index,
                                   AtomId id, uint64_t *key) {
    const TermUniverse *universe = index->universe;
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

static void path_append_row(CettaSpacePathIndex *index, AtomId id,
                            CettaIndex position) {
    uint64_t key = 0u;
    PathProjection projection = path_project(index, id, &key);
    if (projection == PATH_UNCERTAIN) {
        posting_append(index, &index->uncertain, position);
    } else if (projection == PATH_KEY) {
        if (index->key_cap == 0u ||
            index->key_len >= index->key_cap - index->key_cap / 4u)
            path_grow_keys(index);
        PathPosting *posting = path_key_slot(index, key);
        if (posting->len == 0u) {
            posting->key = key;
            index->key_len++;
        }
        posting_append(index, posting, position);
        index->keyed_len++;
    }
    index->source_len++;
}

static bool path_query_key(const Atom *atom, CettaSpacePathKeyKind *kind,
                            uint64_t *key) {
    if (atom && atom->kind == ATOM_SYMBOL) {
        SymbolId id = atom_symbol_current_id(atom);
        if (id == SYMBOL_ID_NONE && atom_symbol_is_spelled(atom))
            return false;
        *kind = CETTA_SPACE_PATH_SYMBOL_KEY;
        *key = id;
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
    bool selected_head = !found && head_path;
    if (selected_head) {
        depth = head_depth;
        kind = head_kind;
        *key = head_key;
        found = true;
    }
    CettaSpacePathIndex *index = NULL;
    if (found) {
        for (index = space->match_backend.native.path_indexes; index;
             index = index->next) {
            if (index->kind != kind || index->path_len != depth)
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
                .path_len = depth, .kind = kind,
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

bool space_path_index_candidates(Space *space, Atom *pattern,
                                CettaIndex **out, CettaIndex *count,
                                CettaIndex *capacity) {
    if (!space || !space->native.universe || !out || !count || space->overlay_base ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !space_path_index_may_select(pattern))
        return false;
    if (!path_generation_current(space))
        space_path_indexes_retire(space);
    uint64_t key = 0u;
    CettaSpacePathIndex *index = path_demand(space, pattern, &key);
    if (!index)
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_LOOKUP);
    PathPosting *posting = index->key_cap ? path_key_slot(index, key) : NULL;
    CettaCount known = posting ? posting->len : 0u;
    CettaCount candidates = known + index->uncertain.len;
    if (candidates >= space->native.len - space->native.len / 4u) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BROAD_DECLINE);
        return false;
    }
    if (candidates > SIZE_MAX / sizeof(CettaIndex))
        cetta_oom(SIZE_MAX);
    CettaIndex *positions = candidates
        ? cetta_malloc((size_t)candidates * sizeof(*positions)) : NULL;
    CettaCount exact_next = 0u, uncertain_next = 0u;
    CettaCount retained = 0u;
    SymbolId query_head = pattern->expr.len != 0u &&
            pattern->expr.elems[0] && pattern->expr.elems[0]->kind == ATOM_SYMBOL
        ? atom_symbol_current_id(pattern->expr.elems[0]) : SYMBOL_ID_NONE;
    for (CettaCount position = 0u; position < candidates; position++) {
        CettaIndex source_position;
        bool uncertain;
        if (uncertain_next == index->uncertain.len ||
            (exact_next < known && posting_at(posting, exact_next) <
                 posting_at(&index->uncertain, uncertain_next))) {
            source_position = posting_at(posting, exact_next++);
            uncertain = false;
        } else {
            source_position = posting_at(&index->uncertain, uncertain_next++);
            uncertain = true;
        }
        /* Uncertainty at the selected field does not erase a rigid outer
         * symbol. Refute its clash without entering the full matcher; variable
         * heads, tag-headed private carriers and unrecorded rows stay candidates.
         * Keyed postings keep their existing canonical check without a second
         * row read. TermPathPostingRefinement proves that selectively refining
         * uncertain postings preserves the ordered matching occurrences. */
        if (uncertain && query_head != SYMBOL_ID_NONE) {
            const CettaTermHdr *header = tu_hdr(space->native.universe,
                space_get_atom_id_at64(space, source_position));
            if (header && header->tag == ATOM_EXPR &&
                header->sym_or_head != SYMBOL_ID_NONE &&
                header->sym_or_head != query_head)
                continue;
        }
        positions[retained++] = source_position;
    }
    *out = positions;
    *count = retained;
    if (capacity)
        *capacity = candidates;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_CANDIDATE,
                           retained);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_NATIVE_CANDIDATES, retained);
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
        .uncertain_rows = first->uncertain.len, .distinct_keys = first->key_len,
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
