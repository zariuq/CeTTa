#include "parser_occurrence_span_mask_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void pposm_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool pposm_v1_digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool pposm_v1_expr_head(
    const Atom *term, const char *head, CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == argument_len + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static char *pposm_v1_render(const Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(
            term, &bytes, &len, NULL, 0u)) {
        return NULL;
    }
    return (char *)bytes;
}

static int pposm_v1_text_compare(
    const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static bool pposm_v1_answer_set_digest(
    Atom *const *terms, size_t term_len, char out[65]) {
    static const char domain[] = "FiniteHornAnswerSetV1\n";
    CettaNativeSha256 sha;
    char **canonical = NULL;
    size_t index;
    bool ok = false;

    if ((!terms && term_len > 0u) || term_len > UINT32_MAX)
        return false;
    canonical = calloc(term_len ? term_len : 1u, sizeof(*canonical));
    if (!canonical)
        return false;
    for (index = 0u; index < term_len; index++) {
        canonical[index] = pposm_v1_render(terms[index]);
        if (!canonical[index])
            goto done;
    }
    if (term_len > 1u) {
        qsort(canonical, term_len, sizeof(*canonical),
              pposm_v1_text_compare);
    }
    for (index = 1u; index < term_len; index++) {
        if (strcmp(canonical[index - 1u], canonical[index]) == 0)
            goto done;
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    for (index = 0u; index < term_len; index++) {
        cetta_native_sha256_update(
            &sha, (const uint8_t *)canonical[index],
            strlen(canonical[index]));
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    ok = true;

done:
    for (index = 0u; index < term_len; index++)
        free(canonical[index]);
    free(canonical);
    return ok;
}

static void pposm_v1_sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void pposm_v1_sha_text(
    CettaNativeSha256 *sha, const char *text) {
    uint64_t len = (uint64_t)strlen(text);
    uint8_t bytes[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        bytes[7u - index] = (uint8_t)(len >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, (size_t)len);
}

static bool pposm_v1_plan_digest(
    const PPOccurrenceSpanMaskV1Plan *plan, char out[65]) {
    static const char domain[] = "ParserOccurrenceSpanMaskPlanV1";
    CettaNativeSha256 sha;
    uint32_t index;

    if (!plan)
        return false;
    cetta_native_sha256_init(&sha);
    pposm_v1_sha_text(&sha, domain);
    pposm_v1_sha_text(&sha, plan->base_pack_digest);
    pposm_v1_sha_text(&sha, plan->cursor_program_digest);
    pposm_v1_sha_text(&sha, plan->occurrence_fold_plan_digest);
    pposm_v1_sha_text(&sha, plan->compiler_digest);
    pposm_v1_sha_text(&sha, plan->answer_set_digest);
    pposm_v1_sha_u32(&sha, plan->terminal_len);
    pposm_v1_sha_u32(&sha, plan->bound_terminal_len);
    pposm_v1_sha_u32(&sha, plan->retention.dfa.state_len);
    pposm_v1_sha_u32(&sha, plan->retention.dfa.transition_len);
    pposm_v1_sha_u32(&sha, plan->retention.dfa.accept_tag_len);
    pposm_v1_sha_u32(&sha, plan->retention.dfa.eof_accept_tag_len);
    pposm_v1_sha_u32(&sha, plan->retention.dfa.start_state);
    pposm_v1_sha_u32(&sha, plan->retention.dfa.tag_len);
    pposm_v1_sha_u32(&sha, plan->retention.action_len);
    for (index = 0u; index < plan->retention.dfa.state_len; index++) {
        const RSDFAV1ProgramState *state =
            &plan->retention.dfa.states[index];
        pposm_v1_sha_u32(&sha, state->transition_begin);
        pposm_v1_sha_u32(&sha, state->transition_len);
        pposm_v1_sha_u32(&sha, state->accept_begin);
        pposm_v1_sha_u32(&sha, state->accept_len);
        pposm_v1_sha_u32(&sha, state->eof_accept_begin);
        pposm_v1_sha_u32(&sha, state->eof_accept_len);
    }
    for (index = 0u; index < plan->retention.dfa.transition_len; index++) {
        const RSDFAV1ProgramTransition *transition =
            &plan->retention.dfa.transitions[index];
        pposm_v1_sha_u32(&sha, transition->low);
        pposm_v1_sha_u32(&sha, transition->high);
        pposm_v1_sha_u32(&sha, transition->target);
        pposm_v1_sha_u32(
            &sha, plan->retention.transition_actions[index]);
    }
    for (index = 0u; index < plan->retention.dfa.accept_tag_len; index++)
        pposm_v1_sha_u32(&sha, plan->retention.dfa.accept_tags[index]);
    for (index = 0u;
         index < plan->retention.dfa.eof_accept_tag_len; index++) {
        pposm_v1_sha_u32(
            &sha, plan->retention.dfa.eof_accept_tags[index]);
    }
    for (index = 0u; index < plan->terminal_len; index++) {
        pposm_v1_sha_u32(&sha, plan->terminal_tags[index]);
        pposm_v1_sha_u32(
            &sha, plan->terminal_value_production_labels[index]);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

void ppoccurrence_span_mask_v1_plan_init(
    PPOccurrenceSpanMaskV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    ppsemantic_mask_dfa_v1_program_init(&plan->retention);
}

void ppoccurrence_span_mask_v1_plan_free(
    PPOccurrenceSpanMaskV1Plan *plan) {
    if (!plan)
        return;
    ppsemantic_mask_dfa_v1_program_free(&plan->retention);
    free(plan->terminal_tags);
    free(plan->terminal_value_production_labels);
    memset(plan, 0, sizeof(*plan));
}

static bool pposm_v1_state_is_definition(
    const PPABIV1Pack *pack, uint32_t state_id, const Atom *definition) {
    const Atom *identity;

    if (!pack || state_id >= pack->state_len || !definition)
        return false;
    identity = pack->states[state_id].identity;
    return pposm_v1_expr_head(identity, "pp-def", 1u) &&
        atom_eq(identity->expr.elems[1], (Atom *)definition);
}

static bool pposm_v1_production_constructs_label(
    const PPABIV1Pack *pack, uint32_t production_id, const Atom *label) {
    const Atom *action;
    const Atom *arguments;
    const Atom *first;
    const Atom *rest;

    if (!pack || production_id >= pack->production_len || !label)
        return false;
    action = pack->productions[production_id].action;
    if (!pposm_v1_expr_head(action, "pa-apply", 2u) ||
        !atom_is_symbol(action->expr.elems[1], "node")) {
        return false;
    }
    arguments = action->expr.elems[2];
    if (!pposm_v1_expr_head(arguments, "pa-cons", 2u))
        return false;
    first = arguments->expr.elems[1];
    rest = arguments->expr.elems[2];
    return pposm_v1_expr_head(first, "pa-const", 1u) &&
        atom_eq(first->expr.elems[1], (Atom *)label) &&
        pposm_v1_expr_head(rest, "pa-cons", 2u) &&
        atom_is_symbol(rest->expr.elems[2], "pa-nil");
}

static bool pposm_v1_languages_equal(
    const RSDFAV1Program *left, uint32_t left_tag,
    const RSDFAV1Program *right, uint32_t right_tag,
    char *error_buf, size_t error_buf_size) {
    RSDFAV1InclusionResult inclusion;
    uint64_t pair_bound;
    bool ok = false;

    rsdfa_v1_inclusion_result_init(&inclusion);
    pair_bound = ((uint64_t)left->state_len + 1u) *
        ((uint64_t)right->state_len + 1u);
    if (pair_bound == 0u || pair_bound > UINT32_MAX) {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "occurrence span-mask equivalence product is too large");
        goto done;
    }
    if (!rsdfa_v1_program_tag_inclusion(
            left, left_tag, right, right_tag, (uint32_t)pair_bound,
            &inclusion, error_buf, error_buf_size) ||
        inclusion.outcome != RSDFA_V1_INCLUSION_COMPLETED ||
        !inclusion.included) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "occurrence span mask is not included in recognition");
        }
        goto done;
    }
    rsdfa_v1_inclusion_result_free(&inclusion);
    rsdfa_v1_inclusion_result_init(&inclusion);
    if (!rsdfa_v1_program_tag_inclusion(
            right, right_tag, left, left_tag, (uint32_t)pair_bound,
            &inclusion, error_buf, error_buf_size) ||
        inclusion.outcome != RSDFA_V1_INCLUSION_COMPLETED ||
        !inclusion.included) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "recognition is not included in occurrence span mask");
        }
        goto done;
    }
    ok = true;

