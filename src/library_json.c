#include "library_json.h"

#include "json_embedded_sources_v1.h"
#include "native/json_nik_v1.h"
#include "native/json_value_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaJsonLibraryRuntimeV1 {
    CettaJsonNikV1 *parser_host;
};

static Atom *json_call(Arena *arena, Atom *head,
                       Atom **args, uint32_t nargs) {
    Atom **items;
    uint32_t index;
    items = (Atom **)arena_alloc(
        arena, sizeof(*items) * ((size_t)nargs + 1u));
    items[0] = head;
    for (index = 0u; index < nargs; index++) items[index + 1u] = args[index];
    return atom_expr(arena, items, (CettaExprLen)nargs + 1u);
}

static Atom *json_error(Arena *arena, Atom *head,
                        Atom **args, uint32_t nargs,
                        const char *message) {
    return atom_error(arena, json_call(arena, head, args, nargs),
                      atom_string(arena, message ? message : "JSON failure"));
}

static Atom *json_failure(Arena *arena, Atom *head,
                          Atom **args, uint32_t nargs,
                          const char *phase,
                          const char *status,
                          const char *message) {
    Atom *quoted_call_items[2] = {
        atom_symbol(arena, "quote"),
        json_call(arena, head, args, nargs),
    };
    Atom *items[5] = {
        atom_symbol(arena, "JsonFailureV1"),
        atom_symbol(arena, phase ? phase : "JsonOperationV1"),
        atom_symbol(arena, status ? status : "JsonInternalFailureV1"),
        atom_expr(arena, quoted_call_items, 2u),
        atom_string(arena, message ? message : "JSON failure"),
    };
    return atom_expr(arena, items, 5u);
}

static Atom *json_failure_or_error(Arena *arena, Atom *head,
                                   Atom **args, uint32_t nargs,
                                   bool legacy,
                                   const char *phase,
                                   const char *status,
                                   const char *message) {
    return legacy
        ? json_error(arena, head, args, nargs, message)
        : json_failure(arena, head, args, nargs, phase, status, message);
}

static const char *json_runtime_failure_symbol(
    CettaJsonRuntimeV1Status status) {
    switch (status) {
    case CETTA_JSON_RUNTIME_V1_OK: return "JsonOkV1";
    case CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT: return "JsonBadArgumentV1";
    case CETTA_JSON_RUNTIME_V1_INVALID_LANGUAGE_SOURCE:
        return "JsonInvalidLanguageSourceV1";
    case CETTA_JSON_RUNTIME_V1_INVALID_TARGET_SOURCE:
        return "JsonInvalidTargetSourceV1";
    case CETTA_JSON_RUNTIME_V1_OUTSIDE_LANGUAGE_FRAGMENT:
        return "JsonOutsideLanguageFragmentV1";
    case CETTA_JSON_RUNTIME_V1_OUTSIDE_ELABORATION_PROFILE:
        return "JsonOutsideElaborationProfileV1";
    case CETTA_JSON_RUNTIME_V1_PREPARATION_FAILURE:
        return "JsonPreparationFailureV1";
    case CETTA_JSON_RUNTIME_V1_INVALID_UTF8: return "JsonInvalidUtf8V1";
    case CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED:
        return "JsonSyntaxRejectedV1";
    case CETTA_JSON_RUNTIME_V1_AMBIGUOUS: return "JsonAmbiguousV1";
    case CETTA_JSON_RUNTIME_V1_BACKEND_DISAGREEMENT:
        return "JsonBackendDisagreementV1";
    case CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT:
        return "JsonResourceLimitV1";
    case CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE:
        return "JsonInvalidUnicodeEscapeV1";
    case CETTA_JSON_RUNTIME_V1_MALFORMED_VALUE:
        return "JsonMalformedValueV1";
    case CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE:
        return "JsonAllocationFailureV1";
    case CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE:
        return "JsonInternalFailureV1";
    }
    return "JsonUnknownFailureV1";
}

