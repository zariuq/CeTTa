#ifndef CETTA_GDL_TYPE_OF_HOST_H
#define CETTA_GDL_TYPE_OF_HOST_H

#include "gdl_type_of_native.h"
#include "space.h"

/* A language-owned hosted GDL typing context.  The source package is ordinary
 * Space data at one exact occurrence.  Admission validates its authored
 * source/profile content and records the surrounding Space revision; the
 * parser contributes neither a mode nor an authority claim. */
typedef struct CettaGdlTypeOfHostV1 CettaGdlTypeOfHostV1;

typedef enum {
    CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 = 1,
    CETTA_GDL_TYPE_OF_HOST_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_TYPE_OF_HOST_INCOMPLETE_V1,
    CETTA_GDL_TYPE_OF_HOST_STALE_V1,
    CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1,
} CettaGdlTypeOfHostAdmissionKindV1;

typedef struct {
    CettaGdlTypeOfHostAdmissionKindV1 kind;
    CettaGdlTypeOfHostV1 *host;
} CettaGdlTypeOfHostAdmissionV1;

/* Admit the package at one exact authored occurrence of a live Space.  The
 * expected revision is supplied by the requesting guest value; equality is
 * checked against the revision recomputed from the exact source/profile
 * bytes. */
CettaGdlTypeOfHostAdmissionV1 cetta_gdl_type_of_host_admit_v1(
    Space *space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits);

void cetta_gdl_type_of_host_destroy_v1(CettaGdlTypeOfHostV1 *host);

bool cetta_gdl_type_of_host_is_current_v1(
    const CettaGdlTypeOfHostV1 *host,
    const Space *live_space);

/* Identity-only receipt for the checked ingress.  Judgment evidence remains
 * in the language-owned query result and cannot be recovered from this
 * receipt alone. */
typedef struct {
    SpaceReadToken read;
    CettaIndex package_occurrence;
    AtomId package_atom_id;
    bool has_package_atom_id;
    CettaNikDirectAuthorityTokenV1 authority;
} CettaGdlTypeOfHostReceiptV1;

bool cetta_gdl_type_of_host_receipt_v1(
    const CettaGdlTypeOfHostV1 *host,
    const Space *live_space,
    CettaGdlTypeOfHostReceiptV1 *receipt_out);

typedef struct {
    CettaGdlTypeOfHostReceiptV1 receipt;
    CettaGdlTypeOfNativeQueryV1 native;
} CettaGdlTypeOfHostQueryV1;

/* Synthesize one source occurrence through the strongest licensed native
 * realization for this exact hosted calculus.  Space revision staleness is
 * checked before and after construction; it never becomes a refutation. */
CettaGdlTypeOfHostQueryV1 cetta_gdl_type_of_host_synthesize_v1(
    const CettaGdlTypeOfHostV1 *host,
    const Space *live_space,
    Atom *source_occurrence,
    Atom *term,
    size_t max_proofs);

/* Episode-owned cache for repeated queries against one hosted source
 * package.  Reuse requires exact Space currentness, package occurrence,
 * source revision, and admission limits; any change discards the old host
 * before a new admission is attempted. */
typedef struct CettaGdlTypeOfHostCacheV1 CettaGdlTypeOfHostCacheV1;

CettaGdlTypeOfHostCacheV1 *cetta_gdl_type_of_host_cache_create_v1(void);

void cetta_gdl_type_of_host_cache_destroy_v1(
    CettaGdlTypeOfHostCacheV1 *cache);

typedef struct {
    CettaGdlTypeOfHostAdmissionKindV1 kind;
    const CettaGdlTypeOfHostV1 *host;
    bool cache_hit;
} CettaGdlTypeOfHostResolutionV1;

CettaGdlTypeOfHostResolutionV1 cetta_gdl_type_of_host_cache_resolve_v1(
    CettaGdlTypeOfHostCacheV1 *cache,
    Space *space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits);

#endif /* CETTA_GDL_TYPE_OF_HOST_H */
