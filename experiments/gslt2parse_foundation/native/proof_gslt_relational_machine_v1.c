#include "proof_gslt_relational_machine_v1.h"

#include "gslt_classified_value_v1.h"
#include "gslt_indexed_effect_machine_v1.h"
#include "gslt_repetition_admission_v1.h"
#include "gslt_reusable_buffer_v1.h"
#include "gslt_u32_index_v1.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTReferenceV1 proof;
    const PPProofGSLTPatternV1 *goal;
} PPProofRelationalMachineStackEntryV1;

typedef struct {
    uint32_t floating_kind;
    uint32_t essential_kind;
    uint32_t variable_kind;
    uint32_t unknown_token;
} PPProofRelationalMachineSelectorsV1;

typedef struct {
    uint32_t assertion;
    PPProofGSLTRelationalDeclarationV1 declaration;
} PPProofRelationalDeclarationCacheEntryV1;

/* A proof-call-local cache.  The first occurrence keeps using the admitted
 * scratch path; the second promotes an immutable declaration, and later
 * occurrences reuse it.  No entry survives the relational context that owns
 * its token pointers. */
typedef struct {
    CettaGsltReusableBufferV1 *entries;
    CettaGsltRepetitionAdmissionV1 *admission;
    uint64_t hit_len;
    uint64_t miss_len;
    uint32_t promotion_len;
    PPProofGSLTRelationalDeclarationSnapshotV1 snapshot;
    bool snapshot_ready;
    bool snapshot_rejected;
} PPProofRelationalDeclarationCacheV1;

typedef struct {
    pthread_mutex_t mutex;
    CettaGsltReusableBufferV1 stack;
    CettaGsltReusableBufferV1 actuals;
    CettaGsltReusableBufferV1 required_binder_ids;
    CettaGsltReusableBufferV1 ordered_rows;
    CettaGsltReusableBufferV1 binder_counts;
    CettaGsltReusableBufferV1 binding_schemas;
    CettaGsltReusableBufferV1 premise_schemas;
    CettaGsltReusableBufferV1 apartness_pairs;
    CettaGsltReusableBufferV1 ordered_premises;
    CettaGsltReusableBufferV1 prepared_bindings;
    CettaGsltReusableBufferV1 prepared_premises;
    CettaGsltReusableBufferV1 declaration_cache_entries;
    CettaGsltReusableBufferV1 prepared;
    CettaGsltReusableBufferV1 saved;
    CettaGsltU32IndexV1 required_binder_index;
    CettaGsltU32IndexV1 premise_label_index;
    CettaGsltU32IndexV1 action_index;
    CettaGsltRepetitionAdmissionV1 declaration_admission;
    CettaGsltRepetitionPolicyV1 declaration_repetition_policy;
    PPProofGSLTSequenceEvidenceProducerV1 evidence;
} PPProofRelationalMachineWorkspaceImplV1;

typedef struct {
    PPProofRelationalMachineWorkspaceImplV1 *impl;
    bool acquired;
    bool reused;
    bool full;
    PPProofGSLTRelationalDeclarationWorkspaceV1 declaration;
    PPProofGSLTRelationalPreparedWorkspaceV1 assertion_preparation;
    PPProofRelationalDeclarationCacheV1 declaration_cache;
} PPProofRelationalMachineWorkspaceLeaseV1;

static void ppproof_relational_machine_v1_clear_declaration_cache(
    CettaGsltReusableBufferV1 *entries) {
    uint32_t index;

    if (!entries || !entries->items)
        return;
    for (index = 0u; index < entries->len; index++) {
        PPProofRelationalDeclarationCacheEntryV1 *entry =
            (PPProofRelationalDeclarationCacheEntryV1 *)entries->items +
            index;
        ppproof_gslt_relational_declaration_v1_free(&entry->declaration);
        memset(entry, 0, sizeof(*entry));
    }
    entries->len = 0u;
}

