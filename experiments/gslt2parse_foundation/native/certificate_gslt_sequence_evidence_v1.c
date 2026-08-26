#include "proof_gslt_sequence_evidence_v1.h"

#include "finite_horn_answer_stream_v1.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
} PPProofGSLTSequenceEvidenceStorageV1;

typedef struct {
    const char *role;
    uint32_t expected_arity;
} PPProofGSLTSequenceRoleSpecV1;

typedef struct {
    const char *role;
    uint32_t expected_formal_len;
    uint32_t expected_premise_len;
} PPProofGSLTSequenceRuleSpecV1;

static const PPProofGSLTSequenceRoleSpecV1
    ppproof_sequence_constructor_specs_v1[] = {
        {"sequence-nil-v1", 0u},
        {"sequence-cons-v1", 2u},
        {"environment-nil-v1", 0u},
        {"environment-cons-v1", 3u},
    };

static const PPProofGSLTSequenceRoleSpecV1
    ppproof_sequence_judgment_specs_v1[] = {
        {"append-v1", 3u},
        {"lookup-v1", 3u},
        {"instantiate-v1", 3u},
        {"literal-v1", 1u},
        {"variable-v1", 1u},
        {"different-v1", 2u},
        {"apart-v1", 2u},
        {"pair-allowed-v1", 2u},
        {"token-against-sequence-v1", 2u},
        {"support-apart-v1", 2u},
    };

static const PPProofGSLTSequenceRuleSpecV1
    ppproof_sequence_rule_specs_v1[] = {
        {"append-nil-v1", 1u, 0u},
        {"append-cons-v1", 4u, 1u},
        {"lookup-head-v1", 3u, 0u},
        {"lookup-tail-v1", 5u, 2u},
        {"instantiate-nil-v1", 1u, 0u},
        {"instantiate-literal-v1", 4u, 2u},
        {"instantiate-variable-v1", 6u, 4u},
        {"pair-left-literal-v1", 2u, 1u},
        {"pair-right-literal-v1", 2u, 1u},
        {"pair-apart-v1", 2u, 3u},
        {"token-against-nil-v1", 1u, 0u},
        {"token-against-cons-v1", 3u, 2u},
        {"support-apart-nil-v1", 1u, 0u},
        {"support-apart-cons-v1", 3u, 2u},
    };

static const PPProofGSLTSequenceRoleSpecV1
    ppproof_assertion_constructor_specs_v1[] = {
        {"assertion-v1", 5u},
        {"variable-v1", 2u},
        {"essential-v1", 1u},
        {"disjoint-v1", 2u},
        {"list-nil-v1", 0u},
        {"list-cons-v1", 2u},
    };

static const PPProofGSLTSequenceRoleSpecV1
    ppproof_assertion_judgment_specs_v1[] = {
        {"declared-v1", 1u},
        {"build-environment-v1", 3u},
        {"check-essentials-v1", 3u},
        {"check-disjoints-v1", 2u},
        {"provable-v1", 1u},
    };

static const PPProofGSLTSequenceRuleSpecV1
    ppproof_assertion_rule_specs_v1[] = {
        {"build-environment-nil-v1", 0u, 0u},
        {"build-environment-cons-v1", 6u, 3u},
        {"check-essentials-nil-v1", 1u, 0u},
        {"check-essentials-cons-v1", 5u, 3u},
        {"check-disjoints-nil-v1", 1u, 0u},
        {"check-disjoints-cons-v1", 6u, 5u},
        {"apply-v1", 9u, 5u},
        {"use-premise-v1", 1u, 1u},
    };

_Static_assert(
    sizeof(ppproof_sequence_constructor_specs_v1) /
            sizeof(ppproof_sequence_constructor_specs_v1[0]) ==
        PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN,
    "sequence constructor role inventory mismatch");
_Static_assert(
    sizeof(ppproof_sequence_judgment_specs_v1) /
            sizeof(ppproof_sequence_judgment_specs_v1[0]) ==
        PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LEN,
    "sequence judgment role inventory mismatch");
_Static_assert(
    sizeof(ppproof_sequence_rule_specs_v1) /
            sizeof(ppproof_sequence_rule_specs_v1[0]) ==
    PPPROOF_GSLT_SEQUENCE_RULE_V1_LEN,
    "sequence rule role inventory mismatch");
_Static_assert(
    sizeof(ppproof_assertion_constructor_specs_v1) /
            sizeof(ppproof_assertion_constructor_specs_v1[0]) ==
        PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN,
    "assertion constructor role inventory mismatch");
_Static_assert(
    sizeof(ppproof_assertion_judgment_specs_v1) /
            sizeof(ppproof_assertion_judgment_specs_v1[0]) ==
        PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_LEN,
    "assertion judgment role inventory mismatch");
_Static_assert(
    sizeof(ppproof_assertion_rule_specs_v1) /
            sizeof(ppproof_assertion_rule_specs_v1[0]) ==
        PPPROOF_GSLT_ASSERTION_RULE_V1_LEN,
    "assertion rule role inventory mismatch");

