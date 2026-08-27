#include "json_runtime_v1.h"

#include "json_cst_value_v1.h"
#include "language_def_core_v1.h"
#include "language_def_parser_pack_v1.h"
#include "operational_language_def_v1.h"
#include "parser_pack_gll_v1.h"
#include "parser_pack_glr_v1.h"
#include "parser_pack_native_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaJsonRuntimeV1 {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOperationalLanguageDefV1 target_wire;
    CettaLanguageDefCoreV1 target_language;
    CettaOpLangV1Document profile_document;
    CettaLdParserProfileV1 profile;
    CettaJsonElaborationPlanV1 elaboration_plan;
    CettaLdParserPackV1 compiled;
    PPNativeV1Prepared prepared;
};

static void json_runtime_set_error(char *error_buf, size_t error_buf_size,
                                   const char *message) {
    if (error_buf && error_buf_size > 0u) {
        (void)snprintf(error_buf, error_buf_size, "%s",
                       message ? message : "JSON runtime failure");
    }
}

static void json_runtime_init(CettaJsonRuntimeV1 *runtime) {
    cetta_op_lang_v1_init(&runtime->wire);
    cetta_language_def_core_v1_init(&runtime->language);
    cetta_op_lang_v1_init(&runtime->target_wire);
    cetta_language_def_core_v1_init(&runtime->target_language);
    cetta_op_lang_v1_document_init(&runtime->profile_document);
    cetta_ld_parser_profile_v1_init(&runtime->profile);
    cetta_json_elaboration_plan_v1_init(&runtime->elaboration_plan);
    cetta_ld_parser_pack_v1_init(&runtime->compiled);
    ppnative_v1_prepared_init(&runtime->prepared);
}

void cetta_json_runtime_v1_free(CettaJsonRuntimeV1 *runtime) {
    if (!runtime) return;
    ppnative_v1_prepared_free(&runtime->prepared);
    cetta_ld_parser_pack_v1_free(&runtime->compiled);
    cetta_json_elaboration_plan_v1_free(&runtime->elaboration_plan);
    cetta_ld_parser_profile_v1_free(&runtime->profile);
    cetta_op_lang_v1_document_free(&runtime->profile_document);
    cetta_language_def_core_v1_free(&runtime->target_language);
    cetta_op_lang_v1_free(&runtime->target_wire);
    cetta_language_def_core_v1_free(&runtime->language);
    cetta_op_lang_v1_free(&runtime->wire);
    free(runtime);
}

void cetta_json_runtime_v1_default_limits(CettaJsonRuntimeV1Limits *limits) {
    if (!limits) return;
    limits->recognizer_work_limit = 2000000u;
    limits->replay_depth_limit = 1024u;
    limits->result_limit = 4096u;
    limits->elaboration_work_limit = 1000000u;
    limits->value_depth_limit = 1024u;
    limits->kernel = CETTA_JSON_KERNEL_V1_PACKED_GLL;
}

