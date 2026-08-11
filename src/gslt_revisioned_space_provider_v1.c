#include "gslt_revisioned_space_provider_v1.h"

#include "native_sha256.h"
#include "match.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { REVISIONED_SPACE_PATTERN_DEPTH_LIMIT_V1 = 4096 };

typedef struct {
    char digest[65];
    Space *space;
} RevisionedSpaceWorldV1;

struct CettaGsltRevisionedSpaceProviderV1 {
    SpaceEngine engine;
    CettaGsltRevisionedSpaceSchemaV1 schema;
    Arena storage_arena;
    TermUniverse universe;
    RevisionedSpaceWorldV1 *worlds;
    size_t world_count;
    size_t world_capacity;
    CettaGsltProviderV1 physical[4];
    CettaGsltProviderRegistryV1 registry;
    pthread_mutex_t mutex;
    bool mutex_initialized;
    bool initialized;
};

static void revisioned_space_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (!error || error_size == 0u)
        return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static bool revisioned_space_name_v1(const char *name) {
    return name && name[0] != '\0';
}

static bool revisioned_space_schema_valid_v1(
    const CettaGsltRevisionedSpaceSchemaV1 *schema,
    char *error,
    size_t error_size) {
    if (!schema ||
        !revisioned_space_name_v1(schema->open_relation) ||
        !revisioned_space_name_v1(schema->open_semantic_id) ||
        !revisioned_space_name_v1(schema->member_relation) ||
        !revisioned_space_name_v1(schema->member_semantic_id) ||
        !revisioned_space_name_v1(schema->candidate_relation) ||
        !revisioned_space_name_v1(schema->candidate_semantic_id) ||
        !revisioned_space_name_v1(schema->emit_relation) ||
        !revisioned_space_name_v1(schema->emit_semantic_id) ||
        !revisioned_space_name_v1(schema->program_nil_constructor) ||
        !revisioned_space_name_v1(schema->program_cons_constructor) ||
        !revisioned_space_name_v1(schema->world_token_constructor) ||
        !revisioned_space_name_v1(schema->stored_occurrence_constructor) ||
        !revisioned_space_name_v1(schema->emitted_occurrence_constructor) ||
        !revisioned_space_name_v1(schema->open_receipt_constructor) ||
        !revisioned_space_name_v1(schema->emit_receipt_constructor)) {
        revisioned_space_error_v1(
            error, error_size,
            "revisioned Space provider schema has an empty name");
        return false;
    }
    if (strcmp(schema->open_relation, schema->member_relation) == 0 ||
        strcmp(schema->open_relation, schema->candidate_relation) == 0 ||
        strcmp(schema->open_relation, schema->emit_relation) == 0 ||
        strcmp(schema->member_relation, schema->candidate_relation) == 0 ||
        strcmp(schema->member_relation, schema->emit_relation) == 0 ||
        strcmp(schema->candidate_relation, schema->emit_relation) == 0) {
        revisioned_space_error_v1(
            error, error_size,
            "revisioned Space provider relations must have distinct dispatch keys");
        return false;
    }
    return true;
}

static Atom *revisioned_space_expr_v1(
    Arena *arena, const char *constructor, Atom **arguments,
    size_t argument_count) {
    if (!arena || !constructor ||
        argument_count == SIZE_MAX)
        return NULL;
    Atom **elements = arena_alloc(
        arena, sizeof(*elements) * (argument_count + 1u));
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, constructor);
    if (!elements[0])
        return NULL;
    for (size_t index = 0u; index < argument_count; index++) {
        elements[index + 1u] = arguments[index];
        if (!elements[index + 1u])
            return NULL;
    }
    return atom_expr(
        arena, elements, (CettaExprLen)(argument_count + 1u));
}

static Atom *revisioned_space_qstr_v1(Arena *arena, const char *value) {
    Atom *argument = atom_string(arena, value);
    return revisioned_space_expr_v1(arena, "q-str", &argument, 1u);
}

static Atom *revisioned_space_world_token_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    Arena *arena,
    const char digest[65]) {
    Atom *quoted_digest = revisioned_space_qstr_v1(arena, digest);
    return revisioned_space_expr_v1(
        arena, provider->schema.world_token_constructor,
        &quoted_digest, 1u);
}

