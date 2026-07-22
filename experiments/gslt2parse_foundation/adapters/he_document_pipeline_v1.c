#include "he_document_pipeline_v1.h"

#include "../native/finite_horn_answer_stream_v1.h"
#include "../native/parser_atom_projection_action_v1.h"
#include "../native/parser_atom_projection_events_v1.h"
#include "../native/parser_pack_abi_stream_v1.h"
#include "../native/parser_pack_guard_evidence_stream_v1.h"
#include "../native/parser_pack_semantic_mask_binding_v1.h"

#include "symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct HEDocumentPipelineV1Prepared {
    bool initialized;
    bool ready;
    uint32_t build_count;
    uint32_t atom_projection_depth_limit;
    SymbolTable *owner_symbols;
    PPGuardedLexExecV1Limits limits;
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 lexical_answers;
    FHAnswerStreamV1 guard_answers;
    FHAnswerStreamV1 guarded_answers;
    FHAnswerStreamV1 action_answers;
    FHAnswerStreamV1 semantic_mask_answers;
    RSNFAV1Plan lexical_nfa;
    RSNFAV1Plan guard_nfa;
    PPLexV1Plan lexical_plan;
    PPGuardEvidenceWireV1 evidence;
    PPGuardPlanV1 guard_plan;
    PPGuardedLexV1Plan guarded_plan;
    PPGuardedLexExecV1Plan exec_plan;
    PPGuardedLexCursorV1Program cursor_program;
    PPAtomProjectionV1Plan atom_projection;
    PPAtomProjectionActionV1Program projection_actions;
    PPGuardedLexCursorV1SemanticMaskBinding semantic_masks;
    HEDocumentPipelineV1Receipt base_receipt;
};

