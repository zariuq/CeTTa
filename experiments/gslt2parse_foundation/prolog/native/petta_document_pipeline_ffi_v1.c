#include <SWI-Prolog.h>

#include "petta_document_pipeline_v1.h"

#include <stdint.h>
#include <string.h>

static bool get_utf8_text(term_t term, char **text, size_t *len) {
    return PL_get_nchars(
        term, len, text,
        CVT_ATOM | CVT_STRING | CVT_EXCEPTION | REP_UTF8 | BUF_MALLOC);
}

static foreign_t raise_pipeline_error(const char *message) {
    term_t exception = PL_new_term_ref();

    return PL_unify_term(
               exception,
               PL_FUNCTOR_CHARS, "error", 2,
                 PL_FUNCTOR_CHARS, "petta_document_pipeline_error", 1,
                   PL_UTF8_STRING, message,
                 PL_VARIABLE) &&
        PL_raise_exception(exception);
}

static bool get_limits(term_t limits_term,
                       PPGuardScalarExecV1Limits *limits) {
    uint64_t values[8];
    term_t list = PL_copy_term_ref(limits_term);
    term_t head = PL_new_term_ref();
    size_t index;

    for (index = 0u; index < 8u; index++) {
        if (!PL_get_list(list, head, list) ||
            !PL_get_uint64_ex(head, &values[index])) {
            return false;
        }
    }
    if (!PL_get_nil(list) ||
        values[0] == 0u || values[0] > UINT32_MAX ||
        values[1] == 0u || values[1] > UINT32_MAX ||
        values[2] == 0u ||
        values[3] == 0u || values[3] > UINT32_MAX ||
        values[4] == 0u || values[4] > UINT32_MAX ||
        values[5] == 0u || values[5] > UINT32_MAX ||
        values[6] == 0u || values[6] > UINT32_MAX ||
        values[7] == 0u || values[7] > UINT32_MAX) {
        return false;
    }
    *limits = (PPGuardScalarExecV1Limits){
        .dfa_state_limit = (uint32_t)values[0],
        .dfa_transition_limit = (uint32_t)values[1],
        .scan_work_limit = values[2],
        .scan_token_limit = (uint32_t)values[3],
        .witness_work_limit = (uint32_t)values[4],
        .parse_work_limit = (uint32_t)values[5],
        .replay_depth = (uint32_t)values[6],
        .result_limit = (uint32_t)values[7],
    };
    return true;
}

static foreign_t petta_document_pipeline_call_v1(
    term_t splitter_abi_term,
    term_t form_abi_term,
    term_t guard_nfa_term,
    term_t guard_evidence_term,
    term_t input_term,
    term_t compiler_digest_term,
    term_t limits_term,
    term_t response_term) {
    char *splitter_abi = NULL;
    char *form_abi = NULL;
    char *guard_nfa = NULL;
    char *guard_evidence = NULL;
    char *input = NULL;
    char *compiler_digest = NULL;
    size_t splitter_abi_len = 0u;
    size_t form_abi_len = 0u;
    size_t guard_nfa_len = 0u;
    size_t guard_evidence_len = 0u;
    size_t input_len = 0u;
    size_t compiler_digest_len = 0u;
    PPGuardScalarExecV1Limits limits;
    PeTTaDocumentPipelineV1Response response;
    char error[1024] = {0};
    foreign_t ok = FALSE;

    petta_document_pipeline_v1_response_init(&response);
    if (!get_utf8_text(splitter_abi_term, &splitter_abi, &splitter_abi_len) ||
        !get_utf8_text(form_abi_term, &form_abi, &form_abi_len) ||
        !get_utf8_text(guard_nfa_term, &guard_nfa, &guard_nfa_len) ||
        !get_utf8_text(
            guard_evidence_term, &guard_evidence, &guard_evidence_len) ||
        !get_utf8_text(input_term, &input, &input_len) ||
        !get_utf8_text(
            compiler_digest_term, &compiler_digest, &compiler_digest_len) ||
        !get_limits(limits_term, &limits)) {
        goto done;
    }
    if (splitter_abi_len == 0u || form_abi_len == 0u ||
        guard_nfa_len == 0u || guard_evidence_len == 0u ||
        memchr(splitter_abi, '\0', splitter_abi_len) != NULL ||
        memchr(form_abi, '\0', form_abi_len) != NULL ||
        memchr(guard_nfa, '\0', guard_nfa_len) != NULL ||
        memchr(guard_evidence, '\0', guard_evidence_len) != NULL) {
        ok = PL_domain_error("native_artifact_path", splitter_abi_term);
        goto done;
    }
    if (compiler_digest_len != 64u ||
        memchr(compiler_digest, '\0', compiler_digest_len) != NULL) {
        ok = PL_domain_error("sha256_digest", compiler_digest_term);
        goto done;
    }
    if (!petta_document_pipeline_v1_parse(
            splitter_abi,
            form_abi,
            guard_nfa,
            guard_evidence,
            (const uint8_t *)input,
            input_len,
            compiler_digest,
            &limits,
            &response,
            error,
            sizeof(error))) {
        ok = raise_pipeline_error(
            error[0] ? error : "native PeTTa document pipeline rejected request");
        goto done;
    }
    ok = PL_unify_chars(
        response_term, PL_STRING | REP_UTF8,
        response.len, (const char *)response.bytes);

done:
    petta_document_pipeline_v1_response_free(&response);
    if (splitter_abi)
        PL_free(splitter_abi);
    if (form_abi)
        PL_free(form_abi);
    if (guard_nfa)
        PL_free(guard_nfa);
    if (guard_evidence)
        PL_free(guard_evidence);
    if (input)
        PL_free(input);
    if (compiler_digest)
        PL_free(compiler_digest);
    return ok;
}

install_t install(void) {
    (void)PL_register_foreign_in_module(
        "petta_document_native_v1",
        "petta_document_pipeline_call_v1",
        8,
        petta_document_pipeline_call_v1,
        0);
}