done:
    rsdfa_v1_inclusion_result_free(&inclusion);
    return ok;
}

bool ppoccurrence_span_mask_v1_plan_validate(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    const PPOccurrenceSpanMaskV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t bound = 0u;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !fold || !plan ||
        !ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !ppoccurrence_fold_v1_plan_validate_program(
            program, fold, error_buf, error_buf_size) ||
        !ppsemantic_mask_dfa_v1_program_validate(
            &plan->retention, error_buf, error_buf_size) ||
        plan->retention.action_len !=
            PPOCCURRENCE_SPAN_MASK_V1_ACTION_LEN ||
        plan->terminal_len != program->terminal_len ||
        plan->terminal_len != fold->cursor_terminal_len ||
        plan->bound_terminal_len == 0u ||
        !plan->terminal_tags ||
        !plan->terminal_value_production_labels ||
        !pposm_v1_digest_valid(plan->base_pack_digest) ||
        !pposm_v1_digest_valid(plan->cursor_program_digest) ||
        !pposm_v1_digest_valid(plan->occurrence_fold_plan_digest) ||
        !pposm_v1_digest_valid(plan->compiler_digest) ||
        !pposm_v1_digest_valid(plan->answer_set_digest) ||
        !pposm_v1_digest_valid(plan->plan_digest) ||
        strcmp(plan->base_pack_digest, fold->base_pack_digest) != 0 ||
        strcmp(plan->cursor_program_digest, program->program_digest) != 0 ||
        strcmp(
            plan->occurrence_fold_plan_digest, fold->plan_digest) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "invalid occurrence span-mask plan header");
        }
        return false;
    }
    for (index = 0u; index < plan->terminal_len; index++) {
        uint32_t tag = plan->terminal_tags[index];
        uint32_t production =
            plan->terminal_value_production_labels[index];
        uint32_t binding_index = fold->terminal_binding_by_source[index];
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];
        uint32_t prior;

        if (tag == UINT32_MAX) {
            if (production != UINT32_MAX)
                goto malformed_mapping;
            continue;
        }
        if (tag >= plan->retention.dfa.tag_len ||
            production == UINT32_MAX ||
            binding_index == UINT32_MAX ||
            binding_index >= fold->terminal_len ||
            fold->terminals[binding_index].source_index != index ||
            fold->terminals[binding_index].value_production_label !=
                production ||
            terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR ||
            terminal->dfa_tag >= program->dfa.tag_len ||
            !pposm_v1_languages_equal(
                &plan->retention.dfa, tag,
                &program->dfa, terminal->dfa_tag,
                error_buf, error_buf_size)) {
            goto malformed_mapping;
        }
        for (prior = 0u; prior < index; prior++) {
            if (plan->terminal_tags[prior] == tag)
                goto malformed_mapping;
        }
        bound++;
    }
    if (bound != plan->bound_terminal_len ||
        !pposm_v1_plan_digest(plan, digest) ||
        strcmp(digest, plan->plan_digest) != 0) {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "occurrence span-mask plan digest or count changed");
        return false;
    }
    return true;

