#include "proof_storage_plan_v1.h"

#include "finite_horn_answer_stream_v1.h"
#include "gslt_indexed_instruction_decoder_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
} PPProofStoragePlanStorageV1;

typedef enum {
    PPPROOF_STORAGE_RECORD_V1_UNKNOWN = 0,
    PPPROOF_STORAGE_RECORD_V1_TABLE,
    PPPROOF_STORAGE_RECORD_V1_MACHINE,
    PPPROOF_STORAGE_RECORD_V1_READ,
    PPPROOF_STORAGE_RECORD_V1_SEQUENCE,
    PPPROOF_STORAGE_RECORD_V1_CALL,
    PPPROOF_STORAGE_RECORD_V1_WORKSPACE,
    PPPROOF_STORAGE_RECORD_V1_REPETITION_CACHE,
    PPPROOF_STORAGE_RECORD_V1_FINITE_SUPPORT,
    PPPROOF_STORAGE_RECORD_V1_INDEXED_VALUE,
    PPPROOF_STORAGE_RECORD_V1_INDEXED_EFFECT_MACHINE,
    PPPROOF_STORAGE_RECORD_V1_PREPARED_ACTION_CASE,
    PPPROOF_STORAGE_RECORD_V1_INDEXED_PROGRAM,
    PPPROOF_STORAGE_RECORD_V1_FRAME_INDEX,
    PPPROOF_STORAGE_RECORD_V1_LITERAL_HOLE,
    PPPROOF_STORAGE_RECORD_V1_TWO_PHASE_FRAME
} PPProofStorageRecordV1;

typedef struct {
    uint32_t tables;
    uint32_t machines;
    uint32_t reads;
    uint32_t sequences;
    uint32_t calls;
    uint32_t workspaces;
    uint32_t repetition_caches;
    uint32_t finite_supports;
    uint32_t indexed_values;
    uint32_t indexed_effect_machines;
    uint32_t prepared_action_cases;
    uint32_t indexed_programs;
    uint32_t frame_indexes;
    uint32_t literal_holes;
    uint32_t two_phase_frames;
} PPProofStorageCountsV1;

static bool ppproof_storage_plan_v1_fail(
    char *buffer, size_t size, const char *format, ...) {
    va_list arguments;

    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool ppproof_storage_plan_v1_expr_head(
    const Atom *atom, const char *head, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static const char *ppproof_storage_plan_v1_symbol(const Atom *atom) {
    const char *name;

    if (!atom || atom->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr((Atom *)atom);
    return name && name[0] != '\0' ? name : NULL;
}

static bool ppproof_storage_plan_v1_u32(
    const Atom *atom, uint32_t *value_out) {
    int64_t value;

    if (!atom || !value_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT)
        return false;
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return false;
    *value_out = (uint32_t)value;
    return true;
}

static bool ppproof_storage_plan_v1_u8(
    const Atom *atom, uint8_t *value_out) {
    uint32_t value;

    if (!value_out || !ppproof_storage_plan_v1_u32(atom, &value) ||
        value > UINT8_MAX)
        return false;
    *value_out = (uint8_t)value;
    return true;
}

static PPProofStorageLifetimeV1 ppproof_storage_plan_v1_lifetime(
    const Atom *atom) {
    if (atom_is_symbol((Atom *)atom, "state-persistent-v1"))
        return PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT;
    if (atom_is_symbol((Atom *)atom, "state-scoped-v1"))
        return PPPROOF_STORAGE_LIFETIME_V1_SCOPED;
    if (atom_is_symbol((Atom *)atom, "state-transactional-v1"))
        return PPPROOF_STORAGE_LIFETIME_V1_TRANSACTIONAL;
    return PPPROOF_STORAGE_LIFETIME_V1_INVALID;
}

static PPProofPreparedActionV1 ppproof_storage_plan_v1_prepared_action(
    const Atom *atom) {
    if (atom_is_symbol((Atom *)atom, "stack-push-declared-v1"))
        return PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED;
    if (atom_is_symbol((Atom *)atom, "stack-apply-frame-v1"))
        return PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME;
    return PPPROOF_PREPARED_ACTION_V1_INVALID;
}

static CettaGsltIndexedSavePlacementV1
ppproof_storage_plan_v1_save_placement(const Atom *atom) {
    if (atom_is_symbol(
            (Atom *)atom,
            "state-proof-save-immediately-after-use-v1"))
        return CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1;
    if (atom_is_symbol((Atom *)atom, "state-proof-save-repeatable-v1"))
        return CETTA_GSLT_INDEXED_SAVE_REPEATABLE_AFTER_USE_V1;
    return CETTA_GSLT_INDEXED_SAVE_INVALID_V1;
}

static CettaGsltHeaderHypothesisPolicyV1
ppproof_storage_plan_v1_header_hypothesis_policy(const Atom *atom) {
    if (atom_is_symbol(
            (Atom *)atom,
            "state-proof-header-nonmandatory-only-v1"))
        return CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1;
    if (atom_is_symbol(
            (Atom *)atom, "state-proof-header-any-active-v1"))
        return CETTA_GSLT_HEADER_HYPOTHESIS_ANY_ACTIVE_V1;
    return CETTA_GSLT_HEADER_HYPOTHESIS_INVALID_V1;
}

static bool ppproof_storage_plan_v1_region_matches(
    PPProofStorageLifetimeV1 lifetime, const char *region) {
    if (!region)
        return false;
    if (lifetime == PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT)
        return strcmp(region, "state-run-region-v1") == 0;
    if (lifetime == PPPROOF_STORAGE_LIFETIME_V1_SCOPED)
        return strcmp(region, "state-scope-region-v1") == 0;
    if (lifetime == PPPROOF_STORAGE_LIFETIME_V1_TRANSACTIONAL)
        return strcmp(region, "state-transaction-region-v1") == 0;
    return false;
}

static bool ppproof_storage_plan_v1_indexed_decoder_valid(
    const PPProofIndexedProgramPlanV1 *program) {
    CettaGsltIndexedInstructionPlanV1 plan;

    if (!program)
        return false;
    plan = (CettaGsltIndexedInstructionPlanV1){
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
    return cetta_gslt_indexed_instruction_plan_validate_v1(&plan);
}

static PPProofStorageRecordV1 ppproof_storage_plan_v1_record(
    const Atom *answer, const Atom **record_out) {
    const Atom *record;

    if (!ppproof_storage_plan_v1_expr_head(
            answer, "compile-proof-storage-plan-v1", 1u))
        return PPPROOF_STORAGE_RECORD_V1_UNKNOWN;
    record = answer->expr.elems[1];
    *record_out = record;
    if (ppproof_storage_plan_v1_expr_head(
            record, "state-table-storage-v1", 5u))
        return PPPROOF_STORAGE_RECORD_V1_TABLE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-machine-binding-v1", 4u))
        return PPPROOF_STORAGE_RECORD_V1_MACHINE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-machine-table-read-v1", 4u))
        return PPPROOF_STORAGE_RECORD_V1_READ;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-sequence-layout-v1", 7u))
        return PPPROOF_STORAGE_RECORD_V1_SEQUENCE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-call-region-plan-v1", 8u))
        return PPPROOF_STORAGE_RECORD_V1_CALL;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-workspace-plan-v1", 6u))
        return PPPROOF_STORAGE_RECORD_V1_WORKSPACE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-repetition-cache-plan-v1", 8u))
        return PPPROOF_STORAGE_RECORD_V1_REPETITION_CACHE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-finite-support-plan-v1", 11u))
        return PPPROOF_STORAGE_RECORD_V1_FINITE_SUPPORT;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-indexed-value-plan-v1", 7u))
        return PPPROOF_STORAGE_RECORD_V1_INDEXED_VALUE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-indexed-effect-machine-plan-v1", 9u))
        return PPPROOF_STORAGE_RECORD_V1_INDEXED_EFFECT_MACHINE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-prepared-action-case-v1", 3u))
        return PPPROOF_STORAGE_RECORD_V1_PREPARED_ACTION_CASE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-indexed-program-plan-v1", 24u))
        return PPPROOF_STORAGE_RECORD_V1_INDEXED_PROGRAM;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-frame-index-plan-v1", 6u))
        return PPPROOF_STORAGE_RECORD_V1_FRAME_INDEX;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-literal-hole-plan-v1", 7u))
        return PPPROOF_STORAGE_RECORD_V1_LITERAL_HOLE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-two-phase-frame-plan-v1", 10u))
        return PPPROOF_STORAGE_RECORD_V1_TWO_PHASE_FRAME;
    return PPPROOF_STORAGE_RECORD_V1_UNKNOWN;
}

static bool ppproof_storage_plan_v1_count(
    uint32_t *value, char *error, size_t error_size) {
    if (*value == UINT32_MAX)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage plan has too many records");
    (*value)++;
    return true;
}

static bool ppproof_storage_plan_v1_alloc(
    void **output, uint32_t count, size_t item_size,
    char *error, size_t error_size) {
    void *items;

    if (count > 0u && item_size > SIZE_MAX / count)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage plan allocation overflows");
    items = calloc(count ? count : 1u, item_size);
    if (!items)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "cannot allocate proof storage plan");
    *output = items;
    return true;
}

static int ppproof_storage_plan_v1_table_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageTableV1 *left = left_raw;
    const PPProofStorageTableV1 *right = right_raw;
    return strcmp(left->table, right->table);
}

static int ppproof_storage_plan_v1_machine_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageMachineV1 *left = left_raw;
    const PPProofStorageMachineV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_read_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageReadV1 *left = left_raw;
    const PPProofStorageReadV1 *right = right_raw;
    int compared = strcmp(left->machine, right->machine);
    return compared != 0 ? compared : strcmp(left->role, right->role);
}

static int ppproof_storage_plan_v1_sequence_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageSequenceV1 *left = left_raw;
    const PPProofStorageSequenceV1 *right = right_raw;
    int compared = strcmp(left->owner, right->owner);
    return compared != 0 ? compared : strcmp(left->provable, right->provable);
}

