#include "prime_regular_kernel_admission.h"

#include "generated/prime_typing_open_regular_kernel_source_binding_v1.generated.h"
#include "prime_semantics.h"
#include "stats.h"

const CettaPrimeRegularKernelConversionProfileV1
    cetta_prime_regular_kernel_closed_conversion_profile_v1 = {
        .stage = CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_STAGE,
        .conversion_profile =
            CETTA_PRIME_REGULAR_KERNEL_BETA_ETA_CONVERSION_PROFILE_V1,
    };

const CettaPrimeRegularKernelSynthesisProfileV1
    cetta_prime_regular_kernel_closed_synthesis_profile_v1 = {
        .stage = CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_STAGE,
        .synthesis_profile =
            CETTA_PRIME_REGULAR_KERNEL_BIDIRECTIONAL_SYNTHESIS_PROFILE_V1,
    };

const CettaPrimeRegularKernelCheckingProfileV1
    cetta_prime_regular_kernel_closed_checking_profile_v1 = {
        .stage = CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_STAGE,
        .checking_profile =
            CETTA_PRIME_REGULAR_KERNEL_BIDIRECTIONAL_CHECKING_PROFILE_V1,
    };

struct CettaPrimeRegularKernelAdmittedConversionV1 {
    const TermUniverse *universe;
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId left_term_id;
    AtomId right_term_id;
    AtomId left_type_id;
    AtomId right_type_id;
    CettaPrimeRegularKernelConversionProfileV1 profile;
    CettaNikDirectAuthorityTokenV1 authority_token;
    const CettaNikDirectSourceBindingV1 *source_binding;
    bool equal;
    const char *reason;
};

#define PRIME_REGULAR_KERNEL_CONVERSION_CACHE_CAPACITY 16u

typedef struct {
    CettaPrimeRegularKernelAdmittedConversionV1 entries[
        PRIME_REGULAR_KERNEL_CONVERSION_CACHE_CAPACITY];
    bool occupied[PRIME_REGULAR_KERNEL_CONVERSION_CACHE_CAPACITY];
    uint32_t next;
} PrimeRegularKernelConversionCache;

static _Thread_local PrimeRegularKernelConversionCache conversion_cache;

static bool profile_equal(
    CettaPrimeRegularKernelConversionProfileV1 left,
    CettaPrimeRegularKernelConversionProfileV1 right) {
    return left.stage == right.stage &&
           left.conversion_profile == right.conversion_profile;
}

static bool profile_supported(
    CettaPrimeRegularKernelConversionProfileV1 profile) {
    return profile_equal(
        profile, cetta_prime_regular_kernel_closed_conversion_profile_v1);
}

static CettaPrimeRegularKernelAdmissionResult admission_result(
    CettaPrimeRegularKernelAdmissionStatus status,
    CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const char *reason) {
    return (CettaPrimeRegularKernelAdmissionResult){
        .status = status,
        .conversion = conversion,
        .reason = reason,
    };
}

static bool cached_conversion_matches(
    const CettaPrimeRegularKernelAdmittedConversionV1 *cached,
    const TermUniverse *universe, AtomId left_id, AtomId right_id,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return cached && universe && token && cached->universe == universe &&
           cached->universe_instance_id == universe->instance_id &&
           cached->universe_storage_epoch == universe->storage_epoch &&
           cached->left_term_id == left_id &&
           cached->right_term_id == right_id &&
           profile_equal(cached->profile, profile) &&
           cached->source_binding == source_binding &&
           cetta_nik_direct_authority_token_v1_equal(
               &cached->authority_token, token);
}

static CettaPrimeRegularKernelAdmittedConversionV1 *cached_conversion_copy(
    Arena *owner, const TermUniverse *universe,
    AtomId left_id, AtomId right_id,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    for (uint32_t index = 0u;
         index < PRIME_REGULAR_KERNEL_CONVERSION_CACHE_CAPACITY; index++) {
        if (!conversion_cache.occupied[index] ||
            !cached_conversion_matches(
                &conversion_cache.entries[index], universe,
                left_id, right_id, profile, source_binding, token)) {
            continue;
        }
        CettaPrimeRegularKernelAdmittedConversionV1 *copy =
            arena_alloc(owner, sizeof(*copy));
        *copy = conversion_cache.entries[index];
        return copy;
    }
    return NULL;
}

static const CettaPrimeRegularKernelAdmittedConversionV1 *cached_conversion_find(
    const TermUniverse *universe, AtomId left_id, AtomId right_id,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    for (uint32_t index = 0u;
         index < PRIME_REGULAR_KERNEL_CONVERSION_CACHE_CAPACITY; index++) {
        if (conversion_cache.occupied[index] &&
            cached_conversion_matches(
                &conversion_cache.entries[index], universe,
                left_id, right_id, profile, source_binding, token)) {
            return &conversion_cache.entries[index];
        }
    }
    return NULL;
}

static void cache_conversion(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion) {
    uint32_t slot = conversion_cache.next;
    conversion_cache.entries[slot] = *conversion;
    conversion_cache.occupied[slot] = true;
    conversion_cache.next =
        (slot + 1u) % PRIME_REGULAR_KERNEL_CONVERSION_CACHE_CAPACITY;
}

