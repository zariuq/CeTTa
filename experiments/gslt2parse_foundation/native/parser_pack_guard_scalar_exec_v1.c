#include "parser_pack_guard_scalar_exec_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ppguard_scalar_exec_v1_set_error(char *buf,
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

static bool ppguard_scalar_exec_v1_limits_valid(
    const PPGuardScalarExecV1Limits *limits) {
    return limits && limits->dfa_state_limit > 0u &&
        limits->dfa_transition_limit > 0u &&
        limits->scan_work_limit > 0u && limits->scan_token_limit > 0u &&
        limits->witness_work_limit > 0u && limits->parse_work_limit > 0u &&
        limits->replay_depth > 0u && limits->result_limit > 0u;
}

void ppguard_scalar_exec_v1_plan_init(PPGuardScalarExecV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    rsdfa_v1_plan_init(&plan->dfa);
    cetta_lp_native_gll_prepared_init(&plan->gll);
    cetta_lp_native_glr_prepared_init(&plan->glr);
    ppnative_v1_prepared_start_family_init(
        &plan->guard_witness_parsers);
}

void ppguard_scalar_exec_v1_plan_free(PPGuardScalarExecV1Plan *plan) {
    if (!plan)
        return;
    free(plan->excluded_tags);
    ppnative_v1_prepared_start_family_free(
        &plan->guard_witness_parsers);
    cetta_lp_native_glr_prepared_free(&plan->glr);
    cetta_lp_native_gll_prepared_free(&plan->gll);
    ppnative_v1_terminal_plan_free(&plan->terminals);
    rsdfa_v1_plan_free(&plan->dfa);
    memset(plan, 0, sizeof(*plan));
}

bool ppguard_scalar_exec_v1_plan_build(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPGuardPlanV1 *guard_plan,
    const RSNFAV1Plan *guard_nfa,
    const PPGuardScalarExecV1Limits *limits,
    PPGuardScalarExecV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardScalarExecV1Plan result;
    RSDFAV1BuildOutcome outcome = RSDFA_V1_BUILD_COMPLETED;
    PPNativeV1ForestExtension extension;
    CettaLpNativeGrammar grammar;
    int32_t start_state_id;
    uint32_t index;
    bool ok = false;

    ppguard_scalar_exec_v1_plan_init(&result);
    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state || !guard_plan || !guard_nfa || !out ||
        !ppguard_scalar_exec_v1_limits_valid(limits) ||
        guard_plan->has_lexical_plan || guard_plan->entry_len == 0u ||
        guard_nfa->nfa.tag_len != guard_plan->entry_len ||
        guard_nfa->nfa.tag_len == 0u || !guard_nfa->tags) {
        ppguard_scalar_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad scalar positive-guard adapter inputs");
        goto done;
    }
    extension = (PPNativeV1ForestExtension){
        .states = guard_plan->states,
        .state_len = guard_plan->state_len,
        .terminals = guard_plan->terminals,
        .terminal_len = guard_plan->terminal_len,
        .productions = guard_plan->productions,
        .production_len = guard_plan->production_len,
    };
    start_state_id = ppnative_v1_state_find_extended(
        pack, &extension, start_state);
    if (start_state_id < 0 ||
        !ppnative_v1_start_is_closed_extended(
            pack, start_state, &extension,
            error_buf, error_buf_size) ||
        !ppguard_plan_v1_grammar_build(
            pack, NULL, guard_plan, &grammar,
            error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            ppguard_scalar_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded scalar start state is absent or open");
        }
        goto done;
    }
    result.excluded_tags = malloc(
        sizeof(*result.excluded_tags) * (size_t)guard_nfa->nfa.tag_len);
    if (!result.excluded_tags ||
        !ppnative_v1_terminal_plan_build(
            pack, &result.terminals, error_buf, error_buf_size) ||
        !rsdfa_v1_plan_build(
            &guard_nfa->nfa, limits->dfa_state_limit,
            limits->dfa_transition_limit, &result.dfa, &outcome,
            error_buf, error_buf_size) ||
        !cetta_lp_native_gll_prepare(
            &result.gll, &grammar, (uint32_t)start_state_id,
            error_buf, error_buf_size) ||
        !cetta_lp_native_glr_prepare(
            &result.glr, &grammar, (uint32_t)start_state_id,
            error_buf, error_buf_size) ||
        !ppguard_ref_v1_witness_family_prepare(
            pack, guard_plan, &result.guard_witness_parsers,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (outcome != RSDFA_V1_BUILD_COMPLETED) {
        ppguard_scalar_exec_v1_set_error(
            error_buf, error_buf_size,
            "positive-guard DFA hit its construction limit");
        goto done;
    }
    for (index = 0u; index < guard_nfa->nfa.tag_len; index++)
        result.excluded_tags[index] = UINT32_MAX;
    result.guard_tags = guard_nfa->tags;
    result.guard_tag_len = guard_nfa->nfa.tag_len;
    result.start_state = (Atom *)start_state;
    result.start_state_id = (uint32_t)start_state_id;
    result.parser_table_build_count = 1u;
    result.guard_witness_parser_family_build_count =
        ppnative_v1_prepared_start_family_build_count(
            &result.guard_witness_parsers);
    result.guard_witness_prepared_start_len =
        ppnative_v1_prepared_start_family_len(
            &result.guard_witness_parsers);
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.guard_plan_digest, guard_plan->plan_digest, 65u);
    ppguard_scalar_exec_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_grammar_free(&grammar);
    ppguard_scalar_exec_v1_plan_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguard_scalar_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to build scalar positive-guard adapter");
    }
    return ok;
}

