#include "finite_horn_answer_stream_v1.h"
#include "native_sha256.h"
#include "parser_pack_abi_stream_v1.h"
#include "regular_span_dfa_v1.h"
#include "regular_span_nfa_v1.h"
#include "semantic_mask_nfa_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static void sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    uint8_t bytes[4];
    uint32_t index;
    for (index = 0u; index < 4u; index++)
        bytes[index] = (uint8_t)(value >> (8u * index));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void sha_u64(CettaNativeSha256 *sha, uint64_t value) {
    uint8_t bytes[8];
    uint32_t index;
    for (index = 0u; index < 8u; index++)
        bytes[index] = (uint8_t)(value >> (8u * index));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void sha_text(CettaNativeSha256 *sha, const char *value) {
    uint32_t len = (uint32_t)strlen(value);
    sha_u32(sha, len);
    cetta_native_sha256_update(sha, (const uint8_t *)value, len);
}

static const char *build_outcome_name(RSDFAV1BuildOutcome outcome) {
    switch (outcome) {
    case RSDFA_V1_BUILD_COMPLETED:
        return "completed";
    case RSDFA_V1_BUILD_STATE_LIMIT:
        return "state-limit";
    case RSDFA_V1_BUILD_TRANSITION_LIMIT:
        return "transition-limit";
    case RSDFA_V1_BUILD_ANNOTATION_CONFLICT:
        return "annotation-conflict";
    }
    return "unknown";
}

static bool run_action_probe(
    const PPSemanticMaskNfaV1Plan *mask,
    const PPSemanticMaskDfaV1Program *program,
    const char *accepted_text,
    const char *rejected_text,
    uint32_t *accepted_scalar_len,
    uint32_t *copy_len,
    uint32_t *drop_len,
    uint32_t *wrapper_len,
    uint64_t *work_item_len,
    char *error,
    size_t error_size) {
    CettaLpNativeUtf8ScalarBuffer accepted;
    CettaLpNativeUtf8ScalarBuffer rejected;
    PPSemanticMaskDfaV1RunResult result;
    uint32_t *actions = NULL;
    uint32_t index;
    bool ok = false;

    cetta_lp_native_utf8_scalar_buffer_init(&accepted);
    cetta_lp_native_utf8_scalar_buffer_init(&rejected);
    *accepted_scalar_len = 0u;
    *copy_len = 0u;
    *drop_len = 0u;
    *wrapper_len = 0u;
    *work_item_len = 0u;
    if (!mask || !program || !accepted_text || !rejected_text ||
        mask->erased.nfa.tag_len != 1u ||
        !cetta_lp_native_utf8_scalar_buffer_decode(
            &accepted, (const uint8_t *)accepted_text,
            strlen(accepted_text), error, error_size) ||
        !cetta_lp_native_utf8_scalar_buffer_decode(
            &rejected, (const uint8_t *)rejected_text,
            strlen(rejected_text), error, error_size)) {
        goto done;
    }
    if (accepted.view.scalar_len > 0u) {
        actions = malloc(
            sizeof(*actions) * (size_t)accepted.view.scalar_len);
        if (!actions) {
            (void)snprintf(error, error_size,
                           "cannot allocate semantic-mask probe actions");
            goto done;
        }
    }
    if (!ppsemantic_mask_dfa_v1_run_exact_prevalidated(
            program, &accepted.view, 0u, accepted.view.scalar_len, 0u,
            UINT64_C(1000000), actions, accepted.view.scalar_len,
            &result, error, error_size) ||
        result.outcome != PPSEMANTIC_MASK_DFA_V1_ACCEPTED ||
        result.action_len != accepted.view.scalar_len) {
        if (!error[0])
            (void)snprintf(error, error_size,
                           "semantic-mask positive probe was not accepted");
        goto done;
    }
    *accepted_scalar_len = accepted.view.scalar_len;
    *work_item_len = result.work_item_len;
    for (index = 0u; index < result.action_len; index++) {
        if (actions[index] >= mask->action_len)
            goto done;
        switch (mask->actions[actions[index]].kind) {
        case PPSEMANTIC_MASK_V1_COPY:
            (*copy_len)++;
            break;
        case PPSEMANTIC_MASK_V1_DROP:
            (*drop_len)++;
            break;
        case PPSEMANTIC_MASK_V1_WRAPPER:
            (*wrapper_len)++;
            break;
        }
    }
    free(actions);
    actions = NULL;
    if (rejected.view.scalar_len > 0u) {
        actions = malloc(
            sizeof(*actions) * (size_t)rejected.view.scalar_len);
        if (!actions) {
            (void)snprintf(error, error_size,
                           "cannot allocate semantic-mask rejection actions");
            goto done;
        }
    }
    if (!ppsemantic_mask_dfa_v1_run_exact_prevalidated(
            program, &rejected.view, 0u, rejected.view.scalar_len, 0u,
            UINT64_C(1000000), actions, rejected.view.scalar_len,
            &result, error, error_size) ||
        result.outcome != PPSEMANTIC_MASK_DFA_V1_REJECTED) {
        if (!error[0])
            (void)snprintf(error, error_size,
                           "semantic-mask negative probe was not rejected");
        goto done;
    }
    ok = true;

done:
    free(actions);
    cetta_lp_native_utf8_scalar_buffer_free(&rejected);
    cetta_lp_native_utf8_scalar_buffer_free(&accepted);
    return ok;
}

static bool run_trace_probe(
    const PPSemanticMaskNfaV1Plan *mask,
    const PPSemanticMaskTraceDfaV1Program *program,
    const char *accepted_text,
    const char *rejected_text,
    uint32_t *accepted_scalar_len,
    uint32_t *copy_len,
    uint32_t *drop_len,
    uint32_t *wrapper_len,
    uint64_t *work_item_len,
    char *error,
    size_t error_size) {
    CettaLpNativeUtf8ScalarBuffer accepted;
    CettaLpNativeUtf8ScalarBuffer rejected;
    PPSemanticMaskDfaV1RunResult result;
    uint32_t *actions = NULL;
    uint32_t index;
    bool ok = false;

    cetta_lp_native_utf8_scalar_buffer_init(&accepted);
    cetta_lp_native_utf8_scalar_buffer_init(&rejected);
    *accepted_scalar_len = 0u;
    *copy_len = 0u;
    *drop_len = 0u;
    *wrapper_len = 0u;
    *work_item_len = 0u;
    if (!mask || !program || !accepted_text || !rejected_text ||
        mask->erased.nfa.tag_len != 1u ||
        !cetta_lp_native_utf8_scalar_buffer_decode(
            &accepted, (const uint8_t *)accepted_text,
            strlen(accepted_text), error, error_size) ||
        !cetta_lp_native_utf8_scalar_buffer_decode(
            &rejected, (const uint8_t *)rejected_text,
            strlen(rejected_text), error, error_size)) {
        goto done;
    }
    if (accepted.view.scalar_len > 0u) {
        actions = malloc(
            sizeof(*actions) * (size_t)accepted.view.scalar_len);
        if (!actions) {
            (void)snprintf(error, error_size,
                           "cannot allocate semantic-mask trace actions");
            goto done;
        }
    }
    if (!ppsemantic_mask_trace_dfa_v1_run_exact_prevalidated(
            program, &accepted.view, 0u, accepted.view.scalar_len, 0u,
            UINT64_C(1000000), actions, accepted.view.scalar_len,
            &result, error, error_size) ||
        result.outcome != PPSEMANTIC_MASK_DFA_V1_ACCEPTED ||
        result.action_len != accepted.view.scalar_len) {
        if (!error[0])
            (void)snprintf(error, error_size,
                           "semantic-mask trace positive probe was rejected");
        goto done;
    }
    *accepted_scalar_len = accepted.view.scalar_len;
    *work_item_len = result.work_item_len;
    for (index = 0u; index < result.action_len; index++) {
        if (actions[index] >= mask->action_len)
            goto done;
        switch (mask->actions[actions[index]].kind) {
        case PPSEMANTIC_MASK_V1_COPY:
            (*copy_len)++;
            break;
        case PPSEMANTIC_MASK_V1_DROP:
            (*drop_len)++;
            break;
        case PPSEMANTIC_MASK_V1_WRAPPER:
            (*wrapper_len)++;
            break;
        }
    }
    free(actions);
    actions = NULL;
    if (rejected.view.scalar_len > 0u) {
        actions = malloc(
            sizeof(*actions) * (size_t)rejected.view.scalar_len);
        if (!actions) {
            (void)snprintf(error, error_size,
                           "cannot allocate semantic-mask trace rejection");
            goto done;
        }
    }
    if (!ppsemantic_mask_trace_dfa_v1_run_exact_prevalidated(
            program, &rejected.view, 0u, rejected.view.scalar_len, 0u,
            UINT64_C(1000000), actions, rejected.view.scalar_len,
            &result, error, error_size) ||
        result.outcome != PPSEMANTIC_MASK_DFA_V1_REJECTED) {
        if (!error[0])
            (void)snprintf(error, error_size,
                           "semantic-mask trace negative probe was accepted");
        goto done;
    }
    ok = true;

done:
    free(actions);
    cetta_lp_native_utf8_scalar_buffer_free(&rejected);
    cetta_lp_native_utf8_scalar_buffer_free(&accepted);
    return ok;
}

static bool run(const char *abi_path,
                const char *mask_path,
                const char *recognition_path,
                const char *semantic_compiler_digest,
                const char *accepted_probe,
                const char *rejected_probe) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 mask_answers;
    FHAnswerStreamV1 recognition_answers;
    PPSemanticMaskNfaV1Plan mask_nfa;
    RSNFAV1Plan recognition_nfa;
    RSDFAV1Plan mask_dfa;
    RSDFAV1Plan recognition_dfa;
    RSDFAV1Program mask_program;
    RSDFAV1Program recognition_program;
    PPSemanticMaskDfaV1Program mask_action_program;
    PPSemanticMaskTraceDfaV1Program mask_trace_program;
    RSDFAV1InclusionResult inclusion;
    RSDFAV1FunctionalityResult functionality;
    RSDFAV1BuildOutcome mask_outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    RSDFAV1BuildOutcome mask_action_outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    RSDFAV1BuildOutcome mask_trace_outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    RSDFAV1BuildOutcome recognition_outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    uint32_t action_index;
    uint32_t wrapper_len = 0u;
    uint32_t probe_scalar_len = 0u;
    uint32_t probe_copy_len = 0u;
    uint32_t probe_drop_len = 0u;
    uint32_t probe_wrapper_len = 0u;
    uint64_t probe_work_item_len = 0u;
    bool probe_ran = false;
    uint32_t trace_probe_scalar_len = 0u;
    uint32_t trace_probe_copy_len = 0u;
    uint32_t trace_probe_drop_len = 0u;
    uint32_t trace_probe_wrapper_len = 0u;
    uint64_t trace_probe_work_item_len = 0u;
    bool trace_probe_ran = false;
    CettaNativeSha256 sha;
    char certificate_digest[65];
    bool left_in_right = false;
    bool right_in_left = false;
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&mask_answers);
    fh_answer_stream_v1_init(&recognition_answers);
    ppsemantic_mask_nfa_v1_plan_init(&mask_nfa);
    rsnfa_v1_plan_init(&recognition_nfa);
    rsdfa_v1_plan_init(&mask_dfa);
    rsdfa_v1_plan_init(&recognition_dfa);
    rsdfa_v1_program_init(&mask_program);
    rsdfa_v1_program_init(&recognition_program);
    ppsemantic_mask_dfa_v1_program_init(&mask_action_program);
    ppsemantic_mask_trace_dfa_v1_program_init(&mask_trace_program);
    rsdfa_v1_inclusion_result_init(&inclusion);
    rsdfa_v1_functionality_result_init(&functionality);

    if (!digest_valid(semantic_compiler_digest)) {
        (void)snprintf(
            error, sizeof(error),
            "semantic-mask compiler digest is malformed");
        goto rejected;
    }
    if (!ppabi_v1_wire_read(&wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &mask_answers, mask_path, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &recognition_answers, recognition_path,
            error, sizeof(error)) ||
        !ppsemantic_mask_nfa_v1_plan_load(
            &pack, mask_answers.terms, mask_answers.len,
            &mask_nfa, error, sizeof(error)) ||
        !rsnfa_v1_plan_load(
            &pack, recognition_answers.terms, recognition_answers.len,
            &recognition_nfa, error, sizeof(error)) ||
        mask_nfa.erased.nfa.tag_len != 1u ||
        recognition_nfa.nfa.tag_len != 1u ||
        !rsdfa_v1_plan_build(
            &mask_nfa.erased.nfa,
            UINT32_C(65536), UINT32_C(2000000),
            &mask_dfa, &mask_outcome, error, sizeof(error)) ||
        mask_outcome != RSDFA_V1_BUILD_COMPLETED ||
        !ppsemantic_mask_dfa_v1_program_build(
            &mask_nfa,
            UINT32_C(65536), UINT32_C(2000000),
            &mask_action_program, &mask_action_outcome,
            error, sizeof(error)) ||
        (mask_action_outcome != RSDFA_V1_BUILD_COMPLETED &&
         mask_action_outcome != RSDFA_V1_BUILD_ANNOTATION_CONFLICT) ||
        !ppsemantic_mask_trace_dfa_v1_program_build(
            &mask_nfa, UINT32_C(65536), UINT32_C(2000000),
            UINT32_C(4000000), UINT64_C(500000000),
            &mask_trace_program, &mask_trace_outcome,
            error, sizeof(error)) ||
        mask_trace_outcome != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_annotated_nfa_functionality(
            &mask_nfa.erased.nfa, mask_nfa.edge_actions, UINT32_MAX,
            0u, UINT32_C(4000000), UINT64_C(500000000),
            &functionality, error, sizeof(error)) ||
        functionality.outcome != RSDFA_V1_FUNCTIONALITY_COMPLETED ||
        !functionality.functional ||
        !rsdfa_v1_plan_build(
            &recognition_nfa.nfa,
            UINT32_C(65536), UINT32_C(2000000),
            &recognition_dfa, &recognition_outcome,
            error, sizeof(error)) ||
        recognition_outcome != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_plan_export_program(
            &mask_dfa, &mask_program, error, sizeof(error)) ||
        !rsdfa_v1_plan_export_program(
            &recognition_dfa, &recognition_program,
            error, sizeof(error)) ||
        !rsdfa_v1_program_tag_inclusion(
            &mask_program, 0u, &recognition_program, 0u,
            UINT32_C(2000000), &inclusion,
            error, sizeof(error)) ||
        inclusion.outcome != RSDFA_V1_INCLUSION_COMPLETED) {
        goto rejected;
    }
    if (mask_action_outcome == RSDFA_V1_BUILD_COMPLETED &&
        mask_action_program.transition_action_len !=
            mask_program.transition_len) {
            goto rejected;
    }
    if (accepted_probe || rejected_probe) {
        if (!accepted_probe || !rejected_probe ||
            !run_trace_probe(
                &mask_nfa, &mask_trace_program,
                accepted_probe, rejected_probe,
                &trace_probe_scalar_len, &trace_probe_copy_len,
                &trace_probe_drop_len, &trace_probe_wrapper_len,
                &trace_probe_work_item_len, error, sizeof(error))) {
            goto rejected;
        }
        trace_probe_ran = true;
    }
    if (mask_action_outcome == RSDFA_V1_BUILD_COMPLETED) {
        if (!accepted_probe || !rejected_probe ||
            !run_action_probe(
                &mask_nfa, &mask_action_program,
                accepted_probe, rejected_probe,
                &probe_scalar_len, &probe_copy_len, &probe_drop_len,
                &probe_wrapper_len, &probe_work_item_len,
                error, sizeof(error))) {
            goto rejected;
        }
        probe_ran = true;
    }
    left_in_right = inclusion.included;
    if (!rsdfa_v1_program_tag_inclusion(
            &recognition_program, 0u, &mask_program, 0u,
            UINT32_C(2000000), &inclusion,
            error, sizeof(error)) ||
        inclusion.outcome != RSDFA_V1_INCLUSION_COMPLETED) {
        goto rejected;
    }
    right_in_left = inclusion.included;
    for (action_index = 0u;
         action_index < mask_action_program.transition_action_len;
         action_index++) {
        if (mask_action_program.transition_actions[action_index] >=
                mask_nfa.action_len) {
            (void)snprintf(
                error, sizeof(error),
                "semantic-mask DFA transition action is out of range");
            goto rejected;
        }
    }
    for (action_index = 0u;
         action_index < mask_nfa.action_len;
         action_index++) {
        if (mask_nfa.actions[action_index].kind ==
                PPSEMANTIC_MASK_V1_WRAPPER) {
            wrapper_len++;
        }
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)"SemanticMaskNfaCertificateV1\n", 29u);
    sha_text(&sha, pack.source_digest);
    sha_text(&sha, semantic_compiler_digest);
    sha_text(&sha, mask_answers.digest);
    sha_text(&sha, recognition_answers.digest);
    sha_u32(&sha, (uint32_t)mask_outcome);
    sha_u32(&sha, (uint32_t)mask_action_outcome);
    sha_u32(&sha, (uint32_t)mask_trace_outcome);
    sha_u32(&sha, (uint32_t)recognition_outcome);
    sha_u32(&sha, mask_program.state_len);
    sha_u32(&sha, mask_program.transition_len);
    sha_u32(&sha, mask_nfa.action_len);
    sha_u32(&sha, wrapper_len);
    sha_u32(&sha, mask_nfa.root_link_len);
    sha_u32(&sha, mask_trace_program.dfa.state_len);
    sha_u32(&sha, mask_trace_program.dfa.transition_len);
    sha_u32(&sha, mask_trace_program.transition_action_len);
    sha_u32(&sha, mask_trace_program.action_local_transition_len);
    sha_u32(&sha, mask_trace_program.backstep_len);
    for (action_index = 0u;
         action_index < mask_trace_program.transition_action_len;
         action_index++) {
        sha_u32(
            &sha, mask_trace_program.transition_actions[action_index]);
    }
    sha_u32(
        &sha,
        mask_action_outcome == RSDFA_V1_BUILD_COMPLETED ? 1u : 0u);
    sha_u32(&sha, functionality.explored_pair_len);
    sha_u64(&sha, functionality.work_item_len);
    sha_u32(&sha, functionality.functional ? 1u : 0u);
    sha_u32(&sha, left_in_right ? 1u : 0u);
    sha_u32(&sha, right_in_left ? 1u : 0u);
    cetta_native_sha256_finish_hex(&sha, certificate_digest);
    (void)printf("semantic-mask-nfa-v1\n");
    (void)printf("source-digest\t%s\n", pack.source_digest);
    (void)printf("semantic-compiler-digest\t%s\n",
                 semantic_compiler_digest);
    (void)printf("mask-answer-digest\t%s\n", mask_answers.digest);
    (void)printf("mask-answers\t%zu\n", mask_answers.len);
    (void)printf("recognition-answer-digest\t%s\n",
                 recognition_answers.digest);
    (void)printf("recognition-answers\t%zu\n", recognition_answers.len);
    (void)printf("mask-build-outcome\t%s\n",
                 build_outcome_name(mask_outcome));
    (void)printf("mask-action-build-outcome\t%s\n",
                 build_outcome_name(mask_action_outcome));
    (void)printf("mask-trace-build-outcome\t%s\n",
                 build_outcome_name(mask_trace_outcome));
    (void)printf("recognition-build-outcome\t%s\n",
                 build_outcome_name(recognition_outcome));
    (void)printf("mask-states\t%u\n", mask_program.state_len);
    (void)printf("mask-transitions\t%u\n", mask_program.transition_len);
    (void)printf("mask-actions\t%u\n", mask_nfa.action_len);
    (void)printf("mask-wrappers\t%u\n", wrapper_len);
    (void)printf("mask-root-links\t%u\n", mask_nfa.root_link_len);
    (void)printf("trace-states\t%u\n",
                 mask_trace_program.dfa.state_len);
    (void)printf("trace-transitions\t%u\n",
                 mask_trace_program.dfa.transition_len);
    (void)printf("trace-action-local-transitions\t%u\n",
                 mask_trace_program.action_local_transition_len);
    (void)printf("trace-backsteps\t%u\n",
                 mask_trace_program.backstep_len);
    (void)printf("trace-functionality-pairs\t%u\n",
                 mask_trace_program.functionality_pair_len);
    (void)printf("trace-functionality-work\t%llu\n",
                 (unsigned long long)
                     mask_trace_program.functionality_work_item_len);
    (void)printf("action-local\t%u\n",
                 mask_action_outcome == RSDFA_V1_BUILD_COMPLETED ? 1u : 0u);
    (void)printf("action-probe-ran\t%u\n", probe_ran ? 1u : 0u);
    (void)printf("action-probe-scalars\t%u\n", probe_scalar_len);
    (void)printf("action-probe-copy\t%u\n", probe_copy_len);
    (void)printf("action-probe-drop\t%u\n", probe_drop_len);
    (void)printf("action-probe-wrapper\t%u\n", probe_wrapper_len);
    (void)printf("action-probe-work\t%llu\n",
                 (unsigned long long)probe_work_item_len);
    (void)printf("trace-probe-ran\t%u\n", trace_probe_ran ? 1u : 0u);
    (void)printf("trace-probe-scalars\t%u\n", trace_probe_scalar_len);
    (void)printf("trace-probe-copy\t%u\n", trace_probe_copy_len);
    (void)printf("trace-probe-drop\t%u\n", trace_probe_drop_len);
    (void)printf("trace-probe-wrapper\t%u\n", trace_probe_wrapper_len);
    (void)printf("trace-probe-work\t%llu\n",
                 (unsigned long long)trace_probe_work_item_len);
    (void)printf("functionality-pairs\t%u\n",
                 functionality.explored_pair_len);
    (void)printf("functionality-work\t%llu\n",
                 (unsigned long long)functionality.work_item_len);
    (void)printf("functional\t%u\n",
                 functionality.functional ? 1u : 0u);
    (void)printf("mask-in-recognition\t%u\n", left_in_right ? 1u : 0u);
    (void)printf("recognition-in-mask\t%u\n", right_in_left ? 1u : 0u);
    (void)printf("erasure-equivalent\t%u\n",
                 left_in_right && right_in_left ? 1u : 0u);
    (void)printf("certificate-digest\t%s\n", certificate_digest);
    (void)printf("end\n");
    ok = true;
    goto done;

