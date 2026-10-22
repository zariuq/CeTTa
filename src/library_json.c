#include "library_json.h"

#include "eval.h"
#include "json_embedded_sources_v1.h"
#include "lang.h"
#include "petta_semantics.h"
#include "native/json_nik_v1.h"
#include "native/json_value_v1.h"
#include "native/json_direct_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaJsonLibraryRuntimeV1 {
    CettaJsonNikV1 *parser_host;
    pthread_mutex_t reference_mutex;
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

/* A JSON value is handed back as data in the running language's own terms.
 * HE holds the failing call inside evidence with quote, since HE would
 * evaluate it; PeTTa never evaluates a returned value, so the call itself
 * is the datum.  PeTTa's booleans are true and false. */
static bool json_lane_is_petta(void) {
    return eval_current_language_id &&
           eval_current_language_id() == CETTA_LANGUAGE_PETTA;
}

/* The codec's boolean is HE's (a grounded bool, or True/False); PeTTa's is
 * the symbol true/false.  The library converts at its boundary: the codec
 * only ever sees its own representation, the program its language's. */
static bool json_codec_boolean(Atom *atom, bool *value) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BOOL) {
        *value = atom->ground.bval;
        return true;
    }
    if (atom_is_symbol(atom, "True") || atom_is_symbol(atom, "False")) {
        *value = atom_is_symbol(atom, "True");
        return true;
    }
    return false;
}

static bool json_petta_boolean(Atom *atom, bool *value) {
    if (atom_is_symbol(atom, "true") || atom_is_symbol(atom, "false")) {
        *value = atom_is_symbol(atom, "true");
        return true;
    }
    return false;
}

static Atom *json_respell_booleans(Arena *arena, Atom *atom, bool to_petta) {
    typedef struct { Atom *source; Atom **items; CettaExprIndex next; } Frame;
    Frame *frames = NULL;
    size_t len = 0u, cap = 0u;
    Atom *current = atom;
    ArenaMark mark = arena_mark(arena);
    for (;;) {
        bool value = false;
        if (to_petta && current && current->kind == ATOM_GROUNDED &&
            current->ground.gkind == GV_BOOL) {
            current = atom_symbol(
                arena, current->ground.bval ? "true" : "false");
        } else if (!to_petta && json_petta_boolean(current, &value)) {
            current = atom_bool(arena, value);
        } else if (current && current->kind == ATOM_EXPR) {
            if (current->expr.len == 2u &&
                atom_is_symbol(current->expr.elems[0], "JsonBoolV1") &&
                (to_petta ? json_codec_boolean(current->expr.elems[1], &value)
                          : json_petta_boolean(current->expr.elems[1], &value))) {
                Atom *items[] = {current->expr.elems[0],
                    to_petta ? atom_symbol(arena, value ? "true" : "false")
                             : atom_bool(arena, value)};
                current = atom_expr(arena, items, 2u);
            } else if (current->expr.len) {
                if (len == cap) {
                    size_t next = cap ? cap * 2u : 16u;
                    if (next < cap || next > SIZE_MAX / sizeof(*frames)) goto allocation;
                    Frame *grown = realloc(frames, next * sizeof(*frames));
                    if (!grown) goto allocation;
                    frames = grown; cap = next;
                }
                frames[len++] = (Frame){.source = current, .next = 1u};
                current = current->expr.elems[0];
                continue;
            }
        }
        while (len) {
            Frame *frame = &frames[len-1u];
            CettaExprIndex index = frame->next - 1u;
            if (current != frame->source->expr.elems[index] && !frame->items) {
                frame->items = arena_alloc(arena, sizeof(*frame->items) * (size_t)frame->source->expr.len);
                memcpy(frame->items, frame->source->expr.elems,
                       sizeof(*frame->items) * (size_t)frame->source->expr.len);
            }
            if (frame->items) frame->items[index] = current;
            if (frame->next < frame->source->expr.len) {
                current = frame->source->expr.elems[frame->next++];
                break;
            }
            current = frame->items ? atom_expr(arena, frame->items, frame->source->expr.len) : frame->source;
            len--;
        }
        if (!len) { free(frames); return current; }
    }
allocation:
    free(frames);
    arena_reset(arena, mark);
    return atom_error(arena, atom, atom_string(arena, "out of memory converting JSON booleans"));
}

