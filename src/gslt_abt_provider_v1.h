#ifndef CETTA_GSLT_ABT_PROVIDER_V1_H
#define CETTA_GSLT_ABT_PROVIDER_V1_H

#include "abt.h"
#include "gslt_provider_runtime.h"

#include <stddef.h>

/* Generic ABT leaf relations.  The surrounding authored GSLT owns matching,
 * environment construction, substitution, and control.  This provider owns
 * only declaration-driven field depths and capture-avoiding transport of one
 * canonical quoted value between two support depths. */
typedef struct {
    const char *field_depth_relation;
    const char *field_depth_semantic_id;
    const char *transport_relation;
    const char *transport_semantic_id;
} CettaGsltAbtProviderSchemaV1;

typedef struct CettaGsltAbtProviderV1 CettaGsltAbtProviderV1;

/* Defaults are always admitted from lib/abt_default_signatures.metta.  An
 * optional extension set uses the ordinary AbtSignatures declaration format
 * and is copied into the provider's immutable signature at construction. */
CettaGsltAbtProviderV1 *cetta_gslt_abt_provider_create_v1(
    const CettaGsltAbtProviderSchemaV1 *schema,
    Atom *extension_signatures,
    char *error,
    size_t error_size);

void cetta_gslt_abt_provider_free_v1(CettaGsltAbtProviderV1 *provider);

const CettaGsltProviderRegistryV1 *cetta_gslt_abt_provider_registry_v1(
    const CettaGsltAbtProviderV1 *provider);

#endif /* CETTA_GSLT_ABT_PROVIDER_V1_H */
