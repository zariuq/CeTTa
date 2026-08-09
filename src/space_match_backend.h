#ifndef CETTA_SPACE_MATCH_BACKEND_H
#define CETTA_SPACE_MATCH_BACKEND_H

#include "atom.h"
#include "subst_tree.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Space Space;
typedef struct DiscNode DiscNode;
typedef struct CettaMorkSpaceHandle CettaMorkSpaceHandle;

typedef enum {
    SPACE_ENGINE_NATIVE = 0,
    SPACE_ENGINE_NATIVE_CANDIDATE_EXACT = 1,
    SPACE_ENGINE_PATHMAP = 2,
    SPACE_ENGINE_MORK = 3,
} SpaceEngine;

/* A batch accelerator is never the mutation authority.  UNSUPPORTED asks the
   Space layer to replay the same ordered occurrences through the singular
   oracle; ERROR reports a transactional failure after which no occurrence may
   have become visible; APPLIED publishes the batch as one backend revision. */
typedef enum {
    SPACE_BACKEND_BATCH_UNSUPPORTED = 0,
    SPACE_BACKEND_BATCH_APPLIED = 1,
    SPACE_BACKEND_BATCH_ERROR = 2,
} SpaceBackendBatchResult;

static inline bool space_engine_uses_pathmap(SpaceEngine engine) {
    return engine == SPACE_ENGINE_PATHMAP || engine == SPACE_ENGINE_MORK;
}

static inline bool space_engine_supports_exec(SpaceEngine engine) {
    return engine == SPACE_ENGINE_MORK;
}

typedef struct {
    DiscNode *match_trie;
    bool match_trie_dirty;
    SubstTree *stree;
    bool stree_dirty;
} SpaceMatchNativeState;

typedef enum {
    IMPORTED_FLAT_SYMBOL = 0,
    IMPORTED_FLAT_VAR = 1,
    IMPORTED_FLAT_EXPR = 2,
    IMPORTED_FLAT_INT = 3,
    IMPORTED_FLAT_FLOAT = 4,
    IMPORTED_FLAT_BOOL = 5,
    IMPORTED_FLAT_STRING = 6,
    IMPORTED_FLAT_BIGINT = 7,
    IMPORTED_FLAT_RATIONAL = 8,
    IMPORTED_FLAT_GROUNDED_OTHER = 9,
} ImportedFlatTokenKind;

typedef struct {
    ImportedFlatTokenKind kind;
    Atom *origin;
    AtomId origin_id;
    CettaIndex span;
    VarId var_id;
    union {
        SymbolId sym_id;
        CettaExprLen arity;
        int64_t ival;
        double fval;
        bool bval;
    };
} ImportedFlatToken;

typedef struct {
    CettaIndex atom_idx;
    uint32_t epoch;
    ImportedFlatToken *tokens;
    CettaIndex len;
} ImportedFlatEntry;

typedef struct {
    ImportedFlatEntry *entries;
    CettaIndex len, cap;
} ImportedFlatBucket;

typedef struct {
    ImportedFlatBucket buckets[STREE_BUCKETS];
    ImportedFlatBucket wildcard;
    bool built;
    bool dirty;
    bool bridge_active;
    bool bridge_unavailable;
    void *bridge_space;
    uint8_t *projected_atom_ids;
    CettaIndex projected_len;
    uint8_t projected_atom_id_width_bits;
    bool projection_valid;
    /* True exactly when the native AtomId array is a current derived view of
       the backend-primary bridge.  This is independent of projection_valid:
       the decoded projection packet may be discarded after the native view
       has been refreshed. */
    bool native_shadow_synced;
    /* Some language constructs observe declaration/occurrence order.  Once
       such a construct is admitted, the native AtomId sequence remains the
       authoritative order sidecar while PathMap continues to mirror/index
       the same logical bag. */
    bool preserve_logical_order;
} ImportedBridgeState;

typedef struct {
    ImportedBridgeState bridge;
} PathmapLocalState;

typedef struct {
    ImportedBridgeState bridge;
    bool attached_compiled;
    uint64_t attached_count;
} MorkImportedState;

