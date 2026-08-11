#include "gslt_provider_runtime.h"

#include "native_sha256.h"
#include "parser.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool provider_error(char *error, size_t error_size,
                           const char *format, ...) {
    if (error && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool text_present(const char *text) {
    return text && text[0] != '\0';
}

static bool sha256_present(const char *text) {
    if (!text || strlen(text) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++)
        if (!isdigit((unsigned char)text[index]) &&
            !(text[index] >= 'a' && text[index] <= 'f'))
            return false;
    return true;
}

static bool provider_expr(const Atom *atom, const char *head,
                          CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(atom->expr.elems[0]), head) == 0;
}

static const char *provider_text(const Atom *atom) {
    if (!atom)
        return NULL;
    if (atom->kind == ATOM_SYMBOL)
        return atom_name_cstr((Atom *)atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
    return NULL;
}

static bool provider_u32(const Atom *atom, uint32_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > UINT32_MAX)
        return false;
    *value = (uint32_t)atom->ground.ival;
    return true;
}

bool cetta_gslt_provider_registry_validate_v1(
    const CettaGsltProviderRegistryV1 *registry,
    char *error, size_t error_size) {
    if (!registry)
        return true;
    if (registry->provider_count > 0u && !registry->providers)
        return provider_error(
            error, error_size, "provider registry has no provider table");
    for (size_t index = 0u; index < registry->provider_count; index++) {
        const CettaGsltProviderV1 *provider = &registry->providers[index];
        if (!text_present(provider->relation) ||
            !text_present(provider->semantic_id) || !provider->query)
            return provider_error(
                error, error_size, "provider registry entry %zu is incomplete",
                index);
        for (size_t prior = 0u; prior < index; prior++) {
            const CettaGsltProviderV1 *candidate =
                &registry->providers[prior];
            if (candidate->arity == provider->arity &&
                strcmp(candidate->relation, provider->relation) == 0)
                return provider_error(
                    error, error_size,
                    "provider relation is registered twice: %s/%u",
                    provider->relation, provider->arity);
            if (strcmp(candidate->semantic_id, provider->semantic_id) == 0)
                return provider_error(
                    error, error_size,
                    "provider semantic identity is registered twice: %s",
                    provider->semantic_id);
        }
    }
    return true;
}

bool cetta_gslt_provider_catalog_validate_v1(
    const CettaGsltProviderCatalogV1 *catalog,
    char *error, size_t error_size) {
    if (!catalog || !text_present(catalog->name) ||
        !text_present(catalog->language_name) ||
        !sha256_present(catalog->language_manifest_sha256) ||
        !catalog->source_bytes || catalog->source_length == 0u ||
        !text_present(catalog->source_name) ||
        !sha256_present(catalog->source_sha256) ||
        (catalog->requirement_count > 0u && !catalog->requirements) ||
        !sha256_present(catalog->generator_sha256) ||
        memchr(catalog->source_bytes, 0, catalog->source_length))
        return provider_error(
            error, error_size, "semantic-provider catalog is incomplete");

    char digest[65];
    cetta_native_sha256_hex(
        catalog->source_bytes, catalog->source_length, digest);
    if (strcmp(digest, catalog->source_sha256) != 0)
        return provider_error(
            error, error_size, "semantic-provider catalog digest changed");

    for (size_t index = 0u; index < catalog->requirement_count; index++) {
        const CettaGsltProviderRequirementV1 *requirement =
            &catalog->requirements[index];
        if (!text_present(requirement->relation) ||
            !text_present(requirement->semantic_id))
            return provider_error(
                error, error_size,
                "semantic-provider requirement %zu is incomplete", index);
        for (size_t prior = 0u; prior < index; prior++) {
            const CettaGsltProviderRequirementV1 *candidate =
                &catalog->requirements[prior];
            if (candidate->arity == requirement->arity &&
                strcmp(candidate->relation, requirement->relation) == 0)
                return provider_error(
                    error, error_size,
                    "semantic-provider requirement is declared twice: %s/%u",
                    requirement->relation, requirement->arity);
            if (strcmp(candidate->semantic_id, requirement->semantic_id) == 0)
                return provider_error(
                    error, error_size,
                    "semantic-provider identity is declared twice: %s",
                    requirement->semantic_id);
        }
    }

    char *source = cetta_malloc(catalog->source_length + 1u);
    memcpy(source, catalog->source_bytes, catalog->source_length);
    source[catalog->source_length] = '\0';
    Arena arena;
    arena_init(&arena);
    Atom **forms = NULL;
    int form_count = parse_metta_text(source, &arena, &forms);
    bool valid = false;
    if (form_count != 1 || !forms || !forms[0] ||
        forms[0]->kind != ATOM_EXPR || forms[0]->expr.len < 3u ||
        forms[0]->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(forms[0]->expr.elems[0]),
               "gslt-provider-catalog-v1") != 0) {
        provider_error(error, error_size,
                       "cannot parse authored semantic-provider catalog");
        goto done;
    }

    const char *name = NULL;
    const char *language = NULL;
    const char *profile = NULL;
    size_t requirement_index = 0u;
    for (CettaExprIndex index = 1u;
         index < forms[0]->expr.len; index++) {
        Atom *field = forms[0]->expr.elems[index];
        if (provider_expr(field, "name", 2u)) {
            const char *value = provider_text(field->expr.elems[1]);
            if (name || !value) {
                provider_error(error, error_size,
                               "provider catalog has an invalid name");
                goto done;
            }
            name = value;
        } else if (provider_expr(field, "language", 2u)) {
            const char *value = provider_text(field->expr.elems[1]);
            if (language || !value) {
                provider_error(error, error_size,
                               "provider catalog has an invalid language");
                goto done;
            }
            language = value;
        } else if (provider_expr(field, "profile", 2u)) {
            const char *value = provider_text(field->expr.elems[1]);
            if (profile || !value) {
                provider_error(error, error_size,
                               "provider catalog has an invalid profile");
                goto done;
            }
            profile = value;
        } else if (provider_expr(field, "provider", 4u)) {
            if (requirement_index >= catalog->requirement_count) {
                provider_error(error, error_size,
                               "provider catalog descriptor omits a declaration");
                goto done;
            }
            const char *relation = provider_text(field->expr.elems[1]);
            uint32_t arity = 0u;
            const char *semantic_id = provider_text(field->expr.elems[3]);
            const CettaGsltProviderRequirementV1 *requirement =
                &catalog->requirements[requirement_index++];
            if (!relation ||
                !provider_u32(field->expr.elems[2], &arity) ||
                !semantic_id || arity != requirement->arity ||
                strcmp(relation, requirement->relation) != 0 ||
                strcmp(semantic_id, requirement->semantic_id) != 0) {
                provider_error(
                    error, error_size,
                    "provider catalog descriptor differs at declaration %zu "
                    "(%s/%u/%s versus %s/%u/%s)",
                    requirement_index - 1u,
                    relation ? relation : "<invalid>", arity,
                    semantic_id ? semantic_id : "<invalid>",
                    requirement->relation, requirement->arity,
                    requirement->semantic_id);
                goto done;
            }
        } else {
            provider_error(error, error_size,
                           "provider catalog contains an unknown field");
            goto done;
        }
    }
    if (!name || !language || requirement_index != catalog->requirement_count ||
        strcmp(name, catalog->name) != 0 ||
        strcmp(language, catalog->language_name) != 0 ||
        ((profile || catalog->profile_name) &&
         (!profile || !catalog->profile_name ||
          strcmp(profile, catalog->profile_name) != 0))) {
        provider_error(
            error, error_size,
            "provider catalog descriptor differs from authored catalog");
        goto done;
    }
    valid = true;

done:
    free(forms);
    arena_free(&arena);
    free(source);
    return valid;
}

