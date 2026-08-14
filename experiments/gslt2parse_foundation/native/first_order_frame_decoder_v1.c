#include "first_order_frame_decoder_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { PPFIRST_ORDER_FRAME_TABLE_COUNT_V1 = 9 };

static bool ppfirst_order_frame_decoder_v1_fail(
    char *buffer, size_t size, const char *format, ...) {
    va_list arguments;

    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static void ppfirst_order_frame_decoder_v1_table_ids(
    const PPRelationalStateProofMachineV1 *machine,
    uint32_t ids[PPFIRST_ORDER_FRAME_TABLE_COUNT_V1]) {
    ids[0] = machine->label_kind_table;
    ids[1] = machine->formula_table;
    ids[2] = machine->binder_variable_table;
    ids[3] = machine->mandatory_variable_table;
    ids[4] = machine->assertion_hypothesis_table;
    ids[5] = machine->assertion_disjoint_table;
    ids[6] = machine->active_hypothesis_table;
    ids[7] = machine->active_disjoint_table;
    ids[8] = machine->symbol_kind_table;
}

static PPProofStorageLifetimeV1
ppfirst_order_frame_decoder_v1_storage_lifetime(
    PPRelationalStateLifetimeV1 lifetime) {
    if (lifetime == PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT)
        return PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT;
    if (lifetime == PPRELATIONAL_STATE_LIFETIME_V1_SCOPED)
        return PPPROOF_STORAGE_LIFETIME_V1_SCOPED;
    if (lifetime == PPRELATIONAL_STATE_LIFETIME_V1_TRANSACTIONAL)
        return PPPROOF_STORAGE_LIFETIME_V1_TRANSACTIONAL;
    return PPPROOF_STORAGE_LIFETIME_V1_INVALID;
}

static bool ppfirst_order_frame_decoder_v1_table_dependencies(
    const PPProofStoragePlanV1 *storage_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *machine;
    uint32_t table_ids[PPFIRST_ORDER_FRAME_TABLE_COUNT_V1];
    bool read_seen[PPFIRST_ORDER_FRAME_TABLE_COUNT_V1] = {false};
    uint32_t machine_read_len = 0u;
    uint32_t index;

    if (!storage_plan || !state_plan ||
        proof_machine_id >= state_plan->proof_machine_len)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "invalid generated frame dependency request");
    machine = &state_plan->proof_machines[proof_machine_id];
    if (!machine->name)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "generated frame machine has no identity");
    ppfirst_order_frame_decoder_v1_table_ids(machine, table_ids);
    for (index = 0u; index < PPFIRST_ORDER_FRAME_TABLE_COUNT_V1; index++) {
        const PPRelationalStateTableV1 *state_table;
        const PPProofStorageTableV1 *storage_table;
        PPProofStorageLifetimeV1 lifetime;
        uint32_t earlier;

        if (table_ids[index] >= state_plan->table_len)
            return ppfirst_order_frame_decoder_v1_fail(
                error_buf, error_buf_size,
                "generated frame references a missing state table");
        for (earlier = 0u; earlier < index; earlier++) {
            if (table_ids[earlier] == table_ids[index])
                return ppfirst_order_frame_decoder_v1_fail(
                    error_buf, error_buf_size,
                    "generated frame aliases two machine table slots");
        }
        state_table = &state_plan->tables[table_ids[index]];
        lifetime = ppfirst_order_frame_decoder_v1_storage_lifetime(
            state_table->lifetime);
        storage_table = state_table->name
                            ? ppproof_storage_plan_v1_table(
                                  storage_plan, state_table->name)
                            : NULL;
        if (!storage_table ||
            storage_table->arity != state_table->arity ||
            storage_table->key_arity != state_table->key_arity ||
            storage_table->lifetime != lifetime)
            return ppfirst_order_frame_decoder_v1_fail(
                error_buf, error_buf_size,
                "generated state and storage table shapes disagree");
    }
    for (index = 0u; index < storage_plan->read_len; index++) {
        const PPProofStorageReadV1 *read = &storage_plan->reads[index];
        uint32_t slot;

        if (strcmp(read->machine, machine->name) != 0)
            continue;
        machine_read_len++;
        for (slot = 0u; slot < PPFIRST_ORDER_FRAME_TABLE_COUNT_V1; slot++) {
            const PPRelationalStateTableV1 *state_table =
                &state_plan->tables[table_ids[slot]];
            if (strcmp(read->table, state_table->name) == 0)
                break;
        }
        if (slot == PPFIRST_ORDER_FRAME_TABLE_COUNT_V1 || read_seen[slot] ||
            read->lifetime !=
                ppfirst_order_frame_decoder_v1_storage_lifetime(
                    state_plan->tables[table_ids[slot]].lifetime))
            return ppfirst_order_frame_decoder_v1_fail(
                error_buf, error_buf_size,
                "generated storage reads do not match the frame table graph");
        read_seen[slot] = true;
    }
    if (machine_read_len != PPFIRST_ORDER_FRAME_TABLE_COUNT_V1)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "generated storage reads do not cover the frame table graph");
    for (index = 0u; index < PPFIRST_ORDER_FRAME_TABLE_COUNT_V1; index++) {
        if (!read_seen[index])
            return ppfirst_order_frame_decoder_v1_fail(
                error_buf, error_buf_size,
                "generated storage omits a frame table dependency");
    }
    return true;
}

