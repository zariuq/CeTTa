#ifndef CETTA_VAR_INDEX_H
#define CETTA_VAR_INDEX_H

#include "atom.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A table from variable identifiers to positions in a list its owner keeps
 * (VariableIndex.lean).  The table only proposes a position; the owner's
 * list confirms it: the variable is there when the position is below the
 * list's length and the list holds the variable at it.  So a list that is
 * cut back, or emptied and refilled, keeps its table: old entries propose
 * positions the list no longer confirms.
 *
 * An owner scans its list while the list is short, up to
 * CETTA_VAR_INDEX_SCAN variables, and asks the table past that.  When the
 * list first grows past the scan it records every variable it holds, and
 * then each one it appends; when the table would pass half full, the owner
 * records its live variables afresh in a larger table, which drops the old
 * entries.  Each lookup and insertion then costs a constant on average. */

enum { CETTA_VAR_INDEX_SCAN = 16u };

typedef struct {
    VarId id;
    uint32_t position;
} CettaVarIndexSlot;

typedef struct {
    CettaVarIndexSlot *slots;
    size_t cap;   /* a power of two, or zero before the table is made */
    size_t used;  /* slots holding an entry */
} CettaVarIndex;

static inline void cetta_var_index_init(CettaVarIndex *index) {
    index->slots = NULL;
    index->cap = 0u;
    index->used = 0u;
}

static inline void cetta_var_index_free(CettaVarIndex *index) {
    free(index->slots);
    cetta_var_index_init(index);
}

static inline size_t cetta_var_index_hash(VarId id) {
    /* A bijective mix of the packed epoch and base, as bindings use, before
     * the table takes its low bits. */
    uint64_t x = (uint64_t)id;
    x ^= x >> 24;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 29;
    return (size_t)x;
}

/* An empty table with room for `expected` entries before it is half full. */
static inline bool cetta_var_index_reset(CettaVarIndex *index,
                                         size_t expected) {
    if (expected > SIZE_MAX / 2u)
        return false;
    size_t cap = 32u;
    while (cap < expected * 2u) {
        if (cap > SIZE_MAX / 4u)
            return false;
        cap *= 2u;
    }
    if (cap != index->cap) {
        CettaVarIndexSlot *slots = calloc(cap, sizeof(*slots));
        if (!slots)
            return false;
        free(index->slots);
        index->slots = slots;
        index->cap = cap;
    } else {
        for (size_t slot = 0u; slot < cap; slot++)
            index->slots[slot] = (CettaVarIndexSlot){0};
    }
    index->used = 0u;
    return true;
}

/* Whether one more entry keeps the table at most half full. */
static inline bool cetta_var_index_has_room(const CettaVarIndex *index) {
    return index->slots && (index->used + 1u) * 2u <= index->cap;
}

/* The position the table proposes for `id`, or UINT32_MAX.  The owner
 * confirms it against its list. */
static inline uint32_t cetta_var_index_propose(const CettaVarIndex *index,
                                               VarId id) {
    if (!index->slots || id == VAR_ID_NONE)
        return UINT32_MAX;
    size_t mask = index->cap - 1u;
    for (size_t slot = cetta_var_index_hash(id) & mask;;
         slot = (slot + 1u) & mask) {
        if (index->slots[slot].id == VAR_ID_NONE)
            return UINT32_MAX;
        if (index->slots[slot].id == id)
            return index->slots[slot].position;
    }
}

/* Records `id` at `position`, over any older entry for it.  The table must
 * have room (cetta_var_index_has_room). */
static inline void cetta_var_index_record(CettaVarIndex *index, VarId id,
                                          uint32_t position) {
    size_t mask = index->cap - 1u;
    size_t slot = cetta_var_index_hash(id) & mask;
    while (index->slots[slot].id != VAR_ID_NONE &&
           index->slots[slot].id != id)
        slot = (slot + 1u) & mask;
    if (index->slots[slot].id == VAR_ID_NONE)
        index->used++;
    index->slots[slot] = (CettaVarIndexSlot){.id = id, .position = position};
}

