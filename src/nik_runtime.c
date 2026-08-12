#include "nik_runtime.h"
#include "nik_runtime_internal.h"

#include "generated/prime_nik_authorities_v1.generated.h"
#include "generated/prime_nik_runtime_v1.generated.h"
#include "generated/prime_nik_side_condition_provider_catalog_v1.generated.h"
#include "gslt_language_runtime.h"
#include "inference_side_condition_provider.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena presentation_arena;
    Atom *presentation;
    CettaInferenceChecker *checker;
    bool arena_initialized;
} CettaNikAuthorityStateV1;

struct CettaNikRuntimeV1 {
    pthread_mutex_t admission_mutex;
    CettaNikAuthorityStateV1 *authorities;
    size_t authority_count;
    size_t admission_count;
    uint64_t symbol_table_instance;
};

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

static bool nik_digest_is_sha256(const char *digest) {
    if (!digest || strlen(digest) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        char character = digest[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
}

static void nik_sha256_update_u64_be(
    CettaNativeSha256 *sha, uint64_t value) {
    uint8_t bytes[8];
    for (size_t index = 0u; index < sizeof(bytes); index++)
        bytes[sizeof(bytes) - 1u - index] =
            (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static bool nik_sha256_update_framed_text(
    CettaNativeSha256 *sha, const char *text) {
    size_t length;
    uint64_t length64;
    if (!sha || !text)
        return false;
    length = strlen(text);
    length64 = (uint64_t)length;
    if ((size_t)length64 != length)
        return false;
    nik_sha256_update_u64_be(sha, length64);
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, length);
    return true;
}

bool cetta_nik_authority_descriptor_valid_v1(
    const CettaNikAuthorityV1 *authority) {
    char actual_digest[65];
    if (!authority || !authority->alias || !authority->alias[0] ||
        !authority->system_id || !authority->system_id[0] ||
        !authority->revision || !authority->revision[0] ||
        !nik_digest_is_sha256(authority->digest) ||
        !authority->presentation_metta)
        return false;
    cetta_native_sha256_hex(
        (const uint8_t *)authority->presentation_metta,
        strlen(authority->presentation_metta), actual_digest);
    return strcmp(actual_digest, authority->digest) == 0;
}

bool cetta_nik_authority_catalog_valid_v1(
    const CettaNikAuthorityV1 *authorities,
    size_t authority_count,
    const char *expected_digest) {
    static const uint8_t identity[] = "NIKAuthorityCatalogV1";
    CettaNativeSha256 sha;
    char actual_digest[65];
    uint64_t count64 = (uint64_t)authority_count;

    if (!authorities || authority_count == 0u ||
        (size_t)count64 != authority_count ||
        !nik_digest_is_sha256(expected_digest))
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, identity, sizeof(identity));
    nik_sha256_update_u64_be(&sha, count64);
    for (size_t index = 0u; index < authority_count; index++) {
        const CettaNikAuthorityV1 *authority = &authorities[index];
        if (!cetta_nik_authority_descriptor_valid_v1(authority) ||
            !nik_sha256_update_framed_text(&sha, authority->alias) ||
            !nik_sha256_update_framed_text(&sha, authority->system_id) ||
            !nik_sha256_update_framed_text(&sha, authority->revision) ||
            !nik_sha256_update_framed_text(&sha, authority->digest))
            return false;
    }
    cetta_native_sha256_finish_hex(&sha, actual_digest);
    return strcmp(actual_digest, expected_digest) == 0;
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

static void nik_receipt_reset(CettaNikReceiptV1 *receipt) {
    if (!receipt)
        return;
    memset(receipt, 0, sizeof(*receipt));
    receipt->outcome = CETTA_NIK_FAULT;
    receipt->native_status = CETTA_INFERENCE_MALFORMED_PROOF;
    receipt->catalog_digest =
        cetta_prime_nik_authorities_v1_catalog_sha256;
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

static void nik_runtime_error(
    char *error_buf, size_t error_buf_size, const char *message) {
    if (error_buf && error_buf_size > 0u)
        (void)snprintf(error_buf, error_buf_size, "%s", message);
}

CettaNikRuntimeV1 *cetta_nik_runtime_v1_new(
    char *error_buf, size_t error_buf_size) {
    CettaNikRuntimeV1 *runtime;
    size_t state_bytes;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!g_symbols) {
        nik_runtime_error(
            error_buf, error_buf_size,
            "NIK runtime requires an active symbol table");
        return NULL;
    }
    if (!cetta_nik_authority_catalog_valid_v1(
            cetta_prime_nik_authorities_v1,
            cetta_prime_nik_authorities_v1_count,
            cetta_prime_nik_authorities_v1_catalog_sha256)) {
        nik_runtime_error(
            error_buf, error_buf_size,
            "embedded NIK authority catalog failed identity verification");
        return NULL;
    }
    if (cetta_prime_nik_authorities_v1_count >
        SIZE_MAX / sizeof(*runtime->authorities)) {
        nik_runtime_error(
            error_buf, error_buf_size,
            "NIK authority state inventory exceeds native range");
        return NULL;
    }
    state_bytes = cetta_prime_nik_authorities_v1_count *
        sizeof(*runtime->authorities);
    runtime = cetta_malloc(sizeof(*runtime));
    memset(runtime, 0, sizeof(*runtime));
    runtime->authorities = cetta_malloc(state_bytes);
    memset(runtime->authorities, 0, state_bytes);
    runtime->authority_count = cetta_prime_nik_authorities_v1_count;
    runtime->symbol_table_instance = symbol_table_instance_id(g_symbols);
    if (pthread_mutex_init(&runtime->admission_mutex, NULL) != 0) {
        free(runtime->authorities);
        free(runtime);
        nik_runtime_error(
            error_buf, error_buf_size,
            "NIK authority admission mutex initialization failed");
        return NULL;
    }
    return runtime;
}

void cetta_nik_runtime_v1_free(CettaNikRuntimeV1 *runtime) {
    if (!runtime)
        return;
    for (size_t index = 0u; index < runtime->authority_count; index++) {
        CettaNikAuthorityStateV1 *state = &runtime->authorities[index];
        cetta_inference_checker_destroy(state->checker);
        if (state->arena_initialized)
            arena_free(&state->presentation_arena);
    }
    pthread_mutex_destroy(&runtime->admission_mutex);
    free(runtime->authorities);
    free(runtime);
}

size_t cetta_nik_runtime_v1_admission_count(
    CettaNikRuntimeV1 *runtime) {
    size_t count;
    if (!runtime)
        return 0u;
    pthread_mutex_lock(&runtime->admission_mutex);
    count = runtime->admission_count;
    pthread_mutex_unlock(&runtime->admission_mutex);
    return count;
}

static CettaInferenceStatus nik_runtime_admit_authority(
    CettaNikRuntimeV1 *runtime,
    const CettaNikAuthorityV1 *authority,
    const CettaInferenceChecker **checker_out,
    char *error_buf,
    size_t error_buf_size) {
    size_t index = (size_t)(authority - cetta_prime_nik_authorities_v1);
    CettaNikAuthorityStateV1 *state;
    CettaInferenceStatus status = CETTA_INFERENCE_OK;

    if (checker_out)
        *checker_out = NULL;
    if (!runtime || !authority || !checker_out ||
        index >= runtime->authority_count) {
        nik_runtime_error(
            error_buf, error_buf_size,
            "invalid NIK authority admission request");
        return CETTA_INFERENCE_INVALID_PRESENTATION;
    }
    pthread_mutex_lock(&runtime->admission_mutex);
    state = &runtime->authorities[index];
    if (!state->checker) {
        arena_init(&state->presentation_arena);
        state->arena_initialized = true;
        state->presentation = nik_parse_one(
            &state->presentation_arena, authority->presentation_metta);
        if (!state->presentation) {
            status = CETTA_INFERENCE_INVALID_PRESENTATION;
            nik_runtime_error(
                error_buf, error_buf_size,
                "embedded NIK presentation cannot be decoded");
            goto done;
        }
        status = cetta_inference_checker_create(
            state->presentation, &state->checker,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            goto done;
        runtime->admission_count++;
    }
    *checker_out = state->checker;

done:
    if (status != CETTA_INFERENCE_OK && !state->checker &&
        state->arena_initialized) {
        arena_free(&state->presentation_arena);
        memset(state, 0, sizeof(*state));
    }
    pthread_mutex_unlock(&runtime->admission_mutex);
    return status;
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

static bool nik_is_dag_article(Atom *proof) {
    return proof && proof->kind == ATOM_EXPR && proof->expr.len > 0u &&
        atom_is_symbol(proof->expr.elems[0], "GProofDAG");
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

static CettaNikOutcome nik_check_v1(
    CettaNikRuntimeV1 *runtime,
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size,
    CettaNikGsltQueryV1 query_realization,
    bool differential) {
    const CettaNikAuthorityV1 *authority;
    const CettaInferenceChecker *checker = NULL;
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
    nik_receipt_reset(receipt);
    if (!runtime || !receipt || !arena || !claim || !proof ||
        (differential && !query_realization))
        return nik_error(receipt, CETTA_NIK_MALFORMED,
                         error_buf, error_buf_size,
                         "invalid NIK check request");
    if (!g_symbols || runtime->symbol_table_instance !=
            symbol_table_instance_id(g_symbols))
        return nik_error(receipt, CETTA_NIK_FAULT,
                         error_buf, error_buf_size,
                         "NIK runtime belongs to another symbol-table lifetime");
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
    receipt->native_status = nik_runtime_admit_authority(
        runtime, authority, &checker, native_error, sizeof(native_error));
    if (receipt->native_status != CETTA_INFERENCE_OK)
        return nik_error(receipt, CETTA_NIK_FAULT,
                         error_buf, error_buf_size,
                         "%s", native_error[0] ? native_error :
                             "embedded NIK presentation was rejected");

    if (total_limited &&
        (replay_limits.max_nodes == 0u ||
         replay_limits.max_nodes > nik_size_limit(remaining))) {
        replay_limits.max_nodes = nik_size_limit(remaining);
    }
    receipt->native_status = nik_is_dag_article(proof)
        ? cetta_inference_checker_check_dag_article(
            checker, claim, proof, replay_limits, &replay_stats, arena,
            native_error, sizeof(native_error))
        : cetta_inference_checker_check_raw_proof(
            checker, claim, proof, replay_limits, &replay_stats, arena,
            native_error, sizeof(native_error));
    receipt->native_ran = true;
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
    if (receipt->native_nodes == 0u)
        return nik_error(receipt, CETTA_NIK_FAULT,
                         error_buf, error_buf_size,
                         "native NIK replay reported no work");
    receipt->native_accepted =
        receipt->native_status == CETTA_INFERENCE_OK;

    if (!differential) {
        outcome = receipt->native_accepted
            ? CETTA_NIK_ACCEPTED : CETTA_NIK_REJECTED;
        goto done;
    }

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
    outcome = nik_check_v1(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size, query_realization, true);
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
    outcome = nik_check_v1(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size,
        nik_gslt_query_with_side_conditions_v1, true);
    cetta_nik_runtime_v1_free(runtime);
    return outcome;
}

CettaNikOutcome cetta_nik_runtime_v1_check(
    CettaNikRuntimeV1 *runtime,
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return nik_check_v1(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size, NULL, false);
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
    outcome = cetta_nik_runtime_v1_check(
        runtime, authority_alias, claim, proof, limits, arena, receipt,
        error_buf, error_buf_size);
    cetta_nik_runtime_v1_free(runtime);
    return outcome;
}
