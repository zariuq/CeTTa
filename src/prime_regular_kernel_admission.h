#ifndef CETTA_PRIME_REGULAR_KERNEL_ADMISSION_H
#define CETTA_PRIME_REGULAR_KERNEL_ADMISSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nik_direct_authority.h"
#include "prime_regular_kernel.h"
#include "space.h"
#include "term_universe.h"

#define CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_STAGE 0u
#define CETTA_PRIME_REGULAR_KERNEL_BETA_ETA_CONVERSION_PROFILE_V1 1u
#define CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_POLICY_V1 UINT32_C(0x524b4331)
#define CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_STAGE 0u
#define CETTA_PRIME_REGULAR_KERNEL_BIDIRECTIONAL_SYNTHESIS_PROFILE_V1 1u
#define CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_POLICY_V1 UINT32_C(0x524b5331)
#define CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_STAGE 0u
#define CETTA_PRIME_REGULAR_KERNEL_BIDIRECTIONAL_CHECKING_PROFILE_V1 1u
#define CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_POLICY_V1 UINT32_C(0x524b5431)

typedef struct {
    uint32_t stage;
    uint32_t conversion_profile;
} CettaPrimeRegularKernelConversionProfileV1;

extern const CettaPrimeRegularKernelConversionProfileV1
    cetta_prime_regular_kernel_closed_conversion_profile_v1;

/* Production folds this to true.  A separately compiled prime_semantics.c
 * test oracle folds it to false, retaining differential qualification of the
 * displaced route without leaving a production runtime bypass. */
#ifdef CETTA_PRIME_REGULAR_KERNEL_TEST_LEGACY_REFERENCE
#define CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE false
#else
#define CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE true
#endif

typedef struct {
    uint32_t stage;
    uint32_t synthesis_profile;
} CettaPrimeRegularKernelSynthesisProfileV1;

extern const CettaPrimeRegularKernelSynthesisProfileV1
    cetta_prime_regular_kernel_closed_synthesis_profile_v1;

typedef struct {
    uint32_t stage;
    uint32_t checking_profile;
} CettaPrimeRegularKernelCheckingProfileV1;

extern const CettaPrimeRegularKernelCheckingProfileV1
    cetta_prime_regular_kernel_closed_checking_profile_v1;

typedef struct CettaPrimeRegularKernelAdmittedConversionV1
    CettaPrimeRegularKernelAdmittedConversionV1;

typedef enum {
    CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT = 0,
    CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED,
    CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED,
    CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE,
    CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID
} CettaPrimeRegularKernelAdmissionStatus;

typedef struct {
    CettaPrimeRegularKernelAdmissionStatus status;
    CettaPrimeRegularKernelAdmittedConversionV1 *conversion;
    const char *reason;
} CettaPrimeRegularKernelAdmissionResult;

typedef struct {
    CettaPrimeRegularKernelAdmissionStatus status;
    bool equal;
    const char *reason;
} CettaPrimeRegularKernelAdmittedConversionDecisionV1;

typedef struct {
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId left_term_id;
    AtomId right_term_id;
    AtomId left_type_id;
    AtomId right_type_id;
    uint32_t stage;
    uint32_t conversion_profile;
} CettaPrimeRegularKernelAdmissionMetadataV1;

/* The source binding is an explicit cold-boundary input.  Production passes
 * the canonical generated binding; a copied or substituted receipt is not
 * accepted even if its descriptive strings happen to match. */
CettaPrimeRegularKernelAdmissionResult
cetta_prime_regular_kernel_admit_closed_conversion_v1(
    Arena *scratch, Space *space, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding);

/* Resolve and consume one admitted conversion.  A current cache hit reads the
 * private admitted record directly; it does not allocate a public handle or
 * repeat kernel work.  A miss uses the ordinary constructor below. */
CettaPrimeRegularKernelAdmittedConversionDecisionV1
cetta_prime_regular_kernel_resolve_closed_conversion_v1(
    Arena *scratch, Space *space, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding);

bool cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const Space *live_space,
    CettaPrimeRegularKernelConversionProfileV1 profile);

/* Consuming a current decision performs no synthesis, checking, conversion,
 * certificate replay, or source lookup. */
bool cetta_prime_regular_kernel_admitted_conversion_v1_decision(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const Space *live_space,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    bool *equal_out, const char **reason_out);

bool cetta_prime_regular_kernel_admitted_conversion_v1_metadata(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    CettaPrimeRegularKernelAdmissionMetadataV1 *metadata_out);

/* Erasure depends only on the interned storage generation, so a stale Space
 * revision can still recover the raw operands without authorizing execution. */
bool cetta_prime_regular_kernel_admitted_conversion_v1_erase(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const TermUniverse *live_universe, Arena *destination,
    Atom **left_out, Atom **right_out);

/* Handles are owned by the constructor's Arena.  Release is intentionally a
 * no-op and lets consumers use one cleanup path if ownership later changes. */
void cetta_prime_regular_kernel_admitted_conversion_v1_free(
    CettaPrimeRegularKernelAdmittedConversionV1 *conversion);

typedef struct CettaPrimeRegularKernelAdmittedSynthesisV1
    CettaPrimeRegularKernelAdmittedSynthesisV1;

typedef struct {
    CettaPrimeRegularKernelAdmissionStatus status;
    CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis;
    const char *reason;
} CettaPrimeRegularKernelSynthesisAdmissionResult;

typedef struct {
    CettaPrimeRegularKernelAdmissionStatus status;
    CettaPrimeRegularKernelStatus judgment_status;
    AtomId type_id;
    const char *reason;
} CettaPrimeRegularKernelAdmittedSynthesisDecisionV1;

typedef struct {
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId term_id;
    AtomId type_id;
    uint32_t stage;
    uint32_t synthesis_profile;
} CettaPrimeRegularKernelSynthesisMetadataV1;

