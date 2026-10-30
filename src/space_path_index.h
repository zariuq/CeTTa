#ifndef CETTA_SPACE_PATH_INDEX_H
#define CETTA_SPACE_PATH_INDEX_H

#include "atom.h"
#include "term_universe.h"

typedef struct Space Space;
typedef struct CettaSpacePathIndex CettaSpacePathIndex;

typedef enum {
    CETTA_SPACE_PATH_SYMBOL_KEY = 0,
    CETTA_SPACE_PATH_INTEGER_KEY,
    CETTA_SPACE_PATH_STRING_HASH_KEY,
    /* A ground term keyed by its universe id. */
    CETTA_SPACE_PATH_TERM_KEY,
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

/* Whether a column's value is keyed by its term: a ground term whose leaves
 * are all symbols, a nested constant such as a level (S (S Z)).  Equal such
 * terms are one id, and only such a term can match it, so its id selects the
 * rows exactly, where the trie would hold every row's term and cuts a deep
 * one to a wildcard. */
bool space_path_index_term_key_admits(const Atom *element);

/* A scalar prefix needs no second access path. Keep this admission test a
 * leaf; only nested patterns enter the explicit coordinate walker. */
static inline bool space_path_index_may_select(const Atom *pattern) {
    if (!pattern || pattern->kind != ATOM_EXPR || !atom_has_vars(pattern))
        return false;
    for (CettaExprIndex position = 1u; position < pattern->expr.len; position++) {
        const Atom *child = pattern->expr.elems[position];
        if (child && child->kind == ATOM_EXPR &&
            space_path_index_term_key_admits(child))
            return true;
    }
    /* A two-coordinate bound prefix followed by one free scalar: the bound
     * field's position index answers it when that field is a symbol, an
     * integer or a string; a ground nested prefix stays with the trie. */
    if (pattern->expr.len == 3u && pattern->expr.elems[2] &&
        pattern->expr.elems[2]->kind == ATOM_VAR &&
        !atom_has_vars(pattern->expr.elems[0]) &&
        pattern->expr.elems[1] && !atom_has_vars(pattern->expr.elems[1])) {
        /* A string field is distinguished only by the maintained index: the
         * primary trie keeps strings on its uncertain branch. */
        const Atom *field = pattern->expr.elems[1];
        return field->kind == ATOM_SYMBOL ||
            (field->kind == ATOM_GROUNDED &&
             (field->ground.gkind == GV_INT || field->ground.gkind == GV_STRING));
    }
    for (CettaExprIndex position = 0u; position < pattern->expr.len; position++) {
        const Atom *child = pattern->expr.elems[position];
        /* The primary trie puts strings on its uncertain branch. A rigid
         * string prefix therefore needs the same maintained path index as
         * a rigid coordinate after a variable. Hash collisions remain
         * candidates and are rejected only by the exact matcher. */
        if (child && child->kind == ATOM_GROUNDED && child->ground.gkind == GV_STRING)
            return true;
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
                    (child->kind == ATOM_GROUNDED &&
                     (child->ground.gkind == GV_INT || child->ground.gkind == GV_STRING))))
                return true;
        }
        return false;
    }
    return false;
}

bool space_path_index_candidates(
    Space *space, Atom *pattern, CettaIndex **out, CettaIndex *count,
    CettaIndex *capacity);
/* The rows of a pattern's head that its first bound column after the head
 * keys (a symbol, an integer or a ground symbolic term) admits, from that
 * column's index: an exact count's candidates, however broad, with no trie.
 * The index is built when `build` asks; false when no column is keyed or
 * the Space keeps no such index. */
bool space_path_index_lead_candidates(Space *space, Atom *pattern, bool build,
                                      CettaIndex **out, CettaIndex *count);
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

/* The head index: for each head symbol and expression length, the rows in
 * row order, as a Prolog system keeps each predicate's clauses.  A query
 * that selects by its head alone, a symbol over variables, reads the rows
 * of its head and length merged in row order with the rows any such query
 * may meet: rows of that length whose head is open, and rows that are
 * variables, private carriers or undecoded.  It reads them lazily, so the
 * first match costs no more than the rows before it.
 *
 * The index holds rows, not logical positions.  A removal by mark leaves
 * the rows in place; readers skip the marked ones, and each posting keeps
 * the count of its leading rows known removed, so a drain from the front
 * passes each removed row once.  Appends extend it; any other change to
 * the rows retires it.  It refutes candidates only; matching decides. */
typedef struct CettaSpaceHeadIndex CettaSpaceHeadIndex;

typedef struct {
    CettaSpaceHeadIndex *index;
    /* Positions in the head's posting, the open rows of its length and the
     * rows every query reads; a posting that does not exist is empty. */
    uint32_t keyed, open;
    CettaCount keyed_next, open_next, every_next;
} CettaSpaceHeadCursor;

/* Whether `pattern` selects by its head alone: a symbol head over
 * variables, with no carrier inside. */
bool space_head_index_admits(const Atom *pattern);
/* Open a cursor over the Space's head index for an admitted pattern,
 * building the index first when the Space has none current.  False when
 * the Space keeps no such index. */
bool space_head_index_open(Space *space, const Atom *pattern,
                           CettaSpaceHeadCursor *cursor);
/* The cursor's next row below `ceiling` that `marks` (a bitmap over
 * [0, mark_rows), NULL when none) does not mark; false at the end. */
bool space_head_index_next(CettaSpaceHeadCursor *cursor, CettaIndex ceiling,
                           const uint64_t *marks, CettaIndex mark_rows,
                           CettaIndex *row_out);
void space_head_index_note_add(Space *space, AtomId id, CettaIndex row);
/* After the prefix rewrite of a removal by mark, which moves no row. */
void space_head_index_adopt_prefix_epoch(Space *space, uint64_t followed);
/* Retire the Space's head index: freed at once, or, while cursors are
 * pinned to the Space's rows, kept for them and freed once none is. */
void space_head_index_retire(Space *space);
/* Detach the Space's head index for a pinned view that takes its rows. */
CettaSpaceHeadIndex *space_head_index_take(Space *space);
void space_head_index_free(CettaSpaceHeadIndex *index);

#endif