static int ppproof_storage_plan_v1_call_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageCallV1 *left = left_raw;
    const PPProofStorageCallV1 *right = right_raw;
    int compared = strcmp(left->operation, right->operation);
    if (compared != 0)
        return compared;
    if (left->action_index < right->action_index)
        return -1;
    if (left->action_index > right->action_index)
        return 1;
    return 0;
}

static int ppproof_storage_plan_v1_workspace_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofWorkspacePlanV1 *left = left_raw;
    const PPProofWorkspacePlanV1 *right = right_raw;
    int compared = strcmp(left->operation, right->operation);
    if (compared != 0)
        return compared;
    if (left->action_index < right->action_index)
        return -1;
    if (left->action_index > right->action_index)
        return 1;
    return 0;
}

static int ppproof_storage_plan_v1_repetition_cache_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofRepetitionCachePlanV1 *left = left_raw;
    const PPProofRepetitionCachePlanV1 *right = right_raw;
    int compared = strcmp(left->operation, right->operation);
    if (compared != 0)
        return compared;
    if (left->action_index < right->action_index)
        return -1;
    if (left->action_index > right->action_index)
        return 1;
    return 0;
}

static int ppproof_storage_plan_v1_finite_support_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofFiniteSupportPlanV1 *left = left_raw;
    const PPProofFiniteSupportPlanV1 *right = right_raw;
    return strcmp(left->owner, right->owner);
}

static int ppproof_storage_plan_v1_indexed_value_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofIndexedValuePlanV1 *left = left_raw;
    const PPProofIndexedValuePlanV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_indexed_effect_machine_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofIndexedEffectMachinePlanV1 *left = left_raw;
    const PPProofIndexedEffectMachinePlanV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_prepared_action_case_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofPreparedActionCaseV1 *left = left_raw;
    const PPProofPreparedActionCaseV1 *right = right_raw;
    int compared = strcmp(left->machine, right->machine);
    if (compared != 0)
        return compared;
    return strcmp(left->source_kind, right->source_kind);
}

static int ppproof_storage_plan_v1_indexed_program_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofIndexedProgramPlanV1 *left = left_raw;
    const PPProofIndexedProgramPlanV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_frame_index_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofFrameIndexPlanV1 *left = left_raw;
    const PPProofFrameIndexPlanV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_literal_hole_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofLiteralHolePlanV1 *left = left_raw;
    const PPProofLiteralHolePlanV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_two_phase_frame_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofTwoPhaseFramePlanV1 *left = left_raw;
    const PPProofTwoPhaseFramePlanV1 *right = right_raw;
    int compared = strcmp(left->operation, right->operation);
    if (compared != 0)
        return compared;
    if (left->action_index < right->action_index)
        return -1;
    if (left->action_index > right->action_index)
        return 1;
    return 0;
}

void ppproof_storage_plan_v1_init(PPProofStoragePlanV1 *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void ppproof_storage_plan_v1_free(PPProofStoragePlanV1 *plan) {
    PPProofStoragePlanStorageV1 *storage;

    if (!plan)
        return;
    storage = plan->storage;
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    free(plan->tables);
    free(plan->machines);
    free(plan->reads);
    free(plan->sequences);
    free(plan->calls);
    free(plan->workspaces);
    free(plan->repetition_caches);
    free(plan->finite_supports);
    free(plan->indexed_values);
    free(plan->indexed_effect_machines);
    free(plan->prepared_action_cases);
    free(plan->indexed_programs);
    free(plan->frame_indexes);
    free(plan->literal_holes);
    free(plan->two_phase_frames);
    memset(plan, 0, sizeof(*plan));
}

const PPProofStorageTableV1 *ppproof_storage_plan_v1_table(
    const PPProofStoragePlanV1 *plan, const char *table) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !table)
        return NULL;
    high = plan->table_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->tables[middle].table, table);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->table_len &&
                   strcmp(plan->tables[low].table, table) == 0
               ? &plan->tables[low]
               : NULL;
}

const PPProofStorageMachineV1 *ppproof_storage_plan_v1_machine(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->machine_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->machines[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->machine_len &&
                   strcmp(plan->machines[low].machine, machine) == 0
               ? &plan->machines[low]
               : NULL;
}

const PPProofStorageCallV1 *ppproof_storage_plan_v1_call(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !operation)
        return NULL;
    high = plan->call_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofStorageCallV1 *call = &plan->calls[middle];
        int compared = strcmp(call->operation, operation);
        if (compared == 0) {
            if (call->action_index < action_index)
                compared = -1;
            else if (call->action_index > action_index)
                compared = 1;
        }
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->call_len &&
                   strcmp(plan->calls[low].operation, operation) == 0 &&
                   plan->calls[low].action_index == action_index
               ? &plan->calls[low]
               : NULL;
}

bool ppproof_storage_call_v1_admits_reusable_workspace(
    const PPProofStorageCallV1 *call,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region) {
    return call && operation && machine && region && call->operation &&
           call->machine && call->region && call->layout &&
           call->observation &&
           strcmp(call->operation, operation) == 0 &&
           call->action_index == action_index &&
           strcmp(call->machine, machine) == 0 &&
           strcmp(call->region, region) == 0 &&
           strcmp(call->layout, "flat-symbol-id-vector-v1") == 0 &&
           strcmp(call->observation, "proof-verdict-only-v1") == 0;
}

const PPProofWorkspacePlanV1 *ppproof_storage_plan_v1_workspace(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !operation)
        return NULL;
    high = plan->workspace_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofWorkspacePlanV1 *workspace =
            &plan->workspaces[middle];
        int compared = strcmp(workspace->operation, operation);
        if (compared == 0) {
            if (workspace->action_index < action_index)
                compared = -1;
            else if (workspace->action_index > action_index)
                compared = 1;
        }
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->workspace_len &&
                   strcmp(plan->workspaces[low].operation, operation) == 0 &&
                   plan->workspaces[low].action_index == action_index
               ? &plan->workspaces[low]
               : NULL;
}

const PPProofRepetitionCachePlanV1 *
ppproof_storage_plan_v1_repetition_cache(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !operation)
        return NULL;
    high = plan->repetition_cache_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofRepetitionCachePlanV1 *cache =
            &plan->repetition_caches[middle];
        int compared = strcmp(cache->operation, operation);
        if (compared == 0) {
            if (cache->action_index < action_index)
                compared = -1;
            else if (cache->action_index > action_index)
                compared = 1;
        }
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->repetition_cache_len &&
                   strcmp(plan->repetition_caches[low].operation,
                          operation) == 0 &&
                   plan->repetition_caches[low].action_index == action_index
               ? &plan->repetition_caches[low]
               : NULL;
}

bool ppproof_repetition_cache_plan_v1_admits(
    const PPProofRepetitionCachePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region) {
    return plan && operation && machine && region && plan->operation &&
           plan->machine && plan->key_carrier && plan->value_carrier &&
           plan->admission_policy && plan->snapshot_policy && plan->region &&
           strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->key_carrier, "u32-identity-key-v1") == 0 &&
           strcmp(plan->value_carrier, "owned-plan-value-v1") == 0 &&
           strcmp(plan->admission_policy,
                  "second-occurrence-admission-v1") == 0 &&
           strcmp(plan->snapshot_policy,
                  "immutable-prefix-snapshot-v1") == 0 &&
           strcmp(plan->region, region) == 0 &&
           strcmp(region, "proof-call-region-v1") == 0;
}

bool ppproof_workspace_plan_v1_admits(
    const PPProofWorkspacePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *carrier,
    const char *region) {
    bool supported = carrier &&
        (strcmp(carrier, "stack-proof-call-workspace-v1") == 0 ||
         strcmp(carrier, "indexed-stack-proof-call-workspace-v1") == 0);

    return supported && plan && operation && machine && region &&
           plan->operation && plan->machine && plan->carrier &&
           plan->region && plan->observation &&
           strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->carrier, carrier) == 0 &&
           strcmp(plan->region, region) == 0 &&
           strcmp(plan->observation, "proof-verdict-only-v1") == 0;
}

const PPProofStorageReadV1 *ppproof_storage_plan_v1_read(
    const PPProofStoragePlanV1 *plan,
    const char *machine,
    const char *role) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine || !role)
        return NULL;
    high = plan->read_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofStorageReadV1 *read = &plan->reads[middle];
        int compared = strcmp(read->machine, machine);
        if (compared == 0)
            compared = strcmp(read->role, role);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->read_len &&
                   strcmp(plan->reads[low].machine, machine) == 0 &&
                   strcmp(plan->reads[low].role, role) == 0
               ? &plan->reads[low]
               : NULL;
}

