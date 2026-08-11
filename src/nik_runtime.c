#include "nik_runtime.h"

#include "generated/prime_nik_authorities_v1.generated.h"
#include "generated/prime_nik_runtime_v1.generated.h"
#include "gslt_language_runtime.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

const char *cetta_nik_outcome_name(CettaNikOutcome outcome) {
    switch (outcome) {
    case CETTA_NIK_ACCEPTED:
        return "accepted";
    case CETTA_NIK_REJECTED:
        return "rejected";
    case CETTA_NIK_MALFORMED:
        return "malformed";
    case CETTA_NIK_UNSUPPORTED:
        return "unsupported";
    case CETTA_NIK_INCOMPLETE:
        return "incomplete";
    case CETTA_NIK_FAULT:
        return "fault";
    }
    return "fault";
}

static const CettaNikAuthorityV1 *nik_find_authority(const char *alias) {
    if (!alias)
        return NULL;
    for (size_t index = 0u;
         index < cetta_prime_nik_authorities_v1_count; index++) {
        const CettaNikAuthorityV1 *authority =
            &cetta_prime_nik_authorities_v1[index];
        if (strcmp(authority->alias, alias) == 0)
            return authority;
    }
    return NULL;
}

static Atom *nik_parse_one(Arena *arena, const char *source) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, source, &position);
    if (!atom || !parser_rest_is_delimiters(source, &position))
        return NULL;
    return atom;
}

static Atom *nik_authority_atom(
    Arena *arena, const CettaNikAuthorityV1 *authority) {
    Atom *items[5] = {
        atom_symbol(arena, "NIKAuthorityV1"),
        atom_symbol(arena, authority->alias),
        atom_string(arena, authority->system_id),
        atom_string(arena, authority->revision),
        atom_string(arena, authority->digest),
    };
    return atom_expr(arena, items, 5u);
}

static Atom *nik_query_atom(
    Arena *arena, const CettaNikAuthorityV1 *authority,
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
        requested.max_rule_attempts = 1000000u;
    if (requested.max_answers == 0u)
        requested.max_answers = 1000u;
    if (requested.max_depth == 0u)
        requested.max_depth = 10000u;
    return requested;
}

static uint64_t nik_u64_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static size_t nik_size_limit(uint64_t value) {
    return value > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)value;
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