bool ppproof_gslt_relational_machine_v1_workspace_init(
    PPProofGSLTRelationalMachineV1Workspace *workspace) {
    PPProofRelationalMachineWorkspaceImplV1 *impl;

    if (!workspace || workspace->impl)
        return false;
    impl = calloc(1u, sizeof(*impl));
    if (!impl)
        return false;
    if (pthread_mutex_init(&impl->mutex, NULL) != 0) {
        free(impl);
        return false;
    }
    cetta_gslt_u32_index_init_v1(&impl->required_binder_index);
    cetta_gslt_u32_index_init_v1(&impl->premise_label_index);
    cetta_gslt_u32_index_init_v1(&impl->action_index);
    cetta_gslt_repetition_admission_init_v1(
        &impl->declaration_admission);
    ppproof_gslt_sequence_evidence_producer_v1_init(&impl->evidence);
    if (!cetta_gslt_reusable_buffer_init_v1(
            &impl->stack,
            sizeof(PPProofRelationalMachineStackEntryV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->actuals,
            sizeof(PPProofGSLTRelationalActualHypothesisV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->required_binder_ids, sizeof(uint32_t)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->ordered_rows,
            sizeof(PPProofGSLTRelationalOrderedRowV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->binder_counts, sizeof(uint32_t)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->binding_schemas,
            sizeof(PPProofGSLTRelationalBindingSchemaV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->premise_schemas,
            sizeof(PPProofGSLTRelationalEssentialSchemaV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->apartness_pairs,
            sizeof(PPProofGSLTAssertionDisjointV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->ordered_premises,
            sizeof(PPProofGSLTRelationalOrderedHypothesisV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->prepared_bindings,
            sizeof(PPProofGSLTAssertionBindingV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->prepared_premises,
            sizeof(PPProofGSLTAssertionEssentialV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->declaration_cache_entries,
            sizeof(PPProofRelationalDeclarationCacheEntryV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->prepared, sizeof(CettaGsltClassifiedValueV1)) ||
        !cetta_gslt_reusable_buffer_init_v1(
            &impl->saved,
            sizeof(PPProofRelationalMachineStackEntryV1))) {
        cetta_gslt_reusable_buffer_free_v1(&impl->saved);
        cetta_gslt_reusable_buffer_free_v1(&impl->prepared);
        cetta_gslt_reusable_buffer_free_v1(
            &impl->declaration_cache_entries);
        cetta_gslt_reusable_buffer_free_v1(&impl->prepared_premises);
        cetta_gslt_reusable_buffer_free_v1(&impl->prepared_bindings);
        cetta_gslt_reusable_buffer_free_v1(&impl->ordered_premises);
        cetta_gslt_reusable_buffer_free_v1(&impl->apartness_pairs);
        cetta_gslt_reusable_buffer_free_v1(&impl->premise_schemas);
        cetta_gslt_reusable_buffer_free_v1(&impl->binding_schemas);
        cetta_gslt_reusable_buffer_free_v1(&impl->binder_counts);
        cetta_gslt_reusable_buffer_free_v1(&impl->ordered_rows);
        cetta_gslt_reusable_buffer_free_v1(&impl->required_binder_ids);
        cetta_gslt_reusable_buffer_free_v1(&impl->actuals);
        cetta_gslt_reusable_buffer_free_v1(&impl->stack);
        cetta_gslt_u32_index_free_v1(&impl->premise_label_index);
        cetta_gslt_u32_index_free_v1(&impl->required_binder_index);
        cetta_gslt_u32_index_free_v1(&impl->action_index);
        cetta_gslt_repetition_admission_free_v1(
            &impl->declaration_admission);
        ppproof_gslt_sequence_evidence_producer_v1_free(&impl->evidence);
        (void)pthread_mutex_destroy(&impl->mutex);
        free(impl);
        return false;
    }
    workspace->impl = impl;
    return true;
}

bool ppproof_gslt_relational_machine_v1_workspace_set_repetition_policy(
    PPProofGSLTRelationalMachineV1Workspace *workspace,
    CettaGsltRepetitionPolicyV1 policy) {
    PPProofRelationalMachineWorkspaceImplV1 *impl;

    if (!workspace || !(impl = workspace->impl) ||
        (policy != CETTA_GSLT_REPETITION_POLICY_NONE_V1 &&
         policy !=
             CETTA_GSLT_REPETITION_POLICY_SECOND_OCCURRENCE_V1) ||
        pthread_mutex_trylock(&impl->mutex) != 0)
        return false;
    impl->declaration_repetition_policy = policy;
    (void)pthread_mutex_unlock(&impl->mutex);
    return true;
}

void ppproof_gslt_relational_machine_v1_workspace_free(
    PPProofGSLTRelationalMachineV1Workspace *workspace) {
    PPProofRelationalMachineWorkspaceImplV1 *impl;

    if (!workspace || !workspace->impl)
        return;
    impl = workspace->impl;
    ppproof_relational_machine_v1_clear_declaration_cache(
        &impl->declaration_cache_entries);
    ppproof_gslt_sequence_evidence_producer_v1_free(&impl->evidence);
    cetta_gslt_repetition_admission_free_v1(
        &impl->declaration_admission);
    cetta_gslt_u32_index_free_v1(&impl->premise_label_index);
    cetta_gslt_u32_index_free_v1(&impl->required_binder_index);
    cetta_gslt_u32_index_free_v1(&impl->action_index);
    cetta_gslt_reusable_buffer_free_v1(&impl->saved);
    cetta_gslt_reusable_buffer_free_v1(&impl->prepared);
    cetta_gslt_reusable_buffer_free_v1(
        &impl->declaration_cache_entries);
    cetta_gslt_reusable_buffer_free_v1(&impl->prepared_premises);
    cetta_gslt_reusable_buffer_free_v1(&impl->prepared_bindings);
    cetta_gslt_reusable_buffer_free_v1(&impl->ordered_premises);
    cetta_gslt_reusable_buffer_free_v1(&impl->apartness_pairs);
    cetta_gslt_reusable_buffer_free_v1(&impl->premise_schemas);
    cetta_gslt_reusable_buffer_free_v1(&impl->binding_schemas);
    cetta_gslt_reusable_buffer_free_v1(&impl->binder_counts);
    cetta_gslt_reusable_buffer_free_v1(&impl->ordered_rows);
    cetta_gslt_reusable_buffer_free_v1(&impl->required_binder_ids);
    cetta_gslt_reusable_buffer_free_v1(&impl->actuals);
    cetta_gslt_reusable_buffer_free_v1(&impl->stack);
    (void)pthread_mutex_destroy(&impl->mutex);
    free(impl);
    workspace->impl = NULL;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_workspace_acquire(
    PPProofGSLTRelationalMachineV1Workspace *workspace,
    bool full,
    PPProofRelationalMachineWorkspaceLeaseV1 *lease) {
    PPProofRelationalMachineWorkspaceImplV1 *impl;
    int lock_result;

    if (!lease)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    memset(lease, 0, sizeof(*lease));
    if (!workspace)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    impl = workspace->impl;
    if (!impl)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    lock_result = pthread_mutex_trylock(&impl->mutex);
    if (lock_result == EBUSY)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    if (lock_result != 0)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    lease->impl = impl;
    lease->full = full;
    lease->reused = impl->stack.capacity != 0u ||
                    impl->actuals.capacity != 0u ||
                    impl->required_binder_ids.capacity != 0u ||
                    impl->ordered_rows.capacity != 0u ||
                    impl->binding_schemas.capacity != 0u ||
                    impl->premise_schemas.capacity != 0u ||
                    impl->apartness_pairs.capacity != 0u ||
                    impl->ordered_premises.capacity != 0u ||
                    impl->prepared_bindings.capacity != 0u ||
                    impl->prepared_premises.capacity != 0u ||
                    impl->action_index.cap != 0u ||
                    impl->declaration_cache_entries.capacity != 0u ||
                    impl->declaration_admission.seen.cap != 0u ||
                    impl->declaration_admission.promoted.cap != 0u ||
                    impl->evidence.implementation != NULL ||
                    (full && (impl->prepared.capacity != 0u ||
                              impl->saved.capacity != 0u ||
                              impl->binder_counts.capacity != 0u ||
                              impl->required_binder_index.cap != 0u ||
                              impl->premise_label_index.cap != 0u));
    if (cetta_gslt_reusable_buffer_acquire_v1(&impl->stack) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->actuals) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(
            &impl->required_binder_ids) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->ordered_rows) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->binding_schemas) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->premise_schemas) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->apartness_pairs) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->ordered_premises) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->prepared_bindings) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(&impl->prepared_premises) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        cetta_gslt_reusable_buffer_acquire_v1(
            &impl->declaration_cache_entries) !=
            CETTA_GSLT_REUSABLE_BUFFER_OK_V1 ||
        (full &&
         cetta_gslt_reusable_buffer_acquire_v1(&impl->binder_counts) !=
             CETTA_GSLT_REUSABLE_BUFFER_OK_V1) ||
        (full &&
         cetta_gslt_reusable_buffer_acquire_v1(&impl->prepared) !=
             CETTA_GSLT_REUSABLE_BUFFER_OK_V1) ||
        (full &&
         cetta_gslt_reusable_buffer_acquire_v1(&impl->saved) !=
             CETTA_GSLT_REUSABLE_BUFFER_OK_V1)) {
        if (impl->saved.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->saved);
        if (impl->prepared.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->prepared);
        if (impl->ordered_rows.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->ordered_rows);
        if (impl->prepared_premises.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->prepared_premises);
        if (impl->declaration_cache_entries.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->declaration_cache_entries);
        if (impl->prepared_bindings.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->prepared_bindings);
        if (impl->ordered_premises.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->ordered_premises);
        if (impl->apartness_pairs.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->apartness_pairs);
        if (impl->premise_schemas.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->premise_schemas);
        if (impl->binding_schemas.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->binding_schemas);
        if (impl->binder_counts.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->binder_counts);
        if (impl->required_binder_ids.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->required_binder_ids);
        if (impl->actuals.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->actuals);
        if (impl->stack.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->stack);
        (void)pthread_mutex_unlock(&impl->mutex);
        memset(lease, 0, sizeof(*lease));
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    lease->declaration = (PPProofGSLTRelationalDeclarationWorkspaceV1){
        .required_binder_ids = &impl->required_binder_ids,
        .ordered_rows = &impl->ordered_rows,
        .binding_schemas = &impl->binding_schemas,
        .premise_schemas = &impl->premise_schemas,
        .apartness_pairs = &impl->apartness_pairs,
        .ordered_premises = &impl->ordered_premises,
    };
    lease->assertion_preparation =
        (PPProofGSLTRelationalPreparedWorkspaceV1){
            .bindings = &impl->prepared_bindings,
            .premises = &impl->prepared_premises,
        };
    cetta_gslt_repetition_admission_reset_v1(
        &impl->declaration_admission);
    cetta_gslt_u32_index_reset_v1(&impl->action_index);
    if (!cetta_gslt_repetition_admission_validate_v1(
            &impl->declaration_admission) ||
        !cetta_gslt_u32_index_validate_v1(&impl->action_index)) {
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->declaration_cache_entries);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->prepared_premises);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->prepared_bindings);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->ordered_premises);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->apartness_pairs);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->premise_schemas);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->binding_schemas);
        if (impl->binder_counts.active)
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->binder_counts);
        (void)cetta_gslt_reusable_buffer_release_v1(&impl->ordered_rows);
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->required_binder_ids);
        (void)cetta_gslt_reusable_buffer_release_v1(&impl->actuals);
        (void)cetta_gslt_reusable_buffer_release_v1(&impl->stack);
        if (impl->saved.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->saved);
        if (impl->prepared.active)
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->prepared);
        (void)pthread_mutex_unlock(&impl->mutex);
        memset(lease, 0, sizeof(*lease));
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    if (impl->declaration_repetition_policy ==
        CETTA_GSLT_REPETITION_POLICY_SECOND_OCCURRENCE_V1) {
        lease->declaration_cache = (PPProofRelationalDeclarationCacheV1){
            .entries = &impl->declaration_cache_entries,
            .admission = &impl->declaration_admission,
        };
    }
    if (full) {
        cetta_gslt_u32_index_reset_v1(&impl->required_binder_index);
        cetta_gslt_u32_index_reset_v1(&impl->premise_label_index);
        if (!cetta_gslt_u32_index_validate_v1(
                &impl->required_binder_index) ||
            !cetta_gslt_u32_index_validate_v1(
                &impl->premise_label_index)) {
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->binder_counts);
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->saved);
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->prepared);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->ordered_rows);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->prepared_premises);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->declaration_cache_entries);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->prepared_bindings);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->ordered_premises);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->apartness_pairs);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->premise_schemas);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->binding_schemas);
            (void)cetta_gslt_reusable_buffer_release_v1(
                &impl->required_binder_ids);
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->actuals);
            (void)cetta_gslt_reusable_buffer_release_v1(&impl->stack);
            (void)pthread_mutex_unlock(&impl->mutex);
            memset(lease, 0, sizeof(*lease));
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
        }
    }
    lease->acquired = true;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static void ppproof_relational_machine_v1_workspace_release(
    PPProofRelationalMachineWorkspaceLeaseV1 *lease,
    void *stack, uint32_t stack_len, uint32_t stack_cap,
    void *prepared, uint32_t prepared_len, uint32_t prepared_cap,
    void *saved, uint32_t saved_len, uint32_t saved_cap,
    PPProofGSLTRelationalMachineV1Receipt *receipt) {
    PPProofRelationalMachineWorkspaceImplV1 *impl;

    if (!lease || !lease->acquired || !lease->impl)
        return;
    impl = lease->impl;
    impl->stack.items = stack;
    impl->stack.len = stack_len;
    impl->stack.capacity = stack_cap;
    if (lease->full) {
        impl->prepared.items = prepared;
        impl->prepared.len = prepared_len;
        impl->prepared.capacity = prepared_cap;
        impl->saved.items = saved;
        impl->saved.len = saved_len;
        impl->saved.capacity = saved_cap;
    }
    if (receipt) {
        PPProofGSLTSequenceEvidenceWorkspaceStatsV1 evidence_stats = {0};
        bool has_evidence_stats =
            ppproof_gslt_sequence_evidence_producer_v1_workspace_stats(
                &impl->evidence, &evidence_stats);

        receipt->workspace_used = true;
        receipt->workspace_reused = lease->reused;
        receipt->workspace_stack_capacity = stack_cap;
        receipt->workspace_prepared_capacity =
            lease->full ? prepared_cap : 0u;
        receipt->workspace_saved_capacity = lease->full ? saved_cap : 0u;
        receipt->workspace_action_index_capacity =
            impl->action_index.cap;
        receipt->workspace_actual_capacity = impl->actuals.capacity;
        receipt->workspace_required_binder_capacity =
            impl->required_binder_ids.capacity;
        receipt->workspace_ordered_row_capacity =
            impl->ordered_rows.capacity;
        receipt->workspace_binder_count_capacity =
            lease->full ? impl->binder_counts.capacity : 0u;
        receipt->workspace_required_binder_index_capacity =
            lease->full ? impl->required_binder_index.cap : 0u;
        receipt->workspace_premise_label_index_capacity =
            lease->full ? impl->premise_label_index.cap : 0u;
        receipt->workspace_binding_schema_capacity =
            impl->binding_schemas.capacity;
        receipt->workspace_premise_schema_capacity =
            impl->premise_schemas.capacity;
        receipt->workspace_apartness_capacity =
            impl->apartness_pairs.capacity;
        receipt->workspace_ordered_premise_capacity =
            impl->ordered_premises.capacity;
        receipt->workspace_prepared_binding_capacity =
            impl->prepared_bindings.capacity;
        receipt->workspace_prepared_premise_capacity =
            impl->prepared_premises.capacity;
        receipt->workspace_declaration_cache_capacity =
            impl->declaration_cache_entries.capacity;
        receipt->workspace_call_control_count = 2u;
        receipt->workspace_evidence_arena_capacity =
            has_evidence_stats
                ? (uint64_t)evidence_stats.arena_reserved_bytes : 0u;
        receipt->workspace_evidence_node_capacity =
            has_evidence_stats ? evidence_stats.node_capacity : 0u;
        receipt->workspace_evidence_cache_capacity =
            has_evidence_stats
                ? evidence_stats.canonical_cache_capacity : 0u;
        receipt->declaration_cache_hit_len =
            lease->declaration_cache.hit_len;
        receipt->declaration_cache_miss_len =
            lease->declaration_cache.miss_len;
        receipt->declaration_cache_promotion_len =
            lease->declaration_cache.promotion_len;
        receipt->declaration_cache_snapshot_rejected =
            lease->declaration_cache.snapshot_rejected;
    }
    ppproof_relational_machine_v1_clear_declaration_cache(
        &impl->declaration_cache_entries);
    cetta_gslt_repetition_admission_reset_v1(
        &impl->declaration_admission);
    if (lease->full) {
        (void)cetta_gslt_reusable_buffer_release_v1(&impl->saved);
        (void)cetta_gslt_reusable_buffer_release_v1(&impl->prepared);
    }
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->ordered_rows);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->prepared_premises);
    (void)cetta_gslt_reusable_buffer_release_v1(
        &impl->declaration_cache_entries);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->prepared_bindings);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->ordered_premises);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->apartness_pairs);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->premise_schemas);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->binding_schemas);
    if (lease->full)
        (void)cetta_gslt_reusable_buffer_release_v1(
            &impl->binder_counts);
    (void)cetta_gslt_reusable_buffer_release_v1(
        &impl->required_binder_ids);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->actuals);
    (void)cetta_gslt_reusable_buffer_release_v1(&impl->stack);
    (void)pthread_mutex_unlock(&impl->mutex);
    memset(lease, 0, sizeof(*lease));
}

CettaGsltRepetitionCostQualificationV1
ppproof_gslt_relational_machine_v1_receipt_repetition_cost_qualify(
    const PPProofGSLTRelationalMachineV1Receipt *receipt,
    const CettaGsltRepetitionCostModelV1 *model,
    uint64_t *cached_cost_out,
    uint64_t *fresh_cost_out) {
    uint64_t occurrences;

    if (!receipt || receipt->declaration_cache_snapshot_rejected ||
        receipt->declaration_cache_miss_len >
            UINT64_MAX - receipt->declaration_cache_hit_len)
        return CETTA_GSLT_REPETITION_COST_INVALID_V1;
    occurrences = receipt->declaration_cache_miss_len +
        receipt->declaration_cache_hit_len;
    return cetta_gslt_repetition_cost_qualify_v1(
        model, occurrences,
        receipt->declaration_cache_miss_len,
        receipt->declaration_cache_hit_len,
        receipt->declaration_cache_promotion_len,
        cached_cost_out, fresh_cost_out);
}

static CettaGsltIndexedInstructionPlanV1
ppproof_relational_machine_v1_indexed_plan(
    const PPProofIndexedProgramPlanV1 *program) {
    if (!program)
        return (CettaGsltIndexedInstructionPlanV1){0};
    return (CettaGsltIndexedInstructionPlanV1){
        .terminal_low = program->terminal_low,
        .terminal_high = program->terminal_high,
        .continuation_low = program->continuation_low,
        .continuation_high = program->continuation_high,
        .save_byte = program->save_byte,
        .unknown_byte = program->unknown_byte,
        .terminal_radix = program->terminal_radix,
        .terminal_digit_bias = program->terminal_digit_bias,
        .continuation_radix = program->continuation_radix,
        .continuation_digit_bias = program->continuation_digit_bias,
        .save_placement = program->save_placement,
    };
}

static bool ppproof_relational_machine_v1_indexed_program_matches(
    const PPProofIndexedProgramPlanV1 *program,
    const PPProofGSLTRelationalExecutionDescriptorV1 *execution) {
    PPProofIndexedEffectUnknownV1 unknown_effect;

    if (!program || !execution || !execution->machine)
        return false;
    unknown_effect = execution->unknown_policy ==
                             PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM
                         ? PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE
                         : PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT;
    return strcmp(program->machine, execution->machine) == 0 &&
           program->terminal_low == execution->terminal_low &&
           program->terminal_high == execution->terminal_high &&
           program->continuation_low == execution->continuation_low &&
           program->continuation_high == execution->continuation_high &&
           program->save_byte == execution->save_byte &&
           program->unknown_byte == execution->unknown_byte &&
           program->terminal_radix == execution->terminal_radix &&
           program->terminal_digit_bias == execution->terminal_digit_bias &&
           program->continuation_radix == execution->continuation_radix &&
           program->continuation_digit_bias ==
               execution->continuation_digit_bias &&
           program->save_placement == execution->save_placement &&
           program->header_hypothesis_policy ==
               execution->header_hypothesis_policy &&
           program->unknown_effect == unknown_effect;
}

