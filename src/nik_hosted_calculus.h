#ifndef CETTA_NIK_HOSTED_CALCULUS_H
#define CETTA_NIK_HOSTED_CALCULUS_H

#include "nik_direct_authority.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A native calculus is language-owned semantic structure.  NIK retains
 * stable identities for its judgments, evidence, computation, capabilities,
 * and operations without imposing a universal term or proof representation. */
typedef uint64_t CettaNikCalculusCapabilityIdV1;
typedef uint64_t CettaNikNativeOperationIdV1;

typedef struct {
    CettaNikNativeOperationIdV1 operation_identity;
} CettaNikNativeOperationV1;

typedef struct {
    const char *system_id;
    uint64_t theory_identity;
    uint32_t theory_revision;
    uint64_t calculus_identity;
    uint32_t calculus_revision;
    uint64_t judgment_family_identity;
    uint64_t evidence_family_identity;
    uint64_t computation_identity;
    /* Both inventories are strictly increasing and duplicate-free. */
    const CettaNikCalculusCapabilityIdV1 *capabilities;
    size_t capability_count;
    const CettaNikNativeOperationV1 *operations;
    size_t operation_count;
} CettaNikNativeCalculusV1;

bool cetta_nik_native_calculus_v1_is_valid(
    const CettaNikNativeCalculusV1 *calculus);

typedef struct {
    size_t weaker_index;
    size_t stronger_index;
} CettaNikNativeCalculusUpgradeV1;

/* Every member belongs to one theory revision.  Upgrade edges are admitted
 * semantic extensions between actual calculi, not backend preferences. */
typedef struct {
    const CettaNikNativeCalculusV1 *calculi;
    size_t calculus_count;
    const CettaNikNativeCalculusUpgradeV1 *upgrades;
    size_t upgrade_count;
} CettaNikNativeCalculusFamilyV1;

typedef struct {
    const CettaNikCalculusCapabilityIdV1 *required_capabilities;
    size_t required_capability_count;
} CettaNikNativeCalculusRequestV1;

typedef enum {
    CETTA_NIK_CALCULUS_SELECTION_STATUS_OK_V1 = 0,
    CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_FAMILY_V1,
    CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_REQUEST_V1,
    CETTA_NIK_CALCULUS_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1,
    CETTA_NIK_CALCULUS_SELECTION_STATUS_RESOURCE_FAULT_V1,
} CettaNikNativeCalculusSelectionStatusV1;

typedef enum {
    CETTA_NIK_CALCULUS_SELECTION_NONE_V1 = 0,
    CETTA_NIK_CALCULUS_SELECTION_UNIQUE_GREATEST_V1,
    CETTA_NIK_CALCULUS_SELECTION_MAXIMAL_FRONTIER_V1,
} CettaNikNativeCalculusSelectionKindV1;

typedef struct {
    CettaNikNativeCalculusSelectionStatusV1 status;
    CettaNikNativeCalculusSelectionKindV1 kind;
    size_t eligible_count;
    size_t frontier_count;
    size_t greatest_index;
} CettaNikNativeCalculusSelectionV1;

CettaNikNativeCalculusSelectionV1 cetta_nik_native_calculus_select_v1(
    const CettaNikNativeCalculusFamilyV1 *family,
    const CettaNikNativeCalculusRequestV1 *request,
    size_t *frontier_indices,
    size_t frontier_capacity);

/* The language owns source-scope and runtime state.  A snapshot carries only
 * exact identity/currentness data; it cannot contain a semantic verdict. */
typedef bool (*CettaNikScopeSnapshotV1)(
    const void *scope_state,
    CettaNikDirectAuthorityTokenV1 *token_out);

typedef bool (*CettaNikNativeOperationRunV1)(
    const void *implementation_context,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request,
    void *language_receipt_out,
    CettaNikResultV1 *result_out);

/* This license names an independently admitted correspondence between one
 * native calculus and one implementation.  NIK checks its identity and
 * currentness; the language-specific admission establishes its laws.  The
 * descriptor, calculus, authority, scope state, and implementation context
 * are borrowed and must outlive the hosted instance.  Operation requests and
 * language receipts are guest-shaped and may be null when the guest callback
 * defines a nullary operation or needs no guest receipt.  The callback treats
 * its implementation context as immutable and returns constructed state or
 * evidence through guest-owned outputs. */
typedef struct {
    const CettaNikNativeCalculusV1 *calculus;
    const CettaNikDirectAuthorityV1 *authority;
    uint64_t correspondence_identity;
    uint32_t correspondence_revision;
    uint32_t policy_identity;
    CettaNikScopeSnapshotV1 snapshot;
    CettaNikNativeOperationRunV1 run;
} CettaNikNativeImplementationLicenseV1;

bool cetta_nik_native_implementation_license_v1_is_valid(
    const CettaNikNativeImplementationLicenseV1 *license);

typedef struct CettaNikHostedNativeInstanceV1
    CettaNikHostedNativeInstanceV1;

typedef enum {
    CETTA_NIK_HOST_ADMISSION_ADMITTED_V1 = 0,
    CETTA_NIK_HOST_ADMISSION_INVALID_V1,
    CETTA_NIK_HOST_ADMISSION_STALE_V1,
    CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1,
} CettaNikHostAdmissionKindV1;