CettaJsonRuntimeV1 *cetta_json_runtime_v1_new(
    const uint8_t *language_source,
    size_t language_source_len,
    const uint8_t *profile_source,
    size_t profile_source_len,
    const uint8_t *target_source,
    size_t target_source_len,
    char *error_buf,
    size_t error_buf_size) {
    CettaJsonRuntimeV1 *runtime = NULL;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    CettaLdParserPackV1Status pack_status =
        CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    CettaJsonElaborationPlanV1Status plan_status =
        CETTA_JSON_ELAB_PLAN_V1_BAD_ARGUMENT;

    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if ((!language_source && language_source_len > 0u) ||
        (!profile_source && profile_source_len > 0u) ||
        (!target_source && target_source_len > 0u) ||
        language_source_len == 0u || profile_source_len == 0u ||
        target_source_len == 0u) {
        json_runtime_set_error(error_buf, error_buf_size,
                               "missing authored JSON source, profile, or target");
        return NULL;
    }
    runtime = (CettaJsonRuntimeV1 *)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        json_runtime_set_error(error_buf, error_buf_size,
                               "out of memory allocating JSON runtime");
        return NULL;
    }
    json_runtime_init(runtime);
    if (!cetta_op_lang_v1_parse_bytes(
            &runtime->wire, language_source, language_source_len,
            8000000u, 16000000u, &wire_status,
            error_buf, error_buf_size) ||
        !cetta_language_def_core_v1_decode(
            &runtime->language, &runtime->wire, 2000000u,
            &core_status, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(error_buf, error_buf_size,
                           "invalid JSON LanguageDef source: %s",
                           cetta_op_lang_v1_status_name(wire_status));
        }
        goto failed;
    }
    if (!cetta_op_lang_v1_parse_document_bytes(
            &runtime->profile_document, profile_source, profile_source_len,
            4000000u, 8000000u, &wire_status,
            error_buf, error_buf_size) ||
        !cetta_ld_parser_profile_v1_decode(
            &runtime->profile, &runtime->profile_document, 200000u,
            &pack_status, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(error_buf, error_buf_size,
                           "invalid JSON parser profile: %s",
                           cetta_ld_parser_pack_v1_status_name(pack_status));
        }
        goto failed;
    }
    if (!cetta_op_lang_v1_parse_bytes(
            &runtime->target_wire, target_source, target_source_len,
            8000000u, 16000000u, &wire_status,
            error_buf, error_buf_size) ||
        !cetta_language_def_core_v1_decode(
            &runtime->target_language, &runtime->target_wire, 2000000u,
            &core_status, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(error_buf, error_buf_size,
                           "invalid JSON target LanguageDef: %s",
                           cetta_op_lang_v1_status_name(wire_status));
        }
        goto failed;
    }
    if (!cetta_json_elaboration_plan_v1_compile(
            &runtime->elaboration_plan,
            &runtime->language, runtime->wire.source_sha256,
            &runtime->profile,
            &runtime->target_language, runtime->target_wire.source_sha256,
            &plan_status, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(
                error_buf, error_buf_size,
                "JSON presentations are outside the elaboration profile: %s",
                cetta_json_elaboration_plan_v1_status_name(plan_status));
        }
        goto failed;
    }
    if (!cetta_language_def_parser_pack_v1_compile(
            &runtime->compiled, &runtime->language,
            runtime->wire.source_sha256, &runtime->profile,
            4000000u, &pack_status, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(error_buf, error_buf_size,
                           "JSON LanguageDef is outside the parser fragment: %s",
                           cetta_ld_parser_pack_v1_status_name(pack_status));
        }
        goto failed;
    }
    if (!ppnative_v1_prepare(
            &runtime->prepared, &runtime->compiled.pack,
            runtime->compiled.start_state, error_buf, error_buf_size)) {
        goto failed;
    }
    return runtime;

failed:
    cetta_json_runtime_v1_free(runtime);
    return NULL;
}

