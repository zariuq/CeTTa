/* Differential qualification only. Ordinary NIK execution does not link this
 * comparison service or its generated replay artifacts. */
#include "nik_runtime_internal.h"
#include "generated/prime_nik_runtime_v1.generated.h"
#include "generated/prime_nik_side_condition_provider_catalog_v1.generated.h"
#include "inference_side_condition_provider.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *nik_horn_outcome_name(CettaGsltHornOutcome outcome) {
    switch (outcome) {
    case CETTA_GSLT_HORN_COMPLETED:
        return "completed";
    case CETTA_GSLT_HORN_RULE_LIMIT:
        return "rule-limit";
    case CETTA_GSLT_HORN_ANSWER_LIMIT:
        return "answer-limit";
    case CETTA_GSLT_HORN_DEPTH_LIMIT:
        return "depth-limit";
    case CETTA_GSLT_HORN_FAULT:
        return "fault";
    }
    return "fault";
}

static CettaNikOutcome nik_error(
    CettaNikReceiptV1 *receipt,
    CettaNikOutcome outcome,
    char *error_buf,
    size_t error_buf_size,
    const char *format,
    ...) {
    if (receipt)
        receipt->outcome = outcome;
    if (error_buf && error_buf_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error_buf, error_buf_size, format, arguments);
        va_end(arguments);
    }
    return outcome;
}

static void nik_receipt_reset(CettaNikReceiptV1 *receipt) {
    if (!receipt)
        return;
    memset(receipt, 0, sizeof(*receipt));
    receipt->outcome = CETTA_NIK_FAULT;
    receipt->native_status = CETTA_INFERENCE_MALFORMED_PROOF;
    receipt->catalog_digest =
        cetta_prime_nik_authorities_v1_catalog_sha256;
}

static Atom *nik_authority_atom(
    Arena *arena, const CettaNikReceiptV1 *authority) {
    Atom *items[5] = {
        atom_symbol(arena, "NIKAuthorityV1"),
        atom_symbol(arena, authority->authority_alias),
        atom_string(arena, authority->system_id),
        atom_string(arena, authority->revision),
        atom_string(arena, authority->authority_digest),
    };
    return atom_expr(arena, items, 5u);
}

static Atom *nik_query_atom(
    Arena *arena, const CettaNikReceiptV1 *authority,
    Atom *claim, Atom *proof) {
    Atom *items[4] = {
        atom_symbol(arena, "nik-check"),
        nik_authority_atom(arena, authority),
        claim,
        proof,
    };
    return atom_expr(arena, items, 4u);
}

static CettaGsltHornLimits nik_gslt_limits(CettaGsltHornLimits requested) {
    if (requested.max_rule_attempts == 0u)
        requested.max_rule_attempts = 5000000u;
    if (requested.max_answers == 0u)
        requested.max_answers = 1000u;
    if (requested.max_depth == 0u)
        requested.max_depth = 1000000u;
    return requested;
}

static uint64_t nik_u64_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static void nik_cap_rule_attempts(
    CettaGsltHornLimits *limits, uint64_t remaining) {
    if (limits->max_rule_attempts > remaining)
        limits->max_rule_attempts = remaining;
}

static bool nik_horn_accepts(
    const CettaGsltHornResult *result, Atom *query) {
    if (!result || result->outcome != CETTA_GSLT_HORN_COMPLETED ||
        result->answer_count == 0u)
        return false;
    for (size_t index = 0u; index < result->answer_count; index++)
        if (!atom_eq_fast(result->answers[index], query))
            return false;
    return true;
}

static bool nik_gslt_query_with_side_conditions_v1(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Arena *output_arena,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error,
    size_t error_size) {
    return cetta_gslt_language_query_with_providers_v1(
        language, realization,
        &cetta_prime_nik_side_condition_provider_catalog_v1,
        cetta_inference_side_condition_provider_registry_v1(),
        output_arena, query, limits, result, error, error_size);
}