static void ppproof_sequence_v1_set_error(char *buf, size_t size,
                                          const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppproof_sequence_v1_expr_head(const Atom *atom,
                                          const char *head,
                                          CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == argument_len + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppproof_sequence_v1_name(const Atom *atom,
                                     PPProofGSLTNameV1 *out) {
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

static bool ppproof_sequence_v1_name_is(PPProofGSLTNameV1 name,
                                        const char *text) {
    size_t len = strlen(text);

    return len <= UINT32_MAX && name.len == (uint32_t)len &&
           memcmp(name.bytes, text, len) == 0;
}

static int32_t ppproof_sequence_v1_role_index(
    PPProofGSLTNameV1 role,
    const PPProofGSLTSequenceRoleSpecV1 *specs,
    uint32_t spec_len) {
    uint32_t index;

    for (index = 0u; index < spec_len; index++) {
        if (ppproof_sequence_v1_name_is(role, specs[index].role))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppproof_sequence_v1_rule_role_index_in(
    PPProofGSLTNameV1 role,
    const PPProofGSLTSequenceRuleSpecV1 *specs,
    uint32_t spec_len) {
    uint32_t index;

    for (index = 0u; index < spec_len; index++) {
        if (ppproof_sequence_v1_name_is(
                role, specs[index].role))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppproof_sequence_v1_rule_role_index(
    PPProofGSLTNameV1 role) {
    return ppproof_sequence_v1_rule_role_index_in(
        role, ppproof_sequence_rule_specs_v1,
        PPPROOF_GSLT_SEQUENCE_RULE_V1_LEN);
}

static const PPProofGSLTConstructorV1 *
ppproof_sequence_v1_find_constructor(
    const PPProofGSLTPresentationV1 *presentation,
    PPProofGSLTNameV1 name,
    uint32_t *index_out) {
    uint32_t index;

    for (index = 0u; presentation &&
                     index < presentation->constructor_len; index++) {
        if (ppproof_gslt_article_v1_name_equal(
                presentation->constructors[index].name, name)) {
            if (index_out)
                *index_out = index;
            return &presentation->constructors[index];
        }
    }
    return NULL;
}

static const PPProofGSLTJudgmentV1 *ppproof_sequence_v1_find_judgment(
    const PPProofGSLTPresentationV1 *presentation,
    PPProofGSLTNameV1 name) {
    uint32_t index;

    for (index = 0u; presentation &&
                     index < presentation->judgment_len; index++) {
        if (ppproof_gslt_article_v1_name_equal(
                presentation->judgments[index].head, name))
            return &presentation->judgments[index];
    }
    return NULL;
}

static const PPProofGSLTRuleSchemaV1 *ppproof_sequence_v1_find_rule(
    const PPProofGSLTPresentationV1 *presentation,
    PPProofGSLTNameV1 name) {
    uint32_t index;

    for (index = 0u; presentation && index < presentation->rule_len;
         index++) {
        if (ppproof_gslt_article_v1_name_equal(
                presentation->rules[index].id, name))
            return &presentation->rules[index];
    }
    return NULL;
}

static bool ppproof_sequence_v1_targets_unique(
    const PPProofGSLTNameV1 *names, uint32_t len) {
    uint32_t left;
    uint32_t right;

    for (left = 0u; left < len; left++) {
        if (!names[left].bytes)
            return false;
        for (right = left + 1u; right < len; right++) {
            if (ppproof_gslt_article_v1_name_equal(
                    names[left], names[right]))
                return false;
        }
    }
    return true;
}

static PPProofGSLTArticleV1Result ppproof_sequence_v1_validate(
    const PPProofGSLTSequenceEvidenceABIV1 *abi,
    const PPProofGSLTPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (!abi || !plan || !plan->storage ||
        !ppproof_gslt_article_v1_name_equal(abi->owner, plan->owner) ||
        !ppproof_gslt_article_v1_name_equal(abi->base, plan->base)) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "sequence evidence ABI identity does not match its proof plan");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    if (!ppproof_sequence_v1_targets_unique(
            abi->constructors,
            PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN) ||
        !ppproof_sequence_v1_targets_unique(
            abi->judgments, PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LEN) ||
        !ppproof_sequence_v1_targets_unique(
            abi->rules, PPPROOF_GSLT_SEQUENCE_RULE_V1_LEN) ||
        !ppproof_sequence_v1_targets_unique(
            abi->assertion_constructors,
            PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN) ||
        !ppproof_sequence_v1_targets_unique(
            abi->assertion_judgments,
            PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_LEN) ||
        !ppproof_sequence_v1_targets_unique(
            abi->assertion_rules,
            PPPROOF_GSLT_ASSERTION_RULE_V1_LEN)) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "sequence evidence ABI has missing or duplicate role targets");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    for (index = 0u;
         index < PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN; index++) {
        uint32_t constructor_index = 0u;
        const PPProofGSLTConstructorV1 *constructor =
            ppproof_sequence_v1_find_constructor(
                &plan->presentation, abi->constructors[index],
                &constructor_index);
        if (!constructor ||
            !plan->constructor_origins ||
            plan->constructor_origins[constructor_index] !=
                PPPROOF_GSLT_CONSTRUCTOR_ORIGIN_V1_CALCULUS ||
            constructor->arity !=
                ppproof_sequence_constructor_specs_v1[index]
                    .expected_arity) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "sequence evidence constructor role has the wrong shape");
            return PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    for (index = 0u;
         index < PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LEN; index++) {
        const PPProofGSLTJudgmentV1 *judgment =
            ppproof_sequence_v1_find_judgment(
                &plan->presentation, abi->judgments[index]);
        if (!judgment ||
            judgment->arity !=
                ppproof_sequence_judgment_specs_v1[index]
                    .expected_arity) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "sequence evidence judgment role has the wrong shape");
            return PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    for (index = 0u; index < PPPROOF_GSLT_SEQUENCE_RULE_V1_LEN;
         index++) {
        const PPProofGSLTRuleSchemaV1 *rule =
            ppproof_sequence_v1_find_rule(
                &plan->presentation, abi->rules[index]);
        if (!rule ||
            rule->formal_len !=
                ppproof_sequence_rule_specs_v1[index]
                    .expected_formal_len ||
            rule->premise_len !=
                ppproof_sequence_rule_specs_v1[index]
                    .expected_premise_len ||
            rule->side_condition_len != 0u) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "sequence evidence rule role has the wrong shape");
            return PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    for (index = 0u;
         index < PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN; index++) {
        uint32_t constructor_index = 0u;
        const PPProofGSLTConstructorV1 *constructor =
            ppproof_sequence_v1_find_constructor(
                &plan->presentation,
                abi->assertion_constructors[index],
                &constructor_index);
        if (!constructor || !plan->constructor_origins ||
            plan->constructor_origins[constructor_index] !=
                PPPROOF_GSLT_CONSTRUCTOR_ORIGIN_V1_CALCULUS ||
            constructor->arity !=
                ppproof_assertion_constructor_specs_v1[index]
                    .expected_arity) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "assertion evidence constructor role has the wrong shape");
            return PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    for (index = 0u;
         index < PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_LEN; index++) {
        const PPProofGSLTJudgmentV1 *judgment =
            ppproof_sequence_v1_find_judgment(
                &plan->presentation,
                abi->assertion_judgments[index]);
        if (!judgment ||
            judgment->arity !=
                ppproof_assertion_judgment_specs_v1[index]
                    .expected_arity) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "assertion evidence judgment role has the wrong shape");
            return PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    for (index = 0u;
         index < PPPROOF_GSLT_ASSERTION_RULE_V1_LEN; index++) {
        const PPProofGSLTRuleSchemaV1 *rule =
            ppproof_sequence_v1_find_rule(
                &plan->presentation, abi->assertion_rules[index]);
        if (!rule ||
            rule->formal_len !=
                ppproof_assertion_rule_specs_v1[index]
                    .expected_formal_len ||
            rule->premise_len !=
                ppproof_assertion_rule_specs_v1[index]
                    .expected_premise_len ||
            rule->side_condition_len != 0u) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "assertion evidence rule role has the wrong shape");
            return PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

void ppproof_gslt_sequence_evidence_abi_v1_init(
    PPProofGSLTSequenceEvidenceABIV1 *abi) {
    if (abi)
        memset(abi, 0, sizeof(*abi));
}

void ppproof_gslt_sequence_evidence_abi_v1_free(
    PPProofGSLTSequenceEvidenceABIV1 *abi) {
    PPProofGSLTSequenceEvidenceStorageV1 *storage;

    if (!abi)
        return;
    storage = abi->storage;
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    memset(abi, 0, sizeof(*abi));
}

PPProofGSLTArticleV1Result ppproof_gslt_sequence_evidence_abi_v1_load(
    PPProofGSLTSequenceEvidenceABIV1 *abi,
    const char *answer_path,
    const PPProofGSLTPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTSequenceEvidenceABIV1 result;
    PPProofGSLTSequenceEvidenceStorageV1 *storage = NULL;
    size_t index;
    PPProofGSLTArticleV1Result validation;

    memset(&result, 0, sizeof(result));
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!abi || !answer_path || !plan || !plan->storage) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid sequence evidence ABI request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "sequence evidence ABI storage allocation failed");
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    }
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path,
            error_buf, error_buf_size)) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *answer = storage->answers.terms[index];
        const Atom *record;
        PPProofGSLTNameV1 owner;
        PPProofGSLTNameV1 role;
        PPProofGSLTNameV1 target;
        PPProofGSLTNameV1 *slot = NULL;
        int32_t role_index;

        if (!ppproof_sequence_v1_expr_head(
                answer, "proof-sequence-evidence-artifact-v1", 1u))
            goto malformed;
        record = answer->expr.elems[1];
        if (ppproof_sequence_v1_expr_head(
                record,
                "proof-sequence-evidence-constructor-role-v1", 4u)) {
            PPProofGSLTNameV1 base;
            if (!ppproof_sequence_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_sequence_v1_name(record->expr.elems[2], &base) ||
                !ppproof_sequence_v1_name(record->expr.elems[3], &role) ||
                !ppproof_sequence_v1_name(record->expr.elems[4], &target))
                goto malformed;
            role_index = ppproof_sequence_v1_role_index(
                role, ppproof_sequence_constructor_specs_v1,
                PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN);
            if (role_index < 0)
                goto unsupported_role;
            if (!result.base.bytes)
                result.base = base;
            else if (!ppproof_gslt_article_v1_name_equal(
                         result.base, base))
                goto mixed_identity;
            slot = &result.constructors[(uint32_t)role_index];
        } else if (ppproof_sequence_v1_expr_head(
                       record,
                       "proof-sequence-evidence-judgment-role-v1", 3u)) {
            if (!ppproof_sequence_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_sequence_v1_name(record->expr.elems[2], &role) ||
                !ppproof_sequence_v1_name(record->expr.elems[3], &target))
                goto malformed;
            role_index = ppproof_sequence_v1_role_index(
                role, ppproof_sequence_judgment_specs_v1,
                PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LEN);
            if (role_index < 0)
                goto unsupported_role;
            slot = &result.judgments[(uint32_t)role_index];
        } else if (ppproof_sequence_v1_expr_head(
                       record,
                       "proof-sequence-evidence-rule-role-v1", 3u)) {
            if (!ppproof_sequence_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_sequence_v1_name(record->expr.elems[2], &role) ||
                !ppproof_sequence_v1_name(record->expr.elems[3], &target))
                goto malformed;
            role_index = ppproof_sequence_v1_rule_role_index(role);
            if (role_index < 0)
                goto unsupported_role;
            slot = &result.rules[(uint32_t)role_index];
        } else if (ppproof_sequence_v1_expr_head(
                       record,
                       "proof-sequence-evidence-assertion-constructor-role-v1",
                       4u)) {
            PPProofGSLTNameV1 base;
            if (!ppproof_sequence_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_sequence_v1_name(record->expr.elems[2], &base) ||
                !ppproof_sequence_v1_name(record->expr.elems[3], &role) ||
                !ppproof_sequence_v1_name(record->expr.elems[4], &target))
                goto malformed;
            role_index = ppproof_sequence_v1_role_index(
                role, ppproof_assertion_constructor_specs_v1,
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN);
            if (role_index < 0)
                goto unsupported_role;
            if (!result.base.bytes)
                result.base = base;
            else if (!ppproof_gslt_article_v1_name_equal(
                         result.base, base))
                goto mixed_identity;
            slot = &result.assertion_constructors[(uint32_t)role_index];
        } else if (ppproof_sequence_v1_expr_head(
                       record,
                       "proof-sequence-evidence-assertion-judgment-role-v1",
                       3u)) {
            if (!ppproof_sequence_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_sequence_v1_name(record->expr.elems[2], &role) ||
                !ppproof_sequence_v1_name(record->expr.elems[3], &target))
                goto malformed;
            role_index = ppproof_sequence_v1_role_index(
                role, ppproof_assertion_judgment_specs_v1,
                PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_LEN);
            if (role_index < 0)
                goto unsupported_role;
            slot = &result.assertion_judgments[(uint32_t)role_index];
        } else if (ppproof_sequence_v1_expr_head(
                       record,
                       "proof-sequence-evidence-assertion-rule-role-v1",
                       3u)) {
            if (!ppproof_sequence_v1_name(record->expr.elems[1], &owner) ||
                !ppproof_sequence_v1_name(record->expr.elems[2], &role) ||
                !ppproof_sequence_v1_name(record->expr.elems[3], &target))
                goto malformed;
            role_index = ppproof_sequence_v1_rule_role_index_in(
                role, ppproof_assertion_rule_specs_v1,
                PPPROOF_GSLT_ASSERTION_RULE_V1_LEN);
            if (role_index < 0)
                goto unsupported_role;
            slot = &result.assertion_rules[(uint32_t)role_index];
        } else {
            goto malformed;
        }
        if (!result.owner.bytes)
            result.owner = owner;
        else if (!ppproof_gslt_article_v1_name_equal(
                     result.owner, owner))
            goto mixed_identity;
        if (!slot || slot->bytes)
            goto duplicate_role;
        *slot = target;
    }
    result.storage = storage;
    memcpy(result.semantic_digest, storage->answers.digest,
           sizeof(result.semantic_digest));
    validation = ppproof_sequence_v1_validate(
        &result, plan, error_buf, error_buf_size);
    if (validation != PPPROOF_GSLT_ARTICLE_V1_OK)
        goto failed;
    ppproof_gslt_sequence_evidence_abi_v1_free(abi);
    *abi = result;
    return PPPROOF_GSLT_ARTICLE_V1_OK;

malformed:
    ppproof_sequence_v1_set_error(
        error_buf, error_buf_size,
        "sequence evidence ABI contains a malformed record");
    validation = PPPROOF_GSLT_ARTICLE_V1_INVALID;
    goto failed;
unsupported_role:
    ppproof_sequence_v1_set_error(
        error_buf, error_buf_size,
        "sequence evidence ABI contains an unknown role");
    validation = PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;
    goto failed;
mixed_identity:
    ppproof_sequence_v1_set_error(
        error_buf, error_buf_size,
        "sequence evidence ABI mixes extension identities");
    validation = PPPROOF_GSLT_ARTICLE_V1_INVALID;
    goto failed;
duplicate_role:
    ppproof_sequence_v1_set_error(
        error_buf, error_buf_size,
        "sequence evidence ABI repeats a role");
    validation = PPPROOF_GSLT_ARTICLE_V1_INVALID;

failed:
    fh_answer_stream_v1_free(&storage->answers);
    free(storage);
    return validation;
}

typedef struct {
    const PPProofGSLTPatternV1 *head;
    const PPProofGSLTPatternV1 *tail;
    const PPProofGSLTPatternV1 *term;
    bool occupied;
} PPProofGSLTSequenceConsCacheEntryV1;

typedef struct PPProofGSLTSequenceArenaChunkV1 {
    struct PPProofGSLTSequenceArenaChunkV1 *next;
    size_t used;
    size_t capacity;
    max_align_t alignment;
    unsigned char bytes[];
} PPProofGSLTSequenceArenaChunkV1;

typedef struct {
    const PPProofGSLTSequenceEvidenceABIV1 *abi;
    PPProofGSLTArticleNodeV1 *nodes;
    uint32_t node_len;
    uint32_t node_cap;
    uint32_t first_node_id;
    PPProofGSLTSequenceArenaChunkV1 *arena_first;
    PPProofGSLTSequenceArenaChunkV1 *arena_current;
    uint32_t pattern_len;
    const PPProofGSLTPatternV1 *sequence_nil_term;
    PPProofGSLTSequenceConsCacheEntryV1 *sequence_cons_cache;
    uint32_t sequence_cons_cache_len;
    uint32_t sequence_cons_cache_cap;
    PPProofGSLTArticleV1Limits limits;
} PPProofGSLTSequenceEvidenceProducerImplV1;

typedef struct {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    char *error_buf;
    size_t error_buf_size;
    PPProofGSLTArticleV1Result result;
} PPProofGSLTSequenceBuildV1;

typedef struct {
    PPProofGSLTMaterializedSequenceV1 sequence;
    const PPProofGSLTPatternV1 **suffixes;
} PPProofGSLTSequenceMaterialV1;

typedef struct {
    const PPProofGSLTPatternV1 **suffixes;
    PPProofGSLTSequenceMaterialV1 *images;
} PPProofGSLTEnvironmentMaterialV1;

typedef struct {
    PPProofGSLTSequenceArenaChunkV1 *arena_current;
    size_t arena_used;
    uint32_t node_len;
    uint32_t pattern_len;
} PPProofGSLTSequenceCheckpointV1;