CettaPrimeRegularKernelAdmissionResult
cetta_prime_regular_kernel_admit_closed_conversion_v1(
    Arena *scratch, Space *space, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ATTEMPT);
    if (!scratch || !space || !space->native.universe || !left || !right ||
        !profile_supported(profile)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "invalid-closed-conversion-admission-input");
    }
    if (source_binding !=
            &prime_typing_open_regular_kernel_source_binding_v1 ||
        source_binding->authority !=
            &cetta_prime_typing_direct_authority_v1) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "untrusted-regular-kernel-source-binding");
    }
    if (!cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(left) ||
        !cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(right)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_DECLINED);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            "outside-regular-kernel-root-syntax");
    }
    TermUniverse *universe = space->native.universe;
    uint64_t instance_before = universe->instance_id;
    uint64_t epoch_before = universe->storage_epoch;
    AtomId left_id = term_universe_store_atom_id(universe, scratch, left);
    AtomId right_id = term_universe_store_atom_id(universe, scratch, right);
    if (left_id == CETTA_ATOM_ID_NONE || right_id == CETTA_ATOM_ID_NONE) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-admission-interning-failed");
    }
    CettaNikDirectAuthorityTokenV1 token;
    if (!cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_POLICY_V1, &token)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-authority-changed-during-admission");
    }
    CettaPrimeRegularKernelAdmittedConversionV1 *cached =
        cached_conversion_copy(
            scratch, universe, left_id, right_id,
            profile, source_binding, &token);
    if (cached) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_CACHE_HIT);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ACCEPTED);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED, cached, NULL);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_CACHE_MISS);

    CettaPrimeRegularKernelResult left_class =
        cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
            left, budget);
    CettaPrimeRegularKernelResult right_class =
        left_class.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
            ? cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
                  right, budget)
            : left_class;
    if (left_class.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ||
        right_class.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_BUDGET_EXHAUSTED);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL,
            left_class.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                ? left_class.reason : right_class.reason);
    }
    if (left_class.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        right_class.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_DECLINED);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            left_class.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                ? left_class.reason : right_class.reason);
    }

    Atom *empty_context = atom_symbol(scratch, "PrimeCtxNil");
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_CHECK);
    CettaPrimeRegularKernelConversionDecision decision =
        cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
            scratch, empty_context, left, right, budget);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_BUDGET_EXHAUSTED);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL,
            decision.reason ? decision.reason : "regular-kernel-admission-budget");
    }
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ENGINE_FAILURE);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE, NULL,
            decision.reason ? decision.reason : "regular-kernel-engine-failure");
    }
    if (!decision.operands_admitted) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_DECLINED);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            decision.reason ? decision.reason : "outside-regular-kernel-fragment");
    }
    if (decision.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        decision.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "nondecisive-regular-kernel-admission");
    }

    AtomId context_id = term_universe_store_atom_id(
        universe, scratch, empty_context);
    AtomId left_type_id = decision.left_type
        ? term_universe_store_atom_id(universe, scratch, decision.left_type)
        : CETTA_ATOM_ID_NONE;
    AtomId right_type_id = decision.right_type
        ? term_universe_store_atom_id(universe, scratch, decision.right_type)
        : CETTA_ATOM_ID_NONE;
    if (context_id == CETTA_ATOM_ID_NONE ||
        left_id == CETTA_ATOM_ID_NONE || right_id == CETTA_ATOM_ID_NONE ||
        (decision.left_type && left_type_id == CETTA_ATOM_ID_NONE) ||
        (decision.right_type && right_type_id == CETTA_ATOM_ID_NONE)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-admission-interning-failed");
    }
    if (universe != space->native.universe ||
        instance_before != universe->instance_id ||
        epoch_before != universe->storage_epoch) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-universe-changed-during-admission");
    }

    if (!cetta_prime_typing_direct_authority_token_v1_is_current(
            &token, space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_POLICY_V1)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_INVALID);
        return admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-authority-changed-during-admission");
    }

    CettaPrimeRegularKernelAdmittedConversionV1 *conversion =
        arena_alloc(scratch, sizeof(*conversion));
    *conversion = (CettaPrimeRegularKernelAdmittedConversionV1){
        .universe = universe,
        .universe_instance_id = universe->instance_id,
        .universe_storage_epoch = universe->storage_epoch,
        .context_id = context_id,
        .left_term_id = left_id,
        .right_term_id = right_id,
        .left_type_id = left_type_id,
        .right_type_id = right_type_id,
        .profile = profile,
        .authority_token = token,
        .source_binding = source_binding,
        .equal = decision.equal,
        .reason = decision.reason,
    };
    cache_conversion(conversion);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ACCEPTED);
    return admission_result(
        CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED, conversion, NULL);
}

CettaPrimeRegularKernelAdmittedConversionDecisionV1
cetta_prime_regular_kernel_resolve_closed_conversion_v1(
    Arena *scratch, Space *space, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding) {
    if (scratch && space && space->native.universe && left && right &&
        profile_supported(profile) &&
        source_binding ==
            &prime_typing_open_regular_kernel_source_binding_v1 &&
        source_binding->authority ==
            &cetta_prime_typing_direct_authority_v1 &&
        cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(left) &&
        cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(right)) {
        TermUniverse *universe = space->native.universe;
        AtomId left_id = term_universe_lookup_atom_id(universe, left);
        AtomId right_id = term_universe_lookup_atom_id(universe, right);
        CettaNikDirectAuthorityTokenV1 token;
        if (left_id != CETTA_ATOM_ID_NONE &&
            right_id != CETTA_ATOM_ID_NONE &&
            cetta_prime_typing_direct_authority_token_v1(
                space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_POLICY_V1,
                &token)) {
            const CettaPrimeRegularKernelAdmittedConversionV1 *cached =
                cached_conversion_find(
                    universe, left_id, right_id, profile,
                    source_binding, &token);
            if (cached) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ATTEMPT);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_CACHE_HIT);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ACCEPTED);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_EXECUTION);
                return (CettaPrimeRegularKernelAdmittedConversionDecisionV1){
                    .status = CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED,
                    .equal = cached->equal,
                    .reason = cached->reason,
                };
            }
        }
    }

    CettaPrimeRegularKernelAdmissionResult admission =
        cetta_prime_regular_kernel_admit_closed_conversion_v1(
            scratch, space, left, right, budget, profile, source_binding);
    if (admission.status != CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED) {
        return (CettaPrimeRegularKernelAdmittedConversionDecisionV1){
            .status = admission.status,
            .reason = admission.reason,
        };
    }

    bool equal = false;
    const char *reason = NULL;
    bool current = cetta_prime_regular_kernel_admitted_conversion_v1_decision(
        admission.conversion, space, profile, &equal, &reason);
    cetta_prime_regular_kernel_admitted_conversion_v1_free(
        admission.conversion);
    return (CettaPrimeRegularKernelAdmittedConversionDecisionV1){
        .status = current ? CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED
                          : CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID,
        .equal = equal,
        .reason = current ? reason : "regular-kernel-admission-became-stale",
    };
}

