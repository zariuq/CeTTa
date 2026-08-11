#include "gslt_abt_provider_v1.h"

#include "gslt_horn_runtime.h"
#include "symbol.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaGsltAbtProviderV1 {
    CettaGsltAbtProviderSchemaV1 schema;
    Arena storage_arena;
    AbtSignature signature;
    CettaGsltProviderV1 physical[2];
    CettaGsltProviderRegistryV1 registry;
    bool initialized;
};

static void abt_provider_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (!error || error_size == 0u)
        return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static bool abt_provider_text_v1(const char *text) {
    return text && text[0] != '\0';
}

static bool abt_provider_schema_valid_v1(
    const CettaGsltAbtProviderSchemaV1 *schema,
    char *error,
    size_t error_size) {
    if (!schema ||
        !abt_provider_text_v1(schema->field_depth_relation) ||
        !abt_provider_text_v1(schema->field_depth_semantic_id) ||
        !abt_provider_text_v1(schema->transport_relation) ||
        !abt_provider_text_v1(schema->transport_semantic_id)) {
        abt_provider_error_v1(
            error, error_size, "ABT provider schema has an empty name");
        return false;
    }
    if (strcmp(schema->field_depth_relation,
               schema->transport_relation) == 0 ||
        strcmp(schema->field_depth_semantic_id,
               schema->transport_semantic_id) == 0) {
        abt_provider_error_v1(
            error, error_size,
            "ABT provider relations and semantic identities must be distinct");
        return false;
    }
    return true;
}

static bool abt_provider_symbol_v1(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool abt_provider_expr_v1(
    const Atom *atom, const char *head, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        abt_provider_symbol_v1(atom->expr.elems[0], head);
}

static bool abt_provider_qnat_v1(const Atom *atom, uint32_t *value) {
    uint32_t result = 0u;
    const Atom *cursor = atom;
    while (!abt_provider_symbol_v1(cursor, "q-zero")) {
        if (!abt_provider_expr_v1(cursor, "q-succ", 2u) ||
            result == UINT32_MAX)
            return false;
        result++;
        cursor = cursor->expr.elems[1];
    }
    *value = result;
    return true;
}

static Atom *abt_provider_qnat_atom_v1(Arena *arena, uint32_t value) {
    Atom *result = atom_symbol(arena, "q-zero");
    if (!result)
        return NULL;
    for (uint32_t index = 0u; index < value; index++) {
        result = atom_expr2(
            arena, atom_symbol(arena, "q-succ"), result);
        if (!result)
            return NULL;
    }
    return result;
}

static bool abt_provider_qlist_length_v1(
    const Atom *list, uint32_t *length) {
    uint32_t result = 0u;
    const Atom *cursor = list;
    while (!abt_provider_symbol_v1(cursor, "q-nil")) {
        if (!abt_provider_expr_v1(cursor, "q-cons", 3u) ||
            result == UINT32_MAX)
            return false;
        result++;
        cursor = cursor->expr.elems[2];
    }
    *length = result;
    return true;
}

static const char *abt_provider_qsymbol_v1(const Atom *quoted) {
    if (!abt_provider_expr_v1(quoted, "q-sym", 2u))
        return NULL;
    const Atom *payload = quoted->expr.elems[1];
    if (abt_provider_expr_v1(payload, "q-str", 2u))
        payload = payload->expr.elems[1];
    if (payload->kind == ATOM_SYMBOL)
        return atom_name_cstr((Atom *)payload);
    if (payload->kind == ATOM_GROUNDED &&
        payload->ground.gkind == GV_STRING)
        return payload->ground.sval;
    return NULL;
}

static Atom *abt_provider_answer_v1(
    Arena *arena, const char *relation,
    const Atom *goal, Atom *last) {
    if (!arena || !relation || !goal || goal->kind != ATOM_EXPR ||
        goal->expr.len < 2u || !last)
        return NULL;
    Atom **elements = arena_alloc(
        arena, sizeof(*elements) * (size_t)goal->expr.len);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, relation);
    if (!elements[0])
        return NULL;
    for (CettaExprIndex index = 1u; index + 1u < goal->expr.len; index++) {
        elements[index] = atom_deep_copy(arena, goal->expr.elems[index]);
        if (!elements[index])
            return NULL;
    }
    elements[goal->expr.len - 1u] = last;
    return atom_expr(arena, elements, goal->expr.len);
}