malformed_mapping:
    if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "occurrence span-mask terminal mapping is invalid");
    }
    return false;
}

bool ppoccurrence_span_mask_v1_plan_build(
    const PPABIV1Pack *pack,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    Atom *const *answer_terms,
    size_t answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    uint32_t state_limit,
    uint32_t transition_limit,
    PPOccurrenceSpanMaskV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceSpanMaskV1Plan result;
    PPSemanticMaskNfaV1Plan mask;
    RSDFAV1BuildOutcome outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    uint32_t *quotient = NULL;
    uint32_t semantic_terminal_len = 0u;
    char computed_answers[65];
    uint32_t index;
    bool ok = false;

    ppoccurrence_span_mask_v1_plan_init(&result);
    ppsemantic_mask_nfa_v1_plan_init(&mask);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !program || !fold || !answer_terms || answer_len == 0u ||
        answer_len > UINT32_MAX || !out || state_limit == 0u ||
        transition_limit == 0u ||
        !pposm_v1_digest_valid(compiler_digest) ||
        !pposm_v1_digest_valid(answer_set_digest) ||
        !ppoccurrence_fold_v1_plan_validate_program(
            program, fold, error_buf, error_buf_size) ||
        strcmp(fold->base_pack_digest, pack->pack_digest) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "bad occurrence span-mask build inputs");
        }
        goto done;
    }
    if (!pposm_v1_answer_set_digest(
            answer_terms, answer_len, computed_answers) ||
        strcmp(computed_answers, answer_set_digest) != 0 ||
        !ppsemantic_mask_nfa_v1_plan_load(
            pack, answer_terms, answer_len, &mask,
            error_buf, error_buf_size) ||
        mask.root_link_len == 0u ||
        mask.root_link_len != mask.erased.nfa.tag_len ||
        (size_t)mask.action_len > SIZE_MAX / sizeof(*quotient)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "invalid occurrence span-mask compiler answers");
        }
        goto done;
    }
    quotient = malloc(
        sizeof(*quotient) * (size_t)(mask.action_len
            ? mask.action_len : 1u));
    if (!quotient)
        goto done;
    for (index = 0u; index < mask.action_len; index++) {
        quotient[index] = mask.actions[index].kind ==
                PPSEMANTIC_MASK_V1_DROP
            ? PPOCCURRENCE_SPAN_MASK_V1_DROP
            : PPOCCURRENCE_SPAN_MASK_V1_RETAIN;
    }
    if (!ppsemantic_mask_dfa_v1_program_build_quotient(
            &mask, quotient, PPOCCURRENCE_SPAN_MASK_V1_ACTION_LEN,
            state_limit, transition_limit, &result.retention,
            &outcome, error_buf, error_buf_size) ||
        outcome != RSDFA_V1_BUILD_COMPLETED) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "occurrence span mask is not retention-local");
        }
        goto done;
    }
    result.terminal_len = program->terminal_len;
    result.terminal_tags = malloc(
        sizeof(*result.terminal_tags) *
        (size_t)(result.terminal_len ? result.terminal_len : 1u));
    result.terminal_value_production_labels = malloc(
        sizeof(*result.terminal_value_production_labels) *
        (size_t)(result.terminal_len ? result.terminal_len : 1u));
    if (!result.terminal_tags ||
        !result.terminal_value_production_labels) {
        goto done;
    }
    for (index = 0u; index < result.terminal_len; index++) {
        uint32_t binding_index = fold->terminal_binding_by_source[index];
        result.terminal_tags[index] = UINT32_MAX;
        result.terminal_value_production_labels[index] = UINT32_MAX;
        if (binding_index != UINT32_MAX &&
            binding_index < fold->terminal_len &&
            fold->terminals[binding_index].value_production_label !=
                UINT32_MAX) {
            semantic_terminal_len++;
        }
    }
    for (index = 0u; index < mask.root_link_len; index++) {
        const PPSemanticMaskV1RootLink *link = &mask.root_links[index];
        uint32_t source_index = UINT32_MAX;
        uint32_t source_matches = 0u;
        uint32_t scan;
        uint32_t binding_index;
        uint32_t production;

        for (scan = 0u; scan < program->terminal_len; scan++) {
            const PPGuardedLexCursorV1Terminal *terminal =
                &program->terminals[scan];
            if (terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR ||
                terminal->semantic_start_state_id >= pack->state_len ||
                !pposm_v1_state_is_definition(
                    pack, terminal->semantic_start_state_id,
                    link->definition)) {
                continue;
            }
            source_index = scan;
            source_matches++;
        }
        if (source_matches != 1u || source_index == UINT32_MAX ||
            result.terminal_tags[source_index] != UINT32_MAX ||
            (binding_index = fold->terminal_binding_by_source[
                 source_index]) == UINT32_MAX ||
            binding_index >= fold->terminal_len ||
            (production = fold->terminals[
                 binding_index].value_production_label) == UINT32_MAX ||
            !pposm_v1_production_constructs_label(
                pack, production, link->label)) {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask root link does not select one fold token");
            goto done;
        }
        result.terminal_tags[source_index] = link->tag;
        result.terminal_value_production_labels[source_index] = production;
        result.bound_terminal_len++;
    }
    if (result.bound_terminal_len != semantic_terminal_len) {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "occurrence span masks do not cover every semantic fold token");
        goto done;
    }
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.cursor_program_digest, program->program_digest, 65u);
    memcpy(result.occurrence_fold_plan_digest, fold->plan_digest, 65u);
    memcpy(result.compiler_digest, compiler_digest, 65u);
    memcpy(result.answer_set_digest, answer_set_digest, 65u);
    if (!pposm_v1_plan_digest(&result, result.plan_digest) ||
        !ppoccurrence_span_mask_v1_plan_validate(
            program, fold, &result, error_buf, error_buf_size)) {
        goto done;
    }
    ppoccurrence_span_mask_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(quotient);
    ppsemantic_mask_nfa_v1_plan_free(&mask);
    ppoccurrence_span_mask_v1_plan_free(&result);
    return ok;
}