typedef struct {
    CettaNikHostAdmissionKindV1 kind;
    CettaNikHostedNativeInstanceV1 *instance;
} CettaNikHostedNativeAdmissionV1;

CettaNikHostedNativeAdmissionV1 cetta_nik_hosted_native_admit_v1(
    const CettaNikNativeImplementationLicenseV1 *license,
    const void *scope_state,
    const void *implementation_context);

void cetta_nik_hosted_native_destroy_v1(
    CettaNikHostedNativeInstanceV1 *instance);

bool cetta_nik_hosted_native_is_current_v1(
    const CettaNikHostedNativeInstanceV1 *instance);

typedef enum {
    CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 = 0,
    CETTA_NIK_HOSTED_NATIVE_CALL_INVALID_V1,
    CETTA_NIK_HOSTED_NATIVE_CALL_OUTSIDE_CALCULUS_V1,
    CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1,
    CETTA_NIK_HOSTED_NATIVE_CALL_ENGINE_FAULT_V1,
} CettaNikHostedNativeCallKindV1;

typedef struct {
    CettaNikHostedNativeCallKindV1 kind;
    CettaNikResultV1 result;
    uint64_t theory_identity;
    uint32_t theory_revision;
    uint64_t calculus_identity;
    uint32_t calculus_revision;
    uint64_t implementation_identity;
    uint32_t implementation_abi;
    uint64_t correspondence_identity;
    uint32_t correspondence_revision;
    uint32_t policy_identity;
    CettaNikNativeOperationIdV1 operation_identity;
    CettaNikDirectAuthorityTokenV1 authority;
} CettaNikHostedNativeReceiptV1;

CettaNikHostedNativeCallKindV1 cetta_nik_hosted_native_run_v1(
    CettaNikHostedNativeInstanceV1 *instance,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request,
    void *language_receipt_out,
    CettaNikHostedNativeReceiptV1 *receipt_out);

/* Certificate checking is deliberately a separate construction.  It checks
 * supplied evidence at a boundary and has no native-operation runner.  The
 * descriptor, calculus, authority, scope state, and checker state are
 * borrowed and must outlive the hosted boundary.  Claim, certificate, and
 * guest receipt shapes—including whether any are null—belong to the checker. */
typedef bool (*CettaNikCertificateCheckV1)(
    void *checker_state,
    const void *claim,
    const void *certificate,
    void *language_receipt_out,
    CettaNikResultV1 *result_out);

typedef struct {
    const CettaNikNativeCalculusV1 *calculus;
    const CettaNikDirectAuthorityV1 *authority;
    uint64_t adequacy_identity;
    uint32_t adequacy_revision;
    uint32_t policy_identity;
    CettaNikScopeSnapshotV1 snapshot;
    CettaNikCertificateCheckV1 check;
} CettaNikCertificateBoundaryV1;

bool cetta_nik_certificate_boundary_v1_is_valid(
    const CettaNikCertificateBoundaryV1 *boundary);

typedef struct CettaNikHostedCertificateBoundaryV1
    CettaNikHostedCertificateBoundaryV1;

typedef struct {
    CettaNikHostAdmissionKindV1 kind;
    CettaNikHostedCertificateBoundaryV1 *boundary;
} CettaNikHostedCertificateAdmissionV1;

CettaNikHostedCertificateAdmissionV1
cetta_nik_hosted_certificate_admit_v1(
    const CettaNikCertificateBoundaryV1 *boundary,
    const void *scope_state,
    void *checker_state);

void cetta_nik_hosted_certificate_destroy_v1(
    CettaNikHostedCertificateBoundaryV1 *boundary);

bool cetta_nik_hosted_certificate_is_current_v1(
    const CettaNikHostedCertificateBoundaryV1 *boundary);

typedef enum {
    CETTA_NIK_HOSTED_CERTIFICATE_CHECK_RESULT_V1 = 0,
    CETTA_NIK_HOSTED_CERTIFICATE_CHECK_INVALID_V1,
    CETTA_NIK_HOSTED_CERTIFICATE_CHECK_STALE_V1,
    CETTA_NIK_HOSTED_CERTIFICATE_CHECK_ENGINE_FAULT_V1,
} CettaNikHostedCertificateCheckKindV1;

typedef struct {
    CettaNikHostedCertificateCheckKindV1 kind;
    CettaNikResultV1 result;
    uint64_t theory_identity;
    uint32_t theory_revision;
    uint64_t calculus_identity;
    uint32_t calculus_revision;
    uint64_t boundary_identity;
    uint32_t boundary_abi;
    uint64_t adequacy_identity;
    uint32_t adequacy_revision;
    uint32_t policy_identity;
    CettaNikDirectAuthorityTokenV1 authority;
} CettaNikHostedCertificateReceiptV1;

CettaNikHostedCertificateCheckKindV1
cetta_nik_hosted_certificate_check_v1(
    CettaNikHostedCertificateBoundaryV1 *boundary,
    const void *claim,
    const void *certificate,
    void *language_receipt_out,
    CettaNikHostedCertificateReceiptV1 *receipt_out);

#endif /* CETTA_NIK_HOSTED_CALCULUS_H */