const PPProofStorageSequenceV1 *ppproof_storage_plan_v1_sequence(
    const PPProofStoragePlanV1 *plan,
    const char *owner,
    const char *provable) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !owner || !provable)
        return NULL;
    high = plan->sequence_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofStorageSequenceV1 *sequence = &plan->sequences[middle];
        int compared = strcmp(sequence->owner, owner);
        if (compared == 0)
            compared = strcmp(sequence->provable, provable);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->sequence_len &&
                   strcmp(plan->sequences[low].owner, owner) == 0 &&
                   strcmp(plan->sequences[low].provable, provable) == 0
               ? &plan->sequences[low]
               : NULL;
}

const PPProofFiniteSupportPlanV1 *ppproof_storage_plan_v1_finite_support(
    const PPProofStoragePlanV1 *plan, const char *owner) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !owner)
        return NULL;
    high = plan->finite_support_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->finite_supports[middle].owner, owner);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->finite_support_len &&
                   strcmp(plan->finite_supports[low].owner, owner) == 0
               ? &plan->finite_supports[low]
               : NULL;
}

const PPProofIndexedValuePlanV1 *ppproof_storage_plan_v1_indexed_value(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->indexed_value_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->indexed_values[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->indexed_value_len &&
                   strcmp(plan->indexed_values[low].machine, machine) == 0
               ? &plan->indexed_values[low]
               : NULL;
}

bool ppproof_indexed_value_plan_v1_admits(
    const PPProofIndexedValuePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *header_role,
    const char *code_role) {
    return plan && operation && machine && header_role && code_role &&
           plan->operation && plan->machine && plan->header_role &&
           plan->code_role && plan->carrier && plan->region &&
           strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->header_role, header_role) == 0 &&
           strcmp(plan->code_role, code_role) == 0 &&
           strcmp(plan->carrier,
                  "prepared-classified-value-table-v1") == 0;
}

const PPProofIndexedEffectMachinePlanV1 *
ppproof_storage_plan_v1_indexed_effect_machine(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->indexed_effect_machine_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(
            plan->indexed_effect_machines[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->indexed_effect_machine_len &&
                   strcmp(plan->indexed_effect_machines[low].machine,
                          machine) == 0
               ? &plan->indexed_effect_machines[low]
               : NULL;
}

bool ppproof_indexed_effect_machine_plan_v1_admits(
    const PPProofIndexedEffectMachinePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region,
    PPProofIndexedEffectUnknownV1 unknown_effect) {
    return plan && operation && machine && region && plan->operation &&
           plan->machine && plan->carrier && plan->prepared_effect &&
           plan->saved_effect && plan->save_effect && plan->region &&
           strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->carrier, "indexed-effect-machine-v1") == 0 &&
           strcmp(plan->prepared_effect, "use-prepared-value-v1") == 0 &&
           strcmp(plan->saved_effect, "use-saved-value-v1") == 0 &&
           strcmp(plan->save_effect, "save-top-value-v1") == 0 &&
           plan->unknown_effect == unknown_effect &&
           strcmp(plan->region, region) == 0;
}

const PPProofIndexedProgramPlanV1 *ppproof_storage_plan_v1_indexed_program(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->indexed_program_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->indexed_programs[middle].machine,
                              machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->indexed_program_len &&
                   strcmp(plan->indexed_programs[low].machine, machine) == 0
               ? &plan->indexed_programs[low]
               : NULL;
}

bool ppproof_storage_plan_v1_prepared_action_cases(
    const PPProofStoragePlanV1 *plan,
    const char *machine,
    const PPProofPreparedActionCaseV1 **cases_out,
    uint32_t *case_len_out) {
    uint32_t low = 0u;
    uint32_t high;
    uint32_t end;

    if (cases_out)
        *cases_out = NULL;
    if (case_len_out)
        *case_len_out = 0u;
    if (!plan || !machine || !cases_out || !case_len_out)
        return false;
    high = plan->prepared_action_case_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(
            plan->prepared_action_cases[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= plan->prepared_action_case_len ||
        strcmp(plan->prepared_action_cases[low].machine, machine) != 0)
        return false;
    end = low;
    while (end < plan->prepared_action_case_len &&
           strcmp(plan->prepared_action_cases[end].machine, machine) == 0)
        end++;
    *cases_out = &plan->prepared_action_cases[low];
    *case_len_out = end - low;
    return true;
}

static bool ppproof_storage_plan_v1_kind_equal(
    const PPRelationalStateLiteralV1 *kind, const char *source_kind) {
    size_t source_len;

    if (!kind || !kind->bytes || kind->len == 0u || !source_kind)
        return false;
    source_len = strlen(source_kind);
    return source_len == kind->len &&
           memcmp(kind->bytes, source_kind, source_len) == 0;
}

static bool ppproof_storage_plan_v1_literal_equal(
    const PPRelationalStateLiteralV1 *left,
    const PPRelationalStateLiteralV1 *right) {
    return left && right && left->bytes && right->bytes &&
           left->len == right->len &&
           memcmp(left->bytes, right->bytes, left->len) == 0;
}

bool ppproof_storage_plan_v1_exact_action_selector(
    const PPProofStoragePlanV1 *plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofPreparedActionCaseV1 **cases_out,
    uint32_t *case_len_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *state_machine;
    const PPRelationalStateTableV1 *state_table;
    const PPProofStorageMachineV1 *storage_machine;
    const PPProofStorageReadV1 *read;
    const PPProofStorageTableV1 *storage_table;
    const PPProofPreparedActionCaseV1 *cases = NULL;
    const PPRelationalStateLiteralV1 *expected_kinds[4];
    const PPProofPreparedActionV1 expected_actions[4] = {
        PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED,
        PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED,
        PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME,
        PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME,
    };
    uint32_t case_len = 0u;
    uint32_t expected;
    uint32_t index;

    if (cases_out)
        *cases_out = NULL;
    if (case_len_out)
        *case_len_out = 0u;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !state_plan || !cases_out || !case_len_out ||
        proof_machine_id >= state_plan->proof_machine_len ||
        !state_plan->proof_machines || !state_plan->tables)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "invalid exact action-selector admission request");

    state_machine = &state_plan->proof_machines[proof_machine_id];
    if (!state_machine->name ||
        state_machine->label_kind_table >= state_plan->table_len)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "exact action selector lacks its state machine");
    state_table =
        &state_plan->tables[state_machine->label_kind_table];
    storage_machine = ppproof_storage_plan_v1_machine(
        plan, state_machine->name);
    read = ppproof_storage_plan_v1_read(
        plan, state_machine->name, "label-kind-v1");
    storage_table = read
        ? ppproof_storage_plan_v1_table(plan, read->table)
        : NULL;
    if (!storage_machine || !state_table->name || !read ||
        !storage_table || strcmp(read->table, state_table->name) != 0 ||
        state_table->arity != 2u || state_table->key_arity != 1u ||
        state_table->lifetime !=
            PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT ||
        storage_table->arity != state_table->arity ||
        storage_table->key_arity != state_table->key_arity ||
        storage_table->lifetime !=
            PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT ||
        read->lifetime != PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "exact action selector disagrees with its persistent label table");

    if (!ppproof_storage_plan_v1_prepared_action_cases(
            plan, state_machine->name, &cases, &case_len) ||
        case_len != 4u)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "exact action selector lacks its four generated action cases");

    expected_kinds[0] = &state_machine->binder_hypothesis_kind;
    expected_kinds[1] = &state_machine->matching_hypothesis_kind;
    expected_kinds[2] = &state_machine->rule_kind_first;
    expected_kinds[3] = &state_machine->rule_kind_second;
    for (expected = 0u; expected < 4u; expected++) {
        uint32_t matched = 0u;
        uint32_t earlier;

        for (earlier = 0u; earlier < expected; earlier++) {
            if (ppproof_storage_plan_v1_literal_equal(
                    expected_kinds[earlier], expected_kinds[expected]))
                return ppproof_storage_plan_v1_fail(
                    error_buf, error_buf_size,
                    "exact action selector has aliased source kinds");
        }
        for (index = 0u; index < case_len; index++) {
            if (!ppproof_storage_plan_v1_kind_equal(
                    expected_kinds[expected], cases[index].source_kind))
                continue;
            if (cases[index].action != expected_actions[expected])
                return ppproof_storage_plan_v1_fail(
                    error_buf, error_buf_size,
                    "exact action selector assigns the wrong stack action");
            matched++;
        }
        if (matched != 1u)
            return ppproof_storage_plan_v1_fail(
                error_buf, error_buf_size,
                "exact action selector is missing or repeats a source kind");
    }
    for (index = 0u; index < case_len; index++) {
        bool recognized = false;
        for (expected = 0u; expected < 4u; expected++) {
            if (ppproof_storage_plan_v1_kind_equal(
                    expected_kinds[expected], cases[index].source_kind)) {
                recognized = true;
                break;
            }
        }
        if (!recognized)
            return ppproof_storage_plan_v1_fail(
                error_buf, error_buf_size,
                "exact action selector contains an unclassified action case");
    }

    *cases_out = cases;
    *case_len_out = case_len;
    return true;
}

static bool ppproof_indexed_program_plan_v1_actions_admitted(
    const PPProofIndexedProgramPlanV1 *plan) {
    uint32_t index;

    if (!plan || !plan->action_cases || plan->action_case_len == 0u)
        return false;
    for (index = 0u; index < plan->action_case_len; index++) {
        const PPProofPreparedActionCaseV1 *action_case =
            &plan->action_cases[index];

        if (!action_case->machine ||
            strcmp(action_case->machine, plan->machine) != 0 ||
            !action_case->source_kind || action_case->source_kind[0] == '\0' ||
            action_case->action <= PPPROOF_PREPARED_ACTION_V1_INVALID ||
            action_case->action > PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME)
            return false;
        for (uint32_t earlier = 0u; earlier < index; earlier++)
            if (strcmp(plan->action_cases[earlier].source_kind,
                       action_case->source_kind) == 0)
                return false;
    }
    return true;
}