static void he_pipeline_v1_set_error(char *buf,
                                     size_t size,
                                     const char *format,
                                     ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool he_pipeline_v1_limits_valid(
    const PPGuardedLexExecV1Limits *limits) {
    return limits &&
        limits->dfa_state_limit > 0u &&
        limits->dfa_transition_limit > 0u &&
        limits->scan_work_limit > 0u &&
        limits->scan_token_limit > 0u &&
        limits->witness_work_limit > 0u &&
        limits->parse_work_limit > 0u &&
        limits->replay_depth > 0u &&
        limits->result_limit > 0u;
}

void he_document_pipeline_v1_result_init(
    HEDocumentPipelineV1Result *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    arena_init(&result->arena);
}

void he_document_pipeline_v1_result_free(
    HEDocumentPipelineV1Result *result) {
    if (!result)
        return;
    arena_free(&result->arena);
    memset(result, 0, sizeof(*result));
}

static void he_pipeline_v1_state_init(
    HEDocumentPipelineV1Prepared *prepared) {
    memset(prepared, 0, sizeof(*prepared));
    ppabi_v1_wire_init(&prepared->wire);
    ppabi_v1_pack_init(&prepared->pack);
    fh_answer_stream_v1_init(&prepared->lexical_answers);
    fh_answer_stream_v1_init(&prepared->guard_answers);
    fh_answer_stream_v1_init(&prepared->guarded_answers);
    fh_answer_stream_v1_init(&prepared->action_answers);
    fh_answer_stream_v1_init(&prepared->semantic_mask_answers);
    rsnfa_v1_plan_init(&prepared->lexical_nfa);
    rsnfa_v1_plan_init(&prepared->guard_nfa);
    pplex_v1_plan_init(&prepared->lexical_plan);
    ppguard_evidence_wire_v1_init(&prepared->evidence);
    ppguard_plan_v1_init(&prepared->guard_plan);
    ppguarded_lex_v1_plan_init(&prepared->guarded_plan);
    ppguarded_lex_exec_v1_plan_init(&prepared->exec_plan);
    ppguarded_lex_cursor_v1_program_init(&prepared->cursor_program);
    ppatom_projection_v1_plan_init(&prepared->atom_projection);
    ppatom_projection_action_v1_program_init(
        &prepared->projection_actions);
    ppguarded_lex_cursor_v1_semantic_mask_binding_init(
        &prepared->semantic_masks);
    prepared->initialized = true;
}

static void he_pipeline_v1_state_release(
    HEDocumentPipelineV1Prepared *prepared) {
    if (!prepared || !prepared->initialized)
        return;
    ppguarded_lex_cursor_v1_semantic_mask_binding_free(
        &prepared->semantic_masks);
    ppatom_projection_action_v1_program_free(
        &prepared->projection_actions);
    ppatom_projection_v1_plan_free(&prepared->atom_projection);
    ppguarded_lex_cursor_v1_program_free(&prepared->cursor_program);
    ppguarded_lex_exec_v1_plan_free(&prepared->exec_plan);
    ppguarded_lex_v1_plan_free(&prepared->guarded_plan);
    ppguard_plan_v1_free(&prepared->guard_plan);
    ppguard_evidence_wire_v1_free(&prepared->evidence);
    pplex_v1_plan_free(&prepared->lexical_plan);
    rsnfa_v1_plan_free(&prepared->guard_nfa);
    rsnfa_v1_plan_free(&prepared->lexical_nfa);
    fh_answer_stream_v1_free(&prepared->guarded_answers);
    fh_answer_stream_v1_free(&prepared->action_answers);
    fh_answer_stream_v1_free(&prepared->semantic_mask_answers);
    fh_answer_stream_v1_free(&prepared->guard_answers);
    fh_answer_stream_v1_free(&prepared->lexical_answers);
    ppabi_v1_pack_free(&prepared->pack);
    ppabi_v1_wire_free(&prepared->wire);
    memset(prepared, 0, sizeof(*prepared));
}

HEDocumentPipelineV1Prepared *he_document_pipeline_v1_prepared_new(void) {
    return calloc(1u, sizeof(HEDocumentPipelineV1Prepared));
}

void he_document_pipeline_v1_prepared_free(
    HEDocumentPipelineV1Prepared *prepared) {
    if (!prepared)
        return;
    he_pipeline_v1_state_release(prepared);
    free(prepared);
}

uint32_t he_document_pipeline_v1_prepared_build_count(
    const HEDocumentPipelineV1Prepared *prepared) {
    return prepared && prepared->ready ? prepared->build_count : 0u;
}

bool he_document_pipeline_v1_prepare(
    HEDocumentPipelineV1Prepared *prepared,
    const char *abi_path,
    const char *lexical_nfa_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *guarded_nfa_path,
    const char *action_answer_path,
    const char *semantic_mask_path,
    const char *regular_compiler_digest,
    const char *guarded_compiler_digest,
    const char *action_compiler_digest,
    const char *semantic_mask_compiler_digest,
    const Atom *atom_projection_artifact,
    const PPGuardedLexExecV1Limits *limits,
    uint32_t atom_projection_depth_limit,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardPlanV1ProvenanceInput provenance;
    PPAtomProjectionV1ProvenanceView atom_provenance;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !abi_path || !lexical_nfa_path || !guard_nfa_path ||
        !guard_evidence_path || !guarded_nfa_path || !action_answer_path ||
        !semantic_mask_path ||
        !regular_compiler_digest || !guarded_compiler_digest ||
        !action_compiler_digest || !semantic_mask_compiler_digest ||
        !atom_projection_artifact || !he_pipeline_v1_limits_valid(limits) ||
        atom_projection_depth_limit == 0u || !g_symbols) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "invalid prepared HE document configuration");
        return false;
    }
    if (prepared->ready) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "prepared HE document pipeline is already bound");
        return false;
    }
    if (prepared->initialized)
        he_pipeline_v1_state_release(prepared);
    he_pipeline_v1_state_init(prepared);
    prepared->owner_symbols = g_symbols;
    prepared->limits = *limits;
    prepared->atom_projection_depth_limit = atom_projection_depth_limit;

    if (!ppabi_v1_wire_read(
            &prepared->wire, abi_path, error_buf, error_buf_size) ||
        !ppabi_v1_wire_load_pack(
            &prepared->wire, &prepared->pack,
            error_buf, error_buf_size) ||
        !fh_answer_stream_v1_read(
            &prepared->lexical_answers, lexical_nfa_path,
            error_buf, error_buf_size) ||
        prepared->lexical_answers.len == 0u ||
        !rsnfa_v1_plan_load(
            &prepared->pack, prepared->lexical_answers.terms,
            prepared->lexical_answers.len, &prepared->lexical_nfa,
            error_buf, error_buf_size) ||
        !pplex_v1_plan_build(
            &prepared->pack, prepared->lexical_nfa.tags,
            prepared->lexical_nfa.nfa.tag_len,
            regular_compiler_digest, prepared->lexical_answers.digest,
            &prepared->lexical_plan, error_buf, error_buf_size) ||
        !fh_answer_stream_v1_read(
            &prepared->guard_answers, guard_nfa_path,
            error_buf, error_buf_size) ||
        prepared->guard_answers.len == 0u ||
        !rsnfa_v1_plan_load(
            &prepared->pack, prepared->guard_answers.terms,
            prepared->guard_answers.len, &prepared->guard_nfa,
            error_buf, error_buf_size) ||
        !ppguard_evidence_wire_v1_read(
            &prepared->evidence, guard_evidence_path,
            error_buf, error_buf_size)) {
        goto done;
    }
    provenance = (PPGuardPlanV1ProvenanceInput){
        .source_digest = prepared->evidence.source_digest,
        .pre_reflection_digest = prepared->evidence.pre_reflection_digest,
        .environment_digest = prepared->evidence.environment_digest,
        .answer_set_digest = prepared->evidence.answer_set_digest,
        .regular_compiler_digest = regular_compiler_digest,
        .guard_nfa_answer_digest = prepared->guard_answers.digest,
        .guard_nfa_tags = prepared->guard_nfa.tags,
        .guard_nfa_tag_len = prepared->guard_nfa.nfa.tag_len,
        .derivations = prepared->evidence.derivations,
        .derivation_len = prepared->evidence.derivation_len,
    };
    if (!ppguard_plan_v1_build(
            &prepared->pack, &prepared->lexical_plan, &provenance,
            &prepared->guard_plan, error_buf, error_buf_size) ||
        !fh_answer_stream_v1_read(
            &prepared->guarded_answers, guarded_nfa_path,
            error_buf, error_buf_size) ||
        prepared->guarded_answers.len == 0u ||
        !ppguarded_lex_v1_plan_build(
            &prepared->pack, &prepared->lexical_plan,
            &prepared->guard_plan, prepared->guarded_answers.terms,
            prepared->guarded_answers.len, guarded_compiler_digest,
            prepared->guarded_answers.digest, &prepared->guarded_plan,
            error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_plan_build(
            &prepared->pack, prepared->wire.start,
            &prepared->lexical_plan, &prepared->guard_plan,
            &prepared->guarded_plan, &prepared->lexical_nfa,
            &prepared->guard_nfa, limits, &prepared->exec_plan,
            error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_program_build(
            &prepared->pack, prepared->wire.start,
            &prepared->lexical_plan, &prepared->guard_plan,
            &prepared->guarded_plan, &prepared->exec_plan,
            &prepared->cursor_program, error_buf, error_buf_size) ||
        !fh_answer_stream_v1_read(
            &prepared->action_answers, action_answer_path,
            error_buf, error_buf_size) ||
        prepared->action_answers.len == 0u ||
        !ppguarded_lex_cursor_v1_program_bind_actions(
            &prepared->cursor_program, &prepared->pack,
            &prepared->lexical_plan, &prepared->guard_plan,
            &prepared->guarded_plan,
            prepared->action_answers.terms,
            prepared->action_answers.len,
            action_compiler_digest, prepared->action_answers.digest,
            error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_load(
            &prepared->atom_projection, atom_projection_artifact,
            error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_provenance(
            &prepared->atom_projection, &atom_provenance,
            error_buf, error_buf_size) ||
        !ppatom_projection_action_v1_program_build_prevalidated(
            &prepared->projection_actions,
            &prepared->cursor_program.actions,
            &prepared->atom_projection,
            error_buf, error_buf_size) ||
        !ppatom_projection_action_v1_program_validate(
            &prepared->projection_actions,
            &prepared->cursor_program.actions,
            &prepared->atom_projection,
            error_buf, error_buf_size) ||
        !fh_answer_stream_v1_read(
            &prepared->semantic_mask_answers, semantic_mask_path,
            error_buf, error_buf_size) ||
        prepared->semantic_mask_answers.len == 0u ||
        !ppguarded_lex_cursor_v1_semantic_mask_binding_build(
            &prepared->semantic_masks, &prepared->cursor_program,
            &prepared->pack, &prepared->projection_actions,
            &prepared->atom_projection,
            prepared->semantic_mask_answers.terms,
            prepared->semantic_mask_answers.len,
            semantic_mask_compiler_digest,
            prepared->semantic_mask_answers.digest,
            limits->dfa_state_limit, limits->dfa_transition_limit,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (strcmp(atom_provenance.profile, "he-v1") != 0) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "Atom projection has the wrong profile");
        goto done;
    }
    if (prepared->exec_plan.final_parser_table_build_count != 1u ||
        prepared->exec_plan.ordinary_witness_parser_family_build_count != 1u ||
        prepared->exec_plan.guard_witness_parser_family_build_count != 1u ||
        prepared->exec_plan.guarded_witness_parser_family_build_count != 1u) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "prepared parser tables were not built once");
        goto done;
    }
    if (!prepared->exec_plan.cursor_certificate.eligible ||
        prepared->exec_plan.cursor_certificate.failure_mask != 0u ||
        !prepared->cursor_program.value_programs_complete ||
        prepared->cursor_program.value_program_conflict_len != 0u ||
        !prepared->cursor_program.actions_bound ||
        prepared->projection_actions.production_len !=
            prepared->cursor_program.actions.production_len) {
        he_pipeline_v1_set_error(
            error_buf, error_buf_size,
            "HE document cursor program is not complete");
        goto done;
    }
    (void)snprintf(prepared->base_receipt.base_pack_digest, 65u, "%s",
                   prepared->pack.pack_digest);
    (void)snprintf(prepared->base_receipt.lexical_plan_digest, 65u, "%s",
                   prepared->lexical_plan.plan_digest);
    (void)snprintf(prepared->base_receipt.guard_plan_digest, 65u, "%s",
                   prepared->guard_plan.plan_digest);
    (void)snprintf(prepared->base_receipt.guarded_plan_digest, 65u, "%s",
                   prepared->guarded_plan.plan_digest);
    (void)snprintf(prepared->base_receipt.execution_plan_digest, 65u, "%s",
                   prepared->exec_plan.execution_plan_digest);
    (void)snprintf(
        prepared->base_receipt.cursor_certificate_digest, 65u, "%s",
        prepared->cursor_program.certificate_digest);
    (void)snprintf(prepared->base_receipt.cursor_program_digest, 65u, "%s",
                   prepared->cursor_program.program_digest);
    (void)snprintf(prepared->base_receipt.action_compiler_digest, 65u, "%s",
                   prepared->cursor_program.actions.compiler_digest);
    (void)snprintf(prepared->base_receipt.action_answer_digest, 65u, "%s",
                   prepared->cursor_program.actions.answer_set_digest);
    (void)snprintf(prepared->base_receipt.action_program_digest, 65u, "%s",
                   prepared->cursor_program.actions.program_digest);
    (void)snprintf(
        prepared->base_receipt.projection_action_program_digest, 65u, "%s",
        prepared->projection_actions.program_digest);
    (void)snprintf(
        prepared->base_receipt.semantic_mask_compiler_digest, 65u, "%s",
        prepared->semantic_masks.compiler_digest);
    (void)snprintf(
        prepared->base_receipt.semantic_mask_answer_digest, 65u, "%s",
        prepared->semantic_masks.answer_set_digest);
    (void)snprintf(
        prepared->base_receipt.semantic_mask_binding_digest, 65u, "%s",
        prepared->semantic_masks.binding_digest);
    (void)snprintf(
        prepared->base_receipt.atom_projection_source_digest, 65u, "%s",
        atom_provenance.source_digest);
    (void)snprintf(
        prepared->base_receipt.atom_projection_reader_digest, 65u, "%s",
        atom_provenance.reader_digest);
    (void)snprintf(
        prepared->base_receipt.atom_projection_compiler_digest, 65u, "%s",
        atom_provenance.compiler_digest);
    (void)snprintf(
        prepared->base_receipt.atom_projection_plan_digest, 65u, "%s",
        atom_provenance.plan_digest);
    prepared->base_receipt.projection_action_production_len =
        prepared->projection_actions.production_len;
    prepared->base_receipt.semantic_mask_bound_terminal_len =
        prepared->semantic_masks.bound_terminal_len;
    prepared->build_count = 1u;
    prepared->base_receipt.prepared_build_count = 1u;
    prepared->ready = true;
    ok = true;

done:
    if (!ok) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            he_pipeline_v1_set_error(
                error_buf, error_buf_size,
                "failed to prepare HE document pipeline");
        }
        he_pipeline_v1_state_release(prepared);
    }
    return ok;
}

