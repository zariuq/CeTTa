#include "parser_atom_projection_closure_v1.h"

#include "parser_atom_projection_domain_v1.h"
#include "regular_span_nfa_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ppclosure_v1_set_error(char *buf,
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

static char *ppclosure_v1_strdup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static bool ppclosure_v1_expr_head(const Atom *atom,
                                   const char *head,
                                   CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static const char *ppclosure_v1_root_label(const Atom *tag,
                                           bool *is_reference) {
    const Atom *label;

    *is_reference = false;
    if (ppclosure_v1_expr_head(tag, "semantic-text-ref", 1u)) {
        *is_reference = true;
        return NULL;
    }
    if (!ppclosure_v1_expr_head(tag, "semantic-text", 1u))
        return NULL;
    label = tag->expr.elems[1];
    if (!label || label->kind != ATOM_SYMBOL)
        return NULL;
    return atom_name_cstr((Atom *)label);
}

void ppatom_projection_closure_v1_result_init(
    PPAtomProjectionClosureV1Result *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    result->stopped_wrapper = UINT32_MAX;
}

void ppatom_projection_closure_v1_result_free(
    PPAtomProjectionClosureV1Result *result) {
    uint32_t index;

    if (!result)
        return;
    if (result->rows) {
        for (index = 0u; index < result->wrapper_len; index++) {
            free(result->rows[index].label);
            free(result->rows[index].counterexample);
        }
    }
    free(result->rows);
    memset(result, 0, sizeof(*result));
    result->stopped_wrapper = UINT32_MAX;
}

static bool ppclosure_v1_match_roots(
    const RSNFAV1Plan *source_nfa,
    const PPAtomProjectionV1Plan *projection,
    uint32_t wrapper_len,
    uint32_t *wrapper_tags,
    uint32_t *out_root_len,
    char *error_buf,
    size_t error_buf_size) {
    bool *root_used = NULL;
    uint32_t root_len = 0u;
    uint32_t tag_index;
    uint32_t wrapper_index;
    bool ok = false;

    root_used = calloc(
        source_nfa->nfa.tag_len ? source_nfa->nfa.tag_len : 1u,
        sizeof(*root_used));
    if (!root_used) {
        ppclosure_v1_set_error(error_buf, error_buf_size,
                               "failed to allocate wrapper correspondence");
        goto done;
    }
    for (tag_index = 0u; tag_index < source_nfa->nfa.tag_len; tag_index++) {
        bool is_reference;
        const char *label = ppclosure_v1_root_label(
            source_nfa->tags[tag_index], &is_reference);
        if (is_reference)
            continue;
        if (!label) {
            ppclosure_v1_set_error(
                error_buf, error_buf_size,
                "semantic-text NFA contains an unknown tag shape");
            goto done;
        }
        root_len++;
    }
    if (root_len != wrapper_len) {
        ppclosure_v1_set_error(
            error_buf, error_buf_size,
            "semantic wrapper roots and projection wrappers are not bijective");
        goto done;
    }
    for (wrapper_index = 0u; wrapper_index < wrapper_len; wrapper_index++) {
        PPAtomProjectionV1WrapperView wrapper;
        uint32_t match = UINT32_MAX;

        if (!ppatom_projection_v1_plan_wrapper(
                projection, wrapper_index, &wrapper,
                error_buf, error_buf_size)) {
            goto done;
        }
        for (tag_index = 0u;
             tag_index < source_nfa->nfa.tag_len;
             tag_index++) {
            bool is_reference;
            const char *label = ppclosure_v1_root_label(
                source_nfa->tags[tag_index], &is_reference);
            if (!is_reference && label && strcmp(label, wrapper.label) == 0) {
                if (match != UINT32_MAX) {
                    ppclosure_v1_set_error(
                        error_buf, error_buf_size,
                        "duplicate semantic wrapper root label");
                    goto done;
                }
                match = tag_index;
            }
        }
        if (match == UINT32_MAX || root_used[match]) {
            ppclosure_v1_set_error(
                error_buf, error_buf_size,
                "projection wrapper lacks a unique semantic wrapper root");
            goto done;
        }
        root_used[match] = true;
        wrapper_tags[wrapper_index] = match;
    }
    *out_root_len = root_len;
    ok = true;

done:
    free(root_used);
    return ok;
}

static PPAtomProjectionClosureV1Outcome ppclosure_v1_source_outcome(
    RSDFAV1BuildOutcome outcome) {
    return outcome == RSDFA_V1_BUILD_STATE_LIMIT
        ? PPATOM_CLOSURE_V1_SOURCE_STATE_LIMIT
        : PPATOM_CLOSURE_V1_SOURCE_TRANSITION_LIMIT;
}

static PPAtomProjectionClosureV1Outcome ppclosure_v1_domain_outcome(
    RSDFAV1BuildOutcome outcome) {
    return outcome == RSDFA_V1_BUILD_STATE_LIMIT
        ? PPATOM_CLOSURE_V1_DOMAIN_STATE_LIMIT
        : PPATOM_CLOSURE_V1_DOMAIN_TRANSITION_LIMIT;
}

bool ppatom_projection_closure_v1_check(
    const PPABIV1Pack *class_pack,
    Atom *const *semantic_nfa_answers,
    size_t semantic_nfa_answer_len,
    const PPAtomProjectionV1Plan *projection,
    uint32_t state_limit,
    uint32_t transition_limit,
    uint32_t pair_limit,
    PPAtomProjectionClosureV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionClosureV1Result result;
    RSNFAV1Plan source_nfa;
    RSDFAV1Plan source_dfa;
    RSDFAV1Program source_program;
    uint32_t *wrapper_tags = NULL;
    uint32_t wrapper_len = 0u;
    uint32_t root_len = 0u;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_COMPLETED;
    uint32_t wrapper_index;
    bool ok = false;

    ppatom_projection_closure_v1_result_init(&result);
    rsnfa_v1_plan_init(&source_nfa);
    rsdfa_v1_plan_init(&source_dfa);
    rsdfa_v1_program_init(&source_program);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!class_pack || !semantic_nfa_answers ||
        semantic_nfa_answer_len == 0u || !projection || !out ||
        state_limit == 0u || transition_limit == 0u || pair_limit == 0u ||
        !ppatom_projection_v1_plan_wrapper_count(
            projection, &wrapper_len, error_buf, error_buf_size) ||
        wrapper_len == 0u) {
        if (!error_buf || !error_buf[0]) {
            ppclosure_v1_set_error(error_buf, error_buf_size,
                                   "bad projection-closure arguments");
        }
        goto done;
    }
    result.wrapper_len = wrapper_len;
    wrapper_tags = malloc(sizeof(*wrapper_tags) * (size_t)wrapper_len);
    result.rows = calloc(wrapper_len, sizeof(*result.rows));
    if (!wrapper_tags || !result.rows) {
        ppclosure_v1_set_error(error_buf, error_buf_size,
                               "failed to allocate projection closure");
        goto done;
    }
    if (!rsnfa_v1_plan_load(
            class_pack, semantic_nfa_answers, semantic_nfa_answer_len,
            &source_nfa, error_buf, error_buf_size) ||
        !ppclosure_v1_match_roots(
            &source_nfa, projection, wrapper_len, wrapper_tags, &root_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    result.source_root_tag_len = root_len;
    if (!rsdfa_v1_plan_build(
            &source_nfa.nfa, state_limit, transition_limit,
            &source_dfa, &build_outcome, error_buf, error_buf_size)) {
        goto done;
    }
    if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
        result.outcome = ppclosure_v1_source_outcome(build_outcome);
        ok = true;
        goto publish;
    }
    result.source_state_len = source_dfa.state_len;
    result.source_transition_len = source_dfa.transition_len;
    if (!rsdfa_v1_plan_export_program(
            &source_dfa, &source_program,
            error_buf, error_buf_size)) {
        goto done;
    }
    result.all_included = true;
    for (wrapper_index = 0u; wrapper_index < wrapper_len; wrapper_index++) {
        PPAtomProjectionV1WrapperView wrapper;
        RSDFAV1Program domain_program;
        RSDFAV1InclusionResult inclusion;

        rsdfa_v1_program_init(&domain_program);
        rsdfa_v1_inclusion_result_init(&inclusion);
        if (!ppatom_projection_v1_plan_wrapper(
                projection, wrapper_index, &wrapper,
                error_buf, error_buf_size)) {
            goto wrapper_done;
        }
        result.rows[wrapper_index].label = ppclosure_v1_strdup(wrapper.label);
        result.rows[wrapper_index].source_tag = wrapper_tags[wrapper_index];
        if (!result.rows[wrapper_index].label) {
            ppclosure_v1_set_error(error_buf, error_buf_size,
                                   "failed to copy wrapper label");
            goto wrapper_done;
        }
        if (!ppatom_projection_domain_v1_program(
                projection, wrapper_index, state_limit, transition_limit,
                &domain_program, &build_outcome,
                error_buf, error_buf_size)) {
            goto wrapper_done;
        }
        if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
            result.outcome = ppclosure_v1_domain_outcome(build_outcome);
            result.stopped_wrapper = wrapper_index;
            ok = true;
            goto resource_done;
        }
        if (!rsdfa_v1_program_tag_inclusion(
                &source_program, wrapper_tags[wrapper_index],
                &domain_program, 0u, pair_limit,
                &inclusion, error_buf, error_buf_size)) {
            goto wrapper_done;
        }
        if (inclusion.outcome != RSDFA_V1_INCLUSION_COMPLETED) {
            result.outcome = PPATOM_CLOSURE_V1_INCLUSION_PAIR_LIMIT;
            result.stopped_wrapper = wrapper_index;
            ok = true;
            goto resource_done;
        }
        result.rows[wrapper_index].included = inclusion.included;
        result.rows[wrapper_index].counterexample = inclusion.counterexample;
        result.rows[wrapper_index].counterexample_len =
            inclusion.counterexample_len;
        result.rows[wrapper_index].explored_pair_len =
            inclusion.explored_pair_len;
        inclusion.counterexample = NULL;
        result.row_len++;
        if (!inclusion.included)
            result.all_included = false;
        rsdfa_v1_inclusion_result_free(&inclusion);
        rsdfa_v1_program_free(&domain_program);
        continue;

resource_done:
        rsdfa_v1_inclusion_result_free(&inclusion);
        rsdfa_v1_program_free(&domain_program);
        goto publish;

wrapper_done:
        rsdfa_v1_inclusion_result_free(&inclusion);
        rsdfa_v1_program_free(&domain_program);
        goto done;
    }
    result.outcome = PPATOM_CLOSURE_V1_COMPLETED;
    ok = true;

publish:
    ppatom_projection_closure_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    result.stopped_wrapper = UINT32_MAX;

done:
    free(wrapper_tags);
    rsdfa_v1_program_free(&source_program);
    rsdfa_v1_plan_free(&source_dfa);
    rsnfa_v1_plan_free(&source_nfa);
    ppatom_projection_closure_v1_result_free(&result);
    return ok;
}