bool ppfirst_order_frame_decoder_v1_validate_state_program(
    const PPFirstOrderFrameDecoderV1 *decoder,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *machine;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!decoder || !decoder->storage_plan || !state_plan ||
        proof_machine_id >= state_plan->proof_machine_len)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "invalid state-program frame validation request");
    machine = &state_plan->proof_machines[proof_machine_id];
    if (!machine->name || strcmp(machine->name, decoder->machine) != 0)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "state program and frame decoder name different machines");
    return ppfirst_order_frame_decoder_v1_table_dependencies(
        decoder->storage_plan, state_plan, proof_machine_id,
        error_buf, error_buf_size);
}

bool ppfirst_order_frame_decoder_v1_cache_admission(
    const PPFirstOrderFrameDecoderV1 *decoder,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    PPRelationalStackProofV1CacheAdmission *admission_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStateProofMachineV1 *machine;

    if (!admission_out ||
        !ppfirst_order_frame_decoder_v1_validate_state_program(
            decoder, state_plan, proof_machine_id,
            error_buf, error_buf_size))
        return false;
    machine = &state_plan->proof_machines[proof_machine_id];
    *admission_out = (PPRelationalStackProofV1CacheAdmission){
        .label_kind_table = machine->label_kind_table,
        .formula_table = machine->formula_table,
        .binder_variable_table = machine->binder_variable_table,
        .mandatory_variable_table = machine->mandatory_variable_table,
        .assertion_hypothesis_table = machine->assertion_hypothesis_table,
        .assertion_disjoint_table = machine->assertion_disjoint_table,
        .symbol_kind_table = machine->symbol_kind_table,
        .scratch_reuse_admitted = true,
        .finite_support_admitted = decoder->finite_support != NULL,
        .indexed_values_admitted = decoder->indexed_value != NULL,
        .literal_hole_admitted = decoder->literal_hole != NULL,
        .two_phase_frame_admitted = decoder->two_phase_frame_admitted,
    };
    memcpy(admission_out->native_type_digest,
           decoder->native_type_digest,
           sizeof(admission_out->native_type_digest));
    memcpy(admission_out->storage_plan_digest,
           decoder->storage_plan_digest,
           sizeof(admission_out->storage_plan_digest));
    return true;
}

static bool ppfirst_order_frame_decoder_v1_head_arity(
    const PPOSLFNativeTypePlanV1 *native_types,
    const char *constructor,
    uint32_t expected,
    char *error,
    size_t error_size) {
    uint32_t actual;

    if (!pposlf_native_type_plan_v1_head_arity(
            native_types, constructor, &actual) || actual != expected)
        return ppfirst_order_frame_decoder_v1_fail(
            error, error_size,
            "native type signature disagrees with the frame layout");
    return true;
}

static bool ppfirst_order_frame_decoder_v1_calls(
    const PPProofStoragePlanV1 *storage_plan,
    const PPProofStorageMachineV1 *machine,
    char *error,
    size_t error_size) {
    const PPProofIndexedProgramPlanV1 *indexed_program;
    uint32_t index;
    uint32_t matched = 0u;

    indexed_program = ppproof_storage_plan_v1_indexed_program(
        storage_plan, machine->machine);
    for (index = 0u; index < storage_plan->call_len; index++) {
        const PPProofStorageCallV1 *call = &storage_plan->calls[index];
        const PPProofWorkspacePlanV1 *workspace;
        const PPProofTwoPhaseFramePlanV1 *frame;
        const PPProofLiteralHolePlanV1 *literal;
        const char *workspace_carrier;
        if (strcmp(call->machine, machine->machine) != 0)
            continue;
        matched++;
        if (strcmp(call->owner, machine->owner) != 0 ||
            strcmp(call->provable, machine->provable) != 0 ||
            !ppproof_storage_call_v1_admits_reusable_workspace(
                call, call->operation, call->action_index,
                call->machine, "proof-call-region-v1"))
            return ppfirst_order_frame_decoder_v1_fail(
                error, error_size,
                "proof call lacks generated reusable-call admission");
        workspace_carrier =
            indexed_program &&
                    strcmp(indexed_program->operation, call->operation) == 0 &&
                    indexed_program->action_index == call->action_index
                ? "indexed-stack-proof-call-workspace-v1"
                : "stack-proof-call-workspace-v1";
        workspace = ppproof_storage_plan_v1_workspace(
            storage_plan, call->operation, call->action_index);
        if (!ppproof_workspace_plan_v1_admits(
                workspace, call->operation, call->action_index,
                call->machine, workspace_carrier,
                "proof-call-region-v1"))
            return ppfirst_order_frame_decoder_v1_fail(
                error, error_size,
                "proof call lacks generated reusable-workspace admission");
        frame = ppproof_storage_plan_v1_two_phase_frame(
            storage_plan, call->operation, call->action_index);
        literal = ppproof_storage_plan_v1_literal_hole(
            storage_plan, call->machine);
        if (!literal ||
            !ppproof_two_phase_frame_plan_v1_admits(
                frame, call->operation, call->action_index, call->machine,
                literal->carrier, call->region))
            return ppfirst_order_frame_decoder_v1_fail(
                error, error_size,
                "proof call lacks generated two-phase-frame admission");
    }
    if (matched == 0u)
        return ppfirst_order_frame_decoder_v1_fail(
            error, error_size, "proof machine has no generated call site");
    return true;
}