bool ppproof_indexed_program_plan_v1_admits(
    const PPProofIndexedProgramPlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *header_role,
    const char *code_role,
    const char *region,
    PPProofIndexedEffectUnknownV1 unknown_effect,
    CettaGsltIndexedSavePlacementV1 save_placement,
    CettaGsltHeaderHypothesisPolicyV1 header_hypothesis_policy) {
    return plan && operation && machine && header_role && code_role && region &&
           plan->operation && plan->machine && plan->header_role &&
           plan->code_role && plan->indexed_carrier && plan->effect_carrier &&
           plan->prepared_effect && plan->saved_effect && plan->save_effect &&
           plan->region && strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->header_role, header_role) == 0 &&
           strcmp(plan->code_role, code_role) == 0 &&
           strcmp(plan->indexed_carrier,
                  "prepared-classified-value-table-v1") == 0 &&
           strcmp(plan->effect_carrier, "indexed-effect-machine-v1") == 0 &&
           strcmp(plan->prepared_effect, "use-prepared-value-v1") == 0 &&
           strcmp(plan->saved_effect, "use-saved-value-v1") == 0 &&
           strcmp(plan->save_effect, "save-top-value-v1") == 0 &&
           ppproof_storage_plan_v1_indexed_decoder_valid(plan) &&
           ppproof_indexed_program_plan_v1_actions_admitted(plan) &&
           plan->unknown_effect == unknown_effect &&
           plan->save_placement == save_placement &&
           plan->header_hypothesis_policy == header_hypothesis_policy &&
           strcmp(plan->region, region) == 0;
}

const PPProofFrameIndexPlanV1 *ppproof_storage_plan_v1_frame_index(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->frame_index_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->frame_indexes[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->frame_index_len &&
                   strcmp(plan->frame_indexes[low].machine, machine) == 0
               ? &plan->frame_indexes[low]
               : NULL;
}

bool ppproof_frame_index_plan_v1_admits(
    const PPProofFrameIndexPlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region) {
    return plan && operation && machine && region && plan->operation &&
           plan->machine && plan->carrier && plan->validation &&
           plan->region &&
           strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->carrier, "u32-open-addressed-index-v1") == 0 &&
           strcmp(plan->validation, "duplicate-reject-v1") == 0 &&
           strcmp(plan->region, region) == 0 &&
           strcmp(region, "proof-call-region-v1") == 0;
}

const PPProofLiteralHolePlanV1 *ppproof_storage_plan_v1_literal_hole(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->literal_hole_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->literal_holes[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->literal_hole_len &&
                   strcmp(plan->literal_holes[low].machine, machine) == 0
               ? &plan->literal_holes[low]
               : NULL;
}

const PPProofTwoPhaseFramePlanV1 *ppproof_storage_plan_v1_two_phase_frame(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !operation)
        return NULL;
    high = plan->two_phase_frame_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofTwoPhaseFramePlanV1 *frame =
            &plan->two_phase_frames[middle];
        int compared = strcmp(frame->operation, operation);
        if (compared == 0) {
            if (frame->action_index < action_index)
                compared = -1;
            else if (frame->action_index > action_index)
                compared = 1;
        }
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->two_phase_frame_len &&
                   strcmp(plan->two_phase_frames[low].operation,
                          operation) == 0 &&
                   plan->two_phase_frames[low].action_index == action_index
               ? &plan->two_phase_frames[low]
               : NULL;
}

bool ppproof_two_phase_frame_plan_v1_admits(
    const PPProofTwoPhaseFramePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *template_carrier,
    const char *region) {
    return plan && operation && machine && template_carrier && region &&
           plan->operation && plan->machine && plan->carrier &&
           plan->template_carrier && plan->literal_head_policy &&
           plan->slot_carrier && plan->binder_validation &&
           plan->stack_discipline && plan->region &&
           strcmp(plan->operation, operation) == 0 &&
           plan->action_index == action_index &&
           strcmp(plan->machine, machine) == 0 &&
           strcmp(plan->carrier, "two-phase-frame-machine-v1") == 0 &&
           strcmp(plan->template_carrier, template_carrier) == 0 &&
           strcmp(plan->literal_head_policy,
                  "literal-head-optional-v1") == 0 &&
           strcmp(plan->slot_carrier,
                  "epoch-stamped-dense-slots-v1") == 0 &&
           strcmp(plan->binder_validation,
                  "unique-dense-binders-v1") == 0 &&
           strcmp(plan->stack_discipline, "exact-stack-suffix-v1") == 0 &&
           strcmp(plan->region, region) == 0;
}

static bool ppproof_storage_plan_v1_parse_table(
    const Atom *record, PPProofStorageTableV1 *table,
    char *error, size_t error_size) {
    table->table = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    table->lifetime = ppproof_storage_plan_v1_lifetime(
        record->expr.elems[4]);
    table->region = ppproof_storage_plan_v1_symbol(record->expr.elems[5]);
    if (!table->table ||
        !ppproof_storage_plan_v1_u32(record->expr.elems[2], &table->arity) ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[3], &table->key_arity) ||
        table->arity == 0u || table->key_arity > table->arity ||
        !ppproof_storage_plan_v1_region_matches(
            table->lifetime, table->region))
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage table record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_machine(
    const Atom *record, PPProofStorageMachineV1 *machine,
    char *error, size_t error_size) {
    machine->machine = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    machine->owner = ppproof_storage_plan_v1_symbol(record->expr.elems[2]);
    machine->base = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    machine->provable = ppproof_storage_plan_v1_symbol(record->expr.elems[4]);
    if (!machine->machine || !machine->owner || !machine->base ||
        !machine->provable)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage machine record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_read(
    const Atom *record, PPProofStorageReadV1 *read,
    char *error, size_t error_size) {
    read->machine = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    read->role = ppproof_storage_plan_v1_symbol(record->expr.elems[2]);
    read->table = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    read->lifetime = ppproof_storage_plan_v1_lifetime(
        record->expr.elems[4]);
    if (!read->machine || !read->role || !read->table ||
        read->lifetime == PPPROOF_STORAGE_LIFETIME_V1_INVALID)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage table-read record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_sequence(
    const Atom *record, PPProofStorageSequenceV1 *sequence,
    char *error, size_t error_size) {
    sequence->owner = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    sequence->base = ppproof_storage_plan_v1_symbol(record->expr.elems[2]);
    sequence->provable = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    sequence->cons = ppproof_storage_plan_v1_symbol(record->expr.elems[4]);
    sequence->nil = ppproof_storage_plan_v1_symbol(record->expr.elems[5]);
    sequence->layout = ppproof_storage_plan_v1_symbol(record->expr.elems[6]);
    sequence->region = ppproof_storage_plan_v1_symbol(record->expr.elems[7]);
    if (!sequence->owner || !sequence->base || !sequence->provable ||
        !sequence->cons || !sequence->nil || !sequence->layout ||
        !sequence->region)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage sequence record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_call(
    const Atom *record, PPProofStorageCallV1 *call,
    char *error, size_t error_size) {
    call->operation = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    call->machine = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    call->owner = ppproof_storage_plan_v1_symbol(record->expr.elems[4]);
    call->provable = ppproof_storage_plan_v1_symbol(record->expr.elems[5]);
    call->region = ppproof_storage_plan_v1_symbol(record->expr.elems[6]);
    call->layout = ppproof_storage_plan_v1_symbol(record->expr.elems[7]);
    call->observation = ppproof_storage_plan_v1_symbol(record->expr.elems[8]);
    if (!call->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &call->action_index) ||
        !call->machine || !call->owner || !call->provable ||
        !call->region || !call->layout || !call->observation)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage call record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_workspace(
    const Atom *record, PPProofWorkspacePlanV1 *workspace,
    char *error, size_t error_size) {
    workspace->operation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[1]);
    workspace->machine = ppproof_storage_plan_v1_symbol(
        record->expr.elems[3]);
    workspace->carrier = ppproof_storage_plan_v1_symbol(
        record->expr.elems[4]);
    workspace->region = ppproof_storage_plan_v1_symbol(
        record->expr.elems[5]);
    workspace->observation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[6]);
    if (!workspace->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &workspace->action_index) ||
        !workspace->machine || !workspace->carrier ||
        !workspace->region || !workspace->observation)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof workspace record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_repetition_cache(
    const Atom *record, PPProofRepetitionCachePlanV1 *cache,
    char *error, size_t error_size) {
    const char **fields[] = {
        &cache->operation, &cache->machine, &cache->key_carrier,
        &cache->value_carrier, &cache->admission_policy,
        &cache->snapshot_policy, &cache->region,
    };
    const uint32_t element_indexes[] = {
        1u, 3u, 4u, 5u, 6u, 7u, 8u,
    };
    uint32_t index;

    if (!ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &cache->action_index))
        return ppproof_storage_plan_v1_fail(
            error, error_size,
            "proof repetition-cache record is malformed");
    for (index = 0u; index < sizeof(fields) / sizeof(fields[0]); index++) {
        *fields[index] = ppproof_storage_plan_v1_symbol(
            record->expr.elems[element_indexes[index]]);
        if (!*fields[index])
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof repetition-cache record is malformed");
    }
    return true;
}