/* The position of the variable `id` in the list `vars` of `len` variables,
 * or UINT32_MAX: a scan while the list is short, the table past it.
 * `index` may be NULL for a list never grown past the scan. */
static inline uint32_t cetta_var_index_find_atom(const CettaVarIndex *index,
                                                 Atom *const *vars,
                                                 uint32_t len, VarId id) {
    if (len <= CETTA_VAR_INDEX_SCAN || !index || !index->slots) {
        for (uint32_t position = 0u; position < len; position++) {
            if (vars[position]->var_id == id)
                return position;
        }
        return UINT32_MAX;
    }
    uint32_t position = cetta_var_index_propose(index, id);
    return position < len && vars[position]->var_id == id
        ? position : UINT32_MAX;
}

/* Records the last variable of the list `vars` of `len` variables, just
 * appended: past the scan, the table then holds every variable of the
 * list. */
static inline bool cetta_var_index_note_atom(CettaVarIndex *index,
                                             Atom *const *vars,
                                             uint32_t len) {
    if (len <= CETTA_VAR_INDEX_SCAN)
        return true;
    if (len == CETTA_VAR_INDEX_SCAN + 1u || !cetta_var_index_has_room(index)) {
        if (!cetta_var_index_reset(index, len))
            return false;
        for (uint32_t position = 0u; position < len; position++)
            cetta_var_index_record(index, vars[position]->var_id, position);
        return true;
    }
    cetta_var_index_record(index, vars[len - 1u]->var_id, len - 1u);
    return true;
}

/* A table for a list of `len` variables that does not change while it is
 * read. */
static inline bool cetta_var_index_build_atom(CettaVarIndex *index,
                                              Atom *const *vars,
                                              uint32_t len) {
    if (len <= CETTA_VAR_INDEX_SCAN)
        return true;
    if (!cetta_var_index_reset(index, len))
        return false;
    for (uint32_t position = 0u; position < len; position++)
        cetta_var_index_record(index, vars[position]->var_id, position);
    return true;
}

/* Inventory records whose first field is a VarId. memcpy avoids a type-
 * aliasing assumption about the surrounding payload. The payload and its
 * first-appearance order remain owned by the inventory. */
static inline VarId cetta_var_record_id(const void *records, size_t stride,
                                       size_t position) {
    VarId id;
    memcpy(&id, (const char *)records + stride * position, sizeof(id));
    return id;
}

static inline size_t cetta_var_index_find_records(
    const CettaVarIndex *index, const void *records, size_t stride,
    size_t len, VarId id) {
    if (len <= CETTA_VAR_INDEX_SCAN || len > UINT32_MAX ||
        id == VAR_ID_NONE || !index || !index->slots) {
        for (size_t position = 0u; position < len; position++) {
            if (cetta_var_record_id(records, stride, position) == id)
                return position;
        }
        return SIZE_MAX;
    }
    uint32_t position = cetta_var_index_propose(index, id);
    return position < len &&
            cetta_var_record_id(records, stride, position) == id
        ? position : SIZE_MAX;
}

static inline bool cetta_var_index_note_records(
    CettaVarIndex *index, const void *records, size_t stride, size_t len) {
    if (len <= CETTA_VAR_INDEX_SCAN || len > UINT32_MAX)
        return true;
    if (len == CETTA_VAR_INDEX_SCAN + 1u || !cetta_var_index_has_room(index)) {
        if (!cetta_var_index_reset(index, len))
            return false;
        for (uint32_t position = 0u; position < len; position++) {
            VarId id = cetta_var_record_id(records, stride, position);
            if (id != VAR_ID_NONE)
                cetta_var_index_record(index, id, position);
        }
    } else {
        VarId id = cetta_var_record_id(records, stride, len - 1u);
        if (id != VAR_ID_NONE)
            cetta_var_index_record(index, id, (uint32_t)(len - 1u));
    }
    return true;
}

#endif
