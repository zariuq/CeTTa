#include "proof_gslt_relational_runtime_v1.h"

#include "langdef/metamath/generated/proof_machine_language_v1.generated.h"
#include "langdef/metamath/generated/proof_machine_provider_catalog_v1.generated.h"

#include "parser_occurrence_fold_v1.h"
#include "parser_occurrence_span_mask_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"

#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool metamath_syntax_cursor_fold_v1_program_init(
    PPGuardedLexCursorV1Program *out,
    char *error_buf, size_t error_buf_size);
extern bool metamath_syntax_cursor_fold_v1_occurrence_fold_plan_init(
    const PPGuardedLexCursorV1Program *program,
    PPOccurrenceFoldV1Plan *out,
    char *error_buf, size_t error_buf_size);
extern bool metamath_syntax_cursor_fold_v1_occurrence_span_mask_plan_init(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    PPOccurrenceSpanMaskV1Plan *out,
    char *error_buf, size_t error_buf_size);
extern bool metamath_syntax_cursor_fold_v1_state_program_plan_init(
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPRelationalStateProgramV1Plan *out,
    char *error_buf, size_t error_buf_size);

static uint32_t checks_run;
static uint32_t checks_failed;
static bool constructor_sharing_observed;

static bool expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

static bool read_file(
    const char *path, uint8_t **bytes_out, size_t *len_out) {
    FILE *input = fopen(path, "rb");
    long size;
    uint8_t *bytes;

    *bytes_out = NULL;
    *len_out = 0u;
    if (!input || fseek(input, 0, SEEK_END) != 0 ||
        (size = ftell(input)) < 0 || fseek(input, 0, SEEK_SET) != 0) {
        if (input)
            fclose(input);
        return false;
    }
    bytes = malloc(size > 0 ? (size_t)size : 1u);
    if (!bytes ||
        (size > 0 && fread(bytes, 1u, (size_t)size, input) != (size_t)size)) {
        free(bytes);
        fclose(input);
        return false;
    }
    if (fclose(input) != 0) {
        free(bytes);
        return false;
    }
    *bytes_out = bytes;
    *len_out = (size_t)size;
    return true;
}