static const char *revisioned_space_world_digest_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider,
    const Atom *token) {
    if (!provider || !token || token->kind != ATOM_EXPR ||
        token->expr.len != 2u ||
        !atom_is_symbol(
            token->expr.elems[0],
            provider->schema.world_token_constructor))
        return NULL;
    const Atom *quoted = token->expr.elems[1];
    if (!quoted || quoted->kind != ATOM_EXPR || quoted->expr.len != 2u ||
        !atom_is_symbol(quoted->expr.elems[0], "q-str"))
        return NULL;
    const Atom *value = quoted->expr.elems[1];
    return value && value->kind == ATOM_GROUNDED &&
        value->ground.gkind == GV_STRING && value->ground.sval &&
        strlen(value->ground.sval) == 64u
        ? value->ground.sval : NULL;
}

static bool revisioned_space_hash_atom_v1(
    CettaNativeSha256 *sha, Arena *scratch, const Atom *atom) {
    char *text = atom_to_parseable_string(scratch, (Atom *)atom);
    if (!text)
        return false;
    uint64_t length = (uint64_t)strlen(text);
    uint8_t length_bytes[8];
    for (unsigned index = 0u; index < 8u; index++)
        length_bytes[index] = (uint8_t)(length >> (index * 8u));
    cetta_native_sha256_update(sha, length_bytes, sizeof(length_bytes));
    cetta_native_sha256_update(sha, (const uint8_t *)text, (size_t)length);
    return true;
}

static bool revisioned_space_digest_v1(
    const char *domain,
    const Atom *const *atoms,
    size_t atom_count,
    char digest[65]) {
    Arena scratch;
    arena_init(&scratch);
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, strlen(domain));
    bool ok = true;
    for (size_t index = 0u; ok && index < atom_count; index++)
        ok = revisioned_space_hash_atom_v1(&sha, &scratch, atoms[index]);
    if (ok)
        cetta_native_sha256_finish_hex(&sha, digest);
    arena_free(&scratch);
    return ok;
}

static RevisionedSpaceWorldV1 *revisioned_space_find_world_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    const char *digest) {
    if (!provider || !digest)
        return NULL;
    for (size_t index = 0u; index < provider->world_count; index++) {
        if (strcmp(provider->worlds[index].digest, digest) == 0)
            return &provider->worlds[index];
    }
    return NULL;
}

static bool revisioned_space_primary_active_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider,
    const Space *space) {
    if (!provider || !space || space->match_backend.kind != provider->engine)
        return false;
    if (provider->engine == SPACE_ENGINE_NATIVE ||
        provider->engine == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)
        return true;
    if (provider->engine != SPACE_ENGINE_PATHMAP ||
        space->match_backend.pathmap.bridge.bridge_unavailable)
        return false;
    if (space_length64((Space *)space) == 0u)
        return true;
    return space->match_backend.pathmap.bridge.bridge_active &&
        space->match_backend.pathmap.bridge.bridge_space;
}

static Space *revisioned_space_clone_world_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    Space *source,
    char *error,
    size_t error_size) {
    if (!provider || !source)
        return NULL;
    if (provider->engine != SPACE_ENGINE_PATHMAP)
        return space_heap_clone_shallow(source);

    Space *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) {
        revisioned_space_error_v1(
            error, error_size,
            "cannot allocate one revisioned PathMap snapshot");
        return NULL;
    }
    space_init_with_universe(snapshot, &provider->universe);
    snapshot->kind = source->kind;
    if (!space_match_backend_snapshot_clone(snapshot, source)) {
        space_free(snapshot);
        free(snapshot);
        revisioned_space_error_v1(
            error, error_size,
            "cannot clone one revisioned PathMap snapshot through the Rust ABI");
        return NULL;
    }
    return snapshot;
}

static RevisionedSpaceWorldV1 *revisioned_space_append_world_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    const char digest[65],
    Space *space,
    char *error,
    size_t error_size) {
    if (provider->world_count == provider->world_capacity) {
        size_t next_capacity = provider->world_capacity
            ? provider->world_capacity * 2u : 8u;
        if (next_capacity < provider->world_capacity ||
            next_capacity > SIZE_MAX / sizeof(*provider->worlds)) {
            revisioned_space_error_v1(
                error, error_size,
                "revisioned Space world catalog is too large");
            return NULL;
        }
        RevisionedSpaceWorldV1 *next = realloc(
            provider->worlds, sizeof(*next) * next_capacity);
        if (!next) {
            revisioned_space_error_v1(
                error, error_size,
                "cannot extend revisioned Space world catalog");
            return NULL;
        }
        provider->worlds = next;
        provider->world_capacity = next_capacity;
    }
    RevisionedSpaceWorldV1 *world =
        &provider->worlds[provider->world_count++];
    memcpy(world->digest, digest, 65u);
    world->space = space;
    return world;
}

