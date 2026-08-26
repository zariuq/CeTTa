#include "certificate_gslt_relational_assertion_v1.h"

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "gslt_indexed_instruction_decoder_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
    PPCertificateGSLTRelationalTableBindingV1 *table_bindings;
    uint32_t table_binding_cap;
    uint8_t *execution_unknown_bytes;
    uint8_t *selector_bytes[PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN];
} PPCertificateGSLTRelationalAssertionStorageV1;

typedef struct {
    const char *role;
    uint32_t arity;
    uint32_t key_arity;
} PPCertificateGSLTRelationalTableSpecV1;

static const PPCertificateGSLTRelationalTableSpecV1
    ppproof_relational_table_specs_v1[] = {
        {"symbol-kind-v1", 2u, 1u},
        {"formula-v1", 2u, 1u},
        {"floating-variable-v1", 2u, 1u},
        {"assertion-active-hypothesis-v1", 2u, 2u},
        {"ordered-hypothesis-v1", 3u, 2u},
        {"mandatory-variable-v1", 2u, 2u},
        {"assertion-disjoint-v1", 3u, 3u},
        {"active-apartness-v1", 2u, 2u},
        {"label-kind-v1", 2u, 1u},
    };

static const char *const ppproof_relational_selector_specs_v1[] = {
    "symbol-literal-v1",
    "symbol-variable-v1",
    "hypothesis-floating-v1",
    "hypothesis-essential-v1",
};

_Static_assert(
    sizeof(ppproof_relational_table_specs_v1) /
            sizeof(ppproof_relational_table_specs_v1[0]) ==
        PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN,
    "relational assertion table role inventory mismatch");
_Static_assert(
    sizeof(ppproof_relational_selector_specs_v1) /
            sizeof(ppproof_relational_selector_specs_v1[0]) ==
        PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN,
    "relational assertion selector role inventory mismatch");

