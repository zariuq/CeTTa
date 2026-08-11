#include "gslt_space_fact_provider_v1.h"

#include "match.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaGsltSpaceFactProviderV1 {
    Arena storage_arena;
    TermUniverse universe;
    Space space;
    CettaGsltProviderV1 physical;
    char *relation;
    char *semantic_id;
    bool initialized;
};

static void space_fact_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (!error || error_size == 0u)
        return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static char *space_fact_copy_string_v1(const char *source) {
    if (!source)
        return NULL;
    size_t length = strlen(source);
    char *copy = malloc(length + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, source, length + 1u);
    return copy;
}

static bool space_fact_has_shape_v1(
    const Atom *fact, const char *relation, uint32_t arity) {
    return fact && relation && fact->kind == ATOM_EXPR &&
        (uint64_t)fact->expr.len == (uint64_t)arity + 1u &&
        fact->expr.elems[0] && fact->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(fact->expr.elems[0]), relation) == 0;
}

static CettaGsltProviderOutcomeV1 space_fact_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltSpaceFactProviderV1 *provider = raw_context;
    if (!provider || !provider->initialized || !answer_arena || !answers ||
        !space_fact_has_shape_v1(
            goal, provider->physical.relation, provider->physical.arity)) {
        space_fact_error_v1(error, error_size,
                            "invalid Space fact-provider query");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    SubstMatchSet matches;
    space_match_backend_clear_error();
    space_subst_query(
        &provider->space, answer_arena, (Atom *)goal, &matches);
    SpaceMatchBackendError backend_error =
        space_match_backend_last_error_code();
    if (backend_error != SPACE_MATCH_BACKEND_ERROR_NONE) {
        smset_free(&matches);
        space_fact_error_v1(
            error, error_size, "Space fact-provider query failed: %s",
            space_match_backend_error_name(backend_error));
        space_match_backend_clear_error();
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    for (CettaIndex index = 0u; index < matches.len; index++) {
        Bindings bindings;
        if (!space_subst_match_with_seed(
                &provider->space, (Atom *)goal, &matches.items[index],
                NULL, answer_arena, &bindings)) {
            continue;
        }
        Atom *answer = bindings_apply(&bindings, answer_arena, (Atom *)goal);
        bindings_free(&bindings);
        if (!answer ||
            !cetta_gslt_provider_answers_push_v1(answers, answer)) {
            smset_free(&matches);
            cetta_gslt_provider_answers_free_v1(answers);
            space_fact_error_v1(
                error, error_size,
                "Space fact-provider could not allocate its completed frontier");
            return CETTA_GSLT_PROVIDER_FAULT;
        }
        if ((uint64_t)answers->answer_count > answer_limit) {
            smset_free(&matches);
            cetta_gslt_provider_answers_free_v1(answers);
            return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
        }
    }
    smset_free(&matches);
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

CettaGsltSpaceFactProviderV1 *cetta_gslt_space_fact_provider_create_v1(
    SpaceEngine engine,
    const char *relation,
    uint32_t arity,
    const char *semantic_id,
    char *error,
    size_t error_size) {
    if (!relation || relation[0] == '\0' ||
        !semantic_id || semantic_id[0] == '\0') {
        space_fact_error_v1(
            error, error_size,
            "Space fact-provider requires relation and semantic identity");
        return NULL;
    }
    if (engine != SPACE_ENGINE_NATIVE &&
        engine != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT &&
        engine != SPACE_ENGINE_PATHMAP) {
        space_fact_error_v1(
            error, error_size,
            "Space fact-provider does not admit execution engine %s",
            space_match_backend_kind_name(engine));
        return NULL;
    }
    const char *unavailable = space_match_backend_unavailable_reason(engine);
    if (unavailable) {
        space_fact_error_v1(error, error_size, "%s", unavailable);
        return NULL;
    }

    CettaGsltSpaceFactProviderV1 *provider =
        calloc(1u, sizeof(*provider));
    if (!provider) {
        space_fact_error_v1(
            error, error_size, "cannot allocate Space fact-provider");
        return NULL;
    }
    provider->relation = space_fact_copy_string_v1(relation);
    provider->semantic_id = space_fact_copy_string_v1(semantic_id);
    if (!provider->relation || !provider->semantic_id) {
        space_fact_error_v1(
            error, error_size, "cannot copy Space fact-provider identity");
        cetta_gslt_space_fact_provider_free_v1(provider);
        return NULL;
    }

    arena_init(&provider->storage_arena);
    term_universe_init(&provider->universe);
    term_universe_set_persistent_arena(
        &provider->universe, &provider->storage_arena);
    space_init_with_universe(&provider->space, &provider->universe);
    provider->initialized = true;
    if (!space_match_backend_try_set(&provider->space, engine)) {
        space_fact_error_v1(
            error, error_size, "cannot select Space fact-provider engine %s",
            space_match_backend_kind_name(engine));
        cetta_gslt_space_fact_provider_free_v1(provider);
        return NULL;
    }
    provider->physical = (CettaGsltProviderV1){
        .relation = provider->relation,
        .arity = arity,
        .semantic_id = provider->semantic_id,
        .context = provider,
        .query = space_fact_query_v1,
    };
    return provider;
}

void cetta_gslt_space_fact_provider_free_v1(
    CettaGsltSpaceFactProviderV1 *provider) {
    if (!provider)
        return;
    if (provider->initialized) {
        space_free(&provider->space);
        term_universe_free(&provider->universe);
        arena_free(&provider->storage_arena);
    }
    free(provider->semantic_id);
    free(provider->relation);
    free(provider);
}

bool cetta_gslt_space_fact_provider_admit_v1(
    CettaGsltSpaceFactProviderV1 *provider,
    const Arena *source_arena,
    Atom *fact,
    char *error,
    size_t error_size) {
    if (!provider || !provider->initialized ||
        !space_fact_has_shape_v1(
            fact, provider->physical.relation, provider->physical.arity)) {
        space_fact_error_v1(
            error, error_size,
            "fact does not match Space provider relation %s/%u",
            provider && provider->physical.relation
                ? provider->physical.relation : "<uninitialized>",
            provider ? provider->physical.arity : 0u);
        return false;
    }
    term_universe_clear_error(&provider->universe);
    if (!space_admit_atom_from_source_arena(
            &provider->space, &provider->storage_arena,
            source_arena, fact)) {
        TermUniverseError universe_error =
            term_universe_last_error_code(&provider->universe);
        space_fact_error_v1(
            error, error_size, "cannot admit Space provider fact: %s",
            term_universe_error_name(universe_error));
        return false;
    }
    return true;
}

const CettaGsltProviderV1 *cetta_gslt_space_fact_provider_physical_v1(
    const CettaGsltSpaceFactProviderV1 *provider) {
    return provider && provider->initialized ? &provider->physical : NULL;
}

SpaceEngine cetta_gslt_space_fact_provider_engine_v1(
    const CettaGsltSpaceFactProviderV1 *provider) {
    return provider && provider->initialized
        ? provider->space.match_backend.kind : SPACE_ENGINE_NATIVE;
}

bool cetta_gslt_space_fact_provider_primary_active_v1(
    const CettaGsltSpaceFactProviderV1 *provider) {
    if (!provider || !provider->initialized)
        return false;
    switch (provider->space.match_backend.kind) {
    case SPACE_ENGINE_NATIVE:
    case SPACE_ENGINE_NATIVE_CANDIDATE_EXACT:
        return true;
    case SPACE_ENGINE_PATHMAP:
        return provider->space.match_backend.pathmap.bridge.bridge_active &&
            provider->space.match_backend.pathmap.bridge.bridge_space &&
            !provider->space.match_backend.pathmap.bridge.bridge_unavailable;
    case SPACE_ENGINE_MORK:
        return false;
    }
    return false;
}