static Atom *revisioned_space_stored_row_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    Arena *arena,
    Atom *occurrence,
    Atom *value) {
    Atom *arguments[] = {occurrence, value};
    return revisioned_space_expr_v1(
        arena, provider->schema.stored_occurrence_constructor,
        arguments, 2u);
}

static bool revisioned_space_import_program_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    Space *space,
    const Atom *program,
    char *error,
    size_t error_size) {
    const Atom *cursor = program;
    while (cursor && cursor->kind == ATOM_EXPR && cursor->expr.len == 4u &&
           atom_is_symbol(
               cursor->expr.elems[0],
               provider->schema.program_cons_constructor)) {
        Atom *row = revisioned_space_stored_row_v1(
            provider, &provider->storage_arena,
            cursor->expr.elems[1], cursor->expr.elems[2]);
        if (!row || !space_admit_atom(
                space, &provider->storage_arena, row)) {
            revisioned_space_error_v1(
                error, error_size,
                "cannot import one authored program occurrence into Space");
            return false;
        }
        cursor = cursor->expr.elems[3];
    }
    if (!cursor || cursor->kind != ATOM_SYMBOL ||
        !atom_is_symbol(
            (Atom *)cursor, provider->schema.program_nil_constructor)) {
        revisioned_space_error_v1(
            error, error_size,
            "malformed authored program carrier at revisioned Space boundary");
        return false;
    }
    return true;
}

static RevisionedSpaceWorldV1 *revisioned_space_open_world_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    const Atom *program,
    char *error,
    size_t error_size) {
    const Atom *hash_atoms[] = {program};
    char digest[65];
    if (!revisioned_space_digest_v1(
            "cetta.gslt.revisioned-space.open.v1", hash_atoms, 1u, digest)) {
        revisioned_space_error_v1(
            error, error_size, "cannot hash authored program carrier");
        return NULL;
    }
    RevisionedSpaceWorldV1 *existing =
        revisioned_space_find_world_v1(provider, digest);
    if (existing)
        return existing;

    Space *space = malloc(sizeof(*space));
    if (!space) {
        revisioned_space_error_v1(
            error, error_size, "cannot allocate initial revisioned Space");
        return NULL;
    }
    space_init_with_universe(space, &provider->universe);
    if (!space_match_backend_try_set(space, provider->engine) ||
        !revisioned_space_import_program_v1(
            provider, space, program, error, error_size)) {
        space_free(space);
        free(space);
        return NULL;
    }
    RevisionedSpaceWorldV1 *world = revisioned_space_append_world_v1(
        provider, digest, space, error, error_size);
    if (!world) {
        space_free(space);
        free(space);
    }
    return world;
}

static bool revisioned_space_answer_push_v1(
    CettaGsltProviderAnswersV1 *answers,
    Atom *answer,
    char *error,
    size_t error_size) {
    if (answer && cetta_gslt_provider_answers_push_v1(answers, answer))
        return true;
    revisioned_space_error_v1(
        error, error_size,
        "cannot allocate revisioned Space provider answer frontier");
    return false;
}