static void ppproof_sequence_v1_build_fail(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTArticleV1Result result,
    const char *message) {
    if (build->result != PPPROOF_GSLT_ARTICLE_V1_OK)
        return;
    build->result = result;
    ppproof_sequence_v1_set_error(
        build->error_buf, build->error_buf_size, "%s", message);
}

static void *ppproof_sequence_v1_producer_alloc(
    PPProofGSLTSequenceBuildV1 *build,
    size_t count,
    size_t item_size,
    uint32_t pattern_count) {
    static const size_t minimum_chunk_size = 64u * 1024u;
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTSequenceArenaChunkV1 *chunk;
    size_t alignment = _Alignof(max_align_t);
    size_t byte_len;
    size_t aligned_used;
    void *allocation;

    if (count == 0u)
        return NULL;
    if (item_size != 0u && count > SIZE_MAX / item_size) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence allocation overflows");
        return NULL;
    }
    byte_len = count * item_size;
    if (byte_len == 0u)
        byte_len = 1u;
    if (pattern_count >
        impl->limits.maximum_materialized_pattern_nodes -
            impl->pattern_len) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence patterns exceed their limit");
        return NULL;
    }
    chunk = impl->arena_current;
    aligned_used = 0u;
    if (chunk) {
        size_t remainder = chunk->used % alignment;
        size_t padding = remainder == 0u ? 0u : alignment - remainder;
        aligned_used = chunk->used > SIZE_MAX - padding
            ? SIZE_MAX : chunk->used + padding;
    }
    if (!chunk || aligned_used > chunk->capacity ||
        byte_len > chunk->capacity - aligned_used) {
        size_t capacity = byte_len > minimum_chunk_size
            ? byte_len : minimum_chunk_size;
        PPProofGSLTSequenceArenaChunkV1 *next =
            chunk ? chunk->next : impl->arena_first;

        while (next && next->capacity < byte_len)
            next = next->next;
        if (next) {
            impl->arena_current = next;
            chunk = next;
            aligned_used = 0u;
            goto allocate;
        }

        if (capacity > SIZE_MAX - sizeof(*next)) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
                "sequence evidence region allocation overflows");
            return NULL;
        }
        next = malloc(sizeof(*next) + capacity);
        if (!next) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
                "sequence evidence region allocation failed");
            return NULL;
        }
        next->next = NULL;
        next->used = 0u;
        next->capacity = capacity;
        if (chunk) {
            PPProofGSLTSequenceArenaChunkV1 *tail = chunk;
            while (tail->next)
                tail = tail->next;
            tail->next = next;
        } else {
            impl->arena_first = next;
        }
        impl->arena_current = next;
        chunk = next;
        aligned_used = 0u;
    }
allocate:
    allocation = chunk->bytes + aligned_used;
    memset(allocation, 0, byte_len);
    chunk->used = aligned_used + byte_len;
    impl->pattern_len += pattern_count;
    return allocation;
}

static PPProofGSLTSequenceCheckpointV1
ppproof_sequence_v1_checkpoint(
    const PPProofGSLTSequenceEvidenceProducerImplV1 *impl) {
    return (PPProofGSLTSequenceCheckpointV1){
        .arena_current = impl->arena_current,
        .arena_used = impl->arena_current
            ? impl->arena_current->used : 0u,
        .node_len = impl->node_len,
        .pattern_len = impl->pattern_len,
    };
}

static void ppproof_sequence_v1_arena_chunks_free(
    PPProofGSLTSequenceArenaChunkV1 *chunk) {
    while (chunk) {
        PPProofGSLTSequenceArenaChunkV1 *next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static void ppproof_sequence_v1_rollback(
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl,
    PPProofGSLTSequenceCheckpointV1 checkpoint) {
    if (checkpoint.arena_current) {
        ppproof_sequence_v1_arena_chunks_free(
            checkpoint.arena_current->next);
        checkpoint.arena_current->next = NULL;
        checkpoint.arena_current->used = checkpoint.arena_used;
        impl->arena_current = checkpoint.arena_current;
    } else {
        ppproof_sequence_v1_arena_chunks_free(impl->arena_first);
        impl->arena_first = NULL;
        impl->arena_current = NULL;
    }
    impl->node_len = checkpoint.node_len;
    impl->pattern_len = checkpoint.pattern_len;
    free(impl->sequence_cons_cache);
    impl->sequence_cons_cache = NULL;
    impl->sequence_cons_cache_len = 0u;
    impl->sequence_cons_cache_cap = 0u;
    impl->sequence_nil_term = NULL;
}

static PPProofGSLTPatternV1 *ppproof_sequence_v1_apply(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTNameV1 constructor,
    const PPProofGSLTPatternV1 *const *arguments,
    uint32_t argument_len) {
    PPProofGSLTPatternV1 *result;
    PPProofGSLTPatternV1 *copied = NULL;
    uint32_t index;

    if (!constructor.bytes || constructor.len == 0u) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence evidence uses an empty compiled role");
        return NULL;
    }
    result = ppproof_sequence_v1_producer_alloc(
        build, 1u, sizeof(*result), 1u);
    if (!result)
        return NULL;
    if (argument_len != 0u) {
        copied = ppproof_sequence_v1_producer_alloc(
            build, argument_len, sizeof(*copied), argument_len);
        if (!copied)
            return NULL;
    }
    for (index = 0u; index < argument_len; index++) {
        if (!arguments || !arguments[index]) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
                "sequence evidence has a null pattern argument");
            return NULL;
        }
        copied[index] = *arguments[index];
    }
    result->kind = PPPROOF_GSLT_PATTERN_V1_APPLY;
    result->as.apply.constructor = constructor;
    result->as.apply.arguments = copied;
    result->as.apply.argument_len = argument_len;
    return result;
}

static size_t ppproof_sequence_v1_cons_hash(
    const PPProofGSLTPatternV1 *head,
    const PPProofGSLTPatternV1 *tail) {
    uintptr_t value = (uintptr_t)head;

    value ^= (uintptr_t)tail + (value << 6u) + (value >> 2u);
    value >>= 3u;
    value ^= value >> 16u;
#if UINTPTR_MAX > UINT32_MAX
    value *= UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 32u;
#else
    value *= UINT32_C(2246822519);
    value ^= value >> 16u;
#endif
    return (size_t)value;
}

static const PPProofGSLTPatternV1 *ppproof_sequence_v1_cons_cache_find(
    const PPProofGSLTSequenceEvidenceProducerImplV1 *impl,
    const PPProofGSLTPatternV1 *head,
    const PPProofGSLTPatternV1 *tail) {
    size_t slot;
    uint32_t probes;

    if (impl->sequence_cons_cache_cap == 0u)
        return NULL;
    slot = ppproof_sequence_v1_cons_hash(head, tail) &
           ((size_t)impl->sequence_cons_cache_cap - 1u);
    for (probes = 0u; probes < impl->sequence_cons_cache_cap; probes++) {
        const PPProofGSLTSequenceConsCacheEntryV1 *entry =
            &impl->sequence_cons_cache[slot];
        if (!entry->occupied)
            return NULL;
        if (entry->head == head && entry->tail == tail)
            return entry->term;
        slot = (slot + 1u) &
               ((size_t)impl->sequence_cons_cache_cap - 1u);
    }
    return NULL;
}

static bool ppproof_sequence_v1_cons_cache_insert_raw(
    PPProofGSLTSequenceConsCacheEntryV1 *entries,
    uint32_t cap,
    PPProofGSLTSequenceConsCacheEntryV1 value) {
    size_t slot = ppproof_sequence_v1_cons_hash(value.head, value.tail) &
                  ((size_t)cap - 1u);
    uint32_t probes;

    for (probes = 0u; probes < cap; probes++) {
        PPProofGSLTSequenceConsCacheEntryV1 *entry = &entries[slot];
        if (!entry->occupied) {
            *entry = value;
            entry->occupied = true;
            return true;
        }
        if (entry->head == value.head && entry->tail == value.tail)
            return true;
        slot = (slot + 1u) & ((size_t)cap - 1u);
    }
    return false;
}

static bool ppproof_sequence_v1_cons_cache_grow(
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl) {
    PPProofGSLTSequenceConsCacheEntryV1 *grown;
    uint32_t next_cap = impl->sequence_cons_cache_cap == 0u
                            ? 64u
                            : impl->sequence_cons_cache_cap * 2u;
    uint32_t index;

    if (next_cap < impl->sequence_cons_cache_cap ||
        (size_t)next_cap > SIZE_MAX / sizeof(*grown))
        return false;
    grown = calloc(next_cap, sizeof(*grown));
    if (!grown)
        return false;
    for (index = 0u; index < impl->sequence_cons_cache_cap; index++) {
        if (impl->sequence_cons_cache[index].occupied &&
            !ppproof_sequence_v1_cons_cache_insert_raw(
                grown, next_cap, impl->sequence_cons_cache[index])) {
            free(grown);
            return false;
        }
    }
    free(impl->sequence_cons_cache);
    impl->sequence_cons_cache = grown;
    impl->sequence_cons_cache_cap = next_cap;
    return true;
}

static bool ppproof_sequence_v1_cons_cache_add(
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl,
    const PPProofGSLTPatternV1 *head,
    const PPProofGSLTPatternV1 *tail,
    const PPProofGSLTPatternV1 *term) {
    PPProofGSLTSequenceConsCacheEntryV1 value = {
        .head = head,
        .tail = tail,
        .term = term,
        .occupied = true,
    };

    if (impl->sequence_cons_cache_cap == 0u ||
        ((uint64_t)impl->sequence_cons_cache_len + 1u) * 10u >=
            (uint64_t)impl->sequence_cons_cache_cap * 7u) {
        if (!ppproof_sequence_v1_cons_cache_grow(impl))
            return false;
    }
    if (!ppproof_sequence_v1_cons_cache_insert_raw(
            impl->sequence_cons_cache,
            impl->sequence_cons_cache_cap, value))
        return false;
    impl->sequence_cons_cache_len++;
    return true;
}

static const PPProofGSLTPatternV1 *ppproof_sequence_v1_sequence_nil(
    PPProofGSLTSequenceBuildV1 *build) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;

    if (!impl->sequence_nil_term) {
        impl->sequence_nil_term = ppproof_sequence_v1_apply(
            build,
            impl->abi->constructors[
                PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_NIL],
            NULL, 0u);
    }
    return impl->sequence_nil_term;
}

static const PPProofGSLTPatternV1 *ppproof_sequence_v1_sequence_cons(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTPatternV1 *head,
    const PPProofGSLTPatternV1 *tail) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    const PPProofGSLTPatternV1 *cached =
        ppproof_sequence_v1_cons_cache_find(impl, head, tail);
    const PPProofGSLTPatternV1 *arguments[2];
    PPProofGSLTPatternV1 *term;

    if (cached)
        return cached;
    arguments[0] = head;
    arguments[1] = tail;
    term = ppproof_sequence_v1_apply(
        build,
        impl->abi->constructors[
            PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS],
        arguments, 2u);
    if (!term)
        return NULL;
    if (!ppproof_sequence_v1_cons_cache_add(impl, head, tail, term)) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence canonical cache exceeds its limit");
        return NULL;
    }
    return term;
}

static bool ppproof_sequence_v1_token_valid(
    const PPProofGSLTSequenceTokenV1 *token) {
    return token && token->term && token->literal != token->variable;
}

