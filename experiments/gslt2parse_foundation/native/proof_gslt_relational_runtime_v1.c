#include "proof_gslt_relational_runtime_v1.h"

#include "gslt_finite_fact_provider_v1.h"

#include "finite_horn_answer_stream_v1.h"
#include "relational_value_list_v1.h"

#include "atom.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PPPROOF_RUNTIME_KEY_V1_REQUEST_LABEL = 0,
    PPPROOF_RUNTIME_KEY_V1_PROOF_LABELS = 1,
} PPProofRuntimeKeyV1;

typedef enum {
    PPPROOF_RUNTIME_INPUT_V1_NORMAL = 0,
    PPPROOF_RUNTIME_INPUT_V1_COMPRESSED = 1,
} PPProofRuntimeInputV1;

typedef struct {
    const char *relation;
    const char *table;
    uint32_t arity;
    uint32_t key_arity;
    uint32_t table_id;
} PPProofRuntimeTableV1;

typedef struct {
    const char *table;
    uint32_t column;
    const char *zero;
    const char *successor;
    uint32_t table_id;
} PPProofRuntimeOrdinalV1;

typedef struct {
    const char *table;
    uint32_t column;
    const char *value;
    uint32_t table_id;
} PPProofRuntimeControlLiteralV1;

typedef struct {
    const char *relation;
    const char *wrapper;
    const char *table;
    const char *zero;
    const char *successor;
    PPProofRuntimeKeyV1 key_source;
    uint32_t table_id;
} PPProofRuntimeCountV1;

typedef struct {
    const char *relation;
    const char *table;
    uint32_t value_column;
    uint32_t filter_column;
    const char *filter_value;
    uint32_t table_id;
} PPProofRuntimeDistinctPairsV1;

typedef struct {
    PPProofRuntimeInputV1 input;
    const char *relation;
    uint8_t byte;
} PPProofRuntimeInputByteV1;

typedef struct {
    PPProofRuntimeInputV1 input;
    uint32_t priority;
    PPRelationalStateProofV1Result result;
    const char *query;
} PPProofRuntimeOutcomeQueryV1;

typedef struct PPProofRuntimePersistentCacheV1
    PPProofRuntimePersistentCacheV1;

typedef struct {
    FHAnswerStreamV1 answers;
    const char *owner;
    const char *base;
    PPProofRuntimeTableV1 *tables;
    uint32_t table_len;
    PPProofRuntimeOrdinalV1 *ordinals;
    uint32_t ordinal_len;
    PPProofRuntimeControlLiteralV1 *control_literals;
    uint32_t control_literal_len;
    PPProofRuntimeCountV1 *counts;
    uint32_t count_len;
    PPProofRuntimeDistinctPairsV1 *distinct_pairs;
    uint32_t distinct_pair_len;
    PPProofRuntimeInputByteV1 *input_bytes;
    uint32_t input_byte_len;
    PPProofRuntimeOutcomeQueryV1 *outcome_queries;
    uint32_t outcome_query_len;
    const char *value_relation;
    const char *value_cons;
    const char *value_nil;
    const char *request_relation;
    const char *request_wrapper;
    const char *query_request_wrapper;
    const char *query_label_wrapper;
    const char *query_list_cons;
    const char *query_list_nil;
    const char *compressed_request_wrapper;
    const char *compressed_label_wrapper;
    const char *compressed_label_cons;
    const char *compressed_label_nil;
    const char *compressed_code_cons;
    const char *compressed_code_nil;
    const char *compressed_initial_table;
    uint32_t compressed_initial_key_column;
    uint32_t compressed_initial_ordinal_column;
    uint32_t compressed_initial_value_column;
    uint32_t compressed_initial_table_id;
    bool normal_input_ready;
    bool compressed_query_ready;
    const PPRelationalStateProgramV1Plan *state_plan;
    const PPOSLFNativeTypePlanV1 *native_plan;
    const PPOSLFNativeTypeVMV1 *vm;
    PPOSLFNativeVMLimitsV1 limits;
    PPProofGSLTRelationalRuntimeV1Receipt receipt;
    bool receipt_ready;
    PPOSLFNativeCapabilitySetV1 pending_capabilities;
    bool pending_capabilities_ready;
    PPProofRuntimePersistentCacheV1 *persistent_cache;
    CettaGsltLanguage *compiled_audit_language;
    const CettaGsltEmbeddedLanguageV1 *compiled_audit_descriptor;
    const CettaGsltProviderCatalogV1 *compiled_audit_catalog;
    CettaGsltHornLimits compiled_audit_limits;
} PPProofRuntimeImplV1;

typedef struct {
    const char *zero;
    const char *successor;
    Atom **values;
    uint32_t value_len;
    uint32_t value_cap;
} PPProofRuntimeConstructorChainV1;

typedef struct {
    Atom **rows;
    uint32_t row_len;
    uint32_t row_cap;
    uint32_t *list_values;
    uint32_t list_value_len;
    uint32_t list_value_cap;
    PPProofRuntimeConstructorChainV1 *constructor_chains;
    uint32_t constructor_chain_len;
    uint32_t constructor_chain_cap;
    uint64_t constructor_chain_requests;
    uint64_t constructor_chain_nodes;
    Arena arena;
} PPProofRuntimeRowsV1;

struct PPProofRuntimePersistentCacheV1 {
    uint64_t store_identity;
    uint32_t *table_row_lens;
    uint32_t table_len;
    PPProofRuntimeRowsV1 rows;
    PPOSLFNativeCapabilitySetV1 capabilities;
};

static void ppproof_runtime_v1_rows_init(
    PPProofRuntimeRowsV1 *rows, CettaArenaRuntimeKind kind) {
    if (!rows)
        return;
    memset(rows, 0, sizeof(*rows));
    arena_init(&rows->arena);
    arena_set_runtime_kind(&rows->arena, kind);
}

static void ppproof_runtime_v1_rows_free(PPProofRuntimeRowsV1 *rows) {
    uint32_t index;

    if (!rows)
        return;
    arena_free(&rows->arena);
    for (index = 0u; index < rows->constructor_chain_len; index++)
        free(rows->constructor_chains[index].values);
    free(rows->constructor_chains);
    free(rows->list_values);
    free(rows->rows);
    memset(rows, 0, sizeof(*rows));
}

static void ppproof_runtime_v1_persistent_cache_free(
    PPProofRuntimePersistentCacheV1 *cache) {
    if (!cache)
        return;
    pposlf_native_capability_set_v1_free(&cache->capabilities);
    ppproof_runtime_v1_rows_free(&cache->rows);
    free(cache->table_row_lens);
    free(cache);
}

