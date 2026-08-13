#ifndef CETTA_GSLT2PARSE_OSLF_NATIVE_TYPE_VM_V1_H
#define CETTA_GSLT2PARSE_OSLF_NATIVE_TYPE_VM_V1_H

#include "oslf_native_type_plan_v1.h"

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PPOSLF_NATIVE_VM_PROVED_V1,
    PPOSLF_NATIVE_VM_NO_PROOF_V1,
    PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1,
    PPOSLF_NATIVE_VM_INVALID_QUERY_V1,
} PPOSLFNativeVMOutcomeV1;

/*
 * Both limits are exact.  A zero rule-attempt limit executes no rule.  A
 * zero depth limit admits a root fact but refuses every generated premise.
 */
typedef struct {
    uint64_t maximum_rule_attempts;
    uint32_t maximum_goal_depth;
} PPOSLFNativeVMLimitsV1;

typedef struct {
    uint64_t goals_entered;
    uint64_t rule_attempts;
    uint64_t rule_matches;
    uint64_t ground_pattern_rule_attempts;
    uint64_t ground_pattern_rule_matches;
    uint64_t ground_dense_match_nodes;
    uint64_t ground_dense_rigid_subtrees_compared;
    uint64_t ground_dense_slot_writes;
    uint64_t ground_dense_slot_compares;
    uint64_t ground_dense_expression_materializations;
    uint64_t ground_dense_rigid_subtrees_reused;
    uint64_t ground_dense_ground_body_reuses;
    uint64_t ground_dense_workspace_growths;
    uint64_t ground_dense_view_nodes;
    uint64_t ground_dense_view_variable_resolutions;
    uint64_t ground_dense_view_deferrals;
    uint64_t positional_linear_rule_attempts;
    uint64_t positional_linear_rule_matches;
    uint64_t positional_linear_rule_fallbacks;
    uint64_t deferred_epoch_goal_materializations;
    uint64_t activation_view_goal_admissions;
    uint64_t activation_view_rule_attempts;
    uint64_t activation_view_rule_matches;
    uint64_t activation_view_fallback_materializations;
    uint64_t structural_shape_guard_attempts;
    uint64_t structural_shape_guard_rejections;
    uint64_t structural_shape_guard_unknowns;
    uint64_t compiled_application_dispatches;
    uint64_t indexed_candidate_visits;
    uint64_t full_scan_candidate_visits;
    uint64_t external_row_candidate_visits;
    uint64_t external_row_matches;
    uint64_t external_exact_key_lookups;
    uint64_t external_exact_key_hits;
    uint64_t external_prefix_key_lookups;
    uint64_t external_prefix_key_hits;
    uint64_t external_prefix_key_candidates;
    uint64_t external_prefix_memo_hits;
    uint64_t external_prefix_memo_misses;
    uint64_t generated_continuations;
    uint64_t generated_raw_tail_deterministic_continuations;
    uint64_t generated_tail_deterministic_continuations;
    uint64_t generated_tail_frame_reuses;
    uint64_t deferred_shape_guard_candidates;
    uint64_t deferred_shape_guard_attempts;
    uint64_t external_continuations;
    uint64_t external_tail_deterministic_continuations;
    uint64_t external_tail_frame_reuses;
    uint64_t deterministic_tail_collections;
    uint64_t deterministic_tail_collection_failures;
    uint64_t deterministic_goal_roots_scanned;
    uint64_t deterministic_binding_collections;
    uint64_t deterministic_binding_collection_failures;
    uint64_t deterministic_binding_roots_scanned;
    uint64_t deterministic_binding_items_discarded;
    uint64_t deterministic_trail_entries_discarded;
    uint64_t deterministic_arena_bytes_copied;
    uint64_t deterministic_arena_bytes_reclaimed;
    uint64_t goal_materialization_arena_bytes;
    uint64_t generated_match_arena_bytes;
    uint64_t body_expansion_arena_bytes;
    uint64_t pending_goal_node_arena_bytes;
    uint64_t rollback_arena_bytes_reclaimed;
    uint32_t maximum_goal_depth;
    uint32_t maximum_search_frame_depth;
} PPOSLFNativeVMStatsV1;

/* Add one vocabulary-neutral execution profile into an aggregate.  Event
 * counters saturate rather than wrap; peak depths retain their maximum. */
void pposlf_native_vm_stats_v1_accumulate(
    PPOSLFNativeVMStatsV1 *aggregate,
    const PPOSLFNativeVMStatsV1 *sample);

/*
 * Generated-step indices address the canonical step_schema vector of
 * program_digest.  External-row indices address the canonical row vector of
 * capability_digest.  Events are emitted in preorder and therefore form a
 * replayable finite proof tree when paired with generated rule arities;
 * external rows are leaves.
 */
typedef enum {
    PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1,
    PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1,
} PPOSLFNativeVMProofEventKindV1;

typedef struct {
    PPOSLFNativeVMProofEventKindV1 kind;
    uint32_t index;
} PPOSLFNativeVMProofEventV1;

typedef struct {
    PPOSLFNativeVMOutcomeV1 outcome;
    PPOSLFNativeVMStatsV1 stats;
    PPOSLFNativeVMProofEventV1 *proof_events;
    uint32_t proof_event_len;
    bool capability_digest_ready;
    char program_digest[65];
    char capability_digest[65];
} PPOSLFNativeVMResultV1;

typedef struct {
    void *impl;
} PPOSLFNativeTypeVMV1;

