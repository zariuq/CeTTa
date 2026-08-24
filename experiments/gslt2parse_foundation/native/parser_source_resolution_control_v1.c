#include "parser_source_resolution_control_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void ppsrc_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppsrc_v1_digest_valid(const char *digest) {
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

static bool ppsrc_v1_expr_head(
    const Atom *term, const char *head, CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == argument_len + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static char *ppsrc_v1_render(const Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static int ppsrc_v1_text_compare(
    const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static bool ppsrc_v1_answer_set_digest(
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
        canonical[index] = ppsrc_v1_render(terms[index]);
        if (!canonical[index])
            goto done;
    }
    if (term_len > 1u) {
        qsort(canonical, term_len, sizeof(*canonical),
              ppsrc_v1_text_compare);
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

static uint32_t ppsrc_v1_decision_index(
    PPSourceControlProbeV1 probe,
    uint32_t completed_policy,
    uint32_t active_policy) {
    return ((uint32_t)probe * PPSOURCE_CONTROL_POLICY_V1_COUNT +
            completed_policy) * PPSOURCE_CONTROL_POLICY_V1_COUNT +
           active_policy;
}

static uint32_t ppsrc_v1_finish_index(
    PPSourceControlDecisionV1 decision,
    PPSourceControlChildV1 child) {
    return (uint32_t)decision * PPSOURCE_CONTROL_CHILD_V1_COUNT +
           (uint32_t)child;
}

static bool ppsrc_v1_finish_slot_required(
    PPSourceControlDecisionV1 decision,
    PPSourceControlChildV1 child) {
    if (decision == PPSOURCE_CONTROL_DECISION_V1_EXPAND_FRESH) {
        return child != PPSOURCE_CONTROL_CHILD_V1_NONE;
    }
    return child == PPSOURCE_CONTROL_CHILD_V1_NONE;
}

static void ppsrc_v1_sha_text(
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

static bool ppsrc_v1_plan_digest(
    const PPSourceResolutionControlV1Plan *plan, char out[65]) {
    static const char domain[] = "ParserSourceResolutionControlPlanV1";
    CettaNativeSha256 sha;

    if (!plan)
        return false;
    cetta_native_sha256_init(&sha);
    ppsrc_v1_sha_text(&sha, domain);
    ppsrc_v1_sha_text(&sha, plan->compiler_digest);
    ppsrc_v1_sha_text(&sha, plan->answer_set_digest);
    cetta_native_sha256_update(
        &sha, plan->decisions, sizeof(plan->decisions));
    cetta_native_sha256_update(
        &sha, plan->outcomes, sizeof(plan->outcomes));
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

void ppsource_resolution_control_v1_plan_init(
    PPSourceResolutionControlV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    memset(plan->decisions, PPSOURCE_CONTROL_ABSENT_V1,
           sizeof(plan->decisions));
    memset(plan->outcomes, PPSOURCE_CONTROL_ABSENT_V1,
           sizeof(plan->outcomes));
}

static bool ppsrc_v1_symbol_index(
    const Atom *term,
    const char *const *names,
    uint32_t name_len,
    uint32_t *out) {
    uint32_t index;

    if (!term || !out)
        return false;
    for (index = 0u; index < name_len; index++) {
        if (atom_is_symbol((Atom *)term, names[index])) {
            *out = index;
            return true;
        }
    }
    return false;
}

static bool ppsrc_v1_parse_decision_record(
    const Atom *record,
    PPSourceControlProbeV1 *probe_out,
    uint32_t *completed_out,
    uint32_t *active_out,
    PPSourceControlDecisionV1 *decision_out) {
    static const char *const probes[] = {
        "SourceProbeFreshKindV1",
        "SourceProbeCompletedKindV1",
        "SourceProbeActiveKindV1",
        "SourceProbeMissingKindV1",
        "SourceProbeResourceKindV1",
        "SourceProbeInvalidKindV1",
    };
    static const char *const decisions[] = {
        "SourceDecisionExpandFreshV1",
        "SourceDecisionSkipCompletedV1",
        "SourceDecisionSkipActiveV1",
        "SourceDecisionRefuseCompletedV1",
        "SourceDecisionRefuseActiveV1",
        "SourceDecisionRefuseMissingV1",
        "SourceDecisionResourceV1",
        "SourceDecisionInvalidV1",
    };
    const Atom *policies;
    uint32_t probe;
    uint32_t decision;

    if (!ppsrc_v1_expr_head(record, "SourceResolutionControlRecordV1", 4u) ||
        !atom_is_symbol(record->expr.elems[1],
                        "SourceControlDecisionRecordV1") ||
        !ppsrc_v1_symbol_index(
            record->expr.elems[2], probes,
            PPSOURCE_CONTROL_PROBE_V1_COUNT, &probe) ||
        !(policies = record->expr.elems[3]) ||
        !ppsrc_v1_expr_head(policies, "SourcePolicyPairV1", 2u) ||
        !ppsrc_v1_symbol_index(
            record->expr.elems[4], decisions,
            PPSOURCE_CONTROL_DECISION_V1_COUNT, &decision)) {
        return false;
    }
    if (atom_is_symbol(policies->expr.elems[1],
                       "SourceCompletedSkipV1")) {
        *completed_out = 1u;
    } else if (atom_is_symbol(policies->expr.elems[1],
                              "SourceCompletedRejectV1")) {
        *completed_out = 0u;
    } else {
        return false;
    }
    if (atom_is_symbol(policies->expr.elems[2],
                       "SourceActiveRejectV1")) {
        *active_out = 1u;
    } else if (atom_is_symbol(policies->expr.elems[2],
                              "SourceActiveSkipV1")) {
        *active_out = 0u;
    } else {
        return false;
    }
    *probe_out = (PPSourceControlProbeV1)probe;
    *decision_out = (PPSourceControlDecisionV1)decision;
    return true;
}

static bool ppsrc_v1_parse_finish_record(
    const Atom *record,
    PPSourceControlDecisionV1 *decision_out,
    PPSourceControlChildV1 *child_out,
    PPSourceControlOutcomeV1 *outcome_out) {
    static const char *const decisions[] = {
        "SourceDecisionExpandFreshV1",
        "SourceDecisionSkipCompletedV1",
        "SourceDecisionSkipActiveV1",
        "SourceDecisionRefuseCompletedV1",
        "SourceDecisionRefuseActiveV1",
        "SourceDecisionRefuseMissingV1",
        "SourceDecisionResourceV1",
        "SourceDecisionInvalidV1",
    };
    static const char *const children[] = {
        "SourceChildAcceptedV1",
        "SourceChildRefusedV1",
        "SourceChildResourceV1",
        "SourceChildInvalidV1",
        "SourceNoChildV1",
    };
    static const char *const outcomes[] = {
        "SourceOutcomeAcceptedV1",
        "SourceOutcomeRefusedV1",
        "SourceOutcomeResourceV1",
        "SourceOutcomeInvalidV1",
    };
    uint32_t decision;
    uint32_t child;
    uint32_t outcome;

    if (!ppsrc_v1_expr_head(record, "SourceResolutionControlRecordV1", 4u) ||
        !atom_is_symbol(record->expr.elems[1],
                        "SourceControlFinishRecordV1") ||
        !ppsrc_v1_symbol_index(
            record->expr.elems[2], decisions,
            PPSOURCE_CONTROL_DECISION_V1_COUNT, &decision) ||
        !ppsrc_v1_symbol_index(
            record->expr.elems[3], children,
            PPSOURCE_CONTROL_CHILD_V1_COUNT, &child) ||
        !ppsrc_v1_symbol_index(
            record->expr.elems[4], outcomes,
            PPSOURCE_CONTROL_OUTCOME_V1_COUNT, &outcome)) {
        return false;
    }
    *decision_out = (PPSourceControlDecisionV1)decision;
    *child_out = (PPSourceControlChildV1)child;
    *outcome_out = (PPSourceControlOutcomeV1)outcome;
    return true;
}

bool ppsource_resolution_control_v1_plan_validate(
    const PPSourceResolutionControlV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !ppsrc_v1_digest_valid(plan->compiler_digest) ||
        !ppsrc_v1_digest_valid(plan->answer_set_digest) ||
        !ppsrc_v1_digest_valid(plan->plan_digest)) {
        ppsrc_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution control plan has invalid provenance");
        return false;
    }
    for (index = 0u;
         index < PPSOURCE_CONTROL_DECISION_CELL_V1_COUNT; index++) {
        if (plan->decisions[index] >=
            PPSOURCE_CONTROL_DECISION_V1_COUNT) {
            ppsrc_v1_set_error(
                error_buf, error_buf_size,
                "source-resolution control decision table is incomplete");
            return false;
        }
    }
    for (index = 0u;
         index < PPSOURCE_CONTROL_FINISH_CELL_V1_COUNT; index++) {
        PPSourceControlDecisionV1 decision =
            (PPSourceControlDecisionV1)(
                index / PPSOURCE_CONTROL_CHILD_V1_COUNT);
        PPSourceControlChildV1 child =
            (PPSourceControlChildV1)(
                index % PPSOURCE_CONTROL_CHILD_V1_COUNT);
        bool required = ppsrc_v1_finish_slot_required(decision, child);
        uint8_t outcome = plan->outcomes[index];

        if ((required && outcome >= PPSOURCE_CONTROL_OUTCOME_V1_COUNT) ||
            (!required && outcome != PPSOURCE_CONTROL_ABSENT_V1)) {
            ppsrc_v1_set_error(
                error_buf, error_buf_size,
                "source-resolution control finish table has the wrong domain");
            return false;
        }
    }
    if (!ppsrc_v1_plan_digest(plan, digest) ||
        strcmp(digest, plan->plan_digest) != 0) {
        ppsrc_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution control plan digest does not reproduce");
        return false;
    }
    return true;
}

bool ppsource_resolution_control_v1_plan_build(
    Atom *const *answer_terms,
    size_t answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    PPSourceResolutionControlV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPSourceResolutionControlV1Plan result;
    char reproduced_answer_digest[65];
    size_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    ppsource_resolution_control_v1_plan_init(&result);
    if (!out || !answer_terms || answer_len != 35u ||
        !ppsrc_v1_digest_valid(compiler_digest) ||
        !ppsrc_v1_digest_valid(answer_set_digest) ||
        !ppsrc_v1_answer_set_digest(
            answer_terms, answer_len, reproduced_answer_digest) ||
        strcmp(reproduced_answer_digest, answer_set_digest) != 0) {
        ppsrc_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution control answer stream is not admitted");
        return false;
    }
    for (index = 0u; index < answer_len; index++) {
        const Atom *record = answer_terms[index];

        if (ppsrc_v1_expr_head(
                record, "SourceResolutionControlRecordV1", 4u) &&
            atom_is_symbol(record->expr.elems[1],
                           "SourceControlDecisionRecordV1")) {
            PPSourceControlProbeV1 probe;
            PPSourceControlDecisionV1 decision;
            uint32_t completed_policy;
            uint32_t active_policy;
            uint32_t slot;

            if (!ppsrc_v1_parse_decision_record(
                    record, &probe, &completed_policy,
                    &active_policy, &decision)) {
                ppsrc_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution decision record is malformed");
                return false;
            }
            slot = ppsrc_v1_decision_index(
                probe, completed_policy, active_policy);
            if (result.decisions[slot] != PPSOURCE_CONTROL_ABSENT_V1) {
                ppsrc_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution decision record is duplicated");
                return false;
            }
            result.decisions[slot] = (uint8_t)decision;
        } else {
            PPSourceControlDecisionV1 decision;
            PPSourceControlChildV1 child;
            PPSourceControlOutcomeV1 outcome;
            uint32_t slot;

            if (!ppsrc_v1_parse_finish_record(
                    record, &decision, &child, &outcome) ||
                !ppsrc_v1_finish_slot_required(decision, child)) {
                ppsrc_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution finish record is malformed");
                return false;
            }
            slot = ppsrc_v1_finish_index(decision, child);
            if (result.outcomes[slot] != PPSOURCE_CONTROL_ABSENT_V1) {
                ppsrc_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution finish record is duplicated");
                return false;
            }
            result.outcomes[slot] = (uint8_t)outcome;
        }
    }
    memcpy(result.compiler_digest, compiler_digest, 65u);
    memcpy(result.answer_set_digest, answer_set_digest, 65u);
    if (!ppsrc_v1_plan_digest(&result, result.plan_digest) ||
        !ppsource_resolution_control_v1_plan_validate(
            &result, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsrc_v1_set_error(
                error_buf, error_buf_size,
                "source-resolution control plan is incomplete");
        }
        return false;
    }
    *out = result;
    return true;
}

bool ppsource_resolution_control_v1_decide(
    const PPSourceResolutionControlV1Plan *plan,
    PPSourceControlProbeV1 probe,
    bool skip_completed_sources,
    bool reject_active_source_cycles,
    PPSourceControlDecisionV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t slot;

    if (!out || probe >= PPSOURCE_CONTROL_PROBE_V1_COUNT ||
        !ppsource_resolution_control_v1_plan_validate(
            plan, error_buf, error_buf_size)) {
        return false;
    }
    slot = ppsrc_v1_decision_index(
        probe, skip_completed_sources ? 1u : 0u,
        reject_active_source_cycles ? 1u : 0u);
    *out = (PPSourceControlDecisionV1)plan->decisions[slot];
    return true;
}

bool ppsource_resolution_control_v1_finish(
    const PPSourceResolutionControlV1Plan *plan,
    PPSourceControlDecisionV1 decision,
    PPSourceControlChildV1 child,
    PPSourceControlOutcomeV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t slot;

    if (!out || decision >= PPSOURCE_CONTROL_DECISION_V1_COUNT ||
        child >= PPSOURCE_CONTROL_CHILD_V1_COUNT ||
        !ppsource_resolution_control_v1_plan_validate(
            plan, error_buf, error_buf_size)) {
        return false;
    }
    slot = ppsrc_v1_finish_index(decision, child);
    if (plan->outcomes[slot] >= PPSOURCE_CONTROL_OUTCOME_V1_COUNT) {
        ppsrc_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution control finish request is outside its domain");
        return false;
    }
    *out = (PPSourceControlOutcomeV1)plan->outcomes[slot];
    return true;
}

static bool ppsrc_v1_identifier_valid(const char *identifier) {
    size_t index;

    if (!identifier || !identifier[0] ||
        !((identifier[0] >= 'A' && identifier[0] <= 'Z') ||
          (identifier[0] >= 'a' && identifier[0] <= 'z') ||
          identifier[0] == '_')) {
        return false;
    }
    for (index = 1u; identifier[index]; index++) {
        char value = identifier[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_')) {
            return false;
        }
    }
    return true;
}

bool ppsource_resolution_control_v1_emit_c(
    const PPSourceResolutionControlV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!output || !ppsrc_v1_identifier_valid(identifier_prefix) ||
        !ppsource_resolution_control_v1_plan_validate(
            plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsrc_v1_set_error(
                error_buf, error_buf_size,
                "cannot emit invalid source-resolution control plan");
        }
        return false;
    }
    if (fprintf(
            output,
            "\n/* Generated from SourceResolutionControlCoreV1. */\n"
            "#include \"parser_source_resolution_control_v1.h\"\n\n"
            "const char *%s_source_resolution_control_plan_digest(void) "
            "{ return \"%s\"; }\n\n"
            "bool %s_source_resolution_control_plan_init(\n"
            "    PPSourceResolutionControlV1Plan *out,\n"
            "    char *error_buf, size_t error_buf_size) {\n"
            "    PPSourceResolutionControlV1Plan result;\n"
            "    if (!out) return false;\n"
            "    ppsource_resolution_control_v1_plan_init(&result);\n",
            identifier_prefix, plan->plan_digest,
            identifier_prefix) < 0) {
        goto write_error;
    }
    for (index = 0u;
         index < PPSOURCE_CONTROL_DECISION_CELL_V1_COUNT; index++) {
        if (fprintf(
                output,
                "    result.decisions[UINT32_C(%u)] = UINT8_C(%u);\n",
                index, (unsigned int)plan->decisions[index]) < 0) {
            goto write_error;
        }
    }
    for (index = 0u;
         index < PPSOURCE_CONTROL_FINISH_CELL_V1_COUNT; index++) {
        if (plan->outcomes[index] == PPSOURCE_CONTROL_ABSENT_V1)
            continue;
        if (fprintf(
                output,
                "    result.outcomes[UINT32_C(%u)] = UINT8_C(%u);\n",
                index, (unsigned int)plan->outcomes[index]) < 0) {
            goto write_error;
        }
    }
    if (fprintf(
            output,
            "    memcpy(result.compiler_digest, \"%s\", 65u);\n"
            "    memcpy(result.answer_set_digest, \"%s\", 65u);\n"
            "    memcpy(result.plan_digest, \"%s\", 65u);\n"
            "    if (!ppsource_resolution_control_v1_plan_validate(\n"
            "            &result, error_buf, error_buf_size)) return false;\n"
            "    *out = result;\n"
            "    return true;\n"
            "}\n",
            plan->compiler_digest, plan->answer_set_digest,
            plan->plan_digest) < 0) {
        goto write_error;
    }
    return true;

write_error:
    ppsrc_v1_set_error(
        error_buf, error_buf_size,
        "cannot write generated source-resolution control plan");
    return false;
}