static bool ppproof_runtime_v1_fail(
    char *buffer, size_t size, const char *format, ...) {
    va_list arguments;

    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static uint64_t ppproof_runtime_v1_add_u64_sat(
    uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static bool ppproof_runtime_v1_expr_head(
    const Atom *atom, const char *head, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static const char *ppproof_runtime_v1_symbol(const Atom *atom) {
    const char *name;

    if (!atom || atom->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr((Atom *)atom);
    return name && name[0] != '\0' ? name : NULL;
}

static bool ppproof_runtime_v1_u32(
    const Atom *atom, uint32_t *value_out) {
    int64_t value;

    if (!atom || !value_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT)
        return false;
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return false;
    *value_out = (uint32_t)value;
    return true;
}

static bool ppproof_runtime_v1_same(
    const char *left, const char *right) {
    return left && right && strcmp(left, right) == 0;
}

static int32_t ppproof_runtime_v1_state_table(
    const PPRelationalStateProgramV1Plan *plan, const char *name) {
    int32_t found = -1;
    uint32_t index;

    if (!plan || !name)
        return -1;
    for (index = 0u; index < plan->table_len; index++) {
        if (!plan->tables[index].name ||
            strcmp(plan->tables[index].name, name) != 0)
            continue;
        if (found >= 0)
            return -2;
        found = (int32_t)index;
    }
    return found;
}

static bool ppproof_runtime_v1_head(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *head, uint32_t arity) {
    uint32_t actual = 0u;
    uint32_t index;

    if (!plan || !head)
        return false;
    if (arity != 0u)
        return pposlf_native_type_plan_v1_head_arity(
                   plan, head, &actual) && actual == arity;
    for (index = 0u; index < plan->term_len; index++) {
        const PPOSLFNativeTermV1 *term = &plan->terms[index];

        if (term->kind == PPOSLF_NATIVE_TERM_SYMBOL_V1 && term->text &&
            strcmp(term->text, head) == 0)
            return true;
    }
    return false;
}

static bool ppproof_runtime_v1_external(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *head, uint32_t arity) {
    uint32_t relation = 0u;
    return pposlf_native_type_plan_v1_external_relation(
        plan, head, arity, &relation);
}

static void ppproof_runtime_v1_impl_free(PPProofRuntimeImplV1 *impl) {
    if (!impl)
        return;
    pposlf_native_capability_set_v1_free(&impl->pending_capabilities);
    ppproof_runtime_v1_persistent_cache_free(impl->persistent_cache);
    cetta_gslt_language_free(impl->compiled_audit_language);
    free(impl->outcome_queries);
    free(impl->input_bytes);
    free(impl->distinct_pairs);
    free(impl->counts);
    free(impl->control_literals);
    free(impl->ordinals);
    free(impl->tables);
    fh_answer_stream_v1_free(&impl->answers);
    free(impl);
}

static bool ppproof_runtime_v1_optional_text_equal(
    const char *left, const char *right) {
    return (!left && !right) ||
        (left && right && strcmp(left, right) == 0);
}

bool ppproof_gslt_relational_runtime_v1_attach_compiled_audit(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    const CettaGsltProviderCatalogV1 *catalog,
    CettaGsltHornLimits limits,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRuntimeImplV1 *impl = runtime ? runtime->implementation : NULL;
    CettaGsltLanguage *language = NULL;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !descriptor || !catalog || !descriptor->name ||
        !descriptor->manifest_sha256 || !descriptor->query_relation ||
        descriptor->query_arity != 1u ||
        limits.max_rule_attempts == 0u || limits.max_answers == 0u ||
        limits.max_depth == 0u)
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "invalid compiled proof audit attachment");
    if (!cetta_gslt_provider_catalog_validate_v1(
            catalog, error_buf, error_buf_size))
        return false;
    if (
        strcmp(catalog->language_name, descriptor->name) != 0 ||
        !ppproof_runtime_v1_optional_text_equal(
            catalog->profile_name, descriptor->profile_name) ||
        strcmp(catalog->language_manifest_sha256,
               descriptor->manifest_sha256) != 0)
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "invalid compiled proof audit attachment");
    if (!cetta_gslt_language_load_embedded_for_realization(
            descriptor, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &language, error_buf, error_buf_size))
        return false;
    cetta_gslt_language_free(impl->compiled_audit_language);
    impl->compiled_audit_language = language;
    impl->compiled_audit_descriptor = descriptor;
    impl->compiled_audit_catalog = catalog;
    impl->compiled_audit_limits = limits;
    return true;
}

void ppproof_gslt_relational_runtime_v1_init(
    PPProofGSLTRelationalRuntimeV1 *runtime) {
    if (runtime)
        memset(runtime, 0, sizeof(*runtime));
}

void ppproof_gslt_relational_runtime_v1_free(
    PPProofGSLTRelationalRuntimeV1 *runtime) {
    if (!runtime)
        return;
    ppproof_runtime_v1_impl_free(runtime->implementation);
    ppproof_gslt_relational_runtime_v1_init(runtime);
}

static bool ppproof_runtime_v1_parse_key(
    const Atom *atom, PPProofRuntimeKeyV1 *key_out) {
    if (atom_is_symbol((Atom *)atom, "proof-runtime-request-label-v1")) {
        *key_out = PPPROOF_RUNTIME_KEY_V1_REQUEST_LABEL;
        return true;
    }
    if (atom_is_symbol((Atom *)atom, "proof-runtime-proof-labels-v1")) {
        *key_out = PPPROOF_RUNTIME_KEY_V1_PROOF_LABELS;
        return true;
    }
    return false;
}

static bool ppproof_runtime_v1_parse_input(
    const Atom *atom, PPProofRuntimeInputV1 *input_out) {
    if (atom_is_symbol((Atom *)atom, "proof-runtime-normal-input-v1")) {
        *input_out = PPPROOF_RUNTIME_INPUT_V1_NORMAL;
        return true;
    }
    if (atom_is_symbol((Atom *)atom, "proof-runtime-compressed-input-v1")) {
        *input_out = PPPROOF_RUNTIME_INPUT_V1_COMPRESSED;
        return true;
    }
    return false;
}

static bool ppproof_runtime_v1_parse_result(
    const Atom *atom, PPRelationalStateProofV1Result *result_out) {
    if (atom_is_symbol((Atom *)atom, "proof-runtime-result-verified-v1")) {
        *result_out = PPRELATIONAL_STATE_PROOF_V1_VERIFIED;
        return true;
    }
    if (atom_is_symbol((Atom *)atom, "proof-runtime-result-incomplete-v1")) {
        *result_out = PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE;
        return true;
    }
    return false;
}

static bool ppproof_runtime_v1_owner(
    PPProofRuntimeImplV1 *impl, const Atom *atom) {
    const char *owner = ppproof_runtime_v1_symbol(atom);

    if (!owner)
        return false;
    if (!impl->owner)
        impl->owner = owner;
    return ppproof_runtime_v1_same(impl->owner, owner);
}

bool ppproof_gslt_relational_runtime_v1_prepare(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    const char *provider_answer_path,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPOSLFNativeTypePlanV1 *native_plan,
    const PPOSLFNativeTypeVMV1 *vm,
    PPOSLFNativeVMLimitsV1 limits,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRuntimeImplV1 *impl = NULL;
    uint32_t table_count = 0u;
    uint32_t ordinal_count = 0u;
    uint32_t control_literal_count = 0u;
    uint32_t count_count = 0u;
    uint32_t distinct_pair_count = 0u;
    uint32_t input_byte_count = 0u;
    uint32_t outcome_query_count = 0u;
    uint32_t table_index = 0u;
    uint32_t ordinal_index = 0u;
    uint32_t control_literal_index = 0u;
    uint32_t count_index = 0u;
    uint32_t distinct_pair_index = 0u;
    uint32_t input_byte_index = 0u;
    uint32_t outcome_query_index = 0u;
    size_t answer_index;
    bool identity_seen = false;
    bool value_seen = false;
    bool request_seen = false;
    bool normal_input_seen = false;
    bool compressed_input_seen = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!runtime || !provider_answer_path || !state_plan || !native_plan ||
        !vm || !native_plan->step_schemas ||
        (state_plan->table_len != 0u && !state_plan->tables))
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "invalid relational proof runtime preparation request");
    impl = calloc(1u, sizeof(*impl));
    if (!impl)
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "relational proof runtime allocation failed");
    fh_answer_stream_v1_init(&impl->answers);
    pposlf_native_capability_set_v1_init(&impl->pending_capabilities);
    if (!fh_answer_stream_v1_read(
            &impl->answers, provider_answer_path,
            error_buf, error_buf_size))
        goto failed;

    for (answer_index = 0u; answer_index < impl->answers.len;
         answer_index++) {
        const Atom *answer = impl->answers.terms[answer_index];
        const Atom *record;

        if (!ppproof_runtime_v1_expr_head(
                answer, "proof-relational-runtime-artifact-v1", 1u))
            goto malformed;
        record = answer->expr.elems[1];
        if (ppproof_runtime_v1_expr_head(
                record, "proof-relational-runtime-identity-v1", 2u)) {
            if (identity_seen)
                goto malformed;
            identity_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-state-table-v1", 5u)) {
            if (table_count == UINT32_MAX)
                goto malformed;
            table_count++;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-ordinal-column-v1", 5u)) {
            if (ordinal_count == UINT32_MAX)
                goto malformed;
            ordinal_count++;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-control-literal-v1", 4u)) {
            if (control_literal_count == UINT32_MAX)
                goto malformed;
            control_literal_count++;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-prefix-count-v1", 7u)) {
            if (count_count == UINT32_MAX)
                goto malformed;
            count_count++;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-distinct-pairs-v1", 6u)) {
            if (distinct_pair_count == UINT32_MAX)
                goto malformed;
            distinct_pair_count++;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-value-list-v1", 4u)) {
            if (value_seen)
                goto malformed;
            value_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-request-v1", 3u)) {
            if (request_seen)
                goto malformed;
            request_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-normal-input-v1", 5u)) {
            if (normal_input_seen)
                goto malformed;
            normal_input_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-compressed-input-v1", 11u)) {
            if (compressed_input_seen)
                goto malformed;
            compressed_input_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-input-byte-v1", 4u)) {
            if (input_byte_count == UINT32_MAX)
                goto malformed;
            input_byte_count++;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-outcome-query-v1", 5u)) {
            if (outcome_query_count == UINT32_MAX)
                goto malformed;
            outcome_query_count++;
        } else {
            goto malformed;
        }
    }
    if (!identity_seen || !value_seen || !request_seen ||
        !normal_input_seen || outcome_query_count == 0u ||
        table_count == 0u || count_count == 0u)
        goto malformed;
    impl->tables = calloc(table_count, sizeof(*impl->tables));
    impl->ordinals = calloc(
        ordinal_count ? ordinal_count : 1u, sizeof(*impl->ordinals));
    impl->control_literals = calloc(
        control_literal_count ? control_literal_count : 1u,
        sizeof(*impl->control_literals));
    impl->counts = calloc(count_count, sizeof(*impl->counts));
    impl->distinct_pairs = calloc(
        distinct_pair_count ? distinct_pair_count : 1u,
        sizeof(*impl->distinct_pairs));
    impl->input_bytes = calloc(
        input_byte_count ? input_byte_count : 1u,
        sizeof(*impl->input_bytes));
    impl->outcome_queries = calloc(
        outcome_query_count, sizeof(*impl->outcome_queries));
    if (!impl->tables || !impl->ordinals || !impl->control_literals ||
        !impl->counts || !impl->distinct_pairs || !impl->input_bytes ||
        !impl->outcome_queries)
        goto resource;

    identity_seen = value_seen = request_seen = normal_input_seen = false;
    compressed_input_seen = false;
    for (answer_index = 0u; answer_index < impl->answers.len;
         answer_index++) {
        const Atom *record = impl->answers.terms[answer_index]->expr.elems[1];

        if (ppproof_runtime_v1_expr_head(
                record, "proof-relational-runtime-identity-v1", 2u)) {
            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(impl->base = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])))
                goto malformed;
            identity_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-state-table-v1", 5u)) {
            PPProofRuntimeTableV1 *table = &impl->tables[table_index++];

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(table->relation = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !(table->table = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[4], &table->arity) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[5], &table->key_arity) ||
                table->arity == 0u || table->key_arity > table->arity)
                goto malformed;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-ordinal-column-v1", 5u)) {
            PPProofRuntimeOrdinalV1 *ordinal =
                &impl->ordinals[ordinal_index++];

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(ordinal->table = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[3], &ordinal->column) ||
                !(ordinal->zero = ppproof_runtime_v1_symbol(
                      record->expr.elems[4])) ||
                !(ordinal->successor = ppproof_runtime_v1_symbol(
                      record->expr.elems[5])))
                goto malformed;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-control-literal-v1", 4u)) {
            PPProofRuntimeControlLiteralV1 *literal =
                &impl->control_literals[control_literal_index++];

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(literal->table = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[3], &literal->column) ||
                !(literal->value = ppproof_runtime_v1_symbol(
                      record->expr.elems[4])))
                goto malformed;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-prefix-count-v1", 7u)) {
            PPProofRuntimeCountV1 *count = &impl->counts[count_index++];

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(count->relation = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !(count->wrapper = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])) ||
                !(count->table = ppproof_runtime_v1_symbol(
                      record->expr.elems[4])) ||
                !(count->zero = ppproof_runtime_v1_symbol(
                      record->expr.elems[5])) ||
                !(count->successor = ppproof_runtime_v1_symbol(
                      record->expr.elems[6])) ||
                !ppproof_runtime_v1_parse_key(
                    record->expr.elems[7], &count->key_source))
                goto malformed;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-distinct-pairs-v1", 6u)) {
            PPProofRuntimeDistinctPairsV1 *pairs =
                &impl->distinct_pairs[distinct_pair_index++];

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(pairs->relation = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !(pairs->table = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[4], &pairs->value_column) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[5], &pairs->filter_column) ||
                !(pairs->filter_value = ppproof_runtime_v1_symbol(
                      record->expr.elems[6])))
                goto malformed;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-value-list-v1", 4u)) {
            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(impl->value_relation = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !(impl->value_cons = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])) ||
                !(impl->value_nil = ppproof_runtime_v1_symbol(
                      record->expr.elems[4])))
                goto malformed;
            value_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-request-v1", 3u)) {
            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(impl->request_relation = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !(impl->request_wrapper = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])))
                goto malformed;
            request_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-normal-input-v1", 5u)) {
            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(impl->query_request_wrapper = ppproof_runtime_v1_symbol(
                      record->expr.elems[2])) ||
                !(impl->query_label_wrapper = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])) ||
                !(impl->query_list_cons = ppproof_runtime_v1_symbol(
                      record->expr.elems[4])) ||
                !(impl->query_list_nil = ppproof_runtime_v1_symbol(
                      record->expr.elems[5])))
                goto malformed;
            normal_input_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-compressed-input-v1", 11u)) {
            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !(impl->compressed_request_wrapper =
                      ppproof_runtime_v1_symbol(record->expr.elems[2])) ||
                !(impl->compressed_label_wrapper =
                      ppproof_runtime_v1_symbol(record->expr.elems[3])) ||
                !(impl->compressed_label_cons =
                      ppproof_runtime_v1_symbol(record->expr.elems[4])) ||
                !(impl->compressed_label_nil =
                      ppproof_runtime_v1_symbol(record->expr.elems[5])) ||
                !(impl->compressed_code_cons =
                      ppproof_runtime_v1_symbol(record->expr.elems[6])) ||
                !(impl->compressed_code_nil =
                      ppproof_runtime_v1_symbol(record->expr.elems[7])) ||
                !(impl->compressed_initial_table =
                      ppproof_runtime_v1_symbol(record->expr.elems[8])) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[9],
                    &impl->compressed_initial_key_column) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[10],
                    &impl->compressed_initial_ordinal_column) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[11],
                    &impl->compressed_initial_value_column))
                goto malformed;
            compressed_input_seen = true;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-input-byte-v1", 4u)) {
            PPProofRuntimeInputByteV1 *input_byte =
                &impl->input_bytes[input_byte_index++];
            uint32_t byte;

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !ppproof_runtime_v1_parse_input(
                    record->expr.elems[2], &input_byte->input) ||
                !(input_byte->relation = ppproof_runtime_v1_symbol(
                      record->expr.elems[3])) ||
                !ppproof_runtime_v1_u32(record->expr.elems[4], &byte) ||
                byte > UINT8_MAX)
                goto malformed;
            input_byte->byte = (uint8_t)byte;
        } else if (ppproof_runtime_v1_expr_head(
                       record,
                       "proof-relational-runtime-outcome-query-v1", 5u)) {
            PPProofRuntimeOutcomeQueryV1 *outcome_query =
                &impl->outcome_queries[outcome_query_index++];

            if (!ppproof_runtime_v1_owner(impl, record->expr.elems[1]) ||
                !ppproof_runtime_v1_parse_input(
                    record->expr.elems[2], &outcome_query->input) ||
                !ppproof_runtime_v1_u32(
                    record->expr.elems[3], &outcome_query->priority) ||
                !ppproof_runtime_v1_parse_result(
                    record->expr.elems[4], &outcome_query->result) ||
                !(outcome_query->query = ppproof_runtime_v1_symbol(
                      record->expr.elems[5])))
                goto malformed;
        } else {
            goto malformed;
        }
    }
    if (!identity_seen || !value_seen || !request_seen ||
        !normal_input_seen ||
        !ppproof_runtime_v1_same(
            impl->request_wrapper, impl->query_request_wrapper))
        goto malformed;

    impl->table_len = table_count;
    impl->ordinal_len = ordinal_count;
    impl->control_literal_len = control_literal_count;
    impl->count_len = count_count;
    impl->distinct_pair_len = distinct_pair_count;
    impl->input_byte_len = input_byte_count;
    impl->outcome_query_len = outcome_query_count;
    for (table_index = 0u; table_index < impl->table_len; table_index++) {
        PPProofRuntimeTableV1 *table = &impl->tables[table_index];
        int32_t table_id = ppproof_runtime_v1_state_table(
            state_plan, table->table);
        uint32_t prior;

        if (table_id < 0 ||
            state_plan->tables[table_id].arity != table->arity ||
            state_plan->tables[table_id].key_arity != table->key_arity ||
            !ppproof_runtime_v1_external(
                native_plan, table->relation, table->arity + 1u))
            goto unsupported;
        for (prior = 0u; prior < table_index; prior++) {
            if (impl->tables[prior].table_id == (uint32_t)table_id)
                goto malformed;
        }
        table->table_id = (uint32_t)table_id;
    }
    for (ordinal_index = 0u; ordinal_index < impl->ordinal_len;
         ordinal_index++) {
        PPProofRuntimeOrdinalV1 *ordinal = &impl->ordinals[ordinal_index];
        int32_t table_id = ppproof_runtime_v1_state_table(
            state_plan, ordinal->table);
        uint32_t prior;

        if (table_id < 0 || ordinal->column >=
                state_plan->tables[table_id].arity ||
            !ppproof_runtime_v1_head(native_plan, ordinal->zero, 0u) ||
            !ppproof_runtime_v1_head(
                native_plan, ordinal->successor, 1u))
            goto unsupported;
        for (prior = 0u; prior < ordinal_index; prior++) {
            if (impl->ordinals[prior].table_id == (uint32_t)table_id &&
                impl->ordinals[prior].column == ordinal->column)
                goto malformed;
        }
        ordinal->table_id = (uint32_t)table_id;
    }
    for (control_literal_index = 0u;
         control_literal_index < impl->control_literal_len;
         control_literal_index++) {
        PPProofRuntimeControlLiteralV1 *literal =
            &impl->control_literals[control_literal_index];
        int32_t table_id = ppproof_runtime_v1_state_table(
            state_plan, literal->table);
        uint32_t prior;

        if (table_id < 0 || literal->column >=
                state_plan->tables[table_id].arity ||
            !ppproof_runtime_v1_head(native_plan, literal->value, 0u))
            goto unsupported;
        for (prior = 0u; prior < impl->ordinal_len; prior++) {
            if (impl->ordinals[prior].table_id == (uint32_t)table_id &&
                impl->ordinals[prior].column == literal->column)
                goto malformed;
        }
        for (prior = 0u; prior < control_literal_index; prior++) {
            const PPProofRuntimeControlLiteralV1 *existing =
                &impl->control_literals[prior];
            if (existing->table_id == (uint32_t)table_id &&
                existing->column == literal->column &&
                ppproof_runtime_v1_same(
                    existing->value, literal->value))
                goto malformed;
        }
        literal->table_id = (uint32_t)table_id;
    }
    for (distinct_pair_index = 0u;
         distinct_pair_index < impl->distinct_pair_len;
         distinct_pair_index++) {
        PPProofRuntimeDistinctPairsV1 *pairs =
            &impl->distinct_pairs[distinct_pair_index];
        int32_t table_id = ppproof_runtime_v1_state_table(
            state_plan, pairs->table);
        uint32_t prior;
        bool filter_declared = false;

        if (table_id < 0 ||
            pairs->value_column >= state_plan->tables[table_id].arity ||
            pairs->filter_column >= state_plan->tables[table_id].arity ||
            !ppproof_runtime_v1_external(
                native_plan, pairs->relation, 2u) ||
            !ppproof_runtime_v1_head(
                native_plan, pairs->filter_value, 0u))
            goto unsupported;
        for (prior = 0u; prior < impl->control_literal_len; prior++) {
            const PPProofRuntimeControlLiteralV1 *literal =
                &impl->control_literals[prior];
            if (literal->table_id == (uint32_t)table_id &&
                literal->column == pairs->filter_column &&
                ppproof_runtime_v1_same(
                    literal->value, pairs->filter_value)) {
                filter_declared = true;
                break;
            }
        }
        if (!filter_declared)
            goto unsupported;
        for (prior = 0u; prior < distinct_pair_index; prior++) {
            const PPProofRuntimeDistinctPairsV1 *existing =
                &impl->distinct_pairs[prior];
            if (existing->table_id == (uint32_t)table_id &&
                existing->value_column == pairs->value_column &&
                existing->filter_column == pairs->filter_column &&
                ppproof_runtime_v1_same(
                    existing->relation, pairs->relation) &&
                ppproof_runtime_v1_same(
                    existing->filter_value, pairs->filter_value))
                goto malformed;
        }
        pairs->table_id = (uint32_t)table_id;
    }
    for (count_index = 0u; count_index < impl->count_len; count_index++) {
        PPProofRuntimeCountV1 *count = &impl->counts[count_index];
        int32_t table_id = ppproof_runtime_v1_state_table(
            state_plan, count->table);

        if (table_id < 0 ||
            state_plan->tables[table_id].arity == 0u ||
            state_plan->tables[table_id].key_arity == 0u ||
            !ppproof_runtime_v1_external(native_plan, count->relation, 2u) ||
            !ppproof_runtime_v1_head(native_plan, count->wrapper, 2u) ||
            !ppproof_runtime_v1_head(native_plan, count->zero, 0u) ||
            !ppproof_runtime_v1_head(
                native_plan, count->successor, 1u))
            goto unsupported;
        count->table_id = (uint32_t)table_id;
    }
    if (!ppproof_runtime_v1_external(native_plan, impl->value_relation, 3u) ||
        !ppproof_runtime_v1_external(native_plan, impl->request_relation, 4u) ||
        !ppproof_runtime_v1_head(native_plan, impl->value_cons, 2u) ||
        !ppproof_runtime_v1_head(native_plan, impl->value_nil, 0u) ||
        !ppproof_runtime_v1_head(native_plan, impl->request_wrapper, 2u) ||
        !ppproof_runtime_v1_head(native_plan, impl->query_label_wrapper, 2u) ||
        !ppproof_runtime_v1_head(native_plan, impl->query_list_cons, 2u) ||
        !ppproof_runtime_v1_head(native_plan, impl->query_list_nil, 0u))
        goto unsupported;
    impl->normal_input_ready = true;
    if (compressed_input_seen) {
        int32_t table_id = ppproof_runtime_v1_state_table(
            state_plan, impl->compressed_initial_table);
        bool ordinal_declared = false;

        if (table_id < 0 ||
            impl->compressed_initial_key_column != 0u ||
            impl->compressed_initial_key_column >=
                state_plan->tables[table_id].key_arity ||
            impl->compressed_initial_ordinal_column >=
                state_plan->tables[table_id].arity ||
            impl->compressed_initial_value_column >=
                state_plan->tables[table_id].arity ||
            impl->compressed_initial_ordinal_column ==
                impl->compressed_initial_value_column ||
            state_plan->tables[table_id].arity >= 32u ||
            !ppproof_runtime_v1_same(
                impl->request_wrapper,
                impl->compressed_request_wrapper) ||
            !ppproof_runtime_v1_head(
                native_plan, impl->compressed_label_wrapper, 2u) ||
            !ppproof_runtime_v1_head(
                native_plan, impl->compressed_label_cons, 2u) ||
            !ppproof_runtime_v1_head(
                native_plan, impl->compressed_label_nil, 0u) ||
            !ppproof_runtime_v1_head(
                native_plan, impl->compressed_code_cons, 2u) ||
            !ppproof_runtime_v1_head(
                native_plan, impl->compressed_code_nil, 0u))
            goto unsupported;
        for (ordinal_index = 0u; ordinal_index < impl->ordinal_len;
             ordinal_index++) {
            if (impl->ordinals[ordinal_index].table_id ==
                    (uint32_t)table_id &&
                impl->ordinals[ordinal_index].column ==
                    impl->compressed_initial_ordinal_column) {
                ordinal_declared = true;
                break;
            }
        }
        if (!ordinal_declared)
            goto unsupported;
        impl->compressed_initial_table_id = (uint32_t)table_id;
        impl->compressed_query_ready = true;
    }
    for (input_byte_index = 0u;
         input_byte_index < impl->input_byte_len; input_byte_index++) {
        const PPProofRuntimeInputByteV1 *input_byte =
            &impl->input_bytes[input_byte_index];
        uint32_t prior;

        if ((input_byte->input == PPPROOF_RUNTIME_INPUT_V1_NORMAL &&
             !impl->normal_input_ready) ||
            (input_byte->input == PPPROOF_RUNTIME_INPUT_V1_COMPRESSED &&
             !impl->compressed_query_ready) ||
            !ppproof_runtime_v1_external(
                native_plan, input_byte->relation, 1u))
            goto unsupported;
        for (prior = 0u; prior < input_byte_index; prior++) {
            const PPProofRuntimeInputByteV1 *existing =
                &impl->input_bytes[prior];
            if (existing->input == input_byte->input &&
                existing->byte == input_byte->byte &&
                ppproof_runtime_v1_same(
                    existing->relation, input_byte->relation))
                goto malformed;
        }
    }
    for (outcome_query_index = 0u;
         outcome_query_index < impl->outcome_query_len;
         outcome_query_index++) {
        const PPProofRuntimeOutcomeQueryV1 *outcome_query =
            &impl->outcome_queries[outcome_query_index];
        uint32_t query_arity =
            outcome_query->input == PPPROOF_RUNTIME_INPUT_V1_NORMAL
                ? 2u : 4u;
        uint32_t prior;
        bool input_bridge_seen = false;

        if ((outcome_query->input == PPPROOF_RUNTIME_INPUT_V1_NORMAL &&
             !impl->normal_input_ready) ||
            (outcome_query->input == PPPROOF_RUNTIME_INPUT_V1_COMPRESSED &&
             !impl->compressed_query_ready) ||
            !ppproof_runtime_v1_head(
                native_plan, outcome_query->query, query_arity))
            goto unsupported;
        for (prior = 0u; prior < outcome_query_index; prior++) {
            const PPProofRuntimeOutcomeQueryV1 *existing =
                &impl->outcome_queries[prior];
            if (existing->input == outcome_query->input &&
                existing->priority == outcome_query->priority)
                goto malformed;
        }
        if (outcome_query->result ==
                PPRELATIONAL_STATE_PROOF_V1_INCOMPLETE) {
            for (prior = 0u; prior < impl->input_byte_len; prior++) {
                if (impl->input_bytes[prior].input == outcome_query->input) {
                    input_bridge_seen = true;
                    break;
                }
            }
            if (!input_bridge_seen)
                goto unsupported;
        }
    }
    {
        PPProofRuntimeInputV1 input;
        for (input = PPPROOF_RUNTIME_INPUT_V1_NORMAL;
             input <= PPPROOF_RUNTIME_INPUT_V1_COMPRESSED;
             input = (PPProofRuntimeInputV1)((uint32_t)input + 1u)) {
            uint32_t candidate_len = 0u;
            uint32_t priority;

            if (input == PPPROOF_RUNTIME_INPUT_V1_COMPRESSED &&
                !impl->compressed_query_ready)
                continue;
            for (outcome_query_index = 0u;
                 outcome_query_index < impl->outcome_query_len;
                 outcome_query_index++) {
                if (impl->outcome_queries[outcome_query_index].input == input)
                    candidate_len++;
            }
            if (candidate_len == 0u)
                goto unsupported;
            for (priority = 0u; priority < candidate_len; priority++) {
                const PPProofRuntimeOutcomeQueryV1 *candidate = NULL;
                for (outcome_query_index = 0u;
                     outcome_query_index < impl->outcome_query_len;
                     outcome_query_index++) {
                    const PPProofRuntimeOutcomeQueryV1 *current =
                        &impl->outcome_queries[outcome_query_index];
                    if (current->input == input &&
                        current->priority == priority) {
                        candidate = current;
                        break;
                    }
                }
                if (!candidate ||
                    (priority == 0u && candidate->result !=
                        PPRELATIONAL_STATE_PROOF_V1_VERIFIED))
                    goto unsupported;
            }
        }
    }

    impl->state_plan = state_plan;
    impl->native_plan = native_plan;
    impl->vm = vm;
    impl->limits = limits;
    memcpy(impl->receipt.provider_digest, impl->answers.digest, 65u);
    ppproof_runtime_v1_impl_free(runtime->implementation);
    runtime->implementation = impl;
    return true;