/* A value leaving the codec, in the running language's spelling. */
static Atom *json_lane_value(Arena *arena, Atom *value) {
    return json_lane_is_petta()
        ? json_respell_booleans(arena, value, true)
        : value;
}

/* A value entering the codec, in the codec's own spelling. */
static Atom *json_codec_value(Arena *arena, Atom *value) {
    /* Normalize evaluated PeTTa cons carriers at the language boundary.
     * Summary checks leave ordinary parsed values allocation-free here. */
    if (json_lane_is_petta() &&
        petta_semantics_value_contains_observable_open_cons(value)) {
        value = petta_semantics_materialize_value(arena, value);
        if (!value) return NULL;
    }
    return json_lane_is_petta()
        ? json_respell_booleans(arena, value, false)
        : value;
}

static Atom *json_failure(Arena *arena, Atom *head,
                          Atom **args, uint32_t nargs,
                          const char *phase,
                          const char *status,
                          const char *message) {
    Atom *call = json_call(arena, head, args, nargs);
    Atom *quoted_call_items[2] = {
        atom_symbol(arena, "quote"),
        call,
    };
    Atom *items[5] = {
        atom_symbol(arena, "JsonFailureV1"),
        atom_symbol(arena, phase ? phase : "JsonOperationV1"),
        atom_symbol(arena, status ? status : "JsonInternalFailureV1"),
        json_lane_is_petta() ? call : atom_expr(arena, quoted_call_items, 2u),
        atom_string(arena, message ? message : "JSON failure"),
    };
    return atom_expr(arena, items, 5u);
}

static Atom *json_failure_or_error(Arena *arena, Atom *head,
                                   Atom **args, uint32_t nargs,
                                   bool ordinary,
                                   const char *phase,
                                   const char *status,
                                   const char *message) {
    (void)ordinary;
    return json_failure(arena, head, args, nargs, phase, status, message);
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
    return atom && atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING ? atom->ground.sval : NULL;
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
    if (pthread_mutex_init(&runtime->reference_mutex, NULL) != 0) {
        free(runtime);
        if (error_buf && error_buf_size)
            snprintf(error_buf, error_buf_size, "cannot initialize JSON reference owner");
        return NULL;
    }
    return runtime;
}

/* Grammar/forest certification is explicitly requested, never paid for by
 * ordinary data ingestion or emission. */
static bool json_reference_prepare(CettaJsonLibraryRuntimeV1 *runtime,
                                   char *error_buf, size_t error_buf_size) {
    pthread_mutex_lock(&runtime->reference_mutex);
    if (runtime->parser_host) {
        pthread_mutex_unlock(&runtime->reference_mutex);
        return true;
    }
    CettaJsonNikV1Admission admission = cetta_json_nik_v1_admit(
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
        pthread_mutex_unlock(&runtime->reference_mutex);
        return false;
    }
    runtime->parser_host = admission.host;
    pthread_mutex_unlock(&runtime->reference_mutex);
    return true;
}

void cetta_json_library_runtime_v1_free(CettaJsonLibraryRuntimeV1 *runtime) {
    if (!runtime) return;
    cetta_json_nik_v1_destroy(runtime->parser_host);
    runtime->parser_host = NULL;
    pthread_mutex_destroy(&runtime->reference_mutex);
    free(runtime);
}