bool cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const Space *live_space,
    CettaPrimeRegularKernelConversionProfileV1 profile) {
    if (!conversion || !live_space || !live_space->native.universe ||
        conversion->universe != live_space->native.universe ||
        conversion->universe_instance_id !=
            live_space->native.universe->instance_id ||
        conversion->universe_storage_epoch !=
            live_space->native.universe->storage_epoch ||
        !profile_equal(conversion->profile, profile) ||
        conversion->source_binding !=
            &prime_typing_open_regular_kernel_source_binding_v1) {
        return false;
    }
    return cetta_prime_typing_direct_authority_token_v1_is_current(
        &conversion->authority_token, live_space,
        CETTA_PRIME_REGULAR_KERNEL_CLOSED_CONVERSION_POLICY_V1);
}

bool cetta_prime_regular_kernel_admitted_conversion_v1_decision(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const Space *live_space,
    CettaPrimeRegularKernelConversionProfileV1 profile,
    bool *equal_out, const char **reason_out) {
    if (!cetta_prime_regular_kernel_admitted_conversion_v1_is_current(
            conversion, live_space, profile)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_STALE_FALLBACK);
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_EXECUTION);
    if (equal_out) *equal_out = conversion->equal;
    if (reason_out) *reason_out = conversion->reason;
    return true;
}

bool cetta_prime_regular_kernel_admitted_conversion_v1_metadata(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    CettaPrimeRegularKernelAdmissionMetadataV1 *metadata_out) {
    if (!conversion || !metadata_out) return false;
    *metadata_out = (CettaPrimeRegularKernelAdmissionMetadataV1){
        .universe_instance_id = conversion->universe_instance_id,
        .universe_storage_epoch = conversion->universe_storage_epoch,
        .context_id = conversion->context_id,
        .left_term_id = conversion->left_term_id,
        .right_term_id = conversion->right_term_id,
        .left_type_id = conversion->left_type_id,
        .right_type_id = conversion->right_type_id,
        .stage = conversion->profile.stage,
        .conversion_profile = conversion->profile.conversion_profile,
    };
    return true;
}

bool cetta_prime_regular_kernel_admitted_conversion_v1_erase(
    const CettaPrimeRegularKernelAdmittedConversionV1 *conversion,
    const TermUniverse *live_universe, Arena *destination,
    Atom **left_out, Atom **right_out) {
    if (!conversion || !live_universe || !destination ||
        conversion->universe != live_universe ||
        conversion->universe_instance_id != live_universe->instance_id ||
        conversion->universe_storage_epoch != live_universe->storage_epoch) {
        return false;
    }
    Atom *left = term_universe_copy_atom(
        live_universe, destination, conversion->left_term_id);
    Atom *right = term_universe_copy_atom(
        live_universe, destination, conversion->right_term_id);
    if (!left || !right) return false;
    if (left_out) *left_out = left;
    if (right_out) *right_out = right;
    return true;
}

void cetta_prime_regular_kernel_admitted_conversion_v1_free(
    CettaPrimeRegularKernelAdmittedConversionV1 *conversion) {
    (void)conversion;
}

struct CettaPrimeRegularKernelAdmittedSynthesisV1 {
    const TermUniverse *universe;
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId term_id;
    AtomId type_id;
    CettaPrimeRegularKernelSynthesisProfileV1 profile;
    CettaNikDirectAuthorityTokenV1 authority_token;
    const CettaNikDirectSourceBindingV1 *source_binding;
    CettaPrimeRegularKernelStatus judgment_status;
    const char *reason;
};

#define PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_CAPACITY 16u

typedef struct {
    CettaPrimeRegularKernelAdmittedSynthesisV1 entries[
        PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_CAPACITY];
    bool occupied[PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_CAPACITY];
    uint32_t next;
} PrimeRegularKernelSynthesisCache;

static _Thread_local PrimeRegularKernelSynthesisCache synthesis_cache;

static bool synthesis_profile_equal(
    CettaPrimeRegularKernelSynthesisProfileV1 left,
    CettaPrimeRegularKernelSynthesisProfileV1 right) {
    return left.stage == right.stage &&
           left.synthesis_profile == right.synthesis_profile;
}

static bool synthesis_profile_supported(
    CettaPrimeRegularKernelSynthesisProfileV1 profile) {
    return synthesis_profile_equal(
        profile, cetta_prime_regular_kernel_closed_synthesis_profile_v1);
}

static CettaPrimeRegularKernelSynthesisAdmissionResult synthesis_admission_result(
    CettaPrimeRegularKernelAdmissionStatus status,
    CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const char *reason) {
    return (CettaPrimeRegularKernelSynthesisAdmissionResult){
        .status = status,
        .synthesis = synthesis,
        .reason = reason,
    };
}

static bool cached_synthesis_matches(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *cached,
    const TermUniverse *universe, AtomId term_id,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return cached && universe && token && cached->universe == universe &&
           cached->universe_instance_id == universe->instance_id &&
           cached->universe_storage_epoch == universe->storage_epoch &&
           cached->term_id == term_id &&
           synthesis_profile_equal(cached->profile, profile) &&
           cached->source_binding == source_binding &&
           cetta_nik_direct_authority_token_v1_equal(
               &cached->authority_token, token);
}

static const CettaPrimeRegularKernelAdmittedSynthesisV1 *cached_synthesis_find(
    const TermUniverse *universe, AtomId term_id,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    for (uint32_t index = 0u;
         index < PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_CAPACITY; index++) {
        if (synthesis_cache.occupied[index] &&
            cached_synthesis_matches(
                &synthesis_cache.entries[index], universe, term_id,
                profile, source_binding, token)) {
            return &synthesis_cache.entries[index];
        }
    }
    return NULL;
}

static CettaPrimeRegularKernelAdmittedSynthesisV1 *cached_synthesis_copy(
    Arena *owner, const TermUniverse *universe, AtomId term_id,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *cached =
        cached_synthesis_find(
            universe, term_id, profile, source_binding, token);
    if (!cached) return NULL;
    CettaPrimeRegularKernelAdmittedSynthesisV1 *copy =
        arena_alloc(owner, sizeof(*copy));
    *copy = *cached;
    return copy;
}