resource:
    (void)ppproof_runtime_v1_fail(
        error_buf, error_buf_size,
        "relational proof runtime vector allocation failed");
    goto failed;
unsupported:
    (void)ppproof_runtime_v1_fail(
        error_buf, error_buf_size,
        "relational proof runtime program is not admitted by its state and native plans");
    goto failed;
malformed:
    (void)ppproof_runtime_v1_fail(
        error_buf, error_buf_size,
        "relational proof runtime artifact is malformed or non-parametric");
failed:
    ppproof_runtime_v1_impl_free(impl);
    return false;
}

static bool ppproof_runtime_v1_rows_grow(
    PPProofRuntimeRowsV1 *rows, uint32_t required) {
    uint32_t cap;
    Atom **next;

    if (required <= rows->row_cap)
        return true;
    cap = rows->row_cap ? rows->row_cap : 64u;
    while (cap < required) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    next = realloc(rows->rows, (size_t)cap * sizeof(*next));
    if (!next)
        return false;
    rows->rows = next;
    rows->row_cap = cap;
    return true;
}

static bool ppproof_runtime_v1_rows_append(
    PPProofRuntimeRowsV1 *rows, Atom *row) {
    if (!row || rows->row_len == UINT32_MAX ||
        !ppproof_runtime_v1_rows_grow(rows, rows->row_len + 1u))
        return false;
    rows->rows[rows->row_len++] = row;
    return true;
}

