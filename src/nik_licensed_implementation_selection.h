#ifndef CETTA_NIK_LICENSED_IMPLEMENTATION_SELECTION_H
#define CETTA_NIK_LICENSED_IMPLEMENTATION_SELECTION_H

#include <stddef.h>
#include <stdint.h>

/* Capabilities are language-owned stable identities.  NIK compares only the
 * capabilities and admitted upgrade edges supplied by one exact licensed
 * implementation family; it assigns no global tier numbers. */
typedef uint64_t CettaNikImplementationCapabilityIdV1;

typedef struct {
    /* Selection is performed only after this native calculus is fixed. */
    uint64_t calculus_identity;
    uint64_t implementation_identity;
    /* Strictly increasing, duplicate-free capability identities. */
    const CettaNikImplementationCapabilityIdV1 *capabilities;
    size_t capability_count;
} CettaNikLicensedImplementationV1;

/* One already-admitted strict replacement relation.  Capability inclusion
 * is checked as a necessary law, but never used by itself to invent an
 * upgrade: semantic dominance must arrive as an explicit licensed edge. */
typedef struct {
    size_t weaker_index;
    size_t stronger_index;
} CettaNikLicensedImplementationUpgradeV1;

/* The caller supplies exactly the implementations licensed for one fixed
 * native calculus, semantic fragment, and dependency revision.  Selection
 * does not choose a calculus, create a license, or refresh one. */
typedef struct {
    const CettaNikLicensedImplementationV1 *implementations;
    size_t implementation_count;
    const CettaNikLicensedImplementationUpgradeV1 *upgrades;
    size_t upgrade_count;
} CettaNikLicensedImplementationFamilyV1;

typedef struct {
    /* Strictly increasing, duplicate-free capability identities. */
    const CettaNikImplementationCapabilityIdV1 *required_capabilities;
    size_t required_capability_count;
} CettaNikImplementationCapabilityRequestV1;

typedef enum {
    CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 = 0,
    CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1,
    CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_REQUEST_V1,
    CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1,
    CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_RESOURCE_FAULT_V1,
} CettaNikImplementationSelectionStatusV1;

typedef enum {
    /* No licensed implementation supports the exact request.  This requests
     * relational/raw fallback; it is not a refutation. */
    CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1 = 0,
    /* The request fibre has one implementation above every other candidate. */
    CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1,
    /* No greatest member exists; every incomparable maximal member is
     * returned and an explicit observation/cost policy must choose. */
    CETTA_NIK_IMPLEMENTATION_SELECTION_MAXIMAL_FRONTIER_V1,
} CettaNikImplementationSelectionKindV1;

typedef struct {
    CettaNikImplementationSelectionStatusV1 status;
    CettaNikImplementationSelectionKindV1 kind;
    size_t eligible_count;
    size_t frontier_count;
    /* Valid only for UNIQUE_GREATEST; otherwise SIZE_MAX. */
    size_t greatest_index;
} CettaNikImplementationSelectionV1;

/* Select within the exact request fibre.  The output is deterministic in
 * family order and contains every maximal implementation.  When capacity is
 * insufficient, no output element is written and frontier_count reports the
 * required capacity.  Selection never invokes a checker, chooses a policy,
 * or treats missing capability as a semantic verdict. */
CettaNikImplementationSelectionV1 cetta_nik_licensed_implementation_select_v1(
    const CettaNikLicensedImplementationFamilyV1 *family,
    const CettaNikImplementationCapabilityRequestV1 *request,
    size_t *frontier_indices,
    size_t frontier_capacity);

#endif /* CETTA_NIK_LICENSED_IMPLEMENTATION_SELECTION_H */