static bool ppproof_relational_machine_v1_prepared_prefix_contains(
    const CettaGsltClassifiedValueV1 *prepared,
    uint32_t prefix_len,
    uint32_t value) {
    uint32_t index;

    if (prefix_len != 0u && !prepared)
        return false;
    for (index = 0u; index < prefix_len; index++)
        if (prepared[index].value == value)
            return true;
    return false;
}

static void ppproof_relational_machine_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_from_article(
    PPProofGSLTArticleV1Result result) {
    switch (result) {
    case PPPROOF_GSLT_ARTICLE_V1_OK:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    case PPPROOF_GSLT_ARTICLE_V1_REJECTED:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    case PPPROOF_GSLT_ARTICLE_V1_RESOURCE:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    case PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED;
    case PPPROOF_GSLT_ARTICLE_V1_INVALID:
    default:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
}

static bool ppproof_relational_machine_v1_slice_valid(
    PPRelationalValueV1Slice slice) {
    return slice.bytes && slice.len != 0u;
}

static bool ppproof_relational_machine_v1_intern(
    const PPRelationalStoreV1 *store,
    PPRelationalValueV1Slice slice,
    uint32_t *value_out) {
    return ppproof_relational_machine_v1_slice_valid(slice) &&
           store->value_intern(
               store->context, slice.bytes, slice.len, value_out);
}

static bool ppproof_relational_machine_v1_intern_literal(
    const PPRelationalStoreV1 *store,
    PPRelationalStateLiteralV1 literal,
    uint32_t *value_out) {
    return literal.bytes && literal.len != 0u &&
           store->value_intern(
               store->context, literal.bytes, literal.len, value_out);
}

static bool ppproof_relational_machine_v1_intern_selector(
    const PPRelationalStoreV1 *store,
    PPProofGSLTRelationalSelectorV1 selector,
    uint32_t *value_out) {
    return selector.bytes && selector.len != 0u &&
           store->value_intern(
               store->context, selector.bytes, selector.len, value_out);
}

static bool ppproof_relational_machine_v1_slice_equals_literal(
    PPRelationalValueV1Slice slice,
    PPRelationalStateLiteralV1 literal) {
    return slice.len == literal.len && slice.bytes && literal.bytes &&
           memcmp(slice.bytes, literal.bytes, slice.len) == 0;
}

static bool ppproof_relational_machine_v1_formula_value(
    const PPRelationalStoreV1 *store,
    const PPRelationalValueV1Slice *items,
    uint32_t item_len,
    uint32_t *value_out) {
    uint8_t *encoded = NULL;
    uint32_t encoded_len = 0u;
    uint32_t index;
    bool result = false;

    if (!store || !items || item_len == 0u || !value_out)
        return false;
    for (index = 0u; index < item_len; index++) {
        if (!ppproof_relational_machine_v1_slice_valid(items[index]))
            return false;
    }
    if (pprelational_value_list_v1_encode_items(
            items, item_len, &encoded, &encoded_len) &&
        store->value_intern(
            store->context, encoded, encoded_len, value_out))
        result = true;
    free(encoded);
    return result;
}

static bool ppproof_relational_machine_v1_formula_equal(
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right) {
    uint32_t index;

    if (left.token_len != right.token_len ||
        (left.token_len != 0u && (!left.tokens || !right.tokens)))
        return false;
    for (index = 0u; index < left.token_len; index++) {
        if (left.tokens[index] != right.tokens[index])
            return false;
    }
    return true;
}

static PPProofGSLTNameV1 ppproof_relational_machine_v1_token_name(
    const PPProofGSLTSequenceTokenV1 *token) {
    if (!token || !token->term ||
        token->term->kind != PPPROOF_GSLT_PATTERN_V1_APPLY ||
        token->term->as.apply.argument_len != 0u)
        return (PPProofGSLTNameV1){0};
    return token->term->as.apply.constructor;
}

static void ppproof_relational_machine_v1_append_formula(
    char *buf, size_t size, const char *prefix,
    PPProofGSLTTokenSequenceV1 formula) {
    size_t used;
    uint32_t index;

    if (!buf || size == 0u || !prefix)
        return;
    used = strlen(buf);
    if (used >= size)
        return;
    (void)snprintf(buf + used, size - used, "%s[", prefix);
    for (index = 0u; index < formula.token_len && index < 32u; index++) {
        PPProofGSLTNameV1 name =
            ppproof_relational_machine_v1_token_name(
                formula.tokens ? formula.tokens[index] : NULL);
        used = strlen(buf);
        if (used >= size)
            return;
        (void)snprintf(
            buf + used, size - used, "%s%.*s",
            index == 0u ? "" : " ",
            (int)(name.len > 40u ? 40u : name.len),
            name.bytes ? (const char *)name.bytes : "?");
    }
    used = strlen(buf);
    if (used < size)
        (void)snprintf(
            buf + used, size - used, "%s]",
            formula.token_len > 32u ? " ..." : "");
}

static bool ppproof_relational_machine_v1_stack_push(
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    PPProofRelationalMachineStackEntryV1 entry) {
    PPProofRelationalMachineStackEntryV1 *next;
    uint32_t next_cap;

    if (!stack || !stack_len || !stack_cap || *stack_len == UINT32_MAX)
        return false;
    if (*stack_len == *stack_cap) {
        next_cap = *stack_cap ? *stack_cap * 2u : 32u;
        if (next_cap < *stack_cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(*stack, (size_t)next_cap * sizeof(*next));
        if (!next)
            return false;
        *stack = next;
        *stack_cap = next_cap;
    }
    (*stack)[(*stack_len)++] = entry;
    return true;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_push_incomplete_claim(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 claim,
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTReferenceV1 premise;
    PPProofGSLTSequenceProofV1 proof;
    PPProofGSLTArticleV1Result article_result;

    article_result = ppproof_gslt_relational_context_v1_add_provable_premise(
        context, claim, &premise, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_sequence_evidence_producer_v1_use_premise(
            producer, claim, premise, &proof, error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(article_result);
    if (!ppproof_relational_machine_v1_stack_push(
            stack, stack_len, stack_cap,
            (PPProofRelationalMachineStackEntryV1){
                .formula = claim,
                .proof = proof.evidence,
                .goal = proof.goal,
            }))
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static bool ppproof_relational_machine_v1_grow(
    void **items, uint32_t *capacity, uint32_t required,
    uint32_t maximum_len, size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (!items || !capacity || item_size == 0u ||
        required > maximum_len)
        return false;
    if (required <= *capacity)
        return true;
    next_cap = *capacity ? *capacity * 2u : 32u;
    if (next_cap < *capacity)
        return false;
    if (next_cap < required)
        next_cap = required;
    if (next_cap > maximum_len)
        next_cap = maximum_len;
    if (next_cap < required ||
        (size_t)next_cap > SIZE_MAX / item_size)
        return false;
    next = realloc(*items, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *items = next;
    *capacity = next_cap;
    return true;
}

static bool ppproof_relational_machine_v1_classify_label(
    CettaGsltClassifiedValueV1 *classified,
    uint32_t label,
    uint32_t source_kind,
    const CettaGsltU32IndexV1 *action_index) {
    uint32_t classification;

    if (!classified ||
        !cetta_gslt_u32_index_find_v1(
            action_index, source_kind, &classification))
        return false;
    return cetta_gslt_classified_value_init_v1(
        classified, label, classification,
        (uint32_t)PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME);
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_build_action_index(
    const PPRelationalStoreV1 *store,
    const char *machine,
    const PPProofPreparedActionCaseV1 *source_cases,
    uint32_t source_case_len,
    CettaGsltU32IndexV1 *action_index,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (!pprelational_store_v1_valid(store) || !machine || !source_cases ||
        source_case_len == 0u || !action_index) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated proof action relation is malformed");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    cetta_gslt_u32_index_reset_v1(action_index);
    if (!cetta_gslt_u32_index_validate_v1(action_index))
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    for (index = 0u; index < source_case_len; index++) {
        const PPProofPreparedActionCaseV1 *source = &source_cases[index];
        CettaGsltU32IndexInsertResultV1 inserted;
        uint32_t source_kind;
        size_t source_len;

        if (!source->machine || strcmp(source->machine, machine) != 0 ||
            !source->source_kind || source->source_kind[0] == '\0' ||
            source->action <= PPPROOF_PREPARED_ACTION_V1_INVALID ||
            source->action > PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "generated proof action relation contains an invalid case");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
        }
        source_len = strlen(source->source_kind);
        if (source_len > UINT32_MAX ||
            !store->value_intern(
                store->context, (const uint8_t *)source->source_kind,
                (uint32_t)source_len, &source_kind)) {
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        }
        inserted = cetta_gslt_u32_index_insert_unique_v1(
            action_index, source_kind, (uint32_t)source->action);
        if (inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "generated proof action relation repeats a source kind");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
        }
        if (inserted == CETTA_GSLT_U32_INDEX_RESOURCE_V1)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    return cetta_gslt_u32_index_validate_v1(action_index)
               ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK
               : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
}

static bool ppproof_relational_machine_v1_label_push(
    CettaGsltClassifiedValueV1 **labels,
    uint32_t *label_len,
    uint32_t *label_cap,
    CettaGsltClassifiedValueV1 classified,
    uint32_t other_len,
    uint32_t maximum_len) {
    if (!labels || !label_len || !label_cap ||
        other_len > maximum_len || *label_len >= maximum_len - other_len ||
        *label_len == UINT32_MAX ||
        !ppproof_relational_machine_v1_grow(
            (void **)labels, label_cap, *label_len + 1u,
            maximum_len, sizeof(**labels)))
        return false;
    (*labels)[(*label_len)++] = classified;
    return true;
}

static bool ppproof_relational_machine_v1_saved_push(
    PPProofRelationalMachineStackEntryV1 **saved,
    uint32_t *saved_len, uint32_t *saved_cap,
    PPProofRelationalMachineStackEntryV1 entry,
    uint32_t prepared_len, uint32_t maximum_len) {
    if (!saved || !saved_len || !saved_cap ||
        prepared_len > maximum_len ||
        *saved_len >= maximum_len - prepared_len ||
        *saved_len == UINT32_MAX ||
        !ppproof_relational_machine_v1_grow(
            (void **)saved, saved_cap, *saved_len + 1u,
            maximum_len, sizeof(**saved)))
        return false;
    (*saved)[(*saved_len)++] = entry;
    return true;
}

static int ppproof_relational_machine_v1_frame_row_compare(
    const void *left, const void *right) {
    const PPProofGSLTRelationalOrderedRowV1 *left_row = left;
    const PPProofGSLTRelationalOrderedRowV1 *right_row = right;
    if (left_row->position < right_row->position)
        return -1;
    if (left_row->position > right_row->position)
        return 1;
    if (left_row->label < right_row->label)
        return -1;
    return left_row->label > right_row->label ? 1 : 0;
}

static bool ppproof_relational_machine_v1_active_hypothesis(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    uint32_t authority,
    uint32_t hypothesis) {
    uint64_t cursor = UINT64_MAX;
    uint32_t row[2];
    uint32_t matches = 0u;

    for (;;) {
        bool found = false;
        if (!store->table_prefix_next(
                store->context,
                relational_plan->resolved_table_ids[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS],
                &authority, 1u, UINT32_C(1) << 1u,
                &cursor, row, 2u, &found))
            return false;
        if (!found)
            return matches == 1u;
        if (row[1] == hypothesis) {
            if (matches == UINT32_MAX)
                return false;
            matches++;
        }
    }
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_preload_frame(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofRelationalMachineSelectorsV1 *selectors,
    const CettaGsltU32IndexV1 *action_index,
    uint32_t authority,
    CettaGsltClassifiedValueV1 **prepared_labels,
    uint32_t *prepared_label_len,
    uint32_t *prepared_label_cap,
    CettaGsltReusableBufferV1 *binder_count_workspace,
    CettaGsltReusableBufferV1 *ordered_row_workspace,
    CettaGsltU32IndexV1 *required_binder_index_workspace,
    CettaGsltU32IndexV1 *premise_label_index_workspace,
    uint32_t maximum_len,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *binder_counts = NULL;
    uint32_t mandatory_len = 0u;
    PPProofGSLTRelationalOrderedRowV1 *ordered = NULL;
    uint32_t ordered_len = 0u;
    uint32_t ordered_cap = 0u;
    uint64_t cursor = UINT64_MAX;
    PPProofGSLTRelationalMachineV1Result result =
        PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    CettaGsltU32IndexV1 local_mandatory_lookup;
    CettaGsltU32IndexV1 local_label_index;
    CettaGsltU32IndexV1 *mandatory_lookup = &local_mandatory_lookup;
    CettaGsltU32IndexV1 *label_index = &local_label_index;
    uint32_t index;
    bool reuse_scratch = false;

    cetta_gslt_u32_index_init_v1(&local_mandatory_lookup);
    cetta_gslt_u32_index_init_v1(&local_label_index);
    if (binder_count_workspace || ordered_row_workspace ||
        required_binder_index_workspace || premise_label_index_workspace) {
        if (!binder_count_workspace || !ordered_row_workspace ||
            !required_binder_index_workspace ||
            !premise_label_index_workspace ||
            !binder_count_workspace->active ||
            !ordered_row_workspace->active ||
            binder_count_workspace->element_size !=
                sizeof(*binder_counts) ||
            ordered_row_workspace->element_size != sizeof(*ordered) ||
            !cetta_gslt_reusable_buffer_truncate_v1(
                binder_count_workspace, 0u) ||
            !cetta_gslt_reusable_buffer_truncate_v1(
                ordered_row_workspace, 0u)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "invalid generated frame workspace");
            goto done;
        }
        reuse_scratch = true;
        mandatory_lookup = required_binder_index_workspace;
        label_index = premise_label_index_workspace;
        cetta_gslt_u32_index_reset_v1(mandatory_lookup);
        cetta_gslt_u32_index_reset_v1(label_index);
        if (!cetta_gslt_u32_index_validate_v1(mandatory_lookup) ||
            !cetta_gslt_u32_index_validate_v1(label_index))
            goto done;
    }

    for (;;) {
        uint32_t row[2];
        bool found = false;
        if (!store->table_prefix_next(
                store->context,
                relational_plan->resolved_table_ids[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE],
                &authority, 1u, UINT32_C(1) << 1u,
                &cursor, row, 2u, &found))
            goto done;
        if (!found)
            break;
        CettaGsltU32IndexInsertResultV1 inserted;
        if (mandatory_len >= maximum_len) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        inserted = cetta_gslt_u32_index_insert_unique_v1(
            mandatory_lookup, row[1], mandatory_len);
        if (inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame repeats a mandatory variable");
            goto done;
        }
        if (inserted == CETTA_GSLT_U32_INDEX_RESOURCE_V1) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1)
            goto done;
        mandatory_len++;
    }
    if (!cetta_gslt_u32_index_validate_v1(mandatory_lookup))
        goto done;
    if (mandatory_len != 0u) {
        if (reuse_scratch) {
            uint32_t zero = 0u;
            for (index = 0u; index < mandatory_len; index++) {
                if (cetta_gslt_reusable_buffer_push_v1(
                        binder_count_workspace, &zero, maximum_len) !=
                    CETTA_GSLT_REUSABLE_BUFFER_OK_V1) {
                    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                    goto done;
                }
            }
            binder_counts = binder_count_workspace->items;
        } else {
            binder_counts = calloc(mandatory_len, sizeof(*binder_counts));
            if (!binder_counts) {
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                goto done;
            }
        }
    }

    cursor = UINT64_MAX;
    for (;;) {
        uint32_t row[3];
        uint32_t position;
        bool found = false;
        if (!store->table_prefix_next(
                store->context,
                relational_plan->resolved_table_ids[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS],
                &authority, 1u,
                (UINT32_C(1) << 1u) | (UINT32_C(1) << 2u),
                &cursor, row, 3u, &found))
            goto done;
        if (!found)
            break;
        if (!pprelational_store_v1_value_u32_decimal(
                store, row[1], &position)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame has a noncanonical ordered authority");
            goto done;
        }
        if (reuse_scratch) {
            PPProofGSLTRelationalOrderedRowV1 ordered_row = {
                position, row[2]
            };
            if (cetta_gslt_reusable_buffer_push_v1(
                    ordered_row_workspace, &ordered_row, maximum_len) !=
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1) {
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                goto done;
            }
            ordered = ordered_row_workspace->items;
            ordered_len = ordered_row_workspace->len;
            ordered_cap = ordered_row_workspace->capacity;
        } else {
            if (!ppproof_relational_machine_v1_grow(
                    (void **)&ordered, &ordered_cap,
                    ordered_len + 1u, maximum_len,
                    sizeof(*ordered))) {
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                goto done;
            }
            ordered[ordered_len++] =
                (PPProofGSLTRelationalOrderedRowV1){position, row[2]};
        }
    }
    if (ordered_len > 1u)
        qsort(ordered, ordered_len, sizeof(*ordered),
              ppproof_relational_machine_v1_frame_row_compare);
    for (index = 0u; index < ordered_len; index++) {
        CettaGsltClassifiedValueV1 classified;
        uint32_t kind_row[2];
        bool include = false;

        if (index != 0u &&
            ordered[index - 1u].position == ordered[index].position) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame has duplicate ordered authority");
            goto done;
        }
        {
            CettaGsltU32IndexInsertResultV1 inserted =
                cetta_gslt_u32_index_insert_unique_v1(
                    label_index, ordered[index].label, index);
            if (inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1) {
                ppproof_relational_machine_v1_set_error(
                    error_buf, error_buf_size,
                    "compressed frame repeats an active hypothesis");
                goto done;
            }
            if (inserted == CETTA_GSLT_U32_INDEX_RESOURCE_V1) {
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                goto done;
            }
            if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1)
                goto done;
        }
        if (!store->table_find(
                store->context,
                relational_plan->resolved_table_ids[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                &ordered[index].label, 1u, kind_row, 2u))
            goto done;
        if (kind_row[1] == selectors->floating_kind) {
            uint32_t variable_row[2];
            uint32_t mandatory_index;
            if (!store->table_find(
                    store->context,
                    relational_plan->resolved_table_ids[
                        PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE],
                    &ordered[index].label, 1u, variable_row, 2u))
                goto done;
            if (cetta_gslt_u32_index_find_v1(
                    mandatory_lookup, variable_row[1], &mandatory_index)) {
                if (mandatory_index >= mandatory_len ||
                    binder_counts[mandatory_index] == UINT32_MAX)
                    goto done;
                binder_counts[mandatory_index]++;
                include = true;
            }
        } else if (kind_row[1] == selectors->essential_kind) {
            include = true;
        } else {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame contains a non-hypothesis label");
            goto done;
        }
        if (include) {
            if (!ppproof_relational_machine_v1_classify_label(
                    &classified, ordered[index].label, kind_row[1],
                    action_index))
                goto done;
            if (!ppproof_relational_machine_v1_label_push(
                    prepared_labels, prepared_label_len,
                    prepared_label_cap, classified, 0u, maximum_len)) {
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                goto done;
            }
        }
    }
    if (!cetta_gslt_u32_index_validate_v1(label_index))
        goto done;
    for (index = 0u; index < mandatory_len; index++) {
        if (binder_counts[index] != 1u) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed frame lacks a unique mandatory binding");
            goto done;
        }
    }
    result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;

done:
    if (!reuse_scratch) {
        free(ordered);
        free(binder_counts);
    }
    if (!reuse_scratch) {
        cetta_gslt_u32_index_free_v1(&local_label_index);
        cetta_gslt_u32_index_free_v1(&local_mandatory_lookup);
    }
    return result;
}

static bool ppproof_relational_machine_v1_composition_valid(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofRelationalMachineSelectorsV1 *selectors_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalMachineSelectorsV1 selectors;
    if (!pprelational_store_v1_valid(store) || !state_plan ||
        !proof_plan ||
        !proof_plan->storage || !evidence_abi || !evidence_abi->storage ||
        !relational_plan || !relational_plan->storage || !selectors_out ||
        strcmp(relational_plan->proof_plan_digest,
               proof_plan->semantic_digest) != 0 ||
        strcmp(relational_plan->state_plan_digest,
               state_plan->plan_digest) != 0 ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->owner, evidence_abi->owner) ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->base, evidence_abi->base) ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->owner, relational_plan->owner) ||
        !ppproof_gslt_article_v1_name_equal(
            proof_plan->base, relational_plan->base)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof composition identities do not agree");
        return false;
    }
    if (!ppproof_relational_machine_v1_intern_literal(
            store, relational_plan->execution.unknown_token,
            &selectors.unknown_token) ||
        !ppproof_relational_machine_v1_intern_selector(
            store,
            relational_plan->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING],
            &selectors.floating_kind) ||
        !ppproof_relational_machine_v1_intern_selector(
            store,
            relational_plan->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL],
            &selectors.essential_kind) ||
        !ppproof_relational_machine_v1_intern_selector(
            store,
            relational_plan->selectors[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE],
            &selectors.variable_kind)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof selectors cannot be interned");
        return false;
    }
    if (selectors.floating_kind == selectors.essential_kind) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "relational proof selector roles are ambiguous");
        return false;
    }
    *selectors_out = selectors;
    return true;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_hypothesis(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    uint32_t authority,
    uint32_t label,
    PPProofRelationalMachineStackEntryV1 *entry_out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t formula_row[2];
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTReferenceV1 premise;
    PPProofGSLTSequenceProofV1 proof;
    PPProofGSLTArticleV1Result result;

    if (!ppproof_relational_machine_v1_active_hypothesis(
            store, relational_plan, authority, label)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof references a hypothesis outside its active snapshot");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (!store->table_find(
            store->context,
            relational_plan->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA],
            &label, 1u, formula_row, 2u)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "active proof hypothesis has no formula");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    result = ppproof_gslt_relational_context_v1_formula(
        context, formula_row[1], &formula, error_buf, error_buf_size);
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_relational_context_v1_add_provable_premise(
            context, formula, &premise, error_buf, error_buf_size);
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_producer_v1_use_premise(
            producer, formula, premise, &proof,
            error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(result);
    *entry_out = (PPProofRelationalMachineStackEntryV1){
        .formula = formula,
        .proof = proof.evidence,
        .goal = proof.goal,
    };
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_declaration(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t assertion,
    const PPProofGSLTRelationalDeclarationWorkspaceV1 *scratch,
    PPProofRelationalDeclarationCacheV1 *cache,
    uint32_t maximum_cache_len,
    PPProofGSLTRelationalDeclarationV1 *local,
    const PPProofGSLTRelationalDeclarationV1 **declaration_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTArticleV1Result elaborated;
    uint32_t entry_index;
    CettaGsltRepetitionDecisionV1 decision;
    CettaGsltRepetitionResultV1 admitted;

    if (!context || !local || !declaration_out)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    *declaration_out = NULL;
    if (!cache) {
        elaborated =
            ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                context, assertion, scratch, local,
                error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK)
            return ppproof_relational_machine_v1_from_article(elaborated);
        *declaration_out = local;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    }
    if (!cache->entries || !cache->admission ||
        !cache->entries->active ||
        cache->entries->element_size !=
            sizeof(PPProofRelationalDeclarationCacheEntryV1))
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    if (cache->snapshot_rejected) {
        if (cache->miss_len == UINT64_MAX)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        cache->miss_len++;
        elaborated =
            ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                context, assertion, scratch, local,
                error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK)
            return ppproof_relational_machine_v1_from_article(elaborated);
        *declaration_out = local;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    }
    admitted = cetta_gslt_repetition_admission_classify_v1(
        cache->admission, assertion, &decision, &entry_index);
    if (admitted != CETTA_GSLT_REPETITION_OK_V1)
        return admitted == CETTA_GSLT_REPETITION_RESOURCE_V1
                   ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE
                   : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    if (decision == CETTA_GSLT_REPETITION_HIT_V1) {
        const PPProofRelationalDeclarationCacheEntryV1 *entry =
            cetta_gslt_reusable_buffer_at_const_v1(
                cache->entries, entry_index);
        if (!cache->snapshot_ready ||
            !ppproof_gslt_relational_context_v1_declaration_snapshot_matches(
                context, &cache->snapshot)) {
            cache->snapshot_rejected = true;
            if (cache->miss_len == UINT64_MAX)
                return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            cache->miss_len++;
            elaborated =
                ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                    context, assertion, scratch, local,
                    error_buf, error_buf_size);
            if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK)
                return ppproof_relational_machine_v1_from_article(elaborated);
            *declaration_out = local;
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
        }
        if (!entry || entry->assertion != assertion ||
            !entry->declaration.storage)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
        if (cache->hit_len == UINT64_MAX)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        cache->hit_len++;
        *declaration_out = &entry->declaration;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    }
    if (cache->miss_len == UINT64_MAX)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    cache->miss_len++;
    if (decision == CETTA_GSLT_REPETITION_FIRST_V1) {
        elaborated =
            ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                context, assertion, scratch, local,
                error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK)
            return ppproof_relational_machine_v1_from_article(elaborated);
        *declaration_out = local;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    }
    if (decision != CETTA_GSLT_REPETITION_PROMOTE_V1)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    if (cache->promotion_len == UINT32_MAX)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    if ((!cache->snapshot_ready &&
         !ppproof_gslt_relational_context_v1_declaration_snapshot(
             context, &cache->snapshot)) ||
        (cache->snapshot_ready &&
         !ppproof_gslt_relational_context_v1_declaration_snapshot_matches(
             context, &cache->snapshot))) {
        cache->snapshot_rejected = true;
        elaborated =
            ppproof_gslt_relational_declaration_v1_elaborate_with_workspace(
                context, assertion, scratch, local,
                error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK)
            return ppproof_relational_machine_v1_from_article(elaborated);
        *declaration_out = local;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    }
    cache->snapshot_ready = true;

    {
        PPProofRelationalDeclarationCacheEntryV1 entry = {
            .assertion = assertion,
        };
        CettaGsltReusableBufferResultV1 pushed;

        ppproof_gslt_relational_declaration_v1_init(&entry.declaration);
        elaborated = ppproof_gslt_relational_declaration_v1_elaborate(
            context, assertion, &entry.declaration,
            error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK)
            return ppproof_relational_machine_v1_from_article(elaborated);
        entry_index = cache->entries->len;
        pushed = cetta_gslt_reusable_buffer_push_v1(
            cache->entries, &entry, maximum_cache_len);
        if (pushed != CETTA_GSLT_REUSABLE_BUFFER_OK_V1) {
            ppproof_gslt_relational_declaration_v1_free(
                &entry.declaration);
            return pushed == CETTA_GSLT_REUSABLE_BUFFER_LIMIT_V1
                       ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED
                       : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        }
        admitted = cetta_gslt_repetition_admission_promote_v1(
            cache->admission, assertion, entry_index);
        if (admitted != CETTA_GSLT_REPETITION_OK_V1) {
            cache->entries->len--;
            ppproof_gslt_relational_declaration_v1_free(
                &entry.declaration);
            return admitted == CETTA_GSLT_REPETITION_RESOURCE_V1
                       ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE
                       : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
        }
        cache->promotion_len++;
        *declaration_out =
            &((PPProofRelationalDeclarationCacheEntryV1 *)
                  cache->entries->items)[entry_index]
                 .declaration;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    }
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_assertion(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    CettaGsltReusableBufferV1 *actual_workspace,
    const PPProofGSLTRelationalDeclarationWorkspaceV1 *declaration_workspace,
    const PPProofGSLTRelationalPreparedWorkspaceV1 *prepared_workspace,
    PPProofRelationalDeclarationCacheV1 *declaration_cache,
    uint32_t maximum_declaration_cache_len,
    uint32_t assertion,
    PPProofRelationalMachineStackEntryV1 *stack,
    uint32_t *stack_len,
    PPProofRelationalMachineStackEntryV1 *entry_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTRelationalDeclarationV1 local_schema;
    const PPProofGSLTRelationalDeclarationV1 *schema = NULL;
    PPProofGSLTRelationalPreparedAssertionV1 prepared;
    PPProofGSLTRelationalDeclarationStorageV1 declaration_control = {0};
    PPProofGSLTRelationalPreparedStorageV1 prepared_control = {0};
    PPProofGSLTRelationalDeclarationWorkspaceV1 declaration_call_workspace;
    PPProofGSLTRelationalPreparedWorkspaceV1 prepared_call_workspace;
    const PPProofGSLTRelationalDeclarationWorkspaceV1 *effective_declaration =
        declaration_workspace;
    const PPProofGSLTRelationalPreparedWorkspaceV1 *effective_prepared =
        prepared_workspace;
    PPProofGSLTRelationalActualHypothesisV1 *actuals = NULL;
    PPProofGSLTRelationalEvidenceV1 relational_evidence;
    PPProofGSLTSequenceEvidenceSourcesV1 sources;
    PPProofGSLTReferenceV1 declaration_reference;
    PPProofGSLTAssertionApplicationV1 application;
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTArticleV1Result result;
    PPProofGSLTRelationalMachineV1Result mapped;
    const char *failure_stage = "declaration elaboration";
    uint32_t stack_base;
    uint32_t index;

    ppproof_gslt_relational_declaration_v1_init(&local_schema);
    ppproof_gslt_relational_prepared_assertion_v1_init(&prepared);
    if (declaration_workspace) {
        declaration_call_workspace = *declaration_workspace;
        declaration_call_workspace.control = &declaration_control;
        effective_declaration = &declaration_call_workspace;
    }
    if (prepared_workspace) {
        prepared_call_workspace = *prepared_workspace;
        prepared_call_workspace.control = &prepared_control;
        effective_prepared = &prepared_call_workspace;
    }
    mapped = ppproof_relational_machine_v1_declaration(
        context, assertion, effective_declaration, declaration_cache,
        maximum_declaration_cache_len, &local_schema, &schema,
        error_buf, error_buf_size);
    if (mapped != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        if (mapped == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
            error_buf && error_buf_size != 0u && error_buf[0] == '\0')
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "assertion %u failed during %s", assertion,
                failure_stage);
        goto done;
    }
    if (schema->ordered_len > *stack_len) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof stack lacks the assertion's ordered hypotheses");
        mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        goto done;
    }
    if (actual_workspace) {
        if (!actual_workspace->active ||
            actual_workspace->element_size != sizeof(*actuals) ||
            !cetta_gslt_reusable_buffer_truncate_v1(
                actual_workspace, 0u)) {
            mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
            goto done;
        }
    } else if (schema->ordered_len != 0u) {
        if ((size_t)schema->ordered_len > SIZE_MAX / sizeof(*actuals) ||
            !(actuals = calloc(schema->ordered_len, sizeof(*actuals)))) {
            mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
    }
    stack_base = *stack_len - schema->ordered_len;
    for (index = 0u; index < schema->ordered_len; index++) {
        PPProofGSLTRelationalActualHypothesisV1 actual = {
            .formula = stack[stack_base + index].formula,
            .proof = stack[stack_base + index].proof,
        };
        if (actual_workspace) {
            if (cetta_gslt_reusable_buffer_push_v1(
                    actual_workspace, &actual,
                    schema->ordered_len) !=
                CETTA_GSLT_REUSABLE_BUFFER_OK_V1) {
                mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
                goto done;
            }
        } else {
            actuals[index] = actual;
        }
    }
    if (actual_workspace)
        actuals = actual_workspace->items;
    failure_stage = "assertion preparation";
    result =
        ppproof_gslt_relational_prepared_assertion_v1_build_with_workspace(
            schema, actuals, schema->ordered_len, effective_prepared,
            &prepared, error_buf, error_buf_size);
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "declaration-premise reservation";
        result = ppproof_gslt_relational_context_v1_reserve_premise(
            context, &declaration_reference, error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "evidence-source construction";
        result = ppproof_gslt_relational_context_v1_evidence_sources(
            context, &relational_evidence, &sources,
            error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "assertion evidence production";
        result = ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
            producer, &prepared.declaration, declaration_reference.index,
            &sources, &application, error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "declaration-premise completion";
        result = ppproof_gslt_relational_context_v1_fill_premise(
            context, declaration_reference, application.declared_goal,
            error_buf, error_buf_size);
    }
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
        failure_stage = "typed-result construction";
        result = ppproof_gslt_relational_context_v1_typed_formula(
            context, prepared.declaration.conclusion_type,
            (PPProofGSLTTokenSequenceV1){
                .tokens = application.result.tokens,
                .token_len = application.result.token_len,
            },
            &formula, error_buf, error_buf_size);
    }
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        if (error_buf && error_buf_size != 0u) {
            char detail[256] = "no diagnostic";

            if (error_buf[0] != '\0')
                (void)snprintf(detail, sizeof(detail), "%s", error_buf);
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "assertion %u failed during %s "
                "(ordered hypotheses %u, stack depth %u): %s",
                assertion, failure_stage, schema->ordered_len, *stack_len,
                detail);
        }
        mapped = ppproof_relational_machine_v1_from_article(result);
        goto done;
    }
    *stack_len = stack_base;
    *entry_out = (PPProofRelationalMachineStackEntryV1){
        .formula = formula,
        .proof = application.proof.evidence,
        .goal = application.proof.goal,
    };
    mapped = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;