bool ppoccurrence_span_mask_v1_project_prevalidated(
    const PPOccurrenceSpanMaskV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t terminal_index,
    uint32_t left,
    uint32_t right,
    uint64_t work_limit,
    uint32_t *scratch_actions,
    uint32_t scratch_capacity,
    PPOccurrenceSpanMaskV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceSpanMaskV1Result result = {
        .outcome = PPOCCURRENCE_SPAN_MASK_V1_UNHANDLED,
        .left_scalar = left,
        .right_scalar = left,
        .value_production_label = UINT32_MAX,
    };
    PPSemanticMaskDfaV1RunResult run;
    uint32_t tag;
    uint32_t index;
    uint32_t retained_left = UINT32_MAX;
    uint32_t retained_right = UINT32_MAX;
    bool closed = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        *out = result;
    if (!plan || !view || !out || terminal_index >= plan->terminal_len ||
        left > right || right > view->scalar_len || work_limit == 0u ||
        scratch_capacity < right - left ||
        (right > left && !scratch_actions)) {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "bad occurrence span-mask projection inputs");
        return false;
    }
    tag = plan->terminal_tags[terminal_index];
    if (tag == UINT32_MAX) {
        *out = result;
        return true;
    }
    if (!ppsemantic_mask_dfa_v1_run_exact_prevalidated(
            &plan->retention, view, left, right, tag, work_limit,
            scratch_actions, scratch_capacity, &run,
            error_buf, error_buf_size)) {
        return false;
    }
    result.work_item_len = run.work_item_len;
    if (run.outcome == PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT) {
        result.outcome = PPOCCURRENCE_SPAN_MASK_V1_WORK_LIMIT;
        *out = result;
        return true;
    }
    if (run.outcome != PPSEMANTIC_MASK_DFA_V1_ACCEPTED ||
        run.action_len != right - left) {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "selected token disagrees with its occurrence span mask");
        return false;
    }
    for (index = 0u; index < run.action_len; index++) {
        uint32_t action = scratch_actions[index];
        if (action == PPOCCURRENCE_SPAN_MASK_V1_RETAIN) {
            if (closed) {
                pposm_v1_set_error(
                    error_buf, error_buf_size,
                    "occurrence span mask retains disjoint intervals");
                return false;
            }
            if (retained_left == UINT32_MAX)
                retained_left = left + index;
            retained_right = left + index + 1u;
        } else if (action == PPOCCURRENCE_SPAN_MASK_V1_DROP) {
            if (retained_left != UINT32_MAX)
                closed = true;
        } else {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "occurrence span mask emitted an unknown quotient action");
            return false;
        }
    }
    if (retained_left == UINT32_MAX || retained_right == UINT32_MAX ||
        retained_left >= retained_right) {
        pposm_v1_set_error(
            error_buf, error_buf_size,
            "occurrence span mask retained no source interval");
        return false;
    }
    result.outcome = PPOCCURRENCE_SPAN_MASK_V1_ACCEPTED;
    result.left_scalar = retained_left;
    result.right_scalar = retained_right;
    result.value_production_label =
        plan->terminal_value_production_labels[terminal_index];
    *out = result;
    return true;
}