typedef struct SpaceMatchBackendOps {
    const char *name;
    /* Primary storage access seam.
       Positive example: a real backend-owned pathmap space can answer length
       and projection through its own state instead of pretending the native C
       shadow is the source of truth.
       Negative example: every backend inheriting native-C atom_ids as an
       implicit universal ownership rule. */
    /* Optional primary mutation seam.
       Positive example: a local PathMap-backed space can accept one canonical
       AtomId directly into backend-owned storage and leave native C arrays as
       lazy projection.
       Negative example: every special backend being forced to append into the
       native shadow first just because `Space` historically started there. */
    bool (*store_atom_direct)(Space *s, Atom *atom);
    bool (*store_atom_id_direct)(Space *s, AtomId atom_id, Atom *atom);
    SpaceBackendBatchResult (*store_atom_ids_batch_direct)(
        Space *s, const AtomId *atom_ids, CettaCount atom_count,
        uint64_t *out_added);
    bool (*remove_atom_id_direct)(Space *s, AtomId atom_id);
    bool (*remove_atom_direct)(Space *s, Atom *atom);
    SpaceBackendBatchResult (*remove_atom_ids_batch_direct)(
        Space *s, const AtomId *atom_ids, CettaCount atom_count,
        uint64_t *out_removed);
    bool (*truncate_direct)(Space *s, uint64_t new_len);
    uint64_t (*logical_len)(const Space *s);
    AtomId (*get_atom_id_at)(const Space *s, uint64_t idx);
    Atom *(*get_at)(const Space *s, uint64_t idx);
    bool (*materialize_native_storage)(Space *s, Arena *persistent_arena);
    bool supports_direct_bindings;
    void (*free)(Space *s);
    void (*note_add)(Space *s, AtomId atom_id, Atom *atom, CettaIndex atom_idx);
    void (*note_remove)(Space *s);
    CettaIndex (*candidates)(Space *s, Atom *pattern, CettaIndex **out);
    /*
     * Exact COUNT pushdown for a flat linear pattern over ground stored
     * atoms.  Returning false declines the fragment without changing state;
     * callers then use the ordinary matcher.  `examined` reports semantic
     * candidate work, while `count` preserves occurrence multiplicity.
     */
    bool (*count_flat_linear)(Space *s, Arena *scratch, Atom *pattern,
                              uint64_t *count, CettaIndex *examined);
    /*
     * Exact bag COUNT for a conjunction, consuming a backend-native pull
     * traversal without reconstructing binding rows.  False declines to the
     * ordinary conjunction evaluator.
     */
    bool (*count_conjunction)(Space *s, Arena *scratch,
                              Atom **patterns, CettaExprLen npatterns,
                              const Bindings *seed, uint64_t *count);
    void (*query)(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
    void (*query_conjunction)(Space *s, Arena *a, Atom **patterns, CettaExprLen npatterns,
                              const Bindings *seed, BindingSet *out);
} SpaceMatchBackendOps;

typedef struct {
    SpaceEngine kind;
    const SpaceMatchBackendOps *ops;
    SpaceMatchNativeState native;
    PathmapLocalState pathmap;
    MorkImportedState mork;
} SpaceMatchBackend;

typedef bool (*CettaMorkBindingsVisitor)(const Bindings *bindings, void *ctx);
typedef bool (*CettaMorkAtomVisitor)(Atom *atom, void *ctx);

/* Interpretation of streamed-row-disposition after transport decoding.
   CONTINUE covers both a contributed row and an additive-zero cyclic row;
   STOP is deliberate consumer cancellation; FAULT is malformed transport. */
typedef enum {
    SPACE_MATCH_DECODED_ROW_CONTINUE = 0,
    SPACE_MATCH_DECODED_ROW_STOP = 1,
    SPACE_MATCH_DECODED_ROW_FAULT = 2,
} SpaceMatchDecodedRowVisitResult;

SpaceMatchDecodedRowVisitResult space_match_backend_visit_decoded_row(
    bool decoded, Bindings *row, CettaMorkBindingsVisitor visitor, void *ctx);

/* Result of a backend pull visitor.  DECLINED is the only state in which the
   caller may replay through the semantic oracle: no cursor was admitted and
   no row was observed.  TERMINATED means that a cursor was admitted but
   traversal did not complete (either the visitor stopped or the backend
   failed), so replay could duplicate an observed prefix. */
typedef enum {
    SPACE_MATCH_PULL_VISIT_DECLINED = 0,
    SPACE_MATCH_PULL_VISIT_COMPLETE = 1,
    SPACE_MATCH_PULL_VISIT_TERMINATED = 2,
} SpaceMatchPullVisitResult;

typedef enum {
    SPACE_BRIDGE_IMPORT_ERROR = 0,
    SPACE_BRIDGE_IMPORT_OK = 1,
    SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK = 2,
} SpaceBridgeImportResult;

typedef enum {
    SPACE_TRANSFER_ENDPOINT_NONE = 0,
    SPACE_TRANSFER_ENDPOINT_SPACE = 1,
    SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE = 2,
} SpaceTransferEndpointKind;

typedef struct {
    SpaceTransferEndpointKind kind;
    union {
        Space *space;
        CettaMorkSpaceHandle *bridge;
    };
} SpaceTransferEndpoint;

typedef enum {
    SPACE_TRANSFER_ERROR = 0,
    SPACE_TRANSFER_OK = 1,
    SPACE_TRANSFER_NEEDS_TEXT_FALLBACK = 2,
} SpaceTransferResult;

typedef enum {
    SPACE_MATCH_BACKEND_ERROR_NONE = 0,
    SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE = 1,
    SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE = 2,
} SpaceMatchBackendError;

void space_match_backend_clear_error(void);
void space_match_backend_set_error(SpaceMatchBackendError code);
SpaceMatchBackendError space_match_backend_last_error_code(void);
const char *space_match_backend_last_error(void);
const char *space_match_backend_error_name(SpaceMatchBackendError code);
uint64_t space_match_backend_native_materialization_limit(void);
uint64_t space_match_backend_packet_materialization_limit(void);
uint64_t space_match_backend_contextual_query_slot_limit(void);
void space_match_backend_diag_set_packet_materialization_limit_override(
    uint64_t limit);
void space_match_backend_diag_set_contextual_query_slot_limit_override(
    uint64_t limit);
void space_match_backend_diag_reset(void);
/* Diagnostic entry for adversarial ownership/compaction tests.  Production
   queries call the same normalizer internally. */
void space_match_backend_diag_normalize_subst_matches(SubstMatchSet *matches);
bool space_match_backend_u32_bound_checked(uint64_t value,
                                           SpaceMatchBackendError error,
                                           uint32_t *out_value);

void space_match_backend_init(Space *s);
void space_match_backend_free(Space *s);
bool space_match_backend_try_set(Space *s, SpaceEngine kind);
bool space_match_backend_needs_atom_on_add(const Space *s, AtomId atom_id);
void space_match_backend_note_add(Space *s, AtomId atom_id, Atom *atom,
                                  CettaIndex atom_idx);
/* Extend already-realized native candidate indexes after a backend-primary
 * append has extended their synchronized AtomId coordinate shadow.  Opaque
 * mutations and removals still invalidate through their ordinary paths. */
void space_match_backend_note_native_shadow_add(Space *s, AtomId atom_id,
                                                CettaIndex atom_idx);
void space_match_backend_note_remove(Space *s);
CettaIndex space_match_backend_candidates64(Space *s, Atom *pattern,
                                            CettaIndex **out);
uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out);
Atom *space_match_backend_candidate_at64(const Space *s, CettaIndex idx);
void space_match_backend_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
const char *space_match_backend_name(const Space *s);
bool space_match_backend_supports_direct_bindings(const Space *s);
const char *space_match_backend_kind_name(SpaceEngine kind);
bool space_match_backend_kind_from_name(const char *name, SpaceEngine *out);
const char *space_match_backend_unavailable_reason(SpaceEngine kind);
bool space_match_backend_attach_act_file(Space *s, const char *path, uint64_t *out_loaded);
bool space_match_backend_materialize_native_storage(Space *s,
                                                    Arena *persistent_arena);