static Atom *ppproof_runtime_v1_app(
    Arena *arena, const char *head, Atom *const *arguments,
    uint32_t argument_len) {
    Atom **elements;
    uint32_t index;

    if (argument_len > UINT16_MAX - 1u)
        return NULL;
    elements = arena_alloc(
        arena, ((size_t)argument_len + 1u) * sizeof(*elements));
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    return atom_expr(arena, elements, (CettaExprLen)(argument_len + 1u));
}

static Atom *ppproof_runtime_v1_binary(
    Arena *arena, const char *head, Atom *left, Atom *right) {
    Atom *arguments[2] = {left, right};
    return ppproof_runtime_v1_app(arena, head, arguments, 2u);
}

static Atom *ppproof_runtime_v1_scalar(
    Arena *arena, const uint8_t *bytes, uint32_t len) {
    static const char hex[] = "0123456789abcdef";
    static const char prefix[] = "@pp-scalar:";
    size_t prefix_len = sizeof(prefix) - 1u;
    char *text;
    uint32_t index;

    if (!bytes || len == 0u || len > (SIZE_MAX - prefix_len - 1u) / 2u)
        return NULL;
    text = arena_alloc(arena, prefix_len + (size_t)len * 2u + 1u);
    if (!text)
        return NULL;
    memcpy(text, prefix, prefix_len);
    for (index = 0u; index < len; index++) {
        text[prefix_len + (size_t)index * 2u] = hex[bytes[index] >> 4u];
        text[prefix_len + (size_t)index * 2u + 1u] =
            hex[bytes[index] & 15u];
    }
    text[prefix_len + (size_t)len * 2u] = '\0';
    return atom_symbol(arena, text);
}

static Atom *ppproof_runtime_v1_list_key(
    Arena *arena, const uint8_t *bytes, uint32_t len) {
    static const char hex[] = "0123456789abcdef";
    static const char prefix[] = "@pp-list:";
    size_t prefix_len = sizeof(prefix) - 1u;
    char *text;
    uint32_t index;

    if (!bytes || len > (SIZE_MAX - prefix_len - 1u) / 2u)
        return NULL;
    text = arena_alloc(arena, prefix_len + (size_t)len * 2u + 1u);
    if (!text)
        return NULL;
    memcpy(text, prefix, prefix_len);
    for (index = 0u; index < len; index++) {
        text[prefix_len + (size_t)index * 2u] = hex[bytes[index] >> 4u];
        text[prefix_len + (size_t)index * 2u + 1u] =
            hex[bytes[index] & 15u];
    }
    text[prefix_len + (size_t)len * 2u] = '\0';
    return atom_symbol(arena, text);
}

static bool ppproof_runtime_v1_list_seen(
    const PPProofRuntimeRowsV1 *rows, uint32_t value) {
    uint32_t index;
    for (index = 0u; index < rows->list_value_len; index++) {
        if (rows->list_values[index] == value)
            return true;
    }
    return false;
}

static bool ppproof_runtime_v1_list_remember(
    PPProofRuntimeRowsV1 *rows, uint32_t value) {
    uint32_t cap;
    uint32_t *next;

    if (ppproof_runtime_v1_list_seen(rows, value))
        return true;
    if (rows->list_value_len == rows->list_value_cap) {
        cap = rows->list_value_cap ? rows->list_value_cap * 2u : 32u;
        if (cap < rows->list_value_cap)
            return false;
        next = realloc(rows->list_values, (size_t)cap * sizeof(*next));
        if (!next)
            return false;
        rows->list_values = next;
        rows->list_value_cap = cap;
    }
    rows->list_values[rows->list_value_len++] = value;
    return true;
}

static bool ppproof_runtime_v1_list_seed(
    PPProofRuntimeRowsV1 *rows,
    const PPProofRuntimeRowsV1 *stable_rows) {
    if (!rows || !stable_rows)
        return false;
    if (stable_rows->list_value_len == 0u)
        return true;
    rows->list_values = malloc(
        (size_t)stable_rows->list_value_len *
        sizeof(*rows->list_values));
    if (!rows->list_values)
        return false;
    memcpy(rows->list_values, stable_rows->list_values,
           (size_t)stable_rows->list_value_len *
           sizeof(*rows->list_values));
    rows->list_value_len = stable_rows->list_value_len;
    rows->list_value_cap = stable_rows->list_value_len;
    return true;
}

static Atom *ppproof_runtime_v1_value(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    PPProofRuntimeRowsV1 *rows,
    uint32_t value);

static Atom *ppproof_runtime_v1_list_value(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    PPProofRuntimeRowsV1 *rows,
    uint32_t value,
    const uint8_t *bytes,
    uint32_t len,
    PPRelationalValueListV1Cursor cursor) {
    Atom *key = ppproof_runtime_v1_list_key(&rows->arena, bytes, len);
    Atom *list;
    Atom *owner;
    Atom *arguments[3];
    Atom **items = NULL;
    uint32_t index;

    (void)store;
    if (!key)
        return NULL;
    if (ppproof_runtime_v1_list_seen(rows, value))
        return key;
    if (cursor.item_len != 0u) {
        items = arena_alloc(
            &rows->arena, (size_t)cursor.item_len * sizeof(*items));
        if (!items)
            return NULL;
    }
    for (index = 0u; index < cursor.item_len; index++) {
        const uint8_t *item_bytes = NULL;
        uint32_t item_len = 0u;
        if (!pprelational_value_list_v1_cursor_next(
                &cursor, &item_bytes, &item_len) ||
            !(items[index] = ppproof_runtime_v1_scalar(
                  &rows->arena, item_bytes, item_len)))
            return NULL;
    }
    list = atom_symbol(&rows->arena, impl->value_nil);
    for (index = cursor.item_len; index > 0u; index--)
        list = ppproof_runtime_v1_binary(
            &rows->arena, impl->value_cons, items[index - 1u], list);
    owner = atom_symbol(&rows->arena, impl->owner);
    arguments[0] = owner;
    arguments[1] = key;
    arguments[2] = list;
    if (!list ||
        !ppproof_runtime_v1_rows_append(
            rows, ppproof_runtime_v1_app(
                &rows->arena, impl->value_relation, arguments, 3u)) ||
        !ppproof_runtime_v1_list_remember(rows, value))
        return NULL;
    return key;
}

static Atom *ppproof_runtime_v1_value(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    PPProofRuntimeRowsV1 *rows,
    uint32_t value) {
    const uint8_t *bytes = NULL;
    uint32_t len = 0u;
    PPRelationalValueListV1Cursor cursor;

    if (!store->value_bytes(store->context, value, &bytes, &len) ||
        !bytes || len == 0u)
        return NULL;
    if (pprelational_value_list_v1_cursor_init(bytes, len, &cursor))
        return ppproof_runtime_v1_list_value(
            impl, store, rows, value, bytes, len, cursor);
    return ppproof_runtime_v1_scalar(&rows->arena, bytes, len);
}

static const PPProofRuntimeOrdinalV1 *ppproof_runtime_v1_ordinal(
    const PPProofRuntimeImplV1 *impl,
    uint32_t table_id, uint32_t column) {
    uint32_t index;
    for (index = 0u; index < impl->ordinal_len; index++) {
        if (impl->ordinals[index].table_id == table_id &&
            impl->ordinals[index].column == column)
            return &impl->ordinals[index];
    }
    return NULL;
}

static const char *ppproof_runtime_v1_control_literal(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    uint32_t table_id,
    uint32_t column,
    uint32_t value,
    bool *column_declared_out) {
    const uint8_t *bytes = NULL;
    uint32_t len = 0u;
    uint32_t index;

    *column_declared_out = false;
    if (!store->value_bytes(store->context, value, &bytes, &len) ||
        !bytes || len == 0u)
        return NULL;
    for (index = 0u; index < impl->control_literal_len; index++) {
        const PPProofRuntimeControlLiteralV1 *literal =
            &impl->control_literals[index];
        size_t literal_len;

        if (literal->table_id != table_id || literal->column != column)
            continue;
        *column_declared_out = true;
        literal_len = strlen(literal->value);
        if (literal_len == len && memcmp(literal->value, bytes, len) == 0)
            return literal->value;
    }
    return NULL;
}

static bool ppproof_runtime_v1_constructor_chains_grow(
    PPProofRuntimeRowsV1 *rows, uint32_t required) {
    PPProofRuntimeConstructorChainV1 *next;
    uint32_t cap;

    if (required <= rows->constructor_chain_cap)
        return true;
    cap = rows->constructor_chain_cap ? rows->constructor_chain_cap : 4u;
    while (cap < required) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    next = realloc(
        rows->constructor_chains, (size_t)cap * sizeof(*next));
    if (!next)
        return false;
    memset(
        next + rows->constructor_chain_cap, 0,
        (size_t)(cap - rows->constructor_chain_cap) * sizeof(*next));
    rows->constructor_chains = next;
    rows->constructor_chain_cap = cap;
    return true;
}