static bool ppproof_sequence_v1_sequence_valid(
    PPProofGSLTTokenSequenceV1 sequence) {
    uint32_t index;

    if (sequence.token_len != 0u && !sequence.tokens)
        return false;
    for (index = 0u; index < sequence.token_len; index++) {
        if (!ppproof_sequence_v1_token_valid(sequence.tokens[index]))
            return false;
    }
    return true;
}

static bool ppproof_sequence_v1_environment_valid(
    PPProofGSLTSequenceEnvironmentV1 environment) {
    uint32_t left;
    uint32_t right;

    if (environment.binding_len != 0u && !environment.bindings)
        return false;
    for (left = 0u; left < environment.binding_len; left++) {
        const PPProofGSLTSequenceBindingV1 *binding =
            &environment.bindings[left];
        if (!ppproof_sequence_v1_token_valid(binding->key) ||
            !binding->key->variable ||
            !ppproof_sequence_v1_sequence_valid(binding->image))
            return false;
        for (right = left + 1u; right < environment.binding_len;
             right++) {
            if (binding->key == environment.bindings[right].key)
                return false;
        }
    }
    return true;
}

static bool ppproof_sequence_v1_materialize_sequence(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTTokenSequenceV1 source,
    PPProofGSLTSequenceMaterialV1 *out) {
    const PPProofGSLTPatternV1 **suffixes;
    uint32_t cursor;

    if (!out || !ppproof_sequence_v1_sequence_valid(source)) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence evidence received a malformed token sequence");
        return false;
    }
    if (source.token_len == UINT32_MAX) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence suffix count overflows");
        return false;
    }
    suffixes = ppproof_sequence_v1_producer_alloc(
        build, (size_t)source.token_len + 1u,
        sizeof(*suffixes), 0u);
    if (!suffixes)
        return false;
    suffixes[source.token_len] = ppproof_sequence_v1_sequence_nil(build);
    if (!suffixes[source.token_len])
        return false;
    cursor = source.token_len;
    while (cursor > 0u) {
        cursor--;
        suffixes[cursor] = ppproof_sequence_v1_sequence_cons(
            build, source.tokens[cursor]->term,
            suffixes[cursor + 1u]);
        if (!suffixes[cursor])
            return false;
    }
    *out = (PPProofGSLTSequenceMaterialV1){
        .sequence = {
            .term = suffixes[0],
            .tokens = source.tokens,
            .token_len = source.token_len,
        },
        .suffixes = suffixes,
    };
    return true;
}

static bool ppproof_sequence_v1_materialize_environment(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTSequenceEnvironmentV1 source,
    PPProofGSLTEnvironmentMaterialV1 *out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    const PPProofGSLTPatternV1 **suffixes;
    PPProofGSLTSequenceMaterialV1 *images = NULL;
    uint32_t cursor;

    if (!out || !ppproof_sequence_v1_environment_valid(source) ||
        source.binding_len == UINT32_MAX) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence evidence received a malformed environment");
        return false;
    }
    suffixes = ppproof_sequence_v1_producer_alloc(
        build, (size_t)source.binding_len + 1u,
        sizeof(*suffixes), 0u);
    if (!suffixes)
        return false;
    if (source.binding_len != 0u) {
        images = ppproof_sequence_v1_producer_alloc(
            build, source.binding_len, sizeof(*images), 0u);
        if (!images)
            return false;
    }
    for (cursor = 0u; cursor < source.binding_len; cursor++) {
        if (!ppproof_sequence_v1_materialize_sequence(
                build, source.bindings[cursor].image,
                &images[cursor]))
            return false;
    }
    suffixes[source.binding_len] = ppproof_sequence_v1_apply(
        build,
        impl->abi->constructors[
            PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_ENVIRONMENT_NIL],
        NULL, 0u);
    if (!suffixes[source.binding_len])
        return false;
    cursor = source.binding_len;
    while (cursor > 0u) {
        const PPProofGSLTPatternV1 *arguments[3];
        cursor--;
        arguments[0] = source.bindings[cursor].key->term;
        arguments[1] = images[cursor].sequence.term;
        arguments[2] = suffixes[cursor + 1u];
        suffixes[cursor] = ppproof_sequence_v1_apply(
            build,
            impl->abi->constructors[
                PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_ENVIRONMENT_CONS],
            arguments, 3u);
        if (!suffixes[cursor])
            return false;
    }
    *out = (PPProofGSLTEnvironmentMaterialV1){
        .suffixes = suffixes,
        .images = images,
    };
    return true;
}

static bool ppproof_sequence_v1_grow_nodes(
    PPProofGSLTSequenceBuildV1 *build) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTArticleNodeV1 *next;
    uint32_t next_cap;

    if (impl->node_len < impl->node_cap)
        return true;
    next_cap = impl->node_cap ? impl->node_cap * 2u : 64u;
    if (next_cap < impl->node_cap ||
        next_cap > impl->limits.maximum_article_nodes ||
        (size_t)next_cap > SIZE_MAX / sizeof(*next)) {
        next_cap = impl->limits.maximum_article_nodes;
        if (next_cap <= impl->node_cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*next)) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
                "sequence evidence nodes exceed their limit");
            return false;
        }
    }
    next = realloc(impl->nodes, (size_t)next_cap * sizeof(*next));
    if (!next) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence node allocation failed");
        return false;
    }
    impl->nodes = next;
    impl->node_cap = next_cap;
    return true;
}

static bool ppproof_sequence_v1_add_node(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTNameV1 rule,
    const PPProofGSLTPatternV1 *const *arguments,
    uint32_t argument_len,
    const PPProofGSLTReferenceV1 *children,
    uint32_t child_len,
    PPProofGSLTReferenceV1 *reference_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTPatternV1 *copied_arguments = NULL;
    PPProofGSLTReferenceV1 *copied_children = NULL;
    PPProofGSLTArticleNodeV1 *node;
    uint32_t index;
    uint32_t id;

    if (!rule.bytes || rule.len == 0u || !reference_out ||
        (argument_len != 0u && !arguments) ||
        (child_len != 0u && !children)) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence evidence node request is malformed");
        return false;
    }
    if (impl->node_len >= impl->limits.maximum_article_nodes) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence nodes exceed their limit");
        return false;
    }
    if (impl->node_len > UINT32_MAX - impl->first_node_id) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence node indices overflow");
        return false;
    }
    if (!ppproof_sequence_v1_grow_nodes(build))
        return false;
    if (argument_len != 0u) {
        copied_arguments = ppproof_sequence_v1_producer_alloc(
            build, argument_len, sizeof(*copied_arguments),
            argument_len);
        if (!copied_arguments)
            return false;
    }
    for (index = 0u; index < argument_len; index++) {
        if (!arguments[index]) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
                "sequence evidence node has a null argument");
            return false;
        }
        copied_arguments[index] = *arguments[index];
    }
    if (child_len != 0u) {
        copied_children = ppproof_sequence_v1_producer_alloc(
            build, child_len, sizeof(*copied_children), 0u);
        if (!copied_children)
            return false;
        memcpy(copied_children, children,
               (size_t)child_len * sizeof(*copied_children));
    }
    id = impl->first_node_id + impl->node_len;
    node = &impl->nodes[impl->node_len++];
    *node = (PPProofGSLTArticleNodeV1){
        .id = id,
        .rule_instance = {
            .rule_id = rule,
            .arguments = copied_arguments,
            .argument_len = argument_len,
        },
        .children = copied_children,
        .child_len = child_len,
    };
    *reference_out = (PPProofGSLTReferenceV1){
        .kind = PPPROOF_GSLT_REFERENCE_V1_NODE,
        .index = id,
    };
    return true;
}

static bool ppproof_sequence_v1_goal(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTNameV1 judgment,
    const PPProofGSLTPatternV1 *const *arguments,
    uint32_t argument_len,
    PPProofGSLTReferenceV1 evidence,
    PPProofGSLTSequenceProofV1 *out) {
    PPProofGSLTPatternV1 *goal = ppproof_sequence_v1_apply(
        build, judgment, arguments, argument_len);

    if (!goal)
        return false;
    *out = (PPProofGSLTSequenceProofV1){
        .goal = goal,
        .evidence = evidence,
    };
    return true;
}

static bool ppproof_sequence_v1_concat_tokens(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right,
    PPProofGSLTTokenSequenceV1 *out) {
    const PPProofGSLTSequenceTokenV1 **tokens = NULL;
    uint32_t total;

    if (left.token_len > UINT32_MAX - right.token_len) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence concatenation overflows");
        return false;
    }
    total = left.token_len + right.token_len;
    if (total != 0u) {
        tokens = ppproof_sequence_v1_producer_alloc(
            build, total, sizeof(*tokens), 0u);
        if (!tokens)
            return false;
        if (left.token_len != 0u)
            memcpy(tokens, left.tokens,
                   (size_t)left.token_len * sizeof(*tokens));
        if (right.token_len != 0u)
            memcpy(tokens + left.token_len, right.tokens,
                   (size_t)right.token_len * sizeof(*tokens));
    }
    *out = (PPProofGSLTTokenSequenceV1){
        .tokens = tokens,
        .token_len = total,
    };
    return true;
}

static bool ppproof_sequence_v1_append(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right,
    PPProofGSLTSequenceProofV1 *proof_out,
    PPProofGSLTSequenceMaterialV1 *result_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTSequenceMaterialV1 left_material;
    PPProofGSLTSequenceMaterialV1 right_material;
    PPProofGSLTSequenceMaterialV1 result_material;
    PPProofGSLTTokenSequenceV1 result_tokens;
    PPProofGSLTReferenceV1 evidence;
    const PPProofGSLTPatternV1 *arguments[4];
    PPProofGSLTReferenceV1 child;
    uint32_t cursor;

    if (!ppproof_sequence_v1_materialize_sequence(
            build, left, &left_material) ||
        !ppproof_sequence_v1_materialize_sequence(
            build, right, &right_material) ||
        !ppproof_sequence_v1_concat_tokens(
            build, left, right, &result_tokens) ||
        !ppproof_sequence_v1_materialize_sequence(
            build, result_tokens, &result_material))
        return false;
    arguments[0] = right_material.sequence.term;
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_APPEND_NIL],
            arguments, 1u, NULL, 0u, &evidence))
        return false;
    cursor = left.token_len;
    while (cursor > 0u) {
        cursor--;
        arguments[0] = left.tokens[cursor]->term;
        arguments[1] = left_material.suffixes[cursor + 1u];
        arguments[2] = right_material.sequence.term;
        arguments[3] = result_material.suffixes[cursor + 1u];
        child = evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->rules[
                    PPPROOF_GSLT_SEQUENCE_RULE_V1_APPEND_CONS],
                arguments, 4u, &child, 1u, &evidence))
            return false;
    }
    arguments[0] = left_material.sequence.term;
    arguments[1] = right_material.sequence.term;
    arguments[2] = result_material.sequence.term;
    if (!ppproof_sequence_v1_goal(
            build,
            impl->abi->judgments[
                PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_APPEND],
            arguments, 3u, evidence, proof_out))
        return false;
    *result_out = result_material;
    return true;
}

