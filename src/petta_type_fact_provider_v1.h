#ifndef CETTA_PETTA_TYPE_FACT_PROVIDER_V1_H
#define CETTA_PETTA_TYPE_FACT_PROVIDER_V1_H

#include "gslt_provider_runtime.h"
#include "petta_program.h"
#include "space.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Revision-pinned realization of the five PeTTa type-fact relations shared by
 * the generated v2 fragment and native v3.  The provider reads declarations
 * and expressions directly from live program state; it never invokes either
 * checker.  Answers use the established v2 wire vocabulary, which v3
 * elaborates explicitly into its native core.
 */
typedef struct CettaPettaTypeFactProviderV1
    CettaPettaTypeFactProviderV1;

CettaPettaTypeFactProviderV1 *
cetta_petta_type_fact_provider_create_v1(
    PettaProgram *program,
    Space *space,
    char *error,
    size_t error_size);

/* Admit one source block against the pinned live revision without publishing
 * it first.  Provider queries see live facts followed by the source-ordered
 * overlay.  The caller retains ownership of the forms and must keep them live
 * until the provider is freed. */
CettaPettaTypeFactProviderV1 *
cetta_petta_type_fact_provider_create_with_overlay_v1(
    PettaProgram *program,
    Space *space,
    Atom *const *forms,
    size_t form_count,
    char *error,
    size_t error_size);

void cetta_petta_type_fact_provider_free_v1(
    CettaPettaTypeFactProviderV1 *provider);

const CettaGsltProviderRegistryV1 *
cetta_petta_type_fact_provider_registry_v1(
    const CettaPettaTypeFactProviderV1 *provider);

SpaceReadToken cetta_petta_type_fact_provider_read_v1(
    const CettaPettaTypeFactProviderV1 *provider);

bool cetta_petta_type_fact_provider_is_current_v1(
    const CettaPettaTypeFactProviderV1 *provider);

/* Encode one PeTTa syntax type with the same wire vocabulary returned by
 * EnvDeclared and KnownExpressionResultType.  This is the shared elaboration
 * boundary for contextual annotations such as `(the T expression)`. */
Atom *cetta_petta_type_fact_wire_type_v1(
    Arena *arena,
    const Atom *type);

#endif /* CETTA_PETTA_TYPE_FACT_PROVIDER_V1_H */