static bool ppproof_storage_plan_v1_parse_finite_support(
    const Atom *record, PPProofFiniteSupportPlanV1 *support,
    char *error, size_t error_size) {
    const char **fields[] = {
        &support->owner, &support->cons, &support->nil,
        &support->support_apart, &support->token_against,
        &support->pair_allowed, &support->apart, &support->literal,
        &support->variable, &support->support_carrier,
        &support->apartness_carrier,
    };
    uint32_t index;

    for (index = 0u; index < sizeof(fields) / sizeof(fields[0]); index++) {
        *fields[index] = ppproof_storage_plan_v1_symbol(
            record->expr.elems[index + 1u]);
        if (!*fields[index])
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof finite-support record is malformed");
    }
    return true;
}

static bool ppproof_storage_plan_v1_parse_indexed_value(
    const Atom *record, PPProofIndexedValuePlanV1 *indexed,
    char *error, size_t error_size) {
    indexed->operation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[1]);
    indexed->machine = ppproof_storage_plan_v1_symbol(
        record->expr.elems[3]);
    indexed->header_role = ppproof_storage_plan_v1_symbol(
        record->expr.elems[4]);
    indexed->code_role = ppproof_storage_plan_v1_symbol(
        record->expr.elems[5]);
    indexed->carrier = ppproof_storage_plan_v1_symbol(
        record->expr.elems[6]);
    indexed->region = ppproof_storage_plan_v1_symbol(
        record->expr.elems[7]);
    if (!indexed->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &indexed->action_index) ||
        !indexed->machine || !indexed->header_role || !indexed->code_role ||
        strcmp(indexed->header_role, indexed->code_role) == 0 ||
        !indexed->carrier || !indexed->region)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof indexed-value record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_indexed_effect_machine(
    const Atom *record, PPProofIndexedEffectMachinePlanV1 *machine,
    char *error, size_t error_size) {
    const char *unknown;

    machine->operation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[1]);
    machine->machine = ppproof_storage_plan_v1_symbol(
        record->expr.elems[3]);
    machine->carrier = ppproof_storage_plan_v1_symbol(
        record->expr.elems[4]);
    machine->prepared_effect = ppproof_storage_plan_v1_symbol(
        record->expr.elems[5]);
    machine->saved_effect = ppproof_storage_plan_v1_symbol(
        record->expr.elems[6]);
    machine->save_effect = ppproof_storage_plan_v1_symbol(
        record->expr.elems[7]);
    unknown = ppproof_storage_plan_v1_symbol(record->expr.elems[8]);
    machine->region = ppproof_storage_plan_v1_symbol(
        record->expr.elems[9]);
    if (unknown && strcmp(unknown, "state-proof-unknown-reject-v1") == 0)
        machine->unknown_effect = PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT;
    else if (unknown &&
             strcmp(unknown, "state-proof-unknown-push-claim-v1") == 0)
        machine->unknown_effect = PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE;
    if (!machine->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &machine->action_index) ||
        !machine->machine || !machine->carrier ||
        !machine->prepared_effect || !machine->saved_effect ||
        !machine->save_effect ||
        machine->unknown_effect ==
            PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_INVALID ||
        !machine->region)
        return ppproof_storage_plan_v1_fail(
            error, error_size,
            "proof indexed-effect-machine record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_prepared_action_case(
    const Atom *record, PPProofPreparedActionCaseV1 *action_case,
    char *error, size_t error_size) {
    action_case->machine = ppproof_storage_plan_v1_symbol(
        record->expr.elems[1]);
    action_case->source_kind = ppproof_storage_plan_v1_symbol(
        record->expr.elems[2]);
    action_case->action = ppproof_storage_plan_v1_prepared_action(
        record->expr.elems[3]);
    if (!action_case->machine || !action_case->source_kind ||
        action_case->action == PPPROOF_PREPARED_ACTION_V1_INVALID)
        return ppproof_storage_plan_v1_fail(
            error, error_size,
            "proof prepared-action case is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_indexed_program(
    const Atom *record, PPProofIndexedProgramPlanV1 *program,
    char *error, size_t error_size) {
    const char *unknown;

    program->operation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[1]);
    program->machine = ppproof_storage_plan_v1_symbol(
        record->expr.elems[3]);
    program->header_role = ppproof_storage_plan_v1_symbol(
        record->expr.elems[4]);
    program->code_role = ppproof_storage_plan_v1_symbol(
        record->expr.elems[5]);
    unknown = ppproof_storage_plan_v1_symbol(record->expr.elems[16]);
    program->save_placement = ppproof_storage_plan_v1_save_placement(
        record->expr.elems[17]);
    program->header_hypothesis_policy =
        ppproof_storage_plan_v1_header_hypothesis_policy(
            record->expr.elems[18]);
    program->indexed_carrier = ppproof_storage_plan_v1_symbol(
        record->expr.elems[19]);
    program->effect_carrier = ppproof_storage_plan_v1_symbol(
        record->expr.elems[20]);
    program->prepared_effect = ppproof_storage_plan_v1_symbol(
        record->expr.elems[21]);
    program->saved_effect = ppproof_storage_plan_v1_symbol(
        record->expr.elems[22]);
    program->save_effect = ppproof_storage_plan_v1_symbol(
        record->expr.elems[23]);
    program->region = ppproof_storage_plan_v1_symbol(
        record->expr.elems[24]);
    if (unknown && strcmp(unknown, "state-proof-unknown-reject-v1") == 0)
        program->unknown_effect = PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT;
    else if (unknown &&
             strcmp(unknown, "state-proof-unknown-push-claim-v1") == 0)
        program->unknown_effect = PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE;
    if (!program->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &program->action_index) ||
        !program->machine || !program->header_role || !program->code_role ||
        strcmp(program->header_role, program->code_role) == 0 ||
        !ppproof_storage_plan_v1_u8(
            record->expr.elems[6], &program->terminal_low) ||
        !ppproof_storage_plan_v1_u8(
            record->expr.elems[7], &program->terminal_high) ||
        !ppproof_storage_plan_v1_u8(
            record->expr.elems[8], &program->continuation_low) ||
        !ppproof_storage_plan_v1_u8(
            record->expr.elems[9], &program->continuation_high) ||
        !ppproof_storage_plan_v1_u8(
            record->expr.elems[10], &program->save_byte) ||
        !ppproof_storage_plan_v1_u8(
            record->expr.elems[11], &program->unknown_byte) ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[12], &program->terminal_radix) ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[13], &program->terminal_digit_bias) ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[14], &program->continuation_radix) ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[15], &program->continuation_digit_bias) ||
        program->unknown_effect ==
            PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_INVALID ||
        program->save_placement == CETTA_GSLT_INDEXED_SAVE_INVALID_V1 ||
        program->header_hypothesis_policy ==
            CETTA_GSLT_HEADER_HYPOTHESIS_INVALID_V1 ||
        !program->indexed_carrier || !program->effect_carrier ||
        !program->prepared_effect || !program->saved_effect ||
        !program->save_effect || !program->region)
        return ppproof_storage_plan_v1_fail(
            error, error_size,
            "proof indexed-program record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_frame_index(
    const Atom *record, PPProofFrameIndexPlanV1 *indexed,
    char *error, size_t error_size) {
    indexed->operation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[1]);
    indexed->machine = ppproof_storage_plan_v1_symbol(
        record->expr.elems[3]);
    indexed->carrier = ppproof_storage_plan_v1_symbol(
        record->expr.elems[4]);
    indexed->validation = ppproof_storage_plan_v1_symbol(
        record->expr.elems[5]);
    indexed->region = ppproof_storage_plan_v1_symbol(
        record->expr.elems[6]);
    if (!indexed->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &indexed->action_index) ||
        !indexed->machine || !indexed->carrier || !indexed->validation ||
        !indexed->region)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof frame-index record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_literal_hole(
    const Atom *record, PPProofLiteralHolePlanV1 *program,
    char *error, size_t error_size) {
    const char **fields[] = {
        &program->machine, &program->owner, &program->cons,
        &program->nil, &program->carrier, &program->source_region,
        &program->execution_region,
    };
    uint32_t index;

    for (index = 0u; index < sizeof(fields) / sizeof(fields[0]); index++) {
        *fields[index] = ppproof_storage_plan_v1_symbol(
            record->expr.elems[index + 1u]);
        if (!*fields[index])
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof literal/hole record is malformed");
    }
    return true;
}

static bool ppproof_storage_plan_v1_parse_two_phase_frame(
    const Atom *record, PPProofTwoPhaseFramePlanV1 *frame,
    char *error, size_t error_size) {
    const char **fields[] = {
        &frame->operation, &frame->machine, &frame->carrier,
        &frame->template_carrier, &frame->literal_head_policy,
        &frame->slot_carrier, &frame->binder_validation,
        &frame->stack_discipline, &frame->region,
    };
    const uint32_t element_indexes[] = {
        1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u,
    };
    uint32_t index;

    if (!ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &frame->action_index))
        return ppproof_storage_plan_v1_fail(
            error, error_size,
            "proof two-phase-frame record is malformed");
    for (index = 0u; index < sizeof(fields) / sizeof(fields[0]); index++) {
        *fields[index] = ppproof_storage_plan_v1_symbol(
            record->expr.elems[element_indexes[index]]);
        if (!*fields[index])
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof two-phase-frame record is malformed");
    }
    return true;
}