static bool ppproof_sequence_v1_material_slice(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTSequenceMaterialV1 *source,
    uint32_t offset,
    PPProofGSLTSequenceMaterialV1 *out) {
    uint32_t remaining;

    if (!source || !source->suffixes || !out ||
        offset > source->sequence.token_len) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence evidence requested a malformed material slice");
        return false;
    }
    remaining = source->sequence.token_len - offset;
    *out = (PPProofGSLTSequenceMaterialV1){
        .sequence = {
            .term = source->suffixes[offset],
            .tokens = remaining != 0u
                          ? source->sequence.tokens + offset
                          : NULL,
            .token_len = remaining,
        },
        .suffixes = source->suffixes + offset,
    };
    return true;
}

static bool ppproof_sequence_v1_tokens_equal_at(
    PPProofGSLTTokenSequenceV1 source,
    uint32_t offset,
    PPProofGSLTTokenSequenceV1 expected) {
    uint32_t index;

    if (offset > source.token_len ||
        expected.token_len > source.token_len - offset)
        return false;
    for (index = 0u; index < expected.token_len; index++) {
        if (source.tokens[offset + index] != expected.tokens[index])
            return false;
    }
    return true;
}

static bool ppproof_sequence_v1_append_expected(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTSequenceMaterialV1 *left,
    const PPProofGSLTSequenceMaterialV1 *right,
    const PPProofGSLTSequenceMaterialV1 *result,
    PPProofGSLTSequenceProofV1 *proof_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTReferenceV1 evidence;
    PPProofGSLTReferenceV1 child;
    const PPProofGSLTPatternV1 *arguments[4];
    uint32_t total;
    uint32_t cursor;

    if (!left || !right || !result || !proof_out) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence evidence received a malformed expected append");
        return false;
    }
    if (left->sequence.token_len >
        UINT32_MAX - right->sequence.token_len) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
            "sequence evidence expected append overflows");
        return false;
    }
    total = left->sequence.token_len + right->sequence.token_len;
    if (result->sequence.token_len != total ||
        !ppproof_sequence_v1_tokens_equal_at(
            (PPProofGSLTTokenSequenceV1){
                result->sequence.tokens,
                result->sequence.token_len,
            },
            0u,
            (PPProofGSLTTokenSequenceV1){
                left->sequence.tokens,
                left->sequence.token_len,
            }) ||
        !ppproof_sequence_v1_tokens_equal_at(
            (PPProofGSLTTokenSequenceV1){
                result->sequence.tokens,
                result->sequence.token_len,
            },
            left->sequence.token_len,
            (PPProofGSLTTokenSequenceV1){
                right->sequence.tokens,
                right->sequence.token_len,
            })) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
            "sequence evidence expected append does not match the premise");
        return false;
    }

    arguments[0] = right->sequence.term;
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_APPEND_NIL],
            arguments, 1u, NULL, 0u, &evidence))
        return false;
    cursor = left->sequence.token_len;
    while (cursor > 0u) {
        cursor--;
        arguments[0] = left->sequence.tokens[cursor]->term;
        arguments[1] = left->suffixes[cursor + 1u];
        arguments[2] = right->sequence.term;
        arguments[3] = result->suffixes[cursor + 1u];
        child = evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->rules[
                    PPPROOF_GSLT_SEQUENCE_RULE_V1_APPEND_CONS],
                arguments, 4u, &child, 1u, &evidence))
            return false;
    }
    arguments[0] = left->sequence.term;
    arguments[1] = right->sequence.term;
    arguments[2] = result->sequence.term;
    return ppproof_sequence_v1_goal(
        build,
        impl->abi->judgments[
            PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_APPEND],
        arguments, 3u, evidence, proof_out);
}

static int32_t ppproof_sequence_v1_find_binding(
    PPProofGSLTSequenceEnvironmentV1 environment,
    const PPProofGSLTSequenceTokenV1 *key) {
    uint32_t index;

    for (index = 0u; index < environment.binding_len; index++) {
        if (environment.bindings[index].key == key)
            return (int32_t)index;
    }
    return -1;
}

static bool ppproof_sequence_v1_lookup(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTSequenceEnvironmentV1 environment,
    const PPProofGSLTEnvironmentMaterialV1 *material,
    const PPProofGSLTSequenceTokenV1 *key,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTSequenceProofV1 *proof_out,
    PPProofGSLTSequenceMaterialV1 *image_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    const PPProofGSLTPatternV1 *arguments[5];
    PPProofGSLTReferenceV1 evidence;
    PPProofGSLTReferenceV1 children[2];
    int32_t found = ppproof_sequence_v1_find_binding(environment, key);
    uint32_t cursor;

    if (found < 0 || !material || !sources) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
            "sequence evidence cannot find a substitution binding");
        return false;
    }
    cursor = (uint32_t)found;
    arguments[0] = key->term;
    arguments[1] = material->images[cursor].sequence.term;
    arguments[2] = material->suffixes[cursor + 1u];
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_LOOKUP_HEAD],
            arguments, 3u, NULL, 0u, &evidence))
        return false;
    while (cursor > 0u) {
        const PPProofGSLTSequenceTokenV1 *head;
        cursor--;
        head = environment.bindings[cursor].key;
        if (!sources->different ||
            !sources->different(
                sources->context, head, key, &children[0])) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                "sequence evidence lacks a key-difference witness");
            return false;
        }
        children[1] = evidence;
        arguments[0] = head->term;
        arguments[1] = material->images[cursor].sequence.term;
        arguments[2] = material->suffixes[cursor + 1u];
        arguments[3] = key->term;
        arguments[4] = material->images[(uint32_t)found].sequence.term;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->rules[
                    PPPROOF_GSLT_SEQUENCE_RULE_V1_LOOKUP_TAIL],
                arguments, 5u, children, 2u, &evidence))
            return false;
    }
    arguments[0] = material->suffixes[0];
    arguments[1] = key->term;
    arguments[2] = material->images[(uint32_t)found].sequence.term;
    if (!ppproof_sequence_v1_goal(
            build,
            impl->abi->judgments[
                PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LOOKUP],
            arguments, 3u, evidence, proof_out))
        return false;
    *image_out = material->images[(uint32_t)found];
    return true;
}

static bool ppproof_sequence_v1_prepend_token(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTSequenceTokenV1 *head,
    PPProofGSLTTokenSequenceV1 tail,
    PPProofGSLTTokenSequenceV1 *out) {
    PPProofGSLTTokenSequenceV1 singleton;
    const PPProofGSLTSequenceTokenV1 *single[1];

    single[0] = head;
    singleton = (PPProofGSLTTokenSequenceV1){
        .tokens = single,
        .token_len = 1u,
    };
    return ppproof_sequence_v1_concat_tokens(
        build, singleton, tail, out);
}

static bool ppproof_sequence_v1_instantiate_build(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTTokenSequenceV1 source,
    PPProofGSLTSequenceEnvironmentV1 environment,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTMaterializedSequenceV1 *result_out,
    PPProofGSLTSequenceProofV1 *proof_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTSequenceMaterialV1 source_material;
    PPProofGSLTEnvironmentMaterialV1 environment_material;
    PPProofGSLTSequenceMaterialV1 current_material;
    PPProofGSLTTokenSequenceV1 current_tokens = {0};
    PPProofGSLTReferenceV1 current_evidence;
    const PPProofGSLTPatternV1 *arguments[6];
    uint32_t cursor;

    if (!sources ||
        !ppproof_sequence_v1_materialize_sequence(
            build, source, &source_material) ||
        !ppproof_sequence_v1_materialize_environment(
            build, environment, &environment_material) ||
        !ppproof_sequence_v1_materialize_sequence(
            build, current_tokens, &current_material))
        return false;
    arguments[0] = environment_material.suffixes[0];
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_NIL],
            arguments, 1u, NULL, 0u, &current_evidence))
        return false;

    cursor = source.token_len;
    while (cursor > 0u) {
        const PPProofGSLTSequenceTokenV1 *token;
        PPProofGSLTTokenSequenceV1 next_tokens;
        PPProofGSLTSequenceMaterialV1 next_material;
        PPProofGSLTReferenceV1 children[4];
        cursor--;
        token = source.tokens[cursor];
        if (token->literal) {
            if (!ppproof_sequence_v1_prepend_token(
                    build, token, current_tokens, &next_tokens) ||
                !ppproof_sequence_v1_materialize_sequence(
                    build, next_tokens, &next_material))
                return false;
            arguments[0] = token->term;
            arguments[1] = source_material.suffixes[cursor + 1u];
            arguments[2] = environment_material.suffixes[0];
            arguments[3] = current_material.sequence.term;
            children[0] = token->literal_evidence;
            children[1] = current_evidence;
            if (!ppproof_sequence_v1_add_node(
                    build,
                    impl->abi->rules[
                        PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_LITERAL],
                    arguments, 4u, children, 2u,
                    &current_evidence))
                return false;
        } else {
            PPProofGSLTSequenceProofV1 lookup_proof;
            PPProofGSLTSequenceProofV1 append_proof;
            PPProofGSLTSequenceMaterialV1 image_material;
            PPProofGSLTSequenceMaterialV1 appended_material;
            int32_t binding = ppproof_sequence_v1_find_binding(
                environment, token);

            if (binding < 0 ||
                !ppproof_sequence_v1_lookup(
                    build, environment, &environment_material,
                    token, sources, &lookup_proof, &image_material) ||
                !ppproof_sequence_v1_append(
                    build, environment.bindings[(uint32_t)binding].image,
                    current_tokens, &append_proof,
                    &appended_material))
                return false;
            next_tokens = (PPProofGSLTTokenSequenceV1){
                .tokens = appended_material.sequence.tokens,
                .token_len = appended_material.sequence.token_len,
            };
            next_material = appended_material;
            arguments[0] = token->term;
            arguments[1] = source_material.suffixes[cursor + 1u];
            arguments[2] = environment_material.suffixes[0];
            arguments[3] = image_material.sequence.term;
            arguments[4] = current_material.sequence.term;
            arguments[5] = appended_material.sequence.term;
            children[0] = token->variable_evidence;
            children[1] = lookup_proof.evidence;
            children[2] = current_evidence;
            children[3] = append_proof.evidence;
            if (!ppproof_sequence_v1_add_node(
                    build,
                    impl->abi->rules[
                        PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_VARIABLE],
                    arguments, 6u, children, 4u,
                    &current_evidence))
                return false;
        }
        current_tokens = next_tokens;
        current_material = next_material;
    }
    arguments[0] = source_material.sequence.term;
    arguments[1] = environment_material.suffixes[0];
    arguments[2] = current_material.sequence.term;
    if (!ppproof_sequence_v1_goal(
            build,
            impl->abi->judgments[
                PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_INSTANTIATE],
            arguments, 3u, current_evidence, proof_out))
        return false;
    *result_out = current_material.sequence;
    return true;
}