static void cache_synthesis(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis) {
    uint32_t slot = synthesis_cache.next;
    synthesis_cache.entries[slot] = *synthesis;
    synthesis_cache.occupied[slot] = true;
    synthesis_cache.next =
        (slot + 1u) % PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_CAPACITY;
}

CettaPrimeRegularKernelSynthesisAdmissionResult
cetta_prime_regular_kernel_admit_closed_synthesis_v1(
    Arena *scratch, Space *space, Atom *term,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ATTEMPT);
    if (!scratch || !space || !space->native.universe || !term ||
        !synthesis_profile_supported(profile)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_INVALID);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "invalid-closed-synthesis-admission-input");
    }
    if (source_binding !=
            &prime_typing_open_regular_kernel_source_binding_v1 ||
        source_binding->authority !=
            &cetta_prime_typing_direct_authority_v1) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_INVALID);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "untrusted-regular-kernel-source-binding");
    }
    if (!cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(term)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_DECLINED);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            "outside-regular-kernel-root-syntax");
    }

    TermUniverse *universe = space->native.universe;
    uint64_t instance_before = universe->instance_id;
    uint64_t epoch_before = universe->storage_epoch;
    AtomId term_id = term_universe_store_atom_id(universe, scratch, term);
    CettaNikDirectAuthorityTokenV1 token;
    if (term_id == CETTA_ATOM_ID_NONE ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_POLICY_V1, &token)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_INVALID);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-synthesis-authority-or-interning-failed");
    }
    CettaPrimeRegularKernelAdmittedSynthesisV1 *cached =
        cached_synthesis_copy(
            scratch, universe, term_id, profile, source_binding, &token);
    if (cached) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_HIT);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ACCEPTED);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED, cached, NULL);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_MISS);

    CettaPrimeRegularKernelResult classification =
        cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
            term, budget);
    if (classification.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_BUDGET_EXHAUSTED);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL,
            classification.reason);
    }
    if (classification.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_DECLINED);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            classification.reason);
    }

    Atom *empty_context = atom_symbol(scratch, "PrimeCtxNil");
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_CHECK);
    CettaPrimeRegularKernelResult result =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            scratch, empty_context, term, budget);
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_BUDGET_EXHAUSTED);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL, result.reason);
    }
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ENGINE_FAILURE);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE, NULL,
            result.reason ? result.reason : "regular-kernel-engine-failure");
    }
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS ||
        result.status == CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_DECLINED);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            result.reason ? result.reason
                          : "outside-regular-kernel-synthesis");
    }
    if (result.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        result.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_INVALID);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "nondecisive-regular-kernel-synthesis");
    }

    AtomId context_id = term_universe_store_atom_id(
        universe, scratch, empty_context);
    AtomId type_id = result.type
        ? term_universe_store_atom_id(universe, scratch, result.type)
        : CETTA_ATOM_ID_NONE;
    if (context_id == CETTA_ATOM_ID_NONE ||
        (result.type && type_id == CETTA_ATOM_ID_NONE) ||
        universe != space->native.universe ||
        instance_before != universe->instance_id ||
        epoch_before != universe->storage_epoch ||
        !cetta_prime_typing_direct_authority_token_v1_is_current(
            &token, space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_POLICY_V1)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_INVALID);
        return synthesis_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-synthesis-authority-changed-during-admission");
    }

    CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis =
        arena_alloc(scratch, sizeof(*synthesis));
    *synthesis = (CettaPrimeRegularKernelAdmittedSynthesisV1){
        .universe = universe,
        .universe_instance_id = universe->instance_id,
        .universe_storage_epoch = universe->storage_epoch,
        .context_id = context_id,
        .term_id = term_id,
        .type_id = type_id,
        .profile = profile,
        .authority_token = token,
        .source_binding = source_binding,
        .judgment_status = result.status,
        .reason = result.reason,
    };
    cache_synthesis(synthesis);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ACCEPTED);
    return synthesis_admission_result(
        CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED, synthesis, NULL);
}

bool cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const Space *live_space,
    CettaPrimeRegularKernelSynthesisProfileV1 profile) {
    if (!synthesis || !live_space || !live_space->native.universe ||
        synthesis->universe != live_space->native.universe ||
        synthesis->universe_instance_id !=
            live_space->native.universe->instance_id ||
        synthesis->universe_storage_epoch !=
            live_space->native.universe->storage_epoch ||
        !synthesis_profile_equal(synthesis->profile, profile) ||
        synthesis->source_binding !=
            &prime_typing_open_regular_kernel_source_binding_v1) {
        return false;
    }
    return cetta_prime_typing_direct_authority_token_v1_is_current(
        &synthesis->authority_token, live_space,
        CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_POLICY_V1);
}

bool cetta_prime_regular_kernel_admitted_synthesis_v1_decision(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const Space *live_space,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    CettaPrimeRegularKernelStatus *judgment_status_out,
    AtomId *type_id_out, const char **reason_out) {
    if (!cetta_prime_regular_kernel_admitted_synthesis_v1_is_current(
            synthesis, live_space, profile)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_STALE_FALLBACK);
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_EXECUTION);
    if (judgment_status_out)
        *judgment_status_out = synthesis->judgment_status;
    if (type_id_out) *type_id_out = synthesis->type_id;
    if (reason_out) *reason_out = synthesis->reason;
    return true;
}

