#ifndef CETTA_SPACE_TUPLES_H
#define CETTA_SPACE_TUPLES_H

#include "space_dense.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A relation kept as a table of tuples: the facts (h c1 ... ck) of one head
 * h whose columns are atoms equal only to themselves, as a set of k-tuples
 * of ids, with no record, universe entry or row per fact.  The bits of
 * space_dense.h keep dense binary relations in fewer bytes; a table takes
 * any arity up to CETTA_TUPLE_MAX_ARITY and sparse columns.
 *
 * The ids that appear are numbered densely, and a tuple is its k numbers,
 * held in no order.  A hash of the tuples answers membership.  A column's
 * side chains, for each number, the tuples holding it in that column; it is
 * built once walks binding that column have read as many tuples as building
 * it reads, and is kept in step from then on.  The relation is a set: a
 * space that holds a fact more than once keeps the other copies as rows.
 *
 * Ids are the universe's: the table neither interns nor reads terms, and a
 * caller that counts references holds each numbered id while it is numbered
 * (cetta_tuple_table_each_numbered lists them).  Ids are CettaDenseId, and
 * CETTA_DENSE_ANY leaves a column free.
 */

enum { CETTA_TUPLE_MAX_ARITY = 8 };

/* One column's side: for each number, the first tuple holding it there and
 * how many do; for each tuple, the next and previous one holding the same
 * number.  Positions are stored plus one, zero ending a chain. */
typedef struct {
    uint32_t *first;
    uint32_t *len;
    uint32_t number_cap;
    uint32_t *next;
    uint32_t *prev;
    uint32_t tuple_cap;
} CettaTupleSide;

typedef struct {
    uint32_t arity;
    /* The numbering: number -> id, and an open-addressed table id -> number
     * plus one, with eight bits of each slot's hash beside it, so a probe
     * reads an id only when those agree. */
    CettaDenseId *ids;
    uint32_t len, cap;
    uint32_t *slots;
    uint8_t *slot_tags;
    uint32_t slot_mask;
    /* The tuples, arity numbers each, in no order. */
    uint32_t *tuples;
    uint32_t count, tuple_cap;
    /* Open-addressed tuple positions plus one, with eight bits of each
     * tuple's hash beside them. */
    uint32_t *hash;
    uint8_t *hash_tags;
    uint32_t hash_mask;
    /* Sides by column, kept once walks have paid for them; the tuples those
     * walks have read so far. */
    CettaTupleSide side[CETTA_TUPLE_MAX_ARITY];
    bool side_kept[CETTA_TUPLE_MAX_ARITY];
    uint64_t side_rent[CETTA_TUPLE_MAX_ARITY];
} CettaTupleTable;

/* An empty table of ARITY columns, 1 <= ARITY <= CETTA_TUPLE_MAX_ARITY;
 * false for any other arity. */
bool cetta_tuple_table_init(CettaTupleTable *table, uint32_t arity);
void cetta_tuple_table_free(CettaTupleTable *table);

/* Add TUPLE: true when it was absent and is now held, false when it was
 * held already or memory ran out (*OOM set then, the tuple not held; ids
 * numbered on the way stay numbered). */
bool cetta_tuple_table_add(CettaTupleTable *table, const CettaDenseId *tuple,
                           bool *oom);
/* Remove TUPLE: true when it was held.  Its ids stay numbered. */
bool cetta_tuple_table_remove(CettaTupleTable *table,
                              const CettaDenseId *tuple);
bool cetta_tuple_table_contains(const CettaTupleTable *table,
                                const CettaDenseId *tuple);
static inline uint64_t cetta_tuple_table_tuples(const CettaTupleTable *table) {
    return table ? table->count : 0u;
}

/* Visit the tuples whose columns equal FIXED's, a column of CETTA_DENSE_ANY
 * being free.  A visit returning false stops the walk, which then returns
 * false.  The visits see ids, not numbers.  A visit must not change the
 * table; a walk may build a side, and returns false when memory runs out
 * doing so. */
typedef bool (*CettaTupleVisit)(void *context, const CettaDenseId *tuple);
bool cetta_tuple_table_each(CettaTupleTable *table, const CettaDenseId *fixed,
                            CettaTupleVisit visit, void *context);
/* The tuples cetta_tuple_table_each would visit, counted. */
uint64_t cetta_tuple_table_count(CettaTupleTable *table,
                                 const CettaDenseId *fixed);

/* Visit every numbered id once. */
void cetta_tuple_table_each_numbered(const CettaTupleTable *table,
                                     CettaDenseIdVisit visit, void *context);

/* Bytes held: numbering, tuples, hash and sides. */
uint64_t cetta_tuple_table_bytes(const CettaTupleTable *table);

#endif