static CettaGsltProviderOutcomeV1 abt_provider_empty_or_fault_v1(
    bool fault, char *error, size_t error_size, const char *message) {
    if (fault) {
        abt_provider_error_v1(error, error_size, "%s", message);
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

static CettaGsltProviderOutcomeV1 abt_provider_field_depth_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltAbtProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !answers || !goal ||
        goal->kind != ATOM_EXPR || goal->expr.len != 5u ||
        !abt_provider_symbol_v1(
            goal->expr.elems[0], provider->schema.field_depth_relation))
        return abt_provider_empty_or_fault_v1(
            true, error, error_size, "invalid ABT field-depth request");
    if (answer_limit < 1u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;

    const char *head = abt_provider_qsymbol_v1(goal->expr.elems[1]);
    uint32_t arity = 0u;
    uint32_t position = 0u;
    if (!abt_provider_qlist_length_v1(goal->expr.elems[2], &arity) ||
        !abt_provider_qnat_v1(goal->expr.elems[3], &position) ||
        position >= arity)
        return CETTA_GSLT_PROVIDER_COMPLETED;

    const AbtSignatureEntry *entry = NULL;
    if (head) {
        Atom *head_atom = atom_symbol(&provider->storage_arena, head);
        if (!head_atom)
            return abt_provider_empty_or_fault_v1(
                true, error, error_size, "cannot intern ABT constructor head");
        entry = abt_signature_lookup(
            &provider->signature, head_atom->sym_id, arity);
    }
    uint32_t depth = entry ? entry->depths[position] : 0u;
    Atom *quoted_depth = abt_provider_qnat_atom_v1(answer_arena, depth);
    Atom *answer = abt_provider_answer_v1(
        answer_arena, provider->schema.field_depth_relation,
        goal, quoted_depth);
    if (!answer || !cetta_gslt_provider_answers_push_v1(answers, answer))
        return abt_provider_empty_or_fault_v1(
            true, error, error_size,
            "cannot allocate ABT field-depth answer");
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

static CettaGsltProviderOutcomeV1 abt_provider_transport_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltAbtProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !answers || !goal ||
        goal->kind != ATOM_EXPR || goal->expr.len != 5u ||
        !abt_provider_symbol_v1(
            goal->expr.elems[0], provider->schema.transport_relation))
        return abt_provider_empty_or_fault_v1(
            true, error, error_size, "invalid ABT transport request");
    if (answer_limit < 1u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;

    uint32_t support_depth = 0u;
    uint32_t use_depth = 0u;
    if (!abt_provider_qnat_v1(goal->expr.elems[1], &support_depth) ||
        !abt_provider_qnat_v1(goal->expr.elems[2], &use_depth))
        return CETTA_GSLT_PROVIDER_COMPLETED;

    Arena scratch;
    arena_init(&scratch);
    Atom *value = cetta_gslt_unquote_atom_v1(
        &scratch, goal->expr.elems[3]);
    Atom *canonical = value
        ? cetta_gslt_quote_atom_v1(&scratch, value) : NULL;
    if (!value || !canonical ||
        !atom_eq(canonical, (Atom *)goal->expr.elems[3])) {
        arena_free(&scratch);
        return CETTA_GSLT_PROVIDER_COMPLETED;
    }

    int64_t delta = use_depth >= support_depth
        ? (int64_t)((uint64_t)use_depth - (uint64_t)support_depth)
        : -(int64_t)((uint64_t)support_depth - (uint64_t)use_depth);
    Atom *transported = abt_shift(
        &provider->signature, &scratch, delta, 0u, value);
    if (!transported) {
        arena_free(&scratch);
        return CETTA_GSLT_PROVIDER_COMPLETED;
    }
    Atom *quoted = transported
        ? cetta_gslt_quote_atom_v1(answer_arena, transported) : NULL;
    arena_free(&scratch);
    if (!quoted)
        return abt_provider_empty_or_fault_v1(
            true, error, error_size, "cannot quote ABT transport result");

    Atom *answer = abt_provider_answer_v1(
        answer_arena, provider->schema.transport_relation, goal, quoted);
    if (!answer || !cetta_gslt_provider_answers_push_v1(answers, answer))
        return abt_provider_empty_or_fault_v1(
            true, error, error_size, "cannot allocate ABT transport answer");
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

CettaGsltAbtProviderV1 *cetta_gslt_abt_provider_create_v1(
    const CettaGsltAbtProviderSchemaV1 *schema,
    Atom *extension_signatures,
    char *error,
    size_t error_size) {
    if (!abt_provider_schema_valid_v1(schema, error, error_size))
        return NULL;
    CettaGsltAbtProviderV1 *provider = calloc(1u, sizeof(*provider));
    if (!provider) {
        abt_provider_error_v1(
            error, error_size, "cannot allocate ABT provider");
        return NULL;
    }
    provider->schema = *schema;
    arena_init(&provider->storage_arena);
    abt_signature_init(&provider->signature);
    if (!abt_signature_add_defaults(
            &provider->signature, &provider->storage_arena) ||
        (extension_signatures && !abt_signature_add_set(
            &provider->signature, extension_signatures))) {
        abt_provider_error_v1(
            error, error_size, "cannot admit ABT provider signature");
        cetta_gslt_abt_provider_free_v1(provider);
        return NULL;
    }

    provider->physical[0] = (CettaGsltProviderV1){
        .relation = provider->schema.field_depth_relation,
        .arity = 4u,
        .semantic_id = provider->schema.field_depth_semantic_id,
        .context = provider,
        .query = abt_provider_field_depth_query_v1,
    };
    provider->physical[1] = (CettaGsltProviderV1){
        .relation = provider->schema.transport_relation,
        .arity = 4u,
        .semantic_id = provider->schema.transport_semantic_id,
        .context = provider,
        .query = abt_provider_transport_query_v1,
    };
    provider->registry.providers = provider->physical;
    provider->registry.provider_count = 2u;
    provider->initialized = true;
    if (!cetta_gslt_provider_registry_validate_v1(
            &provider->registry, error, error_size)) {
        cetta_gslt_abt_provider_free_v1(provider);
        return NULL;
    }
    return provider;
}

void cetta_gslt_abt_provider_free_v1(CettaGsltAbtProviderV1 *provider) {
    if (!provider)
        return;
    abt_signature_free(&provider->signature);
    arena_free(&provider->storage_arena);
    free(provider);
}

const CettaGsltProviderRegistryV1 *cetta_gslt_abt_provider_registry_v1(
    const CettaGsltAbtProviderV1 *provider) {
    return provider && provider->initialized ? &provider->registry : NULL;
}