static bool pposm_v1_identifier_valid(const char *value) {
    size_t index;

    if (!value || !value[0] ||
        !((value[0] >= 'A' && value[0] <= 'Z') ||
          (value[0] >= 'a' && value[0] <= 'z') || value[0] == '_')) {
        return false;
    }
    for (index = 1u; value[index]; index++) {
        if (!((value[index] >= 'A' && value[index] <= 'Z') ||
              (value[index] >= 'a' && value[index] <= 'z') ||
              (value[index] >= '0' && value[index] <= '9') ||
              value[index] == '_')) {
            return false;
        }
    }
    return true;
}

static bool pposm_v1_emit_u32_array(
    FILE *output, const char *prefix, const char *suffix,
    const uint32_t *values, uint32_t len) {
    uint32_t index;

    if (fprintf(
            output, "static const uint32_t %s_%s[%u] = {\n    ",
            prefix, suffix, len ? len : 1u) < 0) {
        return false;
    }
    if (len == 0u && fprintf(output, "UINT32_C(0)") < 0)
        return false;
    for (index = 0u; index < len; index++) {
        if (index > 0u) {
            const char *separator = index % 6u == 0u ? ",\n    " : ", ";
            if (fprintf(output, "%s", separator) < 0)
                return false;
        }
        if (values[index] == UINT32_MAX) {
            if (fprintf(output, "UINT32_MAX") < 0)
                return false;
        } else if (fprintf(output, "UINT32_C(%u)", values[index]) < 0) {
            return false;
        }
    }
    return fprintf(output, "\n};\n\n") >= 0;
}