static bool ppproof_runtime_v1_constructor_values_grow(
    PPProofRuntimeConstructorChainV1 *chain, uint32_t required) {
    Atom **next;
    uint32_t cap;

    if (required <= chain->value_cap)
        return true;
    cap = chain->value_cap ? chain->value_cap : 16u;
    while (cap < required) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    next = realloc(chain->values, (size_t)cap * sizeof(*next));
    if (!next)
        return false;
    chain->values = next;
    chain->value_cap = cap;
    return true;
}

static Atom *ppproof_runtime_v1_constructor_chain(
    PPProofRuntimeRowsV1 *rows, const char *zero, const char *successor,
    uint32_t value) {
    PPProofRuntimeConstructorChainV1 *chain = NULL;
    uint32_t index;

    if (!rows || !zero || !successor || value == UINT32_MAX ||
        rows->constructor_chain_requests == UINT64_MAX)
        return NULL;
    rows->constructor_chain_requests++;
    for (index = 0u; index < rows->constructor_chain_len; index++) {
        PPProofRuntimeConstructorChainV1 *candidate =
            &rows->constructor_chains[index];
        if (ppproof_runtime_v1_same(candidate->zero, zero) &&
            ppproof_runtime_v1_same(candidate->successor, successor)) {
            chain = candidate;
            break;
        }
    }
    if (!chain) {
        if (rows->constructor_chain_len == UINT32_MAX ||
            !ppproof_runtime_v1_constructor_chains_grow(
                rows, rows->constructor_chain_len + 1u))
            return NULL;
        chain = &rows->constructor_chains[rows->constructor_chain_len++];
        chain->zero = zero;
        chain->successor = successor;
    }
    if (!ppproof_runtime_v1_constructor_values_grow(chain, value + 1u))
        return NULL;
    if (chain->value_len == 0u) {
        chain->values[0] = atom_symbol(&rows->arena, zero);
        if (!chain->values[0] ||
            rows->constructor_chain_nodes == UINT64_MAX)
            return NULL;
        rows->constructor_chain_nodes++;
        chain->value_len = 1u;
    }
    while (chain->value_len <= value) {
        Atom *previous = chain->values[chain->value_len - 1u];
        Atom *next = ppproof_runtime_v1_app(
            &rows->arena, successor, &previous, 1u);
        if (!next || rows->constructor_chain_nodes == UINT64_MAX)
            return NULL;
        rows->constructor_chain_nodes++;
        chain->values[chain->value_len++] = next;
    }
    return chain->values[value];
}

static bool ppproof_runtime_v1_state_table_rows(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    const PPProofRuntimeTableV1 *table,
    uint32_t row_begin,
    uint32_t row_end,
    PPProofRuntimeRowsV1 *rows,
    char *error_buf, size_t error_buf_size) {
    uint32_t arity = 0u;
    uint32_t key_arity = 0u;
    uint32_t row_len = 0u;
    uint32_t row_index;
    uint32_t *values;
    Atom **arguments;

    if (!store->table_shape(
            store->context, table->table_id,
            &arity, &key_arity, &row_len) ||
        arity != table->arity || key_arity != table->key_arity ||
        row_begin > row_end || row_end > row_len)
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated provider table no longer matches the state store");
    values = malloc((size_t)arity * sizeof(*values));
    arguments = arena_alloc(
        &rows->arena, ((size_t)arity + 1u) * sizeof(*arguments));
    if (!values || !arguments) {
        free(values);
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "relational provider row allocation failed");
    }
    arguments[0] = atom_symbol(&rows->arena, table->table);
    for (row_index = row_begin; row_index < row_end; row_index++) {
        uint32_t column;
        if (!store->table_row(
                store->context, table->table_id, row_index,
                values, arity)) {
            free(values);
            return ppproof_runtime_v1_fail(
                error_buf, error_buf_size,
                "relational provider could not read a declared state row");
        }
        for (column = 0u; column < arity; column++) {
            const PPProofRuntimeOrdinalV1 *ordinal =
                ppproof_runtime_v1_ordinal(
                    impl, table->table_id, column);
            bool control_column = false;
            const char *control_literal =
                ppproof_runtime_v1_control_literal(
                    impl, store, table->table_id, column,
                    values[column], &control_column);
            if (ordinal) {
                uint32_t decoded = 0u;
                if (!pprelational_store_v1_value_u32_decimal(
                        store, values[column], &decoded)) {
                    free(values);
                    return ppproof_runtime_v1_fail(
                        error_buf, error_buf_size,
                        "generated ordinal codec rejected a state value");
                }
                arguments[column + 1u] =
                    ppproof_runtime_v1_constructor_chain(
                        rows, ordinal->zero,
                        ordinal->successor, decoded);
            } else if (control_column) {
                if (!control_literal) {
                    free(values);
                    return ppproof_runtime_v1_fail(
                        error_buf, error_buf_size,
                        "generated control-literal column rejected a state value");
                }
                arguments[column + 1u] = atom_symbol(
                    &rows->arena, control_literal);
            } else {
                arguments[column + 1u] = ppproof_runtime_v1_value(
                    impl, store, rows, values[column]);
            }
            if (!arguments[column + 1u]) {
                free(values);
                return ppproof_runtime_v1_fail(
                    error_buf, error_buf_size,
                    "relational provider could not decode a state value");
            }
        }
        if (!ppproof_runtime_v1_rows_append(
                rows, ppproof_runtime_v1_app(
                    &rows->arena, table->relation,
                    arguments, arity + 1u))) {
            free(values);
            return ppproof_runtime_v1_fail(
                error_buf, error_buf_size,
                "relational provider row vector exhausted resources");
        }
    }
    free(values);
    return true;
}

static bool ppproof_runtime_v1_state_rows(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    PPProofRuntimeRowsV1 *rows,
    char *error_buf, size_t error_buf_size) {
    uint32_t table_index;

    for (table_index = 0u; table_index < impl->table_len; table_index++) {
        const PPProofRuntimeTableV1 *table = &impl->tables[table_index];
        uint32_t arity = 0u;
        uint32_t key_arity = 0u;
        uint32_t row_len = 0u;

        if (impl->state_plan->tables[table->table_id].lifetime ==
            PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT)
            continue;
        if (!store->table_shape(
                store->context, table->table_id,
                &arity, &key_arity, &row_len) ||
            !ppproof_runtime_v1_state_table_rows(
                impl, store, table, 0u, row_len, rows,
                error_buf, error_buf_size))
            return false;
    }
    return true;
}

static bool ppproof_runtime_v1_persistent_cache_start(
    PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    char *error_buf, size_t error_buf_size) {
    PPProofRuntimePersistentCacheV1 *cache;

    ppproof_runtime_v1_persistent_cache_free(impl->persistent_cache);
    impl->persistent_cache = NULL;
    cache = calloc(1u, sizeof(*cache));
    if (!cache || (impl->table_len != 0u &&
        !(cache->table_row_lens = calloc(
            impl->table_len, sizeof(*cache->table_row_lens))))) {
        ppproof_runtime_v1_persistent_cache_free(cache);
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "persistent capability cache allocation failed");
    }
    cache->table_len = impl->table_len;
    cache->store_identity = store->identity;
    ppproof_runtime_v1_rows_init(
        &cache->rows, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    pposlf_native_capability_set_v1_init(&cache->capabilities);
    if (!pposlf_native_capability_set_v1_prepare_borrowed(
            &cache->capabilities, impl->native_plan,
            NULL, 0u, error_buf, error_buf_size)) {
        ppproof_runtime_v1_persistent_cache_free(cache);
        return false;
    }
    impl->persistent_cache = cache;
    return true;
}

static bool ppproof_runtime_v1_persistent_cache_refresh(
    PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    char *error_buf, size_t error_buf_size) {
    PPProofRuntimePersistentCacheV1 *cache;
    uint32_t *next_lengths = NULL;
    uint32_t new_row_begin;
    uint32_t table_index;
    bool ok = false;

    if (!impl->persistent_cache ||
        impl->persistent_cache->store_identity != store->identity) {
        if (!ppproof_runtime_v1_persistent_cache_start(
                impl, store, error_buf, error_buf_size))
            return false;
    }
    cache = impl->persistent_cache;
    next_lengths = malloc(
        (size_t)(impl->table_len ? impl->table_len : 1u) *
        sizeof(*next_lengths));
    if (!next_lengths)
        goto failed;
    memcpy(next_lengths, cache->table_row_lens,
           (size_t)impl->table_len * sizeof(*next_lengths));
    new_row_begin = cache->rows.row_len;
    for (table_index = 0u; table_index < impl->table_len; table_index++) {
        const PPProofRuntimeTableV1 *table = &impl->tables[table_index];
        uint32_t arity = 0u;
        uint32_t key_arity = 0u;
        uint32_t row_len = 0u;
        uint32_t stable_len = 0u;

        if (impl->state_plan->tables[table->table_id].lifetime !=
            PPRELATIONAL_STATE_LIFETIME_V1_PERSISTENT)
            continue;
        if (!store->table_immutable_prefix(
                store->context, table->table_id, &stable_len) ||
            !store->table_shape(
                store->context, table->table_id,
                &arity, &key_arity, &row_len) ||
            stable_len != row_len ||
            stable_len < cache->table_row_lens[table_index] ||
            !ppproof_runtime_v1_state_table_rows(
                impl, store, table,
                cache->table_row_lens[table_index], stable_len,
                &cache->rows, error_buf, error_buf_size))
            goto failed;
        next_lengths[table_index] = stable_len;
    }
    if (cache->rows.row_len > new_row_begin &&
        !pposlf_native_capability_set_v1_append_borrowed_deferred(
            &cache->capabilities, impl->native_plan,
            &cache->rows.rows[new_row_begin],
            cache->rows.row_len - new_row_begin,
            error_buf, error_buf_size))
        goto failed;
    memcpy(cache->table_row_lens, next_lengths,
           (size_t)impl->table_len * sizeof(*next_lengths));
    ok = true;
    goto done;

failed:
    if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
        (void)ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "persistent capability prefix changed or exhausted resources");
    ppproof_runtime_v1_persistent_cache_free(impl->persistent_cache);
    impl->persistent_cache = NULL;
done:
    free(next_lengths);
    return ok;
}

static bool ppproof_runtime_v1_store_values_equal(
    const PPRelationalStoreV1 *store,
    uint32_t left,
    uint32_t right,
    bool *equal_out) {
    const uint8_t *left_bytes = NULL;
    const uint8_t *right_bytes = NULL;
    uint32_t left_len = 0u;
    uint32_t right_len = 0u;

    if (!store || !equal_out ||
        !store->value_bytes(
            store->context, left, &left_bytes, &left_len) ||
        !store->value_bytes(
            store->context, right, &right_bytes, &right_len) ||
        !left_bytes || !right_bytes || left_len == 0u || right_len == 0u)
        return false;
    *equal_out = left_len == right_len &&
                 memcmp(left_bytes, right_bytes, left_len) == 0;
    return true;
}