done:
    if (!actual_workspace)
        free(actuals);
    ppproof_gslt_relational_prepared_assertion_v1_free(&prepared);
    ppproof_gslt_relational_declaration_v1_free(&local_schema);
    return mapped;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_apply_prepared_label(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    CettaGsltReusableBufferV1 *actual_workspace,
    const PPProofGSLTRelationalDeclarationWorkspaceV1 *declaration_workspace,
    const PPProofGSLTRelationalPreparedWorkspaceV1 *prepared_workspace,
    PPProofRelationalDeclarationCacheV1 *declaration_cache,
    uint32_t maximum_declaration_cache_len,
    uint32_t authority,
    uint32_t label,
    PPProofPreparedActionV1 action,
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalMachineStackEntryV1 entry = {0};
    PPProofGSLTRelationalMachineV1Result result;

    if (action == PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED) {
        result = ppproof_relational_machine_v1_hypothesis(
            store, relational_plan, context, producer,
            authority, label, &entry, error_buf, error_buf_size);
    } else if (action == PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME) {
        result = ppproof_relational_machine_v1_assertion(
            context, producer, actual_workspace, declaration_workspace,
            prepared_workspace, declaration_cache,
            maximum_declaration_cache_len, label,
            *stack, stack_len, &entry, error_buf, error_buf_size);
    } else {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof label has no generated proof role");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        const uint8_t *label_bytes = NULL;
        uint32_t label_len = 0u;

        if (error_buf && error_buf_size != 0u && error_buf[0] != '\0' &&
            store->value_bytes(
                store->context, label, &label_bytes, &label_len) &&
            label_bytes && label_len != 0u) {
            char detail[256];

            (void)snprintf(detail, sizeof(detail), "%s", error_buf);
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "proof label %.*s failed: %s",
                (int)(label_len > 120u ? 120u : label_len),
                (const char *)label_bytes, detail);
        }
        return result;
    }
    if (!ppproof_relational_machine_v1_stack_push(
            stack, stack_len, stack_cap, entry))
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_apply_label(
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    CettaGsltReusableBufferV1 *actual_workspace,
    const PPProofGSLTRelationalDeclarationWorkspaceV1 *declaration_workspace,
    const PPProofGSLTRelationalPreparedWorkspaceV1 *prepared_workspace,
    PPProofRelationalDeclarationCacheV1 *declaration_cache,
    uint32_t maximum_declaration_cache_len,
    const CettaGsltU32IndexV1 *action_index,
    uint32_t authority,
    uint32_t label,
    PPProofRelationalMachineStackEntryV1 **stack,
    uint32_t *stack_len,
    uint32_t *stack_cap,
    char *error_buf,
    size_t error_buf_size) {
    CettaGsltClassifiedValueV1 classified;
    uint32_t kind_row[2];

    if (!store->table_find(
            store->context,
            relational_plan->resolved_table_ids[
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
            &label, 1u, kind_row, 2u)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof references an unknown or future label");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (!ppproof_relational_machine_v1_classify_label(
            &classified, label, kind_row[1], action_index)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof label has no generated proof action");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    return ppproof_relational_machine_v1_apply_prepared_label(
        store, relational_plan, context, producer, actual_workspace,
        declaration_workspace, prepared_workspace, declaration_cache,
        maximum_declaration_cache_len, authority,
        label, (PPProofPreparedActionV1)classified.classification,
        stack, stack_len, stack_cap,
        error_buf, error_buf_size);
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_finish_incomplete(
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTRelationalMachineV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    const PPProofGSLTPatternV1 *premises = NULL;
    uint32_t premise_len = 0u;
    PPProofGSLTArticleV1Result article_result;

    article_result = ppproof_gslt_relational_context_v1_view(
        context, &premises, &premise_len, error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(article_result);
    receipt->context_premise_len = premise_len;
    receipt->article_node_len = producer->node_len;
    receipt->complete = false;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE;
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_finish(
    const PPProofGSLTPlanV1 *proof_plan,
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofRelationalMachineStackEntryV1 *stack,
    uint32_t stack_len,
    PPRelationalValueV1Slice label,
    PPProofGSLTTokenSequenceV1 claim,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    const PPProofGSLTPatternV1 *premises = NULL;
    uint32_t premise_len = 0u;
    PPProofGSLTArticleV1 article;
    PPProofGSLTArticleV1Receipt article_receipt;
    PPProofGSLTArticleV1Result article_result;

    if (stack_len != 1u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof %.*s ends with stack depth %u instead of one",
            (int)(label.len > 200u ? 200u : label.len),
            (const char *)label.bytes, stack_len);
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    if (!stack[0].goal ||
        stack[0].proof.kind != PPPROOF_GSLT_REFERENCE_V1_NODE ||
        stack[0].proof.index >= producer->node_len) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof %.*s has an invalid root reference "
            "(goal %u, kind %u, index %u, nodes %u)",
            (int)(label.len > 120u ? 120u : label.len),
            (const char *)label.bytes,
            stack[0].goal ? 1u : 0u,
            (unsigned)stack[0].proof.kind,
            stack[0].proof.index, producer->node_len);
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
    article_result = ppproof_gslt_relational_context_v1_view(
        context, &premises, &premise_len, error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return ppproof_relational_machine_v1_from_article(article_result);
    if (!ppproof_relational_machine_v1_formula_equal(
            stack[0].formula, claim)) {
        uint32_t mismatch = 0u;
        PPProofGSLTNameV1 actual = {0};
        PPProofGSLTNameV1 expected = {0};

        while (mismatch < stack[0].formula.token_len &&
               mismatch < claim.token_len &&
               stack[0].formula.tokens[mismatch] == claim.tokens[mismatch])
            mismatch++;
        if (mismatch < stack[0].formula.token_len)
            actual = ppproof_relational_machine_v1_token_name(
                stack[0].formula.tokens[mismatch]);
        if (mismatch < claim.token_len)
            expected = ppproof_relational_machine_v1_token_name(
                claim.tokens[mismatch]);
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof %.*s formula mismatch at token %u "
            "(actual %.*s, expected %.*s; lengths %u and %u)",
            (int)(label.len > 120u ? 120u : label.len),
            (const char *)label.bytes, mismatch,
            (int)(actual.len > 80u ? 80u : actual.len),
            actual.bytes ? (const char *)actual.bytes : "",
            (int)(expected.len > 80u ? 80u : expected.len),
            expected.bytes ? (const char *)expected.bytes : "",
            stack[0].formula.token_len, claim.token_len);
        ppproof_relational_machine_v1_append_formula(
            error_buf, error_buf_size, "; actual ", stack[0].formula);
        ppproof_relational_machine_v1_append_formula(
            error_buf, error_buf_size, "; expected ", claim);
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    }
    article = (PPProofGSLTArticleV1){
        .version = 1u,
        .nodes = producer->nodes,
        .node_len = producer->node_len,
        .root_id = stack[0].proof.index,
        .target = stack[0].goal,
    };
    article_result = ppproof_gslt_article_v1_check_open(
        &proof_plan->presentation, premises, premise_len,
        &article, true, limits, &article_receipt,
        error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        if (error_buf && error_buf_size != 0u) {
            char detail[256] = "proof article check failed";
            if (error_buf[0] != '\0')
                (void)snprintf(detail, sizeof(detail), "%s", error_buf);
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "proof %.*s article check failed: %s",
                (int)(label.len > 120u ? 120u : label.len),
                (const char *)label.bytes, detail);
        }
        return ppproof_relational_machine_v1_from_article(article_result);
    }
    receipt->context_premise_len = premise_len;
    receipt->article_node_len = producer->node_len;
    receipt->article = article_receipt;
    receipt->complete = true;
    return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
}

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_normal_with_workspace(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofPreparedActionCaseV1 *action_cases,
    uint32_t action_case_len,
    PPProofGSLTRelationalMachineV1Workspace *workspace,
    const PPProofGSLTRelationalNormalInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPProofGSLTRelationalExecutionDescriptorV1 *execution;
    PPProofRelationalMachineSelectorsV1 selectors;
    PPProofGSLTRelationalContextV1 context;
    PPProofGSLTSequenceEvidenceProducerV1 producer_storage;
    PPProofGSLTSequenceEvidenceProducerV1 *producer = &producer_storage;
    PPProofRelationalMachineStackEntryV1 *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t stack_cap = 0u;
    uint32_t authority = 0u;
    uint32_t claim_value = 0u;
    PPProofGSLTTokenSequenceV1 claim;
    PPProofGSLTRelationalMachineV1Receipt receipt = {0};
    PPProofGSLTRelationalMachineV1Result result =
        PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    PPProofGSLTArticleV1Result article_result;
    PPProofRelationalMachineWorkspaceLeaseV1 workspace_lease = {0};
    CettaGsltU32IndexV1 local_action_index;
    CettaGsltU32IndexV1 *action_index = &local_action_index;
    uint32_t index;
    bool incomplete = false;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (receipt_out)
        memset(receipt_out, 0, sizeof(*receipt_out));
    ppproof_gslt_relational_context_v1_init(&context);
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer_storage);
    cetta_gslt_u32_index_init_v1(&local_action_index);
    receipt.execution =
        PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_LABEL_STREAM;
    if (!input || !receipt_out ||
        !action_cases || action_case_len == 0u ||
        !ppproof_relational_machine_v1_slice_valid(input->label) ||
        !input->claim || input->claim_len == 0u ||
        !input->steps || input->step_len == 0u ||
        !ppproof_relational_machine_v1_composition_valid(
            store, state_plan, proof_plan,
            evidence_abi, relational_plan, &selectors,
            error_buf, error_buf_size))
        goto done;
    execution = &relational_plan->execution;
    result = ppproof_relational_machine_v1_workspace_acquire(
        workspace, false, &workspace_lease);
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof workspace is invalid or unavailable");
        goto done;
    }
    if (workspace_lease.acquired) {
        stack = workspace_lease.impl->stack.items;
        stack_cap = workspace_lease.impl->stack.capacity;
        producer = &workspace_lease.impl->evidence;
        action_index = &workspace_lease.impl->action_index;
    }
    result = ppproof_relational_machine_v1_build_action_index(
        store, execution->machine, action_cases, action_case_len,
        action_index,
        error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
        goto done;
    if (!ppproof_relational_machine_v1_intern(
            store, input->label, &authority) ||
        !ppproof_relational_machine_v1_formula_value(
            store, input->claim, input->claim_len, &claim_value)) {
        result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        goto done;
    }
    article_result = ppproof_gslt_relational_context_v1_begin(
        &context, store, relational_plan, evidence_abi, limits,
        error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_relational_context_v1_formula(
            &context, claim_value, &claim, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            producer, evidence_abi, 0u, limits,
            error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        result = ppproof_relational_machine_v1_from_article(article_result);
        goto done;
    }

    for (index = 0u; index < input->step_len; index++) {
        uint32_t label = 0u;

        if (ppproof_relational_machine_v1_slice_equals_literal(
                input->steps[index], execution->unknown_token)) {
            if (execution->unknown_policy !=
                PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM) {
                ppproof_relational_machine_v1_set_error(
                    error_buf, error_buf_size,
                    "generated proof policy rejects incomplete proofs");
                result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            } else {
                incomplete = true;
                result = ppproof_relational_machine_v1_push_incomplete_claim(
                    &context, producer, claim,
                    &stack, &stack_len, &stack_cap,
                    error_buf, error_buf_size);
            }
            if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
                goto done;
            continue;
        }
        if (!ppproof_relational_machine_v1_intern(
                store, input->steps[index], &label)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        result = ppproof_relational_machine_v1_apply_label(
            store, relational_plan, &context, producer,
            workspace_lease.acquired ? &workspace_lease.impl->actuals : NULL,
            workspace_lease.acquired ? &workspace_lease.declaration : NULL,
            workspace_lease.acquired
                ? &workspace_lease.assertion_preparation : NULL,
            workspace_lease.acquired &&
                    workspace_lease.declaration_cache.admission
                ? &workspace_lease.declaration_cache : NULL,
            limits ? limits->maximum_article_nodes : UINT32_MAX,
            action_index, authority,
            label, &stack, &stack_len,
            &stack_cap, error_buf, error_buf_size);
        if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
            goto done;
    }
    receipt.proof_step_len = input->step_len;
    if (incomplete)
        result = ppproof_relational_machine_v1_finish_incomplete(
            &context, producer, &receipt, error_buf, error_buf_size);
    else
        result = ppproof_relational_machine_v1_finish(
            proof_plan, &context, producer, stack, stack_len, input->label,
            claim, limits, &receipt, error_buf, error_buf_size);

done:
    if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
        error_buf && error_buf_size != 0u && error_buf[0] == '\0') {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational normal-proof request");
    }
    if (workspace_lease.acquired) {
        ppproof_relational_machine_v1_workspace_release(
            &workspace_lease, stack, stack_len, stack_cap,
            NULL, 0u, 0u, NULL, 0u, 0u, &receipt);
        stack = NULL;
    }
    if ((result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK ||
         result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE) &&
        receipt_out)
        *receipt_out = receipt;
    free(stack);
    cetta_gslt_u32_index_free_v1(&local_action_index);
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer_storage);
    ppproof_gslt_relational_context_v1_free(&context);
    return result;
}

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofPreparedActionCaseV1 *action_cases,
    uint32_t action_case_len,
    const PPProofGSLTRelationalNormalInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    return ppproof_gslt_relational_machine_v1_normal_with_workspace(
        store, state_plan, proof_plan, evidence_abi,
        relational_plan, action_cases, action_case_len, NULL,
        input, limits, receipt_out,
        error_buf, error_buf_size);
}