CettaPrimeRegularKernelAdmittedSynthesisDecisionV1
cetta_prime_regular_kernel_resolve_closed_synthesis_v1(
    Arena *scratch, Space *space, Atom *term,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelSynthesisProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding) {
    if (scratch && space && space->native.universe && term &&
        synthesis_profile_supported(profile) &&
        source_binding ==
            &prime_typing_open_regular_kernel_source_binding_v1 &&
        source_binding->authority ==
            &cetta_prime_typing_direct_authority_v1 &&
        cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(term)) {
        TermUniverse *universe = space->native.universe;
        AtomId term_id = term_universe_lookup_atom_id(universe, term);
        CettaNikDirectAuthorityTokenV1 token;
        if (term_id != CETTA_ATOM_ID_NONE &&
            cetta_prime_typing_direct_authority_token_v1(
                space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_SYNTHESIS_POLICY_V1,
                &token)) {
            const CettaPrimeRegularKernelAdmittedSynthesisV1 *cached =
                cached_synthesis_find(
                    universe, term_id, profile, source_binding, &token);
            if (cached) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ATTEMPT);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_CACHE_HIT);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_ADMISSION_ACCEPTED);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_SYNTHESIS_EXECUTION);
                return (CettaPrimeRegularKernelAdmittedSynthesisDecisionV1){
                    .status = CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED,
                    .judgment_status = cached->judgment_status,
                    .type_id = cached->type_id,
                    .reason = cached->reason,
                };
            }
        }
    }

    CettaPrimeRegularKernelSynthesisAdmissionResult admission =
        cetta_prime_regular_kernel_admit_closed_synthesis_v1(
            scratch, space, term, budget, profile, source_binding);
    if (admission.status != CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED) {
        return (CettaPrimeRegularKernelAdmittedSynthesisDecisionV1){
            .status = admission.status,
            .reason = admission.reason,
        };
    }
    AtomId type_id = CETTA_ATOM_ID_NONE;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus judgment_status = CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    bool current = cetta_prime_regular_kernel_admitted_synthesis_v1_decision(
        admission.synthesis, space, profile, &judgment_status,
        &type_id, &reason);
    cetta_prime_regular_kernel_admitted_synthesis_v1_free(
        admission.synthesis);
    return (CettaPrimeRegularKernelAdmittedSynthesisDecisionV1){
        .status = current ? CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED
                          : CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID,
        .judgment_status = judgment_status,
        .type_id = type_id,
        .reason = current ? reason : "regular-kernel-synthesis-became-stale",
    };
}

bool cetta_prime_regular_kernel_admitted_synthesis_v1_metadata(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    CettaPrimeRegularKernelSynthesisMetadataV1 *metadata_out) {
    if (!synthesis || !metadata_out) return false;
    *metadata_out = (CettaPrimeRegularKernelSynthesisMetadataV1){
        .universe_instance_id = synthesis->universe_instance_id,
        .universe_storage_epoch = synthesis->universe_storage_epoch,
        .context_id = synthesis->context_id,
        .term_id = synthesis->term_id,
        .type_id = synthesis->type_id,
        .stage = synthesis->profile.stage,
        .synthesis_profile = synthesis->profile.synthesis_profile,
    };
    return true;
}

bool cetta_prime_regular_kernel_admitted_synthesis_v1_erase(
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis,
    const TermUniverse *live_universe, Arena *destination,
    Atom **term_out) {
    if (!synthesis || !live_universe || !destination ||
        synthesis->universe != live_universe ||
        synthesis->universe_instance_id != live_universe->instance_id ||
        synthesis->universe_storage_epoch != live_universe->storage_epoch) {
        return false;
    }
    Atom *term = term_universe_copy_atom(
        live_universe, destination, synthesis->term_id);
    if (!term) return false;
    if (term_out) *term_out = term;
    return true;
}

void cetta_prime_regular_kernel_admitted_synthesis_v1_free(
    CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis) {
    (void)synthesis;
}

struct CettaPrimeRegularKernelAdmittedCheckingV1 {
    const TermUniverse *universe;
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId term_id;
    AtomId expected_type_id;
    CettaPrimeRegularKernelCheckingProfileV1 profile;
    CettaNikDirectAuthorityTokenV1 authority_token;
    const CettaNikDirectSourceBindingV1 *source_binding;
    CettaPrimeRegularKernelStatus judgment_status;
    const char *reason;
};

#define PRIME_REGULAR_KERNEL_CHECKING_CACHE_CAPACITY 16u

typedef struct {
    CettaPrimeRegularKernelAdmittedCheckingV1 entries[
        PRIME_REGULAR_KERNEL_CHECKING_CACHE_CAPACITY];
    bool occupied[PRIME_REGULAR_KERNEL_CHECKING_CACHE_CAPACITY];
    uint32_t next;
} PrimeRegularKernelCheckingCache;

static _Thread_local PrimeRegularKernelCheckingCache checking_cache;

static bool checking_profile_equal(
    CettaPrimeRegularKernelCheckingProfileV1 left,
    CettaPrimeRegularKernelCheckingProfileV1 right) {
    return left.stage == right.stage &&
           left.checking_profile == right.checking_profile;
}

static bool checking_profile_supported(
    CettaPrimeRegularKernelCheckingProfileV1 profile) {
    return checking_profile_equal(
        profile, cetta_prime_regular_kernel_closed_checking_profile_v1);
}

static CettaPrimeRegularKernelCheckingAdmissionResult checking_admission_result(
    CettaPrimeRegularKernelAdmissionStatus status,
    CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const char *reason) {
    return (CettaPrimeRegularKernelCheckingAdmissionResult){
        .status = status,
        .checking = checking,
        .reason = reason,
    };
}

static bool cached_checking_matches(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *cached,
    const TermUniverse *universe, AtomId term_id, AtomId expected_type_id,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return cached && universe && token && cached->universe == universe &&
           cached->universe_instance_id == universe->instance_id &&
           cached->universe_storage_epoch == universe->storage_epoch &&
           cached->term_id == term_id &&
           cached->expected_type_id == expected_type_id &&
           checking_profile_equal(cached->profile, profile) &&
           cached->source_binding == source_binding &&
           cetta_nik_direct_authority_token_v1_equal(
               &cached->authority_token, token);
}

static const CettaPrimeRegularKernelAdmittedCheckingV1 *cached_checking_find(
    const TermUniverse *universe, AtomId term_id, AtomId expected_type_id,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    for (uint32_t index = 0u;
         index < PRIME_REGULAR_KERNEL_CHECKING_CACHE_CAPACITY; index++) {
        if (checking_cache.occupied[index] &&
            cached_checking_matches(
                &checking_cache.entries[index], universe, term_id,
                expected_type_id, profile, source_binding, token)) {
            return &checking_cache.entries[index];
        }
    }
    return NULL;
}

