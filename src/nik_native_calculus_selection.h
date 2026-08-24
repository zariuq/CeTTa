#ifndef CETTA_NIK_NATIVE_CALCULUS_SELECTION_H
#define CETTA_NIK_NATIVE_CALCULUS_SELECTION_H

#include <stddef.h>
#include <stdint.h>

/* Capabilities are language-owned stable identities.  NIK compares only the
 * capabilities and admitted upgrade edges supplied by one exact licensed
 * family; it assigns no global tier numbers to hosted calculi. */
typedef uint64_t CettaNikNativeCapabilityIdV1;

typedef struct {
    uint64_t realization_identity;
    /* Strictly increasing, duplicate-free capability identities. */
    const CettaNikNativeCapabilityIdV1 *capabilities;
    size_t capability_count;
} CettaNikLicensedNativeRealizationV1;

/* One already-admitted strict replacement relation.  Capability inclusion
 * is checked as a necessary law, but never used by itself to invent an
 * upgrade: semantic dominance must arrive as an explicit licensed edge. */
typedef struct {
    size_t weaker_index;
    size_t stronger_index;
} CettaNikLicensedNativeUpgradeV1;

/* The caller supplies exactly the realizations licensed for one guest,
 * semantic fragment, and dependency revision.  Selection does not create or
 * refresh licenses. */
typedef struct {
    const CettaNikLicensedNativeRealizationV1 *realizations;
    size_t realization_count;
    const CettaNikLicensedNativeUpgradeV1 *upgrades;
    size_t upgrade_count;
} CettaNikLicensedNativeFamilyV1;

typedef struct {
    /* Strictly increasing, duplicate-free capability identities. */
    const CettaNikNativeCapabilityIdV1 *required_capabilities;
    size_t required_capability_count;
} CettaNikNativeCapabilityRequestV1;

typedef enum {
    CETTA_NIK_NATIVE_SELECTION_STATUS_OK_V1 = 0,
    CETTA_NIK_NATIVE_SELECTION_STATUS_INVALID_FAMILY_V1,
    CETTA_NIK_NATIVE_SELECTION_STATUS_INVALID_REQUEST_V1,
    CETTA_NIK_NATIVE_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1,
    CETTA_NIK_NATIVE_SELECTION_STATUS_RESOURCE_FAULT_V1,
} CettaNikNativeSelectionStatusV1;

typedef enum {
    /* No licensed realization supports the exact request.  This requests
     * relational/raw fallback; it is not a refutation. */
    CETTA_NIK_NATIVE_SELECTION_NONE_V1 = 0,
    /* The request fibre has one realization above every other candidate. */
    CETTA_NIK_NATIVE_SELECTION_UNIQUE_GREATEST_V1,
    /* No greatest member exists; every incomparable maximal member is
     * returned and an explicit observation/cost policy must choose. */
    CETTA_NIK_NATIVE_SELECTION_MAXIMAL_FRONTIER_V1,
} CettaNikNativeSelectionKindV1;

typedef struct {
    CettaNikNativeSelectionStatusV1 status;
    CettaNikNativeSelectionKindV1 kind;
    size_t eligible_count;
    size_t frontier_count;
    /* Valid only for UNIQUE_GREATEST; otherwise SIZE_MAX. */
    size_t greatest_index;
} CettaNikNativeSelectionV1;

/* Select within the exact request fibre.  The output is deterministic in
 * family order and contains every maximal realization.  When capacity is
 * insufficient, no output element is written and frontier_count reports the
 * required capacity.  Selection never invokes a checker, chooses a policy,
 * or treats missing capability as a semantic verdict. */
CettaNikNativeSelectionV1 cetta_nik_native_calculus_select_v1(
    const CettaNikLicensedNativeFamilyV1 *family,
    const CettaNikNativeCapabilityRequestV1 *request,
    size_t *frontier_indices,
    size_t frontier_capacity);

#endif /* CETTA_NIK_NATIVE_CALCULUS_SELECTION_H */