static CettaGsltProviderOutcomeV1 revisioned_space_open_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltRevisionedSpaceProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !answers || !goal ||
        goal->kind != ATOM_EXPR || goal->expr.len != 4u ||
        !atom_is_symbol(goal->expr.elems[0], provider->schema.open_relation)) {
        revisioned_space_error_v1(
            error, error_size, "invalid revisioned Space open request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (answer_limit < 1u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;

    pthread_mutex_lock(&provider->mutex);
    RevisionedSpaceWorldV1 *world = revisioned_space_open_world_v1(
        provider, goal->expr.elems[1], error, error_size);
    if (!world) {
        pthread_mutex_unlock(&provider->mutex);
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    Atom *world_token = revisioned_space_world_token_v1(
        provider, answer_arena, world->digest);
    Atom *receipt = revisioned_space_expr_v1(
        answer_arena, provider->schema.open_receipt_constructor,
        &world_token, 1u);
    Atom *arguments[] = {
        atom_deep_copy(answer_arena, goal->expr.elems[1]),
        world_token,
        receipt,
    };
    Atom *answer = revisioned_space_expr_v1(
        answer_arena, provider->schema.open_relation, arguments, 3u);
    bool ok = revisioned_space_answer_push_v1(
        answers, answer, error, error_size);
    pthread_mutex_unlock(&provider->mutex);
    return ok ? CETTA_GSLT_PROVIDER_COMPLETED : CETTA_GSLT_PROVIDER_FAULT;
}

typedef struct {
    CettaGsltRevisionedSpaceProviderV1 *provider;
    Arena *answer_arena;
    Atom *world_token;
    CettaGsltProviderAnswersV1 *answers;
    char *error;
    size_t error_size;
    size_t visited;
    bool fault;
} RevisionedSpaceMemberVisitorV1;

static bool revisioned_space_member_visit_v1(Atom *row, void *raw_context) {
    RevisionedSpaceMemberVisitorV1 *visitor = raw_context;
    if (!visitor || !row || row->kind != ATOM_EXPR || row->expr.len != 3u ||
        !atom_is_symbol(
            row->expr.elems[0],
            visitor->provider->schema.stored_occurrence_constructor)) {
        if (visitor)
            visitor->fault = true;
        return false;
    }
    Atom *arguments[] = {
        atom_deep_copy(visitor->answer_arena, visitor->world_token),
        atom_deep_copy(visitor->answer_arena, row->expr.elems[1]),
        atom_deep_copy(visitor->answer_arena, row->expr.elems[2]),
    };
    Atom *answer = revisioned_space_expr_v1(
        visitor->answer_arena,
        visitor->provider->schema.member_relation,
        arguments, 3u);
    if (!revisioned_space_answer_push_v1(
            visitor->answers, answer,
            visitor->error, visitor->error_size)) {
        visitor->fault = true;
        return false;
    }
    visitor->visited++;
    return true;
}

static CettaGsltProviderOutcomeV1 revisioned_space_member_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltRevisionedSpaceProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !answers || !goal ||
        goal->kind != ATOM_EXPR || goal->expr.len != 4u ||
        !atom_is_symbol(goal->expr.elems[0], provider->schema.member_relation)) {
        revisioned_space_error_v1(
            error, error_size, "invalid revisioned Space member request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    const char *digest = revisioned_space_world_digest_v1(
        provider, goal->expr.elems[1]);
    if (!digest) {
        revisioned_space_error_v1(
            error, error_size,
            "member request carries a malformed world token");
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    pthread_mutex_lock(&provider->mutex);
    RevisionedSpaceWorldV1 *world =
        revisioned_space_find_world_v1(provider, digest);
    if (!world) {
        revisioned_space_error_v1(
            error, error_size,
            "member request cites an unknown world token");
        pthread_mutex_unlock(&provider->mutex);
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    CettaCount logical_count = space_length64(world->space);
    if ((uint64_t)logical_count > answer_limit) {
        pthread_mutex_unlock(&provider->mutex);
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    }

    RevisionedSpaceMemberVisitorV1 visitor = {
        .provider = provider,
        .answer_arena = answer_arena,
        .world_token = goal->expr.elems[1],
        .answers = answers,
        .error = error,
        .error_size = error_size,
    };
    bool complete = true;
    if (provider->engine == SPACE_ENGINE_PATHMAP) {
        Arena scratch;
        arena_init(&scratch);
        SpaceMatchPullVisitResult visited =
            space_match_backend_try_visit_atoms_direct(
                world->space, &scratch,
                revisioned_space_member_visit_v1, &visitor);
        arena_free(&scratch);
        complete = visited == SPACE_MATCH_PULL_VISIT_COMPLETE;
    } else {
        for (CettaIndex index = 0u;
             index < logical_count && !visitor.fault; index++) {
            Atom *row = space_get_at64(world->space, index);
            if (!revisioned_space_member_visit_v1(row, &visitor))
                break;
        }
    }
    if (!complete || visitor.fault || visitor.visited != logical_count) {
        if (!visitor.fault) {
            revisioned_space_error_v1(
                error, error_size,
                "physical Space did not provide one complete occurrence frontier");
        }
        cetta_gslt_provider_answers_free_v1(answers);
        pthread_mutex_unlock(&provider->mutex);
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    pthread_mutex_unlock(&provider->mutex);
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

typedef struct {
    const Atom *index;
    Atom *variable;
} RevisionedSpacePatternVariableV1;

typedef struct {
    RevisionedSpacePatternVariableV1 *items;
    size_t count;
    size_t capacity;
} RevisionedSpacePatternVariablesV1;

static Atom *revisioned_space_pattern_variable_v1(
    Arena *arena,
    RevisionedSpacePatternVariablesV1 *variables,
    const Atom *index) {
    if (!arena || !variables || !index)
        return NULL;
    for (size_t current = 0u; current < variables->count; current++) {
        if (atom_eq((Atom *)variables->items[current].index, (Atom *)index))
            return variables->items[current].variable;
    }
    if (variables->count == variables->capacity) {
        size_t next_capacity = variables->capacity
            ? variables->capacity * 2u : 8u;
        if (next_capacity < variables->capacity ||
            next_capacity > SIZE_MAX / sizeof(*variables->items))
            return NULL;
        RevisionedSpacePatternVariableV1 *next = realloc(
            variables->items, sizeof(*next) * next_capacity);
        if (!next)
            return NULL;
        variables->items = next;
        variables->capacity = next_capacity;
    }
    Atom *variable = atom_var_with_id(
        arena, "gslt-candidate", fresh_var_id());
    if (!variable)
        return NULL;
    variables->items[variables->count++] =
        (RevisionedSpacePatternVariableV1){
            .index = index,
            .variable = variable,
        };
    return variable;
}

/* Interpret quoted object variables as backend query variables while leaving
 * every other quotation constructor as ordinary data.  Stored q-var forms are
 * consequently matched as data, preserving the authored one-way qmatch
 * discipline rather than accidentally enabling symmetric object unification. */
static Atom *revisioned_space_candidate_pattern_v1(
    Arena *arena,
    RevisionedSpacePatternVariablesV1 *variables,
    const Atom *quoted,
    uint32_t depth) {
    if (!arena || !variables || !quoted ||
        depth > REVISIONED_SPACE_PATTERN_DEPTH_LIMIT_V1)
        return NULL;
    if (quoted->kind == ATOM_EXPR && quoted->expr.len == 2u &&
        atom_is_symbol(quoted->expr.elems[0], "q-var")) {
        return revisioned_space_pattern_variable_v1(
            arena, variables, quoted->expr.elems[1]);
    }
    if (quoted->kind == ATOM_VAR)
        return NULL;
    if (quoted->kind != ATOM_EXPR)
        return atom_deep_copy(arena, (Atom *)quoted);
    if ((size_t)quoted->expr.len >
        SIZE_MAX / sizeof(*quoted->expr.elems))
        return NULL;
    Atom **elements = quoted->expr.len
        ? arena_alloc(
            arena, sizeof(*elements) * (size_t)quoted->expr.len)
        : NULL;
    if (quoted->expr.len && !elements)
        return NULL;
    for (CettaExprIndex index = 0u; index < quoted->expr.len; index++) {
        elements[index] = revisioned_space_candidate_pattern_v1(
            arena, variables, quoted->expr.elems[index], depth + 1u);
        if (!elements[index])
            return NULL;
    }
    return atom_expr(arena, elements, quoted->expr.len);
}

typedef struct {
    CettaGsltRevisionedSpaceProviderV1 *provider;
    Arena *answer_arena;
    const Atom *goal;
    Atom *occurrence_variable;
    Atom *candidate_pattern;
    uint64_t answer_limit;
    CettaGsltProviderAnswersV1 *answers;
    char *error;
    size_t error_size;
    bool limited;
    bool fault;
} RevisionedSpaceCandidateVisitorV1;

static bool revisioned_space_candidate_visit_v1(
    const Bindings *raw_bindings,
    void *raw_context) {
    RevisionedSpaceCandidateVisitorV1 *visitor = raw_context;
    Bindings *bindings = (Bindings *)raw_bindings;
    if (!visitor || !bindings) {
        if (visitor)
            visitor->fault = true;
        return false;
    }
    if ((uint64_t)visitor->answers->answer_count >= visitor->answer_limit) {
        visitor->limited = true;
        return false;
    }
    Atom *occurrence = bindings_lookup_var(
        bindings, visitor->occurrence_variable);
    Atom *candidate = bindings_apply(
        bindings, visitor->answer_arena, visitor->candidate_pattern);
    Atom *arguments[] = {
        atom_deep_copy(visitor->answer_arena, visitor->goal->expr.elems[1]),
        atom_deep_copy(visitor->answer_arena, visitor->goal->expr.elems[2]),
        atom_deep_copy(visitor->answer_arena, occurrence),
        atom_deep_copy(visitor->answer_arena, candidate),
    };
    Atom *answer = revisioned_space_expr_v1(
        visitor->answer_arena,
        visitor->provider->schema.candidate_relation,
        arguments, 4u);
    if (!occurrence || !candidate ||
        !revisioned_space_answer_push_v1(
            visitor->answers, answer,
            visitor->error, visitor->error_size)) {
        visitor->fault = true;
        return false;
    }
    return true;
}

static CettaGsltProviderOutcomeV1 revisioned_space_candidate_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltRevisionedSpaceProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !answers || !goal ||
        goal->kind != ATOM_EXPR || goal->expr.len != 5u ||
        !atom_is_symbol(
            goal->expr.elems[0], provider->schema.candidate_relation)) {
        revisioned_space_error_v1(
            error, error_size, "invalid revisioned Space candidate request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    const char *digest = revisioned_space_world_digest_v1(
        provider, goal->expr.elems[1]);
    if (!digest) {
        revisioned_space_error_v1(
            error, error_size,
            "candidate request carries a malformed world token");
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    Arena scratch;
    arena_init(&scratch);
    RevisionedSpacePatternVariablesV1 variables = {0};
    Atom *candidate_pattern = revisioned_space_candidate_pattern_v1(
        &scratch, &variables, goal->expr.elems[2], 0u);
    Atom *occurrence_variable = atom_var_with_id(
        &scratch, "gslt-occurrence", fresh_var_id());
    Atom *row_arguments[] = {occurrence_variable, candidate_pattern};
    Atom *row_pattern = revisioned_space_expr_v1(
        &scratch, provider->schema.stored_occurrence_constructor,
        row_arguments, 2u);
    free(variables.items);
    if (!candidate_pattern || !occurrence_variable || !row_pattern) {
        arena_free(&scratch);
        revisioned_space_error_v1(
            error, error_size,
            "cannot lower one quoted candidate pattern to the Space query ABI");
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    RevisionedSpaceCandidateVisitorV1 visitor = {
        .provider = provider,
        .answer_arena = answer_arena,
        .goal = goal,
        .occurrence_variable = occurrence_variable,
        .candidate_pattern = candidate_pattern,
        .answer_limit = answer_limit,
        .answers = answers,
        .error = error,
        .error_size = error_size,
    };

    pthread_mutex_lock(&provider->mutex);
    RevisionedSpaceWorldV1 *world =
        revisioned_space_find_world_v1(provider, digest);
    if (!world) {
        revisioned_space_error_v1(
            error, error_size,
            "candidate request cites an unknown world token");
        pthread_mutex_unlock(&provider->mutex);
        arena_free(&scratch);
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    space_match_backend_clear_error();
    SpaceMatchPullVisitResult direct =
        space_match_backend_try_visit_bindings_indexed(
            world->space, &scratch, row_pattern,
            revisioned_space_candidate_visit_v1, &visitor);
    if (direct == SPACE_MATCH_PULL_VISIT_DECLINED) {
        SubstMatchSet matches;
        smset_init(&matches);
        space_subst_query(world->space, &scratch, row_pattern, &matches);
        for (CettaIndex index = 0u;
             index < matches.len && !visitor.fault && !visitor.limited;
             index++) {
            Bindings bindings;
            if (space_subst_match_with_seed(
                    world->space, row_pattern, &matches.items[index],
                    NULL, &scratch, &bindings)) {
                (void)revisioned_space_candidate_visit_v1(
                    &bindings, &visitor);
                bindings_free(&bindings);
            }
        }
        smset_free(&matches);
    } else if (direct == SPACE_MATCH_PULL_VISIT_TERMINATED &&
               !visitor.fault && !visitor.limited) {
        visitor.fault = true;
        revisioned_space_error_v1(
            error, error_size,
            "indexed Space candidate traversal terminated before completion");
    }
    SpaceMatchBackendError backend_error =
        space_match_backend_last_error_code();
    if (backend_error != SPACE_MATCH_BACKEND_ERROR_NONE &&
        !visitor.limited) {
        visitor.fault = true;
        revisioned_space_error_v1(
            error, error_size, "Space candidate query failed: %s",
            space_match_backend_error_name(backend_error));
    }
    space_match_backend_clear_error();
    pthread_mutex_unlock(&provider->mutex);
    arena_free(&scratch);

    if (visitor.limited || visitor.fault) {
        cetta_gslt_provider_answers_free_v1(answers);
        return visitor.limited
            ? CETTA_GSLT_PROVIDER_ANSWER_LIMIT
            : CETTA_GSLT_PROVIDER_FAULT;
    }
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

static CettaGsltProviderOutcomeV1 revisioned_space_emit_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltRevisionedSpaceProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !answers || !goal ||
        goal->kind != ATOM_EXPR || goal->expr.len != 7u ||
        !atom_is_symbol(goal->expr.elems[0], provider->schema.emit_relation)) {
        revisioned_space_error_v1(
            error, error_size, "invalid revisioned Space emit request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (answer_limit < 1u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    const char *parent_digest = revisioned_space_world_digest_v1(
        provider, goal->expr.elems[1]);
    if (!parent_digest) {
        revisioned_space_error_v1(
            error, error_size,
            "emit request carries a malformed predecessor world token");
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    const Atom *hash_atoms[] = {
        goal->expr.elems[1], goal->expr.elems[2], goal->expr.elems[3]};
    char child_digest[65];
    if (!revisioned_space_digest_v1(
            "cetta.gslt.revisioned-space.emit.v1",
            hash_atoms, 3u, child_digest)) {
        revisioned_space_error_v1(
            error, error_size, "cannot hash revisioned emission event");
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    pthread_mutex_lock(&provider->mutex);
    RevisionedSpaceWorldV1 *parent =
        revisioned_space_find_world_v1(provider, parent_digest);
    if (!parent) {
        revisioned_space_error_v1(
            error, error_size,
            "emit request cites an unknown predecessor world token");
        pthread_mutex_unlock(&provider->mutex);
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    RevisionedSpaceWorldV1 *child =
        revisioned_space_find_world_v1(provider, child_digest);
    if (!child) {
        Atom *stored_event = atom_deep_copy(
            &provider->storage_arena, goal->expr.elems[2]);
        Atom *stored_parent = atom_deep_copy(
            &provider->storage_arena, goal->expr.elems[1]);
        Atom *stored_value = atom_deep_copy(
            &provider->storage_arena, goal->expr.elems[3]);
        Atom *emitted_arguments[] = {stored_event, stored_parent};
        Atom *emitted_occurrence = revisioned_space_expr_v1(
            &provider->storage_arena,
            provider->schema.emitted_occurrence_constructor,
            emitted_arguments, 2u);
        Space *child_space = revisioned_space_clone_world_v1(
            provider, parent->space, error, error_size);
        Atom *stored_row = revisioned_space_stored_row_v1(
            provider, &provider->storage_arena,
            emitted_occurrence, stored_value);
        if (!stored_event || !stored_parent || !stored_value ||
            !emitted_occurrence || !child_space || !stored_row ||
            !space_admit_atom(
                child_space, &provider->storage_arena, stored_row)) {
            if (child_space) {
                space_free(child_space);
                free(child_space);
            }
            if (!error || error_size == 0u || error[0] == '\0') {
                revisioned_space_error_v1(
                    error, error_size,
                    "cannot publish one occurrence into successor Space");
            }
            pthread_mutex_unlock(&provider->mutex);
            return CETTA_GSLT_PROVIDER_FAULT;
        }
        child = revisioned_space_append_world_v1(
            provider, child_digest, child_space, error, error_size);
        if (!child) {
            space_free(child_space);
            free(child_space);
            pthread_mutex_unlock(&provider->mutex);
            return CETTA_GSLT_PROVIDER_FAULT;
        }
    }

    Atom *successor_token = revisioned_space_world_token_v1(
        provider, answer_arena, child->digest);
    Atom *public_occurrence_arguments[] = {
        atom_deep_copy(answer_arena, goal->expr.elems[2]),
        atom_deep_copy(answer_arena, goal->expr.elems[1]),
    };
    Atom *public_occurrence = revisioned_space_expr_v1(
        answer_arena, provider->schema.emitted_occurrence_constructor,
        public_occurrence_arguments, 2u);
    Atom *receipt_arguments[] = {
        atom_deep_copy(answer_arena, goal->expr.elems[1]),
        atom_deep_copy(answer_arena, goal->expr.elems[2]),
        successor_token,
        public_occurrence,
        atom_deep_copy(answer_arena, goal->expr.elems[3]),
    };
    Atom *receipt = revisioned_space_expr_v1(
        answer_arena, provider->schema.emit_receipt_constructor,
        receipt_arguments, 5u);
    Atom *answer_arguments[] = {
        atom_deep_copy(answer_arena, goal->expr.elems[1]),
        atom_deep_copy(answer_arena, goal->expr.elems[2]),
        atom_deep_copy(answer_arena, goal->expr.elems[3]),
        successor_token,
        public_occurrence,
        receipt,
    };
    Atom *answer = revisioned_space_expr_v1(
        answer_arena, provider->schema.emit_relation,
        answer_arguments, 6u);
    bool ok = revisioned_space_answer_push_v1(
        answers, answer, error, error_size);
    pthread_mutex_unlock(&provider->mutex);
    return ok ? CETTA_GSLT_PROVIDER_COMPLETED : CETTA_GSLT_PROVIDER_FAULT;
}

CettaGsltRevisionedSpaceProviderV1 *
cetta_gslt_revisioned_space_provider_create_v1(
    SpaceEngine engine,
    const CettaGsltRevisionedSpaceSchemaV1 *schema,
    char *error,
    size_t error_size) {
    if (!revisioned_space_schema_valid_v1(schema, error, error_size))
        return NULL;
    if (engine != SPACE_ENGINE_NATIVE &&
        engine != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT &&
        engine != SPACE_ENGINE_PATHMAP) {
        revisioned_space_error_v1(
            error, error_size,
            "revisioned Space provider does not admit engine %s",
            space_match_backend_kind_name(engine));
        return NULL;
    }
    const char *unavailable = space_match_backend_unavailable_reason(engine);
    if (unavailable) {
        revisioned_space_error_v1(error, error_size, "%s", unavailable);
        return NULL;
    }

    CettaGsltRevisionedSpaceProviderV1 *provider =
        calloc(1u, sizeof(*provider));
    if (!provider) {
        revisioned_space_error_v1(
            error, error_size,
            "cannot allocate revisioned Space provider");
        return NULL;
    }
    provider->engine = engine;
    provider->schema = *schema;
    arena_init(&provider->storage_arena);
    term_universe_init(&provider->universe);
    term_universe_set_persistent_arena(
        &provider->universe, &provider->storage_arena);
    if (pthread_mutex_init(&provider->mutex, NULL) != 0) {
        revisioned_space_error_v1(
            error, error_size,
            "cannot initialize revisioned Space provider mutex");
        term_universe_free(&provider->universe);
        arena_free(&provider->storage_arena);
        free(provider);
        return NULL;
    }
    provider->mutex_initialized = true;
    provider->initialized = true;
    provider->physical[0] = (CettaGsltProviderV1){
        .relation = schema->open_relation,
        .arity = 3u,
        .semantic_id = schema->open_semantic_id,
        .context = provider,
        .query = revisioned_space_open_query_v1,
    };
    provider->physical[1] = (CettaGsltProviderV1){
        .relation = schema->member_relation,
        .arity = 3u,
        .semantic_id = schema->member_semantic_id,
        .context = provider,
        .query = revisioned_space_member_query_v1,
    };
    provider->physical[2] = (CettaGsltProviderV1){
        .relation = schema->candidate_relation,
        .arity = 4u,
        .semantic_id = schema->candidate_semantic_id,
        .context = provider,
        .query = revisioned_space_candidate_query_v1,
    };
    provider->physical[3] = (CettaGsltProviderV1){
        .relation = schema->emit_relation,
        .arity = 6u,
        .semantic_id = schema->emit_semantic_id,
        .context = provider,
        .query = revisioned_space_emit_query_v1,
    };
    provider->registry = (CettaGsltProviderRegistryV1){
        .providers = provider->physical,
        .provider_count = 4u,
    };
    if (!cetta_gslt_provider_registry_validate_v1(
            &provider->registry, error, error_size)) {
        cetta_gslt_revisioned_space_provider_free_v1(provider);
        return NULL;
    }
    return provider;
}

void cetta_gslt_revisioned_space_provider_free_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider) {
    if (!provider)
        return;
    for (size_t index = 0u; index < provider->world_count; index++) {
        if (provider->worlds[index].space) {
            space_free(provider->worlds[index].space);
            free(provider->worlds[index].space);
        }
    }
    free(provider->worlds);
    if (provider->initialized) {
        term_universe_free(&provider->universe);
        arena_free(&provider->storage_arena);
    }
    if (provider->mutex_initialized)
        pthread_mutex_destroy(&provider->mutex);
    free(provider);
}

const CettaGsltProviderRegistryV1 *
cetta_gslt_revisioned_space_provider_registry_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider) {
    return provider && provider->initialized ? &provider->registry : NULL;
}

SpaceEngine cetta_gslt_revisioned_space_provider_engine_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider) {
    return provider ? provider->engine : SPACE_ENGINE_NATIVE;
}

size_t cetta_gslt_revisioned_space_provider_world_count_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider) {
    if (!provider)
        return 0u;
    CettaGsltRevisionedSpaceProviderV1 *mutable_provider =
        (CettaGsltRevisionedSpaceProviderV1 *)provider;
    pthread_mutex_lock(&mutable_provider->mutex);
    size_t count = mutable_provider->world_count;
    pthread_mutex_unlock(&mutable_provider->mutex);
    return count;
}

bool cetta_gslt_revisioned_space_provider_primary_active_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider) {
    if (!provider)
        return false;
    CettaGsltRevisionedSpaceProviderV1 *mutable_provider =
        (CettaGsltRevisionedSpaceProviderV1 *)provider;
    pthread_mutex_lock(&mutable_provider->mutex);
    bool active = true;
    for (size_t index = 0u;
         active && index < mutable_provider->world_count; index++) {
        active = revisioned_space_primary_active_v1(
            mutable_provider, mutable_provider->worlds[index].space);
    }
    pthread_mutex_unlock(&mutable_provider->mutex);
    return active;
}