static CettaPrimeRegularKernelAdmittedCheckingV1 *cached_checking_copy(
    Arena *owner, const TermUniverse *universe,
    AtomId term_id, AtomId expected_type_id,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    const CettaNikDirectAuthorityTokenV1 *token) {
    const CettaPrimeRegularKernelAdmittedCheckingV1 *cached =
        cached_checking_find(
            universe, term_id, expected_type_id,
            profile, source_binding, token);
    if (!cached) return NULL;
    CettaPrimeRegularKernelAdmittedCheckingV1 *copy =
        arena_alloc(owner, sizeof(*copy));
    *copy = *cached;
    return copy;
}

static void cache_checking(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking) {
    uint32_t slot = checking_cache.next;
    checking_cache.entries[slot] = *checking;
    checking_cache.occupied[slot] = true;
    checking_cache.next =
        (slot + 1u) % PRIME_REGULAR_KERNEL_CHECKING_CACHE_CAPACITY;
}

CettaPrimeRegularKernelCheckingAdmissionResult
cetta_prime_regular_kernel_admit_closed_checking_v1(
    Arena *scratch, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ATTEMPT);
    if (!scratch || !space || !space->native.universe || !term ||
        !expected || !checking_profile_supported(profile)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_INVALID);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "invalid-closed-checking-admission-input");
    }
    if (source_binding !=
            &prime_typing_open_regular_kernel_source_binding_v1 ||
        source_binding->authority !=
            &cetta_prime_typing_direct_authority_v1) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_INVALID);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "untrusted-regular-kernel-source-binding");
    }
    if (!cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(term) ||
        !cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(expected)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_DECLINED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            "outside-regular-kernel-root-syntax");
    }

    TermUniverse *universe = space->native.universe;
    uint64_t instance_before = universe->instance_id;
    uint64_t epoch_before = universe->storage_epoch;
    AtomId term_id = term_universe_store_atom_id(universe, scratch, term);
    AtomId expected_type_id =
        term_universe_store_atom_id(universe, scratch, expected);
    CettaNikDirectAuthorityTokenV1 token;
    if (term_id == CETTA_ATOM_ID_NONE ||
        expected_type_id == CETTA_ATOM_ID_NONE ||
        !cetta_prime_typing_direct_authority_token_v1(
            space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_POLICY_V1, &token)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_INVALID);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-checking-authority-or-interning-failed");
    }
    CettaPrimeRegularKernelAdmittedCheckingV1 *cached =
        cached_checking_copy(
            scratch, universe, term_id, expected_type_id,
            profile, source_binding, &token);
    if (cached) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_CACHE_HIT);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ACCEPTED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED, cached, NULL);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_CACHE_MISS);

    CettaPrimeRegularKernelResult term_class =
        cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
            term, budget);
    if (term_class.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_BUDGET_EXHAUSTED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL,
            term_class.reason);
    }
    if (term_class.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_DECLINED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL, term_class.reason);
    }
    CettaPrimeRegularKernelResult expected_class =
        cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
            expected, budget);
    if (expected_class.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_BUDGET_EXHAUSTED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL,
            expected_class.reason);
    }
    if (expected_class.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_DECLINED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            expected_class.reason);
    }

    Atom *empty_context = atom_symbol(scratch, "PrimeCtxNil");
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_CHECK);
    CettaPrimeRegularKernelResult result =
        cetta_prime_regular_kernel_check_intrinsic(
            scratch, empty_context, term, expected, budget);
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_BUDGET_EXHAUSTED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED, NULL, result.reason);
    }
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ENGINE_FAILURE);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE, NULL,
            result.reason ? result.reason : "regular-kernel-engine-failure");
    }
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS ||
        result.status == CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_DECLINED);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT, NULL,
            result.reason ? result.reason
                          : "outside-regular-kernel-checking");
    }
    if (result.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        result.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_INVALID);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "nondecisive-regular-kernel-checking");
    }

    AtomId context_id = term_universe_store_atom_id(
        universe, scratch, empty_context);
    if (context_id == CETTA_ATOM_ID_NONE ||
        universe != space->native.universe ||
        instance_before != universe->instance_id ||
        epoch_before != universe->storage_epoch ||
        !cetta_prime_typing_direct_authority_token_v1_is_current(
            &token, space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_POLICY_V1)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_INVALID);
        return checking_admission_result(
            CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID, NULL,
            "regular-kernel-checking-authority-changed-during-admission");
    }

    CettaPrimeRegularKernelAdmittedCheckingV1 *checking =
        arena_alloc(scratch, sizeof(*checking));
    *checking = (CettaPrimeRegularKernelAdmittedCheckingV1){
        .universe = universe,
        .universe_instance_id = universe->instance_id,
        .universe_storage_epoch = universe->storage_epoch,
        .context_id = context_id,
        .term_id = term_id,
        .expected_type_id = expected_type_id,
        .profile = profile,
        .authority_token = token,
        .source_binding = source_binding,
        .judgment_status = result.status,
        .reason = result.reason,
    };
    cache_checking(checking);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ACCEPTED);
    return checking_admission_result(
        CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED, checking, NULL);
}

bool cetta_prime_regular_kernel_admitted_checking_v1_is_current(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const Space *live_space,
    CettaPrimeRegularKernelCheckingProfileV1 profile) {
    if (!checking || !live_space || !live_space->native.universe ||
        checking->universe != live_space->native.universe ||
        checking->universe_instance_id !=
            live_space->native.universe->instance_id ||
        checking->universe_storage_epoch !=
            live_space->native.universe->storage_epoch ||
        !checking_profile_equal(checking->profile, profile) ||
        checking->source_binding !=
            &prime_typing_open_regular_kernel_source_binding_v1) {
        return false;
    }
    return cetta_prime_typing_direct_authority_token_v1_is_current(
        &checking->authority_token, live_space,
        CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_POLICY_V1);
}