static bool json_results_equal(const PPNativeV1Result *left,
                               const PPNativeV1Result *right) {
    uint32_t index;
    if (!left || !right || left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

static CettaJsonRuntimeV1Status json_parser_outcome_status(
    PPNativeV1Outcome outcome) {
    switch (outcome) {
    case PPNATIVE_V1_COMPLETED:
        return CETTA_JSON_RUNTIME_V1_OK;
    case PPNATIVE_V1_RECOGNIZER_LIMIT:
    case PPNATIVE_V1_REPLAY_DEPTH:
    case PPNATIVE_V1_RESULT_LIMIT:
        return CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT;
    case PPNATIVE_V1_UNSUPPORTED_OPEN_PACK:
        return CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE;
    }
    return CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE;
}

static Atom *json_runtime_single_cst(const PPNativeV1Result *result) {
    Atom *answer;
    if (!result || result->semantic_result_len != 1u) return NULL;
    answer = result->semantic_results[0];
    if (!answer || answer->kind != ATOM_EXPR || answer->expr.len != 3u ||
        !atom_is_symbol(answer->expr.elems[0], "result") ||
        !atom_is_symbol(answer->expr.elems[2], "nil")) {
        return NULL;
    }
    return answer->expr.elems[1];
}

bool cetta_json_runtime_v1_parse(
    const CettaJsonRuntimeV1 *runtime,
    Arena *arena,
    const uint8_t *json_bytes,
    size_t json_byte_len,
    const CettaJsonRuntimeV1Limits *limits,
    Atom **out,
    CettaJsonRuntimeV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaJsonRuntimeV1Limits defaults;
    const CettaJsonRuntimeV1Limits *active = limits;
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    const PPNativeV1Result *selected;
    CettaJsonCstValueV1Status value_status =
        CETTA_JSON_CST_VALUE_V1_BAD_ARGUMENT;
    ArenaMark mark;
    Atom *value = NULL;
    bool ok = false;

    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    if (!runtime || !arena || (!json_bytes && json_byte_len > 0u) || !out) {
        json_runtime_set_error(error_buf, error_buf_size,
                               "bad JSON parse arguments");
        return false;
    }
    if (!active) {
        cetta_json_runtime_v1_default_limits(&defaults);
        active = &defaults;
    }
    if (active->recognizer_work_limit == 0u ||
        active->replay_depth_limit == 0u || active->result_limit == 0u ||
        active->elaboration_work_limit == 0u ||
        active->value_depth_limit == 0u ||
        (active->kernel != CETTA_JSON_KERNEL_V1_PACKED_GLL &&
         active->kernel != CETTA_JSON_KERNEL_V1_PACKED_GLR &&
         active->kernel != CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL)) {
        json_runtime_set_error(error_buf, error_buf_size,
                               "invalid JSON resource limits or kernel package");
        return false;
    }

    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    mark = arena_mark(arena);
    if (active->kernel == CETTA_JSON_KERNEL_V1_PACKED_GLL ||
        active->kernel == CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL) {
        if (!ppgll_v1_prepared_parse(
                &runtime->prepared, json_bytes, json_byte_len,
                active->recognizer_work_limit, active->replay_depth_limit,
                active->result_limit, &gll, error_buf, error_buf_size)) {
            if (status) {
                *status = error_buf && strstr(error_buf, "UTF-8")
                    ? CETTA_JSON_RUNTIME_V1_INVALID_UTF8
                    : CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE;
            }
            goto done;
        }
        if (gll.outcome != PPNATIVE_V1_COMPLETED) {
            if (status) *status = json_parser_outcome_status(gll.outcome);
            json_runtime_set_error(
                error_buf, error_buf_size,
                gll.detail[0] ? gll.detail : "JSON parser resource limit");
            goto done;
        }
    }
    if (active->kernel == CETTA_JSON_KERNEL_V1_PACKED_GLR ||
        active->kernel == CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL) {
        if (!ppglr_v1_prepared_parse(
                &runtime->prepared, json_bytes, json_byte_len,
                active->recognizer_work_limit, active->replay_depth_limit,
                active->result_limit, &glr, error_buf, error_buf_size)) {
            if (status) {
                *status = error_buf && strstr(error_buf, "UTF-8")
                    ? CETTA_JSON_RUNTIME_V1_INVALID_UTF8
                    : CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE;
            }
            goto done;
        }
        if (glr.outcome != PPNATIVE_V1_COMPLETED) {
            if (status) *status = json_parser_outcome_status(glr.outcome);
            json_runtime_set_error(
                error_buf, error_buf_size,
                glr.detail[0] ? glr.detail : "JSON parser resource limit");
            goto done;
        }
    }
    if (active->kernel == CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL) {
        if (!json_results_equal(&gll, &glr)) {
            if (status) *status = CETTA_JSON_RUNTIME_V1_BACKEND_DISAGREEMENT;
            json_runtime_set_error(
                error_buf, error_buf_size,
                "prepared GLL and GLR disagree on JSON observation");
            goto done;
        }
    }
    selected = active->kernel == CETTA_JSON_KERNEL_V1_PACKED_GLR
        ? &glr : &gll;
    if (!selected->accepted) {
        if (status) *status = CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED;
        json_runtime_set_error(error_buf, error_buf_size,
                               "JSON syntax rejected by authored grammar");
        goto done;
    }
    if (selected->semantic_result_len != 1u) {
        if (status) *status = CETTA_JSON_RUNTIME_V1_AMBIGUOUS;
        json_runtime_set_error(error_buf, error_buf_size,
                               "JSON grammar produced a non-unique semantic result");
        goto done;
    }
    if (!cetta_json_cst_value_v1_elaborate(
            &runtime->elaboration_plan,
            arena, json_runtime_single_cst(selected),
            active->elaboration_work_limit, active->value_depth_limit,
            &value, &value_status, error_buf, error_buf_size)) {
        if (status) {
            switch (value_status) {
            case CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE:
                *status = CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE;
                break;
            case CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT:
                *status = CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT;
                break;
            case CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE:
                *status = CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE;
                break;
            case CETTA_JSON_CST_VALUE_V1_MALFORMED_CST:
                *status = CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE;
                break;
            case CETTA_JSON_CST_VALUE_V1_BAD_ARGUMENT:
            case CETTA_JSON_CST_VALUE_V1_OK:
                *status = CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE;
                break;
            }
        }
        goto done;
    }
    *out = value;
    if (status) *status = CETTA_JSON_RUNTIME_V1_OK;
    ok = true;

done:
    if (!ok) arena_reset(arena, mark);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    return ok;
}

uint32_t cetta_json_runtime_v1_table_build_count(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime
        ? ppnative_v1_prepared_table_build_count(&runtime->prepared)
        : 0u;
}

const char *cetta_json_runtime_v1_language_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->compiled.language_source_sha256 : NULL;
}

const char *cetta_json_runtime_v1_profile_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->compiled.profile_source_sha256 : NULL;
}