bool cetta_gslt_provider_registry_authorize_v1(
    const CettaGsltProviderCatalogV1 *catalog,
    const CettaGsltProviderRegistryV1 *physical,
    CettaGsltAuthorizedProviderRegistryV1 *authorized,
    char *error, size_t error_size) {
    if (!authorized)
        return provider_error(
            error, error_size, "authorized provider registry is absent");
    memset(authorized, 0, sizeof(*authorized));
    if (!catalog) {
        if (physical && physical->provider_count > 0u)
            return provider_error(
                error, error_size,
                "physical providers have no authored provider catalog");
        return true;
    }
    if (!cetta_gslt_provider_catalog_validate_v1(
            catalog, error, error_size) ||
        !cetta_gslt_provider_registry_validate_v1(
            physical, error, error_size))
        return false;
    if (!physical || physical->provider_count == 0u)
        return true;

    authorized->storage = catalog->requirement_count
        ? cetta_malloc(sizeof(*authorized->storage) *
                       catalog->requirement_count)
        : NULL;
    for (size_t requirement_index = 0u;
         requirement_index < catalog->requirement_count;
         requirement_index++) {
        const CettaGsltProviderRequirementV1 *requirement =
            &catalog->requirements[requirement_index];
        const CettaGsltProviderV1 *provider = NULL;
        for (size_t physical_index = 0u;
             physical_index < physical->provider_count; physical_index++) {
            const CettaGsltProviderV1 *candidate =
                &physical->providers[physical_index];
            if (candidate->arity == requirement->arity &&
                strcmp(candidate->relation, requirement->relation) == 0) {
                provider = candidate;
                break;
            }
        }
        if (!provider)
            continue;
        if (strcmp(provider->semantic_id, requirement->semantic_id) != 0) {
            cetta_gslt_authorized_provider_registry_free_v1(authorized);
            return provider_error(
                error, error_size,
                "physical provider %s/%u has semantic identity %s, expected %s",
                requirement->relation, requirement->arity,
                provider->semantic_id, requirement->semantic_id);
        }
        authorized->storage[authorized->registry.provider_count++] = *provider;
    }
    authorized->registry.providers = authorized->storage;
    return true;
}