static bool ppproof_runtime_v1_distinct_rows(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStoreV1 *store,
    PPProofRuntimeRowsV1 *rows,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t descriptor_index;

    for (descriptor_index = 0u;
         descriptor_index < impl->distinct_pair_len;
         descriptor_index++) {
        const PPProofRuntimeDistinctPairsV1 *pairs =
            &impl->distinct_pairs[descriptor_index];
        uint32_t arity = 0u;
        uint32_t key_arity = 0u;
        uint32_t row_len = 0u;
        uint32_t *row_values = NULL;
        uint32_t *selected = NULL;
        uint32_t selected_len = 0u;
        uint32_t row_index;
        size_t filter_len = strlen(pairs->filter_value);

        if (!store->table_shape(
                store->context, pairs->table_id,
                &arity, &key_arity, &row_len) ||
            pairs->value_column >= arity || pairs->filter_column >= arity ||
            (size_t)arity > SIZE_MAX / sizeof(*row_values) ||
            (size_t)(row_len ? row_len : 1u) >
                SIZE_MAX / sizeof(*selected))
            return ppproof_runtime_v1_fail(
                error_buf, error_buf_size,
                "generated distinct-pair provider no longer matches its state table");
        row_values = malloc((size_t)arity * sizeof(*row_values));
        selected = malloc(
            (size_t)(row_len ? row_len : 1u) * sizeof(*selected));
        if (!row_values || !selected) {
            free(row_values);
            free(selected);
            return ppproof_runtime_v1_fail(
                error_buf, error_buf_size,
                "generated distinct-pair provider exhausted resources");
        }
        for (row_index = 0u; row_index < row_len; row_index++) {
            const uint8_t *filter_bytes = NULL;
            uint32_t filter_bytes_len = 0u;
            uint32_t prior;
            bool duplicate = false;

            if (!store->table_row(
                    store->context, pairs->table_id, row_index,
                    row_values, arity) ||
                !store->value_bytes(
                    store->context,
                    row_values[pairs->filter_column],
                    &filter_bytes, &filter_bytes_len) ||
                !filter_bytes || filter_bytes_len == 0u) {
                free(row_values);
                free(selected);
                return ppproof_runtime_v1_fail(
                    error_buf, error_buf_size,
                    "generated distinct-pair provider could not inspect a state row");
            }
            if (filter_len != filter_bytes_len ||
                memcmp(pairs->filter_value, filter_bytes, filter_len) != 0)
                continue;
            for (prior = 0u; prior < selected_len; prior++) {
                bool equal = false;
                if (!ppproof_runtime_v1_store_values_equal(
                        store, selected[prior],
                        row_values[pairs->value_column], &equal)) {
                    free(row_values);
                    free(selected);
                    return ppproof_runtime_v1_fail(
                        error_buf, error_buf_size,
                        "generated distinct-pair provider could not compare state values");
                }
                if (equal) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                selected[selected_len++] = row_values[pairs->value_column];
        }
        for (row_index = 0u; row_index < selected_len; row_index++) {
            uint32_t right_index;
            Atom *left = ppproof_runtime_v1_value(
                impl, store, rows, selected[row_index]);
            if (!left) {
                free(row_values);
                free(selected);
                return ppproof_runtime_v1_fail(
                    error_buf, error_buf_size,
                    "generated distinct-pair provider could not decode a value");
            }
            for (right_index = 0u; right_index < selected_len;
                 right_index++) {
                Atom *right;
                if (right_index == row_index)
                    continue;
                right = ppproof_runtime_v1_value(
                    impl, store, rows, selected[right_index]);
                if (!right ||
                    !ppproof_runtime_v1_rows_append(
                        rows, ppproof_runtime_v1_binary(
                            &rows->arena, pairs->relation, left, right))) {
                    free(row_values);
                    free(selected);
                    return ppproof_runtime_v1_fail(
                        error_buf, error_buf_size,
                        "generated distinct-pair provider row vector exhausted resources");
                }
            }
        }
        free(row_values);
        free(selected);
    }
    return true;
}

static bool ppproof_runtime_v1_prefix_count(
    const PPRelationalStoreV1 *store,
    uint32_t table_id,
    PPRelationalValueV1Slice key,
    uint32_t *count_out) {
    uint32_t arity = 0u;
    uint32_t key_arity = 0u;
    uint32_t row_len = 0u;
    uint32_t key_value = 0u;
    uint32_t *values = NULL;
    uint64_t cursor = UINT64_MAX;
    uint32_t count = 0u;
    bool found = false;
    bool ok = false;

    if (!key.bytes || key.len == 0u ||
        !store->value_intern(
            store->context, key.bytes, key.len, &key_value) ||
        !store->table_shape(
            store->context, table_id, &arity, &key_arity, &row_len) ||
        arity == 0u || key_arity == 0u)
        return false;
    values = malloc((size_t)arity * sizeof(*values));
    if (!values)
        return false;
    do {
        if (!store->table_prefix_next(
                store->context, table_id, &key_value, 1u, 1u,
                &cursor, values, arity, &found))
            goto done;
        if (found) {
            if (count == UINT32_MAX)
                goto done;
            count++;
        }
    } while (found);
    *count_out = count;
    ok = true;
done:
    free(values);
    return ok;
}

static bool ppproof_runtime_v1_slice_equal(
    PPRelationalValueV1Slice left, PPRelationalValueV1Slice right) {
    return left.len == right.len && left.bytes && right.bytes &&
           memcmp(left.bytes, right.bytes, left.len) == 0;
}

static bool ppproof_runtime_v1_count_row(
    const PPProofRuntimeImplV1 *impl,
    const PPProofRuntimeCountV1 *count,
    const PPRelationalStoreV1 *store,
    PPProofRuntimeRowsV1 *rows,
    PPRelationalValueV1Slice key,
    char *error_buf, size_t error_buf_size) {
    uint32_t row_count = 0u;
    Atom *key_atom;
    Atom *wrapped;
    Atom *natural;
    Atom *arguments[2];

    if (!ppproof_runtime_v1_prefix_count(
            store, count->table_id, key, &row_count) ||
        !(key_atom = ppproof_runtime_v1_scalar(
              &rows->arena, key.bytes, key.len)))
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated prefix-count provider could not inspect its state key");
    wrapped = ppproof_runtime_v1_binary(
        &rows->arena, count->wrapper,
        atom_symbol(&rows->arena, impl->owner), key_atom);
    natural = ppproof_runtime_v1_constructor_chain(
        rows, count->zero, count->successor, row_count);
    arguments[0] = wrapped;
    arguments[1] = natural;
    if (!wrapped || !natural ||
        !ppproof_runtime_v1_rows_append(
            rows, ppproof_runtime_v1_app(
                &rows->arena, count->relation, arguments, 2u)))
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated prefix-count provider exhausted resources");
    return true;
}

static bool ppproof_runtime_v1_count_rows(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStateProofV1Request *request,
    PPProofRuntimeRowsV1 *rows,
    char *error_buf, size_t error_buf_size) {
    uint32_t count_index;

    for (count_index = 0u; count_index < impl->count_len; count_index++) {
        const PPProofRuntimeCountV1 *count = &impl->counts[count_index];
        if (count->key_source == PPPROOF_RUNTIME_KEY_V1_REQUEST_LABEL) {
            if (!ppproof_runtime_v1_count_row(
                    impl, count, request->store, rows, request->label,
                    error_buf, error_buf_size))
                return false;
        } else {
            uint32_t proof_index;
            for (proof_index = 0u; proof_index < request->proof_len;
                 proof_index++) {
                uint32_t prior;
                bool duplicate = false;
                for (prior = 0u; prior < proof_index; prior++) {
                    if (ppproof_runtime_v1_slice_equal(
                            request->proof[prior],
                            request->proof[proof_index])) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate &&
                    !ppproof_runtime_v1_count_row(
                        impl, count, request->store, rows,
                        request->proof[proof_index],
                        error_buf, error_buf_size))
                    return false;
            }
        }
    }
    return true;
}

static Atom *ppproof_runtime_v1_label_list(
    PPProofRuntimeRowsV1 *rows,
    Atom *owner,
    const PPRelationalValueV1Slice *labels,
    uint32_t label_len,
    const char *wrapper,
    const char *cons,
    const char *nil) {
    Atom *list = atom_symbol(&rows->arena, nil);
    uint32_t index;

    if (!list || (label_len != 0u && !labels))
        return NULL;
    for (index = label_len; index > 0u; index--) {
        Atom *label;
        Atom *wrapped;
        if (!labels[index - 1u].bytes || labels[index - 1u].len == 0u ||
            !(label = ppproof_runtime_v1_scalar(
                  &rows->arena, labels[index - 1u].bytes,
                  labels[index - 1u].len)) ||
            !(wrapped = ppproof_runtime_v1_binary(
                  &rows->arena, wrapper, owner, label)) ||
            !(list = ppproof_runtime_v1_binary(
                  &rows->arena, cons, wrapped, list)))
            return NULL;
    }
    return list;
}

static Atom *ppproof_runtime_v1_code_list(
    PPProofRuntimeRowsV1 *rows,
    const PPRelationalValueV1Slice *codes,
    uint32_t code_len,
    const char *cons,
    const char *nil) {
    Atom *list = atom_symbol(&rows->arena, nil);
    uint32_t slice_index;

    if (!list || !codes || code_len == 0u)
        return NULL;
    for (slice_index = code_len; slice_index > 0u; slice_index--) {
        const PPRelationalValueV1Slice code = codes[slice_index - 1u];
        uint32_t byte_index;
        if (!code.bytes || code.len == 0u)
            return NULL;
        for (byte_index = code.len; byte_index > 0u; byte_index--) {
            Atom *byte = atom_int(
                &rows->arena, (int64_t)code.bytes[byte_index - 1u]);
            if (!byte ||
                !(list = ppproof_runtime_v1_binary(
                      &rows->arena, cons, byte, list)))
                return NULL;
        }
    }
    return list;
}

static bool ppproof_runtime_v1_input_byte_rows(
    const PPProofRuntimeImplV1 *impl,
    PPProofRuntimeRowsV1 *rows,
    Atom *owner) {
    uint32_t index;

    for (index = 0u; index < impl->input_byte_len; index++) {
        const PPProofRuntimeInputByteV1 *input_byte =
            &impl->input_bytes[index];
        Atom *value;
        Atom *fact;

        if (input_byte->input == PPPROOF_RUNTIME_INPUT_V1_NORMAL) {
            value = ppproof_runtime_v1_scalar(
                &rows->arena, &input_byte->byte, 1u);
            if (value)
                value = ppproof_runtime_v1_binary(
                    &rows->arena, impl->query_label_wrapper, owner, value);
        } else {
            value = atom_int(&rows->arena, (int64_t)input_byte->byte);
        }
        fact = value
            ? ppproof_runtime_v1_app(
                  &rows->arena, input_byte->relation, &value, 1u)
            : NULL;
        if (!fact || !ppproof_runtime_v1_rows_append(rows, fact))
            return false;
    }
    return true;
}

static Atom *ppproof_runtime_v1_initial_label_list(
    const PPProofRuntimeImplV1 *impl,
    const PPRelationalStateProofV1Request *request,
    PPProofRuntimeRowsV1 *rows,
    Atom *owner,
    char *error_buf,
    size_t error_buf_size) {
    const PPRelationalStoreV1 *store = request->store;
    const PPRelationalStateTableV1 *declared =
        &impl->state_plan->tables[impl->compressed_initial_table_id];
    uint32_t arity = 0u;
    uint32_t key_arity = 0u;
    uint32_t row_len = 0u;
    uint32_t initial_len = 0u;
    uint32_t key_value = 0u;
    uint32_t *values = NULL;
    Atom **ordered = NULL;
    Atom *list = NULL;
    uint32_t column_mask;
    uint64_t cursor = UINT64_MAX;
    bool found = false;
    uint32_t seen = 0u;
    uint32_t index;

    if (!ppproof_runtime_v1_prefix_count(
            store, impl->compressed_initial_table_id,
            request->label, &initial_len) ||
        !store->value_intern(
            store->context, request->label.bytes,
            request->label.len, &key_value) ||
        !store->table_shape(
            store->context, impl->compressed_initial_table_id,
            &arity, &key_arity, &row_len) ||
        arity != declared->arity || key_arity != declared->key_arity ||
        initial_len > row_len ||
        impl->compressed_initial_key_column != 0u || arity >= 32u)
        goto malformed;
    values = calloc(arity ? arity : 1u, sizeof(*values));
    ordered = arena_alloc(
        &rows->arena,
        (size_t)(initial_len ? initial_len : 1u) * sizeof(*ordered));
    if (!values || !ordered)
        goto resource;
    memset(ordered, 0, (size_t)(initial_len ? initial_len : 1u) *
                           sizeof(*ordered));
    column_mask =
        (UINT32_C(1) << impl->compressed_initial_ordinal_column) |
        (UINT32_C(1) << impl->compressed_initial_value_column);
    do {
        uint32_t ordinal;
        const uint8_t *label_bytes = NULL;
        uint32_t label_len = 0u;
        Atom *label;
        if (!store->table_prefix_next(
                store->context, impl->compressed_initial_table_id,
                &key_value, 1u, column_mask, &cursor,
                values, arity, &found))
            goto malformed;
        if (!found)
            break;
        if (!pprelational_store_v1_value_u32_decimal(
                store,
                values[impl->compressed_initial_ordinal_column],
                &ordinal) ||
            ordinal >= initial_len || ordered[ordinal] ||
            !store->value_bytes(
                store->context,
                values[impl->compressed_initial_value_column],
                &label_bytes, &label_len) ||
            !label_bytes || label_len == 0u ||
            !(label = ppproof_runtime_v1_scalar(
                  &rows->arena, label_bytes, label_len)) ||
            !(ordered[ordinal] = ppproof_runtime_v1_binary(
                  &rows->arena, impl->compressed_label_wrapper,
                  owner, label)))
            goto malformed;
        seen++;
    } while (found);
    if (seen != initial_len)
        goto malformed;
    list = atom_symbol(&rows->arena, impl->compressed_label_nil);
    for (index = initial_len; list && index > 0u; index--) {
        if (!ordered[index - 1u])
            goto malformed;
        list = ppproof_runtime_v1_binary(
            &rows->arena, impl->compressed_label_cons,
            ordered[index - 1u], list);
    }
    free(values);
    return list;

resource:
    (void)ppproof_runtime_v1_fail(
        error_buf, error_buf_size,
        "generated initial-label provider exhausted resources");
    free(values);
    return NULL;
malformed:
    (void)ppproof_runtime_v1_fail(
        error_buf, error_buf_size,
        "generated initial-label provider rejected its ordered state rows");
    free(values);
    return NULL;
}

static uint32_t ppproof_runtime_v1_outcome_query_count(
    const PPProofRuntimeImplV1 *impl, PPProofRuntimeInputV1 input) {
    uint32_t count = 0u;
    uint32_t index;

    for (index = 0u; index < impl->outcome_query_len; index++) {
        if (impl->outcome_queries[index].input == input)
            count++;
    }
    return count;
}

static const PPProofRuntimeOutcomeQueryV1 *
ppproof_runtime_v1_outcome_query(
    const PPProofRuntimeImplV1 *impl,
    PPProofRuntimeInputV1 input,
    uint32_t priority) {
    uint32_t index;

    for (index = 0u; index < impl->outcome_query_len; index++) {
        const PPProofRuntimeOutcomeQueryV1 *candidate =
            &impl->outcome_queries[index];
        if (candidate->input == input && candidate->priority == priority)
            return candidate;
    }
    return NULL;
}

static bool ppproof_runtime_v1_compiled_audit(
    PPProofRuntimeImplV1 *impl,
    const CettaGsltProviderRegistryV1 *providers,
    Atom *query,
    PPOSLFNativeVMOutcomeV1 primary_outcome,
    char *error_buf,
    size_t error_buf_size) {
    Arena output;
    CettaGsltHornResult result = {0};
    CettaGsltHornOutcome compiled_outcome = CETTA_GSLT_HORN_FAULT;
    size_t compiled_answer_count = 0u;
    uint64_t compiled_rule_attempts = 0u;
    bool agreed = false;
    bool executed;

    if (!impl || !impl->compiled_audit_language ||
        !impl->compiled_audit_descriptor ||
        !impl->compiled_audit_catalog || !providers || !query)
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "compiled proof audit is incompletely attached");
    arena_init(&output);
    Atom **elements = arena_alloc(&output, sizeof(*elements) * 2u);
    elements[0] = atom_symbol(
        &output, impl->compiled_audit_descriptor->query_relation);
    elements[1] = query;
    Atom *service_query = elements[0]
        ? atom_expr(&output, elements, 2u) : NULL;
    impl->receipt.compiled_audit_attempts++;
    executed = service_query &&
        cetta_gslt_language_query_with_providers_v1(
            impl->compiled_audit_language,
            CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            impl->compiled_audit_catalog, providers,
            &output, service_query, impl->compiled_audit_limits,
            &result, error_buf, error_buf_size);
    if (executed && primary_outcome == PPOSLF_NATIVE_VM_PROVED_V1)
        agreed = result.outcome == CETTA_GSLT_HORN_COMPLETED &&
            result.answer_count > 0u;
    else if (executed && primary_outcome == PPOSLF_NATIVE_VM_NO_PROOF_V1)
        agreed = result.outcome == CETTA_GSLT_HORN_COMPLETED &&
            result.answer_count == 0u;
    else if (executed &&
             primary_outcome == PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1)
        agreed = result.outcome == CETTA_GSLT_HORN_RULE_LIMIT ||
            result.outcome == CETTA_GSLT_HORN_DEPTH_LIMIT ||
            result.outcome == CETTA_GSLT_HORN_ANSWER_LIMIT;
    if (agreed)
        impl->receipt.compiled_audit_agreements++;
    impl->receipt.compiled_rule_attempts = ppproof_runtime_v1_add_u64_sat(
        impl->receipt.compiled_rule_attempts, result.rule_attempts);
    impl->receipt.compiled_rule_matches = ppproof_runtime_v1_add_u64_sat(
        impl->receipt.compiled_rule_matches, result.rule_matches);
    impl->receipt.compiled_dispatch_rejects =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_dispatch_rejects,
            result.rule_dispatch_rejects);
    impl->receipt.compiled_outer_head_elisions =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_outer_head_elisions,
            result.rule_outer_head_elisions);
    impl->receipt.compiled_prefilter_rejects = ppproof_runtime_v1_add_u64_sat(
        impl->receipt.compiled_prefilter_rejects,
        result.rule_prefilter_rejects);
    impl->receipt.compiled_ground_dense_attempts =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_ground_dense_attempts,
            result.rule_ground_dense_attempts);
    impl->receipt.compiled_flat_head_attempts =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_flat_head_attempts,
            result.rule_flat_head_attempts);
    impl->receipt.compiled_general_head_attempts =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_general_head_attempts,
            result.rule_general_head_attempts);
    impl->receipt.compiled_constructor_guided_attempts =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_constructor_guided_attempts,
            result.rule_constructor_guided_attempts);
    impl->receipt.compiled_constructor_guided_matches =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_constructor_guided_matches,
            result.rule_constructor_guided_matches);
    impl->receipt.compiled_constructor_nodes_elided =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_constructor_nodes_elided,
            result.rule_constructor_nodes_elided);
    impl->receipt.compiled_flat_head_matches = ppproof_runtime_v1_add_u64_sat(
        impl->receipt.compiled_flat_head_matches,
        result.rule_flat_head_matches);
    impl->receipt.compiled_ground_dense_matches =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_ground_dense_matches,
            result.rule_ground_dense_matches);
    impl->receipt.compiled_variable_slot_buffer_uses =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_variable_slot_buffer_uses,
            result.rule_variable_slot_buffer_uses);
    impl->receipt.compiled_variable_slot_bytes_elided =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_variable_slot_bytes_elided,
            result.rule_variable_slot_bytes_elided);
    impl->receipt.compiled_variable_slot_clear_bytes_elided =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_variable_slot_clear_bytes_elided,
            result.rule_variable_slot_clear_bytes_elided);
    impl->receipt.compiled_ground_subterm_cache_hits =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_ground_subterm_cache_hits,
            result.rule_ground_subterm_cache_hits);
    impl->receipt.compiled_ground_subterm_nodes_elided =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_ground_subterm_nodes_elided,
            result.rule_ground_subterm_nodes_elided);
    impl->receipt.compiled_worklist_states_created =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_worklist_states_created,
            result.worklist_states_created);
    impl->receipt.compiled_worklist_states_reclaimed =
        ppproof_runtime_v1_add_u64_sat(
            impl->receipt.compiled_worklist_states_reclaimed,
            result.worklist_states_reclaimed);
    if (impl->receipt.compiled_worklist_pending_peak <
        result.worklist_pending_peak)
        impl->receipt.compiled_worklist_pending_peak =
            result.worklist_pending_peak;
    if (impl->receipt.compiled_worklist_state_bytes_peak <
        result.worklist_state_bytes_peak)
        impl->receipt.compiled_worklist_state_bytes_peak =
            result.worklist_state_bytes_peak;
    if (impl->receipt.compiled_maximum_goal_depth <
        result.max_depth_observed)
        impl->receipt.compiled_maximum_goal_depth =
            result.max_depth_observed;
    compiled_outcome = result.outcome;
    compiled_answer_count = result.answer_count;
    compiled_rule_attempts = result.rule_attempts;
    cetta_gslt_horn_result_free(&result);
    arena_free(&output);
    if (!executed)
        return false;
    if (!agreed) {
        const char *query_name =
            query->kind == ATOM_EXPR && query->expr.len > 0u &&
            query->expr.elems[0] &&
            query->expr.elems[0]->kind == ATOM_SYMBOL
                ? atom_name_cstr(query->expr.elems[0]) : "<malformed>";
        return ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated proof realizations disagree on %s: primary=%u "
            "compiled=%u answers=%zu attempts=%llu",
            query_name, (unsigned)primary_outcome,
            (unsigned)compiled_outcome, compiled_answer_count,
            (unsigned long long)compiled_rule_attempts);
    }
    return true;
}

