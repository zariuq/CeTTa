#ifndef CETTA_TABLE_STORE_H
#define CETTA_TABLE_STORE_H

#include "answer_bank.h"
#include "space.h"
#include "term_canon.h"
#include "variant_instance.h"

/*
 * TableStore is one optional memoization substrate beside SearchContext.
 *
 * The authoritative user-visible semantics remain the active language/profile
 * surface plus the public Space API (`new-space`, `match`, `add-atom`,
 * `remove-atom`, revision-observable mutation, and any explicit pragma/mode
 * selected by the caller). This header does not define a backend-wide or
 * language-wide law; it only states the current invariants for the explicit
 * `search-table-mode variant` implementation.
 *
 * Current local invariants for CETTA_TABLE_MODE_VARIANT:
 *   - Variant canonicalization is the lookup key inside this table mode.
 *   - The supplied revision token is this implementation's invalidation key.
 *   - This implementation only replays answers from an exact current-revision
 *     entry.
 *   - Older entries may be reused as storage slots after invalidation, but
 *     lookup still treats them as misses.
 *
 * Positive example:
 *   - A repeated variant query in this mode, against the same revision token,
 *     may replay a memoized answer set.
 *
 * Negative example:
 *   - Replaying answers from an older revision because the variant shape still
 *     matches, silently hiding a mutation boundary in this implementation.
 *
 * Future backends, future `--lang` surfaces, or future explicit table modes
 * may use different internal keys, invalidation schemes, or replay machinery
 * so long as they preserve the higher-level exposed semantics.
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
    AnswerBank *answer_bank;
} TableStore;

typedef bool (*TableDelayedResultVisitor)(Atom *result,
                                          const Bindings *bindings,
                                          const VariantInstance *variant,
                                          void *ctx);
typedef bool (*TableAnswerRefVisitor)(const AnswerBank *bank,
                                      AnswerRef ref,
                                      const CettaVarMap *goal_instantiation,
                                      void *ctx);

void table_store_init(TableStore *store, CettaTableMode mode,
                      AnswerBank *answer_bank);
void table_store_free(TableStore *store);
bool table_store_begin_query(TableStore *store, Space *space, uint64_t revision,
                             Atom *query, TableQueryHandle *handle);
bool table_store_add_answer(TableQueryHandle *handle, Atom *result,
                            const Bindings *bindings, AnswerRef *out_ref);
bool table_store_commit_query(TableQueryHandle *handle);
void table_store_abort_query(TableQueryHandle *handle);
bool table_store_query_goal_instantiation(TableQueryHandle *handle,
                                          Arena *dst,
                                          CettaVarMap *out);
bool table_store_materialize_answer_ref(const AnswerBank *answer_bank,
                                        AnswerRef ref,
                                        Arena *out_arena,
                                        const CettaVarMap *goal_instantiation,
                                        Atom **out_result,
                                        Bindings *out_bindings,
                                        VariantInstance *out_variant);
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
bool table_store_lookup_visit_ref(TableStore *store, Space *space,
                                  uint64_t revision,
                                  Atom *query,
                                  Arena *goal_owner,
                                  TableAnswerRefVisitor visitor,
                                  void *ctx,
                                  uint32_t *visited_out);
bool table_store_put(TableStore *store, Space *space, uint64_t revision,
                     Atom *query, const QueryResults *results);

#endif /* CETTA_TABLE_STORE_H */