static const char *json_value_failure_symbol(CettaJsonValueV1Status status) {
    switch (status) {
    case CETTA_JSON_VALUE_V1_OK: return "JsonOkV1";
    case CETTA_JSON_VALUE_V1_BAD_ARGUMENT: return "JsonBadArgumentV1";
    case CETTA_JSON_VALUE_V1_MALFORMED_VALUE:
        return "JsonMalformedValueV1";
    case CETTA_JSON_VALUE_V1_INVALID_UTF8: return "JsonInvalidUtf8V1";
    case CETTA_JSON_VALUE_V1_UNREPRESENTABLE_LEGACY_STRING:
        return "JsonUnrepresentableLegacyStringV1";
    case CETTA_JSON_VALUE_V1_RESOURCE_LIMIT:
        return "JsonResourceLimitV1";
    case CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE:
        return "JsonAllocationFailureV1";
    case CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT:
        return "JsonRoundtripDisagreementV1";
    }
    return "JsonUnknownFailureV1";
}

static const char *json_text(Atom *atom) {
    if (!atom) return NULL;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
    if (atom->kind == ATOM_SYMBOL) return atom_name_cstr(atom);
    return NULL;
}

static bool json_expr_is(Atom *atom, const char *head, CettaExprLen len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool json_zero_args(Atom **args, uint32_t nargs) {
    return nargs == 0u ||
        (nargs == 1u && args[0] && args[0]->kind == ATOM_EXPR &&
         args[0]->expr.len == 0u);
}

CettaJsonLibraryRuntimeV1 *cetta_json_library_runtime_v1_new(
    char *error_buf, size_t error_buf_size) {
    CettaJsonNikV1Admission admission;
    CettaJsonLibraryRuntimeV1 *runtime =
        (CettaJsonLibraryRuntimeV1 *)calloc(1u, sizeof(*runtime));
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (!runtime) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "out of memory allocating JSON library runtime");
        }
        return NULL;
    }
    admission = cetta_json_nik_v1_admit(
        cetta_rfc8259_json_language_v1_source,
        cetta_rfc8259_json_language_v1_source_len,
        cetta_rfc8259_json_profile_v1_source,
        cetta_rfc8259_json_profile_v1_source_len,
        cetta_rfc8259_json_value_target_v1_source,
        cetta_rfc8259_json_value_target_v1_source_len,
        error_buf, error_buf_size);
    if (admission.kind != CETTA_NIK_HOST_ADMISSION_ADMITTED_V1 ||
        !admission.host) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(
                error_buf, error_buf_size,
                "JSON NIK could not admit the prepared parser realization");
        }
        free(runtime);
        return NULL;
    }
    runtime->parser_host = admission.host;
    return runtime;
}

void cetta_json_library_runtime_v1_free(CettaJsonLibraryRuntimeV1 *runtime) {
    if (!runtime) return;
    cetta_json_nik_v1_destroy(runtime->parser_host);
    runtime->parser_host = NULL;
    free(runtime);
}