static bool ppfirst_order_frame_decoder_v1_indexed_program(
    const PPProofStoragePlanV1 *storage_plan,
    const PPRelationalStateProofMachineV1 *machine,
    bool indexed_values_present,
    char *error,
    size_t error_size) {
    const PPProofIndexedProgramPlanV1 *program;
    const PPProofFrameIndexPlanV1 *frame_index;
    PPProofIndexedEffectUnknownV1 unknown_effect;

    if (!storage_plan || !machine || !machine->name)
        return ppfirst_order_frame_decoder_v1_fail(
            error, error_size,
            "invalid indexed instruction correspondence request");
    program = ppproof_storage_plan_v1_indexed_program(
        storage_plan, machine->name);
    if (!program)
        return !indexed_values_present ||
               ppfirst_order_frame_decoder_v1_fail(
                   error, error_size,
                   "indexed values lack a generated instruction plan");
    if (!indexed_values_present)
        return ppfirst_order_frame_decoder_v1_fail(
            error, error_size,
            "indexed instruction plan lacks its prepared values");
    unknown_effect =
        machine->unknown_policy ==
                PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM
            ? PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE
            : PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT;
    if (program->terminal_low != machine->terminal_low ||
        program->terminal_high != machine->terminal_high ||
        program->continuation_low != machine->continuation_low ||
        program->continuation_high != machine->continuation_high ||
        program->save_byte != machine->save_byte ||
        program->unknown_byte != machine->unknown_byte ||
        program->terminal_radix != machine->terminal_radix ||
        program->terminal_digit_bias != machine->terminal_digit_bias ||
        program->continuation_radix != machine->continuation_radix ||
        program->continuation_digit_bias !=
            machine->continuation_digit_bias ||
        program->unknown_effect != unknown_effect ||
        program->save_placement != machine->save_placement ||
        program->header_hypothesis_policy !=
            machine->header_hypothesis_policy)
        return ppfirst_order_frame_decoder_v1_fail(
            error, error_size,
            "indexed instruction plan disagrees with the generated proof machine");
    frame_index = ppproof_storage_plan_v1_frame_index(
        storage_plan, machine->name);
    if (!ppproof_frame_index_plan_v1_admits(
            frame_index, program->operation, program->action_index,
            machine->name, program->region))
        return ppfirst_order_frame_decoder_v1_fail(
            error, error_size,
            "indexed instruction plan lacks its exact frame index");
    return true;
}