typedef struct {
    const PPRelationalStoreV1 *store;
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan;
    PPProofGSLTRelationalContextV1 *context;
    PPProofGSLTSequenceEvidenceProducerV1 *producer;
    CettaGsltReusableBufferV1 *actual_workspace;
    const PPProofGSLTRelationalDeclarationWorkspaceV1 *declaration_workspace;
    const PPProofGSLTRelationalPreparedWorkspaceV1 *prepared_workspace;
    PPProofRelationalDeclarationCacheV1 *declaration_cache;
    uint32_t authority;
    PPProofGSLTTokenSequenceV1 claim;
    PPProofRelationalMachineStackEntryV1 **stack;
    uint32_t *stack_len;
    uint32_t *stack_cap;
    const CettaGsltClassifiedValueV1 *prepared_labels;
    uint32_t prepared_label_len;
    uint64_t prepared_use_len;
    PPProofRelationalMachineStackEntryV1 **saved_entries;
    uint32_t *saved_entry_len;
    uint32_t *saved_entry_cap;
    uint32_t maximum_value_len;
    bool unknown_uses_value;
    bool *incomplete;
    uint32_t last_label;
    PPProofGSLTRelationalMachineV1Result last_result;
    char *error_buf;
    size_t error_buf_size;
} PPProofRelationalIndexedEffectsV1;