static bool ppproof_storage_plan_v1_validate(
    PPProofStoragePlanV1 *plan,
    char *error, size_t error_size) {
    uint32_t index;

    for (index = 1u; index < plan->table_len; index++) {
        if (strcmp(plan->tables[index - 1u].table,
                   plan->tables[index].table) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage table is duplicated");
    }
    for (index = 1u; index < plan->machine_len; index++) {
        if (strcmp(plan->machines[index - 1u].machine,
                   plan->machines[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage machine is duplicated");
    }
    for (index = 1u; index < plan->read_len; index++) {
        if (strcmp(plan->reads[index - 1u].machine,
                   plan->reads[index].machine) == 0 &&
            strcmp(plan->reads[index - 1u].role,
                   plan->reads[index].role) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage table role is duplicated");
    }
    for (index = 1u; index < plan->sequence_len; index++) {
        if (strcmp(plan->sequences[index - 1u].owner,
                   plan->sequences[index].owner) == 0 &&
            strcmp(plan->sequences[index - 1u].provable,
                   plan->sequences[index].provable) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage sequence is duplicated");
    }
    for (index = 1u; index < plan->call_len; index++) {
        if (strcmp(plan->calls[index - 1u].operation,
                   plan->calls[index].operation) == 0 &&
            plan->calls[index - 1u].action_index ==
                plan->calls[index].action_index)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage call is duplicated");
    }
    for (index = 1u; index < plan->workspace_len; index++) {
        if (strcmp(plan->workspaces[index - 1u].operation,
                   plan->workspaces[index].operation) == 0 &&
            plan->workspaces[index - 1u].action_index ==
                plan->workspaces[index].action_index)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof workspace plan is duplicated");
    }
    for (index = 1u; index < plan->repetition_cache_len; index++) {
        if (strcmp(plan->repetition_caches[index - 1u].operation,
                   plan->repetition_caches[index].operation) == 0 &&
            plan->repetition_caches[index - 1u].action_index ==
                plan->repetition_caches[index].action_index)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof repetition-cache plan is duplicated");
    }
    if (plan->repetition_cache_len != 0u &&
        plan->repetition_cache_len != plan->workspace_len)
        return ppproof_storage_plan_v1_fail(
            error, error_size,
            "proof repetition-cache plan is not closed over workspaces");
    for (index = 1u; index < plan->finite_support_len; index++) {
        if (strcmp(plan->finite_supports[index - 1u].owner,
                   plan->finite_supports[index].owner) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof finite-support plan is duplicated");
    }
    for (index = 1u; index < plan->indexed_value_len; index++) {
        if (strcmp(plan->indexed_values[index - 1u].machine,
                   plan->indexed_values[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-value plan is duplicated");
    }
    for (index = 1u; index < plan->indexed_effect_machine_len; index++) {
        if (strcmp(plan->indexed_effect_machines[index - 1u].machine,
                   plan->indexed_effect_machines[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-effect-machine plan is duplicated");
    }
    for (index = 1u; index < plan->prepared_action_case_len; index++) {
        const PPProofPreparedActionCaseV1 *previous =
            &plan->prepared_action_cases[index - 1u];
        const PPProofPreparedActionCaseV1 *current =
            &plan->prepared_action_cases[index];
        if (strcmp(previous->machine, current->machine) == 0 &&
            strcmp(previous->source_kind, current->source_kind) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof prepared-action source kind is duplicated");
    }
    for (index = 1u; index < plan->indexed_program_len; index++) {
        if (strcmp(plan->indexed_programs[index - 1u].machine,
                   plan->indexed_programs[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-program plan is duplicated");
    }
    for (index = 1u; index < plan->frame_index_len; index++) {
        if (strcmp(plan->frame_indexes[index - 1u].machine,
                   plan->frame_indexes[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof frame-index plan is duplicated");
    }
    for (index = 1u; index < plan->literal_hole_len; index++) {
        if (strcmp(plan->literal_holes[index - 1u].machine,
                   plan->literal_holes[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof literal/hole plan is duplicated");
    }
    for (index = 1u; index < plan->two_phase_frame_len; index++) {
        if (strcmp(plan->two_phase_frames[index - 1u].operation,
                   plan->two_phase_frames[index].operation) == 0 &&
            plan->two_phase_frames[index - 1u].action_index ==
                plan->two_phase_frames[index].action_index)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof two-phase-frame plan is duplicated");
    }
    for (index = 0u; index < plan->read_len; index++) {
        const PPProofStorageReadV1 *read = &plan->reads[index];
        const PPProofStorageTableV1 *table =
            ppproof_storage_plan_v1_table(plan, read->table);
        if (!ppproof_storage_plan_v1_machine(plan, read->machine))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage table read names an unknown machine");
        if (!table || table->lifetime != read->lifetime)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage table read disagrees with its table");
    }
    for (index = 0u; index < plan->machine_len; index++) {
        const PPProofStorageMachineV1 *machine = &plan->machines[index];
        const PPProofStorageSequenceV1 *sequence =
            ppproof_storage_plan_v1_sequence(
                plan, machine->owner, machine->provable);
        if (!sequence || strcmp(sequence->base, machine->base) != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage machine lacks its sequence layout");
    }
    for (index = 0u; index < plan->call_len; index++) {
        const PPProofStorageCallV1 *call = &plan->calls[index];
        const PPProofWorkspacePlanV1 *workspace =
            ppproof_storage_plan_v1_workspace(
                plan, call->operation, call->action_index);
        const PPProofStorageMachineV1 *machine =
            ppproof_storage_plan_v1_machine(plan, call->machine);
        const PPProofStorageSequenceV1 *sequence =
            ppproof_storage_plan_v1_sequence(
                plan, call->owner, call->provable);
        if (!machine || !sequence || !workspace ||
            strcmp(machine->owner, call->owner) != 0 ||
            strcmp(machine->provable, call->provable) != 0 ||
            strcmp(sequence->layout, call->layout) != 0 ||
            strcmp(sequence->region, call->region) != 0 ||
            strcmp(workspace->machine, call->machine) != 0 ||
            strcmp(workspace->region, call->region) != 0 ||
            strcmp(workspace->observation, call->observation) != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage call disagrees with its machine or sequence");
    }
    for (index = 0u; index < plan->workspace_len; index++) {
        const PPProofWorkspacePlanV1 *workspace = &plan->workspaces[index];
        const PPProofStorageCallV1 *call = ppproof_storage_plan_v1_call(
            plan, workspace->operation, workspace->action_index);
        const PPProofTwoPhaseFramePlanV1 *frame =
            ppproof_storage_plan_v1_two_phase_frame(
                plan, workspace->operation, workspace->action_index);
        if (!call || strcmp(call->machine, workspace->machine) != 0 ||
            strcmp(call->region, workspace->region) != 0 ||
            strcmp(call->observation, workspace->observation) != 0 ||
            !frame || strcmp(frame->machine, workspace->machine) != 0 ||
            strcmp(frame->region, workspace->region) != 0 ||
            !ppproof_two_phase_frame_plan_v1_admits(
                frame, workspace->operation, workspace->action_index,
                workspace->machine, "literal-hole-run-program-v1",
                workspace->region))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof workspace lacks its admitted frame plan");
    }
    for (index = 0u; index < plan->repetition_cache_len; index++) {
        const PPProofRepetitionCachePlanV1 *cache =
            &plan->repetition_caches[index];
        const PPProofWorkspacePlanV1 *workspace =
            ppproof_storage_plan_v1_workspace(
                plan, cache->operation, cache->action_index);
        const PPProofTwoPhaseFramePlanV1 *frame =
            ppproof_storage_plan_v1_two_phase_frame(
                plan, cache->operation, cache->action_index);
        if (!workspace || !frame ||
            strcmp(workspace->machine, cache->machine) != 0 ||
            strcmp(workspace->region, cache->region) != 0 ||
            strcmp(frame->machine, cache->machine) != 0 ||
            strcmp(frame->region, cache->region) != 0 ||
            !ppproof_repetition_cache_plan_v1_admits(
                cache, cache->operation, cache->action_index,
                cache->machine, cache->region))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof repetition-cache plan lacks its generated license");
    }
    for (index = 0u; index < plan->finite_support_len; index++) {
        const PPProofFiniteSupportPlanV1 *support =
            &plan->finite_supports[index];
        uint32_t sequence_index;
        bool sequence_found = false;
        for (sequence_index = 0u;
             sequence_index < plan->sequence_len; sequence_index++) {
            const PPProofStorageSequenceV1 *sequence =
                &plan->sequences[sequence_index];
            if (strcmp(sequence->owner, support->owner) == 0 &&
                strcmp(sequence->cons, support->cons) == 0 &&
                strcmp(sequence->nil, support->nil) == 0) {
                sequence_found = true;
                break;
            }
        }
        if (!sequence_found)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof finite-support plan lacks its sequence layout");
    }
    for (index = 0u; index < plan->indexed_value_len; index++) {
        const PPProofIndexedValuePlanV1 *indexed =
            &plan->indexed_values[index];
        const PPProofStorageReadV1 *classifier =
            ppproof_storage_plan_v1_read(
                plan, indexed->machine, "label-kind-v1");
        uint32_t call_index;
        bool call_found = false;
        if (!ppproof_storage_plan_v1_machine(plan, indexed->machine) ||
            !classifier ||
            classifier->lifetime !=
                PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT ||
            strcmp(indexed->carrier,
                   "prepared-classified-value-table-v1") != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-value plan lacks its immutable classifier");
        for (call_index = 0u; call_index < plan->call_len; call_index++) {
            const PPProofStorageCallV1 *call = &plan->calls[call_index];
            if (strcmp(call->operation, indexed->operation) == 0 &&
                call->action_index == indexed->action_index &&
                strcmp(call->machine, indexed->machine) == 0 &&
                strcmp(call->region, indexed->region) == 0) {
                call_found = true;
                break;
            }
        }
        if (!call_found)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-value plan lacks its generated call site");
    }
    for (index = 0u; index < plan->indexed_effect_machine_len; index++) {
        const PPProofIndexedEffectMachinePlanV1 *machine =
            &plan->indexed_effect_machines[index];
        const PPProofIndexedValuePlanV1 *indexed =
            ppproof_storage_plan_v1_indexed_value(plan, machine->machine);
        bool admitted =
            machine->unknown_effect ==
                PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT ||
            machine->unknown_effect == PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE;
        if (!indexed || !admitted ||
            strcmp(indexed->operation, machine->operation) != 0 ||
            indexed->action_index != machine->action_index ||
            strcmp(indexed->region, machine->region) != 0 ||
            strcmp(machine->carrier, "indexed-effect-machine-v1") != 0 ||
            strcmp(machine->prepared_effect,
                   "use-prepared-value-v1") != 0 ||
            strcmp(machine->saved_effect, "use-saved-value-v1") != 0 ||
            strcmp(machine->save_effect, "save-top-value-v1") != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-effect-machine plan lacks its admitted inputs");
    }
    for (index = 0u; index < plan->prepared_action_case_len; index++) {
        const PPProofPreparedActionCaseV1 *action_case =
            &plan->prepared_action_cases[index];
        if (!ppproof_storage_plan_v1_machine(plan, action_case->machine))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof prepared-action case names an unknown machine");
    }
    for (index = 0u; index < plan->indexed_program_len; index++) {
        PPProofIndexedProgramPlanV1 *program =
            &plan->indexed_programs[index];
        const PPProofIndexedValuePlanV1 *indexed =
            ppproof_storage_plan_v1_indexed_value(plan, program->machine);
        const PPProofIndexedEffectMachinePlanV1 *effect =
            ppproof_storage_plan_v1_indexed_effect_machine(
                plan, program->machine);
        uint32_t action_index;

        program->action_cases = NULL;
        program->action_case_len = 0u;
        for (action_index = 0u;
             action_index < plan->prepared_action_case_len; action_index++) {
            const PPProofPreparedActionCaseV1 *action_case =
                &plan->prepared_action_cases[action_index];
            if (strcmp(action_case->machine, program->machine) == 0) {
                if (!program->action_cases)
                    program->action_cases = action_case;
                else if (action_case !=
                         program->action_cases + program->action_case_len)
                    return ppproof_storage_plan_v1_fail(
                        error, error_size,
                        "proof prepared-action cases are not contiguous");
                if (program->action_case_len == UINT32_MAX)
                    return ppproof_storage_plan_v1_fail(
                        error, error_size,
                        "proof prepared-action case count overflows");
                program->action_case_len++;
            }
        }
        if (!indexed || !effect ||
            !ppproof_storage_plan_v1_indexed_decoder_valid(program) ||
            strcmp(program->operation, indexed->operation) != 0 ||
            program->action_index != indexed->action_index ||
            strcmp(program->header_role, indexed->header_role) != 0 ||
            strcmp(program->code_role, indexed->code_role) != 0 ||
            strcmp(program->indexed_carrier, indexed->carrier) != 0 ||
            strcmp(program->region, indexed->region) != 0 ||
            strcmp(program->operation, effect->operation) != 0 ||
            program->action_index != effect->action_index ||
            strcmp(program->effect_carrier, effect->carrier) != 0 ||
            strcmp(program->prepared_effect, effect->prepared_effect) != 0 ||
            strcmp(program->saved_effect, effect->saved_effect) != 0 ||
            strcmp(program->save_effect, effect->save_effect) != 0 ||
            !ppproof_indexed_program_plan_v1_actions_admitted(program) ||
            program->unknown_effect != effect->unknown_effect ||
            strcmp(program->region, effect->region) != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof indexed-program plan disagrees with its generated inputs");
    }
    for (index = 0u; index < plan->frame_index_len; index++) {
        const PPProofFrameIndexPlanV1 *indexed =
            &plan->frame_indexes[index];
        const PPProofStorageReadV1 *mandatory_read;
        const PPProofStorageReadV1 *hypothesis_read;
        uint32_t call_index;
        bool call_found = false;
        if (!ppproof_storage_plan_v1_machine(plan, indexed->machine))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof frame-index plan names an unknown machine");
        mandatory_read = ppproof_storage_plan_v1_read(
            plan, indexed->machine, "mandatory-variable-v1");
        hypothesis_read = ppproof_storage_plan_v1_read(
            plan, indexed->machine, "ordered-hypothesis-v1");
        if (!mandatory_read || !hypothesis_read)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof frame-index plan lacks its generated table reads");
        for (call_index = 0u; call_index < plan->call_len; call_index++) {
            const PPProofStorageCallV1 *call = &plan->calls[call_index];
            if (strcmp(call->operation, indexed->operation) == 0 &&
                call->action_index == indexed->action_index &&
                strcmp(call->machine, indexed->machine) == 0 &&
                strcmp(call->region, indexed->region) == 0) {
                call_found = true;
                break;
            }
        }
        if (!call_found)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof frame-index plan lacks its generated call site");
    }
    for (index = 0u; index < plan->literal_hole_len; index++) {
        const PPProofLiteralHolePlanV1 *program =
            &plan->literal_holes[index];
        const PPProofStorageMachineV1 *machine =
            ppproof_storage_plan_v1_machine(plan, program->machine);
        const PPProofStorageSequenceV1 *sequence;
        const PPProofStorageReadV1 *formula_read;
        const PPProofStorageReadV1 *mandatory_read;
        if (!machine || strcmp(machine->owner, program->owner) != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof literal/hole plan names an incompatible machine");
        sequence = ppproof_storage_plan_v1_sequence(
            plan, machine->owner, machine->provable);
        formula_read = ppproof_storage_plan_v1_read(
            plan, machine->machine, "formula-v1");
        mandatory_read = ppproof_storage_plan_v1_read(
            plan, machine->machine, "mandatory-variable-v1");
        if (!sequence || strcmp(sequence->cons, program->cons) != 0 ||
            strcmp(sequence->nil, program->nil) != 0 ||
            strcmp(sequence->region, program->execution_region) != 0 ||
            !formula_read || !mandatory_read ||
            formula_read->lifetime !=
                PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT ||
            mandatory_read->lifetime !=
                PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT ||
            strcmp(program->source_region, "state-run-region-v1") != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof literal/hole plan lacks immutable sequence inputs");
    }
    for (index = 0u; index < plan->two_phase_frame_len; index++) {
        const PPProofTwoPhaseFramePlanV1 *frame =
            &plan->two_phase_frames[index];
        const PPProofStorageCallV1 *call = NULL;
        const PPProofLiteralHolePlanV1 *literal =
            ppproof_storage_plan_v1_literal_hole(plan, frame->machine);
        uint32_t call_index;

        for (call_index = 0u; call_index < plan->call_len; call_index++) {
            const PPProofStorageCallV1 *candidate = &plan->calls[call_index];
            if (strcmp(candidate->operation, frame->operation) == 0 &&
                candidate->action_index == frame->action_index) {
                call = candidate;
                break;
            }
        }
        if (!call || !literal ||
            strcmp(call->machine, frame->machine) != 0 ||
            strcmp(call->region, frame->region) != 0 ||
            strcmp(literal->carrier, frame->template_carrier) != 0 ||
            strcmp(literal->execution_region, frame->region) != 0 ||
            !ppproof_two_phase_frame_plan_v1_admits(
                frame, frame->operation, frame->action_index,
                frame->machine, literal->carrier, frame->region))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof two-phase-frame plan lacks its admitted inputs");
    }
    return true;
}

bool ppproof_storage_plan_v1_load(
    PPProofStoragePlanV1 *plan,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size) {
    PPProofStoragePlanV1 result;
    PPProofStoragePlanStorageV1 *storage = NULL;
    PPProofStorageCountsV1 counts = {0};
    PPProofStorageCountsV1 writes = {0};
    size_t index;
    bool ok = false;

    ppproof_storage_plan_v1_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !answer_path)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size, "invalid proof storage plan request");
    storage = calloc(1u, sizeof(*storage));
    if (!storage)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "cannot allocate proof storage plan storage");
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path, error_buf, error_buf_size))
        goto done;
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *record = NULL;
        PPProofStorageRecordV1 kind = ppproof_storage_plan_v1_record(
            storage->answers.terms[index], &record);
        uint32_t *count = NULL;

        (void)record;
        switch (kind) {
        case PPPROOF_STORAGE_RECORD_V1_TABLE:
            count = &counts.tables;
            break;
        case PPPROOF_STORAGE_RECORD_V1_MACHINE:
            count = &counts.machines;
            break;
        case PPPROOF_STORAGE_RECORD_V1_READ:
            count = &counts.reads;
            break;
        case PPPROOF_STORAGE_RECORD_V1_SEQUENCE:
            count = &counts.sequences;
            break;
        case PPPROOF_STORAGE_RECORD_V1_CALL:
            count = &counts.calls;
            break;
        case PPPROOF_STORAGE_RECORD_V1_WORKSPACE:
            count = &counts.workspaces;
            break;
        case PPPROOF_STORAGE_RECORD_V1_REPETITION_CACHE:
            count = &counts.repetition_caches;
            break;
        case PPPROOF_STORAGE_RECORD_V1_FINITE_SUPPORT:
            count = &counts.finite_supports;
            break;
        case PPPROOF_STORAGE_RECORD_V1_INDEXED_VALUE:
            count = &counts.indexed_values;
            break;
        case PPPROOF_STORAGE_RECORD_V1_INDEXED_EFFECT_MACHINE:
            count = &counts.indexed_effect_machines;
            break;
        case PPPROOF_STORAGE_RECORD_V1_PREPARED_ACTION_CASE:
            count = &counts.prepared_action_cases;
            break;
        case PPPROOF_STORAGE_RECORD_V1_INDEXED_PROGRAM:
            count = &counts.indexed_programs;
            break;
        case PPPROOF_STORAGE_RECORD_V1_FRAME_INDEX:
            count = &counts.frame_indexes;
            break;
        case PPPROOF_STORAGE_RECORD_V1_LITERAL_HOLE:
            count = &counts.literal_holes;
            break;
        case PPPROOF_STORAGE_RECORD_V1_TWO_PHASE_FRAME:
            count = &counts.two_phase_frames;
            break;
        case PPPROOF_STORAGE_RECORD_V1_UNKNOWN:
        default:
            ppproof_storage_plan_v1_fail(
                error_buf, error_buf_size,
                "proof storage answer has an unknown record");
            goto done;
        }
        if (!ppproof_storage_plan_v1_count(
                count, error_buf, error_buf_size))
            goto done;
    }
    if (counts.tables == 0u || counts.machines == 0u ||
        counts.reads == 0u || counts.sequences == 0u ||
        counts.calls == 0u || counts.workspaces == 0u) {
        ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "proof storage plan omits a required record family");
        goto done;
    }
    if (!ppproof_storage_plan_v1_alloc(
            (void **)&result.tables, counts.tables,
            sizeof(*result.tables), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.machines, counts.machines,
            sizeof(*result.machines), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.reads, counts.reads,
            sizeof(*result.reads), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.sequences, counts.sequences,
            sizeof(*result.sequences), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.calls, counts.calls,
            sizeof(*result.calls), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.workspaces, counts.workspaces,
            sizeof(*result.workspaces), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.repetition_caches,
            counts.repetition_caches,
            sizeof(*result.repetition_caches),
            error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.finite_supports, counts.finite_supports,
            sizeof(*result.finite_supports), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.indexed_values, counts.indexed_values,
            sizeof(*result.indexed_values), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.indexed_effect_machines,
            counts.indexed_effect_machines,
            sizeof(*result.indexed_effect_machines),
            error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.prepared_action_cases,
            counts.prepared_action_cases,
            sizeof(*result.prepared_action_cases),
            error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.indexed_programs,
            counts.indexed_programs,
            sizeof(*result.indexed_programs),
            error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.frame_indexes, counts.frame_indexes,
            sizeof(*result.frame_indexes), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.literal_holes, counts.literal_holes,
            sizeof(*result.literal_holes), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.two_phase_frames, counts.two_phase_frames,
            sizeof(*result.two_phase_frames), error_buf, error_buf_size))
        goto done;
    result.table_len = counts.tables;
    result.machine_len = counts.machines;
    result.read_len = counts.reads;
    result.sequence_len = counts.sequences;
    result.call_len = counts.calls;
    result.workspace_len = counts.workspaces;
    result.repetition_cache_len = counts.repetition_caches;
    result.finite_support_len = counts.finite_supports;
    result.indexed_value_len = counts.indexed_values;
    result.indexed_effect_machine_len = counts.indexed_effect_machines;
    result.prepared_action_case_len = counts.prepared_action_cases;
    result.indexed_program_len = counts.indexed_programs;
    result.frame_index_len = counts.frame_indexes;
    result.literal_hole_len = counts.literal_holes;
    result.two_phase_frame_len = counts.two_phase_frames;
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *record = NULL;
        PPProofStorageRecordV1 kind = ppproof_storage_plan_v1_record(
            storage->answers.terms[index], &record);
        bool parsed = false;

        switch (kind) {
        case PPPROOF_STORAGE_RECORD_V1_TABLE:
            parsed = ppproof_storage_plan_v1_parse_table(
                record, &result.tables[writes.tables++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_MACHINE:
            parsed = ppproof_storage_plan_v1_parse_machine(
                record, &result.machines[writes.machines++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_READ:
            parsed = ppproof_storage_plan_v1_parse_read(
                record, &result.reads[writes.reads++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_SEQUENCE:
            parsed = ppproof_storage_plan_v1_parse_sequence(
                record, &result.sequences[writes.sequences++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_CALL:
            parsed = ppproof_storage_plan_v1_parse_call(
                record, &result.calls[writes.calls++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_WORKSPACE:
            parsed = ppproof_storage_plan_v1_parse_workspace(
                record, &result.workspaces[writes.workspaces++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_REPETITION_CACHE:
            parsed = ppproof_storage_plan_v1_parse_repetition_cache(
                record,
                &result.repetition_caches[writes.repetition_caches++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_FINITE_SUPPORT:
            parsed = ppproof_storage_plan_v1_parse_finite_support(
                record, &result.finite_supports[writes.finite_supports++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_INDEXED_VALUE:
            parsed = ppproof_storage_plan_v1_parse_indexed_value(
                record, &result.indexed_values[writes.indexed_values++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_INDEXED_EFFECT_MACHINE:
            parsed = ppproof_storage_plan_v1_parse_indexed_effect_machine(
                record,
                &result.indexed_effect_machines[
                    writes.indexed_effect_machines++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_PREPARED_ACTION_CASE:
            parsed = ppproof_storage_plan_v1_parse_prepared_action_case(
                record,
                &result.prepared_action_cases[
                    writes.prepared_action_cases++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_INDEXED_PROGRAM:
            parsed = ppproof_storage_plan_v1_parse_indexed_program(
                record,
                &result.indexed_programs[writes.indexed_programs++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_FRAME_INDEX:
            parsed = ppproof_storage_plan_v1_parse_frame_index(
                record, &result.frame_indexes[writes.frame_indexes++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_LITERAL_HOLE:
            parsed = ppproof_storage_plan_v1_parse_literal_hole(
                record, &result.literal_holes[writes.literal_holes++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_TWO_PHASE_FRAME:
            parsed = ppproof_storage_plan_v1_parse_two_phase_frame(
                record,
                &result.two_phase_frames[writes.two_phase_frames++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_UNKNOWN:
        default:
            break;
        }
        if (!parsed)
            goto done;
    }
    qsort(result.tables, result.table_len, sizeof(*result.tables),
          ppproof_storage_plan_v1_table_compare);
    qsort(result.machines, result.machine_len, sizeof(*result.machines),
          ppproof_storage_plan_v1_machine_compare);
    qsort(result.reads, result.read_len, sizeof(*result.reads),
          ppproof_storage_plan_v1_read_compare);
    qsort(result.sequences, result.sequence_len, sizeof(*result.sequences),
          ppproof_storage_plan_v1_sequence_compare);
    qsort(result.calls, result.call_len, sizeof(*result.calls),
          ppproof_storage_plan_v1_call_compare);
    qsort(result.workspaces, result.workspace_len,
          sizeof(*result.workspaces),
          ppproof_storage_plan_v1_workspace_compare);
    qsort(result.repetition_caches, result.repetition_cache_len,
          sizeof(*result.repetition_caches),
          ppproof_storage_plan_v1_repetition_cache_compare);
    qsort(result.finite_supports, result.finite_support_len,
          sizeof(*result.finite_supports),
          ppproof_storage_plan_v1_finite_support_compare);
    qsort(result.indexed_values, result.indexed_value_len,
          sizeof(*result.indexed_values),
          ppproof_storage_plan_v1_indexed_value_compare);
    qsort(result.indexed_effect_machines,
          result.indexed_effect_machine_len,
          sizeof(*result.indexed_effect_machines),
          ppproof_storage_plan_v1_indexed_effect_machine_compare);
    qsort(result.prepared_action_cases,
          result.prepared_action_case_len,
          sizeof(*result.prepared_action_cases),
          ppproof_storage_plan_v1_prepared_action_case_compare);
    qsort(result.indexed_programs, result.indexed_program_len,
          sizeof(*result.indexed_programs),
          ppproof_storage_plan_v1_indexed_program_compare);
    qsort(result.frame_indexes, result.frame_index_len,
          sizeof(*result.frame_indexes),
          ppproof_storage_plan_v1_frame_index_compare);
    qsort(result.literal_holes, result.literal_hole_len,
          sizeof(*result.literal_holes),
          ppproof_storage_plan_v1_literal_hole_compare);
    qsort(result.two_phase_frames, result.two_phase_frame_len,
          sizeof(*result.two_phase_frames),
          ppproof_storage_plan_v1_two_phase_frame_compare);
    if (!ppproof_storage_plan_v1_validate(
            &result, error_buf, error_buf_size))
        goto done;
    memcpy(result.semantic_digest, storage->answers.digest,
           sizeof(result.semantic_digest));
    result.storage = storage;
    storage = NULL;
    ppproof_storage_plan_v1_free(plan);
    *plan = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    ppproof_storage_plan_v1_free(&result);
    return ok;
}
