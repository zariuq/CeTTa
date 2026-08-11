#include "proof_gslt_relational_projection_v1.h"

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
    PPProofGSLTRelationalProjectionTableV1 *tables;
    PPProofGSLTRelationalProjectionSelectorV1 *selectors;
    uint8_t **selector_bytes;
} PPProofGSLTRelationalProjectionStorageV1;

static void ppproof_projection_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppproof_projection_v1_expr_head(
    const Atom *atom, const char *head, CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == argument_len + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppproof_projection_v1_name(
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

static bool ppproof_projection_v1_u32(
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

static bool ppproof_projection_v1_digest_valid(const char *digest) {
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

static int32_t ppproof_projection_v1_state_table(
    const PPRelationalStateProgramV1Plan *state_plan,
    PPProofGSLTNameV1 name) {
    int32_t found = -1;
    uint32_t index;

    for (index = 0u; index < state_plan->table_len; index++) {
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

void ppproof_gslt_relational_projection_v1_init(
    PPProofGSLTRelationalProjectionV1 *projection) {
    if (projection)
        memset(projection, 0, sizeof(*projection));
}

void ppproof_gslt_relational_projection_v1_free(
    PPProofGSLTRelationalProjectionV1 *projection) {
    PPProofGSLTRelationalProjectionStorageV1 *storage;
    uint32_t index;

    if (!projection)
        return;
    storage = projection->storage;
    if (storage) {
        for (index = 0u; index < projection->selector_len; index++)
            free(storage->selector_bytes[index]);
        free(storage->selector_bytes);
        free(storage->selectors);
        free(storage->tables);
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    ppproof_gslt_relational_projection_v1_init(projection);
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_projection_v1_read(
    PPProofGSLTRelationalProjectionV1 *projection,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTRelationalProjectionV1 result;
    PPProofGSLTRelationalProjectionStorageV1 *storage = NULL;
    PPProofGSLTNameV1 artifact_owner = {0};
    size_t table_count = 0u;
    size_t selector_count = 0u;
    size_t answer_index;
    uint32_t table_index = 0u;
    uint32_t selector_index = 0u;
    bool identity_seen = false;
    PPProofGSLTArticleV1Result failure =
        PPPROOF_GSLT_ARTICLE_V1_INVALID;

    ppproof_gslt_relational_projection_v1_init(&result);
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!projection || !answer_path) {
        ppproof_projection_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational projection read request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage) {
        ppproof_projection_v1_set_error(
            error_buf, error_buf_size,
            "relational projection storage allocation failed");
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    }
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path, error_buf, error_buf_size))
        goto failed;

    for (answer_index = 0u; answer_index < storage->answers.len;
         answer_index++) {
        const Atom *answer = storage->answers.terms[answer_index];
        const Atom *record;

        if (!ppproof_projection_v1_expr_head(
                answer, "proof-relational-projection-artifact-v1", 1u))
            goto malformed;
        record = answer->expr.elems[1];
        if (ppproof_projection_v1_expr_head(
                record, "proof-relational-projection-identity-v1", 2u)) {
            if (identity_seen)
                goto malformed;
            identity_seen = true;
        } else if (ppproof_projection_v1_expr_head(
                       record, "proof-relational-projection-table-v1", 5u)) {
            table_count++;
        } else if (ppproof_projection_v1_expr_head(
                       record,
                       "proof-relational-projection-selector-v1", 3u)) {
            selector_count++;
        } else {
            goto malformed;
        }
    }
    if (!identity_seen || table_count > UINT32_MAX ||
        selector_count > UINT32_MAX)
        goto malformed;
    if (table_count != 0u) {
        storage->tables = calloc(table_count, sizeof(*storage->tables));
        if (!storage->tables)
            goto resource;
    }
    if (selector_count != 0u) {
        storage->selectors =
            calloc(selector_count, sizeof(*storage->selectors));
        storage->selector_bytes =
            calloc(selector_count, sizeof(*storage->selector_bytes));
        if (!storage->selectors || !storage->selector_bytes)
            goto resource;
    }

    identity_seen = false;
    for (answer_index = 0u; answer_index < storage->answers.len;
         answer_index++) {
        const Atom *answer = storage->answers.terms[answer_index];
        const Atom *record = answer->expr.elems[1];
        PPProofGSLTNameV1 owner;

        if (ppproof_projection_v1_expr_head(
                record, "proof-relational-projection-identity-v1", 2u)) {
            PPProofGSLTNameV1 base;

            if (identity_seen ||
                !ppproof_projection_v1_name(
                    record->expr.elems[1], &owner) ||
                !ppproof_projection_v1_name(
                    record->expr.elems[2], &base))
                goto malformed;
            result.owner = owner;
            result.base = base;
            identity_seen = true;
        } else if (ppproof_projection_v1_expr_head(
                       record, "proof-relational-projection-table-v1", 5u)) {
            PPProofGSLTRelationalProjectionTableV1 *table =
                &storage->tables[table_index++];

            if (!ppproof_projection_v1_name(
                    record->expr.elems[1], &owner) ||
                !ppproof_projection_v1_name(
                    record->expr.elems[2], &table->role) ||
                !ppproof_projection_v1_name(
                    record->expr.elems[3], &table->table) ||
                !ppproof_projection_v1_u32(
                    record->expr.elems[4], &table->arity) ||
                !ppproof_projection_v1_u32(
                    record->expr.elems[5], &table->key_arity) ||
                table->key_arity > table->arity)
                goto malformed;
            table->table_id = UINT32_MAX;
        } else {
            PPProofGSLTRelationalProjectionSelectorV1 *selector =
                &storage->selectors[selector_index];
            uint8_t *rendered = NULL;
            size_t rendered_len = 0u;

            if (!ppproof_projection_v1_name(
                    record->expr.elems[1], &owner) ||
                !ppproof_projection_v1_name(
                    record->expr.elems[2], &selector->role) ||
                !fh_ground_term_v1_render(
                    record->expr.elems[3], &rendered, &rendered_len,
                    error_buf, error_buf_size) ||
                rendered_len == 0u || rendered_len > UINT32_MAX) {
                free(rendered);
                goto malformed;
            }
            storage->selector_bytes[selector_index] = rendered;
            selector->value =
                (PPProofGSLTRelationalProjectionValueV1){
                    .bytes = rendered,
                    .len = (uint32_t)rendered_len,
                };
            selector_index++;
        }
        if (!artifact_owner.bytes)
            artifact_owner = owner;
        else if (!ppproof_gslt_article_v1_name_equal(
                     artifact_owner, owner))
            goto mixed_identity;
    }
    if (!identity_seen ||
        !ppproof_gslt_article_v1_name_equal(
            artifact_owner, result.owner))
        goto mixed_identity;

    result.tables = storage->tables;
    result.selectors = storage->selectors;
    result.table_len = (uint32_t)table_count;
    result.selector_len = (uint32_t)selector_count;
    memcpy(result.artifact_digest, storage->answers.digest, 65u);
    result.storage = storage;
    ppproof_gslt_relational_projection_v1_free(projection);
    *projection = result;
    return PPPROOF_GSLT_ARTICLE_V1_OK;

resource:
    failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    ppproof_projection_v1_set_error(
        error_buf, error_buf_size,
        "relational projection vector allocation failed");
    goto failed;
malformed:
    ppproof_projection_v1_set_error(
        error_buf, error_buf_size,
        "relational projection artifact contains a malformed record set");
    goto failed;
mixed_identity:
    ppproof_projection_v1_set_error(
        error_buf, error_buf_size,
        "relational projection artifact mixes extension identities");

failed:
    if (storage) {
        for (table_index = 0u; table_index < selector_index; table_index++)
            free(storage->selector_bytes
                     ? storage->selector_bytes[table_index] : NULL);
        free(storage->selector_bytes);
        free(storage->selectors);
        free(storage->tables);
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    return failure;
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_projection_v1_bind_state(
    PPProofGSLTRelationalProjectionV1 *projection,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *table_ids = NULL;
    uint32_t index;
    PPProofGSLTArticleV1Result failure =
        PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!projection || !projection->storage ||
        !ppproof_projection_v1_digest_valid(
            projection->artifact_digest) ||
        !state_plan ||
        (state_plan->table_len != 0u && !state_plan->tables) ||
        !ppproof_projection_v1_digest_valid(state_plan->plan_digest)) {
        ppproof_projection_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational projection state binding request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    if (projection->table_len != 0u) {
        table_ids = malloc(
            (size_t)projection->table_len * sizeof(*table_ids));
        if (!table_ids) {
            ppproof_projection_v1_set_error(
                error_buf, error_buf_size,
                "relational projection binding allocation failed");
            return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        }
    }
    for (index = 0u; index < projection->table_len; index++) {
        const PPProofGSLTRelationalProjectionTableV1 *table =
            &projection->tables[index];
        int32_t state_table = ppproof_projection_v1_state_table(
            state_plan, table->table);

        if (state_table < 0) {
            ppproof_projection_v1_set_error(
                error_buf, error_buf_size,
                "relational projection table is missing or ambiguous");
            goto failed;
        }
        if (state_plan->tables[(uint32_t)state_table].arity !=
                table->arity ||
            state_plan->tables[(uint32_t)state_table].key_arity !=
                table->key_arity) {
            ppproof_projection_v1_set_error(
                error_buf, error_buf_size,
                "relational projection table has the wrong state shape");
            goto failed;
        }
        table_ids[index] = (uint32_t)state_table;
    }
    for (index = 0u; index < projection->table_len; index++)
        projection->tables[index].table_id = table_ids[index];
    memcpy(projection->state_plan_digest, state_plan->plan_digest, 65u);
    free(table_ids);
    return PPPROOF_GSLT_ARTICLE_V1_OK;

failed:
    free(table_ids);
    return failure;
}