static void ppproof_relational_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppproof_relational_v1_expr_head(
    const Atom *atom, const char *head, CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == argument_len + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppproof_relational_v1_name(
    const Atom *atom, PPCertificateGSLTNameV1 *out) {
    const char *name;
    size_t len;

    if (!atom || atom->kind != ATOM_SYMBOL || !out)
        return false;
    name = atom_name_cstr((Atom *)atom);
    if (!name || name[0] == '\0')
        return false;
    len = strlen(name);
    if (len > UINT32_MAX)
        return false;
    *out = (PPCertificateGSLTNameV1){
        .bytes = (const uint8_t *)name,
        .len = (uint32_t)len,
    };
    return true;
}

static bool ppproof_relational_v1_name_is(
    PPCertificateGSLTNameV1 name, const char *text) {
    size_t len = strlen(text);

    return len <= UINT32_MAX && name.len == (uint32_t)len &&
           memcmp(name.bytes, text, len) == 0;
}

static bool ppproof_relational_v1_u32(
    const Atom *atom, uint32_t *out) {
    int64_t value;

    if (!atom || !out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT)
        return false;
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static bool ppproof_relational_v1_u8(
    const Atom *atom, uint8_t *out) {
    uint32_t value;

    if (!out || !ppproof_relational_v1_u32(atom, &value) ||
        value > UINT8_MAX)
        return false;
    *out = (uint8_t)value;
    return true;
}

static bool ppproof_relational_v1_literal(
    const Atom *atom, uint8_t **bytes_out, size_t *len_out,
    char *error_buf, size_t error_buf_size) {
    uint32_t byte_value;
    uint8_t *bytes;

    if (!bytes_out || !len_out)
        return false;
    *bytes_out = NULL;
    *len_out = 0u;
    if (ppproof_relational_v1_expr_head(
            atom, "state-byte-literal-v1", 1u) &&
        ppproof_relational_v1_u32(atom->expr.elems[1], &byte_value) &&
        byte_value <= UINT8_MAX) {
        bytes = malloc(1u);
        if (!bytes)
            return false;
        bytes[0] = (uint8_t)byte_value;
        *bytes_out = bytes;
        *len_out = 1u;
        return true;
    }
    if (!ppproof_relational_v1_expr_head(
            atom, "state-literal-v1", 1u))
        return false;
    return fh_ground_term_v1_render(
        atom->expr.elems[1], bytes_out, len_out,
        error_buf, error_buf_size);
}

static PPRelationalStackProofV1UnknownPolicy
ppproof_relational_v1_unknown_policy(const Atom *atom) {
    if (atom_is_symbol(
            (Atom *)atom, "state-proof-unknown-reject-v1"))
        return PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_REJECT;
    if (atom_is_symbol(
            (Atom *)atom, "state-proof-unknown-push-claim-v1"))
        return PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM;
    return (PPRelationalStackProofV1UnknownPolicy)UINT32_MAX;
}

static CettaGsltIndexedSavePlacementV1
ppproof_relational_v1_save_placement(const Atom *atom) {
    if (atom_is_symbol(
            (Atom *)atom,
            "state-proof-save-immediately-after-use-v1"))
        return CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1;
    if (atom_is_symbol((Atom *)atom, "state-proof-save-repeatable-v1"))
        return CETTA_GSLT_INDEXED_SAVE_REPEATABLE_AFTER_USE_V1;
    return CETTA_GSLT_INDEXED_SAVE_INVALID_V1;
}

static CettaGsltHeaderHypothesisPolicyV1
ppproof_relational_v1_header_hypothesis_policy(const Atom *atom) {
    if (atom_is_symbol(
            (Atom *)atom,
            "state-proof-header-nonmandatory-only-v1"))
        return CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1;
    if (atom_is_symbol(
            (Atom *)atom, "state-proof-header-any-active-v1"))
        return CETTA_GSLT_HEADER_HYPOTHESIS_ANY_ACTIVE_V1;
    return CETTA_GSLT_HEADER_HYPOTHESIS_INVALID_V1;
}

static bool ppproof_relational_v1_execution_valid(
    const PPCertificateGSLTRelationalExecutionDescriptorV1 *execution) {
    CettaGsltIndexedInstructionPlanV1 decoder;

    if (!execution || !execution->machine ||
        !execution->unknown_token.bytes ||
        execution->unknown_token.len == 0u ||
        execution->unknown_policy >
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM ||
        execution->save_placement == CETTA_GSLT_INDEXED_SAVE_INVALID_V1)
        return false;
    if (execution->header_hypothesis_policy ==
        CETTA_GSLT_HEADER_HYPOTHESIS_INVALID_V1)
        return false;
    decoder = (CettaGsltIndexedInstructionPlanV1){
        .terminal_low = execution->terminal_low,
        .terminal_high = execution->terminal_high,
        .continuation_low = execution->continuation_low,
        .continuation_high = execution->continuation_high,
        .save_byte = execution->save_byte,
        .unknown_byte = execution->unknown_byte,
        .terminal_radix = execution->terminal_radix,
        .terminal_digit_bias = execution->terminal_digit_bias,
        .continuation_radix = execution->continuation_radix,
        .continuation_digit_bias = execution->continuation_digit_bias,
        .save_placement = execution->save_placement,
    };
    return cetta_gslt_indexed_instruction_plan_validate_v1(&decoder);
}

static int32_t ppproof_relational_v1_table_role(
    PPCertificateGSLTNameV1 role) {
    uint32_t index;

    for (index = 0u; index < PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN;
         index++) {
        if (ppproof_relational_v1_name_is(
                role, ppproof_relational_table_specs_v1[index].role))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppproof_relational_v1_selector_role(
    PPCertificateGSLTNameV1 role) {
    uint32_t index;

    for (index = 0u;
         index < PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++) {
        if (ppproof_relational_v1_name_is(
                role, ppproof_relational_selector_specs_v1[index]))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppproof_relational_v1_state_table(
    const PPRelationalStateProgramV1Plan *state_plan,
    PPCertificateGSLTNameV1 name) {
    int32_t found = -1;
    uint32_t index;

    for (index = 0u; state_plan && index < state_plan->table_len;
         index++) {
        const char *candidate = state_plan->tables[index].name;
        size_t candidate_len;

        if (!candidate)
            continue;
        candidate_len = strlen(candidate);
        if (candidate_len <= UINT32_MAX &&
            name.len == (uint32_t)candidate_len &&
            memcmp(name.bytes, candidate, candidate_len) == 0) {
            if (found >= 0)
                return -2;
            found = (int32_t)index;
        }
    }
    return found;
}

static bool ppproof_relational_v1_digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

void ppcertificate_gslt_relational_assertion_v1_init(
    PPCertificateGSLTRelationalAssertionPlanV1 *plan) {
    uint32_t index;

    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    for (index = 0u; index < PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN;
         index++)
        plan->resolved_table_ids[index] = UINT32_MAX;
}

void ppcertificate_gslt_relational_assertion_v1_free(
    PPCertificateGSLTRelationalAssertionPlanV1 *plan) {
    PPCertificateGSLTRelationalAssertionStorageV1 *storage;
    uint32_t index;

    if (!plan)
        return;
    storage = plan->storage;
    if (storage) {
        for (index = 0u;
             index < PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++)
            free(storage->selector_bytes[index]);
        free(storage->table_bindings);
        free(storage->execution_unknown_bytes);
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    ppcertificate_gslt_relational_assertion_v1_init(plan);
}

PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_assertion_v1_load(
    PPCertificateGSLTRelationalAssertionPlanV1 *plan,
    const char *answer_path,
    const PPCertificateGSLTPlanV1 *proof_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size) {
    PPCertificateGSLTRelationalAssertionPlanV1 result;
    PPCertificateGSLTRelationalAssertionStorageV1 *storage = NULL;
    PPCertificateGSLTNameV1 artifact_owner = {0};
    bool identity_seen = false;
    bool execution_seen = false;
    size_t answer_index;
    uint32_t index;
    PPCertificateGSLTArticleV1Result failure =
        PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;

    ppcertificate_gslt_relational_assertion_v1_init(&result);
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!plan || !answer_path || !proof_plan || !proof_plan->storage ||
        !state_plan || !state_plan->tables ||
        !ppproof_relational_v1_digest_valid(proof_plan->semantic_digest) ||
        !ppproof_relational_v1_digest_valid(state_plan->plan_digest)) {
        ppproof_relational_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational assertion plan request");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage) {
        ppproof_relational_v1_set_error(
            error_buf, error_buf_size,
            "relational assertion plan allocation failed");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    }
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path,
            error_buf, error_buf_size))
        goto failed;

    for (answer_index = 0u; answer_index < storage->answers.len;
         answer_index++) {
        const Atom *answer = storage->answers.terms[answer_index];
        const Atom *record;
        PPCertificateGSLTNameV1 owner;

        if (!ppproof_relational_v1_expr_head(
                answer, "proof-sequence-relational-artifact-v1", 1u))
            goto malformed;
        record = answer->expr.elems[1];
        if (ppproof_relational_v1_expr_head(
                record, "proof-sequence-relational-identity-v1", 2u)) {
            PPCertificateGSLTNameV1 base;
            if (identity_seen ||
                !ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &base))
                goto malformed;
            if (artifact_owner.bytes &&
                !ppcertificate_gslt_article_v1_name_equal(
                    artifact_owner, owner))
                goto mixed_identity;
            artifact_owner = owner;
            result.owner = owner;
            result.base = base;
            identity_seen = true;
            continue;
        }
        if (ppproof_relational_v1_expr_head(
                record, "proof-sequence-relational-execution-v1", 16u)) {
            PPCertificateGSLTNameV1 machine;
            uint8_t *unknown_token = NULL;
            size_t unknown_token_len = 0u;

            if (execution_seen ||
                !ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &machine) ||
                !ppproof_relational_v1_literal(
                    record->expr.elems[3], &unknown_token,
                    &unknown_token_len, error_buf, error_buf_size) ||
                unknown_token_len == 0u || unknown_token_len > UINT32_MAX ||
                !ppproof_relational_v1_u8(
                    record->expr.elems[4], &result.execution.terminal_low) ||
                !ppproof_relational_v1_u8(
                    record->expr.elems[5], &result.execution.terminal_high) ||
                !ppproof_relational_v1_u8(
                    record->expr.elems[6],
                    &result.execution.continuation_low) ||
                !ppproof_relational_v1_u8(
                    record->expr.elems[7],
                    &result.execution.continuation_high) ||
                !ppproof_relational_v1_u8(
                    record->expr.elems[8], &result.execution.save_byte) ||
                !ppproof_relational_v1_u8(
                    record->expr.elems[9], &result.execution.unknown_byte) ||
                !ppproof_relational_v1_u32(
                    record->expr.elems[10],
                    &result.execution.terminal_radix) ||
                !ppproof_relational_v1_u32(
                    record->expr.elems[11],
                    &result.execution.terminal_digit_bias) ||
                !ppproof_relational_v1_u32(
                    record->expr.elems[12],
                    &result.execution.continuation_radix) ||
                !ppproof_relational_v1_u32(
                    record->expr.elems[13],
                    &result.execution.continuation_digit_bias)) {
                free(unknown_token);
                goto malformed;
            }
            result.execution.unknown_policy =
                ppproof_relational_v1_unknown_policy(
                    record->expr.elems[14]);
            result.execution.save_placement =
                ppproof_relational_v1_save_placement(
                    record->expr.elems[15]);
            result.execution.header_hypothesis_policy =
                ppproof_relational_v1_header_hypothesis_policy(
                    record->expr.elems[16]);
            storage->execution_unknown_bytes = unknown_token;
            result.execution.machine = (const char *)machine.bytes;
            result.execution.unknown_token = (PPRelationalStateLiteralV1){
                .bytes = unknown_token,
                .len = (uint32_t)unknown_token_len,
            };
            if (!ppproof_relational_v1_execution_valid(&result.execution))
                goto malformed;
            execution_seen = true;
        } else if (ppproof_relational_v1_expr_head(
                record, "proof-sequence-relational-table-v1", 5u)) {
            PPCertificateGSLTNameV1 role;
            PPCertificateGSLTNameV1 table_name;
            uint32_t arity;
            uint32_t key_arity;
            int32_t role_index;
            int32_t table_index;
            const PPCertificateGSLTRelationalTableSpecV1 *spec;

            if (!ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &role) ||
                !ppproof_relational_v1_name(
                    record->expr.elems[3], &table_name) ||
                !ppproof_relational_v1_u32(record->expr.elems[4], &arity) ||
                !ppproof_relational_v1_u32(
                    record->expr.elems[5], &key_arity))
                goto malformed;
            role_index = ppproof_relational_v1_table_role(role);
            if (role_index < 0) {
                failure = PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto unknown_role;
            }
            if (result.resolved_table_ids[(uint32_t)role_index] !=
                UINT32_MAX)
                goto duplicate_role;
            spec = &ppproof_relational_table_specs_v1[
                (uint32_t)role_index];
            table_index = ppproof_relational_v1_state_table(
                state_plan, table_name);
            if (table_index < 0 || arity != spec->arity ||
                key_arity != spec->key_arity ||
                state_plan->tables[(uint32_t)table_index].arity != arity ||
                state_plan->tables[(uint32_t)table_index].key_arity !=
                    key_arity) {
                failure = PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto wrong_shape;
            }
            if (result.table_binding_len == storage->table_binding_cap) {
                uint32_t next_cap = storage->table_binding_cap == 0u
                                        ? 8u
                                        : storage->table_binding_cap * 2u;
                PPCertificateGSLTRelationalTableBindingV1 *grown;

                if (next_cap < storage->table_binding_cap ||
                    (size_t)next_cap > SIZE_MAX / sizeof(*grown)) {
                    failure = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                    goto failed;
                }
                grown = realloc(
                    storage->table_bindings,
                    (size_t)next_cap * sizeof(*grown));
                if (!grown) {
                    failure = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                    goto failed;
                }
                storage->table_bindings = grown;
                storage->table_binding_cap = next_cap;
            }
            storage->table_bindings[result.table_binding_len++] =
                (PPCertificateGSLTRelationalTableBindingV1){
                    .role = (PPCertificateGSLTRelationalTableRoleV1)role_index,
                    .table_id = (uint32_t)table_index,
                };
            result.table_bindings = storage->table_bindings;
            result.resolved_table_ids[(uint32_t)role_index] =
                (uint32_t)table_index;
        } else if (ppproof_relational_v1_expr_head(
                       record,
                       "proof-sequence-relational-table-policy-v1", 3u)) {
            PPCertificateGSLTNameV1 role;
            PPCertificateGSLTNameV1 presence;
            int32_t role_index;
            PPCertificateGSLTRelationalPresenceV1 decoded_presence;

            if (!ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &role) ||
                !ppproof_relational_v1_name(
                    record->expr.elems[3], &presence))
                goto malformed;
            role_index = ppproof_relational_v1_table_role(role);
            if (role_index < 0) {
                failure = PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto unknown_role;
            }
            if (ppproof_relational_v1_name_is(presence, "required-v1"))
                decoded_presence =
                    PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_REQUIRED;
            else if (ppproof_relational_v1_name_is(
                         presence, "optional-empty-v1"))
                decoded_presence =
                    PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_OPTIONAL_EMPTY;
            else {
                failure = PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto unknown_role;
            }
            if (result.table_presence[(uint32_t)role_index] !=
                PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_INVALID)
                goto duplicate_role;
            result.table_presence[(uint32_t)role_index] = decoded_presence;
        } else if (ppproof_relational_v1_expr_head(
                       record,
                       "proof-sequence-relational-selector-v1", 3u)) {
            PPCertificateGSLTNameV1 role;
            uint8_t *rendered = NULL;
            size_t rendered_len = 0u;
            int32_t role_index;

            if (!ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &role))
                goto malformed;
            role_index = ppproof_relational_v1_selector_role(role);
            if (role_index < 0) {
                failure = PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto unknown_role;
            }
            if (result.selectors[(uint32_t)role_index].bytes)
                goto duplicate_role;
            if (!fh_ground_term_v1_render(
                    record->expr.elems[3], &rendered, &rendered_len,
                    error_buf, error_buf_size) || rendered_len == 0u ||
                rendered_len > UINT32_MAX) {
                free(rendered);
                goto malformed;
            }
            storage->selector_bytes[(uint32_t)role_index] = rendered;
            result.selectors[(uint32_t)role_index] =
                (PPCertificateGSLTRelationalSelectorV1){
                    .bytes = rendered,
                    .len = (uint32_t)rendered_len,
                };
        } else {
            goto malformed;
        }
        if (!artifact_owner.bytes)
            artifact_owner = owner;
        else if (!ppcertificate_gslt_article_v1_name_equal(
                     artifact_owner, owner))
            goto mixed_identity;
    }
    if (!identity_seen || !execution_seen ||
        !ppcertificate_gslt_article_v1_name_equal(
            result.owner, proof_plan->owner) ||
        !ppcertificate_gslt_article_v1_name_equal(
            result.base, proof_plan->base)) {
        ppproof_relational_v1_set_error(
            error_buf, error_buf_size,
            "relational assertion identity does not match its proof plan");
        goto failed;
    }
    for (index = 0u; index < PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN;
         index++) {
        if (result.table_presence[index] ==
            PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_INVALID ||
            (result.table_presence[index] ==
                 PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_REQUIRED &&
             result.resolved_table_ids[index] == UINT32_MAX))
            goto missing_role;
    }
    for (index = 0u;
         index < PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++) {
        if (!result.selectors[index].bytes)
            goto missing_role;
    }
    memcpy(result.proof_plan_digest, proof_plan->semantic_digest, 65u);
    memcpy(result.state_plan_digest, state_plan->plan_digest, 65u);
    memcpy(result.semantic_digest, storage->answers.digest, 65u);
    result.storage = storage;
    ppcertificate_gslt_relational_assertion_v1_free(plan);
    *plan = result;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;

malformed:
    ppproof_relational_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion artifact contains a malformed record");
    goto failed;
unknown_role:
    ppproof_relational_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion artifact contains an unknown role");
    goto failed;
duplicate_role:
    ppproof_relational_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion artifact repeats a role");
    goto failed;
wrong_shape:
    ppproof_relational_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion table role has the wrong state shape");
    goto failed;
mixed_identity:
    ppproof_relational_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion artifact mixes extension identities");
    goto failed;
missing_role:
    ppproof_relational_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion artifact is missing a required role");

failed:
    if (storage) {
        for (index = 0u;
             index < PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++)
            free(storage->selector_bytes[index]);
        free(storage->table_bindings);
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    return failure;
}