static Atom *json_capabilities(CettaJsonLibraryRuntimeV1 *runtime,
                               Arena *arena, Atom *head,
                               Atom **args, uint32_t nargs) {
    char error[512] = {0};
    if (!json_reference_prepare(runtime, error, sizeof(error)))
        return json_failure(arena, head, args, nargs, "JsonCapabilitiesV1",
                            "JsonPreparationFailureV1", error);
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
    items[2] = atom_expr3(arena, atom_symbol(arena, "JsonValueViewsV1"),
        atom_symbol(arena, "JsonOrdinaryV1"), atom_symbol(arena, "JsonOrderedOccurrenceValueV1"));
    backend_values[0] = atom_symbol(arena, "GLLV1");
    backend_values[1] = atom_symbol(arena, "GLRV1");
    backend_items[0] = atom_symbol(arena, "prepared-backends");
    backend_items[1] = atom_expr(arena, backend_values, 2u);
    items[3] = atom_expr(arena, backend_items, 2u);
    production_items[0] = atom_symbol(arena, "production-backend");
    production_items[1] = atom_symbol(arena, "PredictiveDfaV1");
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
                        bool ordinary) {
    const char *source;
    Atom *canonical = NULL;
    Atom *result = NULL;
    CettaJsonRuntimeV1Status parse_status;
    CettaJsonValueV1Status value_status;
    char error[512] = {0};
    bool reference = nargs == 2u && atom_is_symbol(args[1], "JsonReferenceV1");
    if ((nargs != 1u && !reference) || !(source = json_text(args[0]))) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary,
            "JsonParseV1", "JsonBadArgumentV1",
            "expected one JSON text argument");
    }
    size_t source_len = args[0]->kind == ATOM_GROUNDED ? atom_string_len(args[0]) : strlen(source);
    if (!reference) {
        if (cetta_json_direct_v1_parse(arena, (const uint8_t *)source, source_len,
            ordinary, json_lane_is_petta(), 0u, &result, &parse_status, error, sizeof(error)))
            return result;
        return json_failure_or_error(arena, head, args, nargs, ordinary,
            "JsonParseV1", json_runtime_failure_symbol(parse_status), error);
    }
    if (!json_reference_prepare(runtime, error, sizeof(error)))
        return json_failure_or_error(arena, head, args, nargs, ordinary,
            "JsonParseV1", "JsonPreparationFailureV1", error);
    if (!cetta_json_nik_v1_parse_prepared(
            runtime->parser_host, arena,
            (const uint8_t *)source,
            source_len, NULL,
            &canonical, &parse_status, error, sizeof(error))) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary,
            "JsonParseV1", json_runtime_failure_symbol(parse_status),
            error[0] ? error : cetta_json_runtime_v1_status_name(parse_status));
    }
    if (!ordinary) return json_lane_value(arena, canonical);
    if (!cetta_json_value_v1_to_ordinary(
            arena, canonical, 0u, 0u, &result,
            &value_status, error, sizeof(error))) {
        return json_failure(
            arena, head, args, nargs, "JsonParseV1",
            json_value_failure_symbol(value_status),
            error[0] ? error : cetta_json_value_v1_status_name(value_status));
    }
    return json_lane_value(arena, result);
}

static Atom *json_project(Arena *arena, Atom *head,
                          Atom **args, uint32_t nargs) {
    const char *source = NULL;
    Atom *result = NULL;
    bool found = false;
    CettaJsonRuntimeV1Status parse_status =
        CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    char error[512] = {0};
    if (nargs != 2u || !(source = json_text(args[0])) ||
        !args[1] || args[1]->kind != ATOM_EXPR) {
        return json_failure(
            arena, head, args, nargs,
            "JsonProjectV1", "JsonBadArgumentV1",
            "expected JSON text and a path of text keys or nonnegative integer indices");
    }
    size_t source_len = args[0]->kind == ATOM_GROUNDED
        ? atom_string_len(args[0]) : strlen(source);
    if (!cetta_json_direct_v1_project(
            arena, (const uint8_t *)source, source_len, args[1],
            true, json_lane_is_petta(), 0u,
            &result, &found, &parse_status, error, sizeof(error))) {
        return json_failure(
            arena, head, args, nargs, "JsonProjectV1",
            json_runtime_failure_symbol(parse_status),
            error[0] ? error
                     : cetta_json_runtime_v1_status_name(parse_status));
    }
    return found ? result : atom_symbol(arena, "JsonMissingV1");
}

static Atom *json_from_inspection(Arena *arena, Atom *head,
                                  Atom **args, uint32_t nargs) {
    Atom *result = NULL;
    CettaJsonValueV1Status status;
    char error[512] = {0};
    if (nargs != 1u) {
        return json_failure(
            arena, head, args, nargs,
            "JsonFromInspectionV1", "JsonBadArgumentV1",
            "expected one canonical JSON value");
    }
    if (!cetta_json_value_v1_to_ordinary(
            arena, json_codec_value(arena, args[0]), 0u, 0u,
            &result, &status, error, sizeof(error))) {
        return json_failure(
            arena, head, args, nargs,
            "JsonFromInspectionV1", json_value_failure_symbol(status),
            error[0] ? error : cetta_json_value_v1_status_name(status));
    }
    return json_lane_value(arena, result);
}