const char *cetta_json_runtime_v1_target_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->elaboration_plan.target_sha256 : NULL;
}

const CettaJsonElaborationPlanV1 *
cetta_json_runtime_v1_elaboration_plan(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? &runtime->elaboration_plan : NULL;
}

const char *cetta_json_runtime_v1_binding_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->compiled.binding_sha256 : NULL;
}

const char *cetta_json_runtime_v1_compiler_contract_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->compiled.compiler_sha256 : NULL;
}

const char *cetta_json_runtime_v1_environment_contract_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->compiled.pack.environment_digest : NULL;
}

const char *cetta_json_runtime_v1_parser_pack_digest(
    const CettaJsonRuntimeV1 *runtime) {
    return runtime ? runtime->compiled.pack.pack_digest : NULL;
}

const char *cetta_json_runtime_v1_status_name(
    CettaJsonRuntimeV1Status status) {
    switch (status) {
    case CETTA_JSON_RUNTIME_V1_OK: return "ok";
    case CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_JSON_RUNTIME_V1_INVALID_LANGUAGE_SOURCE:
        return "invalid-language-source";
    case CETTA_JSON_RUNTIME_V1_INVALID_TARGET_SOURCE:
        return "invalid-target-source";
    case CETTA_JSON_RUNTIME_V1_OUTSIDE_LANGUAGE_FRAGMENT:
        return "outside-language-fragment";
    case CETTA_JSON_RUNTIME_V1_OUTSIDE_ELABORATION_PROFILE:
        return "outside-elaboration-profile";
    case CETTA_JSON_RUNTIME_V1_PREPARATION_FAILURE:
        return "preparation-failure";
    case CETTA_JSON_RUNTIME_V1_INVALID_UTF8: return "invalid-utf8";
    case CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED: return "syntax-rejected";
    case CETTA_JSON_RUNTIME_V1_AMBIGUOUS: return "ambiguous";
    case CETTA_JSON_RUNTIME_V1_BACKEND_DISAGREEMENT:
        return "backend-disagreement";
    case CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT: return "resource-limit";
    case CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE:
        return "invalid-unicode-escape";
    case CETTA_JSON_RUNTIME_V1_MALFORMED_VALUE: return "malformed-value";
    case CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    case CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE: return "internal-failure";
    }
    return "unknown";
}

const char *cetta_json_kernel_v1_name(CettaJsonKernelV1 kernel) {
    switch (kernel) {
    case CETTA_JSON_KERNEL_V1_PACKED_GLL:
        return "packed-gll";
    case CETTA_JSON_KERNEL_V1_PACKED_GLR:
        return "packed-glr";
    case CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL:
        return "packed-gll-glr-dual";
    }
    return "unknown";
}