bool ppoccurrence_span_mask_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    const PPOccurrenceSpanMaskV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!output || !pposm_v1_identifier_valid(identifier_prefix) ||
        !ppoccurrence_span_mask_v1_plan_validate(
            program, fold, plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposm_v1_set_error(
                error_buf, error_buf_size,
                "bad occurrence span-mask C emission inputs");
        }
        return false;
    }
    if (fprintf(
            output,
            "\n/* Generated from ParserOccurrenceSpanMaskPlanV1. */\n"
            "#include \"parser_occurrence_span_mask_v1.h\"\n\n"
            "const char *%s_occurrence_span_mask_plan_digest(void) {\n"
            "    return \"%s\";\n}\n\n",
            identifier_prefix, plan->plan_digest) < 0) {
        goto write_error;
    }
    if (fprintf(
            output,
            "static const RSDFAV1ProgramState %s_occurrence_span_mask_states[%u] = {\n",
            identifier_prefix,
            plan->retention.dfa.state_len
                ? plan->retention.dfa.state_len : 1u) < 0) {
        goto write_error;
    }
    for (index = 0u; index < plan->retention.dfa.state_len; index++) {
        const RSDFAV1ProgramState *state =
            &plan->retention.dfa.states[index];
        if (fprintf(
                output,
                "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), UINT32_C(%u) },\n",
                state->transition_begin, state->transition_len,
                state->accept_begin, state->accept_len,
                state->eof_accept_begin, state->eof_accept_len) < 0) {
            goto write_error;
        }
    }
    if (plan->retention.dfa.state_len == 0u &&
        fprintf(output, "    { 0 },\n") < 0) {
        goto write_error;
    }
    if (fprintf(output, "};\n\n") < 0)
        goto write_error;
    if (fprintf(
            output,
            "static const RSDFAV1ProgramTransition %s_occurrence_span_mask_transitions[%u] = {\n",
            identifier_prefix,
            plan->retention.dfa.transition_len
                ? plan->retention.dfa.transition_len : 1u) < 0) {
        goto write_error;
    }
    for (index = 0u; index < plan->retention.dfa.transition_len; index++) {
        const RSDFAV1ProgramTransition *transition =
            &plan->retention.dfa.transitions[index];
        if (fprintf(
                output,
                "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u) },\n",
                transition->low, transition->high,
                transition->target) < 0) {
            goto write_error;
        }
    }
    if (plan->retention.dfa.transition_len == 0u &&
        fprintf(output, "    { 0 },\n") < 0) {
        goto write_error;
    }
    if (fprintf(output, "};\n\n") < 0 ||
        !pposm_v1_emit_u32_array(
            output, identifier_prefix,
            "occurrence_span_mask_accept_tags",
            plan->retention.dfa.accept_tags,
            plan->retention.dfa.accept_tag_len) ||
        !pposm_v1_emit_u32_array(
            output, identifier_prefix,
            "occurrence_span_mask_eof_accept_tags",
            plan->retention.dfa.eof_accept_tags,
            plan->retention.dfa.eof_accept_tag_len) ||
        !pposm_v1_emit_u32_array(
            output, identifier_prefix,
            "occurrence_span_mask_transition_actions",
            plan->retention.transition_actions,
            plan->retention.transition_action_len) ||
        !pposm_v1_emit_u32_array(
            output, identifier_prefix,
            "occurrence_span_mask_terminal_tags",
            plan->terminal_tags, plan->terminal_len) ||
        !pposm_v1_emit_u32_array(
            output, identifier_prefix,
            "occurrence_span_mask_terminal_productions",
            plan->terminal_value_production_labels,
            plan->terminal_len)) {
        goto write_error;
    }
    if (fprintf(
            output,
            "static bool %s_occurrence_span_mask_copy(void **target, const void *source, size_t size, char *error_buf, size_t error_buf_size) {\n"
            "    void *copy;\n"
            "    if (!target || !source || size == 0u) return false;\n"
            "    copy = malloc(size);\n"
            "    if (!copy) { if (error_buf && error_buf_size > 0u) snprintf(error_buf, error_buf_size, \"cannot allocate generated occurrence span mask\"); return false; }\n"
            "    memcpy(copy, source, size); *target = copy; return true;\n"
            "}\n\n"
            "bool %s_occurrence_span_mask_plan_init(const PPGuardedLexCursorV1Program *program, const PPOccurrenceFoldV1Plan *fold, PPOccurrenceSpanMaskV1Plan *out, char *error_buf, size_t error_buf_size) {\n"
            "    PPOccurrenceSpanMaskV1Plan result;\n"
            "    ppoccurrence_span_mask_v1_plan_init(&result);\n"
            "    result.terminal_len = UINT32_C(%u);\n"
            "    result.bound_terminal_len = UINT32_C(%u);\n"
            "    result.retention.dfa.state_len = UINT32_C(%u);\n"
            "    result.retention.dfa.transition_len = UINT32_C(%u);\n"
            "    result.retention.dfa.accept_tag_len = UINT32_C(%u);\n"
            "    result.retention.dfa.eof_accept_tag_len = UINT32_C(%u);\n"
            "    result.retention.dfa.start_state = UINT32_C(%u);\n"
            "    result.retention.dfa.tag_len = UINT32_C(%u);\n"
            "    result.retention.transition_action_len = UINT32_C(%u);\n"
            "    result.retention.action_len = UINT32_C(%u);\n",
            identifier_prefix, identifier_prefix,
            plan->terminal_len, plan->bound_terminal_len,
            plan->retention.dfa.state_len,
            plan->retention.dfa.transition_len,
            plan->retention.dfa.accept_tag_len,
            plan->retention.dfa.eof_accept_tag_len,
            plan->retention.dfa.start_state,
            plan->retention.dfa.tag_len,
            plan->retention.transition_action_len,
            plan->retention.action_len) < 0) {
        goto write_error;
    }