rejected:
    (void)fprintf(stderr, "semantic-mask NFA rejected: %s\n",
                  error[0] ? error : build_outcome_name(mask_outcome));

done:
    rsdfa_v1_functionality_result_free(&functionality);
    rsdfa_v1_inclusion_result_free(&inclusion);
    rsdfa_v1_program_free(&recognition_program);
    rsdfa_v1_program_free(&mask_program);
    ppsemantic_mask_dfa_v1_program_free(&mask_action_program);
    ppsemantic_mask_trace_dfa_v1_program_free(&mask_trace_program);
    rsdfa_v1_plan_free(&recognition_dfa);
    rsdfa_v1_plan_free(&mask_dfa);
    rsnfa_v1_plan_free(&recognition_nfa);
    ppsemantic_mask_nfa_v1_plan_free(&mask_nfa);
    fh_answer_stream_v1_free(&recognition_answers);
    fh_answer_stream_v1_free(&mask_answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 5 && argc != 7) {
        (void)fprintf(
            stderr,
            "usage: semantic_mask_nfa_v1_stream ABI MASK_NFA "
            "RECOGNITION_NFA SEMANTIC_COMPILER_DIGEST "
            "[ACCEPTED_PROBE REJECTED_PROBE]\n");
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(
        argv[1], argv[2], argv[3], argv[4],
        argc == 7 ? argv[5] : NULL,
        argc == 7 ? argv[6] : NULL);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