static Atom *json_capabilities(CettaJsonLibraryRuntimeV1 *runtime,
                               Arena *arena, Atom *head,
                               Atom **args, uint32_t nargs) {
    const CettaJsonRuntimeV1 *parser =
        cetta_json_nik_v1_borrow_selected_runtime(runtime->parser_host);
    Atom *items[12];
    Atom *language_items[2];
    Atom *profile_items[2];
    Atom *binding_items[2];
    Atom *compiler_items[2];
    Atom *environment_items[2];
    Atom *pack_items[2];
    Atom *tables_items[2];
    Atom *backend_values[2];
    Atom *backend_items[2];
    Atom *production_items[2];
    if (!json_zero_args(args, nargs)) {
        return json_failure(
            arena, head, args, nargs,
            "JsonCapabilitiesV1", "JsonBadArgumentV1",
            "expected: (json:capabilities)");
    }
    if (!parser) {
        return json_failure(
            arena, head, args, nargs,
            "JsonCapabilitiesV1", "JsonInternalFailureV1",
            "JSON NIK has no selected prepared parser realization");
    }
    language_items[0] = atom_symbol(arena, "language-source-sha256");
    language_items[1] = atom_string(
        arena, cetta_json_runtime_v1_language_digest(parser));
    profile_items[0] = atom_symbol(arena, "profile-source-sha256");
    profile_items[1] = atom_string(
        arena, cetta_json_runtime_v1_profile_digest(parser));
    binding_items[0] = atom_symbol(arena, "binding-sha256");
    binding_items[1] = atom_string(
        arena, cetta_json_runtime_v1_binding_digest(parser));
    compiler_items[0] = atom_symbol(arena, "compiler-contract-sha256");
    compiler_items[1] = atom_string(
        arena,
        cetta_json_runtime_v1_compiler_contract_digest(parser));
    environment_items[0] = atom_symbol(
        arena, "parser-environment-contract-sha256");
    environment_items[1] = atom_string(
        arena,
        cetta_json_runtime_v1_environment_contract_digest(parser));
    pack_items[0] = atom_symbol(arena, "parser-pack-sha256");
    pack_items[1] = atom_string(
        arena, cetta_json_runtime_v1_parser_pack_digest(parser));
    tables_items[0] = atom_symbol(arena, "prepared-table-builds");
    tables_items[1] = atom_int(
        arena, (int64_t)cetta_json_runtime_v1_table_build_count(parser));
    items[0] = atom_symbol(arena, "JsonCapabilitiesV1");
    items[1] = atom_symbol(arena, "AuthoredLanguageDef");
    items[2] = atom_symbol(arena, "JsonOrderedOccurrenceValueV1");
    backend_values[0] = atom_symbol(arena, "GLLV1");
    backend_values[1] = atom_symbol(arena, "GLRV1");
    backend_items[0] = atom_symbol(arena, "prepared-backends");
    backend_items[1] = atom_expr(arena, backend_values, 2u);
    items[3] = atom_expr(arena, backend_items, 2u);
    production_items[0] = atom_symbol(arena, "production-backend");
    production_items[1] = atom_symbol(
        arena, cetta_json_nik_v1_production_kernel(runtime->parser_host) ==
                CETTA_JSON_KERNEL_V1_PACKED_GLL
            ? "GLLV1" : "GLRV1");
    items[4] = atom_expr(arena, production_items, 2u);
    items[5] = atom_expr(arena, language_items, 2u);
    items[6] = atom_expr(arena, profile_items, 2u);
    items[7] = atom_expr(arena, compiler_items, 2u);
    items[8] = atom_expr(arena, environment_items, 2u);
    items[9] = atom_expr(arena, pack_items, 2u);
    items[10] = atom_expr(arena, binding_items, 2u);
    items[11] = atom_expr(arena, tables_items, 2u);
    return atom_expr(arena, items, 12u);
}

static Atom *json_parse(CettaJsonLibraryRuntimeV1 *runtime,
                        Arena *arena, Atom *head,
                        Atom **args, uint32_t nargs,
                        bool legacy) {
    const char *source;
    Atom *canonical = NULL;
    Atom *result = NULL;
    CettaJsonRuntimeV1Status parse_status;
    CettaJsonValueV1Status value_status;
    char error[512] = {0};
    if (nargs != 1u || !(source = json_text(args[0]))) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy,
            "JsonParseV1", "JsonBadArgumentV1",
            "expected one JSON text argument");
    }
    if (!cetta_json_nik_v1_parse_prepared(
            runtime->parser_host, arena,
            (const uint8_t *)source, strlen(source), NULL,
            &canonical, &parse_status, error, sizeof(error))) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy,
            "JsonParseV1", json_runtime_failure_symbol(parse_status),
            error[0] ? error : cetta_json_runtime_v1_status_name(parse_status));
    }
    if (!legacy) return canonical;
    if (!cetta_json_value_v1_to_legacy(
            arena, canonical, 1000000u, 1024u, &result,
            &value_status, error, sizeof(error))) {
        return json_error(
            arena, head, args, nargs,
            error[0] ? error : cetta_json_value_v1_status_name(value_status));
    }
    return result;
}

