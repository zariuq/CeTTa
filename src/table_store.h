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
    CETTA_TABLE_MODE_VARIANT = 1,
    /* Ground-call memoization (Prime), consulted at the ground-call boundary
     * before equation dispatch — NOT the space-query layer that VARIANT uses.
     * A completed ground canonical call whose receipt delta is pure and whose
     * consulted spaces still match their captured revisions replays its whole
     * answer bag.  Off by default; see canonical_ground_call_key and
     * prime_need_receipt_delta_is_pure. */
    CETTA_TABLE_MODE_GROUND_CALL = 2
} CettaTableMode;

/* Ground-call memo key (composition of the two briefing gaps' first half).
 * Succeeds only for a GROUND call (no variables); for such a call
 * term-universe canonicalization is structurally identity, so the key is the
 * canonical term plus its interning-compatible structural hash.  The caller
 * disambiguates hash collisions with atom_eq on the canonical term. */
bool canonical_ground_call_key(Atom *call, Arena *arena,
                               Atom **out_canonical, uint64_t *out_hash);

typedef struct TableStoreEntry TableStoreEntry;
typedef struct GroundCallStore GroundCallStore;
typedef struct {
    void *impl;
} TableQueryHandle;

typedef struct {
    TableStoreEntry *entries;
    uint32_t len;
    uint32_t cap;
    CettaTableMode mode;
    AnswerBank *answer_bank;
    /* Ground-call memo entries (CETTA_TABLE_MODE_GROUND_CALL); lazily
     * allocated, NULL under other modes. */
    GroundCallStore *ground;
} TableStore;

/* Called once per stored answer occurrence during a ground-call replay. */
typedef bool (*TableGroundAnswerVisitor)(Atom *result, void *ctx);

/* Replay the whole memoized answer bag for a ground canonical call if a live
 * entry exists (found by hash then atom_eq, and the global mutation epoch is
 * unchanged since commit).  Returns true and visits every stored occurrence in
 * order on a hit; returns false (no visit) on a miss.  Multiplicity is
 * preserved: duplicate occurrences replay as duplicates. */
bool table_store_ground_lookup(TableStore *store, Atom *canonical,
                               uint64_t hash,
                               TableGroundAnswerVisitor visit, void *ctx);

/* Commit the whole answer bag of a completed, admitted ground canonical call.
 * Idempotent on (hash, canonical) at the current epoch: re-committing an
 * identical key replaces rather than duplicates.  The caller is responsible
 * for the admission conjuncts (ground, completed, pure receipt delta). */
bool table_store_ground_commit(TableStore *store, Atom *canonical,
                               uint64_t hash,
                               Atom *const *answers, uint32_t answer_len);

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
                              CettaCount *visited_out);
bool table_store_lookup_visit_delayed(TableStore *store, Space *space,
                                      uint64_t revision,
                                      Atom *query, Arena *out_arena,
                                      TableDelayedResultVisitor visitor,
                                      void *ctx,
                                      CettaCount *visited_out);
bool table_store_lookup_visit_ref(TableStore *store, Space *space,
                                  uint64_t revision,
                                  Atom *query,
                                  Arena *goal_owner,
                                  TableAnswerRefVisitor visitor,
                                  void *ctx,
                                  CettaCount *visited_out);
bool table_store_put(TableStore *store, Space *space, uint64_t revision,
                     Atom *query, const QueryResults *results);

#endif /* CETTA_TABLE_STORE_H */