static CettaGsltIndexedEffectResultV1
ppproof_relational_machine_v1_effect_result(
    PPProofGSLTRelationalMachineV1Result result) {
    switch (result) {
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK:
        return CETTA_GSLT_INDEXED_EFFECT_OK_V1;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED:
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE:
        return CETTA_GSLT_INDEXED_EFFECT_REJECTED_V1;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE:
        return CETTA_GSLT_INDEXED_EFFECT_RESOURCE_V1;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED:
        return CETTA_GSLT_INDEXED_EFFECT_UNSUPPORTED_V1;
    case PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID:
    default:
        return CETTA_GSLT_INDEXED_EFFECT_INVALID_V1;
    }
}

static CettaGsltIndexedEffectResultV1
ppproof_relational_machine_v1_effect_table(
    void *raw_context, CettaGsltSplitIndexedTableV1 *table_out) {
    PPProofRelationalIndexedEffectsV1 *effects = raw_context;

    if (!effects || !table_out || !effects->saved_entries ||
        !effects->saved_entry_len)
        return CETTA_GSLT_INDEXED_EFFECT_INVALID_V1;
    *table_out = (CettaGsltSplitIndexedTableV1){
        .prepared = effects->prepared_labels,
        .prepared_len = effects->prepared_label_len,
        .prepared_stride = sizeof(*effects->prepared_labels),
        .saved = *effects->saved_entries,
        .saved_len = *effects->saved_entry_len,
        .saved_stride = sizeof(**effects->saved_entries),
    };
    return CETTA_GSLT_INDEXED_EFFECT_OK_V1;
}

