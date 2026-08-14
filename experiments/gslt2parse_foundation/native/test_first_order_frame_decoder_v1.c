#include "first_order_frame_decoder_v1.h"
#include "atom.h"
#include "finite_horn_answer_stream_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t checks_run;
static uint32_t checks_failed;

static bool expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

static bool expect_native_type_rejected(const char *path) {
    PPOSLFNativeTypePlanV1 plan;
    char error[512] = {0};
    bool rejected;

    pposlf_native_type_plan_v1_init(&plan);
    rejected = !pposlf_native_type_plan_v1_load(
        &plan, path, error, sizeof(error));
    pposlf_native_type_plan_v1_free(&plan);
    return expect(rejected && error[0] != '\0',
                  "malformed native-type artifact was accepted");
}

static bool expect_storage_rejected(const char *path) {
    PPProofStoragePlanV1 plan;
    char error[512] = {0};
    bool rejected;

    ppproof_storage_plan_v1_init(&plan);
    rejected = !ppproof_storage_plan_v1_load(
        &plan, path, error, sizeof(error));
    ppproof_storage_plan_v1_free(&plan);
    return expect(rejected && error[0] != '\0',
                  "malformed proof-storage artifact was accepted");
}

static bool mutated_admission_canaries(
    const PPProofStoragePlanV1 *storage,
    const PPOSLFNativeTypePlanV1 *native_types,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPFirstOrderFrameDecoderV1 *accepted);