static Atom *json_stringify(CettaJsonLibraryRuntimeV1 *runtime,
                            Arena *arena, Atom *head,
                            Atom **args, uint32_t nargs,
                            bool ordinary) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    CettaJsonValueV1Status status;
    bool reference = nargs == 2u && atom_is_symbol(args[1], "JsonReferenceV1");
    bool pretty = nargs == 2u && atom_is_symbol(args[1], "JsonPrettyV1");
    const CettaJsonRuntimeV1 *parser = NULL;
    Atom *result;
    char error[512] = {0};
    if (nargs != 1u && !reference && !pretty) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary,
            "JsonStringifyV1", "JsonBadArgumentV1",
            "expected one JSON value argument");
    }
    if (reference && !json_reference_prepare(runtime, error, sizeof(error))) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary,
            "JsonStringifyV1", "JsonInternalFailureV1",
            "JSON NIK has no selected prepared parser realization");
    }
    if (reference) parser = cetta_json_nik_v1_borrow_selected_runtime(runtime->parser_host);
    Atom *input = json_codec_value(arena, args[0]);
    bool ok = pretty ? cetta_json_value_v1_emit_pretty(input, ordinary, 0u, 0u, 0u,
            &bytes, &len, &status, error, sizeof(error)) :
        reference ? cetta_json_value_v1_stringify(
            parser, input, ordinary,
            4000000u, 1024u, 16u * 1024u * 1024u,
            &bytes, &len, &status, error, sizeof(error)) :
        cetta_json_value_v1_emit(input, ordinary, 0u, 0u, 0u,
            &bytes, &len, &status, error, sizeof(error));
    if (!ok) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary,
            "JsonStringifyV1", json_value_failure_symbol(status),
            error[0] ? error : cetta_json_value_v1_status_name(status));
    }
    result = atom_string_n(arena, (const char *)bytes, len);
    free(bytes);
    return result;
}

static Atom *json_canonical_key(Arena *arena, Atom *key,
                                char *error, size_t error_size) {
    const char *text = json_text(key);
    Atom *canonical = NULL;
    CettaJsonValueV1Status status;
    if (!text) {
        (void)snprintf(error, error_size, "expected a JSON object key");
        return NULL;
    }
    if (!cetta_json_value_v1_from_ordinary(
            arena, key, 0u, 0u, &canonical,
            &status, error, error_size)) {
        return NULL;
    }
    return canonical;
}

typedef enum {
    JSON_NATIVE_LOOKUP_MATCH,
    JSON_NATIVE_LOOKUP_END,
    JSON_NATIVE_LOOKUP_MALFORMED,
} JsonNativeLookupStep;

typedef struct {
    Atom *const *pairs;
    CettaExprLen count;
    const char *wanted;
    size_t wanted_len;
    CettaExprIndex next;
} JsonNativeLookupCursor;

static bool json_native_lookup_cursor_init(
    JsonNativeLookupCursor *cursor, Arena *arena, Atom *object, Atom *key) {
    const char *wanted = json_text(key);
    Atom *const *pairs;
    CettaExprLen count;
    if (!cursor || !json_expr_is(object, "JsonObject", 2u) || !wanted) {
        return false;
    }
    Atom *members = object->expr.elems[1];
    if (json_lane_is_petta()) members = petta_semantics_closed_list(arena, members);
    if (!atom_sequence_view(members, &pairs, &count)) return false;
    *cursor = (JsonNativeLookupCursor){
        .pairs = pairs,
        .count = count,
        .wanted = wanted,
        .wanted_len = key->kind == ATOM_GROUNDED
            ? atom_string_len(key) : strlen(wanted),
    };
    return true;
}