static bool run_database(
    const char *path,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    const PPOccurrenceSpanMaskV1Plan *span_mask,
    const PPRelationalStateProgramV1Plan *state,
    PPProofGSLTRelationalRuntimeV1 *proof_runtime,
    PPRelationalStateProofV1Result expected_result,
    uint32_t expected_result_count) {
    uint8_t *bytes = NULL;
    size_t byte_len = 0u;
    PPRelationalStateProgramV1Run state_run;
    PPRelationalStateProofV1Backend proof_backend;
    PPOccurrenceFoldV1Backend fold_backend;
    PPOccurrenceFoldV1Receipt fold_receipt;
    PPProofGSLTRelationalRuntimeV1Receipt proof_receipt;
    PPProofGSLTRelationalRuntimeV1Profile profile_before;
    PPProofGSLTRelationalRuntimeV1Profile profile_after;
    char error[512] = {0};
    bool executed;
    bool initialized;
    bool receipt_available;
    bool ok = false;

    memset(&state_run, 0, sizeof(state_run));
    memset(&fold_receipt, 0, sizeof(fold_receipt));
    memset(&proof_receipt, 0, sizeof(proof_receipt));
    memset(&profile_before, 0, sizeof(profile_before));
    memset(&profile_after, 0, sizeof(profile_after));
    if (!expect(read_file(path, &bytes, &byte_len),
                "raw proof fixture could not be read"))
        goto done;
    proof_backend = ppproof_gslt_relational_runtime_v1_backend(proof_runtime);
    if (!expect(proof_backend.execute != NULL,
                "prepared generated proof provider has no backend"))
        goto done;
    if (!expect(ppproof_gslt_relational_runtime_v1_profile(
                    proof_runtime, &profile_before),
                "prepared generated proof provider has no profile"))
        goto done;
    initialized = pprelational_state_program_v1_run_init_with_proof_backend(
        &state_run, fold, state, NULL, NULL, &proof_backend,
        PPRELATIONAL_STATE_OBSERVATION_V1_EXACT_RECEIPT,
        error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated state run did not accept the proof provider"))
        goto done;
    fold_backend = pprelational_state_program_v1_backend(&state_run);
    executed = ppoccurrence_fold_v1_run_bytes_with_span_mask(
        program, fold, span_mask, bytes, byte_len, &fold_backend,
        PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
        UINT64_C(100000000), &fold_receipt, error, sizeof(error));
    receipt_available = ppproof_gslt_relational_runtime_v1_last_receipt(
        proof_runtime, &proof_receipt);
    if (!expect(ppproof_gslt_relational_runtime_v1_profile(
                    proof_runtime, &profile_after) &&
                    receipt_available &&
                    profile_after.query_executions >=
                        profile_before.query_executions &&
                    profile_after.query_executions -
                            profile_before.query_executions >=
                        proof_receipt.outcome_query_attempts,
                "generated query profile did not count the final request"))
        goto done;
    if (expected_result_count > 1u &&
        !expect(profile_after.query_executions -
                        profile_before.query_executions >
                    proof_receipt.outcome_query_attempts,
                "aggregate query profile collapsed multiple proof requests"))
        goto done;

    if (expected_result == PPRELATIONAL_STATE_PROOF_V1_VERIFIED ||
        expected_result == PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE) {
        if (!executed && receipt_available) {
            fprintf(stderr,
                    "generated proof receipt: outcome=%u result=%u "
                    "query-priority=%u query-attempts=%u attempts=%llu "
                    "depth=%u capabilities=%u\n",
                    (unsigned)proof_receipt.outcome,
                    (unsigned)proof_receipt.proof_result,
                    proof_receipt.outcome_query_priority,
                    proof_receipt.outcome_query_attempts,
                    (unsigned long long)proof_receipt.stats.rule_attempts,
                    proof_receipt.stats.maximum_goal_depth,
                    proof_receipt.capability_row_len);
        }
        ok = expect(executed,
                    error[0] ? error :
                        "generated raw proof pipeline did not execute") &&
             expect(fold_receipt.committed && state_run.receipt.committed,
                    "generated raw proof transaction did not commit") &&
             expect(state_run.receipt.verified_proof_len ==
                        (expected_result ==
                            PPRELATIONAL_STATE_PROOF_V1_VERIFIED
                                ? expected_result_count : 0u) &&
                        state_run.receipt.incomplete_proof_len ==
                        (expected_result ==
                            PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE
                                ? expected_result_count : 0u) &&
                        state_run.receipt.failure ==
                            PPRELATIONAL_STATE_FAILURE_V1_NONE,
                    "generated raw proof pipeline recorded the wrong outcome") &&
             expect(receipt_available,
                    "generated raw proof pipeline emitted no proof receipt") &&
             expect(proof_receipt.outcome == PPOSLF_NATIVE_VM_PROVED_V1,
                    "generated native type program did not prove its outcome") &&
             expect(proof_receipt.proof_result == expected_result,
                    "generated outcome query selected the wrong result class") &&
             expect(proof_receipt.outcome_query_priority ==
                        (expected_result ==
                            PPRELATIONAL_STATE_PROOF_V1_VERIFIED ? 0u : 1u) &&
                        proof_receipt.outcome_query_attempts ==
                        (expected_result ==
                            PPRELATIONAL_STATE_PROOF_V1_VERIFIED ? 1u : 2u),
                    "generated outcome queries did not run in priority order") &&
             expect(proof_receipt.generated_event_len > 0u &&
                        proof_receipt.external_event_len > 0u,
                    "proof receipt omitted generated or state evidence") &&
             expect(proof_receipt.compiled_audit_attempts ==
                        proof_receipt.outcome_query_attempts &&
                        proof_receipt.compiled_audit_agreements ==
                        proof_receipt.compiled_audit_attempts &&
                        proof_receipt.compiled_rule_attempts > 0u &&
                        proof_receipt.compiled_constructor_guided_attempts >
                            0u &&
                        proof_receipt.compiled_constructor_nodes_elided > 0u &&
                        proof_receipt
                            .compiled_variable_slot_clear_bytes_elided > 0u &&
                        proof_receipt.compiled_worklist_states_created > 0u &&
                        proof_receipt.compiled_worklist_states_reclaimed ==
                            proof_receipt.compiled_worklist_states_created,
                    "compiled proof audit did not agree on every outcome query") &&
             expect(proof_receipt.capability_digest[0] != '\0',
                    "escaping proof receipt omitted its forced commitment");
        if (proof_receipt.constructor_chain_nodes > 0u &&
            proof_receipt.constructor_chain_requests >
                proof_receipt.constructor_chain_nodes)
            constructor_sharing_observed = true;
        if (getenv("CETTA_TEST_OSLF_STATS_V1"))
            fprintf(stderr,
                    "oslf-proof-stats path=%s attempts=%llu "
                    "continuations=%llu raw-tail=%llu proven-tail=%llu "
                    "external-continuations=%llu external-tail=%llu "
                    "collections=%llu roots=%llu binding-collections=%llu "
                    "binding-roots=%llu binding-items-discarded=%llu "
                    "copied=%llu reclaimed=%llu materialize=%llu "
                    "match-bytes=%llu ground-body-reuse=%llu "
                    "positional=%llu/%llu/%llu "
                    "expand-bytes=%llu nodes=%llu "
                    "rollback-reclaimed=%llu max-frames=%u "
                    "max-goal-depth=%u compiled-attempts=%llu "
                    "compiled-matches=%llu compiled-dispatch=%llu "
                    "compiled-outer-head=%llu "
                    "compiled-prefilter=%llu "
                    "compiled-flat=%llu compiled-ground-dense=%llu "
                    "compiled-constructor=%llu/%llu "
                    "compiled-constructor-nodes-elided=%llu "
                    "compiled-slot-clear-bytes-elided=%llu "
                    "compiled-states=%llu/%llu compiled-pending-peak=%llu "
                    "compiled-state-bytes-peak=%llu "
                    "compiled-depth=%u\n",
                    path,
                    (unsigned long long)proof_receipt.stats.rule_attempts,
                    (unsigned long long)
                        proof_receipt.stats.generated_continuations,
                    (unsigned long long)proof_receipt.stats
                        .generated_raw_tail_deterministic_continuations,
                    (unsigned long long)proof_receipt.stats
                        .generated_tail_deterministic_continuations,
                    (unsigned long long)
                        proof_receipt.stats.external_continuations,
                    (unsigned long long)proof_receipt.stats
                        .external_tail_deterministic_continuations,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_tail_collections,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_goal_roots_scanned,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_binding_collections,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_binding_roots_scanned,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_binding_items_discarded,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_arena_bytes_copied,
                    (unsigned long long)proof_receipt.stats
                        .deterministic_arena_bytes_reclaimed,
                    (unsigned long long)proof_receipt.stats
                        .goal_materialization_arena_bytes,
                    (unsigned long long)proof_receipt.stats
                        .generated_match_arena_bytes,
                    (unsigned long long)proof_receipt.stats
                        .ground_dense_ground_body_reuses,
                    (unsigned long long)proof_receipt.stats
                        .positional_linear_rule_matches,
                    (unsigned long long)proof_receipt.stats
                        .positional_linear_rule_attempts,
                    (unsigned long long)proof_receipt.stats
                        .positional_linear_rule_fallbacks,
                    (unsigned long long)proof_receipt.stats
                        .body_expansion_arena_bytes,
                    (unsigned long long)proof_receipt.stats
                        .pending_goal_node_arena_bytes,
                    (unsigned long long)proof_receipt.stats
                        .rollback_arena_bytes_reclaimed,
                    proof_receipt.stats.maximum_search_frame_depth,
                    proof_receipt.stats.maximum_goal_depth,
                    (unsigned long long)
                        proof_receipt.compiled_rule_attempts,
                    (unsigned long long)
                        proof_receipt.compiled_rule_matches,
                    (unsigned long long)
                        proof_receipt.compiled_dispatch_rejects,
                    (unsigned long long)
                        proof_receipt.compiled_outer_head_elisions,
                    (unsigned long long)
                        proof_receipt.compiled_prefilter_rejects,
                    (unsigned long long)
                        proof_receipt.compiled_flat_head_matches,
                    (unsigned long long)
                        proof_receipt.compiled_ground_dense_matches,
                    (unsigned long long)
                        proof_receipt.compiled_constructor_guided_attempts,
                    (unsigned long long)
                        proof_receipt.compiled_constructor_guided_matches,
                    (unsigned long long)
                        proof_receipt.compiled_constructor_nodes_elided,
                    (unsigned long long)proof_receipt
                        .compiled_variable_slot_clear_bytes_elided,
                    (unsigned long long)
                        proof_receipt.compiled_worklist_states_created,
                    (unsigned long long)
                        proof_receipt.compiled_worklist_states_reclaimed,
                    (unsigned long long)
                        proof_receipt.compiled_worklist_pending_peak,
                    (unsigned long long)
                        proof_receipt.compiled_worklist_state_bytes_peak,
                    proof_receipt.compiled_maximum_goal_depth);
    } else {
        ok = expect(!executed && !state_run.receipt.committed,
                    "invalid raw proof transaction unexpectedly committed") &&
             expect(state_run.receipt.failure ==
                        PPRELATIONAL_STATE_FAILURE_V1_REJECTED,
                    error[0] ? error :
                        "invalid raw proof did not fail as a proof rejection") &&
             expect(receipt_available,
                    "negative raw proof emitted no proof receipt") &&
             expect(proof_receipt.outcome == PPOSLF_NATIVE_VM_NO_PROOF_V1,
                    "negative raw proof was not exhausted by generated rules") &&
             expect(proof_receipt.proof_result ==
                        PPRELATIONAL_STATE_PROOF_V1_REJECTED,
                    "negative raw proof selected a non-rejection outcome") &&
             expect(proof_receipt.compiled_audit_attempts ==
                        proof_receipt.outcome_query_attempts &&
                        proof_receipt.compiled_audit_agreements ==
                        proof_receipt.compiled_audit_attempts &&
                        proof_receipt.compiled_rule_attempts > 0u &&
                        proof_receipt.compiled_constructor_guided_attempts >
                            0u &&
                        proof_receipt.compiled_constructor_nodes_elided > 0u &&
                        proof_receipt
                            .compiled_variable_slot_clear_bytes_elided > 0u &&
                        proof_receipt.compiled_worklist_states_created > 0u &&
                        proof_receipt.compiled_worklist_states_reclaimed ==
                            proof_receipt.compiled_worklist_states_created,
                    "compiled proof audit disagreed on a negative outcome") &&
             expect(proof_receipt.capability_digest[0] != '\0',
                    "negative proof receipt omitted its forced commitment");
    }

done:
    pprelational_state_program_v1_run_free(&state_run);
    free(bytes);
    return ok;
}

int main(int argc, char **argv) {
    const PPOSLFNativeVMLimitsV1 limits = {
        .maximum_rule_attempts = UINT64_C(100000000),
        .maximum_goal_depth = UINT32_MAX,
    };
    SymbolTable symbols;
    PPGuardedLexCursorV1Program program;
    PPOccurrenceFoldV1Plan fold;
    PPOccurrenceSpanMaskV1Plan span_mask;
    PPRelationalStateProgramV1Plan state;
    PPOSLFNativeTypePlanV1 native_plan;
    PPOSLFNativeTypeVMV1 vm;
    PPProofGSLTRelationalRuntimeV1 proof_runtime;
    char error[512] = {0};
    bool initialized;
    bool ok = false;

    if (argc != 14) {
        fprintf(stderr,
                "usage: %s PROVIDER NTT POSITIVE-MM NEGATIVE-MM "
                "POSITIVE-COMPRESSED-MM NEGATIVE-COMPRESSED-MM "
                "INCOMPLETE-NORMAL-MM INCOMPLETE-COMPRESSED-MM "
                "PERSISTENT-EXTENSION-MM OPEN-SAVE-MM "
                "INCOMPLETE-COMPRESSED-TAIL-MM "
                "INCOMPLETE-NORMAL-TAIL-MM "
                "OPEN-INCOMPLETE-COMPRESSED-MM\n",
                argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ppguarded_lex_cursor_v1_program_init(&program);
    ppoccurrence_fold_v1_plan_init(&fold);
    ppoccurrence_span_mask_v1_plan_init(&span_mask);
    pprelational_state_program_v1_plan_init(&state);
    pposlf_native_type_plan_v1_init(&native_plan);
    pposlf_native_type_vm_v1_init(&vm);
    ppproof_gslt_relational_runtime_v1_init(&proof_runtime);

    initialized = metamath_syntax_cursor_fold_v1_program_init(
        &program, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated parser did not initialize"))
        goto done;
    initialized = metamath_syntax_cursor_fold_v1_occurrence_fold_plan_init(
        &program, &fold, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated fold did not initialize"))
        goto done;
    initialized =
        metamath_syntax_cursor_fold_v1_occurrence_span_mask_plan_init(
            &program, &fold, &span_mask, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated span mask did not initialize"))
        goto done;
    initialized = metamath_syntax_cursor_fold_v1_state_program_plan_init(
        &fold, &state, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated state plan did not initialize"))
        goto done;
    initialized = pposlf_native_type_plan_v1_load(
        &native_plan, argv[2], error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated native type plan did not load"))
        goto done;
    initialized = pposlf_native_type_vm_v1_prepare(
        &vm, &native_plan, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated native type VM did not prepare"))
        goto done;
    initialized = ppproof_gslt_relational_runtime_v1_prepare(
        &proof_runtime, argv[1], &state, &native_plan, &vm,
        limits, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated relational provider did not prepare"))
        goto done;
    const CettaGsltHornLimits compiled_audit_limits = {
        .max_rule_attempts = UINT64_C(100000000),
        .max_answers = UINT64_C(1024),
        .max_depth = UINT32_MAX,
    };
    initialized = ppproof_gslt_relational_runtime_v1_attach_compiled_audit(
        &proof_runtime, &cetta_metamath_proof_machine_v1,
        &cetta_metamath_proof_machine_provider_catalog_v1,
        compiled_audit_limits, error, sizeof(error));
    if (!expect(initialized, error[0] ? error :
                "generated compiled proof audit did not attach"))
        goto done;
    ok = run_database(argv[3], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_VERIFIED,
                      1u) &&
         run_database(argv[4], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_REJECTED,
                      0u) &&
         run_database(argv[5], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_VERIFIED,
                      1u) &&
         run_database(argv[6], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_REJECTED,
                      0u) &&
         run_database(argv[7], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE,
                      1u) &&
         run_database(argv[8], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE,
                      1u) &&
         run_database(argv[9], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_VERIFIED,
                      2u) &&
         run_database(argv[10], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_REJECTED,
                      0u) &&
         run_database(argv[11], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_REJECTED,
                      0u) &&
         run_database(argv[12], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_REJECTED,
                      0u) &&
         run_database(argv[13], &program, &fold, &span_mask, &state,
                      &proof_runtime, PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE,
                      1u) &&
         expect(constructor_sharing_observed,
                "generated constructor-chain sharing was not exercised");

done:
    ppproof_gslt_relational_runtime_v1_free(&proof_runtime);
    pposlf_native_type_vm_v1_free(&vm);
    pposlf_native_type_plan_v1_free(&native_plan);
    pprelational_state_program_v1_plan_free(&state);
    ppoccurrence_span_mask_v1_plan_free(&span_mask);
    ppoccurrence_fold_v1_plan_free(&fold);
    ppguarded_lex_cursor_v1_program_free(&program);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(ProofGSLTRelationalRuntimeV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    return ok ? 0 : 1;
}