/* Compatibility name kept while callers migrate to the generic projection
   vocabulary above. */
bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena);
bool space_match_backend_load_sexpr_chunk(Space *s, Arena *persistent_arena,
                                          const uint8_t *text, size_t len,
                                          uint64_t *out_added);
bool space_match_backend_remove_sexpr_chunk(Space *s, Arena *persistent_arena,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed);
bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed);
bool space_match_backend_is_attached_compiled(const Space *s);
bool space_match_backend_bridge_space(Space *s,
                                      CettaMorkSpaceHandle **out_bridge);
/* Clone a backend-primary snapshot without projecting its logical rows through
 * the native atom-id sequence. Returns false when the backend has no exact
 * persistent snapshot operation, so callers may use the generic clone path. */
bool space_match_backend_snapshot_clone(Space *dst, Space *src);
bool space_match_backend_require_logical_order(Space *s,
                                               Arena *persistent_arena);
bool space_match_backend_store_atom_id_direct(Space *s, AtomId atom_id,
                                              Atom *atom);
bool space_match_backend_store_atom_direct(Space *s, Atom *atom);
SpaceBackendBatchResult space_match_backend_store_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_added);
bool space_match_backend_remove_atom_id_direct(Space *s, AtomId atom_id);
bool space_match_backend_remove_atom_direct(Space *s, Atom *atom);
SpaceBackendBatchResult space_match_backend_remove_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_removed);
bool space_match_backend_truncate_direct(Space *s, uint32_t new_len);
/* Main backend source/sink transfer seam.
   Positive example: a resolved PathMap/MORK endpoint pair moving logical rows
   without forcing an intermediate tuple or shadow copy.
   Negative example: each caller open-coding its own backend matrix or losing
   text-fallback information by collapsing everything to bool. */
