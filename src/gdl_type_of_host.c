#include "gdl_type_of_host.h"

#include <stdlib.h>
#include <string.h>

struct CettaGdlTypeOfHostV1 {
    Space *space;
    SpaceReadToken read;
    CettaIndex package_occurrence;
    AtomId package_atom_id;
    bool has_package_atom_id;
    CettaGdlTypeOfNativeV1 *native;
    CettaNikDirectAuthorityTokenV1 token;
};

struct CettaGdlTypeOfHostCacheV1 {
    CettaGdlTypeOfHostV1 *host;
    Space *space;
    CettaIndex package_occurrence;
    char *expected_revision;
    CettaGdlTypeOfNativeLimitsV1 limits;
};

static CettaGdlTypeOfHostAdmissionV1 gdl_host_admission_v1(
    CettaGdlTypeOfHostAdmissionKindV1 kind,
    CettaGdlTypeOfHostV1 *host) {
    return (CettaGdlTypeOfHostAdmissionV1){
        .kind = kind,
        .host = host,
    };
}

static CettaGdlTypeOfHostAdmissionKindV1
gdl_host_native_admission_kind_v1(
    CettaGdlTypeOfNativeAdmissionKindV1 kind) {
    switch (kind) {
    case CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1:
        return CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1;
    case CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1:
        return CETTA_GDL_TYPE_OF_HOST_OUTSIDE_FRAGMENT_V1;
    case CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1:
        return CETTA_GDL_TYPE_OF_HOST_INCOMPLETE_V1;
    case CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1:
    default:
        return CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1;
    }
}

static CettaGdlTypeOfNativeQueryV1 gdl_host_stale_query_v1(void) {
    return (CettaGdlTypeOfNativeQueryV1){
        .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1,
        .selection = {
            .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
            .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
            .greatest_index = SIZE_MAX,
        },
    };
}

static CettaGdlTypeOfNativeQueryV1 gdl_host_fault_query_v1(void) {
    return (CettaGdlTypeOfNativeQueryV1){
        .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_ENGINE_FAULT_V1,
        .selection = {
            .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
            .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
            .greatest_index = SIZE_MAX,
        },
        .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
    };
}

CettaGdlTypeOfHostAdmissionV1 cetta_gdl_type_of_host_admit_v1(
    Space *space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits) {
    if (!space || !expected_revision || !*expected_revision ||
        space_instance_id(space) == 0u)
        return gdl_host_admission_v1(
            CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1, NULL);

    SpaceReadToken read = space_read_token(space);
    if (!space_read_token_matches_live_space(read, space))
        return gdl_host_admission_v1(
            CETTA_GDL_TYPE_OF_HOST_STALE_V1, NULL);
    if (package_occurrence >= space_length64(space))
        return gdl_host_admission_v1(
            CETTA_GDL_TYPE_OF_HOST_OUTSIDE_FRAGMENT_V1, NULL);

    Atom *package = space_get_at64(space, package_occurrence);
    AtomId package_atom_id = space_get_atom_id_at64(
        space, package_occurrence);
    CettaGdlTypeOfNativeAdmissionV1 native_admission =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            package, limits);
    if (native_admission.kind != CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 ||
        !native_admission.native) {
        CettaGdlTypeOfHostAdmissionKindV1 kind =
            native_admission.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1
                ? CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1
                : gdl_host_native_admission_kind_v1(
                      native_admission.kind);
        cetta_gdl_type_of_native_destroy_v1(native_admission.native);
        return gdl_host_admission_v1(
            kind, NULL);
    }

    const char *source_sha256 = NULL;
    const char *profile_sha256 = NULL;
    const char *revision = NULL;
    CettaNikDirectAuthorityTokenV1 token;
    bool admitted_identity = cetta_gdl_type_of_native_identity_v1(
            native_admission.native, &source_sha256, &profile_sha256,
            &revision) &&
        source_sha256 && profile_sha256 && revision &&
        strcmp(revision, expected_revision) == 0 &&
        cetta_gdl_type_of_native_token_v1(
            native_admission.native, &token);
    if (!admitted_identity) {
        cetta_gdl_type_of_native_destroy_v1(native_admission.native);
        return gdl_host_admission_v1(
            CETTA_GDL_TYPE_OF_HOST_OUTSIDE_FRAGMENT_V1, NULL);
    }
    AtomId live_package_atom_id = package_occurrence < space_length64(space)
        ? space_get_atom_id_at64(space, package_occurrence)
        : CETTA_ATOM_ID_NONE;
    Atom *live_package = package_occurrence < space_length64(space)
        ? space_get_at64(space, package_occurrence)
        : NULL;
    bool same_package = package_atom_id != CETTA_ATOM_ID_NONE
        ? live_package_atom_id == package_atom_id
        : live_package && atom_eq(live_package, package);
    if (!space_read_token_matches_live_space(read, space) ||
        !same_package) {
        cetta_gdl_type_of_native_destroy_v1(native_admission.native);
        return gdl_host_admission_v1(
            CETTA_GDL_TYPE_OF_HOST_STALE_V1, NULL);
    }

    CettaGdlTypeOfHostV1 *host = malloc(sizeof(*host));
    if (!host) {
        cetta_gdl_type_of_native_destroy_v1(native_admission.native);
        return gdl_host_admission_v1(
            CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1, NULL);
    }
    *host = (CettaGdlTypeOfHostV1){
        .space = space,
        .read = read,
        .package_occurrence = package_occurrence,
        .package_atom_id = package_atom_id,
        .has_package_atom_id =
            package_atom_id != CETTA_ATOM_ID_NONE,
        .native = native_admission.native,
        .token = token,
    };
    return gdl_host_admission_v1(
        CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1, host);
}