static bool he_pipeline_v1_parse_mode(
    HEDocumentPipelineV1Prepared *prepared,
    const uint8_t *input,
    size_t input_len,
    PPGuardedLexCursorV1Observation observation,
    HEDocumentPipelineV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1ProjectedResult execution;
    HEDocumentPipelineV1Result result;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !prepared->ready ||
        prepared->owner_symbols != g_symbols || !out ||
        observation > PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE ||
        input_len > UINT32_MAX || (input_len > 0u && !input)) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "invalid prepared HE document parse");
        return false;
    }
    ppguarded_lex_cursor_v1_projected_result_init(&execution);
    he_document_pipeline_v1_result_init(&result);
    if (!ppguarded_lex_cursor_v1_program_run_projected_direct_bytes_prevalidated(
            &prepared->cursor_program, &prepared->projection_actions,
            &prepared->atom_projection, &prepared->semantic_masks,
            input, input_len, observation,
            prepared->limits.parse_work_limit, &result.arena,
            prepared->atom_projection_depth_limit, &execution,
            error_buf, error_buf_size)) {
        goto done;
    }
    if ((execution.receipt.outcome !=
             PPGUARDED_LEX_CURSOR_V1_ACCEPTED &&
         execution.receipt.outcome !=
             PPGUARDED_LEX_CURSOR_V1_REJECTED) ||
        execution.receipt.source_pass_count != 1u ||
        execution.receipt.input_byte_len != input_len ||
        (observation == PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE &&
         strlen(execution.receipt.trace_digest) != 64u) ||
        (observation == PPGUARDED_LEX_CURSOR_V1_COUNTERS_ONLY &&
         execution.receipt.trace_digest[0] != '\0')) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "HE document execution receipt is invalid");
        goto done;
    }
    result.accepted = execution.receipt.outcome ==
        PPGUARDED_LEX_CURSOR_V1_ACCEPTED;
    if (result.accepted) {
        if (!execution.atoms && execution.atom_len > 0u) {
            he_pipeline_v1_set_error(
                error_buf, error_buf_size,
                "accepted HE document has an invalid host projection");
            goto done;
        }
        result.atoms = execution.atoms;
        result.atom_len = execution.atom_len;
    } else if (execution.atoms || execution.atom_len != 0u) {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "rejected HE document retained semantics");
        goto done;
    }
    result.receipt = prepared->base_receipt;
    result.receipt.parse = execution.receipt;
    result.receipt.terminal_value_len = execution.terminal_value_len;
    result.receipt.action_execution_len = execution.action_execution_len;
    result.receipt.action_instruction_len = execution.action_instruction_len;
    result.receipt.value_program_run_len = execution.value_program_run_len;
    result.receipt.value_parser_work_item_len =
        execution.value_parser_work_item_len;
    result.receipt.semantic_mask_run_len = execution.semantic_mask_run_len;
    result.receipt.semantic_mask_input_scalar_len =
        execution.semantic_mask_input_scalar_len;
    result.receipt.semantic_mask_retained_scalar_len =
        execution.semantic_mask_retained_scalar_len;
    result.receipt.semantic_mask_work_item_len =
        execution.semantic_mask_work_item_len;
    result.receipt.prepared_fallback_run_len = 0u;
    result.receipt.prepared_fallback_work_item_len = 0u;
    he_document_pipeline_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    he_document_pipeline_v1_result_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        he_pipeline_v1_set_error(error_buf, error_buf_size,
                                 "failed to parse HE document");
    }
    return ok;
}

bool he_document_pipeline_v1_parse(
    HEDocumentPipelineV1Prepared *prepared,
    const uint8_t *input,
    size_t input_len,
    HEDocumentPipelineV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return he_pipeline_v1_parse_mode(
        prepared, input, input_len,
        PPGUARDED_LEX_CURSOR_V1_COUNTERS_ONLY,
        out, error_buf, error_buf_size);
}

bool he_document_pipeline_v1_parse_audited(
    HEDocumentPipelineV1Prepared *prepared,
    const uint8_t *input,
    size_t input_len,
    HEDocumentPipelineV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return he_pipeline_v1_parse_mode(
        prepared, input, input_len,
        PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
        out, error_buf, error_buf_size);
}
