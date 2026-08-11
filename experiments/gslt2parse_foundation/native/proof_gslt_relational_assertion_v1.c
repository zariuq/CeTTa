#include "proof_gslt_relational_assertion_v1.h"

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
    uint8_t *selector_bytes[PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN];
} PPProofGSLTRelationalAssertionStorageV1;

typedef struct {
    const char *role;
    uint32_t arity;
    uint32_t key_arity;
} PPProofGSLTRelationalTableSpecV1;

static const PPProofGSLTRelationalTableSpecV1
    ppproof_relational_table_specs_v1[] = {
        {"symbol-kind-v1", 2u, 1u},
        {"formula-v1", 2u, 1u},
        {"floating-variable-v1", 2u, 1u},
        {"assertion-active-hypothesis-v1", 2u, 2u},
        {"ordered-hypothesis-v1", 3u, 2u},
        {"mandatory-variable-v1", 2u, 2u},
        {"assertion-disjoint-v1", 3u, 3u},
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
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN,
    "relational assertion table role inventory mismatch");
_Static_assert(
    sizeof(ppproof_relational_selector_specs_v1) /
            sizeof(ppproof_relational_selector_specs_v1[0]) ==
        PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN,
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
    const Atom *atom, PPProofGSLTNameV1 *out) {
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
    *out = (PPProofGSLTNameV1){
        .bytes = (const uint8_t *)name,
        .len = (uint32_t)len,
    };
    return true;
}

static bool ppproof_relational_v1_name_is(
    PPProofGSLTNameV1 name, const char *text) {
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

static int32_t ppproof_relational_v1_table_role(
    PPProofGSLTNameV1 role) {
    uint32_t index;

    for (index = 0u; index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN;
         index++) {
        if (ppproof_relational_v1_name_is(
                role, ppproof_relational_table_specs_v1[index].role))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppproof_relational_v1_selector_role(
    PPProofGSLTNameV1 role) {
    uint32_t index;

    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++) {
        if (ppproof_relational_v1_name_is(
                role, ppproof_relational_selector_specs_v1[index]))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppproof_relational_v1_state_table(
    const PPRelationalStateProgramV1Plan *state_plan,
    PPProofGSLTNameV1 name) {
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

void ppproof_gslt_relational_assertion_v1_init(
    PPProofGSLTRelationalAssertionPlanV1 *plan) {
    uint32_t index;

    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    for (index = 0u; index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN;
         index++)
        plan->tables[index] = UINT32_MAX;
}

void ppproof_gslt_relational_assertion_v1_free(
    PPProofGSLTRelationalAssertionPlanV1 *plan) {
    PPProofGSLTRelationalAssertionStorageV1 *storage;
    uint32_t index;

    if (!plan)
        return;
    storage = plan->storage;
    if (storage) {
        for (index = 0u;
             index < PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++)
            free(storage->selector_bytes[index]);
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    ppproof_gslt_relational_assertion_v1_init(plan);
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_assertion_v1_load(
    PPProofGSLTRelationalAssertionPlanV1 *plan,
    const char *answer_path,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTRelationalAssertionPlanV1 result;
    PPProofGSLTRelationalAssertionStorageV1 *storage = NULL;
    PPProofGSLTNameV1 artifact_owner = {0};
    bool identity_seen = false;
    size_t answer_index;
    uint32_t index;
    PPProofGSLTArticleV1Result failure =
        PPPROOF_GSLT_ARTICLE_V1_INVALID;

    ppproof_gslt_relational_assertion_v1_init(&result);
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!plan || !answer_path || !proof_plan || !proof_plan->storage ||
        !state_plan || !state_plan->tables ||
        !ppproof_relational_v1_digest_valid(proof_plan->semantic_digest) ||
        !ppproof_relational_v1_digest_valid(state_plan->plan_digest)) {
        ppproof_relational_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational assertion plan request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage) {
        ppproof_relational_v1_set_error(
            error_buf, error_buf_size,
            "relational assertion plan allocation failed");
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
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
        PPProofGSLTNameV1 owner;

        if (!ppproof_relational_v1_expr_head(
                answer, "proof-sequence-relational-artifact-v1", 1u))
            goto malformed;
        record = answer->expr.elems[1];
        if (ppproof_relational_v1_expr_head(
                record, "proof-sequence-relational-identity-v1", 2u)) {
            PPProofGSLTNameV1 base;
            if (identity_seen ||
                !ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &base))
                goto malformed;
            if (artifact_owner.bytes &&
                !ppproof_gslt_article_v1_name_equal(
                    artifact_owner, owner))
                goto mixed_identity;
            artifact_owner = owner;
            result.owner = owner;
            result.base = base;
            identity_seen = true;
            continue;
        }
        if (ppproof_relational_v1_expr_head(
                record, "proof-sequence-relational-table-v1", 5u)) {
            PPProofGSLTNameV1 role;
            PPProofGSLTNameV1 table_name;
            uint32_t arity;
            uint32_t key_arity;
            int32_t role_index;
            int32_t table_index;
            const PPProofGSLTRelationalTableSpecV1 *spec;

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
                failure = PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto unknown_role;
            }
            if (result.tables[(uint32_t)role_index] != UINT32_MAX)
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
                failure = PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto wrong_shape;
            }
            result.tables[(uint32_t)role_index] =
                (uint32_t)table_index;
        } else if (ppproof_relational_v1_expr_head(
                       record,
                       "proof-sequence-relational-selector-v1", 3u)) {
            PPProofGSLTNameV1 role;
            uint8_t *rendered = NULL;
            size_t rendered_len = 0u;
            int32_t role_index;

            if (!ppproof_relational_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_relational_v1_name(record->expr.elems[2], &role))
                goto malformed;
            role_index = ppproof_relational_v1_selector_role(role);
            if (role_index < 0) {
                failure = PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
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
                (PPProofGSLTRelationalSelectorV1){
                    .bytes = rendered,
                    .len = (uint32_t)rendered_len,
                };
        } else {
            goto malformed;
        }
        if (!artifact_owner.bytes)
            artifact_owner = owner;
        else if (!ppproof_gslt_article_v1_name_equal(
                     artifact_owner, owner))
            goto mixed_identity;
    }
    if (!identity_seen ||
        !ppproof_gslt_article_v1_name_equal(
            result.owner, proof_plan->owner) ||
        !ppproof_gslt_article_v1_name_equal(
            result.base, proof_plan->base)) {
        ppproof_relational_v1_set_error(
            error_buf, error_buf_size,
            "relational assertion identity does not match its proof plan");
        goto failed;
    }
    for (index = 0u; index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN;
         index++) {
        if (result.tables[index] == UINT32_MAX)
            goto missing_role;
    }
    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++) {
        if (!result.selectors[index].bytes)
            goto missing_role;
    }
    memcpy(result.proof_plan_digest, proof_plan->semantic_digest, 65u);
    memcpy(result.state_plan_digest, state_plan->plan_digest, 65u);
    memcpy(result.semantic_digest, storage->answers.digest, 65u);
    result.storage = storage;
    ppproof_gslt_relational_assertion_v1_free(plan);
    *plan = result;
    return PPPROOF_GSLT_ARTICLE_V1_OK;

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
             index < PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++)
            free(storage->selector_bytes[index]);
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    return failure;
}