static CettaNikOutcome nik_check_differential_v1(
    CettaNikRuntimeV1 *runtime,
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size,
    CettaNikGsltQueryV1 query_realization) {
    Atom *query;
    CettaGsltLanguage *language = NULL;
    CettaGsltHornResult reference = {0};
    CettaGsltHornResult compiled = {0};
    CettaGsltHornLimits base_gslt_limits;
    CettaGsltHornLimits reference_limits;
    CettaGsltHornLimits compiled_limits;
    char native_error[512] = {0};
    bool total_limited = limits.max_total_work != 0u;
    uint64_t remaining = limits.max_total_work;
    CettaNikOutcome outcome;

    if (!query_realization) {
        if (error_buf && error_buf_size) error_buf[0] = '\0';
        nik_receipt_reset(receipt);
        return nik_error(receipt, CETTA_NIK_MALFORMED,
            error_buf, error_buf_size, "invalid NIK check request");
    }
    outcome = cetta_nik_runtime_v1_check_diagnostic(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size, native_error, sizeof(native_error));
    if (outcome != CETTA_NIK_ACCEPTED && outcome != CETTA_NIK_REJECTED)
        return outcome;
    outcome = CETTA_NIK_FAULT;
    if (total_limited)
        remaining = receipt->native_nodes >= remaining
            ? 0u : remaining - receipt->native_nodes;

    if (total_limited && remaining == 0u)
        return nik_error(
            receipt, CETTA_NIK_INCOMPLETE, error_buf, error_buf_size,
            "aggregate NIK work limit exhausted after native replay");

    if (!cetta_gslt_language_load_embedded(
            &cetta_prime_nik_runtime_v1, &language,
            error_buf, error_buf_size))
        goto done;
    query = nik_query_atom(arena, receipt, claim, proof);
    base_gslt_limits = nik_gslt_limits(limits.gslt);
    reference_limits = base_gslt_limits;
    if (total_limited)
        nik_cap_rule_attempts(&reference_limits, remaining);
    if (!query_realization(
        language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        arena, query, reference_limits, &reference,
        error_buf, error_buf_size))
        goto done;
    receipt->reference_ran = true;
    receipt->reference_outcome = reference.outcome;
    receipt->reference_rule_attempts = reference.rule_attempts;
    receipt->total_work = nik_u64_add_sat(
        receipt->total_work, reference.rule_attempts);
    if (total_limited) {
        if (reference.rule_attempts >= remaining)
            remaining = 0u;
        else
            remaining -= reference.rule_attempts;
    }
    receipt->reference_accepted = nik_horn_accepts(&reference, query);
    if (reference.outcome != CETTA_GSLT_HORN_COMPLETED) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(
                error_buf, error_buf_size,
                "reference NIK replay stopped at %s after %llu rule "
                "attempts and depth %u",
                nik_horn_outcome_name(reference.outcome),
                (unsigned long long)reference.rule_attempts,
                reference.max_depth_observed);
        outcome = CETTA_NIK_INCOMPLETE;
        goto done;
    }
    if (receipt->reference_rule_attempts == 0u) {
        outcome = nik_error(
            receipt, CETTA_NIK_FAULT, error_buf, error_buf_size,
            "reference NIK replay reported no work");
        goto done;
    }
    if (total_limited && remaining == 0u) {
        outcome = nik_error(
            receipt, CETTA_NIK_INCOMPLETE, error_buf, error_buf_size,
            "aggregate NIK work limit exhausted after reference replay");
        goto done;
    }
    compiled_limits = base_gslt_limits;
    if (total_limited)
        nik_cap_rule_attempts(&compiled_limits, remaining);
    if (!query_realization(
        language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
        arena, query, compiled_limits, &compiled,
        error_buf, error_buf_size))
        goto done;
    receipt->compiled_ran = true;
    receipt->compiled_outcome = compiled.outcome;
    receipt->compiled_rule_attempts = compiled.rule_attempts;
    receipt->total_work = nik_u64_add_sat(
        receipt->total_work, compiled.rule_attempts);
    receipt->compiled_accepted = nik_horn_accepts(&compiled, query);
    if (compiled.outcome != CETTA_GSLT_HORN_COMPLETED) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(
                error_buf, error_buf_size,
                "compiled NIK replay stopped at %s after %llu rule "
                "attempts and depth %u",
                nik_horn_outcome_name(compiled.outcome),
                (unsigned long long)compiled.rule_attempts,
                compiled.max_depth_observed);
        outcome = CETTA_NIK_INCOMPLETE;
        goto done;
    }
    if (receipt->compiled_rule_attempts == 0u) {
        outcome = nik_error(
            receipt, CETTA_NIK_FAULT, error_buf, error_buf_size,
            "compiled NIK replay reported no work");
        goto done;
    }
    if (receipt->reference_accepted != receipt->compiled_accepted ||
        receipt->native_accepted != receipt->reference_accepted) {
        outcome = nik_error(
            receipt, CETTA_NIK_FAULT, error_buf, error_buf_size,
            "NIK realizations disagree for authority '%s' "
            "(native=%u status=%s reference=%u compiled=%u%s%s)",
            receipt->authority_alias,
            receipt->native_accepted ? 1u : 0u,
            cetta_inference_status_name(receipt->native_status),
            receipt->reference_accepted ? 1u : 0u,
            receipt->compiled_accepted ? 1u : 0u,
            native_error[0] ? ": " : "",
            native_error);
        goto done;
    }
    outcome = receipt->native_accepted
        ? CETTA_NIK_ACCEPTED : CETTA_NIK_REJECTED;

done:
    cetta_gslt_horn_result_free(&compiled);
    cetta_gslt_horn_result_free(&reference);
    cetta_gslt_language_free(language);
    receipt->outcome = outcome;
    if (outcome == CETTA_NIK_FAULT &&
        error_buf && error_buf_size > 0u && error_buf[0] == '\0')
        (void)snprintf(error_buf, error_buf_size,
                       "NIK runtime fault");
    return outcome;
}

CettaNikOutcome cetta_nik_check_with_query_v1(
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size,
    CettaNikGsltQueryV1 query_realization) {
    char runtime_error[512] = {0};
    nik_receipt_reset(receipt);
    CettaNikRuntimeV1 *runtime = cetta_nik_runtime_v1_new(
        runtime_error, sizeof(runtime_error));
    CettaNikOutcome outcome;
    if (!runtime)
        return nik_error(
            receipt, CETTA_NIK_FAULT, error_buf, error_buf_size,
            "%s", runtime_error[0] ? runtime_error :
                "NIK runtime initialization failed");
    outcome = nik_check_differential_v1(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size, query_realization);
    cetta_nik_runtime_v1_free(runtime);
    return outcome;
}

CettaNikOutcome cetta_nik_check_differential_v1(
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size) {
    char runtime_error[512] = {0};
    nik_receipt_reset(receipt);
    CettaNikRuntimeV1 *runtime = cetta_nik_runtime_v1_new(
        runtime_error, sizeof(runtime_error));
    CettaNikOutcome outcome;
    if (!runtime)
        return nik_error(
            receipt, CETTA_NIK_FAULT, error_buf, error_buf_size,
            "%s", runtime_error[0] ? runtime_error :
                "NIK runtime initialization failed");
    outcome = nik_check_differential_v1(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size,
        nik_gslt_query_with_side_conditions_v1);
    cetta_nik_runtime_v1_free(runtime);
    return outcome;
}