CettaNikOutcome cetta_nik_check_v1(
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size) {
    const CettaNikAuthorityV1 *authority;
    Atom *presentation;
    Atom *query;
    CettaGsltLanguage *language = NULL;
    CettaGsltHornResult reference = {0};
    CettaGsltHornResult compiled = {0};
    CettaInferenceReplayStats replay_stats = {0};
    CettaInferenceReplayLimits replay_limits = limits.replay;
    CettaGsltHornLimits base_gslt_limits;
    CettaGsltHornLimits reference_limits;
    CettaGsltHornLimits compiled_limits;
    char native_error[512] = {0};
    bool total_limited = limits.max_total_work != 0u;
    uint64_t remaining = limits.max_total_work;
    CettaNikOutcome outcome = CETTA_NIK_FAULT;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (receipt) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->outcome = CETTA_NIK_FAULT;
        receipt->native_status = CETTA_INFERENCE_MALFORMED_PROOF;
        receipt->catalog_digest =
            cetta_prime_nik_authorities_v1_catalog_sha256;
    }
    if (!receipt || !arena || !claim || !proof)
        return nik_error(receipt, CETTA_NIK_MALFORMED,
                         error_buf, error_buf_size,
                         "invalid NIK check request");
    authority = nik_find_authority(authority_alias);
    if (!authority)
        return nik_error(receipt, CETTA_NIK_UNSUPPORTED,
                         error_buf, error_buf_size,
                         "unknown NIK authority '%s'",
                         authority_alias ? authority_alias : "");
    receipt->authority_alias = authority->alias;
    receipt->system_id = authority->system_id;
    receipt->revision = authority->revision;
    receipt->authority_digest = authority->digest;
    if (atom_has_vars(claim) || atom_has_vars(proof))
        return nik_error(receipt, CETTA_NIK_MALFORMED,
                         error_buf, error_buf_size,
                         "NIK claims and proof articles must be closed");
    presentation = nik_parse_one(arena, authority->presentation_metta);
    if (!presentation)
        return nik_error(receipt, CETTA_NIK_FAULT,
                         error_buf, error_buf_size,
                         "embedded NIK presentation cannot be decoded");

    if (total_limited &&
        (replay_limits.max_nodes == 0u ||
         replay_limits.max_nodes > nik_size_limit(remaining))) {
        replay_limits.max_nodes = nik_size_limit(remaining);
    }
    receipt->native_ran = true;
    receipt->native_status = cetta_inference_check_raw_proof(
        presentation, claim, proof, replay_limits, &replay_stats, arena,
        native_error, sizeof(native_error));
    receipt->native_nodes = replay_stats.nodes;
    receipt->total_work = replay_stats.nodes;
    if (total_limited) {
        if (receipt->native_nodes >= remaining)
            remaining = 0u;
        else
            remaining -= receipt->native_nodes;
    }
    if (receipt->native_status == CETTA_INFERENCE_MALFORMED_PROOF)
        return nik_error(receipt, CETTA_NIK_MALFORMED,
                         error_buf, error_buf_size, "%s",
                         native_error[0] ? native_error :
                             "malformed NIK proof article");
    if (receipt->native_status == CETTA_INFERENCE_RESOURCE_LIMIT)
        return nik_error(receipt, CETTA_NIK_INCOMPLETE,
                         error_buf, error_buf_size, "%s",
                         native_error[0] ? native_error :
                             "native NIK replay reached a resource boundary");
    if (receipt->native_status == CETTA_INFERENCE_INVALID_PRESENTATION)
        return nik_error(receipt, CETTA_NIK_FAULT,
                         error_buf, error_buf_size, "%s",
                         native_error[0] ? native_error :
                             "embedded NIK presentation was rejected");
    receipt->native_accepted =
        receipt->native_status == CETTA_INFERENCE_OK;

    if (total_limited && remaining == 0u)
        return nik_error(
            receipt, CETTA_NIK_INCOMPLETE, error_buf, error_buf_size,
            "aggregate NIK work limit exhausted after native replay");

    if (!cetta_gslt_language_load_embedded(
            &cetta_prime_nik_runtime_v1, &language,
            error_buf, error_buf_size))
        goto done;
    query = nik_query_atom(arena, authority, claim, proof);
    base_gslt_limits = nik_gslt_limits(limits.gslt);
    reference_limits = base_gslt_limits;
    if (total_limited)
        nik_cap_rule_attempts(&reference_limits, remaining);
    receipt->reference_ran = true;
    if (!cetta_gslt_language_query_v1(
        language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        arena, query, reference_limits, &reference,
        error_buf, error_buf_size))
        goto done;
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
        outcome = CETTA_NIK_INCOMPLETE;
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
    receipt->compiled_ran = true;
    if (!cetta_gslt_language_query_v1(
        language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
        arena, query, compiled_limits, &compiled,
        error_buf, error_buf_size))
        goto done;
    receipt->compiled_outcome = compiled.outcome;
    receipt->compiled_rule_attempts = compiled.rule_attempts;
    receipt->total_work = nik_u64_add_sat(
        receipt->total_work, compiled.rule_attempts);
    receipt->compiled_accepted = nik_horn_accepts(&compiled, query);
    if (compiled.outcome != CETTA_GSLT_HORN_COMPLETED) {
        outcome = CETTA_NIK_INCOMPLETE;
        goto done;
    }
    if (receipt->reference_accepted != receipt->compiled_accepted ||
        receipt->native_accepted != receipt->reference_accepted) {
        outcome = nik_error(
            receipt, CETTA_NIK_FAULT, error_buf, error_buf_size,
            "NIK realizations disagree for authority '%s' "
            "(native=%u status=%s reference=%u compiled=%u%s%s)",
            authority->alias,
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
    if (receipt->compiled_ran)
        cetta_gslt_horn_result_free(&compiled);
    if (receipt->reference_ran)
        cetta_gslt_horn_result_free(&reference);
    cetta_gslt_language_free(language);
    receipt->outcome = outcome;
    if (outcome == CETTA_NIK_FAULT &&
        error_buf && error_buf_size > 0u && error_buf[0] == '\0')
        (void)snprintf(error_buf, error_buf_size,
                       "NIK runtime fault");
    return outcome;
}