#define PPOSM_EMIT_COPY(field, source, len, type) \
    do { \
        if (fprintf( \
                output, \
                "    if (!%s_occurrence_span_mask_copy((void **)&result.%s, %s_%s, sizeof(%s) * (size_t)UINT32_C(%u), error_buf, error_buf_size)) goto done;\n", \
                identifier_prefix, field, identifier_prefix, source, \
                type, len ? len : 1u) < 0) { \
            goto write_error; \
        } \
    } while (0)
    PPOSM_EMIT_COPY(
        "retention.dfa.states", "occurrence_span_mask_states",
        plan->retention.dfa.state_len, "RSDFAV1ProgramState");
    PPOSM_EMIT_COPY(
        "retention.dfa.transitions", "occurrence_span_mask_transitions",
        plan->retention.dfa.transition_len,
        "RSDFAV1ProgramTransition");
    PPOSM_EMIT_COPY(
        "retention.dfa.accept_tags", "occurrence_span_mask_accept_tags",
        plan->retention.dfa.accept_tag_len, "uint32_t");
    PPOSM_EMIT_COPY(
        "retention.dfa.eof_accept_tags",
        "occurrence_span_mask_eof_accept_tags",
        plan->retention.dfa.eof_accept_tag_len, "uint32_t");
    PPOSM_EMIT_COPY(
        "retention.transition_actions",
        "occurrence_span_mask_transition_actions",
        plan->retention.transition_action_len, "uint32_t");
    PPOSM_EMIT_COPY(
        "terminal_tags", "occurrence_span_mask_terminal_tags",
        plan->terminal_len, "uint32_t");
    PPOSM_EMIT_COPY(
        "terminal_value_production_labels",
        "occurrence_span_mask_terminal_productions",
        plan->terminal_len, "uint32_t");