static bool ppproof_sequence_v1_instantiate_expected_build(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTTokenSequenceV1 source,
    PPProofGSLTSequenceEnvironmentV1 environment,
    PPProofGSLTTokenSequenceV1 expected,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTMaterializedSequenceV1 *result_out,
    PPProofGSLTSequenceProofV1 *proof_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTSequenceMaterialV1 source_material;
    PPProofGSLTEnvironmentMaterialV1 environment_material;
    PPProofGSLTSequenceMaterialV1 expected_material;
    PPProofGSLTSequenceMaterialV1 current_material;
    PPProofGSLTReferenceV1 current_evidence;
    const PPProofGSLTPatternV1 *arguments[6];
    uint32_t source_cursor;
    uint32_t expected_cursor;

    if (!sources ||
        !ppproof_sequence_v1_materialize_sequence(
            build, source, &source_material) ||
        !ppproof_sequence_v1_materialize_environment(
            build, environment, &environment_material) ||
        !ppproof_sequence_v1_materialize_sequence(
            build, expected, &expected_material) ||
        !ppproof_sequence_v1_material_slice(
            build, &expected_material, expected.token_len,
            &current_material))
        return false;
    arguments[0] = environment_material.suffixes[0];
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_NIL],
            arguments, 1u, NULL, 0u, &current_evidence))
        return false;

    source_cursor = source.token_len;
    expected_cursor = expected.token_len;
    while (source_cursor > 0u) {
        const PPProofGSLTSequenceTokenV1 *token;
        PPProofGSLTSequenceMaterialV1 next_material;
        PPProofGSLTReferenceV1 children[4];

        source_cursor--;
        token = source.tokens[source_cursor];
        if (token->literal) {
            if (expected_cursor == 0u ||
                expected.tokens[expected_cursor - 1u] != token) {
                ppproof_sequence_v1_build_fail(
                    build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                    "literal instantiation does not match the premise");
                return false;
            }
            expected_cursor--;
            if (!ppproof_sequence_v1_material_slice(
                    build, &expected_material, expected_cursor,
                    &next_material))
                return false;
            arguments[0] = token->term;
            arguments[1] = source_material.suffixes[source_cursor + 1u];
            arguments[2] = environment_material.suffixes[0];
            arguments[3] = current_material.sequence.term;
            children[0] = token->literal_evidence;
            children[1] = current_evidence;
            if (!ppproof_sequence_v1_add_node(
                    build,
                    impl->abi->rules[
                        PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_LITERAL],
                    arguments, 4u, children, 2u,
                    &current_evidence))
                return false;
        } else {
            PPProofGSLTSequenceProofV1 lookup_proof;
            PPProofGSLTSequenceProofV1 append_proof;
            PPProofGSLTSequenceMaterialV1 image_material;
            PPProofGSLTTokenSequenceV1 image;
            int32_t binding = ppproof_sequence_v1_find_binding(
                environment, token);
            uint32_t next_cursor;

            if (binding < 0) {
                ppproof_sequence_v1_build_fail(
                    build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                    "sequence evidence cannot find a substitution binding");
                return false;
            }
            image = environment.bindings[(uint32_t)binding].image;
            if (image.token_len > expected_cursor) {
                ppproof_sequence_v1_build_fail(
                    build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                    "variable instantiation is longer than the premise");
                return false;
            }
            next_cursor = expected_cursor - image.token_len;
            if (!ppproof_sequence_v1_tokens_equal_at(
                    expected, next_cursor, image)) {
                ppproof_sequence_v1_build_fail(
                    build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                    "variable instantiation does not match the premise");
                return false;
            }
            if (!ppproof_sequence_v1_lookup(
                    build, environment, &environment_material,
                    token, sources, &lookup_proof, &image_material) ||
                !ppproof_sequence_v1_material_slice(
                    build, &expected_material, next_cursor,
                    &next_material) ||
                !ppproof_sequence_v1_append_expected(
                    build, &image_material, &current_material,
                    &next_material, &append_proof))
                return false;
            arguments[0] = token->term;
            arguments[1] = source_material.suffixes[source_cursor + 1u];
            arguments[2] = environment_material.suffixes[0];
            arguments[3] = image_material.sequence.term;
            arguments[4] = current_material.sequence.term;
            arguments[5] = next_material.sequence.term;
            children[0] = token->variable_evidence;
            children[1] = lookup_proof.evidence;
            children[2] = current_evidence;
            children[3] = append_proof.evidence;
            if (!ppproof_sequence_v1_add_node(
                    build,
                    impl->abi->rules[
                        PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_VARIABLE],
                    arguments, 6u, children, 4u,
                    &current_evidence))
                return false;
            expected_cursor = next_cursor;
        }
        current_material = next_material;
    }
    if (expected_cursor != 0u) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
            "instantiation does not consume the complete premise");
        return false;
    }
    arguments[0] = source_material.sequence.term;
    arguments[1] = environment_material.suffixes[0];
    arguments[2] = current_material.sequence.term;
    if (!ppproof_sequence_v1_goal(
            build,
            impl->abi->judgments[
                PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_INSTANTIATE],
            arguments, 3u, current_evidence, proof_out))
        return false;
    *result_out = current_material.sequence;
    return true;
}

static bool ppproof_sequence_v1_pair_allowed(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTSequenceProofV1 *proof_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    const PPProofGSLTPatternV1 *arguments[2];
    PPProofGSLTReferenceV1 children[3];
    PPProofGSLTReferenceV1 evidence;
    PPProofGSLTNameV1 rule;
    uint32_t child_len;

    if (!ppproof_sequence_v1_token_valid(left) ||
        !ppproof_sequence_v1_token_valid(right) || !sources) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "sequence apartness received a malformed token");
        return false;
    }
    arguments[0] = left->term;
    arguments[1] = right->term;
    if (left->literal) {
        rule = impl->abi->rules[
            PPPROOF_GSLT_SEQUENCE_RULE_V1_PAIR_LEFT_LITERAL];
        children[0] = left->literal_evidence;
        child_len = 1u;
    } else if (right->literal) {
        rule = impl->abi->rules[
            PPPROOF_GSLT_SEQUENCE_RULE_V1_PAIR_RIGHT_LITERAL];
        children[0] = right->literal_evidence;
        child_len = 1u;
    } else {
        if (!sources->apart ||
            !sources->apart(
                sources->context, left, right, &children[2])) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                "sequence evidence lacks an apartness witness");
            return false;
        }
        rule = impl->abi->rules[
            PPPROOF_GSLT_SEQUENCE_RULE_V1_PAIR_APART];
        children[0] = left->variable_evidence;
        children[1] = right->variable_evidence;
        child_len = 3u;
    }
    if (!ppproof_sequence_v1_add_node(
            build, rule, arguments, 2u, children, child_len,
            &evidence))
        return false;
    return ppproof_sequence_v1_goal(
        build,
        impl->abi->judgments[
            PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_PAIR_ALLOWED],
        arguments, 2u, evidence, proof_out);
}

static bool ppproof_sequence_v1_token_against(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTSequenceTokenV1 *token,
    PPProofGSLTTokenSequenceV1 sequence,
    const PPProofGSLTSequenceMaterialV1 *material,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTSequenceProofV1 *proof_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    const PPProofGSLTPatternV1 *arguments[3];
    PPProofGSLTReferenceV1 evidence;
    uint32_t cursor;

    arguments[0] = token->term;
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_TOKEN_AGAINST_NIL],
            arguments, 1u, NULL, 0u, &evidence))
        return false;
    cursor = sequence.token_len;
    while (cursor > 0u) {
        PPProofGSLTSequenceProofV1 pair;
        PPProofGSLTReferenceV1 children[2];
        cursor--;
        if (!ppproof_sequence_v1_pair_allowed(
                build, token, sequence.tokens[cursor],
                sources, &pair))
            return false;
        arguments[0] = token->term;
        arguments[1] = sequence.tokens[cursor]->term;
        arguments[2] = material->suffixes[cursor + 1u];
        children[0] = pair.evidence;
        children[1] = evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->rules[
                    PPPROOF_GSLT_SEQUENCE_RULE_V1_TOKEN_AGAINST_CONS],
                arguments, 3u, children, 2u, &evidence))
            return false;
    }
    arguments[0] = token->term;
    arguments[1] = material->sequence.term;
    return ppproof_sequence_v1_goal(
        build,
        impl->abi->judgments[
            PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_TOKEN_AGAINST_SEQUENCE],
        arguments, 2u, evidence, proof_out);
}

static bool ppproof_sequence_v1_support_apart_build(
    PPProofGSLTSequenceBuildV1 *build,
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTSequenceProofV1 *proof_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTSequenceMaterialV1 left_material;
    PPProofGSLTSequenceMaterialV1 right_material;
    const PPProofGSLTPatternV1 *arguments[3];
    PPProofGSLTReferenceV1 evidence;
    uint32_t cursor;

    if (!sources ||
        !ppproof_sequence_v1_materialize_sequence(
            build, left, &left_material) ||
        !ppproof_sequence_v1_materialize_sequence(
            build, right, &right_material))
        return false;
    arguments[0] = right_material.sequence.term;
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->rules[
                PPPROOF_GSLT_SEQUENCE_RULE_V1_SUPPORT_APART_NIL],
            arguments, 1u, NULL, 0u, &evidence))
        return false;
    cursor = left.token_len;
    while (cursor > 0u) {
        PPProofGSLTSequenceProofV1 against;
        PPProofGSLTReferenceV1 children[2];
        cursor--;
        if (!ppproof_sequence_v1_token_against(
                build, left.tokens[cursor], right,
                &right_material, sources, &against))
            return false;
        arguments[0] = left.tokens[cursor]->term;
        arguments[1] = left_material.suffixes[cursor + 1u];
        arguments[2] = right_material.sequence.term;
        children[0] = against.evidence;
        children[1] = evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->rules[
                    PPPROOF_GSLT_SEQUENCE_RULE_V1_SUPPORT_APART_CONS],
                arguments, 3u, children, 2u, &evidence))
            return false;
    }
    arguments[0] = left_material.sequence.term;
    arguments[1] = right_material.sequence.term;
    return ppproof_sequence_v1_goal(
        build,
        impl->abi->judgments[
            PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_SUPPORT_APART],
        arguments, 2u, evidence, proof_out);
}