static JsonNativeLookupStep json_native_lookup_cursor_next(
    JsonNativeLookupCursor *cursor, Atom **value_out,
    CettaExprIndex *ordinal_out) {
    if (value_out)
        *value_out = NULL;
    if (!cursor || (cursor->count && !cursor->pairs))
        return JSON_NATIVE_LOOKUP_MALFORMED;
    while (cursor->next < cursor->count) {
        CettaExprIndex ordinal = cursor->next++;
        Atom *pair = cursor->pairs[ordinal];
        if (!json_expr_is(pair, "JsonPair", 3u) ||
            !json_text(pair->expr.elems[1])) {
            return JSON_NATIVE_LOOKUP_MALFORMED;
        }
        Atom *name = pair->expr.elems[1];
        const char *bytes = json_text(name);
        size_t length = name->kind == ATOM_GROUNDED
            ? atom_string_len(name) : strlen(bytes);
        if (length != cursor->wanted_len ||
            memcmp(bytes, cursor->wanted, length) != 0) {
            continue;
        }
        if (value_out)
            *value_out = pair->expr.elems[2];
        if (ordinal_out)
            *ordinal_out = ordinal;
        return JSON_NATIVE_LOOKUP_MATCH;
    }
    return JSON_NATIVE_LOOKUP_END;
}

/* Ordinary parsed scalars already have the active lane's representation.
 * A hand-authored scalar may use a symbol where the public value uses native
 * text, or the other lane's Boolean spelling; normalize only that one leaf.
 * Containers remain ordinary data and are never recursively revalidated by
 * lookup. */
static Atom *json_native_projected_value(
    Arena *arena, Atom *value, bool may_borrow) {
    if (!arena || !value)
        return NULL;
    return may_borrow ? value : atom_deep_copy(arena, value);
}