bool cetta_prime_regular_kernel_admitted_checking_v1_decision(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const Space *live_space,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    CettaPrimeRegularKernelStatus *judgment_status_out,
    const char **reason_out) {
    if (!cetta_prime_regular_kernel_admitted_checking_v1_is_current(
            checking, live_space, profile)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_STALE_FALLBACK);
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_EXECUTION);
    if (judgment_status_out)
        *judgment_status_out = checking->judgment_status;
    if (reason_out) *reason_out = checking->reason;
    return true;
}

CettaPrimeRegularKernelAdmittedCheckingDecisionV1
cetta_prime_regular_kernel_resolve_closed_checking_v1(
    Arena *scratch, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding) {
    if (scratch && space && space->native.universe && term && expected &&
        checking_profile_supported(profile) &&
        source_binding ==
            &prime_typing_open_regular_kernel_source_binding_v1 &&
        source_binding->authority ==
            &cetta_prime_typing_direct_authority_v1 &&
        cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(term) &&
        cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(expected)) {
        TermUniverse *universe = space->native.universe;
        AtomId term_id = term_universe_lookup_atom_id(universe, term);
        AtomId expected_type_id =
            term_universe_lookup_atom_id(universe, expected);
        CettaNikDirectAuthorityTokenV1 token;
        if (term_id != CETTA_ATOM_ID_NONE &&
            expected_type_id != CETTA_ATOM_ID_NONE &&
            cetta_prime_typing_direct_authority_token_v1(
                space, CETTA_PRIME_REGULAR_KERNEL_CLOSED_CHECKING_POLICY_V1,
                &token)) {
            const CettaPrimeRegularKernelAdmittedCheckingV1 *cached =
                cached_checking_find(
                    universe, term_id, expected_type_id,
                    profile, source_binding, &token);
            if (cached) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ATTEMPT);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_CACHE_HIT);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_ADMISSION_ACCEPTED);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CHECKING_EXECUTION);
                return (CettaPrimeRegularKernelAdmittedCheckingDecisionV1){
                    .status = CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED,
                    .judgment_status = cached->judgment_status,
                    .reason = cached->reason,
                };
            }
        }
    }

    CettaPrimeRegularKernelCheckingAdmissionResult admission =
        cetta_prime_regular_kernel_admit_closed_checking_v1(
            scratch, space, term, expected, budget,
            profile, source_binding);
    if (admission.status != CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED) {
        return (CettaPrimeRegularKernelAdmittedCheckingDecisionV1){
            .status = admission.status,
            .reason = admission.reason,
        };
    }
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus judgment_status = CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    bool current = cetta_prime_regular_kernel_admitted_checking_v1_decision(
        admission.checking, space, profile, &judgment_status, &reason);
    cetta_prime_regular_kernel_admitted_checking_v1_free(admission.checking);
    return (CettaPrimeRegularKernelAdmittedCheckingDecisionV1){
        .status = current ? CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED
                          : CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID,
        .judgment_status = judgment_status,
        .reason = current ? reason : "regular-kernel-checking-became-stale",
    };
}

bool cetta_prime_regular_kernel_observe_closed_checking_bag_v1(
    Arena *scratch, Space *space,
    const CettaPrimeRegularKernelCheckingCandidateV1 *candidates,
    CettaPrimeRegularKernelBudget *budgets, size_t count,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    const CettaNikDirectSourceBindingV1 *source_binding,
    CettaPrimeRegularKernelCheckingBagV1 *bag_out) {
    if (!bag_out) return false;
    *bag_out = (CettaPrimeRegularKernelCheckingBagV1){0};
    if (!scratch || !space || !source_binding ||
        !checking_profile_supported(profile) ||
        (count != 0u && (!candidates || !budgets)) ||
        count > SIZE_MAX / sizeof(*bag_out->occurrences)) {
        return false;
    }

    CettaPrimeRegularKernelCheckingOccurrenceV1 *occurrences = count
        ? arena_alloc(scratch, sizeof(*occurrences) * count)
        : NULL;
    for (size_t index = 0u; index < count; index++) {
        if (!candidates[index].term || !candidates[index].expected_type)
            return false;
        occurrences[index] = (CettaPrimeRegularKernelCheckingOccurrenceV1){
            .candidate = candidates[index],
            .result = cetta_nik_result_v1_outcome(
                CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT),
        };

        CettaPrimeRegularKernelCheckingAdmissionResult admission =
            cetta_prime_regular_kernel_admit_closed_checking_v1(
                scratch, space, candidates[index].term,
                candidates[index].expected_type, &budgets[index], profile,
                source_binding);
        occurrences[index].reason = admission.reason;
        switch (admission.status) {
        case CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ADMITTED: {
            CettaPrimeRegularKernelStatus judgment_status =
                CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
            const char *reason = NULL;
            if (!admission.checking ||
                !cetta_prime_regular_kernel_admitted_checking_v1_decision(
                    admission.checking, space, profile,
                    &judgment_status, &reason)) {
                return false;
            }
            occurrences[index].checking = admission.checking;
            occurrences[index].reason = reason;
            if (judgment_status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
                occurrences[index].result = cetta_nik_result_v1_outcome(
                    CETTA_NIK_OUTCOME_ESTABLISHED);
                bag_out->established_count++;
            } else if (judgment_status ==
                       CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
                occurrences[index].result = cetta_nik_result_v1_outcome(
                    CETTA_NIK_OUTCOME_REFUTED);
                bag_out->refuted_count++;
            } else {
                return false;
            }
            break;
        }
        case CETTA_PRIME_REGULAR_KERNEL_ADMISSION_NOT_FRAGMENT:
            occurrences[index].result = cetta_nik_result_v1_outcome(
                CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT);
            bag_out->undetermined_count++;
            break;
        case CETTA_PRIME_REGULAR_KERNEL_ADMISSION_BUDGET_EXHAUSTED:
            occurrences[index].result = cetta_nik_result_v1_outcome(
                CETTA_NIK_OUTCOME_INCOMPLETE);
            bag_out->incomplete_count++;
            break;
        case CETTA_PRIME_REGULAR_KERNEL_ADMISSION_ENGINE_FAILURE:
            occurrences[index].result = cetta_nik_result_v1_engine_fault(
                CETTA_NIK_ENGINE_FAULT_UNAVAILABLE);
            bag_out->engine_fault_count++;
            break;
        case CETTA_PRIME_REGULAR_KERNEL_ADMISSION_INVALID:
            return false;
        }
    }

    bag_out->count = count;
    bag_out->occurrences = occurrences;
    return bag_out->established_count + bag_out->refuted_count +
               bag_out->undetermined_count + bag_out->incomplete_count +
               bag_out->engine_fault_count ==
           count;
}