static bool ppproof_sequence_v1_apply_assertion_build(
    PPProofGSLTSequenceBuildV1 *build,
    const PPProofGSLTAssertionDeclarationV1 *declaration,
    uint32_t declaration_premise_index,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTAssertionApplicationV1 *application_out) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl = build->impl;
    PPProofGSLTSequenceBindingV1 *bindings = NULL;
    PPProofGSLTSequenceEnvironmentV1 environment;
    PPProofGSLTEnvironmentMaterialV1 environment_material;
    const PPProofGSLTPatternV1 *list_nil;
    const PPProofGSLTPatternV1 *variables;
    const PPProofGSLTPatternV1 *images;
    const PPProofGSLTPatternV1 *essentials;
    const PPProofGSLTPatternV1 *actuals;
    const PPProofGSLTPatternV1 *disjoints;
    PPProofGSLTReferenceV1 environment_evidence;
    PPProofGSLTReferenceV1 essential_evidence;
    PPProofGSLTReferenceV1 disjoint_evidence;
    PPProofGSLTReferenceV1 application_evidence;
    PPProofGSLTSequenceMaterialV1 conclusion_template;
    PPProofGSLTSequenceProofV1 conclusion_instantiation;
    const PPProofGSLTPatternV1 *assertion;
    const PPProofGSLTPatternV1 *typed_result;
    const PPProofGSLTPatternV1 *arguments[9];
    PPProofGSLTReferenceV1 children[5];
    uint32_t cursor;

    if (!declaration || !application_out || !sources ||
        !declaration->conclusion_type ||
        (declaration->binding_len != 0u && !declaration->bindings) ||
        (declaration->essential_len != 0u && !declaration->essentials) ||
        (declaration->disjoint_len != 0u && !declaration->disjoints) ||
        !ppproof_sequence_v1_sequence_valid(
            declaration->conclusion_template)) {
        ppproof_sequence_v1_build_fail(
            build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
            "assertion evidence received a malformed declaration");
        return false;
    }
    if (declaration->binding_len != 0u) {
        bindings = ppproof_sequence_v1_producer_alloc(
            build, declaration->binding_len, sizeof(*bindings), 0u);
        if (!bindings)
            return false;
    }
    for (cursor = 0u; cursor < declaration->binding_len; cursor++) {
        const PPProofGSLTAssertionBindingV1 *binding =
            &declaration->bindings[cursor];
        if (!binding->typecode ||
            !ppproof_sequence_v1_token_valid(binding->variable) ||
            !binding->variable->variable ||
            !ppproof_sequence_v1_sequence_valid(binding->image)) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
                "assertion evidence contains a malformed binding");
            return false;
        }
        bindings[cursor] = (PPProofGSLTSequenceBindingV1){
            .key = binding->variable,
            .image = binding->image,
        };
    }
    for (cursor = 0u; cursor < declaration->essential_len; cursor++) {
        if (!ppproof_sequence_v1_sequence_valid(
                declaration->essentials[cursor].template_sequence) ||
            !ppproof_sequence_v1_sequence_valid(
                declaration->essentials[cursor].actual_sequence)) {
            ppproof_sequence_v1_build_fail(
                build, PPPROOF_GSLT_ARTICLE_V1_INVALID,
                "assertion evidence contains a malformed essential");
            return false;
        }
    }
    environment = (PPProofGSLTSequenceEnvironmentV1){
        .bindings = bindings,
        .binding_len = declaration->binding_len,
    };
    if (!ppproof_sequence_v1_environment_valid(environment) ||
        !ppproof_sequence_v1_materialize_environment(
            build, environment, &environment_material))
        return false;

    list_nil = ppproof_sequence_v1_apply(
        build,
        impl->abi->assertion_constructors[
            PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_NIL],
        NULL, 0u);
    if (!list_nil)
        return false;
    variables = list_nil;
    images = list_nil;
    arguments[0] = environment_material.suffixes[
        declaration->binding_len];
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->assertion_rules[
                PPPROOF_GSLT_ASSERTION_RULE_V1_BUILD_ENVIRONMENT_NIL],
            NULL, 0u, NULL, 0u, &environment_evidence))
        return false;
    cursor = declaration->binding_len;
    while (cursor > 0u) {
        const PPProofGSLTAssertionBindingV1 *binding;
        const PPProofGSLTPatternV1 *variable_item;
        const PPProofGSLTPatternV1 *next_variables;
        const PPProofGSLTPatternV1 *next_images;
        const PPProofGSLTPatternV1 *item_arguments[2];
        PPProofGSLTReferenceV1 node_children[3];

        cursor--;
        binding = &declaration->bindings[cursor];
        item_arguments[0] = binding->variable->term;
        item_arguments[1] = binding->typecode;
        variable_item = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_VARIABLE],
            item_arguments, 2u);
        if (!variable_item)
            return false;
        item_arguments[0] = variable_item;
        item_arguments[1] = variables;
        next_variables = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS],
            item_arguments, 2u);
        item_arguments[0] =
            environment_material.images[cursor].sequence.term;
        item_arguments[1] = images;
        next_images = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS],
            item_arguments, 2u);
        if (!next_variables || !next_images)
            return false;
        arguments[0] = binding->variable->term;
        arguments[1] = binding->typecode;
        arguments[2] = environment_material.images[cursor].sequence.term;
        arguments[3] = variables;
        arguments[4] = images;
        arguments[5] = environment_material.suffixes[cursor + 1u];
        node_children[0] = binding->variable->variable_evidence;
        node_children[1] = binding->floating_proof;
        node_children[2] = environment_evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->assertion_rules[
                    PPPROOF_GSLT_ASSERTION_RULE_V1_BUILD_ENVIRONMENT_CONS],
                arguments, 6u, node_children, 3u,
                &environment_evidence))
            return false;
        variables = next_variables;
        images = next_images;
    }

    essentials = list_nil;
    actuals = list_nil;
    arguments[0] = environment_material.suffixes[0];
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->assertion_rules[
                PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_ESSENTIALS_NIL],
            arguments, 1u, NULL, 0u, &essential_evidence))
        return false;
    cursor = declaration->essential_len;
    while (cursor > 0u) {
        const PPProofGSLTAssertionEssentialV1 *essential;
        PPProofGSLTSequenceMaterialV1 template_material;
        PPProofGSLTMaterializedSequenceV1 actual;
        PPProofGSLTSequenceProofV1 instantiation;
        const PPProofGSLTPatternV1 *essential_item;
        const PPProofGSLTPatternV1 *next_essentials;
        const PPProofGSLTPatternV1 *next_actuals;
        const PPProofGSLTPatternV1 *item_arguments[2];
        PPProofGSLTReferenceV1 node_children[3];

        cursor--;
        essential = &declaration->essentials[cursor];
        if (!ppproof_sequence_v1_materialize_sequence(
                build, essential->template_sequence, &template_material) ||
            !ppproof_sequence_v1_instantiate_expected_build(
                build, essential->template_sequence, environment,
                essential->actual_sequence, sources,
                &actual, &instantiation))
            return false;
        item_arguments[0] = template_material.sequence.term;
        essential_item = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_ESSENTIAL],
            item_arguments, 1u);
        if (!essential_item)
            return false;
        item_arguments[0] = essential_item;
        item_arguments[1] = essentials;
        next_essentials = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS],
            item_arguments, 2u);
        item_arguments[0] = actual.term;
        item_arguments[1] = actuals;
        next_actuals = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS],
            item_arguments, 2u);
        if (!next_essentials || !next_actuals)
            return false;
        arguments[0] = template_material.sequence.term;
        arguments[1] = actual.term;
        arguments[2] = essentials;
        arguments[3] = actuals;
        arguments[4] = environment_material.suffixes[0];
        node_children[0] = essential->actual_proof;
        node_children[1] = instantiation.evidence;
        node_children[2] = essential_evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->assertion_rules[
                    PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_ESSENTIALS_CONS],
                arguments, 5u, node_children, 3u,
                &essential_evidence))
            return false;
        essentials = next_essentials;
        actuals = next_actuals;
    }

    disjoints = list_nil;
    arguments[0] = environment_material.suffixes[0];
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->assertion_rules[
                PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_DISJOINTS_NIL],
            arguments, 1u, NULL, 0u, &disjoint_evidence))
        return false;
    cursor = declaration->disjoint_len;
    while (cursor > 0u) {
        const PPProofGSLTAssertionDisjointV1 *disjoint;
        PPProofGSLTSequenceProofV1 left_lookup;
        PPProofGSLTSequenceProofV1 right_lookup;
        PPProofGSLTSequenceProofV1 apart;
        PPProofGSLTSequenceMaterialV1 left_image;
        PPProofGSLTSequenceMaterialV1 right_image;
        PPProofGSLTReferenceV1 different;
        const PPProofGSLTPatternV1 *disjoint_item;
        const PPProofGSLTPatternV1 *next_disjoints;
        const PPProofGSLTPatternV1 *item_arguments[2];
        PPProofGSLTReferenceV1 node_children[5];

        cursor--;
        disjoint = &declaration->disjoints[cursor];
        if (!ppproof_sequence_v1_token_valid(disjoint->left) ||
            !ppproof_sequence_v1_token_valid(disjoint->right) ||
            !disjoint->left->variable || !disjoint->right->variable ||
            !sources->different ||
            !sources->different(
                sources->context, disjoint->left, disjoint->right,
                &different) ||
            !ppproof_sequence_v1_lookup(
                build, environment, &environment_material,
                disjoint->left, sources, &left_lookup, &left_image) ||
            !ppproof_sequence_v1_lookup(
                build, environment, &environment_material,
                disjoint->right, sources, &right_lookup, &right_image) ||
            !ppproof_sequence_v1_support_apart_build(
                build,
                (PPProofGSLTTokenSequenceV1){
                    .tokens = left_image.sequence.tokens,
                    .token_len = left_image.sequence.token_len,
                },
                (PPProofGSLTTokenSequenceV1){
                    .tokens = right_image.sequence.tokens,
                    .token_len = right_image.sequence.token_len,
                },
                sources, &apart)) {
            if (build->result == PPPROOF_GSLT_ARTICLE_V1_OK)
                ppproof_sequence_v1_build_fail(
                    build, PPPROOF_GSLT_ARTICLE_V1_REJECTED,
                    "assertion evidence lacks a disjointness witness");
            return false;
        }
        item_arguments[0] = disjoint->left->term;
        item_arguments[1] = disjoint->right->term;
        disjoint_item = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_DISJOINT],
            item_arguments, 2u);
        if (!disjoint_item)
            return false;
        item_arguments[0] = disjoint_item;
        item_arguments[1] = disjoints;
        next_disjoints = ppproof_sequence_v1_apply(
            build,
            impl->abi->assertion_constructors[
                PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS],
            item_arguments, 2u);
        if (!next_disjoints)
            return false;
        arguments[0] = disjoint->left->term;
        arguments[1] = disjoint->right->term;
        arguments[2] = disjoints;
        arguments[3] = environment_material.suffixes[0];
        arguments[4] = left_image.sequence.term;
        arguments[5] = right_image.sequence.term;
        node_children[0] = different;
        node_children[1] = left_lookup.evidence;
        node_children[2] = right_lookup.evidence;
        node_children[3] = apart.evidence;
        node_children[4] = disjoint_evidence;
        if (!ppproof_sequence_v1_add_node(
                build,
                impl->abi->assertion_rules[
                    PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_DISJOINTS_CONS],
                arguments, 6u, node_children, 5u,
                &disjoint_evidence))
            return false;
        disjoints = next_disjoints;
    }

    if (!ppproof_sequence_v1_materialize_sequence(
            build, declaration->conclusion_template,
            &conclusion_template) ||
        !ppproof_sequence_v1_instantiate_build(
            build, declaration->conclusion_template, environment, sources,
            &application_out->result, &conclusion_instantiation))
        return false;
    arguments[0] = variables;
    arguments[1] = essentials;
    arguments[2] = declaration->conclusion_type;
    arguments[3] = conclusion_template.sequence.term;
    arguments[4] = disjoints;
    assertion = ppproof_sequence_v1_apply(
        build,
        impl->abi->assertion_constructors[
            PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_ASSERTION],
        arguments, 5u);
    if (!assertion)
        return false;
    arguments[0] = assertion;
    application_out->declared_goal = ppproof_sequence_v1_apply(
        build,
        impl->abi->assertion_judgments[
            PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_DECLARED],
        arguments, 1u);
    if (!application_out->declared_goal)
        return false;

    arguments[0] = variables;
    arguments[1] = essentials;
    arguments[2] = declaration->conclusion_type;
    arguments[3] = conclusion_template.sequence.term;
    arguments[4] = disjoints;
    arguments[5] = images;
    arguments[6] = actuals;
    arguments[7] = environment_material.suffixes[0];
    arguments[8] = application_out->result.term;
    children[0] = (PPProofGSLTReferenceV1){
        .kind = PPPROOF_GSLT_REFERENCE_V1_PREMISE,
        .index = declaration_premise_index,
    };
    children[1] = environment_evidence;
    children[2] = essential_evidence;
    children[3] = disjoint_evidence;
    children[4] = conclusion_instantiation.evidence;
    if (!ppproof_sequence_v1_add_node(
            build,
            impl->abi->assertion_rules[
                PPPROOF_GSLT_ASSERTION_RULE_V1_APPLY],
            arguments, 9u, children, 5u, &application_evidence))
        return false;
    arguments[0] = declaration->conclusion_type;
    arguments[1] = application_out->result.term;
    typed_result = ppproof_sequence_v1_apply(
        build,
        impl->abi->constructors[
            PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS],
        arguments, 2u);
    if (!typed_result)
        return false;
    arguments[0] = typed_result;
    return ppproof_sequence_v1_goal(
        build,
        impl->abi->assertion_judgments[
            PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE],
        arguments, 1u, application_evidence, &application_out->proof);
}