void ppguard_scalar_exec_v1_result_init(PPGuardScalarExecV1Result *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    rsdfa_v1_scan_result_init(&result->scan);
    rsdfa_v1_parser_lattice_init(&result->scalar_lattice);
    ppguard_relation_v1_init(&result->gll_relation);
    ppguard_relation_v1_init(&result->glr_relation);
    ppnative_v1_result_init(&result->gll);
    ppnative_v1_result_init(&result->glr);
}

void ppguard_scalar_exec_v1_result_free(PPGuardScalarExecV1Result *result) {
    if (!result)
        return;
    ppnative_v1_result_free(&result->glr);
    ppnative_v1_result_free(&result->gll);
    ppguard_relation_v1_free(&result->glr_relation);
    ppguard_relation_v1_free(&result->gll_relation);
    rsdfa_v1_parser_lattice_free(&result->scalar_lattice);
    rsdfa_v1_scan_result_free(&result->scan);
    memset(result, 0, sizeof(*result));
}

static bool ppguard_scalar_exec_v1_results_equal(
    const PPNativeV1Result *left,
    const PPNativeV1Result *right) {
    uint32_t index;

    if (left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

bool ppguard_scalar_exec_v1_run(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardScalarExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardScalarExecV1Limits *limits,
    PPGuardScalarExecV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardScalarExecV1Result result;
    bool ok = false;

    ppguard_scalar_exec_v1_result_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state || !guard_plan || !exec_plan || !view ||
        !out || !ppguard_scalar_exec_v1_limits_valid(limits) ||
        exec_plan->guard_tag_len == 0u || !exec_plan->guard_tags ||
        !exec_plan->excluded_tags || !exec_plan->dfa.impl ||
        !exec_plan->gll.implementation || !exec_plan->glr.implementation ||
        !exec_plan->start_state ||
        !atom_eq(exec_plan->start_state, (Atom *)start_state) ||
        exec_plan->parser_table_build_count != 1u ||
        exec_plan->guard_witness_parser_family_build_count != 1u ||
        exec_plan->guard_witness_prepared_start_len == 0u ||
        exec_plan->guard_witness_prepared_start_len >
            guard_plan->entry_len ||
        ppnative_v1_prepared_start_family_build_count(
            &exec_plan->guard_witness_parsers) !=
            exec_plan->guard_witness_parser_family_build_count ||
        ppnative_v1_prepared_start_family_len(
            &exec_plan->guard_witness_parsers) !=
            exec_plan->guard_witness_prepared_start_len ||
        strcmp(exec_plan->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(exec_plan->guard_plan_digest, guard_plan->plan_digest) != 0 ||
        exec_plan->guard_tag_len != guard_plan->entry_len) {
        ppguard_scalar_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad scalar positive-guard execution inputs");
        goto done;
    }
    if (!rsdfa_v1_scan_scalar_view(
            &exec_plan->dfa, view, limits->scan_work_limit,
            limits->scan_token_limit, &result.scan,
            error_buf, error_buf_size) ||
        result.scan.outcome != RSDFA_V1_SCAN_COMPLETED ||
        !rsdfa_v1_composite_parser_lattice_build(
            &result.scan, exec_plan->terminals.terminals,
            exec_plan->terminals.len, exec_plan->excluded_tags,
            exec_plan->guard_tag_len, &result.scalar_lattice,
            error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguard_scalar_exec_v1_set_error(
                error_buf, error_buf_size,
                "scalar positive-guard scan did not complete");
        }
        goto done;
    }
    if (!ppguard_ref_v1_relation_build_gll_prepared(
            pack, guard_plan, exec_plan->guard_tags,
            exec_plan->guard_tag_len, &result.scan,
            &result.scalar_lattice.lattice, NULL, 0u,
            &exec_plan->guard_witness_parsers,
            limits->witness_work_limit, limits->replay_depth,
            limits->result_limit, &result.gll_relation,
            &result.gll_witness, error_buf, error_buf_size) ||
        !ppguard_ref_v1_relation_build_glr_prepared(
            pack, guard_plan, exec_plan->guard_tags,
            exec_plan->guard_tag_len, &result.scan,
            &result.scalar_lattice.lattice, NULL, 0u,
            &exec_plan->guard_witness_parsers,
            limits->witness_work_limit, limits->replay_depth,
            limits->result_limit, &result.glr_relation,
            &result.glr_witness, error_buf, error_buf_size) ||
        strcmp(result.gll_relation.relation_digest,
               result.glr_relation.relation_digest) != 0 ||
        result.gll_witness.parser_run_len !=
            result.glr_witness.parser_run_len ||
        result.gll_witness.match_len != result.glr_witness.match_len ||
        result.gll_witness.source_pass_count != view->source_pass_count ||
        result.glr_witness.source_pass_count != view->source_pass_count) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguard_scalar_exec_v1_set_error(
                error_buf, error_buf_size,
                "GLL and GLR scalar guard witnesses differ");
        }
        goto done;
    }
    if (!ppguard_plan_v1_gll_parse_prepared(
            pack, start_state, NULL, guard_plan, &result.gll_relation,
            &exec_plan->gll,
            limits->parse_work_limit, limits->replay_depth,
            limits->result_limit, &result.gll, &result.gll_receipt,
            error_buf, error_buf_size) ||
        !ppguard_plan_v1_glr_parse_prepared(
            pack, start_state, NULL, guard_plan, &result.glr_relation,
            &exec_plan->glr,
            limits->parse_work_limit, limits->replay_depth,
            limits->result_limit, &result.glr, &result.glr_receipt,
            error_buf, error_buf_size) ||
        result.gll_receipt.source_pass_count != view->source_pass_count ||
        result.glr_receipt.source_pass_count != view->source_pass_count ||
        !ppguard_scalar_exec_v1_results_equal(
            &result.gll, &result.glr)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguard_scalar_exec_v1_set_error(
                error_buf, error_buf_size,
                "GLL and GLR scalar guarded parses differ");
        }
        goto done;
    }
    ppguard_scalar_exec_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppguard_scalar_exec_v1_result_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguard_scalar_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to execute scalar positive-guard ParserPack");
    }
    return ok;
}