void cetta_gdl_type_of_host_destroy_v1(CettaGdlTypeOfHostV1 *host) {
    if (!host)
        return;
    cetta_gdl_type_of_native_destroy_v1(host->native);
    free(host);
}

bool cetta_gdl_type_of_host_is_current_v1(
    const CettaGdlTypeOfHostV1 *host,
    const Space *live_space) {
    if (!host || !live_space || host->space != live_space ||
        !space_read_token_matches_live_space(host->read, live_space) ||
        host->package_occurrence >= space_length64(live_space))
        return false;
    if (host->has_package_atom_id &&
        space_get_atom_id_at64(
            live_space, host->package_occurrence) != host->package_atom_id)
        return false;
    return cetta_gdl_type_of_native_token_is_current_v1(
        host->native, &host->token);
}

bool cetta_gdl_type_of_host_receipt_v1(
    const CettaGdlTypeOfHostV1 *host,
    const Space *live_space,
    CettaGdlTypeOfHostReceiptV1 *receipt_out) {
    if (!receipt_out ||
        !cetta_gdl_type_of_host_is_current_v1(host, live_space))
        return false;
    *receipt_out = (CettaGdlTypeOfHostReceiptV1){
        .read = host->read,
        .package_occurrence = host->package_occurrence,
        .package_atom_id = host->package_atom_id,
        .has_package_atom_id = host->has_package_atom_id,
        .authority = host->token,
    };
    return true;
}

CettaGdlTypeOfHostQueryV1 cetta_gdl_type_of_host_synthesize_v1(
    const CettaGdlTypeOfHostV1 *host,
    const Space *live_space,
    Atom *source_occurrence,
    Atom *term,
    size_t max_proofs) {
    CettaGdlTypeOfHostQueryV1 result = {0};
    if (!host || !live_space || !source_occurrence || !term) {
        result.native = gdl_host_fault_query_v1();
        return result;
    }
    if (!cetta_gdl_type_of_host_receipt_v1(
            host, live_space, &result.receipt)) {
        result.native = gdl_host_stale_query_v1();
        return result;
    }
    result.native = cetta_gdl_type_of_native_synthesize_v1(
        host->native, &host->token, source_occurrence, term, max_proofs);
    if (!cetta_gdl_type_of_host_is_current_v1(host, live_space)) {
        result.native = gdl_host_stale_query_v1();
        memset(&result.receipt, 0, sizeof(result.receipt));
    }
    return result;
}

static bool gdl_host_limits_equal_v1(
    CettaGdlTypeOfNativeLimitsV1 left,
    CettaGdlTypeOfNativeLimitsV1 right) {
    return left.max_source_nodes == right.max_source_nodes &&
        left.max_proof_nodes == right.max_proof_nodes &&
        left.max_derivation_depth == right.max_derivation_depth;
}

static void gdl_host_cache_clear_v1(CettaGdlTypeOfHostCacheV1 *cache) {
    if (!cache)
        return;
    cetta_gdl_type_of_host_destroy_v1(cache->host);
    free(cache->expected_revision);
    *cache = (CettaGdlTypeOfHostCacheV1){0};
}

CettaGdlTypeOfHostCacheV1 *cetta_gdl_type_of_host_cache_create_v1(void) {
    return calloc(1u, sizeof(CettaGdlTypeOfHostCacheV1));
}

void cetta_gdl_type_of_host_cache_destroy_v1(
    CettaGdlTypeOfHostCacheV1 *cache) {
    gdl_host_cache_clear_v1(cache);
    free(cache);
}

CettaGdlTypeOfHostResolutionV1 cetta_gdl_type_of_host_cache_resolve_v1(
    CettaGdlTypeOfHostCacheV1 *cache,
    Space *space,
    CettaIndex package_occurrence,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits) {
    if (!cache || !space || !expected_revision || !*expected_revision)
        return (CettaGdlTypeOfHostResolutionV1){
            .kind = CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1,
        };
    if (cache->host && cache->space == space &&
        cache->package_occurrence == package_occurrence &&
        cache->expected_revision &&
        strcmp(cache->expected_revision, expected_revision) == 0 &&
        gdl_host_limits_equal_v1(cache->limits, limits) &&
        cetta_gdl_type_of_host_is_current_v1(cache->host, space)) {
        return (CettaGdlTypeOfHostResolutionV1){
            .kind = CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1,
            .host = cache->host,
            .cache_hit = true,
        };
    }

    gdl_host_cache_clear_v1(cache);
    CettaGdlTypeOfHostAdmissionV1 admission =
        cetta_gdl_type_of_host_admit_v1(
            space, package_occurrence, expected_revision, limits);
    if (admission.kind != CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1 ||
        !admission.host) {
        cetta_gdl_type_of_host_destroy_v1(admission.host);
        return (CettaGdlTypeOfHostResolutionV1){
            .kind = admission.kind == CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1
                ? CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1
                : admission.kind,
        };
    }

    size_t revision_length = strlen(expected_revision);
    cache->expected_revision = malloc(revision_length + 1u);
    if (!cache->expected_revision) {
        cetta_gdl_type_of_host_destroy_v1(admission.host);
        return (CettaGdlTypeOfHostResolutionV1){
            .kind = CETTA_GDL_TYPE_OF_HOST_ENGINE_FAULT_V1,
        };
    }
    memcpy(
        cache->expected_revision, expected_revision,
        revision_length + 1u);
    cache->host = admission.host;
    cache->space = space;
    cache->package_occurrence = package_occurrence;
    cache->limits = limits;
    return (CettaGdlTypeOfHostResolutionV1){
        .kind = CETTA_GDL_TYPE_OF_HOST_ADMITTED_V1,
        .host = cache->host,
        .cache_hit = false,
    };
}