static bool generated_artifact_guest_canary(
    const char *state_path,
    const char *native_type_path,
    const char *storage_path) {
    static char *unused_operations[] = {
        "generated-guest-unused-operation-v1",
    };
    FHAnswerStreamV1 state_answers;
    PPOccurrenceFoldV1Plan occurrence_plan = {0};
    PPRelationalStateProgramV1Plan state_plan;
    PPOSLFNativeTypePlanV1 native_types;
    PPProofStoragePlanV1 storage;
    PPFirstOrderFrameDecoderV1 decoder;
    PPRelationalStackProofV1CacheAdmission cache_admission;
    char error[512] = {0};
    bool ok = false;

    fh_answer_stream_v1_init(&state_answers);
    pprelational_state_program_v1_plan_init(&state_plan);
    pposlf_native_type_plan_v1_init(&native_types);
    ppproof_storage_plan_v1_init(&storage);
    occurrence_plan.operations = unused_operations;
    occurrence_plan.operation_len =
        sizeof(unused_operations) / sizeof(unused_operations[0]);
    memset(occurrence_plan.plan_digest, 'a', 64u);
    occurrence_plan.plan_digest[64] = '\0';

    if (!expect(fh_answer_stream_v1_read(
                    &state_answers, state_path, error, sizeof(error)),
                error[0] ? error
                         : "generated guest state artifact was not readable") ||
        !expect(pprelational_state_program_v1_plan_build(
                    &occurrence_plan,
                    state_answers.terms,
                    state_answers.len,
                    state_answers.digest,
                    &state_plan,
                    error,
                    sizeof(error)),
                error[0] ? error
                         : "generated guest state artifact was not admitted") ||
        !expect(pposlf_native_type_plan_v1_load(
                    &native_types, native_type_path, error, sizeof(error)),
                error[0] ? error
                         : "generated guest native types were not readable") ||
        !expect(ppproof_storage_plan_v1_load(
                    &storage, storage_path, error, sizeof(error)),
                error[0] ? error
                         : "generated guest storage plan was not readable"))
        goto done;
    if (!expect(state_plan.table_len == 9u &&
                    state_plan.proof_machine_len == 1u,
                "generated guest state graph has the wrong shape") ||
        !expect(native_types.head_signature_len == 3u,
                "generated guest native types have the wrong shape") ||
        !expect(storage.table_len == 9u && storage.machine_len == 1u &&
                    storage.read_len == 9u && storage.sequence_len == 1u &&
                    storage.call_len == 1u &&
                    storage.workspace_len == 1u &&
                    storage.repetition_cache_len == 1u &&
                    storage.two_phase_frame_len == 1u,
                "generated guest storage graph has the wrong shape") ||
        !expect(ppproof_storage_call_v1_admits_reusable_workspace(
                    ppproof_storage_plan_v1_call(
                        &storage, "guest-check-v1", 0u),
                    "guest-check-v1", 0u, "guest-machine-v1",
                    "proof-call-region-v1"),
                "generated guest call did not admit reusable storage") ||
        !expect(ppproof_workspace_plan_v1_admits(
                    ppproof_storage_plan_v1_workspace(
                        &storage, "guest-check-v1", 0u),
                    "guest-check-v1", 0u, "guest-machine-v1",
                    "stack-proof-call-workspace-v1",
                    "proof-call-region-v1"),
                "generated guest call did not select its workspace class") ||
        !expect(ppproof_repetition_cache_plan_v1_admits(
                    ppproof_storage_plan_v1_repetition_cache(
                        &storage, "guest-check-v1", 0u),
                    "guest-check-v1", 0u, "guest-machine-v1",
                    "proof-call-region-v1"),
                "generated guest did not derive the generic repetition policy") ||
        !expect(!ppproof_workspace_plan_v1_admits(
                    ppproof_storage_plan_v1_workspace(
                        &storage, "guest-check-v1", 0u),
                    "guest-check-v1", 0u, "guest-machine-v1",
                    "indexed-stack-proof-call-workspace-v1",
                    "proof-call-region-v1"),
                "generated guest workspace admitted the wrong machine class") ||
        !expect(ppfirst_order_frame_decoder_v1_admit(
                    &storage,
                    &native_types,
                    &state_plan,
                    0u,
                    &decoder,
                    error,
                    sizeof(error)),
                error[0] ? error
                         : "generated guest frame was not admitted") ||
        !expect(strcmp(decoder.machine, "guest-machine-v1") == 0 &&
                    strcmp(decoder.owner, "GuestProofV1") == 0 &&
                    strcmp(decoder.provable, "GuestProvableV1") == 0,
                "generated guest identities did not drive admission") ||
        !expect(ppfirst_order_frame_decoder_v1_cache_admission(
                    &decoder,
                    &state_plan,
                    0u,
                    &cache_admission,
                    error,
                    sizeof(error)) &&
                    cache_admission.scratch_reuse_admitted &&
                    !cache_admission.indexed_values_admitted &&
                    cache_admission.literal_hole_admitted &&
                    cache_admission.two_phase_frame_admitted,
                error[0] ? error
                         : "generated guest did not yield cache admission") ||
        !mutated_admission_canaries(
            &storage, &native_types, &state_plan, 0u, &decoder))
        goto done;
    {
        PPProofStorageCallV1 changed = storage.calls[0];
        changed.observation = "returns-workspace-contents-v1";
        if (!expect(!ppproof_storage_call_v1_admits_reusable_workspace(
                        &changed, changed.operation,
                        changed.action_index, changed.machine,
                        changed.region),
                    "observable call-local storage admitted reuse"))
            goto done;
    }
    ok = true;

done:
    ppproof_storage_plan_v1_free(&storage);
    pposlf_native_type_plan_v1_free(&native_types);
    pprelational_state_program_v1_plan_free(&state_plan);
    fh_answer_stream_v1_free(&state_answers);
    return ok;
}