bool ppfirst_order_frame_decoder_v1_admit(
    const PPProofStoragePlanV1 *storage_plan,
    const PPOSLFNativeTypePlanV1 *native_types,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    PPFirstOrderFrameDecoderV1 *decoder_out,
    char *error_buf,
    size_t error_buf_size) {
    PPFirstOrderFrameDecoderV1 decoder = {0};
    const PPProofStorageMachineV1 *machine;
    const PPProofStorageSequenceV1 *sequence;
    const PPProofFiniteSupportPlanV1 *finite_support;
    const PPProofIndexedValuePlanV1 *indexed_value;
    const PPProofLiteralHolePlanV1 *literal_hole;
    const char *machine_name;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!storage_plan || !native_types || !state_plan || !decoder_out ||
        proof_machine_id >= state_plan->proof_machine_len)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "invalid first-order frame decoder request");
    machine_name = state_plan->proof_machines[proof_machine_id].name;
    if (!machine_name)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "generated frame machine has no identity");
    machine = ppproof_storage_plan_v1_machine(
        storage_plan, machine_name);
    if (!machine)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "first-order frame machine is not declared");
    sequence = ppproof_storage_plan_v1_sequence(
        storage_plan, machine->owner, machine->provable);
    if (!sequence || strcmp(sequence->base, machine->base) != 0 ||
        strcmp(sequence->layout, "flat-symbol-id-vector-v1") != 0 ||
        strcmp(sequence->region, "proof-call-region-v1") != 0)
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "proof sequence is unsupported by the first-order frame backend");
    finite_support = ppproof_storage_plan_v1_finite_support(
        storage_plan, machine->owner);
    if (finite_support &&
        (strcmp(finite_support->cons, sequence->cons) != 0 ||
         strcmp(finite_support->nil, sequence->nil) != 0 ||
         strcmp(finite_support->support_carrier,
                "finite-dense-bitset-v1") != 0 ||
         strcmp(finite_support->apartness_carrier,
                "finite-apartness-matrix-v1") != 0))
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "finite support plan is unsupported by the frame backend");
    indexed_value = ppproof_storage_plan_v1_indexed_value(
        storage_plan, machine->machine);
    if (indexed_value &&
        (strcmp(indexed_value->carrier,
                "prepared-classified-value-table-v1") != 0 ||
         strcmp(indexed_value->region, "proof-call-region-v1") != 0))
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "indexed value plan is unsupported by the frame backend");
    literal_hole = ppproof_storage_plan_v1_literal_hole(
        storage_plan, machine->machine);
    if (literal_hole &&
        (strcmp(literal_hole->owner, machine->owner) != 0 ||
         strcmp(literal_hole->cons, sequence->cons) != 0 ||
         strcmp(literal_hole->nil, sequence->nil) != 0 ||
         strcmp(literal_hole->carrier,
                "literal-hole-run-program-v1") != 0 ||
         strcmp(literal_hole->source_region,
                "state-run-region-v1") != 0 ||
         strcmp(literal_hole->execution_region,
                "proof-call-region-v1") != 0))
        return ppfirst_order_frame_decoder_v1_fail(
            error_buf, error_buf_size,
            "literal/hole plan is unsupported by the frame backend");
    if (!ppfirst_order_frame_decoder_v1_head_arity(
            native_types, machine->provable, 1u,
            error_buf, error_buf_size) ||
        !ppfirst_order_frame_decoder_v1_head_arity(
            native_types, sequence->cons, 2u,
            error_buf, error_buf_size) ||
        !ppfirst_order_frame_decoder_v1_head_arity(
            native_types, sequence->nil, 0u,
            error_buf, error_buf_size) ||
        (finite_support &&
         (!ppfirst_order_frame_decoder_v1_head_arity(
              native_types, finite_support->support_apart, 2u,
              error_buf, error_buf_size) ||
          !ppfirst_order_frame_decoder_v1_head_arity(
              native_types, finite_support->token_against, 2u,
              error_buf, error_buf_size) ||
          !ppfirst_order_frame_decoder_v1_head_arity(
              native_types, finite_support->pair_allowed, 2u,
              error_buf, error_buf_size) ||
          !ppfirst_order_frame_decoder_v1_head_arity(
              native_types, finite_support->apart, 2u,
              error_buf, error_buf_size) ||
          !ppfirst_order_frame_decoder_v1_head_arity(
              native_types, finite_support->literal, 1u,
              error_buf, error_buf_size) ||
          !ppfirst_order_frame_decoder_v1_head_arity(
              native_types, finite_support->variable, 1u,
              error_buf, error_buf_size))) ||
        !ppfirst_order_frame_decoder_v1_indexed_program(
            storage_plan,
            &state_plan->proof_machines[proof_machine_id],
            indexed_value != NULL, error_buf, error_buf_size) ||
        !ppfirst_order_frame_decoder_v1_calls(
            storage_plan, machine, error_buf, error_buf_size) ||
        !ppfirst_order_frame_decoder_v1_table_dependencies(
            storage_plan, state_plan, proof_machine_id,
            error_buf, error_buf_size))
        return false;
    decoder.machine = machine->machine;
    decoder.owner = machine->owner;
    decoder.base = machine->base;
    decoder.provable = machine->provable;
    decoder.sequence_cons = sequence->cons;
    decoder.sequence_nil = sequence->nil;
    decoder.finite_support = finite_support;
    decoder.indexed_value = indexed_value;
    decoder.literal_hole = literal_hole;
    decoder.two_phase_frame_admitted = true;
    decoder.storage_plan = storage_plan;
    memcpy(decoder.native_type_digest, native_types->semantic_digest,
           sizeof(decoder.native_type_digest));
    memcpy(decoder.storage_plan_digest, storage_plan->semantic_digest,
           sizeof(decoder.storage_plan_digest));
    *decoder_out = decoder;
    return true;
}