static PPRelationalStateProofV1Result ppproof_runtime_v1_execute(
    void *context,
    const PPRelationalStateProofV1Request *request,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRuntimeImplV1 *impl = context;
    PPProofRuntimeRowsV1 rows;
    PPOSLFNativeCapabilitySetV1 capabilities;
    CettaGsltFiniteFactProviderSetV1 *compiled_audit_providers = NULL;
    PPOSLFNativeVMResultV1 result;
    PPRelationalStateProofV1Result outcome =
        PPRELATIONAL_STATE_PROOF_V1_INVALID;
    uint8_t *claim_bytes = NULL;
    uint32_t claim_len = 0u;
    uint32_t claim_value = 0u;
    Atom *claim_key;
    Atom *label;
    Atom *owner;
    Atom *request_term;
    Atom *label_list;
    Atom *initial_label_list = NULL;
    Atom *code_list = NULL;
    Atom *query = NULL;
    Atom *request_arguments[4];
    Atom *query_arguments[4];
    PPProofRuntimeInputV1 input;
    const PPProofRuntimeOutcomeQueryV1 *candidate = NULL;
    uint32_t candidate_len;
    uint32_t priority;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || !request || !request->store ||
        !pprelational_store_v1_valid(request->store) ||
        request->state_plan != impl->state_plan ||
        !request->label.bytes ||
        request->label.len == 0u || !request->claim ||
        request->claim_len == 0u ||
        (request->proof_len != 0u && !request->proof) ||
        (!request->compressed && request->proof_len == 0u) ||
        (request->compressed &&
         (!request->code || request->code_len == 0u))) {
        (void)ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "invalid generated relational proof request");
        return PPRELATIONAL_STATE_PROOF_V1_INVALID;
    }
    if (request->compressed && !impl->compressed_query_ready)
        return PPRELATIONAL_STATE_PROOF_V1_UNSUPPORTED;
    ppproof_runtime_v1_rows_init(
        &rows, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    pposlf_native_capability_set_v1_init(&capabilities);
    pposlf_native_vm_result_v1_init(&result);
    /*
     * A prior unobserved receipt does not escape.  Release its overlay before
     * refreshing the persistent prefix it borrows; an observed receipt has
     * already forced and copied its canonical digest.
     */
    pposlf_native_capability_set_v1_free(&impl->pending_capabilities);
    pposlf_native_capability_set_v1_init(&impl->pending_capabilities);
    impl->pending_capabilities_ready = false;
    memset(&impl->receipt, 0, sizeof(impl->receipt));
    memcpy(impl->receipt.provider_digest, impl->answers.digest, 65u);
    impl->receipt_ready = false;

    if (!ppproof_runtime_v1_persistent_cache_refresh(
            impl, request->store, error_buf, error_buf_size) ||
        !ppproof_runtime_v1_list_seed(
            &rows, &impl->persistent_cache->rows) ||
        !ppproof_runtime_v1_state_rows(
            impl, request->store, &rows,
            error_buf, error_buf_size) ||
        !ppproof_runtime_v1_distinct_rows(
            impl, request->store, &rows,
            error_buf, error_buf_size) ||
        !pprelational_value_list_v1_encode_items(
            request->claim, request->claim_len,
            &claim_bytes, &claim_len) ||
        !request->store->value_intern(
            request->store->context, claim_bytes, claim_len,
            &claim_value) ||
        !(claim_key = ppproof_runtime_v1_value(
              impl, request->store, &rows, claim_value)) ||
        !(label = ppproof_runtime_v1_scalar(
              &rows.arena, request->label.bytes, request->label.len)) ||
        !(owner = atom_symbol(&rows.arena, impl->owner)))
        goto done;

    request_arguments[0] = owner;
    request_arguments[1] = label;
    request_arguments[2] = label;
    request_arguments[3] = claim_key;
    if (!ppproof_runtime_v1_rows_append(
            &rows, ppproof_runtime_v1_app(
                &rows.arena, impl->request_relation,
                request_arguments, 4u)) ||
        !ppproof_runtime_v1_count_rows(
            impl, request, &rows, error_buf, error_buf_size) ||
        !ppproof_runtime_v1_input_byte_rows(impl, &rows, owner))
        goto done;

    if (request->compressed) {
        input = PPPROOF_RUNTIME_INPUT_V1_COMPRESSED;
        request_term = ppproof_runtime_v1_binary(
            &rows.arena, impl->compressed_request_wrapper, owner, label);
        initial_label_list = ppproof_runtime_v1_initial_label_list(
            impl, request, &rows, owner, error_buf, error_buf_size);
        label_list = ppproof_runtime_v1_label_list(
            &rows, owner, request->proof, request->proof_len,
            impl->compressed_label_wrapper,
            impl->compressed_label_cons, impl->compressed_label_nil);
        code_list = ppproof_runtime_v1_code_list(
            &rows, request->code, request->code_len,
            impl->compressed_code_cons, impl->compressed_code_nil);
        if (!request_term || !initial_label_list || !label_list || !code_list)
            goto done;
        query_arguments[0] = request_term;
        query_arguments[1] = initial_label_list;
        query_arguments[2] = label_list;
        query_arguments[3] = code_list;
    } else {
        input = PPPROOF_RUNTIME_INPUT_V1_NORMAL;
        request_term = ppproof_runtime_v1_binary(
            &rows.arena, impl->query_request_wrapper, owner, label);
        label_list = ppproof_runtime_v1_label_list(
            &rows, owner, request->proof, request->proof_len,
            impl->query_label_wrapper,
            impl->query_list_cons, impl->query_list_nil);
        if (!request_term || !label_list)
            goto done;
        query_arguments[0] = request_term;
        query_arguments[1] = label_list;
    }
    if (!request_term ||
        !pposlf_native_capability_set_v1_prepare_borrowed_overlay_deferred(
            &capabilities, impl->native_plan,
            &impl->persistent_cache->capabilities,
            rows.rows, rows.row_len,
            error_buf, error_buf_size))
        goto done;
    if (impl->compiled_audit_language) {
        CettaGsltFiniteFactSpanV1 spans[2] = {
            {
                .rows = impl->persistent_cache->rows.rows,
                .row_count = impl->persistent_cache->rows.row_len,
            },
            {
                .rows = rows.rows,
                .row_count = rows.row_len,
            },
        };
        compiled_audit_providers =
            cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
                impl->compiled_audit_catalog->requirements,
                impl->compiled_audit_catalog->requirement_count,
                spans, 2u, error_buf, error_buf_size);
        if (!compiled_audit_providers)
            goto done;
    }
    candidate_len = ppproof_runtime_v1_outcome_query_count(impl, input);
    if (candidate_len == 0u)
        goto done;
    for (priority = 0u; priority < candidate_len; priority++) {
        candidate = ppproof_runtime_v1_outcome_query(
            impl, input, priority);
        if (!candidate)
            goto done;
        query = ppproof_runtime_v1_app(
            &rows.arena, candidate->query, query_arguments,
            input == PPPROOF_RUNTIME_INPUT_V1_NORMAL ? 2u : 4u);
        if (!query ||
            !pposlf_native_type_vm_v1_prove_with_uncommitted_capabilities(
                impl->vm, &capabilities, query, impl->limits, &result))
            goto done;
        impl->receipt.outcome_query_attempts++;
        if (compiled_audit_providers &&
            !ppproof_runtime_v1_compiled_audit(
                impl,
                cetta_gslt_finite_fact_provider_set_registry_v1(
                    compiled_audit_providers),
                query, result.outcome, error_buf, error_buf_size))
            goto done;
        if (result.outcome != PPOSLF_NATIVE_VM_NO_PROOF_V1)
            break;
    }
    if (!candidate || priority >= candidate_len)
        candidate = ppproof_runtime_v1_outcome_query(
            impl, input, candidate_len - 1u);
    impl->receipt.outcome = result.outcome;
    impl->receipt.proof_result = PPRELATIONAL_STATE_PROOF_V1_REJECTED;
    impl->receipt.outcome_query_priority = candidate
        ? candidate->priority : UINT32_MAX;
    impl->receipt.stats = result.stats;
    if (impl->persistent_cache->rows.row_len > UINT32_MAX - rows.row_len ||
        impl->persistent_cache->rows.constructor_chain_requests >
            UINT64_MAX - rows.constructor_chain_requests ||
        impl->persistent_cache->rows.constructor_chain_nodes >
            UINT64_MAX - rows.constructor_chain_nodes)
        goto done;
    impl->receipt.capability_row_len =
        impl->persistent_cache->rows.row_len + rows.row_len;
    impl->receipt.constructor_chain_requests =
        impl->persistent_cache->rows.constructor_chain_requests +
        rows.constructor_chain_requests;
    impl->receipt.constructor_chain_nodes =
        impl->persistent_cache->rows.constructor_chain_nodes +
        rows.constructor_chain_nodes;
    if (result.capability_digest_ready ||
        result.capability_digest[0] != '\0')
        goto done;
    memcpy(impl->receipt.program_digest, result.program_digest, 65u);
    for (index = 0u; index < result.proof_event_len; index++) {
        if (result.proof_events[index].kind ==
            PPOSLF_NATIVE_VM_PROOF_GENERATED_STEP_V1)
            impl->receipt.generated_event_len++;
        else if (result.proof_events[index].kind ==
                 PPOSLF_NATIVE_VM_PROOF_EXTERNAL_ROW_V1)
            impl->receipt.external_event_len++;
    }
    impl->receipt_ready = true;
    if (result.outcome == PPOSLF_NATIVE_VM_PROVED_V1 && candidate) {
        outcome = candidate->result;
        impl->receipt.proof_result = outcome;
    }
    else if (result.outcome == PPOSLF_NATIVE_VM_NO_PROOF_V1) {
        (void)ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated proof VM found no derivation after %llu rule attempts "
            "at depth %u (indexed=%llu full=%llu external=%llu matched=%llu "
            "exact=%llu/%llu prefix=%llu/%llu/%llu)",
            (unsigned long long)result.stats.rule_attempts,
            result.stats.maximum_goal_depth,
            (unsigned long long)result.stats.indexed_candidate_visits,
            (unsigned long long)result.stats.full_scan_candidate_visits,
            (unsigned long long)result.stats.external_row_candidate_visits,
            (unsigned long long)result.stats.external_row_matches,
            (unsigned long long)result.stats.external_exact_key_hits,
            (unsigned long long)result.stats.external_exact_key_lookups,
            (unsigned long long)result.stats.external_prefix_key_hits,
            (unsigned long long)result.stats.external_prefix_key_lookups,
            (unsigned long long)
                result.stats.external_prefix_key_candidates);
        outcome = PPRELATIONAL_STATE_PROOF_V1_REJECTED;
    } else if (result.outcome == PPOSLF_NATIVE_VM_RESOURCE_EXHAUSTED_V1) {
        (void)ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated proof VM exhausted resources after %llu rule attempts "
            "at depth %u (indexed=%llu full=%llu external=%llu matched=%llu "
            "exact=%llu/%llu prefix=%llu/%llu/%llu)",
            (unsigned long long)result.stats.rule_attempts,
            result.stats.maximum_goal_depth,
            (unsigned long long)result.stats.indexed_candidate_visits,
            (unsigned long long)result.stats.full_scan_candidate_visits,
            (unsigned long long)result.stats.external_row_candidate_visits,
            (unsigned long long)result.stats.external_row_matches,
            (unsigned long long)result.stats.external_exact_key_hits,
            (unsigned long long)result.stats.external_exact_key_lookups,
            (unsigned long long)result.stats.external_prefix_key_hits,
            (unsigned long long)result.stats.external_prefix_key_lookups,
            (unsigned long long)
                result.stats.external_prefix_key_candidates);
        outcome = PPRELATIONAL_STATE_PROOF_V1_RESOURCE;
    } else
        outcome = PPRELATIONAL_STATE_PROOF_V1_INVALID;
    impl->receipt.proof_result = outcome;
    impl->pending_capabilities = capabilities;
    capabilities.impl = NULL;
    impl->pending_capabilities_ready = true;
    ok = true;

