#ifndef CETTA_SPACE_PATH_INDEX_H
#define CETTA_SPACE_PATH_INDEX_H

#include "atom.h"
#include "term_universe.h"

typedef struct Space Space;
typedef struct CettaSpacePathIndex CettaSpacePathIndex;

typedef enum {
    CETTA_SPACE_PATH_SYMBOL_KEY = 0,
    CETTA_SPACE_PATH_INTEGER_KEY,
} CettaSpacePathKeyKind;

/* A path is a vector of expression-child positions. Each index belongs to
 * one Space and one universe generation. It refutes candidates only; matching
 * still determines bindings, promotion, correlation and opaque equality.
 * Posting lists contain source occurrence positions, never representatives
 * of equal payloads. Queries merge a key's postings and uncertain rows in
 * ascending position order. Appends extend the index; prefix rewrites retire
 * it. Cursors own their selected positions independently of that retirement. */
typedef struct {
    size_t index_count;
    size_t retained_bytes;
    CettaCount source_rows;
    CettaCount keyed_rows;
    CettaCount uncertain_rows;
    CettaCount distinct_keys;
    size_t path_length;
    CettaSpacePathKeyKind key_kind;
    uint64_t universe_instance;
    uint64_t universe_epoch;
    uint64_t prefix_epoch;
} CettaSpacePathIndexInfo;

/* A free leading argument leaves the head trie only the head to select by;
 * a bound value after it is then the selective access, read before any cold
 * scan. A bound leading argument is also read from its position's index when
 * nothing after it is bound: one bucket, where the trie walks and collects. */
static inline bool space_path_index_leads(const Atom *pattern) {
    return pattern && pattern->kind == ATOM_EXPR && pattern->expr.len > 2u &&
        pattern->expr.elems[1] && pattern->expr.elems[1]->kind == ATOM_VAR;
}

/* A scalar prefix needs no second access path. Keep this admission test a
 * leaf; only nested patterns enter the explicit coordinate walker. */
static inline bool space_path_index_may_select(const Atom *pattern) {
    if (!pattern || pattern->kind != ATOM_EXPR || !atom_has_vars(pattern))
        return false;
    /* A two-coordinate bound prefix followed by one free scalar: the bound
     * field's position index answers it when that field is a symbol or an
     * integer; a ground nested prefix stays with the trie. */
    if (pattern->expr.len == 3u && pattern->expr.elems[2] &&
        pattern->expr.elems[2]->kind == ATOM_VAR &&
        !atom_has_vars(pattern->expr.elems[0]) &&
        !atom_has_vars(pattern->expr.elems[1]))
        return pattern->expr.elems[1]->kind == ATOM_SYMBOL ||
            (pattern->expr.elems[1]->kind == ATOM_GROUNDED &&
             pattern->expr.elems[1]->ground.gkind == GV_INT);
    for (CettaExprIndex position = 0u; position < pattern->expr.len; position++) {
        const Atom *child = pattern->expr.elems[position];
        if (!atom_has_vars(child))
            continue;
        if (child->kind == ATOM_EXPR)
            return true;
        if (child->kind != ATOM_VAR)
            continue;
        /* Only the suffix after the first free coordinate can provide a
         * secondary path. A bound scalar prefix needs no per-item tests for
         * coordinates which cannot be secondary keys. */
        for (CettaExprIndex suffix = position + 1u;
             suffix < pattern->expr.len; suffix++) {
            child = pattern->expr.elems[suffix];
            if (child && (child->kind == ATOM_EXPR || child->kind == ATOM_SYMBOL ||
                    (child->kind == ATOM_GROUNDED && child->ground.gkind == GV_INT)))
                return true;
        }
        return false;
    }
    return false;
}

bool space_path_index_candidates(
    Space *space, Atom *pattern, CettaIndex **out, CettaIndex *count);
void space_path_indexes_note_add(Space *space, AtomId id, CettaIndex position);
/* Candidates from an index that already serves this pattern's shape, as
 * physical rows, without building one and only when no open row needs
 * refuting; false otherwise. Rows removed by mark have left the postings. */
bool space_path_index_existing_candidates(Space *space, Atom *pattern,
                                          CettaIndex **out, CettaIndex *count);
void space_path_indexes_note_swap_remove(Space *space, CettaIndex removed_row,
                                         AtomId removed_id, CettaIndex moved_from,
                                         AtomId moved_id);
/* A stable contraction of the space's rows: every index keeps its surviving
 * rows, renumbered by the contraction's monotone map (removed rows map to
 * SPACE_OCCURRENCE_COORDINATE_REMOVED); an index it cannot keep is retired. */
void space_path_indexes_transport(Space *space, const CettaIndex *source_to_target,
                                  CettaCount source_len, CettaCount target_len);
/* A row removed by mark leaves its posting; rows keep their positions until
 * the contraction that settles the marks renumbers them. */
void space_path_indexes_note_mark(Space *space, CettaIndex row, AtomId id);
/* After the prefix rewrite of a removal every index followed, the indexes
 * stamped `followed` take the Space's new prefix epoch. */
void space_path_indexes_adopt_prefix_epoch(Space *space, uint64_t followed);
void space_path_indexes_retire(Space *space);
/* Aggregate storage account, with the most recently created path's record.
 * False means this Space retains no path index. */
bool space_path_indexes_info(const Space *space, CettaSpacePathIndexInfo *out);

#endif