static CettaGsltIndexedEffectResultV1
ppproof_relational_machine_v1_effect_use_prepared(
    void *raw_context, const void *raw_value) {
    PPProofRelationalIndexedEffectsV1 *effects = raw_context;
    const CettaGsltClassifiedValueV1 *prepared = raw_value;

    if (!effects || !prepared ||
        !cetta_gslt_classified_value_validate_v1(
            prepared,
            (uint32_t)PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME))
        return CETTA_GSLT_INDEXED_EFFECT_INVALID_V1;
    if (effects->prepared_use_len == UINT64_MAX) {
        effects->last_result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        return CETTA_GSLT_INDEXED_EFFECT_RESOURCE_V1;
    }
    effects->prepared_use_len++;
    effects->last_label = prepared->value;
    effects->last_result = ppproof_relational_machine_v1_apply_prepared_label(
        effects->store, effects->relational_plan, effects->context,
        effects->producer, effects->actual_workspace,
        effects->declaration_workspace,
        effects->prepared_workspace,
        effects->declaration_cache,
        effects->maximum_value_len,
        effects->authority, prepared->value,
        (PPProofPreparedActionV1)prepared->classification,
        effects->stack, effects->stack_len, effects->stack_cap,
        effects->error_buf, effects->error_buf_size);
    return ppproof_relational_machine_v1_effect_result(
        effects->last_result);
}

static CettaGsltIndexedEffectResultV1
ppproof_relational_machine_v1_effect_use_saved(
    void *raw_context, const void *raw_value) {
    PPProofRelationalIndexedEffectsV1 *effects = raw_context;

    if (!effects || !raw_value)
        return CETTA_GSLT_INDEXED_EFFECT_INVALID_V1;
    effects->last_label = 0u;
    effects->last_result = ppproof_relational_machine_v1_stack_push(
                               effects->stack, effects->stack_len,
                               effects->stack_cap,
                               *(const PPProofRelationalMachineStackEntryV1 *)
                                   raw_value)
                               ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK
                               : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    return ppproof_relational_machine_v1_effect_result(
        effects->last_result);
}

static CettaGsltIndexedEffectResultV1
ppproof_relational_machine_v1_effect_save(void *raw_context) {
    PPProofRelationalIndexedEffectsV1 *effects = raw_context;

    if (!effects || !effects->stack || !effects->stack_len ||
        !effects->saved_entries || !effects->saved_entry_len ||
        !effects->saved_entry_cap)
        return CETTA_GSLT_INDEXED_EFFECT_INVALID_V1;
    if (*effects->stack_len == 0u) {
        ppproof_relational_machine_v1_set_error(
            effects->error_buf, effects->error_buf_size,
            "compressed proof cannot save an empty stack");
        effects->last_result =
            PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    } else {
        effects->last_result =
            ppproof_relational_machine_v1_saved_push(
                effects->saved_entries, effects->saved_entry_len,
                effects->saved_entry_cap,
                (*effects->stack)[*effects->stack_len - 1u],
                effects->prepared_label_len, effects->maximum_value_len)
                ? PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK
                : PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
    }
    return ppproof_relational_machine_v1_effect_result(
        effects->last_result);
}