CettaPrimeRegularKernelSynthesisAdmissionResult
cetta_prime_regular_kernel_admit_closed_synthesis_v1(
    Arena *scratch, Space *space, Atom *term,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding);

CettaPrimeRegularKernelAdmittedSynthesisDecisionV1
cetta_prime_regular_kernel_resolve_closed_synthesis_v1(
    Arena *scratch, Space *space, Atom *term,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding);

bool cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const Space *live_space,
    CettaPrimeRegularKernelSynthesisProfileV1 profile);

bool cetta_prime_regular_kernel_admitted_synthesis_v1_decision(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const Space *live_space,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    CettaPrimeRegularKernelStatus *judgment_status_out,
    AtomId *type_id_out, const char **reason_out);

bool cetta_prime_regular_kernel_admitted_synthesis_v1_metadata(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    CettaPrimeRegularKernelSynthesisMetadataV1 *metadata_out);

bool cetta_prime_regular_kernel_admitted_synthesis_v1_erase(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const TermUniverse *live_universe, Arena *destination,
    Atom **term_out);

void cetta_prime_regular_kernel_admitted_synthesis_v1_free(
    CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis);

typedef struct CettaPrimeRegularKernelAdmittedCheckingV1
    CettaPrimeRegularKernelAdmittedCheckingV1;

typedef struct {
    CettaPrimeRegularKernelAdmissionStatus status;
    CettaPrimeRegularKernelAdmittedCheckingV1 *checking;
    const char *reason;
} CettaPrimeRegularKernelCheckingAdmissionResult;

typedef struct {
    CettaPrimeRegularKernelAdmissionStatus status;
    CettaPrimeRegularKernelStatus judgment_status;
    const char *reason;
} CettaPrimeRegularKernelAdmittedCheckingDecisionV1;

typedef struct {
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId term_id;
    AtomId expected_type_id;
    uint32_t stage;
    uint32_t checking_profile;
} CettaPrimeRegularKernelCheckingMetadataV1;

typedef struct {
    Atom *term;
    Atom *expected_type;
} CettaPrimeRegularKernelCheckingCandidateV1;

typedef struct {
    CettaPrimeRegularKernelCheckingCandidateV1 candidate;
    CettaNikResultV1 result;
    CettaPrimeRegularKernelAdmittedCheckingV1 *checking;
    const char *reason;
} CettaPrimeRegularKernelCheckingOccurrenceV1;

typedef struct {
    size_t count;
    CettaPrimeRegularKernelCheckingOccurrenceV1 *occurrences;
    size_t established_count;
    size_t refuted_count;
    size_t undetermined_count;
    size_t incomplete_count;
    size_t engine_fault_count;
} CettaPrimeRegularKernelCheckingBagV1;

CettaPrimeRegularKernelCheckingAdmissionResult
cetta_prime_regular_kernel_admit_closed_checking_v1(
    Arena *scratch, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding);

CettaPrimeRegularKernelAdmittedCheckingDecisionV1
cetta_prime_regular_kernel_resolve_closed_checking_v1(
    Arena *scratch, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding);

/* Observe every occurrence exactly once, in input order.  Each candidate has
 * its own explicit budget cell; the function mutates those cells exactly as
 * the corresponding sequence of closed checking admissions would.  It never
 * evaluates, normalizes, deduplicates, or re-demands candidate terms. */
bool cetta_prime_regular_kernel_observe_closed_checking_bag_v1(
    Arena *scratch, Space *space,
    const CettaPrimeRegularKernelCheckingCandidateV1 *candidates,
    CettaPrimeRegularKernelBudget *budgets, size_t count,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    CettaPrimeRegularKernelCheckingBagV1 *bag_out);

bool cetta_prime_regular_kernel_checking_bag_v1_is_decision_complete(
    const CettaPrimeRegularKernelCheckingBagV1 *bag);

/* Exact multiset equality for candidate occurrences.  Both the term and its
 * expected type participate, and duplicate multiplicity is preserved. */
bool cetta_prime_regular_kernel_checking_candidate_bag_equal_v1(
    Arena *scratch,
    const CettaPrimeRegularKernelCheckingCandidateV1 *left,
    size_t left_count,
    const CettaPrimeRegularKernelCheckingCandidateV1 *right,
    size_t right_count);

/* Erase only established, current checking certificates.  Refutations and
 * open-coverage occurrences never become executable candidates.  The result
 * preserves the established occurrence order and exact duplicate count. */
bool cetta_prime_regular_kernel_checking_bag_v1_erase_established(
    const CettaPrimeRegularKernelCheckingBagV1 *bag,
    const Space *live_space,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    Arena *destination,
    CettaPrimeRegularKernelCheckingCandidateV1 **candidates_out,
    size_t *count_out);

bool cetta_prime_regular_kernel_admitted_checking_v1_is_current(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const Space *live_space,
    CettaPrimeRegularKernelCheckingProfileV1 profile);

bool cetta_prime_regular_kernel_admitted_checking_v1_decision(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const Space *live_space,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    CettaPrimeRegularKernelStatus *judgment_status_out,
    const char **reason_out);

bool cetta_prime_regular_kernel_admitted_checking_v1_metadata(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    CettaPrimeRegularKernelCheckingMetadataV1 *metadata_out);

bool cetta_prime_regular_kernel_admitted_checking_v1_erase(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const TermUniverse *live_universe, Arena *destination,
    Atom **term_out, Atom **expected_out);

void cetta_prime_regular_kernel_admitted_checking_v1_free(
    CettaPrimeRegularKernelAdmittedCheckingV1 *checking);

#endif /* CETTA_PRIME_REGULAR_KERNEL_ADMISSION_H */