static Atom *json_lookup(CettaJsonLibraryRuntimeV1 *runtime,
                         Arena *arena, Atom *head,
                         Atom **args, uint32_t nargs,
                         int mode, bool ordinary_codec) {
    Atom *object = nargs > 0u ? args[0] : NULL;
    Atom *canonical = object;
    Atom *key;
    Atom *members;
    Atom **matches = NULL;
    uint32_t match_len = 0u;
    CettaExprIndex index;
    char error[512] = {0};
    (void)runtime;
    bool native_text = ordinary_codec || (nargs == 3u && atom_is_symbol(args[2], "JsonOrdinaryV1"));
    if (nargs != 2u && !(nargs == 3u && native_text)) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary_codec,
            "JsonLookupV1", "JsonBadArgumentV1",
            "expected JSON object and key");
    }
    if (native_text) {
        /* Parsed values have already crossed the validation boundary. Read
         * member structure and keys, never rescan unrelated string payloads. */
        JsonNativeLookupCursor cursor;
        if (!json_native_lookup_cursor_init(
                &cursor, arena, object, args[1])) {
            return json_failure(arena, head, args, nargs, "JsonLookupV1",
                                "JsonMalformedValueV1", "expected JSON object and text key");
        }
        Atom *selected = NULL;
        CettaExprIndex selected_at = 0u;
        Atom **values = NULL;
        if (mode == 0 && !ordinary_codec) {
            values = malloc(sizeof(*values) *
                (cursor.count
                    ? (size_t)cursor.count : 1u));
            if (!values) return json_failure(arena, head, args, nargs, "JsonLookupV1",
                "JsonAllocationFailureV1", "out of memory collecting JSON values");
        }
        uint32_t count = 0u;
        for (;;) {
            Atom *borrowed = NULL;
            CettaExprIndex ordinal = 0u;
            JsonNativeLookupStep step = json_native_lookup_cursor_next(
                &cursor, &borrowed, &ordinal);
            if (step == JSON_NATIVE_LOOKUP_END)
                break;
            if (step == JSON_NATIVE_LOOKUP_MALFORMED) {
                free(values);
                return json_failure(arena, head, args, nargs, "JsonLookupV1",
                    "JsonMalformedValueV1", "expected JsonPair with text key");
            }
            /* Lookup is a projection over an already parsed value (or
             * ordinary hand-authored data), not another validation and
             * conversion boundary.  Preserve the selected subtree as it
             * stands; stringify remains responsible for rejecting a
             * malformed value before bytes leave the process. */
            selected = json_native_projected_value(
                arena, borrowed, false);
            if (!selected) {
                free(values);
                return json_failure(
                    arena, head, args, nargs, "JsonLookupV1",
                    "JsonAllocationFailureV1",
                    "out of memory projecting a JSON value");
            }
            selected_at = ordinal;
            if (values) values[count++] = selected;
            if (mode < 0 || ordinary_codec) break;
        }
        if (ordinary_codec) return selected ? selected : atom_symbol(arena, "JsonNull");
        if (mode == 0) {
            Atom *items[] = {atom_symbol(arena, "JsonMatchesV1"), atom_expr(arena, values, count)};
            free(values);
            return atom_expr(arena, items, 2u);
        }
        if (!selected) return atom_symbol(arena, "JsonMissingV1");
        Atom *items[] = {atom_symbol(arena, "JsonFoundV1"), atom_int(arena, (int64_t)selected_at), selected};
        return atom_expr(arena, items, 3u);
    }
    if (!json_expr_is(canonical, "JsonObjectV1", 2u) ||
        !(members = canonical->expr.elems[1]) ||
        members->kind != ATOM_EXPR ||
        !(key = json_canonical_key(arena, args[1], error, sizeof(error)))) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary_codec,
            "JsonLookupV1", "JsonMalformedValueV1",
            error[0] ? error : "expected a canonical JSON object");
    }
    matches = (Atom **)calloc(members->expr.len ? members->expr.len : 1u,
                              sizeof(*matches));
    if (!matches) {
        return json_failure_or_error(
            arena, head, args, nargs, ordinary_codec,
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

bool cetta_json_library_borrowed_value_v1(
    CettaJsonLibraryRuntimeV1 *runtime, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, Atom **value_out) {
    (void)runtime;
    if (value_out)
        *value_out = NULL;
    if (!arena || !head || head->kind != ATOM_SYMBOL || !args ||
        nargs != 2u || !value_out ||
        head->sym_id != g_builtin_syms.lib_json_object_get) {
        return false;
    }
    JsonNativeLookupCursor cursor;
    if (!json_native_lookup_cursor_init(&cursor, arena, args[0], args[1]))
        return false;
    Atom *selected = NULL;
    JsonNativeLookupStep step = json_native_lookup_cursor_next(
        &cursor, &selected, NULL);
    if (step == JSON_NATIVE_LOOKUP_MALFORMED)
        return false;
    if (step == JSON_NATIVE_LOOKUP_END)
        selected = atom_symbol(arena, "JsonNull");
    else
        selected = json_native_projected_value(
            arena, selected, true);
    if (!selected)
        return false;
    *value_out = selected;
    return true;
}

static Atom *json_members(Arena *arena, Atom *head,
                          Atom **args, uint32_t nargs) {
    Atom *members;
    Atom *items[2];
    bool native_text = nargs == 2u && atom_is_symbol(args[1], "JsonOrdinaryV1");
    if ((nargs != 1u && !native_text) || !json_expr_is(args[0], native_text ? "JsonObject" : "JsonObjectV1", 2u) ||
        !(members = args[0]->expr.elems[1]) || members->kind != ATOM_EXPR) {
        return json_failure(
            arena, head, args, nargs,
            "JsonMembersV1", "JsonMalformedValueV1",
            native_text ? "expected one JsonObject" : "expected one inspection JsonObjectV1");
    }
    items[0] = atom_symbol(arena, "JsonMembersV1");
    items[1] = atom_deep_copy(arena, members);
    return atom_expr(arena, items, 2u);
}

Atom *cetta_json_library_dispatch_v1(
    CettaJsonLibraryRuntimeV1 *runtime, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs) {
    SymbolId id;
    if (!runtime || !arena || !head ||
        head->kind != ATOM_SYMBOL) return NULL;
    id = head->sym_id;
    if (id == g_builtin_syms.lib_json_capabilities_v1)
        return json_capabilities(runtime, arena, head, args, nargs);
    if (id == g_builtin_syms.lib_json_parse_v1)
        return json_parse(runtime, arena, head, args, nargs, false);
    if (id == g_builtin_syms.lib_json_from_inspection_v1)
        return json_from_inspection(arena, head, args, nargs);
    if (id == g_builtin_syms.lib_json_parse)
        return json_parse(runtime, arena, head, args, nargs, true);
    if (id == g_builtin_syms.lib_json_project_v1)
        return json_project(arena, head, args, nargs);
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