static bool structurally_renamed_guest_canary(void) {
    static PPProofStorageTableV1 tables[] = {
        {"g-a", 2u, 2u,
         PPPROOF_STORAGE_LIFETIME_V1_SCOPED, "state-scope-region-v1"},
        {"g-b", 4u, 1u,
         PPPROOF_STORAGE_LIFETIME_V1_SCOPED, "state-scope-region-v1"},
        {"g-c", 3u, 3u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
        {"g-d", 2u, 1u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
        {"g-e", 2u, 1u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
        {"g-f", 2u, 1u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
        {"g-g", 2u, 2u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
        {"g-h", 3u, 2u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
        {"g-i", 2u, 1u,
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT, "state-run-region-v1"},
    };
    static PPProofStorageMachineV1 machines[] = {
        {"guest-machine", "GuestProof", "GuestBase", "GuestProvable"},
    };
    static PPProofStorageReadV1 reads[] = {
        {"guest-machine", "guest-role-a", "g-a",
         PPPROOF_STORAGE_LIFETIME_V1_SCOPED},
        {"guest-machine", "guest-role-b", "g-b",
         PPPROOF_STORAGE_LIFETIME_V1_SCOPED},
        {"guest-machine", "guest-role-c", "g-c",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
        {"guest-machine", "guest-role-d", "g-d",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
        {"guest-machine", "guest-role-e", "g-e",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
        {"guest-machine", "guest-role-f", "g-f",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
        {"guest-machine", "guest-role-g", "g-g",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
        {"guest-machine", "guest-role-h", "g-h",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
        {"guest-machine", "guest-role-i", "g-i",
         PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT},
    };
    static PPProofStorageSequenceV1 sequences[] = {
        {"GuestProof", "GuestBase", "GuestProvable",
         "GuestCons", "GuestNil", "flat-symbol-id-vector-v1",
         "proof-call-region-v1"},
    };
    static PPProofStorageCallV1 calls[] = {
        {"guest-check", 7u, "guest-machine", "GuestProof",
         "GuestProvable", "proof-call-region-v1",
         "flat-symbol-id-vector-v1", "proof-verdict-only-v1"},
    };
    static PPProofWorkspacePlanV1 workspaces[] = {
        {"guest-check", 7u, "guest-machine",
         "stack-proof-call-workspace-v1", "proof-call-region-v1",
         "proof-verdict-only-v1"},
    };
    static PPProofLiteralHolePlanV1 literal_holes[] = {
        {"guest-machine", "GuestProof", "GuestCons", "GuestNil",
         "literal-hole-run-program-v1", "state-run-region-v1",
         "proof-call-region-v1"},
    };
    static PPProofTwoPhaseFramePlanV1 two_phase_frames[] = {
        {"guest-check", 7u, "guest-machine",
         "two-phase-frame-machine-v1", "literal-hole-run-program-v1",
         "literal-head-optional-v1", "epoch-stamped-dense-slots-v1",
         "unique-dense-binders-v1", "exact-stack-suffix-v1",
         "proof-call-region-v1"},
    };
    static PPOSLFNativeHeadSignatureV1 signatures[] = {
        {"GuestCons", 2u},
        {"GuestNil", 0u},
        {"GuestProvable", 1u},
    };
    PPProofStoragePlanV1 storage = {
        .tables = tables,
        .table_len = sizeof(tables) / sizeof(tables[0]),
        .machines = machines,
        .machine_len = sizeof(machines) / sizeof(machines[0]),
        .reads = reads,
        .read_len = sizeof(reads) / sizeof(reads[0]),
        .sequences = sequences,
        .sequence_len = sizeof(sequences) / sizeof(sequences[0]),
        .calls = calls,
        .call_len = sizeof(calls) / sizeof(calls[0]),
        .workspaces = workspaces,
        .workspace_len = sizeof(workspaces) / sizeof(workspaces[0]),
        .literal_holes = literal_holes,
        .literal_hole_len =
            sizeof(literal_holes) / sizeof(literal_holes[0]),
        .two_phase_frames = two_phase_frames,
        .two_phase_frame_len =
            sizeof(two_phase_frames) / sizeof(two_phase_frames[0]),
    };
    PPOSLFNativeTypePlanV1 native_types = {
        .head_signatures = signatures,
        .head_signature_len = sizeof(signatures) / sizeof(signatures[0]),
    };
    PPFirstOrderFrameDecoderV1 decoder;
    PPRelationalStateTableV1 state_tables[] = {
        {"g-a", 2u, 2u,
         PPRELATIONAL_STATE_LIFETIME_V1_SCOPED},
        {"g-b", 4u, 1u,
         PPRELATIONAL_STATE_LIFETIME_V1_SCOPED},
        {"g-c", 3u, 3u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
        {"g-d", 2u, 1u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
        {"g-e", 2u, 1u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
        {"g-f", 2u, 1u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
        {"g-g", 2u, 2u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
        {"g-h", 3u, 2u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
        {"g-i", 2u, 1u,
         PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT},
    };
    PPRelationalStateProofMachineV1 state_machine = {
        .name = "guest-machine",
        .label_kind_table = 5u,
        .formula_table = 4u,
        .binder_variable_table = 3u,
        .mandatory_variable_table = 6u,
        .assertion_hypothesis_table = 7u,
        .assertion_disjoint_table = 2u,
        .active_hypothesis_table = 1u,
        .active_disjoint_table = 0u,
        .symbol_kind_table = 8u,
        .header_hypothesis_policy =
            CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1,
    };
    PPRelationalStateProgramV1Plan state_plan = {
        .tables = state_tables,
        .proof_machines = &state_machine,
        .table_len = sizeof(state_tables) / sizeof(state_tables[0]),
        .proof_machine_len = 1u,
    };
    PPRelationalStackProofV1CacheAdmission cache_admission;
    char error[512] = {0};
    bool admitted;

    memset(storage.semantic_digest, '1', 64u);
    storage.semantic_digest[64] = '\0';
    memset(native_types.semantic_digest, '2', 64u);
    native_types.semantic_digest[64] = '\0';
    admitted = ppfirst_order_frame_decoder_v1_admit(
        &storage, &native_types, &state_plan, 0u,
        &decoder, error, sizeof(error));
    if (!expect(admitted,
                error[0] ? error
                         : "structurally renamed guest frame was rejected"))
        return false;
    if (!expect(strcmp(decoder.provable, "GuestProvable") == 0,
                "renamed guest frame selected the wrong judgment") ||
        !expect(ppfirst_order_frame_decoder_v1_validate_state_program(
                    &decoder, &state_plan, 0u, error, sizeof(error)),
                error[0] ? error
                         : "renamed state program disagreed with its decoder") ||
        !expect(ppfirst_order_frame_decoder_v1_cache_admission(
                    &decoder, &state_plan, 0u, &cache_admission,
                    error, sizeof(error)) &&
                    cache_admission.formula_table ==
                        state_machine.formula_table &&
                    cache_admission.scratch_reuse_admitted &&
                    cache_admission.literal_hole_admitted &&
                    cache_admission.two_phase_frame_admitted &&
                    !cache_admission.indexed_values_admitted &&
                    strcmp(cache_admission.native_type_digest,
                           decoder.native_type_digest) == 0 &&
                    strcmp(cache_admission.storage_plan_digest,
                           decoder.storage_plan_digest) == 0,
                error[0] ? error
                         : "decoder did not produce an exact cache admission"))
        return false;
    if (!mutated_admission_canaries(
            &storage, &native_types, &state_plan, 0u, &decoder))
        return false;
    state_tables[4].arity++;
    return expect(!ppfirst_order_frame_decoder_v1_validate_state_program(
                      &decoder, &state_plan, 0u, error, sizeof(error)),
                  "state-program table mismatch was accepted");
}

static bool mutated_admission_canaries(
    const PPProofStoragePlanV1 *storage,
    const PPOSLFNativeTypePlanV1 *native_types,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPFirstOrderFrameDecoderV1 *accepted) {
    PPOSLFNativeTypePlanV1 bad_native = *native_types;
    PPProofStoragePlanV1 bad_storage = *storage;
    PPOSLFNativeHeadSignatureV1 *signatures = NULL;
    PPProofStorageTableV1 *tables = NULL;
    PPProofStorageReadV1 *reads = NULL;
    PPProofStorageSequenceV1 *sequences = NULL;
    PPProofStorageCallV1 *calls = NULL;
    PPFirstOrderFrameDecoderV1 ignored;
    char error[512] = {0};
    uint32_t index;
    bool ok = false;

    signatures = malloc(sizeof(*signatures) * native_types->head_signature_len);
    tables = malloc(sizeof(*tables) * storage->table_len);
    reads = malloc(sizeof(*reads) * storage->read_len);
    sequences = malloc(sizeof(*sequences) * storage->sequence_len);
    calls = malloc(sizeof(*calls) * storage->call_len);
    if (!signatures || !tables || !reads || !sequences || !calls) {
        expect(false, "cannot allocate mutation canaries");
        goto done;
    }
    memcpy(signatures, native_types->head_signatures,
           sizeof(*signatures) * native_types->head_signature_len);
    bad_native.head_signatures = signatures;
    for (index = 0u; index < bad_native.head_signature_len; index++) {
        if (strcmp(signatures[index].constructor, accepted->provable) == 0)
            signatures[index].arity++;
    }
    if (!expect(!ppfirst_order_frame_decoder_v1_admit(
                    storage, &bad_native, state_plan, proof_machine_id,
                    &ignored, error, sizeof(error)),
                "wrong judgment arity was accepted"))
        goto done;

    memcpy(tables, storage->tables, sizeof(*tables) * storage->table_len);
    bad_storage.tables = tables;
    for (index = 0u; index < bad_storage.table_len; index++) {
        if (strcmp(tables[index].table,
                   state_plan->tables[
                       state_plan->proof_machines[proof_machine_id]
                           .formula_table].name) == 0)
            tables[index].arity++;
    }
    if (!expect(!ppfirst_order_frame_decoder_v1_admit(
                    &bad_storage, native_types,
                    state_plan, proof_machine_id,
                    &ignored, error, sizeof(error)),
                "wrong table arity was accepted"))
        goto done;

    bad_storage = *storage;
    memcpy(reads, storage->reads, sizeof(*reads) * storage->read_len);
    bad_storage.reads = reads;
    for (index = 0u; index < bad_storage.read_len; index++) {
        if (strcmp(reads[index].machine, accepted->machine) == 0 &&
            strcmp(reads[index].table,
                   state_plan->tables[
                       state_plan->proof_machines[proof_machine_id]
                           .active_disjoint_table].name) == 0)
            reads[index].lifetime =
                PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT;
    }
    if (!expect(!ppfirst_order_frame_decoder_v1_admit(
                    &bad_storage, native_types,
                    state_plan, proof_machine_id,
                    &ignored, error, sizeof(error)),
                "wrong table lifetime was accepted"))
        goto done;

    bad_storage = *storage;
    memcpy(sequences, storage->sequences,
           sizeof(*sequences) * storage->sequence_len);
    bad_storage.sequences = sequences;
    for (index = 0u; index < bad_storage.sequence_len; index++) {
        if (strcmp(sequences[index].owner, accepted->owner) == 0 &&
            strcmp(sequences[index].provable, accepted->provable) == 0)
            sequences[index].layout = "unsupported-layout-v1";
    }
    if (!expect(!ppfirst_order_frame_decoder_v1_admit(
                    &bad_storage, native_types,
                    state_plan, proof_machine_id,
                    &ignored, error, sizeof(error)),
                "unsupported sequence layout was accepted"))
        goto done;

    bad_storage = *storage;
    memcpy(calls, storage->calls, sizeof(*calls) * storage->call_len);
    bad_storage.calls = calls;
    for (index = 0u; index < bad_storage.call_len; index++) {
        if (strcmp(calls[index].machine, accepted->machine) == 0)
            calls[index].observation = "retain-unbounded-evidence-v1";
    }
    if (!expect(!ppfirst_order_frame_decoder_v1_admit(
                    &bad_storage, native_types,
                    state_plan, proof_machine_id,
                    &ignored, error, sizeof(error)),
                "unsupported proof observation was accepted"))
        goto done;
    ok = true;

done:
    free(calls);
    free(sequences);
    free(reads);
    free(tables);
    free(signatures);
    return ok;
}

static bool exact_action_selector_canaries(void) {
    static uint8_t binder_kind[] = "kind-binder";
    static uint8_t matching_kind[] = "kind-matching";
    static uint8_t rule_first_kind[] = "kind-rule-first";
    static uint8_t rule_second_kind[] = "kind-rule-second";
    PPRelationalStateTableV1 state_table = {
        .name = "declaration-kind-table",
        .arity = 2u,
        .key_arity = 1u,
        .lifetime = PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT,
    };
    PPRelationalStateProofMachineV1 state_machine = {
        .name = "proof-machine",
        .label_kind_table = 0u,
        .binder_hypothesis_kind = {
            binder_kind, sizeof(binder_kind) - 1u },
        .matching_hypothesis_kind = {
            matching_kind, sizeof(matching_kind) - 1u },
        .rule_kind_first = {
            rule_first_kind, sizeof(rule_first_kind) - 1u },
        .rule_kind_second = {
            rule_second_kind, sizeof(rule_second_kind) - 1u },
    };
    PPRelationalStateProgramV1Plan state_plan = {
        .tables = &state_table,
        .proof_machines = &state_machine,
        .table_len = 1u,
        .proof_machine_len = 1u,
    };
    PPProofStorageTableV1 storage_table = {
        .table = "declaration-kind-table",
        .arity = 2u,
        .key_arity = 1u,
        .lifetime = PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT,
        .region = "state-run-region-v1",
    };
    PPProofStorageMachineV1 storage_machine = {
        .machine = "proof-machine",
    };
    PPProofStorageReadV1 read = {
        .machine = "proof-machine",
        .role = "label-kind-v1",
        .table = "declaration-kind-table",
        .lifetime = PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT,
    };
    PPProofPreparedActionCaseV1 cases[4] = {
        { "proof-machine", "kind-binder",
          PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED },
        { "proof-machine", "kind-matching",
          PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED },
        { "proof-machine", "kind-rule-first",
          PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME },
        { "proof-machine", "kind-rule-second",
          PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME },
    };
    PPProofStoragePlanV1 storage_plan = {
        .tables = &storage_table,
        .table_len = 1u,
        .machines = &storage_machine,
        .machine_len = 1u,
        .reads = &read,
        .read_len = 1u,
        .prepared_action_cases = cases,
        .prepared_action_case_len = 4u,
    };
    const PPProofPreparedActionCaseV1 *admitted_cases = NULL;
    uint32_t admitted_len = 0u;
    char error[256] = {0};
    bool ok = true;

    ok = expect(ppproof_storage_plan_v1_exact_action_selector(
                    &storage_plan, &state_plan, 0u,
                    &admitted_cases, &admitted_len,
                    error, sizeof(error)) &&
                    admitted_cases == cases && admitted_len == 4u,
                error[0] ? error
                         : "exact action selector rejected valid derived facts") &&
         ok;
    cases[3].action = PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED;
    ok = expect(!ppproof_storage_plan_v1_exact_action_selector(
                    &storage_plan, &state_plan, 0u,
                    &admitted_cases, &admitted_len,
                    error, sizeof(error)),
                "exact action selector admitted a rule as a push") && ok;
    cases[3].action = PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME;
    state_table.lifetime = PPRELATIONAL_STATE_LIFETIME_V1_SCOPED;
    ok = expect(!ppproof_storage_plan_v1_exact_action_selector(
                    &storage_plan, &state_plan, 0u,
                    &admitted_cases, &admitted_len,
                    error, sizeof(error)),
                "exact action selector admitted a mutable label table") && ok;
    state_table.lifetime = PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT;
    state_machine.rule_kind_second = state_machine.rule_kind_first;
    ok = expect(!ppproof_storage_plan_v1_exact_action_selector(
                    &storage_plan, &state_plan, 0u,
                    &admitted_cases, &admitted_len,
                    error, sizeof(error)),
                "exact action selector admitted aliased source kinds") && ok;
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    PPOSLFNativeTypePlanV1 native_types;
    PPProofStoragePlanV1 storage;
    const PPProofIndexedValuePlanV1 *indexed_value;
    const PPProofIndexedProgramPlanV1 *indexed_program;
    const PPProofFrameIndexPlanV1 *frame_index;
    const PPProofRepetitionCachePlanV1 *normal_cache;
    const PPProofRepetitionCachePlanV1 *compressed_cache;
    PPProofRepetitionCachePlanV1 invalid_cache = {0};
    char error[512] = {0};
    bool ok = true;

    if (argc != 10) {
        fprintf(stderr,
                "usage: %s NTT STORAGE BAD-UNKNOWN-NTT BAD-ARITY-NTT "
                "BAD-DUPLICATE-NTT BAD-STORAGE GUEST-STATE GUEST-NTT "
                "GUEST-STORAGE\n",
                argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    pposlf_native_type_plan_v1_init(&native_types);
    ppproof_storage_plan_v1_init(&storage);
    ok = exact_action_selector_canaries() && ok;
    if (!pposlf_native_type_plan_v1_load(
            &native_types, argv[1], error, sizeof(error))) {
        fprintf(stderr, "FAIL: native-type load: %s\n", error);
        ok = false;
        goto done;
    }
    if (!ppproof_storage_plan_v1_load(
            &storage, argv[2], error, sizeof(error))) {
        fprintf(stderr, "FAIL: proof-storage load: %s\n", error);
        ok = false;
        goto done;
    }
    indexed_value = ppproof_storage_plan_v1_indexed_value(
        &storage, "mm-stack-proof-machine-v1");
    indexed_program = ppproof_storage_plan_v1_indexed_program(
        &storage, "mm-stack-proof-machine-v1");
    frame_index = ppproof_storage_plan_v1_frame_index(
        &storage, "mm-stack-proof-machine-v1");
    normal_cache = ppproof_storage_plan_v1_repetition_cache(
        &storage, "mm-check-theorem-normal", 14u);
    compressed_cache = ppproof_storage_plan_v1_repetition_cache(
        &storage, "mm-check-theorem-compressed", 14u);
    if (normal_cache) {
        invalid_cache = *normal_cache;
        invalid_cache.admission_policy = "retain-first-occurrence-v1";
    }
    ok = expect(storage.table_len == 19u,
                "generated storage table count changed") &&
         expect(storage.read_len == 9u,
                "generated machine-read count changed") &&
         expect(storage.finite_support_len == 1u &&
                    ppproof_storage_plan_v1_finite_support(
                        &storage, "MetamathProofV1") != NULL,
                "generated finite-support admission is missing") &&
         expect(storage.repetition_cache_len == 2u &&
                    normal_cache != NULL && compressed_cache != NULL,
                "generated repetition-cache licenses are missing") &&
         expect(ppproof_repetition_cache_plan_v1_admits(
                    normal_cache, "mm-check-theorem-normal", 14u,
                    "mm-stack-proof-machine-v1", "proof-call-region-v1") &&
                    ppproof_repetition_cache_plan_v1_admits(
                        compressed_cache, "mm-check-theorem-compressed", 14u,
                        "mm-stack-proof-machine-v1", "proof-call-region-v1"),
                "generated repetition-cache licenses match their call sites") &&
         expect(!ppproof_repetition_cache_plan_v1_admits(
                    normal_cache, "mm-check-theorem-normal", 13u,
                    "mm-stack-proof-machine-v1", "proof-call-region-v1"),
                "repetition-cache license rejects another action occurrence") &&
         expect(!ppproof_repetition_cache_plan_v1_admits(
                    &invalid_cache, "mm-check-theorem-normal", 14u,
                    "mm-stack-proof-machine-v1", "proof-call-region-v1"),
                "repetition-cache license rejects an unrecognized policy") &&
         expect(storage.indexed_value_len == 1u &&
                    indexed_value != NULL,
                "generated indexed-value admission is missing") &&
         expect(storage.indexed_program_len == 1u &&
                    indexed_program != NULL,
                "generated indexed-program admission is missing") &&
         expect(ppproof_indexed_program_plan_v1_admits(
                    indexed_program, "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", "mm-proof-step",
                    "mm-compressed-word", indexed_value->region,
                    PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                    indexed_program->save_placement,
                    indexed_program->header_hypothesis_policy),
                "indexed-program admission matches its generated call site") &&
         expect(!ppproof_indexed_program_plan_v1_admits(
                    indexed_program, "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", "different-header-v1",
                    "mm-compressed-word", indexed_value->region,
                    PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE,
                    indexed_program->save_placement,
                    indexed_program->header_hypothesis_policy),
                "indexed-program admission rejects a different header role") &&
         expect(ppproof_indexed_value_plan_v1_admits(
                    indexed_value, "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", "mm-proof-step",
                    "mm-compressed-word"),
                "indexed-value admission matches its generated call site") &&
         expect(!ppproof_indexed_value_plan_v1_admits(
                    indexed_value, "mm-check-theorem-compressed", 13u,
                    "mm-stack-proof-machine-v1", "mm-proof-step",
                    "mm-compressed-word"),
                "indexed-value admission rejects a different action occurrence") &&
         expect(!ppproof_indexed_value_plan_v1_admits(
                    indexed_value, "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", "mm-proof-label",
                    "mm-compressed-word"),
                "indexed-value admission rejects a different header role") &&
         expect(storage.frame_index_len == 1u && frame_index != NULL,
                "generated frame-index admission is missing") &&
         expect(ppproof_frame_index_plan_v1_admits(
                    frame_index, "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", indexed_value->region),
                "frame-index admission matches its generated call site") &&
         expect(!ppproof_frame_index_plan_v1_admits(
                    frame_index, "mm-check-theorem-compressed", 13u,
                    "mm-stack-proof-machine-v1", indexed_value->region),
                "frame-index admission rejects a different action occurrence") &&
         expect(!ppproof_frame_index_plan_v1_admits(
                    frame_index, "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", "different-region-v1"),
                "frame-index admission rejects a different call region") &&
         expect(!ppproof_frame_index_plan_v1_admits(
                    &(PPProofFrameIndexPlanV1){
                        .operation = frame_index->operation,
                        .action_index = frame_index->action_index,
                        .machine = frame_index->machine,
                        .carrier = "unsupported-frame-index-v1",
                        .validation = frame_index->validation,
                        .region = frame_index->region,
                    },
                    "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", indexed_value->region),
                "frame-index admission rejects an unsupported carrier") &&
         expect(!ppproof_frame_index_plan_v1_admits(
                    &(PPProofFrameIndexPlanV1){
                        .operation = frame_index->operation,
                        .action_index = frame_index->action_index,
                        .machine = frame_index->machine,
                        .carrier = frame_index->carrier,
                        .validation = "duplicate-choose-first-v1",
                        .region = frame_index->region,
                    },
                    "mm-check-theorem-compressed", 14u,
                    "mm-stack-proof-machine-v1", indexed_value->region),
                "frame-index admission rejects a non-unique index policy") &&
         expect(storage.literal_hole_len == 1u &&
                    ppproof_storage_plan_v1_literal_hole(
                        &storage, "mm-stack-proof-machine-v1") != NULL,
                "generated literal/hole admission is missing") &&
         expect(native_types.head_signature_len == 26u,
                "generated native head-signature count changed") &&
         structurally_renamed_guest_canary() &&
         generated_artifact_guest_canary(argv[7], argv[8], argv[9]) &&
         expect_native_type_rejected(argv[3]) &&
         expect_native_type_rejected(argv[4]) &&
         expect_native_type_rejected(argv[5]) &&
         expect_storage_rejected(argv[6]);

done:
    ppproof_storage_plan_v1_free(&storage);
    pposlf_native_type_plan_v1_free(&native_types);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(FirstOrderFrameDecoderV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    return ok ? 0 : 1;
}