/*
 * A capability set is a finite, canonical collection of ground rows for the
 * final program's authored open relations.  Preparation rejects undeclared,
 * closed, malformed, or duplicate rows and is transactional.  The set owns
 * its canonical row copies and is bound cryptographically to the admitted
 * program, but it contains no callback or native semantic implementation.
 */
typedef struct {
    void *impl;
} PPOSLFNativeCapabilitySetV1;

void pposlf_native_type_vm_v1_init(PPOSLFNativeTypeVMV1 *vm);
void pposlf_native_type_vm_v1_free(PPOSLFNativeTypeVMV1 *vm);

/*
 * Preparation is transactional: a rejected replacement leaves an already
 * prepared VM unchanged.  The admitted plan must remain alive and unchanged
 * while the VM is used because generated discriminant spans are borrowed.
 */
bool pposlf_native_type_vm_v1_prepare(
    PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size);

void pposlf_native_capability_set_v1_init(
    PPOSLFNativeCapabilitySetV1 *capabilities);
void pposlf_native_capability_set_v1_free(
    PPOSLFNativeCapabilitySetV1 *capabilities);

bool pposlf_native_capability_set_v1_prepare(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Prepare the same canonical capability set while borrowing the immutable
 * ground row graphs.  The caller must keep every row graph alive and
 * unchanged until the capability set is freed.  Canonical ordering, digest
 * ownership, validation, duplicate rejection, and proof observations are
 * identical to the owning preparation above.
 */
bool pposlf_native_capability_set_v1_prepare_borrowed(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Extend an already prepared borrowed set with further immutable rows.  Only
 * the new rows are rendered and validated; the resulting set nevertheless
 * has exactly the canonical order and digest of preparing the union at once.
 * The operation is transactional and rejects duplicates across the old/new
 * boundary.  All borrowed row graphs must remain alive until the set is
 * freed.
 */
bool pposlf_native_capability_set_v1_append_borrowed(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Append to a stable borrowed prefix without computing an intermediate
 * receipt digest.  The resulting set may only be used as the base of a
 * committed overlay (or a later eager append); direct proof execution fails
 * closed until that commitment is produced.
 */
bool pposlf_native_capability_set_v1_append_borrowed_deferred(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Prepare a temporary canonical view of a stable admitted base plus fresh
 * immutable rows.  The view borrows both the base rows/canonical bytes and
 * the fresh row graphs, so the base and all fresh rows must outlive it.  Its
 * canonical order, digest, duplicate rejection, and proof-event indices are
 * identical to one-shot preparation of the union.
 */
bool pposlf_native_capability_set_v1_prepare_borrowed_overlay(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const PPOSLFNativeCapabilitySetV1 *base,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Prepare the same exact overlay without computing its receipt digest.  The
 * admitted rows, canonical order, duplicate rejection, and proof-event
 * indices are unchanged.  The ordinary certified proof entry point refuses
 * this view until pposlf_native_capability_set_v1_commit_digest succeeds.
 */
bool pposlf_native_capability_set_v1_prepare_borrowed_overlay_deferred(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const PPOSLFNativeCapabilitySetV1 *base,
    Atom *const *rows,
    uint32_t row_len,
    char *error_buf,
    size_t error_buf_size);

/*
 * Force the canonical receipt digest of an admitted capability set.  This is
 * idempotent and computes the same digest as eager one-shot preparation.
 */
bool pposlf_native_capability_set_v1_commit_digest(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    char digest_out[65]);

/*
 * Admit an authored finite-Horn reflection stream as an extensional provider.
 * Every record must be a zero-premise source rule owned by expected_owner;
 * quoted variables, non-facts, and heads outside the program's open relations
 * are rejected.  This is the file-backed realization of the same canonical
 * row admission above, not a callback or a second semantic interface.
 */
bool pposlf_native_capability_set_v1_prepare_reflected_facts(
    PPOSLFNativeCapabilitySetV1 *capabilities,
    const PPOSLFNativeTypePlanV1 *plan,
    const char *answer_path,
    const char *expected_owner,
    char *error_buf,
    size_t error_buf_size);

void pposlf_native_vm_result_v1_init(PPOSLFNativeVMResultV1 *result);
void pposlf_native_vm_result_v1_free(PPOSLFNativeVMResultV1 *result);

/*
 * The result must first be initialized and must eventually be freed.  Queries
 * must be closed terms in the generated q-term carrier.  PROVED is a
 * positive receipt.  NO_PROOF means the finite search space was exhausted
 * within the stated limits; RESOURCE_EXHAUSTED never masquerades as rejection.
 */
bool pposlf_native_type_vm_v1_prove(
    const PPOSLFNativeTypeVMV1 *vm,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result);

bool pposlf_native_type_vm_v1_prove_with_capabilities(
    const PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeCapabilitySetV1 *capabilities,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result);

/*
 * Internal staging boundary for a structurally admitted capability view whose
 * receipt digest has not escaped.  Proof search and proof-event indices are
 * exact, but capability_digest_ready is false and capability_digest is empty.
 * The result is therefore not an exportable certificate.  The ordinary proof
 * entry point above remains fail-closed on the same uncommitted view.
 */
bool pposlf_native_type_vm_v1_prove_with_uncommitted_capabilities(
    const PPOSLFNativeTypeVMV1 *vm,
    const PPOSLFNativeCapabilitySetV1 *capabilities,
    Atom *query,
    PPOSLFNativeVMLimitsV1 limits,
    PPOSLFNativeVMResultV1 *result);

#endif