static CettaGsltIndexedEffectResultV1
ppproof_relational_machine_v1_effect_unknown(void *raw_context) {
    PPProofRelationalIndexedEffectsV1 *effects = raw_context;

    if (!effects || !effects->incomplete)
        return CETTA_GSLT_INDEXED_EFFECT_INVALID_V1;
    if (!effects->unknown_uses_value) {
        ppproof_relational_machine_v1_set_error(
            effects->error_buf, effects->error_buf_size,
            "generated proof policy rejects incomplete proofs");
        effects->last_result =
            PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    } else {
        effects->last_result =
            ppproof_relational_machine_v1_push_incomplete_claim(
                effects->context, effects->producer, effects->claim,
                effects->stack, effects->stack_len, effects->stack_cap,
                effects->error_buf, effects->error_buf_size);
        if (effects->last_result ==
            PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
            *effects->incomplete = true;
    }
    return ppproof_relational_machine_v1_effect_result(
        effects->last_result);
}

static PPProofGSLTRelationalMachineV1Result
ppproof_relational_machine_v1_from_indexed_effect_machine(
    CettaGsltIndexedEffectMachineV1 *machine,
    PPProofRelationalIndexedEffectsV1 *effects,
    CettaGsltIndexedEffectMachineResultV1 machine_result,
    char *error_buf,
    size_t error_buf_size) {
    if (!machine || !effects)
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    switch (machine_result) {
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK;
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_REJECTED_V1:
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_RESOURCE_V1:
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1:
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_UNSUPPORTED_V1:
        if (effects->last_result !=
                PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
            effects->last_result !=
                PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
            return effects->last_result;
        if (machine_result ==
            CETTA_GSLT_INDEXED_EFFECT_MACHINE_REJECTED_V1)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        if (machine_result ==
            CETTA_GSLT_INDEXED_EFFECT_MACHINE_RESOURCE_V1)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        if (machine_result ==
            CETTA_GSLT_INDEXED_EFFECT_MACHINE_UNSUPPORTED_V1)
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED;
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_INDEX_V1:
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof index is out of range");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
    case CETTA_GSLT_INDEXED_EFFECT_MACHINE_DECODE_V1:
        if (machine->decode_failure ==
            CETTA_GSLT_INDEXED_DECODE_OVERFLOW_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof index overflows");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        }
        if (machine->decode_failure ==
            CETTA_GSLT_INDEXED_DECODE_SAVE_INSIDE_INDEX_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof save interrupts an open index");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        }
        if (machine->decode_failure ==
            CETTA_GSLT_INDEXED_DECODE_SAVE_WITHOUT_USE_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof save does not follow a completed index");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        }
        if (machine->decode_failure ==
            CETTA_GSLT_INDEXED_DECODE_UNKNOWN_INSIDE_INDEX_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof unknown interrupts an open index");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        }
        if (machine->decode_failure ==
            CETTA_GSLT_INDEXED_DECODE_INVALID_BYTE_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                machine->decoder.phase ==
                    CETTA_GSLT_INDEXED_PHASE_OPEN_INDEX_V1
                    ? "compressed proof byte interrupts an open index"
                    : "compressed proof byte is outside the generated decoder");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        }
        if (machine->decode_failure ==
            CETTA_GSLT_INDEXED_DECODE_OPEN_INDEX_AT_END_V1) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof ends inside an index");
            return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
        }
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "invalid generated compressed-proof decoder state");
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    default:
        return PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    }
}

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_compressed_with_workspace(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofIndexedProgramPlanV1 *indexed_program_plan,
    const PPProofFrameIndexPlanV1 *frame_index_plan,
    PPProofGSLTRelationalMachineV1Workspace *workspace,
    const PPProofGSLTRelationalCompressedInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTArticleV1Limits default_limits;
    const PPProofGSLTArticleV1Limits *effective_limits = limits;
    const PPProofGSLTRelationalExecutionDescriptorV1 *execution;
    PPProofRelationalMachineSelectorsV1 selectors;
    PPProofGSLTRelationalContextV1 context;
    PPProofGSLTSequenceEvidenceProducerV1 producer_storage;
    PPProofGSLTSequenceEvidenceProducerV1 *producer = &producer_storage;
    PPProofRelationalMachineStackEntryV1 *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t stack_cap = 0u;
    CettaGsltClassifiedValueV1 *prepared_labels = NULL;
    uint32_t prepared_label_len = 0u;
    uint32_t prepared_label_cap = 0u;
    uint32_t implicit_header_len = 0u;
    PPProofRelationalMachineStackEntryV1 *saved_entries = NULL;
    uint32_t saved_entry_len = 0u;
    uint32_t saved_entry_cap = 0u;
    uint32_t authority = 0u;
    uint32_t claim_value = 0u;
    PPProofGSLTTokenSequenceV1 claim;
    PPProofGSLTRelationalMachineV1Receipt receipt = {0};
    PPProofGSLTRelationalMachineV1Result result =
        PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
    PPProofGSLTArticleV1Result article_result;
    PPProofRelationalMachineWorkspaceLeaseV1 workspace_lease = {0};
    CettaGsltIndexedEffectMachineV1 effect_machine = {0};
    PPProofRelationalIndexedEffectsV1 effects = {0};
    CettaGsltIndexedInstructionPlanV1 indexed_plan;
    PPProofIndexedEffectUnknownV1 indexed_unknown;
    CettaGsltU32IndexV1 local_action_index;
    CettaGsltU32IndexV1 *action_index = &local_action_index;
    uint32_t index;
    bool incomplete = false;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (receipt_out)
        memset(receipt_out, 0, sizeof(*receipt_out));
    ppproof_gslt_relational_context_v1_init(&context);
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer_storage);
    cetta_gslt_u32_index_init_v1(&local_action_index);
    receipt.execution =
        PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_INDEXED_INSTRUCTION_STREAM;
    if (!effective_limits) {
        default_limits = ppproof_gslt_article_v1_default_limits();
        effective_limits = &default_limits;
    }
    if (!input) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof input is absent");
        goto done;
    }
    if (!receipt_out) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof receipt is absent");
        goto done;
    }
    if (!ppproof_relational_machine_v1_slice_valid(input->label)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof label is empty");
        goto done;
    }
    if (!input->claim || input->claim_len == 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof claim is empty");
        goto done;
    }
    if (input->header_len != 0u && !input->header) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof header storage is absent");
        goto done;
    }
    if (!input->code || input->code_len == 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof code is empty");
        goto done;
    }
    if (effective_limits->maximum_article_nodes == 0u) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof article-node limit is zero");
        goto done;
    }
    if (!ppproof_relational_machine_v1_composition_valid(
            store, state_plan, proof_plan,
            evidence_abi, relational_plan, &selectors,
            error_buf, error_buf_size))
        goto done;
    execution = &relational_plan->execution;
    if (!indexed_program_plan || !indexed_program_plan->operation ||
        !indexed_program_plan->machine ||
        !indexed_program_plan->header_role ||
        !indexed_program_plan->code_role ||
        !indexed_program_plan->indexed_carrier ||
        !indexed_program_plan->effect_carrier ||
        !indexed_program_plan->region || !execution->machine) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated indexed-program plan does not admit the compressed backend");
        goto done;
    }
    if (!frame_index_plan || !frame_index_plan->machine ||
        !frame_index_plan->carrier || !frame_index_plan->validation ||
        !frame_index_plan->region || !execution->machine ||
        strcmp(frame_index_plan->machine, execution->machine) != 0 ||
        strcmp(frame_index_plan->carrier,
               "u32-open-addressed-index-v1") != 0 ||
        strcmp(frame_index_plan->validation, "duplicate-reject-v1") != 0) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated frame-index plan does not admit the compressed backend");
        goto done;
    }
    if (execution->unknown_policy >
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM ||
        execution->header_hypothesis_policy ==
            CETTA_GSLT_HEADER_HYPOTHESIS_INVALID_V1) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated compressed-proof decoder is malformed");
        goto done;
    }
    indexed_unknown = execution->unknown_policy ==
                              PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM
                          ? PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE
                          : PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT;
    if (!ppproof_indexed_program_plan_v1_admits(
            indexed_program_plan, indexed_program_plan->operation,
            indexed_program_plan->action_index, execution->machine,
            indexed_program_plan->header_role,
            indexed_program_plan->code_role,
            indexed_program_plan->region, indexed_unknown,
            execution->save_placement,
            execution->header_hypothesis_policy) ||
        !ppproof_relational_machine_v1_indexed_program_matches(
            indexed_program_plan, execution)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated indexed-program plan does not admit the compressed backend or disagrees with the proof machine");
        goto done;
    }
    if (!ppproof_frame_index_plan_v1_admits(
            frame_index_plan, indexed_program_plan->operation,
            indexed_program_plan->action_index, execution->machine,
            indexed_program_plan->region)) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "generated frame-index plan does not match the compressed call site");
        goto done;
    }
    result = ppproof_relational_machine_v1_workspace_acquire(
        workspace, true, &workspace_lease);
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "proof workspace is invalid or unavailable");
        goto done;
    }
    if (workspace_lease.acquired) {
        stack = workspace_lease.impl->stack.items;
        stack_cap = workspace_lease.impl->stack.capacity;
        prepared_labels = workspace_lease.impl->prepared.items;
        prepared_label_cap = workspace_lease.impl->prepared.capacity;
        saved_entries = workspace_lease.impl->saved.items;
        saved_entry_cap = workspace_lease.impl->saved.capacity;
        producer = &workspace_lease.impl->evidence;
        action_index = &workspace_lease.impl->action_index;
    }
    result = ppproof_relational_machine_v1_build_action_index(
        store, execution->machine, indexed_program_plan->action_cases,
        indexed_program_plan->action_case_len, action_index,
        error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK)
        goto done;
    indexed_plan = ppproof_relational_machine_v1_indexed_plan(
        indexed_program_plan);
    if (!ppproof_relational_machine_v1_intern(
            store, input->label, &authority) ||
        !ppproof_relational_machine_v1_formula_value(
            store, input->claim, input->claim_len, &claim_value)) {
        result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        goto done;
    }
    article_result = ppproof_gslt_relational_context_v1_begin(
        &context, store, relational_plan, evidence_abi,
        effective_limits, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_relational_context_v1_formula(
            &context, claim_value, &claim, error_buf, error_buf_size);
    if (article_result == PPPROOF_GSLT_ARTICLE_V1_OK)
        article_result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            producer, evidence_abi, 0u, effective_limits,
            error_buf, error_buf_size);
    if (article_result != PPPROOF_GSLT_ARTICLE_V1_OK) {
        result = ppproof_relational_machine_v1_from_article(article_result);
        goto done;
    }
    result = ppproof_relational_machine_v1_preload_frame(
        store, relational_plan, &selectors,
        action_index, authority,
        &prepared_labels, &prepared_label_len, &prepared_label_cap,
        workspace_lease.acquired ? &workspace_lease.impl->binder_counts : NULL,
        workspace_lease.acquired ? &workspace_lease.impl->ordered_rows : NULL,
        workspace_lease.acquired
            ? &workspace_lease.impl->required_binder_index : NULL,
        workspace_lease.acquired
            ? &workspace_lease.impl->premise_label_index : NULL,
        effective_limits->maximum_article_nodes,
        error_buf, error_buf_size);
    if (result != PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK) {
        if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
            error_buf && error_buf_size != 0u && error_buf[0] == '\0')
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof frame preload failed");
        goto done;
    }
    implicit_header_len = prepared_label_len;
    for (index = 0u; index < input->header_len; index++) {
        uint32_t label = 0u;
        uint32_t kind_row[2];
        CettaGsltClassifiedValueV1 classified;
        if (!ppproof_relational_machine_v1_intern(
                store, input->header[index], &label)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
        if (!store->table_find(
                store->context,
                relational_plan->resolved_table_ids[
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND],
                &label, 1u, kind_row, 2u)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header references an unknown or future label");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if ((kind_row[1] == selectors.floating_kind ||
             kind_row[1] == selectors.essential_kind) &&
            !ppproof_relational_machine_v1_active_hypothesis(
                store, relational_plan, authority, label)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header references an inactive hypothesis");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if (execution->header_hypothesis_policy ==
                CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1 &&
            ppproof_relational_machine_v1_prepared_prefix_contains(
                prepared_labels, implicit_header_len, label)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header repeats an implicitly prepared hypothesis");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if (!ppproof_relational_machine_v1_classify_label(
                &classified, label, kind_row[1], action_index)) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed header label has no generated proof action");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED;
            goto done;
        }
        if (!ppproof_relational_machine_v1_label_push(
                &prepared_labels, &prepared_label_len,
                &prepared_label_cap, classified, saved_entry_len,
                effective_limits->maximum_article_nodes)) {
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
            goto done;
        }
    }

    effects = (PPProofRelationalIndexedEffectsV1){
        .store = store,
        .relational_plan = relational_plan,
        .context = &context,
        .producer = producer,
        .actual_workspace = workspace_lease.acquired
                                ? &workspace_lease.impl->actuals
                                : NULL,
        .declaration_workspace = workspace_lease.acquired
                                     ? &workspace_lease.declaration
                                     : NULL,
        .prepared_workspace = workspace_lease.acquired
                                  ? &workspace_lease.assertion_preparation
                                  : NULL,
        .declaration_cache = workspace_lease.acquired &&
                                     workspace_lease.declaration_cache.admission
                                 ? &workspace_lease.declaration_cache
                                 : NULL,
        .authority = authority,
        .claim = claim,
        .stack = &stack,
        .stack_len = &stack_len,
        .stack_cap = &stack_cap,
        .prepared_labels = prepared_labels,
        .prepared_label_len = prepared_label_len,
        .saved_entries = &saved_entries,
        .saved_entry_len = &saved_entry_len,
        .saved_entry_cap = &saved_entry_cap,
        .maximum_value_len = effective_limits->maximum_article_nodes,
        .unknown_uses_value =
            indexed_unknown == PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
        .incomplete = &incomplete,
        .last_result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    {
        CettaGsltIndexedEffectAlgebraV1 algebra = {
            .context = &effects,
            .table_view = ppproof_relational_machine_v1_effect_table,
            .use_prepared =
                ppproof_relational_machine_v1_effect_use_prepared,
            .use_saved = ppproof_relational_machine_v1_effect_use_saved,
            .save_top = ppproof_relational_machine_v1_effect_save,
            .use_unknown = ppproof_relational_machine_v1_effect_unknown,
        };
        CettaGsltIndexedEffectMachineResultV1 machine_result =
            cetta_gslt_indexed_effect_machine_init_v1(
                &effect_machine, &indexed_plan, &algebra);

        if (machine_result != CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1) {
            result = ppproof_relational_machine_v1_from_indexed_effect_machine(
                &effect_machine, &effects, machine_result,
                error_buf, error_buf_size);
            goto done;
        }
    }
    for (index = 0u; index < input->code_len; index++) {
        CettaGsltIndexedEffectMachineResultV1 machine_result;

        if (!ppproof_relational_machine_v1_slice_valid(
                input->code[index])) {
            ppproof_relational_machine_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof contains an empty code word");
            result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID;
            goto done;
        }
        machine_result = cetta_gslt_indexed_effect_machine_execute_v1(
            &effect_machine, input->code[index].bytes,
            input->code[index].len);
        if (machine_result != CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1) {
            result = ppproof_relational_machine_v1_from_indexed_effect_machine(
                &effect_machine, &effects, machine_result,
                error_buf, error_buf_size);
            if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
                error_buf && error_buf_size != 0u &&
                error_buf[0] == '\0')
                ppproof_relational_machine_v1_set_error(
                    error_buf, error_buf_size,
                    "compressed proof indexed effect failed "
                    "at decoded value %llu (indexed value %llu, "
                    "label id %u, stack %u)",
                    (unsigned long long)effect_machine.value_instruction_len,
                    (unsigned long long)effect_machine.effect_failure_index,
                    effects.last_label, stack_len);
            goto done;
        }
    }
    {
        CettaGsltIndexedEffectMachineResultV1 machine_result =
            cetta_gslt_indexed_effect_machine_finish_v1(&effect_machine);
        if (machine_result != CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1) {
            result = ppproof_relational_machine_v1_from_indexed_effect_machine(
                &effect_machine, &effects, machine_result,
                error_buf, error_buf_size);
            goto done;
        }
    }
    if (effect_machine.value_instruction_len > UINT32_MAX) {
        result = PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE;
        goto done;
    }
    receipt.proof_step_len =
        (uint32_t)effect_machine.value_instruction_len;
    receipt.decoded_byte_len = effect_machine.decoder.consumed_byte_len;
    receipt.decoded_instruction_len =
        effect_machine.decoder.emitted_instruction_len;
    receipt.prepared_value_len = prepared_label_len;
    receipt.prepared_classification_len = prepared_label_len;
    receipt.prepared_classification_use_len = effects.prepared_use_len;
    receipt.saved_value_len = saved_entry_len;
    if (incomplete)
        result = ppproof_relational_machine_v1_finish_incomplete(
            &context, producer, &receipt, error_buf, error_buf_size);
    else
        result = ppproof_relational_machine_v1_finish(
            proof_plan, &context, producer, stack, stack_len, input->label,
            claim, effective_limits, &receipt, error_buf, error_buf_size);

done:
    if (result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID &&
        error_buf && error_buf_size != 0u && error_buf[0] == '\0') {
        ppproof_relational_machine_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational compressed proof %.*s "
            "(claim %u, header %u, code %u)",
            input && input->label.bytes
                ? (int)(input->label.len > 120u ? 120u : input->label.len)
                : 0,
            input && input->label.bytes
                ? (const char *)input->label.bytes : "",
            input ? input->claim_len : 0u,
            input ? input->header_len : 0u,
            input ? input->code_len : 0u);
    }
    if (workspace_lease.acquired) {
        ppproof_relational_machine_v1_workspace_release(
            &workspace_lease, stack, stack_len, stack_cap,
            prepared_labels, prepared_label_len, prepared_label_cap,
            saved_entries, saved_entry_len, saved_entry_cap, &receipt);
        saved_entries = NULL;
        prepared_labels = NULL;
        stack = NULL;
    }
    if ((result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK ||
         result == PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE) &&
        receipt_out)
        *receipt_out = receipt;
    free(saved_entries);
    free(prepared_labels);
    free(stack);
    cetta_gslt_u32_index_free_v1(&local_action_index);
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer_storage);
    ppproof_gslt_relational_context_v1_free(&context);
    return result;
}

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofIndexedProgramPlanV1 *indexed_program_plan,
    const PPProofFrameIndexPlanV1 *frame_index_plan,
    const PPProofGSLTRelationalCompressedInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    return ppproof_gslt_relational_machine_v1_compressed_with_workspace(
        store, state_plan, proof_plan, evidence_abi,
        relational_plan, indexed_program_plan, frame_index_plan, NULL,
        input, limits, receipt_out, error_buf, error_buf_size);
}