bool cetta_prime_regular_kernel_checking_bag_v1_is_decision_complete(
    const CettaPrimeRegularKernelCheckingBagV1 *bag) {
    return bag && bag->undetermined_count == 0u &&
           bag->incomplete_count == 0u && bag->engine_fault_count == 0u &&
           bag->established_count + bag->refuted_count == bag->count;
}

static bool checking_candidate_equal(
    const CettaPrimeRegularKernelCheckingCandidateV1 *left,
    const CettaPrimeRegularKernelCheckingCandidateV1 *right) {
    return left && right && left->term && right->term &&
           left->expected_type && right->expected_type &&
           atom_eq(left->term, right->term) &&
           atom_eq(left->expected_type, right->expected_type);
}

bool cetta_prime_regular_kernel_checking_candidate_bag_equal_v1(
    Arena *scratch,
    const CettaPrimeRegularKernelCheckingCandidateV1 *left,
    size_t left_count,
    const CettaPrimeRegularKernelCheckingCandidateV1 *right,
    size_t right_count) {
    if (!scratch || left_count != right_count ||
        (left_count != 0u && (!left || !right))) {
        return false;
    }
    if (left_count == 0u) return true;
    bool *matched = arena_alloc(scratch, sizeof(*matched) * right_count);
    for (size_t index = 0u; index < right_count; index++)
        matched[index] = false;
    for (size_t left_index = 0u; left_index < left_count; left_index++) {
        bool found = false;
        for (size_t right_index = 0u;
             right_index < right_count; right_index++) {
            if (!matched[right_index] &&
                checking_candidate_equal(
                    &left[left_index], &right[right_index])) {
                matched[right_index] = true;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool cetta_prime_regular_kernel_checking_bag_v1_erase_established(
    const CettaPrimeRegularKernelCheckingBagV1 *bag,
    const Space *live_space,
    CettaPrimeRegularKernelCheckingProfileV1 profile,
    Arena *destination,
    CettaPrimeRegularKernelCheckingCandidateV1 **candidates_out,
    size_t *count_out) {
    if (candidates_out) *candidates_out = NULL;
    if (count_out) *count_out = 0u;
    if (!bag || !live_space || !live_space->native.universe ||
        !destination || !candidates_out || !count_out ||
        (bag->count != 0u && !bag->occurrences) ||
        bag->established_count >
            SIZE_MAX / sizeof(CettaPrimeRegularKernelCheckingCandidateV1)) {
        return false;
    }

    CettaPrimeRegularKernelCheckingCandidateV1 *erased =
        bag->established_count
            ? arena_alloc(destination, sizeof(*erased) * bag->established_count)
            : NULL;
    size_t write = 0u;
    for (size_t index = 0u; index < bag->count; index++) {
        const CettaPrimeRegularKernelCheckingOccurrenceV1 *occurrence =
            &bag->occurrences[index];
        if (!cetta_nik_result_v1_is_valid(occurrence->result))
            return false;
        if (occurrence->result.kind == CETTA_NIK_RESULT_OUTCOME &&
            occurrence->result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED) {
            if (!occurrence->checking || write >= bag->established_count ||
                !cetta_prime_regular_kernel_admitted_checking_v1_is_current(
                    occurrence->checking, live_space, profile) ||
                !cetta_prime_regular_kernel_admitted_checking_v1_erase(
                    occurrence->checking, live_space->native.universe,
                    destination, &erased[write].term,
                    &erased[write].expected_type)) {
                return false;
            }
            write++;
        } else if (occurrence->result.kind == CETTA_NIK_RESULT_OUTCOME &&
                   occurrence->result.value.outcome ==
                       CETTA_NIK_OUTCOME_REFUTED) {
            if (!occurrence->checking) {
                return false;
            }
        } else if (occurrence->checking) {
            return false;
        }
    }
    if (write != bag->established_count) return false;
    *candidates_out = erased;
    *count_out = write;
    return true;
}

bool cetta_prime_regular_kernel_admitted_checking_v1_metadata(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    CettaPrimeRegularKernelCheckingMetadataV1 *metadata_out) {
    if (!checking || !metadata_out) return false;
    *metadata_out = (CettaPrimeRegularKernelCheckingMetadataV1){
        .universe_instance_id = checking->universe_instance_id,
        .universe_storage_epoch = checking->universe_storage_epoch,
        .context_id = checking->context_id,
        .term_id = checking->term_id,
        .expected_type_id = checking->expected_type_id,
        .stage = checking->profile.stage,
        .checking_profile = checking->profile.checking_profile,
    };
    return true;
}

bool cetta_prime_regular_kernel_admitted_checking_v1_erase(
    const CettaPrimeRegularKernelAdmittedCheckingV1 *checking,
    const TermUniverse *live_universe, Arena *destination,
    Atom **term_out, Atom **expected_out) {
    if (!checking || !live_universe || !destination ||
        checking->universe != live_universe ||
        checking->universe_instance_id != live_universe->instance_id ||
        checking->universe_storage_epoch != live_universe->storage_epoch) {
        return false;
    }
    Atom *term = term_universe_copy_atom(
        live_universe, destination, checking->term_id);
    Atom *expected = term_universe_copy_atom(
        live_universe, destination, checking->expected_type_id);
    if (!term || !expected) return false;
    if (term_out) *term_out = term;
    if (expected_out) *expected_out = expected;
    return true;
}

void cetta_prime_regular_kernel_admitted_checking_v1_free(
    CettaPrimeRegularKernelAdmittedCheckingV1 *checking) {
    (void)checking;
}