#undef PPOSM_EMIT_COPY
    if (fprintf(
            output,
            "    memcpy(result.base_pack_digest, \"%s\", 65u);\n"
            "    memcpy(result.cursor_program_digest, \"%s\", 65u);\n"
            "    memcpy(result.occurrence_fold_plan_digest, \"%s\", 65u);\n"
            "    memcpy(result.compiler_digest, \"%s\", 65u);\n"
            "    memcpy(result.answer_set_digest, \"%s\", 65u);\n"
            "    memcpy(result.plan_digest, \"%s\", 65u);\n"
            "    if (!ppoccurrence_span_mask_v1_plan_validate(program, fold, &result, error_buf, error_buf_size)) goto done;\n"
            "    ppoccurrence_span_mask_v1_plan_free(out); *out = result; memset(&result, 0, sizeof(result)); return true;\n"
            "done:\n"
            "    ppoccurrence_span_mask_v1_plan_free(&result); return false;\n"
            "}\n",
            plan->base_pack_digest, plan->cursor_program_digest,
            plan->occurrence_fold_plan_digest, plan->compiler_digest,
            plan->answer_set_digest, plan->plan_digest) < 0) {
        goto write_error;
    }
    return true;

write_error:
    pposm_v1_set_error(
        error_buf, error_buf_size,
        "cannot emit occurrence span-mask C plan");
    return false;
}