void ppproof_gslt_sequence_evidence_producer_v1_init(
    PPProofGSLTSequenceEvidenceProducerV1 *producer) {
    if (producer)
        memset(producer, 0, sizeof(*producer));
}

void ppproof_gslt_sequence_evidence_producer_v1_free(
    PPProofGSLTSequenceEvidenceProducerV1 *producer) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;

    if (!producer)
        return;
    impl = producer->implementation;
    if (impl) {
        ppproof_sequence_v1_arena_chunks_free(impl->arena_first);
        free(impl->nodes);
        free(impl->sequence_cons_cache);
        free(impl);
    }
    memset(producer, 0, sizeof(*producer));
}

bool ppproof_gslt_sequence_evidence_producer_v1_workspace_stats(
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTSequenceEvidenceWorkspaceStatsV1 *stats_out) {
    const PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    const PPProofGSLTSequenceArenaChunkV1 *chunk;
    size_t arena_reserved = 0u;
    size_t arena_used = 0u;

    if (!producer || !stats_out || !(impl = producer->implementation))
        return false;
    for (chunk = impl->arena_first; chunk; chunk = chunk->next) {
        if (arena_reserved > SIZE_MAX - chunk->capacity)
            arena_reserved = SIZE_MAX;
        else
            arena_reserved += chunk->capacity;
        if (arena_used > SIZE_MAX - chunk->used)
            arena_used = SIZE_MAX;
        else
            arena_used += chunk->used;
    }
    *stats_out = (PPProofGSLTSequenceEvidenceWorkspaceStatsV1){
        .arena_reserved_bytes = arena_reserved,
        .arena_used_bytes = arena_used,
        .node_capacity = impl->node_cap,
        .canonical_cache_capacity = impl->sequence_cons_cache_cap,
    };
    return true;
}

static void ppproof_sequence_v1_reset_workspace(
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl,
    const PPProofGSLTSequenceEvidenceABIV1 *abi,
    uint32_t first_node_id,
    const PPProofGSLTArticleV1Limits *limits) {
    PPProofGSLTSequenceArenaChunkV1 *chunk;

    for (chunk = impl->arena_first; chunk; chunk = chunk->next)
        chunk->used = 0u;
    impl->abi = abi;
    impl->node_len = 0u;
    impl->first_node_id = first_node_id;
    impl->arena_current = impl->arena_first;
    impl->pattern_len = 0u;
    impl->sequence_nil_term = NULL;
    if (impl->sequence_cons_cache_cap != 0u) {
        memset(impl->sequence_cons_cache, 0,
               (size_t)impl->sequence_cons_cache_cap *
                   sizeof(*impl->sequence_cons_cache));
    }
    impl->sequence_cons_cache_len = 0u;
    impl->limits = *limits;
}

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_begin(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofGSLTSequenceEvidenceABIV1 *abi,
    uint32_t first_node_id,
    const PPProofGSLTArticleV1Limits *limits_argument,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTArticleV1Limits default_limits;
    const PPProofGSLTArticleV1Limits *limits = limits_argument;
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!producer || !abi || !abi->storage) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid sequence evidence producer request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    if (!limits) {
        default_limits = ppproof_gslt_article_v1_default_limits();
        limits = &default_limits;
    }
    if (limits->maximum_article_nodes == 0u ||
        limits->maximum_materialized_pattern_nodes == 0u ||
        limits->maximum_pattern_depth == 0u) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "sequence evidence producer limits are malformed");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    impl = producer->implementation;
    if (!impl) {
        impl = calloc(1u, sizeof(*impl));
        if (!impl) {
            ppproof_sequence_v1_set_error(
                error_buf, error_buf_size,
                "sequence evidence producer allocation failed");
            return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        }
        producer->implementation = impl;
    }
    ppproof_sequence_v1_reset_workspace(
        impl, abi, first_node_id, limits);
    producer->nodes = NULL;
    producer->node_len = 0u;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

static void ppproof_sequence_v1_publish_nodes(
    PPProofGSLTSequenceEvidenceProducerV1 *producer) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl =
        producer->implementation;
    producer->nodes = impl->nodes;
    producer->node_len = impl->node_len;
}

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_instantiate(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 source,
    PPProofGSLTSequenceEnvironmentV1 environment,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTMaterializedSequenceV1 *result_out,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    PPProofGSLTSequenceCheckpointV1 checkpoint;
    PPProofGSLTSequenceBuildV1 build;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!producer || !(impl = producer->implementation) ||
        !result_out || !proof_out) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid sequence instantiation evidence request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    memset(result_out, 0, sizeof(*result_out));
    memset(proof_out, 0, sizeof(*proof_out));
    checkpoint = ppproof_sequence_v1_checkpoint(impl);
    build = (PPProofGSLTSequenceBuildV1){
        .impl = impl,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .result = PPPROOF_GSLT_ARTICLE_V1_OK,
    };
    if (!ppproof_sequence_v1_instantiate_build(
            &build, source, environment, sources,
            result_out, proof_out)) {
        ppproof_sequence_v1_rollback(impl, checkpoint);
        ppproof_sequence_v1_publish_nodes(producer);
        return build.result == PPPROOF_GSLT_ARTICLE_V1_OK
                   ? PPPROOF_GSLT_ARTICLE_V1_INVALID
                   : build.result;
    }
    ppproof_sequence_v1_publish_nodes(producer);
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_instantiate_expected(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 source,
    PPProofGSLTSequenceEnvironmentV1 environment,
    PPProofGSLTTokenSequenceV1 expected,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTMaterializedSequenceV1 *result_out,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    PPProofGSLTSequenceCheckpointV1 checkpoint;
    PPProofGSLTSequenceBuildV1 build;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!producer || !(impl = producer->implementation) ||
        !result_out || !proof_out) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid expected instantiation evidence request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    memset(result_out, 0, sizeof(*result_out));
    memset(proof_out, 0, sizeof(*proof_out));
    checkpoint = ppproof_sequence_v1_checkpoint(impl);
    build = (PPProofGSLTSequenceBuildV1){
        .impl = impl,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .result = PPPROOF_GSLT_ARTICLE_V1_OK,
    };
    if (!ppproof_sequence_v1_instantiate_expected_build(
            &build, source, environment, expected, sources,
            result_out, proof_out)) {
        ppproof_sequence_v1_rollback(impl, checkpoint);
        ppproof_sequence_v1_publish_nodes(producer);
        return build.result == PPPROOF_GSLT_ARTICLE_V1_OK
                   ? PPPROOF_GSLT_ARTICLE_V1_INVALID
                   : build.result;
    }
    ppproof_sequence_v1_publish_nodes(producer);
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_support_apart(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    PPProofGSLTSequenceCheckpointV1 checkpoint;
    PPProofGSLTSequenceBuildV1 build;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!producer || !(impl = producer->implementation) || !proof_out) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid sequence apartness evidence request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    memset(proof_out, 0, sizeof(*proof_out));
    checkpoint = ppproof_sequence_v1_checkpoint(impl);
    build = (PPProofGSLTSequenceBuildV1){
        .impl = impl,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .result = PPPROOF_GSLT_ARTICLE_V1_OK,
    };
    if (!ppproof_sequence_v1_support_apart_build(
            &build, left, right, sources, proof_out)) {
        ppproof_sequence_v1_rollback(impl, checkpoint);
        ppproof_sequence_v1_publish_nodes(producer);
        return build.result == PPPROOF_GSLT_ARTICLE_V1_OK
                   ? PPPROOF_GSLT_ARTICLE_V1_INVALID
                   : build.result;
    }
    ppproof_sequence_v1_publish_nodes(producer);
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_use_premise(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 formula,
    PPProofGSLTReferenceV1 premise,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    PPProofGSLTSequenceCheckpointV1 checkpoint;
    PPProofGSLTSequenceBuildV1 build;
    PPProofGSLTSequenceMaterialV1 material;
    const PPProofGSLTPatternV1 *arguments[1];
    PPProofGSLTReferenceV1 evidence;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!producer || !(impl = producer->implementation) || !proof_out ||
        premise.kind != PPPROOF_GSLT_REFERENCE_V1_PREMISE) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid sequence premise evidence request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    memset(proof_out, 0, sizeof(*proof_out));
    memset(&material, 0, sizeof(material));
    checkpoint = ppproof_sequence_v1_checkpoint(impl);
    build = (PPProofGSLTSequenceBuildV1){
        .impl = impl,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .result = PPPROOF_GSLT_ARTICLE_V1_OK,
    };
    if (!ppproof_sequence_v1_materialize_sequence(
            &build, formula, &material))
        goto failed;
    arguments[0] = material.sequence.term;
    if (!ppproof_sequence_v1_add_node(
            &build,
            impl->abi->assertion_rules[
                PPPROOF_GSLT_ASSERTION_RULE_V1_USE_PREMISE],
            arguments, 1u, &premise, 1u, &evidence) ||
        !ppproof_sequence_v1_goal(
            &build,
            impl->abi->assertion_judgments[
                PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE],
            arguments, 1u, evidence, proof_out))
        goto failed;
    ppproof_sequence_v1_publish_nodes(producer);
    return PPPROOF_GSLT_ARTICLE_V1_OK;

failed:
    ppproof_sequence_v1_rollback(impl, checkpoint);
    ppproof_sequence_v1_publish_nodes(producer);
    return build.result == PPPROOF_GSLT_ARTICLE_V1_OK
               ? PPPROOF_GSLT_ARTICLE_V1_INVALID
               : build.result;
}

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofGSLTAssertionDeclarationV1 *declaration,
    uint32_t declaration_premise_index,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTAssertionApplicationV1 *application_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTSequenceEvidenceProducerImplV1 *impl;
    PPProofGSLTSequenceCheckpointV1 checkpoint;
    PPProofGSLTSequenceBuildV1 build;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!producer || !(impl = producer->implementation) ||
        !application_out) {
        ppproof_sequence_v1_set_error(
            error_buf, error_buf_size,
            "invalid assertion application evidence request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    memset(application_out, 0, sizeof(*application_out));
    checkpoint = ppproof_sequence_v1_checkpoint(impl);
    build = (PPProofGSLTSequenceBuildV1){
        .impl = impl,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .result = PPPROOF_GSLT_ARTICLE_V1_OK,
    };
    if (!ppproof_sequence_v1_apply_assertion_build(
            &build, declaration, declaration_premise_index, sources,
            application_out)) {
        ppproof_sequence_v1_rollback(impl, checkpoint);
        memset(application_out, 0, sizeof(*application_out));
        ppproof_sequence_v1_publish_nodes(producer);
        return build.result == PPPROOF_GSLT_ARTICLE_V1_OK
                   ? PPPROOF_GSLT_ARTICLE_V1_INVALID
                   : build.result;
    }
    ppproof_sequence_v1_publish_nodes(producer);
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}