void cetta_gslt_authorized_provider_registry_free_v1(
    CettaGsltAuthorizedProviderRegistryV1 *authorized) {
    if (!authorized)
        return;
    free(authorized->storage);
    memset(authorized, 0, sizeof(*authorized));
}

bool cetta_gslt_provider_registry_union_v1(
    const CettaGsltProviderRegistryV1 *const *registries,
    size_t registry_count,
    CettaGsltOwnedProviderRegistryV1 *combined,
    char *error,
    size_t error_size) {
    if (!combined)
        return provider_error(
            error, error_size, "combined provider registry is absent");
    memset(combined, 0, sizeof(*combined));
    if (registry_count > 0u && !registries)
        return provider_error(
            error, error_size, "provider registry union has no inputs");

    size_t total = 0u;
    for (size_t index = 0u; index < registry_count; index++) {
        const CettaGsltProviderRegistryV1 *registry = registries[index];
        if (!cetta_gslt_provider_registry_validate_v1(
                registry, error, error_size))
            return false;
        size_t count = registry ? registry->provider_count : 0u;
        if (count > SIZE_MAX - total)
            return provider_error(
                error, error_size, "provider registry union is too large");
        total += count;
    }
    if (total > SIZE_MAX / sizeof(*combined->storage))
        return provider_error(
            error, error_size, "provider registry union is too large");
    combined->storage = total
        ? cetta_malloc(sizeof(*combined->storage) * total) : NULL;
    size_t cursor = 0u;
    for (size_t index = 0u; index < registry_count; index++) {
        const CettaGsltProviderRegistryV1 *registry = registries[index];
        if (!registry)
            continue;
        if (registry->provider_count > 0u) {
            memcpy(combined->storage + cursor, registry->providers,
                   sizeof(*combined->storage) * registry->provider_count);
            cursor += registry->provider_count;
        }
    }
    combined->registry.providers = combined->storage;
    combined->registry.provider_count = total;
    if (!cetta_gslt_provider_registry_validate_v1(
            &combined->registry, error, error_size)) {
        cetta_gslt_owned_provider_registry_free_v1(combined);
        return false;
    }
    return true;
}

void cetta_gslt_owned_provider_registry_free_v1(
    CettaGsltOwnedProviderRegistryV1 *registry) {
    if (!registry)
        return;
    free(registry->storage);
    memset(registry, 0, sizeof(*registry));
}

const CettaGsltProviderV1 *cetta_gslt_provider_find_v1(
    const CettaGsltProviderRegistryV1 *registry,
    const Atom *goal) {
    if (!registry || !goal || goal->kind != ATOM_EXPR ||
        goal->expr.len == 0u || goal->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    const char *relation = atom_name_cstr(goal->expr.elems[0]);
    uint32_t arity = (uint32_t)(goal->expr.len - 1u);
    for (size_t index = 0u; index < registry->provider_count; index++) {
        const CettaGsltProviderV1 *provider = &registry->providers[index];
        if (provider->arity == arity &&
            strcmp(provider->relation, relation) == 0)
            return provider;
    }
    return NULL;
}

bool cetta_gslt_provider_answers_push_v1(
    CettaGsltProviderAnswersV1 *answers, Atom *answer) {
    if (!answers || !answer || answers->answer_count == SIZE_MAX)
        return false;
    answers->answers = cetta_realloc(
        answers->answers,
        sizeof(*answers->answers) * (answers->answer_count + 1u));
    answers->answers[answers->answer_count++] = answer;
    return true;
}

void cetta_gslt_provider_answers_free_v1(
    CettaGsltProviderAnswersV1 *answers) {
    if (!answers)
        return;
    free(answers->answers);
    memset(answers, 0, sizeof(*answers));
}