SpaceTransferResult space_match_backend_transfer_resolved_result(
    SpaceTransferEndpoint dst,
    SpaceTransferEndpoint src,
    Arena *persistent_arena,
    uint64_t *out_added);
bool space_match_backend_contains_atom_structural_direct(Space *s,
                                                         Atom *atom,
                                                         bool *out_found);
bool space_match_backend_truncate_direct64(Space *s, uint64_t new_len);
bool space_match_backend_logical_len_u32_checked(const Space *s, uint32_t *out_len);
uint64_t space_match_backend_logical_len64(const Space *s);
AtomId space_match_backend_get_atom_id_at(const Space *s, uint32_t idx);
AtomId space_match_backend_get_atom_id_at64(const Space *s, uint64_t idx);
Atom *space_match_backend_get_at(const Space *s, uint32_t idx);
Atom *space_match_backend_get_at64(const Space *s, uint64_t idx);
/* Internal structural decoder used beneath the endpoint transfer seam.
   Positive example: a ground bridge payload with a live TermUniverse
   materializes directly into canonical AtomIds.
   Negative example: ordinary transfer callers bypassing the endpoint API and
   silently dropping the fallback distinction. */
SpaceBridgeImportResult space_match_backend_import_bridge_space(
    Space *dst,
    CettaMorkSpaceHandle *bridge,
    uint64_t *out_loaded);
bool space_match_backend_mork_query_bindings_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    BindingSet *out);
bool space_match_backend_mork_visit_atoms_direct(
    CettaMorkSpaceHandle *bridge,
    TermUniverse *universe,
    Arena *scratch,
    CettaMorkAtomVisitor visitor,
    void *ctx);
/* Exact ground PathMap atom pull.  The current expression-row cursor does not
   carry the opening context required to preserve stored variable identity, so
   variable-bearing spaces decline to the materializing oracle. */
bool space_match_backend_can_try_visit_atoms_direct(Space *s);
SpaceMatchPullVisitResult space_match_backend_try_visit_atoms_direct(
    Space *s,
    Arena *scratch,
    CettaMorkAtomVisitor visitor,
    void *ctx);
bool space_match_backend_mork_visit_bindings_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
bool space_match_backend_visit_bindings_direct(
    Space *s,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
/* Syntactic preflight for observation-fused consumers.  The Rust index makes
   the authoritative, relation-sensitive admission decision when traversal is
   attempted; a declined attempt remains safe to run through the oracle. */
bool space_match_backend_can_try_visit_bindings_indexed(
    const Space *s,
    const Atom *query);
SpaceMatchPullVisitResult
space_match_backend_try_visit_bindings_indexed(
    Space *s,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
SpaceMatchPullVisitResult
space_match_backend_try_visit_conjunction_indexed(
    Space *s,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
/* At an admitted cursor callback, recover the stored-side variable identities
   associated with the current opening activation.  Outside such a callback
   this is the identity operation.  This is used only when an occurrence-
   addressed effect must be snapshotted beyond the cursor lifetime. */
Atom *space_match_backend_restore_opening_provenance(
    Arena *a,
    Atom *atom);
bool space_match_backend_mork_visit_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
bool space_match_backend_mork_query_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    BindingSet *out);
/*
 * Evaluate only the tuples newly enabled between an immutable PathMap
 * snapshot (`old`) and its additive descendant (`known`). The implementation
 * uses the disjoint first-delta partition and streams each exact bag row to
 * the visitor. False means that the snapshot lineage or query shape is not
 * admitted; callers retain their ordinary fixed-point evaluator as fallback.
 */
bool space_match_backend_visit_conjunction_semi_naive(
    Space *known,
    Space *old,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
bool space_match_backend_count_conjunction_semi_naive(
    Space *known,
    Space *old,
    Arena *a,
    Atom **patterns,
    CettaExprLen npatterns,
    const Bindings *seed,
    uint64_t *out_count);
void space_match_backend_print_inventory(FILE *out);

/* Seeded single-atom matching for streamed conjunctive match (native
   engines): one unification of pattern against the indexed atom under (and
   extending) env. Pair with space_match_backend_candidates64. */
bool space_match_backend_supports_seeded_candidates(Space *s);
bool space_match_backend_match_atom_seeded(Space *s, CettaIndex atom_idx,
                                           Atom *pattern, Bindings *env,
                                           Arena *a);

#endif /* CETTA_SPACE_MATCH_BACKEND_H */
