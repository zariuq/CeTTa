#ifndef CETTA_SPACE_DENSE_H
#define CETTA_SPACE_DENSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A binary relation kept as bits: the facts (h a b) of one head h whose
 * columns are atoms equal only to themselves, as a set of pairs (a, b).
 * One numbering covers the atoms of both columns; each numbered first
 * column has a row over the numbers of its second column: a sorted array
 * while that takes fewer bytes than bits up to its largest number, then
 * bits.  The relation is a set: a space that holds a pair more than once
 * keeps the other copies as rows.
 *
 * Ids are the universe's: the store neither interns nor reads terms, and a
 * caller that counts references holds each numbered id while it is numbered
 * (cetta_dense_relation_each_numbered lists them).
 */

typedef uint64_t CettaDenseId;

/* A column left free in a walk or a count.  Every other value is an id,
 * 0 included. */
#define CETTA_DENSE_ANY UINT64_MAX
#define CETTA_DENSE_ROW_BITS UINT32_C(0x80000000)

typedef struct {
    /* The numbering: number -> id, and an open-addressed table id -> number
     * plus one. */
    CettaDenseId *ids;
    uint32_t len, cap;
    uint32_t *slots;
    uint32_t slot_mask;
    /* Rows, one per number used in the first column; NULL until that
     * number first appears there.  row_len[n] is a sorted array's count of
     * numbers, or with CETTA_DENSE_ROW_BITS a row of bits' count of words. */
    void **rows;
    uint32_t *row_len;
    uint32_t row_cap;
    /* Pairs held, and the bytes the rows take. */
    uint64_t pairs;
    uint64_t row_bytes;
} CettaDenseRelation;

void cetta_dense_relation_init(CettaDenseRelation *rel);
void cetta_dense_relation_free(CettaDenseRelation *rel);

/* Add the pair (a, b): true when it was absent and is now held, false when
 * it was held already or memory ran out (*OOM set then). */
bool cetta_dense_relation_add(CettaDenseRelation *rel, CettaDenseId a,
                              CettaDenseId b, bool *oom);
/* Remove the pair (a, b): true when it was held. */
bool cetta_dense_relation_remove(CettaDenseRelation *rel, CettaDenseId a,
                                 CettaDenseId b);
bool cetta_dense_relation_contains(const CettaDenseRelation *rel,
                                   CettaDenseId a, CettaDenseId b);
static inline uint64_t cetta_dense_relation_pairs(
    const CettaDenseRelation *rel) {
    return rel ? rel->pairs : 0u;
}

/* The number of ID, or UINT32_MAX when it is not numbered. */
uint32_t cetta_dense_relation_number(const CettaDenseRelation *rel,
                                     CettaDenseId id);

/* Visit pairs.  A visit returning false stops the walk, which then returns
 * false.  A_FIXED / B_FIXED of CETTA_DENSE_ANY leave that column free. */
typedef bool (*CettaDensePairVisit)(void *context, CettaDenseId a,
                                    CettaDenseId b);
bool cetta_dense_relation_each(const CettaDenseRelation *rel,
                               CettaDenseId a_fixed, CettaDenseId b_fixed,
                               CettaDensePairVisit visit, void *context);
/* The pairs (a_fixed, b) or (a, b_fixed), counted. */
uint64_t cetta_dense_relation_count(const CettaDenseRelation *rel,
                                    CettaDenseId a_fixed,
                                    CettaDenseId b_fixed);

/* Visit every numbered id once. */
typedef void (*CettaDenseIdVisit)(void *context, CettaDenseId id);
void cetta_dense_relation_each_numbered(const CettaDenseRelation *rel,
                                        CettaDenseIdVisit visit,
                                        void *context);

/* Bytes held: numbering, table and rows. */
uint64_t cetta_dense_relation_bytes(const CettaDenseRelation *rel);

#endif