done:
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0')
        (void)ppproof_runtime_v1_fail(
            error_buf, error_buf_size,
            "generated relational proof execution failed closed");
    free(claim_bytes);
    cetta_gslt_finite_fact_provider_set_free_v1(
        compiled_audit_providers);
    pposlf_native_vm_result_v1_free(&result);
    pposlf_native_capability_set_v1_free(&capabilities);
    ppproof_runtime_v1_rows_free(&rows);
    return outcome;
}

PPRelationalStateProofV1Backend ppproof_gslt_relational_runtime_v1_backend(
    PPProofGSLTRelationalRuntimeV1 *runtime) {
    return (PPRelationalStateProofV1Backend){
        .context = runtime ? runtime->implementation : NULL,
        .execute = runtime && runtime->implementation
            ? ppproof_runtime_v1_execute
            : NULL,
    };
}

bool ppproof_gslt_relational_runtime_v1_last_receipt(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    PPProofGSLTRelationalRuntimeV1Receipt *receipt_out) {
    PPProofRuntimeImplV1 *impl;
    if (!runtime || !receipt_out ||
        !(impl = runtime->implementation) || !impl->receipt_ready ||
        !impl->pending_capabilities_ready ||
        !pposlf_native_capability_set_v1_commit_digest(
            &impl->pending_capabilities,
            impl->receipt.capability_digest))
        return false;
    *receipt_out = impl->receipt;
    return true;
}
