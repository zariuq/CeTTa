#ifndef CETTA_GSLT_PROVIDER_RUNTIME_H
#define CETTA_GSLT_PROVIDER_RUNTIME_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One finite, completed answer frontier returned by a semantic provider.
 * Answers are relational atoms with the same predicate and arity as the
 * submitted goal.  Atom storage belongs to the arena supplied to the provider;
 * only the pointer vector is released by answers_free. */
typedef struct {
    Atom **answers;
    size_t answer_count;
} CettaGsltProviderAnswersV1;

typedef enum {
    CETTA_GSLT_PROVIDER_COMPLETED = 0,
    CETTA_GSLT_PROVIDER_ANSWER_LIMIT = 1,
    CETTA_GSLT_PROVIDER_FAULT = 2,
} CettaGsltProviderOutcomeV1;

typedef CettaGsltProviderOutcomeV1 (*CettaGsltProviderQueryV1)(
    void *context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size);

/* A provider is selected by a semantic identity declared by the authored
 * language pack.  Relation and arity are the physical dispatch key; they do
 * not themselves establish semantic authority. */
typedef struct {
    const char *relation;
    uint32_t arity;
    const char *semantic_id;
    void *context;
    CettaGsltProviderQueryV1 query;
} CettaGsltProviderV1;

typedef struct {
    const CettaGsltProviderV1 *providers;
    size_t provider_count;
} CettaGsltProviderRegistryV1;

/* Authored authority for one externally realized relation.  A physical
 * provider is usable only when all three fields agree with this declaration. */
typedef struct {
    const char *relation;
    uint32_t arity;
    const char *semantic_id;
} CettaGsltProviderRequirementV1;

/* A provider catalog is compiled independently from a language pack, then
 * bound to one exact language manifest and optional profile.  This keeps
 * external libraries modular without allowing runtime registration to invent
 * language meaning. */
typedef struct {
    const char *name;
    const char *language_name;
    const char *profile_name;
    const char *language_manifest_sha256;
    const uint8_t *source_bytes;
    size_t source_length;
    const char *source_name;
    const char *source_sha256;
    const CettaGsltProviderRequirementV1 *requirements;
    size_t requirement_count;
    const char *generator_sha256;
} CettaGsltProviderCatalogV1;

typedef struct {
    CettaGsltProviderRegistryV1 registry;
    CettaGsltProviderV1 *storage;
} CettaGsltAuthorizedProviderRegistryV1;

/* Owned union of independently constructed physical registries.  The union
 * is validated with the same duplicate dispatch/semantic-identity rules as a
 * single registry. */
typedef struct {
    CettaGsltProviderRegistryV1 registry;
    CettaGsltProviderV1 *storage;
} CettaGsltOwnedProviderRegistryV1;

bool cetta_gslt_provider_registry_validate_v1(
    const CettaGsltProviderRegistryV1 *registry,
    char *error, size_t error_size);

bool cetta_gslt_provider_catalog_validate_v1(
    const CettaGsltProviderCatalogV1 *catalog,
    char *error, size_t error_size);

/* Select the physical providers authorized by a catalog.  Missing physical
 * providers remain ordinary empty relations; a provider with the right
 * dispatch key but the wrong semantic identity fails closed. */
bool cetta_gslt_provider_registry_authorize_v1(
    const CettaGsltProviderCatalogV1 *catalog,
    const CettaGsltProviderRegistryV1 *physical,
    CettaGsltAuthorizedProviderRegistryV1 *authorized,
    char *error, size_t error_size);

bool cetta_gslt_provider_registry_union_v1(
    const CettaGsltProviderRegistryV1 *const *registries,
    size_t registry_count,
    CettaGsltOwnedProviderRegistryV1 *combined,
    char *error,
    size_t error_size);

void cetta_gslt_owned_provider_registry_free_v1(
    CettaGsltOwnedProviderRegistryV1 *registry);

void cetta_gslt_authorized_provider_registry_free_v1(
    CettaGsltAuthorizedProviderRegistryV1 *authorized);

const CettaGsltProviderV1 *cetta_gslt_provider_find_v1(
    const CettaGsltProviderRegistryV1 *registry,
    const Atom *goal);

bool cetta_gslt_provider_answers_push_v1(
    CettaGsltProviderAnswersV1 *answers, Atom *answer);

void cetta_gslt_provider_answers_free_v1(
    CettaGsltProviderAnswersV1 *answers);

#endif /* CETTA_GSLT_PROVIDER_RUNTIME_H */
