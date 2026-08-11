#include <SWI-Prolog.h>

#include "parser_pack_native_api_v1.h"

#include <stdint.h>
#include <string.h>

static bool get_utf8_text(term_t term, char **text, size_t *len) {
    return PL_get_nchars(
        term, len, text,
        CVT_ATOM | CVT_STRING | CVT_EXCEPTION | REP_UTF8 | BUF_MALLOC);
}

static foreign_t raise_native_error(const char *message) {
    term_t exception = PL_new_term_ref();

    return PL_unify_term(
               exception,
               PL_FUNCTOR_CHARS, "error", 2,
                 PL_FUNCTOR_CHARS, "parser_pack_native_error", 1,
                   PL_UTF8_STRING, message,
                 PL_VARIABLE) &&
        PL_raise_exception(exception);
}

static foreign_t parser_pack_native_call_v1(
    term_t backend_term,
    term_t abi_path_term,
    term_t start_term,
    term_t input_term,
    term_t work_limit_term,
    term_t replay_depth_term,
    term_t result_limit_term,
    term_t response_term) {
    char *backend_text = NULL;
    char *abi_path = NULL;
    char *start_text = NULL;
    char *input_text = NULL;
    size_t backend_len = 0u;
    size_t abi_path_len = 0u;
    size_t start_len = 0u;
    size_t input_len = 0u;
    uint64_t work_limit = 0u;
    uint64_t replay_depth = 0u;
    uint64_t result_limit = 0u;
    PPNativeApiV1Backend backend;
    PPNativeApiV1Response response;
    char error[1024] = {0};
    foreign_t ok = FALSE;

    ppnative_api_v1_response_init(&response);
    if (!get_utf8_text(backend_term, &backend_text, &backend_len) ||
        !get_utf8_text(abi_path_term, &abi_path, &abi_path_len) ||
        !get_utf8_text(start_term, &start_text, &start_len) ||
        !get_utf8_text(input_term, &input_text, &input_len) ||
        !PL_get_uint64_ex(work_limit_term, &work_limit) ||
        !PL_get_uint64_ex(replay_depth_term, &replay_depth) ||
        !PL_get_uint64_ex(result_limit_term, &result_limit)) {
        goto done;
    }
    (void)abi_path_len;
    if (backend_len == 3u && memcmp(backend_text, "gll", 3u) == 0) {
        backend = PPNATIVE_API_V1_GLL;
    } else if (backend_len == 3u && memcmp(backend_text, "glr", 3u) == 0) {
        backend = PPNATIVE_API_V1_GLR;
    } else {
        ok = PL_domain_error("parser_pack_native_backend", backend_term);
        goto done;
    }
    if (work_limit == 0u || work_limit > UINT32_MAX) {
        ok = PL_domain_error("positive_uint32", work_limit_term);
        goto done;
    }
    if (replay_depth == 0u || replay_depth > UINT32_MAX) {
        ok = PL_domain_error("positive_uint32", replay_depth_term);
        goto done;
    }
    if (result_limit == 0u || result_limit > UINT32_MAX) {
        ok = PL_domain_error("positive_uint32", result_limit_term);
        goto done;
    }
    if (!ppnative_api_v1_parse(
            backend, abi_path, (const uint8_t *)start_text, start_len,
            (const uint8_t *)input_text, input_len,
            (uint32_t)work_limit, (uint32_t)replay_depth,
            (uint32_t)result_limit, &response, error, sizeof(error))) {
        ok = raise_native_error(error[0] ? error : "native parser rejected request");
        goto done;
    }
    ok = PL_unify_chars(
        response_term, PL_STRING | REP_UTF8,
        response.len, (const char *)response.bytes);

done:
    ppnative_api_v1_response_free(&response);
    if (backend_text)
        PL_free(backend_text);
    if (abi_path)
        PL_free(abi_path);
    if (start_text)
        PL_free(start_text);
    if (input_text)
        PL_free(input_text);
    return ok;
}

install_t install(void) {
    (void)PL_register_foreign_in_module(
        "parser_pack_native_v1", "parser_pack_native_call_v1", 8,
        parser_pack_native_call_v1, 0);
}
