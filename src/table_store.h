#ifndef CETTA_TABLE_STORE_H
#define CETTA_TABLE_STORE_H

#include "space.h"
#include "variant_instance.h"

/*
 * TableStore is the first tabling seam beside SearchContext.
 *
 * Shared semantic contract across backends:
 *   - Variant canonicalization defines query equivalence.
 *   - Revision is the authoritative space snapshot boundary.
 *   - Only exact-current-revision entries may replay answers.
 *   - Stale entries are reusable replacement targets, not semantic hits.
 *
 * Positive example:
 *   - A repeated variant query against the same space revision may replay a
 *     memoized answer set.
 *
 * Negative example:
 *   - Replaying answers from an older revision because the variant shape still
 *     matches, silently hiding an invalidation boundary.
 *
 * This tranche follows the conservative literature path: NONE or VARIANT
 * only.
 */

typedef enum {
    CETTA_TABLE_MODE_NONE = 0,
    CETTA_TABLE_MODE_VARIANT = 1
} CettaTableMode;

typedef struct TableStoreEntry TableStoreEntry;
typedef struct {
    void *impl;
} TableQueryHandle;

typedef struct {
    TableStoreEntry *entries;
    uint32_t len;
    uint32_t cap;
    CettaTableMode mode;
} TableStore;

typedef bool (*TableDelayedResultVisitor)(Atom *result,
                                          const Bindings *bindings,
                                          const VariantInstance *variant,
                                          void *ctx);

void table_store_init(TableStore *store, CettaTableMode mode);
void table_store_free(TableStore *store);
bool table_store_begin_query(TableStore *store, Space *space, uint64_t revision,
                             Atom *query, TableQueryHandle *handle);
bool table_store_add_answer(TableQueryHandle *handle, Atom *result,
                            const Bindings *bindings);
bool table_store_commit_query(TableQueryHandle *handle);
void table_store_abort_query(TableQueryHandle *handle);
bool table_store_lookup(TableStore *store, Space *space, uint64_t revision,
                        Atom *query, Arena *out_arena,
                        QueryResults *out);
bool table_store_lookup_visit(TableStore *store, Space *space, uint64_t revision,
                              Atom *query, Arena *out_arena,
                              QueryResultVisitor visitor, void *ctx,
                              uint32_t *visited_out);
bool table_store_lookup_visit_delayed(TableStore *store, Space *space,
                                      uint64_t revision,
                                      Atom *query, Arena *out_arena,
                                      TableDelayedResultVisitor visitor,
                                      void *ctx,
                                      uint32_t *visited_out);
bool table_store_put(TableStore *store, Space *space, uint64_t revision,
                     Atom *query, const QueryResults *results);

#endif /* CETTA_TABLE_STORE_H */