static Atom *json_stringify(CettaJsonLibraryRuntimeV1 *runtime,
                            Arena *arena, Atom *head,
                            Atom **args, uint32_t nargs,
                            bool legacy) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    CettaJsonValueV1Status status;
    const CettaJsonRuntimeV1 *parser =
        cetta_json_nik_v1_borrow_selected_runtime(runtime->parser_host);
    Atom *result;
    char error[512] = {0};
    if (nargs != 1u) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy,
            "JsonStringifyV1", "JsonBadArgumentV1",
            "expected one JSON value argument");
    }
    if (!parser) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy,
            "JsonStringifyV1", "JsonInternalFailureV1",
            "JSON NIK has no selected prepared parser realization");
    }
    if (!cetta_json_value_v1_stringify(
            parser, args[0], legacy,
            4000000u, 1024u, 16u * 1024u * 1024u,
            &bytes, &len, &status, error, sizeof(error))) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy,
            "JsonStringifyV1", json_value_failure_symbol(status),
            error[0] ? error : cetta_json_value_v1_status_name(status));
    }
    if (len > SIZE_MAX - 1u) {
        free(bytes);
        return json_failure_or_error(
            arena, head, args, nargs, legacy,
            "JsonStringifyV1", "JsonResourceLimitV1",
            "JSON output is too large");
    }
    {
        uint8_t *terminated = (uint8_t *)realloc(bytes, len + 1u);
        if (!terminated) {
            free(bytes);
            return json_failure_or_error(
                arena, head, args, nargs, legacy,
                "JsonStringifyV1", "JsonAllocationFailureV1",
                "out of memory materializing JSON text");
        }
        bytes = terminated;
    }
    bytes[len] = '\0';
    result = atom_string(arena, (const char *)bytes);
    free(bytes);
    return result;
}

static Atom *json_canonical_key(Arena *arena, Atom *key,
                                char *error, size_t error_size) {
    const char *text = json_text(key);
    Atom *legacy_items[2];
    Atom *legacy;
    Atom *canonical = NULL;
    CettaJsonValueV1Status status;
    if (!text) {
        (void)snprintf(error, error_size, "expected a JSON object key");
        return NULL;
    }
    legacy_items[0] = atom_symbol(arena, "JsonString");
    legacy_items[1] = atom_string(arena, text);
    legacy = atom_expr(arena, legacy_items, 2u);
    if (!cetta_json_value_v1_from_legacy(
            arena, legacy, 100000u, 128u, &canonical,
            &status, error, error_size)) {
        return NULL;
    }
    return canonical;
}

static Atom *json_lookup(CettaJsonLibraryRuntimeV1 *runtime,
                         Arena *arena, Atom *head,
                         Atom **args, uint32_t nargs,
                         int mode, bool legacy_codec) {
    Atom *object = nargs > 0u ? args[0] : NULL;
    Atom *canonical = object;
    Atom *key;
    Atom *members;
    Atom **matches = NULL;
    uint32_t match_len = 0u;
    CettaExprIndex index;
    CettaJsonValueV1Status status;
    char error[512] = {0};
    (void)runtime;
    if (nargs != 2u) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy_codec,
            "JsonLookupV1", "JsonBadArgumentV1",
            "expected JSON object and key");
    }
    if (legacy_codec && !cetta_json_value_v1_from_legacy(
            arena, object, 1000000u, 1024u, &canonical,
            &status, error, sizeof(error))) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy_codec,
            "JsonLookupV1", json_value_failure_symbol(status),
            error[0] ? error : "malformed legacy JSON object");
    }
    if (!json_expr_is(canonical, "JsonObjectV1", 2u) ||
        !(members = canonical->expr.elems[1]) ||
        members->kind != ATOM_EXPR ||
        !(key = json_canonical_key(arena, args[1], error, sizeof(error)))) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy_codec,
            "JsonLookupV1", "JsonMalformedValueV1",
            error[0] ? error : "expected a canonical JSON object");
    }
    matches = (Atom **)calloc(members->expr.len ? members->expr.len : 1u,
                              sizeof(*matches));
    if (!matches) {
        return json_failure_or_error(
            arena, head, args, nargs, legacy_codec,
            "JsonLookupV1", "JsonAllocationFailureV1",
            "out of memory collecting JSON members");
    }
    for (index = 0u; index < members->expr.len; index++) {
        Atom *member = members->expr.elems[index];
        if (json_expr_is(member, "JsonMemberV1", 5u) &&
            atom_eq(member->expr.elems[2], key)) {
            matches[match_len++] = member;
        }
    }
    if (legacy_codec) {
        Atom *result;
        if (match_len == 0u) {
            free(matches);
            return atom_symbol(arena, "JsonNull");
        }
        if (!cetta_json_value_v1_to_legacy(
                arena, matches[0]->expr.elems[3], 1000000u, 1024u,
                &result, &status, error, sizeof(error))) {
            free(matches);
            return json_error(arena, head, args, nargs,
                              error[0] ? error : "legacy JSON projection failed");
        }
        free(matches);
        return result;
    }
    if (mode == 0) {
        Atom **values = (Atom **)arena_alloc(
            arena, sizeof(*values) * (match_len ? match_len : 1u));
        Atom *items[2];
        uint32_t match_index;
        for (match_index = 0u; match_index < match_len; match_index++) {
            values[match_index] = atom_deep_copy(
                arena, matches[match_index]->expr.elems[3]);
        }
        items[0] = atom_symbol(arena, "JsonMatchesV1");
        items[1] = atom_expr(arena, values, match_len);
        free(matches);
        return atom_expr(arena, items, 2u);
    }
    if (match_len == 0u) {
        free(matches);
        return atom_symbol(arena, "JsonMissingV1");
    }
    {
        Atom *selected = mode < 0 ? matches[0] : matches[match_len - 1u];
        Atom *items[3] = {
            atom_symbol(arena, "JsonFoundV1"),
            atom_deep_copy(arena, selected->expr.elems[1]),
            atom_deep_copy(arena, selected->expr.elems[3]),
        };
        free(matches);
        return atom_expr(arena, items, 3u);
    }
}

static Atom *json_members(Arena *arena, Atom *head,
                          Atom **args, uint32_t nargs) {
    Atom *members;
    Atom *items[2];
    if (nargs != 1u || !json_expr_is(args[0], "JsonObjectV1", 2u) ||
        !(members = args[0]->expr.elems[1]) || members->kind != ATOM_EXPR) {
        return json_failure(
            arena, head, args, nargs,
            "JsonMembersV1", "JsonMalformedValueV1",
            "expected one canonical JsonObjectV1");
    }
    items[0] = atom_symbol(arena, "JsonMembersV1");
    items[1] = atom_deep_copy(arena, members);
    return atom_expr(arena, items, 2u);
}

Atom *cetta_json_library_dispatch_v1(
    CettaJsonLibraryRuntimeV1 *runtime, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs) {
    SymbolId id;
    if (!runtime || !runtime->parser_host || !arena || !head ||
        head->kind != ATOM_SYMBOL) return NULL;
    id = head->sym_id;
    if (id == g_builtin_syms.lib_json_capabilities_v1)
        return json_capabilities(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_json_parse_v1)
        return json_parse(runtime, arena, head, args, nargs, false);
    if (id == g_builtin_syms.lib_json_parse)
        return json_parse(runtime, arena, head, args, nargs, true);
    if (id == g_builtin_syms.lib_json_stringify_v1)
        return json_stringify(runtime, arena, head, args, nargs, false);
    if (id == g_builtin_syms.lib_json_stringify)
        return json_stringify(runtime, arena, head, args, nargs, true);
    if (id == g_builtin_syms.lib_json_members_v1)
        return json_members(arena, head, args, nargs);
    if (id == g_builtin_syms.lib_json_get_all_v1)
        return json_lookup(runtime, arena, head, args, nargs, 0, false);
    if (id == g_builtin_syms.lib_json_get_first_v1)
        return json_lookup(runtime, arena, head, args, nargs, -1, false);
    if (id == g_builtin_syms.lib_json_get_last_v1)
        return json_lookup(runtime, arena, head, args, nargs, 1, false);
    if (id == g_builtin_syms.lib_json_object_get)
        return json_lookup(runtime, arena, head, args, nargs, -1, true);
    return NULL;
}
